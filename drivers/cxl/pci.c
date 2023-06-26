// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sizes.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/io.h>
#include "cxlmem.h"
#include "cxlpci.h"
#include "cxl.h"
#include "pmu.h"

/**
 * DOC: cxl pci
 *
 * This implements the PCI exclusive functionality for a CXL device as it is
 * defined by the Compute Express Link specification. CXL devices may surface
 * certain functionality even if it isn't CXL enabled. While this driver is
 * focused around the PCI specific aspects of a CXL device, it binds to the
 * specific CXL memory device class code, and therefore the implementation of
 * cxl_pci is focused around CXL memory devices.
 *
 * The driver has several responsibilities, mainly:
 *  - Create the memX device and register on the CXL bus.
 *  - Enumerate device's register interface and map them.
 *  - Registers nvdimm bridge device with cxl_core.
 *  - Registers a CXL mailbox with cxl_core.
 */

#define cxl_doorbell_busy(cxlds)                                                \
	(readl((cxlds)->regs.mbox + CXLDEV_MBOX_CTRL_OFFSET) &                  \
	 CXLDEV_MBOX_CTRL_DOORBELL)

/* CXL 2.0 - 8.2.8.4 */
#define CXL_MAILBOX_TIMEOUT_MS (2 * HZ)

/*
 * CXL 2.0 ECN "Add Mailbox Ready Time" defines a capability field to
 * dictate how long to wait for the mailbox to become ready. The new
 * field allows the device to tell software the amount of time to wait
 * before mailbox ready. This field per the spec theoretically allows
 * for up to 255 seconds. 255 seconds is unreasonably long, its longer
 * than the maximum SATA port link recovery wait. Default to 60 seconds
 * until someone builds a CXL device that needs more time in practice.
 */
static unsigned short mbox_ready_timeout = 60;
module_param(mbox_ready_timeout, ushort, 0644);
MODULE_PARM_DESC(mbox_ready_timeout, "seconds to wait for mailbox ready");

static int cxl_pci_mbox_wait_for_doorbell(struct cxl_dev_state *cxlds)
{
	const unsigned long start = jiffies;
	unsigned long end = start;

	while (cxl_doorbell_busy(cxlds)) {
		end = jiffies;

		if (time_after(end, start + CXL_MAILBOX_TIMEOUT_MS)) {
			/* Check again in case preempted before timeout test */
			if (!cxl_doorbell_busy(cxlds))
				break;
			return -ETIMEDOUT;
		}
		cpu_relax();
	}

	dev_dbg(cxlds->dev, "Doorbell wait took %dms",
		jiffies_to_msecs(end) - jiffies_to_msecs(start));
	return 0;
}

#define cxl_err(dev, status, msg)                                        \
	dev_err_ratelimited(dev, msg ", device state %s%s\n",                  \
			    status & CXLMDEV_DEV_FATAL ? " fatal" : "",        \
			    status & CXLMDEV_FW_HALT ? " firmware-halt" : "")

#define cxl_cmd_err(dev, cmd, status, msg)                               \
	dev_err_ratelimited(dev, msg " (opcode: %#x), device state %s%s\n",    \
			    (cmd)->opcode,                                     \
			    status & CXLMDEV_DEV_FATAL ? " fatal" : "",        \
			    status & CXLMDEV_FW_HALT ? " firmware-halt" : "")

struct cxl_dev_id {
	struct cxl_dev_state *cxlds;
};

static int cxl_request_irq(struct cxl_dev_state *cxlds, int irq,
			   irq_handler_t handler, irq_handler_t thread_fn)
{
	struct device *dev = cxlds->dev;
	struct cxl_dev_id *dev_id;

	/* dev_id must be globally unique and must contain the cxlds */
	dev_id = devm_kzalloc(dev, sizeof(*dev_id), GFP_KERNEL);
	if (!dev_id)
		return -ENOMEM;
	dev_id->cxlds = cxlds;

	return devm_request_threaded_irq(dev, irq, handler, thread_fn,
					 IRQF_SHARED | IRQF_ONESHOT,
					 NULL, dev_id);
}

static bool cxl_mbox_background_complete(struct cxl_dev_state *cxlds)
{
	u64 reg;

	reg = readq(cxlds->regs.mbox + CXLDEV_MBOX_BG_CMD_STATUS_OFFSET);
	return FIELD_GET(CXLDEV_MBOX_BG_CMD_COMMAND_PCT_MASK, reg) == 100;
}

