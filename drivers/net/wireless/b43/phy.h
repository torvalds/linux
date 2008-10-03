#ifndef B43_PHY_H_
#define B43_PHY_H_

#include <linux/types.h>

struct b43_wldev;
struct b43_phy;

/*** PHY Registers ***/

/* Routing */
#define B43_PHYROUTE			0x0C00 /* PHY register routing bits mask */
#define  B43_PHYROUTE_BASE		0x0000 /* Base registers */
#define  B43_PHYROUTE_OFDM_GPHY		0x0400 /* OFDM register routing for G-PHYs */
#define  B43_PHYROUTE_EXT_GPHY		0x0800 /* Extended G-PHY registers */
#define  B43_PHYROUTE_N_BMODE		0x0C00 /* N-PHY BMODE registers */

/* CCK (B-PHY) registers. */
#define B43_PHY_CCK(reg)		((reg) | B43_PHYROUTE_BASE)
/* N-PHY registers. */
#define B43_PHY_N(reg)			((reg) | B43_PHYROUTE_BASE)
/* N-PHY BMODE registers. */
#define B43_PHY_N_BMODE(reg)		((reg) | B43_PHYROUTE_N_BMODE)
/* OFDM (A-PHY) registers. */
#define B43_PHY_OFDM(reg)		((reg) | B43_PHYROUTE_OFDM_GPHY)
/* Extended G-PHY registers. */
#define B43_PHY_EXTG(reg)		((reg) | B43_PHYROUTE_EXT_GPHY)

/* OFDM (A) PHY Registers */
#define B43_PHY_VERSION_OFDM		B43_PHY_OFDM(0x00)	/* Versioning register for A-PHY */
#define B43_PHY_BBANDCFG		B43_PHY_OFDM(0x01)	/* Baseband config */
#define  B43_PHY_BBANDCFG_RXANT		0x180	/* RX Antenna selection */
#define  B43_PHY_BBANDCFG_RXANT_SHIFT	7
#define B43_PHY_PWRDOWN			B43_PHY_OFDM(0x03)	/* Powerdown */
#define B43_PHY_CRSTHRES1_R1		B43_PHY_OFDM(0x06)	/* CRS Threshold 1 (phy.rev 1 only) */
#define B43_PHY_LNAHPFCTL		B43_PHY_OFDM(0x1C)	/* LNA/HPF control */
#define B43_PHY_LPFGAINCTL		B43_PHY_OFDM(0x20)	/* LPF Gain control */
#define B43_PHY_ADIVRELATED		B43_PHY_OFDM(0x27)	/* FIXME rename */
#define B43_PHY_CRS0			B43_PHY_OFDM(0x29)
#define  B43_PHY_CRS0_EN		0x4000
#define B43_PHY_PEAK_COUNT		B43_PHY_OFDM(0x30)
#define B43_PHY_ANTDWELL		B43_PHY_OFDM(0x2B)	/* Antenna dwell */
#define  B43_PHY_ANTDWELL_AUTODIV1	0x0100	/* Automatic RX diversity start antenna */
#define B43_PHY_ENCORE			B43_PHY_OFDM(0x49)	/* "Encore" (RangeMax / BroadRange) */
#define  B43_PHY_ENCORE_EN		0x0200	/* Encore enable */
#define B43_PHY_LMS			B43_PHY_OFDM(0x55)
#define B43_PHY_OFDM61			B43_PHY_OFDM(0x61)	/* FIXME rename */
#define  B43_PHY_OFDM61_10		0x0010	/* FIXME rename */
#define B43_PHY_IQBAL			B43_PHY_OFDM(0x69)	/* I/Q balance */
#define B43_PHY_BBTXDC_BIAS		B43_PHY_OFDM(0x6B)	/* Baseband TX DC bias */
#define B43_PHY_OTABLECTL		B43_PHY_OFDM(0x72)	/* OFDM table control (see below) */
#define  B43_PHY_OTABLEOFF		0x03FF	/* OFDM table offset (see below) */
#define  B43_PHY_OTABLENR		0xFC00	/* OFDM table number (see below) */
#define  B43_PHY_OTABLENR_SHIFT		10
#define B43_PHY_OTABLEI			B43_PHY_OFDM(0x73)	/* OFDM table data I */
#define B43_PHY_OTABLEQ			B43_PHY_OFDM(0x74)	/* OFDM table data Q */
#define B43_PHY_HPWR_TSSICTL		B43_PHY_OFDM(0x78)	/* Hardware power TSSI control */
#define B43_PHY_ADCCTL			B43_PHY_OFDM(0x7A)	/* ADC control */
#define B43_PHY_IDLE_TSSI		B43_PHY_OFDM(0x7B)
#define B43_PHY_A_TEMP_SENSE		B43_PHY_OFDM(0x7C)	/* A PHY temperature sense */
#define B43_PHY_NRSSITHRES		B43_PHY_OFDM(0x8A)	/* NRSSI threshold */
#define B43_PHY_ANTWRSETT		B43_PHY_OFDM(0x8C)	/* Antenna WR settle */
#define  B43_PHY_ANTWRSETT_ARXDIV	0x2000	/* Automatic RX diversity enabled */
#define B43_PHY_CLIPPWRDOWNT		B43_PHY_OFDM(0x93)	/* Clip powerdown threshold */
#define B43_PHY_OFDM9B			B43_PHY_OFDM(0x9B)	/* FIXME rename */
#define B43_PHY_N1P1GAIN		B43_PHY_OFDM(0xA0)
#define B43_PHY_P1P2GAIN		B43_PHY_OFDM(0xA1)
#define B43_PHY_N1N2GAIN		B43_PHY_OFDM(0xA2)
#define B43_PHY_CLIPTHRES		B43_PHY_OFDM(0xA3)
#define B43_PHY_CLIPN1P2THRES		B43_PHY_OFDM(0xA4)
#define B43_PHY_CCKSHIFTBITS_WA		B43_PHY_OFDM(0xA5)	/* CCK shiftbits workaround, FIXME rename */
#define B43_PHY_CCKSHIFTBITS		B43_PHY_OFDM(0xA7)	/* FIXME rename */
#define B43_PHY_DIVSRCHIDX		B43_PHY_OFDM(0xA8)	/* Divider search gain/index */
#define B43_PHY_CLIPP2THRES		B43_PHY_OFDM(0xA9)
#define B43_PHY_CLIPP3THRES		B43_PHY_OFDM(0xAA)
#define B43_PHY_DIVP1P2GAIN		B43_PHY_OFDM(0xAB)
#define B43_PHY_DIVSRCHGAINBACK		B43_PHY_OFDM(0xAD)	/* Divider search gain back */
#define B43_PHY_DIVSRCHGAINCHNG		B43_PHY_OFDM(0xAE)	/* Divider search gain change */
#define B43_PHY_CRSTHRES1		B43_PHY_OFDM(0xC0)	/* CRS Threshold 1 (phy.rev >= 2 only) */
#define B43_PHY_CRSTHRES2		B43_PHY_OFDM(0xC1)	/* CRS Threshold 2 (phy.rev >= 2 only) */
#define B43_PHY_TSSIP_LTBASE		B43_PHY_OFDM(0x380)	/* TSSI power lookup table base */
#define B43_PHY_DC_LTBASE		B43_PHY_OFDM(0x3A0)	/* DC lookup table base */
#define B43_PHY_GAIN_LTBASE		B43_PHY_OFDM(0x3C0)	/* Gain lookup table base */

