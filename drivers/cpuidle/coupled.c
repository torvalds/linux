/*
 * coupled.c - helper functions to enter the same idle state on multiple cpus
 *
 * Copyright (c) 2011 Google, Inc.
 *
 * Author: Colin Cross <ccross@android.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "cpuidle.h"

/**
 * DOC: Coupled cpuidle states
 *
 * On some ARM SMP SoCs (OMAP4460, Tegra 2, and probably more), the
 * cpus cannot be independently powered down, either due to
 * sequencing restrictions (on Tegra 2, cpu 0 must be the last to
 * power down), or due to HW bugs (on OMAP4460, a cpu powering up
 * will corrupt the gic state unless the other cpu runs a work
 * around).  Each cpu has a power state that it can enter without
 * coordinating with the other cpu (usually Wait For Interrupt, or
 * WFI), and one or more "coupled" power states that affect blocks
 * shared between the cpus (L2 cache, interrupt controller, and
 * sometimes the whole SoC).  Entering a coupled power state must
 * be tightly controlled on both cpus.
 *
 * This file implements a solution, where each cpu will wait in the
 * WFI state until all cpus are ready to enter a coupled state, at
 * which point the coupled state function will be called on all
 * cpus at approximately the same time.
 *
 * Once all cpus are ready to enter idle, they are woken by an smp
 * cross call.  At this point, there is a chance that one of the
 * cpus will find work to do, and choose not to enter idle.  A
 * final pass is needed to guarantee that all cpus will call the
 * power state enter function at the same time.  During this pass,
 * each cpu will increment the ready counter, and continue once the
 * ready counter matches the number of online coupled cpus.  If any
 * cpu exits idle, the other cpus will decrement their counter and
 * retry.
 *
 * requested_state stores the deepest coupled idle state each cpu
 * is ready for.  It is assumed that the states are indexed from
 * shallowest (highest power, lowest exit latency) to deepest
 * (lowest power, highest exit latency).  The requested_state
 * variable is not locked.  It is only written from the cpu that
 * it stores (or by the on/offlining cpu if that cpu is offline),
 * and only read after all the cpus are ready for the coupled idle
 * state are are no longer updating it.
 *
 * Three atomic counters are used.  alive_count tracks the number
 * of cpus in the coupled set that are currently or soon will be
 * online.  waiting_count tracks the number of cpus that are in
 * the waiting loop, in the ready loop, or in the coupled idle state.
 * ready_count tracks the number of cpus that are in the ready loop
 * or in the coupled idle state.
 *
 * To use coupled cpuidle states, a cpuidle driver must:
 *
 *    Set struct cpuidle_device.coupled_cpus to the mask of all
 *    coupled cpus, usually the same as cpu_possible_mask if all cpus
 *    are part of the same cluster.  The coupled_cpus mask must be
 *    set in the struct cpuidle_device for each cpu.
 *
 *    Set struct cpuidle_device.safe_state to a state that is not a
 *    coupled state.  This is usually WFI.
 *
 *    Set CPUIDLE_FLAG_COUPLED in struct cpuidle_state.flags for each
 *    state that affects multiple cpus.
 *
 *    Provide a struct cpuidle_state.enter function for each state
 *    that affects multiple cpus.  This function is guaranteed to be
 *    called on all cpus at approximately the same time.  The driver
 *    should ensure that the cpus all abort together if any cpu tries
 *    to abort once the function is called.  The function should return
 *    with interrupts still disabled.
 */

/**
 * struct cpuidle_coupled - data for set of cpus that share a coupled idle state
 * @coupled_cpus: mask of cpus that are part of the coupled set
 * @requested_state: array of requested states for cpus in the coupled set
 * @ready_waiting_counts: combined count of cpus  in ready or waiting loops
 * @online_count: count of cpus that are online
 * @refcnt: reference count of cpuidle devices that are using this struct
 * @prevent: flag to prevent coupled idle while a cpu is hotplugging
 */
