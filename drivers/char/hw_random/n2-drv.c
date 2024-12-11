// SPDX-License-Identifier: GPL-2.0-only
/* n2-drv.c: Niagara-2 RNG driver.
 *
 * Copyright (C) 2008, 2011 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/preempt.h>
#include <linux/hw_random.h>

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#include <asm/hypervisor.h>

#include "n2rng.h"

#define DRV_MODULE_NAME		"n2rng"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"0.3"
#define DRV_MODULE_RELDATE	"Jan 7, 2017"

static char version[] =
	DRV_MODULE_NAME " v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("David S. Miller <davem@davemloft.net>");
MODULE_DESCRIPTION("Niagara2 RNG driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

/* The Niagara2 RNG provides a 64-bit read-only random number
 * register, plus a control register.  Access to the RNG is
 * virtualized through the hypervisor so that both guests and control
 * nodes can access the device.
 *
 * The entropy source consists of raw entropy sources, each
 * constructed from a voltage controlled oscillator whose phase is
 * jittered by thermal noise sources.
 *
 * The oscillator in each of the three raw entropy sources run at
 * different frequencies.  Normally, all three generator outputs are
 * gathered, xored together, and fed into a CRC circuit, the output of
 * which is the 64-bit read-only register.
 *
 * Some time is necessary for all the necessary entropy to build up
 * such that a full 64-bits of entropy are available in the register.
 * In normal operating mode (RNG_CTL_LFSR is set), the chip implements
 * an interlock which blocks register reads until sufficient entropy
 * is available.
 *
 * A control register is provided for adjusting various aspects of RNG
 * operation, and to enable diagnostic modes.  Each of the three raw
 * entropy sources has an enable bit (RNG_CTL_ES{1,2,3}).  Also
 * provided are fields for controlling the minimum time in cycles
 * between read accesses to the register (RNG_CTL_WAIT, this controls
 * the interlock described in the previous paragraph).
 *
 * The standard setting is to have the mode bit (RNG_CTL_LFSR) set,
 * all three entropy sources enabled, and the interlock time set
 * appropriately.
 *
 * The CRC polynomial used by the chip is:
 *
 * P(X) = x64 + x61 + x57 + x56 + x52 + x51 + x50 + x48 + x47 + x46 +
 *        x43 + x42 + x41 + x39 + x38 + x37 + x35 + x32 + x28 + x25 +
 *        x22 + x21 + x17 + x15 + x13 + x12 + x11 + x7 + x5 + x + 1
 *
 * The RNG_CTL_VCO value of each noise cell must be programmed
 * separately.  This is why 4 control register values must be provided
 * to the hypervisor.  During a write, the hypervisor writes them all,
 * one at a time, to the actual RNG_CTL register.  The first three
 * values are used to setup the desired RNG_CTL_VCO for each entropy
 * source, for example:
 *
 *	control 0: (1 << RNG_CTL_VCO_SHIFT) | RNG_CTL_ES1
 *	control 1: (2 << RNG_CTL_VCO_SHIFT) | RNG_CTL_ES2
 *	control 2: (3 << RNG_CTL_VCO_SHIFT) | RNG_CTL_ES3
 *
 * And then the fourth value sets the final chip state and enables
 * desired.
 */

static int n2rng_hv_err_trans(unsigned long hv_err)
{
	switch (hv_err) {
	case HV_EOK:
		return 0;
	case HV_EWOULDBLOCK:
		return -EAGAIN;
	case HV_ENOACCESS:
		return -EPERM;
	case HV_EIO:
		return -EIO;
	case HV_EBUSY:
		return -EBUSY;
	case HV_EBADALIGN:
	case HV_ENORADDR:
		return -EFAULT;
	default:
		return -EINVAL;
	}
}

