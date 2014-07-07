#ifndef LINUX_B43_PHY_COMMON_H_
#define LINUX_B43_PHY_COMMON_H_

#include <linux/types.h>
#include <linux/nl80211.h>

struct b43_wldev;

/* Complex number using 2 32-bit signed integers */
struct b43_c32 { s32 i, q; };

#define CORDIC_CONVERT(value)	(((value) >= 0) ? \
				 ((((value) >> 15) + 1) >> 1) : \
				 -((((-(value)) >> 15) + 1) >> 1))

/* PHY register routing bits */
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


/* Masks for the PHY versioning registers. */
#define B43_PHYVER_ANALOG		0xF000
#define B43_PHYVER_ANALOG_SHIFT		12
#define B43_PHYVER_TYPE			0x0F00
#define B43_PHYVER_TYPE_SHIFT		8
#define B43_PHYVER_VERSION		0x00FF

/* PHY writes need to be flushed if we reach limit */
#define B43_MAX_WRITES_IN_ROW		24

/**
 * enum b43_interference_mitigation - Interference Mitigation mode
 *
 * @B43_INTERFMODE_NONE:	Disabled
 * @B43_INTERFMODE_NONWLAN:	Non-WLAN Interference Mitigation
 * @B43_INTERFMODE_MANUALWLAN:	WLAN Interference Mitigation
 * @B43_INTERFMODE_AUTOWLAN:	Automatic WLAN Interference Mitigation
 */
enum b43_interference_mitigation {
	B43_INTERFMODE_NONE,
	B43_INTERFMODE_NONWLAN,
	B43_INTERFMODE_MANUALWLAN,
	B43_INTERFMODE_AUTOWLAN,
};

/* Antenna identifiers */
enum {
	B43_ANTENNA0 = 0,	/* Antenna 0 */
	B43_ANTENNA1 = 1,	/* Antenna 1 */
	B43_ANTENNA_AUTO0 = 2,	/* Automatic, starting with antenna 0 */
	B43_ANTENNA_AUTO1 = 3,	/* Automatic, starting with antenna 1 */
	B43_ANTENNA2 = 4,
	B43_ANTENNA3 = 8,

	B43_ANTENNA_AUTO = B43_ANTENNA_AUTO0,
	B43_ANTENNA_DEFAULT = B43_ANTENNA_AUTO,
};

/**
 * enum b43_txpwr_result - Return value for the recalc_txpower PHY op.
 *
 * @B43_TXPWR_RES_NEED_ADJUST:	Values changed. Hardware adjustment is needed.
 * @B43_TXPWR_RES_DONE:		No more work to do. Everything is done.
 */
enum b43_txpwr_result {
	B43_TXPWR_RES_NEED_ADJUST,
	B43_TXPWR_RES_DONE,
};

