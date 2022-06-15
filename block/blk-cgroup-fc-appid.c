// SPDX-License-Identifier: GPL-2.0

#include "blk-cgroup.h"

/**
 * blkcg_set_fc_appid - set the fc_app_id field associted to blkcg
 * @app_id: application identifier
 * @cgrp_id: cgroup id
 * @app_id_len: size of application identifier
 */
int blkcg_set_fc_appid(char *app_id, u64 cgrp_id, size_t app_id_len)
{
	struct cgroup *cgrp;
	struct cgroup_subsys_state *css;
	struct blkcg *blkcg;
	int ret  = 0;

	if (app_id_len > FC_APPID_LEN)
		return -EINVAL;

	cgrp = cgroup_get_from_id(cgrp_id);
	if (!cgrp)
		return -ENOENT;
	css = cgroup_get_e_css(cgrp, &io_cgrp_subsys);
	if (!css) {
		ret = -ENOENT;
		goto out_cgrp_put;
	}
	blkcg = css_to_blkcg(css);
	/*
	 * There is a slight race condition on setting the appid.
	 * Worst case an I/O may not find the right id.
	 * This is no different from the I/O we let pass while obtaining
	 * the vmid from the fabric.
	 * Adding the overhead of a lock is not necessary.
	 */
	strlcpy(blkcg->fc_app_id, app_id, app_id_len);
	css_put(css);
out_cgrp_put:
	cgroup_put(cgrp);
	return ret;
}
EXPORT_SYMBOL_GPL(blkcg_set_fc_appid);

/**
 * blkcg_get_fc_appid - get the fc app identifier associated with a bio
 * @bio: target bio
 *
 * On success return the fc_app_id, on failure return NULL
 */
char *blkcg_get_fc_appid(struct bio *bio)
{
	if (!bio->bi_blkg || bio->bi_blkg->blkcg->fc_app_id[0] == '\0')
		return NULL;
	return bio->bi_blkg->blkcg->fc_app_id;
}
EXPORT_SYMBOL_GPL(blkcg_get_fc_appid);
