// SPDX-License-Identifier: GPL-2.0
/*
 * ucs.c - Universal Character Set processing
 */

#include <linux/array_size.h>
#include <linux/bsearch.h>
#include <linux/consolemap.h>
#include <linux/minmax.h>

struct ucs_interval {
	u32 first;
	u32 last;
};

#include "ucs_width_table.h"

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

static bool cp_in_range(u32 cp, const struct ucs_interval *ranges, size_t size)
{
	if (!in_range(cp, ranges[0].first, ranges[size - 1].last))
		return false;

	return __inline_bsearch(&cp, ranges, size, sizeof(*ranges),
				interval_cmp) != NULL;
}

/**
 * ucs_is_zero_width() - Determine if a Unicode code point is zero-width.
 * @cp: Unicode code point (UCS-4)
 *
 * Return: true if the character is zero-width, false otherwise
 */
bool ucs_is_zero_width(u32 cp)
{
	return cp_in_range(cp, ucs_zero_width_ranges,
			   ARRAY_SIZE(ucs_zero_width_ranges));
}

/**
 * ucs_is_double_width() - Determine if a Unicode code point is double-width.
 * @cp: Unicode code point (UCS-4)
 *
 * Return: true if the character is double-width, false otherwise
 */
bool ucs_is_double_width(u32 cp)
{
	return cp_in_range(cp, ucs_double_width_ranges,
			   ARRAY_SIZE(ucs_double_width_ranges));
}
