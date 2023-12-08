// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence CDNSP DRD Driver.
 *
 * Copyright (C) 2020 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 *
 */

#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/dmi.h>

#include "core.h"
#include "gadget-export.h"
#include "drd.h"
#include "cdnsp-gadget.h"
#include "cdnsp-trace.h"

unsigned int cdnsp_port_speed(unsigned int port_status)
{
	/*Detect gadget speed based on PORTSC register*/
	if (DEV_SUPERSPEEDPLUS(port_status))
		return USB_SPEED_SUPER_PLUS;
	else if (DEV_SUPERSPEED(port_status))
		return USB_SPEED_SUPER;
	else if (DEV_HIGHSPEED(port_status))
		return USB_SPEED_HIGH;
	else if (DEV_FULLSPEED(port_status))
		return USB_SPEED_FULL;

	/* If device is detached then speed will be USB_SPEED_UNKNOWN.*/
	return USB_SPEED_UNKNOWN;
}

/*
 * Given a port state, this function returns a value that would result in the
 * port being in the same state, if the value was written to the port status
 * control register.
 * Save Read Only (RO) bits and save read/write bits where
 * writing a 0 clears the bit and writing a 1 sets the bit (RWS).
 * For all other types (RW1S, RW1CS, RW, and RZ), writing a '0' has no effect.
 */
u32 cdnsp_port_state_to_neutral(u32 state)
{
	/* Save read-only status and port state. */
	return (state & CDNSP_PORT_RO) | (state & CDNSP_PORT_RWS);
}

/**
 * cdnsp_find_next_ext_cap - Find the offset of the extended capabilities
 *                           with capability ID id.
 * @base: PCI MMIO registers base address.
 * @start: Address at which to start looking, (0 or HCC_PARAMS to start at
 *         beginning of list)
 * @id: Extended capability ID to search for.
 *
 * Returns the offset of the next matching extended capability structure.
 * Some capabilities can occur several times,
 * e.g., the EXT_CAPS_PROTOCOL, and this provides a way to find them all.
 */
int cdnsp_find_next_ext_cap(void __iomem *base, u32 start, int id)
{
	u32 offset = start;
	u32 next;
	u32 val;

	if (!start || start == HCC_PARAMS_OFFSET) {
		val = readl(base + HCC_PARAMS_OFFSET);
		if (val == ~0)
			return 0;

		offset = HCC_EXT_CAPS(val) << 2;
		if (!offset)
			return 0;
	}

	do {
		val = readl(base + offset);
		if (val == ~0)
			return 0;

		if (EXT_CAPS_ID(val) == id && offset != start)
			return offset;

		next = EXT_CAPS_NEXT(val);
		offset += next << 2;
	} while (next);

	return 0;
}

void cdnsp_set_link_state(struct cdnsp_device *pdev,
			  __le32 __iomem *port_regs,
			  u32 link_state)
{
	int port_num = 0xFF;
	u32 temp;

	temp = readl(port_regs);
	temp = cdnsp_port_state_to_neutral(temp);
	temp |= PORT_WKCONN_E | PORT_WKDISC_E;
	writel(temp, port_regs);

	temp &= ~PORT_PLS_MASK;
	temp |= PORT_LINK_STROBE | link_state;

	if (pdev->active_port)
		port_num = pdev->active_port->port_num;

	trace_cdnsp_handle_port_status(port_num, readl(port_regs));
	writel(temp, port_regs);
	trace_cdnsp_link_state_changed(port_num, readl(port_regs));
}

static void cdnsp_disable_port(struct cdnsp_device *pdev,
			       __le32 __iomem *port_regs)
{
	u32 temp = cdnsp_port_state_to_neutral(readl(port_regs));

	writel(temp | PORT_PED, port_regs);
}

static void cdnsp_clear_port_change_bit(struct cdnsp_device *pdev,
					__le32 __iomem *port_regs)
{
	u32 portsc = readl(port_regs);

	writel(cdnsp_port_state_to_neutral(portsc) |
	       (portsc & PORT_CHANGE_BITS), port_regs);
}

static void cdnsp_set_chicken_bits_2(struct cdnsp_device *pdev, u32 bit)
{
	__le32 __iomem *reg;
	void __iomem *base;
	u32 offset = 0;

	base = &pdev->cap_regs->hc_capbase;
	offset = cdnsp_find_next_ext_cap(base, offset, D_XEC_PRE_REGS_CAP);
	reg = base + offset + REG_CHICKEN_BITS_2_OFFSET;

	bit = readl(reg) | bit;
	writel(bit, reg);
}

static void cdnsp_clear_chicken_bits_2(struct cdnsp_device *pdev, u32 bit)
{
	__le32 __iomem *reg;
	void __iomem *base;
	u32 offset = 0;

	base = &pdev->cap_regs->hc_capbase;
	offset = cdnsp_find_next_ext_cap(base, offset, D_XEC_PRE_REGS_CAP);
	reg = base + offset + REG_CHICKEN_BITS_2_OFFSET;

	bit = readl(reg) & ~bit;
	writel(bit, reg);
}

/*
 * Disable interrupts and begin the controller halting process.
 */
static void cdnsp_quiesce(struct cdnsp_device *pdev)
{
	u32 halted;
	u32 mask;
	u32 cmd;

	mask = ~(u32)(CDNSP_IRQS);

	halted = readl(&pdev->op_regs->status) & STS_HALT;
	if (!halted)
		mask &= ~(CMD_R_S | CMD_DEVEN);

	cmd = readl(&pdev->op_regs->command);
	cmd &= mask;
	writel(cmd, &pdev->op_regs->command);
}

/*
 * Force controller into halt state.
 *
 * Disable any IRQs and clear the run/stop bit.
 * Controller will complete any current and actively pipelined transactions, and
 * should halt within 16 ms of the run/stop bit being cleared.
 * Read controller Halted bit in the status register to see when the
 * controller is finished.
 */
int cdnsp_halt(struct cdnsp_device *pdev)
{
	int ret;
	u32 val;

	cdnsp_quiesce(pdev);

	ret = readl_poll_timeout_atomic(&pdev->op_regs->status, val,
					val & STS_HALT, 1,
					CDNSP_MAX_HALT_USEC);
	if (ret) {
		dev_err(pdev->dev, "ERROR: Device halt failed\n");
		return ret;
	}

	pdev->cdnsp_state |= CDNSP_STATE_HALTED;

	return 0;
}

/*
 * device controller died, register read returns 0xffffffff, or command never
 * ends.
 */
void cdnsp_died(struct cdnsp_device *pdev)
{
	dev_err(pdev->dev, "ERROR: CDNSP controller not responding\n");
	pdev->cdnsp_state |= CDNSP_STATE_DYING;
	cdnsp_halt(pdev);
}

/*
 * Set the run bit and wait for the device to be running.
 */
static int cdnsp_start(struct cdnsp_device *pdev)
{
	u32 temp;
	int ret;

	temp = readl(&pdev->op_regs->command);
	temp |= (CMD_R_S | CMD_DEVEN);
	writel(temp, &pdev->op_regs->command);

	pdev->cdnsp_state = 0;

	/*
	 * Wait for the STS_HALT Status bit to be 0 to indicate the device is
	 * running.
	 */
	ret = readl_poll_timeout_atomic(&pdev->op_regs->status, temp,
					!(temp & STS_HALT), 1,
					CDNSP_MAX_HALT_USEC);
	if (ret) {
		pdev->cdnsp_state = CDNSP_STATE_DYING;
		dev_err(pdev->dev, "ERROR: Controller run failed\n");
	}

	return ret;
}

/*
 * Reset a halted controller.
 *
 * This resets pipelines, timers, counters, state machines, etc.
 * Transactions will be terminated immediately, and operational registers
 * will be set to their defaults.
 */
int cdnsp_reset(struct cdnsp_device *pdev)
{
	u32 command;
	u32 temp;
	int ret;

	temp = readl(&pdev->op_regs->status);

	if (temp == ~(u32)0) {
		dev_err(pdev->dev, "Device not accessible, reset failed.\n");
		return -ENODEV;
	}

	if ((temp & STS_HALT) == 0) {
		dev_err(pdev->dev, "Controller not halted, aborting reset.\n");
		return -EINVAL;
	}

	command = readl(&pdev->op_regs->command);
	command |= CMD_RESET;
	writel(command, &pdev->op_regs->command);

	ret = readl_poll_timeout_atomic(&pdev->op_regs->command, temp,
					!(temp & CMD_RESET), 1,
					10 * 1000);
	if (ret) {
		dev_err(pdev->dev, "ERROR: Controller reset failed\n");
		return ret;
	}

	/*
	 * CDNSP cannot write any doorbells or operational registers other
	 * than status until the "Controller Not Ready" flag is cleared.
	 */
	ret = readl_poll_timeout_atomic(&pdev->op_regs->status, temp,
					!(temp & STS_CNR), 1,
					10 * 1000);

	if (ret) {
		dev_err(pdev->dev, "ERROR: Controller not ready to work\n");
		return ret;
	}

	dev_dbg(pdev->dev, "Controller ready to work");

	return ret;
}

