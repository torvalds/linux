/*
 * arch/arm/plat-sunxi/pm/standby/dram.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Berg Xing <bergxing@allwinnertech.com>
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

#include "dram_i.h"


/*
*********************************************************************************************************
*                 DRAM ENTER SELF REFRESH
*
* Description: dram enter/exit self-refresh;
*
* Arguments  : none
*
* Returns    : none
*
* Note       :
*********************************************************************************************************
*/
void mctl_precharge_all(void)
{
	__u32 i;
	__u32 reg_val;

	reg_val = mctl_read_w(SDR_DCR);
	reg_val &= ~(0x1fU<<27);
	reg_val |= 0x15U<<27;
	mctl_write_w(SDR_DCR, reg_val);

	//check whether command has been executed
	while( mctl_read_w(SDR_DCR)& (0x1U<<31) );
    standby_delay(0x100);
}

void DRAMC_enter_selfrefresh(void)
{
	__u32 i;
	__u32 reg_val;

	//disable all port
	for(i=0; i<31; i++)
	{
		DRAMC_hostport_on_off(i, 0x0);
	}

	//disable auto-fresh
	reg_val = mctl_read_w(SDR_DRR);
	reg_val |= 0x1U<<31;
	mctl_write_w(SDR_DRR, reg_val);

	//issue prechage all command
	mctl_precharge_all();

	//enter into self-refresh
	reg_val = mctl_read_w(SDR_DCR);
	reg_val &= ~(0x1fU<<27);
	reg_val |= 0x12U<<27;
	mctl_write_w(SDR_DCR, reg_val);
	while( mctl_read_w(SDR_DCR)& (0x1U<<31) );
	standby_delay(0x100);
}
void mctl_mode_exit(void)
{
	__u32 i;
	__u32 reg_val;

	reg_val = mctl_read_w(SDR_DCR);
	reg_val &= ~(0x1fU<<27);
	reg_val |= 0x17U<<27;
	mctl_write_w(SDR_DCR, reg_val);

	//check whether command has been executed
	while( mctl_read_w(SDR_DCR)& (0x1U<<31) );
	standby_delay(0x100);
}

void DRAMC_exit_selfrefresh(void)
{
	__u32 i;
	__u32 reg_val;

	//exit self-refresh state
	mctl_mode_exit();

	//issue a refresh command
	reg_val = mctl_read_w(SDR_DCR);
	reg_val &= ~(0x1fU<<27);
	reg_val |= 0x13U<<27;
	mctl_write_w(SDR_DCR, reg_val);
	while( mctl_read_w(SDR_DCR)& (0x1U<<31) );
    standby_delay(0x100);

	//enable auto-fresh
	reg_val = mctl_read_w(SDR_DRR);
	reg_val &= ~(0x1U<<31);
	mctl_write_w(SDR_DRR, reg_val);

	//enable all port
	for(i=0; i<31; i++)
	{
		DRAMC_hostport_on_off(i, 0x1);
	}
}

/*
*********************************************************************************************************
*                 DRAM POWER DOWN
*
* Description: enter/exit dram power down state
*
* Arguments  :
*
* Returns    : none;
*
* Note       :
*********************************************************************************************************
*/
void DRAMC_enter_power_down(void)
{
	__u32 i;
	__u32 reg_val;

	reg_val = mctl_read_w(SDR_DCR);
	reg_val &= ~(0x1fU<<27);
	reg_val |= 0x1eU<<27;
	mctl_write_w(SDR_DCR, reg_val);

	//check whether command has been executed
	while( mctl_read_w(SDR_DCR)& (0x1U<<31) );
	standby_delay(0x100);
}

void DRAMC_exit_power_down(void)
{
    mctl_mode_exit();
}

