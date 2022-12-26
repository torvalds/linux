// SPDX-License-Identifier: GPL-2.0+
/*
 * Freescale QUICC Engine USB Host Controller Driver
 *
 * Copyright (c) Freescale Semicondutor, Inc. 2006.
 *               Shlomi Gridish <gridish@freescale.com>
 *               Jerry Huang <Chang-Ming.Huang@freescale.com>
 * Copyright (c) Logic Product Development, Inc. 2007
 *               Peter Barada <peterb@logicpd.com>
 * Copyright (c) MontaVista Software, Inc. 2008.
 *               Anton Vorontsov <avorontsov@ru.mvista.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <soc/fsl/qe/qe.h>
#include <asm/fsl_gtm.h>
#include "fhci.h"

void fhci_start_sof_timer(struct fhci_hcd *fhci)
{
	fhci_dbg(fhci, "-> %s\n", __func__);

	/* clear frame_n */
	out_be16(&fhci->pram->frame_num, 0);

	out_be16(&fhci->regs->usb_ussft, 0);
	setbits8(&fhci->regs->usb_usmod, USB_MODE_SFTE);

	fhci_dbg(fhci, "<- %s\n", __func__);
}

void fhci_stop_sof_timer(struct fhci_hcd *fhci)
{
	fhci_dbg(fhci, "-> %s\n", __func__);

	clrbits8(&fhci->regs->usb_usmod, USB_MODE_SFTE);
	gtm_stop_timer16(fhci->timer);

	fhci_dbg(fhci, "<- %s\n", __func__);
}

u16 fhci_get_sof_timer_count(struct fhci_usb *usb)
{
	return be16_to_cpu(in_be16(&usb->fhci->regs->usb_ussft) / 12);
}

/* initialize the endpoint zero */
static u32 endpoint_zero_init(struct fhci_usb *usb,
			      enum fhci_mem_alloc data_mem,
			      u32 ring_len)
{
	u32 rc;

	rc = fhci_create_ep(usb, data_mem, ring_len);
	if (rc)
		return rc;

	/* inilialize endpoint registers */
	fhci_init_ep_registers(usb, usb->ep0, data_mem);

	return 0;
}

/* enable the USB interrupts */
void fhci_usb_enable_interrupt(struct fhci_usb *usb)
{
	struct fhci_hcd *fhci = usb->fhci;

	if (usb->intr_nesting_cnt == 1) {
		/* initialize the USB interrupt */
		enable_irq(fhci_to_hcd(fhci)->irq);

		/* initialize the event register and mask register */
		out_be16(&usb->fhci->regs->usb_usber, 0xffff);
		out_be16(&usb->fhci->regs->usb_usbmr, usb->saved_msk);

		/* enable the timer interrupts */
		enable_irq(fhci->timer->irq);
	} else if (usb->intr_nesting_cnt > 1)
		fhci_info(fhci, "unbalanced USB interrupts nesting\n");
	usb->intr_nesting_cnt--;
}

/* disable the usb interrupt */
void fhci_usb_disable_interrupt(struct fhci_usb *usb)
{
	struct fhci_hcd *fhci = usb->fhci;

	if (usb->intr_nesting_cnt == 0) {
		/* disable the timer interrupt */
		disable_irq_nosync(fhci->timer->irq);

		/* disable the usb interrupt */
		disable_irq_nosync(fhci_to_hcd(fhci)->irq);
		out_be16(&usb->fhci->regs->usb_usbmr, 0);
	}
	usb->intr_nesting_cnt++;
}

/* enable the USB controller */
static u32 fhci_usb_enable(struct fhci_hcd *fhci)
{
	struct fhci_usb *usb = fhci->usb_lld;

	out_be16(&usb->fhci->regs->usb_usber, 0xffff);
	out_be16(&usb->fhci->regs->usb_usbmr, usb->saved_msk);
	setbits8(&usb->fhci->regs->usb_usmod, USB_MODE_EN);

	mdelay(100);

	return 0;
}

/* disable the USB controller */
static u32 fhci_usb_disable(struct fhci_hcd *fhci)
{
	struct fhci_usb *usb = fhci->usb_lld;

	fhci_usb_disable_interrupt(usb);
	fhci_port_disable(fhci);

	/* disable the usb controller */
	if (usb->port_status == FHCI_PORT_FULL ||
			usb->port_status == FHCI_PORT_LOW)
		fhci_device_disconnected_interrupt(fhci);

	clrbits8(&usb->fhci->regs->usb_usmod, USB_MODE_EN);

	return 0;
}

