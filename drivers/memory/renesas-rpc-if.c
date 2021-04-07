// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RPC-IF core driver
 *
 * Copyright (C) 2018-2019 Renesas Solutions Corp.
 * Copyright (C) 2019 Macronix International Co., Ltd.
 * Copyright (C) 2019-2020 Cogent Embedded, Inc.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <memory/renesas-rpc-if.h>

#define RPCIF_CMNCR		0x0000	/* R/W */
#define RPCIF_CMNCR_MD		BIT(31)
#define RPCIF_CMNCR_SFDE	BIT(24) /* undocumented but must be set */
#define RPCIF_CMNCR_MOIIO3(val)	(((val) & 0x3) << 22)
#define RPCIF_CMNCR_MOIIO2(val)	(((val) & 0x3) << 20)
#define RPCIF_CMNCR_MOIIO1(val)	(((val) & 0x3) << 18)
#define RPCIF_CMNCR_MOIIO0(val)	(((val) & 0x3) << 16)
#define RPCIF_CMNCR_MOIIO_HIZ	(RPCIF_CMNCR_MOIIO0(3) | \
				 RPCIF_CMNCR_MOIIO1(3) | \
				 RPCIF_CMNCR_MOIIO2(3) | RPCIF_CMNCR_MOIIO3(3))
#define RPCIF_CMNCR_IO3FV(val)	(((val) & 0x3) << 14) /* undocumented */
#define RPCIF_CMNCR_IO2FV(val)	(((val) & 0x3) << 12) /* undocumented */
#define RPCIF_CMNCR_IO0FV(val)	(((val) & 0x3) << 8)
#define RPCIF_CMNCR_IOFV_HIZ	(RPCIF_CMNCR_IO0FV(3) | RPCIF_CMNCR_IO2FV(3) | \
				 RPCIF_CMNCR_IO3FV(3))
#define RPCIF_CMNCR_BSZ(val)	(((val) & 0x3) << 0)

#define RPCIF_SSLDR		0x0004	/* R/W */
#define RPCIF_SSLDR_SPNDL(d)	(((d) & 0x7) << 16)
#define RPCIF_SSLDR_SLNDL(d)	(((d) & 0x7) << 8)
#define RPCIF_SSLDR_SCKDL(d)	(((d) & 0x7) << 0)

#define RPCIF_DRCR		0x000C	/* R/W */
#define RPCIF_DRCR_SSLN		BIT(24)
#define RPCIF_DRCR_RBURST(v)	((((v) - 1) & 0x1F) << 16)
#define RPCIF_DRCR_RCF		BIT(9)
#define RPCIF_DRCR_RBE		BIT(8)
#define RPCIF_DRCR_SSLE		BIT(0)

#define RPCIF_DRCMR		0x0010	/* R/W */
#define RPCIF_DRCMR_CMD(c)	(((c) & 0xFF) << 16)
#define RPCIF_DRCMR_OCMD(c)	(((c) & 0xFF) << 0)

#define RPCIF_DREAR		0x0014	/* R/W */
#define RPCIF_DREAR_EAV(c)	(((c) & 0xF) << 16)
#define RPCIF_DREAR_EAC(c)	(((c) & 0x7) << 0)

#define RPCIF_DROPR		0x0018	/* R/W */

#define RPCIF_DRENR		0x001C	/* R/W */
#define RPCIF_DRENR_CDB(o)	(u32)((((o) & 0x3) << 30))
#define RPCIF_DRENR_OCDB(o)	(((o) & 0x3) << 28)
#define RPCIF_DRENR_ADB(o)	(((o) & 0x3) << 24)
#define RPCIF_DRENR_OPDB(o)	(((o) & 0x3) << 20)
#define RPCIF_DRENR_DRDB(o)	(((o) & 0x3) << 16)
#define RPCIF_DRENR_DME		BIT(15)
#define RPCIF_DRENR_CDE		BIT(14)
#define RPCIF_DRENR_OCDE	BIT(12)
#define RPCIF_DRENR_ADE(v)	(((v) & 0xF) << 8)
#define RPCIF_DRENR_OPDE(v)	(((v) & 0xF) << 4)

