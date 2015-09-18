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
 *
 ******************************************************************************/
#ifndef __RTW_DEBUG_H__
#define __RTW_DEBUG_H__

#include <osdep_service.h>
#include <drv_types.h>

#define _drv_always_			1
#define _drv_emerg_			2
#define _drv_alert_			3
#define _drv_err_			4
#define	_drv_warning_			5
#define _drv_notice_			6
#define _drv_info_			7
#define	_drv_debug_			8

#define _module_rtl871x_xmit_c_		BIT(0)
#define _module_xmit_osdep_c_		BIT(1)
#define _module_rtl871x_recv_c_		BIT(2)
#define _module_recv_osdep_c_		BIT(3)
#define _module_rtl871x_mlme_c_		BIT(4)
#define _module_mlme_osdep_c_		BIT(5)
#define _module_rtl871x_sta_mgt_c_	BIT(6)
#define _module_rtl871x_cmd_c_		BIT(7)
#define _module_cmd_osdep_c_		BIT(8)
#define _module_rtl871x_io_c_		BIT(9)
#define _module_io_osdep_c_		BIT(10)
#define _module_os_intfs_c_		BIT(11)
#define _module_rtl871x_security_c_	BIT(12)
#define _module_rtl871x_eeprom_c_	BIT(13)
#define _module_hal_init_c_		BIT(14)
#define _module_hci_hal_init_c_		BIT(15)
#define _module_rtl871x_ioctl_c_	BIT(16)
#define _module_rtl871x_ioctl_set_c_	BIT(17)
#define _module_rtl871x_ioctl_query_c_	BIT(18)
#define _module_rtl871x_pwrctrl_c_	BIT(19)
#define _module_hci_intfs_c_		BIT(20)
#define _module_hci_ops_c_		BIT(21)
#define _module_osdep_service_c_	BIT(22)
#define _module_mp_			BIT(23)
#define _module_hci_ops_os_c_		BIT(24)
#define _module_rtl871x_ioctl_os_c	BIT(25)
#define _module_rtl8712_cmd_c_		BIT(26)
#define	_module_rtl8192c_xmit_c_	BIT(28)
#define _module_hal_xmit_c_		BIT(28) /* duplication intentional */
#define _module_efuse_			BIT(29)
#define _module_rtl8712_recv_c_		BIT(30)
#define _module_rtl8712_led_c_		BIT(31)

#undef _MODULE_DEFINE_

#if defined _RTW_XMIT_C_
	#define _MODULE_DEFINE_	_module_rtl871x_xmit_c_
#elif defined _XMIT_OSDEP_C_
	#define _MODULE_DEFINE_	_module_xmit_osdep_c_
#elif defined _RTW_RECV_C_
	#define _MODULE_DEFINE_	_module_rtl871x_recv_c_
#elif defined _RECV_OSDEP_C_
	#define _MODULE_DEFINE_	_module_recv_osdep_c_
#elif defined _RTW_MLME_C_
	#define _MODULE_DEFINE_	_module_rtl871x_mlme_c_
#elif defined _MLME_OSDEP_C_
	#define _MODULE_DEFINE_	_module_mlme_osdep_c_
#elif defined _RTW_MLME_EXT_C_
	#define _MODULE_DEFINE_ 1
#elif defined _RTW_STA_MGT_C_
	#define _MODULE_DEFINE_	_module_rtl871x_sta_mgt_c_
#elif defined _RTW_CMD_C_
	#define _MODULE_DEFINE_	_module_rtl871x_cmd_c_
#elif defined _CMD_OSDEP_C_
	#define _MODULE_DEFINE_	_module_cmd_osdep_c_
#elif defined _RTW_IO_C_
	#define _MODULE_DEFINE_	_module_rtl871x_io_c_
#elif defined _IO_OSDEP_C_
	#define _MODULE_DEFINE_	_module_io_osdep_c_
#elif defined _OS_INTFS_C_
	#define	_MODULE_DEFINE_	_module_os_intfs_c_
#elif defined _RTW_SECURITY_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_security_c_
#elif defined _RTW_EEPROM_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_eeprom_c_
#elif defined _HAL_INTF_C_
	#define	_MODULE_DEFINE_	_module_hal_init_c_
#elif (defined _HCI_HAL_INIT_C_) || (defined _SDIO_HALINIT_C_)
	#define	_MODULE_DEFINE_	_module_hci_hal_init_c_
