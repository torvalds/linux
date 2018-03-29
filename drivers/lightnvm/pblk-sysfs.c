/*
 * Copyright (C) 2016 CNEX Labs
 * Initial release: Javier Gonzalez <javier@cnexlabs.com>
 *                  Matias Bjorling <matias@cnexlabs.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Implementation of a physical block-device target for Open-channel SSDs.
 *
 * pblk-sysfs.c - pblk's sysfs
 *
 */

#include "pblk.h"

static ssize_t pblk_sysfs_luns_show(struct pblk *pblk, char *page)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_lun *rlun;
	ssize_t sz = 0;
	int i;

	for (i = 0; i < geo->all_luns; i++) {
		int active = 1;

		rlun = &pblk->luns[i];
		if (!down_trylock(&rlun->wr_sem)) {
			active = 0;
			up(&rlun->wr_sem);
		}
		sz += snprintf(page + sz, PAGE_SIZE - sz,
				"pblk: pos:%d, ch:%d, lun:%d - %d\n",
					i,
					rlun->bppa.a.ch,
					rlun->bppa.a.lun,
					active);
	}

	return sz;
}

static ssize_t pblk_sysfs_rate_limiter(struct pblk *pblk, char *page)
{
	int free_blocks, free_user_blocks, total_blocks;
	int rb_user_max, rb_user_cnt;
	int rb_gc_max, rb_gc_cnt, rb_budget, rb_state;

	free_blocks = pblk_rl_nr_free_blks(&pblk->rl);
	free_user_blocks = pblk_rl_nr_user_free_blks(&pblk->rl);
	rb_user_max = pblk->rl.rb_user_max;
	rb_user_cnt = atomic_read(&pblk->rl.rb_user_cnt);
	rb_gc_max = pblk->rl.rb_gc_max;
	rb_gc_cnt = atomic_read(&pblk->rl.rb_gc_cnt);
	rb_budget = pblk->rl.rb_budget;
	rb_state = pblk->rl.rb_state;

	total_blocks = pblk->rl.total_blocks;

	return snprintf(page, PAGE_SIZE,
		"u:%u/%u,gc:%u/%u(%u)(stop:<%u,full:>%u,free:%d/%d/%d)-%d\n",
				rb_user_cnt,
				rb_user_max,
				rb_gc_cnt,
				rb_gc_max,
				rb_state,
				rb_budget,
				pblk->rl.high,
				free_blocks,
				free_user_blocks,
				total_blocks,
				READ_ONCE(pblk->rl.rb_user_active));
}

static ssize_t pblk_sysfs_gc_state_show(struct pblk *pblk, char *page)
{
	int gc_enabled, gc_active;

	pblk_gc_sysfs_state_show(pblk, &gc_enabled, &gc_active);
	return snprintf(page, PAGE_SIZE, "gc_enabled=%d, gc_active=%d\n",
					gc_enabled, gc_active);
}

static ssize_t pblk_sysfs_stats(struct pblk *pblk, char *page)
{
	ssize_t sz;

	sz = snprintf(page, PAGE_SIZE,
			"read_failed=%lu, read_high_ecc=%lu, read_empty=%lu, read_failed_gc=%lu, write_failed=%lu, erase_failed=%lu\n",
			atomic_long_read(&pblk->read_failed),
			atomic_long_read(&pblk->read_high_ecc),
			atomic_long_read(&pblk->read_empty),
			atomic_long_read(&pblk->read_failed_gc),
			atomic_long_read(&pblk->write_failed),
			atomic_long_read(&pblk->erase_failed));

	return sz;
}

static ssize_t pblk_sysfs_write_buffer(struct pblk *pblk, char *page)
{
	return pblk_rb_sysfs(&pblk->rwb, page);
}

