/*
 * Copyright 2014 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/debugfs.h>

#include "snic.h"

/*
 * snic_debugfs_init - Initialize debugfs for snic debug logging
 *
 * Description:
 * When Debugfs is configured this routine sets up fnic debugfs
 * filesystem. If not already created. this routine will crate the
 * fnic directory and statistics directory for trace buffer and
 * stats logging
 */
void snic_debugfs_init(void)
{
	snic_glob->trc_root = debugfs_create_dir("snic", NULL);

	snic_glob->stats_root = debugfs_create_dir("statistics",
						   snic_glob->trc_root);
}

/*
 * snic_debugfs_term - Tear down debugfs intrastructure
 *
 * Description:
 * When Debufs is configured this routine removes debugfs file system
 * elements that are specific to snic
 */
void
snic_debugfs_term(void)
{
	debugfs_remove(snic_glob->stats_root);
	snic_glob->stats_root = NULL;

	debugfs_remove(snic_glob->trc_root);
	snic_glob->trc_root = NULL;
}

/*
 * snic_reset_stats_open - Open the reset_stats file
 */
static int
snic_reset_stats_open(struct inode *inode, struct file *filp)
{
	SNIC_BUG_ON(!inode->i_private);
	filp->private_data = inode->i_private;

	return 0;
}

/*
 * snic_reset_stats_read - Read a reset_stats debugfs file
 * @filp: The file pointer to read from.
 * @ubuf: The buffer tocopy the data to.
 * @cnt: The number of bytes to read.
 * @ppos: The position in the file to start reading frm.
 *
 * Description:
 * This routine reads value of variable reset_stats
 * and stores into local @buf. It will start reading file @ppos and
 * copy up to @cnt of data to @ubuf from @buf.
 *
 * Returns:
 * This function returns the amount of data that was read.
 */
static ssize_t
snic_reset_stats_read(struct file *filp,
		      char __user *ubuf,
		      size_t cnt,
		      loff_t *ppos)
{
	struct snic *snic = (struct snic *) filp->private_data;
	char buf[64];
	int len;

	len = sprintf(buf, "%u\n", snic->reset_stats);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
}

/*
 * snic_reset_stats_write - Write to reset_stats debugfs file
 * @filp: The file pointer to write from
 * @ubuf: The buffer to copy the data from.
 * @cnt: The number of bytes to write.
 * @ppos: The position in the file to start writing to.
 *
 * Description:
 * This routine writes data from user buffer @ubuf to buffer @buf and
 * resets cumulative stats of snic.
 *
 * Returns:
 * This function returns the amount of data that was written.
 */
static ssize_t
snic_reset_stats_write(struct file *filp,
		       const char __user *ubuf,
		       size_t cnt,
		       loff_t *ppos)
{
	struct snic *snic = (struct snic *) filp->private_data;
	struct snic_stats *stats = &snic->s_stats;
	u64 *io_stats_p = (u64 *) &stats->io;
	u64 *fw_stats_p = (u64 *) &stats->fw;
	char buf[64];
	unsigned long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = '\0';

	ret = kstrtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	snic->reset_stats = val;

	if (snic->reset_stats) {
		/* Skip variable is used to avoid descrepancies to Num IOs
		 * and IO Completions stats. Skip incrementing No IO Compls
		 * for pending active IOs after reset_stats
		 */
		atomic64_set(&snic->io_cmpl_skip,
			     atomic64_read(&stats->io.active));
		memset(&stats->abts, 0, sizeof(struct snic_abort_stats));
		memset(&stats->reset, 0, sizeof(struct snic_reset_stats));
		memset(&stats->misc, 0, sizeof(struct snic_misc_stats));
		memset(io_stats_p+1,
			0,
			sizeof(struct snic_io_stats) - sizeof(u64));
		memset(fw_stats_p+1,
			0,
			sizeof(struct snic_fw_stats) - sizeof(u64));
	}

	(*ppos)++;

	SNIC_HOST_INFO(snic->shost, "Reset Op: Driver statistics.\n");

	return cnt;
}