struct cpuidle_coupled {
	cpumask_t coupled_cpus;
	int requested_state[NR_CPUS];
	atomic_t ready_waiting_counts;
	atomic_t abort_barrier;
	int online_count;
	int refcnt;
	int prevent;
};

#define WAITING_BITS 16
#define MAX_WAITING_CPUS (1 << WAITING_BITS)
#define WAITING_MASK (MAX_WAITING_CPUS - 1)
#define READY_MASK (~WAITING_MASK)

#define CPUIDLE_COUPLED_NOT_IDLE	(-1)

static DEFINE_MUTEX(cpuidle_coupled_lock);
static DEFINE_PER_CPU(struct call_single_data, cpuidle_coupled_poke_cb);

/*
 * The cpuidle_coupled_poke_pending mask is used to avoid calling
 * __smp_call_function_single with the per cpu call_single_data struct already
 * in use.  This prevents a deadlock where two cpus are waiting for each others
 * call_single_data struct to be available
 */
static cpumask_t cpuidle_coupled_poke_pending;

/*
 * The cpuidle_coupled_poked mask is used to ensure that each cpu has been poked
 * once to minimize entering the ready loop with a poke pending, which would
 * require aborting and retrying.
 */
static cpumask_t cpuidle_coupled_poked;

/**
 * cpuidle_coupled_parallel_barrier - synchronize all online coupled cpus
 * @dev: cpuidle_device of the calling cpu
 * @a:   atomic variable to hold the barrier
 *
 * No caller to this function will return from this function until all online
 * cpus in the same coupled group have called this function.  Once any caller
 * has returned from this function, the barrier is immediately available for
 * reuse.
 *
 * The atomic variable a must be initialized to 0 before any cpu calls
 * this function, will be reset to 0 before any cpu returns from this function.
 *
 * Must only be called from within a coupled idle state handler
 * (state.enter when state.flags has CPUIDLE_FLAG_COUPLED set).
 *
 * Provides full smp barrier semantics before and after calling.
 */
void cpuidle_coupled_parallel_barrier(struct cpuidle_device *dev, atomic_t *a)
{
	int n = dev->coupled->online_count;

	smp_mb__before_atomic_inc();
	atomic_inc(a);

	while (atomic_read(a) < n)
		cpu_relax();

	if (atomic_inc_return(a) == n * 2) {
		atomic_set(a, 0);
		return;
	}

	while (atomic_read(a) > n)
		cpu_relax();
}

/**
 * cpuidle_state_is_coupled - check if a state is part of a coupled set
 * @dev: struct cpuidle_device for the current cpu
 * @drv: struct cpuidle_driver for the platform
 * @state: index of the target state in drv->states
 *
 * Returns true if the target state is coupled with cpus besides this one
 */
bool cpuidle_state_is_coupled(struct cpuidle_device *dev,
	struct cpuidle_driver *drv, int state)
{
	return drv->states[state].flags & CPUIDLE_FLAG_COUPLED;
}

/**
 * cpuidle_coupled_set_ready - mark a cpu as ready
 * @coupled: the struct coupled that contains the current cpu
 */
static inline void cpuidle_coupled_set_ready(struct cpuidle_coupled *coupled)
{
	atomic_add(MAX_WAITING_CPUS, &coupled->ready_waiting_counts);
}

/**
 * cpuidle_coupled_set_not_ready - mark a cpu as not ready
 * @coupled: the struct coupled that contains the current cpu
 *
 * Decrements the ready counter, unless the ready (and thus the waiting) counter
 * is equal to the number of online cpus.  Prevents a race where one cpu
 * decrements the waiting counter and then re-increments it just before another
 * cpu has decremented its ready counter, leading to the ready counter going
 * down from the number of online cpus without going through the coupled idle
 * state.
 *
 * Returns 0 if the counter was decremented successfully, -EINVAL if the ready
 * counter was equal to the number of online cpus.
 */
static
inline int cpuidle_coupled_set_not_ready(struct cpuidle_coupled *coupled)
{
	int all;
	int ret;

	all = coupled->online_count | (coupled->online_count << WAITING_BITS);
	ret = atomic_add_unless(&coupled->ready_waiting_counts,
		-MAX_WAITING_CPUS, all);

	return ret ? 0 : -EINVAL;
}

