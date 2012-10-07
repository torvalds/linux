/*
 * omap-pm-noop.c - OMAP power management interface - dummy version
 *
 * This code implements the OMAP power management interface to
 * drivers, CPUIdle, CPUFreq, and DSP Bridge.  It is strictly for
 * debug/demonstration use, as it does nothing but printk() whenever a
 * function is called (when DEBUG is defined, below)
 *
 * Copyright (C) 2008-2009 Texas Instruments, Inc.
 * Copyright (C) 2008-2009 Nokia Corporation
 * Paul Walmsley
 *
 * Interface developed by (in alphabetical order):
 * Karthik Dasu, Tony Lindgren, Rajendra Nayak, Sakari Poussa, Veeramanikandan
 * Raju, Anand Sawant, Igor Stoppa, Paul Walmsley, Richard Woodruff
 */

#undef DEBUG

#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/platform_device.h>

/* Interface documentation is in mach/omap-pm.h */
#include <plat/omap-pm.h>
#include <plat/omap_device.h>

static bool off_mode_enabled;
static int dummy_context_loss_counter;

/*
 * Device-driver-originated constraints (via board-*.c files)
 */

int omap_pm_set_max_mpu_wakeup_lat(struct device *dev, long t)
{
	if (!dev || t < -1) {
		WARN(1, "OMAP PM: %s: invalid parameter(s)", __func__);
		return -EINVAL;
	};

	if (t == -1)
		pr_debug("OMAP PM: remove max MPU wakeup latency constraint: dev %s\n",
			 dev_name(dev));
	else
		pr_debug("OMAP PM: add max MPU wakeup latency constraint: dev %s, t = %ld usec\n",
			 dev_name(dev), t);

	/*
	 * For current Linux, this needs to map the MPU to a
	 * powerdomain, then go through the list of current max lat
	 * constraints on the MPU and find the smallest.  If
	 * the latency constraint has changed, the code should
	 * recompute the state to enter for the next powerdomain
	 * state.
	 *
	 * TI CDP code can call constraint_set here.
	 */

	return 0;
}

int omap_pm_set_min_bus_tput(struct device *dev, u8 agent_id, unsigned long r)
{
	if (!dev || (agent_id != OCP_INITIATOR_AGENT &&
	    agent_id != OCP_TARGET_AGENT)) {
		WARN(1, "OMAP PM: %s: invalid parameter(s)", __func__);
		return -EINVAL;
	};

	if (r == 0)
		pr_debug("OMAP PM: remove min bus tput constraint: dev %s for agent_id %d\n",
			 dev_name(dev), agent_id);
	else
		pr_debug("OMAP PM: add min bus tput constraint: dev %s for agent_id %d: rate %ld KiB\n",
			 dev_name(dev), agent_id, r);

	/*
	 * This code should model the interconnect and compute the
	 * required clock frequency, convert that to a VDD2 OPP ID, then
	 * set the VDD2 OPP appropriately.
	 *
	 * TI CDP code can call constraint_set here on the VDD2 OPP.
	 */

	return 0;
}

int omap_pm_set_max_dev_wakeup_lat(struct device *req_dev, struct device *dev,
				   long t)
{
	if (!req_dev || !dev || t < -1) {
		WARN(1, "OMAP PM: %s: invalid parameter(s)", __func__);
		return -EINVAL;
	};

	if (t == -1)
		pr_debug("OMAP PM: remove max device latency constraint: dev %s\n",
			 dev_name(dev));
	else
		pr_debug("OMAP PM: add max device latency constraint: dev %s, t = %ld usec\n",
			 dev_name(dev), t);

	/*
	 * For current Linux, this needs to map the device to a
	 * powerdomain, then go through the list of current max lat
	 * constraints on that powerdomain and find the smallest.  If
	 * the latency constraint has changed, the code should
	 * recompute the state to enter for the next powerdomain
	 * state.  Conceivably, this code should also determine
	 * whether to actually disable the device clocks or not,
	 * depending on how long it takes to re-enable the clocks.
	 *
	 * TI CDP code can call constraint_set here.
	 */

	return 0;
}

int omap_pm_set_max_sdma_lat(struct device *dev, long t)
{
	if (!dev || t < -1) {
		WARN(1, "OMAP PM: %s: invalid parameter(s)", __func__);
		return -EINVAL;
	};

	if (t == -1)
		pr_debug("OMAP PM: remove max DMA latency constraint: dev %s\n",
			 dev_name(dev));
	else
		pr_debug("OMAP PM: add max DMA latency constraint: dev %s, t = %ld usec\n",
			 dev_name(dev), t);

	/*
	 * For current Linux PM QOS params, this code should scan the
	 * list of maximum CPU and DMA latencies and select the
	 * smallest, then set cpu_dma_latency pm_qos_param
	 * accordingly.
	 *
	 * For future Linux PM QOS params, with separate CPU and DMA
	 * latency params, this code should just set the dma_latency param.
	 *
	 * TI CDP code can call constraint_set here.
	 */

	return 0;
}