/* CCK (B) PHY Registers */
#define B43_PHY_VERSION_CCK		B43_PHY_CCK(0x00)	/* Versioning register for B-PHY */
#define B43_PHY_CCKBBANDCFG		B43_PHY_CCK(0x01)	/* Contains antenna 0/1 control bit */
#define B43_PHY_PGACTL			B43_PHY_CCK(0x15)	/* PGA control */
#define  B43_PHY_PGACTL_LPF		0x1000	/* Low pass filter (?) */
#define  B43_PHY_PGACTL_LOWBANDW	0x0040	/* Low bandwidth flag */
#define  B43_PHY_PGACTL_UNKNOWN		0xEFA0
#define B43_PHY_FBCTL1			B43_PHY_CCK(0x18)	/* Frequency bandwidth control 1 */
#define B43_PHY_ITSSI			B43_PHY_CCK(0x29)	/* Idle TSSI */
#define B43_PHY_LO_LEAKAGE		B43_PHY_CCK(0x2D)	/* Measured LO leakage */
#define B43_PHY_ENERGY			B43_PHY_CCK(0x33)	/* Energy */
#define B43_PHY_SYNCCTL			B43_PHY_CCK(0x35)
#define B43_PHY_FBCTL2			B43_PHY_CCK(0x38)	/* Frequency bandwidth control 2 */
#define B43_PHY_DACCTL			B43_PHY_CCK(0x60)	/* DAC control */
#define B43_PHY_RCCALOVER		B43_PHY_CCK(0x78)	/* RC calibration override */