static unsigned long n2rng_generic_read_control_v2(unsigned long ra,
						   unsigned long unit)
{
	unsigned long hv_err, state, ticks, watchdog_delta, watchdog_status;
	int block = 0, busy = 0;

	while (1) {
		hv_err = sun4v_rng_ctl_read_v2(ra, unit, &state,
					       &ticks,
					       &watchdog_delta,
					       &watchdog_status);
		if (hv_err == HV_EOK)
			break;

		if (hv_err == HV_EBUSY) {
			if (++busy >= N2RNG_BUSY_LIMIT)
				break;

			udelay(1);
		} else if (hv_err == HV_EWOULDBLOCK) {
			if (++block >= N2RNG_BLOCK_LIMIT)
				break;

			__delay(ticks);
		} else
			break;
	}

	return hv_err;
}

/* In multi-socket situations, the hypervisor might need to
 * queue up the RNG control register write if it's for a unit
 * that is on a cpu socket other than the one we are executing on.
 *
 * We poll here waiting for a successful read of that control
 * register to make sure the write has been actually performed.
 */
static unsigned long n2rng_control_settle_v2(struct n2rng *np, int unit)
{
	unsigned long ra = __pa(&np->scratch_control[0]);

	return n2rng_generic_read_control_v2(ra, unit);
}

static unsigned long n2rng_write_ctl_one(struct n2rng *np, int unit,
					 unsigned long state,
					 unsigned long control_ra,
					 unsigned long watchdog_timeout,
					 unsigned long *ticks)
{
	unsigned long hv_err;

	if (np->hvapi_major == 1) {
		hv_err = sun4v_rng_ctl_write_v1(control_ra, state,
						watchdog_timeout, ticks);
	} else {
		hv_err = sun4v_rng_ctl_write_v2(control_ra, state,
						watchdog_timeout, unit);
		if (hv_err == HV_EOK)
			hv_err = n2rng_control_settle_v2(np, unit);
		*ticks = N2RNG_ACCUM_CYCLES_DEFAULT;
	}

	return hv_err;
}

static int n2rng_generic_read_data(unsigned long data_ra)
{
	unsigned long ticks, hv_err;
	int block = 0, hcheck = 0;

	while (1) {
		hv_err = sun4v_rng_data_read(data_ra, &ticks);
		if (hv_err == HV_EOK)
			return 0;

		if (hv_err == HV_EWOULDBLOCK) {
			if (++block >= N2RNG_BLOCK_LIMIT)
				return -EWOULDBLOCK;
			__delay(ticks);
		} else if (hv_err == HV_ENOACCESS) {
			return -EPERM;
		} else if (hv_err == HV_EIO) {
			if (++hcheck >= N2RNG_HCHECK_LIMIT)
				return -EIO;
			udelay(10000);
		} else
			return -ENODEV;
	}
}

static unsigned long n2rng_read_diag_data_one(struct n2rng *np,
					      unsigned long unit,
					      unsigned long data_ra,
					      unsigned long data_len,
					      unsigned long *ticks)
{
	unsigned long hv_err;

	if (np->hvapi_major == 1) {
		hv_err = sun4v_rng_data_read_diag_v1(data_ra, data_len, ticks);
	} else {
		hv_err = sun4v_rng_data_read_diag_v2(data_ra, data_len,
						     unit, ticks);
		if (!*ticks)
			*ticks = N2RNG_ACCUM_CYCLES_DEFAULT;
	}
	return hv_err;
}

static int n2rng_generic_read_diag_data(struct n2rng *np,
					unsigned long unit,
					unsigned long data_ra,
					unsigned long data_len)
{
	unsigned long ticks, hv_err;
	int block = 0;

	while (1) {
		hv_err = n2rng_read_diag_data_one(np, unit,
						  data_ra, data_len,
						  &ticks);
		if (hv_err == HV_EOK)
			return 0;

		if (hv_err == HV_EWOULDBLOCK) {
			if (++block >= N2RNG_BLOCK_LIMIT)
				return -EWOULDBLOCK;
			__delay(ticks);
		} else if (hv_err == HV_ENOACCESS) {
			return -EPERM;
		} else if (hv_err == HV_EIO) {
			return -EIO;
		} else
			return -ENODEV;
	}
}


