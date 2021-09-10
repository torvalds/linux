// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2020 Intel Corporation. */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dmaengine.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/nand.h>

#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/units.h>
#include <asm/unaligned.h>

#define EBU_CLC			0x000
#define EBU_CLC_RST		0x00000000u

#define EBU_ADDR_SEL(n)		(0x020 + (n) * 4)
/* 5 bits 26:22 included for comparison in the ADDR_SELx */
#define EBU_ADDR_MASK(x)	((x) << 4)
#define EBU_ADDR_SEL_REGEN	0x1

#define EBU_BUSCON(n)		(0x060 + (n) * 4)
#define EBU_BUSCON_CMULT_V4	0x1
#define EBU_BUSCON_RECOVC(n)	((n) << 2)
#define EBU_BUSCON_HOLDC(n)	((n) << 4)
#define EBU_BUSCON_WAITRDC(n)	((n) << 6)
#define EBU_BUSCON_WAITWRC(n)	((n) << 8)
#define EBU_BUSCON_BCGEN_CS	0x0
#define EBU_BUSCON_SETUP_EN	BIT(22)
#define EBU_BUSCON_ALEC		0xC000

#define EBU_CON			0x0B0
#define EBU_CON_NANDM_EN	BIT(0)
#define EBU_CON_NANDM_DIS	0x0
#define EBU_CON_CSMUX_E_EN	BIT(1)
#define EBU_CON_ALE_P_LOW	BIT(2)
#define EBU_CON_CLE_P_LOW	BIT(3)
#define EBU_CON_CS_P_LOW	BIT(4)
#define EBU_CON_SE_P_LOW	BIT(5)
#define EBU_CON_WP_P_LOW	BIT(6)
#define EBU_CON_PRE_P_LOW	BIT(7)
#define EBU_CON_IN_CS_S(n)	((n) << 8)
#define EBU_CON_OUT_CS_S(n)	((n) << 10)
#define EBU_CON_LAT_EN_CS_P	((0x3D) << 18)

#define EBU_WAIT		0x0B4
#define EBU_WAIT_RDBY		BIT(0)
#define EBU_WAIT_WR_C		BIT(3)

#define HSNAND_CTL1		0x110
#define HSNAND_CTL1_ADDR_SHIFT	24

#define HSNAND_CTL2		0x114
#define HSNAND_CTL2_ADDR_SHIFT	8
#define HSNAND_CTL2_CYC_N_V5	(0x2 << 16)

#define HSNAND_INT_MSK_CTL	0x124
#define HSNAND_INT_MSK_CTL_WR_C	BIT(4)

#define HSNAND_INT_STA		0x128
#define HSNAND_INT_STA_WR_C	BIT(4)

#define HSNAND_CTL		0x130
#define HSNAND_CTL_ENABLE_ECC	BIT(0)
#define HSNAND_CTL_GO		BIT(2)
#define HSNAND_CTL_CE_SEL_CS(n)	BIT(3 + (n))
#define HSNAND_CTL_RW_READ	0x0
#define HSNAND_CTL_RW_WRITE	BIT(10)
#define HSNAND_CTL_ECC_OFF_V8TH	BIT(11)
#define HSNAND_CTL_CKFF_EN	0x0
#define HSNAND_CTL_MSG_EN	BIT(17)

#define HSNAND_PARA0		0x13c
#define HSNAND_PARA0_PAGE_V8192	0x3
#define HSNAND_PARA0_PIB_V256	(0x3 << 4)
#define HSNAND_PARA0_BYP_EN_NP	0x0
#define HSNAND_PARA0_BYP_DEC_NP	0x0
#define HSNAND_PARA0_TYPE_ONFI	BIT(18)
#define HSNAND_PARA0_ADEP_EN	BIT(21)

#define HSNAND_CMSG_0		0x150
#define HSNAND_CMSG_1		0x154

#define HSNAND_ALE_OFFS		BIT(2)
#define HSNAND_CLE_OFFS		BIT(3)
#define HSNAND_CS_OFFS		BIT(4)

#define HSNAND_ECC_OFFSET	0x008

#define NAND_DATA_IFACE_CHECK_ONLY	-1

#define MAX_CS	2

#define USEC_PER_SEC	1000000L

struct ebu_nand_cs {
	void __iomem *chipaddr;
	dma_addr_t nand_pa;
	u32 addr_sel;
};

