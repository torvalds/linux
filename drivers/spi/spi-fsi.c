// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) IBM Corporation 2020

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/fsi.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/spi/spi.h>

#define FSI_ENGID_SPI			0x23
#define FSI_MBOX_ROOT_CTRL_8		0x2860
#define  FSI_MBOX_ROOT_CTRL_8_SPI_MUX	 0xf0000000

#define FSI2SPI_DATA0			0x00
#define FSI2SPI_DATA1			0x04
#define FSI2SPI_CMD			0x08
#define  FSI2SPI_CMD_WRITE		 BIT(31)
#define FSI2SPI_RESET			0x18
#define FSI2SPI_STATUS			0x1c
#define  FSI2SPI_STATUS_ANY_ERROR	 BIT(31)
#define FSI2SPI_IRQ			0x20

#define SPI_FSI_BASE			0x70000
#define SPI_FSI_INIT_TIMEOUT_MS		1000
#define SPI_FSI_MAX_XFR_SIZE		2048
#define SPI_FSI_MAX_XFR_SIZE_RESTRICTED	8

#define SPI_FSI_ERROR			0x0
#define SPI_FSI_COUNTER_CFG		0x1
#define  SPI_FSI_COUNTER_CFG_LOOPS(x)	 (((u64)(x) & 0xffULL) << 32)
#define  SPI_FSI_COUNTER_CFG_N2_RX	 BIT_ULL(8)
#define  SPI_FSI_COUNTER_CFG_N2_TX	 BIT_ULL(9)
#define  SPI_FSI_COUNTER_CFG_N2_IMPLICIT BIT_ULL(10)
#define  SPI_FSI_COUNTER_CFG_N2_RELOAD	 BIT_ULL(11)
#define SPI_FSI_CFG1			0x2
#define SPI_FSI_CLOCK_CFG		0x3
#define  SPI_FSI_CLOCK_CFG_MM_ENABLE	 BIT_ULL(32)
#define  SPI_FSI_CLOCK_CFG_ECC_DISABLE	 (BIT_ULL(35) | BIT_ULL(33))
#define  SPI_FSI_CLOCK_CFG_RESET1	 (BIT_ULL(36) | BIT_ULL(38))
#define  SPI_FSI_CLOCK_CFG_RESET2	 (BIT_ULL(37) | BIT_ULL(39))
#define  SPI_FSI_CLOCK_CFG_MODE		 (BIT_ULL(41) | BIT_ULL(42))
#define  SPI_FSI_CLOCK_CFG_SCK_RECV_DEL	 GENMASK_ULL(51, 44)
#define   SPI_FSI_CLOCK_CFG_SCK_NO_DEL	  BIT_ULL(51)
#define  SPI_FSI_CLOCK_CFG_SCK_DIV	 GENMASK_ULL(63, 52)
#define SPI_FSI_MMAP			0x4
#define SPI_FSI_DATA_TX			0x5
#define SPI_FSI_DATA_RX			0x6
#define SPI_FSI_SEQUENCE		0x7
#define  SPI_FSI_SEQUENCE_STOP		 0x00
#define  SPI_FSI_SEQUENCE_SEL_SLAVE(x)	 (0x10 | ((x) & 0xf))
#define  SPI_FSI_SEQUENCE_SHIFT_OUT(x)	 (0x30 | ((x) & 0xf))
#define  SPI_FSI_SEQUENCE_SHIFT_IN(x)	 (0x40 | ((x) & 0xf))
#define  SPI_FSI_SEQUENCE_COPY_DATA_TX	 0xc0
#define  SPI_FSI_SEQUENCE_BRANCH(x)	 (0xe0 | ((x) & 0xf))
#define SPI_FSI_STATUS			0x8
#define  SPI_FSI_STATUS_ERROR		 \
	(GENMASK_ULL(31, 21) | GENMASK_ULL(15, 12))
#define  SPI_FSI_STATUS_SEQ_STATE	 GENMASK_ULL(55, 48)
#define   SPI_FSI_STATUS_SEQ_STATE_IDLE	  BIT_ULL(48)
#define  SPI_FSI_STATUS_TDR_UNDERRUN	 BIT_ULL(57)
#define  SPI_FSI_STATUS_TDR_OVERRUN	 BIT_ULL(58)
#define  SPI_FSI_STATUS_TDR_FULL	 BIT_ULL(59)
#define  SPI_FSI_STATUS_RDR_UNDERRUN	 BIT_ULL(61)
#define  SPI_FSI_STATUS_RDR_OVERRUN	 BIT_ULL(62)
#define  SPI_FSI_STATUS_RDR_FULL	 BIT_ULL(63)
#define  SPI_FSI_STATUS_ANY_ERROR	 \
	(SPI_FSI_STATUS_ERROR | \
	 SPI_FSI_STATUS_TDR_OVERRUN | SPI_FSI_STATUS_RDR_UNDERRUN | \
	 SPI_FSI_STATUS_RDR_OVERRUN)
