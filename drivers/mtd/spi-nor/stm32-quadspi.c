/*
 * Driver for stm32 quadspi controller
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author(s): Ludovic Barre author <ludovic.barre@st.com>.
 *
 * License terms: GPL V2.0.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * This program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/spi-nor.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sizes.h>

#define QUADSPI_CR		0x00
#define CR_EN			BIT(0)
#define CR_ABORT		BIT(1)
#define CR_DMAEN		BIT(2)
#define CR_TCEN			BIT(3)
#define CR_SSHIFT		BIT(4)
#define CR_DFM			BIT(6)
#define CR_FSEL			BIT(7)
#define CR_FTHRES_SHIFT		8
#define CR_FTHRES_MASK		GENMASK(12, 8)
#define CR_FTHRES(n)		(((n) << CR_FTHRES_SHIFT) & CR_FTHRES_MASK)
#define CR_TEIE			BIT(16)
#define CR_TCIE			BIT(17)
#define CR_FTIE			BIT(18)
#define CR_SMIE			BIT(19)
#define CR_TOIE			BIT(20)
#define CR_PRESC_SHIFT		24
#define CR_PRESC_MASK		GENMASK(31, 24)
#define CR_PRESC(n)		(((n) << CR_PRESC_SHIFT) & CR_PRESC_MASK)

#define QUADSPI_DCR		0x04
#define DCR_CSHT_SHIFT		8
#define DCR_CSHT_MASK		GENMASK(10, 8)
#define DCR_CSHT(n)		(((n) << DCR_CSHT_SHIFT) & DCR_CSHT_MASK)
#define DCR_FSIZE_SHIFT		16
#define DCR_FSIZE_MASK		GENMASK(20, 16)
#define DCR_FSIZE(n)		(((n) << DCR_FSIZE_SHIFT) & DCR_FSIZE_MASK)

#define QUADSPI_SR		0x08
#define SR_TEF			BIT(0)
#define SR_TCF			BIT(1)
#define SR_FTF			BIT(2)
#define SR_SMF			BIT(3)
#define SR_TOF			BIT(4)
#define SR_BUSY			BIT(5)
#define SR_FLEVEL_SHIFT		8
#define SR_FLEVEL_MASK		GENMASK(13, 8)

#define QUADSPI_FCR		0x0c
#define FCR_CTCF		BIT(1)

#define QUADSPI_DLR		0x10

#define QUADSPI_CCR		0x14
#define CCR_INST_SHIFT		0
#define CCR_INST_MASK		GENMASK(7, 0)
#define CCR_INST(n)		(((n) << CCR_INST_SHIFT) & CCR_INST_MASK)
#define CCR_IMODE_NONE		(0U << 8)
#define CCR_IMODE_1		(1U << 8)
#define CCR_IMODE_2		(2U << 8)
#define CCR_IMODE_4		(3U << 8)
#define CCR_ADMODE_NONE		(0U << 10)
#define CCR_ADMODE_1		(1U << 10)
#define CCR_ADMODE_2		(2U << 10)
#define CCR_ADMODE_4		(3U << 10)
#define CCR_ADSIZE_SHIFT	12
#define CCR_ADSIZE_MASK		GENMASK(13, 12)
#define CCR_ADSIZE(n)		(((n) << CCR_ADSIZE_SHIFT) & CCR_ADSIZE_MASK)
#define CCR_ABMODE_NONE		(0U << 14)
#define CCR_ABMODE_1		(1U << 14)
#define CCR_ABMODE_2		(2U << 14)
#define CCR_ABMODE_4		(3U << 14)
#define CCR_ABSIZE_8		(0U << 16)
#define CCR_ABSIZE_16		(1U << 16)
#define CCR_ABSIZE_24		(2U << 16)
#define CCR_ABSIZE_32		(3U << 16)
#define CCR_DCYC_SHIFT		18
#define CCR_DCYC_MASK		GENMASK(22, 18)
#define CCR_DCYC(n)		(((n) << CCR_DCYC_SHIFT) & CCR_DCYC_MASK)
#define CCR_DMODE_NONE		(0U << 24)
#define CCR_DMODE_1		(1U << 24)
#define CCR_DMODE_2		(2U << 24)
#define CCR_DMODE_4		(3U << 24)
#define CCR_FMODE_INDW		(0U << 26)
#define CCR_FMODE_INDR		(1U << 26)
#define CCR_FMODE_APM		(2U << 26)
#define CCR_FMODE_MM		(3U << 26)

#define QUADSPI_AR		0x18
#define QUADSPI_ABR		0x1c
#define QUADSPI_DR		0x20
#define QUADSPI_PSMKR		0x24
#define QUADSPI_PSMAR		0x28
#define QUADSPI_PIR		0x2c
#define QUADSPI_LPTR		0x30
#define LPTR_DFT_TIMEOUT	0x10

#define FSIZE_VAL(size)		(__fls(size) - 1)

#define STM32_MAX_MMAP_SZ	SZ_256M
#define STM32_MAX_NORCHIP	2

#define STM32_QSPI_FIFO_SZ	32
#define STM32_QSPI_FIFO_TIMEOUT_US 30000
#define STM32_QSPI_BUSY_TIMEOUT_US 100000

struct stm32_qspi_flash {
	struct spi_nor nor;
	struct stm32_qspi *qspi;
	u32 cs;
	u32 fsize;
	u32 presc;
	u32 read_mode;
	bool registered;
	u32 prefetch_limit;
};

struct stm32_qspi {
	struct device *dev;
	void __iomem *io_base;
	void __iomem *mm_base;
	resource_size_t mm_size;
	u32 nor_num;
	struct clk *clk;
	u32 clk_rate;
	struct stm32_qspi_flash flash[STM32_MAX_NORCHIP];
	struct completion cmd_completion;

	/*
	 * to protect device configuration, could be different between
	 * 2 flash access (bk1, bk2)
	 */
	struct mutex lock;
};

