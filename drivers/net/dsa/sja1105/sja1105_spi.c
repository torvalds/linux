// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2016-2018, NXP Semiconductors
 * Copyright (c) 2018, Sensor-Technik Wiedemann GmbH
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include <linux/spi/spi.h>
#include <linux/packing.h>
#include "sja1105.h"

#define SJA1105_SIZE_PORT_CTRL		4
#define SJA1105_SIZE_RESET_CMD		4
#define SJA1105_SIZE_SPI_MSG_HEADER	4
#define SJA1105_SIZE_SPI_MSG_MAXLEN	(64 * 4)
#define SJA1105_SIZE_SPI_TRANSFER_MAX	\
	(SJA1105_SIZE_SPI_MSG_HEADER + SJA1105_SIZE_SPI_MSG_MAXLEN)

static int sja1105_spi_transfer(const struct sja1105_private *priv,
				const void *tx, void *rx, int size)
{
	struct spi_device *spi = priv->spidev;
	struct spi_transfer transfer = {
		.tx_buf = tx,
		.rx_buf = rx,
		.len = size,
	};
	struct spi_message msg;
	int rc;

	if (size > SJA1105_SIZE_SPI_TRANSFER_MAX) {
		dev_err(&spi->dev, "SPI message (%d) longer than max of %d\n",
			size, SJA1105_SIZE_SPI_TRANSFER_MAX);
		return -EMSGSIZE;
	}

	spi_message_init(&msg);
	spi_message_add_tail(&transfer, &msg);

	rc = spi_sync(spi, &msg);
	if (rc < 0) {
		dev_err(&spi->dev, "SPI transfer failed: %d\n", rc);
		return rc;
	}

	return rc;
}

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
 *		address reg_addr, taking size_bytes from *packed_buf
 * - SPI_READ:  creates and sends an SPI read message from absolute
 *		address reg_addr, writing size_bytes into *packed_buf
 *
 * This function should only be called if it is priorly known that
 * @size_bytes is smaller than SIZE_SPI_MSG_MAXLEN. Larger packed buffers
 * are chunked in smaller pieces by sja1105_spi_send_long_packed_buf below.
 */
int sja1105_spi_send_packed_buf(const struct sja1105_private *priv,
				sja1105_spi_rw_mode_t rw, u64 reg_addr,
				void *packed_buf, size_t size_bytes)
{
	u8 tx_buf[SJA1105_SIZE_SPI_TRANSFER_MAX] = {0};
	u8 rx_buf[SJA1105_SIZE_SPI_TRANSFER_MAX] = {0};
	const int msg_len = size_bytes + SJA1105_SIZE_SPI_MSG_HEADER;
	struct sja1105_spi_message msg = {0};
	int rc;

	if (msg_len > SJA1105_SIZE_SPI_TRANSFER_MAX)
		return -ERANGE;

	msg.access = rw;
	msg.address = reg_addr;
	if (rw == SPI_READ)
		msg.read_count = size_bytes / 4;

	sja1105_spi_message_pack(tx_buf, &msg);

	if (rw == SPI_WRITE)
		memcpy(tx_buf + SJA1105_SIZE_SPI_MSG_HEADER,
		       packed_buf, size_bytes);

	rc = sja1105_spi_transfer(priv, tx_buf, rx_buf, msg_len);
	if (rc < 0)
		return rc;

	if (rw == SPI_READ)
		memcpy(packed_buf, rx_buf + SJA1105_SIZE_SPI_MSG_HEADER,
		       size_bytes);

	return 0;
}
EXPORT_SYMBOL_GPL(sja1105_spi_send_packed_buf);

/* If @rw is:
 * - SPI_WRITE: creates and sends an SPI write message at absolute
 *		address reg_addr, taking size_bytes from *packed_buf
 * - SPI_READ:  creates and sends an SPI read message from absolute
 *		address reg_addr, writing size_bytes into *packed_buf
 *
 * The u64 *value is unpacked, meaning that it's stored in the native
 * CPU endianness and directly usable by software running on the core.
 *
 * This is a wrapper around sja1105_spi_send_packed_buf().
 */
