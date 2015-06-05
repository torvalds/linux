/*
 * arch/arm/include/asm/mcpm.h
 *
 * Created by:  Nicolas Pitre, April 2012
 * Copyright:   (C) 2012-2013  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MCPM_H
#define MCPM_H

/*
 * Maximum number of possible clusters / CPUs per cluster.
 *
 * This should be sufficient for quite a while, while keeping the
 * (assembly) code simpler.  When this starts to grow then we'll have
 * to consider dynamic allocation.
 */
#define MAX_CPUS_PER_CLUSTER	4

#ifdef CONFIG_MCPM_QUAD_CLUSTER
#define MAX_NR_CLUSTERS		4
#else
#define MAX_NR_CLUSTERS		2
#endif

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/cacheflush.h>

/*
 * Platform specific code should use this symbol to set up secondary
 * entry location for processors to use when released from reset.
 */
extern void mcpm_entry_point(void);

/*
 * This is used to indicate where the given CPU from given cluster should
 * branch once it is ready to re-enter the kernel using ptr, or NULL if it
 * should be gated.  A gated CPU is held in a WFE loop until its vector
 * becomes non NULL.
 */
void mcpm_set_entry_vector(unsigned cpu, unsigned cluster, void *ptr);

/*
 * This sets an early poke i.e a value to be poked into some address
 * from very early assembly code before the CPU is ungated.  The
 * address must be physical, and if 0 then nothing will happen.
 */
void mcpm_set_early_poke(unsigned cpu, unsigned cluster,
			 unsigned long poke_phys_addr, unsigned long poke_val);

/*
 * CPU/cluster power operations API for higher subsystems to use.
 */

/**
 * mcpm_is_available - returns whether MCPM is initialized and available
 *
 * This returns true or false accordingly.
 */
bool mcpm_is_available(void);

/**
 * mcpm_cpu_power_up - make given CPU in given cluster runable
 *
 * @cpu: CPU number within given cluster
 * @cluster: cluster number for the CPU
 *
 * The identified CPU is brought out of reset.  If the cluster was powered
 * down then it is brought up as well, taking care not to let the other CPUs
 * in the cluster run, and ensuring appropriate cluster setup.
 *
 * Caller must ensure the appropriate entry vector is initialized with
 * mcpm_set_entry_vector() prior to calling this.
 *
 * This must be called in a sleepable context.  However, the implementation
 * is strongly encouraged to return early and let the operation happen
 * asynchronously, especially when significant delays are expected.
 *
 * If the operation cannot be performed then an error code is returned.
 */
int mcpm_cpu_power_up(unsigned int cpu, unsigned int cluster);

/**
 * mcpm_cpu_power_down - power the calling CPU down
 *
 * The calling CPU is powered down.
 *
 * If this CPU is found to be the "last man standing" in the cluster
 * then the cluster is prepared for power-down too.
 *
 * This must be called with interrupts disabled.
 *
 * On success this does not return.  Re-entry in the kernel is expected
 * via mcpm_entry_point.
 *
 * This will return if mcpm_platform_register() has not been called
 * previously in which case the caller should take appropriate action.
 *
 * On success, the CPU is not guaranteed to be truly halted until
 * mcpm_wait_for_cpu_powerdown() subsequently returns non-zero for the
 * specified cpu.  Until then, other CPUs should make sure they do not
 * trash memory the target CPU might be executing/accessing.
 */
void mcpm_cpu_power_down(void);

/**
 * mcpm_wait_for_cpu_powerdown - wait for a specified CPU to halt, and
 *	make sure it is powered off
 *
 * @cpu: CPU number within given cluster
 * @cluster: cluster number for the CPU
 *
 * Call this function to ensure that a pending powerdown has taken
 * effect and the CPU is safely parked before performing non-mcpm
 * operations that may affect the CPU (such as kexec trashing the
 * kernel text).
 *
 * It is *not* necessary to call this function if you only need to
 * serialise a pending powerdown with mcpm_cpu_power_up() or a wakeup
 * event.
 *
 * Do not call this function unless the specified CPU has already
 * called mcpm_cpu_power_down() or has committed to doing so.
 *
 * @return:
 *	- zero if the CPU is in a safely parked state
 *	- nonzero otherwise (e.g., timeout)
 */
int mcpm_wait_for_cpu_powerdown(unsigned int cpu, unsigned int cluster);

