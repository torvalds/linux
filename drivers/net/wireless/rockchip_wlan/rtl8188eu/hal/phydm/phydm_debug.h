/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/


#ifndef	__ODM_DBG_H__
#define __ODM_DBG_H__

/*#define DEBUG_VERSION	"1.1"*/  /*2015.07.29 YuChen*/
/*#define DEBUG_VERSION	"1.2"*/  /*2015.08.28 Dino*/
#define DEBUG_VERSION	"1.3"  /*2016.04.28 YuChen*/
/* -----------------------------------------------------------------------------
 *	Define the debug levels
 *
 *	1.	DBG_TRACE and DBG_LOUD are used for normal cases.
 *	So that, they can help SW engineer to develope or trace states changed
 *	and also help HW enginner to trace every operation to and from HW,
 *	e.g IO, Tx, Rx.
 *
 *	2.	DBG_WARNNING and DBG_SERIOUS are used for unusual or error cases,
 *	which help us to debug SW or HW.
 *
 * -----------------------------------------------------------------------------
 *
 *	Never used in a call to ODM_RT_TRACE()!
 *   */
#define ODM_DBG_OFF					1

/*
 *	Fatal bug.
 *	For example, Tx/Rx/IO locked up, OS hangs, memory access violation,
 *	resource allocation failed, unexpected HW behavior, HW BUG and so on.
 *   */
#define ODM_DBG_SERIOUS				2

/*
 *	Abnormal, rare, or unexpeted cases.
 *	For example, IRP/Packet/OID canceled, device suprisely unremoved and so on.
 *   */
#define ODM_DBG_WARNING				3

/*
 *	Normal case with useful information about current SW or HW state.
 *	For example, Tx/Rx descriptor to fill, Tx/Rx descriptor completed status,
 *	SW protocol state change, dynamic mechanism state change and so on.
 *   */
#define ODM_DBG_LOUD					4

/*
 *	Normal case with detail execution flow or information.
 *   */
#define ODM_DBG_TRACE					5

/*FW DBG MSG*/
#define	RATE_DECISION	BIT(0)
#define	INIT_RA_TABLE	BIT(1)
#define	RATE_UP			BIT(2)
#define	RATE_DOWN		BIT(3)
#define	TRY_DONE		BIT(4)
#define	RA_H2C		BIT(5)
#define	F_RATE_AP_RPT	BIT(7)

/* -----------------------------------------------------------------------------
 * Define the tracing components
 *
 * -----------------------------------------------------------------------------
 *BB FW Functions*/
#define	PHYDM_FW_COMP_RA			BIT(0)
#define	PHYDM_FW_COMP_MU			BIT(1)
#define	PHYDM_FW_COMP_PATH_DIV		BIT(2)
#define	PHYDM_FW_COMP_PHY_CONFIG	BIT(3)


/*BB Driver Functions*/
#define	ODM_COMP_DIG					BIT(0)
#define	ODM_COMP_RA_MASK				BIT(1)
#define	ODM_COMP_DYNAMIC_TXPWR		BIT(2)
#define	ODM_COMP_FA_CNT				BIT(3)
#define	ODM_COMP_RSSI_MONITOR		BIT(4)
#define	ODM_COMP_SNIFFER				BIT(5)
#define	ODM_COMP_ANT_DIV				BIT(6)
#define	ODM_COMP_DFS					BIT(7)
#define	ODM_COMP_NOISY_DETECT		BIT(8)
#define	ODM_COMP_RATE_ADAPTIVE		BIT(9)
#define	ODM_COMP_PATH_DIV			BIT(10)
#define	ODM_COMP_CCX					BIT(11)

