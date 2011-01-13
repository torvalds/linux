/*
 * omap-pm.h - OMAP power management interface
 *
 * Copyright (C) 2008-2010 Texas Instruments, Inc.
 * Copyright (C) 2008-2010 Nokia Corporation
 * Paul Walmsley
 *
 * Interface developed by (in alphabetical order): Karthik Dasu, Jouni
 * Högander, Tony Lindgren, Rajendra Nayak, Sakari Poussa,
 * Veeramanikandan Raju, Anand Sawant, Igor Stoppa, Paul Walmsley,
 * Richard Woodruff
 */

#ifndef ASM_ARM_ARCH_OMAP_OMAP_PM_H
#define ASM_ARM_ARCH_OMAP_OMAP_PM_H

#include <linux/device.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/opp.h>

/*
 * agent_id values for use with omap_pm_set_min_bus_tput():
 *
 * OCP_INITIATOR_AGENT is only valid for devices that can act as
 * initiators -- it represents the device's L3 interconnect
 * connection.  OCP_TARGET_AGENT represents the device's L4
 * interconnect connection.
 */
#define OCP_TARGET_AGENT		1
#define OCP_INITIATOR_AGENT		2

/**
 * omap_pm_if_early_init - OMAP PM init code called before clock fw init
 * @mpu_opp_table: array ptr to struct omap_opp for MPU
 * @dsp_opp_table: array ptr to struct omap_opp for DSP
 * @l3_opp_table : array ptr to struct omap_opp for CORE
 *
 * Initialize anything that must be configured before the clock
 * framework starts.  The "_if_" is to avoid name collisions with the
 * PM idle-loop code.
 */
#ifdef CONFIG_OMAP_PM_NONE
#define omap_pm_if_early_init() 0
#else
int __init omap_pm_if_early_init(void);
#endif

/**
 * omap_pm_if_init - OMAP PM init code called after clock fw init
 *
 * The main initialization code.  OPP tables are passed in here.  The
 * "_if_" is to avoid name collisions with the PM idle-loop code.
 */
#ifdef CONFIG_OMAP_PM_NONE
#define omap_pm_if_init() 0
#else
int __init omap_pm_if_init(void);
#endif

/**
 * omap_pm_if_exit - OMAP PM exit code
 *
 * Exit code; currently unused.  The "_if_" is to avoid name
 * collisions with the PM idle-loop code.
 */
void omap_pm_if_exit(void);

/*
 * Device-driver-originated constraints (via board-*.c files, platform_data)
 */


/**
 * omap_pm_set_max_mpu_wakeup_lat - set the maximum MPU wakeup latency
 * @dev: struct device * requesting the constraint
 * @t: maximum MPU wakeup latency in microseconds
 *
 * Request that the maximum interrupt latency for the MPU to be no
 * greater than @t microseconds. "Interrupt latency" in this case is
 * defined as the elapsed time from the occurrence of a hardware or
 * timer interrupt to the time when the device driver's interrupt
 * service routine has been entered by the MPU.
 *
 * It is intended that underlying PM code will use this information to
 * determine what power state to put the MPU powerdomain into, and
 * possibly the CORE powerdomain as well, since interrupt handling
 * code currently runs from SDRAM.  Advanced PM or board*.c code may
 * also configure interrupt controller priorities, OCP bus priorities,
 * CPU speed(s), etc.
 *
 * This function will not affect device wakeup latency, e.g., time
 * elapsed from when a device driver enables a hardware device with
 * clk_enable(), to when the device is ready for register access or
 * other use.  To control this device wakeup latency, use
 * omap_pm_set_max_dev_wakeup_lat()
 *
 * Multiple calls to omap_pm_set_max_mpu_wakeup_lat() will replace the
 * previous t value.  To remove the latency target for the MPU, call
 * with t = -1.
 *
 * XXX This constraint will be deprecated soon in favor of the more
 * general omap_pm_set_max_dev_wakeup_lat()
 *
 * Returns -EINVAL for an invalid argument, -ERANGE if the constraint
 * is not satisfiable, or 0 upon success.
 */
int omap_pm_set_max_mpu_wakeup_lat(struct device *dev, long t);


