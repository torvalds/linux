/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/device.h>
#include <linux/vmalloc.h>

#include "hns_dsaf_main.h"
#include "hns_dsaf_rcb.h"
#include "hns_dsaf_ppe.h"
#include "hns_dsaf_mac.h"

const char *g_dsaf_mode_match[DSAF_MODE_MAX] = {
	[DSAF_MODE_DISABLE_2PORT_64VM] = "2port-64vf",
	[DSAF_MODE_DISABLE_6PORT_0VM] = "6port-16rss",
	[DSAF_MODE_DISABLE_6PORT_16VM] = "6port-16vf",
};

int hns_dsaf_get_cfg(struct dsaf_device *dsaf_dev)
{
	int ret, i;
	u32 desc_num;
	u32 buf_size;
	const char *mode_str;
	struct device_node *np = dsaf_dev->dev->of_node;

	if (of_device_is_compatible(np, "hisilicon,hns-dsaf-v1"))
		dsaf_dev->dsaf_ver = AE_VERSION_1;
	else
		dsaf_dev->dsaf_ver = AE_VERSION_2;

	ret = of_property_read_string(np, "mode", &mode_str);
	if (ret) {
		dev_err(dsaf_dev->dev, "get dsaf mode fail, ret=%d!\n", ret);
		return ret;
	}
	for (i = 0; i < DSAF_MODE_MAX; i++) {
		if (g_dsaf_mode_match[i] &&
		    !strcmp(mode_str, g_dsaf_mode_match[i]))
			break;
	}
	if (i >= DSAF_MODE_MAX ||
	    i == DSAF_MODE_INVALID || i == DSAF_MODE_ENABLE) {
		dev_err(dsaf_dev->dev,
			"%s prs mode str fail!\n", dsaf_dev->ae_dev.name);
		return -EINVAL;
	}
	dsaf_dev->dsaf_mode = (enum dsaf_mode)i;

	if (dsaf_dev->dsaf_mode > DSAF_MODE_ENABLE)
		dsaf_dev->dsaf_en = HRD_DSAF_NO_DSAF_MODE;
	else
		dsaf_dev->dsaf_en = HRD_DSAF_MODE;

	if ((i == DSAF_MODE_ENABLE_16VM) ||
	    (i == DSAF_MODE_DISABLE_2PORT_8VM) ||
	    (i == DSAF_MODE_DISABLE_6PORT_2VM))
		dsaf_dev->dsaf_tc_mode = HRD_DSAF_8TC_MODE;
	else
		dsaf_dev->dsaf_tc_mode = HRD_DSAF_4TC_MODE;

	dsaf_dev->sc_base = of_iomap(np, 0);
	if (!dsaf_dev->sc_base) {
		dev_err(dsaf_dev->dev,
			"%s of_iomap 0 fail!\n", dsaf_dev->ae_dev.name);
		ret = -ENOMEM;
		goto unmap_base_addr;
	}

	dsaf_dev->sds_base = of_iomap(np, 1);
	if (!dsaf_dev->sds_base) {
		dev_err(dsaf_dev->dev,
			"%s of_iomap 1 fail!\n", dsaf_dev->ae_dev.name);
		ret = -ENOMEM;
		goto unmap_base_addr;
	}

	dsaf_dev->ppe_base = of_iomap(np, 2);
	if (!dsaf_dev->ppe_base) {
		dev_err(dsaf_dev->dev,
			"%s of_iomap 2 fail!\n", dsaf_dev->ae_dev.name);
		ret = -ENOMEM;
		goto unmap_base_addr;
	}

	dsaf_dev->io_base = of_iomap(np, 3);
	if (!dsaf_dev->io_base) {
		dev_err(dsaf_dev->dev,
			"%s of_iomap 3 fail!\n", dsaf_dev->ae_dev.name);
		ret = -ENOMEM;
		goto unmap_base_addr;
	}

	dsaf_dev->cpld_base = of_iomap(np, 4);
	if (!dsaf_dev->cpld_base)
		dev_dbg(dsaf_dev->dev, "NO CPLD ADDR");

	ret = of_property_read_u32(np, "desc-num", &desc_num);
	if (ret < 0 || desc_num < HNS_DSAF_MIN_DESC_CNT ||
	    desc_num > HNS_DSAF_MAX_DESC_CNT) {
		dev_err(dsaf_dev->dev, "get desc-num(%d) fail, ret=%d!\n",
			desc_num, ret);
		goto unmap_base_addr;
	}
	dsaf_dev->desc_num = desc_num;

	ret = of_property_read_u32(np, "buf-size", &buf_size);
	if (ret < 0) {
		dev_err(dsaf_dev->dev,
			"get buf-size fail, ret=%d!\r\n", ret);
		goto unmap_base_addr;
	}
	dsaf_dev->buf_size = buf_size;

	dsaf_dev->buf_size_type = hns_rcb_buf_size2type(buf_size);
	if (dsaf_dev->buf_size_type < 0) {
		dev_err(dsaf_dev->dev,
			"buf_size(%d) is wrong!\n", buf_size);
		goto unmap_base_addr;
	}

	if (!dma_set_mask_and_coherent(dsaf_dev->dev, DMA_BIT_MASK(64ULL)))
		dev_dbg(dsaf_dev->dev, "set mask to 64bit\n");
	else
		dev_err(dsaf_dev->dev, "set mask to 64bit fail!\n");

	return 0;

unmap_base_addr:
	if (dsaf_dev->io_base)
		iounmap(dsaf_dev->io_base);
	if (dsaf_dev->ppe_base)
		iounmap(dsaf_dev->ppe_base);
	if (dsaf_dev->sds_base)
		iounmap(dsaf_dev->sds_base);
	if (dsaf_dev->sc_base)
		iounmap(dsaf_dev->sc_base);
	if (dsaf_dev->cpld_base)
		iounmap(dsaf_dev->cpld_base);
	return ret;
}

static void hns_dsaf_free_cfg(struct dsaf_device *dsaf_dev)
{
	if (dsaf_dev->io_base)
		iounmap(dsaf_dev->io_base);

	if (dsaf_dev->ppe_base)
		iounmap(dsaf_dev->ppe_base);

	if (dsaf_dev->sds_base)
		iounmap(dsaf_dev->sds_base);

	if (dsaf_dev->sc_base)
		iounmap(dsaf_dev->sc_base);

	if (dsaf_dev->cpld_base)
		iounmap(dsaf_dev->cpld_base);
}

/**
 * hns_dsaf_sbm_link_sram_init_en - config dsaf_sbm_init_en
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_sbm_link_sram_init_en(struct dsaf_device *dsaf_dev)
{
	dsaf_set_dev_bit(dsaf_dev, DSAF_CFG_0_REG, DSAF_CFG_SBM_INIT_S, 1);
}

/**
 * hns_dsaf_reg_cnt_clr_ce - config hns_dsaf_reg_cnt_clr_ce
 * @dsaf_id: dsa fabric id
 * @hns_dsaf_reg_cnt_clr_ce: config value
 */
static void
hns_dsaf_reg_cnt_clr_ce(struct dsaf_device *dsaf_dev, u32 reg_cnt_clr_ce)
{
	dsaf_set_dev_bit(dsaf_dev, DSAF_DSA_REG_CNT_CLR_CE_REG,
			 DSAF_CNT_CLR_CE_S, reg_cnt_clr_ce);
}

/**
 * hns_ppe_qid_cfg - config ppe qid
 * @dsaf_id: dsa fabric id
 * @pppe_qid_cfg: value array
 */
static void
hns_dsaf_ppe_qid_cfg(struct dsaf_device *dsaf_dev, u32 qid_cfg)
{
	u32 i;

	for (i = 0; i < DSAF_COMM_CHN; i++) {
		dsaf_set_dev_field(dsaf_dev,
				   DSAF_PPE_QID_CFG_0_REG + 0x0004 * i,
				   DSAF_PPE_QID_CFG_M, DSAF_PPE_QID_CFG_S,
				   qid_cfg);
	}
}

static void hns_dsaf_mix_def_qid_cfg(struct dsaf_device *dsaf_dev)
{
	u16 max_q_per_vf, max_vfn;
	u32 q_id, q_num_per_port;
	u32 i;

	hns_rcb_get_queue_mode(dsaf_dev->dsaf_mode,
			       HNS_DSAF_COMM_SERVICE_NW_IDX,
			       &max_vfn, &max_q_per_vf);
	q_num_per_port = max_vfn * max_q_per_vf;

	for (i = 0, q_id = 0; i < DSAF_SERVICE_NW_NUM; i++) {
		dsaf_set_dev_field(dsaf_dev,
				   DSAF_MIX_DEF_QID_0_REG + 0x0004 * i,
				   0xff, 0, q_id);
		q_id += q_num_per_port;
	}
}

static void hns_dsaf_inner_qid_cfg(struct dsaf_device *dsaf_dev)
{
	u16 max_q_per_vf, max_vfn;
	u32 q_id, q_num_per_port;
	u32 mac_id;

	if (AE_IS_VER1(dsaf_dev->dsaf_ver))
		return;

	hns_rcb_get_queue_mode(dsaf_dev->dsaf_mode,
			       HNS_DSAF_COMM_SERVICE_NW_IDX,
			       &max_vfn, &max_q_per_vf);
	q_num_per_port = max_vfn * max_q_per_vf;

	for (mac_id = 0, q_id = 0; mac_id < DSAF_SERVICE_NW_NUM; mac_id++) {
		dsaf_set_dev_field(dsaf_dev,
				   DSAFV2_SERDES_LBK_0_REG + 4 * mac_id,
				   DSAFV2_SERDES_LBK_QID_M,
				   DSAFV2_SERDES_LBK_QID_S,
				   q_id);
		q_id += q_num_per_port;
	}
}

/**
 * hns_dsaf_sw_port_type_cfg - cfg sw type
 * @dsaf_id: dsa fabric id
 * @psw_port_type: array
 */
static void hns_dsaf_sw_port_type_cfg(struct dsaf_device *dsaf_dev,
				      enum dsaf_sw_port_type port_type)
{
	u32 i;

	for (i = 0; i < DSAF_SW_PORT_NUM; i++) {
		dsaf_set_dev_field(dsaf_dev,
				   DSAF_SW_PORT_TYPE_0_REG + 0x0004 * i,
				   DSAF_SW_PORT_TYPE_M, DSAF_SW_PORT_TYPE_S,
				   port_type);
	}
}

/**
 * hns_dsaf_stp_port_type_cfg - cfg stp type
 * @dsaf_id: dsa fabric id
 * @pstp_port_type: array
 */
static void hns_dsaf_stp_port_type_cfg(struct dsaf_device *dsaf_dev,
				       enum dsaf_stp_port_type port_type)
{
	u32 i;

	for (i = 0; i < DSAF_COMM_CHN; i++) {
		dsaf_set_dev_field(dsaf_dev,
				   DSAF_STP_PORT_TYPE_0_REG + 0x0004 * i,
				   DSAF_STP_PORT_TYPE_M, DSAF_STP_PORT_TYPE_S,
				   port_type);
	}
}

#define HNS_DSAF_SBM_NUM(dev) \
	(AE_IS_VER1((dev)->dsaf_ver) ? DSAF_SBM_NUM : DSAFV2_SBM_NUM)
/**
 * hns_dsaf_sbm_cfg - config sbm
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_sbm_cfg(struct dsaf_device *dsaf_dev)
{
	u32 o_sbm_cfg;
	u32 i;

	for (i = 0; i < HNS_DSAF_SBM_NUM(dsaf_dev); i++) {
		o_sbm_cfg = dsaf_read_dev(dsaf_dev,
					  DSAF_SBM_CFG_REG_0_REG + 0x80 * i);
		dsaf_set_bit(o_sbm_cfg, DSAF_SBM_CFG_EN_S, 1);
		dsaf_set_bit(o_sbm_cfg, DSAF_SBM_CFG_SHCUT_EN_S, 0);
		dsaf_write_dev(dsaf_dev,
			       DSAF_SBM_CFG_REG_0_REG + 0x80 * i, o_sbm_cfg);
	}
}

/**
 * hns_dsaf_sbm_cfg_mib_en - config sbm
 * @dsaf_id: dsa fabric id
 */
static int hns_dsaf_sbm_cfg_mib_en(struct dsaf_device *dsaf_dev)
{
	u32 sbm_cfg_mib_en;
	u32 i;
	u32 reg;
	u32 read_cnt;

	/* validate configure by setting SBM_CFG_MIB_EN bit from 0 to 1. */
	for (i = 0; i < HNS_DSAF_SBM_NUM(dsaf_dev); i++) {
		reg = DSAF_SBM_CFG_REG_0_REG + 0x80 * i;
		dsaf_set_dev_bit(dsaf_dev, reg, DSAF_SBM_CFG_MIB_EN_S, 0);
	}

	for (i = 0; i < HNS_DSAF_SBM_NUM(dsaf_dev); i++) {
		reg = DSAF_SBM_CFG_REG_0_REG + 0x80 * i;
		dsaf_set_dev_bit(dsaf_dev, reg, DSAF_SBM_CFG_MIB_EN_S, 1);
	}

	/* waitint for all sbm enable finished */
	for (i = 0; i < HNS_DSAF_SBM_NUM(dsaf_dev); i++) {
		read_cnt = 0;
		reg = DSAF_SBM_CFG_REG_0_REG + 0x80 * i;
		do {
			udelay(1);
			sbm_cfg_mib_en = dsaf_get_dev_bit(
					dsaf_dev, reg, DSAF_SBM_CFG_MIB_EN_S);
			read_cnt++;
		} while (sbm_cfg_mib_en == 0 &&
			read_cnt < DSAF_CFG_READ_CNT);

		if (sbm_cfg_mib_en == 0) {
			dev_err(dsaf_dev->dev,
				"sbm_cfg_mib_en fail,%s,sbm_num=%d\n",
				dsaf_dev->ae_dev.name, i);
			return -ENODEV;
		}
	}

	return 0;
}

