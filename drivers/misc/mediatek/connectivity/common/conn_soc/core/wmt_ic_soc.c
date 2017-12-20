/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WMT-IC]"
#define CFG_IC_SOC 1

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "osal_typedef.h"
#include "wmt_ic.h"
#include "wmt_core.h"
#include "wmt_lib.h"
#include "stp_core.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

#define DEFAULT_PATCH_FRAG_SIZE (1000)
#define WMT_PATCH_FRAG_1ST (0x1)
#define WMT_PATCH_FRAG_MID (0x2)
#define WMT_PATCH_FRAG_LAST (0x3)

#define CFG_CHECK_WMT_RESULT (1)
/* BT Port 2 Feature. this command does not need
 *  after coex command is downconfirmed by LC,
 */
#define CFG_WMT_BT_PORT2 (0)

#define CFG_SET_OPT_REG (0)
#define CFG_WMT_I2S_DBGUART_SUPPORT (0)
#define CFG_SET_OPT_REG_SWLA (0)
#define CFG_SET_OPT_REG_MCUCLK (0)
#define CFG_SET_OPT_REG_MCUIRQ (0)

#define CFG_SUBSYS_COEX_NEED 0

#define CFG_WMT_COREDUMP_ENABLE 0

#define CFG_WMT_MULTI_PATCH (1)

#define CFG_WMT_CRYSTAL_TIMING_SET (0)

#define CFG_WMT_SDIO_DRIVING_SET (0)

#define CFG_WMT_UART_HIF_USE (0)

#define CFG_WMT_WIFI_5G_SUPPORT (1)

#define CFG_WMT_PATCH_DL_OPTM (1)
#if CFG_WMT_LTE_COEX_HANDLING
#define CFG_WMT_FILTER_MODE_SETTING (1)
#else
#define CFG_WMT_FILTER_MODE_SETTING (0)
#endif
#define MTK_WCN_CMB_MERGE_INTERFACE_SUPPORT (0)

#define CFG_WMT_POWER_ON_DLM  (1)
/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

static UINT8 gFullPatchName[NAME_MAX + 1];
static const WMT_IC_INFO_S *gp_soc_info;
static WMT_PATCH gp_soc_patch_info;
static WMT_CO_CLOCK gCoClockEn = WMT_CO_CLOCK_DIS;
#if 0
static UINT8 WMT_WAKEUP_DIS_GATE_CMD[] = { 0x1, 0x3, 0x01, 0x00, 0x04 };
static UINT8 WMT_WAKEUP_DIS_GATE_EVT[] = { 0x2, 0x3, 0x02, 0x0, 0x0, 0x04 };

static UINT8 WMT_WAKEUP_EN_GATE_CMD[] = { 0x1, 0x3, 0x01, 0x00, 0x05 };
static UINT8 WMT_WAKEUP_EN_GATE_EVT[] = { 0x2, 0x3, 0x02, 0x0, 0x0, 0x05 };
#endif

#if CFG_WMT_UART_HIF_USE
static UINT8 WMT_QUERY_BAUD_CMD[] = { 0x01, 0x04, 0x01, 0x00, 0x02 };
static UINT8 WMT_QUERY_BAUD_EVT_115200[] = { 0x02, 0x04, 0x06, 0x00, 0x00, 0x02, 0x00, 0xC2, 0x01, 0x00 };
static UINT8 WMT_QUERY_BAUD_EVT_X[] = { 0x02, 0x04, 0x06, 0x00, 0x00, 0x02, 0xAA, 0xAA, 0xAA, 0xBB };
static UINT8 WMT_SET_BAUD_CMD_X[] = { 0x01, 0x04, 0x05, 0x00, 0x01, 0xAA, 0xAA, 0xAA, 0xBB };
static UINT8 WMT_SET_BAUD_EVT[] = { 0x02, 0x04, 0x02, 0x00, 0x00, 0x01 };
static UINT8 WMT_SET_WAKEUP_WAKE_CMD_RAW[] = { 0xFF };
static UINT8 WMT_SET_WAKEUP_WAKE_EVT[] = { 0x02, 0x03, 0x02, 0x00, 0x00, 0x03 };
#endif
static UINT8 WMT_QUERY_STP_CMD[] = { 0x01, 0x04, 0x01, 0x00, 0x04 };
static UINT8 WMT_QUERY_STP_EVT_DEFAULT[] = { 0x02, 0x04, 0x06, 0x00, 0x00, 0x04, 0x11, 0x00, 0x00, 0x00 };
static UINT8 WMT_QUERY_STP_EVT[] = { 0x02, 0x04, 0x06, 0x00, 0x00, 0x04, 0xDF, 0x0E, 0x68, 0x01 };
static UINT8 WMT_PATCH_CMD[] = { 0x01, 0x01, 0x00, 0x00, 0x00 };
static UINT8 WMT_PATCH_EVT[] = { 0x02, 0x01, 0x01, 0x00, 0x00 };
static UINT8 WMT_RESET_CMD[] = { 0x01, 0x07, 0x01, 0x00, 0x04 };
static UINT8 WMT_RESET_EVT[] = { 0x02, 0x07, 0x01, 0x00, 0x00 };

#if CFG_WMT_BT_PORT2
static UINT8 WMT_BTP2_CMD[] = { 0x01, 0x10, 0x03, 0x00, 0x01, 0x03, 0x01 };
static UINT8 WMT_BTP2_EVT[] = { 0x02, 0x10, 0x01, 0x00, 0x00 };
#endif

/*soc patial patch address cmd & evt need firmware owner provide*/
#if CFG_WMT_MULTI_PATCH
static UINT8 WMT_PATCH_ADDRESS_CMD[] = {
		0x01, 0x08, 0x10, 0x00,
		0x01, 0x01, 0x00, 0x01,
		0x3c, 0x02, 0x09, 0x02,
		0x00, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff
};
static UINT8 WMT_PATCH_ADDRESS_EVT[] = { 0x02, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01 };

static UINT8 WMT_PATCH_P_ADDRESS_CMD[] = {
		0x01, 0x08, 0x10, 0x00,
		0x01, 0x01, 0x00, 0x01,
		0xc4, 0x04, 0x09, 0x02,
		0x00, 0x3f, 0x00, 0x01,
		0xff, 0xff, 0xff, 0xff
};
static UINT8 WMT_PATCH_P_ADDRESS_EVT[] = { 0x02, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01 };
#endif

/*coex cmd/evt++*/
static UINT8 WMT_COEX_SETTING_CONFIG_CMD[] = { 0x01, 0x10, 0x02, 0x00, 0x01, 0x00 };
static UINT8 WMT_COEX_SETTING_CONFIG_EVT[] = { 0x02, 0x10, 0x01, 0x00, 0x00 };

#if CFG_SUBSYS_COEX_NEED
static UINT8 WMT_BT_COEX_SETTING_CONFIG_CMD[] = { 0x01, 0x10, 0x0B,
	0x00, 0x02,
	0x00, 0x00, 0x00, 0x00,
	0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0xAA
};
static UINT8 WMT_BT_COEX_SETTING_CONFIG_EVT[] = { 0x02, 0x10, 0x01, 0x00, 0x00 };

static UINT8 WMT_WIFI_COEX_SETTING_CONFIG_CMD[] = { 0x01, 0x10, 0x0C,
	0x00, 0x03,
	0x00, 0x00, 0x00, 0x00, 0x00,
	0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0xAA
};
static UINT8 WMT_WIFI_COEX_SETTING_CONFIG_EVT[] = { 0x02, 0x10, 0x01, 0x00, 0x00 };

static UINT8 WMT_PTA_COEX_SETTING_CONFIG_CMD[] = { 0x01, 0x10, 0x0A,
	0x00, 0x04,
	0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xEE, 0xFF, 0xFF, 0xFE
};
static UINT8 WMT_PTA_COEX_SETTING_CONFIG_EVT[] = { 0x02, 0x10, 0x01, 0x00, 0x00 };

static UINT8 WMT_MISC_COEX_SETTING_CONFIG_CMD[] = { 0x01, 0x10, 0x09,
	0x00, 0x05,
	0xAA, 0xAA, 0xAA, 0xAA,
	0xBB, 0xBB, 0xBB, 0xBB
};
static UINT8 WMT_MISC_COEX_SETTING_CONFIG_EVT[] = { 0x02, 0x10, 0x01, 0x00, 0x00 };
#endif

/*coex cmd/evt--*/
static UINT8 WMT_SET_STP_CMD[] = { 0x01, 0x04, 0x05, 0x00, 0x03, 0xDF, 0x0E, 0x68, 0x01 };
static UINT8 WMT_SET_STP_EVT[] = { 0x02, 0x04, 0x02, 0x00, 0x00, 0x03 };
static UINT8 WMT_STRAP_CONF_CMD_FM_COMM[] = { 0x01, 0x05, 0x02, 0x00, 0x02, 0x02 };
static UINT8 WMT_STRAP_CONF_EVT[] = { 0x02, 0x05, 0x02, 0x00, 0x00, 0x02 };

#if 0
static UINT8 WMT_SET_OSC32K_BYPASS_CMD[] = { 0x01, 0x0A, 0x01, 0x00, 0x05 };
static UINT8 WMT_SET_OSC32K_BYPASS_EVT[] = { 0x02, 0x0A, 0x01, 0x00, 0x00 };
#endif

#if 0
/* to enable dump feature */
static UINT8 WMT_CORE_DUMP_EN_CMD[] = { 0x01, 0x0F, 0x02, 0x00, 0x03, 0x01 };
static UINT8 WMT_CORE_DUMP_EN_EVT[] = { 0x02, 0x0F, 0x01, 0x00, 0x00 };