static int n2rng_generic_write_control(struct n2rng *np,
				       unsigned long control_ra,
				       unsigned long unit,
				       unsigned long state)
{
	unsigned long hv_err, ticks;
	int block = 0, busy = 0;

	while (1) {
		hv_err = n2rng_write_ctl_one(np, unit, state, control_ra,
					     np->wd_timeo, &ticks);
		if (hv_err == HV_EOK)
			return 0;

		if (hv_err == HV_EWOULDBLOCK) {
			if (++block >= N2RNG_BLOCK_LIMIT)
				return -EWOULDBLOCK;
			__delay(ticks);
		} else if (hv_err == HV_EBUSY) {
			if (++busy >= N2RNG_BUSY_LIMIT)
				return -EBUSY;
			udelay(1);
		} else
			return -ENODEV;
	}
}

/* Just try to see if we can successfully access the control register
 * of the RNG on the domain on which we are currently executing.
 */
static int n2rng_try_read_ctl(struct n2rng *np)
{
	unsigned long hv_err;
	unsigned long x;

	if (np->hvapi_major == 1) {
		hv_err = sun4v_rng_get_diag_ctl();
	} else {
		/* We purposefully give invalid arguments, HV_NOACCESS
		 * is higher priority than the errors we'd get from
		 * these other cases, and that's the error we are
		 * truly interested in.
		 */
		hv_err = sun4v_rng_ctl_read_v2(0UL, ~0UL, &x, &x, &x, &x);
		switch (hv_err) {
		case HV_EWOULDBLOCK:
		case HV_ENOACCESS:
			break;
		default:
			hv_err = HV_EOK;
			break;
		}
	}

	return n2rng_hv_err_trans(hv_err);
}

static u64 n2rng_control_default(struct n2rng *np, int ctl)
{
	u64 val = 0;

	if (np->data->chip_version == 1) {
		val = ((2 << RNG_v1_CTL_ASEL_SHIFT) |
			(N2RNG_ACCUM_CYCLES_DEFAULT << RNG_v1_CTL_WAIT_SHIFT) |
			 RNG_CTL_LFSR);

		switch (ctl) {
		case 0:
			val |= (1 << RNG_v1_CTL_VCO_SHIFT) | RNG_CTL_ES1;
			break;
		case 1:
			val |= (2 << RNG_v1_CTL_VCO_SHIFT) | RNG_CTL_ES2;
			break;
		case 2:
			val |= (3 << RNG_v1_CTL_VCO_SHIFT) | RNG_CTL_ES3;
			break;
		case 3:
			val |= RNG_CTL_ES1 | RNG_CTL_ES2 | RNG_CTL_ES3;
			break;
		default:
			break;
		}

	} else {
		val = ((2 << RNG_v2_CTL_ASEL_SHIFT) |
			(N2RNG_ACCUM_CYCLES_DEFAULT << RNG_v2_CTL_WAIT_SHIFT) |
			 RNG_CTL_LFSR);

		switch (ctl) {
		case 0:
			val |= (1 << RNG_v2_CTL_VCO_SHIFT) | RNG_CTL_ES1;
			break;
		case 1:
			val |= (2 << RNG_v2_CTL_VCO_SHIFT) | RNG_CTL_ES2;
			break;
		case 2:
			val |= (3 << RNG_v2_CTL_VCO_SHIFT) | RNG_CTL_ES3;
			break;
		case 3:
			val |= RNG_CTL_ES1 | RNG_CTL_ES2 | RNG_CTL_ES3;
			break;
		default:
			break;
		}
	}

	return val;
}

