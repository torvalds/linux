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

#ifndef __SNIC_STATS_H
#define __SNIC_STATS_H

struct snic_io_stats {
	atomic64_t active;		/* Active IOs */
	atomic64_t max_active;		/* Max # active IOs */
	atomic64_t max_sgl;		/* Max # SGLs for any IO */
	atomic64_t max_time;		/* Max time to process IO */
	atomic64_t max_qtime;		/* Max time to Queue the IO */
	atomic64_t max_cmpl_time;	/* Max time to complete the IO */
	atomic64_t sgl_cnt[SNIC_MAX_SG_DESC_CNT]; /* SGL Counters */
	atomic64_t max_io_sz;		/* Max IO Size */
	atomic64_t compl;		/* IO Completions */
	atomic64_t fail;		/* IO Failures */
	atomic64_t req_null;		/* req or req info is NULL */
	atomic64_t alloc_fail;		/* Alloc Failures */
	atomic64_t sc_null;
	atomic64_t io_not_found;	/* IO Not Found */
	atomic64_t num_ios;		/* Number of IOs */
};

struct snic_abort_stats {
	atomic64_t num;		/* Abort counter */
	atomic64_t fail;	/* Abort Failure Counter */
	atomic64_t drv_tmo;	/* Abort Driver Timeouts */
	atomic64_t fw_tmo;	/* Abort Firmware Timeouts */
	atomic64_t io_not_found;/* Abort IO Not Found */
};

struct snic_reset_stats {
	atomic64_t dev_resets;		/* Device Reset Counter */
	atomic64_t dev_reset_fail;	/* Device Reset Failures */
	atomic64_t dev_reset_aborts;	/* Device Reset Aborts */
	atomic64_t dev_reset_tmo;	/* Device Reset Timeout */
	atomic64_t dev_reset_terms;	/* Device Reset terminate */
	atomic64_t hba_resets;		/* hba/firmware resets */
	atomic64_t hba_reset_cmpl;	/* hba/firmware reset completions */
	atomic64_t hba_reset_fail;	/* hba/firmware failures */
	atomic64_t snic_resets;		/* snic resets */
	atomic64_t snic_reset_compl;	/* snic reset completions */
	atomic64_t snic_reset_fail;	/* snic reset failures */
};

struct snic_fw_stats {
	atomic64_t actv_reqs;		/* Active Requests */
	atomic64_t max_actv_reqs;	/* Max Active Requests */
	atomic64_t out_of_res;		/* Firmware Out Of Resources */
	atomic64_t io_errs;		/* Firmware IO Firmware Errors */
	atomic64_t scsi_errs;		/* Target hits check condition */
};

struct snic_misc_stats {
	u64	last_isr_time;
	u64	last_ack_time;
	atomic64_t isr_cnt;
	atomic64_t max_cq_ents;		/* Max CQ Entries */
	atomic64_t data_cnt_mismat;	/* Data Count Mismatch */
	atomic64_t io_tmo;
	atomic64_t io_aborted;
	atomic64_t sgl_inval;		/* SGL Invalid */
	atomic64_t abts_wq_alloc_fail;	/* Abort Path WQ desc alloc failure */
	atomic64_t devrst_wq_alloc_fail;/* Device Reset - WQ desc alloc fail */
	atomic64_t wq_alloc_fail;	/* IO WQ desc alloc failure */
	atomic64_t no_icmnd_itmf_cmpls;
	atomic64_t io_under_run;
	atomic64_t qfull;
	atomic64_t tgt_not_rdy;
};

struct snic_stats {
	struct snic_io_stats io;
	struct snic_abort_stats abts;
	struct snic_reset_stats reset;
	struct snic_fw_stats fw;
	struct snic_misc_stats misc;
	atomic64_t io_cmpl_skip;
};

int snic_stats_debugfs_init(struct snic *);
void snic_stats_debugfs_remove(struct snic *);

/* Auxillary function to update active IO counter */
static inline void
snic_stats_update_active_ios(struct snic_stats *s_stats)
{
	struct snic_io_stats *io = &s_stats->io;
	u32 nr_active_ios;

	nr_active_ios = atomic64_inc_return(&io->active);
	if (atomic64_read(&io->max_active) < nr_active_ios)
		atomic64_set(&io->max_active, nr_active_ios);

	atomic64_inc(&io->num_ios);
}

/* Auxillary function to update IO completion counter */
static inline void
snic_stats_update_io_cmpl(struct snic_stats *s_stats)
{
	atomic64_dec(&s_stats->io.active);
	if (unlikely(atomic64_read(&s_stats->io_cmpl_skip)))
		atomic64_dec(&s_stats->io_cmpl_skip);
	else
		atomic64_inc(&s_stats->io.compl);
}
#endif /* __SNIC_STATS_H */
