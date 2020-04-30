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
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/ctype.h>
#include <linux/syscalls.h>

#include <drm/drm_client.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_edid.h>
#include <drm/drm_atomic.h>
#include <drm/drmP.h>

#include "drm_internal.h"
#include "drm_crtc_internal.h"

#if defined(CONFIG_DEBUG_FS)

#define DUMP_BUF_PATH		"/data/vop_buf"

/***************************************************
 * Initialization, etc.
 **************************************************/

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

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
static char *get_format_str(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return "ARGB8888";
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		return "BGR888";
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		return "RGB565";
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV12_10:
		return "YUV420NV12";
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV16_10:
		return "YUV422NV16";
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV24_10:
		return "YUV444NV24";
	case DRM_FORMAT_YUYV:
		return "YUYV";
	default:
		DRM_ERROR("unsupported format[%08x]\n", format);
		return "UNF";
	}
}

int vop_plane_dump(struct vop_dump_info *dump_info, int frame_count)
{
	int flags;
	int bits = 32;
	int fd;
	const char *ptr;
	char file_name[100];
	int width;
	void *kvaddr;
	mm_segment_t old_fs;
	u32 format = dump_info->pixel_format;

	switch (format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		bits = 32;
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV24_10:
		bits = 24;
		break;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV16_10:
		bits = 16;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV12_10:
		bits = 12;
		break;
	case DRM_FORMAT_YUYV:
		bits = 16;
		break;
	default:
		DRM_ERROR("unsupported format[%08x]\n", format);
		return -1;
	}

	if (dump_info->yuv_format) {
		width = dump_info->pitches;
		flags = O_RDWR | O_CREAT | O_APPEND;
		snprintf(file_name, 100, "%s/video%d_%d_%s.%s", DUMP_BUF_PATH,
			 width, dump_info->height, get_format_str(format),
			 "bin");
	} else {
		width = dump_info->pitches >> 2;
		flags = O_RDWR | O_CREAT;
		snprintf(file_name, 100, "%s/win%d_area%d_%dx%d_%s%s%d.%s",
			 DUMP_BUF_PATH, dump_info->win_id,
			 dump_info->area_id, width, dump_info->height,
			 get_format_str(format), dump_info->AFBC_flag ?
			 "_AFBC_" : "_", frame_count, "bin");
	}
	kvaddr = vmap(dump_info->pages, dump_info->num_pages, VM_MAP,
		      pgprot_writecombine(PAGE_KERNEL));
	if (!kvaddr)
		DRM_ERROR("failed to vmap() buffer\n");
	else
		kvaddr += dump_info->offset;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ksys_mkdir(DUMP_BUF_PATH, 0700);
	ptr = file_name;
	fd = ksys_open(ptr, flags, 0644);
	if (fd >= 0) {
		ksys_write(fd, kvaddr, width * dump_info->height * bits >> 3);
		DRM_INFO("dump file name is:%s\n", file_name);
		ksys_close(fd);
	} else {
		DRM_INFO("writ fail fd err fd is %d\n", fd);
	}
	set_fs(old_fs);
	vunmap(kvaddr);

	return 0;
}

static int vop_dump_show(struct seq_file *m, void *data)
{
	seq_puts(m, "  echo dump    > dump to dump one frame\n");
	seq_puts(m, "  echo dumpon  > dump to start vop keep dumping\n");
	seq_puts(m, "  echo dumpoff > dump to stop keep dumping\n");
	seq_puts(m, "  echo dumpn   > dump n is the number of dump times\n");
	seq_puts(m, "  dump path is /data/vop_buf\n");
	seq_puts(m, "  if fd err = -3 try rm -r /data/vopbuf echo dump1 > dump can fix it\n");
	seq_puts(m, "  if fd err = -28 save needed data try rm -r /data/vopbuf\n");

	return 0;
}

static int vop_dump_open(struct inode *inode, struct file *file)
{
	struct drm_crtc *crtc = inode->i_private;

	return single_open(file, vop_dump_show, crtc);
}

static int temp_pow(int sum, int n)
{
	int i;
	int temp = sum;

	if (n < 1)
		return 1;
	for (i = 1; i < n ; i++)
		sum *= temp;
	return sum;
}

static ssize_t vop_dump_write(struct file *file, const char __user *ubuf,
			      size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_crtc *crtc = m->private;
	char buf[14] = {};
	int dump_times = 0;
	struct vop_dump_list *pos, *n;
	int i = 0;

	if (len > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len - 1] = '\0';
	if (strncmp(buf, "dumpon", 6) == 0) {
		crtc->vop_dump_status = DUMP_KEEP;
		DRM_INFO("keep dumping\n");
	} else if (strncmp(buf, "dumpoff", 7) == 0) {
		crtc->vop_dump_status = DUMP_DISABLE;
		DRM_INFO("close keep dumping\n");
	} else if (strncmp(buf, "dump", 4) == 0) {
		if (isdigit(buf[4])) {
			for (i = 4; i < strlen(buf); i++) {
				dump_times += temp_pow(10, (strlen(buf)
						       - i - 1))
						       * (buf[i] - '0');
		}
			crtc->vop_dump_times = dump_times;
		} else {
			drm_modeset_lock_all(crtc->dev);
			list_for_each_entry_safe(pos, n,
						 &crtc->vop_dump_list_head,
						 entry) {
				vop_plane_dump(&pos->dump_info,
					       crtc->frame_count);
		}
			drm_modeset_unlock_all(crtc->dev);
			crtc->frame_count++;
		}
	} else {
		return -EINVAL;
	}

	return len;
}

