// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2025 Amlogic, Inc. All rights reserved
 *
 * Driver for the SPI Mode of Amlogic Flash Controller
 * Authors:
 *  Liang Yang <liang.yang@amlogic.com>
 *  Feng Chen <feng.chen@amlogic.com>
 *  Xianwei Zhao <xianwei.zhao@amlogic.com>
 */

#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/dma-mapping.h>
#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/mtd/spinand.h>
#include <linux/spi/spi-mem.h>

#define SFC_CMD				0x00
#define SFC_CFG				0x04
#define SFC_DADR			0x08
#define SFC_IADR			0x0c
#define SFC_BUF				0x10
#define SFC_INFO			0x14
#define SFC_DC				0x18
#define SFC_ADR				0x1c
#define SFC_DL				0x20
#define SFC_DH				0x24
#define SFC_CADR			0x28
#define SFC_SADR			0x2c
#define SFC_RX_IDX			0x34
#define SFC_RX_DAT			0x38
#define SFC_SPI_CFG			0x40

/* settings in SFC_CMD  */

/* 4 bits support 4 chip select, high false, low select but spi support 2*/
#define CHIP_SELECT_MASK		GENMASK(13, 10)
#define CS_NONE				0xf
#define CS_0				0xe
#define CS_1				0xd

#define CLE				(0x5 << 14)
#define ALE				(0x6 << 14)
#define DWR				(0x4 << 14)
#define DRD				(0x8 << 14)
#define DUMMY				(0xb << 14)
#define IDLE				(0xc << 14)
#define IDLE_CYCLE_MASK			GENMASK(9, 0)
#define EXT_CYCLE_MASK			GENMASK(9, 0)

#define OP_M2N				((0 << 17) | (2 << 20))
#define OP_N2M				((1 << 17) | (2 << 20))
#define OP_STS				((3 << 17) | (2 << 20))
#define OP_ADL				((0 << 16) | (3 << 20))
#define OP_ADH				((1 << 16) | (3 << 20))
#define OP_AIL				((2 << 16) | (3 << 20))
#define OP_AIH				((3 << 16) | (3 << 20))
#define OP_ASL				((4 << 16) | (3 << 20))
#define OP_ASH				((5 << 16) | (3 << 20))
#define OP_SEED				((8 << 16) | (3 << 20))
#define SEED_MASK			GENMASK(14, 0)
#define ENABLE_RANDOM			BIT(19)

#define CMD_COMMAND(cs_sel, cmd)	(CLE | ((cs_sel) << 10) | (cmd))
#define CMD_ADDR(cs_sel, addr)		(ALE | ((cs_sel) << 10) | (addr))
#define CMD_DUMMY(cs_sel, cyc)		(DUMMY | ((cs_sel) << 10) | ((cyc) & EXT_CYCLE_MASK))
#define CMD_IDLE(cs_sel, cyc)		(IDLE | ((cs_sel) << 10) | ((cyc) & IDLE_CYCLE_MASK))
#define CMD_MEM2NAND(bch, pages)	(OP_M2N | ((bch) << 14) | (pages))
#define CMD_NAND2MEM(bch, pages)	(OP_N2M | ((bch) << 14) | (pages))
#define CMD_DATA_ADDRL(addr)		(OP_ADL | ((addr) & 0xffff))
#define CMD_DATA_ADDRH(addr)		(OP_ADH | (((addr) >> 16) & 0xffff))
#define CMD_INFO_ADDRL(addr)		(OP_AIL | ((addr) & 0xffff))
#define CMD_INFO_ADDRH(addr)		(OP_AIH | (((addr) >> 16) & 0xffff))
#define CMD_SEED(seed)			(OP_SEED | ((seed) & SEED_MASK))

#define GET_CMD_SIZE(x)			(((x) >> 22) & GENMASK(4, 0))

#define DEFAULT_PULLUP_CYCLE		2
#define CS_SETUP_CYCLE			1
#define CS_HOLD_CYCLE			2
#define DEFAULT_BUS_CYCLE		4

#define RAW_SIZE			GENMASK(13, 0)
#define RAW_SIZE_BW			14

#define DMA_ADDR_ALIGN			8

/* Bit fields in SFC_SPI_CFG */
#define SPI_MODE_EN			BIT(31)
#define RAW_EXT_SIZE			GENMASK(29, 18)
#define ADDR_LANE			GENMASK(17, 16)
#define CPOL				BIT(15)
#define CPHA				BIT(14)
#define EN_HOLD				BIT(13)
#define EN_WP				BIT(12)
#define TXADJ				GENMASK(11, 8)
#define RXADJ				GENMASK(7, 4)
#define CMD_LANE			GENMASK(3, 2)
#define DATA_LANE			GENMASK(1, 0)
#define LANE_MAX			0x3

/* raw ext size[25:14] + raw size[13:0] */
#define RAW_MAX_RW_SIZE_MASK		GENMASK(25, 0)

/* Ecc fields */
#define ECC_COMPLETE			BIT(31)
#define ECC_UNCORRECTABLE		0x3f
#define ECC_ERR_CNT(x)			(((x) >> 24) & 0x3f)
#define ECC_ZERO_CNT(x)			(((x) >> 16) & 0x3f)