/*
 * cdnsp_get_endpoint_index - Find the index for an endpoint given its
 * descriptor.Use the return value to right shift 1 for the bitmask.
 *
 * Index = (epnum * 2) + direction - 1,
 * where direction = 0 for OUT, 1 for IN.
 * For control endpoints, the IN index is used (OUT index is unused), so
 * index = (epnum * 2) + direction - 1 = (epnum * 2) + 1 - 1 = (epnum * 2)
 */
static unsigned int
	cdnsp_get_endpoint_index(const struct usb_endpoint_descriptor *desc)
{
	unsigned int index = (unsigned int)usb_endpoint_num(desc);

	if (usb_endpoint_xfer_control(desc))
		return index * 2;

	return (index * 2) + (usb_endpoint_dir_in(desc) ? 1 : 0) - 1;
}

/*
 * Find the flag for this endpoint (for use in the control context). Use the
 * endpoint index to create a bitmask. The slot context is bit 0, endpoint 0 is
 * bit 1, etc.
 */
static unsigned int
	cdnsp_get_endpoint_flag(const struct usb_endpoint_descriptor *desc)
{
	return 1 << (cdnsp_get_endpoint_index(desc) + 1);
}

int cdnsp_ep_enqueue(struct cdnsp_ep *pep, struct cdnsp_request *preq)
{
	struct cdnsp_device *pdev = pep->pdev;
	struct usb_request *request;
	int ret;

	if (preq->epnum == 0 && !list_empty(&pep->pending_list)) {
		trace_cdnsp_request_enqueue_busy(preq);
		return -EBUSY;
	}

	request = &preq->request;
	request->actual = 0;
	request->status = -EINPROGRESS;
	preq->direction = pep->direction;
	preq->epnum = pep->number;
	preq->td.drbl = 0;

	ret = usb_gadget_map_request_by_dev(pdev->dev, request, pep->direction);
	if (ret) {
		trace_cdnsp_request_enqueue_error(preq);
		return ret;
	}

	list_add_tail(&preq->list, &pep->pending_list);

	trace_cdnsp_request_enqueue(preq);

	switch (usb_endpoint_type(pep->endpoint.desc)) {
	case USB_ENDPOINT_XFER_CONTROL:
		ret = cdnsp_queue_ctrl_tx(pdev, preq);
		break;
	case USB_ENDPOINT_XFER_BULK:
	case USB_ENDPOINT_XFER_INT:
		ret = cdnsp_queue_bulk_tx(pdev, preq);
		break;
	case USB_ENDPOINT_XFER_ISOC:
		ret = cdnsp_queue_isoc_tx_prepare(pdev, preq);
	}

	if (ret)
		goto unmap;

	return 0;

unmap:
	usb_gadget_unmap_request_by_dev(pdev->dev, &preq->request,
					pep->direction);
	list_del(&preq->list);
	trace_cdnsp_request_enqueue_error(preq);

	return ret;
}

/*
 * Remove the request's TD from the endpoint ring. This may cause the
 * controller to stop USB transfers, potentially stopping in the middle of a
 * TRB buffer. The controller should pick up where it left off in the TD,
 * unless a Set Transfer Ring Dequeue Pointer is issued.
 *
 * The TRBs that make up the buffers for the canceled request will be "removed"
 * from the ring. Since the ring is a contiguous structure, they can't be
 * physically removed. Instead, there are two options:
 *
 *  1) If the controller is in the middle of processing the request to be
 *     canceled, we simply move the ring's dequeue pointer past those TRBs
 *     using the Set Transfer Ring Dequeue Pointer command. This will be
 *     the common case, when drivers timeout on the last submitted request
 *     and attempt to cancel.
 *
 *  2) If the controller is in the middle of a different TD, we turn the TRBs
 *     into a series of 1-TRB transfer no-op TDs. No-ops shouldn't be chained.
 *     The controller will need to invalidate the any TRBs it has cached after
 *     the stop endpoint command.
 *
 *  3) The TD may have completed by the time the Stop Endpoint Command
 *     completes, so software needs to handle that case too.
 *
 */
int cdnsp_ep_dequeue(struct cdnsp_ep *pep, struct cdnsp_request *preq)
{
	struct cdnsp_device *pdev = pep->pdev;
	int ret_stop = 0;
	int ret_rem;

	trace_cdnsp_request_dequeue(preq);

	if (GET_EP_CTX_STATE(pep->out_ctx) == EP_STATE_RUNNING)
		ret_stop = cdnsp_cmd_stop_ep(pdev, pep);

	ret_rem = cdnsp_remove_request(pdev, preq, pep);

	return ret_rem ? ret_rem : ret_stop;
}

static void cdnsp_zero_in_ctx(struct cdnsp_device *pdev)
{
	struct cdnsp_input_control_ctx *ctrl_ctx;
	struct cdnsp_slot_ctx *slot_ctx;
	struct cdnsp_ep_ctx *ep_ctx;
	int i;

	ctrl_ctx = cdnsp_get_input_control_ctx(&pdev->in_ctx);

	/*
	 * When a device's add flag and drop flag are zero, any subsequent
	 * configure endpoint command will leave that endpoint's state
	 * untouched. Make sure we don't leave any old state in the input
	 * endpoint contexts.
	 */
	ctrl_ctx->drop_flags = 0;
	ctrl_ctx->add_flags = 0;
	slot_ctx = cdnsp_get_slot_ctx(&pdev->in_ctx);
	slot_ctx->dev_info &= cpu_to_le32(~LAST_CTX_MASK);

	/* Endpoint 0 is always valid */
	slot_ctx->dev_info |= cpu_to_le32(LAST_CTX(1));
	for (i = 1; i < CDNSP_ENDPOINTS_NUM; ++i) {
		ep_ctx = cdnsp_get_ep_ctx(&pdev->in_ctx, i);
		ep_ctx->ep_info = 0;
		ep_ctx->ep_info2 = 0;
		ep_ctx->deq = 0;
		ep_ctx->tx_info = 0;
	}
}

/* Issue a configure endpoint command and wait for it to finish. */
static int cdnsp_configure_endpoint(struct cdnsp_device *pdev)
{
	int ret;

	cdnsp_queue_configure_endpoint(pdev, pdev->cmd.in_ctx->dma);
	cdnsp_ring_cmd_db(pdev);
	ret = cdnsp_wait_for_cmd_compl(pdev);
	if (ret) {
		dev_err(pdev->dev,
			"ERR: unexpected command completion code 0x%x.\n", ret);
		return -EINVAL;
	}

	return ret;
}

static void cdnsp_invalidate_ep_events(struct cdnsp_device *pdev,
				       struct cdnsp_ep *pep)
{
	struct cdnsp_segment *segment;
	union cdnsp_trb *event;
	u32 cycle_state;
	u32  data;

	event = pdev->event_ring->dequeue;
	segment = pdev->event_ring->deq_seg;
	cycle_state = pdev->event_ring->cycle_state;

	while (1) {
		data = le32_to_cpu(event->trans_event.flags);

		/* Check the owner of the TRB. */
		if ((data & TRB_CYCLE) != cycle_state)
			break;

		if (TRB_FIELD_TO_TYPE(data) == TRB_TRANSFER &&
		    TRB_TO_EP_ID(data) == (pep->idx + 1)) {
			data |= TRB_EVENT_INVALIDATE;
			event->trans_event.flags = cpu_to_le32(data);
		}

		if (cdnsp_last_trb_on_seg(segment, event)) {
			cycle_state ^= 1;
			segment = pdev->event_ring->deq_seg->next;
			event = segment->trbs;
		} else {
			event++;
		}
	}
}

