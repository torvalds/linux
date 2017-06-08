/*
 * Copyright (C) 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/backlight.h>
#include <linux/dma-buf.h>
#include <linux/pm.h>
#include <linux/spi/spi.h>
#include <linux/swab.h>

#include <drm/tinydrm/tinydrm.h>
#include <drm/tinydrm/tinydrm-helpers.h>

static unsigned int spi_max;
module_param(spi_max, uint, 0400);
MODULE_PARM_DESC(spi_max, "Set a lower SPI max transfer size");

/**
 * tinydrm_merge_clips - Merge clip rectangles
 * @dst: Destination clip rectangle
 * @src: Source clip rectangle(s)
 * @num_clips: Number of @src clip rectangles
 * @flags: Dirty fb ioctl flags
 * @max_width: Maximum width of @dst
 * @max_height: Maximum height of @dst
 *
 * This function merges @src clip rectangle(s) into @dst. If @src is NULL,
 * @max_width and @min_width is used to set a full @dst clip rectangle.
 *
 * Returns:
 * true if it's a full clip, false otherwise
 */
bool tinydrm_merge_clips(struct drm_clip_rect *dst,
			 struct drm_clip_rect *src, unsigned int num_clips,
			 unsigned int flags, u32 max_width, u32 max_height)
{
	unsigned int i;

	if (!src || !num_clips) {
		dst->x1 = 0;
		dst->x2 = max_width;
		dst->y1 = 0;
		dst->y2 = max_height;
		return true;
	}

	dst->x1 = ~0;
	dst->y1 = ~0;
	dst->x2 = 0;
	dst->y2 = 0;

	for (i = 0; i < num_clips; i++) {
		if (flags & DRM_MODE_FB_DIRTY_ANNOTATE_COPY)
			i++;
		dst->x1 = min(dst->x1, src[i].x1);
		dst->x2 = max(dst->x2, src[i].x2);
		dst->y1 = min(dst->y1, src[i].y1);
		dst->y2 = max(dst->y2, src[i].y2);
	}

	if (dst->x2 > max_width || dst->y2 > max_height ||
	    dst->x1 >= dst->x2 || dst->y1 >= dst->y2) {
		DRM_DEBUG_KMS("Illegal clip: x1=%u, x2=%u, y1=%u, y2=%u\n",
			      dst->x1, dst->x2, dst->y1, dst->y2);
		dst->x1 = 0;
		dst->y1 = 0;
		dst->x2 = max_width;
		dst->y2 = max_height;
	}

	return (dst->x2 - dst->x1) == max_width &&
	       (dst->y2 - dst->y1) == max_height;
}
EXPORT_SYMBOL(tinydrm_merge_clips);

/**
 * tinydrm_memcpy - Copy clip buffer
 * @dst: Destination buffer
 * @vaddr: Source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 */
void tinydrm_memcpy(void *dst, void *vaddr, struct drm_framebuffer *fb,
		    struct drm_clip_rect *clip)
{
	unsigned int cpp = drm_format_plane_cpp(fb->format->format, 0);
	unsigned int pitch = fb->pitches[0];
	void *src = vaddr + (clip->y1 * pitch) + (clip->x1 * cpp);
	size_t len = (clip->x2 - clip->x1) * cpp;
	unsigned int y;

	for (y = clip->y1; y < clip->y2; y++) {
		memcpy(dst, src, len);
		src += pitch;
		dst += len;
	}
}
EXPORT_SYMBOL(tinydrm_memcpy);

/**
 * tinydrm_swab16 - Swap bytes into clip buffer
 * @dst: RGB565 destination buffer
 * @vaddr: RGB565 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 */
void tinydrm_swab16(u16 *dst, void *vaddr, struct drm_framebuffer *fb,
		    struct drm_clip_rect *clip)
{
	size_t len = (clip->x2 - clip->x1) * sizeof(u16);
	unsigned int x, y;
	u16 *src, *buf;

	/*
	 * The cma memory is write-combined so reads are uncached.
	 * Speed up by fetching one line at a time.
	 */
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return;

