// SPDX-License-Identifier: GPL-2.0-only

#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#define SNAFCFR 0x00
#define   SNAFCFR_DMA_IE BIT(20)
#define SNAFCCR 0x04
#define SNAFWCMR 0x08
#define SNAFRCMR 0x0c
#define SNAFRDR 0x10
#define SNAFWDR 0x14
#define SNAFDTR 0x18
#define SNAFDRSAR 0x1c
#define SNAFDIR 0x20
#define   SNAFDIR_DMA_IP BIT(0)
#define SNAFDLR 0x24
#define SNAFSR 0x40
#define  SNAFSR_NFCOS BIT(3)
#define  SNAFSR_NFDRS BIT(2)
#define  SNAFSR_NFDWS BIT(1)

#define CMR_LEN(len) ((len) - 1)
#define CMR_WID(width) (((width) >> 1) << 28)

struct rtl_snand {
	struct device *dev;
	struct regmap *regmap;
	struct completion comp;
};

static irqreturn_t rtl_snand_irq(int irq, void *data)
{
	struct rtl_snand *snand = data;
	u32 val = 0;

	regmap_read(snand->regmap, SNAFSR, &val);
	if (val & (SNAFSR_NFCOS | SNAFSR_NFDRS | SNAFSR_NFDWS))
		return IRQ_NONE;

	regmap_write(snand->regmap, SNAFDIR, SNAFDIR_DMA_IP);
	complete(&snand->comp);

	return IRQ_HANDLED;
}

static bool rtl_snand_supports_op(struct spi_mem *mem,
				  const struct spi_mem_op *op)
{
	if (!spi_mem_default_supports_op(mem, op))
		return false;
	if (op->cmd.nbytes != 1 || op->cmd.buswidth != 1)
		return false;
	return true;
}

static void rtl_snand_set_cs(struct rtl_snand *snand, int cs, bool active)
{
	u32 val;

	if (active)
		val = ~(1 << (4 * cs));
	else
		val = ~0;

	regmap_write(snand->regmap, SNAFCCR, val);
}

static int rtl_snand_wait_ready(struct rtl_snand *snand)
{
	u32 val;

	return regmap_read_poll_timeout(snand->regmap, SNAFSR, val, !(val & SNAFSR_NFCOS),
					0, 2 * USEC_PER_MSEC);
}

static int rtl_snand_xfer_head(struct rtl_snand *snand, int cs, const struct spi_mem_op *op)
{
	int ret;
	u32 val, len = 0;

	rtl_snand_set_cs(snand, cs, true);

	val = op->cmd.opcode << 24;
	len = 1;
	if (op->addr.nbytes && op->addr.buswidth == 1) {
		val |= op->addr.val << ((3 - op->addr.nbytes) * 8);
		len += op->addr.nbytes;
	}

	ret = rtl_snand_wait_ready(snand);
	if (ret)
		return ret;

	ret = regmap_write(snand->regmap, SNAFWCMR, CMR_LEN(len));
	if (ret)
		return ret;

	ret = regmap_write(snand->regmap, SNAFWDR, val);
	if (ret)
		return ret;

	ret = rtl_snand_wait_ready(snand);
	if (ret)
		return ret;

	if (op->addr.buswidth > 1) {
		val = op->addr.val << ((3 - op->addr.nbytes) * 8);
		len = op->addr.nbytes;

		ret = regmap_write(snand->regmap, SNAFWCMR,
				   CMR_WID(op->addr.buswidth) | CMR_LEN(len));
		if (ret)
			return ret;

		ret = regmap_write(snand->regmap, SNAFWDR, val);
		if (ret)
			return ret;

		ret = rtl_snand_wait_ready(snand);
		if (ret)
			return ret;
	}

	if (op->dummy.nbytes) {
		val = 0;

		ret = regmap_write(snand->regmap, SNAFWCMR,
				   CMR_WID(op->dummy.buswidth) | CMR_LEN(op->dummy.nbytes));
		if (ret)
			return ret;

		ret = regmap_write(snand->regmap, SNAFWDR, val);
		if (ret)
			return ret;

		ret = rtl_snand_wait_ready(snand);
		if (ret)
			return ret;
	}

	return 0;
}

