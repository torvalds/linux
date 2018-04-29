/*
 * MIPI Display Bus Interface (DBI) LCD controller support
 *
 * Copyright 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/tinydrm/mipi-dbi.h>
#include <drm/tinydrm/tinydrm-helpers.h>
#include <linux/debugfs.h>
#include <linux/dma-buf.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

#define MIPI_DBI_MAX_SPI_READ_SPEED 2000000 /* 2MHz */

#define DCS_POWER_MODE_DISPLAY			BIT(2)
#define DCS_POWER_MODE_DISPLAY_NORMAL_MODE	BIT(3)
#define DCS_POWER_MODE_SLEEP_MODE		BIT(4)
#define DCS_POWER_MODE_PARTIAL_MODE		BIT(5)
#define DCS_POWER_MODE_IDLE_MODE		BIT(6)
#define DCS_POWER_MODE_RESERVED_MASK		(BIT(0) | BIT(1) | BIT(7))

/**
 * DOC: overview
 *
 * This library provides helpers for MIPI Display Bus Interface (DBI)
 * compatible display controllers.
 *
 * Many controllers for tiny lcd displays are MIPI compliant and can use this
 * library. If a controller uses registers 0x2A and 0x2B to set the area to
 * update and uses register 0x2C to write to frame memory, it is most likely
 * MIPI compliant.
 *
 * Only MIPI Type 1 displays are supported since a full frame memory is needed.
 *
 * There are 3 MIPI DBI implementation types:
 *
 * A. Motorola 6800 type parallel bus
 *
 * B. Intel 8080 type parallel bus
 *
 * C. SPI type with 3 options:
 *
 *    1. 9-bit with the Data/Command signal as the ninth bit
 *    2. Same as above except it's sent as 16 bits
 *    3. 8-bit with the Data/Command signal as a separate D/CX pin
 *
 * Currently mipi_dbi only supports Type C options 1 and 3 with
 * mipi_dbi_spi_init().
 */

#define MIPI_DBI_DEBUG_COMMAND(cmd, data, len) \
({ \
	if (!len) \
		DRM_DEBUG_DRIVER("cmd=%02x\n", cmd); \
	else if (len <= 32) \
		DRM_DEBUG_DRIVER("cmd=%02x, par=%*ph\n", cmd, (int)len, data);\
	else \
		DRM_DEBUG_DRIVER("cmd=%02x, len=%zu\n", cmd, len); \
})

static const u8 mipi_dbi_dcs_read_commands[] = {
	MIPI_DCS_GET_DISPLAY_ID,
	MIPI_DCS_GET_RED_CHANNEL,
	MIPI_DCS_GET_GREEN_CHANNEL,
	MIPI_DCS_GET_BLUE_CHANNEL,
	MIPI_DCS_GET_DISPLAY_STATUS,
	MIPI_DCS_GET_POWER_MODE,
	MIPI_DCS_GET_ADDRESS_MODE,
	MIPI_DCS_GET_PIXEL_FORMAT,
	MIPI_DCS_GET_DISPLAY_MODE,
	MIPI_DCS_GET_SIGNAL_MODE,
	MIPI_DCS_GET_DIAGNOSTIC_RESULT,
	MIPI_DCS_READ_MEMORY_START,
	MIPI_DCS_READ_MEMORY_CONTINUE,
	MIPI_DCS_GET_SCANLINE,
	MIPI_DCS_GET_DISPLAY_BRIGHTNESS,
	MIPI_DCS_GET_CONTROL_DISPLAY,
	MIPI_DCS_GET_POWER_SAVE,
	MIPI_DCS_GET_CABC_MIN_BRIGHTNESS,
	MIPI_DCS_READ_DDB_START,
	MIPI_DCS_READ_DDB_CONTINUE,
	0, /* sentinel */
};

static bool mipi_dbi_command_is_read(struct mipi_dbi *mipi, u8 cmd)
{
	unsigned int i;

	if (!mipi->read_commands)
		return false;

	for (i = 0; i < 0xff; i++) {
		if (!mipi->read_commands[i])
			return false;
		if (cmd == mipi->read_commands[i])
			return true;
	}

	return false;
}

