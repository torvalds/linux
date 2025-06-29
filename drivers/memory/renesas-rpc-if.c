// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RPC-IF core driver
 *
 * Copyright (C) 2018-2019 Renesas Solutions Corp.
 * Copyright (C) 2019 Macronix International Co., Ltd.
 * Copyright (C) 2019-2020 Cogent Embedded, Inc.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <memory/renesas-rpc-if.h>

#include "renesas-rpc-if-regs.h"
#include "renesas-xspi-if-regs.h"

static const struct regmap_range rpcif_volatile_ranges[] = {
	regmap_reg_range(RPCIF_SMRDR0, RPCIF_SMRDR1),
	regmap_reg_range(RPCIF_SMWDR0, RPCIF_SMWDR1),
	regmap_reg_range(RPCIF_CMNSR, RPCIF_CMNSR),
};

static const struct regmap_access_table rpcif_volatile_table = {
	.yes_ranges	= rpcif_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(rpcif_volatile_ranges),
};

static const struct regmap_range xspi_volatile_ranges[] = {
	regmap_reg_range(XSPI_CDD0BUF0, XSPI_CDD0BUF0),
};

static const struct regmap_access_table xspi_volatile_table = {
	.yes_ranges	= xspi_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(xspi_volatile_ranges),
};

struct rpcif_priv;

struct rpcif_impl {
	int (*hw_init)(struct rpcif_priv *rpc, bool hyperflash);
	void (*prepare)(struct rpcif_priv *rpc, const struct rpcif_op *op,
			u64 *offs, size_t *len);
	int (*manual_xfer)(struct rpcif_priv *rpc);
	size_t (*dirmap_read)(struct rpcif_priv *rpc, u64 offs, size_t len,
			      void *buf);
	u32 status_reg;
	u32 status_mask;
};

struct rpcif_info {
	const struct regmap_config *regmap_config;
	const struct rpcif_impl *impl;
	enum rpcif_type type;
	u8 strtim;
};

struct rpcif_priv {
	struct device *dev;
	void __iomem *base;
	void __iomem *dirmap;
	struct regmap *regmap;
	struct reset_control *rstc;
	struct platform_device *vdev;
	size_t size;
	const struct rpcif_info *info;
	enum rpcif_data_dir dir;
	u8 bus_size;
	u8 xfer_size;
	u8 addr_nbytes;		/* Specified for xSPI */
	u32 proto;		/* Specified for xSPI */
	void *buffer;
	u32 xferlen;
	u32 smcr;
	u32 smadr;
	u32 command;		/* DRCMR or SMCMR */
	u32 option;		/* DROPR or SMOPR */
	u32 enable;		/* DRENR or SMENR */
	u32 dummy;		/* DRDMCR or SMDMCR */
	u32 ddr;		/* DRDRENR or SMDRENR */
};

/*
 * Custom accessor functions to ensure SM[RW]DR[01] are always accessed with
 * proper width.  Requires rpcif_priv.xfer_size to be correctly set before!
 */
static int rpcif_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct rpcif_priv *rpc = context;

	switch (reg) {
	case RPCIF_SMRDR0:
	case RPCIF_SMWDR0:
		switch (rpc->xfer_size) {
		case 1:
			*val = readb(rpc->base + reg);
			return 0;

		case 2:
			*val = readw(rpc->base + reg);
			return 0;

		case 4:
		case 8:
			*val = readl(rpc->base + reg);
			return 0;

		default:
			return -EILSEQ;
		}

	case RPCIF_SMRDR1:
	case RPCIF_SMWDR1:
		if (rpc->xfer_size != 8)
			return -EILSEQ;
		break;
	}

	*val = readl(rpc->base + reg);
	return 0;
}

static int rpcif_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct rpcif_priv *rpc = context;

	switch (reg) {
	case RPCIF_SMWDR0:
		switch (rpc->xfer_size) {
		case 1:
			writeb(val, rpc->base + reg);
			return 0;

		case 2:
			writew(val, rpc->base + reg);
			return 0;

		case 4:
		case 8:
			writel(val, rpc->base + reg);
			return 0;

		default:
			return -EILSEQ;
		}

	case RPCIF_SMWDR1:
		if (rpc->xfer_size != 8)
			return -EILSEQ;
		break;

	case RPCIF_SMRDR0:
	case RPCIF_SMRDR1:
		return -EPERM;
	}

	writel(val, rpc->base + reg);
	return 0;
}

static const struct regmap_config rpcif_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.reg_read	= rpcif_reg_read,
	.reg_write	= rpcif_reg_write,
	.fast_io	= true,
	.max_register	= RPCIF_PHYINT,
	.volatile_table	= &rpcif_volatile_table,
};

static int xspi_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct rpcif_priv *xspi = context;

	*val = readl(xspi->base + reg);
	return 0;
}

static int xspi_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct rpcif_priv *xspi = context;

	writel(val, xspi->base + reg);
	return 0;
}

static const struct regmap_config xspi_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.reg_read	= xspi_reg_read,
	.reg_write	= xspi_reg_write,
	.fast_io	= true,
	.max_register	= XSPI_INTE,
	.volatile_table	= &xspi_volatile_table,
};

int rpcif_sw_init(struct rpcif *rpcif, struct device *dev)
{
	struct rpcif_priv *rpc = dev_get_drvdata(dev);

	rpcif->dev = dev;
	rpcif->dirmap = rpc->dirmap;
	rpcif->size = rpc->size;
	rpcif->xspi = rpc->info->type == XSPI_RZ_G3E;
	return 0;
}
EXPORT_SYMBOL(rpcif_sw_init);