struct stm32_qspi_cmd {
	u8 addr_width;
	u8 dummy;
	bool tx_data;
	u8 opcode;
	u32 framemode;
	u32 qspimode;
	u32 addr;
	size_t len;
	void *buf;
};

static int stm32_qspi_wait_cmd(struct stm32_qspi *qspi)
{
	u32 cr;
	int err = 0;

	if (readl_relaxed(qspi->io_base + QUADSPI_SR) & SR_TCF)
		return 0;

	reinit_completion(&qspi->cmd_completion);
	cr = readl_relaxed(qspi->io_base + QUADSPI_CR);
	writel_relaxed(cr | CR_TCIE, qspi->io_base + QUADSPI_CR);

	if (!wait_for_completion_interruptible_timeout(&qspi->cmd_completion,
						       msecs_to_jiffies(1000)))
		err = -ETIMEDOUT;

	writel_relaxed(cr, qspi->io_base + QUADSPI_CR);
	return err;
}

static int stm32_qspi_wait_nobusy(struct stm32_qspi *qspi)
{
	u32 sr;

	return readl_relaxed_poll_timeout(qspi->io_base + QUADSPI_SR, sr,
					  !(sr & SR_BUSY), 10,
					  STM32_QSPI_BUSY_TIMEOUT_US);
}

static void stm32_qspi_set_framemode(struct spi_nor *nor,
				     struct stm32_qspi_cmd *cmd, bool read)
{
	u32 dmode = CCR_DMODE_1;

	cmd->framemode = CCR_IMODE_1;

	if (read) {
		switch (nor->read_proto) {
		default:
		case SNOR_PROTO_1_1_1:
			dmode = CCR_DMODE_1;
			break;
		case SNOR_PROTO_1_1_2:
			dmode = CCR_DMODE_2;
			break;
		case SNOR_PROTO_1_1_4:
			dmode = CCR_DMODE_4;
			break;
		}
	}

	cmd->framemode |= cmd->tx_data ? dmode : 0;
	cmd->framemode |= cmd->addr_width ? CCR_ADMODE_1 : 0;
}

static void stm32_qspi_read_fifo(u8 *val, void __iomem *addr)
{
	*val = readb_relaxed(addr);
}

