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

struct nphy_rf_control_override_rev7 {
	u16 field;
	u16 val_addr_core0;
	u16 val_addr_core1;
	u16 val_mask;
	u8 val_shift;
};

struct nphy_gain_ctl_workaround_entry {
	s8 lna1_gain[4];
	s8 lna2_gain[4];
	u8 gain_db[10];
	u8 gain_bits[10];

	u16 init_gain;
	u16 rfseq_init[4];

	u16 cliphi_gain;
	u16 clipmd_gain;
	u16 cliplo_gain;

	u16 crsmin;
	u16 crsminl;
	u16 crsminu;

	u16 nbclip;
	u16 wlclip;
};

/* Get entry with workaround values for gain ctl. Does not return NULL. */
struct nphy_gain_ctl_workaround_entry *b43_nphy_get_gain_ctl_workaround_ent(
	struct b43_wldev *dev, bool ghz5, bool ext_lna);


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

/* Volatile N-PHY tables, PHY revision >= 3 */
#define B43_NTAB_ANT_SW_CTL_R3		B43_NTAB16( 9,   0) /* antenna software control */

/* Static N-PHY tables, PHY revision >= 3 */
#define B43_NTAB_FRAMESTRUCT_R3		B43_NTAB32(10,   0) /* frame struct  */
#define B43_NTAB_PILOT_R3		B43_NTAB16(11,   0) /* pilot  */
#define B43_NTAB_TMAP_R3		B43_NTAB32(12,   0) /* TM AP  */
#define B43_NTAB_INTLEVEL_R3		B43_NTAB32(13,   0) /* INT LV  */
#define B43_NTAB_TDTRN_R3		B43_NTAB32(14,   0) /* TD TRN  */
#define B43_NTAB_NOISEVAR0_R3		B43_NTAB32(16,   0) /* noise variance 0  */
#define B43_NTAB_NOISEVAR1_R3		B43_NTAB32(16, 128) /* noise variance 1  */
#define B43_NTAB_MCS_R3			B43_NTAB16(18,   0) /* MCS  */
#define B43_NTAB_TDI20A0_R3		B43_NTAB32(19, 128) /* TDI 20/0  */
#define B43_NTAB_TDI20A1_R3		B43_NTAB32(19, 256) /* TDI 20/1  */
#define B43_NTAB_TDI40A0_R3		B43_NTAB32(19, 640) /* TDI 40/0  */
#define B43_NTAB_TDI40A1_R3		B43_NTAB32(19, 768) /* TDI 40/1  */
#define B43_NTAB_PILOTLT_R3		B43_NTAB32(20,   0) /* PLT lookup  */
#define B43_NTAB_CHANEST_R3		B43_NTAB32(22,   0) /* channel estimate  */
#define B43_NTAB_FRAMELT_R3		 B43_NTAB8(24,   0) /* frame lookup  */
#define B43_NTAB_C0_ESTPLT_R3		 B43_NTAB8(26,   0) /* estimated power lookup 0  */
#define B43_NTAB_C1_ESTPLT_R3		 B43_NTAB8(27,   0) /* estimated power lookup 1  */
#define B43_NTAB_C0_ADJPLT_R3		 B43_NTAB8(26,  64) /* adjusted power lookup 0  */
#define B43_NTAB_C1_ADJPLT_R3		 B43_NTAB8(27,  64) /* adjusted power lookup 1  */
#define B43_NTAB_C0_GAINCTL_R3		B43_NTAB32(26, 192) /* gain control lookup 0  */
#define B43_NTAB_C1_GAINCTL_R3		B43_NTAB32(27, 192) /* gain control lookup 1  */
#define B43_NTAB_C0_IQLT_R3		B43_NTAB32(26, 320) /* I/Q lookup 0  */
#define B43_NTAB_C1_IQLT_R3		B43_NTAB32(27, 320) /* I/Q lookup 1  */
#define B43_NTAB_C0_LOFEEDTH_R3		B43_NTAB16(26, 448) /* Local Oscillator Feed Through lookup 0  */
#define B43_NTAB_C1_LOFEEDTH_R3		B43_NTAB16(27, 448) /* Local Oscillator Feed Through lookup 1 */

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

const u32 *b43_nphy_get_tx_gain_table(struct b43_wldev *dev);

extern const s8 b43_ntab_papd_pga_gain_delta_ipa_2g[];

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
const struct nphy_rf_control_override_rev7 *b43_nphy_get_rf_ctl_over_rev7(
	struct b43_wldev *dev, u16 field, u8 override);

#endif /* B43_TABLES_NPHY_H_ */
