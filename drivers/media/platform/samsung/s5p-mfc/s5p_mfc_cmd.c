// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * linux/drivers/media/platform/samsung/s5p-mfc/s5p_mfc_cmd.c
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 */

#include "s5p_mfc_cmd.h"
#include "s5p_mfc_common.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_cmd_v5.h"
#include "s5p_mfc_cmd_v6.h"

void s5p_mfc_init_hw_cmds(struct s5p_mfc_dev *dev)
{
	if (IS_MFCV6_PLUS(dev))
		dev->mfc_cmds = s5p_mfc_init_hw_cmds_v6();
	else
		dev->mfc_cmds = s5p_mfc_init_hw_cmds_v5();
}