static irqreturn_t cxl_pci_mbox_irq(int irq, void *id)
{
	u64 reg;
	u16 opcode;
	struct cxl_dev_id *dev_id = id;
	struct cxl_dev_state *cxlds = dev_id->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);

	if (!cxl_mbox_background_complete(cxlds))
		return IRQ_NONE;

	reg = readq(cxlds->regs.mbox + CXLDEV_MBOX_BG_CMD_STATUS_OFFSET);
	opcode = FIELD_GET(CXLDEV_MBOX_BG_CMD_COMMAND_OPCODE_MASK, reg);
	if (opcode == CXL_MBOX_OP_SANITIZE) {
		if (mds->security.sanitize_node)
			sysfs_notify_dirent(mds->security.sanitize_node);

		dev_dbg(cxlds->dev, "Sanitization operation ended\n");
	} else {
		/* short-circuit the wait in __cxl_pci_mbox_send_cmd() */
		rcuwait_wake_up(&mds->mbox_wait);
	}

	return IRQ_HANDLED;
}

/*
 * Sanitization operation polling mode.
 */
static void cxl_mbox_sanitize_work(struct work_struct *work)
{
	struct cxl_memdev_state *mds =
		container_of(work, typeof(*mds), security.poll_dwork.work);
	struct cxl_dev_state *cxlds = &mds->cxlds;

	mutex_lock(&mds->mbox_mutex);
	if (cxl_mbox_background_complete(cxlds)) {
		mds->security.poll_tmo_secs = 0;
		put_device(cxlds->dev);

		if (mds->security.sanitize_node)
			sysfs_notify_dirent(mds->security.sanitize_node);

		dev_dbg(cxlds->dev, "Sanitization operation ended\n");
	} else {
		int timeout = mds->security.poll_tmo_secs + 10;

		mds->security.poll_tmo_secs = min(15 * 60, timeout);
		queue_delayed_work(system_wq, &mds->security.poll_dwork,
				   timeout * HZ);
	}
	mutex_unlock(&mds->mbox_mutex);
}

/**
 * __cxl_pci_mbox_send_cmd() - Execute a mailbox command
 * @mds: The memory device driver data
 * @mbox_cmd: Command to send to the memory device.
 *
 * Context: Any context. Expects mbox_mutex to be held.
 * Return: -ETIMEDOUT if timeout occurred waiting for completion. 0 on success.
 *         Caller should check the return code in @mbox_cmd to make sure it
 *         succeeded.
 *
 * This is a generic form of the CXL mailbox send command thus only using the
 * registers defined by the mailbox capability ID - CXL 2.0 8.2.8.4. Memory
 * devices, and perhaps other types of CXL devices may have further information
 * available upon error conditions. Driver facilities wishing to send mailbox
 * commands should use the wrapper command.
 *
 * The CXL spec allows for up to two mailboxes. The intention is for the primary
 * mailbox to be OS controlled and the secondary mailbox to be used by system
 * firmware. This allows the OS and firmware to communicate with the device and
 * not need to coordinate with each other. The driver only uses the primary
 * mailbox.
 */
static int __cxl_pci_mbox_send_cmd(struct cxl_memdev_state *mds,
				   struct cxl_mbox_cmd *mbox_cmd)
{
	struct cxl_dev_state *cxlds = &mds->cxlds;
	void __iomem *payload = cxlds->regs.mbox + CXLDEV_MBOX_PAYLOAD_OFFSET;
	struct device *dev = cxlds->dev;
	u64 cmd_reg, status_reg;
	size_t out_len;
	int rc;

	lockdep_assert_held(&mds->mbox_mutex);

	/*
	 * Here are the steps from 8.2.8.4 of the CXL 2.0 spec.
	 *   1. Caller reads MB Control Register to verify doorbell is clear
	 *   2. Caller writes Command Register
	 *   3. Caller writes Command Payload Registers if input payload is non-empty
	 *   4. Caller writes MB Control Register to set doorbell
	 *   5. Caller either polls for doorbell to be clear or waits for interrupt if configured
	 *   6. Caller reads MB Status Register to fetch Return code
	 *   7. If command successful, Caller reads Command Register to get Payload Length
	 *   8. If output payload is non-empty, host reads Command Payload Registers
	 *
	 * Hardware is free to do whatever it wants before the doorbell is rung,
	 * and isn't allowed to change anything after it clears the doorbell. As
	 * such, steps 2 and 3 can happen in any order, and steps 6, 7, 8 can
	 * also happen in any order (though some orders might not make sense).
	 */