static ssize_t pblk_sysfs_ppaf(struct pblk *pblk, char *page)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct nvm_addrf_12 *ppaf;
	struct nvm_addrf_12 *geo_ppaf;
	ssize_t sz = 0;

	ppaf = (struct nvm_addrf_12 *)&pblk->addrf;
	geo_ppaf = (struct nvm_addrf_12 *)&geo->addrf;

	sz = snprintf(page, PAGE_SIZE,
		"g:(b:%d)blk:%d/%d,pg:%d/%d,lun:%d/%d,ch:%d/%d,pl:%d/%d,sec:%d/%d\n",
			pblk->addrf_len,
			ppaf->blk_offset, ppaf->blk_len,
			ppaf->pg_offset, ppaf->pg_len,
			ppaf->lun_offset, ppaf->lun_len,
			ppaf->ch_offset, ppaf->ch_len,
			ppaf->pln_offset, ppaf->pln_len,
			ppaf->sec_offset, ppaf->sec_len);

	sz += snprintf(page + sz, PAGE_SIZE - sz,
		"d:blk:%d/%d,pg:%d/%d,lun:%d/%d,ch:%d/%d,pl:%d/%d,sec:%d/%d\n",
			geo_ppaf->blk_offset, geo_ppaf->blk_len,
			geo_ppaf->pg_offset, geo_ppaf->pg_len,
			geo_ppaf->lun_offset, geo_ppaf->lun_len,
			geo_ppaf->ch_offset, geo_ppaf->ch_len,
			geo_ppaf->pln_offset, geo_ppaf->pln_len,
			geo_ppaf->sec_offset, geo_ppaf->sec_len);

	return sz;
}

static ssize_t pblk_sysfs_lines(struct pblk *pblk, char *page)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line *line;
	ssize_t sz = 0;
	int nr_free_lines;
	int cur_data, cur_log;
	int free_line_cnt = 0, closed_line_cnt = 0, emeta_line_cnt = 0;
	int d_line_cnt = 0, l_line_cnt = 0;
	int gc_full = 0, gc_high = 0, gc_mid = 0, gc_low = 0, gc_empty = 0;
	int bad = 0, cor = 0;
	int msecs = 0, cur_sec = 0, vsc = 0, sec_in_line = 0;
	int map_weight = 0, meta_weight = 0;

	spin_lock(&l_mg->free_lock);
	cur_data = (l_mg->data_line) ? l_mg->data_line->id : -1;
	cur_log = (l_mg->log_line) ? l_mg->log_line->id : -1;
	nr_free_lines = l_mg->nr_free_lines;

	list_for_each_entry(line, &l_mg->free_list, list)
		free_line_cnt++;
	spin_unlock(&l_mg->free_lock);

	spin_lock(&l_mg->close_lock);
	list_for_each_entry(line, &l_mg->emeta_list, list)
		emeta_line_cnt++;
	spin_unlock(&l_mg->close_lock);

	spin_lock(&l_mg->gc_lock);
	list_for_each_entry(line, &l_mg->gc_full_list, list) {
		if (line->type == PBLK_LINETYPE_DATA)
			d_line_cnt++;
		else if (line->type == PBLK_LINETYPE_LOG)
			l_line_cnt++;
		closed_line_cnt++;
		gc_full++;
	}

	list_for_each_entry(line, &l_mg->gc_high_list, list) {
		if (line->type == PBLK_LINETYPE_DATA)
			d_line_cnt++;
		else if (line->type == PBLK_LINETYPE_LOG)
			l_line_cnt++;
		closed_line_cnt++;
		gc_high++;
	}

	list_for_each_entry(line, &l_mg->gc_mid_list, list) {
		if (line->type == PBLK_LINETYPE_DATA)
			d_line_cnt++;
		else if (line->type == PBLK_LINETYPE_LOG)
			l_line_cnt++;
		closed_line_cnt++;
		gc_mid++;
	}

	list_for_each_entry(line, &l_mg->gc_low_list, list) {
		if (line->type == PBLK_LINETYPE_DATA)
			d_line_cnt++;
		else if (line->type == PBLK_LINETYPE_LOG)
			l_line_cnt++;
		closed_line_cnt++;
		gc_low++;
	}

	list_for_each_entry(line, &l_mg->gc_empty_list, list) {
		if (line->type == PBLK_LINETYPE_DATA)
			d_line_cnt++;
		else if (line->type == PBLK_LINETYPE_LOG)
			l_line_cnt++;
		closed_line_cnt++;
		gc_empty++;
	}

	list_for_each_entry(line, &l_mg->bad_list, list)
		bad++;
	list_for_each_entry(line, &l_mg->corrupt_list, list)
		cor++;
	spin_unlock(&l_mg->gc_lock);

	spin_lock(&l_mg->free_lock);
	if (l_mg->data_line) {
		cur_sec = l_mg->data_line->cur_sec;
		msecs = l_mg->data_line->left_msecs;
		vsc = le32_to_cpu(*l_mg->data_line->vsc);
		sec_in_line = l_mg->data_line->sec_in_line;
		meta_weight = bitmap_weight(&l_mg->meta_bitmap,
							PBLK_DATA_LINES);
		map_weight = bitmap_weight(l_mg->data_line->map_bitmap,
							lm->sec_per_line);
	}
	spin_unlock(&l_mg->free_lock);

	if (nr_free_lines != free_line_cnt)
		pr_err("pblk: corrupted free line list:%d/%d\n",
						nr_free_lines, free_line_cnt);

	sz = snprintf(page, PAGE_SIZE - sz,
		"line: nluns:%d, nblks:%d, nsecs:%d\n",
		geo->all_luns, lm->blk_per_line, lm->sec_per_line);

	sz += snprintf(page + sz, PAGE_SIZE - sz,
		"lines:d:%d,l:%d-f:%d,m:%d/%d,c:%d,b:%d,co:%d(d:%d,l:%d)t:%d\n",
					cur_data, cur_log,
					nr_free_lines,
					emeta_line_cnt, meta_weight,
					closed_line_cnt,
					bad, cor,
					d_line_cnt, l_line_cnt,
					l_mg->nr_lines);

	sz += snprintf(page + sz, PAGE_SIZE - sz,
		"GC: full:%d, high:%d, mid:%d, low:%d, empty:%d, queue:%d\n",
			gc_full, gc_high, gc_mid, gc_low, gc_empty,
			atomic_read(&pblk->gc.read_inflight_gc));

	sz += snprintf(page + sz, PAGE_SIZE - sz,
		"data (%d) cur:%d, left:%d, vsc:%d, s:%d, map:%d/%d (%d)\n",
			cur_data, cur_sec, msecs, vsc, sec_in_line,
			map_weight, lm->sec_per_line,
			atomic_read(&pblk->inflight_io));

	return sz;
}

