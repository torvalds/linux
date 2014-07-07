/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 * Tmis program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * Tmis program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Tme full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL_DEBUG_H__
#define __RTL_DEBUG_H__

/*--------------------------------------------------------------
			Debug level
--------------------------------------------------------------*/
/*
 *Fatal bug.
 *For example, Tx/Rx/IO locked up,
 *memory access violation,
 *resource allocation failed,
 *unexpected HW behavior, HW BUG
 *and so on.
 */
#define DBG_EMERG			1

/*
 *Abnormal, rare, or unexpeted cases.
 *For example, Packet/IO Ctl canceled,
 *device suprisely unremoved and so on.
 */
#define	DBG_WARNING			2

/*
 *Normal case driver developer should
 *open, we can see link status like
 *assoc/AddBA/DHCP/adapter start and
 *so on basic and useful infromations.
 */
#define DBG_DMESG			3

/*
 *Normal case with useful information
 *about current SW or HW state.
 *For example, Tx/Rx descriptor to fill,
 *Tx/Rx descriptor completed status,
 *SW protocol state change, dynamic
 *mechanism state change and so on.
 */
#define DBG_LOUD			4

/*
 *Normal case with detail execution
 *flow or information.
 */
#define	DBG_TRACE			5

/*--------------------------------------------------------------
		Define the rt_trace components
--------------------------------------------------------------*/
#define COMP_ERR			BIT(0)
#define COMP_FW				BIT(1)
#define COMP_INIT			BIT(2)	/*For init/deinit */
#define COMP_RECV			BIT(3)	/*For Rx. */
#define COMP_SEND			BIT(4)	/*For Tx. */
#define COMP_MLME			BIT(5)	/*For MLME. */
#define COMP_SCAN			BIT(6)	/*For Scan. */
#define COMP_INTR			BIT(7)	/*For interrupt Related. */
#define COMP_LED			BIT(8)	/*For LED. */
#define COMP_SEC			BIT(9)	/*For sec. */
#define COMP_BEACON			BIT(10)	/*For beacon. */
#define COMP_RATE			BIT(11)	/*For rate. */
#define COMP_RXDESC			BIT(12)	/*For rx desc. */
#define COMP_DIG			BIT(13)	/*For DIG */
#define COMP_TXAGC			BIT(14)	/*For Tx power */
#define COMP_HIPWR			BIT(15)	/*For High Power Mechanism */
#define COMP_POWER			BIT(16)	/*For lps/ips/aspm. */
#define COMP_POWER_TRACKING		BIT(17)	/*For TX POWER TRACKING */
#define COMP_BB_POWERSAVING		BIT(18)
#define COMP_SWAS			BIT(19)	/*For SW Antenna Switch */
#define COMP_RF				BIT(20)	/*For RF. */
#define COMP_TURBO			BIT(21)	/*For EDCA TURBO. */
#define COMP_RATR			BIT(22)
#define COMP_CMD			BIT(23)
#define COMP_EFUSE			BIT(24)
#define COMP_QOS			BIT(25)
#define COMP_MAC80211			BIT(26)
#define COMP_REGD			BIT(27)
#define COMP_CHAN			BIT(28)
#define COMP_EASY_CONCURRENT		BIT(29)
#define COMP_BT_COEXIST			BIT(30)
#define COMP_IQK			BIT(31)

/*--------------------------------------------------------------
		Define the rt_print components
--------------------------------------------------------------*/
/* Define EEPROM and EFUSE  check module bit*/
#define EEPROM_W			BIT(0)
#define EFUSE_PG			BIT(1)
#define EFUSE_READ_ALL			BIT(2)

/* Define init check for module bit*/
#define	INIT_EEPROM			BIT(0)
#define	INIT_TxPower			BIT(1)
#define	INIT_IQK			BIT(2)
#define	INIT_RF				BIT(3)

/* Define PHY-BB/RF/MAC check module bit */
#define	PHY_BBR				BIT(0)
#define	PHY_BBW				BIT(1)
#define	PHY_RFR				BIT(2)
#define	PHY_RFW				BIT(3)
#define	PHY_MACR			BIT(4)
#define	PHY_MACW			BIT(5)
#define	PHY_ALLR			BIT(6)
#define	PHY_ALLW			BIT(7)
#define	PHY_TXPWR			BIT(8)
#define	PHY_PWRDIFF			BIT(9)

/* Define Dynamic Mechanism check module bit --> FDM */
#define WA_IOT				BIT(0)
#define DM_PWDB				BIT(1)
#define DM_MONITOR			BIT(2)
#define DM_DIG				BIT(3)
#define DM_EDCA_TURBO			BIT(4)

enum dbgp_flag_e {
	FQOS = 0,
	FTX = 1,
	FRX = 2,
	FSEC = 3,
	FMGNT = 4,
	FMLME = 5,
	FRESOURCE = 6,
	FBEACON = 7,
	FISR = 8,
	FPHY = 9,
	FMP = 10,
	FEEPROM = 11,
	FPWR = 12,
	FDM = 13,
	FDBGCtrl = 14,
	FC2H = 15,
	FBT = 16,
	FINIT = 17,
	FIOCTL = 18,
	DBGP_TYPE_MAX
};

#define RT_ASSERT(_exp , fmt)				\
	do { \
		if (!(_exp))	{			\
			pr_debug("%s:%s(): ", KBUILD_MODNAME, \
			__func__);	\
			pr_cont fmt;			\
		} \
	} while (0)

#define RT_TRACE(comp, level, fmt)\
	do { \
		if (unlikely(((comp) & rtlpriv->dbg.global_debugcomponents) && \
			((level) <= rtlpriv->dbg.global_debuglevel))) {\
			pr_debug("%s-%d:%s():<%lx> ", \
			KBUILD_MODNAME, \
			rtlpriv->rtlhal.interfaceindex, __func__, \
			in_interrupt());	\
			pr_cont fmt;			\
		} \
	} while (0)

#define RTPRINT(rtlpriv, dbgtype, dbgflag, fmt, ...)			\
do {									\
	if (unlikely(rtlpriv->dbg.dbgp_type[dbgtype] & dbgflag)) {	\
		pr_debug(KBUILD_MODNAME ": " fmt,		\
		       ##__VA_ARGS__);					\
	}								\
} while (0)

#define RT_PRINT_DATA(rtlpriv, _comp, _level, _titlestring, _hexdata, \
		_hexdatalen) \
	do {\
		if (unlikely(((_comp) & rtlpriv->dbg.global_debugcomponents) &&\
			(_level <= rtlpriv->dbg.global_debuglevel)))	{ \
			int __i;					\
			u8 *ptr = (u8 *)_hexdata;			\
			pr_debug("%s: ", KBUILD_MODNAME);	\
			pr_cont("In process \"%s\" (pid %i):", \
					current->comm,	\
					current->pid); \
			pr_cont(_titlestring);		\
			for (__i = 0; __i < (int)_hexdatalen; __i++) {	\
				pr_cont("%02X%s", ptr[__i], (((__i + 1) % 4) \
							== 0) ? "  " : " ");\
				if (((__i + 1) % 16) == 0)	\
					pr_cont("\n");	\
			}				\
			pr_cont("\n");			\
		} \
	} while (0)

void rtl92e_dbgp_flag_init(struct ieee80211_hw *hw);
void rtl_proc_add_one(struct ieee80211_hw *hw);
void rtl_proc_remove_one(struct ieee80211_hw *hw);
void rtl_proc_add_topdir(void);
void rtl_proc_remove_topdir(void);
#endif