#define ECC_BCH8_512			1
#define ECC_BCH8_1K			2
#define ECC_BCH8_PARITY_BYTES		14
#define ECC_BCH8_USER_BYTES		2
#define ECC_BCH8_INFO_BYTES		(ECC_BCH8_USER_BYTES + ECC_BCH8_PARITY_BYTES)
#define ECC_BCH8_STRENGTH		8
#define ECC_BCH8_DEFAULT_STEP		512
#define ECC_DEFAULT_BCH_MODE		ECC_BCH8_512
#define ECC_PER_INFO_BYTE		8
#define ECC_PATTERN			0x5a
#define ECC_BCH_MAX_SECT_SIZE		63
/* soft flags for sfc */
#define SFC_HWECC			BIT(0)
#define SFC_DATA_RANDOM			BIT(1)
#define SFC_DATA_ONLY			BIT(2)
#define SFC_OOB_ONLY			BIT(3)
#define SFC_DATA_OOB			BIT(4)
#define SFC_AUTO_OOB			BIT(5)
#define SFC_RAW_RW			BIT(6)
#define SFC_XFER_MDOE_MASK		GENMASK(6, 2)

#define SFC_DATABUF_SIZE		8192
#define SFC_INFOBUF_SIZE		256
#define SFC_BUF_SIZE			(SFC_DATABUF_SIZE + SFC_INFOBUF_SIZE)

/* !!! PCB and SPI-NAND chip limitations */
#define SFC_MAX_FREQUENCY		(250 * 1000 * 1000)
#define SFC_MIN_FREQUENCY		(4 * 1000 * 1000)
#define SFC_BUS_DEFAULT_CLK		40000000
#define SFC_MAX_CS_NUM			2

/* SPI-FLASH R/W operation cmd */
#define SPIFLASH_RD_OCTALIO		0xcb
#define SPIFLASH_RD_OCTAL		0x8b
#define SPIFLASH_RD_QUADIO		0xeb
#define SPIFLASH_RD_QUAD		0x6b
#define SPIFLASH_RD_DUALIO		0xbb
#define SPIFLASH_RD_DUAL		0x3b
#define SPIFLASH_RD_FAST		0x0b
#define SPIFLASH_RD			0x03
#define SPIFLASH_WR_OCTALIO		0xC2
#define SPIFLASH_WR_OCTAL		0x82
#define SPIFLASH_WR_QUAD		0x32
#define SPIFLASH_WR			0x02
#define SPIFLASH_UP_QUAD		0x34
#define SPIFLASH_UP			0x84

struct aml_sfc_ecc_cfg {
	u32 stepsize;
	u32 nsteps;
	u32 strength;
	u32 oobsize;
	u32 bch;
};

struct aml_ecc_stats {
	u32 corrected;
	u32 bitflips;
	u32 failed;
};

struct aml_sfc_caps {
	struct aml_sfc_ecc_cfg *ecc_caps;
	u32 num_ecc_caps;
};

struct aml_sfc {
	struct device *dev;
	struct clk *gate_clk;
	struct clk *core_clk;
	struct spi_controller *ctrl;
	struct regmap *regmap_base;
	const struct aml_sfc_caps *caps;
	struct nand_ecc_engine ecc_eng;
	struct aml_ecc_stats ecc_stats;
	dma_addr_t daddr;
	dma_addr_t iaddr;
	u32 info_bytes;
	u32 bus_rate;
	u32 flags;
	u32 rx_adj;
	u32 cs_sel;
	u8 *data_buf;
	__le64 *info_buf;
	u8 *priv;
};

#define AML_ECC_DATA(sz, s, b)	{ .stepsize = (sz), .strength = (s), .bch = (b) }

static struct aml_sfc_ecc_cfg aml_a113l2_ecc_caps[] = {
	AML_ECC_DATA(512, 8, ECC_BCH8_512),
	AML_ECC_DATA(1024, 8, ECC_BCH8_1K),
};

static const struct aml_sfc_caps aml_a113l2_sfc_caps = {
	.ecc_caps = aml_a113l2_ecc_caps,
	.num_ecc_caps = ARRAY_SIZE(aml_a113l2_ecc_caps)
};

static struct aml_sfc *nand_to_aml_sfc(struct nand_device *nand)
{
	struct nand_ecc_engine *eng = nand->ecc.engine;

	return container_of(eng, struct aml_sfc, ecc_eng);
}

static inline void *aml_sfc_to_ecc_ctx(struct aml_sfc *sfc)
{
	return sfc->priv;
}

static int aml_sfc_wait_cmd_finish(struct aml_sfc *sfc, u64 timeout_ms)
{
	u32 cmd_size = 0;
	int ret;

	/*
	 * The SPINAND flash controller employs a two-stage pipeline:
	 * 1) command prefetch; 2) command execution.
	 *
	 * All commands are stored in the FIFO, with one prefetched for execution.
	 *
	 * There are cases where the FIFO is detected as empty, yet a command may
	 * still be in execution and a prefetched command pending execution.
	 *
	 * So, send two idle commands to ensure all previous commands have
	 * been executed.
	 */
	regmap_write(sfc->regmap_base, SFC_CMD, CMD_IDLE(sfc->cs_sel, 0));
	regmap_write(sfc->regmap_base, SFC_CMD, CMD_IDLE(sfc->cs_sel, 0));

	/* Wait for the FIFO to empty. */
	ret = regmap_read_poll_timeout(sfc->regmap_base, SFC_CMD, cmd_size,
				       !GET_CMD_SIZE(cmd_size),
				       10, timeout_ms * 1000);
	if (ret)
		dev_err(sfc->dev, "wait for empty CMD FIFO time out\n");

	return ret;
}

static int aml_sfc_pre_transfer(struct aml_sfc *sfc, u32 idle_cycle, u32 cs2clk_cycle)
{
	int ret;

	ret = regmap_write(sfc->regmap_base, SFC_CMD, CMD_IDLE(CS_NONE, idle_cycle));
	if (ret)
		return ret;

	return regmap_write(sfc->regmap_base, SFC_CMD, CMD_IDLE(sfc->cs_sel, cs2clk_cycle));
}

static int aml_sfc_end_transfer(struct aml_sfc *sfc, u32 clk2cs_cycle)
{
	int ret;

	ret = regmap_write(sfc->regmap_base, SFC_CMD, CMD_IDLE(sfc->cs_sel, clk2cs_cycle));
	if (ret)
		return ret;

	return aml_sfc_wait_cmd_finish(sfc, 0);
}

