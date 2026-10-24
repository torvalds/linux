#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""
Derive the VKMS YCbCr -> RGB conversion constants and the vkms-format KUnit
reference values.

It produces:
  1. The S31.32 fixed-point conversion matrices used in
     drivers/gpu/drm/vkms/vkms_formats.c and asserted by
     vkms_format_test_conversion_matrix.
  2. The 16-bit reference YCbCr inputs for the limited-range
     yuv_u16_to_argb_u16 round-trip cases (the values changed by the fix).

Requires numpy.
"""

import numpy as np

FP = 1 << 32                       # S31.32 scale: fixed = round(coeff * 2^32)

# ITU-R luma weights (Kr, Kb); Kg = 1 - Kr - Kb.
ENCODINGS = {
    "BT601":  (0.299,  0.114),
    "BT709":  (0.2126, 0.0722),
    "BT2020": (0.2627, 0.0593),    # non-constant luminance
}

# 8-bit studio-range levels: luma 16..235 (range 219), chroma 16..240
# (range 224, neutral 128); full-range maximum is 2^n - 1 = 255.
FULL_MAX = 255
Y_RANGE  = 235 - 16                # 219
C_RANGE  = 240 - 16                # 224

# 16-bit RGB test colors.
COLORS = {
    "white": (0xffff, 0xffff, 0xffff),
    "gray":  (0x8080, 0x8080, 0x8080),
    "black": (0x0000, 0x0000, 0x0000),
    "red":   (0xffff, 0x0000, 0x0000),
    "green": (0x0000, 0xffff, 0x0000),
    "blue":  (0x0000, 0x0000, 0xffff),
}


def rgb_matrix(kr, kb, limited):
    """Normalized YCbCr -> RGB (Y in [0,1], Cb/Cr in [-0.5, 0.5])."""
    kg = 1.0 - kr - kb
    # Standard full-range relations.
    m = [[1.0,  0.0,                     2 * (1 - kr)],
         [1.0, -2 * (1 - kb) * kb / kg, -2 * (1 - kr) * kr / kg],
         [1.0,  2 * (1 - kb),            0.0]]
    if limited:
        # Expand studio input range to full range: luma by 255/219,
        # chroma by 255/224 (relative to full-range maximum 2^n - 1).
        ys, cs = FULL_MAX / Y_RANGE, FULL_MAX / C_RANGE
        m = [[r[0] * ys, r[1] * cs, r[2] * cs] for r in m]
    return m


def ref_yuv(m, y_offset):
    """16-bit YCbCr that VKMS decodes back to each RGB color.

    VKMS decode: [R,G,B] = M . [Y - y_offset*257, Cb - 128*257, Cr - 128*257]
    (8-bit offsets lifted to 16-bit by *257 = 65535/255), so invert and
    re-add the offsets.
    """
    inv = np.linalg.inv(np.array(m, float))
    off = np.array([y_offset * 257, 128 * 257, 128 * 257])
    return {name: tuple(int(min(max(round(v), 0), 0xffff))
                        for v in inv @ np.array(rgb, float) + off)
            for name, rgb in COLORS.items()}

print(f"== RGB REFERENCE VALUES ==")
for name, rgb in COLORS.items():
    print(f"    {name:6} {{ {rgb[0]:#06x}, {rgb[1]:#06x}, {rgb[2]:#06x} }}")
print()

for enc, (kr, kb) in ENCODINGS.items():
    for limited in (False, True):
        m = rgb_matrix(kr, kb, limited)
        y_offset = 16 if limited else 0
        print(f"== {enc} {'LIMITED' if limited else 'FULL'} (y_offset={y_offset}) ==")
        for row in m:
            print("    { " + ", ".join(str(round(v * FP)) for v in row) + " },")
        for name, yuv in ref_yuv(m, y_offset).items():
            print(f"    {name:6} {{ {yuv[0]:#06x}, {yuv[1]:#06x}, {yuv[2]:#06x} }}")
        print()