	/* #1 */
	if (cxl_doorbell_busy(cxlds)) {
		u64 md_status =
			readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);

		cxl_cmd_err(cxlds->dev, mbox_cmd, md_status,
			    "mailbox queue busy");
		return -EBUSY;
	}

	/*
	 * With sanitize polling, hardware might be done and the poller still
	 * not be in sync. Ensure no new command comes in until so. Keep the
	 * hardware semantics and only allow device health status.
	 */
	if (mds->security.poll_tmo_secs > 0) {
		if (mbox_cmd->opcode != CXL_MBOX_OP_GET_HEALTH_INFO)
			return -EBUSY;
	}

	cmd_reg = FIELD_PREP(CXLDEV_MBOX_CMD_COMMAND_OPCODE_MASK,
			     mbox_cmd->opcode);
	if (mbox_cmd->size_in) {
		if (WARN_ON(!mbox_cmd->payload_in))
			return -EINVAL;

		cmd_reg |= FIELD_PREP(CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK,
				      mbox_cmd->size_in);
		memcpy_toio(payload, mbox_cmd->payload_in, mbox_cmd->size_in);
	}

	/* #2, #3 */
	writeq(cmd_reg, cxlds->regs.mbox + CXLDEV_MBOX_CMD_OFFSET);

	/* #4 */
	dev_dbg(dev, "Sending command: 0x%04x\n", mbox_cmd->opcode);
	writel(CXLDEV_MBOX_CTRL_DOORBELL,
	       cxlds->regs.mbox + CXLDEV_MBOX_CTRL_OFFSET);

	/* #5 */
	rc = cxl_pci_mbox_wait_for_doorbell(cxlds);
	if (rc == -ETIMEDOUT) {
		u64 md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);

		cxl_cmd_err(cxlds->dev, mbox_cmd, md_status, "mailbox timeout");
		return rc;
	}

	/* #6 */
	status_reg = readq(cxlds->regs.mbox + CXLDEV_MBOX_STATUS_OFFSET);
	mbox_cmd->return_code =
		FIELD_GET(CXLDEV_MBOX_STATUS_RET_CODE_MASK, status_reg);

	/*
	 * Handle the background command in a synchronous manner.
	 *
	 * All other mailbox commands will serialize/queue on the mbox_mutex,
	 * which we currently hold. Furthermore this also guarantees that
	 * cxl_mbox_background_complete() checks are safe amongst each other,
	 * in that no new bg operation can occur in between.
	 *
	 * Background operations are timesliced in accordance with the nature
	 * of the command. In the event of timeout, the mailbox state is
	 * indeterminate until the next successful command submission and the
	 * driver can get back in sync with the hardware state.
	 */
	if (mbox_cmd->return_code == CXL_MBOX_CMD_RC_BACKGROUND) {
		u64 bg_status_reg;
		int i, timeout;

		/*
		 * Sanitization is a special case which monopolizes the device
		 * and cannot be timesliced. Handle asynchronously instead,
		 * and allow userspace to poll(2) for completion.
		 */
		if (mbox_cmd->opcode == CXL_MBOX_OP_SANITIZE) {
			if (mds->security.poll_tmo_secs != -1) {
				/* hold the device throughout */
				get_device(cxlds->dev);

				/* give first timeout a second */
				timeout = 1;
				mds->security.poll_tmo_secs = timeout;
				queue_delayed_work(system_wq,
						   &mds->security.poll_dwork,
						   timeout * HZ);
			}

			dev_dbg(dev, "Sanitization operation started\n");
			goto success;
		}

		dev_dbg(dev, "Mailbox background operation (0x%04x) started\n",
			mbox_cmd->opcode);

		timeout = mbox_cmd->poll_interval_ms;
		for (i = 0; i < mbox_cmd->poll_count; i++) {
			if (rcuwait_wait_event_timeout(&mds->mbox_wait,
				       cxl_mbox_background_complete(cxlds),
				       TASK_UNINTERRUPTIBLE,
				       msecs_to_jiffies(timeout)) > 0)
				break;
		}

		if (!cxl_mbox_background_complete(cxlds)) {
			dev_err(dev, "timeout waiting for background (%d ms)\n",
				timeout * mbox_cmd->poll_count);
			return -ETIMEDOUT;
		}

		bg_status_reg = readq(cxlds->regs.mbox +
				      CXLDEV_MBOX_BG_CMD_STATUS_OFFSET);
		mbox_cmd->return_code =
			FIELD_GET(CXLDEV_MBOX_BG_CMD_COMMAND_RC_MASK,
				  bg_status_reg);
		dev_dbg(dev,
			"Mailbox background operation (0x%04x) completed\n",
			mbox_cmd->opcode);
	}

	if (mbox_cmd->return_code != CXL_MBOX_CMD_RC_SUCCESS) {
		dev_dbg(dev, "Mailbox operation had an error: %s\n",
			cxl_mbox_cmd_rc2str(mbox_cmd));
		return 0; /* completed but caller must check return_code */
	}