#define SPI_FSI_PORT_CTRL		0x9

struct fsi_spi {
	struct device *dev;	/* SPI controller device */
	struct fsi_device *fsi;	/* FSI2SPI CFAM engine device */
	u32 base;
	size_t max_xfr_size;
	bool restricted;
};

struct fsi_spi_sequence {
	int bit;
	u64 data;
};

static int fsi_spi_check_mux(struct fsi_device *fsi, struct device *dev)
{
	int rc;
	u32 root_ctrl_8;
	__be32 root_ctrl_8_be;

	rc = fsi_slave_read(fsi->slave, FSI_MBOX_ROOT_CTRL_8, &root_ctrl_8_be,
			    sizeof(root_ctrl_8_be));
	if (rc)
		return rc;

	root_ctrl_8 = be32_to_cpu(root_ctrl_8_be);
	dev_dbg(dev, "Root control register 8: %08x\n", root_ctrl_8);
	if ((root_ctrl_8 & FSI_MBOX_ROOT_CTRL_8_SPI_MUX) ==
	     FSI_MBOX_ROOT_CTRL_8_SPI_MUX)
		return 0;

	return -ENOLINK;
}

static int fsi_spi_check_status(struct fsi_spi *ctx)
{
	int rc;
	u32 sts;
	__be32 sts_be;

	rc = fsi_device_read(ctx->fsi, FSI2SPI_STATUS, &sts_be,
			     sizeof(sts_be));
	if (rc)
		return rc;

	sts = be32_to_cpu(sts_be);
	if (sts & FSI2SPI_STATUS_ANY_ERROR) {
		dev_err(ctx->dev, "Error with FSI2SPI interface: %08x.\n", sts);
		return -EIO;
	}

	return 0;
}

static int fsi_spi_read_reg(struct fsi_spi *ctx, u32 offset, u64 *value)
{
	int rc;
	__be32 cmd_be;
	__be32 data_be;
	u32 cmd = offset + ctx->base;

	*value = 0ULL;

	if (cmd & FSI2SPI_CMD_WRITE)
		return -EINVAL;

	cmd_be = cpu_to_be32(cmd);
	rc = fsi_device_write(ctx->fsi, FSI2SPI_CMD, &cmd_be, sizeof(cmd_be));
	if (rc)
		return rc;

	rc = fsi_spi_check_status(ctx);
	if (rc)
		return rc;

	rc = fsi_device_read(ctx->fsi, FSI2SPI_DATA0, &data_be,
			     sizeof(data_be));
	if (rc)
		return rc;

	*value |= (u64)be32_to_cpu(data_be) << 32;

	rc = fsi_device_read(ctx->fsi, FSI2SPI_DATA1, &data_be,
			     sizeof(data_be));
	if (rc)
		return rc;

	*value |= (u64)be32_to_cpu(data_be);
	dev_dbg(ctx->dev, "Read %02x[%016llx].\n", offset, *value);

	return 0;
}

static int fsi_spi_write_reg(struct fsi_spi *ctx, u32 offset, u64 value)
{
	int rc;
	__be32 cmd_be;
	__be32 data_be;
	u32 cmd = offset + ctx->base;

	if (cmd & FSI2SPI_CMD_WRITE)
		return -EINVAL;

	dev_dbg(ctx->dev, "Write %02x[%016llx].\n", offset, value);

	data_be = cpu_to_be32(upper_32_bits(value));
	rc = fsi_device_write(ctx->fsi, FSI2SPI_DATA0, &data_be,
			      sizeof(data_be));
	if (rc)
		return rc;

	data_be = cpu_to_be32(lower_32_bits(value));
	rc = fsi_device_write(ctx->fsi, FSI2SPI_DATA1, &data_be,
			      sizeof(data_be));
	if (rc)
		return rc;

	cmd_be = cpu_to_be32(cmd | FSI2SPI_CMD_WRITE);
	rc = fsi_device_write(ctx->fsi, FSI2SPI_CMD, &cmd_be, sizeof(cmd_be));
	if (rc)
		return rc;

	return fsi_spi_check_status(ctx);
}

