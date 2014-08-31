bool rtl88eu_phy_mac_config(struct adapter *adapt);
bool rtl88eu_phy_rf_config(struct adapter *adapt);
bool rtl88eu_phy_bb_config(struct adapter *adapt);

u32 phy_query_bb_reg(struct adapter *adapt, u32 regaddr, u32 bitmask);
void phy_set_bb_reg(struct adapter *adapt, u32 regaddr, u32 bitmask, u32 data);
u32 phy_query_rf_reg(struct adapter *adapt, enum rf_radio_path rf_path,
		     u32 reg_addr, u32 bit_mask);