/* to get system stack dump when f/w assert */
static UINT8 WMT_CORE_DUMP_LEVEL_01_CMD[] = { 0x1, 0x0F, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static UINT8 WMT_CORE_DUMP_LEVEL_01_EVT[] = { 0x2, 0x0F, 0x01, 0x00, 0x00 };

/* to get task and system stack dump when f/w assert */
static UINT8 WMT_CORE_DUMP_LEVEL_02_CMD[] = { 0x1, 0x0F, 0x07, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static UINT8 WMT_CORE_DUMP_LEVEL_02_EVT[] = { 0x2, 0x0F, 0x01, 0x00, 0x00 };

/* to get bt related memory dump when f/w assert */
static UINT8 WMT_CORE_DUMP_LEVEL_03_CMD[] = { 0x1, 0x0F, 0x07, 0x00, 0x03, 0x00, 0x00, 0x09, 0xF0, 0x00, 0x0A };
static UINT8 WMT_CORE_DUMP_LEVEL_03_EVT[] = { 0x2, 0x0F, 0x01, 0x00, 0x00 };
#endif
/* to get full dump when f/w assert */
static UINT8 WMT_CORE_DUMP_LEVEL_04_CMD[] = { 0x1, 0x0F, 0x07, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static UINT8 WMT_CORE_DUMP_LEVEL_04_EVT[] = { 0x2, 0x0F, 0x01, 0x00, 0x00 };

static UINT8 WMT_CORE_CO_CLOCK_CMD[] = { 0x1, 0x0A, 0x02, 0x00, 0x08, 0x03 };
static UINT8 WMT_CORE_CO_CLOCK_EVT[] = { 0x2, 0x0A, 0x01, 0x00, 0x00 };

static UINT8 WMT_CORE_START_RF_CALIBRATION_CMD[] = { 0x1, 0x14, 0x1, 0x00, 0x01 };
static UINT8 WMT_CORE_START_RF_CALIBRATION_EVT[] = { 0x2, 0x14, 0x02, 0x00, 0x00, 0x01 };

#if (MTK_WCN_CMB_MERGE_INTERFACE_SUPPORT)
static UINT8 WMT_SET_I2S_SLAVE_REG_CMD[] = { 0x01, 0x08, 0x10, 0x00	/*length */
	    , 0x01		/* op: w */
	    , 0x01		/*type: reg */
	    , 0x00		/*rev */
	    , 0x01		/*1 registers */
	    , 0x78, 0x00, 0x05, 0x80	/*addr:0x80050078 */
	    , 0x00, 0x00, 0x11, 0x01	/*value:0x11010000 */
	    , 0x00, 0x00, 0x77, 0x07	/*mask:0x07770000 */
};

static UINT8 WMT_SET_I2S_SLAVE_REG_EVT[] = { 0x02, 0x08, 0x04, 0x00	/*length */
	    , 0x00		/*S: 0 */
	    , 0x00		/*type: reg */
	    , 0x00		/*rev */
	    , 0x01		/*1 registers */
};

static UINT8 WMT_SET_DAI_TO_PAD_REG_CMD[] = { 0x01, 0x08, 0x10, 0x00	/*length */
	    , 0x01		/* op: w */
	    , 0x01		/*type: reg */
	    , 0x00		/*rev */
	    , 0x01		/*1 registers */
	    , 0x74, 0x00, 0x05, 0x80	/*addr:0x80050074 */
	    , 0x44, 0x44, 0x00, 0x00	/*value:0x11010000 */
	    , 0x77, 0x77, 0x00, 0x00	/*mask:0x07770000 */
};

static UINT8 WMT_SET_DAI_TO_PAD_REG_EVT[] = { 0x02, 0x08, 0x04, 0x00	/*length */
	    , 0x00		/*S: 0 */
	    , 0x00		/*type: reg */
	    , 0x00		/*rev */
	    , 0x01		/*1 registers */
};

static UINT8 WMT_SET_DAI_REG_CMD[] = { 0x01, 0x08, 0x10, 0x00	/*length */
	    , 0x01		/* op: w */
	    , 0x01		/*type: reg */
	    , 0x00		/*rev */
	    , 0x01		/*1 registers */
	    , 0xA0, 0x00, 0x05, 0x80	/*addr:0x80050074 */
	    , 0x04, 0x00, 0x00, 0x00	/*value:0x11010000 */
	    , 0x04, 0x00, 0x00, 0x00	/*mask:0x07770000 */
};

static UINT8 WMT_SET_DAI_REG_EVT[] = { 0x02, 0x08, 0x04, 0x00	/*length */
	    , 0x00		/*S: 0 */
	    , 0x00		/*type: reg */
	    , 0x00		/*rev */
	    , 0x01		/*1 registers */
};
#endif

#if !(CFG_IC_SOC)		/* For MT6628 no need to set ALLEINT registers, done in f/w */
/* enable all interrupt */
static UINT8 WMT_SET_ALLINT_REG_CMD[] = { 0x01, 0x08, 0x10, 0x00	/*length */
	    , 0x01		/* op: w */
	    , 0x01		/*type: reg */
	    , 0x00		/*rev */
	    , 0x01		/*1 registers */
	    , 0x00, 0x03, 0x05, 0x80	/*addr:0x80050300 */
	    , 0x00, 0xC4, 0x00, 0x00	/*value:0x0000C400 */
	    , 0x00, 0xC4, 0x00, 0x00	/*mask:0x0000C400 */
};

static UINT8 WMT_SET_ALLINT_REG_EVT[] = { 0x02, 0x08, 0x04, 0x00	/*length */
	    , 0x00		/*S: 0 */
	    , 0x00		/*type: reg */
	    , 0x00		/*rev */
	    , 0x01		/*1 registers */
};

#endif

#if CFG_SET_OPT_REG_SWLA	/* enable swla: eesk(7) eecs(8) oscen(19) sck0(24) scs0(25)  */
static UINT8 WMT_SET_SWLA_REG_CMD[] = { 0x01, 0x08, 0x1C, 0x00	/*length */
	    , 0x01		/* op: w */
	    , 0x01		/*type: reg */
	    , 0x00		/*rev */
	    , 0x02		/*2 registers */
	    , 0x10, 0x01, 0x05, 0x80	/*addr:0x80050110 */
	    , 0x10, 0x10, 0x01, 0x00	/*value:0x00011010 */
	    , 0xF0, 0xF0, 0x0F, 0x00	/*mask:0x000FF0F0 */
	    , 0x40, 0x01, 0x05, 0x80	/*addr:0x80050140 */
	    , 0x00, 0x10, 0x01, 0x00	/*value:0x00011000 */
	    , 0x00, 0xF0, 0x0F, 0x00	/*mask:0x000FF000 */
};

static UINT8 WMT_SET_SWLA_REG_EVT[] = { 0x02, 0x08, 0x04, 0x00	/*length */
	    , 0x00		/*S: 0 */
	    , 0x00		/*type: reg */
	    , 0x00		/*rev */
	    , 0x02		/*2 registers */
};
#endif

#if CFG_SET_OPT_REG_MCUCLK	/* enable mcu clk: antsel_4, eedi */
static UINT8 WMT_SET_MCUCLK_REG_CMD[] = { 0x01, 0x08, (4 + 12 * 4), 0x00	/*length */
	    , 0x01		/* op: w */
	    , 0x01		/* type: reg */
	    , 0x00		/* rev */
	    , 0x04		/* 4 registers */
	    , 0x00, 0x04, 0x00, 0x80	/* addr:0x8000 0400 */
	    , 0x00, 0x14, 0x00, 0x00	/* value:0x0000 1400(osc, hclk), 0x0000 1501(PLL, en) */
	    , 0xFF, 0xFF, 0x00, 0x00	/* mask:0x0000 FFFF */
	    , 0x80, 0x01, 0x05, 0x80	/* addr:0x8005 0180 */
	    , 0x12, 0x13, 0x00, 0x00	/* value:0x0000 1312(osc, hclk), 0x0000 1a19(PLL, en) */
	    , 0xFF, 0xFF, 0x00, 0x00	/* mask:0x0000 FFFF */
	    , 0x00, 0x01, 0x05, 0x80	/* addr:0x8005 0100 */
	    , 0x00, 0x00, 0x02, 0x00	/* value:0x0002 0000 */
	    , 0x00, 0x00, 0x0F, 0x00	/* mask:0x000F 0000 */
	    , 0x10, 0x01, 0x05, 0x80	/* addr:0x8005 0110 */
	    , 0x02, 0x00, 0x00, 0x00	/* value:0x0000 0002 */
	    , 0x0F, 0x00, 0x00, 0x00	/* mask:0x0000 000F */
};

static UINT8 WMT_SET_MCUCLK_REG_EVT[] = { 0x02, 0x08, 0x04, 0x00	/*length */
	    , 0x00		/* S: 0 */
	    , 0x00		/* type: reg */
	    , 0x00		/* rev */
	    , 0x04		/* 4 registers */
};
#endif

#if CFG_WMT_I2S_DBGUART_SUPPORT	/* register write for debug uart */
static UINT8 WMT_SET_DBGUART_REG_CMD[] = { 0x01, 0x08, 0x1C, 0x00	/*length */
	    , 0x01		/* op: w */
	    , 0x01		/*type: reg */
	    , 0x00		/*rev */
	    , 0x02		/*2 registers */
	    , 0x30, 0x01, 0x05, 0x80	/*addr:0x80050130 */
	    , 0x00, 0x00, 0x00, 0x00	/*value:0x00000000 */
	    , 0xF0, 0x0F, 0x00, 0x00	/*mask:0x00000FF0 */
	    , 0x40, 0x01, 0x05, 0x80	/*addr:0x80050140 */
	    , 0x00, 0x01, 0x00, 0x00	/*value:0x00000100 */
	    , 0x00, 0x01, 0x00, 0x00	/*mask:0x00000100 */
};

static UINT8 WMT_SET_DBGUART_REG_EVT[] = { 0x02, 0x08, 0x04, 0x00	/*length */
	    , 0x00		/*S: 0 */
	    , 0x00		/*type: reg */
	    , 0x00		/*rev */
	    , 0x02		/*2 registers */
};
#endif

#if CFG_SET_OPT_REG_MCUIRQ	/* enable mcu irq: antsel_4, wlan_act */
#if 1				/* Ray */
static UINT8 WMT_SET_MCUIRQ_REG_CMD[] = { 0x01, 0x08, (4 + 12 * 4), 0x00	/*length */
	    , 0x01		/* op: w */
	    , 0x01		/* type: reg */
	    , 0x00		/* rev */
	    , 0x04		/* 4 registers */
	    , 0x00, 0x04, 0x00, 0x80	/* addr:0x8000_0400 */
	    , 0x03, 0x14, 0x00, 0x00	/* value:0x0000_1403 check confg debug flag 3 low word */
	    , 0xFF, 0xFF, 0x00, 0x00	/* mask:0x0000_FFFF */
	    /* cirq_int_n */
	    , 0x10, 0x01, 0x05, 0x80	/* addr:0x8005_0110 */
	    , 0x02, 0x00, 0x00, 0x00	/* value:0x0000_0002 set EEDI as cirq_int_n debug flag (monitor flag2) */
	    , 0x07, 0x00, 0x00, 0x00	/* mask:0x0000_0007 */
	    , 0x00, 0x01, 0x05, 0x80	/* addr:0x8005_0100 */
	    , 0x00, 0x00, 0x02, 0x00	/* value:0x0002_0000 (ANTSEL4=>monitor flag 0, ahb_x2_gt_ck debug flag) */
	    , 0x00, 0x00, 0x07, 0x00	/* mask:0x0007_0000 */
	    /* 1.    ARM irq_b, monitor flag 0 */
	    , 0x80, 0x01, 0x05, 0x80	/* addr:0x8005_0180 */
	    , 0x1F, 0x1E, 0x00, 0x00	/* value:0x0000_1E1F check mcusys debug flag */
	    , 0x7F, 0x7F, 0x00, 0x00	/* mask:0x0000_7F7F */
};

static UINT8 WMT_SET_MCUIRQ_REG_EVT[] = { 0x02, 0x08, 0x04, 0x00	/*length */
	    , 0x00		/* S: 0 */
	    , 0x00		/* type: reg */
	    , 0x00		/* rev */
	    , 0x04		/* 5 registers */
};
#elif 0				/* KC */
static UINT8 WMT_SET_MCUIRQ_REG_CMD[] = { 0x01, 0x08, (4 + 12 * 5), 0x00	/*length */
	    , 0x01		/* op: w */
	    , 0x01		/* type: reg */
	    , 0x00		/* rev */
	    , 0x05		/* 5 registers */
	    , 0x00, 0x04, 0x00, 0x80	/* addr:0x8000_0400 */
	    , 0x00, 0x02, 0x00, 0x00	/* value:0x0000_0200 [15:8]=0x2 arm irq_b, 0xA irq_bus[5] bt_timcon_irq_b */
	    , 0x00, 0xFF, 0x00, 0x00	/* mask:0x0000_FF00 */
	    /* 1.    ARM irq_b, monitor flag 0 */
	    , 0x80, 0x01, 0x05, 0x80	/* addr:0x8005_0180 */
	    , 0x18, 0x00, 0x00, 0x00	/* value:0x0000_0018 [6:0]=001_1000 (monitor flag 0 select, MCUSYS, SEL:8) */
	    , 0x7F, 0x00, 0x00, 0x00	/* mask:0x0000_007F */
	    , 0x00, 0x01, 0x05, 0x80	/* addr:0x8005_0100 */
	    , 0x00, 0x00, 0x02, 0x00	/* value:0x0002_0000 (ANTSEL4=>monitor flag 0) */
	    , 0x00, 0x00, 0x07, 0x00	/* mask:0x0007_0000 */
	    /* 2.    irq_bus[5] bt_timcon_irq_b monitor flag 15 */
	    , 0xB0, 0x01, 0x05, 0x80	/* addr:0x8005_01B0 */
	    , 0x00, 0x00, 0x00, 0x16	/* value:0x1600_0000 [30:24]=001_0110 (monitor flag 15 select, MCUSYS, SEL:6) */
	    , 0x00, 0x00, 0x00, 0x7F	/* mask:0x7F00_0000 */
	    , 0x30, 0x01, 0x05, 0x80	/* addr:0x8005_0130 */
	    , 0x00, 0x20, 0x00, 0x00	/* value:0x0000_2000 (WLAN_ACT=>monitor flag 15) */
	    , 0x00, 0x70, 0x00, 0x00	/* mask:0x0000_7000 */
};

static UINT8 WMT_SET_MCUIRQ_REG_EVT[] = { 0x02, 0x08, 0x04, 0x00	/*length */
	    , 0x00		/* S: 0 */
	    , 0x00		/* type: reg */
	    , 0x00		/* rev */
	    , 0x05		/* 5 registers */
};
#endif
#endif

#if CFG_WMT_CRYSTAL_TIMING_SET
static UINT8 WMT_SET_CRYSTAL_TRIMING_CMD[] = { 0x01, 0x12, 0x02, 0x00, 0x01, 0x00 };
static UINT8 WMT_SET_CRYSTAL_TRIMING_EVT[] = { 0x02, 0x12, 0x02, 0x00, 0x01, 0x00 };

static UINT8 WMT_GET_CRYSTAL_TRIMING_CMD[] = { 0x01, 0x12, 0x02, 0x00, 0x00, 0x00 };
static UINT8 WMT_GET_CRYSTAL_TRIMING_EVT[] = { 0x02, 0x12, 0x02, 0x00, 0x00, 0x00 };
#endif

#ifdef CFG_WMT_READ_EFUSE_VCN33
static UINT8 WMT_GET_EFUSE_VCN33_CMD[] = { 0x01, 0x12, 0x02, 0x00, 0x04, 0x00 };
static UINT8 WMT_GET_EFUSE_VCN33_EVT[] = { 0x02, 0x12, 0x02, 0x00, 0x04, 0x00 };
#endif

/* set sdio driving */
#if CFG_WMT_SDIO_DRIVING_SET
static UINT8 WMT_SET_SDIO_DRV_REG_CMD[] = { 0x01, 0x08, 0x10, 0x00	/*length */
	    , 0x01		/* op: w */
	    , 0x01		/*type: reg */
	    , 0x00		/*rev */
	    , 0x01		/*1 registers */
	    , 0x50, 0x00, 0x05, 0x80	/*addr:0x80050050 */
	    , 0x44, 0x44, 0x04, 0x00	/*value:0x00044444 */
	    , 0x77, 0x77, 0x07, 0x00	/*mask:0x00077777 */
};

static UINT8 WMT_SET_SDIO_DRV_REG_EVT[] = { 0x02, 0x08, 0x04, 0x00	/*length */
	    , 0x00		/*S: 0 */
	    , 0x00		/*type: reg */
	    , 0x00		/*rev */
	    , 0x01		/*1 registers */
};
#endif

#if CFG_WMT_WIFI_5G_SUPPORT
static UINT8 WMT_GET_SOC_ADIE_CHIPID_CMD[] = { 0x01, 0x13, 0x04, 0x00, 0x02, 0x04, 0x24, 0x00 };
static UINT8 WMT_GET_SOC_ADIE_CHIPID_EVT[] = {
		0x02, 0x13, 0x09, 0x00, 0x00, 0x02, 0x04, 0x24,
		0x00, 0x00, 0x00, 0x00, 0x00
};
static UINT8 WMT_GET_SOC_6625_L_CMD[] = { 0x01, 0x13, 0x04, 0x00, 0x02, 0x04, 0x20, 0x01 };
static UINT8 WMT_GET_SOC_6625_L_EVT[] = {
		0x02, 0x13, 0x09, 0x00, 0x00, 0x02, 0x04, 0x20,
		0x01, 0x00, 0x00, 0x00, 0x00
};
#endif

#if CFG_WMT_PATCH_DL_OPTM
static UINT8 WMT_SET_MCU_CLK_EN_CMD[] = {
		0x01, 0x08, 0x10, 0x00,
		0x01, 0x01, 0x00, 0x01,
		0x34, 0x03, 0x00, 0x80,
		0x00, 0x00, 0x01, 0x00,
		0xff, 0xff, 0xff, 0xff
};
static UINT8 WMT_SET_MCU_CLK_EN_EVT[] = { 0x02, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01 };

static UINT8 WMT_SET_MCU_CLK_138_CMD[] = {
		0x01, 0x08, 0x10, 0x00,
		0x01, 0x01, 0x00, 0x01,
		0x0c, 0x01, 0x00, 0x80,
		0x59, 0x4d, 0x84, 0x00,
		0xff, 0xff, 0xff, 0xff
};
static UINT8 WMT_SET_MCU_CLK_138_EVT[] = { 0x02, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01 };

static UINT8 WMT_SET_MCU_CLK_26_CMD[] = {
		0x01, 0x08, 0x10, 0x00,
		0x01, 0x01, 0x00, 0x01,
		0x0c, 0x01, 0x00, 0x80,
		0x00, 0x4d, 0x84, 0x00,
		0xff, 0xff, 0xff, 0xff
};
static UINT8 WMT_SET_MCU_CLK_26_EVT[] = { 0x02, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01 };

static UINT8 WMT_SET_MCU_CLK_DIS_CMD[] = {
		0x01, 0x08, 0x10, 0x00,
		0x01, 0x01, 0x00, 0x01,
		0x34, 0x03, 0x00, 0x80,
		0x00, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff
};
static UINT8 WMT_SET_MCU_CLK_DIS_EVT[] = { 0x02, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01 };

/*only for 6797,enable high clock frequency*/
/*CLK EN*/
static UINT8 WMT_SET_MCU_CLK_EN_6797[] = {
	0x01, 0x08, 0x10, 0x00, 0x01, 0x01, 0x00, 0x01,
	0x10, 0x11, 0x02, 0x81, 0x00, 0x00, 0x00, 0x10,
	0x00, 0x00, 0x00, 0x10
};
/*RATIO SET*/
static UINT8 WMT_SET_MCU_RATIO_SET_6797[] = {
	0x01, 0x08, 0x10, 0x00, 0x01, 0x01, 0x00, 0x01,
	0x0c, 0x01, 0x00, 0x80, 0x40, 0x00, 0x00, 0x00,
	0xc0, 0x00, 0x00, 0x00
};
/*DIV SET*/
static UINT8 WMT_SET_MCU_DIV_SET_6797[] = {
	0x01, 0x08, 0x10, 0x00, 0x01, 0x01, 0x00, 0x01,
	0x18, 0x11, 0x02, 0x80, 0x07, 0x00, 0x00, 0x00,
	0x3f, 0x00, 0x00, 0x00
};
/*HCLK SET*/
static UINT8 WMT_SET_MCU_HCLK_SET_6797[] = {
	0x01, 0x08, 0x10, 0x00, 0x01, 0x01, 0x00, 0x01,
	0x00, 0x11, 0x02, 0x81, 0x04, 0x00, 0x00, 0x00,
	0x07, 0x00, 0x00, 0x00
};

/*Change clock to 26MHz*/
/*HCLK DIS*/
static UINT8 WMT_SET_MCU_HCLK_DIS_6797[] = {
	0x01, 0x08, 0x10, 0x00, 0x01, 0x01, 0x00, 0x01,
	0x00, 0x11, 0x02, 0x81, 0x00, 0x00, 0x00, 0x00,
	0x07, 0x00, 0x00, 0x00
};
/*RATIO DIS*/
static UINT8 WMT_SET_MCU_RATIO_DIS_6797[] = {
	0x01, 0x08, 0x10, 0x00, 0x01, 0x01, 0x00, 0x01,
	0x0c, 0x01, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
	0xc0, 0x00, 0x00, 0x00
};
/*CLK DIS*/
static UINT8 WMT_SET_MCU_CLK_DIS_6797[] = {
	0x01, 0x08, 0x10, 0x00, 0x01, 0x01, 0x00, 0x01,
	0x10, 0x11, 0x02, 0x81, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x10
};

static UINT8 WMT_SET_MCU_CLK_EVT_6797[] = {
	0x02, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01
};

#endif

#if CFG_WMT_FILTER_MODE_SETTING
static UINT8 WMT_COEX_EXT_COMPONENT_CMD[] = {0x01, 0x10, 0x03, 0x00, 0x0d, 0x00, 0x00};
static UINT8 WMT_COEX_FILTER_SPEC_CMD_TEST[] = {
		0x01, 0x10, 0x45, 0x00, 0x11, 0x00, 0x00, 0x01,
		0x00, 0x11, 0x11, 0x16, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x63, 0x63, 0x63, 0x00, 0x39, 0x43, 0x63,
		0x63, 0x02, 0x02, 0x03, 0x00, 0x01, 0x01, 0x01,
		0x01, 0x0e, 0x0e, 0x0e, 0x00, 0x0a, 0x0c, 0x0e,
		0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static UINT8 WMT_COEX_LTE_FREQ_IDX_TABLE_CMD[] = {
		0x01, 0x10, 0x21, 0x00, 0x12, 0xfc, 0x08, 0x15,
		0x09, 0x2e, 0x09, 0x47, 0x09, 0xc4, 0x09, 0xd4,
		0x09, 0xe3, 0x09, 0x5a, 0x0a, 0x14, 0x09, 0x2d,
		0x09, 0x46, 0x09, 0x60, 0x09, 0xd3, 0x09, 0xe2,
		0x09, 0x59, 0x0a, 0x8B, 0x0a};
static UINT8 WMT_COEX_LTE_CHAN_UNSAFE_CMD[] = { 0x01, 0x10, 0x02, 0x00, 0x13, 0x00 };
static UINT8 WMT_COEX_IS_LTE_L_CMD[] = { 0x01, 0x10, 0x02, 0x00, 0x21, 0x01 };

#if 0
static UINT8 WMT_COEX_SPLIT_FILTER_CMD_TEST[] = {
		0x01, 0x10, 0x19, 0x00, 0x0F, 0x00, 0x00, 0x00,
		0x00, 0x6c, 0x09, 0x8a, 0x09, 0x8a, 0x09, 0x9e,
		0x09, 0x01, 0x07, 0x07, 0x0b, 0x07, 0x07, 0x00,
		0x32, 0x27, 0x4e, 0x27, 0x32
};

static UINT8 WMT_COEX_FILTER_SPEC_CMD_TEST[] = {
		0x01, 0x10, 0x45, 0x00, 0x11, 0x00, 0x00, 0x01,
		0x00, 0x07, 0x07, 0x07, 0x54, 0x54, 0x00, 0x00,
		0x00, 0x50, 0x50, 0x50, 0x54, 0x54, 0x39, 0x39,
		0x39, 0x02, 0x02, 0x02, 0x0e, 0x0e, 0x01, 0x01,
		0x01, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0a, 0x0a,
		0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00
};

static UINT8 WMT_COEX_LTE_FREQ_IDX_TABLE_CMD_TEST[] = {
		0x01, 0x10, 0x21, 0x00, 0x12, 0xfc, 0x08, 0x15,
		0x09, 0x2e, 0x09, 0x47, 0x09, 0xc4, 0x09, 0xdd,
		0x09, 0xf6, 0x09, 0x0f, 0xaf, 0x14, 0x09, 0x2d,
		0x09, 0x46, 0x09, 0x5f, 0x09, 0xdd, 0x09, 0xf5,
		0x09, 0x0d, 0x0a, 0x27, 0x0a
};
static UINT8 WMT_COEX_LTE_CHAN_UNSAFE_CMD_TEST[] = { 0x01, 0x10, 0x02, 0x00, 0x13, 0x00 };
static UINT8 WMT_COEX_EXT_COMPONENT_CMD_TEST[] = { 0x01, 0x10, 0x03, 0x00, 0x0d, 0x7f, 0x03 };
#endif

static UINT8 WMT_COEX_FILTER_SPEC_CMD_0[] = {
		0x01, 0x10, 0x45, 0x00, 0x11, 0x00, 0x00, 0x01,
		0x00, 0x16, 0x16, 0x16, 0x16, 0x00, 0x00, 0x00,
		0x00, 0x63, 0x63, 0x63, 0x63, 0x3c, 0x3c, 0x3c,
		0x3c, 0x04, 0x04, 0x04, 0x04, 0x01, 0x01, 0x01,
		0x01, 0x0e, 0x0e, 0x0e, 0x0e, 0x0b, 0x0b, 0x0b,
		0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00
};

static UINT8 WMT_COEX_LTE_FREQ_IDX_TABLE_CMD_0[] = {
		0x01, 0x10, 0x21, 0x00, 0x12, 0xfc, 0x08, 0x15,
		0x09, 0x2e, 0x09, 0x47, 0x09, 0xc4, 0x09, 0xdd,
		0x09, 0xf6, 0x09, 0x0f, 0x0a, 0x14, 0x09, 0x2d,
		0x09, 0x46, 0x09, 0x5f, 0x09, 0xdd, 0x09, 0xf5,
		0x09, 0x0d, 0x0a, 0x27, 0x0a
};
static UINT8 WMT_COEX_TDM_REQ_ANTSEL_NUM_CMD[] = { 0x01, 0x10, 0x02, 0x00, 0x14, 0x00 };
static UINT8 WMT_COEX_IS_LTE_PROJ_CMD[] = { 0x01, 0x10, 0x02, 0x00, 0x15, 0x01 };
static UINT8 WMT_COEX_SPLIT_MODE_EVT[] = { 0x02, 0x10, 0x01, 0x00, 0x00 };

static UINT8 WMT_COEX_FILTER_SPEC_CMD_6752[] = {
		0x01, 0x10, 0x45, 0x00, 0x11, 0x00, 0x00, 0x01,
		0x00, 0x11, 0x11, 0x16, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x63, 0x63, 0x63, 0x00, 0x39, 0x43, 0x63,
		0x63, 0x02, 0x02, 0x03, 0x00, 0x01, 0x01, 0x01,
		0x01, 0x0E, 0x0E, 0x0E, 0x00, 0x0A, 0x0C, 0x0E,
		0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00
};

static UINT8 WMT_COEX_LTE_FREQ_IDX_TABLE_CMD_6752[] = {
		0x01, 0x10, 0x21, 0x00, 0x12, 0xFC, 0x08, 0x15,
		0x09, 0x2E, 0x09, 0x47, 0x09, 0xC4, 0x09, 0xD4,
		0x09, 0xE3, 0x09, 0x5A, 0x0A, 0x14, 0x09, 0x2D,
		0x09, 0x46, 0x09, 0x60, 0x09, 0xD3, 0x09, 0xE2,
		0x09, 0x59, 0x0A, 0x8B, 0x0A
};
#endif

#if CFG_WMT_POWER_ON_DLM
static UINT8 WMT_POWER_CTRL_DLM_CMD1[] = {
		0x01, 0x08, 0x10, 0x00,
		0x01, 0x01, 0x00, 0x01,
		0x60, 0x00, 0x10, 0x80,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x0f, 0x00, 0x00
};

static UINT8 WMT_POWER_CTRL_DLM_CMD2[] = {
		0x01, 0x08, 0x10, 0x00,
		0x01, 0x01, 0x00, 0x01,
		0x60, 0x00, 0x10, 0x80,
		0x00, 0x00, 0x00, 0x00,
		0xf0, 0x00, 0x00, 0x00
};

static UINT8 WMT_POWER_CTRL_DLM_CMD3[] = {
		0x01, 0x08, 0x10, 0x00,
		0x01, 0x01, 0x00, 0x01,
		0x60, 0x00, 0x10, 0x80,
		0x00, 0x00, 0x00, 0x00,
		0x08, 0x00, 0x00, 0x00
};
static UINT8 WMT_POWER_CTRL_DLM_EVT[] = { 0x02, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01 };
#endif

#if (!CFG_IC_SOC)

/* stp sdio init scripts */
static struct init_script init_table_1_1[] = {
	/* table_1_1 is only applied to common SDIO interface */
	INIT_CMD(WMT_SET_ALLINT_REG_CMD, WMT_SET_ALLINT_REG_EVT, "enable all interrupt"),
	/* applied to MT6628 ? */
	INIT_CMD(WMT_WAKEUP_DIS_GATE_CMD, WMT_WAKEUP_DIS_GATE_EVT, "disable gating"),
};

#endif

static struct init_script init_table_1_2[] = {
	INIT_CMD(WMT_QUERY_STP_CMD, WMT_QUERY_STP_EVT_DEFAULT, "query stp default"),
};

#if CFG_WMT_UART_HIF_USE
static struct init_script init_table_2[] = {
	INIT_CMD(WMT_QUERY_BAUD_CMD, WMT_QUERY_BAUD_EVT_X, "query baud X"),
};
#endif

static struct init_script init_table_3[] = {
	INIT_CMD(WMT_RESET_CMD, WMT_RESET_EVT, "wmt reset"),
#if CFG_WMT_BT_PORT2
	INIT_CMD(WMT_BTP2_CMD, WMT_BTP2_EVT, "set bt port2"),
#endif
};

#if CFG_WMT_CRYSTAL_TIMING_SET
static struct init_script set_crystal_timing_script[] = {
	INIT_CMD(WMT_SET_CRYSTAL_TRIMING_CMD, WMT_SET_CRYSTAL_TRIMING_EVT, "set crystal trim value"),
};

static struct init_script get_crystal_timing_script[] = {
	INIT_CMD(WMT_GET_CRYSTAL_TRIMING_CMD, WMT_GET_CRYSTAL_TRIMING_EVT, "get crystal trim value"),
};
#endif
#ifdef CFG_WMT_READ_EFUSE_VCN33
static struct init_script get_efuse_vcn33_script[] = {
	INIT_CMD(WMT_GET_EFUSE_VCN33_CMD, WMT_GET_EFUSE_VCN33_EVT, "get efuse vcn33 value"),
};
#endif

static struct init_script init_table_4[] = {
	INIT_CMD(WMT_SET_STP_CMD, WMT_SET_STP_EVT, "set stp"),
};

static struct init_script init_table_5[] = {
	INIT_CMD(WMT_QUERY_STP_CMD, WMT_QUERY_STP_EVT, "query stp"),
};

static struct init_script init_table_5_1[] = {
	INIT_CMD(WMT_STRAP_CONF_CMD_FM_COMM, WMT_STRAP_CONF_EVT, "configure FM comm"),
};

static struct init_script init_table_6[] = {
	INIT_CMD(WMT_CORE_DUMP_LEVEL_04_CMD, WMT_CORE_DUMP_LEVEL_04_EVT, "setup core dump level"),
};

static struct init_script calibration_table[] = {
	INIT_CMD(WMT_CORE_START_RF_CALIBRATION_CMD, WMT_CORE_START_RF_CALIBRATION_EVT, "start RF calibration data"),
};

#if CFG_WMT_PATCH_DL_OPTM
static struct init_script set_mcuclk_table_1[] = {
	INIT_CMD(WMT_SET_MCU_CLK_EN_CMD, WMT_SET_MCU_CLK_EN_EVT, "enable set mcu clk"),
	INIT_CMD(WMT_SET_MCU_CLK_138_CMD, WMT_SET_MCU_CLK_138_EVT, "set mcu clk to 138.67MH"),
};

static struct init_script set_mcuclk_table_2[] = {
	INIT_CMD(WMT_SET_MCU_CLK_26_CMD, WMT_SET_MCU_CLK_26_EVT, "set mcu clk to 26MH"),
	INIT_CMD(WMT_SET_MCU_CLK_DIS_CMD, WMT_SET_MCU_CLK_DIS_EVT, "disable set mcu clk"),
};

static struct init_script set_mcuclk_table_3[] = {
	INIT_CMD(WMT_SET_MCU_CLK_EN_6797, WMT_SET_MCU_CLK_EVT_6797, "enable set mcu clk"),
	INIT_CMD(WMT_SET_MCU_RATIO_SET_6797, WMT_SET_MCU_CLK_EVT_6797, "mcu ratio set"),
	INIT_CMD(WMT_SET_MCU_DIV_SET_6797, WMT_SET_MCU_CLK_EVT_6797, "mcu div set"),
	INIT_CMD(WMT_SET_MCU_HCLK_SET_6797, WMT_SET_MCU_CLK_EVT_6797, "set mcu clk to hclk"),
};
static struct init_script set_mcuclk_table_4[] = {
	INIT_CMD(WMT_SET_MCU_HCLK_DIS_6797, WMT_SET_MCU_CLK_EVT_6797, "disable mcu hclk"),
	INIT_CMD(WMT_SET_MCU_RATIO_DIS_6797, WMT_SET_MCU_CLK_EVT_6797, "disable mcu ratio set"),
	INIT_CMD(WMT_SET_MCU_CLK_DIS_6797, WMT_SET_MCU_CLK_EVT_6797, "disable mcu clk set"),
};

#endif

#if CFG_WMT_FILTER_MODE_SETTING
static struct init_script set_wifi_lte_coex_table_1[] = {
	INIT_CMD(WMT_COEX_FILTER_SPEC_CMD_6752, WMT_COEX_SPLIT_MODE_EVT, "wifi lte coex filter spec"),
	INIT_CMD(WMT_COEX_LTE_FREQ_IDX_TABLE_CMD_6752, WMT_COEX_SPLIT_MODE_EVT, "wifi lte freq idx"),
	INIT_CMD(WMT_COEX_IS_LTE_PROJ_CMD, WMT_COEX_SPLIT_MODE_EVT, "set LTE project"),
};

static struct init_script set_wifi_lte_coex_table_2[] = {
	INIT_CMD(WMT_COEX_EXT_COMPONENT_CMD, WMT_COEX_SPLIT_MODE_EVT, "wifi lte ext component"),
	INIT_CMD(WMT_COEX_FILTER_SPEC_CMD_TEST, WMT_COEX_SPLIT_MODE_EVT, "wifi lte coex filter"),
	INIT_CMD(WMT_COEX_LTE_FREQ_IDX_TABLE_CMD, WMT_COEX_SPLIT_MODE_EVT, "wifi lte freq id table"),
	INIT_CMD(WMT_COEX_LTE_CHAN_UNSAFE_CMD, WMT_COEX_SPLIT_MODE_EVT, "wifi lte unsafe channel"),
	INIT_CMD(WMT_COEX_IS_LTE_L_CMD, WMT_COEX_SPLIT_MODE_EVT, "wifi coex is L branch"),
};

static struct init_script set_wifi_lte_coex_table_0[] = {
#if 0
	INIT_CMD(WMT_COEX_SPLIT_FILTER_CMD_TEST, WMT_COEX_SPLIT_MODE_EVT, "wifi lte coex split filter"),
	INIT_CMD(WMT_COEX_FILTER_SPEC_CMD_TEST, WMT_COEX_SPLIT_MODE_EVT, "wifi lte coex filter spec"),
	INIT_CMD(WMT_COEX_LTE_FREQ_IDX_TABLE_CMD_TEST, WMT_COEX_SPLIT_MODE_EVT, "wifi lte freq idx"),
	INIT_CMD(WMT_COEX_LTE_CHAN_UNSAFE_CMD_TEST, WMT_COEX_SPLIT_MODE_EVT, "wifi lte channel unsafe"),
	INIT_CMD(WMT_COEX_EXT_COMPONENT_CMD_TEST, WMT_COEX_SPLIT_MODE_EVT, "wifi coex ext component"),
#endif
	INIT_CMD(WMT_COEX_FILTER_SPEC_CMD_0, WMT_COEX_SPLIT_MODE_EVT, "def wifi lte coex filter spec"),
	INIT_CMD(WMT_COEX_LTE_FREQ_IDX_TABLE_CMD_0, WMT_COEX_SPLIT_MODE_EVT, "def wifi lte freq idx"),
};

static struct init_script get_tdm_req_antsel_num_table[] = {
	INIT_CMD(WMT_COEX_TDM_REQ_ANTSEL_NUM_CMD, WMT_COEX_SPLIT_MODE_EVT, "get tdm req antsel num"),
};
#endif

#if CFG_SET_OPT_REG
static struct init_script set_registers[] = {
	/* INIT_CMD(WMT_SET_GPS_REG_CMD, WMT_SET_GPS_REG_EVT, "set wmt registers"), */
	/* INIT_CMD(WMT_SET_SDIODRV_REG_CMD, WMT_SET_SDIODRV_REG_EVT, "set SDIO driving registers") */
#if CFG_WMT_I2S_DBGUART_SUPPORT
	INIT_CMD(WMT_SET_DBGUART_REG_CMD, WMT_SET_DBGUART_REG_EVT, "set debug uart registers"),
#endif
#if CFG_SET_OPT_REG_SWLA
	INIT_CMD(WMT_SET_SWLA_REG_CMD, WMT_SET_SWLA_REG_EVT, "set swla registers"),
#endif
#if CFG_SET_OPT_REG_MCUCLK
	INIT_CMD(WMT_SET_MCUCLK_REG_CMD, WMT_SET_MCUCLK_REG_EVT, "set mcuclk dbg registers"),
#endif
#if CFG_SET_OPT_REG_MCUIRQ
	INIT_CMD(WMT_SET_MCUIRQ_REG_CMD, WMT_SET_MCUIRQ_REG_EVT, "set mcu irq dbg registers"),
#endif
};
#endif

static struct init_script coex_table[] = {
	INIT_CMD(WMT_COEX_SETTING_CONFIG_CMD, WMT_COEX_SETTING_CONFIG_EVT, "coex_wmt"),

#if CFG_SUBSYS_COEX_NEED
/* no need in MT6628 */
	INIT_CMD(WMT_BT_COEX_SETTING_CONFIG_CMD, WMT_BT_COEX_SETTING_CONFIG_EVT, "coex_bt"),
	INIT_CMD(WMT_WIFI_COEX_SETTING_CONFIG_CMD, WMT_WIFI_COEX_SETTING_CONFIG_EVT, "coex_wifi"),
	INIT_CMD(WMT_PTA_COEX_SETTING_CONFIG_CMD, WMT_PTA_COEX_SETTING_CONFIG_EVT, "coex_ext_pta"),
	INIT_CMD(WMT_MISC_COEX_SETTING_CONFIG_CMD, WMT_MISC_COEX_SETTING_CONFIG_EVT, "coex_misc"),
#endif
};

static struct init_script osc_type_table[] = {
	INIT_CMD(WMT_CORE_CO_CLOCK_CMD, WMT_CORE_CO_CLOCK_EVT, "osc_type"),
};

#if (MTK_WCN_CMB_MERGE_INTERFACE_SUPPORT)
static struct init_script merge_pcm_table[] = {
	INIT_CMD(WMT_SET_I2S_SLAVE_REG_CMD, WMT_SET_I2S_SLAVE_REG_EVT, "I2S_Slave"),
	INIT_CMD(WMT_SET_DAI_TO_PAD_REG_CMD, WMT_SET_DAI_TO_PAD_REG_EVT, "DAI_PAD"),
	INIT_CMD(WMT_SET_DAI_REG_CMD, WMT_SET_DAI_REG_EVT, "DAI_EVT"),
};
#endif

#if CFG_WMT_SDIO_DRIVING_SET
static struct init_script sdio_driving_table[] = {
	INIT_CMD(WMT_SET_SDIO_DRV_REG_CMD, WMT_SET_SDIO_DRV_REG_EVT, "sdio_driving"),
};
#endif

#if CFG_WMT_POWER_ON_DLM
static struct init_script wmt_power_on_dlm_table[] = {
	INIT_CMD(WMT_POWER_CTRL_DLM_CMD1, WMT_POWER_CTRL_DLM_EVT, "power on dlm cmd1"),
	INIT_CMD(WMT_POWER_CTRL_DLM_CMD2, WMT_POWER_CTRL_DLM_EVT, "power on dlm cmd2"),
	INIT_CMD(WMT_POWER_CTRL_DLM_CMD3, WMT_POWER_CTRL_DLM_EVT, "power on dlm cmd3")
};
#endif

/* SOC Chip Version and Info Table */
static const WMT_IC_INFO_S mtk_wcn_soc_info_table[] = {
	{
	 .u4HwVer = 0x8A00,
	 .cChipName = WMT_IC_NAME_DEFAULT,
	 .cChipVersion = WMT_IC_VER_E1,
	 .cPatchNameExt = WMT_IC_PATCH_E1_EXT,
	 /* need to refine? */
	 .eWmtHwVer = WMTHWVER_E1,
	 .bWorkWithoutPatch = MTK_WCN_BOOL_FALSE,
	 .bPsmSupport = MTK_WCN_BOOL_TRUE,
	 },
	{
	 .u4HwVer = 0x8A01,
	 .cChipName = WMT_IC_NAME_DEFAULT,
	 .cChipVersion = WMT_IC_VER_E2,
	 .cPatchNameExt = WMT_IC_PATCH_E1_EXT,
	 .eWmtHwVer = WMTHWVER_E2,
	 .bWorkWithoutPatch = MTK_WCN_BOOL_FALSE,
	 .bPsmSupport = MTK_WCN_BOOL_TRUE,
	 },
	{
	 .u4HwVer = 0x8B01,
	 .cChipName = WMT_IC_NAME_DEFAULT,
	 .cChipVersion = WMT_IC_VER_E3,
	 .cPatchNameExt = WMT_IC_PATCH_E1_EXT,
	 .eWmtHwVer = WMTHWVER_E3,
	 .bWorkWithoutPatch = MTK_WCN_BOOL_FALSE,
	 .bPsmSupport = MTK_WCN_BOOL_TRUE,
	 }
};

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

static INT32 mtk_wcn_soc_sw_init(P_WMT_HIF_CONF pWmtHifConf);

static INT32 mtk_wcn_soc_sw_deinit(P_WMT_HIF_CONF pWmtHifConf);

static INT32 mtk_wcn_soc_pin_ctrl(WMT_IC_PIN_ID id, WMT_IC_PIN_STATE state, UINT32 flag);

static INT32 mtk_wcn_soc_aif_ctrl(WMT_IC_PIN_STATE state, UINT32 flag);

static INT32 mtk_wcn_soc_ver_check(VOID);

static const WMT_IC_INFO_S *mtk_wcn_soc_find_wmt_ic_info(const UINT32 hw_ver);

static INT32 wmt_stp_init_coex(VOID);

#if CFG_WMT_FILTER_MODE_SETTING
static INT32 wmt_stp_wifi_lte_coex(VOID);
#endif

#if CFG_WMT_MULTI_PATCH
static INT32 mtk_wcn_soc_patch_dwn(UINT32 index);
static INT32 mtk_wcn_soc_patch_info_prepare(VOID);
#else
static INT32 mtk_wcn_soc_patch_dwn(VOID);
#endif

static INT32 mtk_wcn_soc_co_clock_ctrl(WMT_CO_CLOCK on);
static WMT_CO_CLOCK mtk_wcn_soc_co_clock_get(VOID);

#if CFG_WMT_CRYSTAL_TIMING_SET
static INT32 mtk_wcn_soc_crystal_triming_set(VOID);
#endif

static MTK_WCN_BOOL mtk_wcn_soc_quick_sleep_flag_get(VOID);

static MTK_WCN_BOOL mtk_wcn_soc_aee_dump_flag_get(VOID);

#if CFG_WMT_SDIO_DRIVING_SET
static INT32 mtk_wcn_soc_set_sdio_driving(void);
#endif
/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/* SOC Operation Function Table */
WMT_IC_OPS wmt_ic_ops_soc = {
	.icId = 0x0000,		/* soc may have mt6572/82/71/83,but they have the same sw init flow */
	.sw_init = mtk_wcn_soc_sw_init,
	.sw_deinit = mtk_wcn_soc_sw_deinit,
	.ic_pin_ctrl = mtk_wcn_soc_pin_ctrl,
	.ic_ver_check = mtk_wcn_soc_ver_check,
	.co_clock_ctrl = mtk_wcn_soc_co_clock_ctrl,
	.is_quick_sleep = mtk_wcn_soc_quick_sleep_flag_get,
	.is_aee_dump_support = mtk_wcn_soc_aee_dump_flag_get,
};

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

static INT32 mtk_wcn_soc_sw_init(P_WMT_HIF_CONF pWmtHifConf)
{
	INT32 iRet = -1;
	unsigned long ctrlPa1;
	unsigned long ctrlPa2;
	UINT32 hw_ver;
	WMT_CTRL_DATA ctrlData;
#ifdef CFG_WMT_READ_EFUSE_VCN33
	UINT32 efuse_d3_vcn33 = 2; /*default voltage is 3.5V*/
#endif
#if CFG_WMT_MULTI_PATCH
	UINT32 patch_num = 0;
	UINT32 patch_index = 0;
#endif
#if CFG_WMT_WIFI_5G_SUPPORT
	UINT32 dDieChipid = 0;
	UINT32 aDieChipid = 0;
	UINT8 evtbuf[20];
	UINT32 u4Res;
	UINT32 pmicChipid = 0;
#endif
	WMT_DBG_FUNC(" start\n");

	osal_assert(NULL != gp_soc_info);
	if ((NULL == gp_soc_info)
	    || (NULL == pWmtHifConf)
	    ) {
		WMT_ERR_FUNC("null pointers: gp_soc_info(0x%p), pWmtHifConf(0x%p)\n", gp_soc_info, pWmtHifConf);
		return -1;
	}

	hw_ver = gp_soc_info->u4HwVer;

	/* 4 <3.2> start init for BTIF */
	if (WMT_HIF_BTIF == pWmtHifConf->hifType) {
		/* 1. Query chip STP default options (TEST-ONLY) */
		/* WMT_DBG_FUNC("WMT-CORE: init_table_1_2 set chip baud:%d", pWmtHifConf->au4HifConf[0]); */
		iRet = wmt_core_init_script(init_table_1_2, osal_array_size(init_table_1_2));
		if (iRet) {
			WMT_ERR_FUNC("init_table_1_2 fail(%d)\n", iRet);
			osal_assert(0);
			return -2;
		}
		/* 2. Set chip STP options */
		iRet = wmt_core_init_script(init_table_4, osal_array_size(init_table_4));
		if (iRet) {
			WMT_ERR_FUNC("init_table_4 fail(%d)\n", iRet);
			return -3;
		}

		/* 3. Enable host full mode */
		ctrlPa1 = WMT_STP_CONF_MODE;
		ctrlPa2 = MTKSTP_BTIF_FULL_MODE;
		iRet = wmt_core_ctrl(WMT_CTRL_STP_CONF, &ctrlPa1, &ctrlPa2);
		ctrlPa1 = WMT_STP_CONF_EN;
		ctrlPa2 = 1;
		iRet += wmt_core_ctrl(WMT_CTRL_STP_CONF, &ctrlPa1, &ctrlPa2);
		if (iRet) {
			WMT_ERR_FUNC("enable host STP-BTIF-FULL mode fail(%d)\n", iRet);
			return -4;
		}
		WMT_DBG_FUNC("enable host STP-BTIF-FULL mode\n");
		/*4. wait for 10ms, enough for chip do mechanism switch.(at least 2ms is needed) */
		osal_sleep_ms(10);
		/* 5. Query chip STP options (TEST-ONLY) */
		iRet = wmt_core_init_script(init_table_5, osal_array_size(init_table_5));
		if (iRet) {
			WMT_ERR_FUNC("init_table_5 fail(%d)\n", iRet);
			return -5;
		}
	}
#if CFG_WMT_POWER_ON_DLM
	iRet = wmt_core_init_script(wmt_power_on_dlm_table, osal_array_size(wmt_power_on_dlm_table));
	if (iRet)
		WMT_ERR_FUNC("wmt_power_on_dlm_table fail(%d)\n", iRet);
	WMT_DBG_FUNC("wmt_power_on_dlm_table ok\n");
#endif
	/* 6. download patch */
#if CFG_WMT_MULTI_PATCH
	/* 6.1 Let launcher to search patch info */
	iRet = mtk_wcn_soc_patch_info_prepare();
	if (iRet) {
		WMT_ERR_FUNC("patch info perpare fail(%d)\n", iRet);
		return -6;
	}

	/* 6.2 Read patch number */
	ctrlPa1 = 0;
	ctrlPa2 = 0;
	wmt_core_ctrl(WMT_CTRL_GET_PATCH_NUM, &ctrlPa1, &ctrlPa2);
	patch_num = ctrlPa1;
	WMT_DBG_FUNC("patch total num = [%d]\n", patch_num);

#if CFG_WMT_PATCH_DL_OPTM
	if (0x0279 == wmt_ic_ops_soc.icId) {
		iRet = wmt_core_init_script(set_mcuclk_table_3, osal_array_size(set_mcuclk_table_3));
		if (iRet)
			WMT_ERR_FUNC("set_mcuclk_table_3 fail(%d)\n", iRet);
	} else {
		iRet = wmt_core_init_script(set_mcuclk_table_1, osal_array_size(set_mcuclk_table_1));
		if (iRet)
			WMT_ERR_FUNC("set_mcuclk_table_1 fail(%d)\n", iRet);
	}
#endif
	/* 6.3 Multi-patch Patch download */
	for (patch_index = 0; patch_index < patch_num; patch_index++) {
		iRet = mtk_wcn_soc_patch_dwn(patch_index);
		if (iRet) {
			WMT_ERR_FUNC("patch dwn fail (%d),patch_index(%d)\n", iRet, patch_index);
			return -7;
		}
		iRet = wmt_core_init_script(init_table_3, osal_array_size(init_table_3));
		if (iRet) {
			WMT_ERR_FUNC("init_table_3 fail(%d)\n", iRet);
			return -8;
		}
	}

#if CFG_WMT_PATCH_DL_OPTM
	if (0x0279 == wmt_ic_ops_soc.icId) {
		iRet = wmt_core_init_script(set_mcuclk_table_4, osal_array_size(set_mcuclk_table_4));
		if (iRet)
			WMT_ERR_FUNC("set_mcuclk_table_4 fail(%d)\n", iRet);
	} else {
		iRet = wmt_core_init_script(set_mcuclk_table_2, osal_array_size(set_mcuclk_table_2));
		if (iRet)
			WMT_ERR_FUNC("set_mcuclk_table_2 fail(%d)\n", iRet);
	}
#endif

#else
	/* 6.3 Patch download */
	iRet = mtk_wcn_soc_patch_dwn();
	/* If patch download fail, we just ignore this error and let chip init process goes on */
	if (iRet)
		WMT_ERR_FUNC("patch dwn fail (%d), just omit\n", iRet);

	/* 6.4. WMT Reset command */
	iRet = wmt_core_init_script(init_table_3, osal_array_size(init_table_3));
	if (iRet) {
		WMT_ERR_FUNC("init_table_3 fail(%d)\n", iRet);
		return -8;
	}
#endif

#ifdef CFG_WMT_READ_EFUSE_VCN33
	/*get CrystalTiming value before set it */
	iRet = wmt_core_tx(get_efuse_vcn33_script[0].cmd, get_efuse_vcn33_script[0].cmdSz, &u4Res,
			MTK_WCN_BOOL_FALSE);
	if (iRet || (u4Res != get_efuse_vcn33_script[0].cmdSz)) {
		WMT_ERR_FUNC("WMT-CORE: write (%s) iRet(%d) cmd len err(%d, %d)\n",
				 get_efuse_vcn33_script[0].str, iRet, u4Res, get_efuse_vcn33_script[0].cmdSz);
	}
	/* EVENT BUF */
	osal_memset(get_efuse_vcn33_script[0].evt, 0, get_efuse_vcn33_script[0].evtSz);
	iRet = wmt_core_rx(get_efuse_vcn33_script[0].evt, get_efuse_vcn33_script[0].evtSz, &u4Res);
	if (iRet || (u4Res != get_efuse_vcn33_script[0].evtSz)) {
		WMT_ERR_FUNC("WMT-CORE: read (%s) iRet(%d) evt len err(rx:%d, exp:%d)\n",
				 get_efuse_vcn33_script[0].str, iRet, u4Res, get_efuse_vcn33_script[0].evtSz);
		mtk_wcn_stp_dbg_dump_package();
	}
	efuse_d3_vcn33 = WMT_GET_EFUSE_VCN33_EVT[5] & 0x03;
	WMT_INFO_FUNC("Read efuse to set PMIC voltage:(%d)\n", efuse_d3_vcn33);
	wmt_set_pmic_voltage(efuse_d3_vcn33);
#endif

#if CFG_WMT_FILTER_MODE_SETTING
	if ((0x6580 == wmt_ic_ops_soc.icId) ||
	    (0x8163 == wmt_ic_ops_soc.icId) ||
	    (0x6752 == wmt_ic_ops_soc.icId) ||
	    (0x6582 == wmt_ic_ops_soc.icId) ||
	    (0x6592 == wmt_ic_ops_soc.icId) ||
	    (0x0279 == wmt_ic_ops_soc.icId) ||
	    (0x0326 == wmt_ic_ops_soc.icId) ||
	    (0x0321 == wmt_ic_ops_soc.icId) || (0x0335 == wmt_ic_ops_soc.icId) || (0x0337 == wmt_ic_ops_soc.icId)) {
		wmt_stp_wifi_lte_coex();
		WMT_DBG_FUNC("wmt_stp_wifi_lte_coex done!\n");
	}
	if ((0x6582 == wmt_ic_ops_soc.icId) || (0x6592 == wmt_ic_ops_soc.icId)) {
		/*get gpio tdm req antsel number */
		ctrlPa1 = 0;
		ctrlPa2 = 0;
		wmt_core_ctrl(WMT_CTRL_GET_TDM_REQ_ANTSEL, &ctrlPa1, &ctrlPa2);
		WMT_INFO_FUNC("get GPIO TDM REQ ANTSEL number(%d)\n", ctrlPa1);
		/*set gpio tdm req antsel number to firmware */
		WMT_COEX_TDM_REQ_ANTSEL_NUM_CMD[5] = ctrlPa1;
		iRet = wmt_core_init_script(get_tdm_req_antsel_num_table,
		    osal_array_size(get_tdm_req_antsel_num_table));
		if (iRet)
			WMT_ERR_FUNC("get_tdm_req_antsel_num_table fail(%d)\n", iRet);
	}
#endif
	/* 7. start RF calibration data */
	ctrlPa1 = BT_PALDO;
	ctrlPa2 = PALDO_ON;
	iRet = wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);
	ctrlPa1 = WIFI_PALDO;
	ctrlPa2 = PALDO_ON;
	iRet = wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);

	iRet = wmt_core_init_script(calibration_table, osal_array_size(calibration_table));
	if (iRet) {
		/* pwrap_read(0x0210,&ctrlPa1); */
		/* pwrap_read(0x0212,&ctrlPa2); */
		WMT_ERR_FUNC("power status: 210:(%d),212:(%d)!\n", ctrlPa1, ctrlPa2);
		WMT_ERR_FUNC("calibration_table fail(%d)\n", iRet);
		return -9;
	}

	ctrlPa1 = BT_PALDO;
	ctrlPa2 = PALDO_OFF;
	iRet = wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);
	ctrlPa1 = WIFI_PALDO;
	ctrlPa2 = PALDO_OFF;
	iRet = wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);

	iRet = wmt_stp_init_coex();
	if (iRet) {
		WMT_ERR_FUNC("init_coex fail(%d)\n", iRet);
		return -10;
	}
	WMT_DBG_FUNC("init_coex ok\n");

#if CFG_WMT_CRYSTAL_TIMING_SET
	mtk_wcn_soc_crystal_triming_set();
#endif

#if CFG_WMT_SDIO_DRIVING_SET
	mtk_wcn_soc_set_sdio_driving();
#endif

	if (WMT_CO_CLOCK_EN == mtk_wcn_soc_co_clock_get()) {
		WMT_INFO_FUNC("co-clock enabled.\n");

		iRet = wmt_core_init_script(osc_type_table, osal_array_size(osc_type_table));
		if (iRet) {
			WMT_ERR_FUNC("osc_type_table fail(%d), goes on\n", iRet);
			return -11;
		}
	} else {
		WMT_WARN_FUNC("co-clock disabled.\n");
	}
#if (MTK_WCN_CMB_MERGE_INTERFACE_SUPPORT)
	iRet = wmt_core_init_script(merge_pcm_table, osal_array_size(merge_pcm_table));
	if (iRet) {
		WMT_ERR_FUNC("merge_pcm_table fail(%d), goes on\n", iRet);
		return -12;
	}
#endif

	/* 15. Set FM strap */
	WMT_STRAP_CONF_CMD_FM_COMM[5] = (UINT8) pWmtHifConf->au4StrapConf[0];
	WMT_STRAP_CONF_EVT[5] = (UINT8) pWmtHifConf->au4StrapConf[0];
	iRet = wmt_core_init_script(init_table_5_1, osal_array_size(init_table_5_1));
	if (iRet) {
		WMT_ERR_FUNC("init_table_5_1 fm mode(%d) fail(%d)\n", pWmtHifConf->au4StrapConf[0], iRet);
		return -13;
	}
	WMT_DBG_FUNC("set fm mode (%d) ok\n", pWmtHifConf->au4StrapConf[0]);

#if CFG_SET_OPT_REG		/*set registers */
	iRet = wmt_core_init_script(set_registers, osal_array_size(set_registers));
	if (iRet) {
		WMT_ERR_FUNC("set_registers fail(%d)", iRet);
		return -14;
	}
#endif

#if CFG_WMT_COREDUMP_ENABLE
	/*Open Core Dump Function @QC begin */
	mtk_wcn_stp_coredump_flag_ctrl(1);
#endif
	if (0 != mtk_wcn_stp_coredump_flag_get()) {
		iRet = wmt_core_init_script(init_table_6, osal_array_size(init_table_6));
		if (iRet) {
			WMT_ERR_FUNC("init_table_6 core dump setting fail(%d)\n", iRet);
			return -15;
		}
		WMT_DBG_FUNC("enable soc_consys firmware coredump\n");
	} else {
		WMT_DBG_FUNC("disable soc_consys firmware coredump\n");
	}

#if CFG_WMT_WIFI_5G_SUPPORT
	dDieChipid = wmt_ic_ops_soc.icId;
	WMT_DBG_FUNC("current SOC chipid is 0x%x\n", dDieChipid);
	if (0x6592 == dDieChipid) {
		/* read A die chipid by wmt cmd */
		iRet =
		    wmt_core_tx((PUINT8) &WMT_GET_SOC_ADIE_CHIPID_CMD[0], sizeof(WMT_GET_SOC_ADIE_CHIPID_CMD), &u4Res,
				MTK_WCN_BOOL_FALSE);
		if (iRet || (u4Res != sizeof(WMT_GET_SOC_ADIE_CHIPID_CMD))) {
			WMT_ERR_FUNC("wmt_core:read A die chipid CMD fail(%d),size(%d)\n", iRet, u4Res);
			return -16;
		}
		osal_memset(evtbuf, 0, sizeof(evtbuf));
		iRet = wmt_core_rx(evtbuf, sizeof(WMT_GET_SOC_ADIE_CHIPID_EVT), &u4Res);
		if (iRet || (u4Res != sizeof(WMT_GET_SOC_ADIE_CHIPID_EVT))) {
			WMT_ERR_FUNC("wmt_core:read A die chipid EVT fail(%d),size(%d)\n", iRet, u4Res);
			return -17;
		}

		osal_memcpy(&aDieChipid, &evtbuf[u4Res - 2], 2);
		WMT_INFO_FUNC("get SOC A die chipid(0x%x)\n", aDieChipid);

		if (0x6625 == aDieChipid) {
			iRet =
			    wmt_core_tx((PUINT8) &WMT_GET_SOC_6625_L_CMD[0], sizeof(WMT_GET_SOC_6625_L_CMD), &u4Res,
					MTK_WCN_BOOL_FALSE);
			if (iRet || (u4Res != sizeof(WMT_GET_SOC_6625_L_CMD)))
				WMT_ERR_FUNC("wmt_core:read A die efuse CMD fail(%d),size(%d)\n", iRet, u4Res);
			osal_memset(evtbuf, 0, sizeof(evtbuf));
			iRet = wmt_core_rx(evtbuf, sizeof(WMT_GET_SOC_6625_L_EVT), &u4Res);
			if (iRet || (u4Res != sizeof(WMT_GET_SOC_6625_L_EVT)))
				WMT_ERR_FUNC("wmt_core:read A die efuse EVT fail(%d),size(%d)\n", iRet, u4Res);

			WMT_INFO_FUNC("read SOC Adie Efuse(0x120) value:0x%2x,0x%2x,0x%2x,0x%2x -> %s\n",
				      evtbuf[u4Res - 4], evtbuf[u4Res - 3], evtbuf[u4Res - 2], evtbuf[u4Res - 1],
				      evtbuf[u4Res - 2] == 0x31 ? "MT6625L" : "MT6625");
		}
		/* get PMIC chipid */

		ctrlData.ctrlId = WMT_CTRL_SOC_PALDO_CTRL;
		ctrlData.au4CtrlData[0] = PMIC_CHIPID_PALDO;
		ctrlData.au4CtrlData[1] = 0;
		iRet = wmt_ctrl(&ctrlData);
		if (iRet < 0) {
			WMT_ERR_FUNC("wmt_core: read PMIC chipid fail(%d)\n", iRet);
			return -18;
		}
		pmicChipid = ctrlData.au4CtrlData[2];
		WMT_INFO_FUNC("current PMIC chipid(0x%x)\n", pmicChipid);

		/* MT6625 & MT6322, write 1 to 0x0414[12] */
		/* MT6625 & MT6323, assert */
		/* MT6627 & (MT6322 or MT6323),write 0 to 0x0414[12] */

		switch (aDieChipid) {
		case 0x6625:
			if (0x6322 == pmicChipid) {
				WMT_INFO_FUNC("wmt-core:enable wifi 5G support\n");
				ctrlPa1 = WIFI_5G_PALDO;
				ctrlPa2 = PALDO_ON;
				wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);
			} else if (0x6323 == pmicChipid) {
				osal_assert(0);
			} else {
				WMT_WARN_FUNC("wmt-core: unknown PMIC chipid\n");
			}
			break;
		case 0x6627:
			if ((0x6322 == pmicChipid) || (0x6323 == pmicChipid)) {
				WMT_INFO_FUNC("wmt-core: disable wifi 5G support\n");
				ctrlPa1 = WIFI_5G_PALDO;
				ctrlPa2 = PALDO_OFF;
				wmt_core_ctrl(WMT_CTRL_SOC_PALDO_CTRL, &ctrlPa1, &ctrlPa2);
			} else {
				WMT_WARN_FUNC("wmt-core: unknown PMIC chipid\n");
			}
			break;
		default:
			WMT_WARN_FUNC("wmt-core: unknown A die chipid(0x%x)\n", aDieChipid);
			break;
		}
	}
