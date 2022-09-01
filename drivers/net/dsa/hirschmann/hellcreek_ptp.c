// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * DSA driver for:
 * Hirschmann Hellcreek TSN switch.
 *
 * Copyright (C) 2019,2020 Hochschule Offenburg
 * Copyright (C) 2019,2020 Linutronix GmbH
 * Authors: Kamil Alkhouri <kamil.alkhouri@hs-offenburg.de>
 *	    Kurt Kanzenbach <kurt@linutronix.de>
 */

#include <linux/ptp_clock_kernel.h>
#include "hellcreek.h"
#include "hellcreek_ptp.h"
#include "hellcreek_hwtstamp.h"

u16 hellcreek_ptp_read(struct hellcreek *hellcreek, unsigned int offset)
{
	return readw(hellcreek->ptp_base + offset);
}

void hellcreek_ptp_write(struct hellcreek *hellcreek, u16 data,
			 unsigned int offset)
{
	writew(data, hellcreek->ptp_base + offset);
}

/* Get nanoseconds from PTP clock */
static u64 hellcreek_ptp_clock_read(struct hellcreek *hellcreek)
{
	u16 nsl, nsh;

	/* Take a snapshot */
	hellcreek_ptp_write(hellcreek, PR_COMMAND_C_SS, PR_COMMAND_C);

	/* The time of the day is saved as 96 bits. However, due to hardware
	 * limitations the seconds are not or only partly kept in the PTP
	 * core. Currently only three bits for the seconds are available. That's
	 * why only the nanoseconds are used and the seconds are tracked in
	 * software. Anyway due to internal locking all five registers should be
	 * read.
	 */
	nsh = hellcreek_ptp_read(hellcreek, PR_SS_SYNC_DATA_C);
	nsh = hellcreek_ptp_read(hellcreek, PR_SS_SYNC_DATA_C);
	nsh = hellcreek_ptp_read(hellcreek, PR_SS_SYNC_DATA_C);
	nsh = hellcreek_ptp_read(hellcreek, PR_SS_SYNC_DATA_C);
	nsl = hellcreek_ptp_read(hellcreek, PR_SS_SYNC_DATA_C);

	return (u64)nsl | ((u64)nsh << 16);
}

static u64 __hellcreek_ptp_gettime(struct hellcreek *hellcreek)
{
	u64 ns;

	ns = hellcreek_ptp_clock_read(hellcreek);
	if (ns < hellcreek->last_ts)
		hellcreek->seconds++;
	hellcreek->last_ts = ns;
	ns += hellcreek->seconds * NSEC_PER_SEC;

	return ns;
}

/* Retrieve the seconds parts in nanoseconds for a packet timestamped with @ns.
 * There has to be a check whether an overflow occurred between the packet
 * arrival and now. If so use the correct seconds (-1) for calculating the
 * packet arrival time.
 */
u64 hellcreek_ptp_gettime_seconds(struct hellcreek *hellcreek, u64 ns)
{
	u64 s;

	__hellcreek_ptp_gettime(hellcreek);
	if (hellcreek->last_ts > ns)
		s = hellcreek->seconds * NSEC_PER_SEC;
	else
		s = (hellcreek->seconds - 1) * NSEC_PER_SEC;

	return s;
}

static int hellcreek_ptp_gettime(struct ptp_clock_info *ptp,
				 struct timespec64 *ts)
{
	struct hellcreek *hellcreek = ptp_to_hellcreek(ptp);
	u64 ns;

	mutex_lock(&hellcreek->ptp_lock);
	ns = __hellcreek_ptp_gettime(hellcreek);
	mutex_unlock(&hellcreek->ptp_lock);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int hellcreek_ptp_settime(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct hellcreek *hellcreek = ptp_to_hellcreek(ptp);
	u16 secl, nsh, nsl;

	secl = ts->tv_sec & 0xffff;
	nsh  = ((u32)ts->tv_nsec & 0xffff0000) >> 16;
	nsl  = ts->tv_nsec & 0xffff;

	mutex_lock(&hellcreek->ptp_lock);

	/* Update overflow data structure */
	hellcreek->seconds = ts->tv_sec;
	hellcreek->last_ts = ts->tv_nsec;

	/* Set time in clock */
	hellcreek_ptp_write(hellcreek, 0x00, PR_CLOCK_WRITE_C);
	hellcreek_ptp_write(hellcreek, 0x00, PR_CLOCK_WRITE_C);
	hellcreek_ptp_write(hellcreek, secl, PR_CLOCK_WRITE_C);
	hellcreek_ptp_write(hellcreek, nsh,  PR_CLOCK_WRITE_C);
	hellcreek_ptp_write(hellcreek, nsl,  PR_CLOCK_WRITE_C);

	mutex_unlock(&hellcreek->ptp_lock);

	return 0;
}

static int hellcreek_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct hellcreek *hellcreek = ptp_to_hellcreek(ptp);
	u16 negative = 0, addendh, addendl;
	u32 addend;
	u64 adj;