/**
 * mipi_dbi_command_read - MIPI DCS read command
 * @mipi: MIPI structure
 * @cmd: Command
 * @val: Value read
 *
 * Send MIPI DCS read command to the controller.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int mipi_dbi_command_read(struct mipi_dbi *mipi, u8 cmd, u8 *val)
{
	if (!mipi->read_commands)
		return -EACCES;

	if (!mipi_dbi_command_is_read(mipi, cmd))
		return -EINVAL;

	return mipi_dbi_command_buf(mipi, cmd, val, 1);
}
EXPORT_SYMBOL(mipi_dbi_command_read);

/**
 * mipi_dbi_command_buf - MIPI DCS command with parameter(s) in an array
 * @mipi: MIPI structure
 * @cmd: Command
 * @data: Parameter buffer
 * @len: Buffer length
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int mipi_dbi_command_buf(struct mipi_dbi *mipi, u8 cmd, u8 *data, size_t len)
{
	int ret;

	mutex_lock(&mipi->cmdlock);
	ret = mipi->command(mipi, cmd, data, len);
	mutex_unlock(&mipi->cmdlock);

	return ret;
}
EXPORT_SYMBOL(mipi_dbi_command_buf);

/**
 * mipi_dbi_buf_copy - Copy a framebuffer, transforming it if necessary
 * @dst: The destination buffer
 * @fb: The source framebuffer
 * @clip: Clipping rectangle of the area to be copied
 * @swap: When true, swap MSB/LSB of 16-bit values
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int mipi_dbi_buf_copy(void *dst, struct drm_framebuffer *fb,
		      struct drm_clip_rect *clip, bool swap)
{
	struct drm_gem_cma_object *cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	struct dma_buf_attachment *import_attach = cma_obj->base.import_attach;
	struct drm_format_name_buf format_name;
	void *src = cma_obj->vaddr;
	int ret = 0;

	if (import_attach) {
		ret = dma_buf_begin_cpu_access(import_attach->dmabuf,
					       DMA_FROM_DEVICE);
		if (ret)
			return ret;
	}

	switch (fb->format->format) {
	case DRM_FORMAT_RGB565:
		if (swap)
			tinydrm_swab16(dst, src, fb, clip);
		else
			tinydrm_memcpy(dst, src, fb, clip);
		break;
	case DRM_FORMAT_XRGB8888:
		tinydrm_xrgb8888_to_rgb565(dst, src, fb, clip, swap);
		break;
	default:
		dev_err_once(fb->dev->dev, "Format is not supported: %s\n",
			     drm_get_format_name(fb->format->format,
						 &format_name));
		return -EINVAL;
	}

	if (import_attach)
		ret = dma_buf_end_cpu_access(import_attach->dmabuf,
					     DMA_FROM_DEVICE);
	return ret;
}
EXPORT_SYMBOL(mipi_dbi_buf_copy);

static int mipi_dbi_fb_dirty(struct drm_framebuffer *fb,
			     struct drm_file *file_priv,
			     unsigned int flags, unsigned int color,
			     struct drm_clip_rect *clips,
			     unsigned int num_clips)
{
	struct drm_gem_cma_object *cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	struct tinydrm_device *tdev = fb->dev->dev_private;
	struct mipi_dbi *mipi = mipi_dbi_from_tinydrm(tdev);
	bool swap = mipi->swap_bytes;
	struct drm_clip_rect clip;
	int ret = 0;
	bool full;
	void *tr;

	if (!mipi->enabled)
		return 0;

	full = tinydrm_merge_clips(&clip, clips, num_clips, flags,
				   fb->width, fb->height);

	DRM_DEBUG("Flushing [FB:%d] x1=%u, x2=%u, y1=%u, y2=%u\n", fb->base.id,
		  clip.x1, clip.x2, clip.y1, clip.y2);

	if (!mipi->dc || !full || swap ||
	    fb->format->format == DRM_FORMAT_XRGB8888) {
		tr = mipi->tx_buf;
		ret = mipi_dbi_buf_copy(mipi->tx_buf, fb, &clip, swap);
		if (ret)
			return ret;
	} else {
		tr = cma_obj->vaddr;
	}

	mipi_dbi_command(mipi, MIPI_DCS_SET_COLUMN_ADDRESS,
			 (clip.x1 >> 8) & 0xFF, clip.x1 & 0xFF,
			 (clip.x2 >> 8) & 0xFF, (clip.x2 - 1) & 0xFF);
	mipi_dbi_command(mipi, MIPI_DCS_SET_PAGE_ADDRESS,
			 (clip.y1 >> 8) & 0xFF, clip.y1 & 0xFF,
			 (clip.y2 >> 8) & 0xFF, (clip.y2 - 1) & 0xFF);

	ret = mipi_dbi_command_buf(mipi, MIPI_DCS_WRITE_MEMORY_START, tr,
				(clip.x2 - clip.x1) * (clip.y2 - clip.y1) * 2);

	return ret;
}

static const struct drm_framebuffer_funcs mipi_dbi_fb_funcs = {
	.destroy	= drm_gem_fb_destroy,
	.create_handle	= drm_gem_fb_create_handle,
	.dirty		= tinydrm_fb_dirty,
};

/**
 * mipi_dbi_enable_flush - MIPI DBI enable helper
 * @mipi: MIPI DBI structure
 *
 * This function sets &mipi_dbi->enabled, flushes the whole framebuffer and
 * enables the backlight. Drivers can use this in their
 * &drm_simple_display_pipe_funcs->enable callback.
 */
