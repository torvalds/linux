// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/debugfs.h>
#include <drm/drm_debugfs.h>

#include "xe_pm.h"
#include "xe_sa.h"
#include "xe_tile_debugfs.h"

static struct xe_tile *node_to_tile(struct drm_info_node *node)
{
	return node->dent->d_parent->d_inode->i_private;
}

/**
 * tile_debugfs_simple_show - A show callback for struct drm_info_list
 * @m: the &seq_file
 * @data: data used by the drm debugfs helpers
 *
 * This callback can be used in struct drm_info_list to describe debugfs
 * files that are &xe_tile specific.
 *
 * It is assumed that those debugfs files will be created on directory entry
 * which struct dentry d_inode->i_private points to &xe_tile.
 *
 *      /sys/kernel/debug/dri/0/
 *      ├── tile0/		# tile = dentry->d_inode->i_private
 *      │   │   ├── id		# tile = dentry->d_parent->d_inode->i_private
 *
 * This function assumes that &m->private will be set to the &struct
 * drm_info_node corresponding to the instance of the info on a given &struct
 * drm_minor (see struct drm_info_list.show for details).
 *
 * This function also assumes that struct drm_info_list.data will point to the
 * function code that will actually print a file content::
 *
 *   int (*print)(struct xe_tile *, struct drm_printer *)
 *
 * Example::
 *
 *    int tile_id(struct xe_tile *tile, struct drm_printer *p)
 *    {
 *        drm_printf(p, "%u\n", tile->id);
 *        return 0;
 *    }
 *
 *    static const struct drm_info_list info[] = {
 *        { name = "id", .show = tile_debugfs_simple_show, .data = tile_id },
 *    };
 *
 *    dir = debugfs_create_dir("tile0", parent);
 *    dir->d_inode->i_private = tile;
 *    drm_debugfs_create_files(info, ARRAY_SIZE(info), dir, minor);
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int tile_debugfs_simple_show(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);
	struct drm_info_node *node = m->private;
	struct xe_tile *tile = node_to_tile(node);
	int (*print)(struct xe_tile *, struct drm_printer *) = node->info_ent->data;

	return print(tile, &p);
}

/**
 * tile_debugfs_show_with_rpm - A show callback for struct drm_info_list
 * @m: the &seq_file
 * @data: data used by the drm debugfs helpers
 *
 * Similar to tile_debugfs_simple_show() but implicitly takes a RPM ref.
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int tile_debugfs_show_with_rpm(struct seq_file *m, void *data)
{
	struct drm_info_node *node = m->private;
	struct xe_tile *tile = node_to_tile(node);
	struct xe_device *xe = tile_to_xe(tile);
	int ret;

	xe_pm_runtime_get(xe);
	ret = tile_debugfs_simple_show(m, data);
	xe_pm_runtime_put(xe);

	return ret;
}

static int sa_info(struct xe_tile *tile, struct drm_printer *p)
{
	drm_suballoc_dump_debug_info(&tile->mem.kernel_bb_pool->base, p,
				     xe_sa_manager_gpu_addr(tile->mem.kernel_bb_pool));

	return 0;
}

/* only for debugfs files which can be safely used on the VF */
static const struct drm_info_list vf_safe_debugfs_list[] = {
	{ "sa_info", .show = tile_debugfs_show_with_rpm, .data = sa_info },
};

/**
 * xe_tile_debugfs_register - Register tile's debugfs attributes
 * @tile: the &xe_tile to register
 *
 * Create debugfs sub-directory with a name that includes a tile ID and
 * then creates set of debugfs files (attributes) specific to this tile.
 */
void xe_tile_debugfs_register(struct xe_tile *tile)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct drm_minor *minor = xe->drm.primary;
	struct dentry *root = minor->debugfs_root;
	char name[8];

	snprintf(name, sizeof(name), "tile%u", tile->id);
	tile->debugfs = debugfs_create_dir(name, root);
	if (IS_ERR(tile->debugfs))
		return;

	/*
	 * Store the xe_tile pointer as private data of the tile/ directory
	 * node so other tile specific attributes under that directory may
	 * refer to it by looking at its parent node private data.
	 */
	tile->debugfs->d_inode->i_private = tile;

	drm_debugfs_create_files(vf_safe_debugfs_list,
				 ARRAY_SIZE(vf_safe_debugfs_list),
				 tile->debugfs, minor);
}
