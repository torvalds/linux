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
#ifndef __RTW_DEBUG_H__
#define __RTW_DEBUG_H__


#define _drv_always_		1
#define _drv_emerg_			2
#define _drv_alert_			3
#define _drv_crit_			4
#define _drv_err_			5
#define	_drv_warning_		6
#define _drv_notice_		7
#define _drv_info_			8
#define _drv_dump_			9
#define	_drv_debug_			10


#define _module_rtl871x_xmit_c_		BIT(0)
#define _module_xmit_osdep_c_		BIT(1)
#define _module_rtl871x_recv_c_		BIT(2)
#define _module_recv_osdep_c_		BIT(3)
#define _module_rtl871x_mlme_c_		BIT(4)
#define _module_mlme_osdep_c_		BIT(5)
#define _module_rtl871x_sta_mgt_c_		BIT(6)
#define _module_rtl871x_cmd_c_			BIT(7)
#define _module_cmd_osdep_c_		BIT(8)
#define _module_rtl871x_io_c_				BIT(9)
#define _module_io_osdep_c_		BIT(10)
#define _module_os_intfs_c_			BIT(11)
#define _module_rtl871x_security_c_		BIT(12)
#define _module_rtl871x_eeprom_c_			BIT(13)
#define _module_hal_init_c_		BIT(14)
#define _module_hci_hal_init_c_		BIT(15)
#define _module_rtl871x_ioctl_c_		BIT(16)
#define _module_rtl871x_ioctl_set_c_		BIT(17)
#define _module_rtl871x_ioctl_query_c_	BIT(18)
#define _module_rtl871x_pwrctrl_c_			BIT(19)
#define _module_hci_intfs_c_			BIT(20)
#define _module_hci_ops_c_			BIT(21)
#define _module_osdep_service_c_			BIT(22)
#define _module_mp_			BIT(23)
#define _module_hci_ops_os_c_			BIT(24)
#define _module_rtl871x_ioctl_os_c		BIT(25)
#define _module_rtl8712_cmd_c_		BIT(26)
//#define _module_efuse_			BIT(27)
#define	_module_rtl8192c_xmit_c_ BIT(28)
#define _module_hal_xmit_c_	BIT(28)
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

#ifdef PLATFORM_OS_CE
extern void rtl871x_cedbg(const char *fmt, ...);
#endif

#define RT_TRACE(_Comp, _Level, Fmt) do{}while(0)
#define _func_enter_ do{}while(0)
#define _func_exit_ do{}while(0)
#define RT_PRINT_DATA(_Comp, _Level, _TitleString, _HexData, _HexDataLen) do{}while(0)

#ifdef PLATFORM_WINDOWS
	#define DBG_871X do {} while(0)
	#define MSG_8192C do {} while(0)
	#define DBG_8192C do {} while(0)
	#define DBG_871X_LEVEL do {} while(0)
#else
	#define DBG_871X(x, ...) do {} while(0)
	#define MSG_8192C(x, ...) do {} while(0)
	#define DBG_8192C(x,...) do {} while(0)
	#define DBG_871X_LEVEL(x,...) do {} while(0)
#endif

#undef _dbgdump
#undef _seqdump

#ifndef _RTL871X_DEBUG_C_
	extern u32 GlobalDebugLevel;
	extern u64 GlobalDebugComponents;
#endif

#if defined(PLATFORM_WINDOWS) && defined(PLATFORM_OS_XP)
	#define _dbgdump DbgPrint
	#define _seqdump(sel, fmt, arg...) _dbgdump(fmt, ##arg)
#elif defined(PLATFORM_WINDOWS) && defined(PLATFORM_OS_CE)
	#define _dbgdump rtl871x_cedbg
	#define _seqdump(sel, fmt, arg...) _dbgdump(fmt, ##arg)
#elif defined PLATFORM_LINUX
	#define _dbgdump printk
	#define _seqdump seq_printf
#elif defined PLATFORM_FREEBSD
	#define _dbgdump printf
	#define _seqdump(sel, fmt, arg...) _dbgdump(fmt, ##arg)
#endif

#define DRIVER_PREFIX "RTL871X: "

#if defined(_dbgdump)

#define DBG_871X_EXP(level, EXP) do { if (level <= GlobalDebugLevel) EXP; } while (0)

