/*
 * drivers/block/sunxi_nand/nfd/clk_for_nand.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
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

#include "nand_private.h"

extern struct __NandDriverGlobal_t NandDriverInfo;

//__krnl_event_t      *nand_clk_adjust_sem;
static __hdle       h_nandmclk, h_nandahbclk, h_dram_access;
static __u32        clk_adjust_lock;


__s32 NAND_SetClk(__u32 nand_clk)
{

//	__u32 ahb_clk, cmu_clk;
//	__u32 edo_clk;
//	__u32 nand_clk_divid_ratio;
//
//	/*get cmu clk and ahb clk*/
//	cmu_clk = 270 * 1000000;
//	//ahb_clk = esCLK_GetSrcFreq(CCMU_SCLK_AHBCLK);
//	ahb_clk = 120*1000000;
//
//	/*configure nand clock divid ratio*/
//	edo_clk = nand_clk * 2 * 1000000;
//	nand_clk_divid_ratio = vedio_clk/ edo_clk;
//	if (nand_clk_divid_ratio){
//		if (vedio_clk % edo_clk)
//			nand_clk_divid_ratio++;
//		if (nand_clk_divid_ratio > 16)
//			nand_clk_divid_ratio = 16;
//	}
//	else
//	{
//		nand_clk_divid_ratio = 1;
//	}
//
//	/*ahb clk must be above nfc clk*/
//	while (ahb_clk <= ((vedio_clk>> 1)/nand_clk_divid_ratio))
//	{
//		nand_clk_divid_ratio++;
//	}
//	if (nand_clk_divid_ratio > 16)
//	{
//		//__wrn("ahb clk less than nfc clk : cmu clk = %dM,ahb clk = %dM, nand clk = %dM\n",
//		//	cmu_clk,ahb_clk,(cmu_clk >> 1)/nand_clk_divid_ratio);
//		return EPDK_FALSE;
//	}
//
//	//__inf("nand clock divid ratio = %d\n",nand_clk_divid_ratio);
//
//	/*set divid ratio*/
//	//nand_clk_divid_ratio--;
//	//eLIBs_printf("nand clock divid ratio = %d\n",nand_clk_divid_ratio);
//	esCLK_SetFreq(h_nandmclk, CCMU_SCLK_VIDEOPLL, nand_clk_divid_ratio);
//    esCLK_OnOff(h_nandmclk, CLK_ON);
//	esCLK_OnOff(h_nandahbclk, CLK_ON);

	return EPDK_OK;
}


__s32 cb_NAND_ClockChange(__u32 cmd, __s32 aux)
{
//    __s32       cpu_sr;
//
//    switch(cmd)
//    {
//        case CLK_CMD_SCLKCHG_REQ:
//        {
//            __u8    err;
//            __u32   tmpLock;
//
//            ENTER_CRITICAL(cpu_sr);
//            tmpLock = clk_adjust_lock;
//            clk_adjust_lock++;
//            EXIT_CRITICAL(cpu_sr);
//
//            if(!tmpLock)
//            {
//                /*wait current op finished*/
//                esKRNL_SemPend(nand_clk_adjust_sem,0,&err);
//            }
//
//            return EPDK_OK;
//        }
//
//        case CLK_CMD_SCLKCHG_DONE:
//        {
//            __u32   tmpLock;
//
//            ENTER_CRITICAL(cpu_sr);
//            clk_adjust_lock--;
//            tmpLock = clk_adjust_lock;
//            EXIT_CRITICAL(cpu_sr);
//
//            if(!tmpLock)
//            {
//                /*configure frequency*/
//                NAND_SetClk(NandStorageInfo.FrequencePar);
//                /*reset nand control machine*/
//                PHY_ChangeMode(1);
//                /*release sem*/
//                esKRNL_SemPost(nand_clk_adjust_sem);
//            }
//            return EPDK_OK;
//        }
//
//        default:
//        {
//            break;
//        }
//    }

    return EPDK_FAIL;
}


__s32 cb_NAND_DMA_DramAccess(__u32 cmd, __s32 aux)
{
//    __u8    err;
//
//    switch(cmd)
//    {
//        case CLK_CMD_SCLKCHG_REQ:
//        {
//            esKRNL_SemPend(nand_clk_adjust_sem,0,&err);
//            return EPDK_OK;
//        }
//
//        case CLK_CMD_SCLKCHG_DONE:
//        {
//            esKRNL_SemPost(nand_clk_adjust_sem);
//            return EPDK_OK;
//        }
//
//        default:
//        {
//            break;
//        }
//    }

    return EPDK_FAIL;
}


__s32 NAND_OpenClk(void)
{
//	__u8 err;
//	/*create sem*/
//	nand_clk_adjust_sem = esKRNL_SemCreate(1);
//
//	/*open 320 clk*/
//	h_nandmclk  = esCLK_Open(CCMU_MCLK_NAND, cb_NAND_ClockChange, &err);
//	h_nandahbclk = esCLK_Open(CCMU_MCLK_AHB_NAND, cb_NAND_ClockChange, &err);
//    //register dram access for dma
//    h_dram_access = esMEM_RegDramAccess(DRAM_DEVICE_DMA, cb_NAND_DMA_DramAccess);
//
//    clk_adjust_lock = 0;

	return EPDK_OK;
}
__s32 NAND_CloseClk(void)
{
//	__u8 err;
//
//    clk_adjust_lock = 0;
//
//    if(h_dram_access)
//    {
//        esMEM_UnRegDramAccess(h_dram_access);
//        h_dram_access = 0;
//    }
//	esKRNL_SemDel(nand_clk_adjust_sem,OS_DEL_ALWAYS,&err);
//	esCLK_Close(h_nandmclk);
//	esCLK_Close(h_nandahbclk);

	return EPDK_OK;
}



