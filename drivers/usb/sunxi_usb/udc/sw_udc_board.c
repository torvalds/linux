/*
 * drivers/usb/sunxi_usb/udc/sw_udc_board.c
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

#include  <mach/clock.h>
#include  "sw_udc_config.h"
#include  "sw_udc_board.h"

//---------------------------------------------------------------
//  宏 定义
//---------------------------------------------------------------

#define res_size(_r) (((_r)->end - (_r)->start) + 1)

#if 1
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

	if(sw_udc_io->sie_clk && sw_udc_io->phy_clk && sw_udc_io->phy0_clk && !sw_udc_io->clk_is_open){
	   	clk_enable(sw_udc_io->sie_clk);
		mdelay(10);

	    clk_enable(sw_udc_io->phy_clk);
	    clk_enable(sw_udc_io->phy0_clk);
	    clk_reset(sw_udc_io->phy0_clk, 0);
		mdelay(10);

		sw_udc_io->clk_is_open = 1;
	}else{
		DMSG_PANIC("ERR: clock handle is null, sie_clk(0x%p), phy_clk(0x%p), phy0_clk(0x%p), open(%d)\n",
			       sw_udc_io->sie_clk, sw_udc_io->phy_clk, sw_udc_io->phy0_clk, sw_udc_io->clk_is_open);
	}

	UsbPhyInit(0);

#if 0
	DMSG_INFO("[udc0]: open, 0x60(0x%x), 0xcc(0x%x)\n",
		      (u32)USBC_Readl(SW_VA_CCM_IO_BASE + 0x60),
		      (u32)USBC_Readl(SW_VA_CCM_IO_BASE + 0xcc));
#endif

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

	if(sw_udc_io->sie_clk && sw_udc_io->phy_clk && sw_udc_io->phy0_clk && sw_udc_io->clk_is_open){
		sw_udc_io->clk_is_open = 0;

	    clk_reset(sw_udc_io->phy0_clk, 1);
	    clk_disable(sw_udc_io->phy0_clk);
	    clk_disable(sw_udc_io->phy_clk);
	    clk_disable(sw_udc_io->sie_clk);
	}else{
		DMSG_PANIC("ERR: clock handle is null, sie_clk(0x%p), phy_clk(0x%p), phy0_clk(0x%p), open(%d)\n",
			       sw_udc_io->sie_clk, sw_udc_io->phy_clk, sw_udc_io->phy0_clk, sw_udc_io->clk_is_open);
	}

#if 0
	DMSG_INFO("[udc0]: close, 0x60(0x%x), 0xcc(0x%x)\n",
		      (u32)USBC_Readl(SW_VA_CCM_IO_BASE + 0x60),
		      (u32)USBC_Readl(SW_VA_CCM_IO_BASE + 0xcc));
#endif

	return 0;
}

#else

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
    u32 reg_value = 0;
    u32 ccmu_base = SW_VA_CCM_IO_BASE;

 	DMSG_INFO_UDC("open_usb_clock\n");

    //Gating AHB clock for USB_phy0
	reg_value = USBC_Readl(ccmu_base + 0x60);
	reg_value |= (1 << 0);	            /* AHB clock gate usb0 */
	USBC_Writel(reg_value, (ccmu_base + 0x60));

    //delay to wati SIE stable
	reg_value = 10000;
	while(reg_value--);

	//Enable module clock for USB phy0
	reg_value = USBC_Readl(ccmu_base + 0xcc);
	reg_value |= (1 << 8);
	reg_value |= (1 << 0);          //disable reset
	USBC_Writel(reg_value, (ccmu_base + 0xcc));

	//delay some time
	reg_value = 10000;
	while(reg_value--);
  sw_udc_io->clk_is_open = 1;
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
    u32 reg_value = 0;
    u32 ccmu_base = SW_VA_CCM_IO_BASE;

	DMSG_INFO_UDC("close_usb_clock\n");

    //Gating AHB clock for USB_phy0
	reg_value = USBC_Readl(ccmu_base + 0x60);
	reg_value &= ~(1 << 0);	            /* AHB clock gate usb0 */
	USBC_Writel(reg_value, (ccmu_base + 0x60));

    //等sie的时钟变稳
	reg_value = 10000;
	while(reg_value--);

	//Enable module clock for USB phy0
	reg_value = USBC_Readl(ccmu_base + 0xcc);
	reg_value &= ~(1 << 8);
	reg_value &= ~(1 << 0);          //disable reset
	USBC_Writel(reg_value, (ccmu_base + 0xcc));

	//延时
	reg_value = 10000;
	while(reg_value--);
  sw_udc_io->clk_is_open = 0;
	return 0;

}

#endif

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
	spinlock_t lock;
	unsigned long flags = 0;

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

	sw_udc_io->phy_clk = clk_get(NULL, "usb_phy");
	if (IS_ERR(sw_udc_io->phy_clk)){
		DMSG_PANIC("ERR: get usb phy clk failed.\n");
		goto io_failed;
	}

	sw_udc_io->phy0_clk = clk_get(NULL, "usb_phy0");
	if (IS_ERR(sw_udc_io->phy0_clk)){
		DMSG_PANIC("ERR: get usb phy0 clk failed.\n");
		goto io_failed;
	}

	open_usb_clock(sw_udc_io);

    /* initialize usb bsp */
	sw_udc_bsp_init(usbc_no, sw_udc_io);

	/* config usb fifo */
	spin_lock_init(&lock);
	spin_lock_irqsave(&lock, flags);
	USBC_ConfigFIFO_Base(sw_udc_io->usb_bsp_hdle, (u32)sw_udc_io->sram_vbase, USBC_FIFO_MODE_8K);
	spin_unlock_irqrestore(&lock, flags);

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

	if(sw_udc_io->phy0_clk){
		clk_put(sw_udc_io->phy0_clk);
		sw_udc_io->phy0_clk = NULL;
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

	if(sw_udc_io->phy0_clk){
		clk_put(sw_udc_io->phy0_clk);
		sw_udc_io->phy0_clk = NULL;
	}

	sw_udc_io->usb_vbase  = NULL;
	sw_udc_io->sram_vbase = NULL;

	return 0;
}