static int
snic_reset_stats_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;

	return 0;
}

/*
 * snic_stats_show - Formats and prints per host specific driver stats.
 */
static int
snic_stats_show(struct seq_file *sfp, void *data)
{
	struct snic *snic = (struct snic *) sfp->private;
	struct snic_stats *stats = &snic->s_stats;
	struct timespec64 last_isr_tms, last_ack_tms;
	u64 maxio_tm;
	int i;

	/* Dump IO Stats */
	seq_printf(sfp,
		   "------------------------------------------\n"
		   "\t\t IO Statistics\n"
		   "------------------------------------------\n");

	maxio_tm = (u64) atomic64_read(&stats->io.max_time);
	seq_printf(sfp,
		   "Active IOs                  : %lld\n"
		   "Max Active IOs              : %lld\n"
		   "Total IOs                   : %lld\n"
		   "IOs Completed               : %lld\n"
		   "IOs Failed                  : %lld\n"
		   "IOs Not Found               : %lld\n"
		   "Memory Alloc Failures       : %lld\n"
		   "REQs Null                   : %lld\n"
		   "SCSI Cmd Pointers Null      : %lld\n"
		   "Max SGL for any IO          : %lld\n"
		   "Max IO Size                 : %lld Sectors\n"
		   "Max Queuing Time            : %lld\n"
		   "Max Completion Time         : %lld\n"
		   "Max IO Process Time(FW)     : %lld (%u msec)\n",
		   (u64) atomic64_read(&stats->io.active),
		   (u64) atomic64_read(&stats->io.max_active),
		   (u64) atomic64_read(&stats->io.num_ios),
		   (u64) atomic64_read(&stats->io.compl),
		   (u64) atomic64_read(&stats->io.fail),
		   (u64) atomic64_read(&stats->io.io_not_found),
		   (u64) atomic64_read(&stats->io.alloc_fail),
		   (u64) atomic64_read(&stats->io.req_null),
		   (u64) atomic64_read(&stats->io.sc_null),
		   (u64) atomic64_read(&stats->io.max_sgl),
		   (u64) atomic64_read(&stats->io.max_io_sz),
		   (u64) atomic64_read(&stats->io.max_qtime),
		   (u64) atomic64_read(&stats->io.max_cmpl_time),
		   maxio_tm,
		   jiffies_to_msecs(maxio_tm));

	seq_puts(sfp, "\nSGL Counters\n");

	for (i = 0; i < SNIC_MAX_SG_DESC_CNT; i++) {
		seq_printf(sfp,
			   "%10lld ",
			   (u64) atomic64_read(&stats->io.sgl_cnt[i]));

		if ((i + 1) % 8 == 0)
			seq_puts(sfp, "\n");
	}

	/* Dump Abort Stats */
	seq_printf(sfp,
		   "\n-------------------------------------------\n"
		   "\t\t Abort Statistics\n"
		   "---------------------------------------------\n");

	seq_printf(sfp,
		   "Aborts                      : %lld\n"
		   "Aborts Fail                 : %lld\n"
		   "Aborts Driver Timeout       : %lld\n"
		   "Abort FW Timeout            : %lld\n"
		   "Abort IO NOT Found          : %lld\n"
		   "Abort Queuing Failed        : %lld\n",
		   (u64) atomic64_read(&stats->abts.num),
		   (u64) atomic64_read(&stats->abts.fail),
		   (u64) atomic64_read(&stats->abts.drv_tmo),
		   (u64) atomic64_read(&stats->abts.fw_tmo),
		   (u64) atomic64_read(&stats->abts.io_not_found),
		   (u64) atomic64_read(&stats->abts.q_fail));

	/* Dump Reset Stats */
	seq_printf(sfp,
		   "\n-------------------------------------------\n"
		   "\t\t Reset Statistics\n"
		   "---------------------------------------------\n");

	seq_printf(sfp,
		   "HBA Resets                  : %lld\n"
		   "HBA Reset Cmpls             : %lld\n"
		   "HBA Reset Fail              : %lld\n",
		   (u64) atomic64_read(&stats->reset.hba_resets),
		   (u64) atomic64_read(&stats->reset.hba_reset_cmpl),
		   (u64) atomic64_read(&stats->reset.hba_reset_fail));

	/* Dump Firmware Stats */
	seq_printf(sfp,
		   "\n-------------------------------------------\n"
		   "\t\t Firmware Statistics\n"
		   "---------------------------------------------\n");

	seq_printf(sfp,
		"Active FW Requests             : %lld\n"
		"Max FW Requests                : %lld\n"
		"FW Out Of Resource Errs        : %lld\n"
		"FW IO Errors                   : %lld\n"
		"FW SCSI Errors                 : %lld\n",
		(u64) atomic64_read(&stats->fw.actv_reqs),
		(u64) atomic64_read(&stats->fw.max_actv_reqs),
		(u64) atomic64_read(&stats->fw.out_of_res),
		(u64) atomic64_read(&stats->fw.io_errs),
		(u64) atomic64_read(&stats->fw.scsi_errs));


	/* Dump Miscellenous Stats */
	seq_printf(sfp,
		   "\n---------------------------------------------\n"
		   "\t\t Other Statistics\n"
		   "\n---------------------------------------------\n");

	jiffies_to_timespec64(stats->misc.last_isr_time, &last_isr_tms);
	jiffies_to_timespec64(stats->misc.last_ack_time, &last_ack_tms);

	seq_printf(sfp,
		   "Last ISR Time               : %llu (%8llu.%09lu)\n"
		   "Last Ack Time               : %llu (%8llu.%09lu)\n"
		   "Ack ISRs                    : %llu\n"
		   "IO Cmpl ISRs                : %llu\n"
		   "Err Notify ISRs             : %llu\n"
		   "Max CQ Entries              : %lld\n"
		   "Data Count Mismatch         : %lld\n"
		   "IOs w/ Timeout Status       : %lld\n"
		   "IOs w/ Aborted Status       : %lld\n"
		   "IOs w/ SGL Invalid Stat     : %lld\n"
		   "WQ Desc Alloc Fail          : %lld\n"
		   "Queue Full                  : %lld\n"
		   "Queue Ramp Up               : %lld\n"
		   "Queue Ramp Down             : %lld\n"
		   "Queue Last Queue Depth      : %lld\n"
		   "Target Not Ready            : %lld\n",
		   (u64) stats->misc.last_isr_time,
		   last_isr_tms.tv_sec, last_isr_tms.tv_nsec,
		   (u64)stats->misc.last_ack_time,
		   last_ack_tms.tv_sec, last_ack_tms.tv_nsec,
		   (u64) atomic64_read(&stats->misc.ack_isr_cnt),
		   (u64) atomic64_read(&stats->misc.cmpl_isr_cnt),
		   (u64) atomic64_read(&stats->misc.errnotify_isr_cnt),
		   (u64) atomic64_read(&stats->misc.max_cq_ents),
		   (u64) atomic64_read(&stats->misc.data_cnt_mismat),
		   (u64) atomic64_read(&stats->misc.io_tmo),
		   (u64) atomic64_read(&stats->misc.io_aborted),
		   (u64) atomic64_read(&stats->misc.sgl_inval),
		   (u64) atomic64_read(&stats->misc.wq_alloc_fail),
		   (u64) atomic64_read(&stats->misc.qfull),
		   (u64) atomic64_read(&stats->misc.qsz_rampup),
		   (u64) atomic64_read(&stats->misc.qsz_rampdown),
		   (u64) atomic64_read(&stats->misc.last_qsz),
		   (u64) atomic64_read(&stats->misc.tgt_not_rdy));

	return 0;
}