void mipi_dbi_enable_flush(struct mipi_dbi *mipi,
			   struct drm_crtc_state *crtc_state,
			   struct drm_plane_state *plane_state)
{
	struct tinydrm_device *tdev = &mipi->tinydrm;
	struct drm_framebuffer *fb = plane_state->fb;

	mipi->enabled = true;
	if (fb)
		tdev->fb_dirty(fb, NULL, 0, 0, NULL, 0);

	backlight_enable(mipi->backlight);
}
EXPORT_SYMBOL(mipi_dbi_enable_flush);

static void mipi_dbi_blank(struct mipi_dbi *mipi)
{
	struct drm_device *drm = mipi->tinydrm.drm;
	u16 height = drm->mode_config.min_height;
	u16 width = drm->mode_config.min_width;
	size_t len = width * height * 2;

	memset(mipi->tx_buf, 0, len);

	mipi_dbi_command(mipi, MIPI_DCS_SET_COLUMN_ADDRESS, 0, 0,
			 (width >> 8) & 0xFF, (width - 1) & 0xFF);
	mipi_dbi_command(mipi, MIPI_DCS_SET_PAGE_ADDRESS, 0, 0,
			 (height >> 8) & 0xFF, (height - 1) & 0xFF);
	mipi_dbi_command_buf(mipi, MIPI_DCS_WRITE_MEMORY_START,
			     (u8 *)mipi->tx_buf, len);
}

/**
 * mipi_dbi_pipe_disable - MIPI DBI pipe disable helper
 * @pipe: Display pipe
 *
 * This function disables backlight if present, if not the display memory is
 * blanked. The regulator is disabled if in use. Drivers can use this as their
 * &drm_simple_display_pipe_funcs->disable callback.
 */
void mipi_dbi_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct tinydrm_device *tdev = pipe_to_tinydrm(pipe);
	struct mipi_dbi *mipi = mipi_dbi_from_tinydrm(tdev);

	DRM_DEBUG_KMS("\n");

	mipi->enabled = false;

	if (mipi->backlight)
		backlight_disable(mipi->backlight);
	else
		mipi_dbi_blank(mipi);

	if (mipi->regulator)
		regulator_disable(mipi->regulator);
}
EXPORT_SYMBOL(mipi_dbi_pipe_disable);

static const uint32_t mipi_dbi_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
};

/**
 * mipi_dbi_init - MIPI DBI initialization
 * @dev: Parent device
 * @mipi: &mipi_dbi structure to initialize
 * @pipe_funcs: Display pipe functions
 * @driver: DRM driver
 * @mode: Display mode
 * @rotation: Initial rotation in degrees Counter Clock Wise
 *
 * This function initializes a &mipi_dbi structure and it's underlying
 * @tinydrm_device. It also sets up the display pipeline.
 *
 * Supported formats: Native RGB565 and emulated XRGB8888.
 *
 * Objects created by this function will be automatically freed on driver
 * detach (devres).
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int mipi_dbi_init(struct device *dev, struct mipi_dbi *mipi,
		  const struct drm_simple_display_pipe_funcs *pipe_funcs,
		  struct drm_driver *driver,
		  const struct drm_display_mode *mode, unsigned int rotation)
{
	size_t bufsize = mode->vdisplay * mode->hdisplay * sizeof(u16);
	struct tinydrm_device *tdev = &mipi->tinydrm;
	int ret;

	if (!mipi->command)
		return -EINVAL;

	mutex_init(&mipi->cmdlock);

	mipi->tx_buf = devm_kmalloc(dev, bufsize, GFP_KERNEL);
	if (!mipi->tx_buf)
		return -ENOMEM;

	ret = devm_tinydrm_init(dev, tdev, &mipi_dbi_fb_funcs, driver);
	if (ret)
		return ret;

	tdev->fb_dirty = mipi_dbi_fb_dirty;

	/* TODO: Maybe add DRM_MODE_CONNECTOR_SPI */
	ret = tinydrm_display_pipe_init(tdev, pipe_funcs,
					DRM_MODE_CONNECTOR_VIRTUAL,
					mipi_dbi_formats,
					ARRAY_SIZE(mipi_dbi_formats), mode,
					rotation);
	if (ret)
		return ret;

	tdev->drm->mode_config.preferred_depth = 16;
	mipi->rotation = rotation;

	drm_mode_config_reset(tdev->drm);

	DRM_DEBUG_KMS("preferred_depth=%u, rotation = %u\n",
		      tdev->drm->mode_config.preferred_depth, rotation);

	return 0;
}
EXPORT_SYMBOL(mipi_dbi_init);

