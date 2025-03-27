// SPDX-License-Identifier: MIT
/*
 * Copyright 2020 Noralf Trønnes
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/lz4.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string_helpers.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/gud.h>

#include "gud_internal.h"

/* Only used internally */
static const struct drm_format_info gud_drm_format_r1 = {
	.format = GUD_DRM_FORMAT_R1,
	.num_planes = 1,
	.char_per_block = { 1, 0, 0 },
	.block_w = { 8, 0, 0 },
	.block_h = { 1, 0, 0 },
	.hsub = 1,
	.vsub = 1,
};

static const struct drm_format_info gud_drm_format_xrgb1111 = {
	.format = GUD_DRM_FORMAT_XRGB1111,
	.num_planes = 1,
	.char_per_block = { 1, 0, 0 },
	.block_w = { 2, 0, 0 },
	.block_h = { 1, 0, 0 },
	.hsub = 1,
	.vsub = 1,
};

static int gud_usb_control_msg(struct usb_interface *intf, bool in,
			       u8 request, u16 value, void *buf, size_t len)
{
	u8 requesttype = USB_TYPE_VENDOR | USB_RECIP_INTERFACE;
	u8 ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
	struct usb_device *usb = interface_to_usbdev(intf);
	unsigned int pipe;

	if (len && !buf)
		return -EINVAL;

	if (in) {
		pipe = usb_rcvctrlpipe(usb, 0);
		requesttype |= USB_DIR_IN;
	} else {
		pipe = usb_sndctrlpipe(usb, 0);
		requesttype |= USB_DIR_OUT;
	}

	return usb_control_msg(usb, pipe, request, requesttype, value,
			       ifnum, buf, len, USB_CTRL_GET_TIMEOUT);
}

static int gud_get_display_descriptor(struct usb_interface *intf,
				      struct gud_display_descriptor_req *desc)
{
	void *buf;
	int ret;

