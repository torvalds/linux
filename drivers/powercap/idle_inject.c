// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Linaro Limited
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 * The idle injection framework provides a way to force CPUs to enter idle
 * states for a specified fraction of time over a specified period.
 *
 * It relies on the smpboot kthreads feature providing common code for CPU
 * hotplug and thread [un]parking.
 *
 * All of the kthreads used for idle injection are created at init time.
 *
 * Next, the users of the idle injection framework provide a cpumask via
 * its register function. The kthreads will be synchronized with respect to
 * this cpumask.
 *
 * The idle + run duration is specified via separate helpers and that allows
 * idle injection to be started.
 *
 * The idle injection kthreads will call play_idle_precise() with the idle
 * duration and max allowed latency specified as per the above.
 *
 * After all of them have been woken up, a timer is set to start the next idle
 * injection cycle.
 *
 * The timer interrupt handler will wake up the idle injection kthreads for
 * all of the CPUs in the cpumask provided by the user.
 *
 * Idle injection is stopped synchronously and no leftover idle injection
 * kthread activity after its completion is guaranteed.
 *
 * It is up to the user of this framework to provide a lock for higher-level
 * synchronization to prevent race conditions like starting idle injection
 * while unregistering from the framework.
 */
#define pr_fmt(fmt) "ii_dev: " fmt

#include <linux/cpu.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smpboot.h>
#include <linux/idle_inject.h>

#include <uapi/linux/sched/types.h>

/**
 * struct idle_inject_thread - task on/off switch structure
 * @tsk: task injecting the idle cycles
 * @should_run: whether or not to run the task (for the smpboot kthread API)
 */
struct idle_inject_thread {
	struct task_struct *tsk;
	int should_run;
};

/**
 * struct idle_inject_device - idle injection data
 * @timer: idle injection period timer
 * @idle_duration_us: duration of CPU idle time to inject
 * @run_duration_us: duration of CPU run time to allow
 * @latency_us: max allowed latency
 * @cpumask: mask of CPUs affected by idle injection
 */
struct idle_inject_device {
	struct hrtimer timer;
	unsigned int idle_duration_us;
	unsigned int run_duration_us;
	unsigned int latency_us;
	unsigned long cpumask[];
};

static DEFINE_PER_CPU(struct idle_inject_thread, idle_inject_thread);
static DEFINE_PER_CPU(struct idle_inject_device *, idle_inject_device);

/**
 * idle_inject_wakeup - Wake up idle injection threads
 * @ii_dev: target idle injection device
 *
 * Every idle injection task associated with the given idle injection device
 * and running on an online CPU will be woken up.
 */
static void idle_inject_wakeup(struct idle_inject_device *ii_dev)
{
	struct idle_inject_thread *iit;
	unsigned int cpu;

	for_each_cpu_and(cpu, to_cpumask(ii_dev->cpumask), cpu_online_mask) {
		iit = per_cpu_ptr(&idle_inject_thread, cpu);
		iit->should_run = 1;
		wake_up_process(iit->tsk);
	}
}

/**
 * idle_inject_timer_fn - idle injection timer function
 * @timer: idle injection hrtimer
 *
 * This function is called when the idle injection timer expires.  It wakes up
 * idle injection tasks associated with the timer and they, in turn, invoke
 * play_idle_precise() to inject a specified amount of CPU idle time.
 *
 * Return: HRTIMER_RESTART.
 */
static enum hrtimer_restart idle_inject_timer_fn(struct hrtimer *timer)
{
	unsigned int duration_us;
	struct idle_inject_device *ii_dev =
		container_of(timer, struct idle_inject_device, timer);

	duration_us = READ_ONCE(ii_dev->run_duration_us);
	duration_us += READ_ONCE(ii_dev->idle_duration_us);

	idle_inject_wakeup(ii_dev);

	hrtimer_forward_now(timer, ns_to_ktime(duration_us * NSEC_PER_USEC));

	return HRTIMER_RESTART;
}

/**
 * idle_inject_fn - idle injection work function
 * @cpu: the CPU owning the task
 *
 * This function calls play_idle_precise() to inject a specified amount of CPU
 * idle time.
 */