	for (y = clip->y1; y < clip->y2; y++) {
		src = vaddr + (y * fb->pitches[0]);
		src += clip->x1;
		memcpy(buf, src, len);
		src = buf;
		for (x = clip->x1; x < clip->x2; x++)
			*dst++ = swab16(*src++);
	}

	kfree(buf);
}
EXPORT_SYMBOL(tinydrm_swab16);

/**
 * tinydrm_xrgb8888_to_rgb565 - Convert XRGB8888 to RGB565 clip buffer
 * @dst: RGB565 destination buffer
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @swap: Swap bytes
 *
 * Drivers can use this function for RGB565 devices that don't natively
 * support XRGB8888.
 */
void tinydrm_xrgb8888_to_rgb565(u16 *dst, void *vaddr,
				struct drm_framebuffer *fb,
				struct drm_clip_rect *clip, bool swap)
{
	size_t len = (clip->x2 - clip->x1) * sizeof(u32);
	unsigned int x, y;
	u32 *src, *buf;
	u16 val16;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return;

	for (y = clip->y1; y < clip->y2; y++) {
		src = vaddr + (y * fb->pitches[0]);
		src += clip->x1;
		memcpy(buf, src, len);
		src = buf;
		for (x = clip->x1; x < clip->x2; x++) {
			val16 = ((*src & 0x00F80000) >> 8) |
				((*src & 0x0000FC00) >> 5) |
				((*src & 0x000000F8) >> 3);
			src++;
			if (swap)
				*dst++ = swab16(val16);
			else
				*dst++ = val16;
		}
	}

	kfree(buf);
}
EXPORT_SYMBOL(tinydrm_xrgb8888_to_rgb565);

/**
 * tinydrm_xrgb8888_to_gray8 - Convert XRGB8888 to grayscale
 * @dst: 8-bit grayscale destination buffer
 * @fb: DRM framebuffer
 *
 * Drm doesn't have native monochrome or grayscale support.
 * Such drivers can announce the commonly supported XR24 format to userspace
 * and use this function to convert to the native format.
 *
 * Monochrome drivers will use the most significant bit,
 * where 1 means foreground color and 0 background color.
 *
 * ITU BT.601 is used for the RGB -> luma (brightness) conversion.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int tinydrm_xrgb8888_to_gray8(u8 *dst, struct drm_framebuffer *fb)
{
	struct drm_gem_cma_object *cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	struct dma_buf_attachment *import_attach = cma_obj->base.import_attach;
	unsigned int x, y, pitch = fb->pitches[0];
	int ret = 0;
	void *buf;
	u32 *src;

	if (WARN_ON(fb->format->format != DRM_FORMAT_XRGB8888))
		return -EINVAL;
	/*
	 * The cma memory is write-combined so reads are uncached.
	 * Speed up by fetching one line at a time.
	 */
	buf = kmalloc(pitch, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (import_attach) {
		ret = dma_buf_begin_cpu_access(import_attach->dmabuf,
					       DMA_FROM_DEVICE);
		if (ret)
			goto err_free;
	}

	for (y = 0; y < fb->height; y++) {
		src = cma_obj->vaddr + (y * pitch);
		memcpy(buf, src, pitch);
		src = buf;
		for (x = 0; x < fb->width; x++) {
			u8 r = (*src & 0x00ff0000) >> 16;
			u8 g = (*src & 0x0000ff00) >> 8;
			u8 b =  *src & 0x000000ff;

			/* ITU BT.601: Y = 0.299 R + 0.587 G + 0.114 B */
			*dst++ = (3 * r + 6 * g + b) / 10;
			src++;
		}
	}

	if (import_attach)
		ret = dma_buf_end_cpu_access(import_attach->dmabuf,
					     DMA_FROM_DEVICE);
err_free:
	kfree(buf);

	return ret;
}
EXPORT_SYMBOL(tinydrm_xrgb8888_to_gray8);

/**
 * tinydrm_of_find_backlight - Find backlight device in device-tree
 * @dev: Device
 *
 * This function looks for a DT node pointed to by a property named 'backlight'
 * and uses of_find_backlight_by_node() to get the backlight device.
 * Additionally if the brightness property is zero, it is set to
 * max_brightness.
 *
 * Returns:
 * NULL if there's no backlight property.
 * Error pointer -EPROBE_DEFER if the DT node is found, but no backlight device
 * is found.
 * If the backlight device is found, a pointer to the structure is returned.
 */