/**
 * struct b43_phy_operations - Function pointers for PHY ops.
 *
 * @allocate:		Allocate and initialise the PHY data structures.
 * 			Must not be NULL.
 * @free:		Destroy and free the PHY data structures.
 * 			Must not be NULL.
 *
 * @prepare_structs:	Prepare the PHY data structures.
 * 			The data structures allocated in @allocate are
 * 			initialized here.
 * 			Must not be NULL.
 * @prepare_hardware:	Prepare the PHY. This is called before b43_chip_init to
 * 			do some early early PHY hardware init.
 * 			Can be NULL, if not required.
 * @init:		Initialize the PHY.
 * 			Must not be NULL.
 * @exit:		Shutdown the PHY.
 * 			Can be NULL, if not required.
 *
 * @phy_read:		Read from a PHY register.
 * 			Must not be NULL.
 * @phy_write:		Write to a PHY register.
 * 			Must not be NULL.
 * @phy_maskset:	Maskset a PHY register, taking shortcuts.
 *			If it is NULL, a generic algorithm is used.
 * @radio_read:		Read from a Radio register.
 * 			Must not be NULL.
 * @radio_write:	Write to a Radio register.
 * 			Must not be NULL.
 *
 * @supports_hwpctl:	Returns a boolean whether Hardware Power Control
 * 			is supported or not.
 * 			If NULL, hwpctl is assumed to be never supported.
 * @software_rfkill:	Turn the radio ON or OFF.
 * 			Possible state values are
 * 			RFKILL_STATE_SOFT_BLOCKED or
 * 			RFKILL_STATE_UNBLOCKED
 * 			Must not be NULL.
 * @switch_analog:	Turn the Analog on/off.
 * 			Must not be NULL.
 * @switch_channel:	Switch the radio to another channel.
 * 			Must not be NULL.
 * @get_default_chan:	Just returns the default channel number.
 * 			Must not be NULL.
 * @set_rx_antenna:	Set the antenna used for RX.
 * 			Can be NULL, if not supported.
 * @interf_mitigation:	Switch the Interference Mitigation mode.
 * 			Can be NULL, if not supported.
 *
 * @recalc_txpower:	Recalculate the transmission power parameters.
 * 			This callback has to recalculate the TX power settings,
 * 			but does not need to write them to the hardware, yet.
 * 			Returns enum b43_txpwr_result to indicate whether the hardware
 * 			needs to be adjusted.
 * 			If B43_TXPWR_NEED_ADJUST is returned, @adjust_txpower
 * 			will be called later.
 * 			If the parameter "ignore_tssi" is true, the TSSI values should
 * 			be ignored and a recalculation of the power settings should be
 * 			done even if the TSSI values did not change.
 * 			This function may sleep, but should not.
 * 			Must not be NULL.
 * @adjust_txpower:	Write the previously calculated TX power settings
 * 			(from @recalc_txpower) to the hardware.
 * 			This function may sleep.
 * 			Can be NULL, if (and ONLY if) @recalc_txpower _always_
 * 			returns B43_TXPWR_RES_DONE.
 *
 * @pwork_15sec:	Periodic work. Called every 15 seconds.
 * 			Can be NULL, if not required.
 * @pwork_60sec:	Periodic work. Called every 60 seconds.
 * 			Can be NULL, if not required.
 */
struct b43_phy_operations {
	/* Initialisation */
	int (*allocate)(struct b43_wldev *dev);
	void (*free)(struct b43_wldev *dev);
	void (*prepare_structs)(struct b43_wldev *dev);
	int (*prepare_hardware)(struct b43_wldev *dev);
	int (*init)(struct b43_wldev *dev);
	void (*exit)(struct b43_wldev *dev);

	/* Register access */
	u16 (*phy_read)(struct b43_wldev *dev, u16 reg);
	void (*phy_write)(struct b43_wldev *dev, u16 reg, u16 value);
	void (*phy_maskset)(struct b43_wldev *dev, u16 reg, u16 mask, u16 set);
	u16 (*radio_read)(struct b43_wldev *dev, u16 reg);
	void (*radio_write)(struct b43_wldev *dev, u16 reg, u16 value);

	/* Radio */
	bool (*supports_hwpctl)(struct b43_wldev *dev);
	void (*software_rfkill)(struct b43_wldev *dev, bool blocked);
	void (*switch_analog)(struct b43_wldev *dev, bool on);
	int (*switch_channel)(struct b43_wldev *dev, unsigned int new_channel);
	unsigned int (*get_default_chan)(struct b43_wldev *dev);
	void (*set_rx_antenna)(struct b43_wldev *dev, int antenna);
	int (*interf_mitigation)(struct b43_wldev *dev,
				 enum b43_interference_mitigation new_mode);

	/* Transmission power adjustment */
	enum b43_txpwr_result (*recalc_txpower)(struct b43_wldev *dev,
						bool ignore_tssi);
	void (*adjust_txpower)(struct b43_wldev *dev);

	/* Misc */
	void (*pwork_15sec)(struct b43_wldev *dev);
	void (*pwork_60sec)(struct b43_wldev *dev);
};

struct b43_phy_a;
struct b43_phy_g;
struct b43_phy_n;
struct b43_phy_lp;
struct b43_phy_ht;
struct b43_phy_lcn;

struct b43_phy {
	/* Hardware operation callbacks. */
	const struct b43_phy_operations *ops;