	buf = kmalloc(sizeof(*desc), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = gud_usb_control_msg(intf, true, GUD_REQ_GET_DESCRIPTOR, 0, buf, sizeof(*desc));
	memcpy(desc, buf, sizeof(*desc));
	kfree(buf);
	if (ret < 0)
		return ret;
	if (ret != sizeof(*desc))
		return -EIO;

	if (desc->magic != le32_to_cpu(GUD_DISPLAY_MAGIC))
		return -ENODATA;

	DRM_DEV_DEBUG_DRIVER(&intf->dev,
			     "version=%u flags=0x%x compression=0x%x max_buffer_size=%u\n",
			     desc->version, le32_to_cpu(desc->flags), desc->compression,
			     le32_to_cpu(desc->max_buffer_size));

	if (!desc->version || !desc->max_width || !desc->max_height ||
	    le32_to_cpu(desc->min_width) > le32_to_cpu(desc->max_width) ||
	    le32_to_cpu(desc->min_height) > le32_to_cpu(desc->max_height))
		return -EINVAL;

	return 0;
}

static int gud_status_to_errno(u8 status)
{
	switch (status) {
	case GUD_STATUS_OK:
		return 0;
	case GUD_STATUS_BUSY:
		return -EBUSY;
	case GUD_STATUS_REQUEST_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case GUD_STATUS_PROTOCOL_ERROR:
		return -EPROTO;
	case GUD_STATUS_INVALID_PARAMETER:
		return -EINVAL;
	case GUD_STATUS_ERROR:
		return -EREMOTEIO;
	default:
		return -EREMOTEIO;
	}
}

static int gud_usb_get_status(struct usb_interface *intf)
{
	int ret, status = -EIO;
	u8 *buf;

	buf = kmalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = gud_usb_control_msg(intf, true, GUD_REQ_GET_STATUS, 0, buf, sizeof(*buf));
	if (ret == sizeof(*buf))
		status = gud_status_to_errno(*buf);
	kfree(buf);

	if (ret < 0)
		return ret;

	return status;
}

static int gud_usb_transfer(struct gud_device *gdrm, bool in, u8 request, u16 index,
			    void *buf, size_t len)
{
	struct usb_interface *intf = to_usb_interface(gdrm->drm.dev);
	int idx, ret;

	drm_dbg(&gdrm->drm, "%s: request=0x%x index=%u len=%zu\n",
		in ? "get" : "set", request, index, len);

	if (!drm_dev_enter(&gdrm->drm, &idx))
		return -ENODEV;

	mutex_lock(&gdrm->ctrl_lock);

	ret = gud_usb_control_msg(intf, in, request, index, buf, len);
	if (ret == -EPIPE || ((gdrm->flags & GUD_DISPLAY_FLAG_STATUS_ON_SET) && !in && ret >= 0)) {
		int status;

		status = gud_usb_get_status(intf);
		if (status < 0) {
			ret = status;
		} else if (ret < 0) {
			dev_err_once(gdrm->drm.dev,
				     "Unexpected status OK for failed transfer\n");
			ret = -EPIPE;
		}
	}

	if (ret < 0) {
		drm_dbg(&gdrm->drm, "ret=%d\n", ret);
		gdrm->stats_num_errors++;
	}

	mutex_unlock(&gdrm->ctrl_lock);
	drm_dev_exit(idx);

	return ret;
}

/*
 * @buf cannot be allocated on the stack.
 * Returns number of bytes received or negative error code on failure.
 */
int gud_usb_get(struct gud_device *gdrm, u8 request, u16 index, void *buf, size_t max_len)
{
	return gud_usb_transfer(gdrm, true, request, index, buf, max_len);
}

/*
 * @buf can be allocated on the stack or NULL.
 * Returns zero on success or negative error code on failure.
 */
int gud_usb_set(struct gud_device *gdrm, u8 request, u16 index, void *buf, size_t len)
{
	void *trbuf = NULL;
	int ret;

	if (buf && len) {
		trbuf = kmemdup(buf, len, GFP_KERNEL);
		if (!trbuf)
			return -ENOMEM;
	}

	ret = gud_usb_transfer(gdrm, false, request, index, trbuf, len);
	kfree(trbuf);
	if (ret < 0)
		return ret;

	return ret != len ? -EIO : 0;
}

/*
 * @val can be allocated on the stack.
 * Returns zero on success or negative error code on failure.
 */
int gud_usb_get_u8(struct gud_device *gdrm, u8 request, u16 index, u8 *val)
{
	u8 *buf;
	int ret;

	buf = kmalloc(sizeof(*val), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = gud_usb_get(gdrm, request, index, buf, sizeof(*val));
	*val = *buf;
	kfree(buf);
	if (ret < 0)
		return ret;

	return ret != sizeof(*val) ? -EIO : 0;
}

/* Returns zero on success or negative error code on failure. */
int gud_usb_set_u8(struct gud_device *gdrm, u8 request, u8 val)
{
	return gud_usb_set(gdrm, request, 0, &val, sizeof(val));
}

static int gud_get_properties(struct gud_device *gdrm)
{
	struct gud_property_req *properties;
	unsigned int i, num_properties;
	int ret;

	properties = kcalloc(GUD_PROPERTIES_MAX_NUM, sizeof(*properties), GFP_KERNEL);
	if (!properties)
		return -ENOMEM;

	ret = gud_usb_get(gdrm, GUD_REQ_GET_PROPERTIES, 0,
			  properties, GUD_PROPERTIES_MAX_NUM * sizeof(*properties));
	if (ret <= 0)
		goto out;
	if (ret % sizeof(*properties)) {
		ret = -EIO;
		goto out;
	}

	num_properties = ret / sizeof(*properties);
	ret = 0;

	gdrm->properties = drmm_kcalloc(&gdrm->drm, num_properties, sizeof(*gdrm->properties),
					GFP_KERNEL);
	if (!gdrm->properties) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_properties; i++) {
		u16 prop = le16_to_cpu(properties[i].prop);
		u64 val = le64_to_cpu(properties[i].val);

		switch (prop) {
		case GUD_PROPERTY_ROTATION:
			/*
			 * DRM UAPI matches the protocol so use the value directly,
			 * but mask out any additions on future devices.
			 */
			val &= GUD_ROTATION_MASK;
			ret = drm_plane_create_rotation_property(&gdrm->pipe.plane,
								 DRM_MODE_ROTATE_0, val);
			break;
		default:
			/* New ones might show up in future devices, skip those we don't know. */
			drm_dbg(&gdrm->drm, "Ignoring unknown property: %u\n", prop);
			continue;
		}

		if (ret)
			goto out;

		gdrm->properties[gdrm->num_properties++] = prop;
	}
out:
	kfree(properties);

	return ret;
}

/*
 * FIXME: Dma-buf sharing requires DMA support by the importing device.
 *        This function is a workaround to make USB devices work as well.
 *        See todo.rst for how to fix the issue in the dma-buf framework.
 */
static struct drm_gem_object *gud_gem_prime_import(struct drm_device *drm, struct dma_buf *dma_buf)
{
	struct gud_device *gdrm = to_gud_device(drm);

