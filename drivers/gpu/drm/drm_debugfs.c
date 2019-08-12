/*
 * Created: Sun Dec 21 13:08:50 2008 by bgamari@gmail.com
 *
 * Copyright 2008 Ben Gamari <bgamari@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <drm/drm_atomic.h>
#include <drm/drm_auth.h>
#include <drm/drm_client.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

#if defined(CONFIG_DEBUG_FS)

/***************************************************
 * Initialization, etc.
 **************************************************/

static int drm_name_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *dev = minor->dev;
	struct drm_master *master;

	mutex_lock(&dev->master_mutex);
	master = dev->master;
	seq_printf(m, "%s", dev->driver->name);
	if (dev->dev)
		seq_printf(m, " dev=%s", dev_name(dev->dev));
	if (master && master->unique)
		seq_printf(m, " master=%s", master->unique);
	if (dev->unique)
		seq_printf(m, " unique=%s", dev->unique);
	seq_printf(m, "\n");
	mutex_unlock(&dev->master_mutex);

	return 0;
}

static int drm_clients_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_file *priv;
	kuid_t uid;

	seq_printf(m,
		   "%20s %5s %3s master a %5s %10s\n",
		   "command",
		   "pid",
		   "dev",
		   "uid",
		   "magic");

	/* dev->filelist is sorted youngest first, but we want to present
	 * oldest first (i.e. kernel, servers, clients), so walk backwardss.
	 */
	mutex_lock(&dev->filelist_mutex);
	list_for_each_entry_reverse(priv, &dev->filelist, lhead) {
		struct task_struct *task;

		rcu_read_lock(); /* locks pid_task()->comm */
		task = pid_task(priv->pid, PIDTYPE_PID);
		uid = task ? __task_cred(task)->euid : GLOBAL_ROOT_UID;
		seq_printf(m, "%20s %5d %3d   %c    %c %5d %10u\n",
			   task ? task->comm : "<unknown>",
			   pid_vnr(priv->pid),
			   priv->minor->index,
			   drm_is_current_master(priv) ? 'y' : 'n',
			   priv->authenticated ? 'y' : 'n',
			   from_kuid_munged(seq_user_ns(m), uid),
			   priv->magic);
		rcu_read_unlock();
	}
	mutex_unlock(&dev->filelist_mutex);
	return 0;
}

static int drm_gem_one_name_info(int id, void *ptr, void *data)
{
	struct drm_gem_object *obj = ptr;
	struct seq_file *m = data;

	seq_printf(m, "%6d %8zd %7d %8d\n",
		   obj->name, obj->size,
		   obj->handle_count,
		   kref_read(&obj->refcount));
	return 0;
}

static int drm_gem_name_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;

	seq_printf(m, "  name     size handles refcount\n");

	mutex_lock(&dev->object_name_lock);
	idr_for_each(&dev->object_name_idr, drm_gem_one_name_info, m);
	mutex_unlock(&dev->object_name_lock);

	return 0;
}

static const struct drm_info_list drm_debugfs_list[] = {
	{"name", drm_name_info, 0},
	{"clients", drm_clients_info, 0},
	{"gem_names", drm_gem_name_info, DRIVER_GEM},
};
#define DRM_DEBUGFS_ENTRIES ARRAY_SIZE(drm_debugfs_list)


static int drm_debugfs_open(struct inode *inode, struct file *file)
{
	struct drm_info_node *node = inode->i_private;

	return single_open(file, node->info_ent->show, node);
}


static const struct file_operations drm_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = drm_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};


/**
 * drm_debugfs_create_files - Initialize a given set of debugfs files for DRM
 * 			minor
 * @files: The array of files to create
 * @count: The number of files given
 * @root: DRI debugfs dir entry.
 * @minor: device minor number
 *
 * Create a given set of debugfs files represented by an array of
 * &struct drm_info_list in the given root directory. These files will be removed
 * automatically on drm_debugfs_cleanup().
 */