success:
	/* #7 */
	cmd_reg = readq(cxlds->regs.mbox + CXLDEV_MBOX_CMD_OFFSET);
	out_len = FIELD_GET(CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK, cmd_reg);

	/* #8 */
	if (out_len && mbox_cmd->payload_out) {
		/*
		 * Sanitize the copy. If hardware misbehaves, out_len per the
		 * spec can actually be greater than the max allowed size (21
		 * bits available but spec defined 1M max). The caller also may
		 * have requested less data than the hardware supplied even
		 * within spec.
		 */
		size_t n;

		n = min3(mbox_cmd->size_out, mds->payload_size, out_len);
		memcpy_fromio(mbox_cmd->payload_out, payload, n);
		mbox_cmd->size_out = n;
	} else {
		mbox_cmd->size_out = 0;
	}

	return 0;
}

static int cxl_pci_mbox_send(struct cxl_memdev_state *mds,
			     struct cxl_mbox_cmd *cmd)
{
	int rc;

	mutex_lock_io(&mds->mbox_mutex);
	rc = __cxl_pci_mbox_send_cmd(mds, cmd);
	mutex_unlock(&mds->mbox_mutex);

	return rc;
}

static int cxl_pci_setup_mailbox(struct cxl_memdev_state *mds)
{
	struct cxl_dev_state *cxlds = &mds->cxlds;
	const int cap = readl(cxlds->regs.mbox + CXLDEV_MBOX_CAPS_OFFSET);
	struct device *dev = cxlds->dev;
	unsigned long timeout;
	u64 md_status;

	timeout = jiffies + mbox_ready_timeout * HZ;
	do {
		md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);
		if (md_status & CXLMDEV_MBOX_IF_READY)
			break;
		if (msleep_interruptible(100))
			break;
	} while (!time_after(jiffies, timeout));

	if (!(md_status & CXLMDEV_MBOX_IF_READY)) {
		cxl_err(dev, md_status, "timeout awaiting mailbox ready");
		return -ETIMEDOUT;
	}

	/*
	 * A command may be in flight from a previous driver instance,
	 * think kexec, do one doorbell wait so that
	 * __cxl_pci_mbox_send_cmd() can assume that it is the only
	 * source for future doorbell busy events.
	 */
	if (cxl_pci_mbox_wait_for_doorbell(cxlds) != 0) {
		cxl_err(dev, md_status, "timeout awaiting mailbox idle");
		return -ETIMEDOUT;
	}

	mds->mbox_send = cxl_pci_mbox_send;
	mds->payload_size =
		1 << FIELD_GET(CXLDEV_MBOX_CAP_PAYLOAD_SIZE_MASK, cap);

	/*
	 * CXL 2.0 8.2.8.4.3 Mailbox Capabilities Register
	 *
	 * If the size is too small, mandatory commands will not work and so
	 * there's no point in going forward. If the size is too large, there's
	 * no harm is soft limiting it.
	 */
	mds->payload_size = min_t(size_t, mds->payload_size, SZ_1M);
	if (mds->payload_size < 256) {
		dev_err(dev, "Mailbox is too small (%zub)",
			mds->payload_size);
		return -ENXIO;
	}

	dev_dbg(dev, "Mailbox payload sized %zu", mds->payload_size);

	rcuwait_init(&mds->mbox_wait);

	if (cap & CXLDEV_MBOX_CAP_BG_CMD_IRQ) {
		u32 ctrl;
		int irq, msgnum;
		struct pci_dev *pdev = to_pci_dev(cxlds->dev);

		msgnum = FIELD_GET(CXLDEV_MBOX_CAP_IRQ_MSGNUM_MASK, cap);
		irq = pci_irq_vector(pdev, msgnum);
		if (irq < 0)
			goto mbox_poll;

		if (cxl_request_irq(cxlds, irq, cxl_pci_mbox_irq, NULL))
			goto mbox_poll;

		/* enable background command mbox irq support */
		ctrl = readl(cxlds->regs.mbox + CXLDEV_MBOX_CTRL_OFFSET);
		ctrl |= CXLDEV_MBOX_CTRL_BG_CMD_IRQ;
		writel(ctrl, cxlds->regs.mbox + CXLDEV_MBOX_CTRL_OFFSET);

		return 0;
	}

