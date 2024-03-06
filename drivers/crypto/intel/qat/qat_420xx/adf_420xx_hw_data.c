// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */
#include <linux/iopoll.h>
#include <adf_accel_devices.h>
#include <adf_admin.h>
#include <adf_cfg.h>
#include <adf_cfg_services.h>
#include <adf_clock.h>
#include <adf_common_drv.h>
#include <adf_fw_config.h>
#include <adf_gen4_config.h>
#include <adf_gen4_dc.h>
#include <adf_gen4_hw_csr_data.h>
#include <adf_gen4_hw_data.h>
#include <adf_gen4_pfvf.h>
#include <adf_gen4_pm.h>
#include <adf_gen4_ras.h>
#include <adf_gen4_timer.h>
#include <adf_gen4_tl.h>
#include "adf_420xx_hw_data.h"
#include "icp_qat_hw.h"

#define ADF_AE_GROUP_0		GENMASK(3, 0)
#define ADF_AE_GROUP_1		GENMASK(7, 4)
#define ADF_AE_GROUP_2		GENMASK(11, 8)
#define ADF_AE_GROUP_3		GENMASK(15, 12)
#define ADF_AE_GROUP_4		BIT(16)

#define ENA_THD_MASK_ASYM	GENMASK(1, 0)
#define ENA_THD_MASK_SYM	GENMASK(3, 0)
#define ENA_THD_MASK_DC		GENMASK(1, 0)

static const char * const adf_420xx_fw_objs[] = {
	[ADF_FW_SYM_OBJ] =  ADF_420XX_SYM_OBJ,
	[ADF_FW_ASYM_OBJ] =  ADF_420XX_ASYM_OBJ,
	[ADF_FW_DC_OBJ] =  ADF_420XX_DC_OBJ,
	[ADF_FW_ADMIN_OBJ] = ADF_420XX_ADMIN_OBJ,
};

static const struct adf_fw_config adf_fw_cy_config[] = {
	{ADF_AE_GROUP_3, ADF_FW_SYM_OBJ},
	{ADF_AE_GROUP_2, ADF_FW_ASYM_OBJ},
	{ADF_AE_GROUP_1, ADF_FW_SYM_OBJ},
	{ADF_AE_GROUP_0, ADF_FW_ASYM_OBJ},
	{ADF_AE_GROUP_4, ADF_FW_ADMIN_OBJ},
};

static const struct adf_fw_config adf_fw_dc_config[] = {
	{ADF_AE_GROUP_1, ADF_FW_DC_OBJ},
	{ADF_AE_GROUP_0, ADF_FW_DC_OBJ},
	{ADF_AE_GROUP_4, ADF_FW_ADMIN_OBJ},
};

static const struct adf_fw_config adf_fw_sym_config[] = {
	{ADF_AE_GROUP_3, ADF_FW_SYM_OBJ},
	{ADF_AE_GROUP_2, ADF_FW_SYM_OBJ},
	{ADF_AE_GROUP_1, ADF_FW_SYM_OBJ},
	{ADF_AE_GROUP_0, ADF_FW_SYM_OBJ},
	{ADF_AE_GROUP_4, ADF_FW_ADMIN_OBJ},
};

static const struct adf_fw_config adf_fw_asym_config[] = {
	{ADF_AE_GROUP_3, ADF_FW_ASYM_OBJ},
	{ADF_AE_GROUP_2, ADF_FW_ASYM_OBJ},
	{ADF_AE_GROUP_1, ADF_FW_ASYM_OBJ},
	{ADF_AE_GROUP_0, ADF_FW_ASYM_OBJ},
	{ADF_AE_GROUP_4, ADF_FW_ADMIN_OBJ},
};

static const struct adf_fw_config adf_fw_asym_dc_config[] = {
	{ADF_AE_GROUP_3, ADF_FW_ASYM_OBJ},
	{ADF_AE_GROUP_2, ADF_FW_ASYM_OBJ},
	{ADF_AE_GROUP_1, ADF_FW_ASYM_OBJ},
	{ADF_AE_GROUP_0, ADF_FW_DC_OBJ},
	{ADF_AE_GROUP_4, ADF_FW_ADMIN_OBJ},
};

static const struct adf_fw_config adf_fw_sym_dc_config[] = {
	{ADF_AE_GROUP_2, ADF_FW_SYM_OBJ},
	{ADF_AE_GROUP_1, ADF_FW_SYM_OBJ},
	{ADF_AE_GROUP_0, ADF_FW_DC_OBJ},
	{ADF_AE_GROUP_4, ADF_FW_ADMIN_OBJ},
};

