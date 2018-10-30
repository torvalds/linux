/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __ODM_DBG_H__
#define __ODM_DBG_H__

/*#define DEBUG_VERSION	"1.1"*/ /*2015.07.29 YuChen*/
/*#define DEBUG_VERSION	"1.2"*/ /*2015.08.28 Dino*/
#define DEBUG_VERSION "1.3" /*2016.04.28 YuChen*/
#define ODM_DBG_TRACE 5

/*FW DBG MSG*/
#define RATE_DECISION BIT(0)
#define INIT_RA_TABLE BIT(1)
#define RATE_UP BIT(2)
#define RATE_DOWN BIT(3)
#define TRY_DONE BIT(4)
#define RA_H2C BIT(5)
#define F_RATE_AP_RPT BIT(7)

/* -----------------------------------------------------------------------------
 * Define the tracing components
 *
 * -----------------------------------------------------------------------------
 */
/*BB FW Functions*/
#define PHYDM_FW_COMP_RA BIT(0)
#define PHYDM_FW_COMP_MU BIT(1)
#define PHYDM_FW_COMP_PATH_DIV BIT(2)
#define PHYDM_FW_COMP_PHY_CONFIG BIT(3)

/*BB Driver Functions*/
#define ODM_COMP_DIG BIT(0)
#define ODM_COMP_RA_MASK BIT(1)
#define ODM_COMP_DYNAMIC_TXPWR BIT(2)
#define ODM_COMP_FA_CNT BIT(3)
#define ODM_COMP_RSSI_MONITOR BIT(4)
#define ODM_COMP_SNIFFER BIT(5)
#define ODM_COMP_ANT_DIV BIT(6)
#define ODM_COMP_DFS BIT(7)
#define ODM_COMP_NOISY_DETECT BIT(8)
#define ODM_COMP_RATE_ADAPTIVE BIT(9)
#define ODM_COMP_PATH_DIV BIT(10)
#define ODM_COMP_CCX BIT(11)

#define ODM_COMP_DYNAMIC_PRICCA BIT(12)
/*BIT13 TBD*/
#define ODM_COMP_MP BIT(14)
#define ODM_COMP_CFO_TRACKING BIT(15)
#define ODM_COMP_ACS BIT(16)
#define PHYDM_COMP_ADAPTIVITY BIT(17)
#define PHYDM_COMP_RA_DBG BIT(18)
#define PHYDM_COMP_TXBF BIT(19)
/* MAC Functions */
#define ODM_COMP_EDCA_TURBO BIT(20)
#define ODM_COMP_DYNAMIC_RX_PATH BIT(21)
#define ODM_FW_DEBUG_TRACE BIT(22)
/* RF Functions */
/*BIT23 TBD*/
#define ODM_COMP_TX_PWR_TRACK BIT(24)
/*BIT25 TBD*/
#define ODM_COMP_CALIBRATION BIT(26)
/* Common Functions */
/*BIT27 TBD*/
#define ODM_PHY_CONFIG BIT(28)
#define ODM_COMP_INIT BIT(29)
#define ODM_COMP_COMMON BIT(30)
#define ODM_COMP_API BIT(31)

#define ODM_COMP_UNCOND 0xFFFFFFFF

/*------------------------Export Marco Definition---------------------------*/

#define config_phydm_read_txagc_check(data) (data != INVALID_TXAGC_DATA)

#define ODM_RT_TRACE(dm, comp, fmt, ...)                                       \
	do {                                                                   \
		if (((comp) & dm->debug_components) ||                         \
		    ((comp) == ODM_COMP_UNCOND))                               \
			RT_TRACE(dm->adapter, COMP_PHYDM, DBG_DMESG, fmt,      \
				 ##__VA_ARGS__);                               \
	} while (0)

#define BB_DBGPORT_PRIORITY_3 3 /*Debug function (the highest priority)*/
#define BB_DBGPORT_PRIORITY_2 2 /*Check hang function & Strong function*/
#define BB_DBGPORT_PRIORITY_1 1 /*Watch dog function*/
#define BB_DBGPORT_RELEASE 0 /*Init value (the lowest priority)*/

void phydm_init_debug_setting(struct phy_dm_struct *dm);

u8 phydm_set_bb_dbg_port(void *dm_void, u8 curr_dbg_priority, u32 debug_port);

void phydm_release_bb_dbg_port(void *dm_void);

u32 phydm_get_bb_dbg_port_value(void *dm_void);

void phydm_basic_dbg_message(void *dm_void);

#define PHYDM_DBGPRINT 0
#define MAX_ARGC 20
#define MAX_ARGV 16
#define DCMD_DECIMAL "%d"
#define DCMD_CHAR "%c"
#define DCMD_HEX "%x"

#define PHYDM_SSCANF(x, y, z)                                                  \
	do {                                                                   \
		if (sscanf(x, y, z) != 1)                                      \
			ODM_RT_TRACE(dm, ODM_COMP_UNCOND,                      \
				     "%s:%d sscanf fail!", __func__,           \
				     __LINE__);                                \
	} while (0)

#define PHYDM_VAST_INFO_SNPRINTF(msg, ...)                                     \
	do {                                                                   \
		snprintf(msg, ##__VA_ARGS__);                                  \
		ODM_RT_TRACE(dm, ODM_COMP_UNCOND, output);                     \
	} while (0)

#if (PHYDM_DBGPRINT == 1)
#define PHYDM_SNPRINTF(msg, ...)                                               \
	do {                                                                   \
		snprintf(msg, ##__VA_ARGS__);                                  \
		ODM_RT_TRACE(dm, ODM_COMP_UNCOND, output);                     \
	} while (0)
#else
#define PHYDM_SNPRINTF(msg, ...)                                               \
	do {                                                                   \
		if (out_len > used)                                            \
			used += snprintf(msg, ##__VA_ARGS__);                  \
	} while (0)
#endif

void phydm_basic_profile(void *dm_void, u32 *_used, char *output,
			 u32 *_out_len);
s32 phydm_cmd(struct phy_dm_struct *dm, char *input, u32 in_len, u8 flag,
	      char *output, u32 out_len);
void phydm_cmd_parser(struct phy_dm_struct *dm, char input[][16], u32 input_num,
		      u8 flag, char *output, u32 out_len);

bool phydm_api_trx_mode(struct phy_dm_struct *dm, enum odm_rf_path tx_path,
			enum odm_rf_path rx_path, bool is_tx2_path);

void phydm_fw_trace_en_h2c(void *dm_void, bool enable, u32 fw_debug_component,
			   u32 monitor_mode, u32 macid);

void phydm_fw_trace_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len);

void phydm_fw_trace_handler_code(void *dm_void, u8 *buffer, u8 cmd_len);

void phydm_fw_trace_handler_8051(void *dm_void, u8 *cmd_buf, u8 cmd_len);

#endif /* __ODM_DBG_H__ */