static void n2rng_control_swstate_init(struct n2rng *np)
{
	int i;

	np->flags |= N2RNG_FLAG_CONTROL;

	np->health_check_sec = N2RNG_HEALTH_CHECK_SEC_DEFAULT;
	np->accum_cycles = N2RNG_ACCUM_CYCLES_DEFAULT;
	np->wd_timeo = N2RNG_WD_TIMEO_DEFAULT;

	for (i = 0; i < np->num_units; i++) {
		struct n2rng_unit *up = &np->units[i];

		up->control[0] = n2rng_control_default(np, 0);
		up->control[1] = n2rng_control_default(np, 1);
		up->control[2] = n2rng_control_default(np, 2);
		up->control[3] = n2rng_control_default(np, 3);
	}

	np->hv_state = HV_RNG_STATE_UNCONFIGURED;
}

static int n2rng_grab_diag_control(struct n2rng *np)
{
	int i, busy_count, err = -ENODEV;

	busy_count = 0;
	for (i = 0; i < 100; i++) {
		err = n2rng_try_read_ctl(np);
		if (err != -EAGAIN)
			break;

		if (++busy_count > 100) {
			dev_err(&np->op->dev,
				"Grab diag control timeout.\n");
			return -ENODEV;
		}

		udelay(1);
	}

	return err;
}

static int n2rng_init_control(struct n2rng *np)
{
	int err = n2rng_grab_diag_control(np);

	/* Not in the control domain, that's OK we are only a consumer
	 * of the RNG data, we don't setup and program it.
	 */
	if (err == -EPERM)
		return 0;
	if (err)
		return err;

	n2rng_control_swstate_init(np);

	return 0;
}

static int n2rng_data_read(struct hwrng *rng, u32 *data)
{
	struct n2rng *np = (struct n2rng *) rng->priv;
	unsigned long ra = __pa(&np->test_data);
	int len;

	if (!(np->flags & N2RNG_FLAG_READY)) {
		len = 0;
	} else if (np->flags & N2RNG_FLAG_BUFFER_VALID) {
		np->flags &= ~N2RNG_FLAG_BUFFER_VALID;
		*data = np->buffer;
		len = 4;
	} else {
		int err = n2rng_generic_read_data(ra);
		if (!err) {
			np->flags |= N2RNG_FLAG_BUFFER_VALID;
			np->buffer = np->test_data >> 32;
			*data = np->test_data & 0xffffffff;
			len = 4;
		} else {
			dev_err(&np->op->dev, "RNG error, retesting\n");
			np->flags &= ~N2RNG_FLAG_READY;
			if (!(np->flags & N2RNG_FLAG_SHUTDOWN))
				schedule_delayed_work(&np->work, 0);
			len = 0;
		}
	}

	return len;
}

/* On a guest node, just make sure we can read random data properly.
 * If a control node reboots or reloads it's n2rng driver, this won't
 * work during that time.  So we have to keep probing until the device
 * becomes usable.
 */
static int n2rng_guest_check(struct n2rng *np)
{
	unsigned long ra = __pa(&np->test_data);

	return n2rng_generic_read_data(ra);
}

static int n2rng_entropy_diag_read(struct n2rng *np, unsigned long unit,
				   u64 *pre_control, u64 pre_state,
				   u64 *buffer, unsigned long buf_len,
				   u64 *post_control, u64 post_state)
{
	unsigned long post_ctl_ra = __pa(post_control);
	unsigned long pre_ctl_ra = __pa(pre_control);
	unsigned long buffer_ra = __pa(buffer);
	int err;

	err = n2rng_generic_write_control(np, pre_ctl_ra, unit, pre_state);
	if (err)
		return err;

	err = n2rng_generic_read_diag_data(np, unit,
					   buffer_ra, buf_len);

	(void) n2rng_generic_write_control(np, post_ctl_ra, unit,
					   post_state);

	return err;
}

