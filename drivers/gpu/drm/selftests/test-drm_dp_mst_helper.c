// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test cases for for the DRM DP MST helpers
 */

#include <drm/drm_dp_mst_helper.h>
#include <drm/drm_print.h>

#include "test-drm_modeset_common.h"

int igt_dp_mst_calc_pbn_mode(void *ignored)
{
	int pbn, i;
	const struct {
		int rate;
		int bpp;
		int expected;
	} test_params[] = {
		{ 154000, 30, 689 },
		{ 234000, 30, 1047 },
		{ 297000, 24, 1063 },
	};

	for (i = 0; i < ARRAY_SIZE(test_params); i++) {
		pbn = drm_dp_calc_pbn_mode(test_params[i].rate,
					   test_params[i].bpp);
		FAIL(pbn != test_params[i].expected,
		     "Expected PBN %d for clock %d bpp %d, got %d\n",
		     test_params[i].expected, test_params[i].rate,
		     test_params[i].bpp, pbn);
	}

	return 0;
}
