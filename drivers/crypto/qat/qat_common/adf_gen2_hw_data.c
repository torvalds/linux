// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2020 Intel Corporation */
#include "adf_gen2_hw_data.h"

void adf_gen2_cfg_iov_thds(struct adf_accel_dev *accel_dev, bool enable,
			   int num_a_regs, int num_b_regs)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *pmisc_addr;
	struct adf_bar *pmisc;
	int pmisc_id, i;
	u32 reg;

	pmisc_id = hw_data->get_misc_bar_id(hw_data);
	pmisc = &GET_BARS(accel_dev)[pmisc_id];
	pmisc_addr = pmisc->virt_addr;

	/* Set/Unset Valid bit in AE Thread to PCIe Function Mapping Group A */
	for (i = 0; i < num_a_regs; i++) {
		reg = READ_CSR_AE2FUNCTION_MAP_A(pmisc_addr, i);
		if (enable)
			reg |= AE2FUNCTION_MAP_VALID;
		else
			reg &= ~AE2FUNCTION_MAP_VALID;
		WRITE_CSR_AE2FUNCTION_MAP_A(pmisc_addr, i, reg);
	}

	/* Set/Unset Valid bit in AE Thread to PCIe Function Mapping Group B */
	for (i = 0; i < num_b_regs; i++) {
		reg = READ_CSR_AE2FUNCTION_MAP_B(pmisc_addr, i);
		if (enable)
			reg |= AE2FUNCTION_MAP_VALID;
		else
			reg &= ~AE2FUNCTION_MAP_VALID;
		WRITE_CSR_AE2FUNCTION_MAP_B(pmisc_addr, i, reg);
	}
}
EXPORT_SYMBOL_GPL(adf_gen2_cfg_iov_thds);