/* with driver-defined prefix */
#undef DBG_871X_LEVEL
#define DBG_871X_LEVEL(level, fmt, arg...)     \
	do {\
		if (level <= GlobalDebugLevel) {\
			if (level <= _drv_err_ && level > _drv_always_) \
				_dbgdump(DRIVER_PREFIX"ERROR " fmt, ##arg);\
			else \
				_dbgdump(DRIVER_PREFIX fmt, ##arg);\
		}\
	}while(0)

/* without driver-defined prefix */
#undef _DBG_871X_LEVEL
#define _DBG_871X_LEVEL(level, fmt, arg...)	   \
	do {\
		if (level <= GlobalDebugLevel) {\
			if (level <= _drv_err_ && level > _drv_always_) \
				_dbgdump("ERROR " fmt, ##arg);\
			else \
				_dbgdump(fmt, ##arg);\
		}\
	}while(0)

#if defined(_seqdump)
#define RTW_DBGDUMP 0 /* 'stream' for _dbgdump */

/* dump message to selected 'stream' */
#define DBG_871X_SEL(sel, fmt, arg...) \
	do {\
		if (sel == RTW_DBGDUMP)\
			_DBG_871X_LEVEL(_drv_always_, fmt, ##arg); \
		else {\
			_seqdump(sel, fmt, ##arg); \
		} \
	}while(0)

/* dump message to selected 'stream' with driver-defined prefix */
#define DBG_871X_SEL_NL(sel, fmt, arg...) \
	do {\
		if (sel == RTW_DBGDUMP)\
			DBG_871X_LEVEL(_drv_always_, fmt, ##arg); \
		else {\
			_seqdump(sel, fmt, ##arg) /*rtw_warn_on(1)*/; \
		} \
	}while(0)

#endif /* defined(_seqdump) */

#endif /* defined(_dbgdump) */

#ifdef CONFIG_DEBUG
#if	defined(_dbgdump)
	#undef DBG_871X
	#define DBG_871X(...)     do {\
		_dbgdump(DRIVER_PREFIX __VA_ARGS__);\
	}while(0)

	#undef MSG_8192C
	#define MSG_8192C(...)     do {\
		_dbgdump(DRIVER_PREFIX __VA_ARGS__);\
	}while(0)

	#undef DBG_8192C
	#define DBG_8192C(...)     do {\
		_dbgdump(DRIVER_PREFIX __VA_ARGS__);\
	}while(0)
#endif /* defined(_dbgdump) */
#endif /* CONFIG_DEBUG */

#ifdef CONFIG_DEBUG_RTL871X

#if	defined(_dbgdump) && defined(_MODULE_DEFINE_)

	#undef RT_TRACE
	#define RT_TRACE(_Comp, _Level, Fmt)\
	do {\
		if((_Comp & GlobalDebugComponents) && (_Level <= GlobalDebugLevel)) {\
			_dbgdump("%s [0x%08x,%d]", DRIVER_PREFIX, (unsigned int)_Comp, _Level);\
			_dbgdump Fmt;\
		}\
	}while(0)

#endif /* defined(_dbgdump) && defined(_MODULE_DEFINE_) */


#if	defined(_dbgdump)
	#undef  _func_enter_
	#define _func_enter_ \
	do {	\
		if (GlobalDebugLevel >= _drv_debug_) \
		{																	\
			_dbgdump("\n %s : %s enters at %d\n", DRIVER_PREFIX, __FUNCTION__, __LINE__);\
		}		\
	} while(0)

	#undef  _func_exit_
	#define _func_exit_ \
	do {	\
		if (GlobalDebugLevel >= _drv_debug_) \
		{																	\
			_dbgdump("\n %s : %s exits at %d\n", DRIVER_PREFIX, __FUNCTION__, __LINE__); \
		}	\
	} while(0)

	#undef RT_PRINT_DATA
	#define RT_PRINT_DATA(_Comp, _Level, _TitleString, _HexData, _HexDataLen)			\
		if(((_Comp) & GlobalDebugComponents) && (_Level <= GlobalDebugLevel))	\
		{									\
			int __i;								\
			u8	*ptr = (u8 *)_HexData;				\
			_dbgdump("%s", DRIVER_PREFIX);						\
			_dbgdump(_TitleString);						\
			for( __i=0; __i<(int)_HexDataLen; __i++ )				\
			{								\
				_dbgdump("%02X%s", ptr[__i], (((__i + 1) % 4) == 0)?"  ":" ");	\
				if (((__i + 1) % 16) == 0)	_dbgdump("\n");			\
			}								\
			_dbgdump("\n");							\
		}
#endif /* defined(_dbgdump) */
#endif /* CONFIG_DEBUG_RTL871X */

#ifdef CONFIG_DBG_COUNTER
#define DBG_COUNTER(counter) counter++
#else
#define DBG_COUNTER(counter) 
#endif

void dump_drv_version(void *sel);
void dump_log_level(void *sel);
void dump_drv_cfg(void *sel);

#ifdef CONFIG_SDIO_HCI
void sd_f0_reg_dump(void *sel, _adapter *adapter);
void sdio_local_reg_dump(void *sel, _adapter *adapter);
#endif /* CONFIG_SDIO_HCI */

void mac_reg_dump(void *sel, _adapter *adapter);
void bb_reg_dump(void *sel, _adapter *adapter);
void rf_reg_dump(void *sel, _adapter *adapter);

bool rtw_fwdl_test_trigger_chksum_fail(void);
bool rtw_fwdl_test_trigger_wintint_rdy_fail(void);
bool rtw_del_rx_ampdu_test_trigger_no_tx_fail(void);

u32 rtw_get_wait_hiq_empty_ms(void);
void rtw_sink_rtp_seq_dbg( _adapter *adapter,_pkt *pkt);

struct sta_info;
void sta_rx_reorder_ctl_dump(void *sel, struct sta_info *sta);

struct dvobj_priv;
void dump_adapters_status(void *sel, struct dvobj_priv *dvobj);

struct sec_cam_ent;
void dump_sec_cam_ent(void *sel, struct sec_cam_ent *ent, int id);
void dump_sec_cam_ent_title(void *sel, u8 has_id);
void dump_sec_cam(void *sel, _adapter *adapter);

#ifdef CONFIG_PROC_DEBUG
ssize_t proc_set_write_reg(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
int proc_get_read_reg(struct seq_file *m, void *v);
ssize_t proc_set_read_reg(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

int proc_get_fwstate(struct seq_file *m, void *v);
int proc_get_sec_info(struct seq_file *m, void *v);
int proc_get_mlmext_state(struct seq_file *m, void *v);
#ifdef CONFIG_LAYER2_ROAMING
int proc_get_roam_flags(struct seq_file *m, void *v);
ssize_t proc_set_roam_flags(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
int proc_get_roam_param(struct seq_file *m, void *v);
ssize_t proc_set_roam_param(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
ssize_t proc_set_roam_tgt_addr(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
#endif /* CONFIG_LAYER2_ROAMING */
int proc_get_qos_option(struct seq_file *m, void *v);
int proc_get_ht_option(struct seq_file *m, void *v);
int proc_get_rf_info(struct seq_file *m, void *v);
int proc_get_scan_param(struct seq_file *m, void *v);
ssize_t proc_set_scan_param(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
int proc_get_scan_abort(struct seq_file *m, void *v);
#ifdef CONFIG_SCAN_BACKOP
int proc_get_backop_flags_sta(struct seq_file *m, void *v);
ssize_t proc_set_backop_flags_sta(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
int proc_get_backop_flags_ap(struct seq_file *m, void *v);
ssize_t proc_set_backop_flags_ap(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
#endif /* CONFIG_SCAN_BACKOP */
int proc_get_survey_info(struct seq_file *m, void *v);
ssize_t proc_set_survey_info(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
int proc_get_ap_info(struct seq_file *m, void *v);
ssize_t proc_reset_trx_info(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
int proc_get_trx_info(struct seq_file *m, void *v);
int proc_get_rate_ctl(struct seq_file *m, void *v);
int proc_get_wifi_spec(struct seq_file *m, void *v);
ssize_t proc_set_rate_ctl(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
#ifdef DBG_RX_COUNTER_DUMP
int proc_get_rx_cnt_dump(struct seq_file *m, void *v);
ssize_t proc_set_rx_cnt_dump(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);	
#endif
int proc_get_dis_pwt(struct seq_file *m, void *v);
ssize_t proc_set_dis_pwt(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);	

int proc_get_suspend_resume_info(struct seq_file *m, void *v);

ssize_t proc_set_fwdl_test_case(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
ssize_t proc_set_del_rx_ampdu_test_case(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
#ifdef CONFIG_DFS_MASTER
int proc_get_dfs_master_test_case(struct seq_file *m, void *v);
ssize_t proc_set_dfs_master_test_case(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
#endif /* CONFIG_DFS_MASTER */
ssize_t proc_set_wait_hiq_empty(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

#ifdef CONFIG_AP_MODE
int proc_get_all_sta_info(struct seq_file *m, void *v);
#endif /* CONFIG_AP_MODE */

#ifdef DBG_MEMORY_LEAK
int proc_get_malloc_cnt(struct seq_file *m, void *v);
#endif /* DBG_MEMORY_LEAK */

#ifdef CONFIG_FIND_BEST_CHANNEL
int proc_get_best_channel(struct seq_file *m, void *v);
ssize_t proc_set_best_channel(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
#endif /* CONFIG_FIND_BEST_CHANNEL */

int proc_get_trx_info_debug(struct seq_file *m, void *v);

int proc_get_rx_signal(struct seq_file *m, void *v);
ssize_t proc_set_rx_signal(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
int proc_get_hw_status(struct seq_file *m, void *v);

#ifdef CONFIG_80211N_HT
int proc_get_ht_enable(struct seq_file *m, void *v);
ssize_t proc_set_ht_enable(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

int proc_get_bw_mode(struct seq_file *m, void *v);
ssize_t proc_set_bw_mode(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

int proc_get_ampdu_enable(struct seq_file *m, void *v);
ssize_t proc_set_ampdu_enable(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

int proc_get_mac_rptbuf(struct seq_file *m, void *v);

int proc_get_rx_ampdu(struct seq_file *m, void *v);
ssize_t proc_set_rx_ampdu(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

int proc_get_rx_stbc(struct seq_file *m, void *v);
ssize_t proc_set_rx_stbc(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);


int proc_get_rx_ampdu_factor(struct seq_file *m, void *v);
ssize_t proc_set_rx_ampdu_factor(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

int proc_get_rx_ampdu_density(struct seq_file *m, void *v);
ssize_t proc_set_rx_ampdu_density(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

int proc_get_tx_ampdu_density(struct seq_file *m, void *v);
ssize_t proc_set_tx_ampdu_density(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
#endif /* CONFIG_80211N_HT */

int proc_get_en_fwps(struct seq_file *m, void *v);
ssize_t proc_set_en_fwps(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

//int proc_get_two_path_rssi(struct seq_file *m, void *v);
//int proc_get_rssi_disp(struct seq_file *m, void *v);
//ssize_t proc_set_rssi_disp(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

#ifdef CONFIG_BT_COEXIST
int proc_get_btcoex_dbg(struct seq_file *m, void *v);
ssize_t proc_set_btcoex_dbg(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
int proc_get_btcoex_info(struct seq_file *m, void *v);
#endif /* CONFIG_BT_COEXIST */

#if defined(DBG_CONFIG_ERROR_DETECT)
int proc_get_sreset(struct seq_file *m, void *v);
ssize_t proc_set_sreset(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
#endif /* DBG_CONFIG_ERROR_DETECT */

int proc_get_odm_dbg_comp(struct seq_file *m, void *v);
ssize_t proc_set_odm_dbg_comp(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
int proc_get_odm_dbg_level(struct seq_file *m, void *v);
ssize_t proc_set_odm_dbg_level(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

int proc_get_odm_adaptivity(struct seq_file *m, void *v);
ssize_t proc_set_odm_adaptivity(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

#ifdef CONFIG_DBG_COUNTER
int proc_get_rx_logs(struct seq_file *m, void *v);
int proc_get_tx_logs(struct seq_file *m, void *v);
int proc_get_int_logs(struct seq_file *m, void *v);
#endif

#ifdef CONFIG_PCI_HCI
int proc_get_rx_ring(struct seq_file *m, void *v);
int proc_get_tx_ring(struct seq_file *m, void *v);
#endif

#ifdef CONFIG_GPIO_WAKEUP
int proc_get_wowlan_gpio_info(struct seq_file *m, void *v);
ssize_t proc_set_wowlan_gpio_info(struct file *file, const char __user *buffer,
		size_t count, loff_t *pos, void *data);
#endif /*CONFIG_GPIO_WAKEUP*/

#ifdef CONFIG_P2P_WOWLAN
int proc_get_p2p_wowlan_info(struct seq_file *m, void *v);
#endif /* CONFIG_P2P_WOWLAN */

int proc_get_new_bcn_max(struct seq_file *m, void *v);
ssize_t proc_set_new_bcn_max(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

#ifdef CONFIG_POWER_SAVING
int proc_get_ps_info(struct seq_file *m, void *v);
#endif //CONFIG_POWER_SAVING

#ifdef CONFIG_TDLS
int proc_get_tdls_info(struct seq_file *m, void *v);
#endif

int proc_get_monitor(struct seq_file *m, void *v);
ssize_t proc_set_monitor(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);


#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
int proc_get_rtkm_info(struct seq_file *m, void *v);
#endif /* CONFIG_PREALLOC_RX_SKB_BUFFER */

#ifdef CONFIG_IEEE80211W
ssize_t proc_set_tx_sa_query(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
int proc_get_tx_sa_query(struct seq_file *m, void *v);
ssize_t proc_set_tx_deauth(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
int proc_get_tx_deauth(struct seq_file *m, void *v);
ssize_t proc_set_tx_auth(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);
int proc_get_tx_auth(struct seq_file *m, void *v);
#endif /* CONFIG_IEEE80211W */

#endif /* CONFIG_PROC_DEBUG */

int proc_get_efuse_map(struct seq_file *m, void *v);
ssize_t proc_set_efuse_map(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data);

#endif	//__RTW_DEBUG_H__