#define RPCIF_SMCR		0x0020	/* R/W */
#define RPCIF_SMCR_SSLKP	BIT(8)
#define RPCIF_SMCR_SPIRE	BIT(2)
#define RPCIF_SMCR_SPIWE	BIT(1)
#define RPCIF_SMCR_SPIE		BIT(0)

#define RPCIF_SMCMR		0x0024	/* R/W */
#define RPCIF_SMCMR_CMD(c)	(((c) & 0xFF) << 16)
#define RPCIF_SMCMR_OCMD(c)	(((c) & 0xFF) << 0)

#define RPCIF_SMADR		0x0028	/* R/W */

#define RPCIF_SMOPR		0x002C	/* R/W */
#define RPCIF_SMOPR_OPD3(o)	(((o) & 0xFF) << 24)
#define RPCIF_SMOPR_OPD2(o)	(((o) & 0xFF) << 16)
#define RPCIF_SMOPR_OPD1(o)	(((o) & 0xFF) << 8)
#define RPCIF_SMOPR_OPD0(o)	(((o) & 0xFF) << 0)

#define RPCIF_SMENR		0x0030	/* R/W */
#define RPCIF_SMENR_CDB(o)	(((o) & 0x3) << 30)
#define RPCIF_SMENR_OCDB(o)	(((o) & 0x3) << 28)
#define RPCIF_SMENR_ADB(o)	(((o) & 0x3) << 24)
#define RPCIF_SMENR_OPDB(o)	(((o) & 0x3) << 20)
#define RPCIF_SMENR_SPIDB(o)	(((o) & 0x3) << 16)
#define RPCIF_SMENR_DME		BIT(15)
#define RPCIF_SMENR_CDE		BIT(14)
#define RPCIF_SMENR_OCDE	BIT(12)
#define RPCIF_SMENR_ADE(v)	(((v) & 0xF) << 8)
#define RPCIF_SMENR_OPDE(v)	(((v) & 0xF) << 4)
#define RPCIF_SMENR_SPIDE(v)	(((v) & 0xF) << 0)

#define RPCIF_SMRDR0		0x0038	/* R */
#define RPCIF_SMRDR1		0x003C	/* R */
#define RPCIF_SMWDR0		0x0040	/* W */
#define RPCIF_SMWDR1		0x0044	/* W */

#define RPCIF_CMNSR		0x0048	/* R */
#define RPCIF_CMNSR_SSLF	BIT(1)
#define RPCIF_CMNSR_TEND	BIT(0)

#define RPCIF_DRDMCR		0x0058	/* R/W */
#define RPCIF_DMDMCR_DMCYC(v)	((((v) - 1) & 0x1F) << 0)

#define RPCIF_DRDRENR		0x005C	/* R/W */
#define RPCIF_DRDRENR_HYPE(v)	(((v) & 0x7) << 12)
#define RPCIF_DRDRENR_ADDRE	BIT(8)
#define RPCIF_DRDRENR_OPDRE	BIT(4)
#define RPCIF_DRDRENR_DRDRE	BIT(0)

#define RPCIF_SMDMCR		0x0060	/* R/W */
#define RPCIF_SMDMCR_DMCYC(v)	((((v) - 1) & 0x1F) << 0)

#define RPCIF_SMDRENR		0x0064	/* R/W */
#define RPCIF_SMDRENR_HYPE(v)	(((v) & 0x7) << 12)
#define RPCIF_SMDRENR_ADDRE	BIT(8)
#define RPCIF_SMDRENR_OPDRE	BIT(4)
#define RPCIF_SMDRENR_SPIDRE	BIT(0)

