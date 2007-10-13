#ifndef B43_LO_H_
#define B43_LO_H_

#include "phy.h"

struct b43_wldev;

/* Local Oscillator control value-pair. */
struct b43_loctl {
	/* Control values. */
	s8 i;
	s8 q;
	/* "Used by hardware" flag. */
	bool used;
#ifdef CONFIG_B43_DEBUG
	/* Is this lo-control-array entry calibrated? */
	bool calibrated;
#endif
};

/* Debugging: Poison value for i and q values. */
#define B43_LOCTL_POISON	111

/* loctl->calibrated debugging mechanism */
#ifdef CONFIG_B43_DEBUG
static inline void b43_loctl_set_calibrated(struct b43_loctl *loctl,
					    bool calibrated)
{
	loctl->calibrated = calibrated;
}
static inline bool b43_loctl_is_calibrated(struct b43_loctl *loctl)
{
	return loctl->calibrated;
}
#else
static inline void b43_loctl_set_calibrated(struct b43_loctl *loctl,
					    bool calibrated)
{
}
static inline bool b43_loctl_is_calibrated(struct b43_loctl *loctl)
{
	return 1;
}
#endif

/* TX Power LO Control Array.
 * Value-pairs to adjust the LocalOscillator are stored
 * in this structure.
 * There are two different set of values. One for "Flag is Set"
 * and one for "Flag is Unset".
 * By "Flag" the flag in struct b43_rfatt is meant.
 * The Value arrays are two-dimensional. The first index
 * is the baseband attenuation and the second index
 * is the radio attenuation.
 * Use b43_get_lo_g_ctl() to retrieve a value from the lists.
 */
struct b43_txpower_lo_control {
#define B43_NR_BB	12
#define B43_NR_RF	16
	/* LO Control values, with PAD Mixer */
	struct b43_loctl with_padmix[B43_NR_BB][B43_NR_RF];
	/* LO Control values, without PAD Mixer */
	struct b43_loctl no_padmix[B43_NR_BB][B43_NR_RF];

	/* Flag to indicate a complete rebuild of the two tables above
	 * to the LO measuring code. */
	bool rebuild;

	/* Lists of valid RF and BB attenuation values for this device. */
	struct b43_rfatt_list rfatt_list;
	struct b43_bbatt_list bbatt_list;

	/* Current TX Bias value */
	u8 tx_bias;
	/* Current TX Magnification Value (if used by the device) */
	u8 tx_magn;

	/* GPHY LO is measured. */
	bool lo_measured;

	/* Saved device PowerVector */
	u64 power_vector;
};

/* Measure the BPHY Local Oscillator. */
void b43_lo_b_measure(struct b43_wldev *dev);
/* Measure the BPHY/GPHY Local Oscillator. */
void b43_lo_g_measure(struct b43_wldev *dev);

/* Adjust the Local Oscillator to the saved attenuation
 * and txctl values.
 */
void b43_lo_g_adjust(struct b43_wldev *dev);
/* Adjust to specific values. */
void b43_lo_g_adjust_to(struct b43_wldev *dev,
			u16 rfatt, u16 bbatt, u16 tx_control);

/* Mark all possible b43_lo_g_ctl as "unused" */
void b43_lo_g_ctl_mark_all_unused(struct b43_wldev *dev);
/* Mark the b43_lo_g_ctl corresponding to the current
 * attenuation values as used.
 */
void b43_lo_g_ctl_mark_cur_used(struct b43_wldev *dev);

/* Get a reference to a LO Control value pair in the
 * TX Power LO Control Array.
 */
struct b43_loctl *b43_get_lo_g_ctl(struct b43_wldev *dev,
				   const struct b43_rfatt *rfatt,
				   const struct b43_bbatt *bbatt);

#endif /* B43_LO_H_ */
