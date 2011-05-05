/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __BOOT_H__
#define __BOOT_H__

#include "wl12xx.h"

int wl1271_boot(struct wl1271 *wl);
int wl1271_load_firmware(struct wl1271 *wl);

#define WL1271_NO_SUBBANDS 8
#define WL1271_NO_POWER_LEVELS 4
#define WL1271_FW_VERSION_MAX_LEN 20

struct wl1271_static_data {
	u8 mac_address[ETH_ALEN];
	u8 padding[2];
	u8 fw_version[WL1271_FW_VERSION_MAX_LEN];
	u32 hw_version;
	u8 tx_power_table[WL1271_NO_SUBBANDS][WL1271_NO_POWER_LEVELS];
};

/* number of times we try to read the INIT interrupt */
#define INIT_LOOP 20000

/* delay between retries */
#define INIT_LOOP_DELAY 50

#define WU_COUNTER_PAUSE_VAL 0x3FF
#define WELP_ARM_COMMAND_VAL 0x4

#define OCP_REG_POLARITY     0x0064
#define OCP_REG_CLK_TYPE     0x0448
#define OCP_REG_CLK_POLARITY 0x0cb2
#define OCP_REG_CLK_PULL     0x0cb4

#define REG_FUSE_DATA_2_1    0x050a
#define PG_VER_MASK          0x3c
#define PG_VER_OFFSET        2

#define PG_MAJOR_VER_MASK    0x3
#define PG_MAJOR_VER_OFFSET  0x0
#define PG_MINOR_VER_MASK    0xc
#define PG_MINOR_VER_OFFSET  0x2

#define CMD_MBOX_ADDRESS     0x407B4

#define POLARITY_LOW         BIT(1)
#define NO_PULL              (BIT(14) | BIT(15))

#define FREF_CLK_TYPE_BITS     0xfffffe7f
#define CLK_REQ_PRCM           0x100
#define FREF_CLK_POLARITY_BITS 0xfffff8ff
#define CLK_REQ_OUTN_SEL       0x700

/* PLL configuration algorithm for wl128x */
#define SYS_CLK_CFG_REG              0x2200
/* Bit[0]   -  0-TCXO,  1-FREF */
#define MCS_PLL_CLK_SEL_FREF         BIT(0)
/* Bit[3:2] - 01-TCXO, 10-FREF */
#define WL_CLK_REQ_TYPE_FREF         BIT(3)
#define WL_CLK_REQ_TYPE_PG2          (BIT(3) | BIT(2))
/* Bit[4]   -  0-TCXO,  1-FREF */
#define PRCM_CM_EN_MUX_WLAN_FREF     BIT(4)

#define TCXO_ILOAD_INT_REG           0x2264
#define TCXO_CLK_DETECT_REG          0x2266

#define TCXO_DET_FAILED              BIT(4)

#define FREF_ILOAD_INT_REG           0x2084
#define FREF_CLK_DETECT_REG          0x2086
#define FREF_CLK_DETECT_FAIL         BIT(4)

/* Use this reg for masking during driver access */
#define WL_SPARE_REG                 0x2320
#define WL_SPARE_VAL                 BIT(2)
/* Bit[6:5:3] -  mask wl write SYS_CLK_CFG[8:5:2:4] */
#define WL_SPARE_MASK_8526           (BIT(6) | BIT(5) | BIT(3))

#define PLL_LOCK_COUNTERS_REG        0xD8C
#define PLL_LOCK_COUNTERS_COEX       0x0F
#define PLL_LOCK_COUNTERS_MCS        0xF0
#define MCS_PLL_OVERRIDE_REG         0xD90
#define MCS_PLL_CONFIG_REG           0xD92
#define MCS_SEL_IN_FREQ_MASK         0x0070
#define MCS_SEL_IN_FREQ_SHIFT        4
#define MCS_PLL_CONFIG_REG_VAL       0x73
#define MCS_PLL_ENABLE_HP            (BIT(0) | BIT(1))

#define MCS_PLL_M_REG                0xD94
#define MCS_PLL_N_REG                0xD96
#define MCS_PLL_M_REG_VAL            0xC8
#define MCS_PLL_N_REG_VAL            0x07

#define SDIO_IO_DS                   0xd14

/* SDIO/wSPI DS configuration values */
enum {
	HCI_IO_DS_8MA = 0,
	HCI_IO_DS_4MA = 1, /* default */
	HCI_IO_DS_6MA = 2,
	HCI_IO_DS_2MA = 3,
};

/* end PLL configuration algorithm for wl128x */

#endif