	if (scaled_ppm < 0) {
		negative = 1;
		scaled_ppm = -scaled_ppm;
	}

	/* IP-Core adjusts the nominal frequency by adding or subtracting 1 ns
	 * from the 8 ns (period of the oscillator) every time the accumulator
	 * register overflows. The value stored in the addend register is added
	 * to the accumulator register every 8 ns.
	 *
	 * addend value = (2^30 * accumulator_overflow_rate) /
	 *                oscillator_frequency
	 * where:
	 *
	 * oscillator_frequency = 125 MHz
	 * accumulator_overflow_rate = 125 MHz * scaled_ppm * 2^-16 * 10^-6 * 8
	 */
	adj = scaled_ppm;
	adj <<= 11;
	addend = (u32)div_u64(adj, 15625);

	addendh = (addend & 0xffff0000) >> 16;
	addendl = addend & 0xffff;

	negative = (negative << 15) & 0x8000;

	mutex_lock(&hellcreek->ptp_lock);

	/* Set drift register */
	hellcreek_ptp_write(hellcreek, negative, PR_CLOCK_DRIFT_C);
	hellcreek_ptp_write(hellcreek, 0x00, PR_CLOCK_DRIFT_C);
	hellcreek_ptp_write(hellcreek, 0x00, PR_CLOCK_DRIFT_C);
	hellcreek_ptp_write(hellcreek, addendh,  PR_CLOCK_DRIFT_C);
	hellcreek_ptp_write(hellcreek, addendl,  PR_CLOCK_DRIFT_C);

	mutex_unlock(&hellcreek->ptp_lock);

	return 0;
}

static int hellcreek_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct hellcreek *hellcreek = ptp_to_hellcreek(ptp);
	u16 negative = 0, counth, countl;
	u32 count_val;

	/* If the offset is larger than IP-Core slow offset resources. Don't
	 * consider slow adjustment. Rather, add the offset directly to the
	 * current time
	 */
	if (abs(delta) > MAX_SLOW_OFFSET_ADJ) {
		struct timespec64 now, then = ns_to_timespec64(delta);

		hellcreek_ptp_gettime(ptp, &now);
		now = timespec64_add(now, then);
		hellcreek_ptp_settime(ptp, &now);

		return 0;
	}

	if (delta < 0) {
		negative = 1;
		delta = -delta;
	}

	/* 'count_val' does not exceed the maximum register size (2^30) */
	count_val = div_s64(delta, MAX_NS_PER_STEP);

	counth = (count_val & 0xffff0000) >> 16;
	countl = count_val & 0xffff;

	negative = (negative << 15) & 0x8000;

	mutex_lock(&hellcreek->ptp_lock);

	/* Set offset write register */
	hellcreek_ptp_write(hellcreek, negative, PR_CLOCK_OFFSET_C);
	hellcreek_ptp_write(hellcreek, MAX_NS_PER_STEP, PR_CLOCK_OFFSET_C);
	hellcreek_ptp_write(hellcreek, MIN_CLK_CYCLES_BETWEEN_STEPS,
			    PR_CLOCK_OFFSET_C);
	hellcreek_ptp_write(hellcreek, countl,  PR_CLOCK_OFFSET_C);
	hellcreek_ptp_write(hellcreek, counth,  PR_CLOCK_OFFSET_C);

	mutex_unlock(&hellcreek->ptp_lock);

	return 0;
}