/**
 * mipi_dbi_hw_reset - Hardware reset of controller
 * @mipi: MIPI DBI structure
 *
 * Reset controller if the &mipi_dbi->reset gpio is set.
 */
void mipi_dbi_hw_reset(struct mipi_dbi *mipi)
{
	if (!mipi->reset)
		return;

	gpiod_set_value_cansleep(mipi->reset, 0);
	usleep_range(20, 1000);
	gpiod_set_value_cansleep(mipi->reset, 1);
	msleep(120);
}
EXPORT_SYMBOL(mipi_dbi_hw_reset);

/**
 * mipi_dbi_display_is_on - Check if display is on
 * @mipi: MIPI DBI structure
 *
 * This function checks the Power Mode register (if readable) to see if
 * display output is turned on. This can be used to see if the bootloader
 * has already turned on the display avoiding flicker when the pipeline is
 * enabled.
 *
 * Returns:
 * true if the display can be verified to be on, false otherwise.
 */
bool mipi_dbi_display_is_on(struct mipi_dbi *mipi)
{
	u8 val;

	if (mipi_dbi_command_read(mipi, MIPI_DCS_GET_POWER_MODE, &val))
		return false;

	val &= ~DCS_POWER_MODE_RESERVED_MASK;

	/* The poweron/reset value is 08h DCS_POWER_MODE_DISPLAY_NORMAL_MODE */
	if (val != (DCS_POWER_MODE_DISPLAY |
	    DCS_POWER_MODE_DISPLAY_NORMAL_MODE | DCS_POWER_MODE_SLEEP_MODE))
		return false;

	DRM_DEBUG_DRIVER("Display is ON\n");

	return true;
}
EXPORT_SYMBOL(mipi_dbi_display_is_on);

static int mipi_dbi_poweron_reset_conditional(struct mipi_dbi *mipi, bool cond)
{
	struct device *dev = mipi->tinydrm.drm->dev;
	int ret;

	if (mipi->regulator) {
		ret = regulator_enable(mipi->regulator);
		if (ret) {
			DRM_DEV_ERROR(dev, "Failed to enable regulator (%d)\n", ret);
			return ret;
		}
	}

	if (cond && mipi_dbi_display_is_on(mipi))
		return 1;

	mipi_dbi_hw_reset(mipi);
	ret = mipi_dbi_command(mipi, MIPI_DCS_SOFT_RESET);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to send reset command (%d)\n", ret);
		if (mipi->regulator)
			regulator_disable(mipi->regulator);
		return ret;
	}

	/*
	 * If we did a hw reset, we know the controller is in Sleep mode and
	 * per MIPI DSC spec should wait 5ms after soft reset. If we didn't,
	 * we assume worst case and wait 120ms.
	 */
	if (mipi->reset)
		usleep_range(5000, 20000);
	else
		msleep(120);

	return 0;
}

/**
 * mipi_dbi_poweron_reset - MIPI DBI poweron and reset
 * @mipi: MIPI DBI structure
 *
 * This function enables the regulator if used and does a hardware and software
 * reset.
 *
 * Returns:
 * Zero on success, or a negative error code.
 */
int mipi_dbi_poweron_reset(struct mipi_dbi *mipi)
{
	return mipi_dbi_poweron_reset_conditional(mipi, false);
}
EXPORT_SYMBOL(mipi_dbi_poweron_reset);

/**
 * mipi_dbi_poweron_conditional_reset - MIPI DBI poweron and conditional reset
 * @mipi: MIPI DBI structure
 *
 * This function enables the regulator if used and if the display is off, it
 * does a hardware and software reset. If mipi_dbi_display_is_on() determines
 * that the display is on, no reset is performed.
 *
 * Returns:
 * Zero if the controller was reset, 1 if the display was already on, or a
 * negative error code.
 */