int cdnsp_wait_for_cmd_compl(struct cdnsp_device *pdev)
{
	struct cdnsp_segment *event_deq_seg;
	union cdnsp_trb *cmd_trb;
	dma_addr_t cmd_deq_dma;
	union cdnsp_trb *event;
	u32 cycle_state;
	int ret, val;
	u64 cmd_dma;
	u32  flags;

	cmd_trb = pdev->cmd.command_trb;
	pdev->cmd.status = 0;

	trace_cdnsp_cmd_wait_for_compl(pdev->cmd_ring, &cmd_trb->generic);

	ret = readl_poll_timeout_atomic(&pdev->op_regs->cmd_ring, val,
					!CMD_RING_BUSY(val), 1,
					CDNSP_CMD_TIMEOUT);
	if (ret) {
		dev_err(pdev->dev, "ERR: Timeout while waiting for command\n");
		trace_cdnsp_cmd_timeout(pdev->cmd_ring, &cmd_trb->generic);
		pdev->cdnsp_state = CDNSP_STATE_DYING;
		return -ETIMEDOUT;
	}

	event = pdev->event_ring->dequeue;
	event_deq_seg = pdev->event_ring->deq_seg;
	cycle_state = pdev->event_ring->cycle_state;

	cmd_deq_dma = cdnsp_trb_virt_to_dma(pdev->cmd_ring->deq_seg, cmd_trb);
	if (!cmd_deq_dma)
		return -EINVAL;

	while (1) {
		flags = le32_to_cpu(event->event_cmd.flags);

		/* Check the owner of the TRB. */
		if ((flags & TRB_CYCLE) != cycle_state)
			return -EINVAL;

		cmd_dma = le64_to_cpu(event->event_cmd.cmd_trb);

		/*
		 * Check whether the completion event is for last queued
		 * command.
		 */
		if (TRB_FIELD_TO_TYPE(flags) != TRB_COMPLETION ||
		    cmd_dma != (u64)cmd_deq_dma) {
			if (!cdnsp_last_trb_on_seg(event_deq_seg, event)) {
				event++;
				continue;
			}

			if (cdnsp_last_trb_on_ring(pdev->event_ring,
						   event_deq_seg, event))
				cycle_state ^= 1;

			event_deq_seg = event_deq_seg->next;
			event = event_deq_seg->trbs;
			continue;
		}

		trace_cdnsp_handle_command(pdev->cmd_ring, &cmd_trb->generic);

		pdev->cmd.status = GET_COMP_CODE(le32_to_cpu(event->event_cmd.status));
		if (pdev->cmd.status == COMP_SUCCESS)
			return 0;

		return -pdev->cmd.status;
	}
}

int cdnsp_halt_endpoint(struct cdnsp_device *pdev,
			struct cdnsp_ep *pep,
			int value)
{
	int ret;

	trace_cdnsp_ep_halt(value ? "Set" : "Clear");

	ret = cdnsp_cmd_stop_ep(pdev, pep);
	if (ret)
		return ret;

	if (value) {
		if (GET_EP_CTX_STATE(pep->out_ctx) == EP_STATE_STOPPED) {
			cdnsp_queue_halt_endpoint(pdev, pep->idx);
			cdnsp_ring_cmd_db(pdev);
			ret = cdnsp_wait_for_cmd_compl(pdev);
		}

		pep->ep_state |= EP_HALTED;
	} else {
		cdnsp_queue_reset_ep(pdev, pep->idx);
		cdnsp_ring_cmd_db(pdev);
		ret = cdnsp_wait_for_cmd_compl(pdev);
		trace_cdnsp_handle_cmd_reset_ep(pep->out_ctx);

		if (ret)
			return ret;

		pep->ep_state &= ~EP_HALTED;

		if (pep->idx != 0 && !(pep->ep_state & EP_WEDGE))
			cdnsp_ring_doorbell_for_active_rings(pdev, pep);

		pep->ep_state &= ~EP_WEDGE;
	}

	return 0;
}

static int cdnsp_update_eps_configuration(struct cdnsp_device *pdev,
					  struct cdnsp_ep *pep)
{
	struct cdnsp_input_control_ctx *ctrl_ctx;
	struct cdnsp_slot_ctx *slot_ctx;
	int ret = 0;
	u32 ep_sts;
	int i;

	ctrl_ctx = cdnsp_get_input_control_ctx(&pdev->in_ctx);

	/* Don't issue the command if there's no endpoints to update. */
	if (ctrl_ctx->add_flags == 0 && ctrl_ctx->drop_flags == 0)
		return 0;

	ctrl_ctx->add_flags |= cpu_to_le32(SLOT_FLAG);
	ctrl_ctx->add_flags &= cpu_to_le32(~EP0_FLAG);
	ctrl_ctx->drop_flags &= cpu_to_le32(~(SLOT_FLAG | EP0_FLAG));

	/* Fix up Context Entries field. Minimum value is EP0 == BIT(1). */
	slot_ctx = cdnsp_get_slot_ctx(&pdev->in_ctx);
	for (i = CDNSP_ENDPOINTS_NUM; i >= 1; i--) {
		__le32 le32 = cpu_to_le32(BIT(i));

		if ((pdev->eps[i - 1].ring && !(ctrl_ctx->drop_flags & le32)) ||
		    (ctrl_ctx->add_flags & le32) || i == 1) {
			slot_ctx->dev_info &= cpu_to_le32(~LAST_CTX_MASK);
			slot_ctx->dev_info |= cpu_to_le32(LAST_CTX(i));
			break;
		}
	}

	ep_sts = GET_EP_CTX_STATE(pep->out_ctx);

	if ((ctrl_ctx->add_flags != cpu_to_le32(SLOT_FLAG) &&
	     ep_sts == EP_STATE_DISABLED) ||
	    (ep_sts != EP_STATE_DISABLED && ctrl_ctx->drop_flags))
		ret = cdnsp_configure_endpoint(pdev);

	trace_cdnsp_configure_endpoint(cdnsp_get_slot_ctx(&pdev->out_ctx));
	trace_cdnsp_handle_cmd_config_ep(pep->out_ctx);

	cdnsp_zero_in_ctx(pdev);

	return ret;
}

/*
 * This submits a Reset Device Command, which will set the device state to 0,
 * set the device address to 0, and disable all the endpoints except the default
 * control endpoint. The USB core should come back and call
 * cdnsp_setup_device(), and then re-set up the configuration.
 */
int cdnsp_reset_device(struct cdnsp_device *pdev)
{
	struct cdnsp_slot_ctx *slot_ctx;
	int slot_state;
	int ret, i;

	slot_ctx = cdnsp_get_slot_ctx(&pdev->in_ctx);
	slot_ctx->dev_info = 0;
	pdev->device_address = 0;

	/* If device is not setup, there is no point in resetting it. */
	slot_ctx = cdnsp_get_slot_ctx(&pdev->out_ctx);
	slot_state = GET_SLOT_STATE(le32_to_cpu(slot_ctx->dev_state));
	trace_cdnsp_reset_device(slot_ctx);

	if (slot_state <= SLOT_STATE_DEFAULT &&
	    pdev->eps[0].ep_state & EP_HALTED) {
		cdnsp_halt_endpoint(pdev, &pdev->eps[0], 0);
	}

	/*
	 * During Reset Device command controller shall transition the
	 * endpoint ep0 to the Running State.
	 */
	pdev->eps[0].ep_state &= ~(EP_STOPPED | EP_HALTED);
	pdev->eps[0].ep_state |= EP_ENABLED;

	if (slot_state <= SLOT_STATE_DEFAULT)
		return 0;

	cdnsp_queue_reset_device(pdev);
	cdnsp_ring_cmd_db(pdev);
	ret = cdnsp_wait_for_cmd_compl(pdev);

	/*
	 * After Reset Device command all not default endpoints
	 * are in Disabled state.
	 */
	for (i = 1; i < CDNSP_ENDPOINTS_NUM; ++i)
		pdev->eps[i].ep_state |= EP_STOPPED | EP_UNCONFIGURED;

	trace_cdnsp_handle_cmd_reset_dev(slot_ctx);

	if (ret)
		dev_err(pdev->dev, "Reset device failed with error code %d",
			ret);

	return ret;
}

/*
 * Sets the MaxPStreams field and the Linear Stream Array field.
 * Sets the dequeue pointer to the stream context array.
 */
static void cdnsp_setup_streams_ep_input_ctx(struct cdnsp_device *pdev,
					     struct cdnsp_ep_ctx *ep_ctx,
					     struct cdnsp_stream_info *stream_info)
{
	u32 max_primary_streams;

	/* MaxPStreams is the number of stream context array entries, not the
	 * number we're actually using. Must be in 2^(MaxPstreams + 1) format.
	 * fls(0) = 0, fls(0x1) = 1, fls(0x10) = 2, fls(0x100) = 3, etc.
	 */
	max_primary_streams = fls(stream_info->num_stream_ctxs) - 2;
	ep_ctx->ep_info &= cpu_to_le32(~EP_MAXPSTREAMS_MASK);
	ep_ctx->ep_info |= cpu_to_le32(EP_MAXPSTREAMS(max_primary_streams)
				       | EP_HAS_LSA);
	ep_ctx->deq  = cpu_to_le64(stream_info->ctx_array_dma);
}

/*
 * The drivers use this function to prepare a bulk endpoints to use streams.
 *
 * Don't allow the call to succeed if endpoint only supports one stream
 * (which means it doesn't support streams at all).
 */