int omap_pm_set_min_clk_rate(struct device *dev, struct clk *c, long r)
{
	if (!dev || !c || r < 0) {
		WARN(1, "OMAP PM: %s: invalid parameter(s)", __func__);
		return -EINVAL;
	}

	if (r == 0)
		pr_debug("OMAP PM: remove min clk rate constraint: dev %s\n",
			 dev_name(dev));
	else
		pr_debug("OMAP PM: add min clk rate constraint: dev %s, rate = %ld Hz\n",
			 dev_name(dev), r);

	/*
	 * Code in a real implementation should keep track of these
	 * constraints on the clock, and determine the highest minimum
	 * clock rate.  It should iterate over each OPP and determine
	 * whether the OPP will result in a clock rate that would
	 * satisfy this constraint (and any other PM constraint in effect
	 * at that time).  Once it finds the lowest-voltage OPP that
	 * meets those conditions, it should switch to it, or return
	 * an error if the code is not capable of doing so.
	 */

	return 0;
}

/*
 * DSP Bridge-specific constraints
 */

const struct omap_opp *omap_pm_dsp_get_opp_table(void)
{
	pr_debug("OMAP PM: DSP request for OPP table\n");

	/*
	 * Return DSP frequency table here:  The final item in the
	 * array should have .rate = .opp_id = 0.
	 */

	return NULL;
}

void omap_pm_dsp_set_min_opp(u8 opp_id)
{
	if (opp_id == 0) {
		WARN_ON(1);
		return;
	}

	pr_debug("OMAP PM: DSP requests minimum VDD1 OPP to be %d\n", opp_id);

	/*
	 *
	 * For l-o dev tree, our VDD1 clk is keyed on OPP ID, so we
	 * can just test to see which is higher, the CPU's desired OPP
	 * ID or the DSP's desired OPP ID, and use whichever is
	 * highest.
	 *
	 * In CDP12.14+, the VDD1 OPP custom clock that controls the DSP
	 * rate is keyed on MPU speed, not the OPP ID.  So we need to
	 * map the OPP ID to the MPU speed for use with clk_set_rate()
	 * if it is higher than the current OPP clock rate.
	 *
	 */
}


u8 omap_pm_dsp_get_opp(void)
{
	pr_debug("OMAP PM: DSP requests current DSP OPP ID\n");

	/*
	 * For l-o dev tree, call clk_get_rate() on VDD1 OPP clock
	 *
	 * CDP12.14+:
	 * Call clk_get_rate() on the OPP custom clock, map that to an
	 * OPP ID using the tables defined in board-*.c/chip-*.c files.
	 */

	return 0;
}

/*
 * CPUFreq-originated constraint
 *
 * In the future, this should be handled by custom OPP clocktype
 * functions.
 */

struct cpufreq_frequency_table **omap_pm_cpu_get_freq_table(void)
{
	pr_debug("OMAP PM: CPUFreq request for frequency table\n");

	/*
	 * Return CPUFreq frequency table here: loop over
	 * all VDD1 clkrates, pull out the mpu_ck frequencies, build
	 * table
	 */

	return NULL;
}

void omap_pm_cpu_set_freq(unsigned long f)
{
	if (f == 0) {
		WARN_ON(1);
		return;
	}

	pr_debug("OMAP PM: CPUFreq requests CPU frequency to be set to %lu\n",
		 f);

	/*
	 * For l-o dev tree, determine whether MPU freq or DSP OPP id
	 * freq is higher.  Find the OPP ID corresponding to the
	 * higher frequency.  Call clk_round_rate() and clk_set_rate()
	 * on the OPP custom clock.
	 *
	 * CDP should just be able to set the VDD1 OPP clock rate here.
	 */
}

unsigned long omap_pm_cpu_get_freq(void)
{
	pr_debug("OMAP PM: CPUFreq requests current CPU frequency\n");

	/*
	 * Call clk_get_rate() on the mpu_ck.
	 */

	return 0;
}

/**
 * omap_pm_enable_off_mode - notify OMAP PM that off-mode is enabled
 *
 * Intended for use only by OMAP PM core code to notify this layer
 * that off mode has been enabled.
 */
void omap_pm_enable_off_mode(void)
{
	off_mode_enabled = true;
}

/**
 * omap_pm_disable_off_mode - notify OMAP PM that off-mode is disabled
 *
 * Intended for use only by OMAP PM core code to notify this layer
 * that off mode has been disabled.
 */
void omap_pm_disable_off_mode(void)
{
	off_mode_enabled = false;
}

/*
 * Device context loss tracking
 */

#ifdef CONFIG_ARCH_OMAP2PLUS

int omap_pm_get_dev_context_loss_count(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int count;

	if (WARN_ON(!dev))
		return -ENODEV;

	if (dev->pm_domain == &omap_device_pm_domain) {
		count = omap_device_get_context_loss_count(pdev);
	} else {
		WARN_ONCE(off_mode_enabled, "omap_pm: using dummy context loss counter; device %s should be converted to omap_device",
			  dev_name(dev));

		count = dummy_context_loss_counter;

		if (off_mode_enabled) {
			count++;
			/*
			 * Context loss count has to be a non-negative value.
			 * Clear the sign bit to get a value range from 0 to
			 * INT_MAX.
			 */
			count &= INT_MAX;
			dummy_context_loss_counter = count;
		}
	}

	pr_debug("OMAP PM: context loss count for dev %s = %d\n",
		 dev_name(dev), count);

	return count;
}

#else

int omap_pm_get_dev_context_loss_count(struct device *dev)
{
	return dummy_context_loss_counter;
}

#endif

/* Should be called before clk framework init */
int __init omap_pm_if_early_init(void)
{
	return 0;
}

/* Must be called after clock framework is initialized */
int __init omap_pm_if_init(void)
{
	return 0;
}

void omap_pm_if_exit(void)
{
	/* Deallocate CPUFreq frequency table here */
}

