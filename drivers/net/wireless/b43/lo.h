#ifndef B43_LO_H_
#define B43_LO_H_

#include "phy.h"

struct b43_wldev;

/* Local Oscillator control value-pair. */
struct b43_loctl {
	/* Control values. */
	s8 i;
	s8 q;
};
/* Debugging: Poison value for i and q values. */
#define B43_LOCTL_POISON	111

/* This struct holds calibrated LO settings for a set of
 * Baseband and RF attenuation settings. */
struct b43_lo_calib {
	/* The set of attenuation values this set of LO
	 * control values is calibrated for. */
	struct b43_bbatt bbatt;
	struct b43_rfatt rfatt;
	/* The set of control values for the LO. */
	struct b43_loctl ctl;
	/* The time when these settings were calibrated (in jiffies) */
	unsigned long calib_time;
	/* List. */
	struct list_head list;
};

/* Size of the DC Lookup Table in 16bit words. */
#define B43_DC_LT_SIZE		32

/* Local Oscillator calibration information */
struct b43_txpower_lo_control {
	/* Lists of RF and BB attenuation values for this device.
	 * Used for building hardware power control tables. */
	struct b43_rfatt_list rfatt_list;
	struct b43_bbatt_list bbatt_list;

	/* The DC Lookup Table is cached in memory here.
	 * Note that this is only used for Hardware Power Control. */
	u16 dc_lt[B43_DC_LT_SIZE];

	/* List of calibrated control values (struct b43_lo_calib). */
	struct list_head calib_list;
	/* Last time the power vector was read (jiffies). */
	unsigned long pwr_vec_read_time;
	/* Last time the txctl values were measured (jiffies). */
	unsigned long txctl_measured_time;

	/* Current TX Bias value */
	u8 tx_bias;
	/* Current TX Magnification Value (if used by the device) */
	u8 tx_magn;

	/* Saved device PowerVector */
	u64 power_vector;
};

/* Calibration expire timeouts.
 * Timeouts must be multiple of 15 seconds. To make sure
 * the item really expired when the 15 second timer hits, we
 * subtract two additional seconds from the timeout. */
#define B43_LO_CALIB_EXPIRE	(HZ * (30 - 2))
#define B43_LO_PWRVEC_EXPIRE	(HZ * (30 - 2))
#define B43_LO_TXCTL_EXPIRE	(HZ * (180 - 4))


/* Adjust the Local Oscillator to the saved attenuation
 * and txctl values.
 */
void b43_lo_g_adjust(struct b43_wldev *dev);
/* Adjust to specific values. */
void b43_lo_g_adjust_to(struct b43_wldev *dev,
			u16 rfatt, u16 bbatt, u16 tx_control);

void b43_gphy_dc_lt_init(struct b43_wldev *dev, bool update_all);

void b43_lo_g_maintanance_work(struct b43_wldev *dev);
void b43_lo_g_cleanup(struct b43_wldev *dev);
void b43_lo_g_init(struct b43_wldev *dev);

#endif /* B43_LO_H_ */