#define	ODM_COMP_DYNAMIC_PRICCA		BIT(12)
/*BIT13 TBD*/
#define	ODM_COMP_MP					BIT(14)
#define	ODM_COMP_CFO_TRACKING		BIT(15)
#define	ODM_COMP_ACS					BIT(16)
#define	PHYDM_COMP_ADAPTIVITY		BIT(17)
#define	PHYDM_COMP_RA_DBG			BIT(18)
#define	PHYDM_COMP_TXBF				BIT(19)
/* MAC Functions */
#define	ODM_COMP_EDCA_TURBO			BIT(20)
#define	ODM_COMP_DYNAMIC_RX_PATH	BIT(21)
#define	ODM_FW_DEBUG_TRACE			BIT(22)
/* RF Functions */
/*BIT23 TBD*/
#define	ODM_COMP_TX_PWR_TRACK		BIT(24)
/*BIT25 TBD*/
#define	ODM_COMP_CALIBRATION			BIT(26)
/* Common Functions */
/*BIT27 TBD*/
#define	ODM_PHY_CONFIG				BIT(28)
#define	ODM_COMP_INIT					BIT(29)
#define	ODM_COMP_COMMON				BIT(30)
#define	ODM_COMP_API					BIT(31)


/*------------------------Export Marco Definition---------------------------*/

#define	config_phydm_read_txagc_check(data)		(data != INVALID_TXAGC_DATA)

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#define	dbg_print				DbgPrint
	#define	dcmd_printf				DCMD_Printf
	#define	dcmd_scanf				DCMD_Scanf
	#define RT_PRINTK				dbg_print
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#define dbg_print	printk
	#define RT_PRINTK(fmt, args...)	dbg_print("%s(): " fmt, __FUNCTION__, ## args);
	#define	RT_DISP(dbgtype, dbgflag, printstr)
#else
	#define dbg_print	panic_printk
	#define RT_PRINTK(fmt, args...)	dbg_print("%s(): " fmt, __FUNCTION__, ## args);
#endif

#ifndef ASSERT
	#define ASSERT(expr)
#endif

#if DBG
#define ODM_RT_TRACE(p_dm_odm, comp, level, fmt)									\
	do {	\
		if (((comp) & p_dm_odm->debug_components) && (level <= p_dm_odm->debug_level || level == ODM_DBG_SERIOUS)) { \
			\
			if (p_dm_odm->support_ic_type == ODM_RTL8188E)							\
				dbg_print("[PhyDM-8188E] ");											\
			else if (p_dm_odm->support_ic_type == ODM_RTL8192E)						\
				dbg_print("[PhyDM-8192E] ");											\
			else if (p_dm_odm->support_ic_type == ODM_RTL8812)							\
				dbg_print("[PhyDM-8812A] ");											\
			else if (p_dm_odm->support_ic_type == ODM_RTL8821)							\
				dbg_print("[PhyDM-8821A] ");											\
			else if (p_dm_odm->support_ic_type == ODM_RTL8814A)							\
				dbg_print("[PhyDM-8814A] ");											\
			else if (p_dm_odm->support_ic_type == ODM_RTL8703B)							\
				dbg_print("[PhyDM-8703B] ");											\
			else if (p_dm_odm->support_ic_type == ODM_RTL8822B)							\
				dbg_print("[PhyDM-8822B] ");											\
			else if (p_dm_odm->support_ic_type == ODM_RTL8188F)							\
				dbg_print("[PhyDM-8188F] ");											\
			RT_PRINTK fmt;															\
		}	\
	} while (0)

#define ODM_RT_TRACE_F(p_dm_odm, comp, level, fmt)									 do {\
		if (((comp) & p_dm_odm->debug_components) && (level <= p_dm_odm->debug_level)) { \
			\
			RT_PRINTK fmt;															\
		}	\
	} while (0)


#define ODM_RT_ASSERT(p_dm_odm, expr, fmt)											 do {\
		if (!(expr)) {																	\
			dbg_print("Assertion failed! %s at ......\n", #expr);								\
			dbg_print("      ......%s,%s, line=%d\n", __FILE__, __FUNCTION__, __LINE__);			\
			RT_PRINTK fmt;															\
			ASSERT(false);															\
		}	\
	} while (0)

#define ODM_dbg_enter() { dbg_print(" == > %s\n", __FUNCTION__); }
#define ODM_dbg_exit() { dbg_print("< == %s\n", __FUNCTION__); }
#define ODM_dbg_trace(str) { dbg_print("%s:%s\n", __FUNCTION__, str); }

#define ODM_PRINT_ADDR(p_dm_odm, comp, level, title_str, ptr)							 do {\
		if (((comp) & p_dm_odm->debug_components) && (level <= p_dm_odm->debug_level)) { \
			\
			int __i;																\
			u8 *__ptr = (u8 *)ptr;											\
			dbg_print("[ODM] ");													\
			dbg_print(title_str);													\
			dbg_print(" ");														\
			for (__i = 0; __i < 6; __i++)												\
				dbg_print("%02X%s", __ptr[__i], (__i == 5) ? "" : "-");						\
			dbg_print("\n");														\
		}	\
	} while (0)