int cdnsp_alloc_streams(struct cdnsp_device *pdev, struct cdnsp_ep *pep)
{
	unsigned int num_streams = usb_ss_max_streams(pep->endpoint.comp_desc);
	unsigned int num_stream_ctxs;
	int ret;

	if (num_streams ==  0)
		return 0;

	if (num_streams > STREAM_NUM_STREAMS)
		return -EINVAL;

	/*
	 * Add two to the number of streams requested to account for
	 * stream 0 that is reserved for controller usage and one additional
	 * for TASK SET FULL response.
	 */
	num_streams += 2;

	/* The stream context array size must be a power of two */
	num_stream_ctxs = roundup_pow_of_two(num_streams);

	trace_cdnsp_stream_number(pep, num_stream_ctxs, num_streams);

	ret = cdnsp_alloc_stream_info(pdev, pep, num_stream_ctxs, num_streams);
	if (ret)
		return ret;

	cdnsp_setup_streams_ep_input_ctx(pdev, pep->in_ctx, &pep->stream_info);

	pep->ep_state |= EP_HAS_STREAMS;
	pep->stream_info.td_count = 0;
	pep->stream_info.first_prime_det = 0;

	/* Subtract 1 for stream 0, which drivers can't use. */
	return num_streams - 1;
}

int cdnsp_disable_slot(struct cdnsp_device *pdev)
{
	int ret;

	cdnsp_queue_slot_control(pdev, TRB_DISABLE_SLOT);
	cdnsp_ring_cmd_db(pdev);
	ret = cdnsp_wait_for_cmd_compl(pdev);

	pdev->slot_id = 0;
	pdev->active_port = NULL;

	trace_cdnsp_handle_cmd_disable_slot(cdnsp_get_slot_ctx(&pdev->out_ctx));

	memset(pdev->in_ctx.bytes, 0, CDNSP_CTX_SIZE);
	memset(pdev->out_ctx.bytes, 0, CDNSP_CTX_SIZE);

	return ret;
}

int cdnsp_enable_slot(struct cdnsp_device *pdev)
{
	struct cdnsp_slot_ctx *slot_ctx;
	int slot_state;
	int ret;

	/* If device is not setup, there is no point in resetting it */
	slot_ctx = cdnsp_get_slot_ctx(&pdev->out_ctx);
	slot_state = GET_SLOT_STATE(le32_to_cpu(slot_ctx->dev_state));

	if (slot_state != SLOT_STATE_DISABLED)
		return 0;

	cdnsp_queue_slot_control(pdev, TRB_ENABLE_SLOT);
	cdnsp_ring_cmd_db(pdev);
	ret = cdnsp_wait_for_cmd_compl(pdev);
	if (ret)
		goto show_trace;

	pdev->slot_id = 1;

show_trace:
	trace_cdnsp_handle_cmd_enable_slot(cdnsp_get_slot_ctx(&pdev->out_ctx));

	return ret;
}

/*
 * Issue an Address Device command with BSR=0 if setup is SETUP_CONTEXT_ONLY
 * or with BSR = 1 if set_address is SETUP_CONTEXT_ADDRESS.
 */
int cdnsp_setup_device(struct cdnsp_device *pdev, enum cdnsp_setup_dev setup)
{
	struct cdnsp_input_control_ctx *ctrl_ctx;
	struct cdnsp_slot_ctx *slot_ctx;
	int dev_state = 0;
	int ret;

	if (!pdev->slot_id) {
		trace_cdnsp_slot_id("incorrect");
		return -EINVAL;
	}

	if (!pdev->active_port->port_num)
		return -EINVAL;

	slot_ctx = cdnsp_get_slot_ctx(&pdev->out_ctx);
	dev_state = GET_SLOT_STATE(le32_to_cpu(slot_ctx->dev_state));

	if (setup == SETUP_CONTEXT_ONLY && dev_state == SLOT_STATE_DEFAULT) {
		trace_cdnsp_slot_already_in_default(slot_ctx);
		return 0;
	}

	slot_ctx = cdnsp_get_slot_ctx(&pdev->in_ctx);
	ctrl_ctx = cdnsp_get_input_control_ctx(&pdev->in_ctx);

	if (!slot_ctx->dev_info || dev_state == SLOT_STATE_DEFAULT) {
		ret = cdnsp_setup_addressable_priv_dev(pdev);
		if (ret)
			return ret;
	}

	cdnsp_copy_ep0_dequeue_into_input_ctx(pdev);

	ctrl_ctx->add_flags = cpu_to_le32(SLOT_FLAG | EP0_FLAG);
	ctrl_ctx->drop_flags = 0;

	trace_cdnsp_setup_device_slot(slot_ctx);

	cdnsp_queue_address_device(pdev, pdev->in_ctx.dma, setup);
	cdnsp_ring_cmd_db(pdev);
	ret = cdnsp_wait_for_cmd_compl(pdev);

	trace_cdnsp_handle_cmd_addr_dev(cdnsp_get_slot_ctx(&pdev->out_ctx));

	/* Zero the input context control for later use. */
	ctrl_ctx->add_flags = 0;
	ctrl_ctx->drop_flags = 0;

	return ret;
}

void cdnsp_set_usb2_hardware_lpm(struct cdnsp_device *pdev,
				 struct usb_request *req,
				 int enable)
{
	if (pdev->active_port != &pdev->usb2_port || !pdev->gadget.lpm_capable)
		return;

	trace_cdnsp_lpm(enable);

	if (enable)
		writel(PORT_BESL(CDNSP_DEFAULT_BESL) | PORT_L1S_NYET | PORT_HLE,
		       &pdev->active_port->regs->portpmsc);
	else
		writel(PORT_L1S_NYET, &pdev->active_port->regs->portpmsc);
}

static int cdnsp_get_frame(struct cdnsp_device *pdev)
{
	return readl(&pdev->run_regs->microframe_index) >> 3;
}

static int cdnsp_gadget_ep_enable(struct usb_ep *ep,
				  const struct usb_endpoint_descriptor *desc)
{
	struct cdnsp_input_control_ctx *ctrl_ctx;
	struct cdnsp_device *pdev;
	struct cdnsp_ep *pep;
	unsigned long flags;
	u32 added_ctxs;
	int ret;

	if (!ep || !desc || desc->bDescriptorType != USB_DT_ENDPOINT ||
	    !desc->wMaxPacketSize)
		return -EINVAL;

	pep = to_cdnsp_ep(ep);
	pdev = pep->pdev;
	pep->ep_state &= ~EP_UNCONFIGURED;

	if (dev_WARN_ONCE(pdev->dev, pep->ep_state & EP_ENABLED,
			  "%s is already enabled\n", pep->name))
		return 0;

	spin_lock_irqsave(&pdev->lock, flags);

	added_ctxs = cdnsp_get_endpoint_flag(desc);
	if (added_ctxs == SLOT_FLAG || added_ctxs == EP0_FLAG) {
		dev_err(pdev->dev, "ERROR: Bad endpoint number\n");
		ret = -EINVAL;
		goto unlock;
	}

	pep->interval = desc->bInterval ? BIT(desc->bInterval - 1) : 0;

	if (pdev->gadget.speed == USB_SPEED_FULL) {
		if (usb_endpoint_type(desc) == USB_ENDPOINT_XFER_INT)
			pep->interval = desc->bInterval << 3;
		if (usb_endpoint_type(desc) == USB_ENDPOINT_XFER_ISOC)
			pep->interval = BIT(desc->bInterval - 1) << 3;
	}

	if (usb_endpoint_type(desc) == USB_ENDPOINT_XFER_ISOC) {
		if (pep->interval > BIT(12)) {
			dev_err(pdev->dev, "bInterval %d not supported\n",
				desc->bInterval);
			ret = -EINVAL;
			goto unlock;
		}
		cdnsp_set_chicken_bits_2(pdev, CHICKEN_XDMA_2_TP_CACHE_DIS);
	}

	ret = cdnsp_endpoint_init(pdev, pep, GFP_ATOMIC);
	if (ret)
		goto unlock;

	ctrl_ctx = cdnsp_get_input_control_ctx(&pdev->in_ctx);
	ctrl_ctx->add_flags = cpu_to_le32(added_ctxs);
	ctrl_ctx->drop_flags = 0;

	ret = cdnsp_update_eps_configuration(pdev, pep);
	if (ret) {
		cdnsp_free_endpoint_rings(pdev, pep);
		goto unlock;
	}

	pep->ep_state |= EP_ENABLED;
	pep->ep_state &= ~EP_STOPPED;

unlock:
	trace_cdnsp_ep_enable_end(pep, 0);
	spin_unlock_irqrestore(&pdev->lock, flags);

	return ret;
}