#endif

#if 1
	ctrlData.ctrlId = WMT_CTRL_SET_STP_DBG_INFO;
	ctrlData.au4CtrlData[0] = wmt_ic_ops_soc.icId;
	ctrlData.au4CtrlData[1] = (SIZE_T) gp_soc_info->cChipVersion;
	ctrlData.au4CtrlData[2] = (SIZE_T) &gp_soc_patch_info;
	iRet = wmt_ctrl(&ctrlData);
	if (iRet) {
		WMT_ERR_FUNC("set dump info fail(%d)\n", iRet);
		return -19;
	}
#endif

#if CFG_WMT_PS_SUPPORT
	osal_assert(NULL != gp_soc_info);
	if (NULL != gp_soc_info) {
		if (MTK_WCN_BOOL_FALSE != gp_soc_info->bPsmSupport)
			wmt_lib_ps_enable();
		else
			wmt_lib_ps_disable();
	}
#endif

	return 0;
}

static INT32 mtk_wcn_soc_sw_deinit(P_WMT_HIF_CONF pWmtHifConf)
{
	WMT_DBG_FUNC(" start\n");

#if CFG_WMT_PS_SUPPORT
	osal_assert(NULL != gp_soc_info);
	if ((NULL != gp_soc_info)
	    && (MTK_WCN_BOOL_FALSE != gp_soc_info->bPsmSupport)) {
		wmt_lib_ps_disable();
	}
#endif

	gp_soc_info = NULL;

	return 0;
}

