/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#include "eventmgr.h"
#include "eventinit.h"
#include "ppinterrupt.h"
#include "hardwaremanager.h"

void pem_init_feature_info(struct pp_eventmgr *eventmgr)
{

	/* PowerPlay info */
	eventmgr->ui_state_info[PP_PowerSource_AC].default_ui_lable =
					    PP_StateUILabel_Performance;

	eventmgr->ui_state_info[PP_PowerSource_AC].current_ui_label =
					    PP_StateUILabel_Performance;

	eventmgr->ui_state_info[PP_PowerSource_DC].default_ui_lable =
						  PP_StateUILabel_Battery;

	eventmgr->ui_state_info[PP_PowerSource_DC].current_ui_label =
						  PP_StateUILabel_Battery;

	if (phm_cap_enabled(eventmgr->platform_descriptor->platformCaps, PHM_PlatformCaps_PowerPlaySupport)) {
		eventmgr->features[PP_Feature_PowerPlay].supported = true;
		eventmgr->features[PP_Feature_PowerPlay].version = PEM_CURRENT_POWERPLAY_FEATURE_VERSION;
		eventmgr->features[PP_Feature_PowerPlay].enabled_default = true;
		eventmgr->features[PP_Feature_PowerPlay].enabled = true;
	} else {
		eventmgr->features[PP_Feature_PowerPlay].supported = false;
		eventmgr->features[PP_Feature_PowerPlay].enabled = false;
		eventmgr->features[PP_Feature_PowerPlay].enabled_default = false;
	}

	eventmgr->features[PP_Feature_Force3DClock].supported = true;
	eventmgr->features[PP_Feature_Force3DClock].enabled = false;
	eventmgr->features[PP_Feature_Force3DClock].enabled_default = false;
	eventmgr->features[PP_Feature_Force3DClock].version = 1;

	/* over drive*/
	eventmgr->features[PP_Feature_User2DPerformance].version = 4;
	eventmgr->features[PP_Feature_User3DPerformance].version = 4;
	eventmgr->features[PP_Feature_OverdriveTest].version = 4;

	eventmgr->features[PP_Feature_OverDrive].version = 4;
	eventmgr->features[PP_Feature_OverDrive].enabled = false;
	eventmgr->features[PP_Feature_OverDrive].enabled_default = false;

	eventmgr->features[PP_Feature_User2DPerformance].supported = false;
	eventmgr->features[PP_Feature_User2DPerformance].enabled = false;
	eventmgr->features[PP_Feature_User2DPerformance].enabled_default = false;

	eventmgr->features[PP_Feature_User3DPerformance].supported = false;
	eventmgr->features[PP_Feature_User3DPerformance].enabled = false;
	eventmgr->features[PP_Feature_User3DPerformance].enabled_default = false;

	eventmgr->features[PP_Feature_OverdriveTest].supported = false;
	eventmgr->features[PP_Feature_OverdriveTest].enabled = false;
	eventmgr->features[PP_Feature_OverdriveTest].enabled_default = false;

	eventmgr->features[PP_Feature_OverDrive].supported = false;

	eventmgr->features[PP_Feature_PowerBudgetWaiver].enabled_default = false;
	eventmgr->features[PP_Feature_PowerBudgetWaiver].version = 1;
	eventmgr->features[PP_Feature_PowerBudgetWaiver].supported = false;
	eventmgr->features[PP_Feature_PowerBudgetWaiver].enabled = false;

	/* Multi UVD States support */
	eventmgr->features[PP_Feature_MultiUVDState].supported = false;
	eventmgr->features[PP_Feature_MultiUVDState].enabled = false;
	eventmgr->features[PP_Feature_MultiUVDState].enabled_default = false;

	/* Dynamic UVD States support */
	eventmgr->features[PP_Feature_DynamicUVDState].supported = false;
	eventmgr->features[PP_Feature_DynamicUVDState].enabled = false;
	eventmgr->features[PP_Feature_DynamicUVDState].enabled_default = false;

	/* VCE DPM support */
	eventmgr->features[PP_Feature_VCEDPM].supported = false;
	eventmgr->features[PP_Feature_VCEDPM].enabled = false;
	eventmgr->features[PP_Feature_VCEDPM].enabled_default = false;

	/* ACP PowerGating support */
	eventmgr->features[PP_Feature_ACP_POWERGATING].supported = false;
	eventmgr->features[PP_Feature_ACP_POWERGATING].enabled = false;
	eventmgr->features[PP_Feature_ACP_POWERGATING].enabled_default = false;

	/* PPM support */
	eventmgr->features[PP_Feature_PPM].version = 1;
	eventmgr->features[PP_Feature_PPM].supported = false;
	eventmgr->features[PP_Feature_PPM].enabled = false;

	/* FFC support (enables fan and temp settings, Gemini needs temp settings) */
	if (phm_cap_enabled(eventmgr->platform_descriptor->platformCaps, PHM_PlatformCaps_ODFuzzyFanControlSupport) ||
	    phm_cap_enabled(eventmgr->platform_descriptor->platformCaps, PHM_PlatformCaps_GeminiRegulatorFanControlSupport)) {
		eventmgr->features[PP_Feature_FFC].version = 1;
		eventmgr->features[PP_Feature_FFC].supported = true;
		eventmgr->features[PP_Feature_FFC].enabled = true;
		eventmgr->features[PP_Feature_FFC].enabled_default = true;
	} else {
		eventmgr->features[PP_Feature_FFC].supported = false;
		eventmgr->features[PP_Feature_FFC].enabled = false;
		eventmgr->features[PP_Feature_FFC].enabled_default = false;
	}

	eventmgr->features[PP_Feature_VariBright].supported = false;
	eventmgr->features[PP_Feature_VariBright].enabled = false;
	eventmgr->features[PP_Feature_VariBright].enabled_default = false;

	eventmgr->features[PP_Feature_BACO].supported = false;
	eventmgr->features[PP_Feature_BACO].supported = false;
	eventmgr->features[PP_Feature_BACO].enabled_default = false;

	/* PowerDown feature support */
	eventmgr->features[PP_Feature_PowerDown].supported = false;
	eventmgr->features[PP_Feature_PowerDown].enabled = false;
	eventmgr->features[PP_Feature_PowerDown].enabled_default = false;

	eventmgr->features[PP_Feature_FPS].version = 1;
	eventmgr->features[PP_Feature_FPS].supported = false;
	eventmgr->features[PP_Feature_FPS].enabled_default = false;
	eventmgr->features[PP_Feature_FPS].enabled = false;

	eventmgr->features[PP_Feature_ViPG].version = 1;
	eventmgr->features[PP_Feature_ViPG].supported = false;
	eventmgr->features[PP_Feature_ViPG].enabled_default = false;
	eventmgr->features[PP_Feature_ViPG].enabled = false;
}

