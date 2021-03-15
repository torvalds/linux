/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __HALPWRSEQCMD_H__
#define __HALPWRSEQCMD_H__

#include <drv_types.h>

/*---------------------------------------------*/
/* 3 The value of cmd: 4 bits */
/*---------------------------------------------*/
#define PWR_CMD_READ			0x00
     /*  offset: the read register offset */
     /*  msk: the mask of the read value */
     /*  value: N/A, left by 0 */
     /*  note: dirver shall implement this function by read & msk */

#define PWR_CMD_WRITE			0x01
     /*  offset: the read register offset */
     /*  msk: the mask of the write bits */
     /*  value: write value */
     /*  note: driver shall implement this cmd by read & msk after write */

#define PWR_CMD_POLLING			0x02
     /*  offset: the read register offset */
     /*  msk: the mask of the polled value */
     /*  value: the value to be polled, masked by the msd field. */
     /*  note: driver shall implement this cmd by */
     /*  do{ */
     /*  if ((Read(offset) & msk) == (value & msk)) */
     /*  break; */
     /*  } while (not timeout); */

#define PWR_CMD_DELAY			0x03
     /*  offset: the value to delay */
     /*  msk: N/A */
     /*  value: the unit of delay, 0: us, 1: ms */

#define PWR_CMD_END				0x04
     /*  offset: N/A */
     /*  msk: N/A */
     /*  value: N/A */

/*---------------------------------------------*/
/* 3 The value of base: 4 bits */
/*---------------------------------------------*/
   /*  define the base address of each block */
#define PWR_BASEADDR_MAC		0x00
#define PWR_BASEADDR_USB		0x01
#define PWR_BASEADDR_PCIE		0x02
#define PWR_BASEADDR_SDIO		0x03

/*---------------------------------------------*/
/* 3 The value of interface_msk: 4 bits */
/*---------------------------------------------*/
#define	PWR_INTF_SDIO_MSK		BIT(0)
#define	PWR_INTF_USB_MSK		BIT(1)
#define	PWR_INTF_PCI_MSK		BIT(2)
#define	PWR_INTF_ALL_MSK		(BIT(0)|BIT(1)|BIT(2)|BIT(3))

/*---------------------------------------------*/
/* 3 The value of fab_msk: 4 bits */
/*---------------------------------------------*/
#define	PWR_FAB_TSMC_MSK		BIT(0)
#define	PWR_FAB_UMC_MSK			BIT(1)
#define	PWR_FAB_ALL_MSK			(BIT(0)|BIT(1)|BIT(2)|BIT(3))

/*---------------------------------------------*/
/* 3 The value of cut_msk: 8 bits */
/*---------------------------------------------*/
#define	PWR_CUT_TESTCHIP_MSK	BIT(0)
#define	PWR_CUT_A_MSK			BIT(1)
#define	PWR_CUT_B_MSK			BIT(2)
#define	PWR_CUT_C_MSK			BIT(3)
#define	PWR_CUT_D_MSK			BIT(4)
#define	PWR_CUT_E_MSK			BIT(5)
#define	PWR_CUT_F_MSK			BIT(6)
#define	PWR_CUT_G_MSK			BIT(7)
#define	PWR_CUT_ALL_MSK			0xFF


enum PWRSEQ_CMD_DELAY_UNIT {
	PWRSEQ_DELAY_US,
	PWRSEQ_DELAY_MS,
};

struct WLAN_PWR_CFG {
	u16 offset;
	u8 cut_msk;
	u8 fab_msk:4;
	u8 interface_msk:4;
	u8 base:4;
	u8 cmd:4;
	u8 msk;
	u8 value;
};


#define GET_PWR_CFG_OFFSET(__PWR_CMD)		__PWR_CMD.offset
#define GET_PWR_CFG_CUT_MASK(__PWR_CMD)		__PWR_CMD.cut_msk
#define GET_PWR_CFG_FAB_MASK(__PWR_CMD)		__PWR_CMD.fab_msk
#define GET_PWR_CFG_INTF_MASK(__PWR_CMD)	__PWR_CMD.interface_msk
#define GET_PWR_CFG_BASE(__PWR_CMD)			__PWR_CMD.base
#define GET_PWR_CFG_CMD(__PWR_CMD)			__PWR_CMD.cmd
#define GET_PWR_CFG_MASK(__PWR_CMD)			__PWR_CMD.msk
#define GET_PWR_CFG_VALUE(__PWR_CMD)		__PWR_CMD.value


/*  */
/* 	Prototype of protected function. */
/*  */
u8 HalPwrSeqCmdParsing(
	struct adapter *padapter,
	u8 		CutVersion,
	u8 		FabVersion,
	u8 		InterfaceType,
	struct WLAN_PWR_CFG	PwrCfgCmd[]);

#endif
