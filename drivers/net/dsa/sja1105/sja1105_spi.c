// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2016-2018 NXP
 * Copyright (c) 2018, Sensor-Technik Wiedemann GmbH
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include <linux/spi/spi.h>
#include <linux/packing.h>
#include "sja1105.h"

struct sja1105_chunk {
	u8	*buf;
	size_t	len;
	u64	reg_addr;
};

static void
sja1105_spi_message_pack(void *buf, const struct sja1105_spi_message *msg)
{
	const int size = SJA1105_SIZE_SPI_MSG_HEADER;

	memset(buf, 0, size);

	sja1105_pack(buf, &msg->access,     31, 31, size);
	sja1105_pack(buf, &msg->read_count, 30, 25, size);
	sja1105_pack(buf, &msg->address,    24,  4, size);
}

/* If @rw is:
 * - SPI_WRITE: creates and sends an SPI write message at absolute
 *		address reg_addr, taking @len bytes from *buf
 * - SPI_READ:  creates and sends an SPI read message from absolute
 *		address reg_addr, writing @len bytes into *buf
 */
static int sja1105_xfer(const struct sja1105_private *priv,
			sja1105_spi_rw_mode_t rw, u64 reg_addr, u8 *buf,
			size_t len, struct ptp_system_timestamp *ptp_sts)
{
	u8 hdr_buf[SJA1105_SIZE_SPI_MSG_HEADER] = {0};
	struct spi_device *spi = priv->spidev;
	struct spi_transfer xfers[2] = {0};
	struct spi_transfer *chunk_xfer;
	struct spi_transfer *hdr_xfer;
	struct sja1105_chunk chunk;
	int num_chunks;
	int rc, i = 0;

	num_chunks = DIV_ROUND_UP(len, priv->max_xfer_len);

	chunk.reg_addr = reg_addr;
	chunk.buf = buf;
	chunk.len = min_t(size_t, len, priv->max_xfer_len);

	hdr_xfer = &xfers[0];
	chunk_xfer = &xfers[1];

	for (i = 0; i < num_chunks; i++) {
		struct spi_transfer *ptp_sts_xfer;
		struct sja1105_spi_message msg;

		/* Populate the transfer's header buffer */
		msg.address = chunk.reg_addr;
		msg.access = rw;
		if (rw == SPI_READ)
			msg.read_count = chunk.len / 4;
		else
			/* Ignored */
			msg.read_count = 0;
		sja1105_spi_message_pack(hdr_buf, &msg);
		hdr_xfer->tx_buf = hdr_buf;
		hdr_xfer->len = SJA1105_SIZE_SPI_MSG_HEADER;

		/* Populate the transfer's data buffer */
		if (rw == SPI_READ)
			chunk_xfer->rx_buf = chunk.buf;
		else
			chunk_xfer->tx_buf = chunk.buf;
		chunk_xfer->len = chunk.len;

		/* Request timestamping for the transfer. Instead of letting
		 * callers specify which byte they want to timestamp, we can
		 * make certain assumptions:
		 * - A read operation will request a software timestamp when
		 *   what's being read is the PTP time. That is snapshotted by
		 *   the switch hardware at the end of the command portion
		 *   (hdr_xfer).
		 * - A write operation will request a software timestamp on
		 *   actions that modify the PTP time. Taking clock stepping as
		 *   an example, the switch writes the PTP time at the end of
		 *   the data portion (chunk_xfer).
		 */
		if (rw == SPI_READ)
			ptp_sts_xfer = hdr_xfer;
		else
			ptp_sts_xfer = chunk_xfer;
		ptp_sts_xfer->ptp_sts_word_pre = ptp_sts_xfer->len - 1;
		ptp_sts_xfer->ptp_sts_word_post = ptp_sts_xfer->len - 1;
		ptp_sts_xfer->ptp_sts = ptp_sts;

		/* Calculate next chunk */
		chunk.buf += chunk.len;
		chunk.reg_addr += chunk.len / 4;
		chunk.len = min_t(size_t, (ptrdiff_t)(buf + len - chunk.buf),
				  priv->max_xfer_len);

		rc = spi_sync_transfer(spi, xfers, 2);
		if (rc < 0) {
			dev_err(&spi->dev, "SPI transfer failed: %d\n", rc);
			return rc;
		}
	}

	return 0;
}

int sja1105_xfer_buf(const struct sja1105_private *priv,
		     sja1105_spi_rw_mode_t rw, u64 reg_addr,
		     u8 *buf, size_t len)
{
	return sja1105_xfer(priv, rw, reg_addr, buf, len, NULL);
}

/* If @rw is:
 * - SPI_WRITE: creates and sends an SPI write message at absolute
 *		address reg_addr
 * - SPI_READ:  creates and sends an SPI read message from absolute
 *		address reg_addr
 *
 * The u64 *value is unpacked, meaning that it's stored in the native
 * CPU endianness and directly usable by software running on the core.
 */
int sja1105_xfer_u64(const struct sja1105_private *priv,
		     sja1105_spi_rw_mode_t rw, u64 reg_addr, u64 *value,
		     struct ptp_system_timestamp *ptp_sts)
{
	u8 packed_buf[8];
	int rc;

	if (rw == SPI_WRITE)
		sja1105_pack(packed_buf, value, 63, 0, 8);

	rc = sja1105_xfer(priv, rw, reg_addr, packed_buf, 8, ptp_sts);

	if (rw == SPI_READ)
		sja1105_unpack(packed_buf, value, 63, 0, 8);

	return rc;
}

