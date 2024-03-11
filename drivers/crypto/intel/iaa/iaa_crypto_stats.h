/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2021 Intel Corporation. All rights rsvd. */

#ifndef __CRYPTO_DEV_IAA_CRYPTO_STATS_H__
#define __CRYPTO_DEV_IAA_CRYPTO_STATS_H__

#if defined(CONFIG_CRYPTO_DEV_IAA_CRYPTO_STATS)
int	iaa_crypto_debugfs_init(void);
void	iaa_crypto_debugfs_cleanup(void);

void	update_total_comp_calls(void);
void	update_total_comp_bytes_out(int n);
void	update_total_decomp_calls(void);
void	update_total_sw_decomp_calls(void);
void	update_total_decomp_bytes_in(int n);
void	update_max_comp_delay_ns(u64 start_time_ns);
void	update_max_decomp_delay_ns(u64 start_time_ns);
void	update_max_acomp_delay_ns(u64 start_time_ns);
void	update_max_adecomp_delay_ns(u64 start_time_ns);
void	update_completion_einval_errs(void);
void	update_completion_timeout_errs(void);
void	update_completion_comp_buf_overflow_errs(void);

void	update_wq_comp_calls(struct idxd_wq *idxd_wq);
void	update_wq_comp_bytes(struct idxd_wq *idxd_wq, int n);
void	update_wq_decomp_calls(struct idxd_wq *idxd_wq);
void	update_wq_decomp_bytes(struct idxd_wq *idxd_wq, int n);

#else
static inline int	iaa_crypto_debugfs_init(void) { return 0; }
static inline void	iaa_crypto_debugfs_cleanup(void) {}

static inline void	update_total_comp_calls(void) {}
static inline void	update_total_comp_bytes_out(int n) {}
static inline void	update_total_decomp_calls(void) {}
static inline void	update_total_sw_decomp_calls(void) {}
static inline void	update_total_decomp_bytes_in(int n) {}
static inline void	update_max_comp_delay_ns(u64 start_time_ns) {}
static inline void	update_max_decomp_delay_ns(u64 start_time_ns) {}
static inline void	update_max_acomp_delay_ns(u64 start_time_ns) {}
static inline void	update_max_adecomp_delay_ns(u64 start_time_ns) {}
static inline void	update_completion_einval_errs(void) {}
static inline void	update_completion_timeout_errs(void) {}
static inline void	update_completion_comp_buf_overflow_errs(void) {}

static inline void	update_wq_comp_calls(struct idxd_wq *idxd_wq) {}
static inline void	update_wq_comp_bytes(struct idxd_wq *idxd_wq, int n) {}
static inline void	update_wq_decomp_calls(struct idxd_wq *idxd_wq) {}
static inline void	update_wq_decomp_bytes(struct idxd_wq *idxd_wq, int n) {}

#endif // CONFIG_CRYPTO_DEV_IAA_CRYPTO_STATS

#endif
