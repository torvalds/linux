// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Sigma Designs
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/rawnand.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

/* Offsets relative to chip->base */
#define PBUS_CMD	0
#define PBUS_ADDR	4
#define PBUS_DATA	8

/* Offsets relative to reg_base */
#define NFC_STATUS	0x00
#define NFC_FLASH_CMD	0x04
#define NFC_DEVICE_CFG	0x08
#define NFC_TIMING1	0x0c
#define NFC_TIMING2	0x10
#define NFC_XFER_CFG	0x14
#define NFC_PKT_0_CFG	0x18
#define NFC_PKT_N_CFG	0x1c
#define NFC_BB_CFG	0x20
#define NFC_ADDR_PAGE	0x24
#define NFC_ADDR_OFFSET	0x28
#define NFC_XFER_STATUS	0x2c

/* NFC_STATUS values */
#define CMD_READY	BIT(31)

/* NFC_FLASH_CMD values */
#define NFC_READ	1
#define NFC_WRITE	2

/* NFC_XFER_STATUS values */
#define PAGE_IS_EMPTY	BIT(16)

/* Offsets relative to mem_base */
#define METADATA	0x000
#define ERROR_REPORT	0x1c0

/*
 * Error reports are split in two bytes:
 * byte 0 for the first packet in the page (PKT_0)
 * byte 1 for other packets in the page (PKT_N, for N > 0)
 * ERR_COUNT_PKT_N is the max error count over all but the first packet.
 */
#define ERR_COUNT_PKT_0(v)	(((v) >> 0) & 0x3f)
#define ERR_COUNT_PKT_N(v)	(((v) >> 8) & 0x3f)
#define DECODE_FAIL_PKT_0(v)	(((v) & BIT(7)) == 0)
#define DECODE_FAIL_PKT_N(v)	(((v) & BIT(15)) == 0)

/* Offsets relative to pbus_base */
#define PBUS_CS_CTRL	0x83c
#define PBUS_PAD_MODE	0x8f0

/* PBUS_CS_CTRL values */
#define PBUS_IORDY	BIT(31)

/*
 * PBUS_PAD_MODE values
 * In raw mode, the driver communicates directly with the NAND chips.
 * In NFC mode, the NAND Flash controller manages the communication.
 * We use NFC mode for read and write; raw mode for everything else.
 */
#define MODE_RAW	0
#define MODE_NFC	BIT(31)

#define METADATA_SIZE	4
#define BBM_SIZE	6
#define FIELD_ORDER	15

#define MAX_CS		4

struct tango_nfc {
	struct nand_controller hw;
	void __iomem *reg_base;
	void __iomem *mem_base;
	void __iomem *pbus_base;
	struct tango_chip *chips[MAX_CS];
	struct dma_chan *chan;
	int freq_kHz;
};

#define to_tango_nfc(ptr) container_of(ptr, struct tango_nfc, hw)

struct tango_chip {
	struct nand_chip nand_chip;
	void __iomem *base;
	u32 timing1;
	u32 timing2;
	u32 xfer_cfg;
	u32 pkt_0_cfg;
	u32 pkt_n_cfg;
	u32 bb_cfg;
};

#define to_tango_chip(ptr) container_of(ptr, struct tango_chip, nand_chip)

#define XFER_CFG(cs, page_count, steps, metadata_size)	\
	((cs) << 24 | (page_count) << 16 | (steps) << 8 | (metadata_size))

#define PKT_CFG(size, strength) ((size) << 16 | (strength))

#define BB_CFG(bb_offset, bb_size) ((bb_offset) << 16 | (bb_size))

#define TIMING(t0, t1, t2, t3) ((t0) << 24 | (t1) << 16 | (t2) << 8 | (t3))

static void tango_cmd_ctrl(struct nand_chip *chip, int dat, unsigned int ctrl)
{
	struct tango_chip *tchip = to_tango_chip(chip);

	if (ctrl & NAND_CLE)
		writeb_relaxed(dat, tchip->base + PBUS_CMD);

	if (ctrl & NAND_ALE)
		writeb_relaxed(dat, tchip->base + PBUS_ADDR);
}

static int tango_dev_ready(struct nand_chip *chip)
{
	struct tango_nfc *nfc = to_tango_nfc(chip->controller);

	return readl_relaxed(nfc->pbus_base + PBUS_CS_CTRL) & PBUS_IORDY;
}

static u8 tango_read_byte(struct nand_chip *chip)
{
	struct tango_chip *tchip = to_tango_chip(chip);

	return readb_relaxed(tchip->base + PBUS_DATA);
}

