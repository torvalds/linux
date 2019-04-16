/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Wei WANG (wei_wang@realsil.com.cn)
 *   Micky Ching (micky_ching@realsil.com.cn)
 */

#ifndef __RTSX_SYS_H
#define __RTSX_SYS_H

#include "rtsx.h"
#include "rtsx_chip.h"
#include "rtsx_card.h"

static inline void rtsx_exclusive_enter_ss(struct rtsx_chip *chip)
{
	struct rtsx_dev *dev = chip->rtsx;

	spin_lock(&dev->reg_lock);
	rtsx_enter_ss(chip);
	spin_unlock(&dev->reg_lock);
}

static inline void rtsx_reset_detected_cards(struct rtsx_chip *chip, int flag)
{
	rtsx_reset_cards(chip);
}

#define RTSX_MSG_IN_INT(x)

#endif  /* __RTSX_SYS_H */