struct ebu_nand_controller {
	struct nand_controller controller;
	struct nand_chip chip;
	struct device *dev;
	void __iomem *ebu;
	void __iomem *hsnand;
	struct dma_chan *dma_tx;
	struct dma_chan *dma_rx;
	struct completion dma_access_complete;
	unsigned long clk_rate;
	struct clk *clk;
	u32 nd_para0;
	u8 cs_num;
	struct ebu_nand_cs cs[MAX_CS];
};

static inline struct ebu_nand_controller *nand_to_ebu(struct nand_chip *chip)
{
	return container_of(chip, struct ebu_nand_controller, chip);
}

static int ebu_nand_waitrdy(struct nand_chip *chip, int timeout_ms)
{
	struct ebu_nand_controller *ctrl = nand_to_ebu(chip);
	u32 status;

	return readl_poll_timeout(ctrl->ebu + EBU_WAIT, status,
				  (status & EBU_WAIT_RDBY) ||
				  (status & EBU_WAIT_WR_C), 20, timeout_ms);
}

static u8 ebu_nand_readb(struct nand_chip *chip)
{
	struct ebu_nand_controller *ebu_host = nand_get_controller_data(chip);
	u8 cs_num = ebu_host->cs_num;
	u8 val;

	val = readb(ebu_host->cs[cs_num].chipaddr + HSNAND_CS_OFFS);
	ebu_nand_waitrdy(chip, 1000);
	return val;
}

static void ebu_nand_writeb(struct nand_chip *chip, u32 offset, u8 value)
{
	struct ebu_nand_controller *ebu_host = nand_get_controller_data(chip);
	u8 cs_num = ebu_host->cs_num;

	writeb(value, ebu_host->cs[cs_num].chipaddr + offset);
	ebu_nand_waitrdy(chip, 1000);
}

static void ebu_read_buf(struct nand_chip *chip, u_char *buf, unsigned int len)
{
	int i;

	for (i = 0; i < len; i++)
		buf[i] = ebu_nand_readb(chip);
}

static void ebu_write_buf(struct nand_chip *chip, const u_char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		ebu_nand_writeb(chip, HSNAND_CS_OFFS, buf[i]);
}

static void ebu_nand_disable(struct nand_chip *chip)
{
	struct ebu_nand_controller *ebu_host = nand_get_controller_data(chip);

	writel(0, ebu_host->ebu + EBU_CON);
}

static void ebu_select_chip(struct nand_chip *chip)
{
	struct ebu_nand_controller *ebu_host = nand_get_controller_data(chip);
	void __iomem *nand_con = ebu_host->ebu + EBU_CON;
	u32 cs = ebu_host->cs_num;

	writel(EBU_CON_NANDM_EN | EBU_CON_CSMUX_E_EN | EBU_CON_CS_P_LOW |
	       EBU_CON_SE_P_LOW | EBU_CON_WP_P_LOW | EBU_CON_PRE_P_LOW |
	       EBU_CON_IN_CS_S(cs) | EBU_CON_OUT_CS_S(cs) |
	       EBU_CON_LAT_EN_CS_P, nand_con);
}

static int ebu_nand_set_timings(struct nand_chip *chip, int csline,
				const struct nand_interface_config *conf)
{
	struct ebu_nand_controller *ctrl = nand_to_ebu(chip);
	unsigned int rate = clk_get_rate(ctrl->clk) / HZ_PER_MHZ;
	unsigned int period = DIV_ROUND_UP(USEC_PER_SEC, rate);
	const struct nand_sdr_timings *timings;
	u32 trecov, thold, twrwait, trdwait;
	u32 reg = 0;

	timings = nand_get_sdr_timings(conf);
	if (IS_ERR(timings))
		return PTR_ERR(timings);

	if (csline == NAND_DATA_IFACE_CHECK_ONLY)
		return 0;

	trecov = DIV_ROUND_UP(max(timings->tREA_max, timings->tREH_min),
			      period);
	reg |= EBU_BUSCON_RECOVC(trecov);

	thold = DIV_ROUND_UP(max(timings->tDH_min, timings->tDS_min), period);
	reg |= EBU_BUSCON_HOLDC(thold);

	trdwait = DIV_ROUND_UP(max(timings->tRC_min, timings->tREH_min),
			       period);
	reg |= EBU_BUSCON_WAITRDC(trdwait);

	twrwait = DIV_ROUND_UP(max(timings->tWC_min, timings->tWH_min), period);
	reg |= EBU_BUSCON_WAITWRC(twrwait);

	reg |= EBU_BUSCON_CMULT_V4 | EBU_BUSCON_BCGEN_CS | EBU_BUSCON_ALEC |
		EBU_BUSCON_SETUP_EN;

	writel(reg, ctrl->ebu + EBU_BUSCON(ctrl->cs_num));

	return 0;
}

