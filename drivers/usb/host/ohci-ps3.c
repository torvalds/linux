/*
 *  PS3 OHCI Host Controller driver
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <asm/firmware.h>
#include <asm/ps3.h>

static int ps3_ohci_hc_reset(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	ohci->flags |= OHCI_QUIRK_BE_MMIO;
	ohci_hcd_init(ohci);
	return ohci_init(ohci);
}

static int __devinit ps3_ohci_hc_start(struct usb_hcd *hcd)
{
	int result;
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	/* Handle root hub init quirk in spider south bridge. */
	/* Also set PwrOn2PwrGood to 0x7f (254ms). */

	ohci_writel(ohci, 0x7f000000 | RH_A_PSM | RH_A_OCPM,
		&ohci->regs->roothub.a);
	ohci_writel(ohci, 0x00060000, &ohci->regs->roothub.b);

	result = ohci_run(ohci);

	if (result < 0) {
		err("can't start %s", hcd->self.bus_name);
		ohci_stop(hcd);
	}

	return result;
}

static const struct hc_driver ps3_ohci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "PS3 OHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ohci_hcd),
	.irq			= ohci_irq,
	.flags			= HCD_MEMORY | HCD_USB11,
	.reset			= ps3_ohci_hc_reset,
	.start			= ps3_ohci_hc_start,
	.stop			= ohci_stop,
	.shutdown		= ohci_shutdown,
	.urb_enqueue		= ohci_urb_enqueue,
	.urb_dequeue		= ohci_urb_dequeue,
	.endpoint_disable	= ohci_endpoint_disable,
	.get_frame_number	= ohci_get_frame,
	.hub_status_data	= ohci_hub_status_data,
	.hub_control		= ohci_hub_control,
	.start_port_reset	= ohci_start_port_reset,
#if defined(CONFIG_PM)
	.bus_suspend 		= ohci_bus_suspend,
	.bus_resume 		= ohci_bus_resume,
#endif
};

static int ps3_ohci_probe(struct ps3_system_bus_device *dev)
{
	int result;
	struct usb_hcd *hcd;
	unsigned int virq;
	static u64 dummy_mask = DMA_BIT_MASK(32);

	if (usb_disabled()) {
		result = -ENODEV;
		goto fail_start;
	}

	result = ps3_open_hv_device(dev);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: ps3_open_hv_device failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		result = -EPERM;
		goto fail_open;
	}

	result = ps3_dma_region_create(dev->d_region);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: ps3_dma_region_create failed: "
			"(%d)\n", __func__, __LINE__, result);
		BUG_ON("check region type");
		goto fail_dma_region;
	}

	result = ps3_mmio_region_create(dev->m_region);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: ps3_map_mmio_region failed\n",
			__func__, __LINE__);
		result = -EPERM;
		goto fail_mmio_region;
	}

	dev_dbg(&dev->core, "%s:%d: mmio mapped_addr %lxh\n", __func__,
		__LINE__, dev->m_region->lpar_addr);

	result = ps3_io_irq_setup(PS3_BINDING_CPU_ANY, dev->interrupt_id, &virq);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: ps3_construct_io_irq(%d) failed.\n",
			__func__, __LINE__, virq);
		result = -EPERM;
		goto fail_irq;
	}

	dev->core.dma_mask = &dummy_mask; /* FIXME: for improper usb code */

	hcd = usb_create_hcd(&ps3_ohci_hc_driver, &dev->core, dev_name(&dev->core));

	if (!hcd) {
		dev_dbg(&dev->core, "%s:%d: usb_create_hcd failed\n", __func__,
			__LINE__);
		result = -ENOMEM;
		goto fail_create_hcd;
	}

	hcd->rsrc_start = dev->m_region->lpar_addr;
	hcd->rsrc_len = dev->m_region->len;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name))
		dev_dbg(&dev->core, "%s:%d: request_mem_region failed\n",
			__func__, __LINE__);

	hcd->regs = ioremap(dev->m_region->lpar_addr, dev->m_region->len);

	if (!hcd->regs) {
		dev_dbg(&dev->core, "%s:%d: ioremap failed\n", __func__,
			__LINE__);
		result = -EPERM;
		goto fail_ioremap;
	}

	dev_dbg(&dev->core, "%s:%d: hcd->rsrc_start %lxh\n", __func__, __LINE__,
		(unsigned long)hcd->rsrc_start);
	dev_dbg(&dev->core, "%s:%d: hcd->rsrc_len   %lxh\n", __func__, __LINE__,
		(unsigned long)hcd->rsrc_len);
	dev_dbg(&dev->core, "%s:%d: hcd->regs       %lxh\n", __func__, __LINE__,
		(unsigned long)hcd->regs);
	dev_dbg(&dev->core, "%s:%d: virq            %lu\n", __func__, __LINE__,
		(unsigned long)virq);

	ps3_system_bus_set_drvdata(dev, hcd);

	result = usb_add_hcd(hcd, virq, IRQF_DISABLED);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: usb_add_hcd failed (%d)\n",
			__func__, __LINE__, result);
		goto fail_add_hcd;
	}

	return result;

fail_add_hcd:
	iounmap(hcd->regs);
fail_ioremap:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
fail_create_hcd:
	ps3_io_irq_destroy(virq);
fail_irq:
	ps3_free_mmio_region(dev->m_region);
fail_mmio_region:
	ps3_dma_region_free(dev->d_region);
fail_dma_region:
	ps3_close_hv_device(dev);
fail_open:
fail_start:
	return result;
}

static int ps3_ohci_remove(struct ps3_system_bus_device *dev)
{
	unsigned int tmp;
	struct usb_hcd *hcd = ps3_system_bus_get_drvdata(dev);

	BUG_ON(!hcd);

	dev_dbg(&dev->core, "%s:%d: regs %p\n", __func__, __LINE__, hcd->regs);
	dev_dbg(&dev->core, "%s:%d: irq %u\n", __func__, __LINE__, hcd->irq);

	tmp = hcd->irq;

	ohci_shutdown(hcd);
	usb_remove_hcd(hcd);

	ps3_system_bus_set_drvdata(dev, NULL);

	BUG_ON(!hcd->regs);
	iounmap(hcd->regs);

	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);

	ps3_io_irq_destroy(tmp);
	ps3_free_mmio_region(dev->m_region);

	ps3_dma_region_free(dev->d_region);
	ps3_close_hv_device(dev);

	return 0;
}

static int ps3_ohci_driver_register(struct ps3_system_bus_driver *drv)
{
	return firmware_has_feature(FW_FEATURE_PS3_LV1)
		? ps3_system_bus_driver_register(drv)
		: 0;
}

static void ps3_ohci_driver_unregister(struct ps3_system_bus_driver *drv)
{
	if (firmware_has_feature(FW_FEATURE_PS3_LV1))
		ps3_system_bus_driver_unregister(drv);
}

MODULE_ALIAS(PS3_MODULE_ALIAS_OHCI);

static struct ps3_system_bus_driver ps3_ohci_driver = {
	.core.name = "ps3-ohci-driver",
	.core.owner = THIS_MODULE,
	.match_id = PS3_MATCH_ID_OHCI,
	.probe = ps3_ohci_probe,
	.remove = ps3_ohci_remove,
	.shutdown = ps3_ohci_remove,
};