static INT32 mtk_wcn_soc_aif_ctrl(WMT_IC_PIN_STATE state, UINT32 flag)
{
	INT32 ret = -1;
	UINT32 val;

	if ((flag & WMT_LIB_AIF_FLAG_MASK) == WMT_LIB_AIF_FLAG_SHARE) {
		WMT_INFO_FUNC("PCM & I2S PIN SHARE\n");
#if 0
		switch (state) {
		case WMT_IC_AIF_0:
			/* BT_PCM_OFF & FM line in/out */
			val = 0x00000770;
			ret = wmt_core_reg_rw_raw(1, 0x80050140, &val, 0x00000FF0);
			val = 0x00000000;
			ret += wmt_core_reg_rw_raw(1, 0x80050150, &val, 0x00000800);
			break;

		case WMT_IC_AIF_1:
			/* BT_PCM_ON & FM line in/out */
			val = 0x00000700;
			ret = wmt_core_reg_rw_raw(1, 0x80050140, &val, 0x00000FF0);
			val = 0x00000000;
			ret += wmt_core_reg_rw_raw(1, 0x80050150, &val, 0x00000800);
			break;

		case WMT_IC_AIF_2:
			/* BT_PCM_OFF & FM I2S */
			val = 0x00000710;
			ret = wmt_core_reg_rw_raw(1, 0x80050140, &val, 0x00000FF0);
			val = 0x00000800;	/* 800:3-wire, 000: 4-wire */
			ret += wmt_core_reg_rw_raw(1, 0x80050150, &val, 0x00000800);
			break;
		default:
			WMT_ERR_FUNC("unsupported state (%d)\n", state);
			ret = -1;
			break;
		}
#else
		WMT_WARN_FUNC("TBD!!");
		ret = 0;
#endif
	} else {
		/*PCM & I2S separate */
		WMT_INFO_FUNC("PCM & I2S PIN SEPARATE\n");
#if 0
		switch (state) {
		case WMT_IC_AIF_0:
			/* BT_PCM_OFF & FM line in/out */
			val = 0x00000770;
			ret = wmt_core_reg_rw_raw(1, 0x80050140, &val, 0x00000FF0);
			val = 0x00000000;
			ret += wmt_core_reg_rw_raw(1, 0x80050150, &val, 0x00000800);
			break;

		case WMT_IC_AIF_1:
			/* BT_PCM_ON & FM line in/out */
			val = 0x00000700;
			ret = wmt_core_reg_rw_raw(1, 0x80050140, &val, 0x00000FF0);
			val = 0x00000000;
			ret += wmt_core_reg_rw_raw(1, 0x80050150, &val, 0x00000800);
			break;

		case WMT_IC_AIF_2:
			/* BT_PCM_OFF & FM I2S */
			val = 0x00000070;
			ret = wmt_core_reg_rw_raw(1, 0x80050140, &val, 0x00000FF0);
			val = 0x00000800;	/* 800:3-wire, 000: 4-wire */
			ret += wmt_core_reg_rw_raw(1, 0x80050150, &val, 0x00000800);

			break;
		case WMT_IC_AIF_3:
			val = 0x00000000;
			ret = wmt_core_reg_rw_raw(1, 0x80050140, &val, 0x00000FF0);
			val = 0x00000800;	/* 800:3-wire, 000: 4-wire */
			ret += wmt_core_reg_rw_raw(1, 0x80050150, &val, 0x00000800);

			break;
		default:
			WMT_ERR_FUNC("unsupported state (%d)\n", state);
			ret = -1;
			break;
		}
#else
		switch (state) {
		case WMT_IC_AIF_0:
			/* BT_PCM_OFF & FM line in/out */
			ret = 0;
			break;
		case WMT_IC_AIF_1:
			/* BT_PCM_ON & FM line in/out */
			ret = 0;
			break;

		case WMT_IC_AIF_2:
			/* BT_PCM_OFF & FM I2S */
			val = 0x01110000;
			ret = wmt_core_reg_rw_raw(1, 0x80050078, &val, 0x0FFF0000);

			break;
		case WMT_IC_AIF_3:
			ret = 0;
			break;

		default:
			WMT_ERR_FUNC("unsupported state (%d)\n", state);
			ret = -1;
			break;
		}
#endif
	}

	if (!ret)
		WMT_WARN_FUNC("new state(%d) fail(%d)\n", state, ret);
	WMT_INFO_FUNC("new state(%d) ok\n", state);

	return ret;
}