	if (!gdrm->dmadev)
		return ERR_PTR(-ENODEV);

	return drm_gem_prime_import_dev(drm, dma_buf, gdrm->dmadev);
}

static int gud_stats_debugfs(struct seq_file *m, void *data)
{
	struct drm_debugfs_entry *entry = m->private;
	struct gud_device *gdrm = to_gud_device(entry->dev);
	char buf[10];

	string_get_size(gdrm->bulk_len, 1, STRING_UNITS_2, buf, sizeof(buf));
	seq_printf(m, "Max buffer size: %s\n", buf);
	seq_printf(m, "Number of errors:  %u\n", gdrm->stats_num_errors);

	seq_puts(m, "Compression:      ");
	if (gdrm->compression & GUD_COMPRESSION_LZ4)
		seq_puts(m, " lz4");
	if (!gdrm->compression)
		seq_puts(m, " none");
	seq_puts(m, "\n");

	if (gdrm->compression) {
		u64 remainder;
		u64 ratio = div64_u64_rem(gdrm->stats_length, gdrm->stats_actual_length,
					  &remainder);
		u64 ratio_frac = div64_u64(remainder * 10, gdrm->stats_actual_length);

		seq_printf(m, "Compression ratio: %llu.%llu\n", ratio, ratio_frac);
	}

	return 0;
}

static const struct drm_simple_display_pipe_funcs gud_pipe_funcs = {
	.check      = gud_pipe_check,
	.update	    = gud_pipe_update,
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS
};

static const struct drm_mode_config_funcs gud_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const u64 gud_pipe_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

DEFINE_DRM_GEM_FOPS(gud_fops);

static const struct drm_driver gud_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops			= &gud_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	.gem_prime_import	= gud_gem_prime_import,
	DRM_FBDEV_SHMEM_DRIVER_OPS,

	.name			= "gud",
	.desc			= "Generic USB Display",
	.major			= 1,
	.minor			= 0,
};

