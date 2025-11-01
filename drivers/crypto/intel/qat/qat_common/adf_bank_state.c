// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2025 Intel Corporation */

#define pr_fmt(fmt)	"QAT: " fmt

#include <linux/bits.h>
#include <linux/dev_printk.h>
#include <linux/printk.h>
#include "adf_accel_devices.h"
#include "adf_bank_state.h"
#include "adf_common_drv.h"

/* Ring interrupt masks */
#define ADF_RP_INT_SRC_SEL_F_RISE_MASK	GENMASK(1, 0)
#define ADF_RP_INT_SRC_SEL_F_FALL_MASK	GENMASK(2, 0)
#define ADF_RP_INT_SRC_SEL_RANGE_WIDTH	4

static inline int check_stat(u32 (*op)(void __iomem *, u32), u32 expect_val,
			     const char *name, void __iomem *base, u32 bank)
{
	u32 actual_val = op(base, bank);

	if (expect_val == actual_val)
		return 0;

	pr_err("Fail to restore %s register. Expected %#x, actual %#x\n",
	       name, expect_val, actual_val);

	return -EINVAL;
}

static void bank_state_save(struct adf_hw_csr_ops *ops, void __iomem *base,
			    u32 bank, struct adf_bank_state *state, u32 num_rings)
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

static int bank_state_restore(struct adf_hw_csr_ops *ops, void __iomem *base,
			      u32 bank, struct adf_bank_state *state, u32 num_rings,
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

	/*
	 * Verify whether any exceptions were raised during the bank save process.
	 * If exceptions occurred, the status and exception registers cannot
	 * be directly restored. Consequently, further restoration is not
	 * feasible, and the current state of the ring should be maintained.
	 */
	val = state->ringexpstat;
	if (val) {
		pr_info("Bank %u state not fully restored due to exception in saved state (%#x)\n",
			bank, val);
		return 0;
	}

	/* Ensure that the restoration process completed without exceptions */
	tmp_val = ops->read_csr_exp_stat(base, bank);
	if (tmp_val) {
		pr_err("Bank %u restored with exception: %#x\n", bank, tmp_val);
		return -EFAULT;
	}

	ops->write_csr_ring_srv_arb_en(base, bank, state->ringsrvarben);

	/* Check that all ring statuses match the saved state. */
	ret = check_stat(ops->read_csr_stat, state->ringstat0, "ringstat",
			 base, bank);
	if (ret)
		return ret;

	ret = check_stat(ops->read_csr_e_stat, state->ringestat, "ringestat",
			 base, bank);
	if (ret)
		return ret;

	ret = check_stat(ops->read_csr_ne_stat, state->ringnestat, "ringnestat",
			 base, bank);
	if (ret)
		return ret;

	ret = check_stat(ops->read_csr_nf_stat, state->ringnfstat, "ringnfstat",
			 base, bank);
	if (ret)
		return ret;

	ret = check_stat(ops->read_csr_f_stat, state->ringfstat, "ringfstat",
			 base, bank);
	if (ret)
		return ret;

	ret = check_stat(ops->read_csr_c_stat, state->ringcstat0, "ringcstat",
			 base, bank);
	if (ret)
		return ret;

	return 0;
}

/**
 * adf_bank_state_save() - save state of bank-related registers
 * @accel_dev: Pointer to the device structure
 * @bank_number: Bank number
 * @state: Pointer to bank state structure
 *
 * This function saves the state of a bank by reading the bank CSRs and
 * writing them in the @state structure.
 *
 * Returns 0 on success, error code otherwise
 */
int adf_bank_state_save(struct adf_accel_dev *accel_dev, u32 bank_number,
			struct adf_bank_state *state)
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
EXPORT_SYMBOL_GPL(adf_bank_state_save);

/**
 * adf_bank_state_restore() - restore state of bank-related registers
 * @accel_dev: Pointer to the device structure
 * @bank_number: Bank number
 * @state: Pointer to bank state structure
 *
 * This function attempts to restore the state of a bank by writing the
 * bank CSRs to the values in the state structure.
 *
 * Returns 0 on success, error code otherwise
 */
int adf_bank_state_restore(struct adf_accel_dev *accel_dev, u32 bank_number,
			   struct adf_bank_state *state)
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
EXPORT_SYMBOL_GPL(adf_bank_state_restore);