mbox_poll:
	mds->security.poll = true;
	INIT_DELAYED_WORK(&mds->security.poll_dwork, cxl_mbox_sanitize_work);

	dev_dbg(cxlds->dev, "Mailbox interrupts are unsupported");
	return 0;
}

/*
 * Assume that any RCIEP that emits the CXL memory expander class code
 * is an RCD
 */
static bool is_cxl_restricted(struct pci_dev *pdev)
{
	return pci_pcie_type(pdev) == PCI_EXP_TYPE_RC_END;
}

static int cxl_rcrb_get_comp_regs(struct pci_dev *pdev,
				  struct cxl_register_map *map)
{
	struct cxl_port *port;
	struct cxl_dport *dport;
	resource_size_t component_reg_phys;

	*map = (struct cxl_register_map) {
		.dev = &pdev->dev,
		.resource = CXL_RESOURCE_NONE,
	};

	port = cxl_pci_find_port(pdev, &dport);
	if (!port)
		return -EPROBE_DEFER;

	component_reg_phys = cxl_rcd_component_reg_phys(&pdev->dev, dport);

	put_device(&port->dev);

	if (component_reg_phys == CXL_RESOURCE_NONE)
		return -ENXIO;

	map->resource = component_reg_phys;
	map->reg_type = CXL_REGLOC_RBI_COMPONENT;
	map->max_size = CXL_COMPONENT_REG_BLOCK_SIZE;

	return 0;
}

static int cxl_pci_setup_regs(struct pci_dev *pdev, enum cxl_regloc_type type,
			      struct cxl_register_map *map)
{
	int rc;

	rc = cxl_find_regblock(pdev, type, map);

	/*
	 * If the Register Locator DVSEC does not exist, check if it
	 * is an RCH and try to extract the Component Registers from
	 * an RCRB.
	 */
	if (rc && type == CXL_REGLOC_RBI_COMPONENT && is_cxl_restricted(pdev))
		rc = cxl_rcrb_get_comp_regs(pdev, map);

	if (rc)
		return rc;

	return cxl_setup_regs(map);
}

static int cxl_pci_ras_unmask(struct pci_dev *pdev)
{
	struct pci_host_bridge *host_bridge = pci_find_host_bridge(pdev->bus);
	struct cxl_dev_state *cxlds = pci_get_drvdata(pdev);
	void __iomem *addr;
	u32 orig_val, val, mask;
	u16 cap;
	int rc;

	if (!cxlds->regs.ras) {
		dev_dbg(&pdev->dev, "No RAS registers.\n");
		return 0;
	}

	/* BIOS has CXL error control */
	if (!host_bridge->native_cxl_error)
		return -ENXIO;

	rc = pcie_capability_read_word(pdev, PCI_EXP_DEVCTL, &cap);
	if (rc)
		return rc;

	if (cap & PCI_EXP_DEVCTL_URRE) {
		addr = cxlds->regs.ras + CXL_RAS_UNCORRECTABLE_MASK_OFFSET;
		orig_val = readl(addr);

		mask = CXL_RAS_UNCORRECTABLE_MASK_MASK |
		       CXL_RAS_UNCORRECTABLE_MASK_F256B_MASK;
		val = orig_val & ~mask;
		writel(val, addr);
		dev_dbg(&pdev->dev,
			"Uncorrectable RAS Errors Mask: %#x -> %#x\n",
			orig_val, val);
	}

	if (cap & PCI_EXP_DEVCTL_CERE) {
		addr = cxlds->regs.ras + CXL_RAS_CORRECTABLE_MASK_OFFSET;
		orig_val = readl(addr);
		val = orig_val & ~CXL_RAS_CORRECTABLE_MASK_MASK;
		writel(val, addr);
		dev_dbg(&pdev->dev, "Correctable RAS Errors Mask: %#x -> %#x\n",
			orig_val, val);
	}

	return 0;
}

static void free_event_buf(void *buf)
{
	kvfree(buf);
}

/*
 * There is a single buffer for reading event logs from the mailbox.  All logs
 * share this buffer protected by the mds->event_log_lock.
 */