static int cdnsp_gadget_ep_disable(struct usb_ep *ep)
{
	struct cdnsp_input_control_ctx *ctrl_ctx;
	struct cdnsp_request *preq;
	struct cdnsp_device *pdev;
	struct cdnsp_ep *pep;
	unsigned long flags;
	u32 drop_flag;
	int ret = 0;

	if (!ep)
		return -EINVAL;

	pep = to_cdnsp_ep(ep);
	pdev = pep->pdev;

	spin_lock_irqsave(&pdev->lock, flags);

	if (!(pep->ep_state & EP_ENABLED)) {
		dev_err(pdev->dev, "%s is already disabled\n", pep->name);
		ret = -EINVAL;
		goto finish;
	}

	pep->ep_state |= EP_DIS_IN_RROGRESS;

	/* Endpoint was unconfigured by Reset Device command. */
	if (!(pep->ep_state & EP_UNCONFIGURED)) {
		cdnsp_cmd_stop_ep(pdev, pep);
		cdnsp_cmd_flush_ep(pdev, pep);
	}

	/* Remove all queued USB requests. */
	while (!list_empty(&pep->pending_list)) {
		preq = next_request(&pep->pending_list);
		cdnsp_ep_dequeue(pep, preq);
	}

	cdnsp_invalidate_ep_events(pdev, pep);

	pep->ep_state &= ~EP_DIS_IN_RROGRESS;
	drop_flag = cdnsp_get_endpoint_flag(pep->endpoint.desc);
	ctrl_ctx = cdnsp_get_input_control_ctx(&pdev->in_ctx);
	ctrl_ctx->drop_flags = cpu_to_le32(drop_flag);
	ctrl_ctx->add_flags = 0;

	cdnsp_endpoint_zero(pdev, pep);

	if (!(pep->ep_state & EP_UNCONFIGURED))
		ret = cdnsp_update_eps_configuration(pdev, pep);

	cdnsp_free_endpoint_rings(pdev, pep);

	pep->ep_state &= ~(EP_ENABLED | EP_UNCONFIGURED);
	pep->ep_state |= EP_STOPPED;

finish:
	trace_cdnsp_ep_disable_end(pep, 0);
	spin_unlock_irqrestore(&pdev->lock, flags);

	return ret;
}

static struct usb_request *cdnsp_gadget_ep_alloc_request(struct usb_ep *ep,
							 gfp_t gfp_flags)
{
	struct cdnsp_ep *pep = to_cdnsp_ep(ep);
	struct cdnsp_request *preq;

	preq = kzalloc(sizeof(*preq), gfp_flags);
	if (!preq)
		return NULL;

	preq->epnum = pep->number;
	preq->pep = pep;

	trace_cdnsp_alloc_request(preq);

	return &preq->request;
}

static void cdnsp_gadget_ep_free_request(struct usb_ep *ep,
					 struct usb_request *request)
{
	struct cdnsp_request *preq = to_cdnsp_request(request);

	trace_cdnsp_free_request(preq);
	kfree(preq);
}

static int cdnsp_gadget_ep_queue(struct usb_ep *ep,
				 struct usb_request *request,
				 gfp_t gfp_flags)
{
	struct cdnsp_request *preq;
	struct cdnsp_device *pdev;
	struct cdnsp_ep *pep;
	unsigned long flags;
	int ret;

	if (!request || !ep)
		return -EINVAL;

	pep = to_cdnsp_ep(ep);
	pdev = pep->pdev;

	if (!(pep->ep_state & EP_ENABLED)) {
		dev_err(pdev->dev, "%s: can't queue to disabled endpoint\n",
			pep->name);
		return -EINVAL;
	}

	preq = to_cdnsp_request(request);
	spin_lock_irqsave(&pdev->lock, flags);
	ret = cdnsp_ep_enqueue(pep, preq);
	spin_unlock_irqrestore(&pdev->lock, flags);

	return ret;
}

static int cdnsp_gadget_ep_dequeue(struct usb_ep *ep,
				   struct usb_request *request)
{
	struct cdnsp_ep *pep = to_cdnsp_ep(ep);
	struct cdnsp_device *pdev = pep->pdev;
	unsigned long flags;
	int ret;

	if (!pep->endpoint.desc) {
		dev_err(pdev->dev,
			"%s: can't dequeue to disabled endpoint\n",
			pep->name);
		return -ESHUTDOWN;
	}

	/* Requests has been dequeued during disabling endpoint. */
	if (!(pep->ep_state & EP_ENABLED))
		return 0;

	spin_lock_irqsave(&pdev->lock, flags);
	ret = cdnsp_ep_dequeue(pep, to_cdnsp_request(request));
	spin_unlock_irqrestore(&pdev->lock, flags);

	return ret;
}

static int cdnsp_gadget_ep_set_halt(struct usb_ep *ep, int value)
{
	struct cdnsp_ep *pep = to_cdnsp_ep(ep);
	struct cdnsp_device *pdev = pep->pdev;
	struct cdnsp_request *preq;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pdev->lock, flags);

	preq = next_request(&pep->pending_list);
	if (value) {
		if (preq) {
			trace_cdnsp_ep_busy_try_halt_again(pep, 0);
			ret = -EAGAIN;
			goto done;
		}
	}

	ret = cdnsp_halt_endpoint(pdev, pep, value);

done:
	spin_unlock_irqrestore(&pdev->lock, flags);
	return ret;
}

static int cdnsp_gadget_ep_set_wedge(struct usb_ep *ep)
{
	struct cdnsp_ep *pep = to_cdnsp_ep(ep);
	struct cdnsp_device *pdev = pep->pdev;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pdev->lock, flags);
	pep->ep_state |= EP_WEDGE;
	ret = cdnsp_halt_endpoint(pdev, pep, 1);
	spin_unlock_irqrestore(&pdev->lock, flags);

	return ret;
}

static const struct usb_ep_ops cdnsp_gadget_ep0_ops = {
	.enable		= cdnsp_gadget_ep_enable,
	.disable	= cdnsp_gadget_ep_disable,
	.alloc_request	= cdnsp_gadget_ep_alloc_request,
	.free_request	= cdnsp_gadget_ep_free_request,
	.queue		= cdnsp_gadget_ep_queue,
	.dequeue	= cdnsp_gadget_ep_dequeue,
	.set_halt	= cdnsp_gadget_ep_set_halt,
	.set_wedge	= cdnsp_gadget_ep_set_wedge,
};

static const struct usb_ep_ops cdnsp_gadget_ep_ops = {
	.enable		= cdnsp_gadget_ep_enable,
	.disable	= cdnsp_gadget_ep_disable,
	.alloc_request	= cdnsp_gadget_ep_alloc_request,
	.free_request	= cdnsp_gadget_ep_free_request,
	.queue		= cdnsp_gadget_ep_queue,
	.dequeue	= cdnsp_gadget_ep_dequeue,
	.set_halt	= cdnsp_gadget_ep_set_halt,
	.set_wedge	= cdnsp_gadget_ep_set_wedge,
};

void cdnsp_gadget_giveback(struct cdnsp_ep *pep,
			   struct cdnsp_request *preq,
			   int status)
{
	struct cdnsp_device *pdev = pep->pdev;

	list_del(&preq->list);

	if (preq->request.status == -EINPROGRESS)
		preq->request.status = status;

	usb_gadget_unmap_request_by_dev(pdev->dev, &preq->request,
					preq->direction);

	trace_cdnsp_request_giveback(preq);

	if (preq != &pdev->ep0_preq) {
		spin_unlock(&pdev->lock);
		usb_gadget_giveback_request(&pep->endpoint, &preq->request);
		spin_lock(&pdev->lock);
	}
}

static struct usb_endpoint_descriptor cdnsp_gadget_ep0_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_CONTROL,
};

static int cdnsp_run(struct cdnsp_device *pdev,
		     enum usb_device_speed speed)
{
	u32 fs_speed = 0;
	u32 temp;
	int ret;

	temp = readl(&pdev->ir_set->irq_control);
	temp &= ~IMOD_INTERVAL_MASK;
	temp |= ((IMOD_DEFAULT_INTERVAL / 250) & IMOD_INTERVAL_MASK);
	writel(temp, &pdev->ir_set->irq_control);

	temp = readl(&pdev->port3x_regs->mode_addr);

	switch (speed) {
	case USB_SPEED_SUPER_PLUS:
		temp |= CFG_3XPORT_SSP_SUPPORT;
		break;
	case USB_SPEED_SUPER:
		temp &= ~CFG_3XPORT_SSP_SUPPORT;
		break;
	case USB_SPEED_HIGH:
		break;
	case USB_SPEED_FULL:
		fs_speed = PORT_REG6_FORCE_FS;
		break;
	default:
		dev_err(pdev->dev, "invalid maximum_speed parameter %d\n",
			speed);
		fallthrough;
	case USB_SPEED_UNKNOWN:
		/* Default to superspeed. */
		speed = USB_SPEED_SUPER;
		break;
	}

	if (speed >= USB_SPEED_SUPER) {
		writel(temp, &pdev->port3x_regs->mode_addr);
		cdnsp_set_link_state(pdev, &pdev->usb3_port.regs->portsc,
				     XDEV_RXDETECT);
	} else {
		cdnsp_disable_port(pdev, &pdev->usb3_port.regs->portsc);
	}

	cdnsp_set_link_state(pdev, &pdev->usb2_port.regs->portsc,
			     XDEV_RXDETECT);

