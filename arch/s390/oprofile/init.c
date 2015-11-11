/*
 * S390 Version
 *   Copyright IBM Corp. 2002, 2011
 *   Author(s): Thomas Spatzier (tspat@de.ibm.com)
 *   Author(s): Mahesh Salgaonkar (mahesh@linux.vnet.ibm.com)
 *   Author(s): Heinz Graalfs (graalfs@linux.vnet.ibm.com)
 *   Author(s): Andreas Krebbel (krebbel@linux.vnet.ibm.com)
 *
 * @remark Copyright 2002-2011 OProfile authors
 */

#include <linux/oprofile.h>
#include <linux/perf_event.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <asm/processor.h>
#include <asm/perf_event.h>

#include "../../../drivers/oprofile/oprof.h"

extern void s390_backtrace(struct pt_regs * const regs, unsigned int depth);

#include "hwsampler.h"
#include "op_counter.h"

#define DEFAULT_INTERVAL	4127518

#define DEFAULT_SDBT_BLOCKS	1
#define DEFAULT_SDB_BLOCKS	511

static unsigned long oprofile_hw_interval = DEFAULT_INTERVAL;
static unsigned long oprofile_min_interval;
static unsigned long oprofile_max_interval;

static unsigned long oprofile_sdbt_blocks = DEFAULT_SDBT_BLOCKS;
static unsigned long oprofile_sdb_blocks = DEFAULT_SDB_BLOCKS;

static int hwsampler_enabled;
static int hwsampler_running;	/* start_mutex must be held to change */
static int hwsampler_available;

static struct oprofile_operations timer_ops;

struct op_counter_config counter_config;

enum __force_cpu_type {
	reserved = 0,		/* do not force */
	timer,
};
static int force_cpu_type;

static int set_cpu_type(const char *str, struct kernel_param *kp)
{
	if (!strcmp(str, "timer")) {
		force_cpu_type = timer;
		printk(KERN_INFO "oprofile: forcing timer to be returned "
		                 "as cpu type\n");
	} else {
		force_cpu_type = 0;
	}

	return 0;
}
module_param_call(cpu_type, set_cpu_type, NULL, NULL, 0);
MODULE_PARM_DESC(cpu_type, "Force legacy basic mode sampling"
		           "(report cpu_type \"timer\"");

static int __oprofile_hwsampler_start(void)
{
	int retval;

	retval = hwsampler_allocate(oprofile_sdbt_blocks, oprofile_sdb_blocks);
	if (retval)
		return retval;

	retval = hwsampler_start_all(oprofile_hw_interval);
	if (retval)
		hwsampler_deallocate();

	return retval;
}

static int oprofile_hwsampler_start(void)
{
	int retval;

	hwsampler_running = hwsampler_enabled;

	if (!hwsampler_running)
		return timer_ops.start();

	retval = perf_reserve_sampling();
	if (retval)
		return retval;

	retval = __oprofile_hwsampler_start();
	if (retval)
		perf_release_sampling();

	return retval;
}

static void oprofile_hwsampler_stop(void)
{
	if (!hwsampler_running) {
		timer_ops.stop();
		return;
	}

	hwsampler_stop_all();
	hwsampler_deallocate();
	perf_release_sampling();
	return;
}

/*
 * File ops used for:
 * /dev/oprofile/0/enabled
 * /dev/oprofile/hwsampling/hwsampler  (cpu_type = timer)
 */

static ssize_t hwsampler_read(struct file *file, char __user *buf,
		size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(hwsampler_enabled, buf, count, offset);
}

static ssize_t hwsampler_write(struct file *file, char const __user *buf,
		size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval <= 0)
		return retval;

	if (val != 0 && val != 1)
		return -EINVAL;

	if (oprofile_started)
		/*
		 * save to do without locking as we set
		 * hwsampler_running in start() when start_mutex is
		 * held
		 */
		return -EBUSY;

	hwsampler_enabled = val;

	return count;
}

static const struct file_operations hwsampler_fops = {
	.read		= hwsampler_read,
	.write		= hwsampler_write,
};

/*
 * File ops used for:
 * /dev/oprofile/0/count
 * /dev/oprofile/hwsampling/hw_interval  (cpu_type = timer)
 *
 * Make sure that the value is within the hardware range.
 */

