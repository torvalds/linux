/*
 * arch/s390/appldata/appldata_os.c
 *
 * Data gathering module for Linux-VM Monitor Stream, Stage 1.
 * Collects misc. OS related data (CPU utilization, running processes).
 *
 * Copyright (C) 2003,2006 IBM Corporation, IBM Deutschland Entwicklung GmbH.
 *
 * Author: Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#define KMSG_COMPONENT	"appldata"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <asm/appldata.h>
#include <asm/smp.h>

#include "appldata.h"


#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)

/*
 * OS data
 *
 * This is accessed as binary data by z/VM. If changes to it can't be avoided,
 * the structure version (product ID, see appldata_base.c) needs to be changed
 * as well and all documentation and z/VM applications using it must be
 * updated.
 *
 * The record layout is documented in the Linux for zSeries Device Drivers
 * book:
 * http://oss.software.ibm.com/developerworks/opensource/linux390/index.shtml
 */
struct appldata_os_per_cpu {
	u32 per_cpu_user;	/* timer ticks spent in user mode   */
	u32 per_cpu_nice;	/* ... spent with modified priority */
	u32 per_cpu_system;	/* ... spent in kernel mode         */
	u32 per_cpu_idle;	/* ... spent in idle mode           */

	/* New in 2.6 */
	u32 per_cpu_irq;	/* ... spent in interrupts          */
	u32 per_cpu_softirq;	/* ... spent in softirqs            */
	u32 per_cpu_iowait;	/* ... spent while waiting for I/O  */

	/* New in modification level 01 */
	u32 per_cpu_steal;	/* ... stolen by hypervisor	    */
	u32 cpu_id;		/* number of this CPU		    */
} __attribute__((packed));

struct appldata_os_data {
	u64 timestamp;
	u32 sync_count_1;	/* after VM collected the record data, */
	u32 sync_count_2;	/* sync_count_1 and sync_count_2 should be the
				   same. If not, the record has been updated on
				   the Linux side while VM was collecting the
				   (possibly corrupt) data */

	u32 nr_cpus;		/* number of (virtual) CPUs        */
	u32 per_cpu_size;	/* size of the per-cpu data struct */
	u32 cpu_offset;		/* offset of the first per-cpu data struct */

	u32 nr_running;		/* number of runnable threads      */
	u32 nr_threads;		/* number of threads               */
	u32 avenrun[3];		/* average nr. of running processes during */
				/* the last 1, 5 and 15 minutes */

	/* New in 2.6 */
	u32 nr_iowait;		/* number of blocked threads
				   (waiting for I/O)               */

	/* per cpu data */
	struct appldata_os_per_cpu os_cpu[0];
} __attribute__((packed));

static struct appldata_os_data *appldata_os_data;

static struct appldata_ops ops = {
	.name	   = "os",
	.record_nr = APPLDATA_RECORD_OS_ID,
	.owner	   = THIS_MODULE,
	.mod_lvl   = {0xF0, 0xF1},		/* EBCDIC "01" */
};


/*
 * appldata_get_os_data()
 *
 * gather OS data
 */
static void appldata_get_os_data(void *data)
{
	int i, j, rc;
	struct appldata_os_data *os_data;
	unsigned int new_size;

	os_data = data;
	os_data->sync_count_1++;

	os_data->nr_threads = nr_threads;
	os_data->nr_running = nr_running();
	os_data->nr_iowait  = nr_iowait();
	os_data->avenrun[0] = avenrun[0] + (FIXED_1/200);
	os_data->avenrun[1] = avenrun[1] + (FIXED_1/200);
	os_data->avenrun[2] = avenrun[2] + (FIXED_1/200);

	j = 0;
	for_each_online_cpu(i) {
		os_data->os_cpu[j].per_cpu_user =
			cputime_to_jiffies(kcpustat_cpu(i).cpustat[CPUTIME_USER]);
		os_data->os_cpu[j].per_cpu_nice =
			cputime_to_jiffies(kcpustat_cpu(i).cpustat[CPUTIME_NICE]);
		os_data->os_cpu[j].per_cpu_system =
			cputime_to_jiffies(kcpustat_cpu(i).cpustat[CPUTIME_SYSTEM]);
		os_data->os_cpu[j].per_cpu_idle =
			cputime_to_jiffies(kcpustat_cpu(i).cpustat[CPUTIME_IDLE]);
		os_data->os_cpu[j].per_cpu_irq =
			cputime_to_jiffies(kcpustat_cpu(i).cpustat[CPUTIME_IRQ]);
		os_data->os_cpu[j].per_cpu_softirq =
			cputime_to_jiffies(kcpustat_cpu(i).cpustat[CPUTIME_SOFTIRQ]);
		os_data->os_cpu[j].per_cpu_iowait =
			cputime_to_jiffies(kcpustat_cpu(i).cpustat[CPUTIME_IOWAIT]);
		os_data->os_cpu[j].per_cpu_steal =
			cputime_to_jiffies(kcpustat_cpu(i).cpustat[CPUTIME_STEAL]);
		os_data->os_cpu[j].cpu_id = i;
		j++;
	}

	os_data->nr_cpus = j;

	new_size = sizeof(struct appldata_os_data) +
		   (os_data->nr_cpus * sizeof(struct appldata_os_per_cpu));
	if (ops.size != new_size) {
		if (ops.active) {
			rc = appldata_diag(APPLDATA_RECORD_OS_ID,
					   APPLDATA_START_INTERVAL_REC,
					   (unsigned long) ops.data, new_size,
					   ops.mod_lvl);
			if (rc != 0)
				pr_err("Starting a new OS data collection "
				       "failed with rc=%d\n", rc);

			rc = appldata_diag(APPLDATA_RECORD_OS_ID,
					   APPLDATA_STOP_REC,
					   (unsigned long) ops.data, ops.size,
					   ops.mod_lvl);
			if (rc != 0)
				pr_err("Stopping a faulty OS data "
				       "collection failed with rc=%d\n", rc);
		}
		ops.size = new_size;
	}
	os_data->timestamp = get_clock();
	os_data->sync_count_2++;
}


/*
 * appldata_os_init()
 *
 * init data, register ops
 */
static int __init appldata_os_init(void)
{
	int rc, max_size;

	max_size = sizeof(struct appldata_os_data) +
		   (NR_CPUS * sizeof(struct appldata_os_per_cpu));
	if (max_size > APPLDATA_MAX_REC_SIZE) {
		pr_err("Maximum OS record size %i exceeds the maximum "
		       "record size %i\n", max_size, APPLDATA_MAX_REC_SIZE);
		rc = -ENOMEM;
		goto out;
	}

	appldata_os_data = kzalloc(max_size, GFP_KERNEL | GFP_DMA);
	if (appldata_os_data == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	appldata_os_data->per_cpu_size = sizeof(struct appldata_os_per_cpu);
	appldata_os_data->cpu_offset   = offsetof(struct appldata_os_data,
							os_cpu);

	ops.data = appldata_os_data;
	ops.callback  = &appldata_get_os_data;
	rc = appldata_register_ops(&ops);
	if (rc != 0)
		kfree(appldata_os_data);
out:
	return rc;
}

/*
 * appldata_os_exit()
 *
 * unregister ops
 */
static void __exit appldata_os_exit(void)
{
	appldata_unregister_ops(&ops);
	kfree(appldata_os_data);
}


module_init(appldata_os_init);
module_exit(appldata_os_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gerald Schaefer");
MODULE_DESCRIPTION("Linux-VM Monitor Stream, OS statistics");