static void rtl_snand_xfer_tail(struct rtl_snand *snand, int cs)
{
	rtl_snand_set_cs(snand, cs, false);
}

static int rtl_snand_xfer(struct rtl_snand *snand, int cs, const struct spi_mem_op *op)
{
	unsigned int pos, nbytes;
	int ret;
	u32 val, len = 0;

	ret = rtl_snand_xfer_head(snand, cs, op);
	if (ret)
		goto out_deselect;

	if (op->data.dir == SPI_MEM_DATA_IN) {
		pos = 0;
		len = op->data.nbytes;

		while (pos < len) {
			nbytes = len - pos;
			if (nbytes > 4)
				nbytes = 4;

			ret = rtl_snand_wait_ready(snand);
			if (ret)
				goto out_deselect;

			ret = regmap_write(snand->regmap, SNAFRCMR,
					   CMR_WID(op->data.buswidth) | CMR_LEN(nbytes));
			if (ret)
				goto out_deselect;

			ret = rtl_snand_wait_ready(snand);
			if (ret)
				goto out_deselect;

			ret = regmap_read(snand->regmap, SNAFRDR, &val);
			if (ret)
				goto out_deselect;

			memcpy(op->data.buf.in + pos, &val, nbytes);

			pos += nbytes;
		}
	} else if (op->data.dir == SPI_MEM_DATA_OUT) {
		pos = 0;
		len = op->data.nbytes;

		while (pos < len) {
			nbytes = len - pos;
			if (nbytes > 4)
				nbytes = 4;

			memcpy(&val, op->data.buf.out + pos, nbytes);

			pos += nbytes;

			ret = regmap_write(snand->regmap, SNAFWCMR, CMR_LEN(nbytes));
			if (ret)
				goto out_deselect;

			ret = regmap_write(snand->regmap, SNAFWDR, val);
			if (ret)
				goto out_deselect;

			ret = rtl_snand_wait_ready(snand);
			if (ret)
				goto out_deselect;
		}
	}

out_deselect:
	rtl_snand_xfer_tail(snand, cs);

	if (ret)
		dev_err(snand->dev, "transfer failed %d\n", ret);

	return ret;
}

static int rtl_snand_dma_xfer(struct rtl_snand *snand, int cs, const struct spi_mem_op *op)
{
	unsigned int pos, nbytes;
	int ret;
	dma_addr_t buf_dma;
	enum dma_data_direction dir;
	u32 trig, len, maxlen;

	ret = rtl_snand_xfer_head(snand, cs, op);
	if (ret)
		goto out_deselect;

	if (op->data.dir == SPI_MEM_DATA_IN) {
		maxlen = 2080;
		dir = DMA_FROM_DEVICE;
		trig = 0;
	} else if (op->data.dir == SPI_MEM_DATA_OUT) {
		maxlen = 520;
		dir = DMA_TO_DEVICE;
		trig = 1;
	} else {
		ret = -EOPNOTSUPP;
		goto out_deselect;
	}

	buf_dma = dma_map_single(snand->dev, op->data.buf.in, op->data.nbytes, dir);
	ret = dma_mapping_error(snand->dev, buf_dma);
	if (ret)
		goto out_deselect;

	ret = regmap_write(snand->regmap, SNAFDIR, SNAFDIR_DMA_IP);
	if (ret)
		goto out_unmap;

	ret = regmap_update_bits(snand->regmap, SNAFCFR, SNAFCFR_DMA_IE, SNAFCFR_DMA_IE);
	if (ret)
		goto out_unmap;

	pos = 0;
	len = op->data.nbytes;

	while (pos < len) {
		nbytes = len - pos;
		if (nbytes > maxlen)
			nbytes = maxlen;

		reinit_completion(&snand->comp);

		ret = regmap_write(snand->regmap, SNAFDRSAR, buf_dma + pos);
		if (ret)
			goto out_disable_int;

		pos += nbytes;

		ret = regmap_write(snand->regmap, SNAFDLR,
				CMR_WID(op->data.buswidth) | nbytes);
		if (ret)
			goto out_disable_int;

		ret = regmap_write(snand->regmap, SNAFDTR, trig);
		if (ret)
			goto out_disable_int;

		if (!wait_for_completion_timeout(&snand->comp, usecs_to_jiffies(20000)))
			ret = -ETIMEDOUT;

		if (ret)
			goto out_disable_int;
	}

out_disable_int:
	regmap_update_bits(snand->regmap, SNAFCFR, SNAFCFR_DMA_IE, 0);
out_unmap:
	dma_unmap_single(snand->dev, buf_dma, op->data.nbytes, dir);
out_deselect:
	rtl_snand_xfer_tail(snand, cs);

	if (ret)
		dev_err(snand->dev, "transfer failed %d\n", ret);

	return ret;
}