static int aml_sfc_set_bus_width(struct aml_sfc *sfc, u8 buswidth, u32 mask)
{
	int i;
	u32 conf = 0;

	for (i = 0; i <= LANE_MAX; i++) {
		if (buswidth == 1 << i) {
			conf = i << __ffs(mask);
			return regmap_update_bits(sfc->regmap_base, SFC_SPI_CFG,
						  mask, conf);
		}
	}

	return 0;
}

static int aml_sfc_send_cmd(struct aml_sfc *sfc, const struct spi_mem_op *op)
{
	int i, ret;
	u8 val;

	ret = aml_sfc_set_bus_width(sfc, op->cmd.buswidth, CMD_LANE);
	if (ret)
		return ret;

	for (i = 0; i < op->cmd.nbytes; i++) {
		val = (op->cmd.opcode >> ((op->cmd.nbytes - i - 1) * 8)) & 0xff;
		ret = regmap_write(sfc->regmap_base, SFC_CMD, CMD_COMMAND(sfc->cs_sel, val));
		if (ret)
			return ret;
	}

	return 0;
}

static int aml_sfc_send_addr(struct aml_sfc *sfc, const struct spi_mem_op *op)
{
	int i, ret;
	u8 val;

	ret = aml_sfc_set_bus_width(sfc, op->addr.buswidth, ADDR_LANE);
	if (ret)
		return ret;

	for (i = 0; i < op->addr.nbytes; i++) {
		val = (op->addr.val >> ((op->addr.nbytes - i - 1) * 8)) & 0xff;

		ret = regmap_write(sfc->regmap_base, SFC_CMD, CMD_ADDR(sfc->cs_sel, val));
		if (ret)
			return ret;
	}

	return 0;
}

static bool aml_sfc_is_xio_op(const struct spi_mem_op *op)
{
	switch (op->cmd.opcode) {
	case SPIFLASH_RD_OCTALIO:
	case SPIFLASH_RD_QUADIO:
	case SPIFLASH_RD_DUALIO:
		return true;
	default:
		break;
	}

	return false;
}

static int aml_sfc_send_cmd_addr_dummy(struct aml_sfc *sfc, const struct spi_mem_op *op)
{
	u32 dummy_cycle, cmd;
	int ret;

	ret = aml_sfc_send_cmd(sfc, op);
	if (ret)
		return ret;

	ret = aml_sfc_send_addr(sfc, op);
	if (ret)
		return ret;

	if (op->dummy.nbytes) {
		/*  Dummy buswidth configuration is not supported */
		if (aml_sfc_is_xio_op(op))
			dummy_cycle = op->dummy.nbytes * 8 / op->data.buswidth;
		else
			dummy_cycle = op->dummy.nbytes * 8;
		cmd = CMD_DUMMY(sfc->cs_sel, dummy_cycle - 1);
		return regmap_write(sfc->regmap_base, SFC_CMD, cmd);
	}

	return 0;
}

static bool aml_sfc_is_snand_hwecc_page_op(struct aml_sfc *sfc, const struct spi_mem_op *op)
{
	switch (op->cmd.opcode) {
	/* SPINAND read from cache cmd */
	case SPIFLASH_RD_QUADIO:
	case SPIFLASH_RD_QUAD:
	case SPIFLASH_RD_DUALIO:
	case SPIFLASH_RD_DUAL:
	case SPIFLASH_RD_FAST:
	case SPIFLASH_RD:
	/* SPINAND write to cache cmd */
	case SPIFLASH_WR_QUAD:
	case SPIFLASH_WR:
	case SPIFLASH_UP_QUAD:
	case SPIFLASH_UP:
		if (sfc->flags & SFC_HWECC)
			return true;
		else
			return false;
	default:
		break;
	}

	return false;
}

static int aml_sfc_dma_buffer_setup(struct aml_sfc *sfc, void *databuf,
				    int datalen, void *infobuf, int infolen,
				    enum dma_data_direction dir)
{
	u32 cmd = 0;
	int ret;

	sfc->daddr = dma_map_single(sfc->dev, databuf, datalen, dir);
	ret = dma_mapping_error(sfc->dev, sfc->daddr);
	if (ret) {
		dev_err(sfc->dev, "DMA mapping error\n");
		goto out_map_data;
	}

	cmd = CMD_DATA_ADDRL(sfc->daddr);
	ret = regmap_write(sfc->regmap_base, SFC_CMD, cmd);
	if (ret)
		goto out_map_data;

	cmd = CMD_DATA_ADDRH(sfc->daddr);
	ret = regmap_write(sfc->regmap_base, SFC_CMD, cmd);
	if (ret)
		goto out_map_data;

	if (infobuf) {
		sfc->iaddr = dma_map_single(sfc->dev, infobuf, infolen, dir);
		ret = dma_mapping_error(sfc->dev, sfc->iaddr);
		if (ret) {
			dev_err(sfc->dev, "DMA mapping error\n");
			dma_unmap_single(sfc->dev, sfc->daddr, datalen, dir);
			goto out_map_data;
		}

		sfc->info_bytes = infolen;
		cmd = CMD_INFO_ADDRL(sfc->iaddr);
		ret = regmap_write(sfc->regmap_base, SFC_CMD, cmd);
		if (ret)
			goto out_map_info;

		cmd = CMD_INFO_ADDRH(sfc->iaddr);
		ret = regmap_write(sfc->regmap_base, SFC_CMD, cmd);
		if (ret)
			goto out_map_info;
	}

	return 0;

out_map_info:
	dma_unmap_single(sfc->dev, sfc->iaddr, datalen, dir);
out_map_data:
	dma_unmap_single(sfc->dev, sfc->daddr, datalen, dir);