static int cxl_mem_alloc_event_buf(struct cxl_memdev_state *mds)
{
	struct cxl_get_event_payload *buf;

	buf = kvmalloc(mds->payload_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	mds->event.buf = buf;

	return devm_add_action_or_reset(mds->cxlds.dev, free_event_buf, buf);
}

static int cxl_alloc_irq_vectors(struct pci_dev *pdev)
{
	int nvecs;

	/*
	 * Per CXL 3.0 3.1.1 CXL.io Endpoint a function on a CXL device must
	 * not generate INTx messages if that function participates in
	 * CXL.cache or CXL.mem.
	 *
	 * Additionally pci_alloc_irq_vectors() handles calling
	 * pci_free_irq_vectors() automatically despite not being called
	 * pcim_*.  See pci_setup_msi_context().
	 */
	nvecs = pci_alloc_irq_vectors(pdev, 1, CXL_PCI_DEFAULT_MAX_VECTORS,
				      PCI_IRQ_MSIX | PCI_IRQ_MSI);
	if (nvecs < 1) {
		dev_dbg(&pdev->dev, "Failed to alloc irq vectors: %d\n", nvecs);
		return -ENXIO;
	}
	return 0;
}

static irqreturn_t cxl_event_thread(int irq, void *id)
{
	struct cxl_dev_id *dev_id = id;
	struct cxl_dev_state *cxlds = dev_id->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);
	u32 status;

	do {
		/*
		 * CXL 3.0 8.2.8.3.1: The lower 32 bits are the status;
		 * ignore the reserved upper 32 bits
		 */
		status = readl(cxlds->regs.status + CXLDEV_DEV_EVENT_STATUS_OFFSET);
		/* Ignore logs unknown to the driver */
		status &= CXLDEV_EVENT_STATUS_ALL;
		if (!status)
			break;
		cxl_mem_get_event_records(mds, status);
		cond_resched();
	} while (status);

	return IRQ_HANDLED;
}

static int cxl_event_req_irq(struct cxl_dev_state *cxlds, u8 setting)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int irq;

	if (FIELD_GET(CXLDEV_EVENT_INT_MODE_MASK, setting) != CXL_INT_MSI_MSIX)
		return -ENXIO;

	irq =  pci_irq_vector(pdev,
			      FIELD_GET(CXLDEV_EVENT_INT_MSGNUM_MASK, setting));
	if (irq < 0)
		return irq;

	return cxl_request_irq(cxlds, irq, NULL, cxl_event_thread);
}

static int cxl_event_get_int_policy(struct cxl_memdev_state *mds,
				    struct cxl_event_interrupt_policy *policy)
{
	struct cxl_mbox_cmd mbox_cmd = {
		.opcode = CXL_MBOX_OP_GET_EVT_INT_POLICY,
		.payload_out = policy,
		.size_out = sizeof(*policy),
	};
	int rc;

	rc = cxl_internal_send_cmd(mds, &mbox_cmd);
	if (rc < 0)
		dev_err(mds->cxlds.dev,
			"Failed to get event interrupt policy : %d", rc);

	return rc;
}

static int cxl_event_config_msgnums(struct cxl_memdev_state *mds,
				    struct cxl_event_interrupt_policy *policy)
{
	struct cxl_mbox_cmd mbox_cmd;
	int rc;

	*policy = (struct cxl_event_interrupt_policy) {
		.info_settings = CXL_INT_MSI_MSIX,
		.warn_settings = CXL_INT_MSI_MSIX,
		.failure_settings = CXL_INT_MSI_MSIX,
		.fatal_settings = CXL_INT_MSI_MSIX,
	};

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_SET_EVT_INT_POLICY,
		.payload_in = policy,
		.size_in = sizeof(*policy),
	};

	rc = cxl_internal_send_cmd(mds, &mbox_cmd);
	if (rc < 0) {
		dev_err(mds->cxlds.dev, "Failed to set event interrupt policy : %d",
			rc);
		return rc;
	}

	/* Retrieve final interrupt settings */
	return cxl_event_get_int_policy(mds, policy);
}

