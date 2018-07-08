/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2014-2016 Sean Wang <sean.wang@mediatek.com>
 *   Copyright (C) 2016-2017 John Crispin <blogic@openwrt.org>
 */

#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/if.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "hnat.h"

struct hnat_priv *host;

static void cr_set_bits(void __iomem * reg, u32 bs)
{
	u32 val = readl(reg);

	val |= bs;
	writel(val, reg);
}

static void cr_clr_bits(void __iomem * reg, u32 bs)
{
	u32 val = readl(reg);

	val &= ~bs;
	writel(val, reg);
}

static void cr_set_field(void __iomem * reg, u32 field, u32 val)
{
	unsigned int tv = readl(reg);

	tv &= ~field;
	tv |= ((val) << (ffs((unsigned int)field) - 1));
	writel(tv, reg);
}

static int hnat_start(void)
{
	u32 foe_table_sz;

	/* mapp the FOE table */
	foe_table_sz = FOE_4TB_SIZ * sizeof(struct foe_entry);
	host->foe_table_cpu =
	    dma_alloc_coherent(host->dev, foe_table_sz, &host->foe_table_dev,
			       GFP_KERNEL);
	if (!host->foe_table_cpu)
		return -1;

	writel(host->foe_table_dev, host->ppe_base + PPE_TB_BASE);
	memset(host->foe_table_cpu, 0, foe_table_sz);

	/* setup hashing */
	cr_set_field(host->ppe_base + PPE_TB_CFG, TB_ETRY_NUM, TABLE_4K);
	cr_set_field(host->ppe_base + PPE_TB_CFG, HASH_MODE, HASH_MODE_1);
	writel(HASH_SEED_KEY, host->ppe_base + PPE_HASH_SEED);
	cr_set_field(host->ppe_base + PPE_TB_CFG, XMODE, 0);
	cr_set_field(host->ppe_base + PPE_TB_CFG, TB_ENTRY_SIZE, ENTRY_64B);
	cr_set_field(host->ppe_base + PPE_TB_CFG, SMA, SMA_FWD_CPU_BUILD_ENTRY);

	/* set ip proto */
	writel(0xFFFFFFFF, host->ppe_base + PPE_IP_PROT_CHK);

	/* setup caching */
	cr_set_field(host->ppe_base + PPE_CAH_CTRL, CAH_X_MODE, 1);
	cr_set_field(host->ppe_base + PPE_CAH_CTRL, CAH_X_MODE, 0);
	cr_set_field(host->ppe_base + PPE_CAH_CTRL, CAH_EN, 1);

	/* enable FOE */
	cr_set_bits(host->ppe_base + PPE_FLOW_CFG,
			    BIT_IPV4_NAT_EN | BIT_IPV4_NAPT_EN |
			    BIT_IPV4_NAT_FRAG_EN | BIT_IPV4_HASH_GREK);

	/* setup FOE aging */
	cr_set_field(host->ppe_base + PPE_TB_CFG, NTU_AGE, 1);
	cr_set_field(host->ppe_base + PPE_TB_CFG, UNBD_AGE, 1);
	cr_set_field(host->ppe_base + PPE_UNB_AGE, UNB_MNP, 1000);
	cr_set_field(host->ppe_base + PPE_UNB_AGE, UNB_DLTA, 3);
	cr_set_field(host->ppe_base + PPE_TB_CFG, TCP_AGE, 1);
	cr_set_field(host->ppe_base + PPE_TB_CFG, UDP_AGE, 1);
	cr_set_field(host->ppe_base + PPE_TB_CFG, FIN_AGE, 1);
	cr_set_field(host->ppe_base + PPE_BND_AGE_0, UDP_DLTA, 5);
	cr_set_field(host->ppe_base + PPE_BND_AGE_0, NTU_DLTA, 5);
	cr_set_field(host->ppe_base + PPE_BND_AGE_1, FIN_DLTA, 5);
	cr_set_field(host->ppe_base + PPE_BND_AGE_1, TCP_DLTA, 5);

	/* setup FOE ka */
	cr_set_field(host->ppe_base + PPE_TB_CFG, KA_CFG, 3);
	cr_set_field(host->ppe_base + PPE_KA, KA_T, 1);
	cr_set_field(host->ppe_base + PPE_KA, TCP_KA, 1);
	cr_set_field(host->ppe_base + PPE_KA, UDP_KA, 1);
	cr_set_field(host->ppe_base + PPE_BIND_LMT_1, NTU_KA, 1);

	/* setup FOE rate limit */
	cr_set_field(host->ppe_base + PPE_BIND_LMT_0, QURT_LMT, 16383);
	cr_set_field(host->ppe_base + PPE_BIND_LMT_0, HALF_LMT, 16383);
	cr_set_field(host->ppe_base + PPE_BIND_LMT_1, FULL_LMT, 16383);
	cr_set_field(host->ppe_base + PPE_BNDR, BIND_RATE, 1);

	/* setup FOE cf gen */
	cr_set_field(host->ppe_base + PPE_GLO_CFG, PPE_EN, 1);
	writel(0, host->ppe_base + PPE_DFT_CPORT); // pdma
	//writel(0x55555555, host->ppe_base + PPE_DFT_CPORT); //qdma
	cr_set_field(host->ppe_base + PPE_GLO_CFG, TTL0_DRP, 1);

	/* fwd packets from gmac to PPE */
	cr_clr_bits(host->fe_base + GDMA1_FWD_CFG, GDM1_ALL_FRC_MASK);
	cr_set_bits(host->fe_base + GDMA1_FWD_CFG,
		    BITS_GDM1_ALL_FRC_P_PPE);
	cr_clr_bits(host->fe_base + GDMA2_FWD_CFG, GDM2_ALL_FRC_MASK);
	cr_set_bits(host->fe_base + GDMA2_FWD_CFG,
		    BITS_GDM2_ALL_FRC_P_PPE);

	dev_info(host->dev, "hwnat start\n");

	return 0;
}

