/*
 * arch/arm/common/bL_switcher.c -- big.LITTLE cluster switcher core driver
 *
 * Created by:	Nicolas Pitre, March 2012
 * Copyright:	(C) 2012  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * TODO:
 *
 * - Allow the outbound CPU to remain online for the inbound CPU to snoop its
 *   cache for a while.
 * - Perform a switch during initialization to probe what the counterpart
 *   CPU's GIC interface ID is and stop hardcoding them in the code.
 * - Local timers migration (they are not supported at the moment).
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/cpu_pm.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/cpu.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/spinlock.h>

#include <asm/suspend.h>
#include <asm/hardware/gic.h>
#include <asm/bL_switcher.h>
#include <asm/bL_entry.h>

/*
 * Notifier list for kernel code which want to called at switch.
 * This is used to stop a switch. If some driver want to keep doing
 * some work without switch, the driver registers the notifier and
 * the notifier callback deal with refusing a switch and some work.
 */
ATOMIC_NOTIFIER_HEAD(bL_switcher_notifier_list);

int register_bL_swicher_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&bL_switcher_notifier_list, nb);
}

int unregister_bL_swicher_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&bL_switcher_notifier_list, nb);
}

/*
 * Before migrating cpu, swicher core driver ask to some dirver
 * whether carries out a switch or not.
 *
 * Switcher core driver decides to do a switch through return value
 * (-) minus value : refuse a switch
 * (+) plus value : go on a switch
 */
static int bL_enter_migration(void)
{
	return atomic_notifier_call_chain(&bL_switcher_notifier_list, SWITCH_ENTER, NULL);
}

static int bL_exit_migration(void)
{
	return atomic_notifier_call_chain(&bL_switcher_notifier_list, SWITCH_EXIT, NULL);
}

/*
 * Use our own MPIDR accessors as the generic ones in asm/cputype.h have
 * __attribute_const__ and we don't want the compiler to assume any
 * constness here.
 */

static int read_mpidr(void)
{
	unsigned int id;
	asm volatile ("mrc\tp15, 0, %0, c0, c0, 5" : "=r" (id));
	return id;
}

/*
 * bL switcher core code.
 */

const struct bL_power_ops *bL_platform_ops;

extern void setup_mm_for_reboot(void);

typedef void (*phys_reset_t)(unsigned long);

static void bL_do_switch(void *_unused)
{
	unsigned mpidr, cpuid, clusterid, ob_cluster, ib_cluster;
	phys_reset_t phys_reset;

	pr_debug("%s\n", __func__);

	mpidr = read_mpidr();
	cpuid = mpidr & 0xf;
	clusterid = (mpidr >> 8) & 0xf;
	ob_cluster = clusterid;
	ib_cluster = clusterid ^ 1;

	/*
	 * Our state has been saved at this point.  Let's release our
	 * inbound CPU.
	 */
	bL_set_entry_vector(cpuid, ib_cluster, cpu_resume);
	sev();

	/*
	 * From this point, we must assume that our counterpart CPU might
	 * have taken over in its parallel world already, as if execution
	 * just returned from cpu_suspend().  It is therefore important to
	 * be very careful not to make any change the other guy is not
	 * expecting.  This is why we need stack isolation.
	 *
	 * Also, because of this special stack, we cannot rely on anything
	 * that expects a valid 'current' pointer.  For example, printk()
	 * may give bogus "BUG: recent printk recursion!\n" messages
	 * because of that.
	 */
	bL_platform_ops->power_down(cpuid, ob_cluster);

	/*
	 * Hey, we're not dead!  This means a request to switch back
	 * has come from our counterpart and reset was deasserted before
	 * we had the chance to enter WFI.  Let's turn off the MMU and
	 * branch back directly through our kernel entry point.
	 */
	setup_mm_for_reboot();
	phys_reset = (phys_reset_t)(unsigned long)virt_to_phys(cpu_reset);
	phys_reset(virt_to_phys(bl_entry_point));

	/* should never get here */
	BUG();
}

/*
 * Stack isolation (size needs to be optimized)
 */

static unsigned long __attribute__((__aligned__(L1_CACHE_BYTES)))
	stacks[BL_CPUS_PER_CLUSTER][BL_NR_CLUSTERS][128];

extern void call_with_stack(void (*fn)(void *), void *arg, void *sp);

static int bL_switchpoint(unsigned long _unused)
{
	unsigned int mpidr = read_mpidr();
	unsigned int cpuid = mpidr & 0xf;
	unsigned int clusterid = (mpidr >> 8) & 0xf;
	void *stack = stacks[cpuid][clusterid] + ARRAY_SIZE(stacks[0][0]);
	call_with_stack(bL_do_switch, NULL, stack);
	BUG();

	/*
	 * For removing warning message of compiler, the statement of return
	 * is added, but this return is nothing to this function.
	 */
	return 0;
}

