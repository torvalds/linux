// SPDX-License-Identifier: GPL-2.0-only
/*
 * salinfo.c
 *
 * Creates entries in /proc/sal for various system features.
 *
 * Copyright (c) 2003, 2006 Silicon Graphics, Inc.  All rights reserved.
 * Copyright (c) 2003 Hewlett-Packard Co
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * 10/30/2001	jbarnes@sgi.com		copied much of Stephane's palinfo
 *					code to create this file
 * Oct 23 2003	kaos@sgi.com
 *   Replace IPI with set_cpus_allowed() to read a record from the required cpu.
 *   Redesign salinfo log processing to separate interrupt and user space
 *   contexts.
 *   Cache the record across multi-block reads from user space.
 *   Support > 64 cpus.
 *   Delete module_exit and MOD_INC/DEC_COUNT, salinfo cannot be a module.
 *
 * Jan 28 2004	kaos@sgi.com
 *   Periodically check for outstanding MCA or INIT records.
 *
 * Dec  5 2004	kaos@sgi.com
 *   Standardize which records are cleared automatically.
 *
 * Aug 18 2005	kaos@sgi.com
 *   mca.c may not pass a buffer, a NULL buffer just indicates that a new
 *   record is available in SAL.
 *   Replace some NR_CPUS by cpus_online, for hotplug cpu.
 *
 * Jan  5 2006        kaos@sgi.com
 *   Handle hotplug cpus coming online.
 *   Handle hotplug cpus going offline while they still have outstanding records.
 *   Use the cpu_* macros consistently.
 *   Replace the counting semaphore with a mutex and a test if the cpumask is non-empty.
 *   Modify the locking to make the test for "work to do" an atomic operation.
 */

#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/semaphore.h>

#include <asm/sal.h>
#include <linux/uaccess.h>

MODULE_AUTHOR("Jesse Barnes <jbarnes@sgi.com>");
MODULE_DESCRIPTION("/proc interface to IA-64 SAL features");
MODULE_LICENSE("GPL");

typedef struct {
	const char		*name;		/* name of the proc entry */
	unsigned long           feature;        /* feature bit */
	struct proc_dir_entry	*entry;		/* registered entry (removal) */
} salinfo_entry_t;

/*
 * List {name,feature} pairs for every entry in /proc/sal/<feature>
 * that this module exports
 */
static const salinfo_entry_t salinfo_entries[]={
	{ "bus_lock",           IA64_SAL_PLATFORM_FEATURE_BUS_LOCK, },
	{ "irq_redirection",	IA64_SAL_PLATFORM_FEATURE_IRQ_REDIR_HINT, },
	{ "ipi_redirection",	IA64_SAL_PLATFORM_FEATURE_IPI_REDIR_HINT, },
	{ "itc_drift",		IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT, },
};

#define NR_SALINFO_ENTRIES ARRAY_SIZE(salinfo_entries)

static char *salinfo_log_name[] = {
	"mca",
	"init",
	"cmc",
	"cpe",
};

static struct proc_dir_entry *salinfo_proc_entries[
	ARRAY_SIZE(salinfo_entries) +			/* /proc/sal/bus_lock */
	ARRAY_SIZE(salinfo_log_name) +			/* /proc/sal/{mca,...} */
	(2 * ARRAY_SIZE(salinfo_log_name)) +		/* /proc/sal/mca/{event,data} */
	1];						/* /proc/sal */

/* Some records we get ourselves, some are accessed as saved data in buffers
 * that are owned by mca.c.
 */
struct salinfo_data_saved {
	u8*			buffer;
	u64			size;
	u64			id;
	int			cpu;
};