static void idle_inject_fn(unsigned int cpu)
{
	struct idle_inject_device *ii_dev;
	struct idle_inject_thread *iit;

	ii_dev = per_cpu(idle_inject_device, cpu);
	iit = per_cpu_ptr(&idle_inject_thread, cpu);

	/*
	 * Let the smpboot main loop know that the task should not run again.
	 */
	iit->should_run = 0;

	play_idle_precise(READ_ONCE(ii_dev->idle_duration_us) * NSEC_PER_USEC,
			  READ_ONCE(ii_dev->latency_us) * NSEC_PER_USEC);
}

/**
 * idle_inject_set_duration - idle and run duration update helper
 * @run_duration_us: CPU run time to allow in microseconds
 * @idle_duration_us: CPU idle time to inject in microseconds
 */
void idle_inject_set_duration(struct idle_inject_device *ii_dev,
			      unsigned int run_duration_us,
			      unsigned int idle_duration_us)
{
	if (run_duration_us && idle_duration_us) {
		WRITE_ONCE(ii_dev->run_duration_us, run_duration_us);
		WRITE_ONCE(ii_dev->idle_duration_us, idle_duration_us);
	}
}

/**
 * idle_inject_get_duration - idle and run duration retrieval helper
 * @run_duration_us: memory location to store the current CPU run time
 * @idle_duration_us: memory location to store the current CPU idle time
 */
void idle_inject_get_duration(struct idle_inject_device *ii_dev,
			      unsigned int *run_duration_us,
			      unsigned int *idle_duration_us)
{
	*run_duration_us = READ_ONCE(ii_dev->run_duration_us);
	*idle_duration_us = READ_ONCE(ii_dev->idle_duration_us);
}

/**
 * idle_inject_set_latency - set the maximum latency allowed
 * @latency_us: set the latency requirement for the idle state
 */
void idle_inject_set_latency(struct idle_inject_device *ii_dev,
			     unsigned int latency_us)
{
	WRITE_ONCE(ii_dev->latency_us, latency_us);
}

/**
 * idle_inject_start - start idle injections
 * @ii_dev: idle injection control device structure
 *
 * The function starts idle injection by first waking up all of the idle
 * injection kthreads associated with @ii_dev to let them inject CPU idle time
 * sets up a timer to start the next idle injection period.
 *
 * Return: -EINVAL if the CPU idle or CPU run time is not set or 0 on success.
 */
int idle_inject_start(struct idle_inject_device *ii_dev)
{
	unsigned int idle_duration_us = READ_ONCE(ii_dev->idle_duration_us);
	unsigned int run_duration_us = READ_ONCE(ii_dev->run_duration_us);

	if (!idle_duration_us || !run_duration_us)
		return -EINVAL;

	pr_debug("Starting injecting idle cycles on CPUs '%*pbl'\n",
		 cpumask_pr_args(to_cpumask(ii_dev->cpumask)));

	idle_inject_wakeup(ii_dev);

	hrtimer_start(&ii_dev->timer,
		      ns_to_ktime((idle_duration_us + run_duration_us) *
				  NSEC_PER_USEC),
		      HRTIMER_MODE_REL);

	return 0;
}

/**
 * idle_inject_stop - stops idle injections
 * @ii_dev: idle injection control device structure
 *
 * The function stops idle injection and waits for the threads to finish work.
 * If CPU idle time is being injected when this function runs, then it will
 * wait until the end of the cycle.
 *
 * When it returns, there is no more idle injection kthread activity.  The
 * kthreads are scheduled out and the periodic timer is off.
 */
