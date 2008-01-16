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


/* The N-PHY tables. */

#define B43_NTAB_TYPEMASK		0xF0000000
#define B43_NTAB_8BIT			0x10000000
#define B43_NTAB_16BIT			0x20000000
#define B43_NTAB_32BIT			0x30000000
#define B43_NTAB8(table, offset)	(((table) << 10) | (offset) | B43_NTAB_8BIT)
#define B43_NTAB16(table, offset)	(((table) << 10) | (offset) | B43_NTAB_16BIT)
#define B43_NTAB32(table, offset)	(((table) << 10) | (offset) | B43_NTAB_32BIT)

/* Static N-PHY tables */
#define B43_NTAB_FRAMESTRUCT		B43_NTAB32(0x0A, 0x000) /* Frame Struct Table */
#define B43_NTAB_FRAMESTRUCT_SIZE	832
#define B43_NTAB_FRAMELT		B43_NTAB8 (0x18, 0x000) /* Frame Lookup Table */
#define B43_NTAB_FRAMELT_SIZE		32
#define B43_NTAB_TMAP			B43_NTAB32(0x0C, 0x000) /* T Map Table */
#define B43_NTAB_TMAP_SIZE		448
#define B43_NTAB_TDTRN			B43_NTAB32(0x0E, 0x000) /* TDTRN Table */
#define B43_NTAB_TDTRN_SIZE		704
#define B43_NTAB_INTLEVEL		B43_NTAB32(0x0D, 0x000) /* Int Level Table */
#define B43_NTAB_INTLEVEL_SIZE		7
#define B43_NTAB_PILOT			B43_NTAB16(0x0B, 0x000) /* Pilot Table */
#define B43_NTAB_PILOT_SIZE		88
#define B43_NTAB_PILOTLT		B43_NTAB32(0x14, 0x000) /* Pilot Lookup Table */
#define B43_NTAB_PILOTLT_SIZE		6
#define B43_NTAB_TDI20A0		B43_NTAB32(0x13, 0x080) /* TDI Table 20 Antenna 0 */
#define B43_NTAB_TDI20A0_SIZE		55
#define B43_NTAB_TDI20A1		B43_NTAB32(0x13, 0x100) /* TDI Table 20 Antenna 1 */
#define B43_NTAB_TDI20A1_SIZE		55
#define B43_NTAB_TDI40A0		B43_NTAB32(0x13, 0x280) /* TDI Table 40 Antenna 0 */
#define B43_NTAB_TDI40A0_SIZE		110
#define B43_NTAB_TDI40A1		B43_NTAB32(0x13, 0x300) /* TDI Table 40 Antenna 1 */
#define B43_NTAB_TDI40A1_SIZE		110
#define B43_NTAB_BDI			B43_NTAB16(0x15, 0x000) /* BDI Table */
#define B43_NTAB_BDI_SIZE		6
#define B43_NTAB_CHANEST		B43_NTAB32(0x16, 0x000) /* Channel Estimate Table */
#define B43_NTAB_CHANEST_SIZE		96
#define B43_NTAB_MCS			B43_NTAB8 (0x12, 0x000) /* MCS Table */
#define B43_NTAB_MCS_SIZE		128

/* Volatile N-PHY tables */
#define B43_NTAB_NOISEVAR10		B43_NTAB32(0x10, 0x000) /* Noise Var Table 10 */
#define B43_NTAB_NOISEVAR10_SIZE	256
#define B43_NTAB_NOISEVAR11		B43_NTAB32(0x10, 0x080) /* Noise Var Table 11 */
#define B43_NTAB_NOISEVAR11_SIZE	256
#define B43_NTAB_C0_ESTPLT		B43_NTAB8 (0x1A, 0x000) /* Estimate Power Lookup Table Core 0 */
#define B43_NTAB_C0_ESTPLT_SIZE		64
#define B43_NTAB_C1_ESTPLT		B43_NTAB8 (0x1B, 0x000) /* Estimate Power Lookup Table Core 1 */
#define B43_NTAB_C1_ESTPLT_SIZE		64
#define B43_NTAB_C0_ADJPLT		B43_NTAB8 (0x1A, 0x040) /* Adjust Power Lookup Table Core 0 */
#define B43_NTAB_C0_ADJPLT_SIZE		128
#define B43_NTAB_C1_ADJPLT		B43_NTAB8 (0x1B, 0x040) /* Adjust Power Lookup Table Core 1 */
#define B43_NTAB_C1_ADJPLT_SIZE		128
#define B43_NTAB_C0_GAINCTL		B43_NTAB32(0x1A, 0x0C0) /* Gain Control Lookup Table Core 0 */
#define B43_NTAB_C0_GAINCTL_SIZE	128
#define B43_NTAB_C1_GAINCTL		B43_NTAB32(0x1B, 0x0C0) /* Gain Control Lookup Table Core 1 */
#define B43_NTAB_C1_GAINCTL_SIZE	128
#define B43_NTAB_C0_IQLT		B43_NTAB32(0x1A, 0x140) /* IQ Lookup Table Core 0 */
#define B43_NTAB_C0_IQLT_SIZE		128
#define B43_NTAB_C1_IQLT		B43_NTAB32(0x1B, 0x140) /* IQ Lookup Table Core 1 */
#define B43_NTAB_C1_IQLT_SIZE		128
#define B43_NTAB_C0_LOFEEDTH		B43_NTAB16(0x1A, 0x1C0) /* Local Oscillator Feed Through Lookup Table Core 0 */
#define B43_NTAB_C0_LOFEEDTH_SIZE	128
#define B43_NTAB_C1_LOFEEDTH		B43_NTAB16(0x1B, 0x1C0) /* Local Oscillator Feed Through Lookup Table Core 1 */
#define B43_NTAB_C1_LOFEEDTH_SIZE	128

void b43_ntab_write(struct b43_wldev *dev, u32 offset, u32 value);

extern const u8 b43_ntab_adjustpower0[];
extern const u8 b43_ntab_adjustpower1[];
extern const u16 b43_ntab_bdi[];
extern const u32 b43_ntab_channelest[];
extern const u8 b43_ntab_estimatepowerlt0[];
extern const u8 b43_ntab_estimatepowerlt1[];
extern const u8 b43_ntab_framelookup[];
extern const u32 b43_ntab_framestruct[];
extern const u32 b43_ntab_gainctl0[];
extern const u32 b43_ntab_gainctl1[];
extern const u32 b43_ntab_intlevel[];
extern const u32 b43_ntab_iqlt0[];
extern const u32 b43_ntab_iqlt1[];
extern const u16 b43_ntab_loftlt0[];
extern const u16 b43_ntab_loftlt1[];
extern const u8 b43_ntab_mcs[];
extern const u32 b43_ntab_noisevar10[];
extern const u32 b43_ntab_noisevar11[];
extern const u16 b43_ntab_pilot[];
extern const u32 b43_ntab_pilotlt[];
extern const u32 b43_ntab_tdi20a0[];
extern const u32 b43_ntab_tdi20a1[];
extern const u32 b43_ntab_tdi40a0[];
extern const u32 b43_ntab_tdi40a1[];
extern const u32 b43_ntab_tdtrn[];
extern const u32 b43_ntab_tmap[];


#endif /* B43_TABLES_NPHY_H_ */