#elif defined _RTL871X_IOCTL_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_ioctl_c_
#elif defined _RTL871X_IOCTL_SET_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_ioctl_set_c_
#elif defined _RTL871X_IOCTL_QUERY_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_ioctl_query_c_
#elif defined _RTL871X_PWRCTRL_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_pwrctrl_c_
#elif defined _RTW_PWRCTRL_C_
	#define	_MODULE_DEFINE_	1
#elif defined _HCI_INTF_C_
	#define	_MODULE_DEFINE_	_module_hci_intfs_c_
#elif defined _HCI_OPS_C_
	#define	_MODULE_DEFINE_	_module_hci_ops_c_
#elif defined _SDIO_OPS_C_
	#define	_MODULE_DEFINE_ 1
#elif defined _OSDEP_HCI_INTF_C_
	#define	_MODULE_DEFINE_	_module_hci_intfs_c_
#elif defined _OSDEP_SERVICE_C_
	#define	_MODULE_DEFINE_	_module_osdep_service_c_
#elif defined _HCI_OPS_OS_C_
	#define	_MODULE_DEFINE_	_module_hci_ops_os_c_
#elif defined _RTL871X_IOCTL_LINUX_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_ioctl_os_c
#elif defined _RTL8712_CMD_C_
	#define	_MODULE_DEFINE_	_module_rtl8712_cmd_c_
#elif defined _RTL8192C_XMIT_C_
	#define	_MODULE_DEFINE_	1
#elif defined _RTL8723AS_XMIT_C_
	#define	_MODULE_DEFINE_	1
#elif defined _RTL8712_RECV_C_
	#define	_MODULE_DEFINE_	_module_rtl8712_recv_c_
#elif defined _RTL8192CU_RECV_C_
	#define	_MODULE_DEFINE_	_module_rtl8712_recv_c_
#elif defined _RTL871X_MLME_EXT_C_
	#define _MODULE_DEFINE_	_module_mlme_osdep_c_
#elif defined _RTW_MP_C_
	#define	_MODULE_DEFINE_	_module_mp_
#elif defined _RTW_MP_IOCTL_C_
	#define	_MODULE_DEFINE_	_module_mp_
#elif defined _RTW_EFUSE_C_
	#define	_MODULE_DEFINE_	_module_efuse_
#endif

#define DRIVER_PREFIX	"RTL8723AU: "
#define DEBUG_LEVEL	(_drv_err_)
#define DBG_8723A_LEVEL(_level, fmt, arg...)				\
	do {								\
		if (_level <= GlobalDebugLevel23A)				\
			pr_info(DRIVER_PREFIX fmt, ##arg);\
	} while (0)

#define DBG_8723A(...)							\
	do {								\
		if (_drv_err_ <= GlobalDebugLevel23A)			\
			pr_info(DRIVER_PREFIX __VA_ARGS__);		\
	} while (0)

#define MSG_8723A(...)							\
	do {								\
		if (_drv_err_ <= GlobalDebugLevel23A)			\
			pr_info(DRIVER_PREFIX __VA_ARGS__);		\
	} while (0)

extern u32 GlobalDebugLevel23A;

__printf(3, 4)
void rt_trace(int comp, int level, const char *fmt, ...);

#define RT_TRACE(_Comp, _Level, Fmt, ...)				\
do {									\
	if (_Level <= GlobalDebugLevel23A)				\
		rt_trace(_Comp, _Level, Fmt, ##__VA_ARGS__);		\
} while (0)

#define RT_PRINT_DATA(_Comp, _Level, _TitleString, _HexData,		\
		      _HexDataLen)					\
	if (_Level <= GlobalDebugLevel23A) {				\
		int __i;						\
		u8	*ptr = (u8 *)_HexData;				\
		pr_info("%s", DRIVER_PREFIX);				\
		pr_info(_TitleString);					\
		for (__i = 0; __i < (int)_HexDataLen; __i++) {		\
			printk("%02X%s", ptr[__i],			\
			       (((__i + 1) % 4) == 0) ? "  " : " ");	\
			if (((__i + 1) % 16) == 0)			\
				printk("\n");				\
		}							\
		printk("\n");						\
	}

#endif	/* __RTW_DEBUG_H__ */
