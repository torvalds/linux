// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2020 Intel Corporation */
#include <linux/iopoll.h>
#include "adf_accel_devices.h"
#include "adf_cfg_services.h"
#include "adf_common_drv.h"
#include "adf_fw_config.h"
#include "adf_gen4_hw_data.h"
#include "adf_gen4_pm.h"

static u64 build_csr_ring_base_addr(dma_addr_t addr, u32 size)
{
	return BUILD_RING_BASE_ADDR(addr, size);
}

static u32 read_csr_ring_head(void __iomem *csr_base_addr, u32 bank, u32 ring)
{
	return READ_CSR_RING_HEAD(csr_base_addr, bank, ring);
}

static void write_csr_ring_head(void __iomem *csr_base_addr, u32 bank, u32 ring,
				u32 value)
{
	WRITE_CSR_RING_HEAD(csr_base_addr, bank, ring, value);
}

static u32 read_csr_ring_tail(void __iomem *csr_base_addr, u32 bank, u32 ring)
{
	return READ_CSR_RING_TAIL(csr_base_addr, bank, ring);
}

static void write_csr_ring_tail(void __iomem *csr_base_addr, u32 bank, u32 ring,
				u32 value)
{
	WRITE_CSR_RING_TAIL(csr_base_addr, bank, ring, value);
}

static u32 read_csr_e_stat(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_E_STAT(csr_base_addr, bank);
}

static void write_csr_ring_config(void __iomem *csr_base_addr, u32 bank, u32 ring,
				  u32 value)
{
	WRITE_CSR_RING_CONFIG(csr_base_addr, bank, ring, value);
}

static void write_csr_ring_base(void __iomem *csr_base_addr, u32 bank, u32 ring,
				dma_addr_t addr)
{
	WRITE_CSR_RING_BASE(csr_base_addr, bank, ring, addr);
}

static void write_csr_int_flag(void __iomem *csr_base_addr, u32 bank,
			       u32 value)
{
	WRITE_CSR_INT_FLAG(csr_base_addr, bank, value);
}

static void write_csr_int_srcsel(void __iomem *csr_base_addr, u32 bank)
{
	WRITE_CSR_INT_SRCSEL(csr_base_addr, bank);
}

static void write_csr_int_col_en(void __iomem *csr_base_addr, u32 bank, u32 value)
{
	WRITE_CSR_INT_COL_EN(csr_base_addr, bank, value);
}

static void write_csr_int_col_ctl(void __iomem *csr_base_addr, u32 bank,
				  u32 value)
{
	WRITE_CSR_INT_COL_CTL(csr_base_addr, bank, value);
}

static void write_csr_int_flag_and_col(void __iomem *csr_base_addr, u32 bank,
				       u32 value)
{
	WRITE_CSR_INT_FLAG_AND_COL(csr_base_addr, bank, value);
}

static void write_csr_ring_srv_arb_en(void __iomem *csr_base_addr, u32 bank,
				      u32 value)
{
	WRITE_CSR_RING_SRV_ARB_EN(csr_base_addr, bank, value);
}

void adf_gen4_init_hw_csr_ops(struct adf_hw_csr_ops *csr_ops)
{
	csr_ops->build_csr_ring_base_addr = build_csr_ring_base_addr;
	csr_ops->read_csr_ring_head = read_csr_ring_head;
	csr_ops->write_csr_ring_head = write_csr_ring_head;
	csr_ops->read_csr_ring_tail = read_csr_ring_tail;
	csr_ops->write_csr_ring_tail = write_csr_ring_tail;
	csr_ops->read_csr_e_stat = read_csr_e_stat;
	csr_ops->write_csr_ring_config = write_csr_ring_config;
	csr_ops->write_csr_ring_base = write_csr_ring_base;
	csr_ops->write_csr_int_flag = write_csr_int_flag;
	csr_ops->write_csr_int_srcsel = write_csr_int_srcsel;
	csr_ops->write_csr_int_col_en = write_csr_int_col_en;
	csr_ops->write_csr_int_col_ctl = write_csr_int_col_ctl;
	csr_ops->write_csr_int_flag_and_col = write_csr_int_flag_and_col;
	csr_ops->write_csr_ring_srv_arb_en = write_csr_ring_srv_arb_en;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_hw_csr_ops);

u32 adf_gen4_get_accel_mask(struct adf_hw_device_data *self)
{
	return ADF_GEN4_ACCELERATORS_MASK;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_accel_mask);

u32 adf_gen4_get_num_accels(struct adf_hw_device_data *self)
{
	return ADF_GEN4_MAX_ACCELERATORS;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_num_accels);

u32 adf_gen4_get_num_aes(struct adf_hw_device_data *self)
{
	if (!self || !self->ae_mask)
		return 0;

	return hweight32(self->ae_mask);
}
EXPORT_SYMBOL_GPL(adf_gen4_get_num_aes);