static int thermal_interrupt_callback(void *private_data,
				      unsigned src_id, const uint32_t *iv_entry)
{
	/* TO DO hanle PEM_Event_ThermalNotification (struct pp_eventmgr *)private_data*/
	pr_info("current thermal is out of range \n");
	return 0;
}

int pem_register_interrupts(struct pp_eventmgr *eventmgr)
{
	int result = 0;
	struct pp_interrupt_registration_info info;

	info.call_back = thermal_interrupt_callback;
	info.context = eventmgr;

	result = phm_register_thermal_interrupt(eventmgr->hwmgr, &info);

	/* TODO:
	 * 2. Register CTF event interrupt
	 * 3. Register for vbios events interrupt
	 * 4. Register External Throttle Interrupt
	 * 5. Register Smc To Host Interrupt
	 * */
	return result;
}


int pem_unregister_interrupts(struct pp_eventmgr *eventmgr)
{
	return 0;
}


void pem_uninit_featureInfo(struct pp_eventmgr *eventmgr)
{
	eventmgr->features[PP_Feature_MultiUVDState].supported = false;
	eventmgr->features[PP_Feature_VariBright].supported = false;
	eventmgr->features[PP_Feature_PowerBudgetWaiver].supported = false;
	eventmgr->features[PP_Feature_OverDrive].supported = false;
	eventmgr->features[PP_Feature_OverdriveTest].supported = false;
	eventmgr->features[PP_Feature_User3DPerformance].supported = false;
	eventmgr->features[PP_Feature_User2DPerformance].supported = false;
	eventmgr->features[PP_Feature_PowerPlay].supported = false;
	eventmgr->features[PP_Feature_Force3DClock].supported = false;
}