static void stm32_qspi_write_fifo(u8 *val, void __iomem *addr)
{
	writeb_relaxed(*val, addr);
}

static int stm32_qspi_tx_poll(struct stm32_qspi *qspi,
			      const struct stm32_qspi_cmd *cmd)
{
	void (*tx_fifo)(u8 *, void __iomem *);
	u32 len = cmd->len, sr;
	u8 *buf = cmd->buf;
	int ret;

	if (cmd->qspimode == CCR_FMODE_INDW)
		tx_fifo = stm32_qspi_write_fifo;
	else
		tx_fifo = stm32_qspi_read_fifo;

	while (len--) {
		ret = readl_relaxed_poll_timeout(qspi->io_base + QUADSPI_SR,
						 sr, (sr & SR_FTF), 10,
						 STM32_QSPI_FIFO_TIMEOUT_US);
		if (ret) {
			dev_err(qspi->dev, "fifo timeout (stat:%#x)\n", sr);
			return ret;
		}
		tx_fifo(buf++, qspi->io_base + QUADSPI_DR);
	}

	return 0;
}

static int stm32_qspi_tx_mm(struct stm32_qspi *qspi,
			    const struct stm32_qspi_cmd *cmd)
{
	memcpy_fromio(cmd->buf, qspi->mm_base + cmd->addr, cmd->len);
	return 0;
}

static int stm32_qspi_tx(struct stm32_qspi *qspi,
			 const struct stm32_qspi_cmd *cmd)
{
	if (!cmd->tx_data)
		return 0;

	if (cmd->qspimode == CCR_FMODE_MM)
		return stm32_qspi_tx_mm(qspi, cmd);

	return stm32_qspi_tx_poll(qspi, cmd);
}

static int stm32_qspi_send(struct stm32_qspi_flash *flash,
			   const struct stm32_qspi_cmd *cmd)
{
	struct stm32_qspi *qspi = flash->qspi;
	u32 ccr, dcr, cr;
	u32 last_byte;
	int err;

	err = stm32_qspi_wait_nobusy(qspi);
	if (err)
		goto abort;

	dcr = readl_relaxed(qspi->io_base + QUADSPI_DCR) & ~DCR_FSIZE_MASK;
	dcr |= DCR_FSIZE(flash->fsize);
	writel_relaxed(dcr, qspi->io_base + QUADSPI_DCR);

	cr = readl_relaxed(qspi->io_base + QUADSPI_CR);
	cr &= ~CR_PRESC_MASK & ~CR_FSEL;
	cr |= CR_PRESC(flash->presc);
	cr |= flash->cs ? CR_FSEL : 0;
	writel_relaxed(cr, qspi->io_base + QUADSPI_CR);

	if (cmd->tx_data)
		writel_relaxed(cmd->len - 1, qspi->io_base + QUADSPI_DLR);

	ccr = cmd->framemode | cmd->qspimode;

	if (cmd->dummy)
		ccr |= CCR_DCYC(cmd->dummy);

	if (cmd->addr_width)
		ccr |= CCR_ADSIZE(cmd->addr_width - 1);

	ccr |= CCR_INST(cmd->opcode);
	writel_relaxed(ccr, qspi->io_base + QUADSPI_CCR);

	if (cmd->addr_width && cmd->qspimode != CCR_FMODE_MM)
		writel_relaxed(cmd->addr, qspi->io_base + QUADSPI_AR);

	err = stm32_qspi_tx(qspi, cmd);
	if (err)
		goto abort;

	if (cmd->qspimode != CCR_FMODE_MM) {
		err = stm32_qspi_wait_cmd(qspi);
		if (err)
			goto abort;
		writel_relaxed(FCR_CTCF, qspi->io_base + QUADSPI_FCR);
	} else {
		last_byte = cmd->addr + cmd->len;
		if (last_byte > flash->prefetch_limit)
			goto abort;
	}

	return err;

abort:
	cr = readl_relaxed(qspi->io_base + QUADSPI_CR) | CR_ABORT;
	writel_relaxed(cr, qspi->io_base + QUADSPI_CR);

	if (err)
		dev_err(qspi->dev, "%s abort err:%d\n", __func__, err);

	return err;
}

