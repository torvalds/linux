/*
 * drivers/usb/host/ohci-sunxi.c: SoftWinner OHCI Driver
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * Author: javen
 * History (author, date, version, notes):
 *	yangnaitian	2011-5-24	1.0	create this file
 *	javen		2011-6-26	1.1	add suspend and resume
 *	javen		2011-7-18	1.2	时钟开关和供电开关从驱动移出来
 *						(Clock switch and power switch
 *						is moved out from the driver)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/platform_device.h>
#include <linux/signal.h>

#include <linux/time.h>
#include <linux/timer.h>

#include <plat/sys_config.h>
#include <linux/clk.h>

#include  <mach/clock.h>
#include "sw_hci_sunxi.h"

#define SW_OHCI_NAME "sw-ohci"

static struct sw_hci_hcd *g_sw_ohci[3];
static u32 ohci_first_probe[3] = { 1, 1, 1 };

static void sw_start_ohc(struct sw_hci_hcd *sw_ohci)
{
	sw_ohci->open_clock(sw_ohci, 1);
	sw_ohci->port_configure(sw_ohci, 1);
	sw_ohci->usb_passby(sw_ohci, 1);
	sw_ohci->set_power(sw_ohci, 1);

	return;
}

static void sw_stop_ohc(struct sw_hci_hcd *sw_ohci)
{
	sw_ohci->set_power(sw_ohci, 0);
	sw_ohci->usb_passby(sw_ohci, 0);
	sw_ohci->port_configure(sw_ohci, 0);
	sw_ohci->close_clock(sw_ohci, 1);

	return;
}

static int __devinit sw_ohci_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	if ((ret = ohci_init(ohci)) < 0)
		return ret;

	if ((ret = ohci_run(ohci)) < 0) {
		pr_err("%s: cannot start %s, rc=%d", __func__,
			hcd->self.bus_name, ret);
		ohci_stop(hcd);
		return ret;
	}

	return 0;
}

static const struct hc_driver sw_ohci_hc_driver = {
	.description = hcd_name,
	.product_desc = "SW USB2.0 'Open' Host Controller (OHCI) Driver",
	.hcd_priv_size = sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ohci_irq,
	.flags = HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start = sw_ohci_start,
	.stop = ohci_stop,
	.shutdown = ohci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ohci_urb_enqueue,
	.urb_dequeue = ohci_urb_dequeue,
	.endpoint_disable = ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number = ohci_get_frame,

	/*
	 * dma fixes
	 */
	.map_urb_for_dma = sunxi_hcd_map_urb_for_dma,
	.unmap_urb_for_dma = sunxi_hcd_unmap_urb_for_dma,

	/*
	 * root hub support
	 */
	.hub_status_data = ohci_hub_status_data,
	.hub_control = ohci_hub_control,

#ifdef	CONFIG_PM
	.bus_suspend = ohci_bus_suspend,
	.bus_resume = ohci_bus_resume,
#endif
	.start_port_reset = ohci_start_port_reset,
};

static int sw_ohci_hcd_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = NULL;
	struct sw_hci_hcd *sw_ohci = NULL;

	if (pdev == NULL)
		return -EINVAL;

	hcd = platform_get_drvdata(pdev);
	if (hcd == NULL)
		return -ENODATA;

	sw_ohci = pdev->dev.platform_data;
	if (sw_ohci == NULL)
		return -ENODATA;

	pr_debug("[%s%d]: remove, pdev->name: %s, pdev->id: %d, sw_ohci:"
		" 0x%p\n", SW_OHCI_NAME, sw_ohci->usbc_no, pdev->name, pdev->id,
		sw_ohci);

	usb_remove_hcd(hcd);

	sw_stop_ohc(sw_ohci);
	sw_ohci->probe = 0;

	iounmap(hcd->regs);

	usb_put_hcd(hcd);

	sw_ohci->hcd = NULL;

	if (sw_ohci->host_init_state)
		g_sw_ohci[sw_ohci->usbc_no] = NULL;

	platform_set_drvdata(pdev, NULL);

	return 0;
}

