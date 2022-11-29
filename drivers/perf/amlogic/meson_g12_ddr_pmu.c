// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Amlogic, Inc. All rights reserved.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/types.h>

#include <soc/amlogic/meson_ddr_pmu.h>

#define PORT_MAJOR		32
#define DEFAULT_XTAL_FREQ	24000000UL

#define DMC_QOS_IRQ		BIT(30)

/* DMC bandwidth monitor register address offset */
#define DMC_MON_G12_CTRL0		(0x20  << 2)
#define DMC_MON_G12_CTRL1		(0x21  << 2)
#define DMC_MON_G12_CTRL2		(0x22  << 2)
#define DMC_MON_G12_CTRL3		(0x23  << 2)
#define DMC_MON_G12_CTRL4		(0x24  << 2)
#define DMC_MON_G12_CTRL5		(0x25  << 2)
#define DMC_MON_G12_CTRL6		(0x26  << 2)
#define DMC_MON_G12_CTRL7		(0x27  << 2)
#define DMC_MON_G12_CTRL8		(0x28  << 2)

#define DMC_MON_G12_ALL_REQ_CNT		(0x29  << 2)
#define DMC_MON_G12_ALL_GRANT_CNT	(0x2a  << 2)
#define DMC_MON_G12_ONE_GRANT_CNT	(0x2b  << 2)
#define DMC_MON_G12_SEC_GRANT_CNT	(0x2c  << 2)
#define DMC_MON_G12_THD_GRANT_CNT	(0x2d  << 2)
#define DMC_MON_G12_FOR_GRANT_CNT	(0x2e  << 2)
#define DMC_MON_G12_TIMER		(0x2f  << 2)

/* Each bit represent a axi line */
PMU_FORMAT_ATTR(event, "config:0-7");
PMU_FORMAT_ATTR(arm, "config1:0");
PMU_FORMAT_ATTR(gpu, "config1:1");
PMU_FORMAT_ATTR(pcie, "config1:2");
PMU_FORMAT_ATTR(hdcp, "config1:3");
PMU_FORMAT_ATTR(hevc_front, "config1:4");
PMU_FORMAT_ATTR(usb3_0, "config1:6");
PMU_FORMAT_ATTR(device, "config1:7");
PMU_FORMAT_ATTR(hevc_back, "config1:8");
PMU_FORMAT_ATTR(h265enc, "config1:9");
PMU_FORMAT_ATTR(vpu_read1, "config1:16");
PMU_FORMAT_ATTR(vpu_read2, "config1:17");
PMU_FORMAT_ATTR(vpu_read3, "config1:18");
PMU_FORMAT_ATTR(vpu_write1, "config1:19");
PMU_FORMAT_ATTR(vpu_write2, "config1:20");
PMU_FORMAT_ATTR(vdec, "config1:21");
PMU_FORMAT_ATTR(hcodec, "config1:22");
PMU_FORMAT_ATTR(ge2d, "config1:23");

PMU_FORMAT_ATTR(spicc1, "config1:32");
PMU_FORMAT_ATTR(usb0, "config1:33");
PMU_FORMAT_ATTR(dma, "config1:34");
PMU_FORMAT_ATTR(arb0, "config1:35");
PMU_FORMAT_ATTR(sd_emmc_b, "config1:36");
PMU_FORMAT_ATTR(usb1, "config1:37");
PMU_FORMAT_ATTR(audio, "config1:38");
PMU_FORMAT_ATTR(aififo, "config1:39");
PMU_FORMAT_ATTR(parser, "config1:41");
PMU_FORMAT_ATTR(ao_cpu, "config1:42");
PMU_FORMAT_ATTR(sd_emmc_c, "config1:43");
PMU_FORMAT_ATTR(spicc2, "config1:44");
PMU_FORMAT_ATTR(ethernet, "config1:45");
PMU_FORMAT_ATTR(sana, "config1:46");

/* for sm1 and g12b */
PMU_FORMAT_ATTR(nna, "config1:10");

