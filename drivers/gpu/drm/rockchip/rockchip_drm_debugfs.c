// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 * Author: Sandy Huang <hjc@rock-chips.com>
 */

#include <drm/drm_atomic_uapi.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include <linux/file.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_debugfs.h"
#include "rockchip_drm_fb.h"

#define DUMP_BUF_PATH		"/data"
#define AFBC_HEADER_SIZE		16
#define AFBC_HDR_ALIGN			64
#define AFBC_SUPERBLK_PIXELS		256
#define AFBC_SUPERBLK_ALIGNMENT		128

#define to_rockchip_crtc(x) container_of(x, struct rockchip_crtc, crtc)

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

static int get_afbc_size(uint32_t width, uint32_t height, uint32_t bpp)
{
	uint32_t h_alignment = 16;
	uint32_t n_blocks;
	uint32_t hdr_size;
	uint32_t size;

	height = ALIGN(height, h_alignment);
	n_blocks = width * height / AFBC_SUPERBLK_PIXELS;
	hdr_size = ALIGN(n_blocks * AFBC_HEADER_SIZE, AFBC_HDR_ALIGN);

	size = hdr_size + n_blocks * ALIGN(bpp * AFBC_SUPERBLK_PIXELS / 8, AFBC_SUPERBLK_ALIGNMENT);

	return size;
}

int rockchip_drm_dump_plane_buffer(struct vop_dump_info *dump_info, int frame_count)
{
	int flags;
	int bpp = 32;
	const char *ptr;
	char file_name[100];
	int width;
	size_t size, uv_size = 0;
	void *kvaddr, *kvaddr_origin;
	struct file *file;
	loff_t pos = 0;
	struct drm_format_name_buf format_name;
	char format[8];

	drm_get_format_name(dump_info->format->format, &format_name);
	strscpy(format, format_name.str, 5);
	bpp = rockchip_drm_get_bpp(dump_info->format);

	if (dump_info->yuv_format) {
		u8 hsub = dump_info->format->hsub;
		u8 vsub = dump_info->format->vsub;

		width = dump_info->pitches * 8 / bpp;
		flags = O_RDWR | O_CREAT | O_APPEND;
		uv_size = (width * dump_info->height * bpp >> 3) * 2 / hsub / vsub;
		snprintf(file_name, 100, "%s/video%d_%d_%s.%s", DUMP_BUF_PATH,
			 width, dump_info->height, format,
			 "bin");
	} else {
		width = dump_info->pitches * 8 / bpp;
		flags = O_RDWR | O_CREAT;
		snprintf(file_name, 100, "%s/win%d_area%d_%dx%d_%s%s%d.%s",
			 DUMP_BUF_PATH, dump_info->win_id,
			 dump_info->area_id, width, dump_info->height,
			 format, dump_info->AFBC_flag ?
			 "_AFBC_" : "_", frame_count, "bin");
	}
	kvaddr = vmap(dump_info->pages, dump_info->num_pages, VM_MAP,
		      pgprot_writecombine(PAGE_KERNEL));
	kvaddr_origin = kvaddr;
	if (!kvaddr)
		DRM_ERROR("failed to vmap() buffer\n");
	else
		kvaddr += dump_info->offset;

	if (dump_info->AFBC_flag)
		size = get_afbc_size(width, dump_info->height, bpp);
	else
		size = (width * dump_info->height * bpp >> 3) + uv_size;

	ptr = file_name;
	file = filp_open(ptr, flags, 0644);
	if (!IS_ERR(file)) {
		kernel_write(file, kvaddr, size, &pos);
		DRM_INFO("dump file name is:%s\n", file_name);
		fput(file);
	} else {
		DRM_INFO("open %s failed\n", ptr);
	}
	vunmap(kvaddr_origin);

	return 0;
}

static int rockchip_drm_dump_buffer_show(struct seq_file *m, void *data)
{
	seq_puts(m, "  echo dump    > dump to dump one frame\n");
	seq_puts(m, "  echo dumpon  > dump to start vop keep dumping\n");
	seq_puts(m, "  echo dumpoff > dump to stop keep dumping\n");
	seq_puts(m, "  echo dumpn   > dump n is the number of dump times\n");
	seq_puts(m, "  dump path is /data\n");

	return 0;
}

static int rockchip_drm_dump_buffer_open(struct inode *inode, struct file *file)
{
	struct drm_crtc *crtc = inode->i_private;

	return single_open(file, rockchip_drm_dump_buffer_show, crtc);
}

static ssize_t
rockchip_drm_dump_buffer_write(struct file *file, const char __user *ubuf,
			       size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_crtc *crtc = m->private;
	char buf[14] = {};
	int dump_times = 0;
	struct vop_dump_list *pos, *n;
	int i = 0;
	struct rockchip_crtc *rockchip_crtc = to_rockchip_crtc(crtc);

	if (!rockchip_crtc->vop_dump_list_init_flag)
		return -EPERM;

	if (len > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len - 1] = '\0';
	if (strncmp(buf, "dumpon", 6) == 0) {
		rockchip_crtc->vop_dump_status = DUMP_KEEP;
		DRM_INFO("keep dumping\n");
	} else if (strncmp(buf, "dumpoff", 7) == 0) {
		rockchip_crtc->vop_dump_status = DUMP_DISABLE;
		DRM_INFO("close keep dumping\n");
	} else if (strncmp(buf, "dump", 4) == 0) {
		if (isdigit(buf[4])) {
			for (i = 4; i < strlen(buf); i++) {
				dump_times += temp_pow(10, (strlen(buf)
						       - i - 1))
						       * (buf[i] - '0');
		}
			rockchip_crtc->vop_dump_times = dump_times;
		} else {
			drm_modeset_lock_all(crtc->dev);
			list_for_each_entry_safe(pos, n,
						 &rockchip_crtc->vop_dump_list_head,
						 entry) {
				rockchip_drm_dump_plane_buffer(&pos->dump_info,
							       rockchip_crtc->frame_count);
		}
			drm_modeset_unlock_all(crtc->dev);
			rockchip_crtc->frame_count++;
		}
	} else {
		return -EINVAL;
	}

	return len;
}

static const struct file_operations rockchip_drm_dump_buffer_fops = {
	.owner = THIS_MODULE,
	.open = rockchip_drm_dump_buffer_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = rockchip_drm_dump_buffer_write,
};

int rockchip_drm_add_dump_buffer(struct drm_crtc *crtc, struct dentry *root)
{
	struct dentry *vop_dump_root;
	struct dentry *ent;
	struct rockchip_crtc *rockchip_crtc = to_rockchip_crtc(crtc);

	vop_dump_root = debugfs_create_dir("vop_dump", root);
	rockchip_crtc->vop_dump_status = DUMP_DISABLE;
	rockchip_crtc->vop_dump_list_init_flag = false;
	rockchip_crtc->vop_dump_times = 0;
	rockchip_crtc->frame_count = 0;
	ent = debugfs_create_file("dump", 0644, vop_dump_root,
				  crtc, &rockchip_drm_dump_buffer_fops);
	if (!ent) {
		DRM_ERROR("create vop_plane_dump err\n");
		debugfs_remove_recursive(vop_dump_root);
	}

	return 0;
}
