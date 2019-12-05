// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 1999 - 2018 Intel Corporation. */

/* PTP 1588 Hardware Clock (PHC)
 * Derived from PTP Hardware Clock driver for Intel 82576 and 82580 (igb)
 * Copyright (C) 2011 Richard Cochran <richardcochran@gmail.com>
 */

#include "e1000.h"

#ifdef CONFIG_E1000E_HWTS
#include <linux/clocksource.h>
#include <linux/ktime.h>
#include <asm/tsc.h>
#endif

/**
 * e1000e_phc_adjfreq - adjust the frequency of the hardware clock
 * @ptp: ptp clock structure
 * @delta: Desired frequency change in parts per billion
 *
 * Adjust the frequency of the PHC cycle counter by the indicated delta from
 * the base frequency.
 **/
static int e1000e_phc_adjfreq(struct ptp_clock_info *ptp, s32 delta)
{
	struct e1000_adapter *adapter = container_of(ptp, struct e1000_adapter,
						     ptp_clock_info);
	struct e1000_hw *hw = &adapter->hw;
	bool neg_adj = false;
	unsigned long flags;
	u64 adjustment;
	u32 timinca, incvalue;
	s32 ret_val;

	if ((delta > ptp->max_adj) || (delta <= -1000000000))
		return -EINVAL;

	if (delta < 0) {
		neg_adj = true;
		delta = -delta;
	}

	/* Get the System Time Register SYSTIM base frequency */
	ret_val = e1000e_get_base_timinca(adapter, &timinca);
	if (ret_val)
		return ret_val;

	spin_lock_irqsave(&adapter->systim_lock, flags);

	incvalue = timinca & E1000_TIMINCA_INCVALUE_MASK;

	adjustment = incvalue;
	adjustment *= delta;
	adjustment = div_u64(adjustment, 1000000000);

	incvalue = neg_adj ? (incvalue - adjustment) : (incvalue + adjustment);

	timinca &= ~E1000_TIMINCA_INCVALUE_MASK;
	timinca |= incvalue;

	ew32(TIMINCA, timinca);

	adapter->ptp_delta = delta;

	spin_unlock_irqrestore(&adapter->systim_lock, flags);

	return 0;
}

/**
 * e1000e_phc_adjtime - Shift the time of the hardware clock
 * @ptp: ptp clock structure
 * @delta: Desired change in nanoseconds
 *
 * Adjust the timer by resetting the timecounter structure.
 **/
static int e1000e_phc_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct e1000_adapter *adapter = container_of(ptp, struct e1000_adapter,
						     ptp_clock_info);
	unsigned long flags;

	spin_lock_irqsave(&adapter->systim_lock, flags);
	timecounter_adjtime(&adapter->tc, delta);
	spin_unlock_irqrestore(&adapter->systim_lock, flags);

	return 0;
}

#ifdef CONFIG_E1000E_HWTS
#define MAX_HW_WAIT_COUNT (3)

/**
 * e1000e_phc_get_syncdevicetime - Callback given to timekeeping code reads system/device registers
 * @device: current device time
 * @system: system counter value read synchronously with device time
 * @ctx: context provided by timekeeping code
 *
 * Read device and system (ART) clock simultaneously and return the corrected
 * clock values in ns.
 **/