/* Same as above, but transfers only a 4 byte word */
int sja1105_xfer_u32(const struct sja1105_private *priv,
		     sja1105_spi_rw_mode_t rw, u64 reg_addr, u32 *value,
		     struct ptp_system_timestamp *ptp_sts)
{
	u8 packed_buf[4];
	u64 tmp;
	int rc;

	if (rw == SPI_WRITE) {
		/* The packing API only supports u64 as CPU word size,
		 * so we need to convert.
		 */
		tmp = *value;
		sja1105_pack(packed_buf, &tmp, 31, 0, 4);
	}

	rc = sja1105_xfer(priv, rw, reg_addr, packed_buf, 4, ptp_sts);

	if (rw == SPI_READ) {
		sja1105_unpack(packed_buf, &tmp, 31, 0, 4);
		*value = tmp;
	}

	return rc;
}

static int sja1105et_reset_cmd(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	const struct sja1105_regs *regs = priv->info->regs;
	u32 cold_reset = BIT(3);

	/* Cold reset */
	return sja1105_xfer_u32(priv, SPI_WRITE, regs->rgu, &cold_reset, NULL);
}

static int sja1105pqrs_reset_cmd(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	const struct sja1105_regs *regs = priv->info->regs;
	u32 cold_reset = BIT(2);

	/* Cold reset */
	return sja1105_xfer_u32(priv, SPI_WRITE, regs->rgu, &cold_reset, NULL);
}

static int sja1110_reset_cmd(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	const struct sja1105_regs *regs = priv->info->regs;
	u32 switch_reset = BIT(20);

	/* Only reset the switch core.
	 * A full cold reset would re-enable the BASE_MCSS_CLOCK PLL which
	 * would turn on the microcontroller, potentially letting it execute
	 * code which could interfere with our configuration.
	 */
	return sja1105_xfer_u32(priv, SPI_WRITE, regs->rgu, &switch_reset, NULL);
}

int sja1105_inhibit_tx(const struct sja1105_private *priv,
		       unsigned long port_bitmap, bool tx_inhibited)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u32 inhibit_cmd;
	int rc;

	rc = sja1105_xfer_u32(priv, SPI_READ, regs->port_control,
			      &inhibit_cmd, NULL);
	if (rc < 0)
		return rc;

	if (tx_inhibited)
		inhibit_cmd |= port_bitmap;
	else
		inhibit_cmd &= ~port_bitmap;

	return sja1105_xfer_u32(priv, SPI_WRITE, regs->port_control,
				&inhibit_cmd, NULL);
}

struct sja1105_status {
	u64 configs;
	u64 crcchkl;
	u64 ids;
	u64 crcchkg;
};

/* This is not reading the entire General Status area, which is also
 * divergent between E/T and P/Q/R/S, but only the relevant bits for
 * ensuring that the static config upload procedure was successful.
 */
static void sja1105_status_unpack(void *buf, struct sja1105_status *status)
{
	/* So that addition translates to 4 bytes */
	u32 *p = buf;

	/* device_id is missing from the buffer, but we don't
	 * want to diverge from the manual definition of the
	 * register addresses, so we'll back off one step with
	 * the register pointer, and never access p[0].
	 */
	p--;
	sja1105_unpack(p + 0x1, &status->configs,   31, 31, 4);
	sja1105_unpack(p + 0x1, &status->crcchkl,   30, 30, 4);
	sja1105_unpack(p + 0x1, &status->ids,       29, 29, 4);
	sja1105_unpack(p + 0x1, &status->crcchkg,   28, 28, 4);
}

static int sja1105_status_get(struct sja1105_private *priv,
			      struct sja1105_status *status)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u8 packed_buf[4];
	int rc;

	rc = sja1105_xfer_buf(priv, SPI_READ, regs->status, packed_buf, 4);
	if (rc < 0)
		return rc;

	sja1105_status_unpack(packed_buf, status);

	return 0;
}

/* Not const because unpacking priv->static_config into buffers and preparing
 * for upload requires the recalculation of table CRCs and updating the
 * structures with these.
 */
int static_config_buf_prepare_for_upload(struct sja1105_private *priv,
					 void *config_buf, int buf_len)
{
	struct sja1105_static_config *config = &priv->static_config;
	struct sja1105_table_header final_header;
	sja1105_config_valid_t valid;
	char *final_header_ptr;
	int crc_len;

	valid = sja1105_static_config_check_valid(config,
						  priv->info->max_frame_mem);
	if (valid != SJA1105_CONFIG_OK) {
		dev_err(&priv->spidev->dev,
			sja1105_static_config_error_msg[valid]);
		return -EINVAL;
	}

	/* Write Device ID and config tables to config_buf */
	sja1105_static_config_pack(config_buf, config);
	/* Recalculate CRC of the last header (right now 0xDEADBEEF).
	 * Don't include the CRC field itself.
	 */
	crc_len = buf_len - 4;
	/* Read the whole table header */
	final_header_ptr = config_buf + buf_len - SJA1105_SIZE_TABLE_HEADER;
	sja1105_table_header_packing(final_header_ptr, &final_header, UNPACK);
	/* Modify */
	final_header.crc = sja1105_crc32(config_buf, crc_len);
	/* Rewrite */
	sja1105_table_header_packing(final_header_ptr, &final_header, PACK);

	return 0;
}

#define RETRIES 10

