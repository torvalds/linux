/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andreamrl@tiscali.it>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#ifndef _RTL_DEBUG_H
#define _RTL_DEBUG_H
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/debugfs.h>

struct r8192_priv;
struct _tx_desc_8192se;
struct _TX_DESC_8192CE;
struct net_device;

#define	DBG_LOUD	4

#define RT_ASSERT(_Exp, Fmt)				\
		if (!(_Exp)) {				\
			printk("Rtl819x: ");		\
			printk Fmt;			\
		}

enum dbgp_flag {
	FQoS				= 0,
	FTX				= 1,
	FRX				= 2,
	FSEC				= 3,
	FMGNT				= 4,
	FMLME				= 5,
	FRESOURCE			= 6,
	FBEACON				= 7,
	FISR				= 8,
	FPHY				= 9,
	FMP				= 10,
	FEEPROM				= 11,
	FPWR				= 12,
	FDM				= 13,
	FDBGCtrl			= 14,
	FC2H				= 15,
	FBT				= 16,
	FINIT				= 17,
	FIOCTL				= 18,
	DBGP_TYPE_MAX
};

#define		QoS_INIT				BIT0
#define		QoS_VISTA				BIT1

#define		TX_DESC					BIT0
#define		TX_DESC_TID				BIT1

#define		RX_DATA					BIT0
#define		RX_PHY_STS				BIT1
#define		RX_PHY_SS				BIT2
#define		RX_PHY_SQ				BIT3
#define		RX_PHY_ASTS				BIT4
#define		RX_ERR_LEN				BIT5
#define		RX_DEFRAG				BIT6
#define		RX_ERR_RATE				BIT7



#define		MEDIA_STS				BIT0
#define		LINK_STS				BIT1

#define		OS_CHK					BIT0

#define		BCN_SHOW				BIT0
#define		BCN_PEER				BIT1

#define		ISR_CHK					BIT0

#define		PHY_BBR					BIT0
#define		PHY_BBW					BIT1
#define		PHY_RFR					BIT2
#define		PHY_RFW					BIT3
#define		PHY_MACR				BIT4
#define		PHY_MACW				BIT5
#define		PHY_ALLR				BIT6
#define		PHY_ALLW				BIT7
#define		PHY_TXPWR				BIT8
#define		PHY_PWRDIFF				BIT9

#define		MP_RX					BIT0
#define		MP_SWICH_CH				BIT1

#define		EEPROM_W				BIT0
#define		EFUSE_PG				BIT1
#define		EFUSE_READ_ALL				BIT2

#define		LPS					BIT0
#define		IPS					BIT1
#define		PWRSW					BIT2
#define		PWRHW					BIT3
#define		PWRHAL					BIT4

#define		WA_IOT					BIT0
#define		DM_PWDB					BIT1
#define		DM_Monitor				BIT2
#define		DM_DIG					BIT3
#define		DM_EDCA_Turbo				BIT4

#define		DbgCtrl_Trace				BIT0
#define		DbgCtrl_InbandNoise			BIT1

#define		BT_TRACE				BIT0
#define		BT_RFPoll				BIT1

#define		C2H_Summary				BIT0
#define		C2H_PacketData				BIT1
#define		C2H_ContentData				BIT2
#define		BT_TRACE				BIT0
#define		BT_RFPoll				BIT1

#define		INIT_EEPROM				BIT0
#define		INIT_TxPower				BIT1
#define		INIT_IQK				BIT2
#define		INIT_RF					BIT3

#define		IOCTL_TRACE				BIT0
#define		IOCTL_BT_EVENT				BIT1
#define		IOCTL_BT_EVENT_DETAIL			BIT2
#define		IOCTL_BT_TX_ACLDATA			BIT3
#define		IOCTL_BT_TX_ACLDATA_DETAIL		BIT4
#define		IOCTL_BT_RX_ACLDATA			BIT5
#define		IOCTL_BT_RX_ACLDATA_DETAIL		BIT6
#define		IOCTL_BT_HCICMD				BIT7
#define		IOCTL_BT_HCICMD_DETAIL			BIT8
#define		IOCTL_IRP				BIT9
#define		IOCTL_IRP_DETAIL			BIT10
#define		IOCTL_CALLBACK_FUN			BIT11
#define		IOCTL_STATE				BIT12
#define		IOCTL_BT_TP				BIT13
#define		IOCTL_BT_LOGO				BIT14

/* 2007/07/13 MH  ------For DeBuG Print modeue------*/
/*------------------------------Define structure----------------------------*/


/*------------------------Export Marco Definition---------------------------*/
#define		DEBUG_PRINT		1

