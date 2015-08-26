/*
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ipu-v3-prg.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "prg-regs.h"

#define PRG_CHAN_NUM	3

struct prg_chan {
	unsigned int pre_num;
	struct mutex mutex;	/* for in_use */
	bool in_use;
};

struct ipu_prg_data {
	unsigned int id;
	void __iomem *base;
	unsigned long memory;
	struct clk *axi_clk;
	struct clk *apb_clk;
	struct list_head list;
	struct device *dev;
	struct prg_chan chan[PRG_CHAN_NUM];
	struct regmap *regmap;
	struct regmap_field *pre_prg_sel[2];
	spinlock_t lock;
};

static LIST_HEAD(prg_list);
static DEFINE_MUTEX(prg_lock);

static inline void prg_write(struct ipu_prg_data *prg,
			u32 value, unsigned int offset)
{
	writel(value, prg->base + offset);
}

static inline u32 prg_read(struct ipu_prg_data *prg, unsigned offset)
{
	return readl(prg->base + offset);
}

static struct ipu_prg_data *get_prg(unsigned int ipu_id)
{
	struct ipu_prg_data *prg;

	mutex_lock(&prg_lock);
	list_for_each_entry(prg, &prg_list, list) {
		if (prg->id == ipu_id) {
			mutex_unlock(&prg_lock);
			return prg;
		}
	}
	mutex_unlock(&prg_lock);

	return NULL;
}

static int assign_prg_chan(struct ipu_prg_data *prg, unsigned int pre_num,
			   ipu_channel_t ipu_ch)
{
	int prg_ch;

	if (!prg)
		return -EINVAL;

	switch (ipu_ch) {
	case MEM_BG_SYNC:
		prg_ch = 0;
		break;
	case MEM_FG_SYNC:
		prg_ch = 1;
		break;
	case MEM_DC_SYNC:
		prg_ch = 2;
		break;
	default:
		dev_err(prg->dev, "wrong ipu channel type\n");
		return -EINVAL;
	}

	mutex_lock(&prg->chan[prg_ch].mutex);
	if (!prg->chan[prg_ch].in_use) {
		prg->chan[prg_ch].in_use = true;
		prg->chan[prg_ch].pre_num = pre_num;

		if (prg_ch != 0) {
			unsigned int pmux, psel;	/* primary */
			unsigned int smux, ssel;	/* secondary */
			struct regmap_field *pfield, *sfield;

			psel = pre_num - 1;
			ssel = psel ? 0 : 1;

			pfield = prg->pre_prg_sel[psel];
			sfield = prg->pre_prg_sel[ssel];
			pmux = (prg_ch - 1) + (prg->id << 1);

			mutex_lock(&prg_lock);
			regmap_field_write(pfield, pmux);

			/*
			 * PRE1 and PRE2 cannot bind with a same channel of
			 * one PRG even if one of the two PREs is disabled.
			 */
			regmap_field_read(sfield, &smux);
			if (smux == pmux) {
				smux = pmux ^ 0x1;
				regmap_field_write(sfield, smux);
			}
			mutex_unlock(&prg_lock);
		}
		mutex_unlock(&prg->chan[prg_ch].mutex);
		dev_dbg(prg->dev, "bind prg%u ch%d with pre%u\n",
				prg->id, prg_ch, pre_num);
		return prg_ch;
	}
	mutex_unlock(&prg->chan[prg_ch].mutex);
	return -EBUSY;
}

static inline int get_prg_chan(struct ipu_prg_data *prg, unsigned int pre_num)
{
	int i;

	if (!prg)
		return -EINVAL;

	for (i = 0; i < PRG_CHAN_NUM; i++) {
		mutex_lock(&prg->chan[i].mutex);
		if (prg->chan[i].in_use &&
		    prg->chan[i].pre_num == pre_num) {
			mutex_unlock(&prg->chan[i].mutex);
			return i;
		}
		mutex_unlock(&prg->chan[i].mutex);
	}
	return -ENOENT;
}