	cdnsp_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(512);

	writel(PORT_REG6_L1_L0_HW_EN | fs_speed, &pdev->port20_regs->port_reg6);

	ret = cdnsp_start(pdev);
	if (ret) {
		ret = -ENODEV;
		goto err;
	}

	temp = readl(&pdev->op_regs->command);
	temp |= (CMD_INTE);
	writel(temp, &pdev->op_regs->command);

	temp = readl(&pdev->ir_set->irq_pending);
	writel(IMAN_IE_SET(temp), &pdev->ir_set->irq_pending);

	trace_cdnsp_init("Controller ready to work");
	return 0;
err:
	cdnsp_halt(pdev);
	return ret;
}

static int cdnsp_gadget_udc_start(struct usb_gadget *g,
				  struct usb_gadget_driver *driver)
{
	enum usb_device_speed max_speed = driver->max_speed;
	struct cdnsp_device *pdev = gadget_to_cdnsp(g);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pdev->lock, flags);
	pdev->gadget_driver = driver;

	/* limit speed if necessary */
	max_speed = min(driver->max_speed, g->max_speed);
	ret = cdnsp_run(pdev, max_speed);

	spin_unlock_irqrestore(&pdev->lock, flags);

	return ret;
}

/*
 * Update Event Ring Dequeue Pointer:
 * - When all events have finished
 * - To avoid "Event Ring Full Error" condition
 */
void cdnsp_update_erst_dequeue(struct cdnsp_device *pdev,
			       union cdnsp_trb *event_ring_deq,
			       u8 clear_ehb)
{
	u64 temp_64;
	dma_addr_t deq;

	temp_64 = cdnsp_read_64(&pdev->ir_set->erst_dequeue);

	/* If necessary, update the HW's version of the event ring deq ptr. */
	if (event_ring_deq != pdev->event_ring->dequeue) {
		deq = cdnsp_trb_virt_to_dma(pdev->event_ring->deq_seg,
					    pdev->event_ring->dequeue);
		temp_64 &= ERST_PTR_MASK;
		temp_64 |= ((u64)deq & (u64)~ERST_PTR_MASK);
	}

	/* Clear the event handler busy flag (RW1C). */
	if (clear_ehb)
		temp_64 |= ERST_EHB;
	else
		temp_64 &= ~ERST_EHB;

	cdnsp_write_64(temp_64, &pdev->ir_set->erst_dequeue);
}

static void cdnsp_clear_cmd_ring(struct cdnsp_device *pdev)
{
	struct cdnsp_segment *seg;
	u64 val_64;
	int i;

	cdnsp_initialize_ring_info(pdev->cmd_ring);

	seg = pdev->cmd_ring->first_seg;
	for (i = 0; i < pdev->cmd_ring->num_segs; i++) {
		memset(seg->trbs, 0,
		       sizeof(union cdnsp_trb) * (TRBS_PER_SEGMENT - 1));
		seg = seg->next;
	}

	/* Set the address in the Command Ring Control register. */
	val_64 = cdnsp_read_64(&pdev->op_regs->cmd_ring);
	val_64 = (val_64 & (u64)CMD_RING_RSVD_BITS) |
		 (pdev->cmd_ring->first_seg->dma & (u64)~CMD_RING_RSVD_BITS) |
		 pdev->cmd_ring->cycle_state;
	cdnsp_write_64(val_64, &pdev->op_regs->cmd_ring);
}

static void cdnsp_consume_all_events(struct cdnsp_device *pdev)
{
	struct cdnsp_segment *event_deq_seg;
	union cdnsp_trb *event_ring_deq;
	union cdnsp_trb *event;
	u32 cycle_bit;

	event_ring_deq = pdev->event_ring->dequeue;
	event_deq_seg = pdev->event_ring->deq_seg;
	event = pdev->event_ring->dequeue;

	/* Update ring dequeue pointer. */
	while (1) {
		cycle_bit = (le32_to_cpu(event->event_cmd.flags) & TRB_CYCLE);

		/* Does the controller or driver own the TRB? */
		if (cycle_bit != pdev->event_ring->cycle_state)
			break;

		cdnsp_inc_deq(pdev, pdev->event_ring);

		if (!cdnsp_last_trb_on_seg(event_deq_seg, event)) {
			event++;
			continue;
		}

		if (cdnsp_last_trb_on_ring(pdev->event_ring, event_deq_seg,
					   event))
			cycle_bit ^= 1;

		event_deq_seg = event_deq_seg->next;
		event = event_deq_seg->trbs;
	}

	cdnsp_update_erst_dequeue(pdev,  event_ring_deq, 1);
}

static void cdnsp_stop(struct cdnsp_device *pdev)
{
	u32 temp;

	cdnsp_cmd_flush_ep(pdev, &pdev->eps[0]);

	/* Remove internally queued request for ep0. */
	if (!list_empty(&pdev->eps[0].pending_list)) {
		struct cdnsp_request *req;

		req = next_request(&pdev->eps[0].pending_list);
		if (req == &pdev->ep0_preq)
			cdnsp_ep_dequeue(&pdev->eps[0], req);
	}

	cdnsp_disable_port(pdev, &pdev->usb2_port.regs->portsc);
	cdnsp_disable_port(pdev, &pdev->usb3_port.regs->portsc);
	cdnsp_disable_slot(pdev);
	cdnsp_halt(pdev);

	temp = readl(&pdev->op_regs->status);
	writel((temp & ~0x1fff) | STS_EINT, &pdev->op_regs->status);
	temp = readl(&pdev->ir_set->irq_pending);
	writel(IMAN_IE_CLEAR(temp), &pdev->ir_set->irq_pending);

	cdnsp_clear_port_change_bit(pdev, &pdev->usb2_port.regs->portsc);
	cdnsp_clear_port_change_bit(pdev, &pdev->usb3_port.regs->portsc);

	/* Clear interrupt line */
	temp = readl(&pdev->ir_set->irq_pending);
	temp |= IMAN_IP;
	writel(temp, &pdev->ir_set->irq_pending);

	cdnsp_consume_all_events(pdev);
	cdnsp_clear_cmd_ring(pdev);

	trace_cdnsp_exit("Controller stopped.");
}

/*
 * Stop controller.
 * This function is called by the gadget core when the driver is removed.
 * Disable slot, disable IRQs, and quiesce the controller.
 */
static int cdnsp_gadget_udc_stop(struct usb_gadget *g)
{
	struct cdnsp_device *pdev = gadget_to_cdnsp(g);
	unsigned long flags;

	spin_lock_irqsave(&pdev->lock, flags);
	cdnsp_stop(pdev);
	pdev->gadget_driver = NULL;
	spin_unlock_irqrestore(&pdev->lock, flags);

	return 0;
}

static int cdnsp_gadget_get_frame(struct usb_gadget *g)
{
	struct cdnsp_device *pdev = gadget_to_cdnsp(g);

	return cdnsp_get_frame(pdev);
}

static void __cdnsp_gadget_wakeup(struct cdnsp_device *pdev)
{
	struct cdnsp_port_regs __iomem *port_regs;
	u32 portpm, portsc;

	port_regs = pdev->active_port->regs;
	portsc = readl(&port_regs->portsc) & PORT_PLS_MASK;

	/* Remote wakeup feature is not enabled by host. */
	if (pdev->gadget.speed < USB_SPEED_SUPER && portsc == XDEV_U2) {
		portpm = readl(&port_regs->portpmsc);

		if (!(portpm & PORT_RWE))
			return;
	}

	if (portsc == XDEV_U3 && !pdev->may_wakeup)
		return;

	cdnsp_set_link_state(pdev, &port_regs->portsc, XDEV_U0);

	pdev->cdnsp_state |= CDNSP_WAKEUP_PENDING;
}

static int cdnsp_gadget_wakeup(struct usb_gadget *g)
{
	struct cdnsp_device *pdev = gadget_to_cdnsp(g);
	unsigned long flags;

	spin_lock_irqsave(&pdev->lock, flags);
	__cdnsp_gadget_wakeup(pdev);
	spin_unlock_irqrestore(&pdev->lock, flags);

	return 0;
}

static int cdnsp_gadget_set_selfpowered(struct usb_gadget *g,
					int is_selfpowered)
{
	struct cdnsp_device *pdev = gadget_to_cdnsp(g);
	unsigned long flags;

	spin_lock_irqsave(&pdev->lock, flags);
	g->is_selfpowered = !!is_selfpowered;
	spin_unlock_irqrestore(&pdev->lock, flags);

	return 0;
}