/*
**********************************************************************************************************************
*                 DRAM HOSTPORT CONTROL
*
* Description: dram host port enable/ disable
*
* Arguments  : __u32 port_idx		host port index   (0,1,...31)
*				__u32 on		enable or disable (0: diable, 1: enable)
*
* Returns    :
*
* Notes      :
*
**********************************************************************************************************************
*/
void DRAMC_hostport_on_off(__u32 port_idx, __u32 on)
{
    __u32   reg_val;

    if(port_idx<=31)
    {
	    reg_val = mctl_read_w(SDR_HPCR + (port_idx<<2));
	    if(on)
	    	reg_val |= 0x1;
	    else
	    	reg_val &= ~(0x1);
	    mctl_write_w(SDR_HPCR + (port_idx<<2), reg_val);
	}
}
/*
**********************************************************************************************************************
*                 DRAM GET HOSTPORT STATUS
*
* Description: dram get AHB FIFO status
*
* Arguments  : __u32 port_idx		host port index   	(0,1,...31)
*
* Returns    : __u32 ret_val		AHB FIFO status 	(0: FIFO not empty ,1: FIFO empty)
*
* Notes      :
*
**********************************************************************************************************************
*/
__u32 DRAMC_hostport_check_ahb_fifo_status(__u32 port_idx)
{
    __u32   reg_val;

    if(port_idx<=31)
    {
	    reg_val = mctl_read_w(SDR_CFSR);
	    return ( (reg_val>>port_idx)&0x1 );
	}
	else
	{
		return 0;
	}
}
/*
**********************************************************************************************************************
*                 DRAM GET HOSTPORT STATUS
*
* Description: dram get AHB FIFO status
*
* Arguments  : 	__u32 port_idx				host port index   	(0,1,...31)
*				__u32 port_pri_level		priority level		(0,1,2,3)
*
* Returns    :
*
* Notes      :
*
**********************************************************************************************************************
*/
void DRAMC_hostport_setup(__u32 port_idx, __u32 port_pri_level, __u32 port_wait_cycle, __u32 cmd_num)
{
    __u32   reg_val;

    if(port_idx<=31)
    {
	    reg_val = mctl_read_w(SDR_HPCR + (port_idx<<2));
	    reg_val &= ~(0x3<<2);
	    reg_val |= (port_pri_level&0x3)<<2;
	    reg_val &= ~(0xf<<4);
	    reg_val |= (port_wait_cycle&0xf)<<4;
	    reg_val &= ~(0xff<<8);
	    reg_val |= (cmd_num&0x3)<<8;
	    mctl_write_w(SDR_HPCR + (port_idx<<2), reg_val);
	}
}
/*
*********************************************************************************************************
*                 DRAM power save process
*
* Description: We can save power by disable DRAM PLL.
*			   DRAMC_power_save_process() is called to disable DRAMC ITM and DLL, then disable PLL to save power;
*			   Before exit SDRAM self-refresh state, we should enable DRAM PLL and make sure that it is stable clock.
*			   Then call function DRAMC_exit_selfrefresh() to exit self-refresh state. Before access external SDRAM,
*              the function DRAMC_power_up_process() should be called to enable DLL and re-training DRAM controller.
*
* Arguments  : none
*
* Returns    : none
*
* Note       :
*********************************************************************************************************
*/
__u32 mctl_ahb_reset(void)
{
	__u32 i;
	__u32 reg_val;
#if   defined(CONFIG_ARCH_SUN4I)
	const __u32 clocks = 0x1;
#elif defined(CONFIG_ARCH_SUN5I)
	const __u32 clocks = 0x3;
#else
#error Unsupported sunxi architecture.
#endif

	reg_val = mctl_read_w(DRAM_CCM_AHB_GATE_REG);
	reg_val &= ~(clocks << 14);
	mctl_write_w(DRAM_CCM_AHB_GATE_REG,reg_val);
	standby_delay(0x10);

	reg_val = mctl_read_w(DRAM_CCM_AHB_GATE_REG);
	reg_val |= (clocks << 14);
	mctl_write_w(DRAM_CCM_AHB_GATE_REG,reg_val);
}