int drm_debugfs_create_files(const struct drm_info_list *files, int count,
			     struct dentry *root, struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct drm_info_node *tmp;
	int i;

	for (i = 0; i < count; i++) {
		u32 features = files[i].driver_features;

		if (features != 0 &&
		    (dev->driver->driver_features & features) != features)
			continue;

		tmp = kmalloc(sizeof(struct drm_info_node), GFP_KERNEL);
		if (tmp == NULL)
			continue;

		tmp->minor = minor;
		tmp->dent = debugfs_create_file(files[i].name,
						S_IFREG | S_IRUGO, root, tmp,
						&drm_debugfs_fops);
		tmp->info_ent = &files[i];

		mutex_lock(&minor->debugfs_lock);
		list_add(&tmp->list, &minor->debugfs_list);
		mutex_unlock(&minor->debugfs_lock);
	}
	return 0;
}
EXPORT_SYMBOL(drm_debugfs_create_files);

int drm_debugfs_init(struct drm_minor *minor, int minor_id,
		     struct dentry *root)
{
	struct drm_device *dev = minor->dev;
	char name[64];
	int ret;

	INIT_LIST_HEAD(&minor->debugfs_list);
	mutex_init(&minor->debugfs_lock);
	sprintf(name, "%d", minor_id);
	minor->debugfs_root = debugfs_create_dir(name, root);

	ret = drm_debugfs_create_files(drm_debugfs_list, DRM_DEBUGFS_ENTRIES,
				       minor->debugfs_root, minor);
	if (ret) {
		debugfs_remove(minor->debugfs_root);
		minor->debugfs_root = NULL;
		DRM_ERROR("Failed to create core drm debugfs files\n");
		return ret;
	}

	if (drm_drv_uses_atomic_modeset(dev)) {
		ret = drm_atomic_debugfs_init(minor);
		if (ret) {
			DRM_ERROR("Failed to create atomic debugfs files\n");
			return ret;
		}
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_framebuffer_debugfs_init(minor);
		if (ret) {
			DRM_ERROR("Failed to create framebuffer debugfs file\n");
			return ret;
		}

		ret = drm_client_debugfs_init(minor);
		if (ret) {
			DRM_ERROR("Failed to create client debugfs file\n");
			return ret;
		}
	}

	if (dev->driver->debugfs_init) {
		ret = dev->driver->debugfs_init(minor);
		if (ret) {
			DRM_ERROR("DRM: Driver failed to initialize "
				  "/sys/kernel/debug/dri.\n");
			return ret;
		}
	}
	return 0;
}


int drm_debugfs_remove_files(const struct drm_info_list *files, int count,
			     struct drm_minor *minor)
{
	struct list_head *pos, *q;
	struct drm_info_node *tmp;
	int i;

	mutex_lock(&minor->debugfs_lock);
	for (i = 0; i < count; i++) {
		list_for_each_safe(pos, q, &minor->debugfs_list) {
			tmp = list_entry(pos, struct drm_info_node, list);
			if (tmp->info_ent == &files[i]) {
				debugfs_remove(tmp->dent);
				list_del(pos);
				kfree(tmp);
			}
		}
	}
	mutex_unlock(&minor->debugfs_lock);
	return 0;
}
EXPORT_SYMBOL(drm_debugfs_remove_files);

static void drm_debugfs_remove_all_files(struct drm_minor *minor)
{
	struct drm_info_node *node, *tmp;

	mutex_lock(&minor->debugfs_lock);
	list_for_each_entry_safe(node, tmp, &minor->debugfs_list, list) {
		debugfs_remove(node->dent);
		list_del(&node->list);
		kfree(node);
	}
	mutex_unlock(&minor->debugfs_lock);
}

void drm_debugfs_cleanup(struct drm_minor *minor)
{
	if (!minor->debugfs_root)
		return;

	drm_debugfs_remove_all_files(minor);

	debugfs_remove_recursive(minor->debugfs_root);
	minor->debugfs_root = NULL;
}