/* State transitions.  Actions are :-
 *   Write "read <cpunum>" to the data file.
 *   Write "clear <cpunum>" to the data file.
 *   Write "oemdata <cpunum> <offset> to the data file.
 *   Read from the data file.
 *   Close the data file.
 *
 * Start state is NO_DATA.
 *
 * NO_DATA
 *    write "read <cpunum>" -> NO_DATA or LOG_RECORD.
 *    write "clear <cpunum>" -> NO_DATA or LOG_RECORD.
 *    write "oemdata <cpunum> <offset> -> return -EINVAL.
 *    read data -> return EOF.
 *    close -> unchanged.  Free record areas.
 *
 * LOG_RECORD
 *    write "read <cpunum>" -> NO_DATA or LOG_RECORD.
 *    write "clear <cpunum>" -> NO_DATA or LOG_RECORD.
 *    write "oemdata <cpunum> <offset> -> format the oem data, goto OEMDATA.
 *    read data -> return the INIT/MCA/CMC/CPE record.
 *    close -> unchanged.  Keep record areas.
 *
 * OEMDATA
 *    write "read <cpunum>" -> NO_DATA or LOG_RECORD.
 *    write "clear <cpunum>" -> NO_DATA or LOG_RECORD.
 *    write "oemdata <cpunum> <offset> -> format the oem data, goto OEMDATA.
 *    read data -> return the formatted oemdata.
 *    close -> unchanged.  Keep record areas.
 *
 * Closing the data file does not change the state.  This allows shell scripts
 * to manipulate salinfo data, each shell redirection opens the file, does one
 * action then closes it again.  The record areas are only freed at close when
 * the state is NO_DATA.
 */
enum salinfo_state {
	STATE_NO_DATA,
	STATE_LOG_RECORD,
	STATE_OEMDATA,
};

struct salinfo_data {
	cpumask_t		cpu_event;	/* which cpus have outstanding events */
	wait_queue_head_t	read_wait;
	u8			*log_buffer;
	u64			log_size;
	u8			*oemdata;	/* decoded oem data */
	u64			oemdata_size;
	int			open;		/* single-open to prevent races */
	u8			type;
	u8			saved_num;	/* using a saved record? */
	enum salinfo_state	state :8;	/* processing state */
	u8			padding;
	int			cpu_check;	/* next CPU to check */
	struct salinfo_data_saved data_saved[5];/* save last 5 records from mca.c, must be < 255 */
};

static struct salinfo_data salinfo_data[ARRAY_SIZE(salinfo_log_name)];

static DEFINE_SPINLOCK(data_lock);
static DEFINE_SPINLOCK(data_saved_lock);

/** salinfo_platform_oemdata - optional callback to decode oemdata from an error
 * record.
 * @sect_header: pointer to the start of the section to decode.
 * @oemdata: returns vmalloc area containing the decoded output.
 * @oemdata_size: returns length of decoded output (strlen).
 *
 * Description: If user space asks for oem data to be decoded by the kernel
 * and/or prom and the platform has set salinfo_platform_oemdata to the address
 * of a platform specific routine then call that routine.  salinfo_platform_oemdata
 * vmalloc's and formats its output area, returning the address of the text
 * and its strlen.  Returns 0 for success, -ve for error.  The callback is
 * invoked on the cpu that generated the error record.
 */
int (*salinfo_platform_oemdata)(const u8 *sect_header, u8 **oemdata, u64 *oemdata_size);

struct salinfo_platform_oemdata_parms {
	const u8 *efi_guid;
	u8 **oemdata;
	u64 *oemdata_size;
};

static long
salinfo_platform_oemdata_cpu(void *context)
{
	struct salinfo_platform_oemdata_parms *parms = context;

	return salinfo_platform_oemdata(parms->efi_guid, parms->oemdata, parms->oemdata_size);
}

static void
shift1_data_saved (struct salinfo_data *data, int shift)
{
	memcpy(data->data_saved+shift, data->data_saved+shift+1,
	       (ARRAY_SIZE(data->data_saved) - (shift+1)) * sizeof(data->data_saved[0]));
	memset(data->data_saved + ARRAY_SIZE(data->data_saved) - 1, 0,
	       sizeof(data->data_saved[0]));
}

/* This routine is invoked in interrupt context.  Note: mca.c enables
 * interrupts before calling this code for CMC/CPE.  MCA and INIT events are
 * not irq safe, do not call any routines that use spinlocks, they may deadlock.
 * MCA and INIT records are recorded, a timer event will look for any
 * outstanding events and wake up the user space code.
 *
 * The buffer passed from mca.c points to the output from ia64_log_get. This is
 * a persistent buffer but its contents can change between the interrupt and
 * when user space processes the record.  Save the record id to identify
 * changes.  If the buffer is NULL then just update the bitmap.
 */