/* check the bus state by polling the QE bit on the IO ports */
int fhci_ioports_check_bus_state(struct fhci_hcd *fhci)
{
	u8 bits = 0;

	/* check USBOE,if transmitting,exit */
	if (!gpiod_get_value(fhci->gpiods[GPIO_USBOE]))
		return -1;

	/* check USBRP */
	if (gpiod_get_value(fhci->gpiods[GPIO_USBRP]))
		bits |= 0x2;

	/* check USBRN */
	if (gpiod_get_value(fhci->gpiods[GPIO_USBRN]))
		bits |= 0x1;

	return bits;
}

static void fhci_mem_free(struct fhci_hcd *fhci)
{
	struct ed *ed;
	struct ed *next_ed;
	struct td *td;
	struct td *next_td;

	list_for_each_entry_safe(ed, next_ed, &fhci->empty_eds, node) {
		list_del(&ed->node);
		kfree(ed);
	}

	list_for_each_entry_safe(td, next_td, &fhci->empty_tds, node) {
		list_del(&td->node);
		kfree(td);
	}

	kfree(fhci->vroot_hub);
	fhci->vroot_hub = NULL;

	kfree(fhci->hc_list);
	fhci->hc_list = NULL;
}

static int fhci_mem_init(struct fhci_hcd *fhci)
{
	int i;

	fhci->hc_list = kzalloc(sizeof(*fhci->hc_list), GFP_KERNEL);
	if (!fhci->hc_list)
		goto err;

	INIT_LIST_HEAD(&fhci->hc_list->ctrl_list);
	INIT_LIST_HEAD(&fhci->hc_list->bulk_list);
	INIT_LIST_HEAD(&fhci->hc_list->iso_list);
	INIT_LIST_HEAD(&fhci->hc_list->intr_list);
	INIT_LIST_HEAD(&fhci->hc_list->done_list);

	fhci->vroot_hub = kzalloc(sizeof(*fhci->vroot_hub), GFP_KERNEL);
	if (!fhci->vroot_hub)
		goto err;

	INIT_LIST_HEAD(&fhci->empty_eds);
	INIT_LIST_HEAD(&fhci->empty_tds);

	/* initialize work queue to handle done list */
	fhci_tasklet.data = (unsigned long)fhci;
	fhci->process_done_task = &fhci_tasklet;

	for (i = 0; i < MAX_TDS; i++) {
		struct td *td;

		td = kmalloc(sizeof(*td), GFP_KERNEL);
		if (!td)
			goto err;
		fhci_recycle_empty_td(fhci, td);
	}
	for (i = 0; i < MAX_EDS; i++) {
		struct ed *ed;

		ed = kmalloc(sizeof(*ed), GFP_KERNEL);
		if (!ed)
			goto err;
		fhci_recycle_empty_ed(fhci, ed);
	}

	fhci->active_urbs = 0;
	return 0;
err:
	fhci_mem_free(fhci);
	return -ENOMEM;
}

/* destroy the fhci_usb structure */
static void fhci_usb_free(void *lld)
{
	struct fhci_usb *usb = lld;
	struct fhci_hcd *fhci;

	if (usb) {
		fhci = usb->fhci;
		fhci_config_transceiver(fhci, FHCI_PORT_POWER_OFF);
		fhci_ep0_free(usb);
		kfree(usb->actual_frame);
		kfree(usb);
	}
}