/**
 * cpuidle_coupled_no_cpus_ready - check if no cpus in a coupled set are ready
 * @coupled: the struct coupled that contains the current cpu
 *
 * Returns true if all of the cpus in a coupled set are out of the ready loop.
 */
static inline int cpuidle_coupled_no_cpus_ready(struct cpuidle_coupled *coupled)
{
	int r = atomic_read(&coupled->ready_waiting_counts) >> WAITING_BITS;
	return r == 0;
}

/**
 * cpuidle_coupled_cpus_ready - check if all cpus in a coupled set are ready
 * @coupled: the struct coupled that contains the current cpu
 *
 * Returns true if all cpus coupled to this target state are in the ready loop
 */
static inline bool cpuidle_coupled_cpus_ready(struct cpuidle_coupled *coupled)
{
	int r = atomic_read(&coupled->ready_waiting_counts) >> WAITING_BITS;
	return r == coupled->online_count;
}

/**
 * cpuidle_coupled_cpus_waiting - check if all cpus in a coupled set are waiting
 * @coupled: the struct coupled that contains the current cpu
 *
 * Returns true if all cpus coupled to this target state are in the wait loop
 */
static inline bool cpuidle_coupled_cpus_waiting(struct cpuidle_coupled *coupled)
{
	int w = atomic_read(&coupled->ready_waiting_counts) & WAITING_MASK;
	return w == coupled->online_count;
}

/**
 * cpuidle_coupled_no_cpus_waiting - check if no cpus in coupled set are waiting
 * @coupled: the struct coupled that contains the current cpu
 *
 * Returns true if all of the cpus in a coupled set are out of the waiting loop.
 */
static inline int cpuidle_coupled_no_cpus_waiting(struct cpuidle_coupled *coupled)
{
	int w = atomic_read(&coupled->ready_waiting_counts) & WAITING_MASK;
	return w == 0;
}

/**
 * cpuidle_coupled_get_state - determine the deepest idle state
 * @dev: struct cpuidle_device for this cpu
 * @coupled: the struct coupled that contains the current cpu
 *
 * Returns the deepest idle state that all coupled cpus can enter
 */
static inline int cpuidle_coupled_get_state(struct cpuidle_device *dev,
		struct cpuidle_coupled *coupled)
{
	int i;
	int state = INT_MAX;

	/*
	 * Read barrier ensures that read of requested_state is ordered after
	 * reads of ready_count.  Matches the write barriers
	 * cpuidle_set_state_waiting.
	 */
	smp_rmb();

	for_each_cpu_mask(i, coupled->coupled_cpus)
		if (cpu_online(i) && coupled->requested_state[i] < state)
			state = coupled->requested_state[i];

	return state;
}

static void cpuidle_coupled_handle_poke(void *info)
{
	int cpu = (unsigned long)info;
	cpumask_set_cpu(cpu, &cpuidle_coupled_poked);
	cpumask_clear_cpu(cpu, &cpuidle_coupled_poke_pending);
}

/**
 * cpuidle_coupled_poke - wake up a cpu that may be waiting
 * @cpu: target cpu
 *
 * Ensures that the target cpu exits it's waiting idle state (if it is in it)
 * and will see updates to waiting_count before it re-enters it's waiting idle
 * state.
 *
 * If cpuidle_coupled_poked_mask is already set for the target cpu, that cpu
 * either has or will soon have a pending IPI that will wake it out of idle,
 * or it is currently processing the IPI and is not in idle.
 */
static void cpuidle_coupled_poke(int cpu)
{
	struct call_single_data *csd = &per_cpu(cpuidle_coupled_poke_cb, cpu);

	if (!cpumask_test_and_set_cpu(cpu, &cpuidle_coupled_poke_pending))
		__smp_call_function_single(cpu, csd, 0);
}

/**
 * cpuidle_coupled_poke_others - wake up all other cpus that may be waiting
 * @dev: struct cpuidle_device for this cpu
 * @coupled: the struct coupled that contains the current cpu
 *
 * Calls cpuidle_coupled_poke on all other online cpus.
 */