#else
#define ODM_RT_TRACE(p_dm_odm, comp, level, fmt)
#define ODM_RT_TRACE_F(p_dm_odm, comp, level, fmt)
#define ODM_RT_ASSERT(p_dm_odm, expr, fmt)
#define ODM_dbg_enter()
#define ODM_dbg_exit()
#define ODM_dbg_trace(str)
#define ODM_PRINT_ADDR(p_dm_odm, comp, level, title_str, ptr)
#endif


void
phydm_init_debug_setting(struct PHY_DM_STRUCT		*p_dm_odm);

void phydm_basic_dbg_message(void			*p_dm_void);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#define	PHYDM_DBGPRINT		0
#define	PHYDM_SSCANF(x, y, z)	dcmd_scanf(x, y, z)
#define	PHYDM_VAST_INFO_SNPRINTF	PHYDM_SNPRINTF
#if (PHYDM_DBGPRINT == 1)
#define	PHYDM_SNPRINTF(msg)	\
	do {\
		rsprintf msg;\
		dbg_print(output);\
	} while (0)
#else
#define	PHYDM_SNPRINTF(msg)	\
	do {\
		rsprintf msg;\
		dcmd_printf(output);\
	} while (0)
#endif
#else
#if (DM_ODM_SUPPORT_TYPE == ODM_CE) || defined(__OSK__)
	#define	PHYDM_DBGPRINT		0
#else
	#define	PHYDM_DBGPRINT		1
#endif
#define	MAX_ARGC				20
#define	MAX_ARGV				16
#define	DCMD_DECIMAL			"%d"
#define	DCMD_CHAR				"%c"
#define	DCMD_HEX				"%x"

#define	PHYDM_SSCANF(x, y, z)	sscanf(x, y, z)

#define	PHYDM_VAST_INFO_SNPRINTF(msg)\
	do {\
		snprintf msg;\
		dbg_print(output);\
	} while (0)

#if (PHYDM_DBGPRINT == 1)
#define	PHYDM_SNPRINTF(msg)\
	do {\
		snprintf msg;\
		dbg_print(output);\
	} while (0)
#else
#define	PHYDM_SNPRINTF(msg)\
	do {\
		if (out_len > used)\
			used += snprintf msg;\
	} while (0)
#endif
#endif


void phydm_basic_profile(
	void			*p_dm_void,
	u32			*_used,
	char				*output,
	u32			*_out_len
);
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_AP))
s32
phydm_cmd(
	struct PHY_DM_STRUCT	*p_dm_odm,
	char		*input,
	u32	in_len,
	u8	flag,
	char	*output,
	u32	out_len
);
#endif
void
phydm_cmd_parser(
	struct PHY_DM_STRUCT	*p_dm_odm,
	char		input[][16],
	u32	input_num,
	u8	flag,
	char	*output,
	u32	out_len
);

bool
phydm_api_trx_mode(
	struct PHY_DM_STRUCT				*p_dm_odm,
	enum odm_rf_path_e			tx_path,
	enum odm_rf_path_e			rx_path,
	bool					is_tx2_path
);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void phydm_sbd_check(
	struct PHY_DM_STRUCT					*p_dm_odm
);

void phydm_sbd_callback(
	struct timer_list		*p_timer
);

void phydm_sbd_workitem_callback(
	void            *p_context
);
#endif

void
phydm_fw_trace_en_h2c(
	void		*p_dm_void,
	bool		enable,
	u32		fw_debug_component,
	u32		monitor_mode,
	u32		macid
);

void
phydm_fw_trace_handler(
	void	*p_dm_void,
	u8	*cmd_buf,
	u8	cmd_len
);

void
phydm_fw_trace_handler_code(
	void	*p_dm_void,
	u8	*buffer,
	u8	cmd_len
);

void
phydm_fw_trace_handler_8051(
	void	*p_dm_void,
	u8	*cmd_buf,
	u8	cmd_len
);

#endif /* __ODM_DBG_H__ */