/* for g12b only */
PMU_FORMAT_ATTR(gdc, "config1:11");
PMU_FORMAT_ATTR(mipi_isp, "config1:12");
PMU_FORMAT_ATTR(arm1, "config1:13");
PMU_FORMAT_ATTR(sd_emmc_a, "config1:40");

static struct attribute *g12_pmu_format_attrs[] = {
	&format_attr_event.attr,
	&format_attr_arm.attr,
	&format_attr_gpu.attr,
	&format_attr_nna.attr,
	&format_attr_gdc.attr,
	&format_attr_arm1.attr,
	&format_attr_mipi_isp.attr,
	&format_attr_sd_emmc_a.attr,
	&format_attr_pcie.attr,
	&format_attr_hdcp.attr,
	&format_attr_hevc_front.attr,
	&format_attr_usb3_0.attr,
	&format_attr_device.attr,
	&format_attr_hevc_back.attr,
	&format_attr_h265enc.attr,
	&format_attr_vpu_read1.attr,
	&format_attr_vpu_read2.attr,
	&format_attr_vpu_read3.attr,
	&format_attr_vpu_write1.attr,
	&format_attr_vpu_write2.attr,
	&format_attr_vdec.attr,
	&format_attr_hcodec.attr,
	&format_attr_ge2d.attr,
	&format_attr_spicc1.attr,
	&format_attr_usb0.attr,
	&format_attr_dma.attr,
	&format_attr_arb0.attr,
	&format_attr_sd_emmc_b.attr,
	&format_attr_usb1.attr,
	&format_attr_audio.attr,
	&format_attr_aififo.attr,
	&format_attr_parser.attr,
	&format_attr_ao_cpu.attr,
	&format_attr_sd_emmc_c.attr,
	&format_attr_spicc2.attr,
	&format_attr_ethernet.attr,
	&format_attr_sana.attr,
	NULL,
};

/* calculate ddr clock */
static unsigned long dmc_g12_get_freq_quick(struct dmc_info *info)
{
	unsigned int val;
	unsigned int n, m, od1;
	unsigned int od_div = 0xfff;
	unsigned long freq = 0;

	val = readl(info->pll_reg);
	val = val & 0xfffff;
	switch ((val >> 16) & 7) {
	case 0:
		od_div = 2;
		break;

	case 1:
		od_div = 3;
		break;

	case 2:
		od_div = 4;
		break;

	case 3:
		od_div = 6;
		break;

	case 4:
		od_div = 8;
		break;

	default:
		break;
	}

	m = val & 0x1ff;
	n = ((val >> 10) & 0x1f);
	od1 = (((val >> 19) & 0x1)) == 1 ? 2 : 1;
	freq = DEFAULT_XTAL_FREQ / 1000;        /* avoid overflow */
	if (n)
		freq = ((((freq * m) / n) >> od1) / od_div) * 1000;

	return freq;
}

#ifdef DEBUG
static void g12_dump_reg(struct dmc_info *db)
{
	int s = 0, i;
	unsigned int r;

	for (i = 0; i < 9; i++) {
		r  = readl(db->ddr_reg[0] + (DMC_MON_G12_CTRL0 + (i << 2)));
		pr_notice("DMC_MON_CTRL%d:        %08x\n", i, r);
	}
	r  = readl(db->ddr_reg[0] + DMC_MON_G12_ALL_REQ_CNT);
	pr_notice("DMC_MON_ALL_REQ_CNT:  %08x\n", r);
	r  = readl(db->ddr_reg[0] + DMC_MON_G12_ALL_GRANT_CNT);
	pr_notice("DMC_MON_ALL_GRANT_CNT:%08x\n", r);
	r  = readl(db->ddr_reg[0] + DMC_MON_G12_ONE_GRANT_CNT);
	pr_notice("DMC_MON_ONE_GRANT_CNT:%08x\n", r);
	r  = readl(db->ddr_reg[0] + DMC_MON_G12_SEC_GRANT_CNT);
	pr_notice("DMC_MON_SEC_GRANT_CNT:%08x\n", r);
	r  = readl(db->ddr_reg[0] + DMC_MON_G12_THD_GRANT_CNT);
	pr_notice("DMC_MON_THD_GRANT_CNT:%08x\n", r);
	r  = readl(db->ddr_reg[0] + DMC_MON_G12_FOR_GRANT_CNT);
	pr_notice("DMC_MON_FOR_GRANT_CNT:%08x\n", r);
	r  = readl(db->ddr_reg[0] + DMC_MON_G12_TIMER);
	pr_notice("DMC_MON_TIMER:        %08x\n", r);
}
#endif