static INT32 mtk_wcn_soc_gps_sync_ctrl(WMT_IC_PIN_STATE state, UINT32 flag)
{
	INT32 iRet = -1;
	UINT32 uVal = 0;

	/* mt6797 can not access reg:0x80050078 and no need to do GPS SYNC */
	if (0x0279 != wmt_ic_ops_soc.icId) {
		if (WMT_IC_PIN_MUX == state)
			uVal = 0x1 << 28;
		else
			uVal = 0x5 << 28;
		iRet = wmt_core_reg_rw_raw(1, 0x80050078, &uVal, 0x7 << 28);
		if (iRet)
			WMT_ERR_FUNC("gps_sync pin ctrl failed, iRet(%d)\n", iRet);
	} else
		WMT_INFO_FUNC("This chip no need to sync GPS and MODEM!\n");

	/* anyway, we return 0 */
	return 0;
}

static INT32 mtk_wcn_soc_pin_ctrl(WMT_IC_PIN_ID id, WMT_IC_PIN_STATE state, UINT32 flag)
{
	INT32 ret;

	WMT_DBG_FUNC("ic pin id:%d, state:%d, flag:0x%x\n", id, state, flag);

	ret = -1;
	switch (id) {
	case WMT_IC_PIN_AUDIO:
		ret = mtk_wcn_soc_aif_ctrl(state, flag);
		break;

	case WMT_IC_PIN_EEDI:
		WMT_WARN_FUNC("TBD!!");
		/* We just return 0 here, prevent from WMT-FUNC do other register read/write */
		ret = 0;
		break;

	case WMT_IC_PIN_EEDO:
		WMT_WARN_FUNC("TBD!!");
		/* We just return 0 here, prevent from WMT-FUNC do other register read/write */
		ret = 0;
		break;
	case WMT_IC_PIN_GSYNC:
		ret = mtk_wcn_soc_gps_sync_ctrl(state, flag);
		break;
	default:
		break;
	}
	WMT_INFO_FUNC("ret = (%d)\n", ret);

	return ret;
}

INT32 mtk_wcn_soc_co_clock_ctrl(WMT_CO_CLOCK on)
{
	INT32 iRet = 0;

	if ((WMT_CO_CLOCK_DIS <= on) && (WMT_CO_CLOCK_MAX > on)) {
		gCoClockEn = on;
	} else {
		WMT_DBG_FUNC("0x%x: error parameter:%d\n", wmt_ic_ops_soc.icId, on);
		iRet = -1;
	}
	WMT_DBG_FUNC("0x%x: Co-clock %s\n", wmt_ic_ops_soc.icId,
		     (gCoClockEn == WMT_CO_CLOCK_DIS) ? "disabled" : "enabled");

	return iRet;
}