static int gud_alloc_bulk_buffer(struct gud_device *gdrm)
{
	unsigned int i, num_pages;
	struct page **pages;
	void *ptr;
	int ret;

	gdrm->bulk_buf = vmalloc_32(gdrm->bulk_len);
	if (!gdrm->bulk_buf)
		return -ENOMEM;

	num_pages = DIV_ROUND_UP(gdrm->bulk_len, PAGE_SIZE);
	pages = kmalloc_array(num_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (i = 0, ptr = gdrm->bulk_buf; i < num_pages; i++, ptr += PAGE_SIZE)
		pages[i] = vmalloc_to_page(ptr);

	ret = sg_alloc_table_from_pages(&gdrm->bulk_sgt, pages, num_pages,
					0, gdrm->bulk_len, GFP_KERNEL);
	kfree(pages);

	return ret;
}

static void gud_free_buffers_and_mutex(void *data)
{
	struct gud_device *gdrm = data;

	vfree(gdrm->compress_buf);
	gdrm->compress_buf = NULL;
	sg_free_table(&gdrm->bulk_sgt);
	vfree(gdrm->bulk_buf);
	gdrm->bulk_buf = NULL;
	mutex_destroy(&gdrm->ctrl_lock);
}

static int gud_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	const struct drm_format_info *xrgb8888_emulation_format = NULL;
	bool rgb565_supported = false, xrgb8888_supported = false;
	unsigned int num_formats_dev, num_formats = 0;
	struct usb_endpoint_descriptor *bulk_out;
	struct gud_display_descriptor_req desc;
	struct device *dev = &intf->dev;
	size_t max_buffer_size = 0;
	struct gud_device *gdrm;
	struct drm_device *drm;
	u8 *formats_dev;
	u32 *formats;
	int ret, i;

	ret = usb_find_bulk_out_endpoint(intf->cur_altsetting, &bulk_out);
	if (ret)
		return ret;

	ret = gud_get_display_descriptor(intf, &desc);
	if (ret) {
		DRM_DEV_DEBUG_DRIVER(dev, "Not a display interface: ret=%d\n", ret);
		return -ENODEV;
	}

	if (desc.version > 1) {
		dev_err(dev, "Protocol version %u is not supported\n", desc.version);
		return -ENODEV;
	}

	gdrm = devm_drm_dev_alloc(dev, &gud_drm_driver, struct gud_device, drm);
	if (IS_ERR(gdrm))
		return PTR_ERR(gdrm);

	drm = &gdrm->drm;
	drm->mode_config.funcs = &gud_mode_config_funcs;
	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	gdrm->flags = le32_to_cpu(desc.flags);
	gdrm->compression = desc.compression & GUD_COMPRESSION_LZ4;

	if (gdrm->flags & GUD_DISPLAY_FLAG_FULL_UPDATE && gdrm->compression)
		return -EINVAL;

	mutex_init(&gdrm->ctrl_lock);
	mutex_init(&gdrm->damage_lock);
	INIT_WORK(&gdrm->work, gud_flush_work);
	gud_clear_damage(gdrm);

	ret = devm_add_action(dev, gud_free_buffers_and_mutex, gdrm);
	if (ret)
		return ret;

	drm->mode_config.min_width = le32_to_cpu(desc.min_width);
	drm->mode_config.max_width = le32_to_cpu(desc.max_width);
	drm->mode_config.min_height = le32_to_cpu(desc.min_height);
	drm->mode_config.max_height = le32_to_cpu(desc.max_height);

	formats_dev = devm_kmalloc(dev, GUD_FORMATS_MAX_NUM, GFP_KERNEL);
	/* Add room for emulated XRGB8888 */
	formats = devm_kmalloc_array(dev, GUD_FORMATS_MAX_NUM + 1, sizeof(*formats), GFP_KERNEL);
	if (!formats_dev || !formats)
		return -ENOMEM;

	ret = gud_usb_get(gdrm, GUD_REQ_GET_FORMATS, 0, formats_dev, GUD_FORMATS_MAX_NUM);
	if (ret < 0)
		return ret;

	num_formats_dev = ret;
	for (i = 0; i < num_formats_dev; i++) {
		const struct drm_format_info *info;
		size_t fmt_buf_size;
		u32 format;

		format = gud_to_fourcc(formats_dev[i]);
		if (!format) {
			drm_dbg(drm, "Unsupported format: 0x%02x\n", formats_dev[i]);
			continue;
		}

		if (format == GUD_DRM_FORMAT_R1)
			info = &gud_drm_format_r1;
		else if (format == GUD_DRM_FORMAT_XRGB1111)
			info = &gud_drm_format_xrgb1111;
		else
			info = drm_format_info(format);

		switch (format) {
		case GUD_DRM_FORMAT_R1:
			fallthrough;
		case DRM_FORMAT_R8:
			fallthrough;
		case GUD_DRM_FORMAT_XRGB1111:
			fallthrough;
		case DRM_FORMAT_RGB332:
			fallthrough;
		case DRM_FORMAT_RGB888:
			if (!xrgb8888_emulation_format)
				xrgb8888_emulation_format = info;
			break;
		case DRM_FORMAT_RGB565:
			rgb565_supported = true;
			if (!xrgb8888_emulation_format)
				xrgb8888_emulation_format = info;
			break;
		case DRM_FORMAT_XRGB8888:
			xrgb8888_supported = true;
			break;
		}

		fmt_buf_size = drm_format_info_min_pitch(info, 0, drm->mode_config.max_width) *
			       drm->mode_config.max_height;
		max_buffer_size = max(max_buffer_size, fmt_buf_size);

		if (format == GUD_DRM_FORMAT_R1 || format == GUD_DRM_FORMAT_XRGB1111)
			continue; /* Internal not for userspace */

		formats[num_formats++] = format;
	}

	if (!num_formats && !xrgb8888_emulation_format) {
		dev_err(dev, "No supported pixel formats found\n");
		return -EINVAL;
	}

	/* Prefer speed over color depth */
	if (rgb565_supported)
		drm->mode_config.preferred_depth = 16;

	if (!xrgb8888_supported && xrgb8888_emulation_format) {
		gdrm->xrgb8888_emulation_format = xrgb8888_emulation_format;
		formats[num_formats++] = DRM_FORMAT_XRGB8888;
	}

	if (desc.max_buffer_size)
		max_buffer_size = le32_to_cpu(desc.max_buffer_size);
	/* Prevent a misbehaving device from allocating loads of RAM. 4096x4096@XRGB8888 = 64 MB */
	if (max_buffer_size > SZ_64M)
		max_buffer_size = SZ_64M;

	gdrm->bulk_pipe = usb_sndbulkpipe(interface_to_usbdev(intf), usb_endpoint_num(bulk_out));
	gdrm->bulk_len = max_buffer_size;

	ret = gud_alloc_bulk_buffer(gdrm);
	if (ret)
		return ret;

	if (gdrm->compression & GUD_COMPRESSION_LZ4) {
		gdrm->lz4_comp_mem = devm_kmalloc(dev, LZ4_MEM_COMPRESS, GFP_KERNEL);
		if (!gdrm->lz4_comp_mem)
			return -ENOMEM;

		gdrm->compress_buf = vmalloc(gdrm->bulk_len);
		if (!gdrm->compress_buf)
			return -ENOMEM;
	}

	ret = drm_simple_display_pipe_init(drm, &gdrm->pipe, &gud_pipe_funcs,
					   formats, num_formats,
					   gud_pipe_modifiers, NULL);
	if (ret)
		return ret;

	devm_kfree(dev, formats);
	devm_kfree(dev, formats_dev);

	ret = gud_get_properties(gdrm);
	if (ret) {
		dev_err(dev, "Failed to get properties (error=%d)\n", ret);
		return ret;
	}

	drm_plane_enable_fb_damage_clips(&gdrm->pipe.plane);

	ret = gud_get_connectors(gdrm);
	if (ret) {
		dev_err(dev, "Failed to get connectors (error=%d)\n", ret);
		return ret;
	}

	drm_mode_config_reset(drm);

	usb_set_intfdata(intf, gdrm);

	gdrm->dmadev = usb_intf_get_dma_device(intf);
	if (!gdrm->dmadev)
		dev_warn(dev, "buffer sharing not supported");

	drm_debugfs_add_file(drm, "stats", gud_stats_debugfs, NULL);

	ret = drm_dev_register(drm, 0);
	if (ret) {
		put_device(gdrm->dmadev);
		return ret;
	}

	drm_kms_helper_poll_init(drm);

	drm_client_setup(drm, NULL);

	return 0;
}