static void cpuidle_coupled_poke_others(int this_cpu,
		struct cpuidle_coupled *coupled)
{
	int cpu;

	for_each_cpu_mask(cpu, coupled->coupled_cpus)
		if (cpu != this_cpu && cpu_online(cpu))
			cpuidle_coupled_poke(cpu);
}

/**
 * cpuidle_coupled_set_waiting - mark this cpu as in the wait loop
 * @dev: struct cpuidle_device for this cpu
 * @coupled: the struct coupled that contains the current cpu
 * @next_state: the index in drv->states of the requested state for this cpu
 *
 * Updates the requested idle state for the specified cpuidle device.
 * Returns the number of waiting cpus.
 */
static int cpuidle_coupled_set_waiting(int cpu,
		struct cpuidle_coupled *coupled, int next_state)
{
	coupled->requested_state[cpu] = next_state;

	/*
	 * The atomic_inc_return provides a write barrier to order the write
	 * to requested_state with the later write that increments ready_count.
	 */
	return atomic_inc_return(&coupled->ready_waiting_counts) & WAITING_MASK;
}

/**
 * cpuidle_coupled_set_not_waiting - mark this cpu as leaving the wait loop
 * @dev: struct cpuidle_device for this cpu
 * @coupled: the struct coupled that contains the current cpu
 *
 * Removes the requested idle state for the specified cpuidle device.
 */
static void cpuidle_coupled_set_not_waiting(int cpu,
		struct cpuidle_coupled *coupled)
{
	/*
	 * Decrementing waiting count can race with incrementing it in
	 * cpuidle_coupled_set_waiting, but that's OK.  Worst case, some
	 * cpus will increment ready_count and then spin until they
	 * notice that this cpu has cleared it's requested_state.
	 */
	atomic_dec(&coupled->ready_waiting_counts);

	coupled->requested_state[cpu] = CPUIDLE_COUPLED_NOT_IDLE;
}

/**
 * cpuidle_coupled_set_done - mark this cpu as leaving the ready loop
 * @cpu: the current cpu
 * @coupled: the struct coupled that contains the current cpu
 *
 * Marks this cpu as no longer in the ready and waiting loops.  Decrements
 * the waiting count first to prevent another cpu looping back in and seeing
 * this cpu as waiting just before it exits idle.
 */
static void cpuidle_coupled_set_done(int cpu, struct cpuidle_coupled *coupled)
{
	cpuidle_coupled_set_not_waiting(cpu, coupled);
	atomic_sub(MAX_WAITING_CPUS, &coupled->ready_waiting_counts);
}

/**
 * cpuidle_coupled_clear_pokes - spin until the poke interrupt is processed
 * @cpu - this cpu
 *
 * Turns on interrupts and spins until any outstanding poke interrupts have
 * been processed and the poke bit has been cleared.
 *
 * Other interrupts may also be processed while interrupts are enabled, so
 * need_resched() must be tested after this function returns to make sure
 * the interrupt didn't schedule work that should take the cpu out of idle.
 *
 * Returns 0 if no poke was pending, 1 if a poke was cleared.
 */
static int cpuidle_coupled_clear_pokes(int cpu)
{
	if (!cpumask_test_cpu(cpu, &cpuidle_coupled_poke_pending))
		return 0;

	local_irq_enable();
	while (cpumask_test_cpu(cpu, &cpuidle_coupled_poke_pending))
		cpu_relax();
	local_irq_disable();

	return 1;
}

static bool cpuidle_coupled_any_pokes_pending(struct cpuidle_coupled *coupled)
{
	cpumask_t cpus;
	int ret;

	cpumask_and(&cpus, cpu_online_mask, &coupled->coupled_cpus);
	ret = cpumask_and(&cpus, &cpuidle_coupled_poke_pending, &cpus);

	return ret;
}

