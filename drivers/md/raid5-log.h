/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RAID5_LOG_H
#define _RAID5_LOG_H

int r5l_init_log(struct r5conf *conf, struct md_rdev *rdev);
void r5l_exit_log(struct r5conf *conf);
int r5l_write_stripe(struct r5l_log *log, struct stripe_head *head_sh);
void r5l_write_stripe_run(struct r5l_log *log);
void r5l_flush_stripe_to_raid(struct r5l_log *log);
void r5l_stripe_write_finished(struct stripe_head *sh);
int r5l_handle_flush_request(struct r5l_log *log, struct bio *bio);
void r5l_quiesce(struct r5l_log *log, int quiesce);
bool r5l_log_disk_error(struct r5conf *conf);
bool r5c_is_writeback(struct r5l_log *log);
int r5c_try_caching_write(struct r5conf *conf, struct stripe_head *sh,
			  struct stripe_head_state *s, int disks);
void r5c_finish_stripe_write_out(struct r5conf *conf, struct stripe_head *sh,
				 struct stripe_head_state *s);
void r5c_release_extra_page(struct stripe_head *sh);
void r5c_use_extra_page(struct stripe_head *sh);
void r5l_wake_reclaim(struct r5l_log *log, sector_t space);
void r5c_handle_cached_data_endio(struct r5conf *conf,
				  struct stripe_head *sh, int disks);
int r5c_cache_data(struct r5l_log *log, struct stripe_head *sh);
void r5c_make_stripe_write_out(struct stripe_head *sh);
void r5c_flush_cache(struct r5conf *conf, int num);
void r5c_check_stripe_cache_usage(struct r5conf *conf);
void r5c_check_cached_full_stripe(struct r5conf *conf);
extern struct md_sysfs_entry r5c_journal_mode;
void r5c_update_on_rdev_error(struct mddev *mddev, struct md_rdev *rdev);
bool r5c_big_stripe_cached(struct r5conf *conf, sector_t sect);
int r5l_start(struct r5l_log *log);

struct dma_async_tx_descriptor *
ops_run_partial_parity(struct stripe_head *sh, struct raid5_percpu *percpu,
		       struct dma_async_tx_descriptor *tx);
int ppl_init_log(struct r5conf *conf);
void ppl_exit_log(struct r5conf *conf);
int ppl_write_stripe(struct r5conf *conf, struct stripe_head *sh);
void ppl_write_stripe_run(struct r5conf *conf);
void ppl_stripe_write_finished(struct stripe_head *sh);
int ppl_modify_log(struct r5conf *conf, struct md_rdev *rdev, bool add);
void ppl_quiesce(struct r5conf *conf, int quiesce);
int ppl_handle_flush_request(struct bio *bio);
extern struct md_sysfs_entry ppl_write_hint;

static inline bool raid5_has_log(struct r5conf *conf)
{
	return test_bit(MD_HAS_JOURNAL, &conf->mddev->flags);
}

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

static inline void log_flush_stripe_to_raid(struct r5conf *conf)
{
	if (conf->log)
		r5l_flush_stripe_to_raid(conf->log);
	else if (raid5_has_ppl(conf))
		ppl_write_stripe_run(conf);
}

static inline int log_handle_flush_request(struct r5conf *conf, struct bio *bio)
{
	int ret = -ENODEV;

	if (conf->log)
		ret = r5l_handle_flush_request(conf->log, bio);
	else if (raid5_has_ppl(conf))
		ret = ppl_handle_flush_request(bio);

	return ret;
}

static inline void log_quiesce(struct r5conf *conf, int quiesce)
{
	if (conf->log)
		r5l_quiesce(conf->log, quiesce);
	else if (raid5_has_ppl(conf))
		ppl_quiesce(conf, quiesce);
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