static ssize_t hw_interval_read(struct file *file, char __user *buf,
				size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(oprofile_hw_interval, buf,
					count, offset);
}

static ssize_t hw_interval_write(struct file *file, char const __user *buf,
				 size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;
	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval <= 0)
		return retval;
	if (val < oprofile_min_interval)
		oprofile_hw_interval = oprofile_min_interval;
	else if (val > oprofile_max_interval)
		oprofile_hw_interval = oprofile_max_interval;
	else
		oprofile_hw_interval = val;

	return count;
}

static const struct file_operations hw_interval_fops = {
	.read		= hw_interval_read,
	.write		= hw_interval_write,
};

/*
 * File ops used for:
 * /dev/oprofile/0/event
 * Only a single event with number 0 is supported with this counter.
 *
 * /dev/oprofile/0/unit_mask
 * This is a dummy file needed by the user space tools.
 * No value other than 0 is accepted or returned.
 */

static ssize_t hwsampler_zero_read(struct file *file, char __user *buf,
				    size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(0, buf, count, offset);
}

static ssize_t hwsampler_zero_write(struct file *file, char const __user *buf,
				     size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval <= 0)
		return retval;
	if (val != 0)
		return -EINVAL;
	return count;
}

static const struct file_operations zero_fops = {
	.read		= hwsampler_zero_read,
	.write		= hwsampler_zero_write,
};

/* /dev/oprofile/0/kernel file ops.  */

static ssize_t hwsampler_kernel_read(struct file *file, char __user *buf,
				     size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(counter_config.kernel,
					buf, count, offset);
}

static ssize_t hwsampler_kernel_write(struct file *file, char const __user *buf,
				      size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval <= 0)
		return retval;

	if (val != 0 && val != 1)
		return -EINVAL;

	counter_config.kernel = val;

	return count;
}

static const struct file_operations kernel_fops = {
	.read		= hwsampler_kernel_read,
	.write		= hwsampler_kernel_write,
};

/* /dev/oprofile/0/user file ops. */

static ssize_t hwsampler_user_read(struct file *file, char __user *buf,
				   size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(counter_config.user,
					buf, count, offset);
}

static ssize_t hwsampler_user_write(struct file *file, char const __user *buf,
				    size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval <= 0)
		return retval;

	if (val != 0 && val != 1)
		return -EINVAL;

	counter_config.user = val;

	return count;
}

static const struct file_operations user_fops = {
	.read		= hwsampler_user_read,
	.write		= hwsampler_user_write,
};


/*
 * File ops used for: /dev/oprofile/timer/enabled
 * The value always has to be the inverted value of hwsampler_enabled. So
 * no separate variable is created. That way we do not need locking.
 */

static ssize_t timer_enabled_read(struct file *file, char __user *buf,
				  size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(!hwsampler_enabled, buf, count, offset);
}

static ssize_t timer_enabled_write(struct file *file, char const __user *buf,
				   size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval <= 0)
		return retval;

	if (val != 0 && val != 1)
		return -EINVAL;

	/* Timer cannot be disabled without having hardware sampling.  */
	if (val == 0 && !hwsampler_available)
		return -EINVAL;

	if (oprofile_started)
		/*
		 * save to do without locking as we set
		 * hwsampler_running in start() when start_mutex is
		 * held
		 */
		return -EBUSY;

	hwsampler_enabled = !val;

	return count;
}

static const struct file_operations timer_enabled_fops = {
	.read		= timer_enabled_read,
	.write		= timer_enabled_write,
};