	return ret;
}

static void aml_sfc_dma_buffer_release(struct aml_sfc *sfc,
				       int datalen, int infolen,
				       enum dma_data_direction dir)
{
	dma_unmap_single(sfc->dev, sfc->daddr, datalen, dir);
	if (infolen) {
		dma_unmap_single(sfc->dev, sfc->iaddr, infolen, dir);
		sfc->info_bytes = 0;
	}
}

static bool aml_sfc_dma_buffer_is_safe(const void *buffer)
{
	if ((uintptr_t)buffer % DMA_ADDR_ALIGN)
		return false;

	if (virt_addr_valid(buffer))
		return true;

	return false;
}

static void *aml_get_dma_safe_input_buf(const struct spi_mem_op *op)
{
	if (aml_sfc_dma_buffer_is_safe(op->data.buf.in))
		return op->data.buf.in;

	return kzalloc(op->data.nbytes, GFP_KERNEL);
}

static void aml_sfc_put_dma_safe_input_buf(const struct spi_mem_op *op, void *buf)
{
	if (WARN_ON(op->data.dir != SPI_MEM_DATA_IN) || WARN_ON(!buf))
		return;

	if (buf == op->data.buf.in)
		return;

	memcpy(op->data.buf.in, buf, op->data.nbytes);
	kfree(buf);
}

static void *aml_sfc_get_dma_safe_output_buf(const struct spi_mem_op *op)
{
	if (aml_sfc_dma_buffer_is_safe(op->data.buf.out))
		return (void *)op->data.buf.out;

	return kmemdup(op->data.buf.out, op->data.nbytes, GFP_KERNEL);
}

static void aml_sfc_put_dma_safe_output_buf(const struct spi_mem_op *op, const void *buf)
{
	if (WARN_ON(op->data.dir != SPI_MEM_DATA_OUT) || WARN_ON(!buf))
		return;

	if (buf != op->data.buf.out)
		kfree(buf);
}

static u64 aml_sfc_cal_timeout_cycle(struct aml_sfc *sfc, const struct spi_mem_op *op)
{
	u64 ms;

	/* For each byte we wait for (8 cycles / buswidth) of the SPI clock. */
	ms = 8 * MSEC_PER_SEC * op->data.nbytes / op->data.buswidth;
	do_div(ms, sfc->bus_rate / DEFAULT_BUS_CYCLE);

	/*
	 * Double the value and add a 200 ms tolerance to compensate for
	 * the impact of specific CS hold time, CS setup time sequences,
	 * controller burst gaps, and other related timing variations.
	 */
	ms += ms + 200;

	if (ms > UINT_MAX)
		ms = UINT_MAX;

	return ms;
}

static void aml_sfc_check_ecc_pages_valid(struct aml_sfc *sfc, bool raw)
{
	struct aml_sfc_ecc_cfg *ecc_cfg;
	__le64 *info;
	int ret;

	info = sfc->info_buf;
	ecc_cfg = aml_sfc_to_ecc_ctx(sfc);
	info += raw ? 0 : ecc_cfg->nsteps - 1;

	do {
		usleep_range(10, 15);
		/* info is updated by nfc dma engine*/
		smp_rmb();
		dma_sync_single_for_cpu(sfc->dev, sfc->iaddr, sfc->info_bytes,
					DMA_FROM_DEVICE);
		ret = le64_to_cpu(*info) & ECC_COMPLETE;
	} while (!ret);
}