static void dmc_g12_counter_enable(struct dmc_info *info)
{
	unsigned int val;
	unsigned long clock_count = dmc_g12_get_freq_quick(info) / 10; /* 100ms */

	writel(clock_count, info->ddr_reg[0] + DMC_MON_G12_TIMER);

	val = readl(info->ddr_reg[0] + DMC_MON_G12_CTRL0);

	/* enable all channel */
	val =  BIT(31) |	/* enable bit */
	       BIT(20) |	/* use timer  */
	       0x0f;		/* 4 channels */

	writel(val, info->ddr_reg[0] + DMC_MON_G12_CTRL0);

#ifdef DEBUG
	g12_dump_reg(info);
#endif
}

static void dmc_g12_config_fiter(struct dmc_info *info,
				 int port, int channel)
{
	u32 val;
	u32 rp[MAX_CHANNEL_NUM] = {DMC_MON_G12_CTRL1, DMC_MON_G12_CTRL3,
					DMC_MON_G12_CTRL5, DMC_MON_G12_CTRL7};
	u32 rs[MAX_CHANNEL_NUM] = {DMC_MON_G12_CTRL2, DMC_MON_G12_CTRL4,
					DMC_MON_G12_CTRL6, DMC_MON_G12_CTRL8};
	int subport = -1;

	/* clear all port mask */
	if (port < 0) {
		writel(0, info->ddr_reg[0] + rp[channel]);
		writel(0, info->ddr_reg[0] + rs[channel]);
		return;
	}

	if (port >= PORT_MAJOR)
		subport = port - PORT_MAJOR;

	if (subport < 0) {
		val = readl(info->ddr_reg[0] + rp[channel]);
		val |=  (1 << port);
		writel(val, info->ddr_reg[0] + rp[channel]);
		val = 0xffff;
		writel(val, info->ddr_reg[0] + rs[channel]);
	} else {
		val = BIT(23);		/* select device */
		writel(val, info->ddr_reg[0] + rp[channel]);
		val = readl(info->ddr_reg[0] + rs[channel]);
		val |= (1 << subport);
		writel(val, info->ddr_reg[0] + rs[channel]);
	}
}

static void dmc_g12_set_axi_filter(struct dmc_info *info, int axi_id, int channel)
{
	if (channel > info->hw_info->chann_nr)
		return;

	dmc_g12_config_fiter(info, axi_id, channel);
}

static void dmc_g12_counter_disable(struct dmc_info *info)
{
	int i;

	/* clear timer */
	writel(0, info->ddr_reg[0] + DMC_MON_G12_CTRL0);
	writel(0, info->ddr_reg[0] + DMC_MON_G12_TIMER);

	writel(0, info->ddr_reg[0] + DMC_MON_G12_ALL_REQ_CNT);
	writel(0, info->ddr_reg[0] + DMC_MON_G12_ALL_GRANT_CNT);
	writel(0, info->ddr_reg[0] + DMC_MON_G12_ONE_GRANT_CNT);
	writel(0, info->ddr_reg[0] + DMC_MON_G12_SEC_GRANT_CNT);
	writel(0, info->ddr_reg[0] + DMC_MON_G12_THD_GRANT_CNT);
	writel(0, info->ddr_reg[0] + DMC_MON_G12_FOR_GRANT_CNT);

	/* clear port channel mapping */
	for (i = 0; i < info->hw_info->chann_nr; i++)
		dmc_g12_config_fiter(info, -1, i);
}

