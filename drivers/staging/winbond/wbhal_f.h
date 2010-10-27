/*
 * =====================================================================
 * Device related include
 * =====================================================================
*/
#include "wb35reg_f.h"
#include "wb35tx_f.h"
#include "wb35rx_f.h"

#include "core.h"

/* =====================================================================
 * Function declaration
 * =====================================================================
 */
void hal_remove_mapping_key(struct hw_data *hw_data, u8 *mac_addr);
void hal_remove_default_key(struct hw_data *hw_data, u32 index);
unsigned char hal_set_mapping_key(struct hw_data *adapter, u8 *mac_addr,
				  u8 null_key, u8 wep_on, u8 *tx_tsc,
				  u8 *rx_tsc, u8 key_type, u8 key_len,
				  u8 *key_data);
unsigned char hal_set_default_key(struct hw_data *adapter, u8 index,
				  u8 null_key, u8 wep_on, u8 *tx_tsc,
				  u8 *rx_tsc, u8 key_type, u8 key_len,
				  u8 *key_data);
void hal_clear_all_default_key(struct hw_data *hw_data);
void hal_clear_all_group_key(struct hw_data *hw_data);
void hal_clear_all_mapping_key(struct hw_data *hw_data);
void hal_clear_all_key(struct hw_data *hw_data);
void hal_set_power_save_mode(struct hw_data *hw_data, unsigned char power_save,
			     unsigned char wakeup, unsigned char dtim);
void hal_get_power_save_mode(struct hw_data *hw_data, u8 *in_pwr_save);
void hal_set_slot_time(struct hw_data *hw_data, u8 type);

#define hal_set_atim_window(_A, _ATM)

void hal_start_bss(struct hw_data *hw_data, u8 mac_op_mode);

/* 0:BSS STA 1:IBSS STA */
void hal_join_request(struct hw_data *hw_data, u8 bss_type);

void hal_stop_sync_bss(struct hw_data *hw_data);
void hal_resume_sync_bss(struct hw_data *hw_data);
void hal_set_aid(struct hw_data *hw_data, u16 aid);
void hal_set_bssid(struct hw_data *hw_data, u8 *bssid);
void hal_get_bssid(struct hw_data *hw_data, u8 *bssid);
void hal_set_listen_interval(struct hw_data *hw_data, u16 listen_interval);
void hal_set_cap_info(struct hw_data *hw_data, u16 capability_info);
void hal_set_ssid(struct hw_data *hw_data, u8 *ssid, u8 ssid_len);
void hal_start_tx0(struct hw_data *hw_data);

#define hal_get_cwmin(_A)	((_A)->cwmin)

void hal_set_cwmax(struct hw_data *hw_data, u16 cwin_max);

#define hal_get_cwmax(_A)	((_A)->cwmax)

void hal_set_rsn_wpa(struct hw_data *hw_data, u32 *rsn_ie_bitmap,
		     u32 *rsn_oui_type , unsigned char desired_auth_mode);
void hal_set_connect_info(struct hw_data *hw_data, unsigned char bo_connect);
u8 hal_get_est_sq3(struct hw_data *hw_data, u8 count);
void hal_descriptor_indicate(struct hw_data *hw_data,
			     struct wb35_descriptor *des);
u8 hal_get_antenna_number(struct hw_data *hw_data);
u32 hal_get_bss_pk_cnt(struct hw_data *hw_data);

#define hal_get_region_from_EEPROM(_A)	((_A)->reg.EEPROMRegion)
#define hal_get_tx_buffer(_A, _B)	Wb35Tx_get_tx_buffer(_A, _B)
#define hal_software_set(_A)		(_A->SoftwareSet)
#define hal_driver_init_OK(_A)		(_A->IsInitOK)
#define hal_rssi_boundary_high(_A)	(_A->RSSI_high)
#define hal_rssi_boundary_low(_A)	(_A->RSSI_low)
#define hal_scan_interval(_A)		(_A->Scan_Interval)

#define PHY_DEBUG(msg, args...)

/* return 100ms count */
#define hal_get_time_count(_P)		(_P->time_count / 10)
#define hal_detect_error(_P)		(_P->WbUsb.DetectCount)

/* The follow function is unused for IS89C35 */
#define hal_disable_interrupt(_A)
#define hal_enable_interrupt(_A)
#define hal_get_interrupt_type(_A)
#define hal_get_clear_interrupt(_A)
#define hal_ibss_disconnect(_A)		(hal_stop_sync_bss(_A))
#define hal_join_request_stop(_A)
#define hw_get_cxx_reg(_A, _B, _C)
#define hw_set_cxx_reg(_A, _B, _C)