static int aml_sfc_raw_io_op(struct aml_sfc *sfc, const struct spi_mem_op *op)
{
	void *buf = NULL;
	int ret;
	bool is_datain = false;
	u32 cmd = 0, conf;
	u64 timeout_ms;

	if (!op->data.nbytes)
		goto end_xfer;

	conf = (op->data.nbytes >> RAW_SIZE_BW) << __ffs(RAW_EXT_SIZE);
	ret = regmap_update_bits(sfc->regmap_base, SFC_SPI_CFG, RAW_EXT_SIZE, conf);
	if (ret)
		goto err_out;

	if (op->data.dir == SPI_MEM_DATA_IN) {
		is_datain = true;

		buf = aml_get_dma_safe_input_buf(op);
		if (!buf) {
			ret = -ENOMEM;
			goto err_out;
		}

		cmd |= CMD_NAND2MEM(0, (op->data.nbytes & RAW_SIZE));
	} else if (op->data.dir == SPI_MEM_DATA_OUT) {
		is_datain = false;

		buf = aml_sfc_get_dma_safe_output_buf(op);
		if (!buf) {
			ret = -ENOMEM;
			goto err_out;
		}

		cmd |= CMD_MEM2NAND(0, (op->data.nbytes & RAW_SIZE));
	} else {
		goto end_xfer;
	}

	ret = aml_sfc_dma_buffer_setup(sfc, buf, op->data.nbytes,
				       is_datain ? sfc->info_buf : NULL,
				       is_datain ? ECC_PER_INFO_BYTE : 0,
				       is_datain ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
	if (ret)
		goto err_out;

	ret = regmap_write(sfc->regmap_base, SFC_CMD, cmd);
	if (ret)
		goto err_out;

	timeout_ms = aml_sfc_cal_timeout_cycle(sfc, op);
	ret = aml_sfc_wait_cmd_finish(sfc, timeout_ms);
	if (ret)
		goto err_out;

	if (is_datain)
		aml_sfc_check_ecc_pages_valid(sfc, 1);

	if (op->data.dir == SPI_MEM_DATA_IN)
		aml_sfc_put_dma_safe_input_buf(op, buf);
	else if (op->data.dir == SPI_MEM_DATA_OUT)
		aml_sfc_put_dma_safe_output_buf(op, buf);

	aml_sfc_dma_buffer_release(sfc, op->data.nbytes,
				   is_datain ? ECC_PER_INFO_BYTE : 0,
				   is_datain ? DMA_FROM_DEVICE : DMA_TO_DEVICE);

end_xfer:
	return aml_sfc_end_transfer(sfc, CS_HOLD_CYCLE);

err_out:
	return ret;
}

static void aml_sfc_set_user_byte(struct aml_sfc *sfc, __le64 *info_buf, u8 *oob_buf, bool auto_oob)
{
	struct aml_sfc_ecc_cfg *ecc_cfg;
	__le64 *info;
	int i, count, step_size;

	ecc_cfg = aml_sfc_to_ecc_ctx(sfc);

	step_size = auto_oob ? ECC_BCH8_INFO_BYTES : ECC_BCH8_USER_BYTES;

	for (i = 0, count = 0; i < ecc_cfg->nsteps; i++, count += step_size) {
		info = &info_buf[i];
		*info &= cpu_to_le64(~0xffff);
		*info |= cpu_to_le64((oob_buf[count + 1] << 8) + oob_buf[count]);
	}
}

static void aml_sfc_get_user_byte(struct aml_sfc *sfc, __le64 *info_buf, u8 *oob_buf)
{
	struct aml_sfc_ecc_cfg *ecc_cfg;
	__le64 *info;
	int i, count;

	ecc_cfg = aml_sfc_to_ecc_ctx(sfc);

	for (i = 0, count = 0; i < ecc_cfg->nsteps; i++, count += ECC_BCH8_INFO_BYTES) {
		info = &info_buf[i];
		oob_buf[count] = le64_to_cpu(*info);
		oob_buf[count + 1] = le64_to_cpu(*info) >> 8;
	}
}

static int aml_sfc_check_hwecc_status(struct aml_sfc *sfc, __le64 *info_buf)
{
	struct aml_sfc_ecc_cfg *ecc_cfg;
	__le64 *info;
	u32 i, max_bitflips = 0, per_sector_bitflips = 0;

	ecc_cfg = aml_sfc_to_ecc_ctx(sfc);

	sfc->ecc_stats.failed = 0;
	sfc->ecc_stats.bitflips = 0;
	sfc->ecc_stats.corrected = 0;

	for (i = 0, info = info_buf; i < ecc_cfg->nsteps; i++, info++) {
		if (ECC_ERR_CNT(le64_to_cpu(*info)) != ECC_UNCORRECTABLE) {
			per_sector_bitflips = ECC_ERR_CNT(le64_to_cpu(*info));
			max_bitflips = max_t(u32, max_bitflips, per_sector_bitflips);
			sfc->ecc_stats.corrected += per_sector_bitflips;
			continue;
		}

		return -EBADMSG;
	}

	return max_bitflips;
}

static int aml_sfc_read_page_hwecc(struct aml_sfc *sfc, const struct spi_mem_op *op)
{
	struct aml_sfc_ecc_cfg *ecc_cfg;
	int ret, data_len, info_len;
	u32 page_size, cmd = 0;
	u64 timeout_ms;

	ecc_cfg = aml_sfc_to_ecc_ctx(sfc);

	page_size = ecc_cfg->stepsize * ecc_cfg->nsteps;
	data_len = page_size + ecc_cfg->oobsize;
	info_len = ecc_cfg->nsteps * ECC_PER_INFO_BYTE;

	ret = aml_sfc_dma_buffer_setup(sfc, sfc->data_buf, data_len,
				       sfc->info_buf, info_len, DMA_FROM_DEVICE);
	if (ret)
		goto err_out;

	cmd |= CMD_NAND2MEM(ecc_cfg->bch, ecc_cfg->nsteps);
	ret = regmap_write(sfc->regmap_base, SFC_CMD, cmd);
	if (ret)
		goto err_out;

	timeout_ms = aml_sfc_cal_timeout_cycle(sfc, op);
	ret = aml_sfc_wait_cmd_finish(sfc, timeout_ms);
	if (ret)
		goto err_out;

	aml_sfc_check_ecc_pages_valid(sfc, 0);
	aml_sfc_dma_buffer_release(sfc, data_len, info_len, DMA_FROM_DEVICE);

	/* check ecc status here */
	ret = aml_sfc_check_hwecc_status(sfc, sfc->info_buf);
	if (ret < 0)
		sfc->ecc_stats.failed++;
	else
		sfc->ecc_stats.bitflips = ret;

	if (sfc->flags & SFC_DATA_ONLY) {
		memcpy(op->data.buf.in, sfc->data_buf, page_size);
	} else if (sfc->flags & SFC_OOB_ONLY) {
		aml_sfc_get_user_byte(sfc, sfc->info_buf, op->data.buf.in);
	} else if (sfc->flags & SFC_DATA_OOB) {
		memcpy(op->data.buf.in, sfc->data_buf, page_size);
		aml_sfc_get_user_byte(sfc, sfc->info_buf, op->data.buf.in + page_size);
	}

	return aml_sfc_end_transfer(sfc, CS_HOLD_CYCLE);

err_out:
	return ret;
}

static int aml_sfc_write_page_hwecc(struct aml_sfc *sfc, const struct spi_mem_op *op)
{
	struct aml_sfc_ecc_cfg *ecc_cfg;
	int ret, data_len, info_len;
	u32 page_size, cmd = 0;
	u64 timeout_ms;

	ecc_cfg = aml_sfc_to_ecc_ctx(sfc);

	page_size = ecc_cfg->stepsize * ecc_cfg->nsteps;
	data_len = page_size + ecc_cfg->oobsize;
	info_len = ecc_cfg->nsteps * ECC_PER_INFO_BYTE;

	memset(sfc->info_buf, ECC_PATTERN, ecc_cfg->oobsize);
	memcpy(sfc->data_buf, op->data.buf.out, page_size);

	if (!(sfc->flags & SFC_DATA_ONLY)) {
		if (sfc->flags & SFC_AUTO_OOB)
			aml_sfc_set_user_byte(sfc, sfc->info_buf,
					      (u8 *)op->data.buf.out + page_size, 1);
		else
			aml_sfc_set_user_byte(sfc, sfc->info_buf,
					      (u8 *)op->data.buf.out + page_size, 0);
	}

	ret = aml_sfc_dma_buffer_setup(sfc, sfc->data_buf, data_len,
				       sfc->info_buf, info_len, DMA_TO_DEVICE);
	if (ret)
		goto err_out;

	cmd |= CMD_MEM2NAND(ecc_cfg->bch, ecc_cfg->nsteps);
	ret = regmap_write(sfc->regmap_base, SFC_CMD, cmd);
	if (ret)
		goto err_out;

	timeout_ms = aml_sfc_cal_timeout_cycle(sfc, op);

	ret = aml_sfc_wait_cmd_finish(sfc, timeout_ms);
	if (ret)
		goto err_out;

	aml_sfc_dma_buffer_release(sfc, data_len, info_len, DMA_TO_DEVICE);

	return  aml_sfc_end_transfer(sfc, CS_HOLD_CYCLE);

err_out:
	return ret;
}

static int aml_sfc_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct aml_sfc *sfc;
	struct spi_device *spi;
	struct aml_sfc_ecc_cfg *ecc_cfg;
	int ret;

	sfc = spi_controller_get_devdata(mem->spi->controller);
	ecc_cfg = aml_sfc_to_ecc_ctx(sfc);
	spi = mem->spi;
	sfc->cs_sel = spi->chip_select[0] ? CS_1 : CS_0;

	dev_dbg(sfc->dev, "cmd:0x%02x - addr:%08llX@%d:%u - dummy:%d:%u - data:%d:%u",
		op->cmd.opcode, op->addr.val, op->addr.buswidth, op->addr.nbytes,
		op->dummy.buswidth, op->dummy.nbytes, op->data.buswidth, op->data.nbytes);

	ret = aml_sfc_pre_transfer(sfc, DEFAULT_PULLUP_CYCLE, CS_SETUP_CYCLE);
	if (ret)
		return ret;

	ret = aml_sfc_send_cmd_addr_dummy(sfc, op);
	if (ret)
		return ret;

	ret = aml_sfc_set_bus_width(sfc, op->data.buswidth, DATA_LANE);
	if (ret)
		return ret;

	if (aml_sfc_is_snand_hwecc_page_op(sfc, op) &&
	    ecc_cfg && !(sfc->flags & SFC_RAW_RW)) {
		if (op->data.dir == SPI_MEM_DATA_IN)
			return aml_sfc_read_page_hwecc(sfc, op);
		else
			return aml_sfc_write_page_hwecc(sfc, op);
	}

	return aml_sfc_raw_io_op(sfc, op);
}