void
salinfo_log_wakeup(int type, u8 *buffer, u64 size, int irqsafe)
{
	struct salinfo_data *data = salinfo_data + type;
	struct salinfo_data_saved *data_saved;
	unsigned long flags = 0;
	int i;
	int saved_size = ARRAY_SIZE(data->data_saved);

	BUG_ON(type >= ARRAY_SIZE(salinfo_log_name));

	if (irqsafe)
		spin_lock_irqsave(&data_saved_lock, flags);
	if (buffer) {
		for (i = 0, data_saved = data->data_saved; i < saved_size; ++i, ++data_saved) {
			if (!data_saved->buffer)
				break;
		}
		if (i == saved_size) {
			if (!data->saved_num) {
				shift1_data_saved(data, 0);
				data_saved = data->data_saved + saved_size - 1;
			} else
				data_saved = NULL;
		}
		if (data_saved) {
			data_saved->cpu = smp_processor_id();
			data_saved->id = ((sal_log_record_header_t *)buffer)->id;
			data_saved->size = size;
			data_saved->buffer = buffer;
		}
	}
	cpumask_set_cpu(smp_processor_id(), &data->cpu_event);
	if (irqsafe) {
		wake_up_interruptible(&data->read_wait);
		spin_unlock_irqrestore(&data_saved_lock, flags);
	}
}

/* Check for outstanding MCA/INIT records every minute (arbitrary) */
#define SALINFO_TIMER_DELAY (60*HZ)
static struct timer_list salinfo_timer;
extern void ia64_mlogbuf_dump(void);

static void
salinfo_timeout_check(struct salinfo_data *data)
{
	if (!data->open)
		return;
	if (!cpumask_empty(&data->cpu_event))
		wake_up_interruptible(&data->read_wait);
}

static void
salinfo_timeout(struct timer_list *unused)
{
	ia64_mlogbuf_dump();
	salinfo_timeout_check(salinfo_data + SAL_INFO_TYPE_MCA);
	salinfo_timeout_check(salinfo_data + SAL_INFO_TYPE_INIT);
	salinfo_timer.expires = jiffies + SALINFO_TIMER_DELAY;
	add_timer(&salinfo_timer);
}

static int
salinfo_event_open(struct inode *inode, struct file *file)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

static ssize_t
salinfo_event_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct salinfo_data *data = PDE_DATA(file_inode(file));
	char cmd[32];
	size_t size;
	int i, n, cpu = -1;

retry:
	if (cpumask_empty(&data->cpu_event)) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(data->read_wait,
					     !cpumask_empty(&data->cpu_event)))
			return -EINTR;
	}

	n = data->cpu_check;
	for (i = 0; i < nr_cpu_ids; i++) {
		if (cpumask_test_cpu(n, &data->cpu_event)) {
			if (!cpu_online(n)) {
				cpumask_clear_cpu(n, &data->cpu_event);
				continue;
			}
			cpu = n;
			break;
		}
		if (++n == nr_cpu_ids)
			n = 0;
	}

	if (cpu == -1)
		goto retry;

	ia64_mlogbuf_dump();

	/* for next read, start checking at next CPU */
	data->cpu_check = cpu;
	if (++data->cpu_check == nr_cpu_ids)
		data->cpu_check = 0;

	snprintf(cmd, sizeof(cmd), "read %d\n", cpu);

	size = strlen(cmd);
	if (size > count)
		size = count;
	if (copy_to_user(buffer, cmd, size))
		return -EFAULT;

	return size;
}

static const struct proc_ops salinfo_event_proc_ops = {
	.proc_open	= salinfo_event_open,
	.proc_read	= salinfo_event_read,
	.proc_lseek	= noop_llseek,
};

static int
salinfo_log_open(struct inode *inode, struct file *file)
{
	struct salinfo_data *data = PDE_DATA(inode);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	spin_lock(&data_lock);
	if (data->open) {
		spin_unlock(&data_lock);
		return -EBUSY;
	}
	data->open = 1;
	spin_unlock(&data_lock);

	if (data->state == STATE_NO_DATA &&
	    !(data->log_buffer = vmalloc(ia64_sal_get_state_info_size(data->type)))) {
		data->open = 0;
		return -ENOMEM;
	}

	return 0;
}

static int
salinfo_log_release(struct inode *inode, struct file *file)
{
	struct salinfo_data *data = PDE_DATA(inode);

	if (data->state == STATE_NO_DATA) {
		vfree(data->log_buffer);
		vfree(data->oemdata);
		data->log_buffer = NULL;
		data->oemdata = NULL;
	}
	spin_lock(&data_lock);
	data->open = 0;
	spin_unlock(&data_lock);
	return 0;
}