static int stm32_qspi_read_reg(struct spi_nor *nor,
			       u8 opcode, u8 *buf, int len)
{
	struct stm32_qspi_flash *flash = nor->priv;
	struct device *dev = flash->qspi->dev;
	struct stm32_qspi_cmd cmd;

	dev_dbg(dev, "read_reg: cmd:%#.2x buf:%p len:%#x\n", opcode, buf, len);

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = opcode;
	cmd.tx_data = true;
	cmd.len = len;
	cmd.buf = buf;
	cmd.qspimode = CCR_FMODE_INDR;

	stm32_qspi_set_framemode(nor, &cmd, false);

	return stm32_qspi_send(flash, &cmd);
}

static int stm32_qspi_write_reg(struct spi_nor *nor, u8 opcode,
				u8 *buf, int len)
{
	struct stm32_qspi_flash *flash = nor->priv;
	struct device *dev = flash->qspi->dev;
	struct stm32_qspi_cmd cmd;

	dev_dbg(dev, "write_reg: cmd:%#.2x buf:%p len:%#x\n", opcode, buf, len);

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = opcode;
	cmd.tx_data = !!(buf && len > 0);
	cmd.len = len;
	cmd.buf = buf;
	cmd.qspimode = CCR_FMODE_INDW;

	stm32_qspi_set_framemode(nor, &cmd, false);

	return stm32_qspi_send(flash, &cmd);
}

static ssize_t stm32_qspi_read(struct spi_nor *nor, loff_t from, size_t len,
			       u_char *buf)
{
	struct stm32_qspi_flash *flash = nor->priv;
	struct stm32_qspi *qspi = flash->qspi;
	struct stm32_qspi_cmd cmd;
	int err;

	dev_dbg(qspi->dev, "read(%#.2x): buf:%p from:%#.8x len:%#zx\n",
		nor->read_opcode, buf, (u32)from, len);

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nor->read_opcode;
	cmd.addr_width = nor->addr_width;
	cmd.addr = (u32)from;
	cmd.tx_data = true;
	cmd.dummy = nor->read_dummy;
	cmd.len = len;
	cmd.buf = buf;
	cmd.qspimode = flash->read_mode;

	stm32_qspi_set_framemode(nor, &cmd, true);
	err = stm32_qspi_send(flash, &cmd);

	return err ? err : len;
}

static ssize_t stm32_qspi_write(struct spi_nor *nor, loff_t to, size_t len,
				const u_char *buf)
{
	struct stm32_qspi_flash *flash = nor->priv;
	struct device *dev = flash->qspi->dev;
	struct stm32_qspi_cmd cmd;
	int err;

	dev_dbg(dev, "write(%#.2x): buf:%p to:%#.8x len:%#zx\n",
		nor->program_opcode, buf, (u32)to, len);

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nor->program_opcode;
	cmd.addr_width = nor->addr_width;
	cmd.addr = (u32)to;
	cmd.tx_data = true;
	cmd.len = len;
	cmd.buf = (void *)buf;
	cmd.qspimode = CCR_FMODE_INDW;

	stm32_qspi_set_framemode(nor, &cmd, false);
	err = stm32_qspi_send(flash, &cmd);

	return err ? err : len;
}

static int stm32_qspi_erase(struct spi_nor *nor, loff_t offs)
{
	struct stm32_qspi_flash *flash = nor->priv;
	struct device *dev = flash->qspi->dev;
	struct stm32_qspi_cmd cmd;

	dev_dbg(dev, "erase(%#.2x):offs:%#x\n", nor->erase_opcode, (u32)offs);

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = nor->erase_opcode;
	cmd.addr_width = nor->addr_width;
	cmd.addr = (u32)offs;
	cmd.qspimode = CCR_FMODE_INDW;

	stm32_qspi_set_framemode(nor, &cmd, false);

	return stm32_qspi_send(flash, &cmd);
}