static int hellcreek_ptp_enable(struct ptp_clock_info *ptp,
				struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static void hellcreek_ptp_overflow_check(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct hellcreek *hellcreek;

	hellcreek = dw_overflow_to_hellcreek(dw);

	mutex_lock(&hellcreek->ptp_lock);
	__hellcreek_ptp_gettime(hellcreek);
	mutex_unlock(&hellcreek->ptp_lock);

	schedule_delayed_work(&hellcreek->overflow_work,
			      HELLCREEK_OVERFLOW_PERIOD);
}

static enum led_brightness hellcreek_get_brightness(struct hellcreek *hellcreek,
						    int led)
{
	return (hellcreek->status_out & led) ? 1 : 0;
}

static void hellcreek_set_brightness(struct hellcreek *hellcreek, int led,
				     enum led_brightness b)
{
	mutex_lock(&hellcreek->ptp_lock);

	if (b)
		hellcreek->status_out |= led;
	else
		hellcreek->status_out &= ~led;

	hellcreek_ptp_write(hellcreek, hellcreek->status_out, STATUS_OUT);

	mutex_unlock(&hellcreek->ptp_lock);
}

static void hellcreek_led_sync_good_set(struct led_classdev *ldev,
					enum led_brightness b)
{
	struct hellcreek *hellcreek = led_to_hellcreek(ldev, led_sync_good);

	hellcreek_set_brightness(hellcreek, STATUS_OUT_SYNC_GOOD, b);
}

static enum led_brightness hellcreek_led_sync_good_get(struct led_classdev *ldev)
{
	struct hellcreek *hellcreek = led_to_hellcreek(ldev, led_sync_good);

	return hellcreek_get_brightness(hellcreek, STATUS_OUT_SYNC_GOOD);
}

static void hellcreek_led_is_gm_set(struct led_classdev *ldev,
				    enum led_brightness b)
{
	struct hellcreek *hellcreek = led_to_hellcreek(ldev, led_is_gm);

	hellcreek_set_brightness(hellcreek, STATUS_OUT_IS_GM, b);
}

static enum led_brightness hellcreek_led_is_gm_get(struct led_classdev *ldev)
{
	struct hellcreek *hellcreek = led_to_hellcreek(ldev, led_is_gm);

	return hellcreek_get_brightness(hellcreek, STATUS_OUT_IS_GM);
}

/* There two available LEDs internally called sync_good and is_gm. However, the
 * user might want to use a different label and specify the default state. Take
 * those properties from device tree.
 */
static int hellcreek_led_setup(struct hellcreek *hellcreek)
{
	struct device_node *leds, *led = NULL;
	const char *label, *state;
	int ret = -EINVAL;

	of_node_get(hellcreek->dev->of_node);
	leds = of_find_node_by_name(hellcreek->dev->of_node, "leds");
	if (!leds) {
		dev_err(hellcreek->dev, "No LEDs specified in device tree!\n");
		return ret;
	}

	hellcreek->status_out = 0;

	led = of_get_next_available_child(leds, led);
	if (!led) {
		dev_err(hellcreek->dev, "First LED not specified!\n");
		goto out;
	}

	ret = of_property_read_string(led, "label", &label);
	hellcreek->led_sync_good.name = ret ? "sync_good" : label;

	ret = of_property_read_string(led, "default-state", &state);
	if (!ret) {
		if (!strcmp(state, "on"))
			hellcreek->led_sync_good.brightness = 1;
		else if (!strcmp(state, "off"))
			hellcreek->led_sync_good.brightness = 0;
		else if (!strcmp(state, "keep"))
			hellcreek->led_sync_good.brightness =
				hellcreek_get_brightness(hellcreek,
							 STATUS_OUT_SYNC_GOOD);
	}

	hellcreek->led_sync_good.max_brightness = 1;
	hellcreek->led_sync_good.brightness_set = hellcreek_led_sync_good_set;
	hellcreek->led_sync_good.brightness_get = hellcreek_led_sync_good_get;

	led = of_get_next_available_child(leds, led);
	if (!led) {
		dev_err(hellcreek->dev, "Second LED not specified!\n");
		ret = -EINVAL;
		goto out;
	}

	ret = of_property_read_string(led, "label", &label);
	hellcreek->led_is_gm.name = ret ? "is_gm" : label;

	ret = of_property_read_string(led, "default-state", &state);
	if (!ret) {
		if (!strcmp(state, "on"))
			hellcreek->led_is_gm.brightness = 1;
		else if (!strcmp(state, "off"))
			hellcreek->led_is_gm.brightness = 0;
		else if (!strcmp(state, "keep"))
			hellcreek->led_is_gm.brightness =
				hellcreek_get_brightness(hellcreek,
							 STATUS_OUT_IS_GM);
	}

	hellcreek->led_is_gm.max_brightness = 1;
	hellcreek->led_is_gm.brightness_set = hellcreek_led_is_gm_set;
	hellcreek->led_is_gm.brightness_get = hellcreek_led_is_gm_get;

	/* Set initial state */
	if (hellcreek->led_sync_good.brightness == 1)
		hellcreek_set_brightness(hellcreek, STATUS_OUT_SYNC_GOOD, 1);
	if (hellcreek->led_is_gm.brightness == 1)
		hellcreek_set_brightness(hellcreek, STATUS_OUT_IS_GM, 1);

	/* Register both leds */
	led_classdev_register(hellcreek->dev, &hellcreek->led_sync_good);
	led_classdev_register(hellcreek->dev, &hellcreek->led_is_gm);

	ret = 0;

out:
	of_node_put(leds);

	return ret;
}

int hellcreek_ptp_setup(struct hellcreek *hellcreek)
{
	u16 status;
	int ret;

	/* Set up the overflow work */
	INIT_DELAYED_WORK(&hellcreek->overflow_work,
			  hellcreek_ptp_overflow_check);

	/* Setup PTP clock */
	hellcreek->ptp_clock_info.owner = THIS_MODULE;
	snprintf(hellcreek->ptp_clock_info.name,
		 sizeof(hellcreek->ptp_clock_info.name),
		 dev_name(hellcreek->dev));

	/* IP-Core can add up to 0.5 ns per 8 ns cycle, which means
	 * accumulator_overflow_rate shall not exceed 62.5 MHz (which adjusts
	 * the nominal frequency by 6.25%)
	 */
	hellcreek->ptp_clock_info.max_adj     = 62500000;
	hellcreek->ptp_clock_info.n_alarm     = 0;
	hellcreek->ptp_clock_info.n_pins      = 0;
	hellcreek->ptp_clock_info.n_ext_ts    = 0;
	hellcreek->ptp_clock_info.n_per_out   = 0;
	hellcreek->ptp_clock_info.pps	      = 0;
	hellcreek->ptp_clock_info.adjfine     = hellcreek_ptp_adjfine;
	hellcreek->ptp_clock_info.adjtime     = hellcreek_ptp_adjtime;
	hellcreek->ptp_clock_info.gettime64   = hellcreek_ptp_gettime;
	hellcreek->ptp_clock_info.settime64   = hellcreek_ptp_settime;
	hellcreek->ptp_clock_info.enable      = hellcreek_ptp_enable;
	hellcreek->ptp_clock_info.do_aux_work = hellcreek_hwtstamp_work;

	hellcreek->ptp_clock = ptp_clock_register(&hellcreek->ptp_clock_info,
						  hellcreek->dev);
	if (IS_ERR(hellcreek->ptp_clock))
		return PTR_ERR(hellcreek->ptp_clock);

	/* Enable the offset correction process, if no offset correction is
	 * already taking place
	 */
	status = hellcreek_ptp_read(hellcreek, PR_CLOCK_STATUS_C);
	if (!(status & PR_CLOCK_STATUS_C_OFS_ACT))
		hellcreek_ptp_write(hellcreek,
				    status | PR_CLOCK_STATUS_C_ENA_OFS,
				    PR_CLOCK_STATUS_C);

	/* Enable the drift correction process */
	hellcreek_ptp_write(hellcreek, status | PR_CLOCK_STATUS_C_ENA_DRIFT,
			    PR_CLOCK_STATUS_C);

	/* LED setup */
	ret = hellcreek_led_setup(hellcreek);
	if (ret) {
		if (hellcreek->ptp_clock)
			ptp_clock_unregister(hellcreek->ptp_clock);
		return ret;
	}

	schedule_delayed_work(&hellcreek->overflow_work,
			      HELLCREEK_OVERFLOW_PERIOD);

	return 0;
}

void hellcreek_ptp_free(struct hellcreek *hellcreek)
{
	led_classdev_unregister(&hellcreek->led_is_gm);
	led_classdev_unregister(&hellcreek->led_sync_good);
	cancel_delayed_work_sync(&hellcreek->overflow_work);
	if (hellcreek->ptp_clock)
		ptp_clock_unregister(hellcreek->ptp_clock);
	hellcreek->ptp_clock = NULL;
}
