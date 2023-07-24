// SPDX-License-Identifier: GPL-2.0-only

#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/dm-verity-loadpin.h>

#include "dm.h"
#include "dm-core.h"
#include "dm-verity.h"

#define DM_MSG_PREFIX	"verity-loadpin"

LIST_HEAD(dm_verity_loadpin_trusted_root_digests);

static bool is_trusted_verity_target(struct dm_target *ti)
{
	int verity_mode;
	u8 *root_digest;
	unsigned int digest_size;
	struct dm_verity_loadpin_trusted_root_digest *trd;
	bool trusted = false;

	if (!dm_is_verity_target(ti))
		return false;

	verity_mode = dm_verity_get_mode(ti);

	if ((verity_mode != DM_VERITY_MODE_EIO) &&
	    (verity_mode != DM_VERITY_MODE_RESTART) &&
	    (verity_mode != DM_VERITY_MODE_PANIC))
		return false;

	if (dm_verity_get_root_digest(ti, &root_digest, &digest_size))
		return false;

	list_for_each_entry(trd, &dm_verity_loadpin_trusted_root_digests, node) {
		if ((trd->len == digest_size) &&
		    !memcmp(trd->data, root_digest, digest_size)) {
			trusted = true;
			break;
		}
	}

	kfree(root_digest);

	return trusted;
}

/*
 * Determines whether the file system of a superblock is located on
 * a verity device that is trusted by LoadPin.
 */
bool dm_verity_loadpin_is_bdev_trusted(struct block_device *bdev)
{
	struct mapped_device *md;
	struct dm_table *table;
	struct dm_target *ti;
	int srcu_idx;
	bool trusted = false;

	if (bdev == NULL)
		return false;

	if (list_empty(&dm_verity_loadpin_trusted_root_digests))
		return false;

	md = dm_get_md(bdev->bd_dev);
	if (!md)
		return false;

	table = dm_get_live_table(md, &srcu_idx);

	if (table->num_targets != 1)
		goto out;

	ti = dm_table_get_target(table, 0);

	if (is_trusted_verity_target(ti))
		trusted = true;

out:
	dm_put_live_table(md, srcu_idx);
	dm_put(md);

	return trusted;
}
