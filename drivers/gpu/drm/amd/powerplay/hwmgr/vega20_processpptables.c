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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fb.h>

#include "smu11_driver_if.h"
#include "vega20_processpptables.h"
#include "ppatomfwctrl.h"
#include "atomfirmware.h"
#include "pp_debug.h"
#include "cgs_common.h"
#include "vega20_pptable.h"

static void set_hw_cap(struct pp_hwmgr *hwmgr, bool enable,
		enum phm_platform_caps cap)
{
	if (enable)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps, cap);
	else
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps, cap);
}

static const void *get_powerplay_table(struct pp_hwmgr *hwmgr)
{
	int index = GetIndexIntoMasterDataTable(powerplayinfo);

	u16 size;
	u8 frev, crev;
	const void *table_address = hwmgr->soft_pp_table;

	if (!table_address) {
		table_address = (ATOM_Vega20_POWERPLAYTABLE *)
				smu_atom_get_data_table(hwmgr->adev, index,
						&size, &frev, &crev);

		hwmgr->soft_pp_table = table_address;
		hwmgr->soft_pp_table_size = size;
	}

	return table_address;
}

#if 0
static void dump_pptable(PPTable_t *pptable)
{
	int i;

	pr_info("Version = 0x%08x\n", pptable->Version);

	pr_info("FeaturesToRun[0] = 0x%08x\n", pptable->FeaturesToRun[0]);
	pr_info("FeaturesToRun[1] = 0x%08x\n", pptable->FeaturesToRun[1]);

	pr_info("SocketPowerLimitAc0 = %d\n", pptable->SocketPowerLimitAc0);
	pr_info("SocketPowerLimitAc0Tau = %d\n", pptable->SocketPowerLimitAc0Tau);
	pr_info("SocketPowerLimitAc1 = %d\n", pptable->SocketPowerLimitAc1);
	pr_info("SocketPowerLimitAc1Tau = %d\n", pptable->SocketPowerLimitAc1Tau);
	pr_info("SocketPowerLimitAc2 = %d\n", pptable->SocketPowerLimitAc2);
	pr_info("SocketPowerLimitAc2Tau = %d\n", pptable->SocketPowerLimitAc2Tau);
	pr_info("SocketPowerLimitAc3 = %d\n", pptable->SocketPowerLimitAc3);
	pr_info("SocketPowerLimitAc3Tau = %d\n", pptable->SocketPowerLimitAc3Tau);
	pr_info("SocketPowerLimitDc = %d\n", pptable->SocketPowerLimitDc);
	pr_info("SocketPowerLimitDcTau = %d\n", pptable->SocketPowerLimitDcTau);
	pr_info("TdcLimitSoc = %d\n", pptable->TdcLimitSoc);
	pr_info("TdcLimitSocTau = %d\n", pptable->TdcLimitSocTau);
	pr_info("TdcLimitGfx = %d\n", pptable->TdcLimitGfx);
	pr_info("TdcLimitGfxTau = %d\n", pptable->TdcLimitGfxTau);

	pr_info("TedgeLimit = %d\n", pptable->TedgeLimit);
	pr_info("ThotspotLimit = %d\n", pptable->ThotspotLimit);
	pr_info("ThbmLimit = %d\n", pptable->ThbmLimit);
	pr_info("Tvr_gfxLimit = %d\n", pptable->Tvr_gfxLimit);
	pr_info("Tvr_memLimit = %d\n", pptable->Tvr_memLimit);
	pr_info("Tliquid1Limit = %d\n", pptable->Tliquid1Limit);
	pr_info("Tliquid2Limit = %d\n", pptable->Tliquid2Limit);
	pr_info("TplxLimit = %d\n", pptable->TplxLimit);
	pr_info("FitLimit = %d\n", pptable->FitLimit);

	pr_info("PpmPowerLimit = %d\n", pptable->PpmPowerLimit);
	pr_info("PpmTemperatureThreshold = %d\n", pptable->PpmTemperatureThreshold);

	pr_info("MemoryOnPackage = 0x%02x\n", pptable->MemoryOnPackage);
	pr_info("padding8_limits = 0x%02x\n", pptable->padding8_limits);
	pr_info("Tvr_SocLimit = %d\n", pptable->Tvr_SocLimit);

	pr_info("UlvVoltageOffsetSoc = %d\n", pptable->UlvVoltageOffsetSoc);
	pr_info("UlvVoltageOffsetGfx = %d\n", pptable->UlvVoltageOffsetGfx);

	pr_info("UlvSmnclkDid = %d\n", pptable->UlvSmnclkDid);
	pr_info("UlvMp1clkDid = %d\n", pptable->UlvMp1clkDid);
	pr_info("UlvGfxclkBypass = %d\n", pptable->UlvGfxclkBypass);
	pr_info("Padding234 = 0x%02x\n", pptable->Padding234);

	pr_info("MinVoltageGfx = %d\n", pptable->MinVoltageGfx);
	pr_info("MinVoltageSoc = %d\n", pptable->MinVoltageSoc);
	pr_info("MaxVoltageGfx = %d\n", pptable->MaxVoltageGfx);
	pr_info("MaxVoltageSoc = %d\n", pptable->MaxVoltageSoc);

	pr_info("LoadLineResistanceGfx = %d\n", pptable->LoadLineResistanceGfx);
	pr_info("LoadLineResistanceSoc = %d\n", pptable->LoadLineResistanceSoc);

	pr_info("[PPCLK_GFXCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n",
			pptable->DpmDescriptor[PPCLK_GFXCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_GFXCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_GFXCLK].padding,
			pptable->DpmDescriptor[PPCLK_GFXCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_GFXCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.c);

	pr_info("[PPCLK_VCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n",
			pptable->DpmDescriptor[PPCLK_VCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_VCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_VCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_VCLK].padding,
			pptable->DpmDescriptor[PPCLK_VCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_VCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_VCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_VCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_VCLK].SsCurve.c);

	pr_info("[PPCLK_DCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n",
			pptable->DpmDescriptor[PPCLK_DCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_DCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_DCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_DCLK].padding,
			pptable->DpmDescriptor[PPCLK_DCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_DCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_DCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_DCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_DCLK].SsCurve.c);

	pr_info("[PPCLK_ECLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n",
			pptable->DpmDescriptor[PPCLK_ECLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_ECLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_ECLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_ECLK].padding,
			pptable->DpmDescriptor[PPCLK_ECLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_ECLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_ECLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_ECLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_ECLK].SsCurve.c);

	pr_info("[PPCLK_SOCCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n",
			pptable->DpmDescriptor[PPCLK_SOCCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_SOCCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_SOCCLK].padding,
			pptable->DpmDescriptor[PPCLK_SOCCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_SOCCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.c);

	pr_info("[PPCLK_UCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n",
			pptable->DpmDescriptor[PPCLK_UCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_UCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_UCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_UCLK].padding,
			pptable->DpmDescriptor[PPCLK_UCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_UCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.c);

	pr_info("[PPCLK_DCEFCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n",
			pptable->DpmDescriptor[PPCLK_DCEFCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_DCEFCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_DCEFCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_DCEFCLK].padding,
			pptable->DpmDescriptor[PPCLK_DCEFCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_DCEFCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_DCEFCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_DCEFCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_DCEFCLK].SsCurve.c);

	pr_info("[PPCLK_DISPCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n",
			pptable->DpmDescriptor[PPCLK_DISPCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_DISPCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_DISPCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_DISPCLK].padding,
			pptable->DpmDescriptor[PPCLK_DISPCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_DISPCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_DISPCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_DISPCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_DISPCLK].SsCurve.c);

	pr_info("[PPCLK_PIXCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n",
			pptable->DpmDescriptor[PPCLK_PIXCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_PIXCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_PIXCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_PIXCLK].padding,
			pptable->DpmDescriptor[PPCLK_PIXCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_PIXCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_PIXCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_PIXCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_PIXCLK].SsCurve.c);

	pr_info("[PPCLK_PHYCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n",
			pptable->DpmDescriptor[PPCLK_PHYCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_PHYCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_PHYCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_PHYCLK].padding,
			pptable->DpmDescriptor[PPCLK_PHYCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_PHYCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_PHYCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_PHYCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_PHYCLK].SsCurve.c);

	pr_info("[PPCLK_FCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n",
			pptable->DpmDescriptor[PPCLK_FCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_FCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_FCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_FCLK].padding,
			pptable->DpmDescriptor[PPCLK_FCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_FCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.c);


	pr_info("FreqTableGfx\n");
	for (i = 0; i < NUM_GFXCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = %d\n", i, pptable->FreqTableGfx[i]);

	pr_info("FreqTableVclk\n");
	for (i = 0; i < NUM_VCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = %d\n", i, pptable->FreqTableVclk[i]);

	pr_info("FreqTableDclk\n");
	for (i = 0; i < NUM_DCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = %d\n", i, pptable->FreqTableDclk[i]);

	pr_info("FreqTableEclk\n");
	for (i = 0; i < NUM_ECLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = %d\n", i, pptable->FreqTableEclk[i]);

	pr_info("FreqTableSocclk\n");
	for (i = 0; i < NUM_SOCCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = %d\n", i, pptable->FreqTableSocclk[i]);

	pr_info("FreqTableUclk\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = %d\n", i, pptable->FreqTableUclk[i]);

	pr_info("FreqTableFclk\n");
	for (i = 0; i < NUM_FCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = %d\n", i, pptable->FreqTableFclk[i]);

	pr_info("FreqTableDcefclk\n");
	for (i = 0; i < NUM_DCEFCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = %d\n", i, pptable->FreqTableDcefclk[i]);

	pr_info("FreqTableDispclk\n");
	for (i = 0; i < NUM_DISPCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = %d\n", i, pptable->FreqTableDispclk[i]);

	pr_info("FreqTablePixclk\n");
	for (i = 0; i < NUM_PIXCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = %d\n", i, pptable->FreqTablePixclk[i]);

	pr_info("FreqTablePhyclk\n");
	for (i = 0; i < NUM_PHYCLK_DPM_LEVELS; i++)
		pr_info("  .[%02d] = %d\n", i, pptable->FreqTablePhyclk[i]);

	pr_info("DcModeMaxFreq[PPCLK_GFXCLK] = %d\n", pptable->DcModeMaxFreq[PPCLK_GFXCLK]);
	pr_info("DcModeMaxFreq[PPCLK_VCLK] = %d\n", pptable->DcModeMaxFreq[PPCLK_VCLK]);
	pr_info("DcModeMaxFreq[PPCLK_DCLK] = %d\n", pptable->DcModeMaxFreq[PPCLK_DCLK]);
	pr_info("DcModeMaxFreq[PPCLK_ECLK] = %d\n", pptable->DcModeMaxFreq[PPCLK_ECLK]);
	pr_info("DcModeMaxFreq[PPCLK_SOCCLK] = %d\n", pptable->DcModeMaxFreq[PPCLK_SOCCLK]);
	pr_info("DcModeMaxFreq[PPCLK_UCLK] = %d\n", pptable->DcModeMaxFreq[PPCLK_UCLK]);
	pr_info("DcModeMaxFreq[PPCLK_DCEFCLK] = %d\n", pptable->DcModeMaxFreq[PPCLK_DCEFCLK]);
	pr_info("DcModeMaxFreq[PPCLK_DISPCLK] = %d\n", pptable->DcModeMaxFreq[PPCLK_DISPCLK]);
	pr_info("DcModeMaxFreq[PPCLK_PIXCLK] = %d\n", pptable->DcModeMaxFreq[PPCLK_PIXCLK]);
	pr_info("DcModeMaxFreq[PPCLK_PHYCLK] = %d\n", pptable->DcModeMaxFreq[PPCLK_PHYCLK]);
	pr_info("DcModeMaxFreq[PPCLK_FCLK] = %d\n", pptable->DcModeMaxFreq[PPCLK_FCLK]);
	pr_info("Padding8_Clks = %d\n", pptable->Padding8_Clks);

	pr_info("Mp0clkFreq\n");
	for (i = 0; i < NUM_MP0CLK_DPM_LEVELS; i++)
		pr_info("  .[%d] = %d\n", i, pptable->Mp0clkFreq[i]);

	pr_info("Mp0DpmVoltage\n");
	for (i = 0; i < NUM_MP0CLK_DPM_LEVELS; i++)
		pr_info("  .[%d] = %d\n", i, pptable->Mp0DpmVoltage[i]);

	pr_info("GfxclkFidle = 0x%x\n", pptable->GfxclkFidle);
	pr_info("GfxclkSlewRate = 0x%x\n", pptable->GfxclkSlewRate);
	pr_info("CksEnableFreq = 0x%x\n", pptable->CksEnableFreq);
	pr_info("Padding789 = 0x%x\n", pptable->Padding789);
	pr_info("CksVoltageOffset[a = 0x%08x b = 0x%08x c = 0x%08x]\n",
			pptable->CksVoltageOffset.a,
			pptable->CksVoltageOffset.b,
			pptable->CksVoltageOffset.c);
	pr_info("Padding567[0] = 0x%x\n", pptable->Padding567[0]);
	pr_info("Padding567[1] = 0x%x\n", pptable->Padding567[1]);
	pr_info("Padding567[2] = 0x%x\n", pptable->Padding567[2]);
	pr_info("Padding567[3] = 0x%x\n", pptable->Padding567[3]);
	pr_info("GfxclkDsMaxFreq = %d\n", pptable->GfxclkDsMaxFreq);
	pr_info("GfxclkSource = 0x%x\n", pptable->GfxclkSource);
	pr_info("Padding456 = 0x%x\n", pptable->Padding456);

	pr_info("LowestUclkReservedForUlv = %d\n", pptable->LowestUclkReservedForUlv);
	pr_info("Padding8_Uclk[0] = 0x%x\n", pptable->Padding8_Uclk[0]);
	pr_info("Padding8_Uclk[1] = 0x%x\n", pptable->Padding8_Uclk[1]);
	pr_info("Padding8_Uclk[2] = 0x%x\n", pptable->Padding8_Uclk[2]);

	pr_info("PcieGenSpeed\n");
	for (i = 0; i < NUM_LINK_LEVELS; i++)
		pr_info("  .[%d] = %d\n", i, pptable->PcieGenSpeed[i]);

	pr_info("PcieLaneCount\n");
	for (i = 0; i < NUM_LINK_LEVELS; i++)
		pr_info("  .[%d] = %d\n", i, pptable->PcieLaneCount[i]);

	pr_info("LclkFreq\n");
	for (i = 0; i < NUM_LINK_LEVELS; i++)
		pr_info("  .[%d] = %d\n", i, pptable->LclkFreq[i]);

	pr_info("EnableTdpm = %d\n", pptable->EnableTdpm);
	pr_info("TdpmHighHystTemperature = %d\n", pptable->TdpmHighHystTemperature);
	pr_info("TdpmLowHystTemperature = %d\n", pptable->TdpmLowHystTemperature);
	pr_info("GfxclkFreqHighTempLimit = %d\n", pptable->GfxclkFreqHighTempLimit);

	pr_info("FanStopTemp = %d\n", pptable->FanStopTemp);
	pr_info("FanStartTemp = %d\n", pptable->FanStartTemp);

	pr_info("FanGainEdge = %d\n", pptable->FanGainEdge);
	pr_info("FanGainHotspot = %d\n", pptable->FanGainHotspot);
	pr_info("FanGainLiquid = %d\n", pptable->FanGainLiquid);
	pr_info("FanGainVrGfx = %d\n", pptable->FanGainVrGfx);
	pr_info("FanGainVrSoc = %d\n", pptable->FanGainVrSoc);
	pr_info("FanGainPlx = %d\n", pptable->FanGainPlx);
	pr_info("FanGainHbm = %d\n", pptable->FanGainHbm);
	pr_info("FanPwmMin = %d\n", pptable->FanPwmMin);
	pr_info("FanAcousticLimitRpm = %d\n", pptable->FanAcousticLimitRpm);
	pr_info("FanThrottlingRpm = %d\n", pptable->FanThrottlingRpm);
	pr_info("FanMaximumRpm = %d\n", pptable->FanMaximumRpm);
	pr_info("FanTargetTemperature = %d\n", pptable->FanTargetTemperature);
	pr_info("FanTargetGfxclk = %d\n", pptable->FanTargetGfxclk);
	pr_info("FanZeroRpmEnable = %d\n", pptable->FanZeroRpmEnable);
	pr_info("FanTachEdgePerRev = %d\n", pptable->FanTachEdgePerRev);

	pr_info("FuzzyFan_ErrorSetDelta = %d\n", pptable->FuzzyFan_ErrorSetDelta);
	pr_info("FuzzyFan_ErrorRateSetDelta = %d\n", pptable->FuzzyFan_ErrorRateSetDelta);
	pr_info("FuzzyFan_PwmSetDelta = %d\n", pptable->FuzzyFan_PwmSetDelta);
	pr_info("FuzzyFan_Reserved = %d\n", pptable->FuzzyFan_Reserved);

	pr_info("OverrideAvfsGb[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->OverrideAvfsGb[AVFS_VOLTAGE_GFX]);
	pr_info("OverrideAvfsGb[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->OverrideAvfsGb[AVFS_VOLTAGE_SOC]);
	pr_info("Padding8_Avfs[0] = %d\n", pptable->Padding8_Avfs[0]);
	pr_info("Padding8_Avfs[1] = %d\n", pptable->Padding8_Avfs[1]);

	pr_info("qAvfsGb[AVFS_VOLTAGE_GFX]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qAvfsGb[AVFS_VOLTAGE_GFX].a,
			pptable->qAvfsGb[AVFS_VOLTAGE_GFX].b,
			pptable->qAvfsGb[AVFS_VOLTAGE_GFX].c);
	pr_info("qAvfsGb[AVFS_VOLTAGE_SOC]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qAvfsGb[AVFS_VOLTAGE_SOC].a,
			pptable->qAvfsGb[AVFS_VOLTAGE_SOC].b,
			pptable->qAvfsGb[AVFS_VOLTAGE_SOC].c);
	pr_info("dBtcGbGfxCksOn{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbGfxCksOn.a,
			pptable->dBtcGbGfxCksOn.b,
			pptable->dBtcGbGfxCksOn.c);
	pr_info("dBtcGbGfxCksOff{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbGfxCksOff.a,
			pptable->dBtcGbGfxCksOff.b,
			pptable->dBtcGbGfxCksOff.c);
	pr_info("dBtcGbGfxAfll{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbGfxAfll.a,
			pptable->dBtcGbGfxAfll.b,
			pptable->dBtcGbGfxAfll.c);
	pr_info("dBtcGbSoc{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbSoc.a,
			pptable->dBtcGbSoc.b,
			pptable->dBtcGbSoc.c);
	pr_info("qAgingGb[AVFS_VOLTAGE_GFX]{m = 0x%x b = 0x%x}\n",
			pptable->qAgingGb[AVFS_VOLTAGE_GFX].m,
			pptable->qAgingGb[AVFS_VOLTAGE_GFX].b);
	pr_info("qAgingGb[AVFS_VOLTAGE_SOC]{m = 0x%x b = 0x%x}\n",
			pptable->qAgingGb[AVFS_VOLTAGE_SOC].m,
			pptable->qAgingGb[AVFS_VOLTAGE_SOC].b);

	pr_info("qStaticVoltageOffset[AVFS_VOLTAGE_GFX]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].a,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].b,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].c);
	pr_info("qStaticVoltageOffset[AVFS_VOLTAGE_SOC]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].a,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].b,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].c);

	pr_info("DcTol[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcTol[AVFS_VOLTAGE_GFX]);
	pr_info("DcTol[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcTol[AVFS_VOLTAGE_SOC]);

	pr_info("DcBtcEnabled[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcEnabled[AVFS_VOLTAGE_GFX]);
	pr_info("DcBtcEnabled[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcEnabled[AVFS_VOLTAGE_SOC]);
	pr_info("Padding8_GfxBtc[0] = 0x%x\n", pptable->Padding8_GfxBtc[0]);
	pr_info("Padding8_GfxBtc[1] = 0x%x\n", pptable->Padding8_GfxBtc[1]);

	pr_info("DcBtcMin[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcMin[AVFS_VOLTAGE_GFX]);
	pr_info("DcBtcMin[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcMin[AVFS_VOLTAGE_SOC]);
	pr_info("DcBtcMax[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcMax[AVFS_VOLTAGE_GFX]);
	pr_info("DcBtcMax[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcMax[AVFS_VOLTAGE_SOC]);

	pr_info("XgmiLinkSpeed\n");
	for (i = 0; i < NUM_XGMI_LEVELS; i++)
		pr_info("  .[%d] = %d\n", i, pptable->XgmiLinkSpeed[i]);
	pr_info("XgmiLinkWidth\n");
	for (i = 0; i < NUM_XGMI_LEVELS; i++)
		pr_info("  .[%d] = %d\n", i, pptable->XgmiLinkWidth[i]);
	pr_info("XgmiFclkFreq\n");
	for (i = 0; i < NUM_XGMI_LEVELS; i++)
		pr_info("  .[%d] = %d\n", i, pptable->XgmiFclkFreq[i]);
	pr_info("XgmiUclkFreq\n");
	for (i = 0; i < NUM_XGMI_LEVELS; i++)
		pr_info("  .[%d] = %d\n", i, pptable->XgmiUclkFreq[i]);
	pr_info("XgmiSocclkFreq\n");
	for (i = 0; i < NUM_XGMI_LEVELS; i++)
		pr_info("  .[%d] = %d\n", i, pptable->XgmiSocclkFreq[i]);
	pr_info("XgmiSocVoltage\n");
	for (i = 0; i < NUM_XGMI_LEVELS; i++)
		pr_info("  .[%d] = %d\n", i, pptable->XgmiSocVoltage[i]);

	pr_info("DebugOverrides = 0x%x\n", pptable->DebugOverrides);
	pr_info("ReservedEquation0{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation0.a,
			pptable->ReservedEquation0.b,
			pptable->ReservedEquation0.c);
	pr_info("ReservedEquation1{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation1.a,
			pptable->ReservedEquation1.b,
			pptable->ReservedEquation1.c);
	pr_info("ReservedEquation2{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation2.a,
			pptable->ReservedEquation2.b,
			pptable->ReservedEquation2.c);
	pr_info("ReservedEquation3{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation3.a,
			pptable->ReservedEquation3.b,
			pptable->ReservedEquation3.c);

	pr_info("MinVoltageUlvGfx = %d\n", pptable->MinVoltageUlvGfx);
	pr_info("MinVoltageUlvSoc = %d\n", pptable->MinVoltageUlvSoc);

	pr_info("MGpuFanBoostLimitRpm = %d\n", pptable->MGpuFanBoostLimitRpm);
	pr_info("padding16_Fan = %d\n", pptable->padding16_Fan);

	pr_info("FanGainVrMem0 = %d\n", pptable->FanGainVrMem0);
	pr_info("FanGainVrMem0 = %d\n", pptable->FanGainVrMem0);

	pr_info("DcBtcGb[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcGb[AVFS_VOLTAGE_GFX]);
	pr_info("DcBtcGb[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcGb[AVFS_VOLTAGE_SOC]);

	for (i = 0; i < 11; i++)
		pr_info("Reserved[%d] = 0x%x\n", i, pptable->Reserved[i]);

	for (i = 0; i < 3; i++)
		pr_info("Padding32[%d] = 0x%x\n", i, pptable->Padding32[i]);

	pr_info("MaxVoltageStepGfx = 0x%x\n", pptable->MaxVoltageStepGfx);
	pr_info("MaxVoltageStepSoc = 0x%x\n", pptable->MaxVoltageStepSoc);

	pr_info("VddGfxVrMapping = 0x%x\n", pptable->VddGfxVrMapping);
	pr_info("VddSocVrMapping = 0x%x\n", pptable->VddSocVrMapping);
	pr_info("VddMem0VrMapping = 0x%x\n", pptable->VddMem0VrMapping);
	pr_info("VddMem1VrMapping = 0x%x\n", pptable->VddMem1VrMapping);

	pr_info("GfxUlvPhaseSheddingMask = 0x%x\n", pptable->GfxUlvPhaseSheddingMask);
	pr_info("SocUlvPhaseSheddingMask = 0x%x\n", pptable->SocUlvPhaseSheddingMask);
	pr_info("ExternalSensorPresent = 0x%x\n", pptable->ExternalSensorPresent);
	pr_info("Padding8_V = 0x%x\n", pptable->Padding8_V);

	pr_info("GfxMaxCurrent = 0x%x\n", pptable->GfxMaxCurrent);
	pr_info("GfxOffset = 0x%x\n", pptable->GfxOffset);
	pr_info("Padding_TelemetryGfx = 0x%x\n", pptable->Padding_TelemetryGfx);

	pr_info("SocMaxCurrent = 0x%x\n", pptable->SocMaxCurrent);
	pr_info("SocOffset = 0x%x\n", pptable->SocOffset);
	pr_info("Padding_TelemetrySoc = 0x%x\n", pptable->Padding_TelemetrySoc);

	pr_info("Mem0MaxCurrent = 0x%x\n", pptable->Mem0MaxCurrent);
	pr_info("Mem0Offset = 0x%x\n", pptable->Mem0Offset);
	pr_info("Padding_TelemetryMem0 = 0x%x\n", pptable->Padding_TelemetryMem0);

	pr_info("Mem1MaxCurrent = 0x%x\n", pptable->Mem1MaxCurrent);
	pr_info("Mem1Offset = 0x%x\n", pptable->Mem1Offset);
	pr_info("Padding_TelemetryMem1 = 0x%x\n", pptable->Padding_TelemetryMem1);

	pr_info("AcDcGpio = %d\n", pptable->AcDcGpio);
	pr_info("AcDcPolarity = %d\n", pptable->AcDcPolarity);
	pr_info("VR0HotGpio = %d\n", pptable->VR0HotGpio);
	pr_info("VR0HotPolarity = %d\n", pptable->VR0HotPolarity);

	pr_info("VR1HotGpio = %d\n", pptable->VR1HotGpio);
	pr_info("VR1HotPolarity = %d\n", pptable->VR1HotPolarity);
	pr_info("Padding1 = 0x%x\n", pptable->Padding1);
	pr_info("Padding2 = 0x%x\n", pptable->Padding2);

	pr_info("LedPin0 = %d\n", pptable->LedPin0);
	pr_info("LedPin1 = %d\n", pptable->LedPin1);
	pr_info("LedPin2 = %d\n", pptable->LedPin2);
	pr_info("padding8_4 = 0x%x\n", pptable->padding8_4);

	pr_info("PllGfxclkSpreadEnabled = %d\n", pptable->PllGfxclkSpreadEnabled);
	pr_info("PllGfxclkSpreadPercent = %d\n", pptable->PllGfxclkSpreadPercent);
	pr_info("PllGfxclkSpreadFreq = %d\n", pptable->PllGfxclkSpreadFreq);

	pr_info("UclkSpreadEnabled = %d\n", pptable->UclkSpreadEnabled);
	pr_info("UclkSpreadPercent = %d\n", pptable->UclkSpreadPercent);
	pr_info("UclkSpreadFreq = %d\n", pptable->UclkSpreadFreq);

	pr_info("FclkSpreadEnabled = %d\n", pptable->FclkSpreadEnabled);
	pr_info("FclkSpreadPercent = %d\n", pptable->FclkSpreadPercent);
	pr_info("FclkSpreadFreq = %d\n", pptable->FclkSpreadFreq);

	pr_info("FllGfxclkSpreadEnabled = %d\n", pptable->FllGfxclkSpreadEnabled);
	pr_info("FllGfxclkSpreadPercent = %d\n", pptable->FllGfxclkSpreadPercent);
	pr_info("FllGfxclkSpreadFreq = %d\n", pptable->FllGfxclkSpreadFreq);

	for (i = 0; i < I2C_CONTROLLER_NAME_COUNT; i++) {
		pr_info("I2cControllers[%d]:\n", i);
		pr_info("                   .Enabled = %d\n",
				pptable->I2cControllers[i].Enabled);
		pr_info("                   .SlaveAddress = 0x%x\n",
				pptable->I2cControllers[i].SlaveAddress);
		pr_info("                   .ControllerPort = %d\n",
				pptable->I2cControllers[i].ControllerPort);
		pr_info("                   .ControllerName = %d\n",
				pptable->I2cControllers[i].ControllerName);
		pr_info("                   .ThermalThrottler = %d\n",
				pptable->I2cControllers[i].ThermalThrottler);
		pr_info("                   .I2cProtocol = %d\n",
				pptable->I2cControllers[i].I2cProtocol);
		pr_info("                   .I2cSpeed = %d\n",
				pptable->I2cControllers[i].I2cSpeed);
	}

	for (i = 0; i < 10; i++)
		pr_info("BoardReserved[%d] = 0x%x\n", i, pptable->BoardReserved[i]);

	for (i = 0; i < 8; i++)
		pr_info("MmHubPadding[%d] = 0x%x\n", i, pptable->MmHubPadding[i]);
}
#endif

static int check_powerplay_tables(
		struct pp_hwmgr *hwmgr,
		const ATOM_Vega20_POWERPLAYTABLE *powerplay_table)
{
	PP_ASSERT_WITH_CODE((powerplay_table->sHeader.format_revision >=
		ATOM_VEGA20_TABLE_REVISION_VEGA20),
		"Unsupported PPTable format!", return -1);
	PP_ASSERT_WITH_CODE(powerplay_table->sHeader.structuresize > 0,
		"Invalid PowerPlay Table!", return -1);
	PP_ASSERT_WITH_CODE(powerplay_table->smcPPTable.Version == PPTABLE_V20_SMU_VERSION,
		"Unmatch PPTable version, vbios update may be needed!", return -1);

	//dump_pptable(&powerplay_table->smcPPTable);

	return 0;
}

static int set_platform_caps(struct pp_hwmgr *hwmgr, uint32_t powerplay_caps)
{
	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_VEGA20_PP_PLATFORM_CAP_POWERPLAY),
		PHM_PlatformCaps_PowerPlaySupport);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_VEGA20_PP_PLATFORM_CAP_SBIOSPOWERSOURCE),
		PHM_PlatformCaps_BiosPowerSourceControl);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_VEGA20_PP_PLATFORM_CAP_BACO),
		PHM_PlatformCaps_BACO);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_VEGA20_PP_PLATFORM_CAP_BAMACO),
		 PHM_PlatformCaps_BAMACO);

	return 0;
}

static int copy_overdrive_feature_capabilities_array(
		struct pp_hwmgr *hwmgr,
		uint8_t **pptable_info_array,
		const uint8_t *pptable_array,
		uint8_t od_feature_count)
{
	uint32_t array_size, i;
	uint8_t *table;
	bool od_supported = false;

	array_size = sizeof(uint8_t) * od_feature_count;
	table = kzalloc(array_size, GFP_KERNEL);
	if (NULL == table)
		return -ENOMEM;

	for (i = 0; i < od_feature_count; i++) {
		table[i] = le32_to_cpu(pptable_array[i]);
		if (table[i])
			od_supported = true;
	}

	*pptable_info_array = table;

	if (od_supported)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ACOverdriveSupport);

	return 0;
}

static int append_vbios_pptable(struct pp_hwmgr *hwmgr, PPTable_t *ppsmc_pptable)
{
	struct atom_smc_dpm_info_v4_4 *smc_dpm_table;
	int index = GetIndexIntoMasterDataTable(smc_dpm_info);
	int i;

	PP_ASSERT_WITH_CODE(
		smc_dpm_table = smu_atom_get_data_table(hwmgr->adev, index, NULL, NULL, NULL),
		"[appendVbiosPPTable] Failed to retrieve Smc Dpm Table from VBIOS!",
		return -1);

	memset(ppsmc_pptable->Padding32,
			0,
			sizeof(struct atom_smc_dpm_info_v4_4) -
			sizeof(struct atom_common_table_header));
	ppsmc_pptable->MaxVoltageStepGfx = smc_dpm_table->maxvoltagestepgfx;
	ppsmc_pptable->MaxVoltageStepSoc = smc_dpm_table->maxvoltagestepsoc;

	ppsmc_pptable->VddGfxVrMapping = smc_dpm_table->vddgfxvrmapping;
	ppsmc_pptable->VddSocVrMapping = smc_dpm_table->vddsocvrmapping;
	ppsmc_pptable->VddMem0VrMapping = smc_dpm_table->vddmem0vrmapping;
	ppsmc_pptable->VddMem1VrMapping = smc_dpm_table->vddmem1vrmapping;

	ppsmc_pptable->GfxUlvPhaseSheddingMask = smc_dpm_table->gfxulvphasesheddingmask;
	ppsmc_pptable->SocUlvPhaseSheddingMask = smc_dpm_table->soculvphasesheddingmask;
	ppsmc_pptable->ExternalSensorPresent = smc_dpm_table->externalsensorpresent;

	ppsmc_pptable->GfxMaxCurrent = smc_dpm_table->gfxmaxcurrent;
	ppsmc_pptable->GfxOffset = smc_dpm_table->gfxoffset;
	ppsmc_pptable->Padding_TelemetryGfx = smc_dpm_table->padding_telemetrygfx;

	ppsmc_pptable->SocMaxCurrent = smc_dpm_table->socmaxcurrent;
	ppsmc_pptable->SocOffset = smc_dpm_table->socoffset;
	ppsmc_pptable->Padding_TelemetrySoc = smc_dpm_table->padding_telemetrysoc;

	ppsmc_pptable->Mem0MaxCurrent = smc_dpm_table->mem0maxcurrent;
	ppsmc_pptable->Mem0Offset = smc_dpm_table->mem0offset;
	ppsmc_pptable->Padding_TelemetryMem0 = smc_dpm_table->padding_telemetrymem0;

	ppsmc_pptable->Mem1MaxCurrent = smc_dpm_table->mem1maxcurrent;
	ppsmc_pptable->Mem1Offset = smc_dpm_table->mem1offset;
	ppsmc_pptable->Padding_TelemetryMem1 = smc_dpm_table->padding_telemetrymem1;

	ppsmc_pptable->AcDcGpio = smc_dpm_table->acdcgpio;
	ppsmc_pptable->AcDcPolarity = smc_dpm_table->acdcpolarity;
	ppsmc_pptable->VR0HotGpio = smc_dpm_table->vr0hotgpio;
	ppsmc_pptable->VR0HotPolarity = smc_dpm_table->vr0hotpolarity;

	ppsmc_pptable->VR1HotGpio = smc_dpm_table->vr1hotgpio;
	ppsmc_pptable->VR1HotPolarity = smc_dpm_table->vr1hotpolarity;
	ppsmc_pptable->Padding1 = smc_dpm_table->padding1;
	ppsmc_pptable->Padding2 = smc_dpm_table->padding2;

	ppsmc_pptable->LedPin0 = smc_dpm_table->ledpin0;
	ppsmc_pptable->LedPin1 = smc_dpm_table->ledpin1;
	ppsmc_pptable->LedPin2 = smc_dpm_table->ledpin2;

	ppsmc_pptable->PllGfxclkSpreadEnabled = smc_dpm_table->pllgfxclkspreadenabled;
	ppsmc_pptable->PllGfxclkSpreadPercent = smc_dpm_table->pllgfxclkspreadpercent;
	ppsmc_pptable->PllGfxclkSpreadFreq = smc_dpm_table->pllgfxclkspreadfreq;

	ppsmc_pptable->UclkSpreadEnabled = 0;
	ppsmc_pptable->UclkSpreadPercent = smc_dpm_table->uclkspreadpercent;
	ppsmc_pptable->UclkSpreadFreq = smc_dpm_table->uclkspreadfreq;

	ppsmc_pptable->FclkSpreadEnabled = smc_dpm_table->fclkspreadenabled;
	ppsmc_pptable->FclkSpreadPercent = smc_dpm_table->fclkspreadpercent;
	ppsmc_pptable->FclkSpreadFreq = smc_dpm_table->fclkspreadfreq;

	ppsmc_pptable->FllGfxclkSpreadEnabled = smc_dpm_table->fllgfxclkspreadenabled;
	ppsmc_pptable->FllGfxclkSpreadPercent = smc_dpm_table->fllgfxclkspreadpercent;
	ppsmc_pptable->FllGfxclkSpreadFreq = smc_dpm_table->fllgfxclkspreadfreq;

	if ((smc_dpm_table->table_header.format_revision == 4) &&
	    (smc_dpm_table->table_header.content_revision == 4)) {
		for (i = 0; i < I2C_CONTROLLER_NAME_COUNT; i++) {
			ppsmc_pptable->I2cControllers[i].Enabled =
				smc_dpm_table->i2ccontrollers[i].enabled;
			ppsmc_pptable->I2cControllers[i].SlaveAddress =
				smc_dpm_table->i2ccontrollers[i].slaveaddress;
			ppsmc_pptable->I2cControllers[i].ControllerPort =
				smc_dpm_table->i2ccontrollers[i].controllerport;
			ppsmc_pptable->I2cControllers[i].ThermalThrottler =
				smc_dpm_table->i2ccontrollers[i].thermalthrottler;
			ppsmc_pptable->I2cControllers[i].I2cProtocol =
				smc_dpm_table->i2ccontrollers[i].i2cprotocol;
			ppsmc_pptable->I2cControllers[i].I2cSpeed =
				smc_dpm_table->i2ccontrollers[i].i2cspeed;
		}
	}

	return 0;
}

#define VEGA20_ENGINECLOCK_HARDMAX 198000
static int init_powerplay_table_information(
		struct pp_hwmgr *hwmgr,
		const ATOM_Vega20_POWERPLAYTABLE *powerplay_table)
{
	struct phm_ppt_v3_information *pptable_information =
		(struct phm_ppt_v3_information *)hwmgr->pptable;
	uint32_t disable_power_control = 0;
	uint32_t od_feature_count, od_setting_count, power_saving_clock_count;
	int result;

	hwmgr->thermal_controller.ucType = powerplay_table->ucThermalControllerType;
	pptable_information->uc_thermal_controller_type = powerplay_table->ucThermalControllerType;
	hwmgr->thermal_controller.fanInfo.ulMinRPM = 0;
	hwmgr->thermal_controller.fanInfo.ulMaxRPM = powerplay_table->smcPPTable.FanMaximumRpm;

	set_hw_cap(hwmgr,
		ATOM_VEGA20_PP_THERMALCONTROLLER_NONE != hwmgr->thermal_controller.ucType,
		PHM_PlatformCaps_ThermalController);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_MicrocodeFanControl);

	if (powerplay_table->OverDrive8Table.ucODTableRevision == 1) {
		od_feature_count =
			(le32_to_cpu(powerplay_table->OverDrive8Table.ODFeatureCount) >
			 ATOM_VEGA20_ODFEATURE_COUNT) ?
			ATOM_VEGA20_ODFEATURE_COUNT :
			le32_to_cpu(powerplay_table->OverDrive8Table.ODFeatureCount);
		od_setting_count =
			(le32_to_cpu(powerplay_table->OverDrive8Table.ODSettingCount) >
			 ATOM_VEGA20_ODSETTING_COUNT) ?
			ATOM_VEGA20_ODSETTING_COUNT :
			le32_to_cpu(powerplay_table->OverDrive8Table.ODSettingCount);

		copy_overdrive_feature_capabilities_array(hwmgr,
				&pptable_information->od_feature_capabilities,
				powerplay_table->OverDrive8Table.ODFeatureCapabilities,
				od_feature_count);
		phm_copy_overdrive_settings_limits_array(hwmgr,
				&pptable_information->od_settings_max,
				powerplay_table->OverDrive8Table.ODSettingsMax,
				od_setting_count);
		phm_copy_overdrive_settings_limits_array(hwmgr,
				&pptable_information->od_settings_min,
				powerplay_table->OverDrive8Table.ODSettingsMin,
				od_setting_count);
	}

	pptable_information->us_small_power_limit1 = le16_to_cpu(powerplay_table->usSmallPowerLimit1);
	pptable_information->us_small_power_limit2 = le16_to_cpu(powerplay_table->usSmallPowerLimit2);
	pptable_information->us_boost_power_limit = le16_to_cpu(powerplay_table->usBoostPowerLimit);
	pptable_information->us_od_turbo_power_limit = le16_to_cpu(powerplay_table->usODTurboPowerLimit);
	pptable_information->us_od_powersave_power_limit = le16_to_cpu(powerplay_table->usODPowerSavePowerLimit);

	pptable_information->us_software_shutdown_temp = le16_to_cpu(powerplay_table->usSoftwareShutdownTemp);

	hwmgr->platform_descriptor.TDPODLimit = le32_to_cpu(powerplay_table->OverDrive8Table.ODSettingsMax[ATOM_VEGA20_ODSETTING_POWERPERCENTAGE]);

	disable_power_control = 0;
	if (!disable_power_control && hwmgr->platform_descriptor.TDPODLimit)
		/* enable TDP overdrive (PowerControl) feature as well if supported */
		phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PowerControl);

	if (powerplay_table->PowerSavingClockTable.ucTableRevision == 1) {
		power_saving_clock_count =
			(le32_to_cpu(powerplay_table->PowerSavingClockTable.PowerSavingClockCount) >=
			 ATOM_VEGA20_PPCLOCK_COUNT) ?
			ATOM_VEGA20_PPCLOCK_COUNT :
			le32_to_cpu(powerplay_table->PowerSavingClockTable.PowerSavingClockCount);
		phm_copy_clock_limits_array(hwmgr,
				&pptable_information->power_saving_clock_max,
				powerplay_table->PowerSavingClockTable.PowerSavingClockMax,
				power_saving_clock_count);
		phm_copy_clock_limits_array(hwmgr,
				&pptable_information->power_saving_clock_min,
				powerplay_table->PowerSavingClockTable.PowerSavingClockMin,
				power_saving_clock_count);
	}

	pptable_information->smc_pptable = (PPTable_t *)kmalloc(sizeof(PPTable_t), GFP_KERNEL);
	if (pptable_information->smc_pptable == NULL)
		return -ENOMEM;

	if (powerplay_table->smcPPTable.Version <= 2)
		memcpy(pptable_information->smc_pptable,
				&(powerplay_table->smcPPTable),
				sizeof(PPTable_t) -
				sizeof(I2cControllerConfig_t) * I2C_CONTROLLER_NAME_COUNT);
	else
		memcpy(pptable_information->smc_pptable,
				&(powerplay_table->smcPPTable),
				sizeof(PPTable_t));

	result = append_vbios_pptable(hwmgr, (pptable_information->smc_pptable));

	return result;
}

static int vega20_pp_tables_initialize(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	const ATOM_Vega20_POWERPLAYTABLE *powerplay_table;

	hwmgr->pptable = kzalloc(sizeof(struct phm_ppt_v3_information), GFP_KERNEL);
	PP_ASSERT_WITH_CODE((hwmgr->pptable != NULL),
		"Failed to allocate hwmgr->pptable!", return -ENOMEM);

	powerplay_table = get_powerplay_table(hwmgr);
	PP_ASSERT_WITH_CODE((powerplay_table != NULL),
		"Missing PowerPlay Table!", return -1);

	result = check_powerplay_tables(hwmgr, powerplay_table);
	PP_ASSERT_WITH_CODE((result == 0),
		"check_powerplay_tables failed", return result);

	result = set_platform_caps(hwmgr,
			le32_to_cpu(powerplay_table->ulPlatformCaps));
	PP_ASSERT_WITH_CODE((result == 0),
		"set_platform_caps failed", return result);

	result = init_powerplay_table_information(hwmgr, powerplay_table);
	PP_ASSERT_WITH_CODE((result == 0),
		"init_powerplay_table_information failed", return result);

	return result;
}

static int vega20_pp_tables_uninitialize(struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v3_information *pp_table_info =
			(struct phm_ppt_v3_information *)(hwmgr->pptable);

	kfree(pp_table_info->power_saving_clock_max);
	pp_table_info->power_saving_clock_max = NULL;

	kfree(pp_table_info->power_saving_clock_min);
	pp_table_info->power_saving_clock_min = NULL;

	kfree(pp_table_info->od_feature_capabilities);
	pp_table_info->od_feature_capabilities = NULL;

	kfree(pp_table_info->od_settings_max);
	pp_table_info->od_settings_max = NULL;

	kfree(pp_table_info->od_settings_min);
	pp_table_info->od_settings_min = NULL;

	kfree(pp_table_info->smc_pptable);
	pp_table_info->smc_pptable = NULL;

	kfree(hwmgr->pptable);
	hwmgr->pptable = NULL;

	return 0;
}

const struct pp_table_func vega20_pptable_funcs = {
	.pptable_init = vega20_pp_tables_initialize,
	.pptable_fini = vega20_pp_tables_uninitialize,
};