static int ebu_nand_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oobregion->offset = HSNAND_ECC_OFFSET;
	oobregion->length = chip->ecc.total;

	return 0;
}

static int ebu_nand_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oobregion->offset = chip->ecc.total + HSNAND_ECC_OFFSET;
	oobregion->length = mtd->oobsize - oobregion->offset;

	return 0;
}

static const struct mtd_ooblayout_ops ebu_nand_ooblayout_ops = {
	.ecc = ebu_nand_ooblayout_ecc,
	.free = ebu_nand_ooblayout_free,
};

static void ebu_dma_rx_callback(void *cookie)
{
	struct ebu_nand_controller *ebu_host = cookie;

	dmaengine_terminate_async(ebu_host->dma_rx);

	complete(&ebu_host->dma_access_complete);
}

static void ebu_dma_tx_callback(void *cookie)
{
	struct ebu_nand_controller *ebu_host = cookie;

	dmaengine_terminate_async(ebu_host->dma_tx);

	complete(&ebu_host->dma_access_complete);
}

static int ebu_dma_start(struct ebu_nand_controller *ebu_host, u32 dir,
			 const u8 *buf, u32 len)
{
	struct dma_async_tx_descriptor *tx;
	struct completion *dma_completion;
	dma_async_tx_callback callback;
	struct dma_chan *chan;
	dma_cookie_t cookie;
	unsigned long flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	dma_addr_t buf_dma;
	int ret;
	u32 timeout;

	if (dir == DMA_DEV_TO_MEM) {
		chan = ebu_host->dma_rx;
		dma_completion = &ebu_host->dma_access_complete;
		callback = ebu_dma_rx_callback;
	} else {
		chan = ebu_host->dma_tx;
		dma_completion = &ebu_host->dma_access_complete;
		callback = ebu_dma_tx_callback;
	}

	buf_dma = dma_map_single(chan->device->dev, (void *)buf, len, dir);
	if (dma_mapping_error(chan->device->dev, buf_dma)) {
		dev_err(ebu_host->dev, "Failed to map DMA buffer\n");
		ret = -EIO;
		goto err_unmap;
	}

	tx = dmaengine_prep_slave_single(chan, buf_dma, len, dir, flags);
	if (!tx) {
		ret = -ENXIO;
		goto err_unmap;
	}

	tx->callback = callback;
	tx->callback_param = ebu_host;
	cookie = tx->tx_submit(tx);

	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(ebu_host->dev, "dma_submit_error %d\n", cookie);
		ret = -EIO;
		goto err_unmap;
	}

	init_completion(dma_completion);
	dma_async_issue_pending(chan);

	/* Wait DMA to finish the data transfer.*/
	timeout = wait_for_completion_timeout(dma_completion, msecs_to_jiffies(1000));
	if (!timeout) {
		dev_err(ebu_host->dev, "I/O Error in DMA RX (status %d)\n",
			dmaengine_tx_status(chan, cookie, NULL));
		dmaengine_terminate_sync(chan);
		ret = -ETIMEDOUT;
		goto err_unmap;
	}

	return 0;

err_unmap:
	dma_unmap_single(ebu_host->dev, buf_dma, len, dir);

	return ret;
}