static void rpcif_rzg2l_timing_adjust_sdr(struct rpcif_priv *rpc)
{
	regmap_write(rpc->regmap, RPCIF_PHYWR, 0xa5390000);
	regmap_write(rpc->regmap, RPCIF_PHYADD, 0x80000000);
	regmap_write(rpc->regmap, RPCIF_PHYWR, 0x00008080);
	regmap_write(rpc->regmap, RPCIF_PHYADD, 0x80000022);
	regmap_write(rpc->regmap, RPCIF_PHYWR, 0x00008080);
	regmap_write(rpc->regmap, RPCIF_PHYADD, 0x80000024);
	regmap_update_bits(rpc->regmap, RPCIF_PHYCNT, RPCIF_PHYCNT_CKSEL(3),
			   RPCIF_PHYCNT_CKSEL(3));
	regmap_write(rpc->regmap, RPCIF_PHYWR, 0x00000030);
	regmap_write(rpc->regmap, RPCIF_PHYADD, 0x80000032);
}

static int rpcif_hw_init_impl(struct rpcif_priv *rpc, bool hyperflash)
{
	u32 dummy;
	int ret;

	if (rpc->info->type == RPCIF_RZ_G2L) {
		ret = reset_control_reset(rpc->rstc);
		if (ret)
			return ret;
		usleep_range(200, 300);
		rpcif_rzg2l_timing_adjust_sdr(rpc);
	}

	regmap_update_bits(rpc->regmap, RPCIF_PHYCNT, RPCIF_PHYCNT_PHYMEM_MASK,
			   RPCIF_PHYCNT_PHYMEM(hyperflash ? 3 : 0));

	/* DMA Transfer is not supported */
	regmap_update_bits(rpc->regmap, RPCIF_PHYCNT, RPCIF_PHYCNT_HS, 0);

	regmap_update_bits(rpc->regmap, RPCIF_PHYCNT,
			   /* create mask with all affected bits set */
			   RPCIF_PHYCNT_STRTIM(BIT(fls(rpc->info->strtim)) - 1),
			   RPCIF_PHYCNT_STRTIM(rpc->info->strtim));

	regmap_update_bits(rpc->regmap, RPCIF_PHYOFFSET1, RPCIF_PHYOFFSET1_DDRTMG(3),
			   RPCIF_PHYOFFSET1_DDRTMG(3));
	regmap_update_bits(rpc->regmap, RPCIF_PHYOFFSET2, RPCIF_PHYOFFSET2_OCTTMG(7),
			   RPCIF_PHYOFFSET2_OCTTMG(4));

	if (hyperflash)
		regmap_update_bits(rpc->regmap, RPCIF_PHYINT,
				   RPCIF_PHYINT_WPVAL, 0);

	if (rpc->info->type == RPCIF_RZ_G2L)
		regmap_update_bits(rpc->regmap, RPCIF_CMNCR,
				   RPCIF_CMNCR_MOIIO(3) | RPCIF_CMNCR_IOFV(3) |
				   RPCIF_CMNCR_BSZ(3),
				   RPCIF_CMNCR_MOIIO(1) | RPCIF_CMNCR_IOFV(3) |
				   RPCIF_CMNCR_BSZ(hyperflash ? 1 : 0));
	else
		regmap_update_bits(rpc->regmap, RPCIF_CMNCR,
				   RPCIF_CMNCR_MOIIO(3) | RPCIF_CMNCR_BSZ(3),
				   RPCIF_CMNCR_MOIIO(3) |
				   RPCIF_CMNCR_BSZ(hyperflash ? 1 : 0));

	/* Set RCF after BSZ update */
	regmap_write(rpc->regmap, RPCIF_DRCR, RPCIF_DRCR_RCF);
	/* Dummy read according to spec */
	regmap_read(rpc->regmap, RPCIF_DRCR, &dummy);
	regmap_write(rpc->regmap, RPCIF_SSLDR, RPCIF_SSLDR_SPNDL(7) |
		     RPCIF_SSLDR_SLNDL(7) | RPCIF_SSLDR_SCKDL(7));

	rpc->bus_size = hyperflash ? 2 : 1;

	return 0;
}

static int xspi_hw_init_impl(struct rpcif_priv *xspi, bool hyperflash)
{
	int ret;

	ret = reset_control_reset(xspi->rstc);
	if (ret)
		return ret;

	regmap_write(xspi->regmap, XSPI_WRAPCFG, 0x0);

	regmap_update_bits(xspi->regmap, XSPI_LIOCFGCS0,
			   XSPI_LIOCFG_PRTMD(0x3ff) | XSPI_LIOCFG_CSMIN(0xf) |
			   XSPI_LIOCFG_CSASTEX | XSPI_LIOCFG_CSNEGEX,
			   XSPI_LIOCFG_PRTMD(0) | XSPI_LIOCFG_CSMIN(0) |
			   XSPI_LIOCFG_CSASTEX | XSPI_LIOCFG_CSNEGEX);

	regmap_update_bits(xspi->regmap, XSPI_CCCTL0CS0, XSPI_CCCTL0_CAEN, 0);

	regmap_update_bits(xspi->regmap, XSPI_CDCTL0,
			   XSPI_CDCTL0_TRREQ | XSPI_CDCTL0_CSSEL, 0);

	regmap_update_bits(xspi->regmap, XSPI_INTE, XSPI_INTE_CMDCMPE,
			   XSPI_INTE_CMDCMPE);

	return 0;
}