static u64 advance_polynomial(u64 poly, u64 val, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		int highbit_set = ((s64)val < 0);

		val <<= 1;
		if (highbit_set)
			val ^= poly;
	}

	return val;
}

static int n2rng_test_buffer_find(struct n2rng *np, u64 val)
{
	int i, count = 0;

	/* Purposefully skip over the first word.  */
	for (i = 1; i < SELFTEST_BUFFER_WORDS; i++) {
		if (np->test_buffer[i] == val)
			count++;
	}
	return count;
}

static void n2rng_dump_test_buffer(struct n2rng *np)
{
	int i;

	for (i = 0; i < SELFTEST_BUFFER_WORDS; i++)
		dev_err(&np->op->dev, "Test buffer slot %d [0x%016llx]\n",
			i, np->test_buffer[i]);
}

static int n2rng_check_selftest_buffer(struct n2rng *np, unsigned long unit)
{
	u64 val;
	int err, matches, limit;

	switch (np->data->id) {
	case N2_n2_rng:
	case N2_vf_rng:
	case N2_kt_rng:
	case N2_m4_rng:  /* yes, m4 uses the old value */
		val = RNG_v1_SELFTEST_VAL;
		break;
	default:
		val = RNG_v2_SELFTEST_VAL;
		break;
	}

	matches = 0;
	for (limit = 0; limit < SELFTEST_LOOPS_MAX; limit++) {
		matches += n2rng_test_buffer_find(np, val);
		if (matches >= SELFTEST_MATCH_GOAL)
			break;
		val = advance_polynomial(SELFTEST_POLY, val, 1);
	}

	err = 0;
	if (limit >= SELFTEST_LOOPS_MAX) {
		err = -ENODEV;
		dev_err(&np->op->dev, "Selftest failed on unit %lu\n", unit);
		n2rng_dump_test_buffer(np);
	} else
		dev_info(&np->op->dev, "Selftest passed on unit %lu\n", unit);

	return err;
}

static int n2rng_control_selftest(struct n2rng *np, unsigned long unit)
{
	int err;
	u64 base, base3;

	switch (np->data->id) {
	case N2_n2_rng:
	case N2_vf_rng:
	case N2_kt_rng:
		base = RNG_v1_CTL_ASEL_NOOUT << RNG_v1_CTL_ASEL_SHIFT;
		base3 = base | RNG_CTL_LFSR |
			((RNG_v1_SELFTEST_TICKS - 2) << RNG_v1_CTL_WAIT_SHIFT);
		break;
	case N2_m4_rng:
		base = RNG_v2_CTL_ASEL_NOOUT << RNG_v2_CTL_ASEL_SHIFT;
		base3 = base | RNG_CTL_LFSR |
			((RNG_v1_SELFTEST_TICKS - 2) << RNG_v2_CTL_WAIT_SHIFT);
		break;
	default:
		base = RNG_v2_CTL_ASEL_NOOUT << RNG_v2_CTL_ASEL_SHIFT;
		base3 = base | RNG_CTL_LFSR |
			(RNG_v2_SELFTEST_TICKS << RNG_v2_CTL_WAIT_SHIFT);
		break;
	}

	np->test_control[0] = base;
	np->test_control[1] = base;
	np->test_control[2] = base;
	np->test_control[3] = base3;

	err = n2rng_entropy_diag_read(np, unit, np->test_control,
				      HV_RNG_STATE_HEALTHCHECK,
				      np->test_buffer,
				      sizeof(np->test_buffer),
				      &np->units[unit].control[0],
				      np->hv_state);
	if (err)
		return err;

	return n2rng_check_selftest_buffer(np, unit);
}

static int n2rng_control_check(struct n2rng *np)
{
	int i;

	for (i = 0; i < np->num_units; i++) {
		int err = n2rng_control_selftest(np, i);
		if (err)
			return err;
	}
	return 0;
}