static void ebu_nand_trigger(struct ebu_nand_controller *ebu_host,
			     int page, u32 cmd)
{
	unsigned int val;

	val = cmd | (page & 0xFF) << HSNAND_CTL1_ADDR_SHIFT;
	writel(val, ebu_host->hsnand + HSNAND_CTL1);
	val = (page & 0xFFFF00) >> 8 | HSNAND_CTL2_CYC_N_V5;
	writel(val, ebu_host->hsnand + HSNAND_CTL2);

	writel(ebu_host->nd_para0, ebu_host->hsnand + HSNAND_PARA0);

	/* clear first, will update later */
	writel(0xFFFFFFFF, ebu_host->hsnand + HSNAND_CMSG_0);
	writel(0xFFFFFFFF, ebu_host->hsnand + HSNAND_CMSG_1);

	writel(HSNAND_INT_MSK_CTL_WR_C,
	       ebu_host->hsnand + HSNAND_INT_MSK_CTL);

	if (!cmd)
		val = HSNAND_CTL_RW_READ;
	else
		val = HSNAND_CTL_RW_WRITE;

	writel(HSNAND_CTL_MSG_EN | HSNAND_CTL_CKFF_EN |
	       HSNAND_CTL_ECC_OFF_V8TH | HSNAND_CTL_CE_SEL_CS(ebu_host->cs_num) |
	       HSNAND_CTL_ENABLE_ECC | HSNAND_CTL_GO | val,
	       ebu_host->hsnand + HSNAND_CTL);
}

static int ebu_nand_read_page_hwecc(struct nand_chip *chip, u8 *buf,
				    int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct ebu_nand_controller *ebu_host = nand_get_controller_data(chip);
	int ret, reg_data;

	ebu_nand_trigger(ebu_host, page, NAND_CMD_READ0);

	ret = ebu_dma_start(ebu_host, DMA_DEV_TO_MEM, buf, mtd->writesize);
	if (ret)
		return ret;

	if (oob_required)
		chip->ecc.read_oob(chip, page);

	reg_data = readl(ebu_host->hsnand + HSNAND_CTL);
	reg_data &= ~HSNAND_CTL_GO;
	writel(reg_data, ebu_host->hsnand + HSNAND_CTL);

	return 0;
}

static int ebu_nand_write_page_hwecc(struct nand_chip *chip, const u8 *buf,
				     int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct ebu_nand_controller *ebu_host = nand_get_controller_data(chip);
	void __iomem *int_sta = ebu_host->hsnand + HSNAND_INT_STA;
	int reg_data, ret, val;
	u32 reg;

	ebu_nand_trigger(ebu_host, page, NAND_CMD_SEQIN);

	ret = ebu_dma_start(ebu_host, DMA_MEM_TO_DEV, buf, mtd->writesize);
	if (ret)
		return ret;

	if (oob_required) {
		reg = get_unaligned_le32(chip->oob_poi);
		writel(reg, ebu_host->hsnand + HSNAND_CMSG_0);

		reg = get_unaligned_le32(chip->oob_poi + 4);
		writel(reg, ebu_host->hsnand + HSNAND_CMSG_1);
	}

	ret = readl_poll_timeout_atomic(int_sta, val, !(val & HSNAND_INT_STA_WR_C),
					10, 1000);
	if (ret)
		return ret;

	reg_data = readl(ebu_host->hsnand + HSNAND_CTL);
	reg_data &= ~HSNAND_CTL_GO;
	writel(reg_data, ebu_host->hsnand + HSNAND_CTL);

	return 0;
}

static const u8 ecc_strength[] = { 1, 1, 4, 8, 24, 32, 40, 60, };