int ipu_prg_config(struct ipu_prg_config *config)
{
	struct ipu_prg_data *prg = get_prg(config->id);
	struct ipu_soc *ipu = ipu_get_soc(config->id);
	int prg_ch, axi_id;
	u32 reg;

	if (!prg || config->crop_line > 3 || !ipu)
		return -EINVAL;

	if (config->height & ~IPU_PR_CH_HEIGHT_MASK)
		return -EINVAL;

	prg_ch = assign_prg_chan(prg, config->pre_num, config->ipu_ch);
	if (prg_ch < 0)
		return prg_ch;

	axi_id = ipu_ch_param_get_axi_id(ipu, config->ipu_ch, IPU_INPUT_BUFFER);

	clk_prepare_enable(prg->axi_clk);
	clk_prepare_enable(prg->apb_clk);

	spin_lock(&prg->lock);
	/* clear all load enable to impact other channels */
	reg = prg_read(prg, IPU_PR_CTRL);
	reg &= ~IPU_PR_CTRL_CH_CNT_LOAD_EN_MASK;
	prg_write(prg, reg, IPU_PR_CTRL);

	/* counter load enable */
	reg = prg_read(prg, IPU_PR_CTRL);
	reg |= IPU_PR_CTRL_CH_CNT_LOAD_EN(prg_ch);
	prg_write(prg, reg, IPU_PR_CTRL);

	/* AXI ID */
	reg = prg_read(prg, IPU_PR_CTRL);
	reg &= ~IPU_PR_CTRL_SOFT_CH_ARID_MASK(prg_ch);
	reg |= IPU_PR_CTRL_SOFT_CH_ARID(prg_ch, axi_id);
	prg_write(prg, reg, IPU_PR_CTRL);

	/* so */
	reg = prg_read(prg, IPU_PR_CTRL);
	reg &= ~IPU_PR_CTRL_CH_SO_MASK(prg_ch);
	reg |= IPU_PR_CTRL_CH_SO(prg_ch, config->so);
	prg_write(prg, reg, IPU_PR_CTRL);

	/* vflip */
	reg = prg_read(prg, IPU_PR_CTRL);
	reg &= ~IPU_PR_CTRL_CH_VFLIP_MASK(prg_ch);
	reg |= IPU_PR_CTRL_CH_VFLIP(prg_ch, config->vflip);
	prg_write(prg, reg, IPU_PR_CTRL);

	/* block mode */
	reg = prg_read(prg, IPU_PR_CTRL);
	reg &= ~IPU_PR_CTRL_CH_BLOCK_MODE_MASK(prg_ch);
	reg |= IPU_PR_CTRL_CH_BLOCK_MODE(prg_ch, config->block_mode);
	prg_write(prg, reg, IPU_PR_CTRL);

	/* disable bypass */
	reg = prg_read(prg, IPU_PR_CTRL);
	reg &= ~IPU_PR_CTRL_CH_BYPASS(prg_ch);
	prg_write(prg, reg, IPU_PR_CTRL);

	/* stride */
	reg = prg_read(prg, IPU_PR_STRIDE(prg_ch));
	reg &= ~IPU_PR_STRIDE_MASK;
	reg |= config->stride - 1;
	prg_write(prg, reg, IPU_PR_STRIDE(prg_ch));

	/* ilo */
	reg = prg_read(prg, IPU_PR_CH_ILO(prg_ch));
	reg &= ~IPU_PR_CH_ILO_MASK;
	reg |= IPU_PR_CH_ILO_NUM(config->ilo);
	prg_write(prg, reg, IPU_PR_CH_ILO(prg_ch));

	/* height */
	reg = prg_read(prg, IPU_PR_CH_HEIGHT(prg_ch));
	reg &= ~IPU_PR_CH_HEIGHT_MASK;
	reg |= IPU_PR_CH_HEIGHT_NUM(config->height);
	prg_write(prg, reg, IPU_PR_CH_HEIGHT(prg_ch));

	/* ipu height */
	reg = prg_read(prg, IPU_PR_CH_HEIGHT(prg_ch));
	reg &= ~IPU_PR_CH_IPU_HEIGHT_MASK;
	reg |= IPU_PR_CH_IPU_HEIGHT_NUM(config->ipu_height);
	prg_write(prg, reg, IPU_PR_CH_HEIGHT(prg_ch));

	/* crop */
	reg = prg_read(prg, IPU_PR_CROP_LINE);
	reg &= ~IPU_PR_CROP_LINE_MASK(prg_ch);
	reg |= IPU_PR_CROP_LINE_NUM(prg_ch, config->crop_line);
	prg_write(prg, reg, IPU_PR_CROP_LINE);

	/* buffer address */
	reg = prg_read(prg, IPU_PR_CH_BADDR(prg_ch));
	reg &= ~IPU_PR_CH_BADDR_MASK;
	reg |= config->baddr;
	prg_write(prg, reg, IPU_PR_CH_BADDR(prg_ch));

	/* offset */
	reg = prg_read(prg, IPU_PR_CH_OFFSET(prg_ch));
	reg &= ~IPU_PR_CH_OFFSET_MASK;
	reg |= config->offset;
	prg_write(prg, reg, IPU_PR_CH_OFFSET(prg_ch));

	/* threshold */
	reg = prg_read(prg, IPU_PR_ADDR_THD);
	reg &= ~IPU_PR_ADDR_THD_MASK;
	reg |= prg->memory;
	prg_write(prg, reg, IPU_PR_ADDR_THD);

	/* shadow enable */
	reg = prg_read(prg, IPU_PR_CTRL);
	reg |= IPU_PR_CTRL_SHADOW_EN;
	prg_write(prg, reg, IPU_PR_CTRL);

	/* register update */
	reg = prg_read(prg, IPU_PR_REG_UPDATE);
	reg |= IPU_PR_REG_UPDATE_EN;
	prg_write(prg, reg, IPU_PR_REG_UPDATE);
	spin_unlock(&prg->lock);

	clk_disable_unprepare(prg->apb_clk);

	return 0;
}
EXPORT_SYMBOL(ipu_prg_config);