u32 adf_gen4_get_misc_bar_id(struct adf_hw_device_data *self)
{
	return ADF_GEN4_PMISC_BAR;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_misc_bar_id);

u32 adf_gen4_get_etr_bar_id(struct adf_hw_device_data *self)
{
	return ADF_GEN4_ETR_BAR;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_etr_bar_id);

u32 adf_gen4_get_sram_bar_id(struct adf_hw_device_data *self)
{
	return ADF_GEN4_SRAM_BAR;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_sram_bar_id);

enum dev_sku_info adf_gen4_get_sku(struct adf_hw_device_data *self)
{
	return DEV_SKU_1;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_sku);

void adf_gen4_get_arb_info(struct arb_info *arb_info)
{
	arb_info->arb_cfg = ADF_GEN4_ARB_CONFIG;
	arb_info->arb_offset = ADF_GEN4_ARB_OFFSET;
	arb_info->wt2sam_offset = ADF_GEN4_ARB_WRK_2_SER_MAP_OFFSET;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_arb_info);

void adf_gen4_get_admin_info(struct admin_info *admin_csrs_info)
{
	admin_csrs_info->mailbox_offset = ADF_GEN4_MAILBOX_BASE_OFFSET;
	admin_csrs_info->admin_msg_ur = ADF_GEN4_ADMINMSGUR_OFFSET;
	admin_csrs_info->admin_msg_lr = ADF_GEN4_ADMINMSGLR_OFFSET;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_admin_info);

u32 adf_gen4_get_heartbeat_clock(struct adf_hw_device_data *self)
{
	/*
	 * GEN4 uses KPT counter for HB
	 */
	return ADF_GEN4_KPT_COUNTER_FREQ;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_heartbeat_clock);

void adf_gen4_enable_error_correction(struct adf_accel_dev *accel_dev)
{
	struct adf_bar *misc_bar = &GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR];
	void __iomem *csr = misc_bar->virt_addr;

	/* Enable all in errsou3 except VFLR notification on host */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK3, ADF_GEN4_VFLNOTIFY);
}
EXPORT_SYMBOL_GPL(adf_gen4_enable_error_correction);