static void gud_disconnect(struct usb_interface *interface)
{
	struct gud_device *gdrm = usb_get_intfdata(interface);
	struct drm_device *drm = &gdrm->drm;

	drm_dbg(drm, "%s:\n", __func__);

	drm_kms_helper_poll_fini(drm);
	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
	put_device(gdrm->dmadev);
	gdrm->dmadev = NULL;
}

static int gud_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct gud_device *gdrm = usb_get_intfdata(intf);

	return drm_mode_config_helper_suspend(&gdrm->drm);
}

static int gud_resume(struct usb_interface *intf)
{
	struct gud_device *gdrm = usb_get_intfdata(intf);

	drm_mode_config_helper_resume(&gdrm->drm);

	return 0;
}

static const struct usb_device_id gud_id_table[] = {
	{ USB_DEVICE_INTERFACE_CLASS(0x1d50, 0x614d, USB_CLASS_VENDOR_SPEC) },
	{ USB_DEVICE_INTERFACE_CLASS(0x16d0, 0x10a9, USB_CLASS_VENDOR_SPEC) },
	{ }
};

MODULE_DEVICE_TABLE(usb, gud_id_table);

static struct usb_driver gud_usb_driver = {
	.name		= "gud",
	.probe		= gud_probe,
	.disconnect	= gud_disconnect,
	.id_table	= gud_id_table,
	.suspend	= gud_suspend,
	.resume		= gud_resume,
	.reset_resume	= gud_resume,
};

module_usb_driver(gud_usb_driver);

MODULE_AUTHOR("Noralf Trønnes");
MODULE_DESCRIPTION("GUD USB Display driver");
MODULE_LICENSE("Dual MIT/GPL");