static int ppe_busy_wait(void)
{
	unsigned long t_start = jiffies;
	u32 r = 0;

	while (1) {
		r = readl((host->ppe_base + 0x0));
		if (!(r & BIT(31)))
			return 0;
		if (time_after(jiffies, t_start + HZ))
			break;
		usleep_range(10, 20);
	}

	dev_err(host->dev, "ppe:%s timeout\n", __func__);

	return -1;
}

static void hnat_stop(void)
{
	u32 foe_table_sz;
	struct foe_entry *entry, *end;
	u32 r1 = 0, r2 = 0;

	/* discard all traffic while we disable the PPE */
	cr_clr_bits(host->fe_base + GDMA1_FWD_CFG, GDM1_ALL_FRC_MASK);
	cr_set_bits(host->fe_base + GDMA1_FWD_CFG,
		    BITS_GDM1_ALL_FRC_P_DISCARD);
	cr_clr_bits(host->fe_base + GDMA2_FWD_CFG, GDM2_ALL_FRC_MASK);
	cr_set_bits(host->fe_base + GDMA2_FWD_CFG,
		    BITS_GDM2_ALL_FRC_P_DISCARD);

	if (ppe_busy_wait()) {
#if 0
		reset_control_reset(host->rstc);
#endif
		msleep(2000);
		return;
	}

	entry = host->foe_table_cpu;
        end = host->foe_table_cpu + FOE_4TB_SIZ;
        while (entry < end) {
		entry->bfib1.state = INVALID;
                entry++;
        }

	/* disable caching */
	cr_set_field(host->ppe_base + PPE_CAH_CTRL, CAH_X_MODE, 1);
	cr_set_field(host->ppe_base + PPE_CAH_CTRL, CAH_X_MODE, 0);
	cr_set_field(host->ppe_base + PPE_CAH_CTRL, CAH_EN, 0);

	/* flush cache has to be ahead of hnat diable --*/
	cr_set_field(host->ppe_base + PPE_GLO_CFG, PPE_EN, 0);

	/* disable FOE */
	cr_clr_bits(host->ppe_base + PPE_FLOW_CFG,
			    BIT_IPV4_NAPT_EN | BIT_IPV4_NAT_EN |
			    BIT_IPV4_NAT_FRAG_EN |
			    BIT_FUC_FOE | BIT_FMC_FOE | BIT_FUC_FOE);

	/* disable FOE aging */
	cr_set_field(host->ppe_base + PPE_TB_CFG, NTU_AGE, 0);
	cr_set_field(host->ppe_base + PPE_TB_CFG, UNBD_AGE, 0);
	cr_set_field(host->ppe_base + PPE_TB_CFG, TCP_AGE, 0);
	cr_set_field(host->ppe_base + PPE_TB_CFG, UDP_AGE, 0);
	cr_set_field(host->ppe_base + PPE_TB_CFG, FIN_AGE, 0);

	r1 = readl(host->fe_base + 0x100);
	r2 = readl(host->fe_base + 0x10c);

	dev_info(host->dev, "0x100 = 0x%x, 0x10c = 0x%x\n", r1, r2);

	if (((r1 & 0xff00) >> 0x8) >= (r1 & 0xff) ||
	    ((r1 & 0xff00) >> 0x8) >= (r2 & 0xff)) {
		dev_info(host->dev, "reset pse\n");
		writel(0x1, host->fe_base + 0x4);
	}

	/* free the FOE table */
	foe_table_sz = FOE_4TB_SIZ * sizeof(struct foe_entry);
	dma_free_coherent(NULL, foe_table_sz, host->foe_table_cpu,
			  host->foe_table_dev);
	writel(0, host->ppe_base + PPE_TB_BASE);

	if (ppe_busy_wait()) {
#if 0
		reset_control_reset(host->rstc);
#endif
		msleep(2000);
		return;
	}

	/* send all traffic back to the DMA engine */
	cr_clr_bits(host->fe_base + GDMA1_FWD_CFG, GDM1_ALL_FRC_MASK);
	cr_set_bits(host->fe_base + GDMA1_FWD_CFG,
		    BITS_GDM1_ALL_FRC_P_CPU_PDMA);
	cr_clr_bits(host->fe_base + GDMA2_FWD_CFG, GDM2_ALL_FRC_MASK);
	cr_set_bits(host->fe_base + GDMA2_FWD_CFG,
		    BITS_GDM2_ALL_FRC_P_CPU_PDMA);
}