__s32 DRAMC_retraining(void)
{
	__u32 i;
	__u32 reg_val;
	__u32 ret_val;
	__u32 reg_dcr, reg_drr, reg_tpr0, reg_tpr1, reg_tpr2, reg_mr, reg_emr, reg_emr2, reg_emr3;
	__u32 reg_zqcr0, reg_iocr;

	//remember register value
	reg_dcr = mctl_read_w(SDR_DCR);
	reg_drr = mctl_read_w(SDR_DRR);
	reg_tpr0 = mctl_read_w(SDR_TPR0);
	reg_tpr1 = mctl_read_w(SDR_TPR1);
	reg_tpr2 = mctl_read_w(SDR_TPR2);
	reg_mr = mctl_read_w(SDR_MR);
	reg_emr = mctl_read_w(SDR_EMR);
	reg_emr2 = mctl_read_w(SDR_EMR2);
	reg_emr3 = mctl_read_w(SDR_EMR3);
	reg_zqcr0 = mctl_read_w(SDR_ZQCR0);
	reg_iocr = mctl_read_w(SDR_IOCR);
	while(1){
		mctl_ahb_reset();

		//reset external DRAM
		mctl_ddr3_reset();
		mctl_set_drive();

		//dram clock off
		DRAMC_clock_output_en(0);

		//select dram controller 1
		mctl_write_w(SDR_SCSR, 0x16237495);

		mctl_itm_disable();
		mctl_enable_dll0();

		//configure external DRAM
		mctl_write_w(SDR_DCR, reg_dcr);

		//dram clock on
		DRAMC_clock_output_en(1);
        standby_delay(0x10);
		while(mctl_read_w(SDR_CCR) & (0x1U<<31)) {};

		mctl_enable_dllx();

		//set odt impendance divide ratio
		mctl_write_w(SDR_ZQCR0, reg_zqcr0);

		//set I/O configure register
		mctl_write_w(SDR_IOCR, reg_iocr);

		//set refresh period
		mctl_write_w(SDR_DRR, reg_drr);

		//set timing parameters
		mctl_write_w(SDR_TPR0, reg_tpr0);
		mctl_write_w(SDR_TPR1, reg_tpr1);
		mctl_write_w(SDR_TPR2, reg_tpr2);

		//set mode register
		mctl_write_w(SDR_MR, reg_mr);
		mctl_write_w(SDR_EMR, reg_emr);
		mctl_write_w(SDR_EMR2, reg_emr2);
		mctl_write_w(SDR_EMR3, reg_emr3);

		//set DQS window mode
		reg_val = mctl_read_w(SDR_CCR);
		reg_val |= 0x1U<<14;
		mctl_write_w(SDR_CCR, reg_val);

		//initial external DRAM
		reg_val = mctl_read_w(SDR_CCR);
		reg_val |= 0x1U<<31;
		mctl_write_w(SDR_CCR, reg_val);

		while(mctl_read_w(SDR_CCR) & (0x1U<<31)) {};

		//scan read pipe value
		mctl_itm_enable();
		ret_val = DRAMC_scan_readpipe();

		//configure all host port
		mctl_configure_hostport();

		if(ret_val == 0)
			return 0;
    }
}

void dram_power_save_process(void)
{
	__u32 reg_val;

	//put external SDRAM into self-fresh state
	DRAMC_enter_selfrefresh();

	//disable ITM
	mctl_itm_disable();

	//dramc clock off
	DRAMC_clock_output_en(0);

	//disable and reset all DLL
	mctl_disable_dll();
}
__u32 dram_power_up_process(void)
{
	__u32 i;
	__s32 ret_val;

	mctl_itm_disable();

	mctl_enable_dll0();

	//dram clock on
	DRAMC_clock_output_en(1);
    standby_delay(0x10);

	mctl_enable_dllx();

	//enable ITM
	mctl_itm_enable();

	//exit from self-refresh state
	DRAMC_exit_selfrefresh();

	//scan read pipe value
	ret_val = DRAMC_scan_readpipe();
	if(ret_val != 0)
	{
		DRAMC_retraining();
	}

	return (ret_val);
}


void dram_enter_selfrefresh(void)
{
    DRAMC_enter_selfrefresh();
}


void dram_exit_selfrefresh(void)
{
    DRAMC_exit_selfrefresh();
}


void dram_enter_power_down(void)
{
    DRAMC_enter_power_down();
}


void dram_exit_power_down(void)
{
    DRAMC_exit_power_down();
}


void dram_hostport_on_off(__u32 port_idx, __u32 on)
{
    DRAMC_hostport_on_off(port_idx, on);
}


__u32 dram_hostport_check_ahb_fifo_status(__u32 port_idx)
{
    return DRAMC_hostport_check_ahb_fifo_status(port_idx);
}


void dram_hostport_setup(__u32 port, __u32 prio, __u32 wait_cycle, __u32 cmd_num)
{
    DRAMC_hostport_setup(port, prio, wait_cycle, cmd_num);
}

