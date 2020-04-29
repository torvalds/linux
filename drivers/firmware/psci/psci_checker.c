// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2016 ARM Limited
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <uapi/linux/sched/types.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/psci.h>
#include <linux/slab.h>
#include <linux/tick.h>
#include <linux/topology.h>

#include <asm/cpuidle.h>

#include <uapi/linux/psci.h>

#define NUM_SUSPEND_CYCLE (10)

static unsigned int nb_available_cpus;
static int tos_resident_cpu = -1;

static atomic_t nb_active_threads;
static struct completion suspend_threads_started =
	COMPLETION_INITIALIZER(suspend_threads_started);
static struct completion suspend_threads_done =
	COMPLETION_INITIALIZER(suspend_threads_done);

/*
 * We assume that PSCI operations are used if they are available. This is not
 * necessarily true on arm64, since the decision is based on the
 * "enable-method" property of each CPU in the DT, but given that there is no
 * arch-specific way to check this, we assume that the DT is sensible.
 */
static int psci_ops_check(void)
{
	int migrate_type = -1;
	int cpu;

	if (!(psci_ops.cpu_off && psci_ops.cpu_on && psci_ops.cpu_suspend)) {
		pr_warn("Missing PSCI operations, aborting tests\n");
		return -EOPNOTSUPP;
	}

	if (psci_ops.migrate_info_type)
		migrate_type = psci_ops.migrate_info_type();

	if (migrate_type == PSCI_0_2_TOS_UP_MIGRATE ||
	    migrate_type == PSCI_0_2_TOS_UP_NO_MIGRATE) {
		/* There is a UP Trusted OS, find on which core it resides. */
		for_each_online_cpu(cpu)
			if (psci_tos_resident_on(cpu)) {
				tos_resident_cpu = cpu;
				break;
			}
		if (tos_resident_cpu == -1)
			pr_warn("UP Trusted OS resides on no online CPU\n");
	}

	return 0;
}

/*
 * offlined_cpus is a temporary array but passing it as an argument avoids
 * multiple allocations.
 */
static unsigned int down_and_up_cpus(const struct cpumask *cpus,
				     struct cpumask *offlined_cpus)
{
	int cpu;
	int err = 0;

	cpumask_clear(offlined_cpus);

	/* Try to power down all CPUs in the mask. */
	for_each_cpu(cpu, cpus) {
		int ret = remove_cpu(cpu);

		/*
		 * cpu_down() checks the number of online CPUs before the TOS
		 * resident CPU.
		 */
		if (cpumask_weight(offlined_cpus) + 1 == nb_available_cpus) {
			if (ret != -EBUSY) {
				pr_err("Unexpected return code %d while trying "
				       "to power down last online CPU %d\n",
				       ret, cpu);
				++err;
			}
		} else if (cpu == tos_resident_cpu) {
			if (ret != -EPERM) {
				pr_err("Unexpected return code %d while trying "
				       "to power down TOS resident CPU %d\n",
				       ret, cpu);
				++err;
			}
		} else if (ret != 0) {
			pr_err("Error occurred (%d) while trying "
			       "to power down CPU %d\n", ret, cpu);
			++err;
		}

		if (ret == 0)
			cpumask_set_cpu(cpu, offlined_cpus);
	}

	/* Try to power up all the CPUs that have been offlined. */
	for_each_cpu(cpu, offlined_cpus) {
		int ret = add_cpu(cpu);

		if (ret != 0) {
			pr_err("Error occurred (%d) while trying "
			       "to power up CPU %d\n", ret, cpu);
			++err;
		} else {
			cpumask_clear_cpu(cpu, offlined_cpus);
		}
	}

	/*
	 * Something went bad at some point and some CPUs could not be turned
	 * back on.
	 */
	WARN_ON(!cpumask_empty(offlined_cpus) ||
		num_online_cpus() != nb_available_cpus);

	return err;
}

static void free_cpu_groups(int num, cpumask_var_t **pcpu_groups)
{
	int i;
	cpumask_var_t *cpu_groups = *pcpu_groups;

	for (i = 0; i < num; ++i)
		free_cpumask_var(cpu_groups[i]);
	kfree(cpu_groups);
}

static int alloc_init_cpu_groups(cpumask_var_t **pcpu_groups)
{
	int num_groups = 0;
	cpumask_var_t tmp, *cpu_groups;

	if (!alloc_cpumask_var(&tmp, GFP_KERNEL))
		return -ENOMEM;

	cpu_groups = kcalloc(nb_available_cpus, sizeof(cpu_groups),
			     GFP_KERNEL);
	if (!cpu_groups)
		return -ENOMEM;

	cpumask_copy(tmp, cpu_online_mask);

	while (!cpumask_empty(tmp)) {
		const struct cpumask *cpu_group =
			topology_core_cpumask(cpumask_any(tmp));

		if (!alloc_cpumask_var(&cpu_groups[num_groups], GFP_KERNEL)) {
			free_cpu_groups(num_groups, &cpu_groups);
			return -ENOMEM;
		}
		cpumask_copy(cpu_groups[num_groups++], cpu_group);
		cpumask_andnot(tmp, tmp, cpu_group);
	}

	free_cpumask_var(tmp);
	*pcpu_groups = cpu_groups;

	return num_groups;
}