static int oprofile_create_hwsampling_files(struct dentry *root)
{
	struct dentry *dir;

	dir = oprofilefs_mkdir(root, "timer");
	if (!dir)
		return -EINVAL;

	oprofilefs_create_file(dir, "enabled", &timer_enabled_fops);

	if (!hwsampler_available)
		return 0;

	/* reinitialize default values */
	hwsampler_enabled = 1;
	counter_config.kernel = 1;
	counter_config.user = 1;

	if (!force_cpu_type) {
		/*
		 * Create the counter file system.  A single virtual
		 * counter is created which can be used to
		 * enable/disable hardware sampling dynamically from
		 * user space.  The user space will configure a single
		 * counter with a single event.  The value of 'event'
		 * and 'unit_mask' are not evaluated by the kernel code
		 * and can only be set to 0.
		 */

		dir = oprofilefs_mkdir(root, "0");
		if (!dir)
			return -EINVAL;

		oprofilefs_create_file(dir, "enabled", &hwsampler_fops);
		oprofilefs_create_file(dir, "event", &zero_fops);
		oprofilefs_create_file(dir, "count", &hw_interval_fops);
		oprofilefs_create_file(dir, "unit_mask", &zero_fops);
		oprofilefs_create_file(dir, "kernel", &kernel_fops);
		oprofilefs_create_file(dir, "user", &user_fops);
		oprofilefs_create_ulong(dir, "hw_sdbt_blocks",
					&oprofile_sdbt_blocks);

	} else {
		/*
		 * Hardware sampling can be used but the cpu_type is
		 * forced to timer in order to deal with legacy user
		 * space tools.  The /dev/oprofile/hwsampling fs is
		 * provided in that case.
		 */
		dir = oprofilefs_mkdir(root, "hwsampling");
		if (!dir)
			return -EINVAL;

		oprofilefs_create_file(dir, "hwsampler",
				       &hwsampler_fops);
		oprofilefs_create_file(dir, "hw_interval",
				       &hw_interval_fops);
		oprofilefs_create_ro_ulong(dir, "hw_min_interval",
					   &oprofile_min_interval);
		oprofilefs_create_ro_ulong(dir, "hw_max_interval",
					   &oprofile_max_interval);
		oprofilefs_create_ulong(dir, "hw_sdbt_blocks",
					&oprofile_sdbt_blocks);
	}
	return 0;
}

static int oprofile_hwsampler_init(struct oprofile_operations *ops)
{
	/*
	 * Initialize the timer mode infrastructure as well in order
	 * to be able to switch back dynamically.  oprofile_timer_init
	 * is not supposed to fail.
	 */
	if (oprofile_timer_init(ops))
		BUG();

	memcpy(&timer_ops, ops, sizeof(timer_ops));
	ops->create_files = oprofile_create_hwsampling_files;

	/*
	 * If the user space tools do not support newer cpu types,
	 * the force_cpu_type module parameter
	 * can be used to always return \"timer\" as cpu type.
	 */
	if (force_cpu_type != timer) {
		struct cpuid id;

		get_cpu_id (&id);

		switch (id.machine) {
		case 0x2097: case 0x2098: ops->cpu_type = "s390/z10"; break;
		case 0x2817: case 0x2818: ops->cpu_type = "s390/z196"; break;
		case 0x2827: case 0x2828: ops->cpu_type = "s390/zEC12"; break;
		default: return -ENODEV;
		}
	}

	if (hwsampler_setup())
		return -ENODEV;

	/*
	 * Query the range for the sampling interval from the
	 * hardware.
	 */
	oprofile_min_interval = hwsampler_query_min_interval();
	if (oprofile_min_interval == 0)
		return -ENODEV;
	oprofile_max_interval = hwsampler_query_max_interval();
	if (oprofile_max_interval == 0)
		return -ENODEV;

	/* The initial value should be sane */
	if (oprofile_hw_interval < oprofile_min_interval)
		oprofile_hw_interval = oprofile_min_interval;
	if (oprofile_hw_interval > oprofile_max_interval)
		oprofile_hw_interval = oprofile_max_interval;

	printk(KERN_INFO "oprofile: System z hardware sampling "
	       "facility found.\n");

	ops->start = oprofile_hwsampler_start;
	ops->stop = oprofile_hwsampler_stop;

	return 0;
}

static void oprofile_hwsampler_exit(void)
{
	hwsampler_shutdown();
}

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	ops->backtrace = s390_backtrace;

	/*
	 * -ENODEV is not reported to the caller.  The module itself
         * will use the timer mode sampling as fallback and this is
         * always available.
	 */
	hwsampler_available = oprofile_hwsampler_init(ops) == 0;

	return 0;
}

void oprofile_arch_exit(void)
{
	oprofile_hwsampler_exit();
}