int mipi_dbi_poweron_conditional_reset(struct mipi_dbi *mipi)
{
	return mipi_dbi_poweron_reset_conditional(mipi, true);
}
EXPORT_SYMBOL(mipi_dbi_poweron_conditional_reset);

#if IS_ENABLED(CONFIG_SPI)

/**
 * mipi_dbi_spi_cmd_max_speed - get the maximum SPI bus speed
 * @spi: SPI device
 * @len: The transfer buffer length.
 *
 * Many controllers have a max speed of 10MHz, but can be pushed way beyond
 * that. Increase reliability by running pixel data at max speed and the rest
 * at 10MHz, preventing transfer glitches from messing up the init settings.
 */
u32 mipi_dbi_spi_cmd_max_speed(struct spi_device *spi, size_t len)
{
	if (len > 64)
		return 0; /* use default */

	return min_t(u32, 10000000, spi->max_speed_hz);
}
EXPORT_SYMBOL(mipi_dbi_spi_cmd_max_speed);

/*
 * MIPI DBI Type C Option 1
 *
 * If the SPI controller doesn't have 9 bits per word support,
 * use blocks of 9 bytes to send 8x 9-bit words using a 8-bit SPI transfer.
 * Pad partial blocks with MIPI_DCS_NOP (zero).
 * This is how the D/C bit (x) is added:
 *     x7654321
 *     0x765432
 *     10x76543
 *     210x7654
 *     3210x765
 *     43210x76
 *     543210x7
 *     6543210x
 *     76543210
 */

static int mipi_dbi_spi1e_transfer(struct mipi_dbi *mipi, int dc,
				   const void *buf, size_t len,
				   unsigned int bpw)
{
	bool swap_bytes = (bpw == 16 && tinydrm_machine_little_endian());
	size_t chunk, max_chunk = mipi->tx_buf9_len;
	struct spi_device *spi = mipi->spi;
	struct spi_transfer tr = {
		.tx_buf = mipi->tx_buf9,
		.bits_per_word = 8,
	};
	struct spi_message m;
	const u8 *src = buf;
	int i, ret;
	u8 *dst;

	if (drm_debug & DRM_UT_DRIVER)
		pr_debug("[drm:%s] dc=%d, max_chunk=%zu, transfers:\n",
			 __func__, dc, max_chunk);

	tr.speed_hz = mipi_dbi_spi_cmd_max_speed(spi, len);
	spi_message_init_with_transfers(&m, &tr, 1);

	if (!dc) {
		if (WARN_ON_ONCE(len != 1))
			return -EINVAL;

		/* Command: pad no-op's (zeroes) at beginning of block */
		dst = mipi->tx_buf9;
		memset(dst, 0, 9);
		dst[8] = *src;
		tr.len = 9;

		tinydrm_dbg_spi_message(spi, &m);

		return spi_sync(spi, &m);
	}

	/* max with room for adding one bit per byte */
	max_chunk = max_chunk / 9 * 8;
	/* but no bigger than len */
	max_chunk = min(max_chunk, len);
	/* 8 byte blocks */
	max_chunk = max_t(size_t, 8, max_chunk & ~0x7);

	while (len) {
		size_t added = 0;

		chunk = min(len, max_chunk);
		len -= chunk;
		dst = mipi->tx_buf9;

		if (chunk < 8) {
			u8 val, carry = 0;

			/* Data: pad no-op's (zeroes) at end of block */
			memset(dst, 0, 9);

			if (swap_bytes) {
				for (i = 1; i < (chunk + 1); i++) {
					val = src[1];
					*dst++ = carry | BIT(8 - i) | (val >> i);
					carry = val << (8 - i);
					i++;
					val = src[0];
					*dst++ = carry | BIT(8 - i) | (val >> i);
					carry = val << (8 - i);
					src += 2;
				}
				*dst++ = carry;
			} else {
				for (i = 1; i < (chunk + 1); i++) {
					val = *src++;
					*dst++ = carry | BIT(8 - i) | (val >> i);
					carry = val << (8 - i);
				}
				*dst++ = carry;
			}

			chunk = 8;
			added = 1;
		} else {
			for (i = 0; i < chunk; i += 8) {
				if (swap_bytes) {
					*dst++ =                 BIT(7) | (src[1] >> 1);
					*dst++ = (src[1] << 7) | BIT(6) | (src[0] >> 2);
					*dst++ = (src[0] << 6) | BIT(5) | (src[3] >> 3);
					*dst++ = (src[3] << 5) | BIT(4) | (src[2] >> 4);
					*dst++ = (src[2] << 4) | BIT(3) | (src[5] >> 5);
					*dst++ = (src[5] << 3) | BIT(2) | (src[4] >> 6);
					*dst++ = (src[4] << 2) | BIT(1) | (src[7] >> 7);
					*dst++ = (src[7] << 1) | BIT(0);
					*dst++ = src[6];
				} else {
					*dst++ =                 BIT(7) | (src[0] >> 1);
					*dst++ = (src[0] << 7) | BIT(6) | (src[1] >> 2);
					*dst++ = (src[1] << 6) | BIT(5) | (src[2] >> 3);
					*dst++ = (src[2] << 5) | BIT(4) | (src[3] >> 4);
					*dst++ = (src[3] << 4) | BIT(3) | (src[4] >> 5);
					*dst++ = (src[4] << 3) | BIT(2) | (src[5] >> 6);
					*dst++ = (src[5] << 2) | BIT(1) | (src[6] >> 7);
					*dst++ = (src[6] << 1) | BIT(0);
					*dst++ = src[7];
				}

				src += 8;
				added++;
			}
		}

		tr.len = chunk + added;

		tinydrm_dbg_spi_message(spi, &m);
		ret = spi_sync(spi, &m);
		if (ret)
			return ret;
	}

	return 0;
}

