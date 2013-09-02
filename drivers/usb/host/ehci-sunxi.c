/*
 * drivers/usb/host/ehci-sunxi.c: SoftWinner EHCI Driver
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
#include <linux/time.h>
#include <linux/timer.h>

#include <plat/sys_config.h>
#include <linux/clk.h>

#include  <mach/clock.h>
#include "sw_hci_sunxi.h"

#define SW_EHCI_NAME "sw-ehci"

static struct sw_hci_hcd *g_sw_ehci[3];
static u32 ehci_first_probe[3] = { 1, 1, 1 };

static void sw_start_ehci(struct sw_hci_hcd *sw_ehci)
{
	sw_ehci->open_clock(sw_ehci, 0);
	sw_ehci->usb_passby(sw_ehci, 1);
	sw_ehci->port_configure(sw_ehci, 1 /*enable*/);
	sw_ehci->set_power(sw_ehci, 1);

	return;
}

static void sw_stop_ehci(struct sw_hci_hcd *sw_ehci)
{
	sw_ehci->set_power(sw_ehci, 0);
	sw_ehci->port_configure(sw_ehci, 0 /*disable*/);
	sw_ehci->usb_passby(sw_ehci, 0);
	sw_ehci->close_clock(sw_ehci, 0);

	return;
}

static int sw_ehci_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int ret;

	ret = ehci_setup(hcd);
	if (ret)
		return ret;

	/* TODO: is following ok for sunxi? Only very few ehci drivers seem to
	 * disable watchdog. */
	ehci->need_io_watchdog = 0;

	return ret;
}

static const struct hc_driver sw_ehci_hc_driver = {
	.description = hcd_name,
	.product_desc = "SW USB2.0 'Enhanced' Host Controller (EHCI) Driver",
	.hcd_priv_size = sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset = sw_ehci_setup,
	.start = ehci_run,
	.stop = ehci_stop,
	.shutdown = ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ehci_urb_enqueue,
	.urb_dequeue = ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,
	.endpoint_reset = ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,

	/*
	 * dma fixes
	 */
	.map_urb_for_dma = sunxi_hcd_map_urb_for_dma,
	.unmap_urb_for_dma = sunxi_hcd_unmap_urb_for_dma,

	/*
	 * root hub support
	 */
	.hub_status_data = ehci_hub_status_data,
	.hub_control = ehci_hub_control,
	.bus_suspend = ehci_bus_suspend,
	.bus_resume = ehci_bus_resume,
	.relinquish_port = ehci_relinquish_port,
	.port_handed_over = ehci_port_handed_over,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
};

static int sw_ehci_hcd_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = NULL;
	struct sw_hci_hcd *sw_ehci = NULL;

	if (pdev == NULL)
		return -EINVAL;

	hcd = platform_get_drvdata(pdev);
	if (hcd == NULL)
		return -ENODATA;

	sw_ehci = pdev->dev.platform_data;
	if (sw_ehci == NULL)
		return -ENODATA;

	pr_debug("[%s%d]: remove, pdev->name: %s, pdev->id: %d, sw_ehci:"
		" 0x%p\n", SW_EHCI_NAME, sw_ehci->usbc_no, pdev->name, pdev->id,
		sw_ehci);

	usb_remove_hcd(hcd);

	iounmap(hcd->regs);

	usb_put_hcd(hcd);

	sw_stop_ehci(sw_ehci);
	sw_ehci->probe = 0;

	sw_ehci->hcd = NULL;

	if (sw_ehci->host_init_state)
		g_sw_ehci[sw_ehci->usbc_no] = NULL;

	platform_set_drvdata(pdev, NULL);

	return 0;
}

int sw_usb_disable_ehci(__u32 usbc_no)
{
	struct sw_hci_hcd *sw_ehci = NULL;

	if (usbc_no != 1 && usbc_no != 2)
		return -EINVAL;

	sw_ehci = g_sw_ehci[usbc_no];
	if (sw_ehci == NULL)
		return -EFAULT;

	if (sw_ehci->host_init_state)
		return -ENOSYS;

	if (sw_ehci->probe == 0)
		return -ENOSYS;

	sw_ehci->probe = 0;
	pr_info("[%s]: disable ehci\n", sw_ehci->hci_name);
	sw_ehci_hcd_remove(sw_ehci->pdev);

	return 0;
}
EXPORT_SYMBOL(sw_usb_disable_ehci);

