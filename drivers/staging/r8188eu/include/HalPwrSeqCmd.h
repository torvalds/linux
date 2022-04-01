/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __HALPWRSEQCMD_H__
#define __HALPWRSEQCMD_H__

#include "drv_types.h"

/*---------------------------------------------*/
/* 3 The value of cmd: 4 bits */
/*---------------------------------------------*/

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
     /*  if ( (Read(offset) & msk) == (value & msk) ) */
     /*  break; */
     /*  } while (not timeout); */

#define PWR_CMD_DELAY			0x03
     /*  offset: the value to delay */
     /*  msk: N/A */
     /*  value: the unit of delay, 0: us, 1: ms */

#define PWR_CMD_END			0x04
     /*  offset: N/A */
     /*  msk: N/A */
     /*  value: N/A */

enum pwrseq_cmd_delat_unit {
	PWRSEQ_DELAY_US,
	PWRSEQ_DELAY_MS,
};

struct wl_pwr_cfg {
	u16 offset;
	u8 cmd:4;
	u8 msk;
	u8 value;
};

#define GET_PWR_CFG_OFFSET(__PWR_CMD)		__PWR_CMD.offset
#define GET_PWR_CFG_CMD(__PWR_CMD)		__PWR_CMD.cmd
#define GET_PWR_CFG_MASK(__PWR_CMD)		__PWR_CMD.msk
#define GET_PWR_CFG_VALUE(__PWR_CMD)		__PWR_CMD.value

/*	Prototype of protected function. */
u8 HalPwrSeqCmdParsing(struct adapter *padapter, struct wl_pwr_cfg PwrCfgCmd[]);

#endif