#if (DEBUG_PRINT == 1)
#define RTPRINT(dbgtype, dbgflag, printstr)			\
{								\
	if (DBGP_Type[dbgtype] & dbgflag) {			\
		printk printstr;				\
	}							\
}

#define	RTPRINT_ADDR(dbgtype, dbgflag, printstr, _Ptr)		\
{								\
	if (DBGP_Type[dbgtype] & dbgflag) {			\
		int __i;					\
		u8 *ptr = (u8 *)_Ptr;				\
		printk printstr;				\
		printk(" ");					\
		for (__i = 0; __i < 6; __i++)			\
			printk("%02X%s", ptr[__i],		\
			       (__i == 5) ? "" : "-");		\
			printk("\n");				\
	}							\
}

#define RTPRINT_DATA(dbgtype, dbgflag, _TitleString, _HexData, _HexDataLen)\
{								\
	if (DBGP_Type[dbgtype] & dbgflag) {			\
		int __i;					\
		u8 *ptr = (u8 *)_HexData;			\
		printk(_TitleString);				\
		for (__i = 0; __i < (int)_HexDataLen; __i++) {	\
			printk("%02X%s", ptr[__i], (((__i + 1)	\
			       % 4) == 0) ? "  " : " ");	\
			if (((__i + 1) % 16) == 0)		\
				printk("\n");			\
		}						\
		printk("\n");					\
	}							\
}
#else
#define	RTPRINT(dbgtype, dbgflag, printstr)
#define	RTPRINT_ADDR(dbgtype, dbgflag, printstr, _Ptr)
#define RTPRINT_DATA(dbgtype, dbgflag, _TitleString, _HexData, _HexDataLen)
#endif

extern u32	DBGP_Type[DBGP_TYPE_MAX];

#define RT_PRINT_DATA(_Comp, _Level, _TitleString, _HexData, _HexDataLen) \
do {\
	if (((_Comp) & rt_global_debug_component) &&			\
	     (_Level <= rt_global_debug_component)) {			\
		int __i;						\
		u8*	ptr = (u8 *)_HexData;				\
		printk(KERN_INFO "Rtl819x: ");				\
		printk(_TitleString);					\
		for (__i = 0; __i < (int)_HexDataLen; __i++) {		\
			printk("%02X%s", ptr[__i], (((__i + 1) %	\
			       4) == 0) ? "  " : " ");			\
			if (((__i + 1) % 16) == 0)			\
				printk("\n");				\
		}							\
		printk("\n");						\
	} \
} while (0);

#define DMESG(x, a...)
#define DMESGW(x, a...)
#define DMESGE(x, a...)
extern u32 rt_global_debug_component;
#define RT_TRACE(component, x, args...) \
do {			\
	if (rt_global_debug_component & component) \
		printk(KERN_DEBUG DRV_NAME ":" x "\n" , \
		       ##args);\
} while (0);

#define assert(expr) \
	if (!(expr)) {				  \
		printk(KERN_INFO "Assertion failed! %s,%s,%s,line=%d\n", \
		#expr, __FILE__, __func__, __LINE__);	  \
	}
#define RT_DEBUG_DATA(level, data, datalen)      \
	do {								\
		if ((rt_global_debug_component & (level)) == (level)) {\
			int _i;				  \
			u8 *_pdata = (u8 *)data;		 \
			printk(KERN_DEBUG DRV_NAME ": %s()\n", __func__);   \
			for (_i = 0; _i < (int)(datalen); _i++) {	\
				printk(KERN_INFO "%2x ", _pdata[_i]);	\
				if ((_i+1) % 16 == 0)			\
					printk("\n");			\
			}			       \
			printk(KERN_INFO "\n");	  \
		}				       \
	} while (0)

struct rtl_fs_debug {
	const char *name;
	struct dentry *dir_drv;
	struct dentry *debug_register;
	u32 hw_type;
	u32 hw_offset;
	bool hw_holding;
};

void print_buffer(u32 *buffer, int len);
void dump_eprom(struct net_device *dev);
void rtl8192_dump_reg(struct net_device *dev);

/* debugfs stuff */
static inline int rtl_debug_module_init(struct r8192_priv *priv,
					const char *name)
{
	return 0;
}

static inline void rtl_debug_module_remove(struct r8192_priv *priv)
{
}

static inline int rtl_create_debugfs_root(void)
{
	return 0;
}

static inline void rtl_remove_debugfs_root(void)
{
}

/* proc stuff */
void rtl8192_proc_init_one(struct net_device *dev);
void rtl8192_proc_remove_one(struct net_device *dev);
void rtl8192_proc_module_init(void);
void rtl8192_proc_module_remove(void);
void rtl8192_dbgp_flag_init(struct net_device *dev);

#endif
