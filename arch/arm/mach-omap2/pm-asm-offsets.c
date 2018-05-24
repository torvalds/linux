// SPDX-License-Identifier: GPL-2.0
/*
 * TI AM33XX and AM43XX PM Assembly Offsets
 *
 * Copyright (C) 2017-2018 Texas Instruments Inc.
 */

#include <linux/kbuild.h>
#include <linux/platform_data/pm33xx.h>

int main(void)
{
	DEFINE(AMX3_PM_WFI_FLAGS_OFFSET,
	       offsetof(struct am33xx_pm_sram_data, wfi_flags));
	DEFINE(AMX3_PM_L2_AUX_CTRL_VAL_OFFSET,
	       offsetof(struct am33xx_pm_sram_data, l2_aux_ctrl_val));
	DEFINE(AMX3_PM_L2_PREFETCH_CTRL_VAL_OFFSET,
	       offsetof(struct am33xx_pm_sram_data, l2_prefetch_ctrl_val));
	DEFINE(AMX3_PM_SRAM_DATA_SIZE, sizeof(struct am33xx_pm_sram_data));

	BLANK();

	DEFINE(AMX3_PM_RO_SRAM_DATA_VIRT_OFFSET,
	       offsetof(struct am33xx_pm_ro_sram_data, amx3_pm_sram_data_virt));
	DEFINE(AMX3_PM_RO_SRAM_DATA_PHYS_OFFSET,
	       offsetof(struct am33xx_pm_ro_sram_data, amx3_pm_sram_data_phys));
	DEFINE(AMX3_PM_RO_SRAM_DATA_SIZE,
	       sizeof(struct am33xx_pm_ro_sram_data));

	return 0;
}