int sw_usb_disable_ohci(__u32 usbc_no)
{
	struct sw_hci_hcd *sw_ohci = NULL;

	if (usbc_no != 1 && usbc_no != 2)
		return -EINVAL;

	sw_ohci = g_sw_ohci[usbc_no];
	if (sw_ohci == NULL)
		return -EFAULT;

	if (sw_ohci->host_init_state)
		return -ENOSYS;

	if (sw_ohci->probe == 0)
		return -ENOSYS;

	sw_ohci->probe = 0;
	pr_info("[%s]: disable ohci\n", sw_ohci->hci_name);
	sw_ohci_hcd_remove(sw_ohci->pdev);

	return 0;
}
EXPORT_SYMBOL(sw_usb_disable_ohci);

static int sw_ohci_hcd_probe(struct platform_device *pdev)
{
	int ret;
	int irq;
	struct resource *res;
	struct usb_hcd *hcd = NULL;
	struct sw_hci_hcd *sw_ohci = NULL;

	if (pdev == NULL)
		return -EINVAL;

	sw_ohci = pdev->dev.platform_data;
	if (!sw_ohci)
		return -ENODATA;

	sw_ohci->pdev = pdev;
	g_sw_ohci[sw_ohci->usbc_no] = sw_ohci;

	pr_debug("[%s%d]: probe, pdev->name: %s, pdev->id: %d, sw_ohci: 0x%p\n",
		SW_OHCI_NAME, sw_ohci->usbc_no, pdev->name, pdev->id, sw_ohci);

	/* get io resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("%s: failed to get io memory\n", __func__);
		ret = -ENOMEM;
		goto err_get_iomem;
	}

	/*creat a usb_hcd for the ohci controller */
	hcd = usb_create_hcd(&sw_ohci_hc_driver, &pdev->dev, SW_OHCI_NAME);
	if (!hcd) {
		pr_err("%s: failed to create hcd\n", __func__);
		ret = -ENOMEM;
		goto err_create_hcd;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = ioremap(res->start, resource_size(res));
	if (!hcd->regs) {
		pr_err("%s: failed to ioremap\n", __func__);
		ret = -ENOMEM;
		goto err_ioremap;
	}

	sw_ohci->hcd = hcd;

	/* ochi start to work */
	sw_start_ohc(sw_ohci);

	ohci_hcd_init(hcd_to_ohci(hcd));

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		pr_err("%s: failed to get irq\n", __func__);
		ret =  -ENODEV;
		goto err_get_irq;
	}

	ret = usb_add_hcd(hcd, irq, IRQF_DISABLED | IRQF_SHARED);
	if (ret != 0) {
		pr_err("%s: failed to add hcd, rc=%d\n", __func__, ret);
		goto err_add_hcd;
	}

	platform_set_drvdata(pdev, hcd);

	pr_debug("[%s]: probe, clock: SW_VA_CCM_AHBMOD_OFFSET(0x%x), SW_VA_CCM_USBCLK_OFFSET(0x%x);"
	     " usb: SW_USB_PMU_IRQ_ENABLE(0x%x), dram:(0x%x, 0x%x)\n",
	     sw_ohci->hci_name, (u32) readl(SW_VA_CCM_IO_BASE + SW_VA_CCM_AHBMOD_OFFSET),
	     (u32) readl(SW_VA_CCM_IO_BASE + SW_VA_CCM_USBCLK_OFFSET),
	     (u32) readl(sw_ohci->usb_vbase + SW_USB_PMU_IRQ_ENABLE),
	     (u32) readl(SW_VA_DRAM_IO_BASE + SW_SDRAM_REG_HPCR_USB1),
	     (u32) readl(SW_VA_DRAM_IO_BASE + SW_SDRAM_REG_HPCR_USB2));

	sw_ohci->probe = 1;

	/* Disable ohci, when driver probe */
	if (sw_ohci->host_init_state == 0) {
		if (ohci_first_probe[sw_ohci->usbc_no]) {
			sw_usb_disable_ohci(sw_ohci->usbc_no);
			ohci_first_probe[sw_ohci->usbc_no]--;
		}
	}

	return 0;

err_add_hcd:
err_get_irq:
	iounmap(hcd->regs);
err_ioremap:
	usb_put_hcd(hcd);
err_get_iomem:
err_create_hcd:
	sw_ohci->hcd = NULL;
	g_sw_ohci[sw_ohci->usbc_no] = NULL;
	return ret;
}

