/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RAID5_LOG_H
#define _RAID5_LOG_H

extern int r5l_init_log(struct r5conf *conf, struct md_rdev *rdev);
extern void r5l_exit_log(struct r5conf *conf);
extern int r5l_write_stripe(struct r5l_log *log, struct stripe_head *head_sh);
extern void r5l_write_stripe_run(struct r5l_log *log);
extern void r5l_flush_stripe_to_raid(struct r5l_log *log);
extern void r5l_stripe_write_finished(struct stripe_head *sh);
extern int r5l_handle_flush_request(struct r5l_log *log, struct bio *bio);
extern void r5l_quiesce(struct r5l_log *log, int state);
extern bool r5l_log_disk_error(struct r5conf *conf);
extern bool r5c_is_writeback(struct r5l_log *log);
extern int
r5c_try_caching_write(struct r5conf *conf, struct stripe_head *sh,
		      struct stripe_head_state *s, int disks);
extern void
r5c_finish_stripe_write_out(struct r5conf *conf, struct stripe_head *sh,
			    struct stripe_head_state *s);
extern void r5c_release_extra_page(struct stripe_head *sh);
extern void r5c_use_extra_page(struct stripe_head *sh);
extern void r5l_wake_reclaim(struct r5l_log *log, sector_t space);
extern void r5c_handle_cached_data_endio(struct r5conf *conf,
	struct stripe_head *sh, int disks);
extern int r5c_cache_data(struct r5l_log *log, struct stripe_head *sh);
extern void r5c_make_stripe_write_out(struct stripe_head *sh);
extern void r5c_flush_cache(struct r5conf *conf, int num);
extern void r5c_check_stripe_cache_usage(struct r5conf *conf);
extern void r5c_check_cached_full_stripe(struct r5conf *conf);
extern struct md_sysfs_entry r5c_journal_mode;
extern void r5c_update_on_rdev_error(struct mddev *mddev,
				     struct md_rdev *rdev);
extern bool r5c_big_stripe_cached(struct r5conf *conf, sector_t sect);

extern struct dma_async_tx_descriptor *
ops_run_partial_parity(struct stripe_head *sh, struct raid5_percpu *percpu,
		       struct dma_async_tx_descriptor *tx);
extern int ppl_init_log(struct r5conf *conf);
extern void ppl_exit_log(struct r5conf *conf);
extern int ppl_write_stripe(struct r5conf *conf, struct stripe_head *sh);
extern void ppl_write_stripe_run(struct r5conf *conf);
extern void ppl_stripe_write_finished(struct stripe_head *sh);
extern int ppl_modify_log(struct r5conf *conf, struct md_rdev *rdev, bool add);

static inline bool raid5_has_ppl(struct r5conf *conf)
{
	return test_bit(MD_HAS_PPL, &conf->mddev->flags);
}

static inline int log_stripe(struct stripe_head *sh, struct stripe_head_state *s)
{
	struct r5conf *conf = sh->raid_conf;

	if (conf->log) {
		if (!test_bit(STRIPE_R5C_CACHING, &sh->state)) {
			/* writing out phase */
			if (s->waiting_extra_page)
				return 0;
			return r5l_write_stripe(conf->log, sh);
		} else if (test_bit(STRIPE_LOG_TRAPPED, &sh->state)) {
			/* caching phase */
			return r5c_cache_data(conf->log, sh);
		}
	} else if (raid5_has_ppl(conf)) {
		return ppl_write_stripe(conf, sh);
	}

	return -EAGAIN;
}

static inline void log_stripe_write_finished(struct stripe_head *sh)
{
	struct r5conf *conf = sh->raid_conf;

	if (conf->log)
		r5l_stripe_write_finished(sh);
	else if (raid5_has_ppl(conf))
		ppl_stripe_write_finished(sh);
}

static inline void log_write_stripe_run(struct r5conf *conf)
{
	if (conf->log)
		r5l_write_stripe_run(conf->log);
	else if (raid5_has_ppl(conf))
		ppl_write_stripe_run(conf);
}

static inline void log_exit(struct r5conf *conf)
{
	if (conf->log)
		r5l_exit_log(conf);
	else if (raid5_has_ppl(conf))
		ppl_exit_log(conf);
}

static inline int log_init(struct r5conf *conf, struct md_rdev *journal_dev,
			   bool ppl)
{
	if (journal_dev)
		return r5l_init_log(conf, journal_dev);
	else if (ppl)
		return ppl_init_log(conf);

	return 0;
}

static inline int log_modify(struct r5conf *conf, struct md_rdev *rdev, bool add)
{
	if (raid5_has_ppl(conf))
		return ppl_modify_log(conf, rdev, add);

	return 0;
}

#endif