static irqreturn_t stm32_qspi_irq(int irq, void *dev_id)
{
	struct stm32_qspi *qspi = (struct stm32_qspi *)dev_id;
	u32 cr, sr, fcr = 0;

	cr = readl_relaxed(qspi->io_base + QUADSPI_CR);
	sr = readl_relaxed(qspi->io_base + QUADSPI_SR);

	if ((cr & CR_TCIE) && (sr & SR_TCF)) {
		/* tx complete */
		fcr |= FCR_CTCF;
		complete(&qspi->cmd_completion);
	} else {
		dev_info_ratelimited(qspi->dev, "spurious interrupt\n");
	}

	writel_relaxed(fcr, qspi->io_base + QUADSPI_FCR);

	return IRQ_HANDLED;
}

static int stm32_qspi_prep(struct spi_nor *nor, enum spi_nor_ops ops)
{
	struct stm32_qspi_flash *flash = nor->priv;
	struct stm32_qspi *qspi = flash->qspi;

	mutex_lock(&qspi->lock);
	return 0;
}

static void stm32_qspi_unprep(struct spi_nor *nor, enum spi_nor_ops ops)
{
	struct stm32_qspi_flash *flash = nor->priv;
	struct stm32_qspi *qspi = flash->qspi;

	mutex_unlock(&qspi->lock);
}

static int stm32_qspi_flash_setup(struct stm32_qspi *qspi,
				  struct device_node *np)
{
	struct spi_nor_hwcaps hwcaps = {
		.mask = SNOR_HWCAPS_READ |
			SNOR_HWCAPS_READ_FAST |
			SNOR_HWCAPS_PP,
	};
	u32 width, presc, cs_num, max_rate = 0;
	struct stm32_qspi_flash *flash;
	struct mtd_info *mtd;
	int ret;

	of_property_read_u32(np, "reg", &cs_num);
	if (cs_num >= STM32_MAX_NORCHIP)
		return -EINVAL;

	of_property_read_u32(np, "spi-max-frequency", &max_rate);
	if (!max_rate)
		return -EINVAL;

	presc = DIV_ROUND_UP(qspi->clk_rate, max_rate) - 1;

	if (of_property_read_u32(np, "spi-rx-bus-width", &width))
		width = 1;

	if (width == 4)
		hwcaps.mask |= SNOR_HWCAPS_READ_1_1_4;
	else if (width == 2)
		hwcaps.mask |= SNOR_HWCAPS_READ_1_1_2;
	else if (width != 1)
		return -EINVAL;

	flash = &qspi->flash[cs_num];
	flash->qspi = qspi;
	flash->cs = cs_num;
	flash->presc = presc;

	flash->nor.dev = qspi->dev;
	spi_nor_set_flash_node(&flash->nor, np);
	flash->nor.priv = flash;
	mtd = &flash->nor.mtd;

	flash->nor.read = stm32_qspi_read;
	flash->nor.write = stm32_qspi_write;
	flash->nor.erase = stm32_qspi_erase;
	flash->nor.read_reg = stm32_qspi_read_reg;
	flash->nor.write_reg = stm32_qspi_write_reg;
	flash->nor.prepare = stm32_qspi_prep;
	flash->nor.unprepare = stm32_qspi_unprep;

	writel_relaxed(LPTR_DFT_TIMEOUT, qspi->io_base + QUADSPI_LPTR);

	writel_relaxed(CR_PRESC(presc) | CR_FTHRES(3) | CR_TCEN | CR_SSHIFT
		       | CR_EN, qspi->io_base + QUADSPI_CR);

	/*
	 * in stm32 qspi controller, QUADSPI_DCR register has a fsize field
	 * which define the size of nor flash.
	 * if fsize is NULL, the controller can't sent spi-nor command.
	 * set a temporary value just to discover the nor flash with
	 * "spi_nor_scan". After, the right value (mtd->size) can be set.
	 */
	flash->fsize = FSIZE_VAL(SZ_1K);

	ret = spi_nor_scan(&flash->nor, NULL, &hwcaps);
	if (ret) {
		dev_err(qspi->dev, "device scan failed\n");
		return ret;
	}