/*
 * Generic switcher interface
 */

static DEFINE_SPINLOCK(switch_gic_lock);

/*
 * bL_switch_to - Switch to a specific cluster for the current CPU
 * @new_cluster_id: the ID of the cluster to switch to.
 *
 * This function must be called on the CPU to be switched.
 * Returns 0 on success, else a negative status code.
 */
static int bL_switch_to(unsigned int new_cluster_id)
{
	unsigned int mpidr, cpuid, clusterid, ob_cluster, ib_cluster;
	int ret = 0;

	mpidr = read_mpidr();
	cpuid = mpidr & 0xf;
	clusterid = (mpidr >> 8) & 0xf;
	ob_cluster = clusterid;
	ib_cluster = clusterid ^ 1;

	if (new_cluster_id == clusterid)
		return 0;

	if (!bL_platform_ops)
		return -ENOSYS;

	pr_debug("before switch: CPU %d in cluster %d\n", cpuid, clusterid);

	/* Close the gate for our entry vectors */
	bL_set_entry_vector(cpuid, ob_cluster, NULL);
	bL_set_entry_vector(cpuid, ib_cluster, NULL);

	/*
	 * From this point we are entering the switch critical zone
	 * and can't sleep/schedule anymore.
	 */
	local_irq_disable();
	local_fiq_disable();

	/*
	 * Get spin_lock to protect concurrent accesses of GIC registers
	 * from both NWd(gic_migrate_target) and SWd(SMC of bL_power_up).
	 */
	spin_lock(&switch_gic_lock);

	/*
	 * Let's wake up the inbound CPU now in case it requires some delay
	 * to come online, but leave it gated in our entry vector code.
	 */
	bL_platform_ops->power_up(cpuid, ib_cluster);

	/* redirect GIC's SGIs to our counterpart */
	gic_migrate_target(cpuid + ib_cluster*4);

	/*
	 * Raise a SGI on the inbound CPU to make sure it doesn't stall
	 * in a possible WFI, such as the one in bL_do_switch().
	 */
	arm_send_ping_ipi(smp_processor_id());

	spin_unlock(&switch_gic_lock);

	ret = cpu_pm_enter();
	if (ret)
		goto out;

	/* Let's do the actual CPU switch. */
	ret = cpu_suspend((unsigned long)NULL, bL_switchpoint);
	if (ret > 0)
		ret = -EINVAL;

	/* We are executing on the inbound CPU at this point */
	mpidr = read_mpidr();
	cpuid = mpidr & 0xf;
	clusterid = (mpidr >> 8) & 0xf;
	pr_debug("after switch: CPU %d in cluster %d\n", cpuid, clusterid);
	BUG_ON(clusterid != ib_cluster);

	bL_platform_ops->inbound_setup(cpuid, !clusterid);
	ret = cpu_pm_exit();

out:
	local_fiq_enable();
	local_irq_enable();

	if (ret)
		pr_err("%s exiting with error %d\n", __func__, ret);
	return ret;
}

struct bL_thread {
	struct task_struct *task;
	wait_queue_head_t wq;
	int wanted_cluster;
};

static struct bL_thread bL_threads[BL_CPUS_PER_CLUSTER];

static int switch_ready = -1;
static DEFINE_SPINLOCK(switch_ready_lock);
#define BL_TIMEOUT_NS 50000000
static int bL_switcher_thread(void *arg)
{
	struct bL_thread *t = arg;
	struct sched_param param = { .sched_priority = 1 };
	int ret;

	sched_setscheduler_nocheck(current, SCHED_FIFO, &param);

	do {
		ret = wait_event_interruptible(t->wq, t->wanted_cluster != -1);
		if (!ret) {
			int cluster = t->wanted_cluster;
#ifdef CONFIG_EXYNOS5_CCI
			t->wanted_cluster = -1;
			bL_switch_to(cluster);
#else
			static atomic_t switch_ready_cnt = ATOMIC_INIT(0);
			unsigned long long start = sched_clock();
			unsigned int cpuid = get_cpu();
			signed long long wait_time = 0;

			atomic_inc(&switch_ready_cnt);
			dmb();

			spin_lock(&switch_ready_lock);
			if (switch_ready < 0) {
				while (atomic_read(&switch_ready_cnt) <
					num_online_cpus()) {
					wait_time = sched_clock() - start;
					if ((wait_time > BL_TIMEOUT_NS) ||
						(wait_time < 0))
						break;
				}

				if (wait_time > BL_TIMEOUT_NS) {
					switch_ready = 0;
					pr_info("%s: aborted on CPU %d by timeout (%ld msecs)\n",
					__func__, cpuid,
					(int) wait_time / NSEC_PER_MSEC);
				} else if (wait_time < 0) {
					switch_ready = 0;
					pr_info("%s: sched_clock is reversed\n",
					__func__);
				} else {
					switch_ready = 1;
				}
			}
			spin_unlock(&switch_ready_lock);

			atomic_dec(&switch_ready_cnt);

			t->wanted_cluster = -1;

			spin_lock(&switch_ready_lock);
			if (switch_ready == 1) {
				spin_unlock(&switch_ready_lock);
				/* condition met before timeout */
				bL_switch_to(cluster);
			} else {
				spin_unlock(&switch_ready_lock);
			}

			put_cpu();
#endif
		}
	} while (!kthread_should_stop());

	return ret;
}