void adf_gen4_enable_ints(struct adf_accel_dev *accel_dev)
{
	void __iomem *addr;

	addr = (&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;

	/* Enable bundle interrupts */
	ADF_CSR_WR(addr, ADF_GEN4_SMIAPF_RP_X0_MASK_OFFSET, 0);
	ADF_CSR_WR(addr, ADF_GEN4_SMIAPF_RP_X1_MASK_OFFSET, 0);

	/* Enable misc interrupts */
	ADF_CSR_WR(addr, ADF_GEN4_SMIAPF_MASK_OFFSET, 0);
}
EXPORT_SYMBOL_GPL(adf_gen4_enable_ints);

int adf_gen4_init_device(struct adf_accel_dev *accel_dev)
{
	void __iomem *addr;
	u32 status;
	u32 csr;
	int ret;

	addr = (&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;

	/* Temporarily mask PM interrupt */
	csr = ADF_CSR_RD(addr, ADF_GEN4_ERRMSK2);
	csr |= ADF_GEN4_PM_SOU;
	ADF_CSR_WR(addr, ADF_GEN4_ERRMSK2, csr);

	/* Set DRV_ACTIVE bit to power up the device */
	ADF_CSR_WR(addr, ADF_GEN4_PM_INTERRUPT, ADF_GEN4_PM_DRV_ACTIVE);

	/* Poll status register to make sure the device is powered up */
	ret = read_poll_timeout(ADF_CSR_RD, status,
				status & ADF_GEN4_PM_INIT_STATE,
				ADF_GEN4_PM_POLL_DELAY_US,
				ADF_GEN4_PM_POLL_TIMEOUT_US, true, addr,
				ADF_GEN4_PM_STATUS);
	if (ret)
		dev_err(&GET_DEV(accel_dev), "Failed to power up the device\n");

	return ret;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_device);

static inline void adf_gen4_unpack_ssm_wdtimer(u64 value, u32 *upper,
					       u32 *lower)
{
	*lower = lower_32_bits(value);
	*upper = upper_32_bits(value);
}

void adf_gen4_set_ssm_wdtimer(struct adf_accel_dev *accel_dev)
{
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);
	u64 timer_val_pke = ADF_SSM_WDT_PKE_DEFAULT_VALUE;
	u64 timer_val = ADF_SSM_WDT_DEFAULT_VALUE;
	u32 ssm_wdt_pke_high = 0;
	u32 ssm_wdt_pke_low = 0;
	u32 ssm_wdt_high = 0;
	u32 ssm_wdt_low = 0;

	/* Convert 64bit WDT timer value into 32bit values for
	 * mmio write to 32bit CSRs.
	 */
	adf_gen4_unpack_ssm_wdtimer(timer_val, &ssm_wdt_high, &ssm_wdt_low);
	adf_gen4_unpack_ssm_wdtimer(timer_val_pke, &ssm_wdt_pke_high,
				    &ssm_wdt_pke_low);

	/* Enable WDT for sym and dc */
	ADF_CSR_WR(pmisc_addr, ADF_SSMWDTL_OFFSET, ssm_wdt_low);
	ADF_CSR_WR(pmisc_addr, ADF_SSMWDTH_OFFSET, ssm_wdt_high);
	/* Enable WDT for pke */
	ADF_CSR_WR(pmisc_addr, ADF_SSMWDTPKEL_OFFSET, ssm_wdt_pke_low);
	ADF_CSR_WR(pmisc_addr, ADF_SSMWDTPKEH_OFFSET, ssm_wdt_pke_high);
}
EXPORT_SYMBOL_GPL(adf_gen4_set_ssm_wdtimer);

/*
 * The vector routing table is used to select the MSI-X entry to use for each
 * interrupt source.
 * The first ADF_GEN4_ETR_MAX_BANKS entries correspond to ring interrupts.
 * The final entry corresponds to VF2PF or error interrupts.
 * This vector table could be used to configure one MSI-X entry to be shared
 * between multiple interrupt sources.
 *
 * The default routing is set to have a one to one correspondence between the
 * interrupt source and the MSI-X entry used.
 */
void adf_gen4_set_msix_default_rttable(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr;
	int i;

	csr = (&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;
	for (i = 0; i <= ADF_GEN4_ETR_MAX_BANKS; i++)
		ADF_CSR_WR(csr, ADF_GEN4_MSIX_RTTABLE_OFFSET(i), i);
}
EXPORT_SYMBOL_GPL(adf_gen4_set_msix_default_rttable);

int adf_pfvf_comms_disabled(struct adf_accel_dev *accel_dev)
{
	return 0;
}
EXPORT_SYMBOL_GPL(adf_pfvf_comms_disabled);

static int reset_ring_pair(void __iomem *csr, u32 bank_number)
{
	u32 status;
	int ret;

	/* Write rpresetctl register BIT(0) as 1
	 * Since rpresetctl registers have no RW fields, no need to preserve
	 * values for other bits. Just write directly.
	 */
	ADF_CSR_WR(csr, ADF_WQM_CSR_RPRESETCTL(bank_number),
		   ADF_WQM_CSR_RPRESETCTL_RESET);

	/* Read rpresetsts register and wait for rp reset to complete */
	ret = read_poll_timeout(ADF_CSR_RD, status,
				status & ADF_WQM_CSR_RPRESETSTS_STATUS,
				ADF_RPRESET_POLL_DELAY_US,
				ADF_RPRESET_POLL_TIMEOUT_US, true,
				csr, ADF_WQM_CSR_RPRESETSTS(bank_number));
	if (!ret) {
		/* When rp reset is done, clear rpresetsts */
		ADF_CSR_WR(csr, ADF_WQM_CSR_RPRESETSTS(bank_number),
			   ADF_WQM_CSR_RPRESETSTS_STATUS);
	}

	return ret;
}

int adf_gen4_ring_pair_reset(struct adf_accel_dev *accel_dev, u32 bank_number)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 etr_bar_id = hw_data->get_etr_bar_id(hw_data);
	void __iomem *csr;
	int ret;

	if (bank_number >= hw_data->num_banks)
		return -EINVAL;

	dev_dbg(&GET_DEV(accel_dev),
		"ring pair reset for bank:%d\n", bank_number);

	csr = (&GET_BARS(accel_dev)[etr_bar_id])->virt_addr;
	ret = reset_ring_pair(csr, bank_number);
	if (ret)
		dev_err(&GET_DEV(accel_dev),
			"ring pair reset failed (timeout)\n");
	else
		dev_dbg(&GET_DEV(accel_dev), "ring pair reset successful\n");

	return ret;
}
EXPORT_SYMBOL_GPL(adf_gen4_ring_pair_reset);

static const u32 thrd_to_arb_map_dcc[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x0000FFFF, 0x0000FFFF, 0x0000FFFF, 0x0000FFFF,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x0
};

static const u16 rp_group_to_arb_mask[] = {
	[RP_GROUP_0] = 0x5,
	[RP_GROUP_1] = 0xA,
};

static bool is_single_service(int service_id)
{
	switch (service_id) {
	case SVC_DC:
	case SVC_SYM:
	case SVC_ASYM:
		return true;
	case SVC_CY:
	case SVC_CY2:
	case SVC_DCC:
	case SVC_ASYM_DC:
	case SVC_DC_ASYM:
	case SVC_SYM_DC:
	case SVC_DC_SYM:
	default:
		return false;
	}
}

int adf_gen4_init_thd2arb_map(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	u32 *thd2arb_map = hw_data->thd_to_arb_map;
	unsigned int ae_cnt, worker_obj_cnt, i, j;
	unsigned long ae_mask, thds_mask;
	int srv_id, rp_group;
	u32 thd2arb_map_base;
	u16 arb_mask;

	if (!hw_data->get_rp_group || !hw_data->get_ena_thd_mask ||
	    !hw_data->get_num_aes || !hw_data->uof_get_num_objs ||
	    !hw_data->uof_get_ae_mask)
		return -EFAULT;

	srv_id = adf_get_service_enabled(accel_dev);
	if (srv_id < 0)
		return srv_id;

	ae_cnt = hw_data->get_num_aes(hw_data);
	worker_obj_cnt = hw_data->uof_get_num_objs(accel_dev) -
			 ADF_GEN4_ADMIN_ACCELENGINES;

	if (srv_id == SVC_DCC) {
		if (ae_cnt > ICP_QAT_HW_AE_DELIMITER)
			return -EINVAL;

		memcpy(thd2arb_map, thrd_to_arb_map_dcc,
		       array_size(sizeof(*thd2arb_map), ae_cnt));
		return 0;
	}

	for (i = 0; i < worker_obj_cnt; i++) {
		ae_mask = hw_data->uof_get_ae_mask(accel_dev, i);
		rp_group = hw_data->get_rp_group(accel_dev, ae_mask);
		thds_mask = hw_data->get_ena_thd_mask(accel_dev, i);
		thd2arb_map_base = 0;

		if (rp_group >= RP_GROUP_COUNT || rp_group < RP_GROUP_0)
			return -EINVAL;

		if (thds_mask == ADF_GEN4_ENA_THD_MASK_ERROR)
			return -EINVAL;

		if (is_single_service(srv_id))
			arb_mask = rp_group_to_arb_mask[RP_GROUP_0] |
				   rp_group_to_arb_mask[RP_GROUP_1];
		else
			arb_mask = rp_group_to_arb_mask[rp_group];

		for_each_set_bit(j, &thds_mask, ADF_NUM_THREADS_PER_AE)
			thd2arb_map_base |= arb_mask << (j * 4);

		for_each_set_bit(j, &ae_mask, ae_cnt)
			thd2arb_map[j] = thd2arb_map_base;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_thd2arb_map);

u16 adf_gen4_get_ring_to_svc_map(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	enum adf_cfg_service_type rps[RP_GROUP_COUNT] = { };
	unsigned int ae_mask, start_id, worker_obj_cnt, i;
	u16 ring_to_svc_map;
	int rp_group;

	if (!hw_data->get_rp_group || !hw_data->uof_get_ae_mask ||
	    !hw_data->uof_get_obj_type || !hw_data->uof_get_num_objs)
		return 0;

	/* If dcc, all rings handle compression requests */
	if (adf_get_service_enabled(accel_dev) == SVC_DCC) {
		for (i = 0; i < RP_GROUP_COUNT; i++)
			rps[i] = COMP;
		goto set_mask;
	}

	worker_obj_cnt = hw_data->uof_get_num_objs(accel_dev) -
			 ADF_GEN4_ADMIN_ACCELENGINES;
	start_id = worker_obj_cnt - RP_GROUP_COUNT;

	for (i = start_id; i < worker_obj_cnt; i++) {
		ae_mask = hw_data->uof_get_ae_mask(accel_dev, i);
		rp_group = hw_data->get_rp_group(accel_dev, ae_mask);
		if (rp_group >= RP_GROUP_COUNT || rp_group < RP_GROUP_0)
			return 0;

		switch (hw_data->uof_get_obj_type(accel_dev, i)) {
		case ADF_FW_SYM_OBJ:
			rps[rp_group] = SYM;
			break;
		case ADF_FW_ASYM_OBJ:
			rps[rp_group] = ASYM;
			break;
		case ADF_FW_DC_OBJ:
			rps[rp_group] = COMP;
			break;
		default:
			rps[rp_group] = 0;
			break;
		}
	}

set_mask:
	ring_to_svc_map = rps[RP_GROUP_0] << ADF_CFG_SERV_RING_PAIR_0_SHIFT |
			  rps[RP_GROUP_1] << ADF_CFG_SERV_RING_PAIR_1_SHIFT |
			  rps[RP_GROUP_0] << ADF_CFG_SERV_RING_PAIR_2_SHIFT |
			  rps[RP_GROUP_1] << ADF_CFG_SERV_RING_PAIR_3_SHIFT;

	return ring_to_svc_map;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_ring_to_svc_map);