static const struct adf_fw_config adf_fw_dcc_config[] = {
	{ADF_AE_GROUP_1, ADF_FW_DC_OBJ},
	{ADF_AE_GROUP_0, ADF_FW_SYM_OBJ},
	{ADF_AE_GROUP_4, ADF_FW_ADMIN_OBJ},
};


static struct adf_hw_device_class adf_420xx_class = {
	.name = ADF_420XX_DEVICE_NAME,
	.type = DEV_420XX,
	.instances = 0,
};

static u32 get_ae_mask(struct adf_hw_device_data *self)
{
	u32 me_disable = self->fuses;

	return ~me_disable & ADF_420XX_ACCELENGINES_MASK;
}

static u32 uof_get_num_objs(struct adf_accel_dev *accel_dev)
{
	switch (adf_get_service_enabled(accel_dev)) {
	case SVC_CY:
	case SVC_CY2:
		return ARRAY_SIZE(adf_fw_cy_config);
	case SVC_DC:
		return ARRAY_SIZE(adf_fw_dc_config);
	case SVC_DCC:
		return ARRAY_SIZE(adf_fw_dcc_config);
	case SVC_SYM:
		return ARRAY_SIZE(adf_fw_sym_config);
	case SVC_ASYM:
		return ARRAY_SIZE(adf_fw_asym_config);
	case SVC_ASYM_DC:
	case SVC_DC_ASYM:
		return ARRAY_SIZE(adf_fw_asym_dc_config);
	case SVC_SYM_DC:
	case SVC_DC_SYM:
		return ARRAY_SIZE(adf_fw_sym_dc_config);
	default:
		return 0;
	}
}

static const struct adf_fw_config *get_fw_config(struct adf_accel_dev *accel_dev)
{
	switch (adf_get_service_enabled(accel_dev)) {
	case SVC_CY:
	case SVC_CY2:
		return adf_fw_cy_config;
	case SVC_DC:
		return adf_fw_dc_config;
	case SVC_DCC:
		return adf_fw_dcc_config;
	case SVC_SYM:
		return adf_fw_sym_config;
	case SVC_ASYM:
		return adf_fw_asym_config;
	case SVC_ASYM_DC:
	case SVC_DC_ASYM:
		return adf_fw_asym_dc_config;
	case SVC_SYM_DC:
	case SVC_DC_SYM:
		return adf_fw_sym_dc_config;
	default:
		return NULL;
	}
}

static void update_ae_mask(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	const struct adf_fw_config *fw_config;
	u32 config_ae_mask = 0;
	u32 ae_mask, num_objs;
	int i;

	ae_mask = get_ae_mask(hw_data);

	/* Modify the AE mask based on the firmware configuration loaded */
	fw_config = get_fw_config(accel_dev);
	num_objs = uof_get_num_objs(accel_dev);

	config_ae_mask |= ADF_420XX_ADMIN_AE_MASK;
	for (i = 0; i < num_objs; i++)
		config_ae_mask |= fw_config[i].ae_mask;

	hw_data->ae_mask = ae_mask & config_ae_mask;
}