int sja1105_static_config_upload(struct sja1105_private *priv)
{
	struct sja1105_static_config *config = &priv->static_config;
	const struct sja1105_regs *regs = priv->info->regs;
	struct device *dev = &priv->spidev->dev;
	struct dsa_switch *ds = priv->ds;
	struct sja1105_status status;
	int rc, retries = RETRIES;
	u8 *config_buf;
	int buf_len;

	buf_len = sja1105_static_config_get_length(config);
	config_buf = kcalloc(buf_len, sizeof(char), GFP_KERNEL);
	if (!config_buf)
		return -ENOMEM;

	rc = static_config_buf_prepare_for_upload(priv, config_buf, buf_len);
	if (rc < 0) {
		dev_err(dev, "Invalid config, cannot upload\n");
		rc = -EINVAL;
		goto out;
	}
	/* Prevent PHY jabbering during switch reset by inhibiting
	 * Tx on all ports and waiting for current packet to drain.
	 * Otherwise, the PHY will see an unterminated Ethernet packet.
	 */
	rc = sja1105_inhibit_tx(priv, GENMASK_ULL(ds->num_ports - 1, 0), true);
	if (rc < 0) {
		dev_err(dev, "Failed to inhibit Tx on ports\n");
		rc = -ENXIO;
		goto out;
	}
	/* Wait for an eventual egress packet to finish transmission
	 * (reach IFG). It is guaranteed that a second one will not
	 * follow, and that switch cold reset is thus safe
	 */
	usleep_range(500, 1000);
	do {
		/* Put the SJA1105 in programming mode */
		rc = priv->info->reset_cmd(priv->ds);
		if (rc < 0) {
			dev_err(dev, "Failed to reset switch, retrying...\n");
			continue;
		}
		/* Wait for the switch to come out of reset */
		usleep_range(1000, 5000);
		/* Upload the static config to the device */
		rc = sja1105_xfer_buf(priv, SPI_WRITE, regs->config,
				      config_buf, buf_len);
		if (rc < 0) {
			dev_err(dev, "Failed to upload config, retrying...\n");
			continue;
		}
		/* Check that SJA1105 responded well to the config upload */
		rc = sja1105_status_get(priv, &status);
		if (rc < 0)
			continue;

		if (status.ids == 1) {
			dev_err(dev, "Mismatch between hardware and static config "
				"device id. Wrote 0x%llx, wants 0x%llx\n",
				config->device_id, priv->info->device_id);
			continue;
		}
		if (status.crcchkl == 1) {
			dev_err(dev, "Switch reported invalid local CRC on "
				"the uploaded config, retrying...\n");
			continue;
		}
		if (status.crcchkg == 1) {
			dev_err(dev, "Switch reported invalid global CRC on "
				"the uploaded config, retrying...\n");
			continue;
		}
		if (status.configs == 0) {
			dev_err(dev, "Switch reported that configuration is "
				"invalid, retrying...\n");
			continue;
		}
		/* Success! */
		break;
	} while (--retries);

	if (!retries) {
		rc = -EIO;
		dev_err(dev, "Failed to upload config to device, giving up\n");
		goto out;
	} else if (retries != RETRIES) {
		dev_info(dev, "Succeeded after %d tried\n", RETRIES - retries);
	}

out:
	kfree(config_buf);
	return rc;
}

static const struct sja1105_regs sja1105et_regs = {
	.device_id = 0x0,
	.prod_id = 0x100BC3,
	.status = 0x1,
	.port_control = 0x11,
	.vl_status = 0x10000,
	.config = 0x020000,
	.rgu = 0x100440,
	/* UM10944.pdf, Table 86, ACU Register overview */
	.pad_mii_tx = {0x100800, 0x100802, 0x100804, 0x100806, 0x100808},
	.pad_mii_rx = {0x100801, 0x100803, 0x100805, 0x100807, 0x100809},
	.rmii_pll1 = 0x10000A,
	.cgu_idiv = {0x10000B, 0x10000C, 0x10000D, 0x10000E, 0x10000F},
	.stats[MAC] = {0x200, 0x202, 0x204, 0x206, 0x208},
	.stats[HL1] = {0x400, 0x410, 0x420, 0x430, 0x440},
	.stats[HL2] = {0x600, 0x610, 0x620, 0x630, 0x640},
	/* UM10944.pdf, Table 78, CGU Register overview */
	.mii_tx_clk = {0x100013, 0x10001A, 0x100021, 0x100028, 0x10002F},
	.mii_rx_clk = {0x100014, 0x10001B, 0x100022, 0x100029, 0x100030},
	.mii_ext_tx_clk = {0x100018, 0x10001F, 0x100026, 0x10002D, 0x100034},
	.mii_ext_rx_clk = {0x100019, 0x100020, 0x100027, 0x10002E, 0x100035},
	.rgmii_tx_clk = {0x100016, 0x10001D, 0x100024, 0x10002B, 0x100032},
	.rmii_ref_clk = {0x100015, 0x10001C, 0x100023, 0x10002A, 0x100031},
	.rmii_ext_tx_clk = {0x100018, 0x10001F, 0x100026, 0x10002D, 0x100034},
	.ptpegr_ts = {0xC0, 0xC2, 0xC4, 0xC6, 0xC8},
	.ptpschtm = 0x12, /* Spans 0x12 to 0x13 */
	.ptppinst = 0x14,
	.ptppindur = 0x16,
	.ptp_control = 0x17,
	.ptpclkval = 0x18, /* Spans 0x18 to 0x19 */
	.ptpclkrate = 0x1A,
	.ptpclkcorp = 0x1D,
	.mdio_100base_tx = SJA1105_RSV_ADDR,
	.mdio_100base_t1 = SJA1105_RSV_ADDR,
};