/**
 * hns_dsaf_sbm_bp_wl_cfg - config sbm
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_sbm_bp_wl_cfg(struct dsaf_device *dsaf_dev)
{
	u32 o_sbm_bp_cfg;
	u32 reg;
	u32 i;

	/* XGE */
	for (i = 0; i < DSAF_XGE_NUM; i++) {
		reg = DSAF_SBM_BP_CFG_0_XGE_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg, DSAF_SBM_CFG0_COM_MAX_BUF_NUM_M,
			       DSAF_SBM_CFG0_COM_MAX_BUF_NUM_S, 512);
		dsaf_set_field(o_sbm_bp_cfg, DSAF_SBM_CFG0_VC0_MAX_BUF_NUM_M,
			       DSAF_SBM_CFG0_VC0_MAX_BUF_NUM_S, 0);
		dsaf_set_field(o_sbm_bp_cfg, DSAF_SBM_CFG0_VC1_MAX_BUF_NUM_M,
			       DSAF_SBM_CFG0_VC1_MAX_BUF_NUM_S, 0);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);

		reg = DSAF_SBM_BP_CFG_1_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg, DSAF_SBM_CFG1_TC4_MAX_BUF_NUM_M,
			       DSAF_SBM_CFG1_TC4_MAX_BUF_NUM_S, 0);
		dsaf_set_field(o_sbm_bp_cfg, DSAF_SBM_CFG1_TC0_MAX_BUF_NUM_M,
			       DSAF_SBM_CFG1_TC0_MAX_BUF_NUM_S, 0);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);

		reg = DSAF_SBM_BP_CFG_2_XGE_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg, DSAF_SBM_CFG2_SET_BUF_NUM_M,
			       DSAF_SBM_CFG2_SET_BUF_NUM_S, 104);
		dsaf_set_field(o_sbm_bp_cfg, DSAF_SBM_CFG2_RESET_BUF_NUM_M,
			       DSAF_SBM_CFG2_RESET_BUF_NUM_S, 128);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);

		reg = DSAF_SBM_BP_CFG_3_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg,
			       DSAF_SBM_CFG3_SET_BUF_NUM_NO_PFC_M,
			       DSAF_SBM_CFG3_SET_BUF_NUM_NO_PFC_S, 110);
		dsaf_set_field(o_sbm_bp_cfg,
			       DSAF_SBM_CFG3_RESET_BUF_NUM_NO_PFC_M,
			       DSAF_SBM_CFG3_RESET_BUF_NUM_NO_PFC_S, 160);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);

		/* for no enable pfc mode */
		reg = DSAF_SBM_BP_CFG_4_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg,
			       DSAF_SBM_CFG3_SET_BUF_NUM_NO_PFC_M,
			       DSAF_SBM_CFG3_SET_BUF_NUM_NO_PFC_S, 128);
		dsaf_set_field(o_sbm_bp_cfg,
			       DSAF_SBM_CFG3_RESET_BUF_NUM_NO_PFC_M,
			       DSAF_SBM_CFG3_RESET_BUF_NUM_NO_PFC_S, 192);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);
	}

	/* PPE */
	for (i = 0; i < DSAF_COMM_CHN; i++) {
		reg = DSAF_SBM_BP_CFG_2_PPE_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg, DSAF_SBM_CFG2_SET_BUF_NUM_M,
			       DSAF_SBM_CFG2_SET_BUF_NUM_S, 10);
		dsaf_set_field(o_sbm_bp_cfg, DSAF_SBM_CFG2_RESET_BUF_NUM_M,
			       DSAF_SBM_CFG2_RESET_BUF_NUM_S, 12);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);
	}

	/* RoCEE */
	for (i = 0; i < DSAF_COMM_CHN; i++) {
		reg = DSAF_SBM_BP_CFG_2_ROCEE_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg, DSAF_SBM_CFG2_SET_BUF_NUM_M,
			       DSAF_SBM_CFG2_SET_BUF_NUM_S, 2);
		dsaf_set_field(o_sbm_bp_cfg, DSAF_SBM_CFG2_RESET_BUF_NUM_M,
			       DSAF_SBM_CFG2_RESET_BUF_NUM_S, 4);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);
	}
}

static void hns_dsafv2_sbm_bp_wl_cfg(struct dsaf_device *dsaf_dev)
{
	u32 o_sbm_bp_cfg;
	u32 reg;
	u32 i;

	/* XGE */
	for (i = 0; i < DSAFV2_SBM_XGE_CHN; i++) {
		reg = DSAF_SBM_BP_CFG_0_XGE_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg, DSAFV2_SBM_CFG0_COM_MAX_BUF_NUM_M,
			       DSAFV2_SBM_CFG0_COM_MAX_BUF_NUM_S, 256);
		dsaf_set_field(o_sbm_bp_cfg, DSAFV2_SBM_CFG0_VC0_MAX_BUF_NUM_M,
			       DSAFV2_SBM_CFG0_VC0_MAX_BUF_NUM_S, 0);
		dsaf_set_field(o_sbm_bp_cfg, DSAFV2_SBM_CFG0_VC1_MAX_BUF_NUM_M,
			       DSAFV2_SBM_CFG0_VC1_MAX_BUF_NUM_S, 0);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);

		reg = DSAF_SBM_BP_CFG_1_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg, DSAFV2_SBM_CFG1_TC4_MAX_BUF_NUM_M,
			       DSAFV2_SBM_CFG1_TC4_MAX_BUF_NUM_S, 0);
		dsaf_set_field(o_sbm_bp_cfg, DSAFV2_SBM_CFG1_TC0_MAX_BUF_NUM_M,
			       DSAFV2_SBM_CFG1_TC0_MAX_BUF_NUM_S, 0);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);

		reg = DSAF_SBM_BP_CFG_2_XGE_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg, DSAFV2_SBM_CFG2_SET_BUF_NUM_M,
			       DSAFV2_SBM_CFG2_SET_BUF_NUM_S, 104);
		dsaf_set_field(o_sbm_bp_cfg, DSAFV2_SBM_CFG2_RESET_BUF_NUM_M,
			       DSAFV2_SBM_CFG2_RESET_BUF_NUM_S, 128);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);

		reg = DSAF_SBM_BP_CFG_3_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg,
			       DSAFV2_SBM_CFG3_SET_BUF_NUM_NO_PFC_M,
			       DSAFV2_SBM_CFG3_SET_BUF_NUM_NO_PFC_S, 110);
		dsaf_set_field(o_sbm_bp_cfg,
			       DSAFV2_SBM_CFG3_RESET_BUF_NUM_NO_PFC_M,
			       DSAFV2_SBM_CFG3_RESET_BUF_NUM_NO_PFC_S, 160);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);

		/* for no enable pfc mode */
		reg = DSAF_SBM_BP_CFG_4_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg,
			       DSAFV2_SBM_CFG4_SET_BUF_NUM_NO_PFC_M,
			       DSAFV2_SBM_CFG4_SET_BUF_NUM_NO_PFC_S, 128);
		dsaf_set_field(o_sbm_bp_cfg,
			       DSAFV2_SBM_CFG4_RESET_BUF_NUM_NO_PFC_M,
			       DSAFV2_SBM_CFG4_RESET_BUF_NUM_NO_PFC_S, 192);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);
	}

	/* PPE */
	reg = DSAF_SBM_BP_CFG_2_PPE_REG_0_REG + 0x80 * i;
	o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
	dsaf_set_field(o_sbm_bp_cfg, DSAFV2_SBM_CFG2_SET_BUF_NUM_M,
		       DSAFV2_SBM_CFG2_SET_BUF_NUM_S, 10);
	dsaf_set_field(o_sbm_bp_cfg, DSAFV2_SBM_CFG2_RESET_BUF_NUM_M,
		       DSAFV2_SBM_CFG2_RESET_BUF_NUM_S, 12);
	dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);
	/* RoCEE */
	for (i = 0; i < DASFV2_ROCEE_CRD_NUM; i++) {
		reg = DSAFV2_SBM_BP_CFG_2_ROCEE_REG_0_REG + 0x80 * i;
		o_sbm_bp_cfg = dsaf_read_dev(dsaf_dev, reg);
		dsaf_set_field(o_sbm_bp_cfg, DSAFV2_SBM_CFG2_SET_BUF_NUM_M,
			       DSAFV2_SBM_CFG2_SET_BUF_NUM_S, 2);
		dsaf_set_field(o_sbm_bp_cfg, DSAFV2_SBM_CFG2_RESET_BUF_NUM_M,
			       DSAFV2_SBM_CFG2_RESET_BUF_NUM_S, 4);
		dsaf_write_dev(dsaf_dev, reg, o_sbm_bp_cfg);
	}
}

/**
 * hns_dsaf_voq_bp_all_thrd_cfg -  voq
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_voq_bp_all_thrd_cfg(struct dsaf_device *dsaf_dev)
{
	u32 voq_bp_all_thrd;
	u32 i;

	for (i = 0; i < DSAF_VOQ_NUM; i++) {
		voq_bp_all_thrd = dsaf_read_dev(
			dsaf_dev, DSAF_VOQ_BP_ALL_THRD_0_REG + 0x40 * i);
		if (i < DSAF_XGE_NUM) {
			dsaf_set_field(voq_bp_all_thrd,
				       DSAF_VOQ_BP_ALL_DOWNTHRD_M,
				       DSAF_VOQ_BP_ALL_DOWNTHRD_S, 930);
			dsaf_set_field(voq_bp_all_thrd,
				       DSAF_VOQ_BP_ALL_UPTHRD_M,
				       DSAF_VOQ_BP_ALL_UPTHRD_S, 950);
		} else {
			dsaf_set_field(voq_bp_all_thrd,
				       DSAF_VOQ_BP_ALL_DOWNTHRD_M,
				       DSAF_VOQ_BP_ALL_DOWNTHRD_S, 220);
			dsaf_set_field(voq_bp_all_thrd,
				       DSAF_VOQ_BP_ALL_UPTHRD_M,
				       DSAF_VOQ_BP_ALL_UPTHRD_S, 230);
		}
		dsaf_write_dev(
			dsaf_dev, DSAF_VOQ_BP_ALL_THRD_0_REG + 0x40 * i,
			voq_bp_all_thrd);
	}
}

/**
 * hns_dsaf_tbl_tcam_data_cfg - tbl
 * @dsaf_id: dsa fabric id
 * @ptbl_tcam_data: addr
 */
static void hns_dsaf_tbl_tcam_data_cfg(
	struct dsaf_device *dsaf_dev,
	struct dsaf_tbl_tcam_data *ptbl_tcam_data)
{
	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_LOW_0_REG,
		       ptbl_tcam_data->tbl_tcam_data_low);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_HIGH_0_REG,
		       ptbl_tcam_data->tbl_tcam_data_high);
}

/**
 * dsaf_tbl_tcam_mcast_cfg - tbl
 * @dsaf_id: dsa fabric id
 * @ptbl_tcam_mcast: addr
 */
static void hns_dsaf_tbl_tcam_mcast_cfg(
	struct dsaf_device *dsaf_dev,
	struct dsaf_tbl_tcam_mcast_cfg *mcast)
{
	u32 mcast_cfg4;

	mcast_cfg4 = dsaf_read_dev(dsaf_dev, DSAF_TBL_TCAM_MCAST_CFG_4_0_REG);
	dsaf_set_bit(mcast_cfg4, DSAF_TBL_MCAST_CFG4_ITEM_VLD_S,
		     mcast->tbl_mcast_item_vld);
	dsaf_set_bit(mcast_cfg4, DSAF_TBL_MCAST_CFG4_OLD_EN_S,
		     mcast->tbl_mcast_old_en);
	dsaf_set_field(mcast_cfg4, DSAF_TBL_MCAST_CFG4_VM128_112_M,
		       DSAF_TBL_MCAST_CFG4_VM128_112_S,
		       mcast->tbl_mcast_port_msk[4]);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_MCAST_CFG_4_0_REG, mcast_cfg4);

	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_MCAST_CFG_3_0_REG,
		       mcast->tbl_mcast_port_msk[3]);

	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_MCAST_CFG_2_0_REG,
		       mcast->tbl_mcast_port_msk[2]);

	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_MCAST_CFG_1_0_REG,
		       mcast->tbl_mcast_port_msk[1]);

	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_MCAST_CFG_0_0_REG,
		       mcast->tbl_mcast_port_msk[0]);
}

/**
 * hns_dsaf_tbl_tcam_ucast_cfg - tbl
 * @dsaf_id: dsa fabric id
 * @ptbl_tcam_ucast: addr
 */
static void hns_dsaf_tbl_tcam_ucast_cfg(
	struct dsaf_device *dsaf_dev,
	struct dsaf_tbl_tcam_ucast_cfg *tbl_tcam_ucast)
{
	u32 ucast_cfg1;

	ucast_cfg1 = dsaf_read_dev(dsaf_dev, DSAF_TBL_TCAM_UCAST_CFG_0_REG);
	dsaf_set_bit(ucast_cfg1, DSAF_TBL_UCAST_CFG1_MAC_DISCARD_S,
		     tbl_tcam_ucast->tbl_ucast_mac_discard);
	dsaf_set_bit(ucast_cfg1, DSAF_TBL_UCAST_CFG1_ITEM_VLD_S,
		     tbl_tcam_ucast->tbl_ucast_item_vld);
	dsaf_set_bit(ucast_cfg1, DSAF_TBL_UCAST_CFG1_OLD_EN_S,
		     tbl_tcam_ucast->tbl_ucast_old_en);
	dsaf_set_bit(ucast_cfg1, DSAF_TBL_UCAST_CFG1_DVC_S,
		     tbl_tcam_ucast->tbl_ucast_dvc);
	dsaf_set_field(ucast_cfg1, DSAF_TBL_UCAST_CFG1_OUT_PORT_M,
		       DSAF_TBL_UCAST_CFG1_OUT_PORT_S,
		       tbl_tcam_ucast->tbl_ucast_out_port);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_UCAST_CFG_0_REG, ucast_cfg1);
}

/**
 * hns_dsaf_tbl_line_cfg - tbl
 * @dsaf_id: dsa fabric id
 * @ptbl_lin: addr
 */
static void hns_dsaf_tbl_line_cfg(struct dsaf_device *dsaf_dev,
				  struct dsaf_tbl_line_cfg *tbl_lin)
{
	u32 tbl_line;

	tbl_line = dsaf_read_dev(dsaf_dev, DSAF_TBL_LIN_CFG_0_REG);
	dsaf_set_bit(tbl_line, DSAF_TBL_LINE_CFG_MAC_DISCARD_S,
		     tbl_lin->tbl_line_mac_discard);
	dsaf_set_bit(tbl_line, DSAF_TBL_LINE_CFG_DVC_S,
		     tbl_lin->tbl_line_dvc);
	dsaf_set_field(tbl_line, DSAF_TBL_LINE_CFG_OUT_PORT_M,
		       DSAF_TBL_LINE_CFG_OUT_PORT_S,
		       tbl_lin->tbl_line_out_port);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_LIN_CFG_0_REG, tbl_line);
}

