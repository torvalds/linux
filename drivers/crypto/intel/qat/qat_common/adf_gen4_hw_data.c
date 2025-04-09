// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2020 Intel Corporation */
#include <linux/bitops.h>
#include <linux/iopoll.h>
#include <asm/div64.h>
#include "adf_accel_devices.h"
#include "adf_cfg_services.h"
#include "adf_common_drv.h"
#include "adf_fw_config.h"
#include "adf_gen4_hw_data.h"
#include "adf_gen4_pm.h"

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

void adf_gen4_set_ssm_wdtimer(struct adf_accel_dev *accel_dev)
{
	void __iomem *pmisc_addr = adf_get_pmisc_base(accel_dev);
	u64 timer_val_pke = ADF_SSM_WDT_PKE_DEFAULT_VALUE;
	u64 timer_val = ADF_SSM_WDT_DEFAULT_VALUE;

	/* Enable watchdog timer for sym and dc */
	ADF_CSR_WR64_LO_HI(pmisc_addr, ADF_SSMWDTL_OFFSET, ADF_SSMWDTH_OFFSET, timer_val);

	/* Enable watchdog timer for pke */
	ADF_CSR_WR64_LO_HI(pmisc_addr, ADF_SSMWDTPKEL_OFFSET, ADF_SSMWDTPKEH_OFFSET,
			   timer_val_pke);
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
	void __iomem *csr = adf_get_etr_base(accel_dev);
	int ret;

	if (bank_number >= hw_data->num_banks)
		return -EINVAL;

	dev_dbg(&GET_DEV(accel_dev),
		"ring pair reset for bank:%d\n", bank_number);

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
	default:
		return false;
	}
}