static u32 get_accel_cap(struct adf_accel_dev *accel_dev)
{
	u32 capabilities_sym, capabilities_asym, capabilities_dc;
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	u32 capabilities_dcc;
	u32 fusectl1;

	/* As a side effect, update ae_mask based on configuration */
	update_ae_mask(accel_dev);

	/* Read accelerator capabilities mask */
	pci_read_config_dword(pdev, ADF_GEN4_FUSECTL1_OFFSET, &fusectl1);

	capabilities_sym = ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC |
			  ICP_ACCEL_CAPABILITIES_CIPHER |
			  ICP_ACCEL_CAPABILITIES_AUTHENTICATION |
			  ICP_ACCEL_CAPABILITIES_SHA3 |
			  ICP_ACCEL_CAPABILITIES_SHA3_EXT |
			  ICP_ACCEL_CAPABILITIES_HKDF |
			  ICP_ACCEL_CAPABILITIES_CHACHA_POLY |
			  ICP_ACCEL_CAPABILITIES_AESGCM_SPC |
			  ICP_ACCEL_CAPABILITIES_SM3 |
			  ICP_ACCEL_CAPABILITIES_SM4 |
			  ICP_ACCEL_CAPABILITIES_AES_V2 |
			  ICP_ACCEL_CAPABILITIES_ZUC |
			  ICP_ACCEL_CAPABILITIES_ZUC_256 |
			  ICP_ACCEL_CAPABILITIES_WIRELESS_CRYPTO_EXT |
			  ICP_ACCEL_CAPABILITIES_EXT_ALGCHAIN;

	/* A set bit in fusectl1 means the feature is OFF in this SKU */
	if (fusectl1 & ICP_ACCEL_GEN4_MASK_CIPHER_SLICE) {
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_HKDF;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_CIPHER;
	}

	if (fusectl1 & ICP_ACCEL_GEN4_MASK_UCS_SLICE) {
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_CHACHA_POLY;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_AESGCM_SPC;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_AES_V2;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_CIPHER;
	}

	if (fusectl1 & ICP_ACCEL_GEN4_MASK_AUTH_SLICE) {
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_AUTHENTICATION;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_SHA3;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_SHA3_EXT;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_CIPHER;
	}

	if (fusectl1 & ICP_ACCEL_GEN4_MASK_SMX_SLICE) {
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_SM3;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_SM4;
	}

	if (fusectl1 & ICP_ACCEL_GEN4_MASK_WCP_WAT_SLICE) {
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_ZUC;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_ZUC_256;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_WIRELESS_CRYPTO_EXT;
	}

	if (fusectl1 & ICP_ACCEL_GEN4_MASK_EIA3_SLICE) {
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_ZUC;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_ZUC_256;
	}

	if (fusectl1 & ICP_ACCEL_GEN4_MASK_ZUC_256_SLICE)
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_ZUC_256;

	capabilities_asym = ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC |
			  ICP_ACCEL_CAPABILITIES_SM2 |
			  ICP_ACCEL_CAPABILITIES_ECEDMONT;

	if (fusectl1 & ICP_ACCEL_GEN4_MASK_PKE_SLICE) {
		capabilities_asym &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;
		capabilities_asym &= ~ICP_ACCEL_CAPABILITIES_SM2;
		capabilities_asym &= ~ICP_ACCEL_CAPABILITIES_ECEDMONT;
	}

	capabilities_dc = ICP_ACCEL_CAPABILITIES_COMPRESSION |
			  ICP_ACCEL_CAPABILITIES_LZ4_COMPRESSION |
			  ICP_ACCEL_CAPABILITIES_LZ4S_COMPRESSION |
			  ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY64;

	if (fusectl1 & ICP_ACCEL_GEN4_MASK_COMPRESS_SLICE) {
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_COMPRESSION;
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_LZ4_COMPRESSION;
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_LZ4S_COMPRESSION;
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY64;
	}

	switch (adf_get_service_enabled(accel_dev)) {
	case SVC_CY:
	case SVC_CY2:
		return capabilities_sym | capabilities_asym;
	case SVC_DC:
		return capabilities_dc;
	case SVC_DCC:
		/*
		 * Sym capabilities are available for chaining operations,
		 * but sym crypto instances cannot be supported
		 */
		capabilities_dcc = capabilities_dc | capabilities_sym;
		capabilities_dcc &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC;
		return capabilities_dcc;
	case SVC_SYM:
		return capabilities_sym;
	case SVC_ASYM:
		return capabilities_asym;
	case SVC_ASYM_DC:
	case SVC_DC_ASYM:
		return capabilities_asym | capabilities_dc;
	case SVC_SYM_DC:
	case SVC_DC_SYM:
		return capabilities_sym | capabilities_dc;
	default:
		return 0;
	}
}

static const u32 *adf_get_arbiter_mapping(struct adf_accel_dev *accel_dev)
{
	if (adf_gen4_init_thd2arb_map(accel_dev))
		dev_warn(&GET_DEV(accel_dev),
			 "Generate of the thread to arbiter map failed");

	return GET_HW_DATA(accel_dev)->thd_to_arb_map;
}

static void adf_init_rl_data(struct adf_rl_hw_data *rl_data)
{
	rl_data->pciout_tb_offset = ADF_GEN4_RL_TOKEN_PCIEOUT_BUCKET_OFFSET;
	rl_data->pciin_tb_offset = ADF_GEN4_RL_TOKEN_PCIEIN_BUCKET_OFFSET;
	rl_data->r2l_offset = ADF_GEN4_RL_R2L_OFFSET;
	rl_data->l2c_offset = ADF_GEN4_RL_L2C_OFFSET;
	rl_data->c2s_offset = ADF_GEN4_RL_C2S_OFFSET;

	rl_data->pcie_scale_div = ADF_420XX_RL_PCIE_SCALE_FACTOR_DIV;
	rl_data->pcie_scale_mul = ADF_420XX_RL_PCIE_SCALE_FACTOR_MUL;
	rl_data->dcpr_correction = ADF_420XX_RL_DCPR_CORRECTION;
	rl_data->max_tp[ADF_SVC_ASYM] = ADF_420XX_RL_MAX_TP_ASYM;
	rl_data->max_tp[ADF_SVC_SYM] = ADF_420XX_RL_MAX_TP_SYM;
	rl_data->max_tp[ADF_SVC_DC] = ADF_420XX_RL_MAX_TP_DC;
	rl_data->scan_interval = ADF_420XX_RL_SCANS_PER_SEC;
	rl_data->scale_ref = ADF_420XX_RL_SLICE_REF;
}

