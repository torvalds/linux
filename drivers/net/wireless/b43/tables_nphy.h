#ifndef B43_TABLES_NPHY_H_
#define B43_TABLES_NPHY_H_

#include <linux/types.h>


struct b43_nphy_channeltab_entry {
	/* The channel number */
	u8 channel;
	/* Radio register values on channelswitch */
	u8 radio_pll_ref;
	u8 radio_rf_pllmod0;
	u8 radio_rf_pllmod1;
	u8 radio_vco_captail;
	u8 radio_vco_cal1;
	u8 radio_vco_cal2;
	u8 radio_pll_lfc1;
	u8 radio_pll_lfr1;
	u8 radio_pll_lfc2;
	u8 radio_lgbuf_cenbuf;
	u8 radio_lgen_tune1;
	u8 radio_lgen_tune2;
	u8 radio_c1_lgbuf_atune;
	u8 radio_c1_lgbuf_gtune;
	u8 radio_c1_rx_rfr1;
	u8 radio_c1_tx_pgapadtn;
	u8 radio_c1_tx_mxbgtrim;
	u8 radio_c2_lgbuf_atune;
	u8 radio_c2_lgbuf_gtune;
	u8 radio_c2_rx_rfr1;
	u8 radio_c2_tx_pgapadtn;
	u8 radio_c2_tx_mxbgtrim;
	/* PHY register values on channelswitch */
	u16 phy_bw1a;
	u16 phy_bw2;
	u16 phy_bw3;
	u16 phy_bw4;
	u16 phy_bw5;
	u16 phy_bw6;
	/* The channel frequency in MHz */
	u16 freq;
	/* An unknown value */
	u16 unk2;
};


struct b43_wldev;

/* Upload the default register value table.
 * If "ghz5" is true, we upload the 5Ghz table. Otherwise the 2.4Ghz
 * table is uploaded. If "ignore_uploadflag" is true, we upload any value
 * and ignore the "UPLOAD" flag. */
void b2055_upload_inittab(struct b43_wldev *dev,
			  bool ghz5, bool ignore_uploadflag);


/* Get the NPHY Channel Switch Table entry for a channel number.
 * Returns NULL on failure to find an entry. */
const struct b43_nphy_channeltab_entry *
b43_nphy_get_chantabent(struct b43_wldev *dev, u8 channel);


#endif /* B43_TABLES_NPHY_H_ */