#define RPCIF_PHYCNT		0x007C	/* R/W */
#define RPCIF_PHYCNT_CAL	BIT(31)
#define RPCIF_PHYCNT_OCTA(v)	(((v) & 0x3) << 22)
#define RPCIF_PHYCNT_EXDS	BIT(21)
#define RPCIF_PHYCNT_OCT	BIT(20)
#define RPCIF_PHYCNT_DDRCAL	BIT(19)
#define RPCIF_PHYCNT_HS		BIT(18)
#define RPCIF_PHYCNT_STRTIM(v)	(((v) & 0x7) << 15)
#define RPCIF_PHYCNT_WBUF2	BIT(4)
#define RPCIF_PHYCNT_WBUF	BIT(2)
#define RPCIF_PHYCNT_PHYMEM(v)	(((v) & 0x3) << 0)

#define RPCIF_PHYOFFSET1	0x0080	/* R/W */
#define RPCIF_PHYOFFSET1_DDRTMG(v) (((v) & 0x3) << 28)

#define RPCIF_PHYOFFSET2	0x0084	/* R/W */
#define RPCIF_PHYOFFSET2_OCTTMG(v) (((v) & 0x7) << 8)

#define RPCIF_PHYINT		0x0088	/* R/W */
#define RPCIF_PHYINT_WPVAL	BIT(1)

#define RPCIF_DIRMAP_SIZE	0x4000000

static const struct regmap_range rpcif_volatile_ranges[] = {
	regmap_reg_range(RPCIF_SMRDR0, RPCIF_SMRDR1),
	regmap_reg_range(RPCIF_SMWDR0, RPCIF_SMWDR1),
	regmap_reg_range(RPCIF_CMNSR, RPCIF_CMNSR),
};

static const struct regmap_access_table rpcif_volatile_table = {
	.yes_ranges	= rpcif_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(rpcif_volatile_ranges),
};

static const struct regmap_config rpcif_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.fast_io	= true,
	.max_register	= RPCIF_PHYINT,
	.volatile_table	= &rpcif_volatile_table,
};

int rpcif_sw_init(struct rpcif *rpc, struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;
	void __iomem *base;

	rpc->dev = dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	rpc->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &rpcif_regmap_config);
	if (IS_ERR(rpc->regmap)) {
		dev_err(&pdev->dev,
			"failed to init regmap for rpcif, error %ld\n",
			PTR_ERR(rpc->regmap));
		return	PTR_ERR(rpc->regmap);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dirmap");
	rpc->dirmap = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rpc->dirmap))
		rpc->dirmap = NULL;
	rpc->size = resource_size(res);

	rpc->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);

	return PTR_ERR_OR_ZERO(rpc->rstc);
}
EXPORT_SYMBOL(rpcif_sw_init);

void rpcif_hw_init(struct rpcif *rpc, bool hyperflash)
{
	u32 dummy;

	pm_runtime_get_sync(rpc->dev);

	/*
	 * NOTE: The 0x260 are undocumented bits, but they must be set.
	 *	 RPCIF_PHYCNT_STRTIM is strobe timing adjustment bits,
	 *	 0x0 : the delay is biggest,
	 *	 0x1 : the delay is 2nd biggest,
	 *	 On H3 ES1.x, the value should be 0, while on others,
	 *	 the value should be 7.
	 */
	regmap_write(rpc->regmap, RPCIF_PHYCNT, RPCIF_PHYCNT_STRTIM(7) |
		     RPCIF_PHYCNT_PHYMEM(hyperflash ? 3 : 0) | 0x260);

	/*
	 * NOTE: The 0x1511144 are undocumented bits, but they must be set
	 *       for RPCIF_PHYOFFSET1.
	 *	 The 0x31 are undocumented bits, but they must be set
	 *	 for RPCIF_PHYOFFSET2.
	 */
	regmap_write(rpc->regmap, RPCIF_PHYOFFSET1, 0x1511144 |
		     RPCIF_PHYOFFSET1_DDRTMG(3));
	regmap_write(rpc->regmap, RPCIF_PHYOFFSET2, 0x31 |
		     RPCIF_PHYOFFSET2_OCTTMG(4));

	if (hyperflash)
		regmap_update_bits(rpc->regmap, RPCIF_PHYINT,
				   RPCIF_PHYINT_WPVAL, 0);

	regmap_write(rpc->regmap, RPCIF_CMNCR, RPCIF_CMNCR_SFDE |
		     RPCIF_CMNCR_MOIIO_HIZ | RPCIF_CMNCR_IOFV_HIZ |
		     RPCIF_CMNCR_BSZ(hyperflash ? 1 : 0));
	/* Set RCF after BSZ update */
	regmap_write(rpc->regmap, RPCIF_DRCR, RPCIF_DRCR_RCF);
	/* Dummy read according to spec */
	regmap_read(rpc->regmap, RPCIF_DRCR, &dummy);
	regmap_write(rpc->regmap, RPCIF_SSLDR, RPCIF_SSLDR_SPNDL(7) |
		     RPCIF_SSLDR_SLNDL(7) | RPCIF_SSLDR_SCKDL(7));

	pm_runtime_put(rpc->dev);

	rpc->bus_size = hyperflash ? 2 : 1;
}
EXPORT_SYMBOL(rpcif_hw_init);

