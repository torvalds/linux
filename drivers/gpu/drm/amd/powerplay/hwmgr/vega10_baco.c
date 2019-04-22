/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "amdgpu.h"
#include "soc15.h"
#include "soc15_hw_ip.h"
#include "vega10_ip_offset.h"
#include "soc15_common.h"
#include "vega10_inc.h"
#include "vega10_ppsmc.h"
#include "vega10_baco.h"



static const struct soc15_baco_cmd_entry  pre_baco_tbl[] =
{
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(NBIF, 0, mmBIF_DOORBELL_CNTL), BIF_DOORBELL_CNTL__DOORBELL_MONITOR_EN_MASK, BIF_DOORBELL_CNTL__DOORBELL_MONITOR_EN__SHIFT, 0, 1},
	{CMD_WRITE, SOC15_REG_ENTRY(NBIF, 0, mmBIF_FB_EN), 0, 0, 0, 0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(NBIF, 0, mmBACO_CNTL), BACO_CNTL__BACO_DSTATE_BYPASS_MASK, BACO_CNTL__BACO_DSTATE_BYPASS__SHIFT, 0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(NBIF, 0, mmBACO_CNTL), BACO_CNTL__BACO_RST_INTR_MASK_MASK, BACO_CNTL__BACO_RST_INTR_MASK__SHIFT, 0, 1}
};

static const struct soc15_baco_cmd_entry enter_baco_tbl[] =
{
	{CMD_WAITFOR, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__SOC_DOMAIN_IDLE_MASK, THM_BACO_CNTL__SOC_DOMAIN_IDLE__SHIFT, 0xffffffff, 0x80000000},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(NBIF, 0, mmBACO_CNTL), BACO_CNTL__BACO_EN_MASK, BACO_CNTL__BACO_EN__SHIFT, 0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(NBIF, 0, mmBACO_CNTL), BACO_CNTL__BACO_BIF_LCLK_SWITCH_MASK, BACO_CNTL__BACO_BIF_LCLK_SWITCH__SHIFT, 0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(NBIF, 0, mmBACO_CNTL), BACO_CNTL__BACO_DUMMY_EN_MASK, BACO_CNTL__BACO_DUMMY_EN__SHIFT, 0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_SOC_VDCI_RESET_MASK, THM_BACO_CNTL__BACO_SOC_VDCI_RESET__SHIFT, 0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_SMNCLK_MUX_MASK, THM_BACO_CNTL__BACO_SMNCLK_MUX__SHIFT,0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_ISO_EN_MASK, THM_BACO_CNTL__BACO_ISO_EN__SHIFT, 0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_AEB_ISO_EN_MASK, THM_BACO_CNTL__BACO_AEB_ISO_EN__SHIFT,0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_ANA_ISO_EN_MASK, THM_BACO_CNTL__BACO_ANA_ISO_EN__SHIFT, 0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_SOC_REFCLK_OFF_MASK,     THM_BACO_CNTL__BACO_SOC_REFCLK_OFF__SHIFT, 0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(NBIF, 0, mmBACO_CNTL), BACO_CNTL__BACO_POWER_OFF_MASK, BACO_CNTL__BACO_POWER_OFF__SHIFT, 0, 1},
	{CMD_DELAY_MS, 0, 0, 0, 0, 0, 0, 5, 0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_RESET_EN_MASK, THM_BACO_CNTL__BACO_RESET_EN__SHIFT, 0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_PWROKRAW_CNTL_MASK, THM_BACO_CNTL__BACO_PWROKRAW_CNTL__SHIFT, 0, 0},
	{CMD_WAITFOR, SOC15_REG_ENTRY(NBIF, 0, mmBACO_CNTL), BACO_CNTL__BACO_MODE_MASK, BACO_CNTL__BACO_MODE__SHIFT, 0xffffffff, 0x100}
};