int rpcif_hw_init(struct device *dev, bool hyperflash)
{
	struct rpcif_priv *rpc = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	ret = rpc->info->impl->hw_init(rpc, hyperflash);

	pm_runtime_put(dev);

	return ret;
}
EXPORT_SYMBOL(rpcif_hw_init);

static int wait_msg_xfer_end(struct rpcif_priv *rpc)
{
	u32 sts;

	return regmap_read_poll_timeout(rpc->regmap, rpc->info->impl->status_reg,
					sts, sts & rpc->info->impl->status_mask,
					0, USEC_PER_SEC);
}

static u8 rpcif_bits_set(struct rpcif_priv *rpc, u32 nbytes)
{
	if (rpc->bus_size == 2)
		nbytes /= 2;
	nbytes = clamp(nbytes, 1U, 4U);
	return GENMASK(3, 4 - nbytes);
}

static u8 rpcif_bit_size(u8 buswidth)
{
	return buswidth > 4 ? 2 : ilog2(buswidth);
}

static void rpcif_prepare_impl(struct rpcif_priv *rpc, const struct rpcif_op *op,
			       u64 *offs, size_t *len)
{
	rpc->smcr = 0;
	rpc->smadr = 0;
	rpc->enable = 0;
	rpc->command = 0;
	rpc->option = 0;
	rpc->dummy = 0;
	rpc->ddr = 0;
	rpc->xferlen = 0;

	if (op->cmd.buswidth) {
		rpc->enable  = RPCIF_SMENR_CDE |
			RPCIF_SMENR_CDB(rpcif_bit_size(op->cmd.buswidth));
		rpc->command = RPCIF_SMCMR_CMD(op->cmd.opcode);
		if (op->cmd.ddr)
			rpc->ddr = RPCIF_SMDRENR_HYPE(0x5);
	}
	if (op->ocmd.buswidth) {
		rpc->enable  |= RPCIF_SMENR_OCDE |
			RPCIF_SMENR_OCDB(rpcif_bit_size(op->ocmd.buswidth));
		rpc->command |= RPCIF_SMCMR_OCMD(op->ocmd.opcode);
	}

	if (op->addr.buswidth) {
		rpc->enable |=
			RPCIF_SMENR_ADB(rpcif_bit_size(op->addr.buswidth));
		if (op->addr.nbytes == 4)
			rpc->enable |= RPCIF_SMENR_ADE(0xF);
		else
			rpc->enable |= RPCIF_SMENR_ADE(GENMASK(
						2, 3 - op->addr.nbytes));
		if (op->addr.ddr)
			rpc->ddr |= RPCIF_SMDRENR_ADDRE;

		if (offs && len)
			rpc->smadr = *offs;
		else
			rpc->smadr = op->addr.val;
	}

	if (op->dummy.buswidth) {
		rpc->enable |= RPCIF_SMENR_DME;
		rpc->dummy = RPCIF_SMDMCR_DMCYC(op->dummy.ncycles);
	}

	if (op->option.buswidth) {
		rpc->enable |= RPCIF_SMENR_OPDE(
			rpcif_bits_set(rpc, op->option.nbytes)) |
			RPCIF_SMENR_OPDB(rpcif_bit_size(op->option.buswidth));
		if (op->option.ddr)
			rpc->ddr |= RPCIF_SMDRENR_OPDRE;
		rpc->option = op->option.val;
	}

	rpc->dir = op->data.dir;
	if (op->data.buswidth) {
		u32 nbytes;

		rpc->buffer = op->data.buf.in;
		switch (op->data.dir) {
		case RPCIF_DATA_IN:
			rpc->smcr = RPCIF_SMCR_SPIRE;
			break;
		case RPCIF_DATA_OUT:
			rpc->smcr = RPCIF_SMCR_SPIWE;
			break;
		default:
			break;
		}
		if (op->data.ddr)
			rpc->ddr |= RPCIF_SMDRENR_SPIDRE;

		if (offs && len)
			nbytes = *len;
		else
			nbytes = op->data.nbytes;
		rpc->xferlen = nbytes;

		rpc->enable |= RPCIF_SMENR_SPIDB(rpcif_bit_size(op->data.buswidth));
	}
}

static void xspi_prepare_impl(struct rpcif_priv *xspi, const struct rpcif_op *op,
			      u64 *offs, size_t *len)
{
	xspi->smadr = 0;
	xspi->addr_nbytes = 0;
	xspi->command = 0;
	xspi->option = 0;
	xspi->dummy = 0;
	xspi->xferlen = 0;
	xspi->proto = 0;

	if (op->cmd.buswidth)
		xspi->command = op->cmd.opcode;

	if (op->ocmd.buswidth)
		xspi->command = (xspi->command << 8) | op->ocmd.opcode;

	if (op->addr.buswidth) {
		xspi->addr_nbytes = op->addr.nbytes;
		if (offs && len)
			xspi->smadr = *offs;
		else
			xspi->smadr = op->addr.val;
	}

	if (op->dummy.buswidth)
		xspi->dummy = op->dummy.ncycles;

	xspi->dir = op->data.dir;
	if (op->data.buswidth) {
		u32 nbytes;

		xspi->buffer = op->data.buf.in;

		if (offs && len)
			nbytes = *len;
		else
			nbytes = op->data.nbytes;
		xspi->xferlen = nbytes;
	}

	if (op->cmd.buswidth == 1) {
		if (op->addr.buswidth == 2 || op->data.buswidth == 2)
			xspi->proto = PROTO_1S_2S_2S;
		else if (op->addr.buswidth == 4 || op->data.buswidth == 4)
			xspi->proto = PROTO_1S_4S_4S;
	} else if (op->cmd.buswidth == 2 &&
		   (op->addr.buswidth == 2 || op->data.buswidth == 2)) {
		xspi->proto = PROTO_2S_2S_2S;
	} else if (op->cmd.buswidth == 4 &&
		   (op->addr.buswidth == 4 || op->data.buswidth == 4)) {
		xspi->proto = PROTO_4S_4S_4S;
	}
}