static int aml_sfc_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	struct aml_sfc *sfc;
	struct aml_sfc_ecc_cfg *ecc_cfg;

	sfc = spi_controller_get_devdata(mem->spi->controller);
	ecc_cfg = aml_sfc_to_ecc_ctx(sfc);

	if (aml_sfc_is_snand_hwecc_page_op(sfc, op) && ecc_cfg) {
		if (op->data.nbytes > ecc_cfg->stepsize * ECC_BCH_MAX_SECT_SIZE)
			return -EOPNOTSUPP;
	} else if (op->data.nbytes & ~RAW_MAX_RW_SIZE_MASK) {
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct spi_controller_mem_ops aml_sfc_mem_ops = {
	.adjust_op_size = aml_sfc_adjust_op_size,
	.exec_op = aml_sfc_exec_op,
};

static int aml_sfc_layout_ecc(struct mtd_info *mtd, int section,
			      struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);

	if (section >= nand->ecc.ctx.nsteps)
		return -ERANGE;

	oobregion->offset =  ECC_BCH8_USER_BYTES + (section * ECC_BCH8_INFO_BYTES);
	oobregion->length = ECC_BCH8_PARITY_BYTES;

	return 0;
}

static int aml_sfc_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);

	if (section >= nand->ecc.ctx.nsteps)
		return -ERANGE;

	oobregion->offset = section * ECC_BCH8_INFO_BYTES;
	oobregion->length = ECC_BCH8_USER_BYTES;

	return 0;
}

static const struct mtd_ooblayout_ops aml_sfc_ooblayout_ops = {
	.ecc = aml_sfc_layout_ecc,
	.free = aml_sfc_ooblayout_free,
};

static int aml_spi_settings(struct aml_sfc *sfc, struct spi_device *spi)
{
	u32 conf = 0;

	if (spi->mode & SPI_CPHA)
		conf |= CPHA;

	if (spi->mode & SPI_CPOL)
		conf |= CPOL;

	conf |= FIELD_PREP(RXADJ, sfc->rx_adj);
	conf |= EN_HOLD | EN_WP;
	return regmap_update_bits(sfc->regmap_base, SFC_SPI_CFG,
					CPHA | CPOL | RXADJ |
					EN_HOLD | EN_WP, conf);
}

static int aml_set_spi_clk(struct aml_sfc *sfc, struct spi_device *spi)
{
	u32 speed_hz;
	int ret;

	if (spi->max_speed_hz > SFC_MAX_FREQUENCY)
		speed_hz = SFC_MAX_FREQUENCY;
	else if (!spi->max_speed_hz)
		speed_hz = SFC_BUS_DEFAULT_CLK;
	else if (spi->max_speed_hz < SFC_MIN_FREQUENCY)
		speed_hz = SFC_MIN_FREQUENCY;
	else
		speed_hz = spi->max_speed_hz;

	/* The SPI clock is generated by dividing the bus clock by four by default. */
	ret = regmap_write(sfc->regmap_base, SFC_CFG, (DEFAULT_BUS_CYCLE - 1));
	if (ret) {
		dev_err(sfc->dev, "failed to set bus cycle\n");
		return ret;
	}

	return clk_set_rate(sfc->core_clk, speed_hz * DEFAULT_BUS_CYCLE);
}