/* Extended G-PHY Registers */
#define B43_PHY_CLASSCTL		B43_PHY_EXTG(0x02)	/* Classify control */
#define B43_PHY_GTABCTL			B43_PHY_EXTG(0x03)	/* G-PHY table control (see below) */
#define  B43_PHY_GTABOFF		0x03FF	/* G-PHY table offset (see below) */
#define  B43_PHY_GTABNR			0xFC00	/* G-PHY table number (see below) */
#define  B43_PHY_GTABNR_SHIFT		10
#define B43_PHY_GTABDATA		B43_PHY_EXTG(0x04)	/* G-PHY table data */
#define B43_PHY_LO_MASK			B43_PHY_EXTG(0x0F)	/* Local Oscillator control mask */
#define B43_PHY_LO_CTL			B43_PHY_EXTG(0x10)	/* Local Oscillator control */
#define B43_PHY_RFOVER			B43_PHY_EXTG(0x11)	/* RF override */
#define B43_PHY_RFOVERVAL		B43_PHY_EXTG(0x12)	/* RF override value */
#define  B43_PHY_RFOVERVAL_EXTLNA	0x8000
#define  B43_PHY_RFOVERVAL_LNA		0x7000
#define  B43_PHY_RFOVERVAL_LNA_SHIFT	12
#define  B43_PHY_RFOVERVAL_PGA		0x0F00
#define  B43_PHY_RFOVERVAL_PGA_SHIFT	8
#define  B43_PHY_RFOVERVAL_UNK		0x0010	/* Unknown, always set. */
#define  B43_PHY_RFOVERVAL_TRSWRX	0x00E0
#define  B43_PHY_RFOVERVAL_BW		0x0003	/* Bandwidth flags */
#define   B43_PHY_RFOVERVAL_BW_LPF	0x0001	/* Low Pass Filter */
#define   B43_PHY_RFOVERVAL_BW_LBW	0x0002	/* Low Bandwidth (when set), high when unset */
#define B43_PHY_ANALOGOVER		B43_PHY_EXTG(0x14)	/* Analog override */
#define B43_PHY_ANALOGOVERVAL		B43_PHY_EXTG(0x15)	/* Analog override value */

/*** OFDM table numbers ***/
#define B43_OFDMTAB(number, offset)	(((number) << B43_PHY_OTABLENR_SHIFT) | (offset))
#define B43_OFDMTAB_AGC1		B43_OFDMTAB(0x00, 0)
#define B43_OFDMTAB_GAIN0		B43_OFDMTAB(0x00, 0)
#define B43_OFDMTAB_GAINX		B43_OFDMTAB(0x01, 0)	//TODO rename
#define B43_OFDMTAB_GAIN1		B43_OFDMTAB(0x01, 4)
#define B43_OFDMTAB_AGC3		B43_OFDMTAB(0x02, 0)
#define B43_OFDMTAB_GAIN2		B43_OFDMTAB(0x02, 3)
#define B43_OFDMTAB_LNAHPFGAIN1		B43_OFDMTAB(0x03, 0)
#define B43_OFDMTAB_WRSSI		B43_OFDMTAB(0x04, 0)
#define B43_OFDMTAB_LNAHPFGAIN2		B43_OFDMTAB(0x04, 0)
#define B43_OFDMTAB_NOISESCALE		B43_OFDMTAB(0x05, 0)
#define B43_OFDMTAB_AGC2		B43_OFDMTAB(0x06, 0)
#define B43_OFDMTAB_ROTOR		B43_OFDMTAB(0x08, 0)
#define B43_OFDMTAB_ADVRETARD		B43_OFDMTAB(0x09, 0)
#define B43_OFDMTAB_DAC			B43_OFDMTAB(0x0C, 0)
#define B43_OFDMTAB_DC			B43_OFDMTAB(0x0E, 7)
#define B43_OFDMTAB_PWRDYN2		B43_OFDMTAB(0x0E, 12)
#define B43_OFDMTAB_LNAGAIN		B43_OFDMTAB(0x0E, 13)
#define B43_OFDMTAB_UNKNOWN_0F		B43_OFDMTAB(0x0F, 0)	//TODO rename
#define B43_OFDMTAB_UNKNOWN_APHY	B43_OFDMTAB(0x0F, 7)	//TODO rename
#define B43_OFDMTAB_LPFGAIN		B43_OFDMTAB(0x0F, 12)
#define B43_OFDMTAB_RSSI		B43_OFDMTAB(0x10, 0)
#define B43_OFDMTAB_UNKNOWN_11		B43_OFDMTAB(0x11, 4)	//TODO rename
#define B43_OFDMTAB_AGC1_R1		B43_OFDMTAB(0x13, 0)
#define B43_OFDMTAB_GAINX_R1		B43_OFDMTAB(0x14, 0)	//TODO remove!
#define B43_OFDMTAB_MINSIGSQ		B43_OFDMTAB(0x14, 0)
#define B43_OFDMTAB_AGC3_R1		B43_OFDMTAB(0x15, 0)
#define B43_OFDMTAB_WRSSI_R1		B43_OFDMTAB(0x15, 4)
#define B43_OFDMTAB_TSSI		B43_OFDMTAB(0x15, 0)
#define B43_OFDMTAB_DACRFPABB		B43_OFDMTAB(0x16, 0)
#define B43_OFDMTAB_DACOFF		B43_OFDMTAB(0x17, 0)
#define B43_OFDMTAB_DCBIAS		B43_OFDMTAB(0x18, 0)