void rpcif_prepare(struct device *dev, const struct rpcif_op *op, u64 *offs,
		   size_t *len)
{
	struct rpcif_priv *rpc = dev_get_drvdata(dev);

	rpc->info->impl->prepare(rpc, op, offs, len);
}
EXPORT_SYMBOL(rpcif_prepare);

static int rpcif_manual_xfer_impl(struct rpcif_priv *rpc)
{
	u32 smenr, smcr, pos = 0, max = rpc->bus_size == 2 ? 8 : 4;
	int ret = 0;

	regmap_update_bits(rpc->regmap, RPCIF_PHYCNT,
			   RPCIF_PHYCNT_CAL, RPCIF_PHYCNT_CAL);
	regmap_update_bits(rpc->regmap, RPCIF_CMNCR,
			   RPCIF_CMNCR_MD, RPCIF_CMNCR_MD);
	regmap_write(rpc->regmap, RPCIF_SMCMR, rpc->command);
	regmap_write(rpc->regmap, RPCIF_SMOPR, rpc->option);
	regmap_write(rpc->regmap, RPCIF_SMDMCR, rpc->dummy);
	regmap_write(rpc->regmap, RPCIF_SMDRENR, rpc->ddr);
	regmap_write(rpc->regmap, RPCIF_SMADR, rpc->smadr);
	smenr = rpc->enable;

	switch (rpc->dir) {
	case RPCIF_DATA_OUT:
		while (pos < rpc->xferlen) {
			u32 bytes_left = rpc->xferlen - pos;
			u32 nbytes, data[2], *p = data;

			smcr = rpc->smcr | RPCIF_SMCR_SPIE;

			/* nbytes may only be 1, 2, 4, or 8 */
			nbytes = bytes_left >= max ? max : (1 << ilog2(bytes_left));
			if (bytes_left > nbytes)
				smcr |= RPCIF_SMCR_SSLKP;

			smenr |= RPCIF_SMENR_SPIDE(rpcif_bits_set(rpc, nbytes));
			regmap_write(rpc->regmap, RPCIF_SMENR, smenr);
			rpc->xfer_size = nbytes;

			memcpy(data, rpc->buffer + pos, nbytes);
			if (nbytes == 8)
				regmap_write(rpc->regmap, RPCIF_SMWDR1, *p++);
			regmap_write(rpc->regmap, RPCIF_SMWDR0, *p);

			regmap_write(rpc->regmap, RPCIF_SMCR, smcr);
			ret = wait_msg_xfer_end(rpc);
			if (ret)
				goto err_out;

			pos += nbytes;
			smenr = rpc->enable &
				~RPCIF_SMENR_CDE & ~RPCIF_SMENR_ADE(0xF);
		}
		break;
	case RPCIF_DATA_IN:
		/*
		 * RPC-IF spoils the data for the commands without an address
		 * phase (like RDID) in the manual mode, so we'll have to work
		 * around this issue by using the external address space read
		 * mode instead.
		 */
		if (!(smenr & RPCIF_SMENR_ADE(0xF)) && rpc->dirmap) {
			u32 dummy;

			regmap_update_bits(rpc->regmap, RPCIF_CMNCR,
					   RPCIF_CMNCR_MD, 0);
			regmap_write(rpc->regmap, RPCIF_DRCR,
				     RPCIF_DRCR_RBURST(32) | RPCIF_DRCR_RBE);
			regmap_write(rpc->regmap, RPCIF_DRCMR, rpc->command);
			regmap_write(rpc->regmap, RPCIF_DREAR,
				     RPCIF_DREAR_EAC(1));
			regmap_write(rpc->regmap, RPCIF_DROPR, rpc->option);
			regmap_write(rpc->regmap, RPCIF_DRENR,
				     smenr & ~RPCIF_SMENR_SPIDE(0xF));
			regmap_write(rpc->regmap, RPCIF_DRDMCR,  rpc->dummy);
			regmap_write(rpc->regmap, RPCIF_DRDRENR, rpc->ddr);
			memcpy_fromio(rpc->buffer, rpc->dirmap, rpc->xferlen);
			regmap_write(rpc->regmap, RPCIF_DRCR, RPCIF_DRCR_RCF);
			/* Dummy read according to spec */
			regmap_read(rpc->regmap, RPCIF_DRCR, &dummy);
			break;
		}
		while (pos < rpc->xferlen) {
			u32 bytes_left = rpc->xferlen - pos;
			u32 nbytes, data[2], *p = data;

			/* nbytes may only be 1, 2, 4, or 8 */
			nbytes = bytes_left >= max ? max : (1 << ilog2(bytes_left));

			regmap_write(rpc->regmap, RPCIF_SMADR,
				     rpc->smadr + pos);
			smenr &= ~RPCIF_SMENR_SPIDE(0xF);
			smenr |= RPCIF_SMENR_SPIDE(rpcif_bits_set(rpc, nbytes));
			regmap_write(rpc->regmap, RPCIF_SMENR, smenr);
			regmap_write(rpc->regmap, RPCIF_SMCR,
				     rpc->smcr | RPCIF_SMCR_SPIE);
			rpc->xfer_size = nbytes;
			ret = wait_msg_xfer_end(rpc);
			if (ret)
				goto err_out;

			if (nbytes == 8)
				regmap_read(rpc->regmap, RPCIF_SMRDR1, p++);
			regmap_read(rpc->regmap, RPCIF_SMRDR0, p);
			memcpy(rpc->buffer + pos, data, nbytes);

			pos += nbytes;
		}
		break;
	default:
		regmap_write(rpc->regmap, RPCIF_SMENR, rpc->enable);
		regmap_write(rpc->regmap, RPCIF_SMCR,
			     rpc->smcr | RPCIF_SMCR_SPIE);
		ret = wait_msg_xfer_end(rpc);
		if (ret)
			goto err_out;
	}

	return ret;

err_out:
	if (reset_control_reset(rpc->rstc))
		dev_err(rpc->dev, "Failed to reset HW\n");
	rpcif_hw_init_impl(rpc, rpc->bus_size == 2);
	return ret;
}