/* The sanity checks passed, install the final configuration into the
 * chip, it's ready to use.
 */
static int n2rng_control_configure_units(struct n2rng *np)
{
	int unit, err;

	err = 0;
	for (unit = 0; unit < np->num_units; unit++) {
		struct n2rng_unit *up = &np->units[unit];
		unsigned long ctl_ra = __pa(&up->control[0]);
		int esrc;
		u64 base, shift;

		if (np->data->chip_version == 1) {
			base = ((np->accum_cycles << RNG_v1_CTL_WAIT_SHIFT) |
			      (RNG_v1_CTL_ASEL_NOOUT << RNG_v1_CTL_ASEL_SHIFT) |
			      RNG_CTL_LFSR);
			shift = RNG_v1_CTL_VCO_SHIFT;
		} else {
			base = ((np->accum_cycles << RNG_v2_CTL_WAIT_SHIFT) |
			      (RNG_v2_CTL_ASEL_NOOUT << RNG_v2_CTL_ASEL_SHIFT) |
			      RNG_CTL_LFSR);
			shift = RNG_v2_CTL_VCO_SHIFT;
		}

		/* XXX This isn't the best.  We should fetch a bunch
		 * XXX of words using each entropy source combined XXX
		 * with each VCO setting, and see which combinations
		 * XXX give the best random data.
		 */
		for (esrc = 0; esrc < 3; esrc++)
			up->control[esrc] = base |
				(esrc << shift) |
				(RNG_CTL_ES1 << esrc);

		up->control[3] = base |
			(RNG_CTL_ES1 | RNG_CTL_ES2 | RNG_CTL_ES3);

		err = n2rng_generic_write_control(np, ctl_ra, unit,
						  HV_RNG_STATE_CONFIGURED);
		if (err)
			break;
	}

	return err;
}

static void n2rng_work(struct work_struct *work)
{
	struct n2rng *np = container_of(work, struct n2rng, work.work);
	int err = 0;
	static int retries = 4;

	if (!(np->flags & N2RNG_FLAG_CONTROL)) {
		err = n2rng_guest_check(np);
	} else {
		preempt_disable();
		err = n2rng_control_check(np);
		preempt_enable();

		if (!err)
			err = n2rng_control_configure_units(np);
	}

	if (!err) {
		np->flags |= N2RNG_FLAG_READY;
		dev_info(&np->op->dev, "RNG ready\n");
	}

	if (--retries == 0)
		dev_err(&np->op->dev, "Self-test retries failed, RNG not ready\n");
	else if (err && !(np->flags & N2RNG_FLAG_SHUTDOWN))
		schedule_delayed_work(&np->work, HZ * 2);
}

static void n2rng_driver_version(void)
{
	static int n2rng_version_printed;

	if (n2rng_version_printed++ == 0)
		pr_info("%s", version);
}