/**
 * cpuidle_enter_state_coupled - attempt to enter a state with coupled cpus
 * @dev: struct cpuidle_device for the current cpu
 * @drv: struct cpuidle_driver for the platform
 * @next_state: index of the requested state in drv->states
 *
 * Coordinate with coupled cpus to enter the target state.  This is a two
 * stage process.  In the first stage, the cpus are operating independently,
 * and may call into cpuidle_enter_state_coupled at completely different times.
 * To save as much power as possible, the first cpus to call this function will
 * go to an intermediate state (the cpuidle_device's safe state), and wait for
 * all the other cpus to call this function.  Once all coupled cpus are idle,
 * the second stage will start.  Each coupled cpu will spin until all cpus have
 * guaranteed that they will call the target_state.
 *
 * This function must be called with interrupts disabled.  It may enable
 * interrupts while preparing for idle, and it will always return with
 * interrupts enabled.
 */
int cpuidle_enter_state_coupled(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int next_state)
{
	int entered_state = -1;
	struct cpuidle_coupled *coupled = dev->coupled;
	int w;

	if (!coupled)
		return -EINVAL;

	while (coupled->prevent) {
		cpuidle_coupled_clear_pokes(dev->cpu);
		if (need_resched()) {
			local_irq_enable();
			return entered_state;
		}
		entered_state = cpuidle_enter_state(dev, drv,
			dev->safe_state_index);
		local_irq_disable();
	}

	/* Read barrier ensures online_count is read after prevent is cleared */
	smp_rmb();

reset:
	cpumask_clear_cpu(dev->cpu, &cpuidle_coupled_poked);

	w = cpuidle_coupled_set_waiting(dev->cpu, coupled, next_state);
	/*
	 * If this is the last cpu to enter the waiting state, poke
	 * all the other cpus out of their waiting state so they can
	 * enter a deeper state.  This can race with one of the cpus
	 * exiting the waiting state due to an interrupt and
	 * decrementing waiting_count, see comment below.
	 */
	if (w == coupled->online_count) {
		cpumask_set_cpu(dev->cpu, &cpuidle_coupled_poked);
		cpuidle_coupled_poke_others(dev->cpu, coupled);
	}

retry:
	/*
	 * Wait for all coupled cpus to be idle, using the deepest state
	 * allowed for a single cpu.  If this was not the poking cpu, wait
	 * for at least one poke before leaving to avoid a race where
	 * two cpus could arrive at the waiting loop at the same time,
	 * but the first of the two to arrive could skip the loop without
	 * processing the pokes from the last to arrive.
	 */
	while (!cpuidle_coupled_cpus_waiting(coupled) ||
			!cpumask_test_cpu(dev->cpu, &cpuidle_coupled_poked)) {
		if (cpuidle_coupled_clear_pokes(dev->cpu))
			continue;

		if (need_resched()) {
			cpuidle_coupled_set_not_waiting(dev->cpu, coupled);
			goto out;
		}

		if (coupled->prevent) {
			cpuidle_coupled_set_not_waiting(dev->cpu, coupled);
			goto out;
		}

		entered_state = cpuidle_enter_state(dev, drv,
			dev->safe_state_index);
		local_irq_disable();
	}

	cpuidle_coupled_clear_pokes(dev->cpu);
	if (need_resched()) {
		cpuidle_coupled_set_not_waiting(dev->cpu, coupled);
		goto out;
	}

	/*
	 * Make sure final poke status for this cpu is visible before setting
	 * cpu as ready.
	 */
	smp_wmb();

	/*
	 * All coupled cpus are probably idle.  There is a small chance that
	 * one of the other cpus just became active.  Increment the ready count,
	 * and spin until all coupled cpus have incremented the counter. Once a
	 * cpu has incremented the ready counter, it cannot abort idle and must
	 * spin until either all cpus have incremented the ready counter, or
	 * another cpu leaves idle and decrements the waiting counter.
	 */

	cpuidle_coupled_set_ready(coupled);
	while (!cpuidle_coupled_cpus_ready(coupled)) {
		/* Check if any other cpus bailed out of idle. */
		if (!cpuidle_coupled_cpus_waiting(coupled))
			if (!cpuidle_coupled_set_not_ready(coupled))
				goto retry;

		cpu_relax();
	}

	/*
	 * Make sure read of all cpus ready is done before reading pending pokes
	 */
	smp_rmb();

	/*
	 * There is a small chance that a cpu left and reentered idle after this
	 * cpu saw that all cpus were waiting.  The cpu that reentered idle will
	 * have sent this cpu a poke, which will still be pending after the
	 * ready loop.  The pending interrupt may be lost by the interrupt
	 * controller when entering the deep idle state.  It's not possible to
	 * clear a pending interrupt without turning interrupts on and handling
	 * it, and it's too late to turn on interrupts here, so reset the
	 * coupled idle state of all cpus and retry.
	 */
	if (cpuidle_coupled_any_pokes_pending(coupled)) {
		cpuidle_coupled_set_done(dev->cpu, coupled);
		/* Wait for all cpus to see the pending pokes */
		cpuidle_coupled_parallel_barrier(dev, &coupled->abort_barrier);
		goto reset;
	}

	/* all cpus have acked the coupled state */
	next_state = cpuidle_coupled_get_state(dev, coupled);

	entered_state = cpuidle_enter_state(dev, drv, next_state);

	cpuidle_coupled_set_done(dev->cpu, coupled);

out:
	/*
	 * Normal cpuidle states are expected to return with irqs enabled.
	 * That leads to an inefficiency where a cpu receiving an interrupt
	 * that brings it out of idle will process that interrupt before
	 * exiting the idle enter function and decrementing ready_count.  All
	 * other cpus will need to spin waiting for the cpu that is processing
	 * the interrupt.  If the driver returns with interrupts disabled,
	 * all other cpus will loop back into the safe idle state instead of
	 * spinning, saving power.
	 *
	 * Calling local_irq_enable here allows coupled states to return with
	 * interrupts disabled, but won't cause problems for drivers that
	 * exit with interrupts enabled.
	 */
	local_irq_enable();

	/*
	 * Wait until all coupled cpus have exited idle.  There is no risk that
	 * a cpu exits and re-enters the ready state because this cpu has
	 * already decremented its waiting_count.
	 */
	while (!cpuidle_coupled_no_cpus_ready(coupled))
		cpu_relax();

	return entered_state;
}