static const struct sja1105_regs sja1105pqrs_regs = {
	.device_id = 0x0,
	.prod_id = 0x100BC3,
	.status = 0x1,
	.port_control = 0x12,
	.vl_status = 0x10000,
	.config = 0x020000,
	.rgu = 0x100440,
	/* UM10944.pdf, Table 86, ACU Register overview */
	.pad_mii_tx = {0x100800, 0x100802, 0x100804, 0x100806, 0x100808},
	.pad_mii_rx = {0x100801, 0x100803, 0x100805, 0x100807, 0x100809},
	.pad_mii_id = {0x100810, 0x100811, 0x100812, 0x100813, 0x100814},
	.rmii_pll1 = 0x10000A,
	.cgu_idiv = {0x10000B, 0x10000C, 0x10000D, 0x10000E, 0x10000F},
	.stats[MAC] = {0x200, 0x202, 0x204, 0x206, 0x208},
	.stats[HL1] = {0x400, 0x410, 0x420, 0x430, 0x440},
	.stats[HL2] = {0x600, 0x610, 0x620, 0x630, 0x640},
	.stats[ETHER] = {0x1400, 0x1418, 0x1430, 0x1448, 0x1460},
	/* UM11040.pdf, Table 114 */
	.mii_tx_clk = {0x100013, 0x100019, 0x10001F, 0x100025, 0x10002B},
	.mii_rx_clk = {0x100014, 0x10001A, 0x100020, 0x100026, 0x10002C},
	.mii_ext_tx_clk = {0x100017, 0x10001D, 0x100023, 0x100029, 0x10002F},
	.mii_ext_rx_clk = {0x100018, 0x10001E, 0x100024, 0x10002A, 0x100030},
	.rgmii_tx_clk = {0x100016, 0x10001C, 0x100022, 0x100028, 0x10002E},
	.rmii_ref_clk = {0x100015, 0x10001B, 0x100021, 0x100027, 0x10002D},
	.rmii_ext_tx_clk = {0x100017, 0x10001D, 0x100023, 0x100029, 0x10002F},
	.ptpegr_ts = {0xC0, 0xC4, 0xC8, 0xCC, 0xD0},
	.ptpschtm = 0x13, /* Spans 0x13 to 0x14 */
	.ptppinst = 0x15,
	.ptppindur = 0x17,
	.ptp_control = 0x18,
	.ptpclkval = 0x19,
	.ptpclkrate = 0x1B,
	.ptpclkcorp = 0x1E,
	.ptpsyncts = 0x1F,
	.mdio_100base_tx = SJA1105_RSV_ADDR,
	.mdio_100base_t1 = SJA1105_RSV_ADDR,
};

static const struct sja1105_regs sja1110_regs = {
	.device_id = SJA1110_SPI_ADDR(0x0),
	.prod_id = SJA1110_ACU_ADDR(0xf00),
	.status = SJA1110_SPI_ADDR(0x4),
	.port_control = SJA1110_SPI_ADDR(0x50), /* actually INHIB_TX */
	.vl_status = 0x10000,
	.config = 0x020000,
	.rgu = SJA1110_RGU_ADDR(0x100), /* Reset Control Register 0 */
	/* Ports 2 and 3 are capable of xMII, but there isn't anything to
	 * configure in the CGU/ACU for them.
	 */
	.pad_mii_tx = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR},
	.pad_mii_rx = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR},
	.pad_mii_id = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1110_ACU_ADDR(0x18), SJA1110_ACU_ADDR(0x28),
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR},
	.rmii_pll1 = SJA1105_RSV_ADDR,
	.cgu_idiv = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		     SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		     SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		     SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.stats[MAC] = {0x200, 0x202, 0x204, 0x206, 0x208, 0x20a,
		       0x20c, 0x20e, 0x210, 0x212, 0x214},
	.stats[HL1] = {0x400, 0x410, 0x420, 0x430, 0x440, 0x450,
		       0x460, 0x470, 0x480, 0x490, 0x4a0},
	.stats[HL2] = {0x600, 0x610, 0x620, 0x630, 0x640, 0x650,
		       0x660, 0x670, 0x680, 0x690, 0x6a0},
	.stats[ETHER] = {0x1400, 0x1418, 0x1430, 0x1448, 0x1460, 0x1478,
			 0x1490, 0x14a8, 0x14c0, 0x14d8, 0x14f0},
	.mii_tx_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.mii_rx_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		       SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.mii_ext_tx_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			   SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			   SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			   SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.mii_ext_rx_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			   SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			   SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			   SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.rgmii_tx_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			 SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			 SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			 SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.rmii_ref_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			 SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			 SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			 SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
	.rmii_ext_tx_clk = {SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			    SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			    SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			    SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			    SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
			    SJA1105_RSV_ADDR},
	.ptpschtm = SJA1110_SPI_ADDR(0x54),
	.ptppinst = SJA1110_SPI_ADDR(0x5c),
	.ptppindur = SJA1110_SPI_ADDR(0x64),
	.ptp_control = SJA1110_SPI_ADDR(0x68),
	.ptpclkval = SJA1110_SPI_ADDR(0x6c),
	.ptpclkrate = SJA1110_SPI_ADDR(0x74),
	.ptpclkcorp = SJA1110_SPI_ADDR(0x80),
	.ptpsyncts = SJA1110_SPI_ADDR(0x84),
	.mdio_100base_tx = 0x1c2400,
	.mdio_100base_t1 = 0x1c1000,
	.pcs_base = {SJA1105_RSV_ADDR, 0x1c1400, 0x1c1800, 0x1c1c00, 0x1c2000,
		     SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR,
		     SJA1105_RSV_ADDR, SJA1105_RSV_ADDR, SJA1105_RSV_ADDR},
};

