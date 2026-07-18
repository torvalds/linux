// SPDX-License-Identifier: GPL-2.0
/*
 * ucs.c - Universal Character Set processing
 */

#include <linux/array_size.h>
#include <linux/build_bug.h>
#include <linux/bsearch.h>
#include <linux/consolemap.h>
#include <linux/math.h>

struct ucs_width16 {
	u16 first;
	u16 last;
};

struct ucs_width32 {
	u32 first;
	u32 last;
};

/*
 * Width table encoding (consumed by ucs_width_table.h):
 *
 * Zero- and double-width ranges are merged into one sorted-by-`first` table
 * per region (BMP / non-BMP). The BMP table stores plain (first, last)
 * pairs; per-entry width lives in a packed bitmap *hosted by the non-BMP
 * table*.
 *
 * That hosting is the whole point of the encoding. Non-BMP code points use
 * only 20 bits, so each u32 has 12 spare high bits sitting around doing
 * nothing — we'd rather use them than spend a separate parallel array for
 * width and BMP-bitmap bits. So we move the cp value up by UCS_CP_SHIFT
 * and stash metadata in the now-free low bits of `last`:
 *   - bit UCS_NONBMP_W2_FLAG_BIT: this entry's own width (0=zero, 1=double),
 *   - bits 0..UCS_NONBMP_BMP_BITS-1: a chunk of the BMP double-width
 *     bitmap. Bit `j` of the chunk in non-BMP entry `c` is set iff BMP
 *     entry (c * UCS_NONBMP_BMP_BITS + j) is double-width. The first
 *     ceil(N_BMP / UCS_NONBMP_BMP_BITS) non-BMP entries carry the bitmap;
 *     the rest leave these bits zero.
 *
 * Because the metadata bits sit strictly below the lowest cp-scale bit,
 * the bsearch comparator does plain u32 comparison on the shifted key and
 * stored values without masking — ordering between distinct code points is
 * undisturbed.
 */
#define UCS_CP_SHIFT           12
#define UCS_NONBMP_W2_FLAG_BIT 11
#define UCS_NONBMP_W2_FLAG     (1u << UCS_NONBMP_W2_FLAG_BIT)

#define BMP_0WIDTH(first, last)   first, last
#define BMP_2WIDTH(first, last)   first, last
#define RANGE_0WIDTH(first, last) \
	(u32)(first) << UCS_CP_SHIFT,  (u32)(last) << UCS_CP_SHIFT
#define RANGE_2WIDTH(first, last) \
	(u32)(first) << UCS_CP_SHIFT, ((u32)(last) << UCS_CP_SHIFT) | UCS_NONBMP_W2_FLAG
#define BMP_2W_BITS(b)            (b)

#include "ucs_width_table.h"

static_assert(UCS_NONBMP_BMP_BITS <= UCS_NONBMP_W2_FLAG_BIT,
	      "BMP bitmap chunk would overlap the per-entry width flag");
static_assert(UCS_NONBMP_W2_FLAG_BIT < UCS_CP_SHIFT,
	      "Metadata bits collide with the shifted cp value");
static_assert(DIV_ROUND_UP(ARRAY_SIZE(ucs_bmp_ranges), UCS_NONBMP_BMP_BITS)
	      <= ARRAY_SIZE(ucs_nonbmp_ranges),
	      "Not enough non-BMP entries to host the BMP width bitmap");

#define UCS_IS_BMP(cp)	((cp) <= 0xffff)

static int width16_cmp(const void *key, const void *element)
{
	u16 cp = *(u16 *)key;
	const struct ucs_width16 *entry = element;

	if (cp < entry->first)
		return -1;
	if (cp > entry->last)
		return 1;
	return 0;
}

static int width32_cmp(const void *key, const void *element)
{
	u32 k = *(u32 *)key;
	const struct ucs_width32 *entry = element;

	if (k < entry->first)
		return -1;
	if (k > entry->last)
		return 1;
	return 0;
}

/**
 * ucs_get_width() - Get the display width of a Unicode code point.
 * @cp: Unicode code point (UCS-4)
 *
 * Return: 2 for double-width (East Asian Wide/Fullwidth, emoji, ...),
 *         0 for zero-width (combining marks, format characters, ...),
 *         1 for everything else (the common case).
 */
unsigned int ucs_get_width(u32 cp)
{
	const struct ucs_width16 *e16;
	const struct ucs_width32 *e32;
	unsigned int idx;
	u32 k;

	if (UCS_IS_BMP(cp)) {
		u16 bmp = cp;

		if (bmp < ucs_bmp_ranges[0].first ||
		    bmp > ucs_bmp_ranges[ARRAY_SIZE(ucs_bmp_ranges) - 1].last)
			return 1;

		e16 = __inline_bsearch(&bmp, ucs_bmp_ranges,
				       ARRAY_SIZE(ucs_bmp_ranges),
				       sizeof(*ucs_bmp_ranges), width16_cmp);
		if (!e16)
			return 1;

		idx = e16 - ucs_bmp_ranges;
		return (ucs_nonbmp_ranges[idx / UCS_NONBMP_BMP_BITS].last
			>> (idx % UCS_NONBMP_BMP_BITS)) & 1 ? 2 : 0;
	}

	k = cp << UCS_CP_SHIFT;
	if (k < ucs_nonbmp_ranges[0].first ||
	    k > ucs_nonbmp_ranges[ARRAY_SIZE(ucs_nonbmp_ranges) - 1].last)
		return 1;

	e32 = __inline_bsearch(&k, ucs_nonbmp_ranges,
			       ARRAY_SIZE(ucs_nonbmp_ranges),
			       sizeof(*ucs_nonbmp_ranges), width32_cmp);
	if (!e32)
		return 1;
	return (e32->last & UCS_NONBMP_W2_FLAG) ? 2 : 0;
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