static int get_rp_group(struct adf_accel_dev *accel_dev, u32 ae_mask)
{
	switch (ae_mask) {
	case ADF_AE_GROUP_0:
		return RP_GROUP_0;
	case ADF_AE_GROUP_1:
	case ADF_AE_GROUP_3:
		return RP_GROUP_1;
	case ADF_AE_GROUP_2:
		if (get_fw_config(accel_dev) == adf_fw_cy_config)
			return RP_GROUP_0;
		else
			return RP_GROUP_1;
	default:
		dev_dbg(&GET_DEV(accel_dev), "ae_mask not recognized");
		return -EINVAL;
	}
}

static u32 get_ena_thd_mask(struct adf_accel_dev *accel_dev, u32 obj_num)
{
	const struct adf_fw_config *fw_config;

	if (obj_num >= uof_get_num_objs(accel_dev))
		return ADF_GEN4_ENA_THD_MASK_ERROR;

	fw_config = get_fw_config(accel_dev);
	if (!fw_config)
		return ADF_GEN4_ENA_THD_MASK_ERROR;

	switch (fw_config[obj_num].obj) {
	case ADF_FW_ASYM_OBJ:
		return ENA_THD_MASK_ASYM;
	case ADF_FW_SYM_OBJ:
		return ENA_THD_MASK_SYM;
	case ADF_FW_DC_OBJ:
		return ENA_THD_MASK_DC;
	default:
		return ADF_GEN4_ENA_THD_MASK_ERROR;
	}
}

static const char *uof_get_name(struct adf_accel_dev *accel_dev, u32 obj_num,
				const char * const fw_objs[], int num_objs)
{
	const struct adf_fw_config *fw_config;
	int id;

	fw_config = get_fw_config(accel_dev);
	if (fw_config)
		id = fw_config[obj_num].obj;
	else
		id = -EINVAL;

	if (id < 0 || id > num_objs)
		return NULL;

	return fw_objs[id];
}

static const char *uof_get_name_420xx(struct adf_accel_dev *accel_dev, u32 obj_num)
{
	int num_fw_objs = ARRAY_SIZE(adf_420xx_fw_objs);

	return uof_get_name(accel_dev, obj_num, adf_420xx_fw_objs, num_fw_objs);
}

static int uof_get_obj_type(struct adf_accel_dev *accel_dev, u32 obj_num)
{
	const struct adf_fw_config *fw_config;

	if (obj_num >= uof_get_num_objs(accel_dev))
		return -EINVAL;

	fw_config = get_fw_config(accel_dev);
	if (!fw_config)
		return -EINVAL;

	return fw_config[obj_num].obj;
}

static u32 uof_get_ae_mask(struct adf_accel_dev *accel_dev, u32 obj_num)
{
	const struct adf_fw_config *fw_config;

	fw_config = get_fw_config(accel_dev);
	if (!fw_config)
		return 0;

	return fw_config[obj_num].ae_mask;
}

static void adf_gen4_set_err_mask(struct adf_dev_err_mask *dev_err_mask)
{
	dev_err_mask->cppagentcmdpar_mask = ADF_420XX_HICPPAGENTCMDPARERRLOG_MASK;
	dev_err_mask->parerr_ath_cph_mask = ADF_420XX_PARITYERRORMASK_ATH_CPH_MASK;
	dev_err_mask->parerr_cpr_xlt_mask = ADF_420XX_PARITYERRORMASK_CPR_XLT_MASK;
	dev_err_mask->parerr_dcpr_ucs_mask = ADF_420XX_PARITYERRORMASK_DCPR_UCS_MASK;
	dev_err_mask->parerr_pke_mask = ADF_420XX_PARITYERRORMASK_PKE_MASK;
	dev_err_mask->ssmfeatren_mask = ADF_420XX_SSMFEATREN_MASK;
}