/*
 * snic_stats_open - Open the stats file for specific host
 *
 * Description:
 * This routine opens a debugfs file stats of specific host
 */
static int
snic_stats_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, snic_stats_show, inode->i_private);
}

static const struct file_operations snic_stats_fops = {
	.owner	= THIS_MODULE,
	.open	= snic_stats_open,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations snic_reset_stats_fops = {
	.owner = THIS_MODULE,
	.open = snic_reset_stats_open,
	.read = snic_reset_stats_read,
	.write = snic_reset_stats_write,
	.release = snic_reset_stats_release,
};

/*
 * snic_stats_init - Initialize stats struct and create stats file
 * per snic
 *
 * Description:
 * When debugfs is cofigured this routine sets up the stats file per snic
 * It will create file stats and reset_stats under statistics/host# directory
 * to log per snic stats
 */
void snic_stats_debugfs_init(struct snic *snic)
{
	char name[16];

	snprintf(name, sizeof(name), "host%d", snic->shost->host_no);

	snic->stats_host = debugfs_create_dir(name, snic_glob->stats_root);

	snic->stats_file = debugfs_create_file("stats", S_IFREG|S_IRUGO,
					       snic->stats_host, snic,
					       &snic_stats_fops);

	snic->reset_stats_file = debugfs_create_file("reset_stats",
						     S_IFREG|S_IRUGO|S_IWUSR,
						     snic->stats_host, snic,
						     &snic_reset_stats_fops);
}

/*
 * snic_stats_debugfs_remove - Tear down debugfs infrastructure of stats
 *
 * Description:
 * When Debufs is configured this routine removes debugfs file system
 * elements that are specific to to snic stats
 */
void
snic_stats_debugfs_remove(struct snic *snic)
{
	debugfs_remove(snic->stats_file);
	snic->stats_file = NULL;

	debugfs_remove(snic->reset_stats_file);
	snic->reset_stats_file = NULL;

	debugfs_remove(snic->stats_host);
	snic->stats_host = NULL;
}

/* Trace Facility related API */
static void *
snic_trc_seq_start(struct seq_file *sfp, loff_t *pos)
{
	return &snic_glob->trc;
}

static void *
snic_trc_seq_next(struct seq_file *sfp, void *data, loff_t *pos)
{
	return NULL;
}

static void
snic_trc_seq_stop(struct seq_file *sfp, void *data)
{
}

#define SNIC_TRC_PBLEN	256
static int
snic_trc_seq_show(struct seq_file *sfp, void *data)
{
	char buf[SNIC_TRC_PBLEN];

	if (snic_get_trc_data(buf, SNIC_TRC_PBLEN) > 0)
		seq_printf(sfp, "%s\n", buf);

	return 0;
}

static const struct seq_operations snic_trc_sops = {
	.start	= snic_trc_seq_start,
	.next	= snic_trc_seq_next,
	.stop	= snic_trc_seq_stop,
	.show	= snic_trc_seq_show,
};

DEFINE_SEQ_ATTRIBUTE(snic_trc);

/*
 * snic_trc_debugfs_init : creates trace/tracing_enable files for trace
 * under debugfs
 */
void snic_trc_debugfs_init(void)
{
	snic_glob->trc.trc_enable = debugfs_create_bool("tracing_enable",
							S_IFREG | S_IRUGO | S_IWUSR,
							snic_glob->trc_root,
							&snic_glob->trc.enable);

	snic_glob->trc.trc_file = debugfs_create_file("trace",
						      S_IFREG | S_IRUGO | S_IWUSR,
						      snic_glob->trc_root, NULL,
						      &snic_trc_fops);
}

/*
 * snic_trc_debugfs_term : cleans up the files created for trace under debugfs
 */
void
snic_trc_debugfs_term(void)
{
	debugfs_remove(snic_glob->trc.trc_file);
	snic_glob->trc.trc_file = NULL;

	debugfs_remove(snic_glob->trc.trc_enable);
	snic_glob->trc.trc_enable = NULL;
}
