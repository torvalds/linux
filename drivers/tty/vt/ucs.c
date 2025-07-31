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
	if (cp < ranges[0].first || cp > ranges[size - 1].last)
		return false;

	return __inline_bsearch(&cp, ranges, size, sizeof(*ranges),
				interval16_cmp) != NULL;
}

static bool cp_in_range32(u32 cp, const struct ucs_interval32 *ranges, size_t size)
{
	if (cp < ranges[0].first || cp > ranges[size - 1].last)
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
	if (base < UCS_RECOMPOSE_MIN_BASE || base > UCS_RECOMPOSE_MAX_BASE ||
	    mark < UCS_RECOMPOSE_MIN_MARK || mark > UCS_RECOMPOSE_MAX_MARK)
		return 0;

	struct compare_key key = { base, mark };
	struct ucs_recomposition *result =
		__inline_bsearch(&key, ucs_recomposition_table,
				 ARRAY_SIZE(ucs_recomposition_table),
				 sizeof(*ucs_recomposition_table),
				 recomposition_cmp);

	return result ? result->recomposed : 0;
}

/*
 * The fallback table structures implement a 2-level lookup.
 */

struct ucs_page_desc {
	u8 page;	/* Page index (high byte of code points) */
	u8 count;	/* Number of entries in this page */
	u16 start;	/* Start index in entries array */
};

struct ucs_page_entry {
	u8 offset;	/* Offset within page (0-255) */
	u8 fallback;	/* Fallback character or range start marker */
};

#include "ucs_fallback_table.h"

static int ucs_page_desc_cmp(const void *key, const void *element)
{
	u8 page = *(u8 *)key;
	const struct ucs_page_desc *entry = element;

	if (page < entry->page)
		return -1;
	if (page > entry->page)
		return 1;
	return 0;
}

static int ucs_page_entry_cmp(const void *key, const void *element)
{
	u8 offset = *(u8 *)key;
	const struct ucs_page_entry *entry = element;

	if (offset < entry->offset)
		return -1;
	if (entry->fallback == UCS_PAGE_ENTRY_RANGE_MARKER) {
		if (offset > entry[1].offset)
			return 1;
	} else {
		if (offset > entry->offset)
			return 1;
	}
	return 0;
}

/**
 * ucs_get_fallback() - Get a substitution for the provided Unicode character
 * @cp: Unicode code point (UCS-4)
 *
 * Get a simpler fallback character for the provided Unicode character.
 * This is used for terminal display when corresponding glyph is unavailable.
 * The substitution may not be as good as the actual glyph for the original
 * character but still way more helpful than a squared question mark.
 *
 * Return: Fallback Unicode code point, or 0 if none is available
 */
u32 ucs_get_fallback(u32 cp)
{
	const struct ucs_page_desc *page;
	const struct ucs_page_entry *entry;
	u8 page_idx = cp >> 8, offset = cp;

	if (!UCS_IS_BMP(cp))
		return 0;

	/*
	 * Full-width to ASCII mapping (covering all printable ASCII 33-126)
	 * 0xFF01 (！) to 0xFF5E (～) -> ASCII 33 (!) to 126 (~)
	 * We process them programmatically to reduce the table size.
	 */
	if (cp >= 0xFF01 && cp <= 0xFF5E)
		return cp - 0xFF01 + 33;

	page = __inline_bsearch(&page_idx, ucs_fallback_pages,
				ARRAY_SIZE(ucs_fallback_pages),
				sizeof(*ucs_fallback_pages),
				ucs_page_desc_cmp);
	if (!page)
		return 0;

	entry = __inline_bsearch(&offset, ucs_fallback_entries + page->start,
				 page->count, sizeof(*ucs_fallback_entries),
				 ucs_page_entry_cmp);
	if (!entry)
		return 0;

	if (entry->fallback == UCS_PAGE_ENTRY_RANGE_MARKER)
		entry++;
	return entry->fallback;
}
