/*
 *  drivers/s390/cio/qdio_perf.c
 *
 *  Copyright IBM Corp. 2008
 *
 *  Author: Jan Glauber (jang@linux.vnet.ibm.com)
 */
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/ccwdev.h>

#include "cio.h"
#include "css.h"
#include "device.h"
#include "ioasm.h"
#include "chsc.h"
#include "qdio_debug.h"
#include "qdio_perf.h"

int qdio_performance_stats;
struct qdio_perf_stats perf_stats;

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *qdio_perf_pde;
#endif

inline void qdio_perf_stat_inc(atomic_long_t *count)
{
	if (qdio_performance_stats)
		atomic_long_inc(count);
}

inline void qdio_perf_stat_dec(atomic_long_t *count)
{
	if (qdio_performance_stats)
		atomic_long_dec(count);
}

/*
 * procfs functions
 */
static int qdio_perf_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "Number of qdio interrupts\t\t\t: %li\n",
		   (long)atomic_long_read(&perf_stats.qdio_int));
	seq_printf(m, "Number of PCI interrupts\t\t\t: %li\n",
		   (long)atomic_long_read(&perf_stats.pci_int));
	seq_printf(m, "Number of adapter interrupts\t\t\t: %li\n",
		   (long)atomic_long_read(&perf_stats.thin_int));
	seq_printf(m, "\n");
	seq_printf(m, "Inbound tasklet runs\t\t\t\t: %li\n",
		   (long)atomic_long_read(&perf_stats.tasklet_inbound));
	seq_printf(m, "Outbound tasklet runs\t\t\t\t: %li\n",
		   (long)atomic_long_read(&perf_stats.tasklet_outbound));
	seq_printf(m, "Adapter interrupt tasklet runs/loops\t\t: %li/%li\n",
		   (long)atomic_long_read(&perf_stats.tasklet_thinint),
		   (long)atomic_long_read(&perf_stats.tasklet_thinint_loop));
	seq_printf(m, "Adapter interrupt inbound tasklet runs/loops\t: %li/%li\n",
		   (long)atomic_long_read(&perf_stats.thinint_inbound),
		   (long)atomic_long_read(&perf_stats.thinint_inbound_loop));
	seq_printf(m, "\n");
	seq_printf(m, "Number of SIGA In issued\t\t\t: %li\n",
		   (long)atomic_long_read(&perf_stats.siga_in));
	seq_printf(m, "Number of SIGA Out issued\t\t\t: %li\n",
		   (long)atomic_long_read(&perf_stats.siga_out));
	seq_printf(m, "Number of SIGA Sync issued\t\t\t: %li\n",
		   (long)atomic_long_read(&perf_stats.siga_sync));
	seq_printf(m, "\n");
	seq_printf(m, "Number of inbound transfers\t\t\t: %li\n",
		   (long)atomic_long_read(&perf_stats.inbound_handler));
	seq_printf(m, "Number of outbound transfers\t\t\t: %li\n",
		   (long)atomic_long_read(&perf_stats.outbound_handler));
	seq_printf(m, "\n");
	seq_printf(m, "Number of fast requeues (outg. SBAL w/o SIGA)\t: %li\n",
		   (long)atomic_long_read(&perf_stats.fast_requeue));
	seq_printf(m, "Number of outbound target full condition\t: %li\n",
		   (long)atomic_long_read(&perf_stats.outbound_target_full));
	seq_printf(m, "Number of outbound tasklet mod_timer calls\t: %li\n",
		   (long)atomic_long_read(&perf_stats.debug_tl_out_timer));
	seq_printf(m, "Number of stop polling calls\t\t\t: %li\n",
		   (long)atomic_long_read(&perf_stats.debug_stop_polling));
	seq_printf(m, "AI inbound tasklet loops after stop polling\t: %li\n",
		   (long)atomic_long_read(&perf_stats.thinint_inbound_loop2));
	seq_printf(m, "QEBSM EQBS total/incomplete\t\t\t: %li/%li\n",
		   (long)atomic_long_read(&perf_stats.debug_eqbs_all),
		   (long)atomic_long_read(&perf_stats.debug_eqbs_incomplete));
	seq_printf(m, "QEBSM SQBS total/incomplete\t\t\t: %li/%li\n",
		   (long)atomic_long_read(&perf_stats.debug_sqbs_all),
		   (long)atomic_long_read(&perf_stats.debug_sqbs_incomplete));
	seq_printf(m, "\n");
	return 0;
}
static int qdio_perf_seq_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, qdio_perf_proc_show, NULL);
}

static struct file_operations qdio_perf_proc_fops = {
	.owner	 = THIS_MODULE,
	.open	 = qdio_perf_seq_open,
	.read	 = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

/*
 * sysfs functions
 */
static ssize_t qdio_perf_stats_show(struct bus_type *bus, char *buf)
{
	return sprintf(buf, "%i\n", qdio_performance_stats ? 1 : 0);
}

static ssize_t qdio_perf_stats_store(struct bus_type *bus,
			      const char *buf, size_t count)
{
	unsigned long i;

	if (strict_strtoul(buf, 16, &i) != 0)
		return -EINVAL;
	if ((i != 0) && (i != 1))
		return -EINVAL;
	if (i == qdio_performance_stats)
		return count;

	qdio_performance_stats = i;
	/* reset performance statistics */
	if (i == 0)
		memset(&perf_stats, 0, sizeof(struct qdio_perf_stats));
	return count;
}

static BUS_ATTR(qdio_performance_stats, 0644, qdio_perf_stats_show,
		qdio_perf_stats_store);

int __init qdio_setup_perf_stats(void)
{
	int rc;

	rc = bus_create_file(&ccw_bus_type, &bus_attr_qdio_performance_stats);
	if (rc)
		return rc;

#ifdef CONFIG_PROC_FS
	memset(&perf_stats, 0, sizeof(struct qdio_perf_stats));
	qdio_perf_pde = proc_create("qdio_perf", S_IFREG | S_IRUGO,
				    NULL, &qdio_perf_proc_fops);
#endif
	return 0;
}

void qdio_remove_perf_stats(void)
{
#ifdef CONFIG_PROC_FS
	remove_proc_entry("qdio_perf", NULL);
#endif
	bus_remove_file(&ccw_bus_type, &bus_attr_qdio_performance_stats);
}