static int hnat_probe(struct platform_device *pdev)
{
	struct net *net;
	int ret;
	int err = 0;
	struct resource *res ;
	const char *name;
	struct device_node *np;

	host = devm_kzalloc(&pdev->dev, sizeof(struct hnat_priv), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->dev = &pdev->dev;
	np = host->dev->of_node;

	err = of_property_read_string(np, "mtketh-wan", &name);
	if (err < 0)
		return -EINVAL;

	strncpy(host->wan, (char *)name, IFNAMSIZ);
	dev_info(&pdev->dev, "wan = %s\n", host->wan);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;

	host->fe_base = devm_ioremap_nocache(&pdev->dev, res->start,
					     res->end - res->start + 1);
	if (!host->fe_base)
		return -EADDRNOTAVAIL;

	host->ppe_base = host->fe_base + 0xe00;
	err = hnat_init_debugfs(host);
	if (err)
		return err;
#if 0
	host->rstc = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(host->rstc))
		return PTR_ERR(host->rstc);
#endif
	err = hnat_start();
	if (err)
		goto err_out;

	//err = hnat_register_nf_hooks(); //how to call the function with net-param??
	for_each_net(net) {
		ret=hnat_register_nf_hooks(net);
		//if (err)
		if (ret && ret != -ENOENT)
			goto err_out;
	}
	return 0;

err_out:
	hnat_stop();
	hnat_deinit_debugfs(host);
	return err;
}

static int hnat_remove(struct platform_device *pdev)
{
	struct net *net;
	//hnat_unregister_nf_hooks(); //how to call the function with net-param??
    for_each_net(net) {
        hnat_unregister_nf_hooks(net);
    }

	hnat_stop();
	hnat_deinit_debugfs(host);

	return 0;
}

const struct of_device_id of_hnat_match[] = {
	{ .compatible = "mediatek,mt7623-hnat" },
	{},
};

static struct platform_driver hnat_driver = {
	.probe = hnat_probe,
	.remove = hnat_remove,
	.driver = {
		.name = "mediatek_soc_hnat",
		.of_match_table = of_hnat_match,
	},
};

module_platform_driver(hnat_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_AUTHOR("John Crispin <john@phrozen.org>");
MODULE_DESCRIPTION("Mediatek Hardware NAT");
