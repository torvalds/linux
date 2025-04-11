#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# This script uses Python's unicodedata module to generate ucs_width.c

import unicodedata
import sys

def generate_ucs_width():
    # Output file name
    c_file = "ucs_width.c"

    # Width data mapping
    width_map = {}  # Maps code points to width (0, 1, 2)

    # Define emoji modifiers and components that should have zero width
    emoji_zero_width = [
        # Skin tone modifiers
        (0x1F3FB, 0x1F3FF),  # Emoji modifiers (skin tones)

        # Variation selectors (note: VS16 is treated specially in vt.c)
        (0xFE00, 0xFE0F),    # Variation Selectors 1-16

        # Gender and hair style modifiers
        (0x2640, 0x2640),    # Female sign
        (0x2642, 0x2642),    # Male sign
        (0x26A7, 0x26A7),    # Transgender symbol
        (0x1F9B0, 0x1F9B3),  # Hair components (red, curly, white, bald)

        # Tag characters
        (0xE0020, 0xE007E),  # Tags
    ]

    # Mark these emoji modifiers as zero-width
    for start, end in emoji_zero_width:
        for cp in range(start, end + 1):
            try:
                width_map[cp] = 0
            except (ValueError, OverflowError):
                continue

    # Mark all regional indicators as single-width as they are usually paired
    # providing a combined with of 2.
    regional_indicators = (0x1F1E6, 0x1F1FF)  # Regional indicator symbols A-Z
    start, end = regional_indicators
    for cp in range(start, end + 1):
        try:
            width_map[cp] = 1
        except (ValueError, OverflowError):
            continue

    # Process all assigned Unicode code points (Basic Multilingual Plane + Supplementary Planes)
    # Range 0x0 to 0x10FFFF (the full Unicode range)
    for block_start in range(0, 0x110000, 0x1000):
        block_end = block_start + 0x1000
        for cp in range(block_start, block_end):
            try:
                char = chr(cp)

                # Skip if already processed
                if cp in width_map:
                    continue

                # Check if the character is a combining mark
                category = unicodedata.category(char)

                # Combining marks, format characters, zero-width characters
                if (category.startswith('M') or  # Mark (combining)
                    (category == 'Cf' and cp not in (0x061C, 0x06DD, 0x070F, 0x180E, 0x200F, 0x202E, 0x2066, 0x2067, 0x2068, 0x2069)) or
                    cp in (0x200B, 0x200C, 0x200D, 0x2060, 0xFEFF)):  # Known zero-width characters
                    width_map[cp] = 0
                    continue

                # Use East Asian Width property
                eaw = unicodedata.east_asian_width(char)

                if eaw in ('F', 'W'):  # Fullwidth or Wide
                    width_map[cp] = 2
                elif eaw in ('Na', 'H', 'N', 'A'):  # Narrow, Halfwidth, Neutral, Ambiguous
                    width_map[cp] = 1
                else:
                    # Default to single-width for unknown
                    width_map[cp] = 1

            except (ValueError, OverflowError):
                # Skip invalid code points
                continue

    # Process Emoji - generally double-width
    # Ranges according to Unicode Emoji standard
    emoji_ranges = [
        (0x1F000, 0x1F02F),  # Mahjong Tiles
        (0x1F0A0, 0x1F0FF),  # Playing Cards
        (0x1F300, 0x1F5FF),  # Miscellaneous Symbols and Pictographs
        (0x1F600, 0x1F64F),  # Emoticons
        (0x1F680, 0x1F6FF),  # Transport and Map Symbols
        (0x1F700, 0x1F77F),  # Alchemical Symbols
        (0x1F780, 0x1F7FF),  # Geometric Shapes Extended
        (0x1F800, 0x1F8FF),  # Supplemental Arrows-C
        (0x1F900, 0x1F9FF),  # Supplemental Symbols and Pictographs
        (0x1FA00, 0x1FA6F),  # Chess Symbols
        (0x1FA70, 0x1FAFF),  # Symbols and Pictographs Extended-A
    ]

    for start, end in emoji_ranges:
        for cp in range(start, end + 1):
            if cp not in width_map or width_map[cp] != 0:  # Don't override zero-width
                try:
                    char = chr(cp)
                    width_map[cp] = 2
                except (ValueError, OverflowError):
                    continue

    # Optimize to create range tables
    def ranges_optimize(width_data, target_width):
        points = sorted([cp for cp, width in width_data.items() if width == target_width])
        if not points:
            return []

        # Group consecutive code points into ranges
        ranges = []
        start = points[0]
        prev = start

        for cp in points[1:]:
            if cp > prev + 1:
                ranges.append((start, prev))
                start = cp
            prev = cp

        # Add the last range
        ranges.append((start, prev))
        return ranges

    # Function to split ranges into BMP (16-bit) and non-BMP (above 16-bit)
    def split_ranges_by_size(ranges):
        bmp_ranges = []
        non_bmp_ranges = []

        for start, end in ranges:
            if end <= 0xFFFF:
                bmp_ranges.append((start, end))
            elif start > 0xFFFF:
                non_bmp_ranges.append((start, end))
            else:
                # Split the range at 0xFFFF
                bmp_ranges.append((start, 0xFFFF))
                non_bmp_ranges.append((0x10000, end))

        return bmp_ranges, non_bmp_ranges

    # Extract ranges for each width
    zero_width_ranges = ranges_optimize(width_map, 0)
    double_width_ranges = ranges_optimize(width_map, 2)

    # Split ranges into BMP and non-BMP
    zero_width_bmp, zero_width_non_bmp = split_ranges_by_size(zero_width_ranges)
    double_width_bmp, double_width_non_bmp = split_ranges_by_size(double_width_ranges)

    # Get Unicode version information
    unicode_version = unicodedata.unidata_version

    # Function to generate code point description comments
    def get_code_point_comment(start, end):
        try:
            start_char_desc = unicodedata.name(chr(start))
            if start == end:
                return f"/* {start_char_desc} */"
            else:
                end_char_desc = unicodedata.name(chr(end))
                return f"/* {start_char_desc} - {end_char_desc} */"
        except:
            if start == end:
                return f"/* U+{start:04X} */"
            else:
                return f"/* U+{start:04X} - U+{end:04X} */"

    # Generate C implementation file
    with open(c_file, 'w') as f:
        f.write(f"""\
// SPDX-License-Identifier: GPL-2.0
/*
 * ucs_width.c - Unicode character width lookup
 *
 * Auto-generated by gen_ucs_width.py
 *
 * Unicode Version: {unicode_version}
 */

#include <linux/types.h>
#include <linux/array_size.h>
#include <linux/bsearch.h>
#include <linux/consolemap.h>

struct interval16 {{
	uint16_t first;
	uint16_t last;
}};

struct interval32 {{
	uint32_t first;
	uint32_t last;
}};

/* Zero-width character ranges (BMP - Basic Multilingual Plane, U+0000 to U+FFFF) */
static const struct interval16 zero_width_bmp[] = {{
""")

        for start, end in zero_width_bmp:
            comment = get_code_point_comment(start, end)
            f.write(f"\t{{ 0x{start:04X}, 0x{end:04X} }}, {comment}\n")

        f.write("""\
};

/* Zero-width character ranges (non-BMP, U+10000 and above) */
static const struct interval32 zero_width_non_bmp[] = {
""")

        for start, end in zero_width_non_bmp:
            comment = get_code_point_comment(start, end)
            f.write(f"\t{{ 0x{start:05X}, 0x{end:05X} }}, {comment}\n")

        f.write("""\
};

/* Double-width character ranges (BMP - Basic Multilingual Plane, U+0000 to U+FFFF) */
static const struct interval16 double_width_bmp[] = {
""")

        for start, end in double_width_bmp:
            comment = get_code_point_comment(start, end)
            f.write(f"\t{{ 0x{start:04X}, 0x{end:04X} }}, {comment}\n")

        f.write("""\
};

/* Double-width character ranges (non-BMP, U+10000 and above) */
static const struct interval32 double_width_non_bmp[] = {
""")

        for start, end in double_width_non_bmp:
            comment = get_code_point_comment(start, end)
            f.write(f"\t{{ 0x{start:05X}, 0x{end:05X} }}, {comment}\n")

        f.write("""\
};


static int ucs_cmp16(const void *key, const void *element)
{
	uint16_t cp = *(uint16_t *)key;
	const struct interval16 *e = element;

	if (cp > e->last)
		return 1;
	if (cp < e->first)
		return -1;
	return 0;
}

static int ucs_cmp32(const void *key, const void *element)
{
	uint32_t cp = *(uint32_t *)key;
	const struct interval32 *e = element;

	if (cp > e->last)
		return 1;
	if (cp < e->first)
		return -1;
	return 0;
}

static bool is_in_interval16(uint16_t cp, const struct interval16 *intervals, size_t count)
{
	if (cp < intervals[0].first || cp > intervals[count - 1].last)
		return false;

	return __inline_bsearch(&cp, intervals, count,
				sizeof(*intervals), ucs_cmp16) != NULL;
}

static bool is_in_interval32(uint32_t cp, const struct interval32 *intervals, size_t count)
{
	if (cp < intervals[0].first || cp > intervals[count - 1].last)
		return false;

	return __inline_bsearch(&cp, intervals, count,
				sizeof(*intervals), ucs_cmp32) != NULL;
}

/**
 * Determine if a Unicode code point is zero-width.
 *
 * @param cp: Unicode code point (UCS-4)
 * Return: true if the character is zero-width, false otherwise
 */
bool ucs_is_zero_width(uint32_t cp)
{
	return (cp <= 0xFFFF)
	       ? is_in_interval16(cp, zero_width_bmp, ARRAY_SIZE(zero_width_bmp))
	       : is_in_interval32(cp, zero_width_non_bmp, ARRAY_SIZE(zero_width_non_bmp));
}

/**
 * Determine if a Unicode code point is double-width.
 *
 * @param cp: Unicode code point (UCS-4)
 * Return: true if the character is double-width, false otherwise
 */
bool ucs_is_double_width(uint32_t cp)
{
	return (cp <= 0xFFFF)
	       ? is_in_interval16(cp, double_width_bmp, ARRAY_SIZE(double_width_bmp))
	       : is_in_interval32(cp, double_width_non_bmp, ARRAY_SIZE(double_width_non_bmp));
}
""")

    # Print summary
    zero_width_bmp_count = sum(end - start + 1 for start, end in zero_width_bmp)
    zero_width_non_bmp_count = sum(end - start + 1 for start, end in zero_width_non_bmp)
    double_width_bmp_count = sum(end - start + 1 for start, end in double_width_bmp)
    double_width_non_bmp_count = sum(end - start + 1 for start, end in double_width_non_bmp)

    total_zero_width = zero_width_bmp_count + zero_width_non_bmp_count
    total_double_width = double_width_bmp_count + double_width_non_bmp_count

    print(f"Generated {c_file} with:")
    print(f"- {len(zero_width_bmp)} zero-width BMP ranges (16-bit) covering ~{zero_width_bmp_count} code points")
    print(f"- {len(zero_width_non_bmp)} zero-width non-BMP ranges (32-bit) covering ~{zero_width_non_bmp_count} code points")
    print(f"- {len(double_width_bmp)} double-width BMP ranges (16-bit) covering ~{double_width_bmp_count} code points")
    print(f"- {len(double_width_non_bmp)} double-width non-BMP ranges (32-bit) covering ~{double_width_non_bmp_count} code points")
    print(f"Total: {len(zero_width_bmp) + len(zero_width_non_bmp) + len(double_width_bmp) + len(double_width_non_bmp)} ranges covering ~{total_zero_width + total_double_width} code points")

if __name__ == "__main__":
    generate_ucs_width()
