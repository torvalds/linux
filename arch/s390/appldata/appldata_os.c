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
			"idle = %u, irq = %u, softirq = %u, iowait = %u, "
			"steal = %u\n",
				os_data->os_cpu[i].cpu_id,
				os_data->os_cpu[i].per_cpu_user,
				os_data->os_cpu[i].per_cpu_nice,
				os_data->os_cpu[i].per_cpu_system,
				os_data->os_cpu[i].per_cpu_idle,
				os_data->os_cpu[i].per_cpu_irq,
				os_data->os_cpu[i].per_cpu_softirq,
				os_data->os_cpu[i].per_cpu_iowait,
				os_data->os_cpu[i].per_cpu_steal);
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
			cputime_to_jiffies(kstat_cpu(i).cpustat.user);
		os_data->os_cpu[j].per_cpu_nice =
			cputime_to_jiffies(kstat_cpu(i).cpustat.nice);
		os_data->os_cpu[j].per_cpu_system =
			cputime_to_jiffies(kstat_cpu(i).cpustat.system);
		os_data->os_cpu[j].per_cpu_idle =
			cputime_to_jiffies(kstat_cpu(i).cpustat.idle);
		os_data->os_cpu[j].per_cpu_irq =
			cputime_to_jiffies(kstat_cpu(i).cpustat.irq);
		os_data->os_cpu[j].per_cpu_softirq =
			cputime_to_jiffies(kstat_cpu(i).cpustat.softirq);
		os_data->os_cpu[j].per_cpu_iowait =
			cputime_to_jiffies(kstat_cpu(i).cpustat.iowait);
		os_data->os_cpu[j].per_cpu_steal =
			cputime_to_jiffies(kstat_cpu(i).cpustat.steal);
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
			if (rc != 0) {
				P_ERROR("os: START NEW DIAG 0xDC failed, "
					"return code: %d, new size = %i\n", rc,
					new_size);
				P_INFO("os: stopping old record now\n");
			} else
				P_INFO("os: new record size = %i\n", new_size);

			rc = appldata_diag(APPLDATA_RECORD_OS_ID,
					   APPLDATA_STOP_REC,
					   (unsigned long) ops.data, ops.size,
					   ops.mod_lvl);
			if (rc != 0)
				P_ERROR("os: STOP OLD DIAG 0xDC failed, "
					"return code: %d, old size = %i\n", rc,
					ops.size);
			else
				P_INFO("os: old record size = %i stopped\n",
					ops.size);
		}
		ops.size = new_size;
	}
	os_data->timestamp = get_clock();
	os_data->sync_count_2++;
#ifdef APPLDATA_DEBUG
	appldata_print_debug(os_data);
#endif
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
		P_ERROR("Max. size of OS record = %i, bigger than maximum "
			"record size (%i)\n", max_size, APPLDATA_MAX_REC_SIZE);
		rc = -ENOMEM;
		goto out;
	}
	P_DEBUG("max. sizeof(os) = %i, sizeof(os_cpu) = %lu\n", max_size,
		sizeof(struct appldata_os_per_cpu));

	appldata_os_data = kzalloc(max_size, GFP_DMA);
	if (appldata_os_data == NULL) {
		P_ERROR("No memory for %s!\n", ops.name);
		rc = -ENOMEM;
		goto out;
	}

	appldata_os_data->per_cpu_size = sizeof(struct appldata_os_per_cpu);
	appldata_os_data->cpu_offset   = offsetof(struct appldata_os_data,
							os_cpu);
	P_DEBUG("cpu offset = %u\n", appldata_os_data->cpu_offset);

	ops.data = appldata_os_data;
	ops.callback  = &appldata_get_os_data;
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