static int ebu_nand_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct ebu_nand_controller *ebu_host = nand_get_controller_data(chip);
	u32 ecc_steps, ecc_bytes, ecc_total, pagesize, pg_per_blk;
	u32 ecc_strength_ds = chip->ecc.strength;
	u32 ecc_size = chip->ecc.size;
	u32 writesize = mtd->writesize;
	u32 blocksize = mtd->erasesize;
	int bch_algo, start, val;

	/* Default to an ECC size of 512 */
	if (!chip->ecc.size)
		chip->ecc.size = 512;

	switch (ecc_size) {
	case 512:
		start = 1;
		if (!ecc_strength_ds)
			ecc_strength_ds = 4;
		break;
	case 1024:
		start = 4;
		if (!ecc_strength_ds)
			ecc_strength_ds = 32;
		break;
	default:
		return -EINVAL;
	}

	/* BCH ECC algorithm Settings for number of bits per 512B/1024B */
	bch_algo = round_up(start + 1, 4);
	for (val = start; val < bch_algo; val++) {
		if (ecc_strength_ds == ecc_strength[val])
			break;
	}
	if (val == bch_algo)
		return -EINVAL;

	if (ecc_strength_ds == 8)
		ecc_bytes = 14;
	else
		ecc_bytes = DIV_ROUND_UP(ecc_strength_ds * fls(8 * ecc_size), 8);

	ecc_steps = writesize / ecc_size;
	ecc_total = ecc_steps * ecc_bytes;
	if ((ecc_total + 8) > mtd->oobsize)
		return -ERANGE;

	chip->ecc.total = ecc_total;
	pagesize = fls(writesize >> 11);
	if (pagesize > HSNAND_PARA0_PAGE_V8192)
		return -ERANGE;

	pg_per_blk = fls((blocksize / writesize) >> 6) / 8;
	if (pg_per_blk > HSNAND_PARA0_PIB_V256)
		return -ERANGE;

	ebu_host->nd_para0 = pagesize | pg_per_blk | HSNAND_PARA0_BYP_EN_NP |
			     HSNAND_PARA0_BYP_DEC_NP | HSNAND_PARA0_ADEP_EN |
			     HSNAND_PARA0_TYPE_ONFI | (val << 29);

	mtd_set_ooblayout(mtd, &ebu_nand_ooblayout_ops);
	chip->ecc.read_page = ebu_nand_read_page_hwecc;
	chip->ecc.write_page = ebu_nand_write_page_hwecc;

	return 0;
}

static int ebu_nand_exec_op(struct nand_chip *chip,
			    const struct nand_operation *op, bool check_only)
{
	const struct nand_op_instr *instr = NULL;
	unsigned int op_id;
	int i, timeout_ms, ret = 0;

	if (check_only)
		return 0;

	ebu_select_chip(chip);
	for (op_id = 0; op_id < op->ninstrs; op_id++) {
		instr = &op->instrs[op_id];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			ebu_nand_writeb(chip, HSNAND_CLE_OFFS | HSNAND_CS_OFFS,
					instr->ctx.cmd.opcode);
			break;

		case NAND_OP_ADDR_INSTR:
			for (i = 0; i < instr->ctx.addr.naddrs; i++)
				ebu_nand_writeb(chip,
						HSNAND_ALE_OFFS | HSNAND_CS_OFFS,
						instr->ctx.addr.addrs[i]);
			break;

		case NAND_OP_DATA_IN_INSTR:
			ebu_read_buf(chip, instr->ctx.data.buf.in,
				     instr->ctx.data.len);
			break;

		case NAND_OP_DATA_OUT_INSTR:
			ebu_write_buf(chip, instr->ctx.data.buf.out,
				      instr->ctx.data.len);
			break;

		case NAND_OP_WAITRDY_INSTR:
			timeout_ms = instr->ctx.waitrdy.timeout_ms * 1000;
			ret = ebu_nand_waitrdy(chip, timeout_ms);
			break;
		}
	}

	return ret;
}

static const struct nand_controller_ops ebu_nand_controller_ops = {
	.attach_chip = ebu_nand_attach_chip,
	.setup_interface = ebu_nand_set_timings,
	.exec_op = ebu_nand_exec_op,
};

static void ebu_dma_cleanup(struct ebu_nand_controller *ebu_host)
{
	if (ebu_host->dma_rx)
		dma_release_channel(ebu_host->dma_rx);

	if (ebu_host->dma_tx)
		dma_release_channel(ebu_host->dma_tx);
}

