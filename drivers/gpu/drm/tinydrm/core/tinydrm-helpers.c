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

static unsigned int spi_max;
module_param(spi_max, uint, 0400);
MODULE_PARM_DESC(spi_max, "Set a lower SPI max transfer size");

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
	int i = 0;

	list_for_each_entry(tmp, &m->transfers, transfer_list) {

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

MODULE_LICENSE("GPL");