/**
 * hns_dsaf_tbl_tcam_mcast_pul - tbl
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_tbl_tcam_mcast_pul(struct dsaf_device *dsaf_dev)
{
	u32 o_tbl_pul;

	o_tbl_pul = dsaf_read_dev(dsaf_dev, DSAF_TBL_PUL_0_REG);
	dsaf_set_bit(o_tbl_pul, DSAF_TBL_PUL_MCAST_VLD_S, 1);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_PUL_0_REG, o_tbl_pul);
	dsaf_set_bit(o_tbl_pul, DSAF_TBL_PUL_MCAST_VLD_S, 0);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_PUL_0_REG, o_tbl_pul);
}

/**
 * hns_dsaf_tbl_line_pul - tbl
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_tbl_line_pul(struct dsaf_device *dsaf_dev)
{
	u32 tbl_pul;

	tbl_pul = dsaf_read_dev(dsaf_dev, DSAF_TBL_PUL_0_REG);
	dsaf_set_bit(tbl_pul, DSAF_TBL_PUL_LINE_VLD_S, 1);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_PUL_0_REG, tbl_pul);
	dsaf_set_bit(tbl_pul, DSAF_TBL_PUL_LINE_VLD_S, 0);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_PUL_0_REG, tbl_pul);
}

/**
 * hns_dsaf_tbl_tcam_data_mcast_pul - tbl
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_tbl_tcam_data_mcast_pul(
	struct dsaf_device *dsaf_dev)
{
	u32 o_tbl_pul;

	o_tbl_pul = dsaf_read_dev(dsaf_dev, DSAF_TBL_PUL_0_REG);
	dsaf_set_bit(o_tbl_pul, DSAF_TBL_PUL_TCAM_DATA_VLD_S, 1);
	dsaf_set_bit(o_tbl_pul, DSAF_TBL_PUL_MCAST_VLD_S, 1);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_PUL_0_REG, o_tbl_pul);
	dsaf_set_bit(o_tbl_pul, DSAF_TBL_PUL_TCAM_DATA_VLD_S, 0);
	dsaf_set_bit(o_tbl_pul, DSAF_TBL_PUL_MCAST_VLD_S, 0);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_PUL_0_REG, o_tbl_pul);
}

/**
 * hns_dsaf_tbl_tcam_data_ucast_pul - tbl
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_tbl_tcam_data_ucast_pul(
	struct dsaf_device *dsaf_dev)
{
	u32 o_tbl_pul;

	o_tbl_pul = dsaf_read_dev(dsaf_dev, DSAF_TBL_PUL_0_REG);
	dsaf_set_bit(o_tbl_pul, DSAF_TBL_PUL_TCAM_DATA_VLD_S, 1);
	dsaf_set_bit(o_tbl_pul, DSAF_TBL_PUL_UCAST_VLD_S, 1);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_PUL_0_REG, o_tbl_pul);
	dsaf_set_bit(o_tbl_pul, DSAF_TBL_PUL_TCAM_DATA_VLD_S, 0);
	dsaf_set_bit(o_tbl_pul, DSAF_TBL_PUL_UCAST_VLD_S, 0);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_PUL_0_REG, o_tbl_pul);
}

void hns_dsaf_set_promisc_mode(struct dsaf_device *dsaf_dev, u32 en)
{
	dsaf_set_dev_bit(dsaf_dev, DSAF_CFG_0_REG, DSAF_CFG_MIX_MODE_S, !!en);
}

void hns_dsaf_set_inner_lb(struct dsaf_device *dsaf_dev, u32 mac_id, u32 en)
{
	if (AE_IS_VER1(dsaf_dev->dsaf_ver) ||
	    dsaf_dev->mac_cb[mac_id].mac_type == HNAE_PORT_DEBUG)
		return;

	dsaf_set_dev_bit(dsaf_dev, DSAFV2_SERDES_LBK_0_REG + 4 * mac_id,
			 DSAFV2_SERDES_LBK_EN_B, !!en);
}

/**
 * hns_dsaf_tbl_stat_en - tbl
 * @dsaf_id: dsa fabric id
 * @ptbl_stat_en: addr
 */
static void hns_dsaf_tbl_stat_en(struct dsaf_device *dsaf_dev)
{
	u32 o_tbl_ctrl;

	o_tbl_ctrl = dsaf_read_dev(dsaf_dev, DSAF_TBL_DFX_CTRL_0_REG);
	dsaf_set_bit(o_tbl_ctrl, DSAF_TBL_DFX_LINE_LKUP_NUM_EN_S, 1);
	dsaf_set_bit(o_tbl_ctrl, DSAF_TBL_DFX_UC_LKUP_NUM_EN_S, 1);
	dsaf_set_bit(o_tbl_ctrl, DSAF_TBL_DFX_MC_LKUP_NUM_EN_S, 1);
	dsaf_set_bit(o_tbl_ctrl, DSAF_TBL_DFX_BC_LKUP_NUM_EN_S, 1);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_DFX_CTRL_0_REG, o_tbl_ctrl);
}

/**
 * hns_dsaf_rocee_bp_en - rocee back press enable
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_rocee_bp_en(struct dsaf_device *dsaf_dev)
{
	if (AE_IS_VER1(dsaf_dev->dsaf_ver))
		dsaf_set_dev_bit(dsaf_dev, DSAF_XGE_CTRL_SIG_CFG_0_REG,
				 DSAF_FC_XGE_TX_PAUSE_S, 1);
}

/* set msk for dsaf exception irq*/
static void hns_dsaf_int_xge_msk_set(struct dsaf_device *dsaf_dev,
				     u32 chnn_num, u32 mask_set)
{
	dsaf_write_dev(dsaf_dev,
		       DSAF_XGE_INT_MSK_0_REG + 0x4 * chnn_num, mask_set);
}

static void hns_dsaf_int_ppe_msk_set(struct dsaf_device *dsaf_dev,
				     u32 chnn_num, u32 msk_set)
{
	dsaf_write_dev(dsaf_dev,
		       DSAF_PPE_INT_MSK_0_REG + 0x4 * chnn_num, msk_set);
}

static void hns_dsaf_int_rocee_msk_set(struct dsaf_device *dsaf_dev,
				       u32 chnn, u32 msk_set)
{
	dsaf_write_dev(dsaf_dev,
		       DSAF_ROCEE_INT_MSK_0_REG + 0x4 * chnn, msk_set);
}

static void
hns_dsaf_int_tbl_msk_set(struct dsaf_device *dsaf_dev, u32 msk_set)
{
	dsaf_write_dev(dsaf_dev, DSAF_TBL_INT_MSK_0_REG, msk_set);
}

/* clr dsaf exception irq*/
static void hns_dsaf_int_xge_src_clr(struct dsaf_device *dsaf_dev,
				     u32 chnn_num, u32 int_src)
{
	dsaf_write_dev(dsaf_dev,
		       DSAF_XGE_INT_SRC_0_REG + 0x4 * chnn_num, int_src);
}

static void hns_dsaf_int_ppe_src_clr(struct dsaf_device *dsaf_dev,
				     u32 chnn, u32 int_src)
{
	dsaf_write_dev(dsaf_dev,
		       DSAF_PPE_INT_SRC_0_REG + 0x4 * chnn, int_src);
}

static void hns_dsaf_int_rocee_src_clr(struct dsaf_device *dsaf_dev,
				       u32 chnn, u32 int_src)
{
	dsaf_write_dev(dsaf_dev,
		       DSAF_ROCEE_INT_SRC_0_REG + 0x4 * chnn, int_src);
}

static void hns_dsaf_int_tbl_src_clr(struct dsaf_device *dsaf_dev,
				     u32 int_src)
{
	dsaf_write_dev(dsaf_dev, DSAF_TBL_INT_SRC_0_REG, int_src);
}

/**
 * hns_dsaf_single_line_tbl_cfg - INT
 * @dsaf_id: dsa fabric id
 * @address:
 * @ptbl_line:
 */
static void hns_dsaf_single_line_tbl_cfg(
	struct dsaf_device *dsaf_dev,
	u32 address, struct dsaf_tbl_line_cfg *ptbl_line)
{
	/*Write Addr*/
	hns_dsaf_tbl_line_addr_cfg(dsaf_dev, address);

	/*Write Line*/
	hns_dsaf_tbl_line_cfg(dsaf_dev, ptbl_line);

	/*Write Plus*/
	hns_dsaf_tbl_line_pul(dsaf_dev);
}

/**
 * hns_dsaf_tcam_uc_cfg - INT
 * @dsaf_id: dsa fabric id
 * @address,
 * @ptbl_tcam_data,
 */
static void hns_dsaf_tcam_uc_cfg(
	struct dsaf_device *dsaf_dev, u32 address,
	struct dsaf_tbl_tcam_data *ptbl_tcam_data,
	struct dsaf_tbl_tcam_ucast_cfg *ptbl_tcam_ucast)
{
	/*Write Addr*/
	hns_dsaf_tbl_tcam_addr_cfg(dsaf_dev, address);
	/*Write Tcam Data*/
	hns_dsaf_tbl_tcam_data_cfg(dsaf_dev, ptbl_tcam_data);
	/*Write Tcam Ucast*/
	hns_dsaf_tbl_tcam_ucast_cfg(dsaf_dev, ptbl_tcam_ucast);
	/*Write Plus*/
	hns_dsaf_tbl_tcam_data_ucast_pul(dsaf_dev);
}

/**
 * hns_dsaf_tcam_mc_cfg - INT
 * @dsaf_id: dsa fabric id
 * @address,
 * @ptbl_tcam_data,
 * @ptbl_tcam_mcast,
 */
static void hns_dsaf_tcam_mc_cfg(
	struct dsaf_device *dsaf_dev, u32 address,
	struct dsaf_tbl_tcam_data *ptbl_tcam_data,
	struct dsaf_tbl_tcam_mcast_cfg *ptbl_tcam_mcast)
{
	/*Write Addr*/
	hns_dsaf_tbl_tcam_addr_cfg(dsaf_dev, address);
	/*Write Tcam Data*/
	hns_dsaf_tbl_tcam_data_cfg(dsaf_dev, ptbl_tcam_data);
	/*Write Tcam Mcast*/
	hns_dsaf_tbl_tcam_mcast_cfg(dsaf_dev, ptbl_tcam_mcast);
	/*Write Plus*/
	hns_dsaf_tbl_tcam_data_mcast_pul(dsaf_dev);
}

/**
 * hns_dsaf_tcam_mc_invld - INT
 * @dsaf_id: dsa fabric id
 * @address
 */
static void hns_dsaf_tcam_mc_invld(struct dsaf_device *dsaf_dev, u32 address)
{
	/*Write Addr*/
	hns_dsaf_tbl_tcam_addr_cfg(dsaf_dev, address);

	/*write tcam mcast*/
	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_MCAST_CFG_0_0_REG, 0);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_MCAST_CFG_1_0_REG, 0);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_MCAST_CFG_2_0_REG, 0);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_MCAST_CFG_3_0_REG, 0);
	dsaf_write_dev(dsaf_dev, DSAF_TBL_TCAM_MCAST_CFG_4_0_REG, 0);

	/*Write Plus*/
	hns_dsaf_tbl_tcam_mcast_pul(dsaf_dev);
}

/**
 * hns_dsaf_tcam_uc_get - INT
 * @dsaf_id: dsa fabric id
 * @address
 * @ptbl_tcam_data
 * @ptbl_tcam_ucast
 */
static void hns_dsaf_tcam_uc_get(
	struct dsaf_device *dsaf_dev, u32 address,
	struct dsaf_tbl_tcam_data *ptbl_tcam_data,
	struct dsaf_tbl_tcam_ucast_cfg *ptbl_tcam_ucast)
{
	u32 tcam_read_data0;
	u32 tcam_read_data4;

	/*Write Addr*/
	hns_dsaf_tbl_tcam_addr_cfg(dsaf_dev, address);

	/*read tcam item puls*/
	hns_dsaf_tbl_tcam_load_pul(dsaf_dev);

	/*read tcam data*/
	ptbl_tcam_data->tbl_tcam_data_high
		= dsaf_read_dev(dsaf_dev, DSAF_TBL_TCAM_RDATA_LOW_0_REG);
	ptbl_tcam_data->tbl_tcam_data_low
		= dsaf_read_dev(dsaf_dev, DSAF_TBL_TCAM_RDATA_HIGH_0_REG);

	/*read tcam mcast*/
	tcam_read_data0 = dsaf_read_dev(dsaf_dev,
					DSAF_TBL_TCAM_RAM_RDATA0_0_REG);
	tcam_read_data4 = dsaf_read_dev(dsaf_dev,
					DSAF_TBL_TCAM_RAM_RDATA4_0_REG);

	ptbl_tcam_ucast->tbl_ucast_item_vld
		= dsaf_get_bit(tcam_read_data4,
			       DSAF_TBL_MCAST_CFG4_ITEM_VLD_S);
	ptbl_tcam_ucast->tbl_ucast_old_en
		= dsaf_get_bit(tcam_read_data4, DSAF_TBL_MCAST_CFG4_OLD_EN_S);
	ptbl_tcam_ucast->tbl_ucast_mac_discard
		= dsaf_get_bit(tcam_read_data0,
			       DSAF_TBL_UCAST_CFG1_MAC_DISCARD_S);
	ptbl_tcam_ucast->tbl_ucast_out_port
		= dsaf_get_field(tcam_read_data0,
				 DSAF_TBL_UCAST_CFG1_OUT_PORT_M,
				 DSAF_TBL_UCAST_CFG1_OUT_PORT_S);
	ptbl_tcam_ucast->tbl_ucast_dvc
		= dsaf_get_bit(tcam_read_data0, DSAF_TBL_UCAST_CFG1_DVC_S);
}

/**
 * hns_dsaf_tcam_mc_get - INT
 * @dsaf_id: dsa fabric id
 * @address
 * @ptbl_tcam_data
 * @ptbl_tcam_ucast
 */
static void hns_dsaf_tcam_mc_get(
	struct dsaf_device *dsaf_dev, u32 address,
	struct dsaf_tbl_tcam_data *ptbl_tcam_data,
	struct dsaf_tbl_tcam_mcast_cfg *ptbl_tcam_mcast)
{
	u32 data_tmp;

	/*Write Addr*/
	hns_dsaf_tbl_tcam_addr_cfg(dsaf_dev, address);