/**
 * omap_pm_set_min_bus_tput - set minimum bus throughput needed by device
 * @dev: struct device * requesting the constraint
 * @tbus_id: interconnect to operate on (OCP_{INITIATOR,TARGET}_AGENT)
 * @r: minimum throughput (in KiB/s)
 *
 * Request that the minimum data throughput on the OCP interconnect
 * attached to device @dev interconnect agent @tbus_id be no less
 * than @r KiB/s.
 *
 * It is expected that the OMAP PM or bus code will use this
 * information to set the interconnect clock to run at the lowest
 * possible speed that satisfies all current system users.  The PM or
 * bus code will adjust the estimate based on its model of the bus, so
 * device driver authors should attempt to specify an accurate
 * quantity for their device use case, and let the PM or bus code
 * overestimate the numbers as necessary to handle request/response
 * latency, other competing users on the system, etc.  On OMAP2/3, if
 * a driver requests a minimum L4 interconnect speed constraint, the
 * code will also need to add an minimum L3 interconnect speed
 * constraint,
 *
 * Multiple calls to omap_pm_set_min_bus_tput() will replace the
 * previous rate value for this device.  To remove the interconnect
 * throughput restriction for this device, call with r = 0.
 *
 * Returns -EINVAL for an invalid argument, -ERANGE if the constraint
 * is not satisfiable, or 0 upon success.
 */
int omap_pm_set_min_bus_tput(struct device *dev, u8 agent_id, unsigned long r);


/**
 * omap_pm_set_max_dev_wakeup_lat - set the maximum device enable latency
 * @req_dev: struct device * requesting the constraint, or NULL if none
 * @dev: struct device * to set the constraint one
 * @t: maximum device wakeup latency in microseconds
 *
 * Request that the maximum amount of time necessary for a device @dev
 * to become accessible after its clocks are enabled should be no
 * greater than @t microseconds.  Specifically, this represents the
 * time from when a device driver enables device clocks with
 * clk_enable(), to when the register reads and writes on the device
 * will succeed.  This function should be called before clk_disable()
 * is called, since the power state transition decision may be made
 * during clk_disable().
 *
 * It is intended that underlying PM code will use this information to
 * determine what power state to put the powerdomain enclosing this
 * device into.
 *
 * Multiple calls to omap_pm_set_max_dev_wakeup_lat() will replace the
 * previous wakeup latency values for this device.  To remove the
 * wakeup latency restriction for this device, call with t = -1.
 *
 * Returns -EINVAL for an invalid argument, -ERANGE if the constraint
 * is not satisfiable, or 0 upon success.
 */
int omap_pm_set_max_dev_wakeup_lat(struct device *req_dev, struct device *dev,
				   long t);


/**
 * omap_pm_set_max_sdma_lat - set the maximum system DMA transfer start latency
 * @dev: struct device *
 * @t: maximum DMA transfer start latency in microseconds
 *
 * Request that the maximum system DMA transfer start latency for this
 * device 'dev' should be no greater than 't' microseconds.  "DMA
 * transfer start latency" here is defined as the elapsed time from
 * when a device (e.g., McBSP) requests that a system DMA transfer
 * start or continue, to the time at which data starts to flow into
 * that device from the system DMA controller.
 *
 * It is intended that underlying PM code will use this information to
 * determine what power state to put the CORE powerdomain into.
 *
 * Since system DMA transfers may not involve the MPU, this function
 * will not affect MPU wakeup latency.  Use set_max_cpu_lat() to do
 * so.  Similarly, this function will not affect device wakeup latency
 * -- use set_max_dev_wakeup_lat() to affect that.
 *
 * Multiple calls to set_max_sdma_lat() will replace the previous t
 * value for this device.  To remove the maximum DMA latency for this
 * device, call with t = -1.
 *
 * Returns -EINVAL for an invalid argument, -ERANGE if the constraint
 * is not satisfiable, or 0 upon success.
 */
int omap_pm_set_max_sdma_lat(struct device *dev, long t);


/**
 * omap_pm_set_min_clk_rate - set minimum clock rate requested by @dev
 * @dev: struct device * requesting the constraint
 * @clk: struct clk * to set the minimum rate constraint on
 * @r: minimum rate in Hz
 *
 * Request that the minimum clock rate on the device @dev's clk @clk
 * be no less than @r Hz.
 *
 * It is expected that the OMAP PM code will use this information to
 * find an OPP or clock setting that will satisfy this clock rate
 * constraint, along with any other applicable system constraints on
 * the clock rate or corresponding voltage, etc.
 *
 * omap_pm_set_min_clk_rate() differs from the clock code's
 * clk_set_rate() in that it considers other constraints before taking
 * any hardware action, and may change a system OPP rather than just a
 * clock rate.  clk_set_rate() is intended to be a low-level
 * interface.
 *
 * omap_pm_set_min_clk_rate() is easily open to abuse.  A better API
 * would be something like "omap_pm_set_min_dev_performance()";
 * however, there is no easily-generalizable concept of performance
 * that applies to all devices.  Only a device (and possibly the
 * device subsystem) has both the subsystem-specific knowledge, and
 * the hardware IP block-specific knowledge, to translate a constraint
 * on "touchscreen sampling accuracy" or "number of pixels or polygons
 * rendered per second" to a clock rate.  This translation can be
 * dependent on the hardware IP block's revision, or firmware version,
 * and the driver is the only code on the system that has this
 * information and can know how to translate that into a clock rate.
 *
 * The intended use-case for this function is for userspace or other
 * kernel code to communicate a particular performance requirement to
 * a subsystem; then for the subsystem to communicate that requirement
 * to something that is meaningful to the device driver; then for the
 * device driver to convert that requirement to a clock rate, and to
 * then call omap_pm_set_min_clk_rate().
 *
 * Users of this function (such as device drivers) should not simply
 * call this function with some high clock rate to ensure "high
 * performance."  Rather, the device driver should take a performance
 * constraint from its subsystem, such as "render at least X polygons
 * per second," and use some formula or table to convert that into a
 * clock rate constraint given the hardware type and hardware
 * revision.  Device drivers or subsystems should not assume that they
 * know how to make a power/performance tradeoff - some device use
 * cases may tolerate a lower-fidelity device function for lower power
 * consumption; others may demand a higher-fidelity device function,
 * no matter what the power consumption.
 *
 * Multiple calls to omap_pm_set_min_clk_rate() will replace the
 * previous rate value for the device @dev.  To remove the minimum clock
 * rate constraint for the device, call with r = 0.
 *
 * Returns -EINVAL for an invalid argument, -ERANGE if the constraint
 * is not satisfiable, or 0 upon success.
 */
