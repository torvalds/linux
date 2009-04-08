//=====================================================================
// Device related include
//=====================================================================
#include "wb35reg_f.h"
#include "wb35tx_f.h"
#include "wb35rx_f.h"

#include "core.h"

//====================================================================================
// Function declaration
//====================================================================================
void hal_remove_mapping_key(  struct hw_data * pHwData,  u8 *pmac_addr );
void hal_remove_default_key(  struct hw_data * pHwData,  u32 index );
unsigned char hal_set_mapping_key(  struct hw_data * adapter,  u8 *pmac_addr,  u8 null_key,  u8 wep_on,  u8 *ptx_tsc,  u8 *prx_tsc,  u8 key_type,  u8 key_len,  u8 *pkey_data );
unsigned char hal_set_default_key(  struct hw_data * adapter,  u8 index,  u8 null_key,  u8 wep_on,  u8 *ptx_tsc,  u8 *prx_tsc,  u8 key_type,  u8 key_len,  u8 *pkey_data );
void hal_clear_all_default_key(  struct hw_data * pHwData );
void hal_clear_all_group_key(  struct hw_data * pHwData );
void hal_clear_all_mapping_key(  struct hw_data * pHwData );
void hal_clear_all_key(  struct hw_data * pHwData );
void hal_get_ethernet_address(  struct hw_data * pHwData,  u8 *current_address );
void hal_set_ethernet_address(  struct hw_data * pHwData,  u8 *current_address );
void hal_get_permanent_address(  struct hw_data * pHwData,  u8 *pethernet_address );
void hal_set_power_save_mode(  struct hw_data * pHwData,  unsigned char power_save,  unsigned char wakeup,  unsigned char dtim );
void hal_get_power_save_mode(  struct hw_data * pHwData,   u8 *pin_pwr_save );
void hal_set_slot_time(  struct hw_data * pHwData,  u8 type );
#define hal_set_atim_window( _A, _ATM )
void hal_start_bss(  struct hw_data * pHwData,  u8 mac_op_mode );
void hal_join_request(  struct hw_data * pHwData,  u8 bss_type ); // 0:BSS STA 1:IBSS STA//
void hal_stop_sync_bss(  struct hw_data * pHwData );
void hal_resume_sync_bss(  struct hw_data * pHwData);
void hal_set_aid(  struct hw_data * pHwData,  u16 aid );
void hal_set_bssid(  struct hw_data * pHwData,  u8 *pbssid );
void hal_get_bssid(  struct hw_data * pHwData,  u8 *pbssid );
void hal_set_beacon_period(  struct hw_data * pHwData,  u16 beacon_period );
void hal_set_listen_interval(  struct hw_data * pHwData,  u16 listen_interval );
void hal_set_cap_info(  struct hw_data * pHwData,  u16 capability_info );
void hal_set_ssid(  struct hw_data * pHwData,  u8 *pssid,  u8 ssid_len );
void hal_set_current_channel(  struct hw_data * pHwData,  ChanInfo channel );
void hal_set_accept_broadcast(  struct hw_data * pHwData,  u8 enable );
void hal_set_accept_multicast(  struct hw_data * pHwData,  u8 enable );
void hal_set_accept_beacon(  struct hw_data * pHwData,  u8 enable );
void hal_stop(  struct hw_data * pHwData );
void hal_start_tx0(  struct hw_data * pHwData );
#define hal_get_cwmin( _A ) ( (_A)->cwmin )
void hal_set_cwmax(  struct hw_data * pHwData,  u16 cwin_max );
#define hal_get_cwmax( _A ) ( (_A)->cwmax )
void hal_set_rsn_wpa(  struct hw_data * pHwData,  u32 * RSN_IE_Bitmap , u32 * RSN_OUI_type , unsigned char bDesiredAuthMode);
void hal_set_connect_info(  struct hw_data * pHwData,  unsigned char boConnect );
u8 hal_get_est_sq3(  struct hw_data * pHwData,  u8 Count );
void hal_set_rf_power(  struct hw_data * pHwData,  u8 PowerIndex ); // 20060621 Modify
void hal_set_radio_mode(  struct hw_data * pHwData,  unsigned char boValue);
void hal_descriptor_indicate(  struct hw_data * pHwData,  PDESCRIPTOR pDes );
u8 hal_get_antenna_number(  struct hw_data * pHwData );
u32 hal_get_bss_pk_cnt(  struct hw_data * pHwData );
#define hal_get_region_from_EEPROM( _A ) ( (_A)->reg.EEPROMRegion )
void hal_set_accept_promiscuous		(  struct hw_data * pHwData,  u8 enable);
#define hal_get_tx_buffer( _A, _B ) Wb35Tx_get_tx_buffer( _A, _B )
u8 hal_get_hw_radio_off			(  struct hw_data * pHwData );
#define hal_software_set( _A )		(_A->SoftwareSet)
#define hal_driver_init_OK( _A )	(_A->IsInitOK)
#define hal_rssi_boundary_high( _A ) (_A->RSSI_high)
#define hal_rssi_boundary_low( _A ) (_A->RSSI_low)
#define hal_scan_interval( _A )		(_A->Scan_Interval)

#define PHY_DEBUG( msg, args... )

unsigned char hal_get_dxx_reg(  struct hw_data * pHwData,  u16 number,  u32 * pValue );
unsigned char hal_set_dxx_reg(  struct hw_data * pHwData,  u16 number,  u32 value );
#define hal_get_time_count( _P )	(_P->time_count/10)	// return 100ms count
#define hal_detect_error( _P )		(_P->WbUsb.DetectCount)

//-------------------------------------------------------------------------
// The follow function is unused for IS89C35
//-------------------------------------------------------------------------
#define hal_disable_interrupt(_A)
#define hal_enable_interrupt(_A)
#define hal_get_interrupt_type( _A)
#define hal_get_clear_interrupt(_A)
#define hal_ibss_disconnect(_A) hal_stop_sync_bss(_A)
#define hal_join_request_stop(_A)
unsigned char	hal_idle(  struct hw_data * pHwData );
#define hw_get_cxx_reg( _A, _B, _C )
#define hw_set_cxx_reg( _A, _B, _C )
#define hw_get_dxx_reg( _A, _B, _C )	hal_get_dxx_reg( _A, _B, (u32 *)_C )
#define hw_set_dxx_reg( _A, _B, _C )	hal_set_dxx_reg( _A, _B, (u32)_C )