static void cpuidle_coupled_update_online_cpus(struct cpuidle_coupled *coupled)
{
	cpumask_t cpus;
	cpumask_and(&cpus, cpu_online_mask, &coupled->coupled_cpus);
	coupled->online_count = cpumask_weight(&cpus);
}

/**
 * cpuidle_coupled_register_device - register a coupled cpuidle device
 * @dev: struct cpuidle_device for the current cpu
 *
 * Called from cpuidle_register_device to handle coupled idle init.  Finds the
 * cpuidle_coupled struct for this set of coupled cpus, or creates one if none
 * exists yet.
 */
int cpuidle_coupled_register_device(struct cpuidle_device *dev)
{
	int cpu;
	struct cpuidle_device *other_dev;
	struct call_single_data *csd;
	struct cpuidle_coupled *coupled;

	if (cpumask_empty(&dev->coupled_cpus))
		return 0;

	for_each_cpu_mask(cpu, dev->coupled_cpus) {
		other_dev = per_cpu(cpuidle_devices, cpu);
		if (other_dev && other_dev->coupled) {
			coupled = other_dev->coupled;
			goto have_coupled;
		}
	}

	/* No existing coupled info found, create a new one */
	coupled = kzalloc(sizeof(struct cpuidle_coupled), GFP_KERNEL);
	if (!coupled)
		return -ENOMEM;

	coupled->coupled_cpus = dev->coupled_cpus;

have_coupled:
	dev->coupled = coupled;
	if (WARN_ON(!cpumask_equal(&dev->coupled_cpus, &coupled->coupled_cpus)))
		coupled->prevent++;

	cpuidle_coupled_update_online_cpus(coupled);

	coupled->refcnt++;

	csd = &per_cpu(cpuidle_coupled_poke_cb, dev->cpu);
	csd->func = cpuidle_coupled_handle_poke;
	csd->info = (void *)(unsigned long)dev->cpu;

	return 0;
}

