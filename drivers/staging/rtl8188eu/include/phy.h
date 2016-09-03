#include <odm.h>

#define IQK_DELAY_TIME_88E	    10
#define index_mapping_NUM_88E	    15
#define AVG_THERMAL_NUM_88E	    4
#define ODM_TARGET_CHNL_NUM_2G_5G   59

bool rtl88eu_phy_mac_config(struct adapter *adapt);
bool rtl88eu_phy_rf_config(struct adapter *adapt);
bool rtl88eu_phy_bb_config(struct adapter *adapt);

u32 phy_query_bb_reg(struct adapter *adapt, u32 regaddr, u32 bitmask);
void phy_set_bb_reg(struct adapter *adapt, u32 regaddr, u32 bitmask, u32 data);
u32 rtw_hal_read_rfreg(struct adapter *adapt, enum rf_radio_path rf_path,
		     u32 reg_addr, u32 bit_mask);
void phy_set_rf_reg(struct adapter *adapt, enum rf_radio_path rf_path,
		    u32 reg_addr, u32 bit_mask, u32 data);

void phy_set_tx_power_level(struct adapter *adapt, u8 channel);

void rtl88eu_dm_txpower_track_adjust(struct odm_dm_struct *dm_odm,
				     u8 type, u8 *dir, u32 *out_write);

void rtl88eu_dm_txpower_tracking_callback_thermalmeter(struct adapter *adapt);
void rtl88eu_phy_iq_calibrate(struct adapter *adapter, bool recovery);
void rtl88eu_phy_lc_calibrate(struct adapter *adapter);
