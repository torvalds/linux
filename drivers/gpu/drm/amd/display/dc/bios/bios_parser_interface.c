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

#include "dm_services.h"
#include "include/logger_interface.h"

#include "bios_parser_interface.h"
#include "bios_parser.h"

#if defined(CONFIG_DRM_AMD_DC_DCE12_0)
#include "bios_parser2.h"
#endif


struct dc_bios *dal_bios_parser_create(
	struct bp_init_data *init,
	enum dce_version dce_version)
{
	struct dc_bios *bios = NULL;

#if defined(CONFIG_DRM_AMD_DC_DCE12_0)
	bios = firmware_parser_create(init, dce_version);

	if (bios == NULL)
		/* TODO: remove dce_version from bios_parser.
		 * cannot remove today because dal enum to bp enum translation is dce specific
		 */
		bios = bios_parser_create(init, dce_version);
#else
	bios = bios_parser_create(init, dce_version);
#endif

	return bios;
}

void dal_bios_parser_destroy(struct dc_bios **dcb)
{
	struct dc_bios *bios = *dcb;

	bios->funcs->bios_parser_destroy(dcb);
}