static MTK_WCN_BOOL mtk_wcn_soc_quick_sleep_flag_get(VOID)
{
	return MTK_WCN_BOOL_TRUE;
}

static MTK_WCN_BOOL mtk_wcn_soc_aee_dump_flag_get(VOID)
{
	return MTK_WCN_BOOL_FALSE;
}

WMT_CO_CLOCK mtk_wcn_soc_co_clock_get(VOID)
{
	return gCoClockEn;
}

static INT32 mtk_wcn_soc_ver_check(VOID)
{
	UINT32 hw_ver;
	UINT32 fw_ver;
	INT32 iret;
	const WMT_IC_INFO_S *p_info;
	unsigned long ctrlPa1;
	unsigned long ctrlPa2;

	/* 1. identify chip versions: HVR(HW_VER) and FVR(FW_VER) */
	WMT_LOUD_FUNC("0x%x: before read hw_ver (hw version)\n", wmt_ic_ops_soc.icId);
	iret = wmt_core_reg_rw_raw(0, GEN_HVR, &hw_ver, GEN_VER_MASK);
	if (iret) {
		WMT_ERR_FUNC("0x%x: read hw_ver fail:%d\n", wmt_ic_ops_soc.icId, iret);
		return -2;
	}
	WMT_WARN_FUNC("0x%x: read hw_ver (hw version) (0x%x)\n", wmt_ic_ops_soc.icId, hw_ver);

	WMT_LOUD_FUNC("0x%x: before fw_ver (rom version)\n", wmt_ic_ops_soc.icId);
	wmt_core_reg_rw_raw(0, GEN_FVR, &fw_ver, GEN_VER_MASK);
	if (iret) {
		WMT_ERR_FUNC("0x%x: read fw_ver fail:%d\n", wmt_ic_ops_soc.icId, iret);
		return -2;
	}
	WMT_WARN_FUNC("0x%x: read fw_ver (rom version) (0x%x)\n", wmt_ic_ops_soc.icId, fw_ver);

	p_info = mtk_wcn_soc_find_wmt_ic_info(hw_ver);
	if (NULL == p_info) {
		WMT_ERR_FUNC("0x%x: hw_ver(0x%x) find wmt ic info fail\n", wmt_ic_ops_soc.icId);
		return -3;
	}
	WMT_WARN_FUNC("0x%x: ic info: %s.%s (0x%x/0x%x, WMTHWVER:%d, patch_ext:%s)\n",
		      wmt_ic_ops_soc.icId, p_info->cChipName, p_info->cChipVersion,
		      hw_ver, fw_ver, p_info->eWmtHwVer, p_info->cPatchNameExt);

	/* hw id & version */
	ctrlPa1 = (wmt_ic_ops_soc.icId << 16) | (hw_ver & 0x0000FFFF);
	/* translated hw version & fw rom version */
	ctrlPa2 = ((UINT32) (p_info->eWmtHwVer) << 16) | (fw_ver & 0x0000FFFF);

	iret = wmt_core_ctrl(WMT_CTRL_HWIDVER_SET, &ctrlPa1, &ctrlPa2);
	if (iret)
		WMT_WARN_FUNC("0x%x: WMT_CTRL_HWIDVER_SET fail(%d)\n", wmt_ic_ops_soc.icId, iret);

	gp_soc_info = p_info;
	return 0;
}

static const WMT_IC_INFO_S *mtk_wcn_soc_find_wmt_ic_info(const UINT32 hw_ver)
{
	/* match chipversion with u4HwVer item in mtk_wcn_soc_info_table */
	const UINT32 size = osal_array_size(mtk_wcn_soc_info_table);
	INT32 index = 0;

	/* George: reverse the search order to favor newer version products
	* TODO:[FixMe][GeorgeKuo] Remove full match once API wmt_lib_get_hwver()
	* is changed correctly in the future!!
	* Leave full match here is a workaround for GPS to distinguish E3/E4 ICs.
	*/
	index = size - 1;
	/* full match */
	while ((0 <= index) && (hw_ver != mtk_wcn_soc_info_table[index].u4HwVer))
		--index;
	if (0 <= index) {
		WMT_DBG_FUNC("found ic info(0x%x) by full match! index:%d\n", hw_ver, index);
		return &mtk_wcn_soc_info_table[index];
	}

	WMT_WARN_FUNC("find no ic info for (0x%x) by full match!try major num match!\n", hw_ver);

	/* George: The ONLY CORRECT method to find supported hw table. Match MAJOR
	 * NUM only can help us support future minor hw ECO, or fab switch, etc.
	 * FULL matching eliminate such flexibility and software package have to be
	 * updated EACH TIME even when minor hw ECO or fab switch!!!
	 */
	/* George: reverse the search order to favor newer version products */
	index = size - 1;
	/* major num match */
	while ((0 <= index) &&
		(MAJORNUM(hw_ver) != MAJORNUM(mtk_wcn_soc_info_table[index].u4HwVer))) {
		--index;
	}
	if (0 <= index) {
		WMT_DBG_FUNC("0x%x: found ic info for hw_ver(0x%x) by major num! index:%d\n",
			wmt_ic_ops_soc.icId, hw_ver, index);
		return &mtk_wcn_soc_info_table[index];
	}

	WMT_ERR_FUNC("0x%x: find no ic info for hw_ver(0x%x) by full match nor major num match!\n",
			wmt_ic_ops_soc.icId, hw_ver);
	WMT_ERR_FUNC("Set default chip version: E1!\n");
	return &mtk_wcn_soc_info_table[0];
}

#if CFG_WMT_FILTER_MODE_SETTING
static INT32 wmt_stp_wifi_lte_coex(VOID)
{
	INT32 iRet;
	unsigned long addr;
	WMT_GEN_CONF *pWmtGenConf;

	/*Get wmt config */
	iRet = wmt_core_ctrl(WMT_CTRL_GET_WMT_CONF, &addr, 0);
	if (iRet) {
		WMT_ERR_FUNC("ctrl GET_WMT_CONF fail(%d)\n", iRet);
		return -2;
	}
	WMT_DBG_FUNC("ctrl GET_WMT_CONF ok(0x%08lx)\n", addr);

	pWmtGenConf = (P_WMT_GEN_CONF) addr;

	/*Check if WMT.cfg exists */
	if (pWmtGenConf->cfgExist == 0) {
		WMT_INFO_FUNC("cfgExist == 0, skip config chip\n");
		/*if WMT.cfg not existed, still return success and adopt the default value */
		return 0;
	}

	osal_sleep_ms(5);

	if (pWmtGenConf->coex_wmt_filter_mode == 0) {
		if ((0x6752 == wmt_ic_ops_soc.icId) ||
		    (0x6580 == wmt_ic_ops_soc.icId) ||
		    (0x8163 == wmt_ic_ops_soc.icId) ||
		    (0x0326 == wmt_ic_ops_soc.icId) ||
		    (0x0321 == wmt_ic_ops_soc.icId) ||
		    (0x0335 == wmt_ic_ops_soc.icId) || (0x0337 == wmt_ic_ops_soc.icId)) {
			iRet =
			    wmt_core_init_script(set_wifi_lte_coex_table_1, osal_array_size(set_wifi_lte_coex_table_1));
			WMT_DBG_FUNC("wmt_core:set_wifi_lte_coex_table_1 %s(%d)\n", iRet ? "fail" : "ok", iRet);
		} else if (0x0279 == wmt_ic_ops_soc.icId) {
			/* add WMT_COXE_CONFIG_EXT_COMPONENT_OPCODE command for 2G4 eLNA demand*/
			if (pWmtGenConf->coex_wmt_ext_component) {
				WMT_INFO_FUNC("coex_wmt_ext_component:0x%x\n", pWmtGenConf->coex_wmt_ext_component);
				set_wifi_lte_coex_table_2[0].cmd[5] = pWmtGenConf->coex_wmt_ext_component;
			}
			iRet =
			    wmt_core_init_script(set_wifi_lte_coex_table_2, osal_array_size(set_wifi_lte_coex_table_2));
			WMT_DBG_FUNC("wmt_core:set_wifi_lte_coex_table_2 %s(%d)\n", iRet ? "fail" : "ok", iRet);
		} else {
			iRet =
			    wmt_core_init_script(set_wifi_lte_coex_table_0, osal_array_size(set_wifi_lte_coex_table_0));
			WMT_DBG_FUNC("wmt_core:set_wifi_lte_coex_table_0 %s(%d)\n", iRet ? "fail" : "ok", iRet);
		}
	}

	return iRet;
}
#endif

static INT32 wmt_stp_init_coex(VOID)
{
	INT32 iRet;
	unsigned long addr;
	WMT_GEN_CONF *pWmtGenConf;

#define COEX_WMT  0

#if CFG_SUBSYS_COEX_NEED
	/* no need for MT6628 */
#define COEX_BT   1
#define COEX_WIFI 2
#define COEX_PTA  3
#define COEX_MISC 4
#endif
	/*Get wmt config */
	iRet = wmt_core_ctrl(WMT_CTRL_GET_WMT_CONF, &addr, 0);
	if (iRet) {
		WMT_ERR_FUNC("ctrl GET_WMT_CONF fail(%d)\n", iRet);
		return -2;
	}
	WMT_INFO_FUNC("ctrl GET_WMT_CONF ok(0x%08lx)\n", addr);

	pWmtGenConf = (P_WMT_GEN_CONF) addr;

	/*Check if WMT.cfg exists */
	if (pWmtGenConf->cfgExist == 0) {
		WMT_INFO_FUNC("cfgExist == 0, skip config chip\n");
		/*if WMT.cfg not existed, still return success and adopt the default value */
		return 0;
	}

	/*Dump the coex-related info */
	WMT_DBG_FUNC("coex_wmt:0x%x\n", pWmtGenConf->coex_wmt_ant_mode);
#if CFG_SUBSYS_COEX_NEED
	WMT_DBG_FUNC("coex_bt:0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		     pWmtGenConf->coex_bt_rssi_upper_limit,
		     pWmtGenConf->coex_bt_rssi_mid_limit,
		     pWmtGenConf->coex_bt_rssi_lower_limit,
		     pWmtGenConf->coex_bt_pwr_high, pWmtGenConf->coex_bt_pwr_mid, pWmtGenConf->coex_bt_pwr_low);
	WMT_DBG_FUNC("coex_wifi:0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		     pWmtGenConf->coex_wifi_rssi_upper_limit,
		     pWmtGenConf->coex_wifi_rssi_mid_limit,
		     pWmtGenConf->coex_wifi_rssi_lower_limit,
		     pWmtGenConf->coex_wifi_pwr_high, pWmtGenConf->coex_wifi_pwr_mid, pWmtGenConf->coex_wifi_pwr_low);
	WMT_DBG_FUNC("coex_ext_pta:0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		     pWmtGenConf->coex_ext_pta_hi_tx_tag,
		     pWmtGenConf->coex_ext_pta_hi_rx_tag,
		     pWmtGenConf->coex_ext_pta_lo_tx_tag,
		     pWmtGenConf->coex_ext_pta_lo_rx_tag,
		     pWmtGenConf->coex_ext_pta_sample_t1,
		     pWmtGenConf->coex_ext_pta_sample_t2, pWmtGenConf->coex_ext_pta_wifi_bt_con_trx);
	WMT_DBG_FUNC("coex_misc:0x%x 0x%x 0x%x\n",
		     pWmtGenConf->coex_misc_ext_pta_on, pWmtGenConf->coex_misc_ext_feature_set);
#endif

	/*command adjustion due to WMT.cfg */
	coex_table[COEX_WMT].cmd[5] = pWmtGenConf->coex_wmt_ant_mode;
	if (gWmtDbgLvl >= WMT_LOG_DBG)
		wmt_core_dump_data(&coex_table[COEX_WMT].cmd[0], coex_table[COEX_WMT].str, coex_table[COEX_WMT].cmdSz);

#if CFG_SUBSYS_COEX_NEED
	coex_table[COEX_BT].cmd[9] = pWmtGenConf->coex_bt_rssi_upper_limit;
	coex_table[COEX_BT].cmd[10] = pWmtGenConf->coex_bt_rssi_mid_limit;
	coex_table[COEX_BT].cmd[11] = pWmtGenConf->coex_bt_rssi_lower_limit;
	coex_table[COEX_BT].cmd[12] = pWmtGenConf->coex_bt_pwr_high;
	coex_table[COEX_BT].cmd[13] = pWmtGenConf->coex_bt_pwr_mid;
	coex_table[COEX_BT].cmd[14] = pWmtGenConf->coex_bt_pwr_low;
	if (gWmtDbgLvl >= WMT_LOG_DBG)
		wmt_core_dump_data(&coex_table[COEX_BT].cmd[0], coex_table[COEX_BT].str, coex_table[COEX_BT].cmdSz);

	coex_table[COEX_WIFI].cmd[10] = pWmtGenConf->coex_wifi_rssi_upper_limit;
	coex_table[COEX_WIFI].cmd[11] = pWmtGenConf->coex_wifi_rssi_mid_limit;
	coex_table[COEX_WIFI].cmd[12] = pWmtGenConf->coex_wifi_rssi_lower_limit;
	coex_table[COEX_WIFI].cmd[13] = pWmtGenConf->coex_wifi_pwr_high;
	coex_table[COEX_WIFI].cmd[14] = pWmtGenConf->coex_wifi_pwr_mid;
	coex_table[COEX_WIFI].cmd[15] = pWmtGenConf->coex_wifi_pwr_low;
	if (gWmtDbgLvl >= WMT_LOG_DBG)
		wmt_core_dump_data(&coex_table[COEX_WIFI].cmd[0],
				   coex_table[COEX_WIFI].str, coex_table[COEX_WIFI].cmdSz);

	coex_table[COEX_PTA].cmd[5] = pWmtGenConf->coex_ext_pta_hi_tx_tag;
	coex_table[COEX_PTA].cmd[6] = pWmtGenConf->coex_ext_pta_hi_rx_tag;
	coex_table[COEX_PTA].cmd[7] = pWmtGenConf->coex_ext_pta_lo_tx_tag;
	coex_table[COEX_PTA].cmd[8] = pWmtGenConf->coex_ext_pta_lo_rx_tag;
	coex_table[COEX_PTA].cmd[9] = ((pWmtGenConf->coex_ext_pta_sample_t1 & 0xff00) >> 8);
	coex_table[COEX_PTA].cmd[10] = ((pWmtGenConf->coex_ext_pta_sample_t1 & 0x00ff) >> 0);
	coex_table[COEX_PTA].cmd[11] = ((pWmtGenConf->coex_ext_pta_sample_t2 & 0xff00) >> 8);
	coex_table[COEX_PTA].cmd[12] = ((pWmtGenConf->coex_ext_pta_sample_t2 & 0x00ff) >> 0);
	coex_table[COEX_PTA].cmd[13] = pWmtGenConf->coex_ext_pta_wifi_bt_con_trx;
	if (gWmtDbgLvl >= WMT_LOG_DBG)
		wmt_core_dump_data(&coex_table[COEX_PTA].cmd[0], coex_table[COEX_PTA].str, coex_table[COEX_PTA].cmdSz);

	osal_memcpy(&coex_table[COEX_MISC].cmd[5], &pWmtGenConf->coex_misc_ext_pta_on,
		    sizeof(pWmtGenConf->coex_misc_ext_pta_on));
	osal_memcpy(&coex_table[COEX_MISC].cmd[9], &pWmtGenConf->coex_misc_ext_feature_set,
		    sizeof(pWmtGenConf->coex_misc_ext_feature_set));

	wmt_core_dump_data(&coex_table[COEX_MISC].cmd[0], coex_table[COEX_MISC].str, coex_table[COEX_MISC].cmdSz);
#endif

	iRet = wmt_core_init_script(coex_table, sizeof(coex_table) / sizeof(coex_table[0]));

	return iRet;
}