	/* Most hardware context information is stored in the standard-
	 * specific data structures pointed to by the pointers below.
	 * Only one of them is valid (the currently enabled PHY). */
#ifdef CONFIG_B43_DEBUG
	/* No union for debug build to force NULL derefs in buggy code. */
	struct {
#else
	union {
#endif
		/* A-PHY specific information */
		struct b43_phy_a *a;
		/* G-PHY specific information */
		struct b43_phy_g *g;
		/* N-PHY specific information */
		struct b43_phy_n *n;
		/* LP-PHY specific information */
		struct b43_phy_lp *lp;
		/* HT-PHY specific information */
		struct b43_phy_ht *ht;
		/* LCN-PHY specific information */
		struct b43_phy_lcn *lcn;
	};

	/* Band support flags. */
	bool supports_2ghz;
	bool supports_5ghz;

	/* HT info */
	bool is_40mhz;

	/* Is GMODE (2 GHz mode) bit enabled? */
	bool gmode;

	/* After power reset full init has to be performed */
	bool do_full_init;

	/* Analog Type */
	u8 analog;
	/* B43_PHYTYPE_ */
	u8 type;
	/* PHY revision number. */
	u8 rev;

	/* Count writes since last read */
	u8 writes_counter;

	/* Radio versioning */
	u16 radio_manuf;	/* Radio manufacturer */
	u16 radio_ver;		/* Radio version */
	u8 radio_rev;		/* Radio revision */

	/* Software state of the radio */
	bool radio_on;

	/* Desired TX power level (in dBm).
	 * This is set by the user and adjusted in b43_phy_xmitpower(). */
	int desired_txpower;

	/* Hardware Power Control enabled? */
	bool hardware_power_control;

	/* The time (in absolute jiffies) when the next TX power output
	 * check is needed. */
	unsigned long next_txpwr_check_time;

	/* Current channel */
	unsigned int channel;
	u16 channel_freq;
	enum nl80211_channel_type channel_type;