	/*read tcam item puls*/
	hns_dsaf_tbl_tcam_load_pul(dsaf_dev);

	/*read tcam data*/
	ptbl_tcam_data->tbl_tcam_data_high =
		dsaf_read_dev(dsaf_dev, DSAF_TBL_TCAM_RDATA_LOW_0_REG);
	ptbl_tcam_data->tbl_tcam_data_low =
		dsaf_read_dev(dsaf_dev, DSAF_TBL_TCAM_RDATA_HIGH_0_REG);

	/*read tcam mcast*/
	ptbl_tcam_mcast->tbl_mcast_port_msk[0] =
		dsaf_read_dev(dsaf_dev, DSAF_TBL_TCAM_RAM_RDATA0_0_REG);
	ptbl_tcam_mcast->tbl_mcast_port_msk[1] =
		dsaf_read_dev(dsaf_dev, DSAF_TBL_TCAM_RAM_RDATA1_0_REG);
	ptbl_tcam_mcast->tbl_mcast_port_msk[2] =
		dsaf_read_dev(dsaf_dev, DSAF_TBL_TCAM_RAM_RDATA2_0_REG);
	ptbl_tcam_mcast->tbl_mcast_port_msk[3] =
		dsaf_read_dev(dsaf_dev, DSAF_TBL_TCAM_RAM_RDATA3_0_REG);

	data_tmp = dsaf_read_dev(dsaf_dev, DSAF_TBL_TCAM_RAM_RDATA4_0_REG);
	ptbl_tcam_mcast->tbl_mcast_item_vld =
		dsaf_get_bit(data_tmp, DSAF_TBL_MCAST_CFG4_ITEM_VLD_S);
	ptbl_tcam_mcast->tbl_mcast_old_en =
		dsaf_get_bit(data_tmp, DSAF_TBL_MCAST_CFG4_OLD_EN_S);
	ptbl_tcam_mcast->tbl_mcast_port_msk[4] =
		dsaf_get_field(data_tmp, DSAF_TBL_MCAST_CFG4_VM128_112_M,
			       DSAF_TBL_MCAST_CFG4_VM128_112_S);
}

/**
 * hns_dsaf_tbl_line_init - INT
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_tbl_line_init(struct dsaf_device *dsaf_dev)
{
	u32 i;
	/* defaultly set all lineal mac table entry resulting discard */
	struct dsaf_tbl_line_cfg tbl_line[] = {{1, 0, 0} };

	for (i = 0; i < DSAF_LINE_SUM; i++)
		hns_dsaf_single_line_tbl_cfg(dsaf_dev, i, tbl_line);
}

/**
 * hns_dsaf_tbl_tcam_init - INT
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_tbl_tcam_init(struct dsaf_device *dsaf_dev)
{
	u32 i;
	struct dsaf_tbl_tcam_data tcam_data[] = {{0, 0} };
	struct dsaf_tbl_tcam_ucast_cfg tcam_ucast[] = {{0, 0, 0, 0, 0} };

	/*tcam tbl*/
	for (i = 0; i < DSAF_TCAM_SUM; i++)
		hns_dsaf_tcam_uc_cfg(dsaf_dev, i, tcam_data, tcam_ucast);
}

/**
 * hns_dsaf_pfc_en_cfg - dsaf pfc pause cfg
 * @mac_cb: mac contrl block
 */
static void hns_dsaf_pfc_en_cfg(struct dsaf_device *dsaf_dev,
				int mac_id, int en)
{
	if (!en)
		dsaf_write_dev(dsaf_dev, DSAF_PFC_EN_0_REG + mac_id * 4, 0);
	else
		dsaf_write_dev(dsaf_dev, DSAF_PFC_EN_0_REG + mac_id * 4, 0xff);
}

/**
 * hns_dsaf_tbl_tcam_init - INT
 * @dsaf_id: dsa fabric id
 * @dsaf_mode
 */
static void hns_dsaf_comm_init(struct dsaf_device *dsaf_dev)
{
	u32 i;
	u32 o_dsaf_cfg;

	o_dsaf_cfg = dsaf_read_dev(dsaf_dev, DSAF_CFG_0_REG);
	dsaf_set_bit(o_dsaf_cfg, DSAF_CFG_EN_S, dsaf_dev->dsaf_en);
	dsaf_set_bit(o_dsaf_cfg, DSAF_CFG_TC_MODE_S, dsaf_dev->dsaf_tc_mode);
	dsaf_set_bit(o_dsaf_cfg, DSAF_CFG_CRC_EN_S, 0);
	dsaf_set_bit(o_dsaf_cfg, DSAF_CFG_MIX_MODE_S, 0);
	dsaf_set_bit(o_dsaf_cfg, DSAF_CFG_LOCA_ADDR_EN_S, 0);
	dsaf_write_dev(dsaf_dev, DSAF_CFG_0_REG, o_dsaf_cfg);

	hns_dsaf_reg_cnt_clr_ce(dsaf_dev, 1);
	hns_dsaf_stp_port_type_cfg(dsaf_dev, DSAF_STP_PORT_TYPE_FORWARD);

	/* set 22 queue per tx ppe engine, only used in switch mode */
	hns_dsaf_ppe_qid_cfg(dsaf_dev, DSAF_DEFAUTL_QUEUE_NUM_PER_PPE);

	/* set promisc def queue id */
	hns_dsaf_mix_def_qid_cfg(dsaf_dev);

	/* set inner loopback queue id */
	hns_dsaf_inner_qid_cfg(dsaf_dev);

	/* in non switch mode, set all port to access mode */
	hns_dsaf_sw_port_type_cfg(dsaf_dev, DSAF_SW_PORT_TYPE_NON_VLAN);

	/*set dsaf pfc  to 0 for parseing rx pause*/
	for (i = 0; i < DSAF_COMM_CHN; i++)
		hns_dsaf_pfc_en_cfg(dsaf_dev, i, 0);

	/*msk and  clr exception irqs */
	for (i = 0; i < DSAF_COMM_CHN; i++) {
		hns_dsaf_int_xge_src_clr(dsaf_dev, i, 0xfffffffful);
		hns_dsaf_int_ppe_src_clr(dsaf_dev, i, 0xfffffffful);
		hns_dsaf_int_rocee_src_clr(dsaf_dev, i, 0xfffffffful);

		hns_dsaf_int_xge_msk_set(dsaf_dev, i, 0xfffffffful);
		hns_dsaf_int_ppe_msk_set(dsaf_dev, i, 0xfffffffful);
		hns_dsaf_int_rocee_msk_set(dsaf_dev, i, 0xfffffffful);
	}
	hns_dsaf_int_tbl_src_clr(dsaf_dev, 0xfffffffful);
	hns_dsaf_int_tbl_msk_set(dsaf_dev, 0xfffffffful);
}

/**
 * hns_dsaf_inode_init - INT
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_inode_init(struct dsaf_device *dsaf_dev)
{
	u32 reg;
	u32 tc_cfg;
	u32 i;

	if (dsaf_dev->dsaf_tc_mode == HRD_DSAF_4TC_MODE)
		tc_cfg = HNS_DSAF_I4TC_CFG;
	else
		tc_cfg = HNS_DSAF_I8TC_CFG;

	if (AE_IS_VER1(dsaf_dev->dsaf_ver)) {
		for (i = 0; i < DSAF_INODE_NUM; i++) {
			reg = DSAF_INODE_IN_PORT_NUM_0_REG + 0x80 * i;
			dsaf_set_dev_field(dsaf_dev, reg,
					   DSAF_INODE_IN_PORT_NUM_M,
					   DSAF_INODE_IN_PORT_NUM_S,
					   i % DSAF_XGE_NUM);
		}
	} else {
		for (i = 0; i < DSAF_PORT_TYPE_NUM; i++) {
			reg = DSAF_INODE_IN_PORT_NUM_0_REG + 0x80 * i;
			dsaf_set_dev_field(dsaf_dev, reg,
					   DSAF_INODE_IN_PORT_NUM_M,
					   DSAF_INODE_IN_PORT_NUM_S, 0);
			dsaf_set_dev_field(dsaf_dev, reg,
					   DSAFV2_INODE_IN_PORT1_NUM_M,
					   DSAFV2_INODE_IN_PORT1_NUM_S, 1);
			dsaf_set_dev_field(dsaf_dev, reg,
					   DSAFV2_INODE_IN_PORT2_NUM_M,
					   DSAFV2_INODE_IN_PORT2_NUM_S, 2);
			dsaf_set_dev_field(dsaf_dev, reg,
					   DSAFV2_INODE_IN_PORT3_NUM_M,
					   DSAFV2_INODE_IN_PORT3_NUM_S, 3);
			dsaf_set_dev_field(dsaf_dev, reg,
					   DSAFV2_INODE_IN_PORT4_NUM_M,
					   DSAFV2_INODE_IN_PORT4_NUM_S, 4);
			dsaf_set_dev_field(dsaf_dev, reg,
					   DSAFV2_INODE_IN_PORT5_NUM_M,
					   DSAFV2_INODE_IN_PORT5_NUM_S, 5);
		}
	}
	for (i = 0; i < DSAF_INODE_NUM; i++) {
		reg = DSAF_INODE_PRI_TC_CFG_0_REG + 0x80 * i;
		dsaf_write_dev(dsaf_dev, reg, tc_cfg);
	}
}

/**
 * hns_dsaf_sbm_init - INT
 * @dsaf_id: dsa fabric id
 */
static int hns_dsaf_sbm_init(struct dsaf_device *dsaf_dev)
{
	u32 flag;
	u32 finish_msk;
	u32 cnt = 0;
	int ret;

	if (AE_IS_VER1(dsaf_dev->dsaf_ver)) {
		hns_dsaf_sbm_bp_wl_cfg(dsaf_dev);
		finish_msk = DSAF_SRAM_INIT_OVER_M;
	} else {
		hns_dsafv2_sbm_bp_wl_cfg(dsaf_dev);
		finish_msk = DSAFV2_SRAM_INIT_OVER_M;
	}

	/* enable sbm chanel, disable sbm chanel shcut function*/
	hns_dsaf_sbm_cfg(dsaf_dev);

	/* enable sbm mib */
	ret = hns_dsaf_sbm_cfg_mib_en(dsaf_dev);
	if (ret) {
		dev_err(dsaf_dev->dev,
			"hns_dsaf_sbm_cfg_mib_en fail,%s, ret=%d\n",
			dsaf_dev->ae_dev.name, ret);
		return ret;
	}

	/* enable sbm initial link sram */
	hns_dsaf_sbm_link_sram_init_en(dsaf_dev);

	do {
		usleep_range(200, 210);/*udelay(200);*/
		flag = dsaf_get_dev_field(dsaf_dev, DSAF_SRAM_INIT_OVER_0_REG,
					  finish_msk, DSAF_SRAM_INIT_OVER_S);
		cnt++;
	} while (flag != (finish_msk >> DSAF_SRAM_INIT_OVER_S) &&
		 cnt < DSAF_CFG_READ_CNT);

	if (flag != (finish_msk >> DSAF_SRAM_INIT_OVER_S)) {
		dev_err(dsaf_dev->dev,
			"hns_dsaf_sbm_init fail %s, flag=%d, cnt=%d\n",
			dsaf_dev->ae_dev.name, flag, cnt);
		return -ENODEV;
	}

	hns_dsaf_rocee_bp_en(dsaf_dev);

	return 0;
}

/**
 * hns_dsaf_tbl_init - INT
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_tbl_init(struct dsaf_device *dsaf_dev)
{
	hns_dsaf_tbl_stat_en(dsaf_dev);

	hns_dsaf_tbl_tcam_init(dsaf_dev);
	hns_dsaf_tbl_line_init(dsaf_dev);
}

/**
 * hns_dsaf_voq_init - INT
 * @dsaf_id: dsa fabric id
 */
static void hns_dsaf_voq_init(struct dsaf_device *dsaf_dev)
{
	hns_dsaf_voq_bp_all_thrd_cfg(dsaf_dev);
}

/**
 * hns_dsaf_init_hw - init dsa fabric hardware
 * @dsaf_dev: dsa fabric device struct pointer
 */
static int hns_dsaf_init_hw(struct dsaf_device *dsaf_dev)
{
	int ret;

	dev_dbg(dsaf_dev->dev,
		"hns_dsaf_init_hw begin %s !\n", dsaf_dev->ae_dev.name);

	hns_dsaf_rst(dsaf_dev, 0);
	mdelay(10);
	hns_dsaf_rst(dsaf_dev, 1);

	hns_dsaf_comm_init(dsaf_dev);

	/*init XBAR_INODE*/
	hns_dsaf_inode_init(dsaf_dev);

	/*init SBM*/
	ret = hns_dsaf_sbm_init(dsaf_dev);
	if (ret)
		return ret;

	/*init TBL*/
	hns_dsaf_tbl_init(dsaf_dev);

	/*init VOQ*/
	hns_dsaf_voq_init(dsaf_dev);

	return 0;
}

/**
 * hns_dsaf_remove_hw - uninit dsa fabric hardware
 * @dsaf_dev: dsa fabric device struct pointer
 */
static void hns_dsaf_remove_hw(struct dsaf_device *dsaf_dev)
{
	/*reset*/
	hns_dsaf_rst(dsaf_dev, 0);
}

/**
 * hns_dsaf_init - init dsa fabric
 * @dsaf_dev: dsa fabric device struct pointer
 * retuen 0 - success , negative --fail
 */
static int hns_dsaf_init(struct dsaf_device *dsaf_dev)
{
	struct dsaf_drv_priv *priv =
	    (struct dsaf_drv_priv *)hns_dsaf_dev_priv(dsaf_dev);
	u32 i;
	int ret;

	ret = hns_dsaf_init_hw(dsaf_dev);
	if (ret)
		return ret;

	/* malloc mem for tcam mac key(vlan+mac) */
	priv->soft_mac_tbl = vzalloc(sizeof(*priv->soft_mac_tbl)
		  * DSAF_TCAM_SUM);
	if (!priv->soft_mac_tbl) {
		ret = -ENOMEM;
		goto remove_hw;
	}

	/*all entry invall */
	for (i = 0; i < DSAF_TCAM_SUM; i++)
		(priv->soft_mac_tbl + i)->index = DSAF_INVALID_ENTRY_IDX;

	return 0;

remove_hw:
	hns_dsaf_remove_hw(dsaf_dev);
	return ret;
}

/**
 * hns_dsaf_free - free dsa fabric
 * @dsaf_dev: dsa fabric device struct pointer
 */