int sja1105_spi_send_int(const struct sja1105_private *priv,
			 sja1105_spi_rw_mode_t rw, u64 reg_addr,
			 u64 *value, u64 size_bytes)
{
	u8 packed_buf[SJA1105_SIZE_SPI_MSG_MAXLEN];
	int rc;

	if (size_bytes > SJA1105_SIZE_SPI_MSG_MAXLEN)
		return -ERANGE;

	if (rw == SPI_WRITE)
		sja1105_pack(packed_buf, value, 8 * size_bytes - 1, 0,
			     size_bytes);

	rc = sja1105_spi_send_packed_buf(priv, rw, reg_addr, packed_buf,
					 size_bytes);

	if (rw == SPI_READ)
		sja1105_unpack(packed_buf, value, 8 * size_bytes - 1, 0,
			       size_bytes);

	return rc;
}
EXPORT_SYMBOL_GPL(sja1105_spi_send_int);

/* Should be used if a @packed_buf larger than SJA1105_SIZE_SPI_MSG_MAXLEN
 * must be sent/received. Splitting the buffer into chunks and assembling
 * those into SPI messages is done automatically by this function.
 */
int sja1105_spi_send_long_packed_buf(const struct sja1105_private *priv,
				     sja1105_spi_rw_mode_t rw, u64 base_addr,
				     void *packed_buf, u64 buf_len)
{
	struct chunk {
		void *buf_ptr;
		int len;
		u64 spi_address;
	} chunk;
	int distance_to_end;
	int rc;

	/* Initialize chunk */
	chunk.buf_ptr = packed_buf;
	chunk.spi_address = base_addr;
	chunk.len = min_t(int, buf_len, SJA1105_SIZE_SPI_MSG_MAXLEN);

	while (chunk.len) {
		rc = sja1105_spi_send_packed_buf(priv, rw, chunk.spi_address,
						 chunk.buf_ptr, chunk.len);
		if (rc < 0)
			return rc;

		chunk.buf_ptr += chunk.len;
		chunk.spi_address += chunk.len / 4;
		distance_to_end = (uintptr_t)(packed_buf + buf_len -
					      chunk.buf_ptr);
		chunk.len = min(distance_to_end, SJA1105_SIZE_SPI_MSG_MAXLEN);
	}

	return 0;
}

/* Back-ported structure from UM11040 Table 112.
 * Reset control register (addr. 100440h)
 * In the SJA1105 E/T, only warm_rst and cold_rst are
 * supported (exposed in UM10944 as rst_ctrl), but the bit
 * offsets of warm_rst and cold_rst are actually reversed.
 */
struct sja1105_reset_cmd {
	u64 switch_rst;
	u64 cfg_rst;
	u64 car_rst;
	u64 otp_rst;
	u64 warm_rst;
	u64 cold_rst;
	u64 por_rst;
};

static void
sja1105et_reset_cmd_pack(void *buf, const struct sja1105_reset_cmd *reset)
{
	const int size = SJA1105_SIZE_RESET_CMD;

	memset(buf, 0, size);

	sja1105_pack(buf, &reset->cold_rst, 3, 3, size);
	sja1105_pack(buf, &reset->warm_rst, 2, 2, size);
}

static void
sja1105pqrs_reset_cmd_pack(void *buf, const struct sja1105_reset_cmd *reset)
{
	const int size = SJA1105_SIZE_RESET_CMD;

	memset(buf, 0, size);

	sja1105_pack(buf, &reset->switch_rst, 8, 8, size);
	sja1105_pack(buf, &reset->cfg_rst,    7, 7, size);
	sja1105_pack(buf, &reset->car_rst,    5, 5, size);
	sja1105_pack(buf, &reset->otp_rst,    4, 4, size);
	sja1105_pack(buf, &reset->warm_rst,   3, 3, size);
	sja1105_pack(buf, &reset->cold_rst,   2, 2, size);
	sja1105_pack(buf, &reset->por_rst,    1, 1, size);
}

static int sja1105et_reset_cmd(const void *ctx, const void *data)
{
	const struct sja1105_private *priv = ctx;
	const struct sja1105_reset_cmd *reset = data;
	const struct sja1105_regs *regs = priv->info->regs;
	struct device *dev = priv->ds->dev;
	u8 packed_buf[SJA1105_SIZE_RESET_CMD];

	if (reset->switch_rst ||
	    reset->cfg_rst ||
	    reset->car_rst ||
	    reset->otp_rst ||
	    reset->por_rst) {
		dev_err(dev, "Only warm and cold reset is supported "
			"for SJA1105 E/T!\n");
		return -EINVAL;
	}

	if (reset->warm_rst)
		dev_dbg(dev, "Warm reset requested\n");
	if (reset->cold_rst)
		dev_dbg(dev, "Cold reset requested\n");

	sja1105et_reset_cmd_pack(packed_buf, reset);

	return sja1105_spi_send_packed_buf(priv, SPI_WRITE, regs->rgu,
					   packed_buf, SJA1105_SIZE_RESET_CMD);
}

