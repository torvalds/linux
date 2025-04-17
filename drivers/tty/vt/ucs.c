// SPDX-License-Identifier: GPL-2.0
/*
 * ucs.c - Universal Character Set processing
 */

#include <linux/array_size.h>
#include <linux/bsearch.h>
#include <linux/consolemap.h>
#include <linux/minmax.h>

/* ucs_is_double_width() is based on the wcwidth() implementation by
 * Markus Kuhn -- 2007-05-26 (Unicode 5.0)
 * Latest version: https://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
 */

struct ucs_interval {
	u32 first;
	u32 last;
};

static const struct ucs_interval ucs_double_width_ranges[] = {
	{ 0x1100, 0x115F }, { 0x2329, 0x232A }, { 0x2E80, 0x303E },
	{ 0x3040, 0xA4CF }, { 0xAC00, 0xD7A3 }, { 0xF900, 0xFAFF },
	{ 0xFE10, 0xFE19 }, { 0xFE30, 0xFE6F }, { 0xFF00, 0xFF60 },
	{ 0xFFE0, 0xFFE6 }, { 0x20000, 0x2FFFD }, { 0x30000, 0x3FFFD }
};

static int interval_cmp(const void *key, const void *element)
{
	u32 cp = *(u32 *)key;
	const struct ucs_interval *entry = element;

	if (cp < entry->first)
		return -1;
	if (cp > entry->last)
		return 1;
	return 0;
}

/**
 * ucs_is_double_width() - Determine if a Unicode code point is double-width.
 * @cp: Unicode code point (UCS-4)
 *
 * Return: true if the character is double-width, false otherwise
 */
bool ucs_is_double_width(u32 cp)
{
	size_t size = ARRAY_SIZE(ucs_double_width_ranges);

	if (!in_range(cp, ucs_double_width_ranges[0].first,
			  ucs_double_width_ranges[size - 1].last))
		return false;

	return __inline_bsearch(&cp, ucs_double_width_ranges, size,
				sizeof(*ucs_double_width_ranges),
				interval_cmp) != NULL;
}
