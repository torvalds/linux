/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#include <linux/slab.h>

#include "dm_services.h"

/*
 * Pre-requisites: headers required by header of this unit
 */
#include "include/gpio_types.h"

/*
 * Header of this unit
 */

#include "hw_factory.h"

/*
 * Post-requisites: headers required by this unit
 */

#include "dce80/hw_factory_dce80.h"
#include "dce110/hw_factory_dce110.h"
#include "dce120/hw_factory_dce120.h"
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
#include "dcn10/hw_factory_dcn10.h"
#endif
#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
#include "dcn20/hw_factory_dcn20.h"
#endif
#if defined(CONFIG_DRM_AMD_DC_DCN2_1)
#include "dcn21/hw_factory_dcn21.h"
#endif

#include "diagnostics/hw_factory_diag.h"

/*
 * This unit
 */

bool dal_hw_factory_init(
	struct hw_factory *factory,
	enum dce_version dce_version,
	enum dce_environment dce_environment)
{
	if (IS_FPGA_MAXIMUS_DC(dce_environment)) {
		dal_hw_factory_diag_fpga_init(factory);
		return true;
	}

	switch (dce_version) {
	case DCE_VERSION_8_0:
	case DCE_VERSION_8_1:
	case DCE_VERSION_8_3:
		dal_hw_factory_dce80_init(factory);
		return true;

	case DCE_VERSION_10_0:
		dal_hw_factory_dce110_init(factory);
		return true;
	case DCE_VERSION_11_0:
	case DCE_VERSION_11_2:
	case DCE_VERSION_11_22:
		dal_hw_factory_dce110_init(factory);
		return true;
	case DCE_VERSION_12_0:
	case DCE_VERSION_12_1:
		dal_hw_factory_dce120_init(factory);
		return true;
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	case DCN_VERSION_1_0:
	case DCN_VERSION_1_01:
		dal_hw_factory_dcn10_init(factory);
		return true;
#endif

#if defined(CONFIG_DRM_AMD_DC_DCN2_0)
	case DCN_VERSION_2_0:
		dal_hw_factory_dcn20_init(factory);
		return true;
#endif
#if defined(CONFIG_DRM_AMD_DC_DCN2_1)
	case DCN_VERSION_2_1:
		dal_hw_factory_dcn21_init(factory);
		return true;
#endif

	default:
		ASSERT_CRITICAL(false);
		return false;
	}
}

void dal_hw_factory_destroy(
	struct dc_context *ctx,
	struct hw_factory **factory)
{
	if (!factory || !*factory) {
		BREAK_TO_DEBUGGER();
		return;
	}

	kfree(*factory);

	*factory = NULL;
}