static void hns_dsaf_free(struct dsaf_device *dsaf_dev)
{
	struct dsaf_drv_priv *priv =
	    (struct dsaf_drv_priv *)hns_dsaf_dev_priv(dsaf_dev);

	hns_dsaf_remove_hw(dsaf_dev);

	/* free all mac mem */
	vfree(priv->soft_mac_tbl);
	priv->soft_mac_tbl = NULL;
}

/**
 * hns_dsaf_find_soft_mac_entry - find dsa fabric soft entry
 * @dsaf_dev: dsa fabric device struct pointer
 * @mac_key: mac entry struct pointer
 */
static u16 hns_dsaf_find_soft_mac_entry(
	struct dsaf_device *dsaf_dev,
	struct dsaf_drv_tbl_tcam_key *mac_key)
{
	struct dsaf_drv_priv *priv =
	    (struct dsaf_drv_priv *)hns_dsaf_dev_priv(dsaf_dev);
	struct dsaf_drv_soft_mac_tbl *soft_mac_entry;
	u32 i;

	soft_mac_entry = priv->soft_mac_tbl;
	for (i = 0; i < DSAF_TCAM_SUM; i++) {
		/* invall tab entry */
		if ((soft_mac_entry->index != DSAF_INVALID_ENTRY_IDX) &&
		    (soft_mac_entry->tcam_key.high.val == mac_key->high.val) &&
		    (soft_mac_entry->tcam_key.low.val == mac_key->low.val))
			/* return find result --soft index */
			return soft_mac_entry->index;

		soft_mac_entry++;
	}
	return DSAF_INVALID_ENTRY_IDX;
}

/**
 * hns_dsaf_find_empty_mac_entry - search dsa fabric soft empty-entry
 * @dsaf_dev: dsa fabric device struct pointer
 */
static u16 hns_dsaf_find_empty_mac_entry(struct dsaf_device *dsaf_dev)
{
	struct dsaf_drv_priv *priv =
	    (struct dsaf_drv_priv *)hns_dsaf_dev_priv(dsaf_dev);
	struct dsaf_drv_soft_mac_tbl *soft_mac_entry;
	u32 i;

	soft_mac_entry = priv->soft_mac_tbl;
	for (i = 0; i < DSAF_TCAM_SUM; i++) {
		/* inv all entry */
		if (soft_mac_entry->index == DSAF_INVALID_ENTRY_IDX)
			/* return find result --soft index */
			return i;

		soft_mac_entry++;
	}
	return DSAF_INVALID_ENTRY_IDX;
}

/**
 * hns_dsaf_set_mac_key - set mac key
 * @dsaf_dev: dsa fabric device struct pointer
 * @mac_key: tcam key pointer
 * @vlan_id: vlan id
 * @in_port_num: input port num
 * @addr: mac addr
 */
static void hns_dsaf_set_mac_key(
	struct dsaf_device *dsaf_dev,
	struct dsaf_drv_tbl_tcam_key *mac_key, u16 vlan_id, u8 in_port_num,
	u8 *addr)
{
	u8 port;

	if (dsaf_dev->dsaf_mode <= DSAF_MODE_ENABLE)
		/*DSAF mode : in port id fixed 0*/
		port = 0;
	else
		/*non-dsaf mode*/
		port = in_port_num;

	mac_key->high.bits.mac_0 = addr[0];
	mac_key->high.bits.mac_1 = addr[1];
	mac_key->high.bits.mac_2 = addr[2];
	mac_key->high.bits.mac_3 = addr[3];
	mac_key->low.bits.mac_4 = addr[4];
	mac_key->low.bits.mac_5 = addr[5];
	mac_key->low.bits.vlan = vlan_id;
	mac_key->low.bits.port = port;
}

/**
 * hns_dsaf_set_mac_uc_entry - set mac uc-entry
 * @dsaf_dev: dsa fabric device struct pointer
 * @mac_entry: uc-mac entry
 */
int hns_dsaf_set_mac_uc_entry(
	struct dsaf_device *dsaf_dev,
	struct dsaf_drv_mac_single_dest_entry *mac_entry)
{
	u16 entry_index = DSAF_INVALID_ENTRY_IDX;
	struct dsaf_drv_tbl_tcam_key mac_key;
	struct dsaf_tbl_tcam_ucast_cfg mac_data;
	struct dsaf_drv_priv *priv =
	    (struct dsaf_drv_priv *)hns_dsaf_dev_priv(dsaf_dev);
	struct dsaf_drv_soft_mac_tbl *soft_mac_entry = priv->soft_mac_tbl;

	/* mac addr check */
	if (MAC_IS_ALL_ZEROS(mac_entry->addr) ||
	    MAC_IS_BROADCAST(mac_entry->addr) ||
	    MAC_IS_MULTICAST(mac_entry->addr)) {
		dev_err(dsaf_dev->dev, "set_uc %s Mac %pM err!\n",
			dsaf_dev->ae_dev.name, mac_entry->addr);
		return -EINVAL;
	}

	/* config key */
	hns_dsaf_set_mac_key(dsaf_dev, &mac_key, mac_entry->in_vlan_id,
			     mac_entry->in_port_num, mac_entry->addr);

	/* entry ie exist? */
	entry_index = hns_dsaf_find_soft_mac_entry(dsaf_dev, &mac_key);
	if (entry_index == DSAF_INVALID_ENTRY_IDX) {
		/*if has not inv entry,find a empty entry */
		entry_index = hns_dsaf_find_empty_mac_entry(dsaf_dev);
		if (entry_index == DSAF_INVALID_ENTRY_IDX) {
			/* has not empty,return error */
			dev_err(dsaf_dev->dev,
				"set_uc_entry failed, %s Mac key(%#x:%#x)\n",
				dsaf_dev->ae_dev.name,
				mac_key.high.val, mac_key.low.val);
			return -EINVAL;
		}
	}

	dev_dbg(dsaf_dev->dev,
		"set_uc_entry, %s Mac key(%#x:%#x) entry_index%d\n",
		dsaf_dev->ae_dev.name, mac_key.high.val,
		mac_key.low.val, entry_index);

	/* config hardware entry */
	mac_data.tbl_ucast_item_vld = 1;
	mac_data.tbl_ucast_mac_discard = 0;
	mac_data.tbl_ucast_old_en = 0;
	/* default config dvc to 0 */
	mac_data.tbl_ucast_dvc = 0;
	mac_data.tbl_ucast_out_port = mac_entry->port_num;
	hns_dsaf_tcam_uc_cfg(
		dsaf_dev, entry_index,
		(struct dsaf_tbl_tcam_data *)(&mac_key), &mac_data);

	/* config software entry */
	soft_mac_entry += entry_index;
	soft_mac_entry->index = entry_index;
	soft_mac_entry->tcam_key.high.val = mac_key.high.val;
	soft_mac_entry->tcam_key.low.val = mac_key.low.val;

	return 0;
}

/**
 * hns_dsaf_set_mac_mc_entry - set mac mc-entry
 * @dsaf_dev: dsa fabric device struct pointer
 * @mac_entry: mc-mac entry
 */
int hns_dsaf_set_mac_mc_entry(
	struct dsaf_device *dsaf_dev,
	struct dsaf_drv_mac_multi_dest_entry *mac_entry)
{
	u16 entry_index = DSAF_INVALID_ENTRY_IDX;
	struct dsaf_drv_tbl_tcam_key mac_key;
	struct dsaf_tbl_tcam_mcast_cfg mac_data;
	struct dsaf_drv_priv *priv =
	    (struct dsaf_drv_priv *)hns_dsaf_dev_priv(dsaf_dev);
	struct dsaf_drv_soft_mac_tbl *soft_mac_entry = priv->soft_mac_tbl;
	struct dsaf_drv_tbl_tcam_key tmp_mac_key;

	/* mac addr check */
	if (MAC_IS_ALL_ZEROS(mac_entry->addr)) {
		dev_err(dsaf_dev->dev, "set uc %s Mac %pM err!\n",
			dsaf_dev->ae_dev.name, mac_entry->addr);
		return -EINVAL;
	}

	/*config key */
	hns_dsaf_set_mac_key(dsaf_dev, &mac_key,
			     mac_entry->in_vlan_id,
			     mac_entry->in_port_num, mac_entry->addr);

	/* entry ie exist? */
	entry_index = hns_dsaf_find_soft_mac_entry(dsaf_dev, &mac_key);
	if (entry_index == DSAF_INVALID_ENTRY_IDX) {
		/*if hasnot, find enpty entry*/
		entry_index = hns_dsaf_find_empty_mac_entry(dsaf_dev);
		if (entry_index == DSAF_INVALID_ENTRY_IDX) {
			/*if hasnot empty, error*/
			dev_err(dsaf_dev->dev,
				"set_uc_entry failed, %s Mac key(%#x:%#x)\n",
				dsaf_dev->ae_dev.name,
				mac_key.high.val, mac_key.low.val);
			return -EINVAL;
		}

		/* config hardware entry */
		memset(mac_data.tbl_mcast_port_msk,
		       0, sizeof(mac_data.tbl_mcast_port_msk));
	} else {
		/* config hardware entry */
		hns_dsaf_tcam_mc_get(
			dsaf_dev, entry_index,
			(struct dsaf_tbl_tcam_data *)(&tmp_mac_key), &mac_data);
	}
	mac_data.tbl_mcast_old_en = 0;
	mac_data.tbl_mcast_item_vld = 1;
	dsaf_set_field(mac_data.tbl_mcast_port_msk[0],
		       0x3F, 0, mac_entry->port_mask[0]);

	dev_dbg(dsaf_dev->dev,
		"set_uc_entry, %s key(%#x:%#x) entry_index%d\n",
		dsaf_dev->ae_dev.name, mac_key.high.val,
		mac_key.low.val, entry_index);

	hns_dsaf_tcam_mc_cfg(
		dsaf_dev, entry_index,
		(struct dsaf_tbl_tcam_data *)(&mac_key), &mac_data);

	/* config software entry */
	soft_mac_entry += entry_index;
	soft_mac_entry->index = entry_index;
	soft_mac_entry->tcam_key.high.val = mac_key.high.val;
	soft_mac_entry->tcam_key.low.val = mac_key.low.val;

	return 0;
}

/**
 * hns_dsaf_add_mac_mc_port - add mac mc-port
 * @dsaf_dev: dsa fabric device struct pointer
 * @mac_entry: mc-mac entry
 */
int hns_dsaf_add_mac_mc_port(struct dsaf_device *dsaf_dev,
			     struct dsaf_drv_mac_single_dest_entry *mac_entry)
{
	u16 entry_index = DSAF_INVALID_ENTRY_IDX;
	struct dsaf_drv_tbl_tcam_key mac_key;
	struct dsaf_tbl_tcam_mcast_cfg mac_data;
	struct dsaf_drv_priv *priv =
	    (struct dsaf_drv_priv *)hns_dsaf_dev_priv(dsaf_dev);
	struct dsaf_drv_soft_mac_tbl *soft_mac_entry = priv->soft_mac_tbl;
	struct dsaf_drv_tbl_tcam_key tmp_mac_key;
	int mskid;

	/*chechk mac addr */
	if (MAC_IS_ALL_ZEROS(mac_entry->addr)) {
		dev_err(dsaf_dev->dev, "set_entry failed,addr %pM!\n",
			mac_entry->addr);
		return -EINVAL;
	}

	/*config key */
	hns_dsaf_set_mac_key(
		dsaf_dev, &mac_key, mac_entry->in_vlan_id,
		mac_entry->in_port_num, mac_entry->addr);

	memset(&mac_data, 0, sizeof(struct dsaf_tbl_tcam_mcast_cfg));

	/*check exist? */
	entry_index = hns_dsaf_find_soft_mac_entry(dsaf_dev, &mac_key);
	if (entry_index == DSAF_INVALID_ENTRY_IDX) {
		/*if hasnot , find a empty*/
		entry_index = hns_dsaf_find_empty_mac_entry(dsaf_dev);
		if (entry_index == DSAF_INVALID_ENTRY_IDX) {
			/*if hasnot empty, error*/
			dev_err(dsaf_dev->dev,
				"set_uc_entry failed, %s Mac key(%#x:%#x)\n",
				dsaf_dev->ae_dev.name, mac_key.high.val,
				mac_key.low.val);
			return -EINVAL;
		}
	} else {
		/*if exist, add in */
		hns_dsaf_tcam_mc_get(
			dsaf_dev, entry_index,
			(struct dsaf_tbl_tcam_data *)(&tmp_mac_key), &mac_data);
	}
	/* config hardware entry */
	if (mac_entry->port_num < DSAF_SERVICE_NW_NUM) {
		mskid = mac_entry->port_num;
	} else if (mac_entry->port_num >= DSAF_BASE_INNER_PORT_NUM) {
		mskid = mac_entry->port_num -
			DSAF_BASE_INNER_PORT_NUM + DSAF_SERVICE_NW_NUM;
	} else {
		dev_err(dsaf_dev->dev,
			"%s,pnum(%d)error,key(%#x:%#x)\n",
			dsaf_dev->ae_dev.name, mac_entry->port_num,
			mac_key.high.val, mac_key.low.val);
		return -EINVAL;
	}
	dsaf_set_bit(mac_data.tbl_mcast_port_msk[mskid / 32], mskid % 32, 1);
	mac_data.tbl_mcast_old_en = 0;
	mac_data.tbl_mcast_item_vld = 1;

	dev_dbg(dsaf_dev->dev,
		"set_uc_entry, %s Mac key(%#x:%#x) entry_index%d\n",
		dsaf_dev->ae_dev.name, mac_key.high.val,
		mac_key.low.val, entry_index);

	hns_dsaf_tcam_mc_cfg(
		dsaf_dev, entry_index,
		(struct dsaf_tbl_tcam_data *)(&mac_key), &mac_data);

	/*config software entry */
	soft_mac_entry += entry_index;
	soft_mac_entry->index = entry_index;
	soft_mac_entry->tcam_key.high.val = mac_key.high.val;
	soft_mac_entry->tcam_key.low.val = mac_key.low.val;

	return 0;
}

/**
 * hns_dsaf_del_mac_entry - del mac mc-port
 * @dsaf_dev: dsa fabric device struct pointer
 * @vlan_id: vlian id
 * @in_port_num: input port num
 * @addr : mac addr
 */
