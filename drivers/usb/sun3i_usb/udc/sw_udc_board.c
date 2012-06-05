/*
 * drivers/usb/sun3i_usb/udc/sw_udc_board.c
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>

#include  "sw_udc_config.h"
#include  "sw_udc_board.h"

//---------------------------------------------------------------
//  宏 定义
//---------------------------------------------------------------

#define res_size(_r) (((_r)->end - (_r)->start) + 1)

/*
*******************************************************************************
*                     open_usb_clock
*
* Description:
*
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
u32  open_usb_clock(sw_udc_io_t *sw_udc_io)
{
 	DMSG_INFO_UDC("open_usb_clock\n");

	if(sw_udc_io->sie_clk && sw_udc_io->phy_clk && !sw_udc_io->clk_is_open){
	   	clk_enable(sw_udc_io->sie_clk);
		msleep(10);

	    clk_enable(sw_udc_io->phy_clk);
	    clk_reset(sw_udc_io->phy_clk, 0);
		msleep(10);

		sw_udc_io->clk_is_open = 1;
	}else{
		DMSG_PANIC("ERR: clock handle is null, sie_clk(0x%p), phy_clk(0x%p), open(%d)\n",
			       sw_udc_io->sie_clk, sw_udc_io->phy_clk, sw_udc_io->clk_is_open);
	}

	return 0;
}

/*
*******************************************************************************
*                     close_usb_clock
*
* Description:
*
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
u32 close_usb_clock(sw_udc_io_t *sw_udc_io)
{
	DMSG_INFO_UDC("close_usb_clock\n");

	if(sw_udc_io->sie_clk && sw_udc_io->phy_clk && sw_udc_io->clk_is_open){
	    clk_disable(sw_udc_io->sie_clk);
	    clk_disable(sw_udc_io->phy_clk);
	    clk_reset(sw_udc_io->phy_clk, 1);
		sw_udc_io->clk_is_open = 0;
	}else{
		DMSG_PANIC("ERR: clock handle is null, sie_clk(0x%p), phy_clk(0x%p), open(%d)\n",
			       sw_udc_io->sie_clk, sw_udc_io->phy_clk, sw_udc_io->clk_is_open);
	}

	return 0;
}

u32 close_usb_clock_ex(sw_udc_io_t *sw_udc_io)
{
	DMSG_INFO_UDC("close_usb_clock_ex\n");

	if(sw_udc_io->sie_clk && sw_udc_io->phy_clk){
	    clk_disable(sw_udc_io->sie_clk);
	    clk_disable(sw_udc_io->phy_clk);
	    clk_reset(sw_udc_io->phy_clk, 1);
	}

	return 0;
}


/*
*******************************************************************************
*                     sw_udc_bsp_init
*
* Description:
*    initialize usb bsp
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
__s32 sw_udc_bsp_init(__u32 usbc_no, sw_udc_io_t *sw_udc_io)
{
	sw_udc_io->usbc.usbc_info[usbc_no].num = usbc_no;
   	sw_udc_io->usbc.usbc_info[usbc_no].base = (u32)sw_udc_io->usb_vbase;
	sw_udc_io->usbc.sram_base = (u32)sw_udc_io->sram_vbase;

//	USBC_init(&sw_udc_io->usbc);
	sw_udc_io->usb_bsp_hdle = USBC_open_otg(usbc_no);
	if(sw_udc_io->usb_bsp_hdle == 0){
		DMSG_PANIC("ERR: sw_udc_init: USBC_open_otg failed\n");
		return -1;
	}

	USBC_EnhanceSignal(sw_udc_io->usb_bsp_hdle);

	USBC_EnableDpDmPullUp(sw_udc_io->usb_bsp_hdle);
    USBC_EnableIdPullUp(sw_udc_io->usb_bsp_hdle);
	USBC_ForceId(sw_udc_io->usb_bsp_hdle, USBC_ID_TYPE_DEVICE);
	USBC_ForceVbusValid(sw_udc_io->usb_bsp_hdle, USBC_VBUS_TYPE_HIGH);

	USBC_SelectBus(sw_udc_io->usb_bsp_hdle, USBC_IO_TYPE_PIO, 0, 0);

    return 0;
}

/*
*******************************************************************************
*                     sw_udc_bsp_exit
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
__s32 sw_udc_bsp_exit(__u32 usbc_no, sw_udc_io_t *sw_udc_io)
{
    USBC_DisableDpDmPullUp(sw_udc_io->usb_bsp_hdle);
    USBC_DisableIdPullUp(sw_udc_io->usb_bsp_hdle);
	USBC_ForceId(sw_udc_io->usb_bsp_hdle, USBC_ID_TYPE_DISABLE);
	USBC_ForceVbusValid(sw_udc_io->usb_bsp_hdle, USBC_VBUS_TYPE_DISABLE);

	USBC_close_otg(sw_udc_io->usb_bsp_hdle);
	sw_udc_io->usb_bsp_hdle = 0;

//	USBC_exit(&sw_udc_io->usbc);

    return 0;
}

/*
*******************************************************************************
*                     sw_udc_io_init
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
__s32 sw_udc_io_init(__u32 usbc_no, struct platform_device *pdev, sw_udc_io_t *sw_udc_io)
{
	__s32 ret = 0;

	sw_udc_io->usb_vbase  = (void __iomem *)SW_VA_USB0_IO_BASE;
	sw_udc_io->sram_vbase = (void __iomem *)SW_VA_SRAM_IO_BASE;

	DMSG_INFO_UDC("usb_vbase  = 0x%x\n", (u32)sw_udc_io->usb_vbase);
	DMSG_INFO_UDC("sram_vbase = 0x%x\n", (u32)sw_udc_io->sram_vbase);

    /* open usb lock */
	sw_udc_io->sie_clk = clk_get(NULL, "ahb_usb0");
	if (IS_ERR(sw_udc_io->sie_clk)){
		DMSG_PANIC("ERR: get usb sie clk failed.\n");
		goto io_failed;
	}

	sw_udc_io->phy_clk = clk_get(NULL, "usb_phy0");
	if (IS_ERR(sw_udc_io->phy_clk)){
		DMSG_PANIC("ERR: get usb phy clk failed.\n");
		goto io_failed;
	}

	close_usb_clock_ex(sw_udc_io);
	open_usb_clock(sw_udc_io);

    /* initialize usb bsp */
	sw_udc_bsp_init(usbc_no, sw_udc_io);

	/* config usb fifo */
	USBC_ConfigFIFO_Base(sw_udc_io->usb_bsp_hdle, (u32)sw_udc_io->sram_vbase, USBC_FIFO_MODE_8K);

	return 0;

io_failed:
	if(sw_udc_io->sie_clk){
		clk_put(sw_udc_io->sie_clk);
		sw_udc_io->sie_clk = NULL;
	}

	if(sw_udc_io->phy_clk){
		clk_put(sw_udc_io->phy_clk);
		sw_udc_io->phy_clk = NULL;
	}

	return ret;
}

/*
*******************************************************************************
*                     sw_udc_exit
*
* Description:
*    void
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
__s32 sw_udc_io_exit(__u32 usbc_no, struct platform_device *pdev, sw_udc_io_t *sw_udc_io)
{
	sw_udc_bsp_exit(usbc_no, sw_udc_io);

	close_usb_clock(sw_udc_io);

	if(sw_udc_io->sie_clk){
		clk_put(sw_udc_io->sie_clk);
		sw_udc_io->sie_clk = NULL;
	}

	if(sw_udc_io->phy_clk){
		clk_put(sw_udc_io->phy_clk);
		sw_udc_io->phy_clk = NULL;
	}

	sw_udc_io->usb_vbase  = NULL;
	sw_udc_io->sram_vbase = NULL;

	return 0;
}