static ssize_t pblk_sysfs_lines_info(struct pblk *pblk, char *page)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	ssize_t sz = 0;

	sz = snprintf(page, PAGE_SIZE - sz,
				"smeta - len:%d, secs:%d\n",
					lm->smeta_len, lm->smeta_sec);
	sz += snprintf(page + sz, PAGE_SIZE - sz,
				"emeta - len:%d, sec:%d, bb_start:%d\n",
					lm->emeta_len[0], lm->emeta_sec[0],
					lm->emeta_bb);
	sz += snprintf(page + sz, PAGE_SIZE - sz,
				"bitmap lengths: sec:%d, blk:%d, lun:%d\n",
					lm->sec_bitmap_len,
					lm->blk_bitmap_len,
					lm->lun_bitmap_len);
	sz += snprintf(page + sz, PAGE_SIZE - sz,
				"blk_line:%d, sec_line:%d, sec_blk:%d\n",
					lm->blk_per_line,
					lm->sec_per_line,
					geo->clba);

	return sz;
}

static ssize_t pblk_sysfs_get_sec_per_write(struct pblk *pblk, char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n", pblk->sec_per_write);
}

static ssize_t pblk_get_write_amp(u64 user, u64 gc, u64 pad,
				  char *page)
{
	int sz;


	sz = snprintf(page, PAGE_SIZE,
			"user:%lld gc:%lld pad:%lld WA:",
			user, gc, pad);

	if (!user) {
		sz += snprintf(page + sz, PAGE_SIZE - sz, "NaN\n");
	} else {
		u64 wa_int;
		u32 wa_frac;

		wa_int = (user + gc + pad) * 100000;
		wa_int = div_u64(wa_int, user);
		wa_int = div_u64_rem(wa_int, 100000, &wa_frac);

		sz += snprintf(page + sz, PAGE_SIZE - sz, "%llu.%05u\n",
							wa_int, wa_frac);
	}

	return sz;
}