static void tango_read_buf(struct nand_chip *chip, u8 *buf, int len)
{
	struct tango_chip *tchip = to_tango_chip(chip);

	ioread8_rep(tchip->base + PBUS_DATA, buf, len);
}

static void tango_write_buf(struct nand_chip *chip, const u8 *buf, int len)
{
	struct tango_chip *tchip = to_tango_chip(chip);

	iowrite8_rep(tchip->base + PBUS_DATA, buf, len);
}

static void tango_select_chip(struct nand_chip *chip, int idx)
{
	struct tango_nfc *nfc = to_tango_nfc(chip->controller);
	struct tango_chip *tchip = to_tango_chip(chip);

	if (idx < 0)
		return; /* No "chip unselect" function */

	writel_relaxed(tchip->timing1, nfc->reg_base + NFC_TIMING1);
	writel_relaxed(tchip->timing2, nfc->reg_base + NFC_TIMING2);
	writel_relaxed(tchip->xfer_cfg, nfc->reg_base + NFC_XFER_CFG);
	writel_relaxed(tchip->pkt_0_cfg, nfc->reg_base + NFC_PKT_0_CFG);
	writel_relaxed(tchip->pkt_n_cfg, nfc->reg_base + NFC_PKT_N_CFG);
	writel_relaxed(tchip->bb_cfg, nfc->reg_base + NFC_BB_CFG);
}

/*
 * The controller does not check for bitflips in erased pages,
 * therefore software must check instead.
 */
static int check_erased_page(struct nand_chip *chip, u8 *buf)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	u8 *meta = chip->oob_poi + BBM_SIZE;
	u8 *ecc = chip->oob_poi + BBM_SIZE + METADATA_SIZE;
	const int ecc_size = chip->ecc.bytes;
	const int pkt_size = chip->ecc.size;
	int i, res, meta_len, bitflips = 0;

	for (i = 0; i < chip->ecc.steps; ++i) {
		meta_len = i ? 0 : METADATA_SIZE;
		res = nand_check_erased_ecc_chunk(buf, pkt_size, ecc, ecc_size,
						  meta, meta_len,
						  chip->ecc.strength);
		if (res < 0)
			mtd->ecc_stats.failed++;
		else
			mtd->ecc_stats.corrected += res;

		bitflips = max(res, bitflips);
		buf += pkt_size;
		ecc += ecc_size;
	}

	return bitflips;
}

static int decode_error_report(struct nand_chip *chip)
{
	u32 status, res;
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct tango_nfc *nfc = to_tango_nfc(chip->controller);

	status = readl_relaxed(nfc->reg_base + NFC_XFER_STATUS);
	if (status & PAGE_IS_EMPTY)
		return 0;

	res = readl_relaxed(nfc->mem_base + ERROR_REPORT);

	if (DECODE_FAIL_PKT_0(res) || DECODE_FAIL_PKT_N(res))
		return -EBADMSG;

	/* ERR_COUNT_PKT_N is max, not sum, but that's all we have */
	mtd->ecc_stats.corrected +=
		ERR_COUNT_PKT_0(res) + ERR_COUNT_PKT_N(res);

	return max(ERR_COUNT_PKT_0(res), ERR_COUNT_PKT_N(res));
}

static void tango_dma_callback(void *arg)
{
	complete(arg);
}

static int do_dma(struct tango_nfc *nfc, enum dma_data_direction dir, int cmd,
		  const void *buf, int len, int page)
{
	void __iomem *addr = nfc->reg_base + NFC_STATUS;
	struct dma_chan *chan = nfc->chan;
	struct dma_async_tx_descriptor *desc;
	enum dma_transfer_direction tdir;
	struct scatterlist sg;
	struct completion tx_done;
	int err = -EIO;
	u32 res, val;

	sg_init_one(&sg, buf, len);
	if (dma_map_sg(chan->device->dev, &sg, 1, dir) != 1)
		return -EIO;

	tdir = dir == DMA_TO_DEVICE ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM;
	desc = dmaengine_prep_slave_sg(chan, &sg, 1, tdir, DMA_PREP_INTERRUPT);
	if (!desc)
		goto dma_unmap;

	desc->callback = tango_dma_callback;
	desc->callback_param = &tx_done;
	init_completion(&tx_done);

	writel_relaxed(MODE_NFC, nfc->pbus_base + PBUS_PAD_MODE);

	writel_relaxed(page, nfc->reg_base + NFC_ADDR_PAGE);
	writel_relaxed(0, nfc->reg_base + NFC_ADDR_OFFSET);
	writel_relaxed(cmd, nfc->reg_base + NFC_FLASH_CMD);

	dmaengine_submit(desc);
	dma_async_issue_pending(chan);

	res = wait_for_completion_timeout(&tx_done, HZ);
	if (res > 0)
		err = readl_poll_timeout(addr, val, val & CMD_READY, 0, 1000);

	writel_relaxed(MODE_RAW, nfc->pbus_base + PBUS_PAD_MODE);

dma_unmap:
	dma_unmap_sg(chan->device->dev, &sg, 1, dir);

	return err;
}

