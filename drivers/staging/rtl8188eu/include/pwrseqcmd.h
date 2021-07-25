/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __HALPWRSEQCMD_H__
#define __HALPWRSEQCMD_H__

#include <drv_types.h>

/* The value of cmd: 4 bits */
#define PWR_CMD_WRITE		0x01
#define PWR_CMD_POLLING		0x02
#define PWR_CMD_DELAY		0x03
#define PWR_CMD_END		0x04

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

u8 rtl88eu_pwrseqcmdparsing(struct adapter *padapter, struct wl_pwr_cfg pwrcfgCmd[]);

#endif
