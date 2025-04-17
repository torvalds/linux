// SPDX-License-Identifier: GPL-2.0
/*
 * ucs.c - Universal Character Set processing
 */

#include <linux/array_size.h>
#include <linux/bsearch.h>
#include <linux/consolemap.h>
#include <linux/minmax.h>

struct ucs_interval16 {
	u16 first;
	u16 last;
};

struct ucs_interval32 {
	u32 first;
	u32 last;
};

#include "ucs_width_table.h"

static int interval16_cmp(const void *key, const void *element)
{
	u16 cp = *(u16 *)key;
	const struct ucs_interval16 *entry = element;

	if (cp < entry->first)
		return -1;
	if (cp > entry->last)
		return 1;
	return 0;
}

static int interval32_cmp(const void *key, const void *element)
{
	u32 cp = *(u32 *)key;
	const struct ucs_interval32 *entry = element;

	if (cp < entry->first)
		return -1;
	if (cp > entry->last)
		return 1;
	return 0;
}

static bool cp_in_range16(u16 cp, const struct ucs_interval16 *ranges, size_t size)
{
	if (!in_range(cp, ranges[0].first, ranges[size - 1].last))
		return false;

	return __inline_bsearch(&cp, ranges, size, sizeof(*ranges),
				interval16_cmp) != NULL;
}

static bool cp_in_range32(u32 cp, const struct ucs_interval32 *ranges, size_t size)
{
	if (!in_range(cp, ranges[0].first, ranges[size - 1].last))
		return false;

	return __inline_bsearch(&cp, ranges, size, sizeof(*ranges),
				interval32_cmp) != NULL;
}

#define UCS_IS_BMP(cp)	((cp) <= 0xffff)

/**
 * ucs_is_zero_width() - Determine if a Unicode code point is zero-width.
 * @cp: Unicode code point (UCS-4)
 *
 * Return: true if the character is zero-width, false otherwise
 */
bool ucs_is_zero_width(u32 cp)
{
	if (UCS_IS_BMP(cp))
		return cp_in_range16(cp, ucs_zero_width_bmp_ranges,
				     ARRAY_SIZE(ucs_zero_width_bmp_ranges));
	else
		return cp_in_range32(cp, ucs_zero_width_non_bmp_ranges,
				     ARRAY_SIZE(ucs_zero_width_non_bmp_ranges));
}

/**
 * ucs_is_double_width() - Determine if a Unicode code point is double-width.
 * @cp: Unicode code point (UCS-4)
 *
 * Return: true if the character is double-width, false otherwise
 */
bool ucs_is_double_width(u32 cp)
{
	if (UCS_IS_BMP(cp))
		return cp_in_range16(cp, ucs_double_width_bmp_ranges,
				     ARRAY_SIZE(ucs_double_width_bmp_ranges));
	else
		return cp_in_range32(cp, ucs_double_width_non_bmp_ranges,
				     ARRAY_SIZE(ucs_double_width_non_bmp_ranges));
}

/*
 * Structure for base with combining mark pairs and resulting recompositions.
 * Using u16 to save space since all values are within BMP range.
 */
struct ucs_recomposition {
	u16 base;	/* base character */
	u16 mark;	/* combining mark */
	u16 recomposed;	/* corresponding recomposed character */
};

#include "ucs_recompose_table.h"

struct compare_key {
	u16 base;
	u16 mark;
};

static int recomposition_cmp(const void *key, const void *element)
{
	const struct compare_key *search_key = key;
	const struct ucs_recomposition *entry = element;

	/* Compare base character first */
	if (search_key->base < entry->base)
		return -1;
	if (search_key->base > entry->base)
		return 1;

	/* Base characters match, now compare combining character */
	if (search_key->mark < entry->mark)
		return -1;
	if (search_key->mark > entry->mark)
		return 1;

	/* Both match */
	return 0;
}

/**
 * ucs_recompose() - Attempt to recompose two Unicode characters into a single character.
 * @base: Base Unicode code point (UCS-4)
 * @mark: Combining mark Unicode code point (UCS-4)
 *
 * Return: Recomposed Unicode code point, or 0 if no recomposition is possible
 */
u32 ucs_recompose(u32 base, u32 mark)
{
	/* Check if characters are within the range of our table */
	if (!in_range(base, UCS_RECOMPOSE_MIN_BASE, UCS_RECOMPOSE_MAX_BASE) ||
	    !in_range(mark, UCS_RECOMPOSE_MIN_MARK, UCS_RECOMPOSE_MAX_MARK))
		return 0;

	struct compare_key key = { base, mark };
	struct ucs_recomposition *result =
		__inline_bsearch(&key, ucs_recomposition_table,
				 ARRAY_SIZE(ucs_recomposition_table),
				 sizeof(*ucs_recomposition_table),
				 recomposition_cmp);

	return result ? result->recomposed : 0;
}