struct backlight_device *tinydrm_of_find_backlight(struct device *dev)
{
	struct backlight_device *backlight;
	struct device_node *np;

	np = of_parse_phandle(dev->of_node, "backlight", 0);
	if (!np)
		return NULL;

	backlight = of_find_backlight_by_node(np);
	of_node_put(np);

	if (!backlight)
		return ERR_PTR(-EPROBE_DEFER);

	if (!backlight->props.brightness) {
		backlight->props.brightness = backlight->props.max_brightness;
		DRM_DEBUG_KMS("Backlight brightness set to %d\n",
			      backlight->props.brightness);
	}

	return backlight;
}
EXPORT_SYMBOL(tinydrm_of_find_backlight);

/**
 * tinydrm_enable_backlight - Enable backlight helper
 * @backlight: Backlight device
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int tinydrm_enable_backlight(struct backlight_device *backlight)
{
	unsigned int old_state;
	int ret;

	if (!backlight)
		return 0;

	old_state = backlight->props.state;
	backlight->props.state &= ~BL_CORE_FBBLANK;
	DRM_DEBUG_KMS("Backlight state: 0x%x -> 0x%x\n", old_state,
		      backlight->props.state);

	ret = backlight_update_status(backlight);
	if (ret)
		DRM_ERROR("Failed to enable backlight %d\n", ret);

	return ret;
}
EXPORT_SYMBOL(tinydrm_enable_backlight);

/**
 * tinydrm_disable_backlight - Disable backlight helper
 * @backlight: Backlight device
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int tinydrm_disable_backlight(struct backlight_device *backlight)
{
	unsigned int old_state;
	int ret;

	if (!backlight)
		return 0;

	old_state = backlight->props.state;
	backlight->props.state |= BL_CORE_FBBLANK;
	DRM_DEBUG_KMS("Backlight state: 0x%x -> 0x%x\n", old_state,
		      backlight->props.state);
	ret = backlight_update_status(backlight);
	if (ret)
		DRM_ERROR("Failed to disable backlight %d\n", ret);

	return ret;
}
EXPORT_SYMBOL(tinydrm_disable_backlight);

#if IS_ENABLED(CONFIG_SPI)

/**
 * tinydrm_spi_max_transfer_size - Determine max SPI transfer size
 * @spi: SPI device
 * @max_len: Maximum buffer size needed (optional)
 *
 * This function returns the maximum size to use for SPI transfers. It checks
 * the SPI master, the optional @max_len and the module parameter spi_max and
 * returns the smallest.
 *
 * Returns:
 * Maximum size for SPI transfers
 */
size_t tinydrm_spi_max_transfer_size(struct spi_device *spi, size_t max_len)
{
	size_t ret;

	ret = min(spi_max_transfer_size(spi), spi->master->max_dma_len);
	if (max_len)
		ret = min(ret, max_len);
	if (spi_max)
		ret = min_t(size_t, ret, spi_max);
	ret &= ~0x3;
	if (ret < 4)
		ret = 4;

	return ret;
}
EXPORT_SYMBOL(tinydrm_spi_max_transfer_size);

/**
 * tinydrm_spi_bpw_supported - Check if bits per word is supported
 * @spi: SPI device
 * @bpw: Bits per word
 *
 * This function checks to see if the SPI master driver supports @bpw.
 *
 * Returns:
 * True if @bpw is supported, false otherwise.
 */
bool tinydrm_spi_bpw_supported(struct spi_device *spi, u8 bpw)
{
	u32 bpw_mask = spi->master->bits_per_word_mask;

	if (bpw == 8)
		return true;

	if (!bpw_mask) {
		dev_warn_once(&spi->dev,
			      "bits_per_word_mask not set, assume 8-bit only\n");
		return false;
	}

	if (bpw_mask & SPI_BPW_MASK(bpw))
		return true;

	return false;
}
EXPORT_SYMBOL(tinydrm_spi_bpw_supported);