static int __init bL_switcher_thread_create(unsigned int cpu, struct bL_thread *t)
{
	t->task = kthread_create_on_node(bL_switcher_thread, t,
					 cpu_to_node(cpu),
					 "kswitcher_%d", cpu);
	if (IS_ERR(t->task)) {
		pr_err("%s failed for CPU %d\n", __func__, cpu);
		return PTR_ERR(t->task);
	}
	kthread_bind(t->task, cpu);
	init_waitqueue_head(&t->wq);
	t->wanted_cluster = -1;
	wake_up_process(t->task);

	return 0;
}

static unsigned int switch_operation = 0x11;

/*
 * bL_check_auto_switcher_enable - check whether enable or disable switch
 * automatically
 */
bool bL_check_auto_switcher_enable(void)
{
	bool result = true;

	if (switch_operation != 0x11)
		result = false;

	return result;
}

/*
 * bL_switch_request - Switch to a specific cluster for the given CPU
 *
 * @cpu: the CPU to switch
 * @new_cluster_id: the ID of the cluster to switch to.
 *
 * This function causes a cluster switch on the given CPU.  If the given
 * CPU is the same as the calling CPU then the switch happens right away.
 * Otherwise the request is put on a work queue to be scheduled on the
 * remote CPU.
 */
void bL_switch_request(unsigned int cpu, unsigned int new_cluster_id)
{
	struct bL_thread *t;

	if (switch_operation == 0x00)
		return;

	if (cpu >= BL_CPUS_PER_CLUSTER) {
		pr_err("%s: cpu %d out of bounds\n", __func__, cpu);
		return;
	}

	t = &bL_threads[cpu];
	if (IS_ERR_OR_NULL(t->task)) {
		pr_err("%s: cpu %d out of bounds\n", __func__, cpu);
		return;
	}

	t->wanted_cluster = new_cluster_id;
	wake_up(&t->wq);
}

EXPORT_SYMBOL_GPL(bL_switch_request);

int bL_cluster_switch_request(unsigned int new_cluster)
{
	struct bL_thread *t;
	int cpu;
	int ret;

	BUG_ON(new_cluster >= 2);

	if (unlikely(switch_operation == 0x00))
		return -EPERM;

	get_online_cpus();

	spin_lock(&switch_ready_lock);
	switch_ready = -1;
	spin_unlock(&switch_ready_lock);

	local_irq_disable();
	if (bL_enter_migration() < 0) {
		local_irq_enable();
		put_online_cpus();
		return -EBUSY;
	}

	for (cpu = BL_CPUS_PER_CLUSTER - 1; cpu >= 0; cpu--) {
		if (unlikely(!cpu_online(cpu)))
			continue;

		t = &bL_threads[cpu];

		if (unlikely(IS_ERR_OR_NULL(t->task))) {
			pr_err("%s: cpu %d out of bounds\n", __func__, cpu);
			local_irq_enable();
			put_online_cpus();
			return -EINVAL;
		}

		t->wanted_cluster = new_cluster;
		wake_up(&t->wq);
		smp_send_reschedule(cpu);
	}
	local_irq_enable();
	schedule();
	put_online_cpus();

	bL_exit_migration();

	ret = ((read_mpidr() >> 8) & 0xf) == new_cluster ? 0 : -EAGAIN;
	return ret;
}
EXPORT_SYMBOL_GPL(bL_cluster_switch_request);

#ifdef CONFIG_BL_SWITCHER_DUMMY_IF

/*
 * Dummy interface to user space (to be replaced by cpufreq based interface).
 */

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>