static int sja1105pqrs_reset_cmd(const void *ctx, const void *data)
{
	const struct sja1105_private *priv = ctx;
	const struct sja1105_reset_cmd *reset = data;
	const struct sja1105_regs *regs = priv->info->regs;
	struct device *dev = priv->ds->dev;
	u8 packed_buf[SJA1105_SIZE_RESET_CMD];

	if (reset->switch_rst)
		dev_dbg(dev, "Main reset for all functional modules requested\n");
	if (reset->cfg_rst)
		dev_dbg(dev, "Chip configuration reset requested\n");
	if (reset->car_rst)
		dev_dbg(dev, "Clock and reset control logic reset requested\n");
	if (reset->otp_rst)
		dev_dbg(dev, "OTP read cycle for reading product "
			"config settings requested\n");
	if (reset->warm_rst)
		dev_dbg(dev, "Warm reset requested\n");
	if (reset->cold_rst)
		dev_dbg(dev, "Cold reset requested\n");
	if (reset->por_rst)
		dev_dbg(dev, "Power-on reset requested\n");

	sja1105pqrs_reset_cmd_pack(packed_buf, reset);

	return sja1105_spi_send_packed_buf(priv, SPI_WRITE, regs->rgu,
					   packed_buf, SJA1105_SIZE_RESET_CMD);
}

static int sja1105_cold_reset(const struct sja1105_private *priv)
{
	struct sja1105_reset_cmd reset = {0};

	reset.cold_rst = 1;
	return priv->info->reset_cmd(priv, &reset);
}

int sja1105_inhibit_tx(const struct sja1105_private *priv,
		       unsigned long port_bitmap, bool tx_inhibited)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u64 inhibit_cmd;
	int rc;

	rc = sja1105_spi_send_int(priv, SPI_READ, regs->port_control,
				  &inhibit_cmd, SJA1105_SIZE_PORT_CTRL);
	if (rc < 0)
		return rc;

	if (tx_inhibited)
		inhibit_cmd |= port_bitmap;
	else
		inhibit_cmd &= ~port_bitmap;

	return sja1105_spi_send_int(priv, SPI_WRITE, regs->port_control,
				    &inhibit_cmd, SJA1105_SIZE_PORT_CTRL);
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

	rc = sja1105_spi_send_packed_buf(priv, SPI_READ,
					 regs->status,
					 packed_buf, 4);
	if (rc < 0)
		return rc;

	sja1105_status_unpack(packed_buf, status);

	return 0;
}

/* Not const because unpacking priv->static_config into buffers and preparing
 * for upload requires the recalculation of table CRCs and updating the
 * structures with these.
 */