static int cdnsp_gadget_pullup(struct usb_gadget *gadget, int is_on)
{
	struct cdnsp_device *pdev = gadget_to_cdnsp(gadget);
	struct cdns *cdns = dev_get_drvdata(pdev->dev);
	unsigned long flags;

	trace_cdnsp_pullup(is_on);

	/*
	 * Disable events handling while controller is being
	 * enabled/disabled.
	 */
	disable_irq(cdns->dev_irq);
	spin_lock_irqsave(&pdev->lock, flags);

	if (!is_on) {
		cdnsp_reset_device(pdev);
		cdns_clear_vbus(cdns);
	} else {
		cdns_set_vbus(cdns);
	}

	spin_unlock_irqrestore(&pdev->lock, flags);
	enable_irq(cdns->dev_irq);

	return 0;
}

static const struct usb_gadget_ops cdnsp_gadget_ops = {
	.get_frame		= cdnsp_gadget_get_frame,
	.wakeup			= cdnsp_gadget_wakeup,
	.set_selfpowered	= cdnsp_gadget_set_selfpowered,
	.pullup			= cdnsp_gadget_pullup,
	.udc_start		= cdnsp_gadget_udc_start,
	.udc_stop		= cdnsp_gadget_udc_stop,
};

static void cdnsp_get_ep_buffering(struct cdnsp_device *pdev,
				   struct cdnsp_ep *pep)
{
	void __iomem *reg = &pdev->cap_regs->hc_capbase;
	int endpoints;

	reg += cdnsp_find_next_ext_cap(reg, 0, XBUF_CAP_ID);

	if (!pep->direction) {
		pep->buffering = readl(reg + XBUF_RX_TAG_MASK_0_OFFSET);
		pep->buffering_period = readl(reg + XBUF_RX_TAG_MASK_1_OFFSET);
		pep->buffering = (pep->buffering + 1) / 2;
		pep->buffering_period = (pep->buffering_period + 1) / 2;
		return;
	}

	endpoints = HCS_ENDPOINTS(pdev->hcs_params1) / 2;

	/* Set to XBUF_TX_TAG_MASK_0 register. */
	reg += XBUF_TX_CMD_OFFSET + (endpoints * 2 + 2) * sizeof(u32);
	/* Set reg to XBUF_TX_TAG_MASK_N related with this endpoint. */
	reg += pep->number * sizeof(u32) * 2;

	pep->buffering = (readl(reg) + 1) / 2;
	pep->buffering_period = pep->buffering;
}

static int cdnsp_gadget_init_endpoints(struct cdnsp_device *pdev)
{
	int max_streams = HCC_MAX_PSA(pdev->hcc_params);
	struct cdnsp_ep *pep;
	int i;

	INIT_LIST_HEAD(&pdev->gadget.ep_list);

	if (max_streams < STREAM_LOG_STREAMS) {
		dev_err(pdev->dev, "Stream size %d not supported\n",
			max_streams);
		return -EINVAL;
	}

	max_streams = STREAM_LOG_STREAMS;

	for (i = 0; i < CDNSP_ENDPOINTS_NUM; i++) {
		bool direction = !(i & 1); /* Start from OUT endpoint. */
		u8 epnum = ((i + 1) >> 1);

		if (!CDNSP_IF_EP_EXIST(pdev, epnum, direction))
			continue;

		pep = &pdev->eps[i];
		pep->pdev = pdev;
		pep->number = epnum;
		pep->direction = direction; /* 0 for OUT, 1 for IN. */

		/*
		 * Ep0 is bidirectional, so ep0in and ep0out are represented by
		 * pdev->eps[0]
		 */
		if (epnum == 0) {
			snprintf(pep->name, sizeof(pep->name), "ep%d%s",
				 epnum, "BiDir");

			pep->idx = 0;
			usb_ep_set_maxpacket_limit(&pep->endpoint, 512);
			pep->endpoint.maxburst = 1;
			pep->endpoint.ops = &cdnsp_gadget_ep0_ops;
			pep->endpoint.desc = &cdnsp_gadget_ep0_desc;
			pep->endpoint.comp_desc = NULL;
			pep->endpoint.caps.type_control = true;
			pep->endpoint.caps.dir_in = true;
			pep->endpoint.caps.dir_out = true;

			pdev->ep0_preq.epnum = pep->number;
			pdev->ep0_preq.pep = pep;
			pdev->gadget.ep0 = &pep->endpoint;
		} else {
			snprintf(pep->name, sizeof(pep->name), "ep%d%s",
				 epnum, (pep->direction) ? "in" : "out");

			pep->idx =  (epnum * 2 + (direction ? 1 : 0)) - 1;
			usb_ep_set_maxpacket_limit(&pep->endpoint, 1024);

			pep->endpoint.max_streams = max_streams;
			pep->endpoint.ops = &cdnsp_gadget_ep_ops;
			list_add_tail(&pep->endpoint.ep_list,
				      &pdev->gadget.ep_list);

			pep->endpoint.caps.type_iso = true;
			pep->endpoint.caps.type_bulk = true;
			pep->endpoint.caps.type_int = true;

			pep->endpoint.caps.dir_in = direction;
			pep->endpoint.caps.dir_out = !direction;
		}

		pep->endpoint.name = pep->name;
		pep->in_ctx = cdnsp_get_ep_ctx(&pdev->in_ctx, pep->idx);
		pep->out_ctx = cdnsp_get_ep_ctx(&pdev->out_ctx, pep->idx);
		cdnsp_get_ep_buffering(pdev, pep);

		dev_dbg(pdev->dev, "Init %s, MPS: %04x SupType: "
			"CTRL: %s, INT: %s, BULK: %s, ISOC %s, "
			"SupDir IN: %s, OUT: %s\n",
			pep->name, 1024,
			(pep->endpoint.caps.type_control) ? "yes" : "no",
			(pep->endpoint.caps.type_int) ? "yes" : "no",
			(pep->endpoint.caps.type_bulk) ? "yes" : "no",
			(pep->endpoint.caps.type_iso) ? "yes" : "no",
			(pep->endpoint.caps.dir_in) ? "yes" : "no",
			(pep->endpoint.caps.dir_out) ? "yes" : "no");

		INIT_LIST_HEAD(&pep->pending_list);
	}

	return 0;
}

static void cdnsp_gadget_free_endpoints(struct cdnsp_device *pdev)
{
	struct cdnsp_ep *pep;
	int i;

	for (i = 0; i < CDNSP_ENDPOINTS_NUM; i++) {
		pep = &pdev->eps[i];
		if (pep->number != 0 && pep->out_ctx)
			list_del(&pep->endpoint.ep_list);
	}
}

void cdnsp_disconnect_gadget(struct cdnsp_device *pdev)
{
	pdev->cdnsp_state |= CDNSP_STATE_DISCONNECT_PENDING;

	if (pdev->gadget_driver && pdev->gadget_driver->disconnect) {
		spin_unlock(&pdev->lock);
		pdev->gadget_driver->disconnect(&pdev->gadget);
		spin_lock(&pdev->lock);
	}

	pdev->gadget.speed = USB_SPEED_UNKNOWN;
	usb_gadget_set_state(&pdev->gadget, USB_STATE_NOTATTACHED);

	pdev->cdnsp_state &= ~CDNSP_STATE_DISCONNECT_PENDING;
}

void cdnsp_suspend_gadget(struct cdnsp_device *pdev)
{
	if (pdev->gadget_driver && pdev->gadget_driver->suspend) {
		spin_unlock(&pdev->lock);
		pdev->gadget_driver->suspend(&pdev->gadget);
		spin_lock(&pdev->lock);
	}
}

void cdnsp_resume_gadget(struct cdnsp_device *pdev)
{
	if (pdev->gadget_driver && pdev->gadget_driver->resume) {
		spin_unlock(&pdev->lock);
		pdev->gadget_driver->resume(&pdev->gadget);
		spin_lock(&pdev->lock);
	}
}

void cdnsp_irq_reset(struct cdnsp_device *pdev)
{
	struct cdnsp_port_regs __iomem *port_regs;

	cdnsp_reset_device(pdev);

	port_regs = pdev->active_port->regs;
	pdev->gadget.speed = cdnsp_port_speed(readl(port_regs));

	spin_unlock(&pdev->lock);
	usb_gadget_udc_reset(&pdev->gadget, pdev->gadget_driver);
	spin_lock(&pdev->lock);

	switch (pdev->gadget.speed) {
	case USB_SPEED_SUPER_PLUS:
	case USB_SPEED_SUPER:
		cdnsp_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(512);
		pdev->gadget.ep0->maxpacket = 512;
		break;
	case USB_SPEED_HIGH:
	case USB_SPEED_FULL:
		cdnsp_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(64);
		pdev->gadget.ep0->maxpacket = 64;
		break;
	default:
		/* Low speed is not supported. */
		dev_err(pdev->dev, "Unknown device speed\n");
		break;
	}

	cdnsp_clear_chicken_bits_2(pdev, CHICKEN_XDMA_2_TP_CACHE_DIS);
	cdnsp_setup_device(pdev, SETUP_CONTEXT_ONLY);
	usb_gadget_set_state(&pdev->gadget, USB_STATE_DEFAULT);
}