static ssize_t pblk_sysfs_get_write_amp_mileage(struct pblk *pblk, char *page)
{
	return pblk_get_write_amp(atomic64_read(&pblk->user_wa),
		atomic64_read(&pblk->gc_wa), atomic64_read(&pblk->pad_wa),
		page);
}

static ssize_t pblk_sysfs_get_write_amp_trip(struct pblk *pblk, char *page)
{
	return pblk_get_write_amp(
		atomic64_read(&pblk->user_wa) - pblk->user_rst_wa,
		atomic64_read(&pblk->gc_wa) - pblk->gc_rst_wa,
		atomic64_read(&pblk->pad_wa) - pblk->pad_rst_wa, page);
}

static long long bucket_percentage(unsigned long long bucket,
				   unsigned long long total)
{
	int p = bucket * 100;

	p = div_u64(p, total);

	return p;
}

static ssize_t pblk_sysfs_get_padding_dist(struct pblk *pblk, char *page)
{
	int sz = 0;
	unsigned long long total;
	unsigned long long total_buckets = 0;
	int buckets = pblk->min_write_pgs - 1;
	int i;

	total = atomic64_read(&pblk->nr_flush) - pblk->nr_flush_rst;
	if (!total) {
		for (i = 0; i < (buckets + 1); i++)
			sz += snprintf(page + sz, PAGE_SIZE - sz,
				"%d:0 ", i);
		sz += snprintf(page + sz, PAGE_SIZE - sz, "\n");

		return sz;
	}

	for (i = 0; i < buckets; i++)
		total_buckets += atomic64_read(&pblk->pad_dist[i]);

	sz += snprintf(page + sz, PAGE_SIZE - sz, "0:%lld%% ",
		bucket_percentage(total - total_buckets, total));

	for (i = 0; i < buckets; i++) {
		unsigned long long p;

		p = bucket_percentage(atomic64_read(&pblk->pad_dist[i]),
					  total);
		sz += snprintf(page + sz, PAGE_SIZE - sz, "%d:%lld%% ",
				i + 1, p);
	}
	sz += snprintf(page + sz, PAGE_SIZE - sz, "\n");

	return sz;
}

#ifdef CONFIG_NVM_DEBUG
static ssize_t pblk_sysfs_stats_debug(struct pblk *pblk, char *page)
{
	return snprintf(page, PAGE_SIZE,
		"%lu\t%lu\t%ld\t%llu\t%ld\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\n",
			atomic_long_read(&pblk->inflight_writes),
			atomic_long_read(&pblk->inflight_reads),
			atomic_long_read(&pblk->req_writes),
			(u64)atomic64_read(&pblk->nr_flush),
			atomic_long_read(&pblk->padded_writes),
			atomic_long_read(&pblk->padded_wb),
			atomic_long_read(&pblk->sub_writes),
			atomic_long_read(&pblk->sync_writes),
			atomic_long_read(&pblk->recov_writes),
			atomic_long_read(&pblk->recov_gc_writes),
			atomic_long_read(&pblk->recov_gc_reads),
			atomic_long_read(&pblk->cache_reads),
			atomic_long_read(&pblk->sync_reads));
}
#endif

static ssize_t pblk_sysfs_gc_force(struct pblk *pblk, const char *page,
				   size_t len)
{
	size_t c_len;
	int force;

	c_len = strcspn(page, "\n");
	if (c_len >= len)
		return -EINVAL;

	if (kstrtouint(page, 0, &force))
		return -EINVAL;

	pblk_gc_sysfs_force(pblk, force);

	return len;
}