static long
salinfo_log_read_cpu(void *context)
{
	struct salinfo_data *data = context;
	sal_log_record_header_t *rh;
	data->log_size = ia64_sal_get_state_info(data->type, (u64 *) data->log_buffer);
	rh = (sal_log_record_header_t *)(data->log_buffer);
	/* Clear corrected errors as they are read from SAL */
	if (rh->severity == sal_log_severity_corrected)
		ia64_sal_clear_state_info(data->type);
	return 0;
}

static void
salinfo_log_new_read(int cpu, struct salinfo_data *data)
{
	struct salinfo_data_saved *data_saved;
	unsigned long flags;
	int i;
	int saved_size = ARRAY_SIZE(data->data_saved);

	data->saved_num = 0;
	spin_lock_irqsave(&data_saved_lock, flags);
retry:
	for (i = 0, data_saved = data->data_saved; i < saved_size; ++i, ++data_saved) {
		if (data_saved->buffer && data_saved->cpu == cpu) {
			sal_log_record_header_t *rh = (sal_log_record_header_t *)(data_saved->buffer);
			data->log_size = data_saved->size;
			memcpy(data->log_buffer, rh, data->log_size);
			barrier();	/* id check must not be moved */
			if (rh->id == data_saved->id) {
				data->saved_num = i+1;
				break;
			}
			/* saved record changed by mca.c since interrupt, discard it */
			shift1_data_saved(data, i);
			goto retry;
		}
	}
	spin_unlock_irqrestore(&data_saved_lock, flags);

	if (!data->saved_num)
		work_on_cpu_safe(cpu, salinfo_log_read_cpu, data);
	if (!data->log_size) {
		data->state = STATE_NO_DATA;
		cpumask_clear_cpu(cpu, &data->cpu_event);
	} else {
		data->state = STATE_LOG_RECORD;
	}
}

static ssize_t
salinfo_log_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct salinfo_data *data = PDE_DATA(file_inode(file));
	u8 *buf;
	u64 bufsize;

	if (data->state == STATE_LOG_RECORD) {
		buf = data->log_buffer;
		bufsize = data->log_size;
	} else if (data->state == STATE_OEMDATA) {
		buf = data->oemdata;
		bufsize = data->oemdata_size;
	} else {
		buf = NULL;
		bufsize = 0;
	}
	return simple_read_from_buffer(buffer, count, ppos, buf, bufsize);
}

static long
salinfo_log_clear_cpu(void *context)
{
	struct salinfo_data *data = context;

	ia64_sal_clear_state_info(data->type);
	return 0;
}

static int
salinfo_log_clear(struct salinfo_data *data, int cpu)
{
	sal_log_record_header_t *rh;
	unsigned long flags;
	spin_lock_irqsave(&data_saved_lock, flags);
	data->state = STATE_NO_DATA;
	if (!cpumask_test_cpu(cpu, &data->cpu_event)) {
		spin_unlock_irqrestore(&data_saved_lock, flags);
		return 0;
	}
	cpumask_clear_cpu(cpu, &data->cpu_event);
	if (data->saved_num) {
		shift1_data_saved(data, data->saved_num - 1);
		data->saved_num = 0;
	}
	spin_unlock_irqrestore(&data_saved_lock, flags);
	rh = (sal_log_record_header_t *)(data->log_buffer);
	/* Corrected errors have already been cleared from SAL */
	if (rh->severity != sal_log_severity_corrected)
		work_on_cpu_safe(cpu, salinfo_log_clear_cpu, data);
	/* clearing a record may make a new record visible */
	salinfo_log_new_read(cpu, data);
	if (data->state == STATE_LOG_RECORD) {
		spin_lock_irqsave(&data_saved_lock, flags);
		cpumask_set_cpu(cpu, &data->cpu_event);
		wake_up_interruptible(&data->read_wait);
		spin_unlock_irqrestore(&data_saved_lock, flags);
	}
	return 0;
}

static ssize_t
salinfo_log_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct salinfo_data *data = PDE_DATA(file_inode(file));
	char cmd[32];
	size_t size;
	u32 offset;
	int cpu;

	size = sizeof(cmd);
	if (count < size)
		size = count;
	if (copy_from_user(cmd, buffer, size))
		return -EFAULT;

	if (sscanf(cmd, "read %d", &cpu) == 1) {
		salinfo_log_new_read(cpu, data);
	} else if (sscanf(cmd, "clear %d", &cpu) == 1) {
		int ret;
		if ((ret = salinfo_log_clear(data, cpu)))
			count = ret;
	} else if (sscanf(cmd, "oemdata %d %d", &cpu, &offset) == 2) {
		if (data->state != STATE_LOG_RECORD && data->state != STATE_OEMDATA)
			return -EINVAL;
		if (offset > data->log_size - sizeof(efi_guid_t))
			return -EINVAL;
		data->state = STATE_OEMDATA;
		if (salinfo_platform_oemdata) {
			struct salinfo_platform_oemdata_parms parms = {
				.efi_guid = data->log_buffer + offset,
				.oemdata = &data->oemdata,
				.oemdata_size = &data->oemdata_size
			};
			count = work_on_cpu_safe(cpu, salinfo_platform_oemdata_cpu,
						 &parms);
		} else
			data->oemdata_size = 0;
	} else
		return -EINVAL;

	return count;
}