static void dmc_g12_get_counters(struct dmc_info *info,
				 struct dmc_counter *counter)
{
	int i;
	unsigned int reg;

	counter->all_cnt = readl(info->ddr_reg[0] + DMC_MON_G12_ALL_GRANT_CNT);
	counter->all_req   = readl(info->ddr_reg[0] + DMC_MON_G12_ALL_REQ_CNT);

	for (i = 0; i < info->hw_info->chann_nr; i++) {
		reg = DMC_MON_G12_ONE_GRANT_CNT + (i << 2);
		counter->channel_cnt[i] = readl(info->ddr_reg[0] + reg);
	}
}

static int dmc_g12_irq_handler(struct dmc_info *info,
			       struct dmc_counter *counter)
{
	unsigned int val;
	int ret = -EINVAL;

	val = readl(info->ddr_reg[0] + DMC_MON_G12_CTRL0);
	if (val & DMC_QOS_IRQ) {
		dmc_g12_get_counters(info, counter);
		/* clear irq flags */
		writel(val, info->ddr_reg[0] + DMC_MON_G12_CTRL0);
		ret = 0;
	}
	return ret;
}

static const struct dmc_hw_info g12a_dmc_info = {
	.enable		= dmc_g12_counter_enable,
	.disable	= dmc_g12_counter_disable,
	.irq_handler	= dmc_g12_irq_handler,
	.get_counters	= dmc_g12_get_counters,
	.set_axi_filter	= dmc_g12_set_axi_filter,

	.dmc_nr = 1,
	.chann_nr = 4,
	.capability = {0X7EFF00FF03DF, 0},
	.fmt_attr = g12_pmu_format_attrs,
};

static const struct dmc_hw_info g12b_dmc_info = {
	.enable		= dmc_g12_counter_enable,
	.disable	= dmc_g12_counter_disable,
	.irq_handler	= dmc_g12_irq_handler,
	.get_counters	= dmc_g12_get_counters,
	.set_axi_filter	= dmc_g12_set_axi_filter,

	.dmc_nr = 1,
	.chann_nr = 4,
	.capability = {0X7FFF00FF3FDF, 0},
	.fmt_attr = g12_pmu_format_attrs,
};

static const struct dmc_hw_info sm1_dmc_info = {
	.enable		= dmc_g12_counter_enable,
	.disable	= dmc_g12_counter_disable,
	.irq_handler	= dmc_g12_irq_handler,
	.get_counters	= dmc_g12_get_counters,
	.set_axi_filter	= dmc_g12_set_axi_filter,

	.dmc_nr = 1,
	.chann_nr = 4,
	.capability = {0X7EFF00FF07DF, 0},
	.fmt_attr = g12_pmu_format_attrs,
};

static int g12_ddr_pmu_probe(struct platform_device *pdev)
{
	return meson_ddr_pmu_create(pdev);
}

static int g12_ddr_pmu_remove(struct platform_device *pdev)
{
	meson_ddr_pmu_remove(pdev);

	return 0;
}

static const struct of_device_id meson_ddr_pmu_dt_match[] = {
	{
		.compatible = "amlogic,g12a-ddr-pmu",
		.data = &g12a_dmc_info,
	},
	{
		.compatible = "amlogic,g12b-ddr-pmu",
		.data = &g12b_dmc_info,
	},
	{
		.compatible = "amlogic,sm1-ddr-pmu",
		.data = &sm1_dmc_info,
	},
	{}
};

static struct platform_driver g12_ddr_pmu_driver = {
	.probe = g12_ddr_pmu_probe,
	.remove = g12_ddr_pmu_remove,

	.driver = {
		.name = "meson-g12-ddr-pmu",
		.of_match_table = meson_ddr_pmu_dt_match,
	},
};

module_platform_driver(g12_ddr_pmu_driver);
MODULE_AUTHOR("Jiucheng Xu");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Amlogic G12 series SoC DDR PMU");
