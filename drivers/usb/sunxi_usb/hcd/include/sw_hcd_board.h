/*
 * drivers/usb/sunxi_usb/hcd/include/sw_hcd_board.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen <javen@allwinnertech.com>
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

#ifndef  __SW_HCD_BOARD_H__
#define  __SW_HCD_BOARD_H__

#include <plat/sys_config.h>

#define  USB_SRAMC_BASE	            0x01c00000
#define  USB_CLOCK_BASE				0x01C20000
#define  USB_PIO_BASE	            0x01c20800

/* i/o 信息 */
typedef struct sw_hcd_io{
	struct resource	*usb_base_res;   	/* USB  resources 		*/
	struct resource	*usb_base_req;   	/* USB  resources 		*/
	void __iomem	*usb_vbase;			/* USB  base address 	*/

	struct resource	*sram_base_res;   	/* SRAM resources 		*/
	struct resource	*sram_base_req;   	/* SRAM resources 		*/
	void __iomem	*sram_vbase;		/* SRAM base address 	*/

	struct resource	*clock_base_res;   	/* clock resources 		*/
	struct resource	*clock_base_req;   	/* clock resources 		*/
	void __iomem	*clock_vbase;		/* clock base address 	*/

	struct resource	*pio_base_res;   	/* pio resources 		*/
	struct resource	*pio_base_req;   	/* pio resources 		*/
	void __iomem	*pio_vbase;			/* pio base address 	*/

	bsp_usbc_t usbc;					/* usb bsp config 		*/
	__hdle usb_bsp_hdle;				/* usb bsp handle 		*/

	__u32 clk_is_open;					/* is usb clock open? 	*/
	struct clk	*sie_clk;				/* SIE clock handle 	*/
	struct clk	*phy_clk;				/* PHY gate 			*/
	struct clk	*phy0_clk;				/* PHY reset 			*/

	unsigned Drv_vbus_Handle;
	user_gpio_set_t drv_vbus_gpio_set;
	__u32 host_init_state;				/* usb 控制器的初始化状态。0 : 不工作. 1 : 工作 */
	__u32 usb_enable;
}sw_hcd_io_t;

#endif   //__SW_HCD_BOARD_H__