static int aml_sfc_setup(struct spi_device *spi)
{
	struct aml_sfc *sfc;
	int ret;

	sfc = spi_controller_get_devdata(spi->controller);
	ret = aml_spi_settings(sfc, spi);
	if (ret)
		return ret;

	ret = aml_set_spi_clk(sfc, spi);
	if (ret)
		return ret;

	sfc->bus_rate = clk_get_rate(sfc->core_clk);

	return 0;
}

static int aml_sfc_ecc_init_ctx(struct nand_device *nand)
{
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	struct aml_sfc *sfc = nand_to_aml_sfc(nand);
	struct aml_sfc_ecc_cfg *ecc_cfg;
	const struct aml_sfc_caps *caps = sfc->caps;
	struct aml_sfc_ecc_cfg *ecc_caps = caps->ecc_caps;
	int i, ecc_strength, ecc_step_size;

	ecc_step_size = nand->ecc.user_conf.step_size;
	ecc_strength = nand->ecc.user_conf.strength;

	for (i = 0; i < caps->num_ecc_caps; i++) {
		if (ecc_caps[i].stepsize == ecc_step_size) {
			nand->ecc.ctx.conf.step_size = ecc_step_size;
			nand->ecc.ctx.conf.flags |= BIT(ecc_caps[i].bch);
		}

		if (ecc_caps[i].strength == ecc_strength)
			nand->ecc.ctx.conf.strength = ecc_strength;
	}

	if (!nand->ecc.ctx.conf.step_size) {
		nand->ecc.ctx.conf.step_size = ECC_BCH8_DEFAULT_STEP;
		nand->ecc.ctx.conf.flags |= BIT(ECC_DEFAULT_BCH_MODE);
	}

	if (!nand->ecc.ctx.conf.strength)
		nand->ecc.ctx.conf.strength = ECC_BCH8_STRENGTH;

	nand->ecc.ctx.nsteps = nand->memorg.pagesize / nand->ecc.ctx.conf.step_size;
	nand->ecc.ctx.total = nand->ecc.ctx.nsteps * ECC_BCH8_PARITY_BYTES;

	/* Verify the page size and OOB size against the SFC requirements. */
	if ((nand->memorg.pagesize % nand->ecc.ctx.conf.step_size) ||
	    (nand->memorg.oobsize < (nand->ecc.ctx.total +
	     nand->ecc.ctx.nsteps * ECC_BCH8_USER_BYTES)))
		return -EOPNOTSUPP;

	nand->ecc.ctx.conf.engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;

	ecc_cfg = kzalloc(sizeof(*ecc_cfg), GFP_KERNEL);
	if (!ecc_cfg)
		return -ENOMEM;

	ecc_cfg->stepsize = nand->ecc.ctx.conf.step_size;
	ecc_cfg->nsteps = nand->ecc.ctx.nsteps;
	ecc_cfg->strength = nand->ecc.ctx.conf.strength;
	ecc_cfg->oobsize = nand->memorg.oobsize;
	ecc_cfg->bch = nand->ecc.ctx.conf.flags & BIT(ECC_DEFAULT_BCH_MODE) ? 1 : 2;

	nand->ecc.ctx.priv = ecc_cfg;
	sfc->priv = (void *)ecc_cfg;
	mtd_set_ooblayout(mtd, &aml_sfc_ooblayout_ops);

	sfc->flags |= SFC_HWECC;

	return 0;
}

static void aml_sfc_ecc_cleanup_ctx(struct nand_device *nand)
{
	struct aml_sfc *sfc = nand_to_aml_sfc(nand);

	sfc->flags &= ~(SFC_HWECC);
	kfree(nand->ecc.ctx.priv);
	sfc->priv = NULL;
}

static int aml_sfc_ecc_prepare_io_req(struct nand_device *nand,
				      struct nand_page_io_req *req)
{
	struct aml_sfc *sfc = nand_to_aml_sfc(nand);
	struct spinand_device *spinand = nand_to_spinand(nand);

	sfc->flags &= ~SFC_XFER_MDOE_MASK;

	if (req->datalen && !req->ooblen)
		sfc->flags |= SFC_DATA_ONLY;
	else if (!req->datalen && req->ooblen)
		sfc->flags |= SFC_OOB_ONLY;
	else if (req->datalen && req->ooblen)
		sfc->flags |= SFC_DATA_OOB;

	if (req->mode == MTD_OPS_RAW)
		sfc->flags |= SFC_RAW_RW;
	else if (req->mode == MTD_OPS_AUTO_OOB)
		sfc->flags |= SFC_AUTO_OOB;

	memset(spinand->oobbuf, 0xff, nanddev_per_page_oobsize(nand));

	return 0;
}

static int aml_sfc_ecc_finish_io_req(struct nand_device *nand,
				     struct nand_page_io_req *req)
{
	struct aml_sfc *sfc = nand_to_aml_sfc(nand);
	struct mtd_info *mtd = nanddev_to_mtd(nand);

	if (req->mode == MTD_OPS_RAW || req->type == NAND_PAGE_WRITE)
		return 0;

	if (sfc->ecc_stats.failed)
		mtd->ecc_stats.failed++;

	mtd->ecc_stats.corrected += sfc->ecc_stats.corrected;

	return sfc->ecc_stats.failed ? -EBADMSG : sfc->ecc_stats.bitflips;
}