static int wait_msg_xfer_end(struct rpcif *rpc)
{
	u32 sts;

	return regmap_read_poll_timeout(rpc->regmap, RPCIF_CMNSR, sts,
					sts & RPCIF_CMNSR_TEND, 0,
					USEC_PER_SEC);
}

static u8 rpcif_bits_set(struct rpcif *rpc, u32 nbytes)
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

void rpcif_prepare(struct rpcif *rpc, const struct rpcif_op *op, u64 *offs,
		   size_t *len)
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
		rpc->dummy = RPCIF_SMDMCR_DMCYC(op->dummy.ncycles /
						op->dummy.buswidth);
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

		rpc->enable |= RPCIF_SMENR_SPIDE(rpcif_bits_set(rpc, nbytes)) |
			RPCIF_SMENR_SPIDB(rpcif_bit_size(op->data.buswidth));
	}
}
EXPORT_SYMBOL(rpcif_prepare);

int rpcif_manual_xfer(struct rpcif *rpc)
{
	u32 smenr, smcr, pos = 0, max = 4;
	int ret = 0;

	if (rpc->bus_size == 2)
		max = 8;

	pm_runtime_get_sync(rpc->dev);

	regmap_update_bits(rpc->regmap, RPCIF_PHYCNT,
			   RPCIF_PHYCNT_CAL, RPCIF_PHYCNT_CAL);
	regmap_update_bits(rpc->regmap, RPCIF_CMNCR,
			   RPCIF_CMNCR_MD, RPCIF_CMNCR_MD);
	regmap_write(rpc->regmap, RPCIF_SMCMR, rpc->command);
	regmap_write(rpc->regmap, RPCIF_SMOPR, rpc->option);
	regmap_write(rpc->regmap, RPCIF_SMDMCR, rpc->dummy);
	regmap_write(rpc->regmap, RPCIF_SMDRENR, rpc->ddr);
	smenr = rpc->enable;

	switch (rpc->dir) {
	case RPCIF_DATA_OUT:
		while (pos < rpc->xferlen) {
			u32 nbytes = rpc->xferlen - pos;
			u32 data[2];

			smcr = rpc->smcr | RPCIF_SMCR_SPIE;
			if (nbytes > max) {
				nbytes = max;
				smcr |= RPCIF_SMCR_SSLKP;
			}

			memcpy(data, rpc->buffer + pos, nbytes);
			if (nbytes > 4) {
				regmap_write(rpc->regmap, RPCIF_SMWDR1,
					     data[0]);
				regmap_write(rpc->regmap, RPCIF_SMWDR0,
					     data[1]);
			} else if (nbytes > 2) {
				regmap_write(rpc->regmap, RPCIF_SMWDR0,
					     data[0]);
			} else	{
				regmap_write(rpc->regmap, RPCIF_SMWDR0,
					     data[0] << 16);
			}

			regmap_write(rpc->regmap, RPCIF_SMADR,
				     rpc->smadr + pos);
			regmap_write(rpc->regmap, RPCIF_SMENR, smenr);
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
			u32 nbytes = rpc->xferlen - pos;
			u32 data[2];

			if (nbytes > max)
				nbytes = max;

			regmap_write(rpc->regmap, RPCIF_SMADR,
				     rpc->smadr + pos);
			regmap_write(rpc->regmap, RPCIF_SMENR, smenr);
			regmap_write(rpc->regmap, RPCIF_SMCR,
				     rpc->smcr | RPCIF_SMCR_SPIE);
			ret = wait_msg_xfer_end(rpc);
			if (ret)
				goto err_out;

			if (nbytes > 4) {
				regmap_read(rpc->regmap, RPCIF_SMRDR1,
					    &data[0]);
				regmap_read(rpc->regmap, RPCIF_SMRDR0,
					    &data[1]);
			} else if (nbytes > 2) {
				regmap_read(rpc->regmap, RPCIF_SMRDR0,
					    &data[0]);
			} else	{
				regmap_read(rpc->regmap, RPCIF_SMRDR0,
					    &data[0]);
				data[0] >>= 16;
			}
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

exit:
	pm_runtime_put(rpc->dev);
	return ret;

err_out:
	if (reset_control_reset(rpc->rstc))
		dev_err(rpc->dev, "Failed to reset HW\n");
	rpcif_hw_init(rpc, rpc->bus_size == 2);
	goto exit;
}
EXPORT_SYMBOL(rpcif_manual_xfer);

ssize_t rpcif_dirmap_read(struct rpcif *rpc, u64 offs, size_t len, void *buf)
{
	loff_t from = offs & (RPCIF_DIRMAP_SIZE - 1);
	size_t size = RPCIF_DIRMAP_SIZE - from;

	if (len > size)
		len = size;

	pm_runtime_get_sync(rpc->dev);

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

	memcpy_fromio(buf, rpc->dirmap + from, len);

	pm_runtime_put(rpc->dev);

	return len;
}
EXPORT_SYMBOL(rpcif_dirmap_read);

static int rpcif_probe(struct platform_device *pdev)
{
	struct platform_device *vdev;
	struct device_node *flash;
	const char *name;

	flash = of_get_next_child(pdev->dev.of_node, NULL);
	if (!flash) {
		dev_warn(&pdev->dev, "no flash node found\n");
		return -ENODEV;
	}

	if (of_device_is_compatible(flash, "jedec,spi-nor")) {
		name = "rpc-if-spi";
	} else if (of_device_is_compatible(flash, "cfi-flash")) {
		name = "rpc-if-hyperflash";
	} else	{
		of_node_put(flash);
		dev_warn(&pdev->dev, "unknown flash type\n");
		return -ENODEV;
	}
	of_node_put(flash);

	vdev = platform_device_alloc(name, pdev->id);
	if (!vdev)
		return -ENOMEM;
	vdev->dev.parent = &pdev->dev;
	platform_set_drvdata(pdev, vdev);
	return platform_device_add(vdev);
}

static int rpcif_remove(struct platform_device *pdev)
{
	struct platform_device *vdev = platform_get_drvdata(pdev);

	platform_device_unregister(vdev);

	return 0;
}

static const struct of_device_id rpcif_of_match[] = {
	{ .compatible = "renesas,rcar-gen3-rpc-if", },
	{},
};
MODULE_DEVICE_TABLE(of, rpcif_of_match);

static struct platform_driver rpcif_driver = {
	.probe	= rpcif_probe,
	.remove	= rpcif_remove,
	.driver = {
		.name =	"rpc-if",
		.of_match_table = rpcif_of_match,
	},
};
module_platform_driver(rpcif_driver);

MODULE_DESCRIPTION("Renesas RPC-IF core driver");
MODULE_LICENSE("GPL v2");