	/* PHY TX errors counter. */
	atomic_t txerr_cnt;

#ifdef CONFIG_B43_DEBUG
	/* PHY registers locked (w.r.t. firmware) */
	bool phy_locked;
	/* Radio registers locked (w.r.t. firmware) */
	bool radio_locked;
#endif /* B43_DEBUG */
};


/**
 * b43_phy_allocate - Allocate PHY structs
 * Allocate the PHY data structures, based on the current dev->phy.type
 */
int b43_phy_allocate(struct b43_wldev *dev);

/**
 * b43_phy_free - Free PHY structs
 */
void b43_phy_free(struct b43_wldev *dev);

/**
 * b43_phy_init - Initialise the PHY
 */
int b43_phy_init(struct b43_wldev *dev);

/**
 * b43_phy_exit - Cleanup PHY
 */
void b43_phy_exit(struct b43_wldev *dev);

/**
 * b43_has_hardware_pctl - Hardware Power Control supported?
 * Returns a boolean, whether hardware power control is supported.
 */
bool b43_has_hardware_pctl(struct b43_wldev *dev);

/**
 * b43_phy_read - 16bit PHY register read access
 */
u16 b43_phy_read(struct b43_wldev *dev, u16 reg);

/**
 * b43_phy_write - 16bit PHY register write access
 */
void b43_phy_write(struct b43_wldev *dev, u16 reg, u16 value);

/**
 * b43_phy_copy - copy contents of 16bit PHY register to another
 */
void b43_phy_copy(struct b43_wldev *dev, u16 destreg, u16 srcreg);

/**
 * b43_phy_mask - Mask a PHY register with a mask
 */
void b43_phy_mask(struct b43_wldev *dev, u16 offset, u16 mask);

/**
 * b43_phy_set - OR a PHY register with a bitmap
 */
void b43_phy_set(struct b43_wldev *dev, u16 offset, u16 set);

/**
 * b43_phy_maskset - Mask and OR a PHY register with a mask and bitmap
 */
void b43_phy_maskset(struct b43_wldev *dev, u16 offset, u16 mask, u16 set);

/**
 * b43_radio_read - 16bit Radio register read access
 */
u16 b43_radio_read(struct b43_wldev *dev, u16 reg);
#define b43_radio_read16	b43_radio_read /* DEPRECATED */

/**
 * b43_radio_write - 16bit Radio register write access
 */
void b43_radio_write(struct b43_wldev *dev, u16 reg, u16 value);
#define b43_radio_write16	b43_radio_write /* DEPRECATED */

/**
 * b43_radio_mask - Mask a 16bit radio register with a mask
 */
void b43_radio_mask(struct b43_wldev *dev, u16 offset, u16 mask);

/**
 * b43_radio_set - OR a 16bit radio register with a bitmap
 */
void b43_radio_set(struct b43_wldev *dev, u16 offset, u16 set);

/**
 * b43_radio_maskset - Mask and OR a radio register with a mask and bitmap
 */
void b43_radio_maskset(struct b43_wldev *dev, u16 offset, u16 mask, u16 set);

/**
 * b43_radio_wait_value - Waits for a given value in masked register read
 */
bool b43_radio_wait_value(struct b43_wldev *dev, u16 offset, u16 mask,
			  u16 value, int delay, int timeout);

/**
 * b43_radio_lock - Lock firmware radio register access
 */
void b43_radio_lock(struct b43_wldev *dev);

/**
 * b43_radio_unlock - Unlock firmware radio register access
 */
void b43_radio_unlock(struct b43_wldev *dev);

/**
 * b43_phy_lock - Lock firmware PHY register access
 */
void b43_phy_lock(struct b43_wldev *dev);

/**
 * b43_phy_unlock - Unlock firmware PHY register access
 */
void b43_phy_unlock(struct b43_wldev *dev);

void b43_phy_put_into_reset(struct b43_wldev *dev);
void b43_phy_take_out_of_reset(struct b43_wldev *dev);

/**
 * b43_switch_channel - Switch to another channel
 */
int b43_switch_channel(struct b43_wldev *dev, unsigned int new_channel);
/**
 * B43_DEFAULT_CHANNEL - Switch to the default channel.
 */
#define B43_DEFAULT_CHANNEL	UINT_MAX

/**
 * b43_software_rfkill - Turn the radio ON or OFF in software.
 */
void b43_software_rfkill(struct b43_wldev *dev, bool blocked);

/**
 * b43_phy_txpower_check - Check TX power output.
 *
 * Compare the current TX power output to the desired power emission
 * and schedule an adjustment in case it mismatches.
 *
 * @flags:	OR'ed enum b43_phy_txpower_check_flags flags.
 * 		See the docs below.
 */
void b43_phy_txpower_check(struct b43_wldev *dev, unsigned int flags);
/**
 * enum b43_phy_txpower_check_flags - Flags for b43_phy_txpower_check()
 *
 * @B43_TXPWR_IGNORE_TIME: Ignore the schedule time and force-redo
 *                         the check now.
 * @B43_TXPWR_IGNORE_TSSI: Redo the recalculation, even if the average
 *                         TSSI did not change.
 */
enum b43_phy_txpower_check_flags {
	B43_TXPWR_IGNORE_TIME		= (1 << 0),
	B43_TXPWR_IGNORE_TSSI		= (1 << 1),
};

struct work_struct;
void b43_phy_txpower_adjust_work(struct work_struct *work);

/**
 * b43_phy_shm_tssi_read - Read the average of the last 4 TSSI from SHM.
 *
 * @shm_offset:		The SHM address to read the values from.
 *
 * Returns the average of the 4 TSSI values, or a negative error code.
 */
int b43_phy_shm_tssi_read(struct b43_wldev *dev, u16 shm_offset);

/**
 * b43_phy_switch_analog_generic - Generic PHY operation for switching the Analog.
 *
 * It does the switching based on the PHY0 core register.
 * Do _not_ call this directly. Only use it as a switch_analog callback
 * for struct b43_phy_operations.
 */
void b43_phyop_switch_analog_generic(struct b43_wldev *dev, bool on);

bool b43_channel_type_is_40mhz(enum nl80211_channel_type channel_type);

void b43_phy_force_clock(struct b43_wldev *dev, bool force);

struct b43_c32 b43_cordic(int theta);

#endif /* LINUX_B43_PHY_COMMON_H_ */