#if CFG_WMT_SDIO_DRIVING_SET
static INT32 mtk_wcn_soc_set_sdio_driving(void)
{
	INT32 ret = 0;

	unsigned long addr;
	WMT_GEN_CONF *pWmtGenConf;
	UINT32 drv_val = 0;

	/*Get wmt config */
	ret = wmt_core_ctrl(WMT_CTRL_GET_WMT_CONF, &addr, 0);
	if (ret) {
		WMT_ERR_FUNC("ctrl GET_WMT_CONF fail(%d)\n", ret);
		return -1;
	}
	WMT_INFO_FUNC("ctrl GET_WMT_CONF ok(0x%08lx)\n", addr);

	pWmtGenConf = (P_WMT_GEN_CONF) addr;

	/*Check if WMT.cfg exists */
	if (pWmtGenConf->cfgExist == 0) {
		WMT_INFO_FUNC("cfgExist == 0, skip config chip\n");
		/*if WMT.cfg not existed, still return success and adopt the default value */
		return 0;
	}

	drv_val = pWmtGenConf->sdio_driving_cfg;

	/*Dump the sdio driving related info */
	WMT_INFO_FUNC("sdio driving:0x%x\n", drv_val);

	sdio_driving_table[0].cmd[12] = (UINT8) ((drv_val & 0x00000077UL) >> 0);	/* DAT0 and DAT1 */
	sdio_driving_table[0].cmd[13] = (UINT8) ((drv_val & 0x00007700UL) >> 8);	/* DAT2 and DAT3 */
	sdio_driving_table[0].cmd[14] = (UINT8) ((drv_val & 0x00070000UL) >> 16);	/* CMD */

	ret = wmt_core_init_script(sdio_driving_table, sizeof(sdio_driving_table) / sizeof(sdio_driving_table[0]));

	return ret;
}
#endif

#if CFG_WMT_CRYSTAL_TIMING_SET
static INT32 mtk_wcn_soc_crystal_triming_set(VOID)
{
	INT32 iRet = 0;
	PUINT8 pbuf = NULL;
	UINT32 bufLen = 0;
	WMT_CTRL_DATA ctrlData;
	UINT32 uCryTimOffset = 0x6D;
	MTK_WCN_BOOL bIsNvramExist = MTK_WCN_BOOL_FALSE;
	INT8 cCrystalTimingOffset = 0x0;
	UINT8 cCrystalTiming = 0x0;
	INT32 iCrystalTiming = 0x0;
	MTK_WCN_BOOL bIsCrysTrimEnabled = MTK_WCN_BOOL_FALSE;
	UINT32 u4Res;

	bIsNvramExist = MTK_WCN_BOOL_FALSE;
	 /**/ ctrlData.ctrlId = WMT_CTRL_CRYSTAL_TRIMING_GET;
	ctrlData.au4CtrlData[0] = (UINT32) "/data/nvram/APCFG/APRDEB/WIFI";
	ctrlData.au4CtrlData[1] = (UINT32) &pbuf;
	ctrlData.au4CtrlData[2] = (UINT32) &bufLen;

	iRet = wmt_ctrl(&ctrlData);
	if (0 != iRet) {
		WMT_ERR_FUNC("0x%x: WMT_CTRL_CRYSTAL_TRIMING_GET fail:%d\n", wmt_ic_ops_soc.icId, iRet);
		bIsNvramExist = MTK_WCN_BOOL_FALSE;
		bIsCrysTrimEnabled = MTK_WCN_BOOL_FALSE;
		cCrystalTimingOffset = 0x0;
		cCrystalTiming = 0x0;
		iRet = -1;
	} else {
		WMT_DBG_FUNC("0x%x: nvram pBuf(0x%08x), bufLen(%d)\n", wmt_ic_ops_soc.icId, pbuf, bufLen);
		if (bufLen < (uCryTimOffset + 1)) {
			WMT_ERR_FUNC("0x%x: nvram len(%d) too short, crystalTimging value offset(%d)\n",
				     wmt_ic_ops_soc.icId, bufLen, uCryTimOffset);
			bIsNvramExist = MTK_WCN_BOOL_FALSE;
			bIsCrysTrimEnabled = MTK_WCN_BOOL_FALSE;
			cCrystalTimingOffset = 0x0;
			cCrystalTiming = 0x0;
		} else {
			bIsNvramExist = MTK_WCN_BOOL_TRUE;
			cCrystalTimingOffset = *(pbuf + uCryTimOffset);
			if (cCrystalTimingOffset & 0x80) {
				bIsCrysTrimEnabled = MTK_WCN_BOOL_TRUE;
				cCrystalTimingOffset = (UINT8) cCrystalTimingOffset & 0x7f;
			}
			WMT_DBG_FUNC("cCrystalTimingOffset (%d), bIsCrysTrimEnabled(%d)\n", cCrystalTimingOffset,
				     bIsCrysTrimEnabled);
		}
		ctrlData.ctrlId = WMT_CTRL_CRYSTAL_TRIMING_PUT;
		ctrlData.au4CtrlData[0] = (UINT32) "/data/nvram/APCFG/APRDEB/WIFI";
		iRet = wmt_ctrl(&ctrlData);
		if (0 != iRet) {
			WMT_ERR_FUNC("0x%x: WMT_CTRL_CRYSTAL_TRIMING_PUT fail:%d\n", wmt_ic_ops_soc.icId, iRet);
			iRet = -2;
		} else {
			WMT_DBG_FUNC("0x%x: WMT_CTRL_CRYSTAL_TRIMING_PUT succeed\n", wmt_ic_ops_soc.icId);
		}
	}
	if ((MTK_WCN_BOOL_TRUE == bIsNvramExist) && (MTK_WCN_BOOL_TRUE == bIsCrysTrimEnabled)) {
		/*get CrystalTiming value before set it */
		iRet =
		    wmt_core_tx(get_crystal_timing_script[0].cmd, get_crystal_timing_script[0].cmdSz, &u4Res,
				MTK_WCN_BOOL_FALSE);
		if (iRet || (u4Res != get_crystal_timing_script[0].cmdSz)) {
			WMT_ERR_FUNC("WMT-CORE: write (%s) iRet(%d) cmd len err(%d, %d)\n",
				     get_crystal_timing_script[0].str, iRet, u4Res, get_crystal_timing_script[0].cmdSz);
			iRet = -3;
			goto done;
		}
		/* EVENT BUF */
		osal_memset(get_crystal_timing_script[0].evt, 0, get_crystal_timing_script[0].evtSz);
		iRet = wmt_core_rx(get_crystal_timing_script[0].evt, get_crystal_timing_script[0].evtSz, &u4Res);
		if (iRet || (u4Res != get_crystal_timing_script[0].evtSz)) {
			WMT_ERR_FUNC("WMT-CORE: read (%s) iRet(%d) evt len err(rx:%d, exp:%d)\n",
				     get_crystal_timing_script[0].str, iRet, u4Res, get_crystal_timing_script[0].evtSz);
			mtk_wcn_stp_dbg_dump_package();
			iRet = -4;
			goto done;
		}

		iCrystalTiming = WMT_GET_CRYSTAL_TRIMING_EVT[5] & 0x7f;
		if (cCrystalTimingOffset & 0x40) {
			/*nagative offset value */
			iCrystalTiming = iCrystalTiming + cCrystalTimingOffset - 128;
		} else {
			iCrystalTiming += cCrystalTimingOffset;
		}
		WMT_DBG_FUNC("iCrystalTiming (0x%x)\n", iCrystalTiming);
		cCrystalTiming = iCrystalTiming > 0x7f ? 0x7f : iCrystalTiming;
		cCrystalTiming = iCrystalTiming < 0 ? 0 : iCrystalTiming;
		WMT_DBG_FUNC("cCrystalTiming (0x%x)\n", cCrystalTiming);
		/* set_crystal_timing_script */
		WMT_SET_CRYSTAL_TRIMING_CMD[5] = cCrystalTiming;
		WMT_GET_CRYSTAL_TRIMING_EVT[5] = cCrystalTiming;

		iRet = wmt_core_init_script(set_crystal_timing_script, osal_array_size(set_crystal_timing_script));
		if (iRet) {
			WMT_ERR_FUNC("set_crystal_timing_script fail(%d)\n", iRet);
			iRet = -5;
		} else {
			WMT_DBG_FUNC("set crystal timing value (0x%x) succeed\n", WMT_SET_CRYSTAL_TRIMING_CMD[5]);
			iRet =
			    wmt_core_init_script(get_crystal_timing_script, osal_array_size(get_crystal_timing_script));
			if (iRet) {
				WMT_ERR_FUNC("get_crystal_timing_script fail(%d)\n", iRet);
				iRet = -6;
			} else {
				WMT_INFO_FUNC("succeed, updated crystal timing value (0x%x)\n",
					      WMT_GET_CRYSTAL_TRIMING_EVT[5]);
				iRet = 0x0;
			}
		}
	}
done:
	return iRet;
}
#endif

#if CFG_WMT_MULTI_PATCH
static INT32 mtk_wcn_soc_patch_info_prepare(VOID)
{
	INT32 iRet = -1;
	WMT_CTRL_DATA ctrlData;

	ctrlData.ctrlId = WMT_CTRL_PATCH_SEARCH;
	iRet = wmt_ctrl(&ctrlData);

	return iRet;
}

static INT32 mtk_wcn_soc_patch_dwn(UINT32 index)
{
	INT32 iRet = -1;
	P_WMT_PATCH patchHdr;
	PUINT8 pbuf;
	UINT32 patchSize;
	UINT32 fragSeq;
	UINT32 fragNum;
	UINT16 fragSize = 0;
	UINT16 cmdLen;
	UINT32 offset;
	UINT32 u4Res;
	UINT8 evtBuf[8];
	UINT8 addressevtBuf[12];
	UINT8 addressByte[4];
	PINT8 cDataTime = NULL;
	/*PINT8 cPlat = NULL; */
	UINT16 u2HwVer = 0;
	UINT16 u2SwVer = 0;
	UINT32 u4PatchVer = 0;
	UINT32 patchSizePerFrag = 0;
	WMT_CTRL_DATA ctrlData;

	/*1.check hardware information */
	if (NULL == gp_soc_info) {
		WMT_ERR_FUNC("null gp_soc_info!\n");
		return -1;
	}

	osal_memset(gFullPatchName, 0, osal_sizeof(gFullPatchName));

	ctrlData.ctrlId = WMT_CTRL_GET_PATCH_INFO;
	ctrlData.au4CtrlData[0] = index + 1;
	ctrlData.au4CtrlData[1] = (SIZE_T) &gFullPatchName;
	ctrlData.au4CtrlData[2] = (SIZE_T) &addressByte;
	iRet = wmt_ctrl(&ctrlData);
	WMT_DBG_FUNC("the %d time valid patch found: (%s)\n", index + 1, gFullPatchName);

	/* <2.2> read patch content */
	ctrlData.ctrlId = WMT_CTRL_GET_PATCH;
	ctrlData.au4CtrlData[0] = (SIZE_T) NULL;
	ctrlData.au4CtrlData[1] = (SIZE_T) &gFullPatchName;
	ctrlData.au4CtrlData[2] = (SIZE_T) &pbuf;
	ctrlData.au4CtrlData[3] = (SIZE_T) &patchSize;
	iRet = wmt_ctrl(&ctrlData);
	if (iRet) {
		WMT_ERR_FUNC("wmt_core: WMT_CTRL_GET_PATCH fail:%d\n", iRet);
		iRet -= 1;
		goto done;
	}

	/* |<-BCNT_PATCH_BUF_HEADROOM(8) bytes dummy allocated->|<-patch file->| */
	pbuf += BCNT_PATCH_BUF_HEADROOM;
	/* patch file with header:
	 * |<-patch header: 28 Bytes->|<-patch body: X Bytes ----->|
	 */
	patchHdr = (P_WMT_PATCH) pbuf;
	/* check patch file information */

	cDataTime = patchHdr->ucDateTime;
	u2HwVer = patchHdr->u2HwVer;
	u2SwVer = patchHdr->u2SwVer;
	u4PatchVer = patchHdr->u4PatchVer;
	/*cPlat = &patchHdr->ucPLat[0]; */

	cDataTime[15] = '\0';
	if (index == 0) {
		WMT_DBG_FUNC("===========================================\n");
		WMT_INFO_FUNC("[Patch]BuiltTime = %s, HVer = 0x%x, SVer = 0x%x, PhVer = 0x%04x,Platform = %c%c%c%c\n",
		cDataTime, ((u2HwVer & 0x00ff) << 8) | ((u2HwVer & 0xff00) >> 8),
		((u2SwVer & 0x00ff) << 8) | ((u2SwVer & 0xff00) >> 8),
		((u4PatchVer & 0xff000000) >> 24) | ((u4PatchVer & 0x00ff0000) >> 16),
		patchHdr->ucPLat[0], patchHdr->ucPLat[1], patchHdr->ucPLat[2], patchHdr->ucPLat[3]);
		WMT_DBG_FUNC("[Consys Patch] Hw Ver = 0x%x\n", ((u2HwVer & 0x00ff) << 8) | ((u2HwVer & 0xff00) >> 8));
		WMT_DBG_FUNC("[Consys Patch] Sw Ver = 0x%x\n", ((u2SwVer & 0x00ff) << 8) | ((u2SwVer & 0xff00) >> 8));
		WMT_DBG_FUNC("[Consys Patch] Ph Ver = 0x%04x\n",
			      ((u4PatchVer & 0xff000000) >> 24) | ((u4PatchVer & 0x00ff0000) >> 16));
		WMT_DBG_FUNC("[Consys Patch] Platform = %c%c%c%c\n", patchHdr->ucPLat[0], patchHdr->ucPLat[1],
			      patchHdr->ucPLat[2], patchHdr->ucPLat[3]);
		WMT_DBG_FUNC("===========================================\n");
	}

	/* remove patch header:
	 * |<-patch body: X Bytes (X=patchSize)--->|
	 */
	patchSize -= sizeof(WMT_PATCH);
	pbuf += sizeof(WMT_PATCH);
	patchSizePerFrag = DEFAULT_PATCH_FRAG_SIZE;
	/* reserve 1st patch cmd space before patch body
	 *        |<-WMT_CMD: 5Bytes->|<-patch body: X Bytes (X=patchSize)----->|
	 */
	/* gp_soc_patch_info = patchHdr; */
	osal_memcpy(&gp_soc_patch_info, patchHdr, osal_sizeof(WMT_PATCH));
	pbuf -= sizeof(WMT_PATCH_CMD);

	fragNum = patchSize / patchSizePerFrag;
	fragNum += ((fragNum * patchSizePerFrag) == patchSize) ? 0 : 1;

	WMT_DBG_FUNC("patch size(%d) fragNum(%d)\n", patchSize, fragNum);

	/*send wmt part patch address command */
	if (0x6752 == wmt_ic_ops_soc.icId ||
	    0x8127 == wmt_ic_ops_soc.icId ||
	    0x7623 == wmt_ic_ops_soc.icId ||
	    0x6571 == wmt_ic_ops_soc.icId ||
	    0x0326 == wmt_ic_ops_soc.icId ||
	    0x0321 == wmt_ic_ops_soc.icId ||
	    0x0335 == wmt_ic_ops_soc.icId ||
	    0x0337 == wmt_ic_ops_soc.icId || 0x8163 == wmt_ic_ops_soc.icId || 0x6580 == wmt_ic_ops_soc.icId) {
		/* MT6571 patch RAM base */
		WMT_PATCH_ADDRESS_CMD[8] = 0x40;
		WMT_PATCH_P_ADDRESS_CMD[8] = 0xc8;
	}
	/*send wmt part patch address command */
	if (0x0279 == wmt_ic_ops_soc.icId) {
		/* MT6797 patch RAM base */
		WMT_PATCH_ADDRESS_CMD[8] = 0x08;
		WMT_PATCH_ADDRESS_CMD[9] = 0x05;
		WMT_PATCH_P_ADDRESS_CMD[8] = 0x2c;
		WMT_PATCH_P_ADDRESS_CMD[9] = 0x0b;
	}

	/*send wmt part patch address command */
	iRet =
	    wmt_core_tx((PUINT8) &WMT_PATCH_ADDRESS_CMD[0], sizeof(WMT_PATCH_ADDRESS_CMD), &u4Res, MTK_WCN_BOOL_FALSE);
	if (iRet || (u4Res != sizeof(WMT_PATCH_ADDRESS_CMD))) {
		WMT_ERR_FUNC("wmt_core:wmt patch address CMD fail(%d),size(%d)\n", iRet, u4Res);
		iRet -= 1;
		goto done;
	}
	osal_memset(addressevtBuf, 0, sizeof(addressevtBuf));
	iRet = wmt_core_rx(addressevtBuf, sizeof(WMT_PATCH_ADDRESS_EVT), &u4Res);
	if (iRet || (u4Res != sizeof(WMT_PATCH_ADDRESS_EVT))) {
		WMT_ERR_FUNC("wmt_core:wmt patch address EVT fail(%d),size(%d)\n", iRet, u4Res);
		iRet -= 1;
		goto done;
	}
#if CFG_CHECK_WMT_RESULT
	if (osal_memcmp(addressevtBuf, WMT_PATCH_ADDRESS_EVT, osal_sizeof(WMT_PATCH_ADDRESS_EVT)) != 0) {
		WMT_ERR_FUNC("wmt_core: write WMT_PATCH_ADDRESS_CMD status fail\n");
		iRet -= 1;
		goto done;
	}
#endif

	/*send part patch address command */
	osal_memcpy(&WMT_PATCH_P_ADDRESS_CMD[12], addressByte, osal_sizeof(addressByte));
	WMT_DBG_FUNC("4 bytes address command:0x%02x,0x%02x,0x%02x,0x%02x",
		      WMT_PATCH_P_ADDRESS_CMD[12],
		      WMT_PATCH_P_ADDRESS_CMD[13], WMT_PATCH_P_ADDRESS_CMD[14], WMT_PATCH_P_ADDRESS_CMD[15]);
	iRet =
	    wmt_core_tx((PUINT8) &WMT_PATCH_P_ADDRESS_CMD[0], sizeof(WMT_PATCH_P_ADDRESS_CMD), &u4Res,
			MTK_WCN_BOOL_FALSE);
	if (iRet || (u4Res != sizeof(WMT_PATCH_P_ADDRESS_CMD))) {
		WMT_ERR_FUNC("wmt_core:wmt part patch address CMD fail(%d),size(%d),index(%d)\n", iRet, u4Res, index);
		iRet -= 1;
		goto done;
	}
	osal_memset(addressevtBuf, 0, sizeof(addressevtBuf));
	iRet = wmt_core_rx(addressevtBuf, sizeof(WMT_PATCH_P_ADDRESS_EVT), &u4Res);
	if (iRet || (u4Res != sizeof(WMT_PATCH_P_ADDRESS_EVT))) {
		WMT_ERR_FUNC("wmt_core:wmt patch address EVT fail(%d),size(%d),index(%d)\n", iRet, u4Res, index);
		iRet -= 1;
		goto done;
	}
#if CFG_CHECK_WMT_RESULT
	if (osal_memcmp(addressevtBuf, WMT_PATCH_P_ADDRESS_EVT, osal_sizeof(WMT_PATCH_ADDRESS_EVT)) != 0) {
		WMT_ERR_FUNC("wmt_core: write WMT_PATCH_ADDRESS_CMD status fail,index(%d)\n", index);
		iRet -= 1;
		goto done;
	}
#endif

	/* send all fragments */
	offset = sizeof(WMT_PATCH_CMD);
	fragSeq = 0;
	while (fragSeq < fragNum) {
		WMT_DBG_FUNC("patch size(%d) fragNum(%d)\n", patchSize, fragNum);
		if (fragSeq == (fragNum - 1)) {
			/* last fragment */
			fragSize = patchSize - fragSeq * patchSizePerFrag;
			WMT_PATCH_CMD[4] = WMT_PATCH_FRAG_LAST;
		} else {
			fragSize = patchSizePerFrag;
			WMT_PATCH_CMD[4] = (fragSeq == 0) ? WMT_PATCH_FRAG_1ST : WMT_PATCH_FRAG_MID;
		}
		/* update length field in CMD:flag+frag */
		cmdLen = 1 + fragSize;
		osal_memcpy(&WMT_PATCH_CMD[2], &cmdLen, 2);
		/* copy patch CMD to buf (overwrite last 5-byte in prev frag) */
		osal_memcpy(pbuf + offset - sizeof(WMT_PATCH_CMD), WMT_PATCH_CMD, sizeof(WMT_PATCH_CMD));

		/* iRet =
		*(*kal_stp_tx)(pbuf + offset - sizeof(WMT_PATCH_CMD), fragSize + sizeof(WMT_PATCH_CMD),
		*&u4Res);
		*/
		iRet =
			wmt_core_tx(pbuf + offset - sizeof(WMT_PATCH_CMD), fragSize + sizeof(WMT_PATCH_CMD),
				&u4Res, MTK_WCN_BOOL_FALSE);
		if (iRet || (u4Res != fragSize + sizeof(WMT_PATCH_CMD))) {
			WMT_ERR_FUNC("wmt_core: write fragSeq(%d) size(%d, %d) fail(%d)\n", fragSeq,
				     fragSize + sizeof(WMT_PATCH_CMD), u4Res, iRet);
			iRet -= 1;
			break;
		}
		WMT_DBG_FUNC("wmt_core: write fragSeq(%d) size(%d, %d) ok\n",
			     fragSeq, fragSize + sizeof(WMT_PATCH_CMD), u4Res);

		osal_memset(evtBuf, 0, sizeof(evtBuf));
		/* iRet = (*kal_stp_rx)(evtBuf, sizeof(WMT_PATCH_EVT), &u4Res); */
		iRet = wmt_core_rx(evtBuf, sizeof(WMT_PATCH_EVT), &u4Res);
		if (iRet || (u4Res != sizeof(WMT_PATCH_EVT))) {
			WMT_ERR_FUNC("wmt_core: read WMT_PATCH_EVT length(%d, %d) fail(%d)\n", sizeof(WMT_PATCH_EVT),
				     u4Res, iRet);
			iRet -= 1;
			break;
		}
#if CFG_CHECK_WMT_RESULT
		if (osal_memcmp(evtBuf, WMT_PATCH_EVT, sizeof(WMT_PATCH_EVT)) != 0) {
			WMT_ERR_FUNC("wmt_core: compare WMT_PATCH_EVT error rx(%d):[%02X,%02X,%02X,%02X,%02X]\n",
				u4Res,
				evtBuf[0],
				evtBuf[1],
				evtBuf[2],
				evtBuf[3],
				evtBuf[4]);
			WMT_ERR_FUNC("wmt_core: exp(%d):[%02X,%02X,%02X,%02X,%02X]\n",
				sizeof(WMT_PATCH_EVT),
				WMT_PATCH_EVT[0],
				WMT_PATCH_EVT[1],
				WMT_PATCH_EVT[2],
				WMT_PATCH_EVT[3],
				WMT_PATCH_EVT[4]);
			iRet -= 1;
			break;
		}
#endif
		WMT_DBG_FUNC("wmt_core: read WMT_PATCH_EVT length(%d, %d) ok\n", sizeof(WMT_PATCH_EVT), u4Res);
		offset += patchSizePerFrag;
		++fragSeq;
	}

	WMT_WARN_FUNC("wmt_core: patch dwn:%d frag(%d, %d) %s\n",
		      iRet, fragSeq, fragSize, (!iRet && (fragSeq == fragNum)) ? "ok" : "fail");

	if (fragSeq != fragNum)
		iRet -= 1;
done:
	/* WMT_CTRL_FREE_PATCH always return 0 */
	/* wmt_core_ctrl(WMT_CTRL_FREE_PATCH, NULL, NULL); */
	ctrlData.ctrlId = WMT_CTRL_FREE_PATCH;
	ctrlData.au4CtrlData[0] = index + 1;
	wmt_ctrl(&ctrlData);

	return iRet;
}