static void
tinydrm_dbg_spi_print(struct spi_device *spi, struct spi_transfer *tr,
		      const void *buf, int idx, bool tx)
{
	u32 speed_hz = tr->speed_hz ? tr->speed_hz : spi->max_speed_hz;
	char linebuf[3 * 32];

	hex_dump_to_buffer(buf, tr->len, 16,
			   DIV_ROUND_UP(tr->bits_per_word, 8),
			   linebuf, sizeof(linebuf), false);

	printk(KERN_DEBUG
	       "    tr(%i): speed=%u%s, bpw=%i, len=%u, %s_buf=[%s%s]\n", idx,
	       speed_hz > 1000000 ? speed_hz / 1000000 : speed_hz / 1000,
	       speed_hz > 1000000 ? "MHz" : "kHz", tr->bits_per_word, tr->len,
	       tx ? "tx" : "rx", linebuf, tr->len > 16 ? " ..." : "");
}

/* called through tinydrm_dbg_spi_message() */
void _tinydrm_dbg_spi_message(struct spi_device *spi, struct spi_message *m)
{
	struct spi_transfer *tmp;
	struct list_head *pos;
	int i = 0;

	list_for_each(pos, &m->transfers) {
		tmp = list_entry(pos, struct spi_transfer, transfer_list);

		if (tmp->tx_buf)
			tinydrm_dbg_spi_print(spi, tmp, tmp->tx_buf, i, true);
		if (tmp->rx_buf)
			tinydrm_dbg_spi_print(spi, tmp, tmp->rx_buf, i, false);
		i++;
	}
}
EXPORT_SYMBOL(_tinydrm_dbg_spi_message);

/**
 * tinydrm_spi_transfer - SPI transfer helper
 * @spi: SPI device
 * @speed_hz: Override speed (optional)
 * @header: Optional header transfer
 * @bpw: Bits per word
 * @buf: Buffer to transfer
 * @len: Buffer length
 *
 * This SPI transfer helper breaks up the transfer of @buf into chunks which
 * the SPI master driver can handle. If the machine is Little Endian and the
 * SPI master driver doesn't support 16 bits per word, it swaps the bytes and
 * does a 8-bit transfer.
 * If @header is set, it is prepended to each SPI message.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int tinydrm_spi_transfer(struct spi_device *spi, u32 speed_hz,
			 struct spi_transfer *header, u8 bpw, const void *buf,
			 size_t len)
{
	struct spi_transfer tr = {
		.bits_per_word = bpw,
		.speed_hz = speed_hz,
	};
	struct spi_message m;
	u16 *swap_buf = NULL;
	size_t max_chunk;
	size_t chunk;
	int ret = 0;

	if (WARN_ON_ONCE(bpw != 8 && bpw != 16))
		return -EINVAL;

	max_chunk = tinydrm_spi_max_transfer_size(spi, 0);

	if (drm_debug & DRM_UT_DRIVER)
		pr_debug("[drm:%s] bpw=%u, max_chunk=%zu, transfers:\n",
			 __func__, bpw, max_chunk);

	if (bpw == 16 && !tinydrm_spi_bpw_supported(spi, 16)) {
		tr.bits_per_word = 8;
		if (tinydrm_machine_little_endian()) {
			swap_buf = kmalloc(min(len, max_chunk), GFP_KERNEL);
			if (!swap_buf)
				return -ENOMEM;
		}
	}

	spi_message_init(&m);
	if (header)
		spi_message_add_tail(header, &m);
	spi_message_add_tail(&tr, &m);

	while (len) {
		chunk = min(len, max_chunk);

		tr.tx_buf = buf;
		tr.len = chunk;

		if (swap_buf) {
			const u16 *buf16 = buf;
			unsigned int i;

			for (i = 0; i < chunk / 2; i++)
				swap_buf[i] = swab16(buf16[i]);

			tr.tx_buf = swap_buf;
		}

		buf += chunk;
		len -= chunk;

		tinydrm_dbg_spi_message(spi, &m);
		ret = spi_sync(spi, &m);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(tinydrm_spi_transfer);

#endif /* CONFIG_SPI */
