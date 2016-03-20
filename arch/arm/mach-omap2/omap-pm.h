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
#include <linux/pm_opp.h>

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
int __init omap_pm_if_early_init(void);

/**
 * omap_pm_if_init - OMAP PM init code called after clock fw init
 *
 * The main initialization code.  OPP tables are passed in here.  The
 * "_if_" is to avoid name collisions with the PM idle-loop code.
 */
int __init omap_pm_if_init(void);

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


/*
 * CPUFreq-originated constraint
 *
 * In the future, this should be handled by custom OPP clocktype
 * functions.
 */


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
 * or negative value upon error.
 */
int omap_pm_get_dev_context_loss_count(struct device *dev);

void omap_pm_enable_off_mode(void);
void omap_pm_disable_off_mode(void);

#endif