static int
static_config_buf_prepare_for_upload(struct sja1105_private *priv,
				     void *config_buf, int buf_len)
{
	struct sja1105_static_config *config = &priv->static_config;
	struct sja1105_table_header final_header;
	sja1105_config_valid_t valid;
	char *final_header_ptr;
	int crc_len;

	valid = sja1105_static_config_check_valid(config);
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
	unsigned long port_bitmap = GENMASK_ULL(SJA1105_NUM_PORTS - 1, 0);
	struct sja1105_static_config *config = &priv->static_config;
	const struct sja1105_regs *regs = priv->info->regs;
	struct device *dev = &priv->spidev->dev;
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
		return -EINVAL;
	}
	/* Prevent PHY jabbering during switch reset by inhibiting
	 * Tx on all ports and waiting for current packet to drain.
	 * Otherwise, the PHY will see an unterminated Ethernet packet.
	 */
	rc = sja1105_inhibit_tx(priv, port_bitmap, true);
	if (rc < 0) {
		dev_err(dev, "Failed to inhibit Tx on ports\n");
		return -ENXIO;
	}
	/* Wait for an eventual egress packet to finish transmission
	 * (reach IFG). It is guaranteed that a second one will not
	 * follow, and that switch cold reset is thus safe
	 */
	usleep_range(500, 1000);
	do {
		/* Put the SJA1105 in programming mode */
		rc = sja1105_cold_reset(priv);
		if (rc < 0) {
			dev_err(dev, "Failed to reset switch, retrying...\n");
			continue;
		}
		/* Wait for the switch to come out of reset */
		usleep_range(1000, 5000);
		/* Upload the static config to the device */
		rc = sja1105_spi_send_long_packed_buf(priv, SPI_WRITE,
						      regs->config,
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

	rc = sja1105_ptp_reset(priv);
	if (rc < 0)
		dev_err(dev, "Failed to reset PTP clock: %d\n", rc);

	dev_info(dev, "Reset switch and programmed static config\n");

out:
	kfree(config_buf);
	return rc;
}

static struct sja1105_regs sja1105et_regs = {
	.device_id = 0x0,
	.prod_id = 0x100BC3,
	.status = 0x1,
	.port_control = 0x11,
	.config = 0x020000,
	.rgu = 0x100440,
	/* UM10944.pdf, Table 86, ACU Register overview */
	.pad_mii_tx = {0x100800, 0x100802, 0x100804, 0x100806, 0x100808},
	.rmii_pll1 = 0x10000A,
	.cgu_idiv = {0x10000B, 0x10000C, 0x10000D, 0x10000E, 0x10000F},
	.mac = {0x200, 0x202, 0x204, 0x206, 0x208},
	.mac_hl1 = {0x400, 0x410, 0x420, 0x430, 0x440},
	.mac_hl2 = {0x600, 0x610, 0x620, 0x630, 0x640},
	/* UM10944.pdf, Table 78, CGU Register overview */
	.mii_tx_clk = {0x100013, 0x10001A, 0x100021, 0x100028, 0x10002F},
	.mii_rx_clk = {0x100014, 0x10001B, 0x100022, 0x100029, 0x100030},
	.mii_ext_tx_clk = {0x100018, 0x10001F, 0x100026, 0x10002D, 0x100034},
	.mii_ext_rx_clk = {0x100019, 0x100020, 0x100027, 0x10002E, 0x100035},
	.rgmii_tx_clk = {0x100016, 0x10001D, 0x100024, 0x10002B, 0x100032},
	.rmii_ref_clk = {0x100015, 0x10001C, 0x100023, 0x10002A, 0x100031},
	.rmii_ext_tx_clk = {0x100018, 0x10001F, 0x100026, 0x10002D, 0x100034},
	.ptpegr_ts = {0xC0, 0xC2, 0xC4, 0xC6, 0xC8},
	.ptp_control = 0x17,
	.ptpclk = 0x18, /* Spans 0x18 to 0x19 */
	.ptpclkrate = 0x1A,
	.ptptsclk = 0x1B, /* Spans 0x1B to 0x1C */
};

static struct sja1105_regs sja1105pqrs_regs = {
	.device_id = 0x0,
	.prod_id = 0x100BC3,
	.status = 0x1,
	.port_control = 0x12,
	.config = 0x020000,
	.rgu = 0x100440,
	/* UM10944.pdf, Table 86, ACU Register overview */
	.pad_mii_tx = {0x100800, 0x100802, 0x100804, 0x100806, 0x100808},
	.pad_mii_id = {0x100810, 0x100811, 0x100812, 0x100813, 0x100814},
	.rmii_pll1 = 0x10000A,
	.cgu_idiv = {0x10000B, 0x10000C, 0x10000D, 0x10000E, 0x10000F},
	.mac = {0x200, 0x202, 0x204, 0x206, 0x208},
	.mac_hl1 = {0x400, 0x410, 0x420, 0x430, 0x440},
	.mac_hl2 = {0x600, 0x610, 0x620, 0x630, 0x640},
	/* UM11040.pdf, Table 114 */
	.mii_tx_clk = {0x100013, 0x100019, 0x10001F, 0x100025, 0x10002B},
	.mii_rx_clk = {0x100014, 0x10001A, 0x100020, 0x100026, 0x10002C},
	.mii_ext_tx_clk = {0x100017, 0x10001D, 0x100023, 0x100029, 0x10002F},
	.mii_ext_rx_clk = {0x100018, 0x10001E, 0x100024, 0x10002A, 0x100030},
	.rgmii_tx_clk = {0x100016, 0x10001C, 0x100022, 0x100028, 0x10002E},
	.rmii_ref_clk = {0x100015, 0x10001B, 0x100021, 0x100027, 0x10002D},
	.rmii_ext_tx_clk = {0x100017, 0x10001D, 0x100023, 0x100029, 0x10002F},
	.qlevel = {0x604, 0x614, 0x624, 0x634, 0x644},
	.ptpegr_ts = {0xC0, 0xC4, 0xC8, 0xCC, 0xD0},
	.ptp_control = 0x18,
	.ptpclk = 0x19,
	.ptpclkrate = 0x1B,
	.ptptsclk = 0x1C,
};

struct sja1105_info sja1105e_info = {
	.device_id		= SJA1105E_DEVICE_ID,
	.part_no		= SJA1105ET_PART_NO,
	.static_ops		= sja1105e_table_ops,
	.dyn_ops		= sja1105et_dyn_ops,
	.ptp_ts_bits		= 24,
	.ptpegr_ts_bytes	= 4,
	.reset_cmd		= sja1105et_reset_cmd,
	.fdb_add_cmd		= sja1105et_fdb_add,
	.fdb_del_cmd		= sja1105et_fdb_del,
	.ptp_cmd		= sja1105et_ptp_cmd,
	.regs			= &sja1105et_regs,
	.name			= "SJA1105E",
};
struct sja1105_info sja1105t_info = {
	.device_id		= SJA1105T_DEVICE_ID,
	.part_no		= SJA1105ET_PART_NO,
	.static_ops		= sja1105t_table_ops,
	.dyn_ops		= sja1105et_dyn_ops,
	.ptp_ts_bits		= 24,
	.ptpegr_ts_bytes	= 4,
	.reset_cmd		= sja1105et_reset_cmd,
	.fdb_add_cmd		= sja1105et_fdb_add,
	.fdb_del_cmd		= sja1105et_fdb_del,
	.ptp_cmd		= sja1105et_ptp_cmd,
	.regs			= &sja1105et_regs,
	.name			= "SJA1105T",
};
struct sja1105_info sja1105p_info = {
	.device_id		= SJA1105PR_DEVICE_ID,
	.part_no		= SJA1105P_PART_NO,
	.static_ops		= sja1105p_table_ops,
	.dyn_ops		= sja1105pqrs_dyn_ops,
	.ptp_ts_bits		= 32,
	.ptpegr_ts_bytes	= 8,
	.setup_rgmii_delay	= sja1105pqrs_setup_rgmii_delay,
	.reset_cmd		= sja1105pqrs_reset_cmd,
	.fdb_add_cmd		= sja1105pqrs_fdb_add,
	.fdb_del_cmd		= sja1105pqrs_fdb_del,
	.ptp_cmd		= sja1105pqrs_ptp_cmd,
	.regs			= &sja1105pqrs_regs,
	.name			= "SJA1105P",
};
struct sja1105_info sja1105q_info = {
	.device_id		= SJA1105QS_DEVICE_ID,
	.part_no		= SJA1105Q_PART_NO,
	.static_ops		= sja1105q_table_ops,
	.dyn_ops		= sja1105pqrs_dyn_ops,
	.ptp_ts_bits		= 32,
	.ptpegr_ts_bytes	= 8,
	.setup_rgmii_delay	= sja1105pqrs_setup_rgmii_delay,
	.reset_cmd		= sja1105pqrs_reset_cmd,
	.fdb_add_cmd		= sja1105pqrs_fdb_add,
	.fdb_del_cmd		= sja1105pqrs_fdb_del,
	.ptp_cmd		= sja1105pqrs_ptp_cmd,
	.regs			= &sja1105pqrs_regs,
	.name			= "SJA1105Q",
};
struct sja1105_info sja1105r_info = {
	.device_id		= SJA1105PR_DEVICE_ID,
	.part_no		= SJA1105R_PART_NO,
	.static_ops		= sja1105r_table_ops,
	.dyn_ops		= sja1105pqrs_dyn_ops,
	.ptp_ts_bits		= 32,
	.ptpegr_ts_bytes	= 8,
	.setup_rgmii_delay	= sja1105pqrs_setup_rgmii_delay,
	.reset_cmd		= sja1105pqrs_reset_cmd,
	.fdb_add_cmd		= sja1105pqrs_fdb_add,
	.fdb_del_cmd		= sja1105pqrs_fdb_del,
	.ptp_cmd		= sja1105pqrs_ptp_cmd,
	.regs			= &sja1105pqrs_regs,
	.name			= "SJA1105R",
};
struct sja1105_info sja1105s_info = {
	.device_id		= SJA1105QS_DEVICE_ID,
	.part_no		= SJA1105S_PART_NO,
	.static_ops		= sja1105s_table_ops,
	.dyn_ops		= sja1105pqrs_dyn_ops,
	.regs			= &sja1105pqrs_regs,
	.ptp_ts_bits		= 32,
	.ptpegr_ts_bytes	= 8,
	.setup_rgmii_delay	= sja1105pqrs_setup_rgmii_delay,
	.reset_cmd		= sja1105pqrs_reset_cmd,
	.fdb_add_cmd		= sja1105pqrs_fdb_add,
	.fdb_del_cmd		= sja1105pqrs_fdb_del,
	.ptp_cmd		= sja1105pqrs_ptp_cmd,
	.name			= "SJA1105S",
};