static const struct of_device_id n2rng_match[];
static int n2rng_probe(struct platform_device *op)
{
	int err = -ENOMEM;
	struct n2rng *np;

	n2rng_driver_version();
	np = devm_kzalloc(&op->dev, sizeof(*np), GFP_KERNEL);
	if (!np)
		goto out;
	np->op = op;
	np->data = (struct n2rng_template *)device_get_match_data(&op->dev);

	INIT_DELAYED_WORK(&np->work, n2rng_work);

	if (np->data->multi_capable)
		np->flags |= N2RNG_FLAG_MULTI;

	err = -ENODEV;
	np->hvapi_major = 2;
	if (sun4v_hvapi_register(HV_GRP_RNG,
				 np->hvapi_major,
				 &np->hvapi_minor)) {
		np->hvapi_major = 1;
		if (sun4v_hvapi_register(HV_GRP_RNG,
					 np->hvapi_major,
					 &np->hvapi_minor)) {
			dev_err(&op->dev, "Cannot register suitable "
				"HVAPI version.\n");
			goto out;
		}
	}

	if (np->flags & N2RNG_FLAG_MULTI) {
		if (np->hvapi_major < 2) {
			dev_err(&op->dev, "multi-unit-capable RNG requires "
				"HVAPI major version 2 or later, got %lu\n",
				np->hvapi_major);
			goto out_hvapi_unregister;
		}
		np->num_units = of_getintprop_default(op->dev.of_node,
						      "rng-#units", 0);
		if (!np->num_units) {
			dev_err(&op->dev, "VF RNG lacks rng-#units property\n");
			goto out_hvapi_unregister;
		}
	} else {
		np->num_units = 1;
	}

	dev_info(&op->dev, "Registered RNG HVAPI major %lu minor %lu\n",
		 np->hvapi_major, np->hvapi_minor);
	np->units = devm_kcalloc(&op->dev, np->num_units, sizeof(*np->units),
				 GFP_KERNEL);
	err = -ENOMEM;
	if (!np->units)
		goto out_hvapi_unregister;

	err = n2rng_init_control(np);
	if (err)
		goto out_hvapi_unregister;

	dev_info(&op->dev, "Found %s RNG, units: %d\n",
		 ((np->flags & N2RNG_FLAG_MULTI) ?
		  "multi-unit-capable" : "single-unit"),
		 np->num_units);

	np->hwrng.name = DRV_MODULE_NAME;
	np->hwrng.data_read = n2rng_data_read;
	np->hwrng.priv = (unsigned long) np;

	err = devm_hwrng_register(&op->dev, &np->hwrng);
	if (err)
		goto out_hvapi_unregister;

	platform_set_drvdata(op, np);

	schedule_delayed_work(&np->work, 0);

	return 0;

out_hvapi_unregister:
	sun4v_hvapi_unregister(HV_GRP_RNG);

out:
	return err;
}

static void n2rng_remove(struct platform_device *op)
{
	struct n2rng *np = platform_get_drvdata(op);

	np->flags |= N2RNG_FLAG_SHUTDOWN;

	cancel_delayed_work_sync(&np->work);

	sun4v_hvapi_unregister(HV_GRP_RNG);
}

static struct n2rng_template n2_template = {
	.id = N2_n2_rng,
	.multi_capable = 0,
	.chip_version = 1,
};

static struct n2rng_template vf_template = {
	.id = N2_vf_rng,
	.multi_capable = 1,
	.chip_version = 1,
};

static struct n2rng_template kt_template = {
	.id = N2_kt_rng,
	.multi_capable = 1,
	.chip_version = 1,
};

static struct n2rng_template m4_template = {
	.id = N2_m4_rng,
	.multi_capable = 1,
	.chip_version = 2,
};

static struct n2rng_template m7_template = {
	.id = N2_m7_rng,
	.multi_capable = 1,
	.chip_version = 2,
};

static const struct of_device_id n2rng_match[] = {
	{
		.name		= "random-number-generator",
		.compatible	= "SUNW,n2-rng",
		.data		= &n2_template,
	},
	{
		.name		= "random-number-generator",
		.compatible	= "SUNW,vf-rng",
		.data		= &vf_template,
	},
	{
		.name		= "random-number-generator",
		.compatible	= "SUNW,kt-rng",
		.data		= &kt_template,
	},
	{
		.name		= "random-number-generator",
		.compatible	= "ORCL,m4-rng",
		.data		= &m4_template,
	},
	{
		.name		= "random-number-generator",
		.compatible	= "ORCL,m7-rng",
		.data		= &m7_template,
	},
	{},
};
MODULE_DEVICE_TABLE(of, n2rng_match);

static struct platform_driver n2rng_driver = {
	.driver = {
		.name = "n2rng",
		.of_match_table = n2rng_match,
	},
	.probe		= n2rng_probe,
	.remove		= n2rng_remove,
};

module_platform_driver(n2rng_driver);