int ipu_prg_disable(unsigned int ipu_id, unsigned int pre_num)
{
	struct ipu_prg_data *prg = get_prg(ipu_id);
	int prg_ch;
	u32 reg;

	if (!prg)
		return -EINVAL;

	prg_ch = get_prg_chan(prg, pre_num);
	if (prg_ch < 0)
		return prg_ch;

	clk_prepare_enable(prg->apb_clk);

	spin_lock(&prg->lock);
	/* clear all load enable to impact other channels */
	reg = prg_read(prg, IPU_PR_CTRL);
	reg &= ~IPU_PR_CTRL_CH_CNT_LOAD_EN_MASK;
	prg_write(prg, reg, IPU_PR_CTRL);

	/* counter load enable */
	reg = prg_read(prg, IPU_PR_CTRL);
	reg |= IPU_PR_CTRL_CH_CNT_LOAD_EN(prg_ch);
	prg_write(prg, reg, IPU_PR_CTRL);

	/* enable bypass */
	reg = prg_read(prg, IPU_PR_CTRL);
	reg |= IPU_PR_CTRL_CH_BYPASS(prg_ch);
	prg_write(prg, reg, IPU_PR_CTRL);

	/* shadow enable */
	reg = prg_read(prg, IPU_PR_CTRL);
	reg |= IPU_PR_CTRL_SHADOW_EN;
	prg_write(prg, reg, IPU_PR_CTRL);

	/* register update */
	reg = prg_read(prg, IPU_PR_REG_UPDATE);
	reg |= IPU_PR_REG_UPDATE_EN;
	prg_write(prg, reg, IPU_PR_REG_UPDATE);
	spin_unlock(&prg->lock);

	clk_disable_unprepare(prg->apb_clk);
	clk_disable_unprepare(prg->axi_clk);

	mutex_lock(&prg->chan[prg_ch].mutex);
	prg->chan[prg_ch].in_use = false;
	mutex_unlock(&prg->chan[prg_ch].mutex);

	return 0;
}
EXPORT_SYMBOL(ipu_prg_disable);

int ipu_prg_wait_buf_ready(unsigned int ipu_id, unsigned int pre_num,
			   unsigned int hsk_line_num,
			   int pre_store_out_height)
{
	struct ipu_prg_data *prg = get_prg(ipu_id);
	int prg_ch, timeout = 1000;
	u32 reg;

	if (!prg)
		return -EINVAL;

	prg_ch = get_prg_chan(prg, pre_num);
	if (prg_ch < 0)
		return prg_ch;

	clk_prepare_enable(prg->apb_clk);

	spin_lock(&prg->lock);
	if (pre_store_out_height <= (4 << hsk_line_num)) {
		do {
			reg = prg_read(prg, IPU_PR_STATUS);
			udelay(1000);
			timeout--;
		} while (!(reg & IPU_PR_STATUS_BUF_RDY(prg_ch, 0)) && timeout);
	} else {
		do {
			reg = prg_read(prg, IPU_PR_STATUS);
			udelay(1000);
			timeout--;
		} while ((!(reg & IPU_PR_STATUS_BUF_RDY(prg_ch, 0)) ||
			  !(reg & IPU_PR_STATUS_BUF_RDY(prg_ch, 1))) && timeout);
	}
	spin_unlock(&prg->lock);

	clk_disable_unprepare(prg->apb_clk);

	if (!timeout)
		dev_err(prg->dev, "wait for buffer ready timeout\n");

	return 0;
}
EXPORT_SYMBOL(ipu_prg_wait_buf_ready);