/* initialize the USB */
static int fhci_usb_init(struct fhci_hcd *fhci)
{
	struct fhci_usb *usb = fhci->usb_lld;

	memset_io(usb->fhci->pram, 0, FHCI_PRAM_SIZE);

	usb->port_status = FHCI_PORT_DISABLED;
	usb->max_frame_usage = FRAME_TIME_USAGE;
	usb->sw_transaction_time = SW_FIX_TIME_BETWEEN_TRANSACTION;

	usb->actual_frame = kzalloc(sizeof(*usb->actual_frame), GFP_KERNEL);
	if (!usb->actual_frame) {
		fhci_usb_free(usb);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&usb->actual_frame->tds_list);

	/* initializing registers on chip, clear frame number */
	out_be16(&fhci->pram->frame_num, 0);

	/* clear rx state */
	out_be32(&fhci->pram->rx_state, 0);

	/* set mask register */
	usb->saved_msk = (USB_E_TXB_MASK |
			  USB_E_TXE1_MASK |
			  USB_E_IDLE_MASK |
			  USB_E_RESET_MASK | USB_E_SFT_MASK | USB_E_MSF_MASK);

	out_8(&usb->fhci->regs->usb_usmod, USB_MODE_HOST | USB_MODE_EN);

	/* clearing the mask register */
	out_be16(&usb->fhci->regs->usb_usbmr, 0);

	/* initialing the event register */
	out_be16(&usb->fhci->regs->usb_usber, 0xffff);

	if (endpoint_zero_init(usb, DEFAULT_DATA_MEM, DEFAULT_RING_LEN) != 0) {
		fhci_usb_free(usb);
		return -EINVAL;
	}

	return 0;
}

/* initialize the fhci_usb struct and the corresponding data staruct */
static struct fhci_usb *fhci_create_lld(struct fhci_hcd *fhci)
{
	struct fhci_usb *usb;

	/* allocate memory for SCC data structure */
	usb = kzalloc(sizeof(*usb), GFP_KERNEL);
	if (!usb)
		return NULL;

	usb->fhci = fhci;
	usb->hc_list = fhci->hc_list;
	usb->vroot_hub = fhci->vroot_hub;

	usb->transfer_confirm = fhci_transfer_confirm_callback;

	return usb;
}