static int cxl_event_irqsetup(struct cxl_memdev_state *mds)
{
	struct cxl_dev_state *cxlds = &mds->cxlds;
	struct cxl_event_interrupt_policy policy;
	int rc;

	rc = cxl_event_config_msgnums(mds, &policy);
	if (rc)
		return rc;

	rc = cxl_event_req_irq(cxlds, policy.info_settings);
	if (rc) {
		dev_err(cxlds->dev, "Failed to get interrupt for event Info log\n");
		return rc;
	}

	rc = cxl_event_req_irq(cxlds, policy.warn_settings);
	if (rc) {
		dev_err(cxlds->dev, "Failed to get interrupt for event Warn log\n");
		return rc;
	}

	rc = cxl_event_req_irq(cxlds, policy.failure_settings);
	if (rc) {
		dev_err(cxlds->dev, "Failed to get interrupt for event Failure log\n");
		return rc;
	}

	rc = cxl_event_req_irq(cxlds, policy.fatal_settings);
	if (rc) {
		dev_err(cxlds->dev, "Failed to get interrupt for event Fatal log\n");
		return rc;
	}

	return 0;
}

static bool cxl_event_int_is_fw(u8 setting)
{
	u8 mode = FIELD_GET(CXLDEV_EVENT_INT_MODE_MASK, setting);

	return mode == CXL_INT_FW;
}

static int cxl_event_config(struct pci_host_bridge *host_bridge,
			    struct cxl_memdev_state *mds)
{
	struct cxl_event_interrupt_policy policy;
	int rc;

	/*
	 * When BIOS maintains CXL error reporting control, it will process
	 * event records.  Only one agent can do so.
	 */
	if (!host_bridge->native_cxl_error)
		return 0;

	rc = cxl_mem_alloc_event_buf(mds);
	if (rc)
		return rc;

	rc = cxl_event_get_int_policy(mds, &policy);
	if (rc)
		return rc;

	if (cxl_event_int_is_fw(policy.info_settings) ||
	    cxl_event_int_is_fw(policy.warn_settings) ||
	    cxl_event_int_is_fw(policy.failure_settings) ||
	    cxl_event_int_is_fw(policy.fatal_settings)) {
		dev_err(mds->cxlds.dev,
			"FW still in control of Event Logs despite _OSC settings\n");
		return -EBUSY;
	}

	rc = cxl_event_irqsetup(mds);
	if (rc)
		return rc;

	cxl_mem_get_event_records(mds, CXLDEV_EVENT_STATUS_ALL);

	return 0;
}