static const struct proc_ops salinfo_data_proc_ops = {
	.proc_open	= salinfo_log_open,
	.proc_release	= salinfo_log_release,
	.proc_read	= salinfo_log_read,
	.proc_write	= salinfo_log_write,
	.proc_lseek	= default_llseek,
};

static int salinfo_cpu_online(unsigned int cpu)
{
	unsigned int i, end = ARRAY_SIZE(salinfo_data);
	struct salinfo_data *data;

	spin_lock_irq(&data_saved_lock);
	for (i = 0, data = salinfo_data; i < end; ++i, ++data) {
		cpumask_set_cpu(cpu, &data->cpu_event);
		wake_up_interruptible(&data->read_wait);
	}
	spin_unlock_irq(&data_saved_lock);
	return 0;
}

static int salinfo_cpu_pre_down(unsigned int cpu)
{
	unsigned int i, end = ARRAY_SIZE(salinfo_data);
	struct salinfo_data *data;

	spin_lock_irq(&data_saved_lock);
	for (i = 0, data = salinfo_data; i < end; ++i, ++data) {
		struct salinfo_data_saved *data_saved;
		int j = ARRAY_SIZE(data->data_saved) - 1;

		for (data_saved = data->data_saved + j; j >= 0;
		     --j, --data_saved) {
			if (data_saved->buffer && data_saved->cpu == cpu)
				shift1_data_saved(data, j);
		}
		cpumask_clear_cpu(cpu, &data->cpu_event);
	}
	spin_unlock_irq(&data_saved_lock);
	return 0;
}

/*
 * 'data' contains an integer that corresponds to the feature we're
 * testing
 */
static int __maybe_unused proc_salinfo_show(struct seq_file *m, void *v)
{
	unsigned long data = (unsigned long)v;
	seq_puts(m, (sal_platform_features & data) ? "1\n" : "0\n");
	return 0;
}

static int __init
salinfo_init(void)
{
	struct proc_dir_entry *salinfo_dir; /* /proc/sal dir entry */
	struct proc_dir_entry **sdir = salinfo_proc_entries; /* keeps track of every entry */
	struct proc_dir_entry *dir, *entry;
	struct salinfo_data *data;
	int i;

	salinfo_dir = proc_mkdir("sal", NULL);
	if (!salinfo_dir)
		return 0;

	for (i=0; i < NR_SALINFO_ENTRIES; i++) {
		/* pass the feature bit in question as misc data */
		*sdir++ = proc_create_single_data(salinfo_entries[i].name, 0,
				salinfo_dir, proc_salinfo_show,
				(void *)salinfo_entries[i].feature);
	}

	for (i = 0; i < ARRAY_SIZE(salinfo_log_name); i++) {
		data = salinfo_data + i;
		data->type = i;
		init_waitqueue_head(&data->read_wait);
		dir = proc_mkdir(salinfo_log_name[i], salinfo_dir);
		if (!dir)
			continue;

		entry = proc_create_data("event", S_IRUSR, dir,
					 &salinfo_event_proc_ops, data);
		if (!entry)
			continue;
		*sdir++ = entry;

		entry = proc_create_data("data", S_IRUSR | S_IWUSR, dir,
					 &salinfo_data_proc_ops, data);
		if (!entry)
			continue;
		*sdir++ = entry;

		*sdir++ = dir;
	}

	*sdir++ = salinfo_dir;

	timer_setup(&salinfo_timer, salinfo_timeout, 0);
	salinfo_timer.expires = jiffies + SALINFO_TIMER_DELAY;
	add_timer(&salinfo_timer);

	i = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "ia64/salinfo:online",
			      salinfo_cpu_online, salinfo_cpu_pre_down);
	WARN_ON(i < 0);
	return 0;
}

module_init(salinfo_init);
