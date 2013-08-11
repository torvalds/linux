/*
 * drivers/usb/host/sw_hci_sunxi.h: header file for SUNXI HCI HCD
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * Author: yangnaitian
 * History (author, date, version, notes):
 *	yangnaitian	2011-5-24	1.0	create this file
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

#ifndef __SW_HCI_SUNXI_H__
#define __SW_HCI_SUNXI_H__

#include <mach/irqs.h>

#define  DMSG_PRINT(stuff...)	printk(stuff)
#define  DMSG_ERR(...) \
	(DMSG_PRINT("WRN:L%d(%s):", __LINE__, __FILE__), \
	DMSG_PRINT(__VA_ARGS__))

#if 0
#define DMSG_DEBUG	DMSG_PRINT
#else
#define DMSG_DEBUG(...)
#endif

#if 1
#define DMSG_INFO	DMSG_PRINT
#else
#define DMSG_INFO(...)
#endif

#if	1
#define DMSG_PANIC	DMSG_ERR
#else
#define DMSG_PANIC(...)
#endif

#define SW_VA_CCM_USBCLK_OFFSET			0xcc
#define SW_VA_CCM_AHBMOD_OFFSET			0x60

#define SW_USB1_BASE		0x01c14000
#define SW_USB2_BASE		0x01c1c000

#define SW_USB_EHCI_BASE_OFFSET	0x00
#define SW_USB_OHCI_BASE_OFFSET	0x400
#define SW_USB_EHCI_LEN			0x100
#define SW_USB_OHCI_LEN			0x100
#define SW_USB_PMU_IRQ_ENABLE	0x800

/* ABH Gating Reg0 */
#define SW_CCMU_BP_AHB_GATING_USBC2		2
#define SW_CCMU_BP_AHB_GATING_USBC1		1

/* usb clock reg */
#define SW_CCMU_BP_USB_CLK_GATING_USBPHY	8
#define SW_CCMU_BP_USB_CLK_GATING_OHCI1		7
#define SW_CCMU_BP_USB_CLK_GATING_OHCI0		6
#define SW_CCMU_BP_USB_CLK_48M_SEL		4
#define SW_CCMU_BP_USB_CLK_USBPHY2_RST		2
#define SW_CCMU_BP_USB_CLK_USBPHY1_RST		1
#define SW_CCMU_BP_USB_CLK_USBPHY0_RST		0

#define SW_SDRAM_REG_HPCR_USB1	(0x250 + ((1 << 2) * 4))
#define SW_SDRAM_REG_HPCR_USB2	(0x250 + ((1 << 2) * 5))

/* HPCR */
#define SW_SDRAM_BP_HPCR_READ_CNT_EN		31
#define SW_SDRAM_BP_HPCR_RWRITE_CNT_EN		30
#define SW_SDRAM_BP_HPCR_COMMAND_NUM		8
#define SW_SDRAM_BP_HPCR_WAIT_STATE		4
#define SW_SDRAM_BP_HPCR_PRIORITY_LEVEL		2
#define SW_SDRAM_BP_HPCR_ACCESS_EN		0

struct sw_hci_hcd {
	__u32 usbc_no; /* usb controller number */
	char hci_name[32]; /* hci name */

	void __iomem *usb_vbase; /* USB base address */

	struct platform_device *pdev;
	struct usb_hcd *hcd;

	struct clk *sie_clk; /* SIE clock handle */
	struct clk *phy_gate; /* PHY clock handle */
	struct clk *phy_reset; /* PHY reset handle */
	struct clk *ohci_gate; /* ohci clock handle */
	__u32 clk_is_open; /* is usb clock open */

	u32 drv_vbus_Handle;
	user_gpio_set_t drv_vbus_gpio_set;
	__u32 power_flag; /* flag. 是否供电*/
	__u32 used; /* flag. 控制器是否被使用*/
	__u32 probe; /* 控制器初始化 */
	__u32 host_init_state; /* usb 控制器的初始化状态。0 : 不工作. 1 : 工作*/
				/* 0 not initialized, 1 otherwise */

	int (*open_clock) (struct sw_hci_hcd *sw_hci, u32 ohci);
	int (*close_clock) (struct sw_hci_hcd *sw_hci, u32 ohci);
	void (*set_power) (struct sw_hci_hcd *sw_hci, int is_on);
	void (*port_configure) (struct sw_hci_hcd *sw_hci, u32 enable);
	void (*usb_passby) (struct sw_hci_hcd *sw_hci, u32 enable);
};

extern int sunxi_hcd_map_urb_for_dma(struct usb_hcd *hcd, struct urb *urb,
				     gfp_t mem_flags);
extern void sunxi_hcd_unmap_urb_for_dma(struct usb_hcd *hcd, struct urb *urb);

#endif /* __SW_HCI_SUNXI_H__ */