int hns_dsaf_del_mac_entry(struct dsaf_device *dsaf_dev, u16 vlan_id,
			   u8 in_port_num, u8 *addr)
{
	u16 entry_index = DSAF_INVALID_ENTRY_IDX;
	struct dsaf_drv_tbl_tcam_key mac_key;
	struct dsaf_drv_priv *priv =
	    (struct dsaf_drv_priv *)hns_dsaf_dev_priv(dsaf_dev);
	struct dsaf_drv_soft_mac_tbl *soft_mac_entry = priv->soft_mac_tbl;

	/*check mac addr */
	if (MAC_IS_ALL_ZEROS(addr) || MAC_IS_BROADCAST(addr)) {
		dev_err(dsaf_dev->dev, "del_entry failed,addr %pM!\n",
			addr);
		return -EINVAL;
	}

	/*config key */
	hns_dsaf_set_mac_key(dsaf_dev, &mac_key, vlan_id, in_port_num, addr);

	/*exist ?*/
	entry_index = hns_dsaf_find_soft_mac_entry(dsaf_dev, &mac_key);
	if (entry_index == DSAF_INVALID_ENTRY_IDX) {
		/*not exist, error */
		dev_err(dsaf_dev->dev,
			"del_mac_entry failed, %s Mac key(%#x:%#x)\n",
			dsaf_dev->ae_dev.name,
			mac_key.high.val, mac_key.low.val);
		return -EINVAL;
	}
	dev_dbg(dsaf_dev->dev,
		"del_mac_entry, %s Mac key(%#x:%#x) entry_index%d\n",
		dsaf_dev->ae_dev.name, mac_key.high.val,
		mac_key.low.val, entry_index);

	/*do del opt*/
	hns_dsaf_tcam_mc_invld(dsaf_dev, entry_index);

	/*del soft emtry */
	soft_mac_entry += entry_index;
	soft_mac_entry->index = DSAF_INVALID_ENTRY_IDX;

	return 0;
}

/**
 * hns_dsaf_del_mac_mc_port - del mac mc- port
 * @dsaf_dev: dsa fabric device struct pointer
 * @mac_entry: mac entry
 */
int hns_dsaf_del_mac_mc_port(struct dsaf_device *dsaf_dev,
			     struct dsaf_drv_mac_single_dest_entry *mac_entry)
{
	u16 entry_index = DSAF_INVALID_ENTRY_IDX;
	struct dsaf_drv_tbl_tcam_key mac_key;
	struct dsaf_drv_priv *priv =
	    (struct dsaf_drv_priv *)hns_dsaf_dev_priv(dsaf_dev);
	struct dsaf_drv_soft_mac_tbl *soft_mac_entry = priv->soft_mac_tbl;
	u16 vlan_id;
	u8 in_port_num;
	struct dsaf_tbl_tcam_mcast_cfg mac_data;
	struct dsaf_drv_tbl_tcam_key tmp_mac_key;
	int mskid;
	const u8 empty_msk[sizeof(mac_data.tbl_mcast_port_msk)] = {0};

	if (!(void *)mac_entry) {
		dev_err(dsaf_dev->dev,
			"hns_dsaf_del_mac_mc_port mac_entry is NULL\n");
		return -EINVAL;
	}

	/*get key info*/
	vlan_id = mac_entry->in_vlan_id;
	in_port_num = mac_entry->in_port_num;

	/*check mac addr */
	if (MAC_IS_ALL_ZEROS(mac_entry->addr)) {
		dev_err(dsaf_dev->dev, "del_port failed, addr %pM!\n",
			mac_entry->addr);
		return -EINVAL;
	}

	/*config key */
	hns_dsaf_set_mac_key(dsaf_dev, &mac_key, vlan_id, in_port_num,
			     mac_entry->addr);

	/*check is exist? */
	entry_index = hns_dsaf_find_soft_mac_entry(dsaf_dev, &mac_key);
	if (entry_index == DSAF_INVALID_ENTRY_IDX) {
		/*find none */
		dev_err(dsaf_dev->dev,
			"find_soft_mac_entry failed, %s Mac key(%#x:%#x)\n",
			dsaf_dev->ae_dev.name,
			mac_key.high.val, mac_key.low.val);
		return -EINVAL;
	}

	dev_dbg(dsaf_dev->dev,
		"del_mac_mc_port, %s key(%#x:%#x) index%d\n",
		dsaf_dev->ae_dev.name, mac_key.high.val,
		mac_key.low.val, entry_index);

	/*read entry*/
	hns_dsaf_tcam_mc_get(
		dsaf_dev, entry_index,
		(struct dsaf_tbl_tcam_data *)(&tmp_mac_key), &mac_data);

	/*del the port*/
	if (mac_entry->port_num < DSAF_SERVICE_NW_NUM) {
		mskid = mac_entry->port_num;
	} else if (mac_entry->port_num >= DSAF_BASE_INNER_PORT_NUM) {
		mskid = mac_entry->port_num -
			DSAF_BASE_INNER_PORT_NUM + DSAF_SERVICE_NW_NUM;
	} else {
		dev_err(dsaf_dev->dev,
			"%s,pnum(%d)error,key(%#x:%#x)\n",
			dsaf_dev->ae_dev.name, mac_entry->port_num,
			mac_key.high.val, mac_key.low.val);
		return -EINVAL;
	}
	dsaf_set_bit(mac_data.tbl_mcast_port_msk[mskid / 32], mskid % 32, 0);

	/*check non port, do del entry */
	if (!memcmp(mac_data.tbl_mcast_port_msk, empty_msk,
		    sizeof(mac_data.tbl_mcast_port_msk))) {
		hns_dsaf_tcam_mc_invld(dsaf_dev, entry_index);

		/* del soft entry */
		soft_mac_entry += entry_index;
		soft_mac_entry->index = DSAF_INVALID_ENTRY_IDX;
	} else { /* not zer, just del port, updata*/
		hns_dsaf_tcam_mc_cfg(
			dsaf_dev, entry_index,
			(struct dsaf_tbl_tcam_data *)(&mac_key), &mac_data);
	}

	return 0;
}

/**
 * hns_dsaf_get_mac_uc_entry - get mac uc entry
 * @dsaf_dev: dsa fabric device struct pointer
 * @mac_entry: mac entry
 */
int hns_dsaf_get_mac_uc_entry(struct dsaf_device *dsaf_dev,
			      struct dsaf_drv_mac_single_dest_entry *mac_entry)
{
	u16 entry_index = DSAF_INVALID_ENTRY_IDX;
	struct dsaf_drv_tbl_tcam_key mac_key;

	struct dsaf_tbl_tcam_ucast_cfg mac_data;

	/* check macaddr */
	if (MAC_IS_ALL_ZEROS(mac_entry->addr) ||
	    MAC_IS_BROADCAST(mac_entry->addr)) {
		dev_err(dsaf_dev->dev, "get_entry failed,addr %pM\n",
			mac_entry->addr);
		return -EINVAL;
	}

	/*config key */
	hns_dsaf_set_mac_key(dsaf_dev, &mac_key, mac_entry->in_vlan_id,
			     mac_entry->in_port_num, mac_entry->addr);

	/*check exist? */
	entry_index = hns_dsaf_find_soft_mac_entry(dsaf_dev, &mac_key);
	if (entry_index == DSAF_INVALID_ENTRY_IDX) {
		/*find none, error */
		dev_err(dsaf_dev->dev,
			"get_uc_entry failed, %s Mac key(%#x:%#x)\n",
			dsaf_dev->ae_dev.name,
			mac_key.high.val, mac_key.low.val);
		return -EINVAL;
	}
	dev_dbg(dsaf_dev->dev,
		"get_uc_entry, %s Mac key(%#x:%#x) entry_index%d\n",
		dsaf_dev->ae_dev.name, mac_key.high.val,
		mac_key.low.val, entry_index);

	/*read entry*/
	hns_dsaf_tcam_uc_get(dsaf_dev, entry_index,
			     (struct dsaf_tbl_tcam_data *)&mac_key, &mac_data);
	mac_entry->port_num = mac_data.tbl_ucast_out_port;

	return 0;
}

/**
 * hns_dsaf_get_mac_mc_entry - get mac mc entry
 * @dsaf_dev: dsa fabric device struct pointer
 * @mac_entry: mac entry
 */
int hns_dsaf_get_mac_mc_entry(struct dsaf_device *dsaf_dev,
			      struct dsaf_drv_mac_multi_dest_entry *mac_entry)
{
	u16 entry_index = DSAF_INVALID_ENTRY_IDX;
	struct dsaf_drv_tbl_tcam_key mac_key;

	struct dsaf_tbl_tcam_mcast_cfg mac_data;

	/*check mac addr */
	if (MAC_IS_ALL_ZEROS(mac_entry->addr) ||
	    MAC_IS_BROADCAST(mac_entry->addr)) {
		dev_err(dsaf_dev->dev, "get_entry failed,addr %pM\n",
			mac_entry->addr);
		return -EINVAL;
	}

	/*config key */
	hns_dsaf_set_mac_key(dsaf_dev, &mac_key, mac_entry->in_vlan_id,
			     mac_entry->in_port_num, mac_entry->addr);

	/*check exist? */
	entry_index = hns_dsaf_find_soft_mac_entry(dsaf_dev, &mac_key);
	if (entry_index == DSAF_INVALID_ENTRY_IDX) {
		/* find none, error */
		dev_err(dsaf_dev->dev,
			"get_mac_uc_entry failed, %s Mac key(%#x:%#x)\n",
			dsaf_dev->ae_dev.name, mac_key.high.val,
			mac_key.low.val);
		return -EINVAL;
	}
	dev_dbg(dsaf_dev->dev,
		"get_mac_uc_entry, %s Mac key(%#x:%#x) entry_index%d\n",
		dsaf_dev->ae_dev.name, mac_key.high.val,
		mac_key.low.val, entry_index);

	/*read entry */
	hns_dsaf_tcam_mc_get(dsaf_dev, entry_index,
			     (struct dsaf_tbl_tcam_data *)&mac_key, &mac_data);

	mac_entry->port_mask[0] = mac_data.tbl_mcast_port_msk[0] & 0x3F;
	return 0;
}

/**
 * hns_dsaf_get_mac_entry_by_index - get mac entry by tab index
 * @dsaf_dev: dsa fabric device struct pointer
 * @entry_index: tab entry index
 * @mac_entry: mac entry
 */
int hns_dsaf_get_mac_entry_by_index(
	struct dsaf_device *dsaf_dev,
	u16 entry_index, struct dsaf_drv_mac_multi_dest_entry *mac_entry)
{
	struct dsaf_drv_tbl_tcam_key mac_key;

	struct dsaf_tbl_tcam_mcast_cfg mac_data;
	struct dsaf_tbl_tcam_ucast_cfg mac_uc_data;
	char mac_addr[MAC_NUM_OCTETS_PER_ADDR] = {0};

	if (entry_index >= DSAF_TCAM_SUM) {
		/* find none, del error */
		dev_err(dsaf_dev->dev, "get_uc_entry failed, %s\n",
			dsaf_dev->ae_dev.name);
		return -EINVAL;
	}

	/* mc entry, do read opt */
	hns_dsaf_tcam_mc_get(dsaf_dev, entry_index,
			     (struct dsaf_tbl_tcam_data *)&mac_key, &mac_data);

	mac_entry->port_mask[0] = mac_data.tbl_mcast_port_msk[0] & 0x3F;

	/***get mac addr*/
	mac_addr[0] = mac_key.high.bits.mac_0;
	mac_addr[1] = mac_key.high.bits.mac_1;
	mac_addr[2] = mac_key.high.bits.mac_2;
	mac_addr[3] = mac_key.high.bits.mac_3;
	mac_addr[4] = mac_key.low.bits.mac_4;
	mac_addr[5] = mac_key.low.bits.mac_5;
	/**is mc or uc*/
	if (MAC_IS_MULTICAST((u8 *)mac_addr) ||
	    MAC_IS_L3_MULTICAST((u8 *)mac_addr)) {
		/**mc donot do*/
	} else {
		/*is not mc, just uc... */
		hns_dsaf_tcam_uc_get(dsaf_dev, entry_index,
				     (struct dsaf_tbl_tcam_data *)&mac_key,
				     &mac_uc_data);
		mac_entry->port_mask[0] = (1 << mac_uc_data.tbl_ucast_out_port);
	}

	return 0;
}

static struct dsaf_device *hns_dsaf_alloc_dev(struct device *dev,
					      size_t sizeof_priv)
{
	struct dsaf_device *dsaf_dev;

	dsaf_dev = devm_kzalloc(dev,
				sizeof(*dsaf_dev) + sizeof_priv, GFP_KERNEL);
	if (unlikely(!dsaf_dev)) {
		dsaf_dev = ERR_PTR(-ENOMEM);
	} else {
		dsaf_dev->dev = dev;
		dev_set_drvdata(dev, dsaf_dev);
	}

	return dsaf_dev;
}

/**
 * hns_dsaf_free_dev - free dev mem
 * @dev: struct device pointer
 */
static void hns_dsaf_free_dev(struct dsaf_device *dsaf_dev)
{
	(void)dev_set_drvdata(dsaf_dev->dev, NULL);
}

/**
 * dsaf_pfc_unit_cnt - set pfc unit count
 * @dsaf_id: dsa fabric id
 * @pport_rate:  value array
 * @pdsaf_pfc_unit_cnt:  value array
 */
static void hns_dsaf_pfc_unit_cnt(struct dsaf_device *dsaf_dev, int  mac_id,
				  enum dsaf_port_rate_mode rate)
{
	u32 unit_cnt;

	switch (rate) {
	case DSAF_PORT_RATE_10000:
		unit_cnt = HNS_DSAF_PFC_UNIT_CNT_FOR_XGE;
		break;
	case DSAF_PORT_RATE_1000:
		unit_cnt = HNS_DSAF_PFC_UNIT_CNT_FOR_GE_1000;
		break;
	case DSAF_PORT_RATE_2500:
		unit_cnt = HNS_DSAF_PFC_UNIT_CNT_FOR_GE_1000;
		break;
	default:
		unit_cnt = HNS_DSAF_PFC_UNIT_CNT_FOR_XGE;
	}

	dsaf_set_dev_field(dsaf_dev,
			   (DSAF_PFC_UNIT_CNT_0_REG + 0x4 * (u64)mac_id),
			   DSAF_PFC_UNINT_CNT_M, DSAF_PFC_UNINT_CNT_S,
			   unit_cnt);
}

/**
 * dsaf_port_work_rate_cfg - fifo
 * @dsaf_id: dsa fabric id
 * @xge_ge_work_mode
 */