static int xspi_manual_xfer_impl(struct rpcif_priv *xspi)
{
	u32 pos = 0, max = 8;
	int ret = 0;

	regmap_update_bits(xspi->regmap, XSPI_CDCTL0, XSPI_CDCTL0_TRNUM(0x3),
			   XSPI_CDCTL0_TRNUM(0));

	regmap_update_bits(xspi->regmap, XSPI_CDCTL0, XSPI_CDCTL0_TRREQ, 0);

	regmap_write(xspi->regmap, XSPI_CDTBUF0,
		     XSPI_CDTBUF_CMDSIZE(0x1) | XSPI_CDTBUF_CMD_FIELD(xspi->command));

	regmap_write(xspi->regmap, XSPI_CDABUF0, 0);

	regmap_update_bits(xspi->regmap, XSPI_CDTBUF0, XSPI_CDTBUF_ADDSIZE(0x7),
			   XSPI_CDTBUF_ADDSIZE(xspi->addr_nbytes));

	regmap_write(xspi->regmap, XSPI_CDABUF0, xspi->smadr);

	regmap_update_bits(xspi->regmap, XSPI_LIOCFGCS0, XSPI_LIOCFG_PRTMD(0x3ff),
			   XSPI_LIOCFG_PRTMD(xspi->proto));

	switch (xspi->dir) {
	case RPCIF_DATA_OUT:
		while (pos < xspi->xferlen) {
			u32 bytes_left = xspi->xferlen - pos;
			u32 nbytes, data[2], *p = data;

			regmap_update_bits(xspi->regmap, XSPI_CDTBUF0,
					   XSPI_CDTBUF_TRTYPE, XSPI_CDTBUF_TRTYPE);

			nbytes = bytes_left >= max ? max : bytes_left;

			regmap_update_bits(xspi->regmap, XSPI_CDTBUF0,
					   XSPI_CDTBUF_DATASIZE(0xf),
					   XSPI_CDTBUF_DATASIZE(nbytes));

			regmap_update_bits(xspi->regmap, XSPI_CDTBUF0,
					   XSPI_CDTBUF_ADDSIZE(0x7),
					   XSPI_CDTBUF_ADDSIZE(xspi->addr_nbytes));

			memcpy(data, xspi->buffer + pos, nbytes);

			if (nbytes > 4) {
				regmap_write(xspi->regmap, XSPI_CDD0BUF0, *p++);
				regmap_write(xspi->regmap, XSPI_CDD1BUF0, *p);
			} else {
				regmap_write(xspi->regmap, XSPI_CDD0BUF0, *p);
			}

			regmap_write(xspi->regmap, XSPI_CDABUF0, xspi->smadr + pos);

			regmap_update_bits(xspi->regmap, XSPI_CDCTL0,
					   XSPI_CDCTL0_TRREQ, XSPI_CDCTL0_TRREQ);

			ret = wait_msg_xfer_end(xspi);
			if (ret)
				goto err_out;

			regmap_update_bits(xspi->regmap, XSPI_INTC,
					   XSPI_INTC_CMDCMPC, XSPI_INTC_CMDCMPC);

			pos += nbytes;
		}
		regmap_update_bits(xspi->regmap, XSPI_CDCTL0, XSPI_CDCTL0_TRREQ, 0);
		break;
	case RPCIF_DATA_IN:
		while (pos < xspi->xferlen) {
			u32 bytes_left = xspi->xferlen - pos;
			u32 nbytes, data[2], *p = data;

			regmap_update_bits(xspi->regmap, XSPI_CDTBUF0,
					   XSPI_CDTBUF_TRTYPE,
					   ~(u32)XSPI_CDTBUF_TRTYPE);

			/* nbytes can be up to 8 bytes */
			nbytes = bytes_left >= max ? max : bytes_left;

			regmap_update_bits(xspi->regmap, XSPI_CDTBUF0,
					   XSPI_CDTBUF_DATASIZE(0xf),
					   XSPI_CDTBUF_DATASIZE(nbytes));

			regmap_update_bits(xspi->regmap, XSPI_CDTBUF0,
					   XSPI_CDTBUF_ADDSIZE(0x7),
					   XSPI_CDTBUF_ADDSIZE(xspi->addr_nbytes));

			if (xspi->addr_nbytes)
				regmap_write(xspi->regmap, XSPI_CDABUF0,
					     xspi->smadr + pos);

			regmap_update_bits(xspi->regmap, XSPI_CDTBUF0,
					   XSPI_CDTBUF_LATE(0x1f),
					   XSPI_CDTBUF_LATE(xspi->dummy));

			regmap_update_bits(xspi->regmap, XSPI_CDCTL0,
					   XSPI_CDCTL0_TRREQ, XSPI_CDCTL0_TRREQ);

			ret = wait_msg_xfer_end(xspi);
			if (ret)
				goto err_out;

			if (nbytes > 4) {
				regmap_read(xspi->regmap, XSPI_CDD0BUF0, p++);
				regmap_read(xspi->regmap, XSPI_CDD1BUF0, p);
			} else {
				regmap_read(xspi->regmap, XSPI_CDD0BUF0, p);
			}

			memcpy(xspi->buffer + pos, data, nbytes);

			regmap_update_bits(xspi->regmap, XSPI_INTC,
					   XSPI_INTC_CMDCMPC, XSPI_INTC_CMDCMPC);

			pos += nbytes;
		}
		regmap_update_bits(xspi->regmap, XSPI_CDCTL0,
				   XSPI_CDCTL0_TRREQ, 0);
		break;
	default:
		regmap_update_bits(xspi->regmap, XSPI_CDTBUF0,
				   XSPI_CDTBUF_TRTYPE, XSPI_CDTBUF_TRTYPE);
		regmap_update_bits(xspi->regmap, XSPI_CDCTL0,
				   XSPI_CDCTL0_TRREQ, XSPI_CDCTL0_TRREQ);

		ret = wait_msg_xfer_end(xspi);
		if (ret)
			goto err_out;

		regmap_update_bits(xspi->regmap, XSPI_INTC,
				   XSPI_INTC_CMDCMPC, XSPI_INTC_CMDCMPC);
	}

	return ret;

err_out:
	xspi_hw_init_impl(xspi, false);
	return ret;
}

