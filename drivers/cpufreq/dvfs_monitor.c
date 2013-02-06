#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/atomic.h>
#include <linux/tick.h>

struct cpufreq_load_data {
	cputime64_t prev_idle;
	cputime64_t prev_wall;
	unsigned char load;
};

struct dvfs_data {
	atomic_t opened;
	atomic_t num_events;
	unsigned char cpus[NR_CPUS];
	unsigned int prev_freq[NR_CPUS];
	unsigned int freq[NR_CPUS];
	struct cpufreq_load_data load_data[NR_CPUS];
	wait_queue_head_t wait_queue;
	spinlock_t load_lock;
};

static struct dvfs_data *dvfs_info;

static void init_dvfs_mon(void)
{
	int cpu;
	int cur_freq = cpufreq_get(0);

	for_each_possible_cpu(cpu) {
		dvfs_info->cpus[cpu] = cpu_online(cpu);
		dvfs_info->freq[cpu] = cur_freq;
	}
	atomic_set(&dvfs_info->num_events, 1);
}

static void calculate_load(void)
{
	int cpu;
	cputime64_t cur_wall, cur_idle;
	cputime64_t prev_wall, prev_idle;
	unsigned int wall_time, idle_time;
	unsigned long flags;

	spin_lock_irqsave(&dvfs_info->load_lock, flags);
	for_each_online_cpu(cpu) {
		cur_idle = get_cpu_idle_time_us(cpu, &cur_wall);
		prev_idle = dvfs_info->load_data[cpu].prev_idle;
		prev_wall = dvfs_info->load_data[cpu].prev_wall;

		dvfs_info->load_data[cpu].prev_idle = cur_idle;
		dvfs_info->load_data[cpu].prev_wall = cur_wall;

		idle_time = (unsigned int)cputime64_sub(cur_idle, prev_idle);
		wall_time = (unsigned int)cputime64_sub(cur_wall, prev_wall);

		if (wall_time < idle_time) {
			pr_err("%s walltime < idletime\n", __func__);
			dvfs_info->load_data[cpu].load = 0;
		}

		dvfs_info->load_data[cpu].load = (wall_time - idle_time) * 100
			/ wall_time;
	}
	spin_unlock_irqrestore(&dvfs_info->load_lock, flags);
	return;
}

static int dvfs_monitor_trans(struct notifier_block *nb,
			      unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;

	if (val != CPUFREQ_POSTCHANGE)
		return 0;

	if (freq->new == freq->old)
		return 0;

	dvfs_info->prev_freq[freq->cpu] = freq->old;
	dvfs_info->freq[freq->cpu] = freq->new;

	calculate_load();

	atomic_inc(&dvfs_info->num_events);
	wake_up_interruptible(&dvfs_info->wait_queue);

	return 0;
}

static int __cpuinit dvfs_monitor_hotplug(struct notifier_block *nb,
					  unsigned long action,
					  void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	int cpu_status = 0;

	switch (action) {
	case CPU_ONLINE:
		cpu_status = 1;
		break;
	case CPU_DOWN_PREPARE:
		cpu_status = 0;
		break;
	default:
		return NOTIFY_OK;
	}

	dvfs_info->cpus[cpu] = cpu_status;
	atomic_inc(&dvfs_info->num_events);
	calculate_load();
	wake_up_interruptible(&dvfs_info->wait_queue);

	return NOTIFY_OK;
}

static struct notifier_block notifier_trans_block = {
	.notifier_call = dvfs_monitor_trans,
};

static struct notifier_block notifier_hotplug_block __refdata = {
	.notifier_call = dvfs_monitor_hotplug,
	.priority = 1,
};

static int dvfs_mon_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	if (atomic_xchg(&dvfs_info->opened, 1) != 0)
		return -EBUSY;

	init_dvfs_mon();
	ret = cpufreq_register_notifier(&notifier_trans_block,
				  CPUFREQ_TRANSITION_NOTIFIER);
	if (ret)
		return ret;

	register_hotcpu_notifier(&notifier_hotplug_block);

	return 0;
}

static int dvfs_mon_release(struct inode *inode, struct file *file)
{
	int ret = 0;

	atomic_dec(&dvfs_info->opened);
	ret = cpufreq_unregister_notifier(&notifier_trans_block,
				    CPUFREQ_TRANSITION_NOTIFIER);
	unregister_hotcpu_notifier(&notifier_hotplug_block);

	return ret;
}

static ssize_t dvfs_mon_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	unsigned long long t;
	unsigned long nanosec_rem;
	int freq, prev_freq;
	char cpu_status[NR_CPUS * 8 + 1];
	char temp[3];
	int i;

	wait_event_interruptible(dvfs_info->wait_queue,
				 atomic_read(&dvfs_info->num_events));

	atomic_set(&dvfs_info->num_events, 0);

	/* for now, assume that all cores run on same speed */
	freq = dvfs_info->freq[0];
	prev_freq = dvfs_info->prev_freq[0];
	dvfs_info->prev_freq[0] = freq;

	memset(cpu_status, 0, sizeof(cpu_status));
	for (i = 0; i != num_possible_cpus(); ++i) {
		unsigned char load = dvfs_info->cpus[i] ?
			dvfs_info->load_data[i].load : 0;
		sprintf(temp, "(%d,%3d),", dvfs_info->cpus[i], load);
		strcat(cpu_status, temp);
	}

	t = cpu_clock(0);
	nanosec_rem = do_div(t, 1000000000);

	return sprintf(buf, "%lu.%06lu,%s%d,%d\n",
		       (unsigned long) t, nanosec_rem / 1000,
		       cpu_status, prev_freq, freq);
}

static const struct file_operations dvfs_mon_operations = {
	.read = dvfs_mon_read,
	.open = dvfs_mon_open,
	.release = dvfs_mon_release,
};

static int __init dvfs_monitor_init(void)
{
	dvfs_info = kzalloc(sizeof(struct dvfs_data), GFP_KERNEL);
	if (dvfs_info == NULL) {
		pr_err("[DVFS_MON] cannot allocate memory\n");
		return -ENOMEM;
	}

	spin_lock_init(&dvfs_info->load_lock);

	init_waitqueue_head(&dvfs_info->wait_queue);

	proc_create("dvfs_mon", S_IRUSR, NULL, &dvfs_mon_operations);

	return 0;
}
late_initcall(dvfs_monitor_init);

static void __exit dvfs_monitor_exit(void)
{
	kfree(dvfs_info);
	return;
}
module_exit(dvfs_monitor_exit);

MODULE_AUTHOR("ByungChang Cha <bc.cha@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DVFS Monitoring proc file");