static const struct file_operations drm_vop_dump_fops = {
	.owner = THIS_MODULE,
	.open = vop_dump_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = vop_dump_write,
};

int drm_debugfs_vop_add(struct drm_crtc *crtc, struct dentry *root)
{
	struct dentry *vop_dump_root;
	struct dentry *ent;

	vop_dump_root = debugfs_create_dir("vop_dump", root);
	crtc->vop_dump_status = DUMP_DISABLE;
	crtc->vop_dump_list_init_flag = false;
	crtc->vop_dump_times = 0;
	crtc->frame_count = 0;
	ent = debugfs_create_file("dump", 0644, vop_dump_root,
				  crtc, &drm_vop_dump_fops);
	if (!ent) {
		DRM_ERROR("create vop_plane_dump err\n");
		debugfs_remove_recursive(vop_dump_root);
	}

	return 0;
}
#endif

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
	struct dentry *ent;
	struct drm_info_node *tmp;
	int i, ret;

	for (i = 0; i < count; i++) {
		u32 features = files[i].driver_features;

		if (features != 0 &&
		    (dev->driver->driver_features & features) != features)
			continue;

		tmp = kmalloc(sizeof(struct drm_info_node), GFP_KERNEL);
		if (tmp == NULL) {
			ret = -1;
			goto fail;
		}
		ent = debugfs_create_file(files[i].name, S_IFREG | S_IRUGO,
					  root, tmp, &drm_debugfs_fops);
		if (!ent) {
			DRM_ERROR("Cannot create /sys/kernel/debug/dri/%pd/%s\n",
				  root, files[i].name);
			kfree(tmp);
			ret = -1;
			goto fail;
		}

		tmp->minor = minor;
		tmp->dent = ent;
		tmp->info_ent = &files[i];

		mutex_lock(&minor->debugfs_lock);
		list_add(&tmp->list, &minor->debugfs_list);
		mutex_unlock(&minor->debugfs_lock);
	}
	return 0;

fail:
	drm_debugfs_remove_files(files, count, minor);
	return ret;
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
	if (!minor->debugfs_root) {
		DRM_ERROR("Cannot create /sys/kernel/debug/dri/%s\n", name);
		return -1;
	}

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

int drm_debugfs_cleanup(struct drm_minor *minor)
{
	if (!minor->debugfs_root)
		return 0;

	drm_debugfs_remove_all_files(minor);

	debugfs_remove_recursive(minor->debugfs_root);
	minor->debugfs_root = NULL;

	return 0;
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

int drm_debugfs_connector_add(struct drm_connector *connector)
{
	struct drm_minor *minor = connector->dev->primary;
	struct dentry *root, *ent;

	if (!minor->debugfs_root)
		return -1;

	root = debugfs_create_dir(connector->name, minor->debugfs_root);
	if (!root)
		return -ENOMEM;

	connector->debugfs_entry = root;

	/* force */
	ent = debugfs_create_file("force", S_IRUGO | S_IWUSR, root, connector,
				  &drm_connector_fops);
	if (!ent)
		goto error;

	/* edid */
	ent = debugfs_create_file("edid_override", S_IRUGO | S_IWUSR, root,
				  connector, &drm_edid_fops);
	if (!ent)
		goto error;

	return 0;

error:
	debugfs_remove_recursive(connector->debugfs_entry);
	connector->debugfs_entry = NULL;
	return -ENOMEM;
}

void drm_debugfs_connector_remove(struct drm_connector *connector)
{
	if (!connector->debugfs_entry)
		return;

	debugfs_remove_recursive(connector->debugfs_entry);

	connector->debugfs_entry = NULL;
}

int drm_debugfs_crtc_add(struct drm_crtc *crtc)
{
	struct drm_minor *minor = crtc->dev->primary;
	struct dentry *root;
	char *name;

	name = kasprintf(GFP_KERNEL, "crtc-%d", crtc->index);
	if (!name)
		return -ENOMEM;

	root = debugfs_create_dir(name, minor->debugfs_root);
	kfree(name);
	if (!root)
		return -ENOMEM;

	crtc->debugfs_entry = root;

	if (drm_debugfs_crtc_crc_add(crtc))
		goto error;

	return 0;

error:
	drm_debugfs_crtc_remove(crtc);
	return -ENOMEM;
}

void drm_debugfs_crtc_remove(struct drm_crtc *crtc)
{
	debugfs_remove_recursive(crtc->debugfs_entry);
	crtc->debugfs_entry = NULL;
}

#endif /* CONFIG_DEBUG_FS */
