/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Amlogic A1 PLL Clock Controller internals
 *
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 * Author: Jian Hu <jian.hu@amlogic.com>
 *
 * Copyright (c) 2023, SberDevices. All Rights Reserved.
 * Author: Dmitry Rokosov <ddrokosov@sberdevices.ru>
 */

#ifndef __A1_PLL_H
#define __A1_PLL_H

#include "clk-pll.h"

/* PLL register offset */
#define ANACTRL_FIXPLL_CTRL0	0x0
#define ANACTRL_FIXPLL_CTRL1	0x4
#define ANACTRL_FIXPLL_STS	0x14
#define ANACTRL_HIFIPLL_CTRL0	0xc0
#define ANACTRL_HIFIPLL_CTRL1	0xc4
#define ANACTRL_HIFIPLL_CTRL2	0xc8
#define ANACTRL_HIFIPLL_CTRL3	0xcc
#define ANACTRL_HIFIPLL_CTRL4	0xd0
#define ANACTRL_HIFIPLL_STS	0xd4

#endif /* __A1_PLL_H */