#else
static INT32 mtk_wcn_soc_patch_dwn(VOID)
{
	INT32 iRet = -1;
	P_WMT_PATCH patchHdr;
	PUINT8 pbuf;
	UINT32 patchSize;
	UINT32 fragSeq;
	UINT32 fragNum;
	UINT16 fragSize = 0;
	UINT16 cmdLen;
	UINT32 offset;
	UINT32 u4Res;
	UINT8 evtBuf[8];
	PINT8 cDataTime = NULL;
	/*PINT8 cPlat = NULL; */
	UINT16 u2HwVer = 0;
	UINT16 u2SwVer = 0;
	UINT32 u4PatchVer = 0;
	UINT32 patchSizePerFrag = 0;
	WMT_CTRL_DATA ctrlData;

	/*1.check hardware information */
	if (NULL == gp_soc_info) {
		WMT_ERR_FUNC("null gp_soc_info!\n");
		return -1;
	}
	/* <2> search patch and read patch content */
	/* <2.1> search patch */
	ctrlData.ctrlId = WMT_CTRL_PATCH_SEARCH;
	iRet = wmt_ctrl(&ctrlData);
	if (0 == iRet) {
		/* patch with correct Hw Ver Major Num found */
		ctrlData.ctrlId = WMT_CTRL_GET_PATCH_NAME;
		ctrlData.au4CtrlData[0] = (UINT32) &gFullPatchName;
		iRet = wmt_ctrl(&ctrlData);

		WMT_INFO_FUNC("valid patch found: (%s)\n", gFullPatchName);
		/* <2.2> read patch content */
		ctrlData.ctrlId = WMT_CTRL_GET_PATCH;
		ctrlData.au4CtrlData[0] = (UINT32) NULL;
		ctrlData.au4CtrlData[1] = (UINT32) &gFullPatchName;

	} else {
		iRet -= 1;
		return iRet;
	}
	ctrlData.au4CtrlData[2] = (UINT32) &pbuf;
	ctrlData.au4CtrlData[3] = (UINT32) &patchSize;
	iRet = wmt_ctrl(&ctrlData);
	if (iRet) {
		WMT_ERR_FUNC("wmt_core: WMT_CTRL_GET_PATCH fail:%d\n", iRet);
		iRet -= 1;
		goto done;
	}

	/* |<-BCNT_PATCH_BUF_HEADROOM(8) bytes dummy allocated->|<-patch file->| */
	pbuf += BCNT_PATCH_BUF_HEADROOM;
	/* patch file with header:
	 * |<-patch header: 28 Bytes->|<-patch body: X Bytes ----->|
	 */
	patchHdr = (P_WMT_PATCH) pbuf;
	/* check patch file information */

	cDataTime = patchHdr->ucDateTime;
	u2HwVer = patchHdr->u2HwVer;
	u2SwVer = patchHdr->u2SwVer;
	u4PatchVer = patchHdr->u4PatchVer;
	/*cPlat = &patchHdr->ucPLat[0]; */

	cDataTime[15] = '\0';
	WMT_DBG_FUNC("===========================================\n");
	WMT_INFO_FUNC("[ConsysPatch]BuiltTime = %s, HVer = 0x%x, SVer = 0x%x, PhVer = 0x%04x,Platform = %c%c%c%c\n",
	cDataTime, ((u2HwVer & 0x00ff) << 8) | ((u2HwVer & 0xff00) >> 8),
	((u2SwVer & 0x00ff) << 8) | ((u2SwVer & 0xff00) >> 8),
	((u4PatchVer & 0xff000000) >> 24) | ((u4PatchVer & 0x00ff0000) >> 16),
	patchHdr->ucPLat[0], patchHdr->ucPLat[1], patchHdr->ucPLat[2], patchHdr->ucPLat[3]);
	WMT_DBG_FUNC("[Consys Patch] Hw Ver = 0x%x\n", ((u2HwVer & 0x00ff) << 8) | ((u2HwVer & 0xff00) >> 8));
	WMT_DBG_FUNC("[Consys Patch] Sw Ver = 0x%x\n", ((u2SwVer & 0x00ff) << 8) | ((u2SwVer & 0xff00) >> 8));
	WMT_DBG_FUNC("[Consys Patch] Ph Ver = 0x%04x\n",
		      ((u4PatchVer & 0xff000000) >> 24) | ((u4PatchVer & 0x00ff0000) >> 16));
	WMT_DBG_FUNC("[Consys Patch] Platform = %c%c%c%c\n", patchHdr->ucPLat[0], patchHdr->ucPLat[1],
		      patchHdr->ucPLat[2], patchHdr->ucPLat[3]);
	WMT_DBG_FUNC("===========================================\n");

	/* remove patch header:
	 * |<-patch body: X Bytes (X=patchSize)--->|
	 */
	patchSize -= sizeof(WMT_PATCH);
	pbuf += sizeof(WMT_PATCH);
	patchSizePerFrag = DEFAULT_PATCH_FRAG_SIZE;
	/* reserve 1st patch cmd space before patch body
	 *        |<-WMT_CMD: 5Bytes->|<-patch body: X Bytes (X=patchSize)----->|
	 */
	pbuf -= sizeof(WMT_PATCH_CMD);

	fragNum = patchSize / patchSizePerFrag;
	fragNum += ((fragNum * patchSizePerFrag) == patchSize) ? 0 : 1;

	WMT_DBG_FUNC("patch size(%d) fragNum(%d)\n", patchSize, fragNum);

	/* send all fragments */
	offset = sizeof(WMT_PATCH_CMD);
	fragSeq = 0;
	while (fragSeq < fragNum) {
		WMT_DBG_FUNC("patch size(%d) fragNum(%d)\n", patchSize, fragNum);
		if (fragSeq == (fragNum - 1)) {
			/* last fragment */
			fragSize = patchSize - fragSeq * patchSizePerFrag;
			WMT_PATCH_CMD[4] = WMT_PATCH_FRAG_LAST;
		} else {
			fragSize = patchSizePerFrag;
			WMT_PATCH_CMD[4] = (fragSeq == 0) ? WMT_PATCH_FRAG_1ST : WMT_PATCH_FRAG_MID;
		}
		/* update length field in CMD:flag+frag */
		cmdLen = 1 + fragSize;
		osal_memcpy(&WMT_PATCH_CMD[2], &cmdLen, 2);
		/* copy patch CMD to buf (overwrite last 5-byte in prev frag) */
		osal_memcpy(pbuf + offset - sizeof(WMT_PATCH_CMD), WMT_PATCH_CMD, sizeof(WMT_PATCH_CMD));

		/* iRet =
		*	(*kal_stp_tx)(pbuf + offset - sizeof(WMT_PATCH_CMD), fragSize + sizeof(WMT_PATCH_CMD),
		*	&u4Res);
		*/
		iRet =
		    wmt_core_tx(pbuf + offset - sizeof(WMT_PATCH_CMD), fragSize + sizeof(WMT_PATCH_CMD), &u4Res,
				MTK_WCN_BOOL_FALSE);
		if (iRet || (u4Res != fragSize + sizeof(WMT_PATCH_CMD))) {
			WMT_ERR_FUNC("wmt_core: write fragSeq(%d) size(%d, %d) fail(%d)\n", fragSeq,
				     fragSize + sizeof(WMT_PATCH_CMD), u4Res, iRet);
			iRet -= 1;
			break;
		}
		WMT_DBG_FUNC("wmt_core: write fragSeq(%d) size(%d, %d) ok\n",
			     fragSeq, fragSize + sizeof(WMT_PATCH_CMD), u4Res);

		osal_memset(evtBuf, 0, sizeof(evtBuf));
		/* iRet = (*kal_stp_rx)(evtBuf, sizeof(WMT_PATCH_EVT), &u4Res); */
		iRet = wmt_core_rx(evtBuf, sizeof(WMT_PATCH_EVT), &u4Res);
		if (iRet || (u4Res != sizeof(WMT_PATCH_EVT))) {
			WMT_ERR_FUNC("wmt_core: read WMT_PATCH_EVT length(%d, %d) fail(%d)\n", sizeof(WMT_PATCH_EVT),
				     u4Res, iRet);
			iRet -= 1;
			break;
		}
#if CFG_CHECK_WMT_RESULT
		if (osal_memcmp(evtBuf, WMT_PATCH_EVT, sizeof(WMT_PATCH_EVT)) != 0) {
			WMT_ERR_FUNC("wmt_core: compare WMT_PATCH_EVT error rx(%d):[%02X,%02X,%02X,%02X,%02X]\n",
				u4Res,
				evtBuf[0],
				evtBuf[1],
				evtBuf[2],
				evtBuf[3],
				evtBuf[4]);
			WMT_ERR_FUNC("wmt_core: exp(%d):[%02X,%02X,%02X,%02X,%02X]\n",
				sizeof(WMT_PATCH_EVT),
				WMT_PATCH_EVT[0],
				WMT_PATCH_EVT[1],
				WMT_PATCH_EVT[2],
				WMT_PATCH_EVT[3],
				WMT_PATCH_EVT[4]);
			iRet -= 1;
			break;
		}
#endif
		WMT_DBG_FUNC("wmt_core: read WMT_PATCH_EVT length(%d, %d) ok\n", sizeof(WMT_PATCH_EVT), u4Res);
		offset += patchSizePerFrag;
		++fragSeq;
	}

	WMT_WARN_FUNC("wmt_core: patch dwn:%d frag(%d, %d) %s\n",
		      iRet, fragSeq, fragSize, (!iRet && (fragSeq == fragNum)) ? "ok" : "fail");

	if (fragSeq != fragNum)
		iRet -= 1;
done:
	/* WMT_CTRL_FREE_PATCH always return 0 */
	wmt_core_ctrl(WMT_CTRL_FREE_PATCH, NULL, NULL);

	return iRet;
}

#endif