	flash->fsize = FSIZE_VAL(mtd->size);
	flash->prefetch_limit = mtd->size - STM32_QSPI_FIFO_SZ;

	flash->read_mode = CCR_FMODE_MM;
	if (mtd->size > qspi->mm_size)
		flash->read_mode = CCR_FMODE_INDR;

	writel_relaxed(DCR_CSHT(1), qspi->io_base + QUADSPI_DCR);

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(qspi->dev, "mtd device parse failed\n");
		return ret;
	}

	flash->registered = true;

	dev_dbg(qspi->dev, "read mm:%s cs:%d bus:%d\n",
		flash->read_mode == CCR_FMODE_MM ? "yes" : "no", cs_num, width);

	return 0;
}

static void stm32_qspi_mtd_free(struct stm32_qspi *qspi)
{
	int i;

	for (i = 0; i < STM32_MAX_NORCHIP; i++)
		if (qspi->flash[i].registered)
			mtd_device_unregister(&qspi->flash[i].nor.mtd);
}

static int stm32_qspi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *flash_np;
	struct reset_control *rstc;
	struct stm32_qspi *qspi;
	struct resource *res;
	int ret, irq;

	qspi = devm_kzalloc(dev, sizeof(*qspi), GFP_KERNEL);
	if (!qspi)
		return -ENOMEM;

	qspi->nor_num = of_get_child_count(dev->of_node);
	if (!qspi->nor_num || qspi->nor_num > STM32_MAX_NORCHIP)
		return -ENODEV;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "qspi");
	qspi->io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(qspi->io_base))
		return PTR_ERR(qspi->io_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "qspi_mm");
	qspi->mm_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(qspi->mm_base))
		return PTR_ERR(qspi->mm_base);

	qspi->mm_size = resource_size(res);

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(dev, irq, stm32_qspi_irq, 0,
			       dev_name(dev), qspi);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	init_completion(&qspi->cmd_completion);

	qspi->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(qspi->clk))
		return PTR_ERR(qspi->clk);

	qspi->clk_rate = clk_get_rate(qspi->clk);
	if (!qspi->clk_rate)
		return -EINVAL;

	ret = clk_prepare_enable(qspi->clk);
	if (ret) {
		dev_err(dev, "can not enable the clock\n");
		return ret;
	}

	rstc = devm_reset_control_get_exclusive(dev, NULL);
	if (!IS_ERR(rstc)) {
		reset_control_assert(rstc);
		udelay(2);
		reset_control_deassert(rstc);
	}

	qspi->dev = dev;
	platform_set_drvdata(pdev, qspi);
	mutex_init(&qspi->lock);

	for_each_available_child_of_node(dev->of_node, flash_np) {
		ret = stm32_qspi_flash_setup(qspi, flash_np);
		if (ret) {
			dev_err(dev, "unable to setup flash chip\n");
			goto err_flash;
		}
	}

	return 0;

err_flash:
	mutex_destroy(&qspi->lock);
	stm32_qspi_mtd_free(qspi);

	clk_disable_unprepare(qspi->clk);
	return ret;
}

static int stm32_qspi_remove(struct platform_device *pdev)
{
	struct stm32_qspi *qspi = platform_get_drvdata(pdev);

	/* disable qspi */
	writel_relaxed(0, qspi->io_base + QUADSPI_CR);

	stm32_qspi_mtd_free(qspi);
	mutex_destroy(&qspi->lock);

	clk_disable_unprepare(qspi->clk);
	return 0;
}

static const struct of_device_id stm32_qspi_match[] = {
	{.compatible = "st,stm32f469-qspi"},
	{}
};
MODULE_DEVICE_TABLE(of, stm32_qspi_match);

static struct platform_driver stm32_qspi_driver = {
	.probe	= stm32_qspi_probe,
	.remove	= stm32_qspi_remove,
	.driver	= {
		.name = "stm32-quadspi",
		.of_match_table = stm32_qspi_match,
	},
};
module_platform_driver(stm32_qspi_driver);

MODULE_AUTHOR("Ludovic Barre <ludovic.barre@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 quad spi driver");
MODULE_LICENSE("GPL v2");
