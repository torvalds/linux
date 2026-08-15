#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Leverage Python's unicodedata module to generate ucs_width_table.h

import unicodedata
import sys
import argparse

# This script's file name
from pathlib import Path
this_file = Path(__file__).name

# Default output file name
DEFAULT_OUT_FILE = "ucs_width_table.h"

# --- Global Constants for Width Assignments ---

# Known zero-width characters
KNOWN_ZERO_WIDTH = (
    0x200B,  # ZERO WIDTH SPACE
    0x200C,  # ZERO WIDTH NON-JOINER
    0x200D,  # ZERO WIDTH JOINER
    0x2060,  # WORD JOINER
    0xFEFF   # ZERO WIDTH NO-BREAK SPACE (BOM)
)

# Zero-width emoji modifiers and components
# NOTE: Some of these characters would normally be single-width according to
# East Asian Width properties, but we deliberately override them to be
# zero-width because they function as modifiers in emoji sequences.
EMOJI_ZERO_WIDTH = [
    # Skin tone modifiers
    (0x1F3FB, 0x1F3FF),  # Emoji modifiers (skin tones)

    # Variation selectors (note: VS16 is treated specially in vt.c)
    (0xFE00, 0xFE0F),    # Variation Selectors 1-16

    # Gender and hair style modifiers
    # These would be single-width by Unicode properties, but are zero-width
    # when part of emoji
    (0x2640, 0x2640),    # Female sign
    (0x2642, 0x2642),    # Male sign
    (0x26A7, 0x26A7),    # Transgender symbol
    (0x1F9B0, 0x1F9B3),  # Hair components (red, curly, white, bald)

    # Tag characters
    (0xE0020, 0xE007E),  # Tags
]

# Regional indicators (flag components)
REGIONAL_INDICATORS = (0x1F1E6, 0x1F1FF)  # Regional indicator symbols A-Z

