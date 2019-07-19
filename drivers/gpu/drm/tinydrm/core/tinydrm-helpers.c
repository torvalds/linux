// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Noralf Trønnes
 */

#include <linux/backlight.h>
#include <linux/dma-buf.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/spi/spi.h>
#include <linux/swab.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_print.h>
#include <drm/drm_rect.h>
#include <drm/tinydrm/tinydrm-helpers.h>

#if IS_ENABLED(CONFIG_SPI)

/**
 * tinydrm_spi_transfer - SPI transfer helper
 * @spi: SPI device
 * @speed_hz: Override speed (optional)
 * @bpw: Bits per word
 * @buf: Buffer to transfer
 * @len: Buffer length
 *
 * This SPI transfer helper breaks up the transfer of @buf into chunks which
 * the SPI controller driver can handle.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int tinydrm_spi_transfer(struct spi_device *spi, u32 speed_hz,
			 u8 bpw, const void *buf, size_t len)
{
	size_t max_chunk = spi_max_transfer_size(spi);
	struct spi_transfer tr = {
		.bits_per_word = bpw,
		.speed_hz = speed_hz,
	};
	struct spi_message m;
	size_t chunk;
	int ret;

	spi_message_init_with_transfers(&m, &tr, 1);

	while (len) {
		chunk = min(len, max_chunk);

		tr.tx_buf = buf;
		tr.len = chunk;
		buf += chunk;
		len -= chunk;

		ret = spi_sync(spi, &m);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(tinydrm_spi_transfer);

#endif /* CONFIG_SPI */

MODULE_LICENSE("GPL");
