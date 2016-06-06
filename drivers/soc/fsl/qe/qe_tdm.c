/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Authors:	Zhao Qiang <qiang.zhao@nxp.com>
 *
 * Description:
 * QE TDM API Set - TDM specific routines implementations.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <soc/fsl/qe/qe_tdm.h>

static int set_tdm_framer(const char *tdm_framer_type)
{
	if (strcmp(tdm_framer_type, "e1") == 0)
		return TDM_FRAMER_E1;
	else if (strcmp(tdm_framer_type, "t1") == 0)
		return TDM_FRAMER_T1;
	else
		return -EINVAL;
}

static void set_si_param(struct ucc_tdm *utdm, struct ucc_tdm_info *ut_info)
{
	struct si_mode_info *si_info = &ut_info->si_info;

	if (utdm->tdm_mode == TDM_INTERNAL_LOOPBACK) {
		si_info->simr_crt = 1;
		si_info->simr_rfsd = 0;
	}
}

int ucc_of_parse_tdm(struct device_node *np, struct ucc_tdm *utdm,
		     struct ucc_tdm_info *ut_info)
{
	const char *sprop;
	int ret = 0;
	u32 val;
	struct resource *res;
	struct device_node *np2;
	static int siram_init_flag;
	struct platform_device *pdev;

	sprop = of_get_property(np, "fsl,rx-sync-clock", NULL);
	if (sprop) {
		ut_info->uf_info.rx_sync = qe_clock_source(sprop);
		if ((ut_info->uf_info.rx_sync < QE_CLK_NONE) ||
		    (ut_info->uf_info.rx_sync > QE_RSYNC_PIN)) {
			pr_err("QE-TDM: Invalid rx-sync-clock property\n");
			return -EINVAL;
		}
	} else {
		pr_err("QE-TDM: Invalid rx-sync-clock property\n");
		return -EINVAL;
	}

	sprop = of_get_property(np, "fsl,tx-sync-clock", NULL);
	if (sprop) {
		ut_info->uf_info.tx_sync = qe_clock_source(sprop);
		if ((ut_info->uf_info.tx_sync < QE_CLK_NONE) ||
		    (ut_info->uf_info.tx_sync > QE_TSYNC_PIN)) {
			pr_err("QE-TDM: Invalid tx-sync-clock property\n");
		return -EINVAL;
		}
	} else {
		pr_err("QE-TDM: Invalid tx-sync-clock property\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_index(np, "fsl,tx-timeslot-mask", 0, &val);
	if (ret) {
		pr_err("QE-TDM: Invalid tx-timeslot-mask property\n");
		return -EINVAL;
	}
	utdm->tx_ts_mask = val;

	ret = of_property_read_u32_index(np, "fsl,rx-timeslot-mask", 0, &val);
	if (ret) {
		ret = -EINVAL;
		pr_err("QE-TDM: Invalid rx-timeslot-mask property\n");
		return ret;
	}
	utdm->rx_ts_mask = val;

	ret = of_property_read_u32_index(np, "fsl,tdm-id", 0, &val);
	if (ret) {
		ret = -EINVAL;
		pr_err("QE-TDM: No fsl,tdm-id property for this UCC\n");
		return ret;
	}
	utdm->tdm_port = val;
	ut_info->uf_info.tdm_num = utdm->tdm_port;

	if (of_get_property(np, "fsl,tdm-internal-loopback", NULL))
		utdm->tdm_mode = TDM_INTERNAL_LOOPBACK;
	else
		utdm->tdm_mode = TDM_NORMAL;

	sprop = of_get_property(np, "fsl,tdm-framer-type", NULL);
	if (!sprop) {
		ret = -EINVAL;
		pr_err("QE-TDM: No tdm-framer-type property for UCC\n");
		return ret;
	}
	ret = set_tdm_framer(sprop);
	if (ret < 0)
		return -EINVAL;
	utdm->tdm_framer_type = ret;

	ret = of_property_read_u32_index(np, "fsl,siram-entry-id", 0, &val);
	if (ret) {
		ret = -EINVAL;
		pr_err("QE-TDM: No siram entry id for UCC\n");
		return ret;
	}
	utdm->siram_entry_id = val;

	set_si_param(utdm, ut_info);

	np2 = of_find_compatible_node(NULL, NULL, "fsl,t1040-qe-si");
	if (!np2)
		return -EINVAL;

	pdev = of_find_device_by_node(np2);
	if (!pdev) {
		pr_err("%s: failed to lookup pdev\n", np2->name);
		of_node_put(np2);
		return -EINVAL;
	}

	of_node_put(np2);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	utdm->si_regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(utdm->si_regs)) {
		ret = PTR_ERR(utdm->si_regs);
		goto err_miss_siram_property;
	}

	np2 = of_find_compatible_node(NULL, NULL, "fsl,t1040-qe-siram");
	if (!np2) {
		ret = -EINVAL;
		goto err_miss_siram_property;
	}

	pdev = of_find_device_by_node(np2);
	if (!pdev) {
		ret = -EINVAL;
		pr_err("%s: failed to lookup pdev\n", np2->name);
		of_node_put(np2);
		goto err_miss_siram_property;
	}

	of_node_put(np2);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	utdm->siram = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(utdm->siram)) {
		ret = PTR_ERR(utdm->siram);
		goto err_miss_siram_property;
	}