static int fsi_spi_data_in(u64 in, u8 *rx, int len)
{
	int i;
	int num_bytes = min(len, 8);

	for (i = 0; i < num_bytes; ++i)
		rx[i] = (u8)(in >> (8 * ((num_bytes - 1) - i)));

	return num_bytes;
}

static int fsi_spi_data_out(u64 *out, const u8 *tx, int len)
{
	int i;
	int num_bytes = min(len, 8);
	u8 *out_bytes = (u8 *)out;

	/* Unused bytes of the tx data should be 0. */
	*out = 0ULL;

	for (i = 0; i < num_bytes; ++i)
		out_bytes[8 - (i + 1)] = tx[i];

	return num_bytes;
}

static int fsi_spi_reset(struct fsi_spi *ctx)
{
	int rc;

	dev_dbg(ctx->dev, "Resetting SPI controller.\n");

	rc = fsi_spi_write_reg(ctx, SPI_FSI_CLOCK_CFG,
			       SPI_FSI_CLOCK_CFG_RESET1);
	if (rc)
		return rc;

	rc = fsi_spi_write_reg(ctx, SPI_FSI_CLOCK_CFG,
			       SPI_FSI_CLOCK_CFG_RESET2);
	if (rc)
		return rc;

	return fsi_spi_write_reg(ctx, SPI_FSI_STATUS, 0ULL);
}

static int fsi_spi_sequence_add(struct fsi_spi_sequence *seq, u8 val)
{
	/*
	 * Add the next byte of instruction to the 8-byte sequence register.
	 * Then decrement the counter so that the next instruction will go in
	 * the right place. Return the index of the slot we just filled in the
	 * sequence register.
	 */
	seq->data |= (u64)val << seq->bit;
	seq->bit -= 8;

	return ((64 - seq->bit) / 8) - 2;
}

static void fsi_spi_sequence_init(struct fsi_spi_sequence *seq)
{
	seq->bit = 56;
	seq->data = 0ULL;
}

static int fsi_spi_sequence_transfer(struct fsi_spi *ctx,
				     struct fsi_spi_sequence *seq,
				     struct spi_transfer *transfer)
{
	int loops;
	int idx;
	int rc;
	u8 val = 0;
	u8 len = min(transfer->len, 8U);
	u8 rem = transfer->len % len;

	loops = transfer->len / len;

	if (transfer->tx_buf) {
		val = SPI_FSI_SEQUENCE_SHIFT_OUT(len);
		idx = fsi_spi_sequence_add(seq, val);

		if (rem)
			rem = SPI_FSI_SEQUENCE_SHIFT_OUT(rem);
	} else if (transfer->rx_buf) {
		val = SPI_FSI_SEQUENCE_SHIFT_IN(len);
		idx = fsi_spi_sequence_add(seq, val);

		if (rem)
			rem = SPI_FSI_SEQUENCE_SHIFT_IN(rem);
	} else {
		return -EINVAL;
	}

	if (ctx->restricted && loops > 1) {
		dev_warn(ctx->dev,
			 "Transfer too large; no branches permitted.\n");
		return -EINVAL;
	}

	if (loops > 1) {
		u64 cfg = SPI_FSI_COUNTER_CFG_LOOPS(loops - 1);

		fsi_spi_sequence_add(seq, SPI_FSI_SEQUENCE_BRANCH(idx));

		if (transfer->rx_buf)
			cfg |= SPI_FSI_COUNTER_CFG_N2_RX |
				SPI_FSI_COUNTER_CFG_N2_TX |
				SPI_FSI_COUNTER_CFG_N2_IMPLICIT |
				SPI_FSI_COUNTER_CFG_N2_RELOAD;

		rc = fsi_spi_write_reg(ctx, SPI_FSI_COUNTER_CFG, cfg);
		if (rc)
			return rc;
	} else {
		fsi_spi_write_reg(ctx, SPI_FSI_COUNTER_CFG, 0ULL);
	}

	if (rem)
		fsi_spi_sequence_add(seq, rem);

	return 0;
}

static int fsi_spi_transfer_data(struct fsi_spi *ctx,
				 struct spi_transfer *transfer)
{
	int rc = 0;
	u64 status = 0ULL;
	u64 cfg = 0ULL;