static int ipu_prg_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node, *memory;
	struct ipu_prg_data *prg;
	struct resource *res;
	struct reg_field reg_field0 = REG_FIELD(IOMUXC_GPR5,
						IMX6Q_GPR5_PRE_PRG_SEL0_LSB,
						IMX6Q_GPR5_PRE_PRG_SEL0_MSB);
	struct reg_field reg_field1 = REG_FIELD(IOMUXC_GPR5,
						IMX6Q_GPR5_PRE_PRG_SEL1_LSB,
						IMX6Q_GPR5_PRE_PRG_SEL1_MSB);
	int id, i;

	prg = devm_kzalloc(&pdev->dev, sizeof(*prg), GFP_KERNEL);
	if (!prg)
		return -ENOMEM;
	prg->dev = &pdev->dev;

	for (i = 0; i < PRG_CHAN_NUM; i++)
		mutex_init(&prg->chan[i].mutex);

	spin_lock_init(&prg->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	prg->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(prg->base))
		return PTR_ERR(prg->base);

	prg->axi_clk = devm_clk_get(&pdev->dev, "axi");
	if (IS_ERR(prg->axi_clk)) {
		dev_err(&pdev->dev, "failed to get the axi clk\n");
		return PTR_ERR(prg->axi_clk);
	}

	prg->apb_clk = devm_clk_get(&pdev->dev, "apb");
	if (IS_ERR(prg->apb_clk)) {
		dev_err(&pdev->dev, "failed to get the apb clk\n");
		return PTR_ERR(prg->apb_clk);
	}

	prg->regmap = syscon_regmap_lookup_by_phandle(np, "gpr");
	if (IS_ERR(prg->regmap)) {
		dev_err(&pdev->dev, "failed to get regmap\n");
		return PTR_ERR(prg->regmap);
	}

	prg->pre_prg_sel[0] = devm_regmap_field_alloc(&pdev->dev, prg->regmap,
							reg_field0);
	if (IS_ERR(prg->pre_prg_sel[0]))
		return PTR_ERR(prg->pre_prg_sel[0]);

	prg->pre_prg_sel[1] = devm_regmap_field_alloc(&pdev->dev, prg->regmap,
							reg_field1);
	if (IS_ERR(prg->pre_prg_sel[1]))
		return PTR_ERR(prg->pre_prg_sel[1]);

	memory = of_parse_phandle(np, "memory-region", 0);
	if (!memory)
		return -ENODEV;

	prg->memory = of_translate_address(memory,
				of_get_address(memory, 0, NULL, NULL));

	id = of_alias_get_id(np, "prg");
	if (id < 0) {
		dev_err(&pdev->dev, "failed to get PRG id\n");
		return id;
	}
	prg->id = id;

	mutex_lock(&prg_lock);
	list_add_tail(&prg->list, &prg_list);
	mutex_unlock(&prg_lock);

	platform_set_drvdata(pdev, prg);

	dev_info(&pdev->dev, "driver probed\n");

	return 0;
}

static int ipu_prg_remove(struct platform_device *pdev)
{
	struct ipu_prg_data *prg = platform_get_drvdata(pdev);

	mutex_lock(&prg_lock);
	list_del(&prg->list);
	mutex_unlock(&prg_lock);

	return 0;
}

static const struct of_device_id imx_ipu_prg_dt_ids[] = {
	{ .compatible = "fsl,imx6q-prg", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_ipu_prg_dt_ids);

static struct platform_driver ipu_prg_driver = {
	.driver = {
			.name = "imx-prg",
			.of_match_table = of_match_ptr(imx_ipu_prg_dt_ids),
		  },
	.probe  = ipu_prg_probe,
	.remove = ipu_prg_remove,
};

static int __init ipu_prg_init(void)
{
	return platform_driver_register(&ipu_prg_driver);
}
subsys_initcall(ipu_prg_init);

static void __exit ipu_prg_exit(void)
{
	platform_driver_unregister(&ipu_prg_driver);
}
module_exit(ipu_prg_exit);

MODULE_DESCRIPTION("i.MX PRG driver");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_LICENSE("GPL");