static ssize_t pblk_sysfs_set_sec_per_write(struct pblk *pblk,
					     const char *page, size_t len)
{
	size_t c_len;
	int sec_per_write;

	c_len = strcspn(page, "\n");
	if (c_len >= len)
		return -EINVAL;

	if (kstrtouint(page, 0, &sec_per_write))
		return -EINVAL;

	if (sec_per_write < pblk->min_write_pgs
				|| sec_per_write > pblk->max_write_pgs
				|| sec_per_write % pblk->min_write_pgs != 0)
		return -EINVAL;

	pblk_set_sec_per_write(pblk, sec_per_write);

	return len;
}

static ssize_t pblk_sysfs_set_write_amp_trip(struct pblk *pblk,
			const char *page, size_t len)
{
	size_t c_len;
	int reset_value;

	c_len = strcspn(page, "\n");
	if (c_len >= len)
		return -EINVAL;

	if (kstrtouint(page, 0, &reset_value))
		return -EINVAL;

	if (reset_value !=  0)
		return -EINVAL;

	pblk->user_rst_wa = atomic64_read(&pblk->user_wa);
	pblk->pad_rst_wa = atomic64_read(&pblk->pad_wa);
	pblk->gc_rst_wa = atomic64_read(&pblk->gc_wa);

	return len;
}


static ssize_t pblk_sysfs_set_padding_dist(struct pblk *pblk,
			const char *page, size_t len)
{
	size_t c_len;
	int reset_value;
	int buckets = pblk->min_write_pgs - 1;
	int i;

	c_len = strcspn(page, "\n");
	if (c_len >= len)
		return -EINVAL;

	if (kstrtouint(page, 0, &reset_value))
		return -EINVAL;

	if (reset_value !=  0)
		return -EINVAL;

	for (i = 0; i < buckets; i++)
		atomic64_set(&pblk->pad_dist[i], 0);

	pblk->nr_flush_rst = atomic64_read(&pblk->nr_flush);

	return len;
}

static struct attribute sys_write_luns = {
	.name = "write_luns",
	.mode = 0444,
};

static struct attribute sys_rate_limiter_attr = {
	.name = "rate_limiter",
	.mode = 0444,
};

static struct attribute sys_gc_state = {
	.name = "gc_state",
	.mode = 0444,
};

static struct attribute sys_errors_attr = {
	.name = "errors",
	.mode = 0444,
};

static struct attribute sys_rb_attr = {
	.name = "write_buffer",
	.mode = 0444,
};

static struct attribute sys_stats_ppaf_attr = {
	.name = "ppa_format",
	.mode = 0444,
};

static struct attribute sys_lines_attr = {
	.name = "lines",
	.mode = 0444,
};

static struct attribute sys_lines_info_attr = {
	.name = "lines_info",
	.mode = 0444,
};

static struct attribute sys_gc_force = {
	.name = "gc_force",
	.mode = 0200,
};

static struct attribute sys_max_sec_per_write = {
	.name = "max_sec_per_write",
	.mode = 0644,
};

static struct attribute sys_write_amp_mileage = {
	.name = "write_amp_mileage",
	.mode = 0444,
};

static struct attribute sys_write_amp_trip = {
	.name = "write_amp_trip",
	.mode = 0644,
};

static struct attribute sys_padding_dist = {
	.name = "padding_dist",
	.mode = 0644,
};

#ifdef CONFIG_NVM_DEBUG
static struct attribute sys_stats_debug_attr = {
	.name = "stats",
	.mode = 0444,
};
#endif

static struct attribute *pblk_attrs[] = {
	&sys_write_luns,
	&sys_rate_limiter_attr,
	&sys_errors_attr,
	&sys_gc_state,
	&sys_gc_force,
	&sys_max_sec_per_write,
	&sys_rb_attr,
	&sys_stats_ppaf_attr,
	&sys_lines_attr,
	&sys_lines_info_attr,
	&sys_write_amp_mileage,
	&sys_write_amp_trip,
	&sys_padding_dist,
#ifdef CONFIG_NVM_DEBUG
	&sys_stats_debug_attr,
#endif
	NULL,
};