const struct sja1105_info sja1105e_info = {
	.device_id		= SJA1105E_DEVICE_ID,
	.part_no		= SJA1105ET_PART_NO,
	.static_ops		= sja1105e_table_ops,
	.dyn_ops		= sja1105et_dyn_ops,
	.tag_proto		= DSA_TAG_PROTO_SJA1105,
	.can_limit_mcast_flood	= false,
	.ptp_ts_bits		= 24,
	.ptpegr_ts_bytes	= 4,
	.max_frame_mem		= SJA1105_MAX_FRAME_MEMORY,
	.num_ports		= SJA1105_NUM_PORTS,
	.num_cbs_shapers	= SJA1105ET_MAX_CBS_COUNT,
	.reset_cmd		= sja1105et_reset_cmd,
	.fdb_add_cmd		= sja1105et_fdb_add,
	.fdb_del_cmd		= sja1105et_fdb_del,
	.ptp_cmd_packing	= sja1105et_ptp_cmd_packing,
	.rxtstamp		= sja1105_rxtstamp,
	.clocking_setup		= sja1105_clocking_setup,
	.regs			= &sja1105et_regs,
	.port_speed		= {
		[SJA1105_SPEED_AUTO] = 0,
		[SJA1105_SPEED_10MBPS] = 3,
		[SJA1105_SPEED_100MBPS] = 2,
		[SJA1105_SPEED_1000MBPS] = 1,
		[SJA1105_SPEED_2500MBPS] = 0, /* Not supported */
	},
	.supports_mii		= {true, true, true, true, true},
	.supports_rmii		= {true, true, true, true, true},
	.supports_rgmii		= {true, true, true, true, true},
	.name			= "SJA1105E",
};

const struct sja1105_info sja1105t_info = {
	.device_id		= SJA1105T_DEVICE_ID,
	.part_no		= SJA1105ET_PART_NO,
	.static_ops		= sja1105t_table_ops,
	.dyn_ops		= sja1105et_dyn_ops,
	.tag_proto		= DSA_TAG_PROTO_SJA1105,
	.can_limit_mcast_flood	= false,
	.ptp_ts_bits		= 24,
	.ptpegr_ts_bytes	= 4,
	.max_frame_mem		= SJA1105_MAX_FRAME_MEMORY,
	.num_ports		= SJA1105_NUM_PORTS,
	.num_cbs_shapers	= SJA1105ET_MAX_CBS_COUNT,
	.reset_cmd		= sja1105et_reset_cmd,
	.fdb_add_cmd		= sja1105et_fdb_add,
	.fdb_del_cmd		= sja1105et_fdb_del,
	.ptp_cmd_packing	= sja1105et_ptp_cmd_packing,
	.rxtstamp		= sja1105_rxtstamp,
	.clocking_setup		= sja1105_clocking_setup,
	.regs			= &sja1105et_regs,
	.port_speed		= {
		[SJA1105_SPEED_AUTO] = 0,
		[SJA1105_SPEED_10MBPS] = 3,
		[SJA1105_SPEED_100MBPS] = 2,
		[SJA1105_SPEED_1000MBPS] = 1,
		[SJA1105_SPEED_2500MBPS] = 0, /* Not supported */
	},
	.supports_mii		= {true, true, true, true, true},
	.supports_rmii		= {true, true, true, true, true},
	.supports_rgmii		= {true, true, true, true, true},
	.name			= "SJA1105T",
};

const struct sja1105_info sja1105p_info = {
	.device_id		= SJA1105PR_DEVICE_ID,
	.part_no		= SJA1105P_PART_NO,
	.static_ops		= sja1105p_table_ops,
	.dyn_ops		= sja1105pqrs_dyn_ops,
	.tag_proto		= DSA_TAG_PROTO_SJA1105,
	.can_limit_mcast_flood	= true,
	.ptp_ts_bits		= 32,
	.ptpegr_ts_bytes	= 8,
	.max_frame_mem		= SJA1105_MAX_FRAME_MEMORY,
	.num_ports		= SJA1105_NUM_PORTS,
	.num_cbs_shapers	= SJA1105PQRS_MAX_CBS_COUNT,
	.setup_rgmii_delay	= sja1105pqrs_setup_rgmii_delay,
	.reset_cmd		= sja1105pqrs_reset_cmd,
	.fdb_add_cmd		= sja1105pqrs_fdb_add,
	.fdb_del_cmd		= sja1105pqrs_fdb_del,
	.ptp_cmd_packing	= sja1105pqrs_ptp_cmd_packing,
	.rxtstamp		= sja1105_rxtstamp,
	.clocking_setup		= sja1105_clocking_setup,
	.regs			= &sja1105pqrs_regs,
	.port_speed		= {
		[SJA1105_SPEED_AUTO] = 0,
		[SJA1105_SPEED_10MBPS] = 3,
		[SJA1105_SPEED_100MBPS] = 2,
		[SJA1105_SPEED_1000MBPS] = 1,
		[SJA1105_SPEED_2500MBPS] = 0, /* Not supported */
	},
	.supports_mii		= {true, true, true, true, true},
	.supports_rmii		= {true, true, true, true, true},
	.supports_rgmii		= {true, true, true, true, true},
	.name			= "SJA1105P",
};