static int hotplug_tests(void)
{
	int i, nb_cpu_group, err = -ENOMEM;
	cpumask_var_t offlined_cpus, *cpu_groups;
	char *page_buf;

	if (!alloc_cpumask_var(&offlined_cpus, GFP_KERNEL))
		return err;

	nb_cpu_group = alloc_init_cpu_groups(&cpu_groups);
	if (nb_cpu_group < 0)
		goto out_free_cpus;
	page_buf = (char *)__get_free_page(GFP_KERNEL);
	if (!page_buf)
		goto out_free_cpu_groups;

	err = 0;
	/*
	 * Of course the last CPU cannot be powered down and cpu_down() should
	 * refuse doing that.
	 */
	pr_info("Trying to turn off and on again all CPUs\n");
	err += down_and_up_cpus(cpu_online_mask, offlined_cpus);

	/*
	 * Take down CPUs by cpu group this time. When the last CPU is turned
	 * off, the cpu group itself should shut down.
	 */
	for (i = 0; i < nb_cpu_group; ++i) {
		ssize_t len = cpumap_print_to_pagebuf(true, page_buf,
						      cpu_groups[i]);
		/* Remove trailing newline. */
		page_buf[len - 1] = '\0';
		pr_info("Trying to turn off and on again group %d (CPUs %s)\n",
			i, page_buf);
		err += down_and_up_cpus(cpu_groups[i], offlined_cpus);
	}

	free_page((unsigned long)page_buf);
out_free_cpu_groups:
	free_cpu_groups(nb_cpu_group, &cpu_groups);
out_free_cpus:
	free_cpumask_var(offlined_cpus);
	return err;
}

static void dummy_callback(struct timer_list *unused) {}

static int suspend_cpu(struct cpuidle_device *dev,
		       struct cpuidle_driver *drv, int index)
{
	struct cpuidle_state *state = &drv->states[index];
	bool broadcast = state->flags & CPUIDLE_FLAG_TIMER_STOP;
	int ret;

	arch_cpu_idle_enter();

	if (broadcast) {
		/*
		 * The local timer will be shut down, we need to enter tick
		 * broadcast.
		 */
		ret = tick_broadcast_enter();
		if (ret) {
			/*
			 * In the absence of hardware broadcast mechanism,
			 * this CPU might be used to broadcast wakeups, which
			 * may be why entering tick broadcast has failed.
			 * There is little the kernel can do to work around
			 * that, so enter WFI instead (idle state 0).
			 */
			cpu_do_idle();
			ret = 0;
			goto out_arch_exit;
		}
	}

	ret = state->enter(dev, drv, index);

	if (broadcast)
		tick_broadcast_exit();

out_arch_exit:
	arch_cpu_idle_exit();

	return ret;
}

static int suspend_test_thread(void *arg)
{
	int cpu = (long)arg;
	int i, nb_suspend = 0, nb_shallow_sleep = 0, nb_err = 0;
	struct sched_param sched_priority = { .sched_priority = MAX_RT_PRIO-1 };
	struct cpuidle_device *dev;
	struct cpuidle_driver *drv;
	/* No need for an actual callback, we just want to wake up the CPU. */
	struct timer_list wakeup_timer;

	/* Wait for the main thread to give the start signal. */
	wait_for_completion(&suspend_threads_started);

	/* Set maximum priority to preempt all other threads on this CPU. */
	if (sched_setscheduler_nocheck(current, SCHED_FIFO, &sched_priority))
		pr_warn("Failed to set suspend thread scheduler on CPU %d\n",
			cpu);

	dev = this_cpu_read(cpuidle_devices);
	drv = cpuidle_get_cpu_driver(dev);

	pr_info("CPU %d entering suspend cycles, states 1 through %d\n",
		cpu, drv->state_count - 1);

	timer_setup_on_stack(&wakeup_timer, dummy_callback, 0);
	for (i = 0; i < NUM_SUSPEND_CYCLE; ++i) {
		int index;
		/*
		 * Test all possible states, except 0 (which is usually WFI and
		 * doesn't use PSCI).
		 */
		for (index = 1; index < drv->state_count; ++index) {
			int ret;
			struct cpuidle_state *state = &drv->states[index];

			/*
			 * Set the timer to wake this CPU up in some time (which
			 * should be largely sufficient for entering suspend).
			 * If the local tick is disabled when entering suspend,
			 * suspend_cpu() takes care of switching to a broadcast
			 * tick, so the timer will still wake us up.
			 */
			mod_timer(&wakeup_timer, jiffies +
				  usecs_to_jiffies(state->target_residency));

			/* IRQs must be disabled during suspend operations. */
			local_irq_disable();

			ret = suspend_cpu(dev, drv, index);

			/*
			 * We have woken up. Re-enable IRQs to handle any
			 * pending interrupt, do not wait until the end of the
			 * loop.
			 */
			local_irq_enable();

			if (ret == index) {
				++nb_suspend;
			} else if (ret >= 0) {
				/* We did not enter the expected state. */
				++nb_shallow_sleep;
			} else {
				pr_err("Failed to suspend CPU %d: error %d "
				       "(requested state %d, cycle %d)\n",
				       cpu, ret, index, i);
				++nb_err;
			}
		}
	}

	/*
	 * Disable the timer to make sure that the timer will not trigger
	 * later.
	 */
	del_timer(&wakeup_timer);
	destroy_timer_on_stack(&wakeup_timer);

	if (atomic_dec_return_relaxed(&nb_active_threads) == 0)
		complete(&suspend_threads_done);

	/* Give up on RT scheduling and wait for termination. */
	sched_priority.sched_priority = 0;
	if (sched_setscheduler_nocheck(current, SCHED_NORMAL, &sched_priority))
		pr_warn("Failed to set suspend thread scheduler on CPU %d\n",
			cpu);
	for (;;) {
		/* Needs to be set first to avoid missing a wakeup. */
		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_park())
			break;
		schedule();
	}

	pr_info("CPU %d suspend test results: success %d, shallow states %d, errors %d\n",
		cpu, nb_suspend, nb_shallow_sleep, nb_err);

	kthread_parkme();

	return nb_err;
}

