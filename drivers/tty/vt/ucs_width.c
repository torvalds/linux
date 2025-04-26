// SPDX-License-Identifier: GPL-2.0

#include <linux/types.h>
#include <linux/array_size.h>
#include <linux/bsearch.h>
#include <linux/consolemap.h>

/* ucs_is_double_width() is based on the wcwidth() implementation by
 * Markus Kuhn -- 2007-05-26 (Unicode 5.0)
 * Latest version: https://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
 */

struct interval {
	uint32_t first;
	uint32_t last;
};

static int ucs_cmp(const void *key, const void *elt)
{
	uint32_t cp = *(uint32_t *)key;
	struct interval e = *(struct interval *) elt;

	if (cp > e.last)
		return 1;
	else if (cp < e.first)
		return -1;
	return 0;
}

static const struct interval double_width[] = {
	{ 0x1100, 0x115F }, { 0x2329, 0x232A }, { 0x2E80, 0x303E },
	{ 0x3040, 0xA4CF }, { 0xAC00, 0xD7A3 }, { 0xF900, 0xFAFF },
	{ 0xFE10, 0xFE19 }, { 0xFE30, 0xFE6F }, { 0xFF00, 0xFF60 },
	{ 0xFFE0, 0xFFE6 }, { 0x20000, 0x2FFFD }, { 0x30000, 0x3FFFD }
};

bool ucs_is_double_width(uint32_t cp)
{
	if (cp < double_width[0].first ||
	    cp > double_width[ARRAY_SIZE(double_width) - 1].last)
		return false;

	return bsearch(&cp, double_width, ARRAY_SIZE(double_width),
		       sizeof(struct interval), ucs_cmp) != NULL;
}