const struct sja1105_info sja1105q_info = {
	.device_id		= SJA1105QS_DEVICE_ID,
	.part_no		= SJA1105Q_PART_NO,
	.static_ops		= sja1105q_table_ops,
	.dyn_ops		= sja1105pqrs_dyn_ops,
	.tag_proto		= DSA_TAG_PROTO_SJA1105,
	.can_limit_mcast_flood	= true,
	.ptp_ts_bits		= 32,
	.ptpegr_ts_bytes	= 8,
	.max_frame_mem		= SJA1105_MAX_FRAME_MEMORY,
	.num_ports		= SJA1105_NUM_PORTS,
	.num_cbs_shapers	= SJA1105PQRS_MAX_CBS_COUNT,
	.setup_rgmii_delay	= sja1105pqrs_setup_rgmii_delay,
	.reset_cmd		= sja1105pqrs_reset_cmd,
	.fdb_add_cmd		= sja1105pqrs_fdb_add,
	.fdb_del_cmd		= sja1105pqrs_fdb_del,
	.ptp_cmd_packing	= sja1105pqrs_ptp_cmd_packing,
	.rxtstamp		= sja1105_rxtstamp,
	.clocking_setup		= sja1105_clocking_setup,
	.regs			= &sja1105pqrs_regs,
	.port_speed		= {
		[SJA1105_SPEED_AUTO] = 0,
		[SJA1105_SPEED_10MBPS] = 3,
		[SJA1105_SPEED_100MBPS] = 2,
		[SJA1105_SPEED_1000MBPS] = 1,
		[SJA1105_SPEED_2500MBPS] = 0, /* Not supported */
	},
	.supports_mii		= {true, true, true, true, true},
	.supports_rmii		= {true, true, true, true, true},
	.supports_rgmii		= {true, true, true, true, true},
	.name			= "SJA1105Q",
};

const struct sja1105_info sja1105r_info = {
	.device_id		= SJA1105PR_DEVICE_ID,
	.part_no		= SJA1105R_PART_NO,
	.static_ops		= sja1105r_table_ops,
	.dyn_ops		= sja1105pqrs_dyn_ops,
	.tag_proto		= DSA_TAG_PROTO_SJA1105,
	.can_limit_mcast_flood	= true,
	.ptp_ts_bits		= 32,
	.ptpegr_ts_bytes	= 8,
	.max_frame_mem		= SJA1105_MAX_FRAME_MEMORY,
	.num_ports		= SJA1105_NUM_PORTS,
	.num_cbs_shapers	= SJA1105PQRS_MAX_CBS_COUNT,
	.setup_rgmii_delay	= sja1105pqrs_setup_rgmii_delay,
	.reset_cmd		= sja1105pqrs_reset_cmd,
	.fdb_add_cmd		= sja1105pqrs_fdb_add,
	.fdb_del_cmd		= sja1105pqrs_fdb_del,
	.ptp_cmd_packing	= sja1105pqrs_ptp_cmd_packing,
	.rxtstamp		= sja1105_rxtstamp,
	.clocking_setup		= sja1105_clocking_setup,
	.pcs_mdio_read		= sja1105_pcs_mdio_read,
	.pcs_mdio_write		= sja1105_pcs_mdio_write,
	.regs			= &sja1105pqrs_regs,
	.port_speed		= {
		[SJA1105_SPEED_AUTO] = 0,
		[SJA1105_SPEED_10MBPS] = 3,
		[SJA1105_SPEED_100MBPS] = 2,
		[SJA1105_SPEED_1000MBPS] = 1,
		[SJA1105_SPEED_2500MBPS] = 0, /* Not supported */
	},
	.supports_mii		= {true, true, true, true, true},
	.supports_rmii		= {true, true, true, true, true},
	.supports_rgmii		= {true, true, true, true, true},
	.supports_sgmii		= {false, false, false, false, true},
	.name			= "SJA1105R",
};

const struct sja1105_info sja1105s_info = {
	.device_id		= SJA1105QS_DEVICE_ID,
	.part_no		= SJA1105S_PART_NO,
	.static_ops		= sja1105s_table_ops,
	.dyn_ops		= sja1105pqrs_dyn_ops,
	.regs			= &sja1105pqrs_regs,
	.tag_proto		= DSA_TAG_PROTO_SJA1105,
	.can_limit_mcast_flood	= true,
	.ptp_ts_bits		= 32,
	.ptpegr_ts_bytes	= 8,
	.max_frame_mem		= SJA1105_MAX_FRAME_MEMORY,
	.num_ports		= SJA1105_NUM_PORTS,
	.num_cbs_shapers	= SJA1105PQRS_MAX_CBS_COUNT,
	.setup_rgmii_delay	= sja1105pqrs_setup_rgmii_delay,
	.reset_cmd		= sja1105pqrs_reset_cmd,
	.fdb_add_cmd		= sja1105pqrs_fdb_add,
	.fdb_del_cmd		= sja1105pqrs_fdb_del,
	.ptp_cmd_packing	= sja1105pqrs_ptp_cmd_packing,
	.rxtstamp		= sja1105_rxtstamp,
	.clocking_setup		= sja1105_clocking_setup,
	.pcs_mdio_read		= sja1105_pcs_mdio_read,
	.pcs_mdio_write		= sja1105_pcs_mdio_write,
	.port_speed		= {
		[SJA1105_SPEED_AUTO] = 0,
		[SJA1105_SPEED_10MBPS] = 3,
		[SJA1105_SPEED_100MBPS] = 2,
		[SJA1105_SPEED_1000MBPS] = 1,
		[SJA1105_SPEED_2500MBPS] = 0, /* Not supported */
	},
	.supports_mii		= {true, true, true, true, true},
	.supports_rmii		= {true, true, true, true, true},
	.supports_rgmii		= {true, true, true, true, true},
	.supports_sgmii		= {false, false, false, false, true},
	.name			= "SJA1105S",
};