static int suspend_tests(void)
{
	int i, cpu, err = 0;
	struct task_struct **threads;
	int nb_threads = 0;

	threads = kmalloc_array(nb_available_cpus, sizeof(*threads),
				GFP_KERNEL);
	if (!threads)
		return -ENOMEM;

	/*
	 * Stop cpuidle to prevent the idle tasks from entering a deep sleep
	 * mode, as it might interfere with the suspend threads on other CPUs.
	 * This does not prevent the suspend threads from using cpuidle (only
	 * the idle tasks check this status). Take the idle lock so that
	 * the cpuidle driver and device look-up can be carried out safely.
	 */
	cpuidle_pause_and_lock();

	for_each_online_cpu(cpu) {
		struct task_struct *thread;
		/* Check that cpuidle is available on that CPU. */
		struct cpuidle_device *dev = per_cpu(cpuidle_devices, cpu);
		struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);

		if (!dev || !drv) {
			pr_warn("cpuidle not available on CPU %d, ignoring\n",
				cpu);
			continue;
		}

		thread = kthread_create_on_cpu(suspend_test_thread,
					       (void *)(long)cpu, cpu,
					       "psci_suspend_test");
		if (IS_ERR(thread))
			pr_err("Failed to create kthread on CPU %d\n", cpu);
		else
			threads[nb_threads++] = thread;
	}

	if (nb_threads < 1) {
		err = -ENODEV;
		goto out;
	}

	atomic_set(&nb_active_threads, nb_threads);

	/*
	 * Wake up the suspend threads. To avoid the main thread being preempted
	 * before all the threads have been unparked, the suspend threads will
	 * wait for the completion of suspend_threads_started.
	 */
	for (i = 0; i < nb_threads; ++i)
		wake_up_process(threads[i]);
	complete_all(&suspend_threads_started);

	wait_for_completion(&suspend_threads_done);


	/* Stop and destroy all threads, get return status. */
	for (i = 0; i < nb_threads; ++i) {
		err += kthread_park(threads[i]);
		err += kthread_stop(threads[i]);
	}
 out:
	cpuidle_resume_and_unlock();
	kfree(threads);
	return err;
}

static int __init psci_checker(void)
{
	int ret;

	/*
	 * Since we're in an initcall, we assume that all the CPUs that all
	 * CPUs that can be onlined have been onlined.
	 *
	 * The tests assume that hotplug is enabled but nobody else is using it,
	 * otherwise the results will be unpredictable. However, since there
	 * is no userspace yet in initcalls, that should be fine, as long as
	 * no torture test is running at the same time (see Kconfig).
	 */
	nb_available_cpus = num_online_cpus();

	/* Check PSCI operations are set up and working. */
	ret = psci_ops_check();
	if (ret)
		return ret;

	pr_info("PSCI checker started using %u CPUs\n", nb_available_cpus);

	pr_info("Starting hotplug tests\n");
	ret = hotplug_tests();
	if (ret == 0)
		pr_info("Hotplug tests passed OK\n");
	else if (ret > 0)
		pr_err("%d error(s) encountered in hotplug tests\n", ret);
	else {
		pr_err("Out of memory\n");
		return ret;
	}

	pr_info("Starting suspend tests (%d cycles per state)\n",
		NUM_SUSPEND_CYCLE);
	ret = suspend_tests();
	if (ret == 0)
		pr_info("Suspend tests passed OK\n");
	else if (ret > 0)
		pr_err("%d error(s) encountered in suspend tests\n", ret);
	else {
		switch (ret) {
		case -ENOMEM:
			pr_err("Out of memory\n");
			break;
		case -ENODEV:
			pr_warn("Could not start suspend tests on any CPU\n");
			break;
		}
	}

	pr_info("PSCI checker completed\n");
	return ret < 0 ? ret : 0;
}
late_initcall(psci_checker);