static int mipi_dbi_spi1_transfer(struct mipi_dbi *mipi, int dc,
				  const void *buf, size_t len,
				  unsigned int bpw)
{
	struct spi_device *spi = mipi->spi;
	struct spi_transfer tr = {
		.bits_per_word = 9,
	};
	const u16 *src16 = buf;
	const u8 *src8 = buf;
	struct spi_message m;
	size_t max_chunk;
	u16 *dst16;
	int ret;

	if (!tinydrm_spi_bpw_supported(spi, 9))
		return mipi_dbi_spi1e_transfer(mipi, dc, buf, len, bpw);

	tr.speed_hz = mipi_dbi_spi_cmd_max_speed(spi, len);
	max_chunk = mipi->tx_buf9_len;
	dst16 = mipi->tx_buf9;

	if (drm_debug & DRM_UT_DRIVER)
		pr_debug("[drm:%s] dc=%d, max_chunk=%zu, transfers:\n",
			 __func__, dc, max_chunk);

	max_chunk = min(max_chunk / 2, len);

	spi_message_init_with_transfers(&m, &tr, 1);
	tr.tx_buf = dst16;

	while (len) {
		size_t chunk = min(len, max_chunk);
		unsigned int i;

		if (bpw == 16 && tinydrm_machine_little_endian()) {
			for (i = 0; i < (chunk * 2); i += 2) {
				dst16[i]     = *src16 >> 8;
				dst16[i + 1] = *src16++ & 0xFF;
				if (dc) {
					dst16[i]     |= 0x0100;
					dst16[i + 1] |= 0x0100;
				}
			}
		} else {
			for (i = 0; i < chunk; i++) {
				dst16[i] = *src8++;
				if (dc)
					dst16[i] |= 0x0100;
			}
		}

		tr.len = chunk;
		len -= chunk;

		tinydrm_dbg_spi_message(spi, &m);
		ret = spi_sync(spi, &m);
		if (ret)
			return ret;
	}

	return 0;
}

static int mipi_dbi_typec1_command(struct mipi_dbi *mipi, u8 cmd,
				   u8 *parameters, size_t num)
{
	unsigned int bpw = (cmd == MIPI_DCS_WRITE_MEMORY_START) ? 16 : 8;
	int ret;

	if (mipi_dbi_command_is_read(mipi, cmd))
		return -ENOTSUPP;

	MIPI_DBI_DEBUG_COMMAND(cmd, parameters, num);

	ret = mipi_dbi_spi1_transfer(mipi, 0, &cmd, 1, 8);
	if (ret || !num)
		return ret;

	return mipi_dbi_spi1_transfer(mipi, 1, parameters, num, bpw);
}

/* MIPI DBI Type C Option 3 */

