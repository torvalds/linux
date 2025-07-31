#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Leverage Python's unicodedata module to generate ucs_recompose_table.h
#
# The generated table maps base character + combining mark pairs to their
# precomposed equivalents.
#
# Usage:
#   python3 gen_ucs_recompose_table.py         # Generate with common recomposition pairs
#   python3 gen_ucs_recompose_table.py --full  # Generate with all recomposition pairs

import unicodedata
import sys
import argparse
import textwrap

# This script's file name
from pathlib import Path
this_file = Path(__file__).name

# Default output file name
DEFAULT_OUT_FILE = "ucs_recompose_table.h"

common_recompose_description = "most commonly used Latin, Greek, and Cyrillic recomposition pairs only"
COMMON_RECOMPOSITION_PAIRS = [
    # Latin letters with accents - uppercase
    (0x0041, 0x0300, 0x00C0),  # A + COMBINING GRAVE ACCENT = LATIN CAPITAL LETTER A WITH GRAVE
    (0x0041, 0x0301, 0x00C1),  # A + COMBINING ACUTE ACCENT = LATIN CAPITAL LETTER A WITH ACUTE
    (0x0041, 0x0302, 0x00C2),  # A + COMBINING CIRCUMFLEX ACCENT = LATIN CAPITAL LETTER A WITH CIRCUMFLEX
    (0x0041, 0x0303, 0x00C3),  # A + COMBINING TILDE = LATIN CAPITAL LETTER A WITH TILDE
    (0x0041, 0x0308, 0x00C4),  # A + COMBINING DIAERESIS = LATIN CAPITAL LETTER A WITH DIAERESIS
    (0x0041, 0x030A, 0x00C5),  # A + COMBINING RING ABOVE = LATIN CAPITAL LETTER A WITH RING ABOVE
    (0x0043, 0x0327, 0x00C7),  # C + COMBINING CEDILLA = LATIN CAPITAL LETTER C WITH CEDILLA
    (0x0045, 0x0300, 0x00C8),  # E + COMBINING GRAVE ACCENT = LATIN CAPITAL LETTER E WITH GRAVE
    (0x0045, 0x0301, 0x00C9),  # E + COMBINING ACUTE ACCENT = LATIN CAPITAL LETTER E WITH ACUTE
    (0x0045, 0x0302, 0x00CA),  # E + COMBINING CIRCUMFLEX ACCENT = LATIN CAPITAL LETTER E WITH CIRCUMFLEX
    (0x0045, 0x0308, 0x00CB),  # E + COMBINING DIAERESIS = LATIN CAPITAL LETTER E WITH DIAERESIS
    (0x0049, 0x0300, 0x00CC),  # I + COMBINING GRAVE ACCENT = LATIN CAPITAL LETTER I WITH GRAVE
    (0x0049, 0x0301, 0x00CD),  # I + COMBINING ACUTE ACCENT = LATIN CAPITAL LETTER I WITH ACUTE
    (0x0049, 0x0302, 0x00CE),  # I + COMBINING CIRCUMFLEX ACCENT = LATIN CAPITAL LETTER I WITH CIRCUMFLEX
    (0x0049, 0x0308, 0x00CF),  # I + COMBINING DIAERESIS = LATIN CAPITAL LETTER I WITH DIAERESIS
    (0x004E, 0x0303, 0x00D1),  # N + COMBINING TILDE = LATIN CAPITAL LETTER N WITH TILDE
    (0x004F, 0x0300, 0x00D2),  # O + COMBINING GRAVE ACCENT = LATIN CAPITAL LETTER O WITH GRAVE
    (0x004F, 0x0301, 0x00D3),  # O + COMBINING ACUTE ACCENT = LATIN CAPITAL LETTER O WITH ACUTE
    (0x004F, 0x0302, 0x00D4),  # O + COMBINING CIRCUMFLEX ACCENT = LATIN CAPITAL LETTER O WITH CIRCUMFLEX
    (0x004F, 0x0303, 0x00D5),  # O + COMBINING TILDE = LATIN CAPITAL LETTER O WITH TILDE
    (0x004F, 0x0308, 0x00D6),  # O + COMBINING DIAERESIS = LATIN CAPITAL LETTER O WITH DIAERESIS
    (0x0055, 0x0300, 0x00D9),  # U + COMBINING GRAVE ACCENT = LATIN CAPITAL LETTER U WITH GRAVE
    (0x0055, 0x0301, 0x00DA),  # U + COMBINING ACUTE ACCENT = LATIN CAPITAL LETTER U WITH ACUTE
    (0x0055, 0x0302, 0x00DB),  # U + COMBINING CIRCUMFLEX ACCENT = LATIN CAPITAL LETTER U WITH CIRCUMFLEX
    (0x0055, 0x0308, 0x00DC),  # U + COMBINING DIAERESIS = LATIN CAPITAL LETTER U WITH DIAERESIS
    (0x0059, 0x0301, 0x00DD),  # Y + COMBINING ACUTE ACCENT = LATIN CAPITAL LETTER Y WITH ACUTE

    # Latin letters with accents - lowercase
    (0x0061, 0x0300, 0x00E0),  # a + COMBINING GRAVE ACCENT = LATIN SMALL LETTER A WITH GRAVE
    (0x0061, 0x0301, 0x00E1),  # a + COMBINING ACUTE ACCENT = LATIN SMALL LETTER A WITH ACUTE
    (0x0061, 0x0302, 0x00E2),  # a + COMBINING CIRCUMFLEX ACCENT = LATIN SMALL LETTER A WITH CIRCUMFLEX
    (0x0061, 0x0303, 0x00E3),  # a + COMBINING TILDE = LATIN SMALL LETTER A WITH TILDE
    (0x0061, 0x0308, 0x00E4),  # a + COMBINING DIAERESIS = LATIN SMALL LETTER A WITH DIAERESIS
    (0x0061, 0x030A, 0x00E5),  # a + COMBINING RING ABOVE = LATIN SMALL LETTER A WITH RING ABOVE
    (0x0063, 0x0327, 0x00E7),  # c + COMBINING CEDILLA = LATIN SMALL LETTER C WITH CEDILLA
    (0x0065, 0x0300, 0x00E8),  # e + COMBINING GRAVE ACCENT = LATIN SMALL LETTER E WITH GRAVE
    (0x0065, 0x0301, 0x00E9),  # e + COMBINING ACUTE ACCENT = LATIN SMALL LETTER E WITH ACUTE
    (0x0065, 0x0302, 0x00EA),  # e + COMBINING CIRCUMFLEX ACCENT = LATIN SMALL LETTER E WITH CIRCUMFLEX
    (0x0065, 0x0308, 0x00EB),  # e + COMBINING DIAERESIS = LATIN SMALL LETTER E WITH DIAERESIS
    (0x0069, 0x0300, 0x00EC),  # i + COMBINING GRAVE ACCENT = LATIN SMALL LETTER I WITH GRAVE
    (0x0069, 0x0301, 0x00ED),  # i + COMBINING ACUTE ACCENT = LATIN SMALL LETTER I WITH ACUTE
    (0x0069, 0x0302, 0x00EE),  # i + COMBINING CIRCUMFLEX ACCENT = LATIN SMALL LETTER I WITH CIRCUMFLEX
    (0x0069, 0x0308, 0x00EF),  # i + COMBINING DIAERESIS = LATIN SMALL LETTER I WITH DIAERESIS
    (0x006E, 0x0303, 0x00F1),  # n + COMBINING TILDE = LATIN SMALL LETTER N WITH TILDE
    (0x006F, 0x0300, 0x00F2),  # o + COMBINING GRAVE ACCENT = LATIN SMALL LETTER O WITH GRAVE
    (0x006F, 0x0301, 0x00F3),  # o + COMBINING ACUTE ACCENT = LATIN SMALL LETTER O WITH ACUTE
    (0x006F, 0x0302, 0x00F4),  # o + COMBINING CIRCUMFLEX ACCENT = LATIN SMALL LETTER O WITH CIRCUMFLEX
    (0x006F, 0x0303, 0x00F5),  # o + COMBINING TILDE = LATIN SMALL LETTER O WITH TILDE
    (0x006F, 0x0308, 0x00F6),  # o + COMBINING DIAERESIS = LATIN SMALL LETTER O WITH DIAERESIS
    (0x0075, 0x0300, 0x00F9),  # u + COMBINING GRAVE ACCENT = LATIN SMALL LETTER U WITH GRAVE
    (0x0075, 0x0301, 0x00FA),  # u + COMBINING ACUTE ACCENT = LATIN SMALL LETTER U WITH ACUTE
    (0x0075, 0x0302, 0x00FB),  # u + COMBINING CIRCUMFLEX ACCENT = LATIN SMALL LETTER U WITH CIRCUMFLEX
    (0x0075, 0x0308, 0x00FC),  # u + COMBINING DIAERESIS = LATIN SMALL LETTER U WITH DIAERESIS
    (0x0079, 0x0301, 0x00FD),  # y + COMBINING ACUTE ACCENT = LATIN SMALL LETTER Y WITH ACUTE
    (0x0079, 0x0308, 0x00FF),  # y + COMBINING DIAERESIS = LATIN SMALL LETTER Y WITH DIAERESIS

    # Common Greek characters
    (0x0391, 0x0301, 0x0386),  # Α + COMBINING ACUTE ACCENT = GREEK CAPITAL LETTER ALPHA WITH TONOS
    (0x0395, 0x0301, 0x0388),  # Ε + COMBINING ACUTE ACCENT = GREEK CAPITAL LETTER EPSILON WITH TONOS
    (0x0397, 0x0301, 0x0389),  # Η + COMBINING ACUTE ACCENT = GREEK CAPITAL LETTER ETA WITH TONOS
    (0x0399, 0x0301, 0x038A),  # Ι + COMBINING ACUTE ACCENT = GREEK CAPITAL LETTER IOTA WITH TONOS
    (0x039F, 0x0301, 0x038C),  # Ο + COMBINING ACUTE ACCENT = GREEK CAPITAL LETTER OMICRON WITH TONOS
    (0x03A5, 0x0301, 0x038E),  # Υ + COMBINING ACUTE ACCENT = GREEK CAPITAL LETTER UPSILON WITH TONOS
    (0x03A9, 0x0301, 0x038F),  # Ω + COMBINING ACUTE ACCENT = GREEK CAPITAL LETTER OMEGA WITH TONOS
    (0x03B1, 0x0301, 0x03AC),  # α + COMBINING ACUTE ACCENT = GREEK SMALL LETTER ALPHA WITH TONOS
    (0x03B5, 0x0301, 0x03AD),  # ε + COMBINING ACUTE ACCENT = GREEK SMALL LETTER EPSILON WITH TONOS
    (0x03B7, 0x0301, 0x03AE),  # η + COMBINING ACUTE ACCENT = GREEK SMALL LETTER ETA WITH TONOS
    (0x03B9, 0x0301, 0x03AF),  # ι + COMBINING ACUTE ACCENT = GREEK SMALL LETTER IOTA WITH TONOS
    (0x03BF, 0x0301, 0x03CC),  # ο + COMBINING ACUTE ACCENT = GREEK SMALL LETTER OMICRON WITH TONOS
    (0x03C5, 0x0301, 0x03CD),  # υ + COMBINING ACUTE ACCENT = GREEK SMALL LETTER UPSILON WITH TONOS
    (0x03C9, 0x0301, 0x03CE),  # ω + COMBINING ACUTE ACCENT = GREEK SMALL LETTER OMEGA WITH TONOS

    # Common Cyrillic characters
    (0x0418, 0x0306, 0x0419),  # И + COMBINING BREVE = CYRILLIC CAPITAL LETTER SHORT I
    (0x0438, 0x0306, 0x0439),  # и + COMBINING BREVE = CYRILLIC SMALL LETTER SHORT I
    (0x0423, 0x0306, 0x040E),  # У + COMBINING BREVE = CYRILLIC CAPITAL LETTER SHORT U
    (0x0443, 0x0306, 0x045E),  # у + COMBINING BREVE = CYRILLIC SMALL LETTER SHORT U
]

