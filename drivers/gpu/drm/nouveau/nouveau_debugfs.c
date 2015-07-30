/*
 * Copyright (C) 2009 Red Hat <bskeggs@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * Authors:
 *  Ben Skeggs <bskeggs@redhat.com>
 */

#include <linux/debugfs.h>
#include "nouveau_debugfs.h"
#include "nouveau_drm.h"

static int
nouveau_debugfs_vbios_image(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct nouveau_drm *drm = nouveau_drm(node->minor->dev);
	int i;

	for (i = 0; i < drm->vbios.length; i++)
		seq_printf(m, "%c", drm->vbios.data[i]);
	return 0;
}

static struct drm_info_list nouveau_debugfs_list[] = {
	{ "vbios.rom", nouveau_debugfs_vbios_image, 0, NULL },
};
#define NOUVEAU_DEBUGFS_ENTRIES ARRAY_SIZE(nouveau_debugfs_list)

static const struct nouveau_debugfs_files {
	const char *name;
	const struct file_operations *fops;
} nouveau_debugfs_files[] = {};


static int
nouveau_debugfs_create_file(struct drm_minor *minor,
		const struct nouveau_debugfs_files *ndf)
{
	struct drm_info_node *node;

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (node == NULL)
		return -ENOMEM;

	node->minor = minor;
	node->info_ent = (const void *)ndf->fops;
	node->dent = debugfs_create_file(ndf->name, S_IRUGO | S_IWUSR,
					 minor->debugfs_root, node, ndf->fops);
	if (!node->dent) {
		kfree(node);
		return -ENOMEM;
	}

	mutex_lock(&minor->debugfs_lock);
	list_add(&node->list, &minor->debugfs_list);
	mutex_unlock(&minor->debugfs_lock);
	return 0;
}

int
nouveau_drm_debugfs_init(struct drm_minor *minor)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(nouveau_debugfs_files); i++) {
		ret = nouveau_debugfs_create_file(minor,
						  &nouveau_debugfs_files[i]);

		if (ret)
			return ret;
	}

	return drm_debugfs_create_files(nouveau_debugfs_list,
					NOUVEAU_DEBUGFS_ENTRIES,
					minor->debugfs_root, minor);
}

void
nouveau_drm_debugfs_cleanup(struct drm_minor *minor)
{
	int i;

	drm_debugfs_remove_files(nouveau_debugfs_list, NOUVEAU_DEBUGFS_ENTRIES,
				 minor);

	for (i = 0; i < ARRAY_SIZE(nouveau_debugfs_files); i++) {
		drm_debugfs_remove_files((struct drm_info_list *)
					 nouveau_debugfs_files[i].fops,
					 1, minor);
	}
}