void adf_init_hw_data_420xx(struct adf_hw_device_data *hw_data, u32 dev_id)
{
	hw_data->dev_class = &adf_420xx_class;
	hw_data->instance_id = adf_420xx_class.instances++;
	hw_data->num_banks = ADF_GEN4_ETR_MAX_BANKS;
	hw_data->num_banks_per_vf = ADF_GEN4_NUM_BANKS_PER_VF;
	hw_data->num_rings_per_bank = ADF_GEN4_NUM_RINGS_PER_BANK;
	hw_data->num_accel = ADF_GEN4_MAX_ACCELERATORS;
	hw_data->num_engines = ADF_420XX_MAX_ACCELENGINES;
	hw_data->num_logical_accel = 1;
	hw_data->tx_rx_gap = ADF_GEN4_RX_RINGS_OFFSET;
	hw_data->tx_rings_mask = ADF_GEN4_TX_RINGS_MASK;
	hw_data->ring_to_svc_map = ADF_GEN4_DEFAULT_RING_TO_SRV_MAP;
	hw_data->alloc_irq = adf_isr_resource_alloc;
	hw_data->free_irq = adf_isr_resource_free;
	hw_data->enable_error_correction = adf_gen4_enable_error_correction;
	hw_data->get_accel_mask = adf_gen4_get_accel_mask;
	hw_data->get_ae_mask = get_ae_mask;
	hw_data->get_num_accels = adf_gen4_get_num_accels;
	hw_data->get_num_aes = adf_gen4_get_num_aes;
	hw_data->get_sram_bar_id = adf_gen4_get_sram_bar_id;
	hw_data->get_etr_bar_id = adf_gen4_get_etr_bar_id;
	hw_data->get_misc_bar_id = adf_gen4_get_misc_bar_id;
	hw_data->get_arb_info = adf_gen4_get_arb_info;
	hw_data->get_admin_info = adf_gen4_get_admin_info;
	hw_data->get_accel_cap = get_accel_cap;
	hw_data->get_sku = adf_gen4_get_sku;
	hw_data->init_admin_comms = adf_init_admin_comms;
	hw_data->exit_admin_comms = adf_exit_admin_comms;
	hw_data->send_admin_init = adf_send_admin_init;
	hw_data->init_arb = adf_init_arb;
	hw_data->exit_arb = adf_exit_arb;
	hw_data->get_arb_mapping = adf_get_arbiter_mapping;
	hw_data->enable_ints = adf_gen4_enable_ints;
	hw_data->init_device = adf_gen4_init_device;
	hw_data->reset_device = adf_reset_flr;
	hw_data->admin_ae_mask = ADF_420XX_ADMIN_AE_MASK;
	hw_data->num_rps = ADF_GEN4_MAX_RPS;
	hw_data->fw_name = ADF_420XX_FW;
	hw_data->fw_mmp_name = ADF_420XX_MMP;
	hw_data->uof_get_name = uof_get_name_420xx;
	hw_data->uof_get_num_objs = uof_get_num_objs;
	hw_data->uof_get_obj_type = uof_get_obj_type;
	hw_data->uof_get_ae_mask = uof_get_ae_mask;
	hw_data->get_rp_group = get_rp_group;
	hw_data->get_ena_thd_mask = get_ena_thd_mask;
	hw_data->set_msix_rttable = adf_gen4_set_msix_default_rttable;
	hw_data->set_ssm_wdtimer = adf_gen4_set_ssm_wdtimer;
	hw_data->get_ring_to_svc_map = adf_gen4_get_ring_to_svc_map;
	hw_data->disable_iov = adf_disable_sriov;
	hw_data->ring_pair_reset = adf_gen4_ring_pair_reset;
	hw_data->enable_pm = adf_gen4_enable_pm;
	hw_data->handle_pm_interrupt = adf_gen4_handle_pm_interrupt;
	hw_data->dev_config = adf_gen4_dev_config;
	hw_data->start_timer = adf_gen4_timer_start;
	hw_data->stop_timer = adf_gen4_timer_stop;
	hw_data->get_hb_clock = adf_gen4_get_heartbeat_clock;
	hw_data->num_hb_ctrs = ADF_NUM_HB_CNT_PER_AE;
	hw_data->clock_frequency = ADF_420XX_AE_FREQ;

	adf_gen4_set_err_mask(&hw_data->dev_err_mask);
	adf_gen4_init_hw_csr_ops(&hw_data->csr_ops);
	adf_gen4_init_pf_pfvf_ops(&hw_data->pfvf_ops);
	adf_gen4_init_dc_ops(&hw_data->dc_ops);
	adf_gen4_init_ras_ops(&hw_data->ras_ops);
	adf_gen4_init_tl_data(&hw_data->tl_data);
	adf_init_rl_data(&hw_data->rl_data);
}

void adf_clean_hw_data_420xx(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class->instances--;
}