static int tango_read_page(struct nand_chip *chip, u8 *buf,
			   int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct tango_nfc *nfc = to_tango_nfc(chip->controller);
	int err, res, len = mtd->writesize;

	if (oob_required)
		chip->ecc.read_oob(chip, page);

	err = do_dma(nfc, DMA_FROM_DEVICE, NFC_READ, buf, len, page);
	if (err)
		return err;

	res = decode_error_report(chip);
	if (res < 0) {
		chip->ecc.read_oob_raw(chip, page);
		res = check_erased_page(chip, buf);
	}

	return res;
}

static int tango_write_page(struct nand_chip *chip, const u8 *buf,
			    int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct tango_nfc *nfc = to_tango_nfc(chip->controller);
	int err, status, len = mtd->writesize;

	/* Calling tango_write_oob() would send PAGEPROG twice */
	if (oob_required)
		return -ENOTSUPP;

	writel_relaxed(0xffffffff, nfc->mem_base + METADATA);
	err = do_dma(nfc, DMA_TO_DEVICE, NFC_WRITE, buf, len, page);
	if (err)
		return err;

	status = chip->legacy.waitfunc(chip);
	if (status & NAND_STATUS_FAIL)
		return -EIO;

	return 0;
}

static void aux_read(struct nand_chip *chip, u8 **buf, int len, int *pos)
{
	*pos += len;

	if (!*buf) {
		/* skip over "len" bytes */
		nand_change_read_column_op(chip, *pos, NULL, 0, false);
	} else {
		tango_read_buf(chip, *buf, len);
		*buf += len;
	}
}

static void aux_write(struct nand_chip *chip, const u8 **buf, int len, int *pos)
{
	*pos += len;

	if (!*buf) {
		/* skip over "len" bytes */
		nand_change_write_column_op(chip, *pos, NULL, 0, false);
	} else {
		tango_write_buf(chip, *buf, len);
		*buf += len;
	}
}

/*
 * Physical page layout (not drawn to scale)
 *
 * NB: Bad Block Marker area splits PKT_N in two (N1, N2).
 *
 * +---+-----------------+-------+-----+-----------+-----+----+-------+
 * | M |      PKT_0      | ECC_0 | ... |     N1    | BBM | N2 | ECC_N |
 * +---+-----------------+-------+-----+-----------+-----+----+-------+
 *
 * Logical page layout:
 *
 *       +-----+---+-------+-----+-------+
 * oob = | BBM | M | ECC_0 | ... | ECC_N |
 *       +-----+---+-------+-----+-------+
 *
 *       +-----------------+-----+-----------------+
 * buf = |      PKT_0      | ... |      PKT_N      |
 *       +-----------------+-----+-----------------+
 */
static void raw_read(struct nand_chip *chip, u8 *buf, u8 *oob)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	u8 *oob_orig = oob;
	const int page_size = mtd->writesize;
	const int ecc_size = chip->ecc.bytes;
	const int pkt_size = chip->ecc.size;
	int pos = 0; /* position within physical page */
	int rem = page_size; /* bytes remaining until BBM area */

	if (oob)
		oob += BBM_SIZE;

	aux_read(chip, &oob, METADATA_SIZE, &pos);

	while (rem > pkt_size) {
		aux_read(chip, &buf, pkt_size, &pos);
		aux_read(chip, &oob, ecc_size, &pos);
		rem = page_size - pos;
	}

	aux_read(chip, &buf, rem, &pos);
	aux_read(chip, &oob_orig, BBM_SIZE, &pos);
	aux_read(chip, &buf, pkt_size - rem, &pos);
	aux_read(chip, &oob, ecc_size, &pos);
}

static void raw_write(struct nand_chip *chip, const u8 *buf, const u8 *oob)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	const u8 *oob_orig = oob;
	const int page_size = mtd->writesize;
	const int ecc_size = chip->ecc.bytes;
	const int pkt_size = chip->ecc.size;
	int pos = 0; /* position within physical page */
	int rem = page_size; /* bytes remaining until BBM area */

	if (oob)
		oob += BBM_SIZE;

	aux_write(chip, &oob, METADATA_SIZE, &pos);

	while (rem > pkt_size) {
		aux_write(chip, &buf, pkt_size, &pos);
		aux_write(chip, &oob, ecc_size, &pos);
		rem = page_size - pos;
	}

	aux_write(chip, &buf, rem, &pos);
	aux_write(chip, &oob_orig, BBM_SIZE, &pos);
	aux_write(chip, &buf, pkt_size - rem, &pos);
	aux_write(chip, &oob, ecc_size, &pos);
}