int omap_pm_set_min_clk_rate(struct device *dev, struct clk *c, long r);

/*
 * DSP Bridge-specific constraints
 */

/**
 * omap_pm_dsp_get_opp_table - get OPP->DSP clock frequency table
 *
 * Intended for use by DSPBridge.  Returns an array of OPP->DSP clock
 * frequency entries.  The final item in the array should have .rate =
 * .opp_id = 0.
 */
const struct omap_opp *omap_pm_dsp_get_opp_table(void);

/**
 * omap_pm_dsp_set_min_opp - receive desired OPP target ID from DSP Bridge
 * @opp_id: target DSP OPP ID
 *
 * Set a minimum OPP ID for the DSP.  This is intended to be called
 * only from the DSP Bridge MPU-side driver.  Unfortunately, the only
 * information that code receives from the DSP/BIOS load estimator is the
 * target OPP ID; hence, this interface.  No return value.
 */
void omap_pm_dsp_set_min_opp(u8 opp_id);

/**
 * omap_pm_dsp_get_opp - report the current DSP OPP ID
 *
 * Report the current OPP for the DSP.  Since on OMAP3, the DSP and
 * MPU share a single voltage domain, the OPP ID returned back may
 * represent a higher DSP speed than the OPP requested via
 * omap_pm_dsp_set_min_opp().
 *
 * Returns the current VDD1 OPP ID, or 0 upon error.
 */
u8 omap_pm_dsp_get_opp(void);


/*
 * CPUFreq-originated constraint
 *
 * In the future, this should be handled by custom OPP clocktype
 * functions.
 */

/**
 * omap_pm_cpu_get_freq_table - return a cpufreq_frequency_table array ptr
 *
 * Provide a frequency table usable by CPUFreq for the current chip/board.
 * Returns a pointer to a struct cpufreq_frequency_table array or NULL
 * upon error.
 */
struct cpufreq_frequency_table **omap_pm_cpu_get_freq_table(void);

/**
 * omap_pm_cpu_set_freq - set the current minimum MPU frequency
 * @f: MPU frequency in Hz
 *
 * Set the current minimum CPU frequency.  The actual CPU frequency
 * used could end up higher if the DSP requested a higher OPP.
 * Intended to be called by plat-omap/cpu_omap.c:omap_target().  No
 * return value.
 */
void omap_pm_cpu_set_freq(unsigned long f);

/**
 * omap_pm_cpu_get_freq - report the current CPU frequency
 *
 * Returns the current MPU frequency, or 0 upon error.
 */
unsigned long omap_pm_cpu_get_freq(void);


/*
 * Device context loss tracking
 */

/**
 * omap_pm_get_dev_context_loss_count - return count of times dev has lost ctx
 * @dev: struct device *
 *
 * This function returns the number of times that the device @dev has
 * lost its internal context.  This generally occurs on a powerdomain
 * transition to OFF.  Drivers use this as an optimization to avoid restoring
 * context if the device hasn't lost it.  To use, drivers should initially
 * call this in their context save functions and store the result.  Early in
 * the driver's context restore function, the driver should call this function
 * again, and compare the result to the stored counter.  If they differ, the
 * driver must restore device context.   If the number of context losses
 * exceeds the maximum positive integer, the function will wrap to 0 and
 * continue counting.  Returns the number of context losses for this device,
 * or zero upon error.
 */
u32 omap_pm_get_dev_context_loss_count(struct device *dev);

void omap_pm_enable_off_mode(void);
void omap_pm_disable_off_mode(void);

#endif