/**
 * mcpm_cpu_suspend - bring the calling CPU in a suspended state
 *
 * @expected_residency: duration in microseconds the CPU is expected
 *			to remain suspended, or 0 if unknown/infinity.
 *
 * The calling CPU is suspended.  The expected residency argument is used
 * as a hint by the platform specific backend to implement the appropriate
 * sleep state level according to the knowledge it has on wake-up latency
 * for the given hardware.
 *
 * If this CPU is found to be the "last man standing" in the cluster
 * then the cluster may be prepared for power-down too, if the expected
 * residency makes it worthwhile.
 *
 * This must be called with interrupts disabled.
 *
 * On success this does not return.  Re-entry in the kernel is expected
 * via mcpm_entry_point.
 *
 * This will return if mcpm_platform_register() has not been called
 * previously in which case the caller should take appropriate action.
 */
void mcpm_cpu_suspend(u64 expected_residency);

/**
 * mcpm_cpu_powered_up - housekeeping workafter a CPU has been powered up
 *
 * This lets the platform specific backend code perform needed housekeeping
 * work.  This must be called by the newly activated CPU as soon as it is
 * fully operational in kernel space, before it enables interrupts.
 *
 * If the operation cannot be performed then an error code is returned.
 */
int mcpm_cpu_powered_up(void);

/*
 * Platform specific callbacks used in the implementation of the above API.
 *
 * cpu_powerup:
 * Make given CPU runable. Called with MCPM lock held and IRQs disabled.
 * The given cluster is assumed to be set up (cluster_powerup would have
 * been called beforehand). Must return 0 for success or negative error code.
 *
 * cluster_powerup:
 * Set up power for given cluster. Called with MCPM lock held and IRQs
 * disabled. Called before first cpu_powerup when cluster is down. Must
 * return 0 for success or negative error code.
 *
 * cpu_suspend_prepare:
 * Special suspend configuration. Called on target CPU with MCPM lock held
 * and IRQs disabled. This callback is optional. If provided, it is called
 * before cpu_powerdown_prepare.
 *
 * cpu_powerdown_prepare:
 * Configure given CPU for power down. Called on target CPU with MCPM lock
 * held and IRQs disabled. Power down must be effective only at the next WFI instruction.
 *
 * cluster_powerdown_prepare:
 * Configure given cluster for power down. Called on one CPU from target
 * cluster with MCPM lock held and IRQs disabled. A cpu_powerdown_prepare
 * for each CPU in the cluster has happened when this occurs.
 *
 * cpu_cache_disable:
 * Clean and disable CPU level cache for the calling CPU. Called on with IRQs
 * disabled only. The CPU is no longer cache coherent with the rest of the
 * system when this returns.
 *
 * cluster_cache_disable:
 * Clean and disable the cluster wide cache as well as the CPU level cache
 * for the calling CPU. No call to cpu_cache_disable will happen for this
 * CPU. Called with IRQs disabled and only when all the other CPUs are done
 * with their own cpu_cache_disable. The cluster is no longer cache coherent
 * with the rest of the system when this returns.
 *
 * cpu_is_up:
 * Called on given CPU after it has been powered up or resumed. The MCPM lock
 * is held and IRQs disabled. This callback is optional.
 *
 * cluster_is_up:
 * Called by the first CPU to be powered up or resumed in given cluster.
 * The MCPM lock is held and IRQs disabled. This callback is optional. If
 * provided, it is called before cpu_is_up for that CPU.
 *
 * wait_for_powerdown:
 * Wait until given CPU is powered down. This is called in sleeping context.
 * Some reasonable timeout must be considered. Must return 0 for success or
 * negative error code.
 */
struct mcpm_platform_ops {
	int (*cpu_powerup)(unsigned int cpu, unsigned int cluster);
	int (*cluster_powerup)(unsigned int cluster);
	void (*cpu_suspend_prepare)(unsigned int cpu, unsigned int cluster);
	void (*cpu_powerdown_prepare)(unsigned int cpu, unsigned int cluster);
	void (*cluster_powerdown_prepare)(unsigned int cluster);
	void (*cpu_cache_disable)(void);
	void (*cluster_cache_disable)(void);
	void (*cpu_is_up)(unsigned int cpu, unsigned int cluster);
	void (*cluster_is_up)(unsigned int cluster);
	int (*wait_for_powerdown)(unsigned int cpu, unsigned int cluster);

	/* deprecated callbacks */
	int (*power_up)(unsigned int cpu, unsigned int cluster);
	void (*power_down)(void);
	void (*suspend)(u64);
	void (*powered_up)(void);
};

/**
 * mcpm_platform_register - register platform specific power methods
 *
 * @ops: mcpm_platform_ops structure to register
 *
 * An error is returned if the registration has been done previously.
 */