	if (transfer->tx_buf) {
		int nb;
		int sent = 0;
		u64 out = 0ULL;
		const u8 *tx = transfer->tx_buf;

		while (transfer->len > sent) {
			nb = fsi_spi_data_out(&out, &tx[sent],
					      (int)transfer->len - sent);

			rc = fsi_spi_write_reg(ctx, SPI_FSI_DATA_TX, out);
			if (rc)
				return rc;

			do {
				rc = fsi_spi_read_reg(ctx, SPI_FSI_STATUS,
						      &status);
				if (rc)
					return rc;

				if (status & SPI_FSI_STATUS_ANY_ERROR) {
					rc = fsi_spi_reset(ctx);
					if (rc)
						return rc;

					return -EREMOTEIO;
				}
			} while (status & SPI_FSI_STATUS_TDR_FULL);

			sent += nb;
		}
	} else if (transfer->rx_buf) {
		int recv = 0;
		u64 in = 0ULL;
		u8 *rx = transfer->rx_buf;

		rc = fsi_spi_read_reg(ctx, SPI_FSI_COUNTER_CFG, &cfg);
		if (rc)
			return rc;

		if (cfg & SPI_FSI_COUNTER_CFG_N2_IMPLICIT) {
			rc = fsi_spi_write_reg(ctx, SPI_FSI_DATA_TX, 0);
			if (rc)
				return rc;
		}

		while (transfer->len > recv) {
			do {
				rc = fsi_spi_read_reg(ctx, SPI_FSI_STATUS,
						      &status);
				if (rc)
					return rc;

				if (status & SPI_FSI_STATUS_ANY_ERROR) {
					rc = fsi_spi_reset(ctx);
					if (rc)
						return rc;

					return -EREMOTEIO;
				}
			} while (!(status & SPI_FSI_STATUS_RDR_FULL));

			rc = fsi_spi_read_reg(ctx, SPI_FSI_DATA_RX, &in);
			if (rc)
				return rc;

			recv += fsi_spi_data_in(in, &rx[recv],
						(int)transfer->len - recv);
		}
	}

	return 0;
}

static int fsi_spi_transfer_init(struct fsi_spi *ctx)
{
	int rc;
	bool reset = false;
	unsigned long end;
	u64 seq_state;
	u64 clock_cfg = 0ULL;
	u64 status = 0ULL;
	u64 wanted_clock_cfg = SPI_FSI_CLOCK_CFG_ECC_DISABLE |
		SPI_FSI_CLOCK_CFG_SCK_NO_DEL |
		FIELD_PREP(SPI_FSI_CLOCK_CFG_SCK_DIV, 19);

	end = jiffies + msecs_to_jiffies(SPI_FSI_INIT_TIMEOUT_MS);
	do {
		if (time_after(jiffies, end))
			return -ETIMEDOUT;

		rc = fsi_spi_read_reg(ctx, SPI_FSI_STATUS, &status);
		if (rc)
			return rc;

		seq_state = status & SPI_FSI_STATUS_SEQ_STATE;

		if (status & (SPI_FSI_STATUS_ANY_ERROR |
			      SPI_FSI_STATUS_TDR_FULL |
			      SPI_FSI_STATUS_RDR_FULL)) {
			if (reset)
				return -EIO;

			rc = fsi_spi_reset(ctx);
			if (rc)
				return rc;

			reset = true;
			continue;
		}
	} while (seq_state && (seq_state != SPI_FSI_STATUS_SEQ_STATE_IDLE));

	rc = fsi_spi_read_reg(ctx, SPI_FSI_CLOCK_CFG, &clock_cfg);
	if (rc)
		return rc;

	if ((clock_cfg & (SPI_FSI_CLOCK_CFG_MM_ENABLE |
			  SPI_FSI_CLOCK_CFG_ECC_DISABLE |
			  SPI_FSI_CLOCK_CFG_MODE |
			  SPI_FSI_CLOCK_CFG_SCK_RECV_DEL |
			  SPI_FSI_CLOCK_CFG_SCK_DIV)) != wanted_clock_cfg)
		rc = fsi_spi_write_reg(ctx, SPI_FSI_CLOCK_CFG,
				       wanted_clock_cfg);

	return rc;
}

static int fsi_spi_transfer_one_message(struct spi_controller *ctlr,
					struct spi_message *mesg)
{
	int rc;
	u8 seq_slave = SPI_FSI_SEQUENCE_SEL_SLAVE(mesg->spi->chip_select + 1);
	struct spi_transfer *transfer;
	struct fsi_spi *ctx = spi_controller_get_devdata(ctlr);

	rc = fsi_spi_check_mux(ctx->fsi, ctx->dev);
	if (rc)
		goto error;