int sw_usb_enable_ohci(__u32 usbc_no)
{
	struct sw_hci_hcd *sw_ohci = NULL;

	if (usbc_no != 1 && usbc_no != 2)
		return -EINVAL;

	sw_ohci = g_sw_ohci[usbc_no];
	if (sw_ohci == NULL)
		return -EFAULT;

	if (sw_ohci->host_init_state)
		return -ENOSYS;

	if (sw_ohci->probe == 1) /* already enabled */
		return 0;

	sw_ohci->probe = 1;
	pr_info("[%s]: enable ohci\n", sw_ohci->hci_name);
	sw_ohci_hcd_probe(sw_ohci->pdev);

	return 0;
}
EXPORT_SYMBOL(sw_usb_enable_ohci);

void sw_ohci_hcd_shutdown(struct platform_device *pdev)
{
	struct sw_hci_hcd *sw_ohci = NULL;

	if (pdev == NULL)
		return;

	sw_ohci = pdev->dev.platform_data;
	if (sw_ohci == NULL)
		return;

	if (sw_ohci->probe == 0)
		return;

	pr_info("[%s]: shutdown start\n", sw_ohci->hci_name);
	usb_hcd_platform_shutdown(pdev);
	sw_stop_ohc(sw_ohci);
	pr_info("[%s]: shutdown end\n", sw_ohci->hci_name);

	return;
}

#ifdef CONFIG_PM
static int sw_ohci_hcd_suspend(struct device *dev)
{
	struct sw_hci_hcd *sw_ohci = NULL;
	struct usb_hcd *hcd = NULL;
	struct ohci_hcd *ohci = NULL;
	unsigned long flags = 0;
	int rc = 0;

	if (dev == NULL)
		return 0;

	hcd = dev_get_drvdata(dev);
	if (hcd == NULL)
		return 0;

	sw_ohci = dev->platform_data;
	if (sw_ohci == NULL)
		return 0;

	if (sw_ohci->probe == 0)
		return 0;

	ohci = hcd_to_ohci(hcd);
	if (ohci == NULL)
		return 0;

	pr_info("[%s]: suspend\n", sw_ohci->hci_name);

	/* Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 *
	 * This is still racy as hcd->state is manipulated outside of
	 * any locks =P But that will be a different fix.
	 */
	spin_lock_irqsave(&ohci->lock, flags);
	ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	(void)ohci_readl(ohci, &ohci->regs->intrdisable);

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	spin_unlock_irqrestore(&ohci->lock, flags);

	sw_stop_ohc(sw_ohci);

	return rc;
}

static int sw_ohci_hcd_resume(struct device *dev)
{
	struct sw_hci_hcd *sw_ohci = NULL;
	struct usb_hcd *hcd = NULL;

	if (dev == NULL)
		return 0;

	hcd = dev_get_drvdata(dev);
	if (hcd == NULL)
		return 0;

	sw_ohci = dev->platform_data;
	if (sw_ohci == NULL)
		return 0;

	if (sw_ohci->probe == 0)
		return 0;

	pr_info("[%s]: resume\n", sw_ohci->hci_name);
	sw_start_ohc(sw_ohci);

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	ohci_finish_controller_resume(hcd);

	return 0;
}

static const struct dev_pm_ops sw_ohci_pmops = {
	.suspend = sw_ohci_hcd_suspend,
	.resume = sw_ohci_hcd_resume,
};
#define SW_OHCI_PMOPS  (&sw_ohci_pmops)
#else
#define SW_OHCI_PMOPS NULL
#endif

static struct platform_driver sw_ohci_hcd_driver = {
	.probe = sw_ohci_hcd_probe,
	.remove = sw_ohci_hcd_remove,
	.shutdown = sw_ohci_hcd_shutdown,
	.driver = {
		.name = SW_OHCI_NAME,
		.owner = THIS_MODULE,
		.pm = SW_OHCI_PMOPS,
	},
};
