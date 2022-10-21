/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __HALPWRSEQCMD_H__
#define __HALPWRSEQCMD_H__

#include "drv_types.h"

enum r8188eu_pwr_seq {
	PWR_ON_FLOW,
	DISABLE_FLOW,
	LPS_ENTER_FLOW,
};

/*	Prototype of protected function. */
u8 HalPwrSeqCmdParsing(struct adapter *padapter, enum r8188eu_pwr_seq seq);

#endif