full_recompose_description = "all possible recomposition pairs from the Unicode BMP"
def collect_all_recomposition_pairs():
    """Collect all possible recomposition pairs from the Unicode data."""
    # Map to store recomposition pairs: (base, combining) -> recomposed
    recompose_map = {}

    # Process all assigned Unicode code points in BMP (Basic Multilingual Plane)
    # We limit to BMP (0x0000-0xFFFF) to keep our table smaller with uint16_t
    for cp in range(0, 0x10000):
        try:
            char = chr(cp)

            # Skip unassigned or control characters
            if not unicodedata.name(char, ''):
                continue

            # Find decomposition
            decomp = unicodedata.decomposition(char)
            if not decomp or '<' in decomp:  # Skip compatibility decompositions
                continue

            # Parse the decomposition
            parts = decomp.split()
            if len(parts) == 2:  # Simple base + combining mark
                base = int(parts[0], 16)
                combining = int(parts[1], 16)

                # Only store if both are in BMP
                if base < 0x10000 and combining < 0x10000:
                    recompose_map[(base, combining)] = cp

        except (ValueError, TypeError):
            continue

    # Convert to a list of tuples and sort for binary search
    recompose_list = [(base, combining, recomposed)
                     for (base, combining), recomposed in recompose_map.items()]
    recompose_list.sort()

    return recompose_list