static int mipi_dbi_typec3_command_read(struct mipi_dbi *mipi, u8 cmd,
					u8 *data, size_t len)
{
	struct spi_device *spi = mipi->spi;
	u32 speed_hz = min_t(u32, MIPI_DBI_MAX_SPI_READ_SPEED,
			     spi->max_speed_hz / 2);
	struct spi_transfer tr[2] = {
		{
			.speed_hz = speed_hz,
			.tx_buf = &cmd,
			.len = 1,
		}, {
			.speed_hz = speed_hz,
			.len = len,
		},
	};
	struct spi_message m;
	u8 *buf;
	int ret;

	if (!len)
		return -EINVAL;

	/*
	 * Support non-standard 24-bit and 32-bit Nokia read commands which
	 * start with a dummy clock, so we need to read an extra byte.
	 */
	if (cmd == MIPI_DCS_GET_DISPLAY_ID ||
	    cmd == MIPI_DCS_GET_DISPLAY_STATUS) {
		if (!(len == 3 || len == 4))
			return -EINVAL;

		tr[1].len = len + 1;
	}

	buf = kmalloc(tr[1].len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	tr[1].rx_buf = buf;
	gpiod_set_value_cansleep(mipi->dc, 0);

	spi_message_init_with_transfers(&m, tr, ARRAY_SIZE(tr));
	ret = spi_sync(spi, &m);
	if (ret)
		goto err_free;

	tinydrm_dbg_spi_message(spi, &m);

	if (tr[1].len == len) {
		memcpy(data, buf, len);
	} else {
		unsigned int i;

		for (i = 0; i < len; i++)
			data[i] = (buf[i] << 1) | !!(buf[i + 1] & BIT(7));
	}

	MIPI_DBI_DEBUG_COMMAND(cmd, data, len);

err_free:
	kfree(buf);

	return ret;
}

static int mipi_dbi_typec3_command(struct mipi_dbi *mipi, u8 cmd,
				   u8 *par, size_t num)
{
	struct spi_device *spi = mipi->spi;
	unsigned int bpw = 8;
	u32 speed_hz;
	int ret;

	if (mipi_dbi_command_is_read(mipi, cmd))
		return mipi_dbi_typec3_command_read(mipi, cmd, par, num);

	MIPI_DBI_DEBUG_COMMAND(cmd, par, num);

	gpiod_set_value_cansleep(mipi->dc, 0);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, 1);
	ret = tinydrm_spi_transfer(spi, speed_hz, NULL, 8, &cmd, 1);
	if (ret || !num)
		return ret;

	if (cmd == MIPI_DCS_WRITE_MEMORY_START && !mipi->swap_bytes)
		bpw = 16;

	gpiod_set_value_cansleep(mipi->dc, 1);
	speed_hz = mipi_dbi_spi_cmd_max_speed(spi, num);

	return tinydrm_spi_transfer(spi, speed_hz, NULL, bpw, par, num);
}

/**
 * mipi_dbi_spi_init - Initialize MIPI DBI SPI interfaced controller
 * @spi: SPI device
 * @mipi: &mipi_dbi structure to initialize
 * @dc: D/C gpio (optional)
 *
 * This function sets &mipi_dbi->command, enables &mipi->read_commands for the
 * usual read commands. It should be followed by a call to mipi_dbi_init() or
 * a driver-specific init.
 *
 * If @dc is set, a Type C Option 3 interface is assumed, if not
 * Type C Option 1.
 *
 * If the SPI master driver doesn't support the necessary bits per word,
 * the following transformation is used:
 *
 * - 9-bit: reorder buffer as 9x 8-bit words, padded with no-op command.
 * - 16-bit: if big endian send as 8-bit, if little endian swap bytes
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int mipi_dbi_spi_init(struct spi_device *spi, struct mipi_dbi *mipi,
		      struct gpio_desc *dc)
{
	size_t tx_size = tinydrm_spi_max_transfer_size(spi, 0);
	struct device *dev = &spi->dev;
	int ret;

	if (tx_size < 16) {
		DRM_ERROR("SPI transmit buffer too small: %zu\n", tx_size);
		return -EINVAL;
	}

	/*
	 * Even though it's not the SPI device that does DMA (the master does),
	 * the dma mask is necessary for the dma_alloc_wc() in
	 * drm_gem_cma_create(). The dma_addr returned will be a physical
	 * adddress which might be different from the bus address, but this is
	 * not a problem since the address will not be used.
	 * The virtual address is used in the transfer and the SPI core
	 * re-maps it on the SPI master device using the DMA streaming API
	 * (spi_map_buf()).
	 */
	if (!dev->coherent_dma_mask) {
		ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_warn(dev, "Failed to set dma mask %d\n", ret);
			return ret;
		}
	}

	mipi->spi = spi;
	mipi->read_commands = mipi_dbi_dcs_read_commands;

	if (dc) {
		mipi->command = mipi_dbi_typec3_command;
		mipi->dc = dc;
		if (tinydrm_machine_little_endian() &&
		    !tinydrm_spi_bpw_supported(spi, 16))
			mipi->swap_bytes = true;
	} else {
		mipi->command = mipi_dbi_typec1_command;
		mipi->tx_buf9_len = tx_size;
		mipi->tx_buf9 = devm_kmalloc(dev, tx_size, GFP_KERNEL);
		if (!mipi->tx_buf9)
			return -ENOMEM;
	}

	DRM_DEBUG_DRIVER("SPI speed: %uMHz\n", spi->max_speed_hz / 1000000);

	return 0;
}
EXPORT_SYMBOL(mipi_dbi_spi_init);

