#ifndef LINUX_B43_PHY_G_H_
#define LINUX_B43_PHY_G_H_

/* OFDM PHY registers are defined in the A-PHY header. */
#include "phy_a.h"

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


/*** G-PHY table numbers */
#define B43_GTAB(number, offset)	(((number) << B43_PHY_GTABNR_SHIFT) | (offset))
#define B43_GTAB_NRSSI			B43_GTAB(0x00, 0)
#define B43_GTAB_TRFEMW			B43_GTAB(0x0C, 0x120)
#define B43_GTAB_ORIGTR			B43_GTAB(0x2E, 0x298)

u16 b43_gtab_read(struct b43_wldev *dev, u16 table, u16 offset);
void b43_gtab_write(struct b43_wldev *dev, u16 table, u16 offset, u16 value);


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

struct b43_txpower_lo_control;

struct b43_phy_g {
	bool initialised;

	/* ACI (adjacent channel interference) flags. */
	bool aci_enable;
	bool aci_wlan_automatic;
	bool aci_hw_rssi;

	/* Radio switched on/off */
	bool radio_on;
	struct {
		/* Values saved when turning the radio off.
		 * They are needed when turning it on again. */
		bool valid;
		u16 rfover;
		u16 rfoverval;
	} radio_off_context;

	u16 minlowsig[2];
	u16 minlowsigpos[2];

	/* Pointer to the table used to convert a
	 * TSSI value to dBm-Q5.2 */
	const s8 *tssi2dbm;
	/* tssi2dbm is kmalloc()ed. Only used for free()ing. */
	bool dyn_tssi_tbl;
	/* Target idle TSSI */
	int tgt_idle_tssi;
	/* Current idle TSSI */
	int cur_idle_tssi;
	/* The current average TSSI.
	 * Needs irq_lock, as it's updated in the IRQ path. */
	u8 average_tssi;
	/* Current TX power level attenuation control values */
	struct b43_bbatt bbatt;
	struct b43_rfatt rfatt;
	u8 tx_control;		/* B43_TXCTL_XXX */
	/* The calculated attenuation deltas that are used later
	 * when adjusting the actual power output. */
	int bbatt_delta;
	int rfatt_delta;

	/* LocalOscillator control values. */
	struct b43_txpower_lo_control *lo_control;
	/* Values from b43_calc_loopback_gain() */
	s16 max_lb_gain;	/* Maximum Loopback gain in hdB */
	s16 trsw_rx_gain;	/* TRSW RX gain in hdB */
	s16 lna_lod_gain;	/* LNA lod */
	s16 lna_gain;		/* LNA */
	s16 pga_gain;		/* PGA */

	/* Current Interference Mitigation mode */
	int interfmode;
	/* Stack of saved values from the Interference Mitigation code.
	 * Each value in the stack is layed out as follows:
	 * bit 0-11:  offset
	 * bit 12-15: register ID
	 * bit 16-32: value
	 * register ID is: 0x1 PHY, 0x2 Radio, 0x3 ILT
	 */
#define B43_INTERFSTACK_SIZE	26
	u32 interfstack[B43_INTERFSTACK_SIZE];	//FIXME: use a data structure

	/* Saved values from the NRSSI Slope calculation */
	s16 nrssi[2];
	s32 nrssislope;
	/* In memory nrssi lookup table. */
	s8 nrssi_lt[64];

	u16 lofcal;

	u16 initval;		//FIXME rename?

	/* The device does address auto increment for the OFDM tables.
	 * We cache the previously used address here and omit the address
	 * write on the next table access, if possible. */
	u16 ofdmtab_addr; /* The address currently set in hardware. */
	enum { /* The last data flow direction. */
		B43_OFDMTAB_DIRECTION_UNKNOWN = 0,
		B43_OFDMTAB_DIRECTION_READ,
		B43_OFDMTAB_DIRECTION_WRITE,
	} ofdmtab_addr_direction;
};

void b43_gphy_set_baseband_attenuation(struct b43_wldev *dev,
				       u16 baseband_attenuation);
void b43_gphy_channel_switch(struct b43_wldev *dev,
			     unsigned int channel,
			     bool synthetic_pu_workaround);

struct b43_phy_operations;
extern const struct b43_phy_operations b43_phyops_g;

#endif /* LINUX_B43_PHY_G_H_ */