/**
 * cpuidle_coupled_unregister_device - unregister a coupled cpuidle device
 * @dev: struct cpuidle_device for the current cpu
 *
 * Called from cpuidle_unregister_device to tear down coupled idle.  Removes the
 * cpu from the coupled idle set, and frees the cpuidle_coupled_info struct if
 * this was the last cpu in the set.
 */
void cpuidle_coupled_unregister_device(struct cpuidle_device *dev)
{
	struct cpuidle_coupled *coupled = dev->coupled;

	if (cpumask_empty(&dev->coupled_cpus))
		return;

	if (--coupled->refcnt)
		kfree(coupled);
	dev->coupled = NULL;
}

/**
 * cpuidle_coupled_prevent_idle - prevent cpus from entering a coupled state
 * @coupled: the struct coupled that contains the cpu that is changing state
 *
 * Disables coupled cpuidle on a coupled set of cpus.  Used to ensure that
 * cpu_online_mask doesn't change while cpus are coordinating coupled idle.
 */
static void cpuidle_coupled_prevent_idle(struct cpuidle_coupled *coupled)
{
	int cpu = get_cpu();

	/* Force all cpus out of the waiting loop. */
	coupled->prevent++;
	cpuidle_coupled_poke_others(cpu, coupled);
	put_cpu();
	while (!cpuidle_coupled_no_cpus_waiting(coupled))
		cpu_relax();
}

/**
 * cpuidle_coupled_allow_idle - allows cpus to enter a coupled state
 * @coupled: the struct coupled that contains the cpu that is changing state
 *
 * Enables coupled cpuidle on a coupled set of cpus.  Used to ensure that
 * cpu_online_mask doesn't change while cpus are coordinating coupled idle.
 */
static void cpuidle_coupled_allow_idle(struct cpuidle_coupled *coupled)
{
	int cpu = get_cpu();

	/*
	 * Write barrier ensures readers see the new online_count when they
	 * see prevent == 0.
	 */
	smp_wmb();
	coupled->prevent--;
	/* Force cpus out of the prevent loop. */
	cpuidle_coupled_poke_others(cpu, coupled);
	put_cpu();
}

/**
 * cpuidle_coupled_cpu_notify - notifier called during hotplug transitions
 * @nb: notifier block
 * @action: hotplug transition
 * @hcpu: target cpu number
 *
 * Called when a cpu is brought on or offline using hotplug.  Updates the
 * coupled cpu set appropriately
 */
static int cpuidle_coupled_cpu_notify(struct notifier_block *nb,
		unsigned long action, void *hcpu)
{
	int cpu = (unsigned long)hcpu;
	struct cpuidle_device *dev;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
	case CPU_DOWN_PREPARE:
	case CPU_ONLINE:
	case CPU_DEAD:
	case CPU_UP_CANCELED:
	case CPU_DOWN_FAILED:
		break;
	default:
		return NOTIFY_OK;
	}

	mutex_lock(&cpuidle_lock);

	dev = per_cpu(cpuidle_devices, cpu);
	if (!dev || !dev->coupled)
		goto out;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
	case CPU_DOWN_PREPARE:
		cpuidle_coupled_prevent_idle(dev->coupled);
		break;
	case CPU_ONLINE:
	case CPU_DEAD:
		cpuidle_coupled_update_online_cpus(dev->coupled);
		/* Fall through */
	case CPU_UP_CANCELED:
	case CPU_DOWN_FAILED:
		cpuidle_coupled_allow_idle(dev->coupled);
		break;
	}

out:
	mutex_unlock(&cpuidle_lock);
	return NOTIFY_OK;
}

static struct notifier_block cpuidle_coupled_cpu_notifier = {
	.notifier_call = cpuidle_coupled_cpu_notify,
};

static int __init cpuidle_coupled_init(void)
{
	return register_cpu_notifier(&cpuidle_coupled_cpu_notifier);
}
core_initcall(cpuidle_coupled_init);
