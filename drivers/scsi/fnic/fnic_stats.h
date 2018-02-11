/*
 * Copyright 2013 Cisco Systems, Inc.  All rights reserved.
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
#ifndef _FNIC_STATS_H_
#define _FNIC_STATS_H_

struct stats_timestamps {
	struct timespec last_reset_time;
	struct timespec last_read_time;
};

struct io_path_stats {
	atomic64_t active_ios;
	atomic64_t max_active_ios;
	atomic64_t io_completions;
	atomic64_t io_failures;
	atomic64_t ioreq_null;
	atomic64_t alloc_failures;
	atomic64_t sc_null;
	atomic64_t io_not_found;
	atomic64_t num_ios;
	atomic64_t io_btw_0_to_10_msec;
	atomic64_t io_btw_10_to_100_msec;
	atomic64_t io_btw_100_to_500_msec;
	atomic64_t io_btw_500_to_5000_msec;
	atomic64_t io_btw_5000_to_10000_msec;
	atomic64_t io_btw_10000_to_30000_msec;
	atomic64_t io_greater_than_30000_msec;
	atomic64_t current_max_io_time;
};

struct abort_stats {
	atomic64_t aborts;
	atomic64_t abort_failures;
	atomic64_t abort_drv_timeouts;
	atomic64_t abort_fw_timeouts;
	atomic64_t abort_io_not_found;
	atomic64_t abort_issued_btw_0_to_6_sec;
	atomic64_t abort_issued_btw_6_to_20_sec;
	atomic64_t abort_issued_btw_20_to_30_sec;
	atomic64_t abort_issued_btw_30_to_40_sec;
	atomic64_t abort_issued_btw_40_to_50_sec;
	atomic64_t abort_issued_btw_50_to_60_sec;
	atomic64_t abort_issued_greater_than_60_sec;
};

struct terminate_stats {
	atomic64_t terminates;
	atomic64_t max_terminates;
	atomic64_t terminate_drv_timeouts;
	atomic64_t terminate_fw_timeouts;
	atomic64_t terminate_io_not_found;
	atomic64_t terminate_failures;
};

struct reset_stats {
	atomic64_t device_resets;
	atomic64_t device_reset_failures;
	atomic64_t device_reset_aborts;
	atomic64_t device_reset_timeouts;
	atomic64_t device_reset_terminates;
	atomic64_t fw_resets;
	atomic64_t fw_reset_completions;
	atomic64_t fw_reset_failures;
	atomic64_t fnic_resets;
	atomic64_t fnic_reset_completions;
	atomic64_t fnic_reset_failures;
};

struct fw_stats {
	atomic64_t active_fw_reqs;
	atomic64_t max_fw_reqs;
	atomic64_t fw_out_of_resources;
	atomic64_t io_fw_errs;
};

struct vlan_stats {
	atomic64_t vlan_disc_reqs;
	atomic64_t resp_withno_vlanID;
	atomic64_t sol_expiry_count;
	atomic64_t flogi_rejects;
};

struct misc_stats {
	u64 last_isr_time;
	u64 last_ack_time;
	atomic64_t isr_count;
	atomic64_t max_cq_entries;
	atomic64_t ack_index_out_of_range;
	atomic64_t data_count_mismatch;
	atomic64_t fcpio_timeout;
	atomic64_t fcpio_aborted;
	atomic64_t sgl_invalid;
	atomic64_t mss_invalid;
	atomic64_t abts_cpwq_alloc_failures;
	atomic64_t devrst_cpwq_alloc_failures;
	atomic64_t io_cpwq_alloc_failures;
	atomic64_t no_icmnd_itmf_cmpls;
	atomic64_t check_condition;
	atomic64_t queue_fulls;
	atomic64_t rport_not_ready;
	atomic64_t frame_errors;
};

struct fnic_stats {
	struct stats_timestamps stats_timestamps;
	struct io_path_stats io_stats;
	struct abort_stats abts_stats;
	struct terminate_stats term_stats;
	struct reset_stats reset_stats;
	struct fw_stats fw_stats;
	struct vlan_stats vlan_stats;
	struct misc_stats misc_stats;
};

struct stats_debug_info {
	char *debug_buffer;
	void *i_private;
	int buf_size;
	int buffer_len;
};

int fnic_get_stats_data(struct stats_debug_info *, struct fnic_stats *);
int fnic_stats_debugfs_init(struct fnic *);
void fnic_stats_debugfs_remove(struct fnic *);
#endif /* _FNIC_STATS_H_ */