static int connector_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;

	seq_printf(m, "%s\n", drm_get_connector_force_name(connector->force));

	return 0;
}

static int connector_open(struct inode *inode, struct file *file)
{
	struct drm_connector *dev = inode->i_private;

	return single_open(file, connector_show, dev);
}

static ssize_t connector_write(struct file *file, const char __user *ubuf,
			       size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_connector *connector = m->private;
	char buf[12];

	if (len > sizeof(buf) - 1)
		return -EINVAL;

	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (!strcmp(buf, "on"))
		connector->force = DRM_FORCE_ON;
	else if (!strcmp(buf, "digital"))
		connector->force = DRM_FORCE_ON_DIGITAL;
	else if (!strcmp(buf, "off"))
		connector->force = DRM_FORCE_OFF;
	else if (!strcmp(buf, "unspecified"))
		connector->force = DRM_FORCE_UNSPECIFIED;
	else
		return -EINVAL;

	return len;
}

static int edid_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct drm_property_blob *edid = connector->edid_blob_ptr;

	if (connector->override_edid && edid)
		seq_write(m, edid->data, edid->length);

	return 0;
}

static int edid_open(struct inode *inode, struct file *file)
{
	struct drm_connector *dev = inode->i_private;

	return single_open(file, edid_show, dev);
}

static ssize_t edid_write(struct file *file, const char __user *ubuf,
			  size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_connector *connector = m->private;
	char *buf;
	struct edid *edid;
	int ret;

	buf = memdup_user(ubuf, len);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	edid = (struct edid *) buf;

	if (len == 5 && !strncmp(buf, "reset", 5)) {
		connector->override_edid = false;
		ret = drm_connector_update_edid_property(connector, NULL);
	} else if (len < EDID_LENGTH ||
		   EDID_LENGTH * (1 + edid->extensions) > len)
		ret = -EINVAL;
	else {
		connector->override_edid = false;
		ret = drm_connector_update_edid_property(connector, edid);
		if (!ret)
			connector->override_edid = true;
	}

	kfree(buf);

	return (ret) ? ret : len;
}

static const struct file_operations drm_edid_fops = {
	.owner = THIS_MODULE,
	.open = edid_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = edid_write
};


static const struct file_operations drm_connector_fops = {
	.owner = THIS_MODULE,
	.open = connector_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = connector_write
};

void drm_debugfs_connector_add(struct drm_connector *connector)
{
	struct drm_minor *minor = connector->dev->primary;
	struct dentry *root;

	if (!minor->debugfs_root)
		return;

	root = debugfs_create_dir(connector->name, minor->debugfs_root);
	connector->debugfs_entry = root;

	/* force */
	debugfs_create_file("force", S_IRUGO | S_IWUSR, root, connector,
			    &drm_connector_fops);

	/* edid */
	debugfs_create_file("edid_override", S_IRUGO | S_IWUSR, root, connector,
			    &drm_edid_fops);
}

void drm_debugfs_connector_remove(struct drm_connector *connector)
{
	if (!connector->debugfs_entry)
		return;

	debugfs_remove_recursive(connector->debugfs_entry);

	connector->debugfs_entry = NULL;
}

void drm_debugfs_crtc_add(struct drm_crtc *crtc)
{
	struct drm_minor *minor = crtc->dev->primary;
	struct dentry *root;
	char *name;

	name = kasprintf(GFP_KERNEL, "crtc-%d", crtc->index);
	if (!name)
		return;

	root = debugfs_create_dir(name, minor->debugfs_root);
	kfree(name);

	crtc->debugfs_entry = root;

	drm_debugfs_crtc_crc_add(crtc);
}

void drm_debugfs_crtc_remove(struct drm_crtc *crtc)
{
	debugfs_remove_recursive(crtc->debugfs_entry);
	crtc->debugfs_entry = NULL;
}

#endif /* CONFIG_DEBUG_FS */