int rpcif_manual_xfer(struct device *dev)
{
	struct rpcif_priv *rpc = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	ret = rpc->info->impl->manual_xfer(rpc);

	pm_runtime_put(dev);

	return ret;
}
EXPORT_SYMBOL(rpcif_manual_xfer);

static void memcpy_fromio_readw(void *to,
				const void __iomem *from,
				size_t count)
{
	const int maxw = (IS_ENABLED(CONFIG_64BIT)) ? 8 : 4;
	u8 buf[2];

	if (count && ((unsigned long)from & 1)) {
		*(u16 *)buf = __raw_readw((void __iomem *)((unsigned long)from & ~1));
		*(u8 *)to = buf[1];
		from++;
		to++;
		count--;
	}
	while (count >= 2 && !IS_ALIGNED((unsigned long)from, maxw)) {
		*(u16 *)to = __raw_readw(from);
		from += 2;
		to += 2;
		count -= 2;
	}
	while (count >= maxw) {
#ifdef CONFIG_64BIT
		*(u64 *)to = __raw_readq(from);
#else
		*(u32 *)to = __raw_readl(from);
#endif
		from += maxw;
		to += maxw;
		count -= maxw;
	}
	while (count >= 2) {
		*(u16 *)to = __raw_readw(from);
		from += 2;
		to += 2;
		count -= 2;
	}
	if (count) {
		*(u16 *)buf = __raw_readw(from);
		*(u8 *)to = buf[0];
	}
}

static size_t rpcif_dirmap_read_impl(struct rpcif_priv *rpc, u64 offs,
				     size_t len, void *buf)
{
	loff_t from = offs & (rpc->size - 1);
	size_t size = rpc->size - from;

	if (len > size)
		len = size;

	regmap_update_bits(rpc->regmap, RPCIF_CMNCR, RPCIF_CMNCR_MD, 0);
	regmap_write(rpc->regmap, RPCIF_DRCR, 0);
	regmap_write(rpc->regmap, RPCIF_DRCMR, rpc->command);
	regmap_write(rpc->regmap, RPCIF_DREAR,
		     RPCIF_DREAR_EAV(offs >> 25) | RPCIF_DREAR_EAC(1));
	regmap_write(rpc->regmap, RPCIF_DROPR, rpc->option);
	regmap_write(rpc->regmap, RPCIF_DRENR,
		     rpc->enable & ~RPCIF_SMENR_SPIDE(0xF));
	regmap_write(rpc->regmap, RPCIF_DRDMCR, rpc->dummy);
	regmap_write(rpc->regmap, RPCIF_DRDRENR, rpc->ddr);

	if (rpc->bus_size == 2)
		memcpy_fromio_readw(buf, rpc->dirmap + from, len);
	else
		memcpy_fromio(buf, rpc->dirmap + from, len);

	return len;
}