static ssize_t bL_switcher_write(struct file *file, const char __user *buf,
			size_t len, loff_t *pos)
{
	unsigned char val[3];
	unsigned int cpu, cluster;

	pr_debug("%s\n", __func__);

	if (len < 3)
		return -EINVAL;

	if (copy_from_user(val, buf, 3))
		return -EFAULT;

	/* format: <cpu#>,<cluster#> */
	if (val[0] < '0' || val[0] > '4' ||
	    val[1] != ',' ||
	    val[2] < '0' || val[2] > '1')
		return -EINVAL;

	cpu = val[0] - '0';
	cluster = val[2] - '0';

	if (cpu_online(cpu))
		bL_switch_request(cpu, cluster);
	return len;
}

static const struct file_operations bL_switcher_fops = {
	.write		= bL_switcher_write,
	.owner	= THIS_MODULE,
};

static struct miscdevice bL_switcher_device = {
	MISC_DYNAMIC_MINOR,
	"b.L_switcher",
	&bL_switcher_fops
};

static ssize_t bL_operator_write(struct file *file, const char __user *buf,
				 size_t len, loff_t *pos)
{
	char val[2];
	unsigned int loop;

	if (copy_from_user(val, buf, 2))
		return -EINVAL;

	if (val[0] < '0' || val[0] > '1')
		goto cmd_err;
	else if (val[1] < '0' || val[1] > '1')
		goto cmd_err;

	if (!strncmp(val, "00", 2)) {
		pr_info("Disable switcher\n");
		switch_operation = 0x00;
		goto end;
	}

	if (!strncmp(val, "01", 2)) {
		pr_info("LITTLE only\n");
		switch_operation = 0x01;
		for (loop = 0; loop < BL_CPUS_PER_CLUSTER; loop++) {
			if (bL_running_cluster_num_cpus(loop) == 0)
				bL_switch_request(loop, 1);
		}
		goto end;
	}

	if (!strncmp(val, "10", 2)) {
		pr_info("big only\n");
		switch_operation = 0x10;
		for (loop = 0; loop < BL_CPUS_PER_CLUSTER; loop++) {
			if (bL_running_cluster_num_cpus(loop) == 1)
				bL_switch_request(loop, 0);
		}
		goto end;
	}

	if (!strncmp(val, "11", 2)) {
		pr_info("big.LITTLE(switcher enable)\n");
		switch_operation = 0x11;
		goto end;
	}

cmd_err:
	pr_info("Usage: command > /dev/bL_operator\n"
		"command : 00 - switch disable\n"
		"	   01 - LITTLE only\n"
		"	   10 - big only\n"
		"	   11 - big.LITTLE\n"
		"echo 10 > /dev/bL_operator\n");
end:
	return len;
}

static ssize_t bL_operator_read(struct file *file, char __user *buf,
				size_t len, loff_t *pos)
{
	char buff[20];
	size_t count = 0;

	switch (switch_operation) {
	case 0x00:
		count += sprintf(buff, "Disable switcher\n");
		break;
	case 0x01:
		count += sprintf(buff, "LITTLE only\n");
		break;
	case 0x10:
		count += sprintf(buff, "big only\n");
		break;
	case 0x11:
		count += sprintf(buff, "big.LITTLE\n");
		break;
	default:
		count += sprintf(buff, "Not support operation mode\n");
		break;
	}

	return simple_read_from_buffer(buf, len, pos, buff, count);
}

static const struct file_operations bL_operator_fops = {
	.write		= bL_operator_write,
	.read		= bL_operator_read,
	.owner		= THIS_MODULE,
};

static struct miscdevice bL_operator_device = {
	MISC_DYNAMIC_MINOR,
	"b.L_operator",
	&bL_operator_fops
};

#endif

static void __init switcher_thread_on_each_cpu(struct work_struct *work)
{
	unsigned int mpidr, cluster, cpuid;
	mpidr = read_mpidr();
	cluster = (mpidr >> 8) & 0xf;
	cpuid = mpidr & 0xf;

	BUG_ON(cluster > 2 || cpuid > 4);
	pr_debug("create switcher thread %d(%d)\n", cpuid, cluster);

	bL_switcher_thread_create(cpuid, &bL_threads[cpuid]);
}

int __init bL_switcher_init(const struct bL_power_ops *ops)
{
	int ret, err;

	pr_info("big.LITTLE switcher initializing\n");

	ret = bL_cluster_sync_init(ops);
	if (ret)
		return ret;

	bL_platform_ops = ops;
#ifdef CONFIG_BL_SWITCHER_DUMMY_IF
	err = misc_register(&bL_switcher_device);
	if (err) {
		pr_err("Switcher device is not registered "
			"so user can not execute the manual switch");
		return err;
	}

	err = misc_register(&bL_operator_device);
	if (err) {
		pr_err("Switcher operation device is not registerd "
		       "so bL_operation is not accessed\n");
		return err;
	}
#endif
	schedule_on_each_cpu(switcher_thread_on_each_cpu);
	pr_info("big.LITTLE switcher initialized\n");
	return 0;
}