u16 b43_ofdmtab_read16(struct b43_wldev *dev, u16 table, u16 offset);
void b43_ofdmtab_write16(struct b43_wldev *dev, u16 table,
			 u16 offset, u16 value);
u32 b43_ofdmtab_read32(struct b43_wldev *dev, u16 table, u16 offset);
void b43_ofdmtab_write32(struct b43_wldev *dev, u16 table,
			 u16 offset, u32 value);

/*** G-PHY table numbers */
#define B43_GTAB(number, offset)	(((number) << B43_PHY_GTABNR_SHIFT) | (offset))
#define B43_GTAB_NRSSI			B43_GTAB(0x00, 0)
#define B43_GTAB_TRFEMW			B43_GTAB(0x0C, 0x120)
#define B43_GTAB_ORIGTR			B43_GTAB(0x2E, 0x298)

u16 b43_gtab_read(struct b43_wldev *dev, u16 table, u16 offset);	//TODO implement
void b43_gtab_write(struct b43_wldev *dev, u16 table, u16 offset, u16 value);	//TODO implement

#define B43_DEFAULT_CHANNEL_A	36
#define B43_DEFAULT_CHANNEL_BG	6

enum {
	B43_ANTENNA0,		/* Antenna 0 */
	B43_ANTENNA1,		/* Antenna 0 */
	B43_ANTENNA_AUTO1,	/* Automatic, starting with antenna 1 */
	B43_ANTENNA_AUTO0,	/* Automatic, starting with antenna 0 */
	B43_ANTENNA2,
	B43_ANTENNA3 = 8,

	B43_ANTENNA_AUTO = B43_ANTENNA_AUTO0,
	B43_ANTENNA_DEFAULT = B43_ANTENNA_AUTO,
};

enum {
	B43_INTERFMODE_NONE,
	B43_INTERFMODE_NONWLAN,
	B43_INTERFMODE_MANUALWLAN,
	B43_INTERFMODE_AUTOWLAN,
};

/* Masks for the different PHY versioning registers. */
#define B43_PHYVER_ANALOG		0xF000
#define B43_PHYVER_ANALOG_SHIFT		12
#define B43_PHYVER_TYPE			0x0F00
#define B43_PHYVER_TYPE_SHIFT		8
#define B43_PHYVER_VERSION		0x00FF

void b43_phy_lock(struct b43_wldev *dev);
void b43_phy_unlock(struct b43_wldev *dev);


/* Read a value from a PHY register */
u16 b43_phy_read(struct b43_wldev *dev, u16 offset);
/* Write a value to a PHY register */
void b43_phy_write(struct b43_wldev *dev, u16 offset, u16 val);
/* Mask a PHY register with a mask */
void b43_phy_mask(struct b43_wldev *dev, u16 offset, u16 mask);
/* OR a PHY register with a bitmap */
void b43_phy_set(struct b43_wldev *dev, u16 offset, u16 set);
/* Mask and OR a PHY register with a mask and bitmap */
void b43_phy_maskset(struct b43_wldev *dev, u16 offset, u16 mask, u16 set);


int b43_phy_init_tssi2dbm_table(struct b43_wldev *dev);

void b43_phy_early_init(struct b43_wldev *dev);
int b43_phy_init(struct b43_wldev *dev);

void b43_set_rx_antenna(struct b43_wldev *dev, int antenna);

void b43_phy_xmitpower(struct b43_wldev *dev);