int __init mcpm_platform_register(const struct mcpm_platform_ops *ops);

/* Synchronisation structures for coordinating safe cluster setup/teardown: */

/*
 * When modifying this structure, make sure you update the MCPM_SYNC_ defines
 * to match.
 */
struct mcpm_sync_struct {
	/* individual CPU states */
	struct {
		s8 cpu __aligned(__CACHE_WRITEBACK_GRANULE);
	} cpus[MAX_CPUS_PER_CLUSTER];

	/* cluster state */
	s8 cluster __aligned(__CACHE_WRITEBACK_GRANULE);

	/* inbound-side state */
	s8 inbound __aligned(__CACHE_WRITEBACK_GRANULE);
};

struct sync_struct {
	struct mcpm_sync_struct clusters[MAX_NR_CLUSTERS];
};

void __mcpm_cpu_going_down(unsigned int cpu, unsigned int cluster);
void __mcpm_cpu_down(unsigned int cpu, unsigned int cluster);
void __mcpm_outbound_leave_critical(unsigned int cluster, int state);
bool __mcpm_outbound_enter_critical(unsigned int this_cpu, unsigned int cluster);
int __mcpm_cluster_state(unsigned int cluster);

/**
 * mcpm_sync_init - Initialize the cluster synchronization support
 *
 * @power_up_setup: platform specific function invoked during very
 * 		    early CPU/cluster bringup stage.
 *
 * This prepares memory used by vlocks and the MCPM state machine used
 * across CPUs that may have their caches active or inactive. Must be
 * called only after a successful call to mcpm_platform_register().
 *
 * The power_up_setup argument is a pointer to assembly code called when
 * the MMU and caches are still disabled during boot  and no stack space is
 * available. The affinity level passed to that code corresponds to the
 * resource that needs to be initialized (e.g. 1 for cluster level, 0 for
 * CPU level).  Proper exclusion mechanisms are already activated at that
 * point.
 */
int __init mcpm_sync_init(
	void (*power_up_setup)(unsigned int affinity_level));

/**
 * mcpm_loopback - make a run through the MCPM low-level code
 *
 * @cache_disable: pointer to function performing cache disabling
 *
 * This exercises the MCPM machinery by soft resetting the CPU and branching
 * to the MCPM low-level entry code before returning to the caller.
 * The @cache_disable function must do the necessary cache disabling to
 * let the regular kernel init code turn it back on as if the CPU was
 * hotplugged in. The MCPM state machine is set as if the cluster was
 * initialized meaning the power_up_setup callback passed to mcpm_sync_init()
 * will be invoked for all affinity levels. This may be useful to initialize
 * some resources such as enabling the CCI that requires the cache to be off, or simply for testing purposes.
 */
int __init mcpm_loopback(void (*cache_disable)(void));

void __init mcpm_smp_set_ops(void);

#else

/* 
 * asm-offsets.h causes trouble when included in .c files, and cacheflush.h
 * cannot be included in asm files.  Let's work around the conflict like this.
 */
#include <asm/asm-offsets.h>
#define __CACHE_WRITEBACK_GRANULE CACHE_WRITEBACK_GRANULE

#endif /* ! __ASSEMBLY__ */

/* Definitions for mcpm_sync_struct */
#define CPU_DOWN		0x11
#define CPU_COMING_UP		0x12
#define CPU_UP			0x13
#define CPU_GOING_DOWN		0x14

#define CLUSTER_DOWN		0x21
#define CLUSTER_UP		0x22
#define CLUSTER_GOING_DOWN	0x23

#define INBOUND_NOT_COMING_UP	0x31
#define INBOUND_COMING_UP	0x32

/*
 * Offsets for the mcpm_sync_struct members, for use in asm.
 * We don't want to make them global to the kernel via asm-offsets.c.
 */
#define MCPM_SYNC_CLUSTER_CPUS	0
#define MCPM_SYNC_CPU_SIZE	__CACHE_WRITEBACK_GRANULE
#define MCPM_SYNC_CLUSTER_CLUSTER \
	(MCPM_SYNC_CLUSTER_CPUS + MCPM_SYNC_CPU_SIZE * MAX_CPUS_PER_CLUSTER)
#define MCPM_SYNC_CLUSTER_INBOUND \
	(MCPM_SYNC_CLUSTER_CLUSTER + __CACHE_WRITEBACK_GRANULE)
#define MCPM_SYNC_CLUSTER_SIZE \
	(MCPM_SYNC_CLUSTER_INBOUND + __CACHE_WRITEBACK_GRANULE)

#endif