static int ebu_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ebu_nand_controller *ebu_host;
	struct nand_chip *nand;
	struct mtd_info *mtd;
	struct resource *res;
	char *resname;
	int ret;
	u32 cs;

	ebu_host = devm_kzalloc(dev, sizeof(*ebu_host), GFP_KERNEL);
	if (!ebu_host)
		return -ENOMEM;

	ebu_host->dev = dev;
	nand_controller_init(&ebu_host->controller);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ebunand");
	ebu_host->ebu = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ebu_host->ebu))
		return PTR_ERR(ebu_host->ebu);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hsnand");
	ebu_host->hsnand = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ebu_host->hsnand))
		return PTR_ERR(ebu_host->hsnand);

	ret = device_property_read_u32(dev, "reg", &cs);
	if (ret) {
		dev_err(dev, "failed to get chip select: %d\n", ret);
		return ret;
	}
	ebu_host->cs_num = cs;

	resname = devm_kasprintf(dev, GFP_KERNEL, "nand_cs%d", cs);
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, resname);
	ebu_host->cs[cs].chipaddr = devm_ioremap_resource(dev, res);
	ebu_host->cs[cs].nand_pa = res->start;
	if (IS_ERR(ebu_host->cs[cs].chipaddr))
		return PTR_ERR(ebu_host->cs[cs].chipaddr);

	ebu_host->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ebu_host->clk))
		return dev_err_probe(dev, PTR_ERR(ebu_host->clk),
				     "failed to get clock\n");

	ret = clk_prepare_enable(ebu_host->clk);
	if (ret) {
		dev_err(dev, "failed to enable clock: %d\n", ret);
		return ret;
	}
	ebu_host->clk_rate = clk_get_rate(ebu_host->clk);

	ebu_host->dma_tx = dma_request_chan(dev, "tx");
	if (IS_ERR(ebu_host->dma_tx)) {
		ret = dev_err_probe(dev, PTR_ERR(ebu_host->dma_tx),
				    "failed to request DMA tx chan!.\n");
		goto err_disable_unprepare_clk;
	}

	ebu_host->dma_rx = dma_request_chan(dev, "rx");
	if (IS_ERR(ebu_host->dma_rx)) {
		ret = dev_err_probe(dev, PTR_ERR(ebu_host->dma_rx),
				    "failed to request DMA rx chan!.\n");
		ebu_host->dma_rx = NULL;
		goto err_cleanup_dma;
	}

	resname = devm_kasprintf(dev, GFP_KERNEL, "addr_sel%d", cs);
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, resname);
	if (!res) {
		ret = -EINVAL;
		goto err_cleanup_dma;
	}
	ebu_host->cs[cs].addr_sel = res->start;
	writel(ebu_host->cs[cs].addr_sel | EBU_ADDR_MASK(5) | EBU_ADDR_SEL_REGEN,
	       ebu_host->ebu + EBU_ADDR_SEL(cs));

	nand_set_flash_node(&ebu_host->chip, dev->of_node);

	mtd = nand_to_mtd(&ebu_host->chip);
	if (!mtd->name) {
		dev_err(ebu_host->dev, "NAND label property is mandatory\n");
		ret = -EINVAL;
		goto err_cleanup_dma;
	}

	mtd->dev.parent = dev;
	ebu_host->dev = dev;

	platform_set_drvdata(pdev, ebu_host);
	nand_set_controller_data(&ebu_host->chip, ebu_host);

	nand = &ebu_host->chip;
	nand->controller = &ebu_host->controller;
	nand->controller->ops = &ebu_nand_controller_ops;

	/* Scan to find existence of the device */
	ret = nand_scan(&ebu_host->chip, 1);
	if (ret)
		goto err_cleanup_dma;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret)
		goto err_clean_nand;

	return 0;

err_clean_nand:
	nand_cleanup(&ebu_host->chip);
err_cleanup_dma:
	ebu_dma_cleanup(ebu_host);
err_disable_unprepare_clk:
	clk_disable_unprepare(ebu_host->clk);

	return ret;
}

static int ebu_nand_remove(struct platform_device *pdev)
{
	struct ebu_nand_controller *ebu_host = platform_get_drvdata(pdev);
	int ret;

	ret = mtd_device_unregister(nand_to_mtd(&ebu_host->chip));
	WARN_ON(ret);
	nand_cleanup(&ebu_host->chip);
	ebu_nand_disable(&ebu_host->chip);
	ebu_dma_cleanup(ebu_host);
	clk_disable_unprepare(ebu_host->clk);

	return 0;
}

static const struct of_device_id ebu_nand_match[] = {
	{ .compatible = "intel,nand-controller" },
	{ .compatible = "intel,lgm-ebunand" },
	{}
};
MODULE_DEVICE_TABLE(of, ebu_nand_match);

static struct platform_driver ebu_nand_driver = {
	.probe = ebu_nand_probe,
	.remove = ebu_nand_remove,
	.driver = {
		.name = "intel-nand-controller",
		.of_match_table = ebu_nand_match,
	},

};
module_platform_driver(ebu_nand_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Vadivel Murugan R <vadivel.muruganx.ramuthevar@intel.com>");
MODULE_DESCRIPTION("Intel's LGM External Bus NAND Controller driver");