static int sw_ehci_hcd_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd = NULL;
	struct ehci_hcd *ehci = NULL;
	struct sw_hci_hcd *sw_ehci = NULL;
	struct resource *res;
	int irq;
	int ret = 0;

	if (pdev == NULL)
		return -EINVAL;

	sw_ehci = pdev->dev.platform_data;
	if (!sw_ehci)
		return -ENODATA;

	sw_ehci->pdev = pdev;
	g_sw_ehci[sw_ehci->usbc_no] = sw_ehci;

	pr_debug("[%s%d]: probe, pdev->name: %s, pdev->id: %d,"
		" sw_ehci: 0x%p\n",
		SW_EHCI_NAME, sw_ehci->usbc_no, pdev->name, pdev->id, sw_ehci);

	/* get io resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("%s: failed to get io memory\n", __func__);
		ret = -ENOMEM;
		goto err_get_iomem;
	}

	/* creat a usb_hcd for the ehci controller */
	hcd = usb_create_hcd(&sw_ehci_hc_driver, &pdev->dev, SW_EHCI_NAME);
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

	sw_ehci->hcd = hcd;

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		pr_err("%s: failed to get irq\n", __func__);
		ret =  -ENODEV;
		goto err_get_irq;
	}

	/* ehci start to work */
	sw_start_ehci(sw_ehci);

	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;

	ret = usb_add_hcd(hcd, irq, IRQF_DISABLED | IRQF_SHARED);
	if (ret != 0) {
		pr_err("%s: failed to add hcd, rc=%d\n", __func__, ret);
		goto err_add_hcd;
	}

	platform_set_drvdata(pdev, hcd);

	pr_debug("[%s]: probe, clock: SW_VA_CCM_AHBMOD_OFFSET(0x%x), SW_VA_CCM_USBCLK_OFFSET(0x%x);"
	     " usb: 0x800(0x%x), dram:(0x%x, 0x%x)\n",
	     sw_ehci->hci_name, (u32) readl(SW_VA_CCM_IO_BASE + SW_VA_CCM_AHBMOD_OFFSET),
	     (u32) readl(SW_VA_CCM_IO_BASE + SW_VA_CCM_USBCLK_OFFSET),
	     (u32) readl(sw_ehci->usb_vbase + 0x800),
	     (u32) readl(SW_VA_DRAM_IO_BASE + SW_SDRAM_REG_HPCR_USB1),
	     (u32) readl(SW_VA_DRAM_IO_BASE + SW_SDRAM_REG_HPCR_USB2));

	sw_ehci->probe = 1;

	/* Disable ehci, when driver probe */
	if (sw_ehci->host_init_state == 0) {
		if (ehci_first_probe[sw_ehci->usbc_no]) {
			sw_usb_disable_ehci(sw_ehci->usbc_no);
			ehci_first_probe[sw_ehci->usbc_no]--;
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
	sw_ehci->hcd = NULL;
	g_sw_ehci[sw_ehci->usbc_no] = NULL;
	return ret;
}

int sw_usb_enable_ehci(__u32 usbc_no)
{
	struct sw_hci_hcd *sw_ehci = NULL;

	if (usbc_no != 1 && usbc_no != 2)
		return -EINVAL;

	sw_ehci = g_sw_ehci[usbc_no];
	if (sw_ehci == NULL)
		return -EFAULT;

	if (sw_ehci->host_init_state)
		return -ENOSYS;

	if (sw_ehci->probe == 1) /* already enabled */
		return 0;

	sw_ehci->probe = 1;
	pr_info("[%s]: enable ehci\n", sw_ehci->hci_name);
	sw_ehci_hcd_probe(sw_ehci->pdev);

	return 0;
}
EXPORT_SYMBOL(sw_usb_enable_ehci);

void sw_ehci_hcd_shutdown(struct platform_device *pdev)
{
	struct sw_hci_hcd *sw_ehci = NULL;

	if (pdev == NULL)
		return;

	sw_ehci = pdev->dev.platform_data;
	if (sw_ehci == NULL)
		return;

	if (sw_ehci->probe == 0)
		return;

	pr_info("[%s]: shutdown start\n", sw_ehci->hci_name);
	usb_hcd_platform_shutdown(pdev);
	sw_stop_ehci(sw_ehci);
	pr_info("[%s]: shutdown end\n", sw_ehci->hci_name);

	return;
}

#ifdef CONFIG_PM
static int sw_ehci_hcd_suspend(struct device *dev)
{
	struct sw_hci_hcd *sw_ehci = NULL;
	struct usb_hcd *hcd = NULL;
	struct ehci_hcd *ehci = NULL;
	unsigned long flags = 0;

	if (dev == NULL)
		return 0;

	hcd = dev_get_drvdata(dev);
	if (hcd == NULL)
		return 0;

	sw_ehci = dev->platform_data;
	if (sw_ehci == NULL)
		return 0;

	if (sw_ehci->probe == 0)
		return 0;

	ehci = hcd_to_ehci(hcd);
	if (ehci == NULL)
		return 0;

	pr_info("[%s]: suspend\n", sw_ehci->hci_name);

	spin_lock_irqsave(&ehci->lock, flags);
	ehci_prepare_ports_for_controller_suspend(ehci, device_may_wakeup(dev));
	ehci_writel(ehci, 0, &ehci->regs->intr_enable);
	(void)ehci_readl(ehci, &ehci->regs->intr_enable);

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	spin_unlock_irqrestore(&ehci->lock, flags);
	sw_stop_ehci(sw_ehci);

	return 0;
}

static int sw_ehci_hcd_resume(struct device *dev)
{
	struct sw_hci_hcd *sw_ehci = NULL;
	struct usb_hcd *hcd = NULL;
	struct ehci_hcd *ehci = NULL;

	if (dev == NULL)
		return 0;

	hcd = dev_get_drvdata(dev);
	if (hcd == NULL)
		return 0;

	sw_ehci = dev->platform_data;
	if (sw_ehci == NULL)
		return 0;

	if (sw_ehci->probe == 0)
		return 0;

	ehci = hcd_to_ehci(hcd);
	if (ehci == NULL)
		return 0;

	pr_info("[%s]: resume\n", sw_ehci->hci_name);
	sw_start_ehci(sw_ehci);

	/* Mark hardware accessible again as we are out of D3 state by now */
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	if (ehci_readl(ehci, &ehci->regs->configured_flag) == FLAG_CF) {
		int mask = INTR_MASK;

		ehci_prepare_ports_for_controller_resume(ehci);

		if (!hcd->self.root_hub->do_remote_wakeup)
			mask &= ~STS_PCD;

		ehci_writel(ehci, mask, &ehci->regs->intr_enable);
		ehci_readl(ehci, &ehci->regs->intr_enable);

		return 0;
	}

	pr_info("[%s]: hci lost power, restarting\n", sw_ehci->hci_name);
	usb_root_hub_lost_power(hcd->self.root_hub);

	/* Else reset, to cope with power loss or flush-to-storage
	 * style "resume" having let BIOS kick in during reboot.
	 */
	(void) ehci_halt(ehci);
	(void) ehci_reset(ehci);

	/* emptying the schedule aborts any urbs */
	spin_lock_irq(&ehci->lock);
	if (ehci->reclaim)
		end_unlink_async(ehci);
	ehci_work(ehci);
	spin_unlock_irq(&ehci->lock);

	ehci_writel(ehci, ehci->command, &ehci->regs->command);
	ehci_writel(ehci, FLAG_CF, &ehci->regs->configured_flag);
	ehci_readl(ehci, &ehci->regs->command);	/* unblock posted writes */

	/* here we "know" root ports should always stay powered */
	ehci_port_power(ehci, 1);

	hcd->state = HC_STATE_SUSPENDED;

	return 0;
}

static const struct dev_pm_ops aw_ehci_pmops = {
	.suspend = sw_ehci_hcd_suspend,
	.resume = sw_ehci_hcd_resume,
};

#define SW_EHCI_PMOPS (&aw_ehci_pmops)
#else
#define SW_EHCI_PMOPS NULL
#endif

static struct platform_driver sw_ehci_hcd_driver = {
	.probe = sw_ehci_hcd_probe,
	.remove = sw_ehci_hcd_remove,
	.shutdown = sw_ehci_hcd_shutdown,
	.driver = {
		.name = SW_EHCI_NAME,
		.owner = THIS_MODULE,
		.pm = SW_EHCI_PMOPS,
	},
};
