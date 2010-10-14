#ifndef B43_TABLES_NPHY_H_
#define B43_TABLES_NPHY_H_

#include <linux/types.h>

struct b43_phy_n_sfo_cfg {
	u16 phy_bw1a;
	u16 phy_bw2;
	u16 phy_bw3;
	u16 phy_bw4;
	u16 phy_bw5;
	u16 phy_bw6;
};

struct b43_wldev;

struct nphy_txiqcal_ladder {
	u8 percent;
	u8 g_env;
};

struct nphy_rf_control_override_rev2 {
	u8 addr0;
	u8 addr1;
	u16 bmask;
	u8 shift;
};

struct nphy_rf_control_override_rev3 {
	u16 val_mask;
	u8 val_shift;
	u8 en_addr0;
	u8 val_addr0;
	u8 en_addr1;
	u8 val_addr1;
};

/* Get the NPHY Channel Switch Table entry for a channel.
 * Returns NULL on failure to find an entry. */
const struct b43_nphy_channeltab_entry_rev2 *
b43_nphy_get_chantabent_rev2(struct b43_wldev *dev, u8 channel);
const struct b43_nphy_channeltab_entry_rev3 *
b43_nphy_get_chantabent_rev3(struct b43_wldev *dev, u16 freq);


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

#define B43_NTAB_TX_IQLO_CAL_LOFT_LADDER_40_SIZE	18
#define B43_NTAB_TX_IQLO_CAL_LOFT_LADDER_20_SIZE	18
#define B43_NTAB_TX_IQLO_CAL_IQIMB_LADDER_40_SIZE	18
#define B43_NTAB_TX_IQLO_CAL_IQIMB_LADDER_20_SIZE	18
#define B43_NTAB_TX_IQLO_CAL_STARTCOEFS_REV3		11
#define B43_NTAB_TX_IQLO_CAL_STARTCOEFS			9
#define B43_NTAB_TX_IQLO_CAL_CMDS_RECAL_REV3		12
#define B43_NTAB_TX_IQLO_CAL_CMDS_RECAL			10
#define B43_NTAB_TX_IQLO_CAL_CMDS_FULLCAL		10
#define B43_NTAB_TX_IQLO_CAL_CMDS_FULLCAL_REV3		12

u32 b43_ntab_read(struct b43_wldev *dev, u32 offset);
void b43_ntab_read_bulk(struct b43_wldev *dev, u32 offset,
			 unsigned int nr_elements, void *_data);
void b43_ntab_write(struct b43_wldev *dev, u32 offset, u32 value);
void b43_ntab_write_bulk(struct b43_wldev *dev, u32 offset,
			  unsigned int nr_elements, const void *_data);

void b43_nphy_rev0_1_2_tables_init(struct b43_wldev *dev);
void b43_nphy_rev3plus_tables_init(struct b43_wldev *dev);

extern const u32 b43_ntab_tx_gain_rev0_1_2[];
extern const u32 b43_ntab_tx_gain_rev3plus_2ghz[];
extern const u32 b43_ntab_tx_gain_rev3_5ghz[];
extern const u32 b43_ntab_tx_gain_rev4_5ghz[];
extern const u32 b43_ntab_tx_gain_rev5plus_5ghz[];

extern const u32 txpwrctrl_tx_gain_ipa[];
extern const u32 txpwrctrl_tx_gain_ipa_rev5[];
extern const u32 txpwrctrl_tx_gain_ipa_rev6[];
extern const u32 txpwrctrl_tx_gain_ipa_5g[];
extern const u16 tbl_iqcal_gainparams[2][9][8];
extern const struct nphy_txiqcal_ladder ladder_lo[];
extern const struct nphy_txiqcal_ladder ladder_iq[];
extern const u16 loscale[];

extern const u16 tbl_tx_iqlo_cal_loft_ladder_40[];
extern const u16 tbl_tx_iqlo_cal_loft_ladder_20[];
extern const u16 tbl_tx_iqlo_cal_iqimb_ladder_40[];
extern const u16 tbl_tx_iqlo_cal_iqimb_ladder_20[];
extern const u16 tbl_tx_iqlo_cal_startcoefs_nphyrev3[];
extern const u16 tbl_tx_iqlo_cal_startcoefs[];
extern const u16 tbl_tx_iqlo_cal_cmds_recal_nphyrev3[];
extern const u16 tbl_tx_iqlo_cal_cmds_recal[];
extern const u16 tbl_tx_iqlo_cal_cmds_fullcal[];
extern const u16 tbl_tx_iqlo_cal_cmds_fullcal_nphyrev3[];
extern const s16 tbl_tx_filter_coef_rev4[7][15];

extern const struct nphy_rf_control_override_rev2
	tbl_rf_control_override_rev2[];
extern const struct nphy_rf_control_override_rev3
	tbl_rf_control_override_rev3[];

#endif /* B43_TABLES_NPHY_H_ */