#endif /* CONFIG_SPI */

#ifdef CONFIG_DEBUG_FS

static ssize_t mipi_dbi_debugfs_command_write(struct file *file,
					      const char __user *ubuf,
					      size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct mipi_dbi *mipi = m->private;
	u8 val, cmd = 0, parameters[64];
	char *buf, *pos, *token;
	unsigned int i;
	int ret;

	buf = memdup_user_nul(ubuf, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	/* strip trailing whitespace */
	for (i = count - 1; i > 0; i--)
		if (isspace(buf[i]))
			buf[i] = '\0';
		else
			break;
	i = 0;
	pos = buf;
	while (pos) {
		token = strsep(&pos, " ");
		if (!token) {
			ret = -EINVAL;
			goto err_free;
		}

		ret = kstrtou8(token, 16, &val);
		if (ret < 0)
			goto err_free;

		if (token == buf)
			cmd = val;
		else
			parameters[i++] = val;

		if (i == 64) {
			ret = -E2BIG;
			goto err_free;
		}
	}

	ret = mipi_dbi_command_buf(mipi, cmd, parameters, i);

err_free:
	kfree(buf);

	return ret < 0 ? ret : count;
}

static int mipi_dbi_debugfs_command_show(struct seq_file *m, void *unused)
{
	struct mipi_dbi *mipi = m->private;
	u8 cmd, val[4];
	size_t len;
	int ret;

	for (cmd = 0; cmd < 255; cmd++) {
		if (!mipi_dbi_command_is_read(mipi, cmd))
			continue;

		switch (cmd) {
		case MIPI_DCS_READ_MEMORY_START:
		case MIPI_DCS_READ_MEMORY_CONTINUE:
			len = 2;
			break;
		case MIPI_DCS_GET_DISPLAY_ID:
			len = 3;
			break;
		case MIPI_DCS_GET_DISPLAY_STATUS:
			len = 4;
			break;
		default:
			len = 1;
			break;
		}

		seq_printf(m, "%02x: ", cmd);
		ret = mipi_dbi_command_buf(mipi, cmd, val, len);
		if (ret) {
			seq_puts(m, "XX\n");
			continue;
		}
		seq_printf(m, "%*phN\n", (int)len, val);
	}

	return 0;
}

static int mipi_dbi_debugfs_command_open(struct inode *inode,
					 struct file *file)
{
	return single_open(file, mipi_dbi_debugfs_command_show,
			   inode->i_private);
}

static const struct file_operations mipi_dbi_debugfs_command_fops = {
	.owner = THIS_MODULE,
	.open = mipi_dbi_debugfs_command_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = mipi_dbi_debugfs_command_write,
};

/**
 * mipi_dbi_debugfs_init - Create debugfs entries
 * @minor: DRM minor
 *
 * This function creates a 'command' debugfs file for sending commands to the
 * controller or getting the read command values.
 * Drivers can use this as their &drm_driver->debugfs_init callback.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int mipi_dbi_debugfs_init(struct drm_minor *minor)
{
	struct tinydrm_device *tdev = minor->dev->dev_private;
	struct mipi_dbi *mipi = mipi_dbi_from_tinydrm(tdev);
	umode_t mode = S_IFREG | S_IWUSR;

	if (mipi->read_commands)
		mode |= S_IRUGO;
	debugfs_create_file("command", mode, minor->debugfs_root, mipi,
			    &mipi_dbi_debugfs_command_fops);

	return 0;
}
EXPORT_SYMBOL(mipi_dbi_debugfs_init);

#endif

MODULE_LICENSE("GPL");