static void cdnsp_get_rev_cap(struct cdnsp_device *pdev)
{
	void __iomem *reg = &pdev->cap_regs->hc_capbase;

	reg += cdnsp_find_next_ext_cap(reg, 0, RTL_REV_CAP);
	pdev->rev_cap  = reg;

	dev_info(pdev->dev, "Rev: %08x/%08x, eps: %08x, buff: %08x/%08x\n",
		 readl(&pdev->rev_cap->ctrl_revision),
		 readl(&pdev->rev_cap->rtl_revision),
		 readl(&pdev->rev_cap->ep_supported),
		 readl(&pdev->rev_cap->rx_buff_size),
		 readl(&pdev->rev_cap->tx_buff_size));
}

static int cdnsp_gen_setup(struct cdnsp_device *pdev)
{
	int ret;
	u32 reg;

	pdev->cap_regs = pdev->regs;
	pdev->op_regs = pdev->regs +
		HC_LENGTH(readl(&pdev->cap_regs->hc_capbase));
	pdev->run_regs = pdev->regs +
		(readl(&pdev->cap_regs->run_regs_off) & RTSOFF_MASK);

	/* Cache read-only capability registers */
	pdev->hcs_params1 = readl(&pdev->cap_regs->hcs_params1);
	pdev->hcc_params = readl(&pdev->cap_regs->hc_capbase);
	pdev->hci_version = HC_VERSION(pdev->hcc_params);
	pdev->hcc_params = readl(&pdev->cap_regs->hcc_params);

	cdnsp_get_rev_cap(pdev);

	/* Make sure the Device Controller is halted. */
	ret = cdnsp_halt(pdev);
	if (ret)
		return ret;

	/* Reset the internal controller memory state and registers. */
	ret = cdnsp_reset(pdev);
	if (ret)
		return ret;

	/*
	 * Set dma_mask and coherent_dma_mask to 64-bits,
	 * if controller supports 64-bit addressing.
	 */
	if (HCC_64BIT_ADDR(pdev->hcc_params) &&
	    !dma_set_mask(pdev->dev, DMA_BIT_MASK(64))) {
		dev_dbg(pdev->dev, "Enabling 64-bit DMA addresses.\n");
		dma_set_coherent_mask(pdev->dev, DMA_BIT_MASK(64));
	} else {
		/*
		 * This is to avoid error in cases where a 32-bit USB
		 * controller is used on a 64-bit capable system.
		 */
		ret = dma_set_mask(pdev->dev, DMA_BIT_MASK(32));
		if (ret)
			return ret;

		dev_dbg(pdev->dev, "Enabling 32-bit DMA addresses.\n");
		dma_set_coherent_mask(pdev->dev, DMA_BIT_MASK(32));
	}

	spin_lock_init(&pdev->lock);

	ret = cdnsp_mem_init(pdev);
	if (ret)
		return ret;

	/*
	 * Software workaround for U1: after transition
	 * to U1 the controller starts gating clock, and in some cases,
	 * it causes that controller stack.
	 */
	reg = readl(&pdev->port3x_regs->mode_2);
	reg &= ~CFG_3XPORT_U1_PIPE_CLK_GATE_EN;
	writel(reg, &pdev->port3x_regs->mode_2);

	return 0;
}

static int __cdnsp_gadget_init(struct cdns *cdns)
{
	struct cdnsp_device *pdev;
	u32 max_speed;
	int ret = -ENOMEM;

	cdns_drd_gadget_on(cdns);

	pdev = kzalloc(sizeof(*pdev), GFP_KERNEL);
	if (!pdev)
		return -ENOMEM;

	pm_runtime_get_sync(cdns->dev);

	cdns->gadget_dev = pdev;
	pdev->dev = cdns->dev;
	pdev->regs = cdns->dev_regs;
	max_speed = usb_get_maximum_speed(cdns->dev);

	switch (max_speed) {
	case USB_SPEED_FULL:
	case USB_SPEED_HIGH:
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		break;
	default:
		dev_err(cdns->dev, "invalid speed parameter %d\n", max_speed);
		fallthrough;
	case USB_SPEED_UNKNOWN:
		/* Default to SSP */
		max_speed = USB_SPEED_SUPER_PLUS;
		break;
	}

	pdev->gadget.ops = &cdnsp_gadget_ops;
	pdev->gadget.name = "cdnsp-gadget";
	pdev->gadget.speed = USB_SPEED_UNKNOWN;
	pdev->gadget.sg_supported = 1;
	pdev->gadget.max_speed = max_speed;
	pdev->gadget.lpm_capable = 1;

	pdev->setup_buf = kzalloc(CDNSP_EP0_SETUP_SIZE, GFP_KERNEL);
	if (!pdev->setup_buf)
		goto free_pdev;

	/*
	 * Controller supports not aligned buffer but it should improve
	 * performance.
	 */
	pdev->gadget.quirk_ep_out_aligned_size = true;

	ret = cdnsp_gen_setup(pdev);
	if (ret) {
		dev_err(pdev->dev, "Generic initialization failed %d\n", ret);
		goto free_setup;
	}

	ret = cdnsp_gadget_init_endpoints(pdev);
	if (ret) {
		dev_err(pdev->dev, "failed to initialize endpoints\n");
		goto halt_pdev;
	}

	ret = usb_add_gadget_udc(pdev->dev, &pdev->gadget);
	if (ret) {
		dev_err(pdev->dev, "failed to register udc\n");
		goto free_endpoints;
	}

	ret = devm_request_threaded_irq(pdev->dev, cdns->dev_irq,
					cdnsp_irq_handler,
					cdnsp_thread_irq_handler, IRQF_SHARED,
					dev_name(pdev->dev), pdev);
	if (ret)
		goto del_gadget;

	return 0;

del_gadget:
	usb_del_gadget_udc(&pdev->gadget);
free_endpoints:
	cdnsp_gadget_free_endpoints(pdev);
halt_pdev:
	cdnsp_halt(pdev);
	cdnsp_reset(pdev);
	cdnsp_mem_cleanup(pdev);
free_setup:
	kfree(pdev->setup_buf);
free_pdev:
	kfree(pdev);

	return ret;
}

static void cdnsp_gadget_exit(struct cdns *cdns)
{
	struct cdnsp_device *pdev = cdns->gadget_dev;

	devm_free_irq(pdev->dev, cdns->dev_irq, pdev);
	pm_runtime_mark_last_busy(cdns->dev);
	pm_runtime_put_autosuspend(cdns->dev);
	usb_del_gadget_udc(&pdev->gadget);
	cdnsp_gadget_free_endpoints(pdev);
	cdnsp_mem_cleanup(pdev);
	kfree(pdev);
	cdns->gadget_dev = NULL;
	cdns_drd_gadget_off(cdns);
}

static int cdnsp_gadget_suspend(struct cdns *cdns, bool do_wakeup)
{
	struct cdnsp_device *pdev = cdns->gadget_dev;
	unsigned long flags;

	if (pdev->link_state == XDEV_U3)
		return 0;

	spin_lock_irqsave(&pdev->lock, flags);
	cdnsp_disconnect_gadget(pdev);
	cdnsp_stop(pdev);
	spin_unlock_irqrestore(&pdev->lock, flags);

	return 0;
}

static int cdnsp_gadget_resume(struct cdns *cdns, bool hibernated)
{
	struct cdnsp_device *pdev = cdns->gadget_dev;
	enum usb_device_speed max_speed;
	unsigned long flags;
	int ret;

	if (!pdev->gadget_driver)
		return 0;

	spin_lock_irqsave(&pdev->lock, flags);
	max_speed = pdev->gadget_driver->max_speed;

	/* Limit speed if necessary. */
	max_speed = min(max_speed, pdev->gadget.max_speed);

	ret = cdnsp_run(pdev, max_speed);

	if (pdev->link_state == XDEV_U3)
		__cdnsp_gadget_wakeup(pdev);

	spin_unlock_irqrestore(&pdev->lock, flags);

	return ret;
}

/**
 * cdnsp_gadget_init - initialize device structure
 * @cdns: cdnsp instance
 *
 * This function initializes the gadget.
 */
int cdnsp_gadget_init(struct cdns *cdns)
{
	struct cdns_role_driver *rdrv;

	rdrv = devm_kzalloc(cdns->dev, sizeof(*rdrv), GFP_KERNEL);
	if (!rdrv)
		return -ENOMEM;

	rdrv->start	= __cdnsp_gadget_init;
	rdrv->stop	= cdnsp_gadget_exit;
	rdrv->suspend	= cdnsp_gadget_suspend;
	rdrv->resume	= cdnsp_gadget_resume;
	rdrv->state	= CDNS_ROLE_STATE_INACTIVE;
	rdrv->name	= "gadget";
	cdns->roles[USB_ROLE_DEVICE] = rdrv;

	return 0;
}