const struct sja1105_info sja1110a_info = {
	.device_id		= SJA1110_DEVICE_ID,
	.part_no		= SJA1110A_PART_NO,
	.static_ops		= sja1110_table_ops,
	.dyn_ops		= sja1110_dyn_ops,
	.regs			= &sja1110_regs,
	.tag_proto		= DSA_TAG_PROTO_SJA1110,
	.can_limit_mcast_flood	= true,
	.multiple_cascade_ports	= true,
	.ptp_ts_bits		= 32,
	.ptpegr_ts_bytes	= 8,
	.max_frame_mem		= SJA1110_MAX_FRAME_MEMORY,
	.num_ports		= SJA1110_NUM_PORTS,
	.num_cbs_shapers	= SJA1110_MAX_CBS_COUNT,
	.setup_rgmii_delay	= sja1110_setup_rgmii_delay,
	.reset_cmd		= sja1110_reset_cmd,
	.fdb_add_cmd		= sja1105pqrs_fdb_add,
	.fdb_del_cmd		= sja1105pqrs_fdb_del,
	.ptp_cmd_packing	= sja1105pqrs_ptp_cmd_packing,
	.rxtstamp		= sja1110_rxtstamp,
	.txtstamp		= sja1110_txtstamp,
	.disable_microcontroller = sja1110_disable_microcontroller,
	.pcs_mdio_read		= sja1110_pcs_mdio_read,
	.pcs_mdio_write		= sja1110_pcs_mdio_write,
	.port_speed		= {
		[SJA1105_SPEED_AUTO] = 0,
		[SJA1105_SPEED_10MBPS] = 4,
		[SJA1105_SPEED_100MBPS] = 3,
		[SJA1105_SPEED_1000MBPS] = 2,
		[SJA1105_SPEED_2500MBPS] = 1,
	},
	.supports_mii		= {true, true, true, true, false,
				   true, true, true, true, true, true},
	.supports_rmii		= {false, false, true, true, false,
				   false, false, false, false, false, false},
	.supports_rgmii		= {false, false, true, true, false,
				   false, false, false, false, false, false},
	.supports_sgmii		= {false, true, true, true, true,
				   false, false, false, false, false, false},
	.supports_2500basex	= {false, false, false, true, true,
				   false, false, false, false, false, false},
	.internal_phy		= {SJA1105_NO_PHY, SJA1105_PHY_BASE_TX,
				   SJA1105_NO_PHY, SJA1105_NO_PHY,
				   SJA1105_NO_PHY, SJA1105_PHY_BASE_T1,
				   SJA1105_PHY_BASE_T1, SJA1105_PHY_BASE_T1,
				   SJA1105_PHY_BASE_T1, SJA1105_PHY_BASE_T1,
				   SJA1105_PHY_BASE_T1},
	.name			= "SJA1110A",
};

const struct sja1105_info sja1110b_info = {
	.device_id		= SJA1110_DEVICE_ID,
	.part_no		= SJA1110B_PART_NO,
	.static_ops		= sja1110_table_ops,
	.dyn_ops		= sja1110_dyn_ops,
	.regs			= &sja1110_regs,
	.tag_proto		= DSA_TAG_PROTO_SJA1110,
	.can_limit_mcast_flood	= true,
	.multiple_cascade_ports	= true,
	.ptp_ts_bits		= 32,
	.ptpegr_ts_bytes	= 8,
	.max_frame_mem		= SJA1110_MAX_FRAME_MEMORY,
	.num_ports		= SJA1110_NUM_PORTS,
	.num_cbs_shapers	= SJA1110_MAX_CBS_COUNT,
	.setup_rgmii_delay	= sja1110_setup_rgmii_delay,
	.reset_cmd		= sja1110_reset_cmd,
	.fdb_add_cmd		= sja1105pqrs_fdb_add,
	.fdb_del_cmd		= sja1105pqrs_fdb_del,
	.ptp_cmd_packing	= sja1105pqrs_ptp_cmd_packing,
	.rxtstamp		= sja1110_rxtstamp,
	.txtstamp		= sja1110_txtstamp,
	.disable_microcontroller = sja1110_disable_microcontroller,
	.pcs_mdio_read		= sja1110_pcs_mdio_read,
	.pcs_mdio_write		= sja1110_pcs_mdio_write,
	.port_speed		= {
		[SJA1105_SPEED_AUTO] = 0,
		[SJA1105_SPEED_10MBPS] = 4,
		[SJA1105_SPEED_100MBPS] = 3,
		[SJA1105_SPEED_1000MBPS] = 2,
		[SJA1105_SPEED_2500MBPS] = 1,
	},
	.supports_mii		= {true, true, true, true, false,
				   true, true, true, true, true, false},
	.supports_rmii		= {false, false, true, true, false,
				   false, false, false, false, false, false},
	.supports_rgmii		= {false, false, true, true, false,
				   false, false, false, false, false, false},
	.supports_sgmii		= {false, false, false, true, true,
				   false, false, false, false, false, false},
	.supports_2500basex	= {false, false, false, true, true,
				   false, false, false, false, false, false},
	.internal_phy		= {SJA1105_NO_PHY, SJA1105_PHY_BASE_TX,
				   SJA1105_NO_PHY, SJA1105_NO_PHY,
				   SJA1105_NO_PHY, SJA1105_PHY_BASE_T1,
				   SJA1105_PHY_BASE_T1, SJA1105_PHY_BASE_T1,
				   SJA1105_PHY_BASE_T1, SJA1105_PHY_BASE_T1,
				   SJA1105_NO_PHY},
	.name			= "SJA1110B",
};