static size_t xspi_dirmap_read_impl(struct rpcif_priv *xspi, u64 offs,
				    size_t len, void *buf)
{
	loff_t from = offs & (xspi->size - 1);
	size_t size = xspi->size - from;
	u8 addsize = xspi->addr_nbytes - 1;

	if (len > size)
		len = size;

	regmap_update_bits(xspi->regmap, XSPI_CMCFG0CS0,
			   XSPI_CMCFG0_FFMT(0x3) | XSPI_CMCFG0_ADDSIZE(0x3),
			   XSPI_CMCFG0_FFMT(0) | XSPI_CMCFG0_ADDSIZE(addsize));

	regmap_update_bits(xspi->regmap, XSPI_CMCFG1CS0,
			   XSPI_CMCFG1_RDCMD(0xffff) | XSPI_CMCFG1_RDLATE(0x1f),
			   XSPI_CMCFG1_RDCMD_UPPER_BYTE(xspi->command) |
			   XSPI_CMCFG1_RDLATE(xspi->dummy));

	regmap_update_bits(xspi->regmap, XSPI_BMCTL0, XSPI_BMCTL0_CS0ACC(0xff),
			   XSPI_BMCTL0_CS0ACC(0x01));

	regmap_update_bits(xspi->regmap, XSPI_BMCFG,
			   XSPI_BMCFG_WRMD | XSPI_BMCFG_MWRCOMB |
			   XSPI_BMCFG_MWRSIZE(0xff) | XSPI_BMCFG_PREEN,
			   0 | XSPI_BMCFG_MWRCOMB | XSPI_BMCFG_MWRSIZE(0x0f) |
			   XSPI_BMCFG_PREEN);

	regmap_update_bits(xspi->regmap, XSPI_LIOCFGCS0, XSPI_LIOCFG_PRTMD(0x3ff),
			   XSPI_LIOCFG_PRTMD(xspi->proto));

	memcpy_fromio(buf, xspi->dirmap + from, len);

	return len;
}

ssize_t rpcif_dirmap_read(struct device *dev, u64 offs, size_t len, void *buf)
{
	struct rpcif_priv *rpc = dev_get_drvdata(dev);
	size_t read;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	read = rpc->info->impl->dirmap_read(rpc, offs, len, buf);

	pm_runtime_put(dev);

	return read;
}
EXPORT_SYMBOL(rpcif_dirmap_read);

/**
 * xspi_dirmap_write - Write data to xspi memory.
 * @dev: xspi device
 * @offs: offset
 * @len: Number of bytes to be written.
 * @buf: Buffer holding write data.
 *
 * This function writes data into xspi memory.
 *
 * Returns number of bytes written on success, else negative errno.
 */
ssize_t xspi_dirmap_write(struct device *dev, u64 offs, size_t len, const void *buf)
{
	struct rpcif_priv *xspi = dev_get_drvdata(dev);
	loff_t from = offs & (xspi->size - 1);
	u8 addsize = xspi->addr_nbytes - 1;
	size_t size = xspi->size - from;
	ssize_t writebytes;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	if (len > size)
		len = size;

	if (len > MWRSIZE_MAX)
		writebytes = MWRSIZE_MAX;
	else
		writebytes = len;

	regmap_update_bits(xspi->regmap, XSPI_CMCFG0CS0,
			   XSPI_CMCFG0_FFMT(0x3) | XSPI_CMCFG0_ADDSIZE(0x3),
			   XSPI_CMCFG0_FFMT(0) | XSPI_CMCFG0_ADDSIZE(addsize));

	regmap_update_bits(xspi->regmap, XSPI_CMCFG2CS0,
			   XSPI_CMCFG2_WRCMD_UPPER(0xff) | XSPI_CMCFG2_WRLATE(0x1f),
			   XSPI_CMCFG2_WRCMD_UPPER(xspi->command) |
			   XSPI_CMCFG2_WRLATE(xspi->dummy));

	regmap_update_bits(xspi->regmap, XSPI_BMCTL0,
			   XSPI_BMCTL0_CS0ACC(0xff), XSPI_BMCTL0_CS0ACC(0x03));

	regmap_update_bits(xspi->regmap, XSPI_BMCFG,
			   XSPI_BMCFG_WRMD | XSPI_BMCFG_MWRCOMB |
			   XSPI_BMCFG_MWRSIZE(0xff) | XSPI_BMCFG_PREEN,
			   0 | XSPI_BMCFG_MWRCOMB | XSPI_BMCFG_MWRSIZE(0x0f) |
			   XSPI_BMCFG_PREEN);

	regmap_update_bits(xspi->regmap, XSPI_LIOCFGCS0, XSPI_LIOCFG_PRTMD(0x3ff),
			   XSPI_LIOCFG_PRTMD(xspi->proto));

	memcpy_toio(xspi->dirmap + from, buf, writebytes);

	/* Request to push the pending data */
	if (writebytes < MWRSIZE_MAX)
		regmap_update_bits(xspi->regmap, XSPI_BMCTL1,
				   XSPI_BMCTL1_MWRPUSH, XSPI_BMCTL1_MWRPUSH);

	pm_runtime_put(dev);

	return writebytes;
}
EXPORT_SYMBOL_GPL(xspi_dirmap_write);