static bool rtl_snand_dma_op(const struct spi_mem_op *op)
{
	switch (op->data.dir) {
	case SPI_MEM_DATA_IN:
	case SPI_MEM_DATA_OUT:
		return op->data.nbytes > 32;
	default:
		return false;
	}
}

static int rtl_snand_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct rtl_snand *snand = spi_controller_get_devdata(mem->spi->controller);
	int cs = spi_get_chipselect(mem->spi, 0);

	dev_dbg(snand->dev, "cs %d op cmd %02x %d:%d, dummy %d:%d, addr %08llx@%d:%d, data %d:%d\n",
		cs, op->cmd.opcode,
		op->cmd.buswidth, op->cmd.nbytes, op->dummy.buswidth,
		op->dummy.nbytes, op->addr.val, op->addr.buswidth,
		op->addr.nbytes, op->data.buswidth, op->data.nbytes);

	if (rtl_snand_dma_op(op))
		return rtl_snand_dma_xfer(snand, cs, op);
	else
		return rtl_snand_xfer(snand, cs, op);
}

static const struct spi_controller_mem_ops rtl_snand_mem_ops = {
	.supports_op = rtl_snand_supports_op,
	.exec_op = rtl_snand_exec_op,
};

static const struct of_device_id rtl_snand_match[] = {
	{ .compatible = "realtek,rtl9301-snand" },
	{ .compatible = "realtek,rtl9302b-snand" },
	{ .compatible = "realtek,rtl9302c-snand" },
	{ .compatible = "realtek,rtl9303-snand" },
	{},
};
MODULE_DEVICE_TABLE(of, rtl_snand_match);

static int rtl_snand_probe(struct platform_device *pdev)
{
	struct rtl_snand *snand;
	struct device *dev = &pdev->dev;
	struct spi_controller *ctrl;
	void __iomem *base;
	const struct regmap_config rc = {
		.reg_bits	= 32,
		.val_bits	= 32,
		.reg_stride	= 4,
	};
	int irq, ret;

	ctrl = devm_spi_alloc_host(dev, sizeof(*snand));
	if (!ctrl)
		return -ENOMEM;

	snand = spi_controller_get_devdata(ctrl);
	snand->dev = dev;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	snand->regmap = devm_regmap_init_mmio(dev, base, &rc);
	if (IS_ERR(snand->regmap))
		return PTR_ERR(snand->regmap);

	init_completion(&snand->comp);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = dma_set_mask(snand->dev, DMA_BIT_MASK(32));
	if (ret)
		return dev_err_probe(dev, ret, "failed to set DMA mask\n");

	ret = devm_request_irq(dev, irq, rtl_snand_irq, 0, "rtl-snand", snand);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request irq\n");

	ctrl->num_chipselect = 2;
	ctrl->mem_ops = &rtl_snand_mem_ops;
	ctrl->bits_per_word_mask = SPI_BPW_MASK(8);
	ctrl->mode_bits = SPI_RX_DUAL | SPI_RX_QUAD | SPI_TX_DUAL | SPI_TX_QUAD;
	device_set_node(&ctrl->dev, dev_fwnode(dev));

	return devm_spi_register_controller(dev, ctrl);
}

static struct platform_driver rtl_snand_driver = {
	.driver = {
		.name = "realtek-rtl-snand",
		.of_match_table = rtl_snand_match,
	},
	.probe = rtl_snand_probe,
};
module_platform_driver(rtl_snand_driver);

MODULE_DESCRIPTION("Realtek SPI-NAND Flash Controller Driver");
MODULE_LICENSE("GPL");
