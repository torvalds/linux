/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __RTL871X_DEBUG_H__
#define __RTL871X_DEBUG_H__

#include "osdep_service.h"
#include "drv_types.h"

#define _drv_emerg_			1
#define _drv_alert_			2
#define _drv_crit_			3
#define _drv_err_			4
#define	_drv_warning_			5
#define _drv_notice_			6
#define _drv_info_			7
#define _drv_dump_			8
#define	_drv_debug_			9

#define _module_rtl871x_xmit_c_		BIT(0)
#define _module_xmit_osdep_c_		BIT(1)
#define _module_rtl871x_recv_c_		BIT(2)
#define _module_recv_osdep_c_		BIT(3)
#define _module_rtl871x_mlme_c_		BIT(4)
#define	_module_mlme_osdep_c_		BIT(5)
#define _module_rtl871x_sta_mgt_c_	BIT(6)
#define _module_rtl871x_cmd_c_		BIT(7)
#define	_module_cmd_osdep_c_		BIT(8)
#define _module_rtl871x_io_c_		BIT(9)
#define	_module_io_osdep_c_		BIT(10)
#define _module_os_intfs_c_		BIT(11)
#define _module_rtl871x_security_c_	BIT(12)
#define _module_rtl871x_eeprom_c_	BIT(13)
#define _module_hal_init_c_		BIT(14)
#define _module_hci_hal_init_c_		BIT(15)
#define _module_rtl871x_ioctl_c_	BIT(16)
#define _module_rtl871x_ioctl_set_c_	BIT(17)
#define _module_rtl871x_pwrctrl_c_	BIT(19)
#define _module_hci_intfs_c_		BIT(20)
#define _module_hci_ops_c_		BIT(21)
#define _module_osdep_service_c_	BIT(22)
#define _module_rtl871x_mp_ioctl_c_	BIT(23)
#define _module_hci_ops_os_c_		BIT(24)
#define _module_rtl871x_ioctl_os_c	BIT(25)
#define _module_rtl8712_cmd_c_		BIT(26)
#define _module_rtl871x_mp_c_		BIT(27)
#define _module_rtl8712_xmit_c_		BIT(28)
#define _module_rtl8712_efuse_c_	BIT(29)
#define _module_rtl8712_recv_c_		BIT(30)
#define _module_rtl8712_led_c_		BIT(31)

#undef _MODULE_DEFINE_

#if defined _RTL871X_XMIT_C_
	#define _MODULE_DEFINE_	_module_rtl871x_xmit_c_
#elif defined _XMIT_OSDEP_C_
	#define _MODULE_DEFINE_	_module_xmit_osdep_c_
#elif defined _RTL871X_RECV_C_
	#define _MODULE_DEFINE_	_module_rtl871x_recv_c_
#elif defined _RECV_OSDEP_C_
	#define _MODULE_DEFINE_	_module_recv_osdep_c_
#elif defined _RTL871X_MLME_C_
	#define _MODULE_DEFINE_	_module_rtl871x_mlme_c_
#elif defined _MLME_OSDEP_C_
	#define _MODULE_DEFINE_	_module_mlme_osdep_c_
#elif defined _RTL871X_STA_MGT_C_
	#define _MODULE_DEFINE_	_module_rtl871x_sta_mgt_c_
#elif defined _RTL871X_CMD_C_
	#define _MODULE_DEFINE_	_module_rtl871x_cmd_c_
#elif defined _CMD_OSDEP_C_
	#define _MODULE_DEFINE_	_module_cmd_osdep_c_
#elif defined _RTL871X_IO_C_
	#define _MODULE_DEFINE_	_module_rtl871x_io_c_
#elif defined _IO_OSDEP_C_
	#define _MODULE_DEFINE_	_module_io_osdep_c_
#elif defined _OS_INTFS_C_
	#define	_MODULE_DEFINE_	_module_os_intfs_c_
#elif defined _RTL871X_SECURITY_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_security_c_
#elif defined _RTL871X_EEPROM_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_eeprom_c_
#elif defined _HAL_INIT_C_
	#define	_MODULE_DEFINE_	_module_hal_init_c_
#elif defined _HCI_HAL_INIT_C_
	#define	_MODULE_DEFINE_	_module_hci_hal_init_c_
#elif defined _RTL871X_IOCTL_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_ioctl_c_
#elif defined _RTL871X_IOCTL_SET_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_ioctl_set_c_
#elif defined _RTL871X_PWRCTRL_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_pwrctrl_c_
#elif defined _HCI_INTF_C_
	#define	_MODULE_DEFINE_	_module_hci_intfs_c_
#elif defined _HCI_OPS_C_
	#define	_MODULE_DEFINE_	_module_hci_ops_c_
#elif defined _OSDEP_HCI_INTF_C_
	#define	_MODULE_DEFINE_	_module_hci_intfs_c_
#elif defined _OSDEP_SERVICE_C_
	#define	_MODULE_DEFINE_	_module_osdep_service_c_
#elif defined _RTL871X_MP_IOCTL_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_mp_ioctl_c_
#elif defined _HCI_OPS_OS_C_
	#define	_MODULE_DEFINE_	_module_hci_ops_os_c_
#elif defined _RTL871X_IOCTL_LINUX_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_ioctl_os_c
#elif defined _RTL871X_MP_C_
	#define	_MODULE_DEFINE_	_module_rtl871x_mp_c_
#elif defined _RTL8712_CMD_C_
	#define	_MODULE_DEFINE_	_module_rtl8712_cmd_c_
#elif defined _RTL8712_XMIT_C_
	#define	_MODULE_DEFINE_	_module_rtl8712_xmit_c_
#elif defined _RTL8712_EFUSE_C_
	#define	_MODULE_DEFINE_	_module_rtl8712_efuse_c_
#elif defined _RTL8712_RECV_C_
	#define	_MODULE_DEFINE_	_module_rtl8712_recv_c_
#else
	#undef	_MODULE_DEFINE_
#endif

#define _dbgdump	printk

#define MSG_8712(x, ...) {}

#define DBG_8712(x, ...)  {}

#define WRN_8712(x, ...)  {}

#define ERR_8712(x, ...)  {}

#undef MSG_8712
#define MSG_8712 _dbgdump

#undef DBG_8712
#define DBG_8712 _dbgdump

#undef WRN_8712
#define WRN_8712 _dbgdump

#undef ERR_8712
#define ERR_8712 _dbgdump

#endif	/*__RTL871X_DEBUG_H__*/