static int tango_read_page_raw(struct nand_chip *chip, u8 *buf,
			       int oob_required, int page)
{
	nand_read_page_op(chip, page, 0, NULL, 0);
	raw_read(chip, buf, chip->oob_poi);
	return 0;
}

static int tango_write_page_raw(struct nand_chip *chip, const u8 *buf,
				int oob_required, int page)
{
	nand_prog_page_begin_op(chip, page, 0, NULL, 0);
	raw_write(chip, buf, chip->oob_poi);
	return nand_prog_page_end_op(chip);
}

static int tango_read_oob(struct nand_chip *chip, int page)
{
	nand_read_page_op(chip, page, 0, NULL, 0);
	raw_read(chip, NULL, chip->oob_poi);
	return 0;
}

static int tango_write_oob(struct nand_chip *chip, int page)
{
	nand_prog_page_begin_op(chip, page, 0, NULL, 0);
	raw_write(chip, NULL, chip->oob_poi);
	return nand_prog_page_end_op(chip);
}

static int oob_ecc(struct mtd_info *mtd, int idx, struct mtd_oob_region *res)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct nand_ecc_ctrl *ecc = &chip->ecc;

	if (idx >= ecc->steps)
		return -ERANGE;

	res->offset = BBM_SIZE + METADATA_SIZE + ecc->bytes * idx;
	res->length = ecc->bytes;

	return 0;
}

static int oob_free(struct mtd_info *mtd, int idx, struct mtd_oob_region *res)
{
	return -ERANGE; /* no free space in spare area */
}

static const struct mtd_ooblayout_ops tango_nand_ooblayout_ops = {
	.ecc	= oob_ecc,
	.free	= oob_free,
};

static u32 to_ticks(int kHz, int ps)
{
	return DIV_ROUND_UP_ULL((u64)kHz * ps, NSEC_PER_SEC);
}

static int tango_set_timings(struct nand_chip *chip, int csline,
			     const struct nand_data_interface *conf)
{
	const struct nand_sdr_timings *sdr = nand_get_sdr_timings(conf);
	struct tango_nfc *nfc = to_tango_nfc(chip->controller);
	struct tango_chip *tchip = to_tango_chip(chip);
	u32 Trdy, Textw, Twc, Twpw, Tacc, Thold, Trpw, Textr;
	int kHz = nfc->freq_kHz;

	if (IS_ERR(sdr))
		return PTR_ERR(sdr);

	if (csline == NAND_DATA_IFACE_CHECK_ONLY)
		return 0;

	Trdy = to_ticks(kHz, sdr->tCEA_max - sdr->tREA_max);
	Textw = to_ticks(kHz, sdr->tWB_max);
	Twc = to_ticks(kHz, sdr->tWC_min);
	Twpw = to_ticks(kHz, sdr->tWC_min - sdr->tWP_min);

	Tacc = to_ticks(kHz, sdr->tREA_max);
	Thold = to_ticks(kHz, sdr->tREH_min);
	Trpw = to_ticks(kHz, sdr->tRC_min - sdr->tREH_min);
	Textr = to_ticks(kHz, sdr->tRHZ_max);

	tchip->timing1 = TIMING(Trdy, Textw, Twc, Twpw);
	tchip->timing2 = TIMING(Tacc, Thold, Trpw, Textr);

	return 0;
}

static int tango_attach_chip(struct nand_chip *chip)
{
	struct nand_ecc_ctrl *ecc = &chip->ecc;

	ecc->mode = NAND_ECC_HW;
	ecc->algo = NAND_ECC_BCH;
	ecc->bytes = DIV_ROUND_UP(ecc->strength * FIELD_ORDER, BITS_PER_BYTE);

	ecc->read_page_raw = tango_read_page_raw;
	ecc->write_page_raw = tango_write_page_raw;
	ecc->read_page = tango_read_page;
	ecc->write_page = tango_write_page;
	ecc->read_oob = tango_read_oob;
	ecc->write_oob = tango_write_oob;

	return 0;
}

static const struct nand_controller_ops tango_controller_ops = {
	.attach_chip = tango_attach_chip,
	.setup_data_interface = tango_set_timings,
};