static ssize_t pblk_sysfs_show(struct kobject *kobj, struct attribute *attr,
			       char *buf)
{
	struct pblk *pblk = container_of(kobj, struct pblk, kobj);

	if (strcmp(attr->name, "rate_limiter") == 0)
		return pblk_sysfs_rate_limiter(pblk, buf);
	else if (strcmp(attr->name, "write_luns") == 0)
		return pblk_sysfs_luns_show(pblk, buf);
	else if (strcmp(attr->name, "gc_state") == 0)
		return pblk_sysfs_gc_state_show(pblk, buf);
	else if (strcmp(attr->name, "errors") == 0)
		return pblk_sysfs_stats(pblk, buf);
	else if (strcmp(attr->name, "write_buffer") == 0)
		return pblk_sysfs_write_buffer(pblk, buf);
	else if (strcmp(attr->name, "ppa_format") == 0)
		return pblk_sysfs_ppaf(pblk, buf);
	else if (strcmp(attr->name, "lines") == 0)
		return pblk_sysfs_lines(pblk, buf);
	else if (strcmp(attr->name, "lines_info") == 0)
		return pblk_sysfs_lines_info(pblk, buf);
	else if (strcmp(attr->name, "max_sec_per_write") == 0)
		return pblk_sysfs_get_sec_per_write(pblk, buf);
	else if (strcmp(attr->name, "write_amp_mileage") == 0)
		return pblk_sysfs_get_write_amp_mileage(pblk, buf);
	else if (strcmp(attr->name, "write_amp_trip") == 0)
		return pblk_sysfs_get_write_amp_trip(pblk, buf);
	else if (strcmp(attr->name, "padding_dist") == 0)
		return pblk_sysfs_get_padding_dist(pblk, buf);
#ifdef CONFIG_NVM_DEBUG
	else if (strcmp(attr->name, "stats") == 0)
		return pblk_sysfs_stats_debug(pblk, buf);
#endif
	return 0;
}

static ssize_t pblk_sysfs_store(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t len)
{
	struct pblk *pblk = container_of(kobj, struct pblk, kobj);

	if (strcmp(attr->name, "gc_force") == 0)
		return pblk_sysfs_gc_force(pblk, buf, len);
	else if (strcmp(attr->name, "max_sec_per_write") == 0)
		return pblk_sysfs_set_sec_per_write(pblk, buf, len);
	else if (strcmp(attr->name, "write_amp_trip") == 0)
		return pblk_sysfs_set_write_amp_trip(pblk, buf, len);
	else if (strcmp(attr->name, "padding_dist") == 0)
		return pblk_sysfs_set_padding_dist(pblk, buf, len);
	return 0;
}

static const struct sysfs_ops pblk_sysfs_ops = {
	.show = pblk_sysfs_show,
	.store = pblk_sysfs_store,
};

static struct kobj_type pblk_ktype = {
	.sysfs_ops	= &pblk_sysfs_ops,
	.default_attrs	= pblk_attrs,
};

int pblk_sysfs_init(struct gendisk *tdisk)
{
	struct pblk *pblk = tdisk->private_data;
	struct device *parent_dev = disk_to_dev(pblk->disk);
	int ret;

	ret = kobject_init_and_add(&pblk->kobj, &pblk_ktype,
					kobject_get(&parent_dev->kobj),
					"%s", "pblk");
	if (ret) {
		pr_err("pblk: could not register %s/pblk\n",
						tdisk->disk_name);
		return ret;
	}

	kobject_uevent(&pblk->kobj, KOBJ_ADD);
	return 0;
}

void pblk_sysfs_exit(struct gendisk *tdisk)
{
	struct pblk *pblk = tdisk->private_data;

	kobject_uevent(&pblk->kobj, KOBJ_REMOVE);
	kobject_del(&pblk->kobj);
	kobject_put(&pblk->kobj);
}