static int rpcif_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct platform_device *vdev;
	struct device_node *flash;
	struct rpcif_priv *rpc;
	struct resource *res;
	const char *name;
	int ret;

	flash = of_get_next_child(dev->of_node, NULL);
	if (!flash) {
		dev_warn(dev, "no flash node found\n");
		return -ENODEV;
	}

	if (of_device_is_compatible(flash, "jedec,spi-nor")) {
		name = "rpc-if-spi";
	} else if (of_device_is_compatible(flash, "cfi-flash")) {
		name = "rpc-if-hyperflash";
	} else	{
		of_node_put(flash);
		dev_warn(dev, "unknown flash type\n");
		return -ENODEV;
	}
	of_node_put(flash);

	rpc = devm_kzalloc(dev, sizeof(*rpc), GFP_KERNEL);
	if (!rpc)
		return -ENOMEM;

	rpc->base = devm_platform_ioremap_resource_byname(pdev, "regs");
	if (IS_ERR(rpc->base))
		return PTR_ERR(rpc->base);
	rpc->info = of_device_get_match_data(dev);
	rpc->regmap = devm_regmap_init(dev, NULL, rpc, rpc->info->regmap_config);
	if (IS_ERR(rpc->regmap)) {
		dev_err(dev, "failed to init regmap for rpcif, error %ld\n",
			PTR_ERR(rpc->regmap));
		return	PTR_ERR(rpc->regmap);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dirmap");
	rpc->dirmap = devm_ioremap_resource(dev, res);
	if (IS_ERR(rpc->dirmap))
		return PTR_ERR(rpc->dirmap);

	rpc->size = resource_size(res);
	rpc->rstc = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(rpc->rstc))
		return PTR_ERR(rpc->rstc);

	/*
	 * The enabling/disabling of spi/spix2 clocks at runtime leading to
	 * flash write failure. So, enable these clocks during probe() and
	 * disable it in remove().
	 */
	if (rpc->info->type == XSPI_RZ_G3E) {
		struct clk *spi_clk;

		spi_clk = devm_clk_get_enabled(dev, "spix2");
		if (IS_ERR(spi_clk))
			return dev_err_probe(dev, PTR_ERR(spi_clk),
					     "cannot get enabled spix2 clk\n");

		spi_clk = devm_clk_get_enabled(dev, "spi");
		if (IS_ERR(spi_clk))
			return dev_err_probe(dev, PTR_ERR(spi_clk),
					     "cannot get enabled spi clk\n");
	}

	vdev = platform_device_alloc(name, pdev->id);
	if (!vdev)
		return -ENOMEM;
	vdev->dev.parent = dev;

	rpc->dev = dev;
	rpc->vdev = vdev;
	platform_set_drvdata(pdev, rpc);

	ret = platform_device_add(vdev);
	if (ret) {
		platform_device_put(vdev);
		return ret;
	}

	return 0;
}

static void rpcif_remove(struct platform_device *pdev)
{
	struct rpcif_priv *rpc = platform_get_drvdata(pdev);

	platform_device_unregister(rpc->vdev);
}

static const struct rpcif_impl rpcif_impl = {
	.hw_init = rpcif_hw_init_impl,
	.prepare = rpcif_prepare_impl,
	.manual_xfer = rpcif_manual_xfer_impl,
	.dirmap_read = rpcif_dirmap_read_impl,
	.status_reg = RPCIF_CMNSR,
	.status_mask = RPCIF_CMNSR_TEND,
};

static const struct rpcif_impl xspi_impl = {
	.hw_init = xspi_hw_init_impl,
	.prepare = xspi_prepare_impl,
	.manual_xfer = xspi_manual_xfer_impl,
	.dirmap_read = xspi_dirmap_read_impl,
	.status_reg = XSPI_INTS,
	.status_mask = XSPI_INTS_CMDCMP,
};

static const struct rpcif_info rpcif_info_r8a7796 = {
	.regmap_config = &rpcif_regmap_config,
	.impl = &rpcif_impl,
	.type = RPCIF_RCAR_GEN3,
	.strtim = 6,
};

static const struct rpcif_info rpcif_info_gen3 = {
	.regmap_config = &rpcif_regmap_config,
	.impl = &rpcif_impl,
	.type = RPCIF_RCAR_GEN3,
	.strtim = 7,
};

static const struct rpcif_info rpcif_info_rz_g2l = {
	.regmap_config = &rpcif_regmap_config,
	.impl = &rpcif_impl,
	.type = RPCIF_RZ_G2L,
	.strtim = 7,
};

static const struct rpcif_info rpcif_info_gen4 = {
	.regmap_config = &rpcif_regmap_config,
	.impl = &rpcif_impl,
	.type = RPCIF_RCAR_GEN4,
	.strtim = 15,
};

static const struct rpcif_info xspi_info_r9a09g047 = {
	.regmap_config = &xspi_regmap_config,
	.impl = &xspi_impl,
	.type = XSPI_RZ_G3E,
};

static const struct of_device_id rpcif_of_match[] = {
	{ .compatible = "renesas,r8a7796-rpc-if", .data = &rpcif_info_r8a7796 },
	{ .compatible = "renesas,r9a09g047-xspi", .data = &xspi_info_r9a09g047 },
	{ .compatible = "renesas,rcar-gen3-rpc-if", .data = &rpcif_info_gen3 },
	{ .compatible = "renesas,rcar-gen4-rpc-if", .data = &rpcif_info_gen4 },
	{ .compatible = "renesas,rzg2l-rpc-if", .data = &rpcif_info_rz_g2l },
	{},
};
MODULE_DEVICE_TABLE(of, rpcif_of_match);

static struct platform_driver rpcif_driver = {
	.probe	= rpcif_probe,
	.remove = rpcif_remove,
	.driver = {
		.name =	"rpc-if",
		.of_match_table = rpcif_of_match,
	},
};
module_platform_driver(rpcif_driver);

MODULE_DESCRIPTION("Renesas RPC-IF core driver");
MODULE_LICENSE("GPL v2");