void hns_dsaf_port_work_rate_cfg(struct dsaf_device *dsaf_dev, int mac_id,
				 enum dsaf_port_rate_mode rate_mode)
{
	u32 port_work_mode;

	port_work_mode = dsaf_read_dev(
		dsaf_dev, DSAF_XGE_GE_WORK_MODE_0_REG + 0x4 * (u64)mac_id);

	if (rate_mode == DSAF_PORT_RATE_10000)
		dsaf_set_bit(port_work_mode, DSAF_XGE_GE_WORK_MODE_S, 1);
	else
		dsaf_set_bit(port_work_mode, DSAF_XGE_GE_WORK_MODE_S, 0);

	dsaf_write_dev(dsaf_dev,
		       DSAF_XGE_GE_WORK_MODE_0_REG + 0x4 * (u64)mac_id,
		       port_work_mode);

	hns_dsaf_pfc_unit_cnt(dsaf_dev, mac_id, rate_mode);
}

/**
 * hns_dsaf_fix_mac_mode - dsaf modify mac mode
 * @mac_cb: mac contrl block
 */
void hns_dsaf_fix_mac_mode(struct hns_mac_cb *mac_cb)
{
	enum dsaf_port_rate_mode mode;
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	int mac_id = mac_cb->mac_id;

	if (mac_cb->mac_type != HNAE_PORT_SERVICE)
		return;
	if (mac_cb->phy_if == PHY_INTERFACE_MODE_XGMII)
		mode = DSAF_PORT_RATE_10000;
	else
		mode = DSAF_PORT_RATE_1000;

	hns_dsaf_port_work_rate_cfg(dsaf_dev, mac_id, mode);
}

void hns_dsaf_update_stats(struct dsaf_device *dsaf_dev, u32 node_num)
{
	struct dsaf_hw_stats *hw_stats
		= &dsaf_dev->hw_stats[node_num];

	hw_stats->pad_drop += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_PAD_DISCARD_NUM_0_REG + 0x80 * (u64)node_num);
	hw_stats->man_pkts += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_FINAL_IN_MAN_NUM_0_REG + 0x80 * (u64)node_num);
	hw_stats->rx_pkts += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_FINAL_IN_PKT_NUM_0_REG + 0x80 * (u64)node_num);
	hw_stats->rx_pkt_id += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_SBM_PID_NUM_0_REG + 0x80 * (u64)node_num);
	hw_stats->rx_pause_frame += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_FINAL_IN_PAUSE_NUM_0_REG + 0x80 * (u64)node_num);
	hw_stats->release_buf_num += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_SBM_RELS_NUM_0_REG + 0x80 * (u64)node_num);
	hw_stats->sbm_drop += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_SBM_DROP_NUM_0_REG + 0x80 * (u64)node_num);
	hw_stats->crc_false += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_CRC_FALSE_NUM_0_REG + 0x80 * (u64)node_num);
	hw_stats->bp_drop += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_BP_DISCARD_NUM_0_REG + 0x80 * (u64)node_num);
	hw_stats->rslt_drop += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_RSLT_DISCARD_NUM_0_REG + 0x80 * (u64)node_num);
	hw_stats->local_addr_false += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_LOCAL_ADDR_FALSE_NUM_0_REG + 0x80 * (u64)node_num);

	hw_stats->vlan_drop += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_SW_VLAN_TAG_DISC_0_REG + 0x80 * (u64)node_num);
	hw_stats->stp_drop += dsaf_read_dev(dsaf_dev,
		DSAF_INODE_IN_DATA_STP_DISC_0_REG + 0x80 * (u64)node_num);

	hw_stats->tx_pkts += dsaf_read_dev(dsaf_dev,
		DSAF_XOD_RCVPKT_CNT_0_REG + 0x90 * (u64)node_num);
}

/**
 *hns_dsaf_get_regs - dump dsaf regs
 *@dsaf_dev: dsaf device
 *@data:data for value of regs
 */
void hns_dsaf_get_regs(struct dsaf_device *ddev, u32 port, void *data)
{
	u32 i = 0;
	u32 j;
	u32 *p = data;

	/* dsaf common registers */
	p[0] = dsaf_read_dev(ddev, DSAF_SRAM_INIT_OVER_0_REG);
	p[1] = dsaf_read_dev(ddev, DSAF_CFG_0_REG);
	p[2] = dsaf_read_dev(ddev, DSAF_ECC_ERR_INVERT_0_REG);
	p[3] = dsaf_read_dev(ddev, DSAF_ABNORMAL_TIMEOUT_0_REG);
	p[4] = dsaf_read_dev(ddev, DSAF_FSM_TIMEOUT_0_REG);
	p[5] = dsaf_read_dev(ddev, DSAF_DSA_REG_CNT_CLR_CE_REG);
	p[6] = dsaf_read_dev(ddev, DSAF_DSA_SBM_INF_FIFO_THRD_REG);
	p[7] = dsaf_read_dev(ddev, DSAF_DSA_SRAM_1BIT_ECC_SEL_REG);
	p[8] = dsaf_read_dev(ddev, DSAF_DSA_SRAM_1BIT_ECC_CNT_REG);

	p[9] = dsaf_read_dev(ddev, DSAF_PFC_EN_0_REG + port * 4);
	p[10] = dsaf_read_dev(ddev, DSAF_PFC_UNIT_CNT_0_REG + port * 4);
	p[11] = dsaf_read_dev(ddev, DSAF_XGE_INT_MSK_0_REG + port * 4);
	p[12] = dsaf_read_dev(ddev, DSAF_XGE_INT_SRC_0_REG + port * 4);
	p[13] = dsaf_read_dev(ddev, DSAF_XGE_INT_STS_0_REG + port * 4);
	p[14] = dsaf_read_dev(ddev, DSAF_XGE_INT_MSK_0_REG + port * 4);
	p[15] = dsaf_read_dev(ddev, DSAF_PPE_INT_MSK_0_REG + port * 4);
	p[16] = dsaf_read_dev(ddev, DSAF_ROCEE_INT_MSK_0_REG + port * 4);
	p[17] = dsaf_read_dev(ddev, DSAF_XGE_INT_SRC_0_REG + port * 4);
	p[18] = dsaf_read_dev(ddev, DSAF_PPE_INT_SRC_0_REG + port * 4);
	p[19] =  dsaf_read_dev(ddev, DSAF_ROCEE_INT_SRC_0_REG + port * 4);
	p[20] = dsaf_read_dev(ddev, DSAF_XGE_INT_STS_0_REG + port * 4);
	p[21] = dsaf_read_dev(ddev, DSAF_PPE_INT_STS_0_REG + port * 4);
	p[22] = dsaf_read_dev(ddev, DSAF_ROCEE_INT_STS_0_REG + port * 4);
	p[23] = dsaf_read_dev(ddev, DSAF_PPE_QID_CFG_0_REG + port * 4);

	for (i = 0; i < DSAF_SW_PORT_NUM; i++)
		p[24 + i] = dsaf_read_dev(ddev,
				DSAF_SW_PORT_TYPE_0_REG + i * 4);

	p[32] = dsaf_read_dev(ddev, DSAF_MIX_DEF_QID_0_REG + port * 4);

	for (i = 0; i < DSAF_SW_PORT_NUM; i++)
		p[33 + i] = dsaf_read_dev(ddev,
				DSAF_PORT_DEF_VLAN_0_REG + i * 4);

	for (i = 0; i < DSAF_TOTAL_QUEUE_NUM; i++)
		p[41 + i] = dsaf_read_dev(ddev,
				DSAF_VM_DEF_VLAN_0_REG + i * 4);

	/* dsaf inode registers */
	p[170] = dsaf_read_dev(ddev, DSAF_INODE_CUT_THROUGH_CFG_0_REG);

	p[171] = dsaf_read_dev(ddev,
			DSAF_INODE_ECC_ERR_ADDR_0_REG + port * 0x80);

	for (i = 0; i < DSAF_INODE_NUM / DSAF_COMM_CHN; i++) {
		j = i * DSAF_COMM_CHN + port;
		p[172 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_IN_PORT_NUM_0_REG + j * 0x80);
		p[175 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_PRI_TC_CFG_0_REG + j * 0x80);
		p[178 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_BP_STATUS_0_REG + j * 0x80);
		p[181 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_PAD_DISCARD_NUM_0_REG + j * 0x80);
		p[184 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_FINAL_IN_MAN_NUM_0_REG + j * 0x80);
		p[187 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_FINAL_IN_PKT_NUM_0_REG + j * 0x80);
		p[190 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_SBM_PID_NUM_0_REG + j * 0x80);
		p[193 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_FINAL_IN_PAUSE_NUM_0_REG + j * 0x80);
		p[196 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_SBM_RELS_NUM_0_REG + j * 0x80);
		p[199 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_SBM_DROP_NUM_0_REG + j * 0x80);
		p[202 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_CRC_FALSE_NUM_0_REG + j * 0x80);
		p[205 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_BP_DISCARD_NUM_0_REG + j * 0x80);
		p[208 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_RSLT_DISCARD_NUM_0_REG + j * 0x80);
		p[211 + i] = dsaf_read_dev(ddev,
			DSAF_INODE_LOCAL_ADDR_FALSE_NUM_0_REG + j * 0x80);
		p[214 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_VOQ_OVER_NUM_0_REG + j * 0x80);
		p[217 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_BD_SAVE_STATUS_0_REG + j * 4);
		p[220 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_BD_ORDER_STATUS_0_REG + j * 4);
		p[223 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_SW_VLAN_TAG_DISC_0_REG + j * 4);
		p[224 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_IN_DATA_STP_DISC_0_REG + j * 4);
	}

	p[227] = dsaf_read_dev(ddev, DSAF_INODE_GE_FC_EN_0_REG + port * 4);

	for (i = 0; i < DSAF_INODE_NUM / DSAF_COMM_CHN; i++) {
		j = i * DSAF_COMM_CHN + port;
		p[228 + i] = dsaf_read_dev(ddev,
				DSAF_INODE_VC0_IN_PKT_NUM_0_REG + j * 4);
	}

	p[231] = dsaf_read_dev(ddev,
		DSAF_INODE_VC1_IN_PKT_NUM_0_REG + port * 4);

	/* dsaf inode registers */
	for (i = 0; i < HNS_DSAF_SBM_NUM(ddev) / DSAF_COMM_CHN; i++) {
		j = i * DSAF_COMM_CHN + port;
		p[232 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_CFG_REG_0_REG + j * 0x80);
		p[235 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_BP_CFG_0_XGE_REG_0_REG + j * 0x80);
		p[238 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_BP_CFG_1_REG_0_REG + j * 0x80);
		p[241 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_BP_CFG_2_XGE_REG_0_REG + j * 0x80);
		p[244 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_FREE_CNT_0_0_REG + j * 0x80);
		p[245 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_FREE_CNT_1_0_REG + j * 0x80);
		p[248 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_BP_CNT_0_0_REG + j * 0x80);
		p[251 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_BP_CNT_1_0_REG + j * 0x80);
		p[254 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_BP_CNT_2_0_REG + j * 0x80);
		p[257 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_BP_CNT_3_0_REG + j * 0x80);
		p[260 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_INER_ST_0_REG + j * 0x80);
		p[263 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_MIB_REQ_FAILED_TC_0_REG + j * 0x80);
		p[266 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_LNK_INPORT_CNT_0_REG + j * 0x80);
		p[269 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_LNK_DROP_CNT_0_REG + j * 0x80);
		p[272 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_INF_OUTPORT_CNT_0_REG + j * 0x80);
		p[275 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_LNK_INPORT_TC0_CNT_0_REG + j * 0x80);
		p[278 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_LNK_INPORT_TC1_CNT_0_REG + j * 0x80);
		p[281 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_LNK_INPORT_TC2_CNT_0_REG + j * 0x80);
		p[284 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_LNK_INPORT_TC3_CNT_0_REG + j * 0x80);
		p[287 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_LNK_INPORT_TC4_CNT_0_REG + j * 0x80);
		p[290 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_LNK_INPORT_TC5_CNT_0_REG + j * 0x80);
		p[293 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_LNK_INPORT_TC6_CNT_0_REG + j * 0x80);
		p[296 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_LNK_INPORT_TC7_CNT_0_REG + j * 0x80);
		p[299 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_LNK_REQ_CNT_0_REG + j * 0x80);
		p[302 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_LNK_RELS_CNT_0_REG + j * 0x80);
		p[305 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_BP_CFG_3_REG_0_REG + j * 0x80);
		p[308 + i] = dsaf_read_dev(ddev,
				DSAF_SBM_BP_CFG_4_REG_0_REG + j * 0x80);
	}

	/* dsaf onode registers */
	for (i = 0; i < DSAF_XOD_NUM; i++) {
		p[311 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_ETS_TSA_TC0_TC3_CFG_0_REG + j * 0x90);
		p[319 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_ETS_TSA_TC4_TC7_CFG_0_REG + j * 0x90);
		p[327 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_ETS_BW_TC0_TC3_CFG_0_REG + j * 0x90);
		p[335 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_ETS_BW_TC4_TC7_CFG_0_REG + j * 0x90);
		p[343 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_ETS_BW_OFFSET_CFG_0_REG + j * 0x90);
		p[351 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_ETS_TOKEN_CFG_0_REG + j * 0x90);
	}

	p[359] = dsaf_read_dev(ddev, DSAF_XOD_PFS_CFG_0_0_REG + port * 0x90);
	p[360] = dsaf_read_dev(ddev, DSAF_XOD_PFS_CFG_1_0_REG + port * 0x90);
	p[361] = dsaf_read_dev(ddev, DSAF_XOD_PFS_CFG_2_0_REG + port * 0x90);

	for (i = 0; i < DSAF_XOD_BIG_NUM / DSAF_COMM_CHN; i++) {
		j = i * DSAF_COMM_CHN + port;
		p[362 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_GNT_L_0_REG + j * 0x90);
		p[365 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_GNT_H_0_REG + j * 0x90);
		p[368 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_CONNECT_STATE_0_REG + j * 0x90);
		p[371 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_RCVPKT_CNT_0_REG + j * 0x90);
		p[374 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_RCVTC0_CNT_0_REG + j * 0x90);
		p[377 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_RCVTC1_CNT_0_REG + j * 0x90);
		p[380 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_RCVTC2_CNT_0_REG + j * 0x90);
		p[383 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_RCVTC3_CNT_0_REG + j * 0x90);
		p[386 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_RCVVC0_CNT_0_REG + j * 0x90);
		p[389 + i] = dsaf_read_dev(ddev,
				DSAF_XOD_RCVVC1_CNT_0_REG + j * 0x90);
	}

	p[392] = dsaf_read_dev(ddev,
		DSAF_XOD_XGE_RCVIN0_CNT_0_REG + port * 0x90);
	p[393] = dsaf_read_dev(ddev,
		DSAF_XOD_XGE_RCVIN1_CNT_0_REG + port * 0x90);
	p[394] = dsaf_read_dev(ddev,
		DSAF_XOD_XGE_RCVIN2_CNT_0_REG + port * 0x90);
	p[395] = dsaf_read_dev(ddev,
		DSAF_XOD_XGE_RCVIN3_CNT_0_REG + port * 0x90);
	p[396] = dsaf_read_dev(ddev,
		DSAF_XOD_XGE_RCVIN4_CNT_0_REG + port * 0x90);
	p[397] = dsaf_read_dev(ddev,
		DSAF_XOD_XGE_RCVIN5_CNT_0_REG + port * 0x90);
	p[398] = dsaf_read_dev(ddev,
		DSAF_XOD_XGE_RCVIN6_CNT_0_REG + port * 0x90);
	p[399] = dsaf_read_dev(ddev,
		DSAF_XOD_XGE_RCVIN7_CNT_0_REG + port * 0x90);
	p[400] = dsaf_read_dev(ddev,
		DSAF_XOD_PPE_RCVIN0_CNT_0_REG + port * 0x90);
	p[401] = dsaf_read_dev(ddev,
		DSAF_XOD_PPE_RCVIN1_CNT_0_REG + port * 0x90);
	p[402] = dsaf_read_dev(ddev,
		DSAF_XOD_ROCEE_RCVIN0_CNT_0_REG + port * 0x90);
	p[403] = dsaf_read_dev(ddev,
		DSAF_XOD_ROCEE_RCVIN1_CNT_0_REG + port * 0x90);
	p[404] = dsaf_read_dev(ddev,
		DSAF_XOD_FIFO_STATUS_0_REG + port * 0x90);

	/* dsaf voq registers */
	for (i = 0; i < DSAF_VOQ_NUM / DSAF_COMM_CHN; i++) {
		j = (i * DSAF_COMM_CHN + port) * 0x90;
		p[405 + i] = dsaf_read_dev(ddev,
			DSAF_VOQ_ECC_INVERT_EN_0_REG + j);
		p[408 + i] = dsaf_read_dev(ddev,
			DSAF_VOQ_SRAM_PKT_NUM_0_REG + j);
		p[411 + i] = dsaf_read_dev(ddev, DSAF_VOQ_IN_PKT_NUM_0_REG + j);
		p[414 + i] = dsaf_read_dev(ddev,
			DSAF_VOQ_OUT_PKT_NUM_0_REG + j);
		p[417 + i] = dsaf_read_dev(ddev,
			DSAF_VOQ_ECC_ERR_ADDR_0_REG + j);
		p[420 + i] = dsaf_read_dev(ddev, DSAF_VOQ_BP_STATUS_0_REG + j);
		p[423 + i] = dsaf_read_dev(ddev, DSAF_VOQ_SPUP_IDLE_0_REG + j);
		p[426 + i] = dsaf_read_dev(ddev,
			DSAF_VOQ_XGE_XOD_REQ_0_0_REG + j);
		p[429 + i] = dsaf_read_dev(ddev,
			DSAF_VOQ_XGE_XOD_REQ_1_0_REG + j);
		p[432 + i] = dsaf_read_dev(ddev,
			DSAF_VOQ_PPE_XOD_REQ_0_REG + j);
		p[435 + i] = dsaf_read_dev(ddev,
			DSAF_VOQ_ROCEE_XOD_REQ_0_REG + j);
		p[438 + i] = dsaf_read_dev(ddev,
			DSAF_VOQ_BP_ALL_THRD_0_REG + j);
	}

	/* dsaf tbl registers */
	p[441] = dsaf_read_dev(ddev, DSAF_TBL_CTRL_0_REG);
	p[442] = dsaf_read_dev(ddev, DSAF_TBL_INT_MSK_0_REG);
	p[443] = dsaf_read_dev(ddev, DSAF_TBL_INT_SRC_0_REG);
	p[444] = dsaf_read_dev(ddev, DSAF_TBL_INT_STS_0_REG);
	p[445] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_ADDR_0_REG);
	p[446] = dsaf_read_dev(ddev, DSAF_TBL_LINE_ADDR_0_REG);
	p[447] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_HIGH_0_REG);
	p[448] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_LOW_0_REG);
	p[449] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_MCAST_CFG_4_0_REG);
	p[450] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_MCAST_CFG_3_0_REG);
	p[451] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_MCAST_CFG_2_0_REG);
	p[452] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_MCAST_CFG_1_0_REG);
	p[453] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_MCAST_CFG_0_0_REG);
	p[454] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_UCAST_CFG_0_REG);
	p[455] = dsaf_read_dev(ddev, DSAF_TBL_LIN_CFG_0_REG);
	p[456] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_RDATA_HIGH_0_REG);
	p[457] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_RDATA_LOW_0_REG);
	p[458] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_RAM_RDATA4_0_REG);
	p[459] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_RAM_RDATA3_0_REG);
	p[460] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_RAM_RDATA2_0_REG);
	p[461] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_RAM_RDATA1_0_REG);
	p[462] = dsaf_read_dev(ddev, DSAF_TBL_TCAM_RAM_RDATA0_0_REG);
	p[463] = dsaf_read_dev(ddev, DSAF_TBL_LIN_RDATA_0_REG);

	for (i = 0; i < DSAF_SW_PORT_NUM; i++) {
		j = i * 0x8;
		p[464 + 2 * i] = dsaf_read_dev(ddev,
			DSAF_TBL_DA0_MIS_INFO1_0_REG + j);
		p[465 + 2 * i] = dsaf_read_dev(ddev,
			DSAF_TBL_DA0_MIS_INFO0_0_REG + j);
	}

	p[480] = dsaf_read_dev(ddev, DSAF_TBL_SA_MIS_INFO2_0_REG);
	p[481] = dsaf_read_dev(ddev, DSAF_TBL_SA_MIS_INFO1_0_REG);
	p[482] = dsaf_read_dev(ddev, DSAF_TBL_SA_MIS_INFO0_0_REG);
	p[483] = dsaf_read_dev(ddev, DSAF_TBL_PUL_0_REG);
	p[484] = dsaf_read_dev(ddev, DSAF_TBL_OLD_RSLT_0_REG);
	p[485] = dsaf_read_dev(ddev, DSAF_TBL_OLD_SCAN_VAL_0_REG);
	p[486] = dsaf_read_dev(ddev, DSAF_TBL_DFX_CTRL_0_REG);
	p[487] = dsaf_read_dev(ddev, DSAF_TBL_DFX_STAT_0_REG);
	p[488] = dsaf_read_dev(ddev, DSAF_TBL_DFX_STAT_2_0_REG);
	p[489] = dsaf_read_dev(ddev, DSAF_TBL_LKUP_NUM_I_0_REG);
	p[490] = dsaf_read_dev(ddev, DSAF_TBL_LKUP_NUM_O_0_REG);
	p[491] = dsaf_read_dev(ddev, DSAF_TBL_UCAST_BCAST_MIS_INFO_0_0_REG);

	/* dsaf other registers */
	p[492] = dsaf_read_dev(ddev, DSAF_INODE_FIFO_WL_0_REG + port * 0x4);
	p[493] = dsaf_read_dev(ddev, DSAF_ONODE_FIFO_WL_0_REG + port * 0x4);
	p[494] = dsaf_read_dev(ddev, DSAF_XGE_GE_WORK_MODE_0_REG + port * 0x4);
	p[495] = dsaf_read_dev(ddev,
		DSAF_XGE_APP_RX_LINK_UP_0_REG + port * 0x4);
	p[496] = dsaf_read_dev(ddev, DSAF_NETPORT_CTRL_SIG_0_REG + port * 0x4);
	p[497] = dsaf_read_dev(ddev, DSAF_XGE_CTRL_SIG_CFG_0_REG + port * 0x4);

	/* mark end of dsaf regs */
	for (i = 498; i < 504; i++)
		p[i] = 0xdddddddd;
}