/* Returns the boolean whether the board has HardwarePowerControl */
bool b43_has_hardware_pctl(struct b43_phy *phy);
/* Returns the boolean whether "TX Magnification" is enabled. */
#define has_tx_magnification(phy) \
	(((phy)->rev >= 2) &&			\
	 ((phy)->radio_ver == 0x2050) &&	\
	 ((phy)->radio_rev == 8))
/* Card uses the loopback gain stuff */
#define has_loopback_gain(phy) \
	(((phy)->rev > 1) || ((phy)->gmode))

/* Radio Attenuation (RF Attenuation) */
struct b43_rfatt {
	u8 att;			/* Attenuation value */
	bool with_padmix;	/* Flag, PAD Mixer enabled. */
};
struct b43_rfatt_list {
	/* Attenuation values list */
	const struct b43_rfatt *list;
	u8 len;
	/* Minimum/Maximum attenuation values */
	u8 min_val;
	u8 max_val;
};

/* Returns true, if the values are the same. */
static inline bool b43_compare_rfatt(const struct b43_rfatt *a,
				     const struct b43_rfatt *b)
{
	return ((a->att == b->att) &&
		(a->with_padmix == b->with_padmix));
}

/* Baseband Attenuation */
struct b43_bbatt {
	u8 att;			/* Attenuation value */
};
struct b43_bbatt_list {
	/* Attenuation values list */
	const struct b43_bbatt *list;
	u8 len;
	/* Minimum/Maximum attenuation values */
	u8 min_val;
	u8 max_val;
};

/* Returns true, if the values are the same. */
static inline bool b43_compare_bbatt(const struct b43_bbatt *a,
				     const struct b43_bbatt *b)
{
	return (a->att == b->att);
}

/* tx_control bits. */
#define B43_TXCTL_PA3DB		0x40	/* PA Gain 3dB */
#define B43_TXCTL_PA2DB		0x20	/* PA Gain 2dB */
#define B43_TXCTL_TXMIX		0x10	/* TX Mixer Gain */

/* Write BasebandAttenuation value to the device. */
void b43_phy_set_baseband_attenuation(struct b43_wldev *dev,
				      u16 baseband_attenuation);

extern const u8 b43_radio_channel_codes_bg[];

void b43_radio_lock(struct b43_wldev *dev);
void b43_radio_unlock(struct b43_wldev *dev);


/* Read a value from a 16bit radio register */
u16 b43_radio_read16(struct b43_wldev *dev, u16 offset);
/* Write a value to a 16bit radio register */
void b43_radio_write16(struct b43_wldev *dev, u16 offset, u16 val);
/* Mask a 16bit radio register with a mask */
void b43_radio_mask(struct b43_wldev *dev, u16 offset, u16 mask);
/* OR a 16bit radio register with a bitmap */
void b43_radio_set(struct b43_wldev *dev, u16 offset, u16 set);
/* Mask and OR a PHY register with a mask and bitmap */
void b43_radio_maskset(struct b43_wldev *dev, u16 offset, u16 mask, u16 set);


u16 b43_radio_init2050(struct b43_wldev *dev);
void b43_radio_init2060(struct b43_wldev *dev);

void b43_radio_turn_on(struct b43_wldev *dev);
void b43_radio_turn_off(struct b43_wldev *dev, bool force);

int b43_radio_selectchannel(struct b43_wldev *dev, u8 channel,
			    int synthetic_pu_workaround);

u8 b43_radio_aci_detect(struct b43_wldev *dev, u8 channel);
u8 b43_radio_aci_scan(struct b43_wldev *dev);

int b43_radio_set_interference_mitigation(struct b43_wldev *dev, int mode);

void b43_calc_nrssi_slope(struct b43_wldev *dev);
void b43_calc_nrssi_threshold(struct b43_wldev *dev);
s16 b43_nrssi_hw_read(struct b43_wldev *dev, u16 offset);
void b43_nrssi_hw_write(struct b43_wldev *dev, u16 offset, s16 val);
void b43_nrssi_hw_update(struct b43_wldev *dev, u16 val);
void b43_nrssi_mem_update(struct b43_wldev *dev);

void b43_radio_set_tx_iq(struct b43_wldev *dev);
u16 b43_radio_calibrationvalue(struct b43_wldev *dev);

void b43_put_attenuation_into_ranges(struct b43_wldev *dev,
				     int *_bbatt, int *_rfatt);

void b43_set_txpower_g(struct b43_wldev *dev,
		       const struct b43_bbatt *bbatt,
		       const struct b43_rfatt *rfatt, u8 tx_control);

#endif /* B43_PHY_H_ */