static int chip_init(struct device *dev, struct device_node *np)
{
	u32 cs;
	int err, res;
	struct mtd_info *mtd;
	struct nand_chip *chip;
	struct tango_chip *tchip;
	struct nand_ecc_ctrl *ecc;
	struct tango_nfc *nfc = dev_get_drvdata(dev);

	tchip = devm_kzalloc(dev, sizeof(*tchip), GFP_KERNEL);
	if (!tchip)
		return -ENOMEM;

	res = of_property_count_u32_elems(np, "reg");
	if (res < 0)
		return res;

	if (res != 1)
		return -ENOTSUPP; /* Multi-CS chips are not supported */

	err = of_property_read_u32_index(np, "reg", 0, &cs);
	if (err)
		return err;

	if (cs >= MAX_CS)
		return -EINVAL;

	chip = &tchip->nand_chip;
	ecc = &chip->ecc;
	mtd = nand_to_mtd(chip);

	chip->legacy.read_byte = tango_read_byte;
	chip->legacy.write_buf = tango_write_buf;
	chip->legacy.read_buf = tango_read_buf;
	chip->legacy.select_chip = tango_select_chip;
	chip->legacy.cmd_ctrl = tango_cmd_ctrl;
	chip->legacy.dev_ready = tango_dev_ready;
	chip->options = NAND_USES_DMA |
			NAND_NO_SUBPAGE_WRITE |
			NAND_WAIT_TCCS;
	chip->controller = &nfc->hw;
	tchip->base = nfc->pbus_base + (cs * 256);

	nand_set_flash_node(chip, np);
	mtd_set_ooblayout(mtd, &tango_nand_ooblayout_ops);
	mtd->dev.parent = dev;

	err = nand_scan(chip, 1);
	if (err)
		return err;

	tchip->xfer_cfg = XFER_CFG(cs, 1, ecc->steps, METADATA_SIZE);
	tchip->pkt_0_cfg = PKT_CFG(ecc->size + METADATA_SIZE, ecc->strength);
	tchip->pkt_n_cfg = PKT_CFG(ecc->size, ecc->strength);
	tchip->bb_cfg = BB_CFG(mtd->writesize, BBM_SIZE);

	err = mtd_device_register(mtd, NULL, 0);
	if (err) {
		nand_cleanup(chip);
		return err;
	}

	nfc->chips[cs] = tchip;

	return 0;
}

static int tango_nand_remove(struct platform_device *pdev)
{
	struct tango_nfc *nfc = platform_get_drvdata(pdev);
	struct nand_chip *chip;
	int cs, ret;

	dma_release_channel(nfc->chan);

	for (cs = 0; cs < MAX_CS; ++cs) {
		if (nfc->chips[cs]) {
			chip = &nfc->chips[cs]->nand_chip;
			ret = mtd_device_unregister(nand_to_mtd(chip));
			WARN_ON(ret);
			nand_cleanup(chip);
		}
	}

	return 0;
}

static int tango_nand_probe(struct platform_device *pdev)
{
	int err;
	struct clk *clk;
	struct resource *res;
	struct tango_nfc *nfc;
	struct device_node *np;

	nfc = devm_kzalloc(&pdev->dev, sizeof(*nfc), GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nfc->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(nfc->reg_base))
		return PTR_ERR(nfc->reg_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	nfc->mem_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(nfc->mem_base))
		return PTR_ERR(nfc->mem_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	nfc->pbus_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(nfc->pbus_base))
		return PTR_ERR(nfc->pbus_base);

	writel_relaxed(MODE_RAW, nfc->pbus_base + PBUS_PAD_MODE);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	nfc->chan = dma_request_chan(&pdev->dev, "rxtx");
	if (IS_ERR(nfc->chan))
		return PTR_ERR(nfc->chan);

	platform_set_drvdata(pdev, nfc);
	nand_controller_init(&nfc->hw);
	nfc->hw.ops = &tango_controller_ops;
	nfc->freq_kHz = clk_get_rate(clk) / 1000;

	for_each_child_of_node(pdev->dev.of_node, np) {
		err = chip_init(&pdev->dev, np);
		if (err) {
			tango_nand_remove(pdev);
			of_node_put(np);
			return err;
		}
	}

	return 0;
}

static const struct of_device_id tango_nand_ids[] = {
	{ .compatible = "sigma,smp8758-nand" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tango_nand_ids);

static struct platform_driver tango_nand_driver = {
	.probe	= tango_nand_probe,
	.remove	= tango_nand_remove,
	.driver	= {
		.name		= "tango-nand",
		.of_match_table	= tango_nand_ids,
	},
};

module_platform_driver(tango_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sigma Designs");
MODULE_DESCRIPTION("Tango4 NAND Flash controller driver");
