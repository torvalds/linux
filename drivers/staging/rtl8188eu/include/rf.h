#define RF6052_MAX_TX_PWR	0x3F
#define RF6052_MAX_REG		0x3F

void rtl88eu_phy_rf6052_set_bandwidth(struct adapter *adapt,
				      enum ht_channel_width bandwidth);
void rtl88eu_phy_rf6052_set_cck_txpower(struct adapter *adapt,
				       u8 *powerlevel);
void rtl88eu_phy_rf6052_set_ofdm_txpower(struct adapter *adapt,
					 u8 *powerlevel_ofdm,
					 u8 *powerlevel_bw20,
					 u8 *powerlevel_bw40, u8 channel);