	list_for_each_entry(transfer, &mesg->transfers, transfer_list) {
		struct fsi_spi_sequence seq;
		struct spi_transfer *next = NULL;

		/* Sequencer must do shift out (tx) first. */
		if (!transfer->tx_buf ||
		    transfer->len > (ctx->max_xfr_size + 8)) {
			rc = -EINVAL;
			goto error;
		}

		dev_dbg(ctx->dev, "Start tx of %d bytes.\n", transfer->len);

		rc = fsi_spi_transfer_init(ctx);
		if (rc < 0)
			goto error;

		fsi_spi_sequence_init(&seq);
		fsi_spi_sequence_add(&seq, seq_slave);

		rc = fsi_spi_sequence_transfer(ctx, &seq, transfer);
		if (rc)
			goto error;

		if (!list_is_last(&transfer->transfer_list,
				  &mesg->transfers)) {
			next = list_next_entry(transfer, transfer_list);

			/* Sequencer can only do shift in (rx) after tx. */
			if (next->rx_buf) {
				if (next->len > ctx->max_xfr_size) {
					rc = -EINVAL;
					goto error;
				}

				dev_dbg(ctx->dev, "Sequence rx of %d bytes.\n",
					next->len);

				rc = fsi_spi_sequence_transfer(ctx, &seq,
							       next);
				if (rc)
					goto error;
			} else {
				next = NULL;
			}
		}

		fsi_spi_sequence_add(&seq, SPI_FSI_SEQUENCE_SEL_SLAVE(0));

		rc = fsi_spi_write_reg(ctx, SPI_FSI_SEQUENCE, seq.data);
		if (rc)
			goto error;

		rc = fsi_spi_transfer_data(ctx, transfer);
		if (rc)
			goto error;

		if (next) {
			rc = fsi_spi_transfer_data(ctx, next);
			if (rc)
				goto error;

			transfer = next;
		}
	}

error:
	mesg->status = rc;
	spi_finalize_current_message(ctlr);

	return rc;
}

static size_t fsi_spi_max_transfer_size(struct spi_device *spi)
{
	struct fsi_spi *ctx = spi_controller_get_devdata(spi->controller);

	return ctx->max_xfr_size;
}

static int fsi_spi_probe(struct device *dev)
{
	int rc;
	struct device_node *np;
	int num_controllers_registered = 0;
	struct fsi_device *fsi = to_fsi_dev(dev);

	rc = fsi_spi_check_mux(fsi, dev);
	if (rc)
		return -ENODEV;

	for_each_available_child_of_node(dev->of_node, np) {
		u32 base;
		struct fsi_spi *ctx;
		struct spi_controller *ctlr;

		if (of_property_read_u32(np, "reg", &base))
			continue;

		ctlr = spi_alloc_master(dev, sizeof(*ctx));
		if (!ctlr) {
			of_node_put(np);
			break;
		}

		ctlr->dev.of_node = np;
		ctlr->num_chipselect = of_get_available_child_count(np) ?: 1;
		ctlr->flags = SPI_CONTROLLER_HALF_DUPLEX;
		ctlr->max_transfer_size = fsi_spi_max_transfer_size;
		ctlr->transfer_one_message = fsi_spi_transfer_one_message;

		ctx = spi_controller_get_devdata(ctlr);
		ctx->dev = &ctlr->dev;
		ctx->fsi = fsi;
		ctx->base = base + SPI_FSI_BASE;

		if (of_device_is_compatible(np, "ibm,fsi2spi-restricted")) {
			ctx->restricted = true;
			ctx->max_xfr_size = SPI_FSI_MAX_XFR_SIZE_RESTRICTED;
		} else {
			ctx->restricted = false;
			ctx->max_xfr_size = SPI_FSI_MAX_XFR_SIZE;
		}

		rc = devm_spi_register_controller(dev, ctlr);
		if (rc)
			spi_controller_put(ctlr);
		else
			num_controllers_registered++;
	}

	if (!num_controllers_registered)
		return -ENODEV;

	return 0;
}

static const struct fsi_device_id fsi_spi_ids[] = {
	{ FSI_ENGID_SPI, FSI_VERSION_ANY },
	{ }
};
MODULE_DEVICE_TABLE(fsi, fsi_spi_ids);

static struct fsi_driver fsi_spi_driver = {
	.id_table = fsi_spi_ids,
	.drv = {
		.name = "spi-fsi",
		.bus = &fsi_bus_type,
		.probe = fsi_spi_probe,
	},
};
module_fsi_driver(fsi_spi_driver);

MODULE_AUTHOR("Eddie James <eajames@linux.ibm.com>");
MODULE_DESCRIPTION("FSI attached SPI controller");
MODULE_LICENSE("GPL");