def validate_common_pairs(full_list):
    """Validate that all common pairs are in the full list.

    Raises:
        ValueError: If any common pair is missing or has a different recomposition
        value than what's in the full table.
    """
    full_pairs = {(base, combining): recomposed for base, combining, recomposed in full_list}
    for base, combining, recomposed in COMMON_RECOMPOSITION_PAIRS:
        full_recomposed = full_pairs.get((base, combining))
        if full_recomposed is None:
            error_msg = f"Error: Common pair (0x{base:04X}, 0x{combining:04X}) not found in full data"
            print(error_msg)
            raise ValueError(error_msg)
        elif full_recomposed != recomposed:
            error_msg = (f"Error: Common pair (0x{base:04X}, 0x{combining:04X}) has different recomposition: "
                         f"0x{recomposed:04X} vs 0x{full_recomposed:04X}")
            print(error_msg)
            raise ValueError(error_msg)

def generate_recomposition_table(use_full_list=False, out_file=DEFAULT_OUT_FILE):
    """Generate the recomposition C table."""

    # Collect all recomposition pairs for validation
    full_recompose_list = collect_all_recomposition_pairs()

    # Decide which list to use
    if use_full_list:
        print("Using full recomposition list...")
        recompose_list = full_recompose_list
        table_description = full_recompose_description
        alt_list = COMMON_RECOMPOSITION_PAIRS
        alt_description = common_recompose_description
    else:
        print("Using common recomposition list...")
        # Validate that all common pairs are in the full list
        validate_common_pairs(full_recompose_list)
        recompose_list = sorted(COMMON_RECOMPOSITION_PAIRS)
        table_description = common_recompose_description
        alt_list = full_recompose_list
        alt_description = full_recompose_description
    generation_mode = " --full" if use_full_list else ""
    alternative_mode = " --full" if not use_full_list else ""
    table_description_detail = f"{table_description} ({len(recompose_list)} entries)"
    alt_description_detail = f"{alt_description} ({len(alt_list)} entries)"

    # Calculate min/max values for boundary checks
    min_base = min(base for base, _, _ in recompose_list)
    max_base = max(base for base, _, _ in recompose_list)
    min_combining = min(combining for _, combining, _ in recompose_list)
    max_combining = max(combining for _, combining, _ in recompose_list)

    # Generate implementation file
    with open(out_file, 'w') as f:
        f.write(f"""\
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * {out_file} - Unicode character recomposition
 *
 * Auto-generated by {this_file}{generation_mode}
 *
 * Unicode Version: {unicodedata.unidata_version}
 *
{textwrap.fill(
    f"This file contains a table with {table_description_detail}. " +
    f"To generate a table with {alt_description_detail} instead, run:",
    width=75, initial_indent=" * ", subsequent_indent=" * ")}
 *
 *   python3 {this_file}{alternative_mode}
 */

/*
 * Table of {table_description}
 * Sorted by base character and then combining mark for binary search
 */
static const struct ucs_recomposition ucs_recomposition_table[] = {{
""")

        for base, combining, recomposed in recompose_list:
            try:
                base_name = unicodedata.name(chr(base))
                combining_name = unicodedata.name(chr(combining))
                recomposed_name = unicodedata.name(chr(recomposed))
                comment = f"/* {base_name} + {combining_name} = {recomposed_name} */"
            except ValueError:
                comment = f"/* U+{base:04X} + U+{combining:04X} = U+{recomposed:04X} */"
            f.write(f"\t{{ 0x{base:04X}, 0x{combining:04X}, 0x{recomposed:04X} }}, {comment}\n")

        f.write(f"""\
}};

/*
 * Boundary values for quick rejection
 * These are calculated by analyzing the table during generation
 */
#define UCS_RECOMPOSE_MIN_BASE  0x{min_base:04X}
#define UCS_RECOMPOSE_MAX_BASE  0x{max_base:04X}
#define UCS_RECOMPOSE_MIN_MARK  0x{min_combining:04X}
#define UCS_RECOMPOSE_MAX_MARK  0x{max_combining:04X}
""")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate Unicode recomposition table")
    parser.add_argument("--full", action="store_true",
                        help="Generate a full recomposition table (default: common pairs only)")
    parser.add_argument("-o", "--output", dest="output_file", default=DEFAULT_OUT_FILE,
                        help=f"Output file name (default: {DEFAULT_OUT_FILE})")
    args = parser.parse_args()

    generate_recomposition_table(use_full_list=args.full, out_file=args.output_file)
