/*
 * arch/s390/appldata/appldata_os.c
 *
 * Data gathering module for Linux-VM Monitor Stream, Stage 1.
 * Collects misc. OS related data (CPU utilization, running processes).
 *
 * Copyright (C) 2003 IBM Corporation, IBM Deutschland Entwicklung GmbH.
 *
 * Author: Gerald Schaefer <geraldsc@de.ibm.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <asm/smp.h>

#include "appldata.h"


#define MY_PRINT_NAME	"appldata_os"		/* for debug messages, etc. */
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

// New in 2.6 -->
	u32 per_cpu_irq;	/* ... spent in interrupts          */
	u32 per_cpu_softirq;	/* ... spent in softirqs            */
	u32 per_cpu_iowait;	/* ... spent while waiting for I/O  */
// <-- New in 2.6
};

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

// New in 2.6 -->
	u32 nr_iowait;		/* number of blocked threads
				   (waiting for I/O)               */
// <-- New in 2.6

	/* per cpu data */
	struct appldata_os_per_cpu os_cpu[0];
};

static struct appldata_os_data *appldata_os_data;


static inline void appldata_print_debug(struct appldata_os_data *os_data)
{
	int a0, a1, a2, i;

	P_DEBUG("--- OS - RECORD ---\n");
	P_DEBUG("nr_threads   = %u\n", os_data->nr_threads);
	P_DEBUG("nr_running   = %u\n", os_data->nr_running);
	P_DEBUG("nr_iowait    = %u\n", os_data->nr_iowait);
	P_DEBUG("avenrun(int) = %8x / %8x / %8x\n", os_data->avenrun[0],
		os_data->avenrun[1], os_data->avenrun[2]);
	a0 = os_data->avenrun[0];
	a1 = os_data->avenrun[1];
	a2 = os_data->avenrun[2];
	P_DEBUG("avenrun(float) = %d.%02d / %d.%02d / %d.%02d\n",
		LOAD_INT(a0), LOAD_FRAC(a0), LOAD_INT(a1), LOAD_FRAC(a1),
		LOAD_INT(a2), LOAD_FRAC(a2));

	P_DEBUG("nr_cpus = %u\n", os_data->nr_cpus);
	for (i = 0; i < os_data->nr_cpus; i++) {
		P_DEBUG("cpu%u : user = %u, nice = %u, system = %u, "
			"idle = %u, irq = %u, softirq = %u, iowait = %u\n",
				i,
				os_data->os_cpu[i].per_cpu_user,
				os_data->os_cpu[i].per_cpu_nice,
				os_data->os_cpu[i].per_cpu_system,
				os_data->os_cpu[i].per_cpu_idle,
				os_data->os_cpu[i].per_cpu_irq,
				os_data->os_cpu[i].per_cpu_softirq,
				os_data->os_cpu[i].per_cpu_iowait);
	}

	P_DEBUG("sync_count_1 = %u\n", os_data->sync_count_1);
	P_DEBUG("sync_count_2 = %u\n", os_data->sync_count_2);
	P_DEBUG("timestamp    = %lX\n", os_data->timestamp);
}

/*
 * appldata_get_os_data()
 *
 * gather OS data
 */
static void appldata_get_os_data(void *data)
{
	int i, j;
	struct appldata_os_data *os_data;

	os_data = data;
	os_data->sync_count_1++;

	os_data->nr_cpus = num_online_cpus();

	os_data->nr_threads = nr_threads;
	os_data->nr_running = nr_running();
	os_data->nr_iowait  = nr_iowait();
	os_data->avenrun[0] = avenrun[0] + (FIXED_1/200);
	os_data->avenrun[1] = avenrun[1] + (FIXED_1/200);
	os_data->avenrun[2] = avenrun[2] + (FIXED_1/200);

	j = 0;
	for_each_online_cpu(i) {
		os_data->os_cpu[j].per_cpu_user =
					kstat_cpu(i).cpustat.user;
		os_data->os_cpu[j].per_cpu_nice =
					kstat_cpu(i).cpustat.nice;
		os_data->os_cpu[j].per_cpu_system =
					kstat_cpu(i).cpustat.system;
		os_data->os_cpu[j].per_cpu_idle =
					kstat_cpu(i).cpustat.idle;
		os_data->os_cpu[j].per_cpu_irq =
					kstat_cpu(i).cpustat.irq;
		os_data->os_cpu[j].per_cpu_softirq =
					kstat_cpu(i).cpustat.softirq;
		os_data->os_cpu[j].per_cpu_iowait =
					kstat_cpu(i).cpustat.iowait;
		j++;
	}

	os_data->timestamp = get_clock();
	os_data->sync_count_2++;
#ifdef APPLDATA_DEBUG
	appldata_print_debug(os_data);
#endif
}


static struct appldata_ops ops = {
	.ctl_nr    = CTL_APPLDATA_OS,
	.name	   = "os",
	.record_nr = APPLDATA_RECORD_OS_ID,
	.callback  = &appldata_get_os_data,
	.owner     = THIS_MODULE,
};


/*
 * appldata_os_init()
 *
 * init data, register ops
 */
static int __init appldata_os_init(void)
{
	int rc, size;

	size = sizeof(struct appldata_os_data) +
		(NR_CPUS * sizeof(struct appldata_os_per_cpu));
	if (size > APPLDATA_MAX_REC_SIZE) {
		P_ERROR("Size of record = %i, bigger than maximum (%i)!\n",
			size, APPLDATA_MAX_REC_SIZE);
		rc = -ENOMEM;
		goto out;
	}
	P_DEBUG("sizeof(os) = %i, sizeof(os_cpu) = %lu\n", size,
		sizeof(struct appldata_os_per_cpu));

	appldata_os_data = kmalloc(size, GFP_DMA);
	if (appldata_os_data == NULL) {
		P_ERROR("No memory for %s!\n", ops.name);
		rc = -ENOMEM;
		goto out;
	}
	memset(appldata_os_data, 0, size);

	appldata_os_data->per_cpu_size = sizeof(struct appldata_os_per_cpu);
	appldata_os_data->cpu_offset   = offsetof(struct appldata_os_data,
							os_cpu);
	P_DEBUG("cpu offset = %u\n", appldata_os_data->cpu_offset);

	ops.data = appldata_os_data;
	ops.size = size;
	rc = appldata_register_ops(&ops);
	if (rc != 0) {
		P_ERROR("Error registering ops, rc = %i\n", rc);
		kfree(appldata_os_data);
	} else {
		P_DEBUG("%s-ops registered!\n", ops.name);
	}
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
	P_DEBUG("%s-ops unregistered!\n", ops.name);
}


module_init(appldata_os_init);
module_exit(appldata_os_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gerald Schaefer");
MODULE_DESCRIPTION("Linux-VM Monitor Stream, OS statistics");