static const struct soc15_baco_cmd_entry exit_baco_tbl[] =
{
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(NBIF, 0, mmBACO_CNTL), BACO_CNTL__BACO_POWER_OFF_MASK, BACO_CNTL__BACO_POWER_OFF__SHIFT, 0, 0},
	{CMD_DELAY_MS, 0, 0, 0, 0, 0, 0, 10,0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_SOC_REFCLK_OFF_MASK, THM_BACO_CNTL__BACO_SOC_REFCLK_OFF__SHIFT, 0,0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_ANA_ISO_EN_MASK, THM_BACO_CNTL__BACO_ANA_ISO_EN__SHIFT, 0, 0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_AEB_ISO_EN_MASK, THM_BACO_CNTL__BACO_AEB_ISO_EN__SHIFT,0, 0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_ISO_EN_MASK, THM_BACO_CNTL__BACO_ISO_EN__SHIFT, 0, 0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_PWROKRAW_CNTL_MASK, THM_BACO_CNTL__BACO_PWROKRAW_CNTL__SHIFT, 0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_SMNCLK_MUX_MASK, THM_BACO_CNTL__BACO_SMNCLK_MUX__SHIFT, 0, 0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_SOC_VDCI_RESET_MASK, THM_BACO_CNTL__BACO_SOC_VDCI_RESET__SHIFT, 0, 0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_EXIT_MASK, THM_BACO_CNTL__BACO_EXIT__SHIFT, 0, 1},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_RESET_EN_MASK, THM_BACO_CNTL__BACO_RESET_EN__SHIFT, 0, 0},
	{CMD_WAITFOR, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_EXIT_MASK, 0, 0xffffffff, 0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(THM, 0, mmTHM_BACO_CNTL), THM_BACO_CNTL__BACO_SB_AXI_FENCE_MASK, THM_BACO_CNTL__BACO_SB_AXI_FENCE__SHIFT, 0, 0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(NBIF, 0, mmBACO_CNTL), BACO_CNTL__BACO_DUMMY_EN_MASK, BACO_CNTL__BACO_DUMMY_EN__SHIFT,  0, 0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(NBIF, 0, mmBACO_CNTL), BACO_CNTL__BACO_BIF_LCLK_SWITCH_MASK ,BACO_CNTL__BACO_BIF_LCLK_SWITCH__SHIFT, 0, 0},
	{CMD_READMODIFYWRITE, SOC15_REG_ENTRY(NBIF, 0, mmBACO_CNTL), BACO_CNTL__BACO_EN_MASK , BACO_CNTL__BACO_EN__SHIFT, 0,0},
	{CMD_WAITFOR, SOC15_REG_ENTRY(NBIF, 0, mmBACO_CNTL), BACO_CNTL__BACO_MODE_MASK, 0, 0xffffffff, 0}
 };

static const struct soc15_baco_cmd_entry clean_baco_tbl[] =
{
	{CMD_WRITE, SOC15_REG_ENTRY(NBIF, 0, mmBIOS_SCRATCH_6), 0, 0, 0, 0},
	{CMD_WRITE, SOC15_REG_ENTRY(NBIF, 0, mmBIOS_SCRATCH_7), 0, 0, 0, 0},
};

int vega10_baco_get_capability(struct pp_hwmgr *hwmgr, bool *cap)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)(hwmgr->adev);
	uint32_t reg, data;

	*cap = false;
	if (!phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_BACO))
		return 0;

	WREG32(0x12074, 0xFFF0003B);
	data = RREG32(0x12075);

	if (data == 0x1) {
		reg = RREG32_SOC15(NBIF, 0, mmRCC_BIF_STRAP0);

		if (reg & RCC_BIF_STRAP0__STRAP_PX_CAPABLE_MASK)
			*cap = true;
	}

	return 0;
}

int vega10_baco_get_state(struct pp_hwmgr *hwmgr, enum BACO_STATE *state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)(hwmgr->adev);
	uint32_t reg;

	reg = RREG32_SOC15(NBIF, 0, mmBACO_CNTL);

	if (reg & BACO_CNTL__BACO_MODE_MASK)
		/* gfx has already entered BACO state */
		*state = BACO_STATE_IN;
	else
		*state = BACO_STATE_OUT;
	return 0;
}

int vega10_baco_set_state(struct pp_hwmgr *hwmgr, enum BACO_STATE state)
{
	enum BACO_STATE cur_state;

	vega10_baco_get_state(hwmgr, &cur_state);

	if (cur_state == state)
		/* aisc already in the target state */
		return 0;

	if (state == BACO_STATE_IN) {
		if (soc15_baco_program_registers(hwmgr, pre_baco_tbl,
					     ARRAY_SIZE(pre_baco_tbl))) {
			if (smum_send_msg_to_smc(hwmgr, PPSMC_MSG_EnterBaco))
				return -EINVAL;

			if (soc15_baco_program_registers(hwmgr, enter_baco_tbl,
						   ARRAY_SIZE(enter_baco_tbl)))
				return 0;
		}
	} else if (state == BACO_STATE_OUT) {
		/* HW requires at least 20ms between regulator off and on */
		msleep(20);
		/* Execute Hardware BACO exit sequence */
		if (soc15_baco_program_registers(hwmgr, exit_baco_tbl,
					     ARRAY_SIZE(exit_baco_tbl))) {
			if (soc15_baco_program_registers(hwmgr, clean_baco_tbl,
						     ARRAY_SIZE(clean_baco_tbl)))
				return 0;
		}
	}

	return -EINVAL;
}