static int e1000e_phc_get_syncdevicetime(ktime_t *device,
					 struct system_counterval_t *system,
					 void *ctx)
{
	struct e1000_adapter *adapter = (struct e1000_adapter *)ctx;
	struct e1000_hw *hw = &adapter->hw;
	unsigned long flags;
	int i;
	u32 tsync_ctrl;
	u64 dev_cycles;
	u64 sys_cycles;

	tsync_ctrl = er32(TSYNCTXCTL);
	tsync_ctrl |= E1000_TSYNCTXCTL_START_SYNC |
		E1000_TSYNCTXCTL_MAX_ALLOWED_DLY_MASK;
	ew32(TSYNCTXCTL, tsync_ctrl);
	for (i = 0; i < MAX_HW_WAIT_COUNT; ++i) {
		udelay(1);
		tsync_ctrl = er32(TSYNCTXCTL);
		if (tsync_ctrl & E1000_TSYNCTXCTL_SYNC_COMP)
			break;
	}

	if (i == MAX_HW_WAIT_COUNT)
		return -ETIMEDOUT;

	dev_cycles = er32(SYSSTMPH);
	dev_cycles <<= 32;
	dev_cycles |= er32(SYSSTMPL);
	spin_lock_irqsave(&adapter->systim_lock, flags);
	*device = ns_to_ktime(timecounter_cyc2time(&adapter->tc, dev_cycles));
	spin_unlock_irqrestore(&adapter->systim_lock, flags);

	sys_cycles = er32(PLTSTMPH);
	sys_cycles <<= 32;
	sys_cycles |= er32(PLTSTMPL);
	*system = convert_art_to_tsc(sys_cycles);

	return 0;
}

/**
 * e1000e_phc_getsynctime - Reads the current system/device cross timestamp
 * @ptp: ptp clock structure
 * @cts: structure containing timestamp
 *
 * Read device and system (ART) clock simultaneously and return the scaled
 * clock values in ns.
 **/
static int e1000e_phc_getcrosststamp(struct ptp_clock_info *ptp,
				     struct system_device_crosststamp *xtstamp)
{
	struct e1000_adapter *adapter = container_of(ptp, struct e1000_adapter,
						     ptp_clock_info);

	return get_device_system_crosststamp(e1000e_phc_get_syncdevicetime,
						adapter, NULL, xtstamp);
}
#endif/*CONFIG_E1000E_HWTS*/

/**
 * e1000e_phc_gettimex - Reads the current time from the hardware clock and
 *                       system clock
 * @ptp: ptp clock structure
 * @ts: timespec structure to hold the current PHC time
 * @sts: structure to hold the current system time
 *
 * Read the timecounter and return the correct value in ns after converting
 * it into a struct timespec.
 **/
static int e1000e_phc_gettimex(struct ptp_clock_info *ptp,
			       struct timespec64 *ts,
			       struct ptp_system_timestamp *sts)
{
	struct e1000_adapter *adapter = container_of(ptp, struct e1000_adapter,
						     ptp_clock_info);
	unsigned long flags;
	u64 cycles, ns;

	spin_lock_irqsave(&adapter->systim_lock, flags);

	/* NOTE: Non-monotonic SYSTIM readings may be returned */
	cycles = e1000e_read_systim(adapter, sts);
	ns = timecounter_cyc2time(&adapter->tc, cycles);