bool adf_gen4_services_supported(unsigned long mask)
{
	unsigned long num_svc = hweight_long(mask);

	if (mask >= BIT(SVC_BASE_COUNT))
		return false;

	switch (num_svc) {
	case ADF_ONE_SERVICE:
		return true;
	case ADF_TWO_SERVICES:
		return !test_bit(SVC_DCC, &mask);
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(adf_gen4_services_supported);

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

/*
 * adf_gen4_bank_quiesce_coal_timer() - quiesce bank coalesced interrupt timer
 * @accel_dev: Pointer to the device structure
 * @bank_idx: Offset to the bank within this device
 * @timeout_ms: Timeout in milliseconds for the operation
 *
 * This function tries to quiesce the coalesced interrupt timer of a bank if
 * it has been enabled and triggered.
 *
 * Returns 0 on success, error code otherwise
 *
 */
int adf_gen4_bank_quiesce_coal_timer(struct adf_accel_dev *accel_dev,
				     u32 bank_idx, int timeout_ms)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	struct adf_hw_csr_ops *csr_ops = GET_CSR_OPS(accel_dev);
	void __iomem *csr_misc = adf_get_pmisc_base(accel_dev);
	void __iomem *csr_etr = adf_get_etr_base(accel_dev);
	u32 int_col_ctl, int_col_mask, int_col_en;
	u32 e_stat, intsrc;
	u64 wait_us;
	int ret;

	if (timeout_ms < 0)
		return -EINVAL;

	int_col_ctl = csr_ops->read_csr_int_col_ctl(csr_etr, bank_idx);
	int_col_mask = csr_ops->get_int_col_ctl_enable_mask();
	if (!(int_col_ctl & int_col_mask))
		return 0;

	int_col_en = csr_ops->read_csr_int_col_en(csr_etr, bank_idx);
	int_col_en &= BIT(ADF_WQM_CSR_RP_IDX_RX);

	e_stat = csr_ops->read_csr_e_stat(csr_etr, bank_idx);
	if (!(~e_stat & int_col_en))
		return 0;

	wait_us = 2 * ((int_col_ctl & ~int_col_mask) << 8) * USEC_PER_SEC;
	do_div(wait_us, hw_data->clock_frequency);
	wait_us = min(wait_us, (u64)timeout_ms * USEC_PER_MSEC);
	dev_dbg(&GET_DEV(accel_dev),
		"wait for bank %d - coalesced timer expires in %llu us (max=%u ms estat=0x%x intcolen=0x%x)\n",
		bank_idx, wait_us, timeout_ms, e_stat, int_col_en);

	ret = read_poll_timeout(ADF_CSR_RD, intsrc, intsrc,
				ADF_COALESCED_POLL_DELAY_US, wait_us, true,
				csr_misc, ADF_WQM_CSR_RPINTSOU(bank_idx));
	if (ret)
		dev_warn(&GET_DEV(accel_dev),
			 "coalesced timer for bank %d expired (%llu us)\n",
			 bank_idx, wait_us);

	return ret;
}
EXPORT_SYMBOL_GPL(adf_gen4_bank_quiesce_coal_timer);

static int drain_bank(void __iomem *csr, u32 bank_number, int timeout_us)
{
	u32 status;

	ADF_CSR_WR(csr, ADF_WQM_CSR_RPRESETCTL(bank_number),
		   ADF_WQM_CSR_RPRESETCTL_DRAIN);

	return read_poll_timeout(ADF_CSR_RD, status,
				status & ADF_WQM_CSR_RPRESETSTS_STATUS,
				ADF_RPRESET_POLL_DELAY_US, timeout_us, true,
				csr, ADF_WQM_CSR_RPRESETSTS(bank_number));
}

void adf_gen4_bank_drain_finish(struct adf_accel_dev *accel_dev,
				u32 bank_number)
{
	void __iomem *csr = adf_get_etr_base(accel_dev);

	ADF_CSR_WR(csr, ADF_WQM_CSR_RPRESETSTS(bank_number),
		   ADF_WQM_CSR_RPRESETSTS_STATUS);
}

int adf_gen4_bank_drain_start(struct adf_accel_dev *accel_dev,
			      u32 bank_number, int timeout_us)
{
	void __iomem *csr = adf_get_etr_base(accel_dev);
	int ret;

	dev_dbg(&GET_DEV(accel_dev), "Drain bank %d\n", bank_number);

	ret = drain_bank(csr, bank_number, timeout_us);
	if (ret)
		dev_err(&GET_DEV(accel_dev), "Bank drain failed (timeout)\n");
	else
		dev_dbg(&GET_DEV(accel_dev), "Bank drain successful\n");

	return ret;
}

static void bank_state_save(struct adf_hw_csr_ops *ops, void __iomem *base,
			    u32 bank, struct bank_state *state, u32 num_rings)
{
	u32 i;

	state->ringstat0 = ops->read_csr_stat(base, bank);
	state->ringuostat = ops->read_csr_uo_stat(base, bank);
	state->ringestat = ops->read_csr_e_stat(base, bank);
	state->ringnestat = ops->read_csr_ne_stat(base, bank);
	state->ringnfstat = ops->read_csr_nf_stat(base, bank);
	state->ringfstat = ops->read_csr_f_stat(base, bank);
	state->ringcstat0 = ops->read_csr_c_stat(base, bank);
	state->iaintflagen = ops->read_csr_int_en(base, bank);
	state->iaintflagreg = ops->read_csr_int_flag(base, bank);
	state->iaintflagsrcsel0 = ops->read_csr_int_srcsel(base, bank);
	state->iaintcolen = ops->read_csr_int_col_en(base, bank);
	state->iaintcolctl = ops->read_csr_int_col_ctl(base, bank);
	state->iaintflagandcolen = ops->read_csr_int_flag_and_col(base, bank);
	state->ringexpstat = ops->read_csr_exp_stat(base, bank);
	state->ringexpintenable = ops->read_csr_exp_int_en(base, bank);
	state->ringsrvarben = ops->read_csr_ring_srv_arb_en(base, bank);

	for (i = 0; i < num_rings; i++) {
		state->rings[i].head = ops->read_csr_ring_head(base, bank, i);
		state->rings[i].tail = ops->read_csr_ring_tail(base, bank, i);
		state->rings[i].config = ops->read_csr_ring_config(base, bank, i);
		state->rings[i].base = ops->read_csr_ring_base(base, bank, i);
	}
}

#define CHECK_STAT(op, expect_val, name, args...) \
({ \
	u32 __expect_val = (expect_val); \
	u32 actual_val = op(args); \
	(__expect_val == actual_val) ? 0 : \
		(pr_err("QAT: Fail to restore %s register. Expected 0x%x, actual 0x%x\n", \
			name, __expect_val, actual_val), -EINVAL); \
})

static int bank_state_restore(struct adf_hw_csr_ops *ops, void __iomem *base,
			      u32 bank, struct bank_state *state, u32 num_rings,
			      int tx_rx_gap)
{
	u32 val, tmp_val, i;
	int ret;

	for (i = 0; i < num_rings; i++)
		ops->write_csr_ring_base(base, bank, i, state->rings[i].base);

	for (i = 0; i < num_rings; i++)
		ops->write_csr_ring_config(base, bank, i, state->rings[i].config);

	for (i = 0; i < num_rings / 2; i++) {
		int tx = i * (tx_rx_gap + 1);
		int rx = tx + tx_rx_gap;

		ops->write_csr_ring_head(base, bank, tx, state->rings[tx].head);
		ops->write_csr_ring_tail(base, bank, tx, state->rings[tx].tail);

		/*
		 * The TX ring head needs to be updated again to make sure that
		 * the HW will not consider the ring as full when it is empty
		 * and the correct state flags are set to match the recovered state.
		 */
		if (state->ringestat & BIT(tx)) {
			val = ops->read_csr_int_srcsel(base, bank);
			val |= ADF_RP_INT_SRC_SEL_F_RISE_MASK;
			ops->write_csr_int_srcsel_w_val(base, bank, val);
			ops->write_csr_ring_head(base, bank, tx, state->rings[tx].head);
		}

		ops->write_csr_ring_tail(base, bank, rx, state->rings[rx].tail);
		val = ops->read_csr_int_srcsel(base, bank);
		val |= ADF_RP_INT_SRC_SEL_F_RISE_MASK << ADF_RP_INT_SRC_SEL_RANGE_WIDTH;
		ops->write_csr_int_srcsel_w_val(base, bank, val);

		ops->write_csr_ring_head(base, bank, rx, state->rings[rx].head);
		val = ops->read_csr_int_srcsel(base, bank);
		val |= ADF_RP_INT_SRC_SEL_F_FALL_MASK << ADF_RP_INT_SRC_SEL_RANGE_WIDTH;
		ops->write_csr_int_srcsel_w_val(base, bank, val);

		/*
		 * The RX ring tail needs to be updated again to make sure that
		 * the HW will not consider the ring as empty when it is full
		 * and the correct state flags are set to match the recovered state.
		 */
		if (state->ringfstat & BIT(rx))
			ops->write_csr_ring_tail(base, bank, rx, state->rings[rx].tail);
	}

	ops->write_csr_int_flag_and_col(base, bank, state->iaintflagandcolen);
	ops->write_csr_int_en(base, bank, state->iaintflagen);
	ops->write_csr_int_col_en(base, bank, state->iaintcolen);
	ops->write_csr_int_srcsel_w_val(base, bank, state->iaintflagsrcsel0);
	ops->write_csr_exp_int_en(base, bank, state->ringexpintenable);
	ops->write_csr_int_col_ctl(base, bank, state->iaintcolctl);
	ops->write_csr_ring_srv_arb_en(base, bank, state->ringsrvarben);

	/* Check that all ring statuses match the saved state. */
	ret = CHECK_STAT(ops->read_csr_stat, state->ringstat0, "ringstat",
			 base, bank);
	if (ret)
		return ret;

	ret = CHECK_STAT(ops->read_csr_e_stat, state->ringestat, "ringestat",
			 base, bank);
	if (ret)
		return ret;

	ret = CHECK_STAT(ops->read_csr_ne_stat, state->ringnestat, "ringnestat",
			 base, bank);
	if (ret)
		return ret;

	ret = CHECK_STAT(ops->read_csr_nf_stat, state->ringnfstat, "ringnfstat",
			 base, bank);
	if (ret)
		return ret;

	ret = CHECK_STAT(ops->read_csr_f_stat, state->ringfstat, "ringfstat",
			 base, bank);
	if (ret)
		return ret;

	ret = CHECK_STAT(ops->read_csr_c_stat, state->ringcstat0, "ringcstat",
			 base, bank);
	if (ret)
		return ret;

	tmp_val = ops->read_csr_exp_stat(base, bank);
	val = state->ringexpstat;
	if (tmp_val && !val) {
		pr_err("QAT: Bank was restored with exception: 0x%x\n", val);
		return -EINVAL;
	}

	return 0;
}

int adf_gen4_bank_state_save(struct adf_accel_dev *accel_dev, u32 bank_number,
			     struct bank_state *state)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	struct adf_hw_csr_ops *csr_ops = GET_CSR_OPS(accel_dev);
	void __iomem *csr_base = adf_get_etr_base(accel_dev);

	if (bank_number >= hw_data->num_banks || !state)
		return -EINVAL;

	dev_dbg(&GET_DEV(accel_dev), "Saving state of bank %d\n", bank_number);

	bank_state_save(csr_ops, csr_base, bank_number, state,
			hw_data->num_rings_per_bank);

	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_bank_state_save);

int adf_gen4_bank_state_restore(struct adf_accel_dev *accel_dev, u32 bank_number,
				struct bank_state *state)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	struct adf_hw_csr_ops *csr_ops = GET_CSR_OPS(accel_dev);
	void __iomem *csr_base = adf_get_etr_base(accel_dev);
	int ret;

	if (bank_number >= hw_data->num_banks  || !state)
		return -EINVAL;

	dev_dbg(&GET_DEV(accel_dev), "Restoring state of bank %d\n", bank_number);

	ret = bank_state_restore(csr_ops, csr_base, bank_number, state,
				 hw_data->num_rings_per_bank, hw_data->tx_rx_gap);
	if (ret)
		dev_err(&GET_DEV(accel_dev),
			"Unable to restore state of bank %d\n", bank_number);

	return ret;
}
EXPORT_SYMBOL_GPL(adf_gen4_bank_state_restore);