static const struct spi_controller_mem_caps aml_sfc_mem_caps = {
	.ecc = true,
};

static const struct nand_ecc_engine_ops aml_sfc_ecc_engine_ops = {
	.init_ctx = aml_sfc_ecc_init_ctx,
	.cleanup_ctx = aml_sfc_ecc_cleanup_ctx,
	.prepare_io_req = aml_sfc_ecc_prepare_io_req,
	.finish_io_req = aml_sfc_ecc_finish_io_req,
};

static int aml_sfc_clk_init(struct aml_sfc *sfc)
{
	sfc->gate_clk = devm_clk_get_enabled(sfc->dev, "gate");
	if (IS_ERR(sfc->gate_clk)) {
		dev_err(sfc->dev, "unable to enable gate clk\n");
		return PTR_ERR(sfc->gate_clk);
	}

	sfc->core_clk = devm_clk_get_enabled(sfc->dev, "core");
	if (IS_ERR(sfc->core_clk)) {
		dev_err(sfc->dev, "unable to enable core clk\n");
		return PTR_ERR(sfc->core_clk);
	}

	return clk_set_rate(sfc->core_clk, SFC_BUS_DEFAULT_CLK);
}

static int aml_sfc_disable_clk(struct aml_sfc *sfc)
{
	clk_disable_unprepare(sfc->core_clk);
	clk_disable_unprepare(sfc->gate_clk);

	return 0;
}

static int aml_sfc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct spi_controller *ctrl;
	struct aml_sfc *sfc;
	void __iomem *reg_base;
	int ret;
	u32 val = 0;

	const struct regmap_config core_config = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = SFC_SPI_CFG,
	};

	ctrl = devm_spi_alloc_host(dev, sizeof(*sfc));
	if (!ctrl)
		return -ENOMEM;
	platform_set_drvdata(pdev, ctrl);

	sfc = spi_controller_get_devdata(ctrl);
	sfc->dev = dev;
	sfc->ctrl = ctrl;

	sfc->caps = of_device_get_match_data(dev);
	if (!sfc->caps)
		return dev_err_probe(dev, -ENODEV, "failed to get device data\n");

	reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg_base))
		return PTR_ERR(reg_base);

	sfc->regmap_base = devm_regmap_init_mmio(dev, reg_base, &core_config);
	if (IS_ERR(sfc->regmap_base))
		return dev_err_probe(dev, PTR_ERR(sfc->regmap_base),
			"failed to init sfc base regmap\n");

	sfc->data_buf = devm_kzalloc(dev, SFC_BUF_SIZE, GFP_KERNEL);
	if (!sfc->data_buf)
		return -ENOMEM;
	sfc->info_buf = (__le64 *)(sfc->data_buf + SFC_DATABUF_SIZE);

	ret = aml_sfc_clk_init(sfc);
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialize SFC clock\n");

	/* Enable Amlogic flash controller spi mode */
	ret = regmap_write(sfc->regmap_base, SFC_SPI_CFG, SPI_MODE_EN);
	if (ret) {
		dev_err(dev, "failed to enable SPI mode\n");
		goto err_out;
	}

	ret = dma_set_mask(sfc->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(sfc->dev, "failed to set dma mask\n");
		goto err_out;
	}

	sfc->ecc_eng.dev = &pdev->dev;
	sfc->ecc_eng.integration = NAND_ECC_ENGINE_INTEGRATION_PIPELINED;
	sfc->ecc_eng.ops = &aml_sfc_ecc_engine_ops;
	sfc->ecc_eng.priv = sfc;

	ret = nand_ecc_register_on_host_hw_engine(&sfc->ecc_eng);
	if (ret) {
		dev_err(&pdev->dev, "failed to register Aml host ecc engine.\n");
		goto err_out;
	}

	ret = of_property_read_u32(np, "amlogic,rx-adj", &val);
	if (!ret)
		sfc->rx_adj = val;

	ctrl->dev.of_node = np;
	ctrl->mem_ops = &aml_sfc_mem_ops;
	ctrl->mem_caps = &aml_sfc_mem_caps;
	ctrl->setup = aml_sfc_setup;
	ctrl->mode_bits = SPI_TX_QUAD | SPI_TX_DUAL | SPI_RX_QUAD |
			  SPI_RX_DUAL | SPI_TX_OCTAL | SPI_RX_OCTAL;
	ctrl->max_speed_hz = SFC_MAX_FREQUENCY;
	ctrl->min_speed_hz = SFC_MIN_FREQUENCY;
	ctrl->num_chipselect = SFC_MAX_CS_NUM;

	ret = devm_spi_register_controller(dev, ctrl);
	if (ret)
		goto err_out;

	return 0;

err_out:
	aml_sfc_disable_clk(sfc);

	return ret;
}

static void aml_sfc_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr = platform_get_drvdata(pdev);
	struct aml_sfc *sfc = spi_controller_get_devdata(ctlr);

	aml_sfc_disable_clk(sfc);
}

static const struct of_device_id aml_sfc_of_match[] = {
	{
		.compatible = "amlogic,a4-spifc",
		.data = &aml_a113l2_sfc_caps
	},
	{},
};
MODULE_DEVICE_TABLE(of, aml_sfc_of_match);

static struct platform_driver aml_sfc_driver = {
	.driver = {
		.name = "aml_sfc",
		.of_match_table = aml_sfc_of_match,
	},
	.probe = aml_sfc_probe,
	.remove = aml_sfc_remove,
};
module_platform_driver(aml_sfc_driver);

MODULE_DESCRIPTION("Amlogic SPI Flash Controller driver");
MODULE_AUTHOR("Feng Chen <feng.chen@amlogic.com>");
MODULE_LICENSE("Dual MIT/GPL");