	spin_unlock_irqrestore(&adapter->systim_lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

/**
 * e1000e_phc_settime - Set the current time on the hardware clock
 * @ptp: ptp clock structure
 * @ts: timespec containing the new time for the cycle counter
 *
 * Reset the timecounter to use a new base value instead of the kernel
 * wall timer value.
 **/
static int e1000e_phc_settime(struct ptp_clock_info *ptp,
			      const struct timespec64 *ts)
{
	struct e1000_adapter *adapter = container_of(ptp, struct e1000_adapter,
						     ptp_clock_info);
	unsigned long flags;
	u64 ns;

	ns = timespec64_to_ns(ts);

	/* reset the timecounter */
	spin_lock_irqsave(&adapter->systim_lock, flags);
	timecounter_init(&adapter->tc, &adapter->cc, ns);
	spin_unlock_irqrestore(&adapter->systim_lock, flags);

	return 0;
}

/**
 * e1000e_phc_enable - enable or disable an ancillary feature
 * @ptp: ptp clock structure
 * @request: Desired resource to enable or disable
 * @on: Caller passes one to enable or zero to disable
 *
 * Enable (or disable) ancillary features of the PHC subsystem.
 * Currently, no ancillary features are supported.
 **/
static int e1000e_phc_enable(struct ptp_clock_info __always_unused *ptp,
			     struct ptp_clock_request __always_unused *request,
			     int __always_unused on)
{
	return -EOPNOTSUPP;
}

static void e1000e_systim_overflow_work(struct work_struct *work)
{
	struct e1000_adapter *adapter = container_of(work, struct e1000_adapter,
						     systim_overflow_work.work);
	struct e1000_hw *hw = &adapter->hw;
	struct timespec64 ts;
	u64 ns;

	/* Update the timecounter */
	ns = timecounter_read(&adapter->tc);

	ts = ns_to_timespec64(ns);
	e_dbg("SYSTIM overflow check at %lld.%09lu\n",
	      (long long) ts.tv_sec, ts.tv_nsec);

	schedule_delayed_work(&adapter->systim_overflow_work,
			      E1000_SYSTIM_OVERFLOW_PERIOD);
}

static const struct ptp_clock_info e1000e_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.adjfreq	= e1000e_phc_adjfreq,
	.adjtime	= e1000e_phc_adjtime,
	.gettimex64	= e1000e_phc_gettimex,
	.settime64	= e1000e_phc_settime,
	.enable		= e1000e_phc_enable,
};

/**
 * e1000e_ptp_init - initialize PTP for devices which support it
 * @adapter: board private structure
 *
 * This function performs the required steps for enabling PTP support.
 * If PTP support has already been loaded it simply calls the cyclecounter
 * init routine and exits.
 **/
void e1000e_ptp_init(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	adapter->ptp_clock = NULL;

	if (!(adapter->flags & FLAG_HAS_HW_TIMESTAMP))
		return;

	adapter->ptp_clock_info = e1000e_ptp_clock_info;

	snprintf(adapter->ptp_clock_info.name,
		 sizeof(adapter->ptp_clock_info.name), "%pm",
		 adapter->netdev->perm_addr);

	switch (hw->mac.type) {
	case e1000_pch2lan:
	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
		/* fall-through */
	case e1000_pch_tgp:
		if ((hw->mac.type < e1000_pch_lpt) ||
		    (er32(TSYNCRXCTL) & E1000_TSYNCRXCTL_SYSCFI)) {
			adapter->ptp_clock_info.max_adj = 24000000 - 1;
			break;
		}
		/* fall-through */
	case e1000_82574:
	case e1000_82583:
		adapter->ptp_clock_info.max_adj = 600000000 - 1;
		break;
	default:
		break;
	}

#ifdef CONFIG_E1000E_HWTS
	/* CPU must have ART and GBe must be from Sunrise Point or greater */
	if (hw->mac.type >= e1000_pch_spt && boot_cpu_has(X86_FEATURE_ART))
		adapter->ptp_clock_info.getcrosststamp =
			e1000e_phc_getcrosststamp;
#endif/*CONFIG_E1000E_HWTS*/

	INIT_DELAYED_WORK(&adapter->systim_overflow_work,
			  e1000e_systim_overflow_work);

	schedule_delayed_work(&adapter->systim_overflow_work,
			      E1000_SYSTIM_OVERFLOW_PERIOD);

	adapter->ptp_clock = ptp_clock_register(&adapter->ptp_clock_info,
						&adapter->pdev->dev);
	if (IS_ERR(adapter->ptp_clock)) {
		adapter->ptp_clock = NULL;
		e_err("ptp_clock_register failed\n");
	} else if (adapter->ptp_clock) {
		e_info("registered PHC clock\n");
	}
}

/**
 * e1000e_ptp_remove - disable PTP device and stop the overflow check
 * @adapter: board private structure
 *
 * Stop the PTP support, and cancel the delayed work.
 **/
void e1000e_ptp_remove(struct e1000_adapter *adapter)
{
	if (!(adapter->flags & FLAG_HAS_HW_TIMESTAMP))
		return;

	cancel_delayed_work_sync(&adapter->systim_overflow_work);

	if (adapter->ptp_clock) {
		ptp_clock_unregister(adapter->ptp_clock);
		adapter->ptp_clock = NULL;
		e_info("removed PHC\n");
	}
}