static int cxl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct pci_host_bridge *host_bridge = pci_find_host_bridge(pdev->bus);
	struct cxl_memdev_state *mds;
	struct cxl_dev_state *cxlds;
	struct cxl_register_map map;
	struct cxl_memdev *cxlmd;
	int i, rc, pmu_count;

	/*
	 * Double check the anonymous union trickery in struct cxl_regs
	 * FIXME switch to struct_group()
	 */
	BUILD_BUG_ON(offsetof(struct cxl_regs, memdev) !=
		     offsetof(struct cxl_regs, device_regs.memdev));

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;
	pci_set_master(pdev);

	mds = cxl_memdev_state_create(&pdev->dev);
	if (IS_ERR(mds))
		return PTR_ERR(mds);
	cxlds = &mds->cxlds;
	pci_set_drvdata(pdev, cxlds);

	cxlds->rcd = is_cxl_restricted(pdev);
	cxlds->serial = pci_get_dsn(pdev);
	cxlds->cxl_dvsec = pci_find_dvsec_capability(
		pdev, PCI_DVSEC_VENDOR_ID_CXL, CXL_DVSEC_PCIE_DEVICE);
	if (!cxlds->cxl_dvsec)
		dev_warn(&pdev->dev,
			 "Device DVSEC not present, skip CXL.mem init\n");

	rc = cxl_pci_setup_regs(pdev, CXL_REGLOC_RBI_MEMDEV, &map);
	if (rc)
		return rc;

	rc = cxl_map_device_regs(&map, &cxlds->regs.device_regs);
	if (rc)
		return rc;

	/*
	 * If the component registers can't be found, the cxl_pci driver may
	 * still be useful for management functions so don't return an error.
	 */
	cxlds->component_reg_phys = CXL_RESOURCE_NONE;
	rc = cxl_pci_setup_regs(pdev, CXL_REGLOC_RBI_COMPONENT, &map);
	if (rc)
		dev_warn(&pdev->dev, "No component registers (%d)\n", rc);
	else if (!map.component_map.ras.valid)
		dev_dbg(&pdev->dev, "RAS registers not found\n");

	cxlds->component_reg_phys = map.resource;

	rc = cxl_map_component_regs(&map, &cxlds->regs.component,
				    BIT(CXL_CM_CAP_CAP_ID_RAS));
	if (rc)
		dev_dbg(&pdev->dev, "Failed to map RAS capability.\n");

	rc = cxl_await_media_ready(cxlds);
	if (rc == 0)
		cxlds->media_ready = true;
	else
		dev_warn(&pdev->dev, "Media not active (%d)\n", rc);

	rc = cxl_alloc_irq_vectors(pdev);
	if (rc)
		return rc;

	rc = cxl_pci_setup_mailbox(mds);
	if (rc)
		return rc;

	rc = cxl_enumerate_cmds(mds);
	if (rc)
		return rc;

	rc = cxl_set_timestamp(mds);
	if (rc)
		return rc;

	rc = cxl_poison_state_init(mds);
	if (rc)
		return rc;

	rc = cxl_dev_state_identify(mds);
	if (rc)
		return rc;

	rc = cxl_mem_create_range_info(mds);
	if (rc)
		return rc;

	cxlmd = devm_cxl_add_memdev(cxlds);
	if (IS_ERR(cxlmd))
		return PTR_ERR(cxlmd);

	rc = cxl_memdev_setup_fw_upload(mds);
	if (rc)
		return rc;

	pmu_count = cxl_count_regblock(pdev, CXL_REGLOC_RBI_PMU);
	for (i = 0; i < pmu_count; i++) {
		struct cxl_pmu_regs pmu_regs;

		rc = cxl_find_regblock_instance(pdev, CXL_REGLOC_RBI_PMU, &map, i);
		if (rc) {
			dev_dbg(&pdev->dev, "Could not find PMU regblock\n");
			break;
		}

		rc = cxl_map_pmu_regs(pdev, &pmu_regs, &map);
		if (rc) {
			dev_dbg(&pdev->dev, "Could not map PMU regs\n");
			break;
		}

		rc = devm_cxl_pmu_add(cxlds->dev, &pmu_regs, cxlmd->id, i, CXL_PMU_MEMDEV);
		if (rc) {
			dev_dbg(&pdev->dev, "Could not add PMU instance\n");
			break;
		}
	}

	rc = cxl_event_config(host_bridge, mds);
	if (rc)
		return rc;

	rc = cxl_pci_ras_unmask(pdev);
	if (rc)
		dev_dbg(&pdev->dev, "No RAS reporting unmasked\n");

	pci_save_state(pdev);

	return rc;
}

static const struct pci_device_id cxl_mem_pci_tbl[] = {
	/* PCI class code for CXL.mem Type-3 Devices */
	{ PCI_DEVICE_CLASS((PCI_CLASS_MEMORY_CXL << 8 | CXL_MEMORY_PROGIF), ~0)},
	{ /* terminate list */ },
};
MODULE_DEVICE_TABLE(pci, cxl_mem_pci_tbl);

static pci_ers_result_t cxl_slot_reset(struct pci_dev *pdev)
{
	struct cxl_dev_state *cxlds = pci_get_drvdata(pdev);
	struct cxl_memdev *cxlmd = cxlds->cxlmd;
	struct device *dev = &cxlmd->dev;

	dev_info(&pdev->dev, "%s: restart CXL.mem after slot reset\n",
		 dev_name(dev));
	pci_restore_state(pdev);
	if (device_attach(dev) <= 0)
		return PCI_ERS_RESULT_DISCONNECT;
	return PCI_ERS_RESULT_RECOVERED;
}

static void cxl_error_resume(struct pci_dev *pdev)
{
	struct cxl_dev_state *cxlds = pci_get_drvdata(pdev);
	struct cxl_memdev *cxlmd = cxlds->cxlmd;
	struct device *dev = &cxlmd->dev;

	dev_info(&pdev->dev, "%s: error resume %s\n", dev_name(dev),
		 dev->driver ? "successful" : "failed");
}

static const struct pci_error_handlers cxl_error_handlers = {
	.error_detected	= cxl_error_detected,
	.slot_reset	= cxl_slot_reset,
	.resume		= cxl_error_resume,
	.cor_error_detected	= cxl_cor_error_detected,
};

static struct pci_driver cxl_pci_driver = {
	.name			= KBUILD_MODNAME,
	.id_table		= cxl_mem_pci_tbl,
	.probe			= cxl_pci_probe,
	.err_handler		= &cxl_error_handlers,
	.driver	= {
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
	},
};

MODULE_LICENSE("GPL v2");
module_pci_driver(cxl_pci_driver);
MODULE_IMPORT_NS(CXL);