const struct sja1105_info sja1110c_info = {
	.device_id		= SJA1110_DEVICE_ID,
	.part_no		= SJA1110C_PART_NO,
	.static_ops		= sja1110_table_ops,
	.dyn_ops		= sja1110_dyn_ops,
	.regs			= &sja1110_regs,
	.tag_proto		= DSA_TAG_PROTO_SJA1110,
	.can_limit_mcast_flood	= true,
	.multiple_cascade_ports	= true,
	.ptp_ts_bits		= 32,
	.ptpegr_ts_bytes	= 8,
	.max_frame_mem		= SJA1110_MAX_FRAME_MEMORY,
	.num_ports		= SJA1110_NUM_PORTS,
	.num_cbs_shapers	= SJA1110_MAX_CBS_COUNT,
	.setup_rgmii_delay	= sja1110_setup_rgmii_delay,
	.reset_cmd		= sja1110_reset_cmd,
	.fdb_add_cmd		= sja1105pqrs_fdb_add,
	.fdb_del_cmd		= sja1105pqrs_fdb_del,
	.ptp_cmd_packing	= sja1105pqrs_ptp_cmd_packing,
	.rxtstamp		= sja1110_rxtstamp,
	.txtstamp		= sja1110_txtstamp,
	.disable_microcontroller = sja1110_disable_microcontroller,
	.pcs_mdio_read		= sja1110_pcs_mdio_read,
	.pcs_mdio_write		= sja1110_pcs_mdio_write,
	.port_speed		= {
		[SJA1105_SPEED_AUTO] = 0,
		[SJA1105_SPEED_10MBPS] = 4,
		[SJA1105_SPEED_100MBPS] = 3,
		[SJA1105_SPEED_1000MBPS] = 2,
		[SJA1105_SPEED_2500MBPS] = 1,
	},
	.supports_mii		= {true, true, true, true, false,
				   true, true, true, false, false, false},
	.supports_rmii		= {false, false, true, true, false,
				   false, false, false, false, false, false},
	.supports_rgmii		= {false, false, true, true, false,
				   false, false, false, false, false, false},
	.supports_sgmii		= {false, false, false, false, true,
				   false, false, false, false, false, false},
	.supports_2500basex	= {false, false, false, false, true,
				   false, false, false, false, false, false},
	.internal_phy		= {SJA1105_NO_PHY, SJA1105_PHY_BASE_TX,
				   SJA1105_NO_PHY, SJA1105_NO_PHY,
				   SJA1105_NO_PHY, SJA1105_PHY_BASE_T1,
				   SJA1105_PHY_BASE_T1, SJA1105_PHY_BASE_T1,
				   SJA1105_NO_PHY, SJA1105_NO_PHY,
				   SJA1105_NO_PHY},
	.name			= "SJA1110C",
};

const struct sja1105_info sja1110d_info = {
	.device_id		= SJA1110_DEVICE_ID,
	.part_no		= SJA1110D_PART_NO,
	.static_ops		= sja1110_table_ops,
	.dyn_ops		= sja1110_dyn_ops,
	.regs			= &sja1110_regs,
	.tag_proto		= DSA_TAG_PROTO_SJA1110,
	.can_limit_mcast_flood	= true,
	.multiple_cascade_ports	= true,
	.ptp_ts_bits		= 32,
	.ptpegr_ts_bytes	= 8,
	.max_frame_mem		= SJA1110_MAX_FRAME_MEMORY,
	.num_ports		= SJA1110_NUM_PORTS,
	.num_cbs_shapers	= SJA1110_MAX_CBS_COUNT,
	.setup_rgmii_delay	= sja1110_setup_rgmii_delay,
	.reset_cmd		= sja1110_reset_cmd,
	.fdb_add_cmd		= sja1105pqrs_fdb_add,
	.fdb_del_cmd		= sja1105pqrs_fdb_del,
	.ptp_cmd_packing	= sja1105pqrs_ptp_cmd_packing,
	.rxtstamp		= sja1110_rxtstamp,
	.txtstamp		= sja1110_txtstamp,
	.disable_microcontroller = sja1110_disable_microcontroller,
	.pcs_mdio_read		= sja1110_pcs_mdio_read,
	.pcs_mdio_write		= sja1110_pcs_mdio_write,
	.port_speed		= {
		[SJA1105_SPEED_AUTO] = 0,
		[SJA1105_SPEED_10MBPS] = 4,
		[SJA1105_SPEED_100MBPS] = 3,
		[SJA1105_SPEED_1000MBPS] = 2,
		[SJA1105_SPEED_2500MBPS] = 1,
	},
	.supports_mii		= {true, false, true, false, false,
				   true, true, true, false, false, false},
	.supports_rmii		= {false, false, true, false, false,
				   false, false, false, false, false, false},
	.supports_rgmii		= {false, false, true, false, false,
				   false, false, false, false, false, false},
	.supports_sgmii		= {false, true, true, true, true,
				   false, false, false, false, false, false},
	.supports_2500basex     = {false, false, false, true, true,
				   false, false, false, false, false, false},
	.internal_phy		= {SJA1105_NO_PHY, SJA1105_NO_PHY,
				   SJA1105_NO_PHY, SJA1105_NO_PHY,
				   SJA1105_NO_PHY, SJA1105_PHY_BASE_T1,
				   SJA1105_PHY_BASE_T1, SJA1105_PHY_BASE_T1,
				   SJA1105_NO_PHY, SJA1105_NO_PHY,
				   SJA1105_NO_PHY},
	.name			= "SJA1110D",
};