# Double-width emoji ranges
#
# Many emoji characters are classified as single-width according to Unicode
# Standard Annex #11 East Asian Width property (N or Neutral), but we
# deliberately override them to be double-width. References:
# 1. Unicode Technical Standard #51: Unicode Emoji
#    (https://www.unicode.org/reports/tr51/)
# 2. Principle of "emoji presentation" in WHATWG CSS Text specification
#    (https://drafts.csswg.org/css-text-3/#character-properties)
# 3. Terminal emulator implementations (iTerm2, Windows Terminal, etc.) which
#    universally render emoji as double-width characters regardless of their
#    Unicode EAW property
# 4. W3C Work Item: Requirements for Japanese Text Layout - Section 3.8.1
#    Emoji width (https://www.w3.org/TR/jlreq/)
EMOJI_RANGES = [
    (0x1F000, 0x1F02F),  # Mahjong Tiles (EAW: N, but displayed as double-width)
    (0x1F0A0, 0x1F0FF),  # Playing Cards (EAW: N, but displayed as double-width)
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

def create_width_tables():
    """
    Creates Unicode character width tables and returns the data structures.

    Returns:
        tuple: (zero_width_ranges, double_width_ranges)
    """

    # Width data mapping
    width_map = {}  # Maps code points to width (0, 1, 2)

    # Mark emoji modifiers as zero-width
    for start, end in EMOJI_ZERO_WIDTH:
        for cp in range(start, end + 1):
            width_map[cp] = 0

    # Mark all regional indicators as single-width as they are usually paired
    # providing a combined width of 2 when displayed together.
    start, end = REGIONAL_INDICATORS
    for cp in range(start, end + 1):
        width_map[cp] = 1

    # Process all assigned Unicode code points (Basic Multilingual Plane +
    # Supplementary Planes) Range 0x0 to 0x10FFFF (the full Unicode range)
    for block_start in range(0, 0x110000, 0x1000):
        block_end = block_start + 0x1000
        for cp in range(block_start, block_end):
            try:
                char = chr(cp)

                # Skip if already processed
                if cp in width_map:
                    continue

                # Check for combining marks and a format characters
                category = unicodedata.category(char)

                # Combining marks
                if category.startswith('M'):
                    width_map[cp] = 0
                    continue

                # Format characters
                # Since we have no support for bidirectional text, all format
                # characters (category Cf) can be treated with width 0 (zero)
                # for simplicity, as they don't need to occupy visual space
                # in a non-bidirectional text environment.
                if category == 'Cf':
                    width_map[cp] = 0
                    continue

                # Known zero-width characters
                if cp in KNOWN_ZERO_WIDTH:
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
    for start, end in EMOJI_RANGES:
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

    # Extract ranges for each width
    zero_width_ranges = ranges_optimize(width_map, 0)
    double_width_ranges = ranges_optimize(width_map, 2)

    return zero_width_ranges, double_width_ranges

def write_tables(zero_width_ranges, double_width_ranges, out_file=DEFAULT_OUT_FILE):
    """
    Write the generated tables to C header file.

    The output uses a single sorted-by-`first` table per region (BMP and
    non-BMP), with zero-width and double-width ranges merged together. The
    non-BMP table also hosts the BMP double-width bitmap in spare bits of
    `last`. See the encoding comment at the top of ucs.c for the layout.

    Args:
        zero_width_ranges: List of (start, end) ranges for zero-width characters
        double_width_ranges: List of (start, end) ranges for double-width characters
        out_file: Output file name (default: DEFAULT_OUT_FILE)
    """

    # Bits per BMP-bitmap chunk hosted in one non-BMP entry's `last` field.
    # 8 bits makes `idx / BITS_PER_CHUNK` / `idx % BITS_PER_CHUNK` compile to
    # a cheap shift+mask in the lookup. The chunk size is also emitted as
    # UCS_NONBMP_BMP_BITS in the generated header so ucs.c stays in sync.
    BITS_PER_CHUNK = 8

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

    # Split ranges into BMP and non-BMP
    zero_width_bmp, zero_width_non_bmp = split_ranges_by_size(zero_width_ranges)
    double_width_bmp, double_width_non_bmp = split_ranges_by_size(double_width_ranges)

    # Merge zero- and double-width ranges per region, tagging each with its
    # width, then sort by `first` so binary search works on the union.
    bmp_entries = sorted(
        [(s, e, 0) for s, e in zero_width_bmp] +
        [(s, e, 2) for s, e in double_width_bmp],
        key=lambda t: t[0])
    nonbmp_entries = sorted(
        [(s, e, 0) for s, e in zero_width_non_bmp] +
        [(s, e, 2) for s, e in double_width_non_bmp],
        key=lambda t: t[0])

    # Build the BMP double-width bitmap: one bit per BMP entry (in sort
    # order), set iff that entry is double-width. Pack into BITS_PER_CHUNK-
    # wide chunks, with bit j of the chunk corresponding to entry
    # (chunk_index * BITS_PER_CHUNK + j).
    bmp_w2_bits = [1 if w == 2 else 0 for _, _, w in bmp_entries]
    n_chunks = (len(bmp_w2_bits) + BITS_PER_CHUNK - 1) // BITS_PER_CHUNK

    if n_chunks > len(nonbmp_entries):
        raise RuntimeError(
            f"BMP bitmap needs {n_chunks} host entries, "
            f"but only {len(nonbmp_entries)} non-BMP entries are available")

    chunks = []  # list of (base_index, end_index, packed_value)
    for c in range(n_chunks):
        base = c * BITS_PER_CHUNK
        end_idx = min(base + BITS_PER_CHUNK - 1, len(bmp_w2_bits) - 1)
        value = 0
        for j in range(BITS_PER_CHUNK):
            k = base + j
            if k < len(bmp_w2_bits) and bmp_w2_bits[k]:
                value |= 1 << j
        chunks.append((base, end_idx, value))

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

    # Generate C tables
    with open(out_file, 'w') as f:
        f.write(f"""\
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * {out_file} - Unicode character width
 *
 * Auto-generated by {this_file}
 *
 * Unicode Version: {unicodedata.unidata_version}
 *
 * Zero-width and double-width ranges are merged into one sorted-by-`first`
 * table per region. The non-BMP table additionally hosts the BMP
 * double-width bitmap in the low {BITS_PER_CHUNK} bits of `last` of its
 * first {n_chunks} entries (covering {len(bmp_w2_bits)} BMP entries).
 * See ucs.c for the encoding details and the lookup code.
 */

/* Bits per BMP-bitmap chunk hosted in one non-BMP entry's `last` field. */
#define UCS_NONBMP_BMP_BITS {BITS_PER_CHUNK}

/* Combined zero- and double-width ranges
 * (BMP - Basic Multilingual Plane, U+0000 to U+FFFF). */
static const struct ucs_width16 ucs_bmp_ranges[] = {{
""")

        for s, e, w in bmp_entries:
            macro = "BMP_0WIDTH" if w == 0 else "BMP_2WIDTH"
            comment = get_code_point_comment(s, e)
            f.write(f"\t{{ {macro}(0x{s:04X}, 0x{e:04X}) }}, {comment}\n")

        f.write(f"""\
}};

/* Combined zero- and double-width ranges (non-BMP, U+10000 and above).
 * The first {n_chunks} entries host the BMP double-width bitmap in the low
 * {BITS_PER_CHUNK} bits of `last`. */
static const struct ucs_width32 ucs_nonbmp_ranges[] = {{
""")

        for i, (s, e, w) in enumerate(nonbmp_entries):
            macro = "RANGE_0WIDTH" if w == 0 else "RANGE_2WIDTH"
            comment = get_code_point_comment(s, e)
            if i < len(chunks):
                base, end_idx, value = chunks[i]
                f.write(
                    f"\t{{ {macro}(0x{s:05X}, 0x{e:05X})   {comment}\n"
                    f"\t  | BMP_2W_BITS(0b{value:0{BITS_PER_CHUNK}b}) }},"
                    f" /* BMP entries [{base:>3}..{end_idx:>3}] */\n")
            else:
                f.write(f"\t{{ {macro}(0x{s:05X}, 0x{e:05X}) }}, {comment}\n")

        f.write("};\n")

if __name__ == "__main__":
    # Parse command line arguments
    parser = argparse.ArgumentParser(description="Generate Unicode width tables")
    parser.add_argument("-o", "--output", dest="output_file", default=DEFAULT_OUT_FILE,
                        help=f"Output file name (default: {DEFAULT_OUT_FILE})")
    args = parser.parse_args()

    # Write tables to header file
    zero_width_ranges, double_width_ranges = create_width_tables()
    write_tables(zero_width_ranges, double_width_ranges, out_file=args.output_file)

    # Print summary
    zero_width_count = sum(end - start + 1 for start, end in zero_width_ranges)
    double_width_count = sum(end - start + 1 for start, end in double_width_ranges)
    n_zero = len(zero_width_ranges)
    n_double = len(double_width_ranges)
    print(f"Generated {args.output_file} with:")
    print(f"- {n_zero} zero-width ranges covering ~{zero_width_count} code points")
    print(f"- {n_double} double-width ranges covering ~{double_width_count} code points")
    print(f"- {n_zero + n_double} merged ranges total")
    print(f"- Unicode Version: {unicodedata.unidata_version}")