static char *hns_dsaf_get_node_stats_strings(char *data, int node)
{
	char *buff = data;

	snprintf(buff, ETH_GSTRING_LEN, "innod%d_pad_drop_pkts", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "innod%d_manage_pkts", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "innod%d_rx_pkts", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "innod%d_rx_pkt_id", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "innod%d_rx_pause_frame", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "innod%d_release_buf_num", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "innod%d_sbm_drop_pkts", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "innod%d_crc_false_pkts", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "innod%d_bp_drop_pkts", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "innod%d_lookup_rslt_drop_pkts", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "innod%d_local_rslt_fail_pkts", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "innod%d_vlan_drop_pkts", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "innod%d_stp_drop_pkts", node);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "onnod%d_tx_pkts", node);
	buff = buff + ETH_GSTRING_LEN;

	return buff;
}

static u64 *hns_dsaf_get_node_stats(struct dsaf_device *ddev, u64 *data,
				    int node_num)
{
	u64 *p = data;
	struct dsaf_hw_stats *hw_stats = &ddev->hw_stats[node_num];

	p[0] = hw_stats->pad_drop;
	p[1] = hw_stats->man_pkts;
	p[2] = hw_stats->rx_pkts;
	p[3] = hw_stats->rx_pkt_id;
	p[4] = hw_stats->rx_pause_frame;
	p[5] = hw_stats->release_buf_num;
	p[6] = hw_stats->sbm_drop;
	p[7] = hw_stats->crc_false;
	p[8] = hw_stats->bp_drop;
	p[9] = hw_stats->rslt_drop;
	p[10] = hw_stats->local_addr_false;
	p[11] = hw_stats->vlan_drop;
	p[12] = hw_stats->stp_drop;
	p[13] = hw_stats->tx_pkts;

	return &p[14];
}

/**
 *hns_dsaf_get_stats - get dsaf statistic
 *@ddev: dsaf device
 *@data:statistic value
 *@port: port num
 */
void hns_dsaf_get_stats(struct dsaf_device *ddev, u64 *data, int port)
{
	u64 *p = data;
	int node_num = port;

	/* for ge/xge node info */
	p = hns_dsaf_get_node_stats(ddev, p, node_num);

	/* for ppe node info */
	node_num = port + DSAF_PPE_INODE_BASE;
	(void)hns_dsaf_get_node_stats(ddev, p, node_num);
}

/**
 *hns_dsaf_get_sset_count - get dsaf string set count
 *@stringset: type of values in data
 *return dsaf string name count
 */
int hns_dsaf_get_sset_count(int stringset)
{
	if (stringset == ETH_SS_STATS)
		return DSAF_STATIC_NUM;

	return 0;
}

/**
 *hns_dsaf_get_strings - get dsaf string set
 *@stringset:srting set index
 *@data:strings name value
 *@port:port index
 */
void hns_dsaf_get_strings(int stringset, u8 *data, int port)
{
	char *buff = (char *)data;
	int node = port;

	if (stringset != ETH_SS_STATS)
		return;

	/* for ge/xge node info */
	buff = hns_dsaf_get_node_stats_strings(buff, node);

	/* for ppe node info */
	node = port + DSAF_PPE_INODE_BASE;
	(void)hns_dsaf_get_node_stats_strings(buff, node);
}

/**
 *hns_dsaf_get_sset_count - get dsaf regs count
 *return dsaf regs count
 */
int hns_dsaf_get_regs_count(void)
{
	return DSAF_DUMP_REGS_NUM;
}

/**
 * dsaf_probe - probo dsaf dev
 * @pdev: dasf platform device
 * retuen 0 - success , negative --fail
 */
static int hns_dsaf_probe(struct platform_device *pdev)
{
	struct dsaf_device *dsaf_dev;
	int ret;

	dsaf_dev = hns_dsaf_alloc_dev(&pdev->dev, sizeof(struct dsaf_drv_priv));
	if (IS_ERR(dsaf_dev)) {
		ret = PTR_ERR(dsaf_dev);
		dev_err(&pdev->dev,
			"dsaf_probe dsaf_alloc_dev failed, ret = %#x!\n", ret);
		return ret;
	}

	ret = hns_dsaf_get_cfg(dsaf_dev);
	if (ret)
		goto free_dev;

	ret = hns_dsaf_init(dsaf_dev);
	if (ret)
		goto free_cfg;

	ret = hns_mac_init(dsaf_dev);
	if (ret)
		goto uninit_dsaf;

	ret = hns_ppe_init(dsaf_dev);
	if (ret)
		goto uninit_mac;

	ret = hns_dsaf_ae_init(dsaf_dev);
	if (ret)
		goto uninit_ppe;

	return 0;

uninit_ppe:
	hns_ppe_uninit(dsaf_dev);

uninit_mac:
	hns_mac_uninit(dsaf_dev);

uninit_dsaf:
	hns_dsaf_free(dsaf_dev);

free_cfg:
	hns_dsaf_free_cfg(dsaf_dev);

free_dev:
	hns_dsaf_free_dev(dsaf_dev);

	return ret;
}

/**
 * dsaf_remove - remove dsaf dev
 * @pdev: dasf platform device
 */
static int hns_dsaf_remove(struct platform_device *pdev)
{
	struct dsaf_device *dsaf_dev = dev_get_drvdata(&pdev->dev);

	hns_dsaf_ae_uninit(dsaf_dev);

	hns_ppe_uninit(dsaf_dev);

	hns_mac_uninit(dsaf_dev);

	hns_dsaf_free(dsaf_dev);

	hns_dsaf_free_cfg(dsaf_dev);

	hns_dsaf_free_dev(dsaf_dev);

	return 0;
}

static const struct of_device_id g_dsaf_match[] = {
	{.compatible = "hisilicon,hns-dsaf-v1"},
	{.compatible = "hisilicon,hns-dsaf-v2"},
	{}
};

static struct platform_driver g_dsaf_driver = {
	.probe = hns_dsaf_probe,
	.remove = hns_dsaf_remove,
	.driver = {
		.name = DSAF_DRV_NAME,
		.of_match_table = g_dsaf_match,
	},
};

module_platform_driver(g_dsaf_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_DESCRIPTION("HNS DSAF driver");
MODULE_VERSION(DSAF_MOD_VERSION);