static int fhci_start(struct usb_hcd *hcd)
{
	int ret;
	struct fhci_hcd *fhci = hcd_to_fhci(hcd);

	ret = fhci_mem_init(fhci);
	if (ret) {
		fhci_err(fhci, "failed to allocate memory\n");
		goto err;
	}

	fhci->usb_lld = fhci_create_lld(fhci);
	if (!fhci->usb_lld) {
		fhci_err(fhci, "low level driver config failed\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = fhci_usb_init(fhci);
	if (ret) {
		fhci_err(fhci, "low level driver initialize failed\n");
		goto err;
	}

	spin_lock_init(&fhci->lock);

	/* connect the virtual root hub */
	fhci->vroot_hub->dev_num = 1;	/* this field may be needed to fix */
	fhci->vroot_hub->hub.wHubStatus = 0;
	fhci->vroot_hub->hub.wHubChange = 0;
	fhci->vroot_hub->port.wPortStatus = 0;
	fhci->vroot_hub->port.wPortChange = 0;

	hcd->state = HC_STATE_RUNNING;

	/*
	 * From here on, hub_wq concurrently accesses the root
	 * hub; drivers will be talking to enumerated devices.
	 * (On restart paths, hub_wq already knows about the root
	 * hub and could find work as soon as we wrote FLAG_CF.)
	 *
	 * Before this point the HC was idle/ready.  After, hub_wq
	 * and device drivers may start it running.
	 */
	fhci_usb_enable(fhci);
	return 0;
err:
	fhci_mem_free(fhci);
	return ret;
}

static void fhci_stop(struct usb_hcd *hcd)
{
	struct fhci_hcd *fhci = hcd_to_fhci(hcd);

	fhci_usb_disable_interrupt(fhci->usb_lld);
	fhci_usb_disable(fhci);

	fhci_usb_free(fhci->usb_lld);
	fhci->usb_lld = NULL;
	fhci_mem_free(fhci);
}

static int fhci_urb_enqueue(struct usb_hcd *hcd, struct urb *urb,
			    gfp_t mem_flags)
{
	struct fhci_hcd *fhci = hcd_to_fhci(hcd);
	u32 pipe = urb->pipe;
	int ret;
	int i;
	int size = 0;
	struct urb_priv *urb_priv;
	unsigned long flags;

	switch (usb_pipetype(pipe)) {
	case PIPE_CONTROL:
		/* 1 td fro setup,1 for ack */
		size = 2;
		fallthrough;
	case PIPE_BULK:
		/* one td for every 4096 bytes(can be up to 8k) */
		size += urb->transfer_buffer_length / 4096;
		/* ...add for any remaining bytes... */
		if ((urb->transfer_buffer_length % 4096) != 0)
			size++;
		/* ..and maybe a zero length packet to wrap it up */
		if (size == 0)
			size++;
		else if ((urb->transfer_flags & URB_ZERO_PACKET) != 0
			 && (urb->transfer_buffer_length
			     % usb_maxpacket(urb->dev, pipe)) != 0)
			size++;
		break;
	case PIPE_ISOCHRONOUS:
		size = urb->number_of_packets;
		if (size <= 0)
			return -EINVAL;
		for (i = 0; i < urb->number_of_packets; i++) {
			urb->iso_frame_desc[i].actual_length = 0;
			urb->iso_frame_desc[i].status = (u32) (-EXDEV);
		}
		break;
	case PIPE_INTERRUPT:
		size = 1;
	}

	/* allocate the private part of the URB */
	urb_priv = kzalloc(sizeof(*urb_priv), mem_flags);
	if (!urb_priv)
		return -ENOMEM;

	/* allocate the private part of the URB */
	urb_priv->tds = kcalloc(size, sizeof(*urb_priv->tds), mem_flags);
	if (!urb_priv->tds) {
		kfree(urb_priv);
		return -ENOMEM;
	}

	spin_lock_irqsave(&fhci->lock, flags);

	ret = usb_hcd_link_urb_to_ep(hcd, urb);
	if (ret)
		goto err;

	/* fill the private part of the URB */
	urb_priv->num_of_tds = size;

	urb->status = -EINPROGRESS;
	urb->actual_length = 0;
	urb->error_count = 0;
	urb->hcpriv = urb_priv;

	fhci_queue_urb(fhci, urb);
err:
	if (ret) {
		kfree(urb_priv->tds);
		kfree(urb_priv);
	}
	spin_unlock_irqrestore(&fhci->lock, flags);
	return ret;
}

/* dequeue FHCI URB */
static int fhci_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct fhci_hcd *fhci = hcd_to_fhci(hcd);
	struct fhci_usb *usb = fhci->usb_lld;
	int ret = -EINVAL;
	unsigned long flags;

	if (!urb || !urb->dev || !urb->dev->bus)
		goto out;

	spin_lock_irqsave(&fhci->lock, flags);

	ret = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (ret)
		goto out2;

	if (usb->port_status != FHCI_PORT_DISABLED) {
		struct urb_priv *urb_priv;

		/*
		 * flag the urb's data for deletion in some upcoming
		 * SF interrupt's delete list processing
		 */
		urb_priv = urb->hcpriv;

		if (!urb_priv || (urb_priv->state == URB_DEL))
			goto out2;

		urb_priv->state = URB_DEL;

		/* already pending? */
		urb_priv->ed->state = FHCI_ED_URB_DEL;
	} else {
		fhci_urb_complete_free(fhci, urb);
	}

out2:
	spin_unlock_irqrestore(&fhci->lock, flags);
out:
	return ret;
}

static void fhci_endpoint_disable(struct usb_hcd *hcd,
				  struct usb_host_endpoint *ep)
{
	struct fhci_hcd *fhci;
	struct ed *ed;
	unsigned long flags;

	fhci = hcd_to_fhci(hcd);
	spin_lock_irqsave(&fhci->lock, flags);
	ed = ep->hcpriv;
	if (ed) {
		while (ed->td_head != NULL) {
			struct td *td = fhci_remove_td_from_ed(ed);
			fhci_urb_complete_free(fhci, td->urb);
		}
		fhci_recycle_empty_ed(fhci, ed);
		ep->hcpriv = NULL;
	}
	spin_unlock_irqrestore(&fhci->lock, flags);
}

static int fhci_get_frame_number(struct usb_hcd *hcd)
{
	struct fhci_hcd *fhci = hcd_to_fhci(hcd);

	return get_frame_num(fhci);
}

static const struct hc_driver fhci_driver = {
	.description = "fsl,usb-fhci",
	.product_desc = "FHCI HOST Controller",
	.hcd_priv_size = sizeof(struct fhci_hcd),

	/* generic hardware linkage */
	.irq = fhci_irq,
	.flags = HCD_DMA | HCD_USB11 | HCD_MEMORY,

	/* basic lifecycle operation */
	.start = fhci_start,
	.stop = fhci_stop,

	/* managing i/o requests and associated device resources */
	.urb_enqueue = fhci_urb_enqueue,
	.urb_dequeue = fhci_urb_dequeue,
	.endpoint_disable = fhci_endpoint_disable,

	/* scheduling support */
	.get_frame_number = fhci_get_frame_number,

	/* root hub support */
	.hub_status_data = fhci_hub_status_data,
	.hub_control = fhci_hub_control,
};

static int of_fhci_probe(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct device_node *node = dev->of_node;
	struct usb_hcd *hcd;
	struct fhci_hcd *fhci;
	struct resource usb_regs;
	unsigned long pram_addr;
	unsigned int usb_irq;
	const char *sprop;
	const u32 *iprop;
	int size;
	int ret;
	int i;
	int j;

	if (usb_disabled())
		return -ENODEV;

	sprop = of_get_property(node, "mode", NULL);
	if (sprop && strcmp(sprop, "host"))
		return -ENODEV;

	hcd = usb_create_hcd(&fhci_driver, dev, dev_name(dev));
	if (!hcd) {
		dev_err(dev, "could not create hcd\n");
		return -ENOMEM;
	}

	fhci = hcd_to_fhci(hcd);
	hcd->self.controller = dev;
	dev_set_drvdata(dev, hcd);

	iprop = of_get_property(node, "hub-power-budget", &size);
	if (iprop && size == sizeof(*iprop))
		hcd->power_budget = *iprop;

	/* FHCI registers. */
	ret = of_address_to_resource(node, 0, &usb_regs);
	if (ret) {
		dev_err(dev, "could not get regs\n");
		goto err_regs;
	}

	hcd->regs = ioremap(usb_regs.start, resource_size(&usb_regs));
	if (!hcd->regs) {
		dev_err(dev, "could not ioremap regs\n");
		ret = -ENOMEM;
		goto err_regs;
	}
	fhci->regs = hcd->regs;

	/* Parameter RAM. */
	iprop = of_get_property(node, "reg", &size);
	if (!iprop || size < sizeof(*iprop) * 4) {
		dev_err(dev, "can't get pram offset\n");
		ret = -EINVAL;
		goto err_pram;
	}

	pram_addr = cpm_muram_alloc(FHCI_PRAM_SIZE, 64);
	if (IS_ERR_VALUE(pram_addr)) {
		dev_err(dev, "failed to allocate usb pram\n");
		ret = -ENOMEM;
		goto err_pram;
	}

	qe_issue_cmd(QE_ASSIGN_PAGE_TO_DEVICE, QE_CR_SUBBLOCK_USB,
		     QE_CR_PROTOCOL_UNSPECIFIED, pram_addr);
	fhci->pram = cpm_muram_addr(pram_addr);

	/* GPIOs and pins */
	for (i = 0; i < NUM_GPIOS; i++) {
		if (i < GPIO_SPEED)
			fhci->gpiods[i] = devm_gpiod_get_index(dev,
					NULL, i, GPIOD_IN);

		else
			fhci->gpiods[i] = devm_gpiod_get_index_optional(dev,
					NULL, i, GPIOD_OUT_LOW);

		if (IS_ERR(fhci->gpiods[i])) {
			dev_err(dev, "incorrect GPIO%d: %ld\n",
				i, PTR_ERR(fhci->gpiods[i]));
			goto err_gpios;
		}
		if (!fhci->gpiods[i]) {
			dev_info(dev, "assuming board doesn't have "
				 "%s gpio\n", i == GPIO_SPEED ?
				 "speed" : "power");
		}
	}

	for (j = 0; j < NUM_PINS; j++) {
		fhci->pins[j] = qe_pin_request(dev, j);
		if (IS_ERR(fhci->pins[j])) {
			ret = PTR_ERR(fhci->pins[j]);
			dev_err(dev, "can't get pin %d: %d\n", j, ret);
			goto err_pins;
		}
	}

	/* Frame limit timer and its interrupt. */
	fhci->timer = gtm_get_timer16();
	if (IS_ERR(fhci->timer)) {
		ret = PTR_ERR(fhci->timer);
		dev_err(dev, "failed to request qe timer: %i", ret);
		goto err_get_timer;
	}

	ret = request_irq(fhci->timer->irq, fhci_frame_limit_timer_irq,
			  0, "qe timer (usb)", hcd);
	if (ret) {
		dev_err(dev, "failed to request timer irq");
		goto err_timer_irq;
	}

	/* USB Host interrupt. */
	usb_irq = irq_of_parse_and_map(node, 0);
	if (!usb_irq) {
		dev_err(dev, "could not get usb irq\n");
		ret = -EINVAL;
		goto err_usb_irq;
	}

	/* Clocks. */
	sprop = of_get_property(node, "fsl,fullspeed-clock", NULL);
	if (sprop) {
		fhci->fullspeed_clk = qe_clock_source(sprop);
		if (fhci->fullspeed_clk == QE_CLK_DUMMY) {
			dev_err(dev, "wrong fullspeed-clock\n");
			ret = -EINVAL;
			goto err_clocks;
		}
	}

	sprop = of_get_property(node, "fsl,lowspeed-clock", NULL);
	if (sprop) {
		fhci->lowspeed_clk = qe_clock_source(sprop);
		if (fhci->lowspeed_clk == QE_CLK_DUMMY) {
			dev_err(dev, "wrong lowspeed-clock\n");
			ret = -EINVAL;
			goto err_clocks;
		}
	}

	if (fhci->fullspeed_clk == QE_CLK_NONE &&
			fhci->lowspeed_clk == QE_CLK_NONE) {
		dev_err(dev, "no clocks specified\n");
		ret = -EINVAL;
		goto err_clocks;
	}

	dev_info(dev, "at 0x%p, irq %d\n", hcd->regs, usb_irq);

	fhci_config_transceiver(fhci, FHCI_PORT_POWER_OFF);

	/* Start with full-speed, if possible. */
	if (fhci->fullspeed_clk != QE_CLK_NONE) {
		fhci_config_transceiver(fhci, FHCI_PORT_FULL);
		qe_usb_clock_set(fhci->fullspeed_clk, USB_CLOCK);
	} else {
		fhci_config_transceiver(fhci, FHCI_PORT_LOW);
		qe_usb_clock_set(fhci->lowspeed_clk, USB_CLOCK >> 3);
	}

	/* Clear and disable any pending interrupts. */
	out_be16(&fhci->regs->usb_usber, 0xffff);
	out_be16(&fhci->regs->usb_usbmr, 0);

	ret = usb_add_hcd(hcd, usb_irq, 0);
	if (ret < 0)
		goto err_add_hcd;

	device_wakeup_enable(hcd->self.controller);

	fhci_dfs_create(fhci);

	return 0;

err_add_hcd:
err_clocks:
	irq_dispose_mapping(usb_irq);
err_usb_irq:
	free_irq(fhci->timer->irq, hcd);
err_timer_irq:
	gtm_put_timer16(fhci->timer);
err_get_timer:
err_pins:
	while (--j >= 0)
		qe_pin_free(fhci->pins[j]);
err_gpios:
	cpm_muram_free(pram_addr);
err_pram:
	iounmap(hcd->regs);
err_regs:
	usb_put_hcd(hcd);
	return ret;
}

static int fhci_remove(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct fhci_hcd *fhci = hcd_to_fhci(hcd);
	int j;

	usb_remove_hcd(hcd);
	free_irq(fhci->timer->irq, hcd);
	gtm_put_timer16(fhci->timer);
	cpm_muram_free(cpm_muram_offset(fhci->pram));
	for (j = 0; j < NUM_PINS; j++)
		qe_pin_free(fhci->pins[j]);
	fhci_dfs_destroy(fhci);
	usb_put_hcd(hcd);
	return 0;
}

static int of_fhci_remove(struct platform_device *ofdev)
{
	return fhci_remove(&ofdev->dev);
}

static const struct of_device_id of_fhci_match[] = {
	{ .compatible = "fsl,mpc8323-qe-usb", },
	{},
};
MODULE_DEVICE_TABLE(of, of_fhci_match);

static struct platform_driver of_fhci_driver = {
	.driver = {
		.name = "fsl,usb-fhci",
		.of_match_table = of_fhci_match,
	},
	.probe		= of_fhci_probe,
	.remove		= of_fhci_remove,
};

module_platform_driver(of_fhci_driver);

MODULE_DESCRIPTION("USB Freescale Host Controller Interface Driver");
MODULE_AUTHOR("Shlomi Gridish <gridish@freescale.com>, "
	      "Jerry Huang <Chang-Ming.Huang@freescale.com>, "
	      "Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_LICENSE("GPL");
