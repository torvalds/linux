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
	size_t max_chunk = spi_max_transfer_size(spi);
	struct spi_transfer tr = {
		.bits_per_word = bpw,
		.speed_hz = speed_hz,
	};
	struct spi_message m;
	u16 *swap_buf = NULL;
	size_t chunk;
	int ret = 0;

	if (WARN_ON_ONCE(bpw != 8 && bpw != 16))
		return -EINVAL;

	if (bpw == 16 && !spi_is_bpw_supported(spi, 16)) {
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

		ret = spi_sync(spi, &m);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(tinydrm_spi_transfer);

#endif /* CONFIG_SPI */

MODULE_LICENSE("GPL");