	if (siram_init_flag == 0) {
		memset_io(utdm->siram, 0,  res->end - res->start + 1);
		siram_init_flag = 1;
	}

	return ret;

err_miss_siram_property:
	devm_iounmap(&pdev->dev, utdm->si_regs);
	return ret;
}

void ucc_tdm_init(struct ucc_tdm *utdm, struct ucc_tdm_info *ut_info)
{
	struct si1 __iomem *si_regs;
	u16 __iomem *siram;
	u16 siram_entry_valid;
	u16 siram_entry_closed;
	u16 ucc_num;
	u8 csel;
	u16 sixmr;
	u16 tdm_port;
	u32 siram_entry_id;
	u32 mask;
	int i;

	si_regs = utdm->si_regs;
	siram = utdm->siram;
	ucc_num = ut_info->uf_info.ucc_num;
	tdm_port = utdm->tdm_port;
	siram_entry_id = utdm->siram_entry_id;

	if (utdm->tdm_framer_type == TDM_FRAMER_T1)
		utdm->num_of_ts = 24;
	if (utdm->tdm_framer_type == TDM_FRAMER_E1)
		utdm->num_of_ts = 32;

	/* set siram table */
	csel = (ucc_num < 4) ? ucc_num + 9 : ucc_num - 3;

	siram_entry_valid = SIR_CSEL(csel) | SIR_BYTE | SIR_CNT(0);
	siram_entry_closed = SIR_IDLE | SIR_BYTE | SIR_CNT(0);

	for (i = 0; i < utdm->num_of_ts; i++) {
		mask = 0x01 << i;

		if (utdm->tx_ts_mask & mask)
			iowrite16be(siram_entry_valid,
				    &siram[siram_entry_id * 32 + i]);
		else
			iowrite16be(siram_entry_closed,
				    &siram[siram_entry_id * 32 + i]);

		if (utdm->rx_ts_mask & mask)
			iowrite16be(siram_entry_valid,
				    &siram[siram_entry_id * 32 + 0x200 +  i]);
		else
			iowrite16be(siram_entry_closed,
				    &siram[siram_entry_id * 32 + 0x200 +  i]);
	}

	setbits16(&siram[(siram_entry_id * 32) + (utdm->num_of_ts - 1)],
		  SIR_LAST);
	setbits16(&siram[(siram_entry_id * 32) + 0x200 + (utdm->num_of_ts - 1)],
		  SIR_LAST);

	/* Set SIxMR register */
	sixmr = SIMR_SAD(siram_entry_id);

	sixmr &= ~SIMR_SDM_MASK;

	if (utdm->tdm_mode == TDM_INTERNAL_LOOPBACK)
		sixmr |= SIMR_SDM_INTERNAL_LOOPBACK;
	else
		sixmr |= SIMR_SDM_NORMAL;

	sixmr |= SIMR_RFSD(ut_info->si_info.simr_rfsd) |
			SIMR_TFSD(ut_info->si_info.simr_tfsd);

	if (ut_info->si_info.simr_crt)
		sixmr |= SIMR_CRT;
	if (ut_info->si_info.simr_sl)
		sixmr |= SIMR_SL;
	if (ut_info->si_info.simr_ce)
		sixmr |= SIMR_CE;
	if (ut_info->si_info.simr_fe)
		sixmr |= SIMR_FE;
	if (ut_info->si_info.simr_gm)
		sixmr |= SIMR_GM;

	switch (tdm_port) {
	case 0:
		iowrite16be(sixmr, &si_regs->sixmr1[0]);
		break;
	case 1:
		iowrite16be(sixmr, &si_regs->sixmr1[1]);
		break;
	case 2:
		iowrite16be(sixmr, &si_regs->sixmr1[2]);
		break;
	case 3:
		iowrite16be(sixmr, &si_regs->sixmr1[3]);
		break;
	default:
		pr_err("QE-TDM: can not find tdm sixmr reg\n");
		break;
	}
}