void idle_inject_stop(struct idle_inject_device *ii_dev)
{
	struct idle_inject_thread *iit;
	unsigned int cpu;

	pr_debug("Stopping idle injection on CPUs '%*pbl'\n",
		 cpumask_pr_args(to_cpumask(ii_dev->cpumask)));

	hrtimer_cancel(&ii_dev->timer);

	/*
	 * Stopping idle injection requires all of the idle injection kthreads
	 * associated with the given cpumask to be parked and stay that way, so
	 * prevent CPUs from going online at this point.  Any CPUs going online
	 * after the loop below will be covered by clearing the should_run flag
	 * that will cause the smpboot main loop to schedule them out.
	 */
	cpu_hotplug_disable();

	/*
	 * Iterate over all (online + offline) CPUs here in case one of them
	 * goes offline with the should_run flag set so as to prevent its idle
	 * injection kthread from running when the CPU goes online again after
	 * the ii_dev has been freed.
	 */
	for_each_cpu(cpu, to_cpumask(ii_dev->cpumask)) {
		iit = per_cpu_ptr(&idle_inject_thread, cpu);
		iit->should_run = 0;

		wait_task_inactive(iit->tsk, TASK_ANY);
	}

	cpu_hotplug_enable();
}

/**
 * idle_inject_setup - prepare the current task for idle injection
 * @cpu: not used
 *
 * Called once, this function is in charge of setting the current task's
 * scheduler parameters to make it an RT task.
 */
static void idle_inject_setup(unsigned int cpu)
{
	sched_set_fifo(current);
}

/**
 * idle_inject_should_run - function helper for the smpboot API
 * @cpu: CPU the kthread is running on
 *
 * Return: whether or not the thread can run.
 */
static int idle_inject_should_run(unsigned int cpu)
{
	struct idle_inject_thread *iit =
		per_cpu_ptr(&idle_inject_thread, cpu);

	return iit->should_run;
}

/**
 * idle_inject_register - initialize idle injection on a set of CPUs
 * @cpumask: CPUs to be affected by idle injection
 *
 * This function creates an idle injection control device structure for the
 * given set of CPUs and initializes the timer associated with it.  It does not
 * start any injection cycles.
 *
 * Return: NULL if memory allocation fails, idle injection control device
 * pointer on success.
 */
struct idle_inject_device *idle_inject_register(struct cpumask *cpumask)
{
	struct idle_inject_device *ii_dev;
	int cpu, cpu_rb;

	ii_dev = kzalloc(sizeof(*ii_dev) + cpumask_size(), GFP_KERNEL);
	if (!ii_dev)
		return NULL;

	cpumask_copy(to_cpumask(ii_dev->cpumask), cpumask);
	hrtimer_init(&ii_dev->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ii_dev->timer.function = idle_inject_timer_fn;
	ii_dev->latency_us = UINT_MAX;

	for_each_cpu(cpu, to_cpumask(ii_dev->cpumask)) {

		if (per_cpu(idle_inject_device, cpu)) {
			pr_err("cpu%d is already registered\n", cpu);
			goto out_rollback;
		}

		per_cpu(idle_inject_device, cpu) = ii_dev;
	}

	return ii_dev;

out_rollback:
	for_each_cpu(cpu_rb, to_cpumask(ii_dev->cpumask)) {
		if (cpu == cpu_rb)
			break;
		per_cpu(idle_inject_device, cpu_rb) = NULL;
	}

	kfree(ii_dev);

	return NULL;
}

/**
 * idle_inject_unregister - unregister idle injection control device
 * @ii_dev: idle injection control device to unregister
 *
 * The function stops idle injection for the given control device,
 * unregisters its kthreads and frees memory allocated when that device was
 * created.
 */
void idle_inject_unregister(struct idle_inject_device *ii_dev)
{
	unsigned int cpu;

	idle_inject_stop(ii_dev);

	for_each_cpu(cpu, to_cpumask(ii_dev->cpumask))
		per_cpu(idle_inject_device, cpu) = NULL;

	kfree(ii_dev);
}

static struct smp_hotplug_thread idle_inject_threads = {
	.store = &idle_inject_thread.tsk,
	.setup = idle_inject_setup,
	.thread_fn = idle_inject_fn,
	.thread_comm = "idle_inject/%u",
	.thread_should_run = idle_inject_should_run,
};

static int __init idle_inject_init(void)
{
	return smpboot_register_percpu_thread(&idle_inject_threads);
}
early_initcall(idle_inject_init);
