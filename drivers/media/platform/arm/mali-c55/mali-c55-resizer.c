// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Mali-C55 ISP Driver - Image signal processor
 *
 * Copyright (C) 2025 Ideas on Board Oy
 */

#include <linux/math.h>
#include <linux/minmax.h>

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

#include "mali-c55-common.h"
#include "mali-c55-registers.h"

/* Scaling factor in Q4.20 format. */
#define MALI_C55_RSZ_SCALER_FACTOR	(1U << 20)

#define MALI_C55_RSZ_COEFS_BANKS	8
#define MALI_C55_RSZ_COEFS_ENTRIES	64

static inline struct mali_c55_resizer *
sd_to_mali_c55_rsz(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mali_c55_resizer, sd);
}

static const unsigned int
mali_c55_rsz_filter_coeffs_h[MALI_C55_RSZ_COEFS_BANKS]
			    [MALI_C55_RSZ_COEFS_ENTRIES] = {
	{	/* Bank 0 */
		0x24fc0000, 0x0000fc24, 0x27fc0000, 0x0000fc21,
		0x28fc0000, 0x0000fd1f, 0x2cfb0000, 0x0000fd1c,
		0x2efb0000, 0x0000fd1a, 0x30fb0000, 0x0000fe17,
		0x32fb0000, 0x0000fe15, 0x35fb0000, 0x0000fe12,
		0x35fc0000, 0x0000ff10, 0x37fc0000, 0x0000ff0e,
		0x39fc0000, 0x0000ff0c, 0x3afd0000, 0x0000ff0a,
		0x3afe0000, 0x00000008, 0x3cfe0000, 0x00000006,
		0x3dff0000, 0x00000004, 0x3d000000, 0x00000003,
		0x3c020000, 0x00000002, 0x3d030000, 0x00000000,
		0x3d040000, 0x000000ff, 0x3c060000, 0x000000fe,
		0x3a080000, 0x000000fe, 0x3a0aff00, 0x000000fd,
		0x390cff00, 0x000000fc, 0x370eff00, 0x000000fc,
		0x3510ff00, 0x000000fc, 0x3512fe00, 0x000000fb,
		0x3215fe00, 0x000000fb, 0x3017fe00, 0x000000fb,
		0x2e1afd00, 0x000000fb, 0x2c1cfd00, 0x000000fb,
		0x281ffd00, 0x000000fc, 0x2721fc00, 0x000000fc,
	},
	{	/* Bank 1 */
		0x25fb0000, 0x0000fb25, 0x27fb0000, 0x0000fb23,
		0x29fb0000, 0x0000fb21, 0x2afc0000, 0x0000fb1f,
		0x2cfc0000, 0x0000fb1d, 0x2efc0000, 0x0000fb1b,
		0x2ffd0000, 0x0000fb19, 0x2ffe0000, 0x0000fc17,
		0x31fe0000, 0x0000fc15, 0x32ff0000, 0x0000fc13,
		0x3400ff00, 0x0000fc11, 0x3301ff00, 0x0000fd10,
		0x3402ff00, 0x0000fd0e, 0x3503ff00, 0x0000fd0c,
		0x3505ff00, 0x0000fd0a, 0x3506fe00, 0x0000fe09,
		0x3607fe00, 0x0000fe07, 0x3509fe00, 0x0000fe06,
		0x350afd00, 0x0000ff05, 0x350cfd00, 0x0000ff03,
		0x340efd00, 0x0000ff02, 0x3310fd00, 0x0000ff01,
		0x3411fc00, 0x0000ff00, 0x3213fc00, 0x000000ff,
		0x3115fc00, 0x000000fe, 0x2f17fc00, 0x000000fe,
		0x2f19fb00, 0x000000fd, 0x2e1bfb00, 0x000000fc,
		0x2c1dfb00, 0x000000fc, 0x2a1ffb00, 0x000000fc,
		0x2921fb00, 0x000000fb, 0x2723fb00, 0x000000fb,
	},
	{	/* Bank 2 */
		0x1f010000, 0x0000011f, 0x21010000, 0x0000001e,
		0x21020000, 0x0000001d, 0x22020000, 0x0000001c,
		0x23030000, 0x0000ff1b, 0x2404ff00, 0x0000ff1a,
		0x2504ff00, 0x0000ff19, 0x2505ff00, 0x0000ff18,
		0x2606ff00, 0x0000fe17, 0x2607ff00, 0x0000fe16,
		0x2708ff00, 0x0000fe14, 0x2709ff00, 0x0000fe13,
		0x270aff00, 0x0000fe12, 0x280bfe00, 0x0000fe11,
		0x280cfe00, 0x0000fe10, 0x280dfe00, 0x0000fe0f,
		0x280efe00, 0x0000fe0e, 0x280ffe00, 0x0000fe0d,
		0x2810fe00, 0x0000fe0c, 0x2811fe00, 0x0000fe0b,
		0x2712fe00, 0x0000ff0a, 0x2713fe00, 0x0000ff09,
		0x2714fe00, 0x0000ff08, 0x2616fe00, 0x0000ff07,
		0x2617fe00, 0x0000ff06, 0x2518ff00, 0x0000ff05,
		0x2519ff00, 0x0000ff04, 0x241aff00, 0x0000ff04,
		0x231bff00, 0x00000003, 0x221c0000, 0x00000002,
		0x211d0000, 0x00000002, 0x211e0000, 0x00000001,
	},
	{	/* Bank 3 */
		0x1b06ff00, 0x00ff061b, 0x1b07ff00, 0x00ff061a,
		0x1c07ff00, 0x00ff051a, 0x1c08ff00, 0x00ff0519,
		0x1c09ff00, 0x00ff0419, 0x1d09ff00, 0x00ff0418,
		0x1e0aff00, 0x00ff0317, 0x1e0aff00, 0x00ff0317,
		0x1e0bff00, 0x00ff0316, 0x1f0cff00, 0x00ff0215,
		0x1e0cff00, 0x00000215, 0x1e0dff00, 0x00000214,
		0x1e0e0000, 0x00000113, 0x1e0e0000, 0x00000113,
		0x1e0f0000, 0x00000112, 0x1f100000, 0x00000011,
		0x20100000, 0x00000010, 0x1f110000, 0x00000010,
		0x1e120100, 0x0000000f, 0x1e130100, 0x0000000e,
		0x1e130100, 0x0000000e, 0x1e140200, 0x0000ff0d,
		0x1e150200, 0x0000ff0c, 0x1f1502ff, 0x0000ff0c,
		0x1e1603ff, 0x0000ff0b, 0x1e1703ff, 0x0000ff0a,
		0x1e1703ff, 0x0000ff0a, 0x1d1804ff, 0x0000ff09,
		0x1c1904ff, 0x0000ff09, 0x1c1905ff, 0x0000ff08,
		0x1c1a05ff, 0x0000ff07, 0x1b1a06ff, 0x0000ff07,
	},
	{	/* Bank 4 */
		0x17090000, 0x00000917, 0x18090000, 0x00000916,
		0x170a0100, 0x00000816, 0x170a0100, 0x00000816,
		0x180b0100, 0x00000715, 0x180b0100, 0x00000715,
		0x170c0100, 0x00000715, 0x190c0100, 0x00000614,
		0x180d0100, 0x00000614, 0x190d0200, 0x00000513,
		0x180e0200, 0x00000513, 0x180e0200, 0x00000513,
		0x1a0e0200, 0x00000412, 0x190f0200, 0x00000412,
		0x190f0300, 0x00000411, 0x18100300, 0x00000411,
		0x1a100300, 0x00000310, 0x18110400, 0x00000310,
		0x19110400, 0x0000030f, 0x19120400, 0x0000020f,
		0x1a120400, 0x0000020e, 0x18130500, 0x0000020e,
		0x18130500, 0x0000020e, 0x19130500, 0x0000020d,
		0x18140600, 0x0000010d, 0x19140600, 0x0000010c,
		0x17150700, 0x0000010c, 0x18150700, 0x0000010b,
		0x18150700, 0x0000010b, 0x17160800, 0x0000010a,
		0x17160800, 0x0000010a, 0x18160900, 0x00000009,
	},
	{	/* Bank 5 */
		0x120b0300, 0x00030b12, 0x120c0300, 0x00030b11,
		0x110c0400, 0x00030b11, 0x110c0400, 0x00030b11,
		0x130c0400, 0x00020a11, 0x120d0400, 0x00020a11,
		0x110d0500, 0x00020a11, 0x110d0500, 0x00020a11,
		0x130d0500, 0x00010911, 0x130e0500, 0x00010910,
		0x120e0600, 0x00010910, 0x120e0600, 0x00010910,
		0x130e0600, 0x00010810, 0x120f0600, 0x00010810,
		0x120f0700, 0x00000810, 0x130f0700, 0x0000080f,
		0x140f0700, 0x0000070f, 0x130f0800, 0x0000070f,
		0x12100800, 0x0000070f, 0x12100801, 0x0000060f,
		0x13100801, 0x0000060e, 0x12100901, 0x0000060e,
		0x12100901, 0x0000060e, 0x13100901, 0x0000050e,
		0x13110901, 0x0000050d, 0x11110a02, 0x0000050d,
		0x11110a02, 0x0000050d, 0x12110a02, 0x0000040d,
		0x13110a02, 0x0000040c, 0x11110b03, 0x0000040c,
		0x11110b03, 0x0000040c, 0x12110b03, 0x0000030c,
	},
	{	/* Bank 6 */
		0x0b0a0805, 0x00080a0c, 0x0b0a0805, 0x00080a0c,
		0x0c0a0805, 0x00080a0b, 0x0c0a0805, 0x00080a0b,
		0x0d0a0805, 0x00070a0b, 0x0d0a0805, 0x00070a0b,
		0x0d0a0805, 0x00070a0b, 0x0c0a0806, 0x00070a0b,
		0x0b0b0806, 0x00070a0b, 0x0c0b0806, 0x0007090b,
		0x0b0b0906, 0x0007090b, 0x0b0b0906, 0x0007090b,
		0x0b0b0906, 0x0007090b, 0x0b0b0906, 0x0007090b,
		0x0b0b0906, 0x0007090b, 0x0c0b0906, 0x0006090b,
		0x0c0b0906, 0x0006090b, 0x0c0b0906, 0x0006090b,
		0x0b0b0907, 0x0006090b, 0x0b0b0907, 0x0006090b,
		0x0b0b0907, 0x0006090b, 0x0b0b0907, 0x0006090b,
		0x0b0b0907, 0x0006090b, 0x0c0b0907, 0x0006080b,
		0x0b0b0a07, 0x0006080b, 0x0c0b0a07, 0x0006080a,
		0x0d0b0a07, 0x0005080a, 0x0d0b0a07, 0x0005080a,
		0x0d0b0a07, 0x0005080a, 0x0c0b0a08, 0x0005080a,
		0x0c0b0a08, 0x0005080a, 0x0c0b0a08, 0x0005080a,
	},
	{	/* Bank 7 */
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
		0x0909090a, 0x00090909, 0x0909090a, 0x00090909,
	}
};

static const unsigned int
mali_c55_rsz_filter_coeffs_v[MALI_C55_RSZ_COEFS_BANKS]
			    [MALI_C55_RSZ_COEFS_ENTRIES] = {
	{	/* Bank 0 */
		0x2424fc00, 0x000000fc, 0x2721fc00, 0x000000fc,
		0x281ffd00, 0x000000fc, 0x2c1cfd00, 0x000000fb,
		0x2e1afd00, 0x000000fb, 0x3017fe00, 0x000000fb,
		0x3215fe00, 0x000000fb, 0x3512fe00, 0x000000fb,
		0x3510ff00, 0x000000fc, 0x370eff00, 0x000000fc,
		0x390cff00, 0x000000fc, 0x3a0aff00, 0x000000fd,
		0x3a080000, 0x000000fe, 0x3c060000, 0x000000fe,
		0x3d040000, 0x000000ff, 0x3d030000, 0x00000000,
		0x3c020000, 0x00000002, 0x3d000000, 0x00000003,
		0x3dff0000, 0x00000004, 0x3cfe0000, 0x00000006,
		0x3afe0000, 0x00000008, 0x3afd0000, 0x0000ff0a,
		0x39fc0000, 0x0000ff0c, 0x37fc0000, 0x0000ff0e,
		0x35fc0000, 0x0000ff10, 0x35fb0000, 0x0000fe12,
		0x32fb0000, 0x0000fe15, 0x30fb0000, 0x0000fe17,
		0x2efb0000, 0x0000fd1a, 0x2cfb0000, 0x0000fd1c,
		0x28fc0000, 0x0000fd1f, 0x27fc0000, 0x0000fc21,
	},
	{	/* Bank 1 */
		0x2525fb00, 0x000000fb, 0x2723fb00, 0x000000fb,
		0x2921fb00, 0x000000fb, 0x2a1ffb00, 0x000000fc,
		0x2c1dfb00, 0x000000fc, 0x2e1bfb00, 0x000000fc,
		0x2f19fb00, 0x000000fd, 0x2f17fc00, 0x000000fe,
		0x3115fc00, 0x000000fe, 0x3213fc00, 0x000000ff,
		0x3411fc00, 0x0000ff00, 0x3310fd00, 0x0000ff01,
		0x340efd00, 0x0000ff02, 0x350cfd00, 0x0000ff03,
		0x350afd00, 0x0000ff05, 0x3509fe00, 0x0000fe06,
		0x3607fe00, 0x0000fe07, 0x3506fe00, 0x0000fe09,
		0x3505ff00, 0x0000fd0a, 0x3503ff00, 0x0000fd0c,
		0x3402ff00, 0x0000fd0e, 0x3301ff00, 0x0000fd10,
		0x3400ff00, 0x0000fc11, 0x32ff0000, 0x0000fc13,
		0x31fe0000, 0x0000fc15, 0x2ffe0000, 0x0000fc17,
		0x2ffd0000, 0x0000fb19, 0x2efc0000, 0x0000fb1b,
		0x2cfc0000, 0x0000fb1d, 0x2afc0000, 0x0000fb1f,
		0x29fb0000, 0x0000fb21, 0x27fb0000, 0x0000fb23,
	},
	{	/* Bank 2 */
		0x1f1f0100, 0x00000001, 0x211e0000, 0x00000001,
		0x211d0000, 0x00000002, 0x221c0000, 0x00000002,
		0x231bff00, 0x00000003, 0x241aff00, 0x0000ff04,
		0x2519ff00, 0x0000ff04, 0x2518ff00, 0x0000ff05,
		0x2617fe00, 0x0000ff06, 0x2616fe00, 0x0000ff07,
		0x2714fe00, 0x0000ff08, 0x2713fe00, 0x0000ff09,
		0x2712fe00, 0x0000ff0a, 0x2811fe00, 0x0000fe0b,
		0x2810fe00, 0x0000fe0c, 0x280ffe00, 0x0000fe0d,
		0x280efe00, 0x0000fe0e, 0x280dfe00, 0x0000fe0f,
		0x280cfe00, 0x0000fe10, 0x280bfe00, 0x0000fe11,
		0x270aff00, 0x0000fe12, 0x2709ff00, 0x0000fe13,
		0x2708ff00, 0x0000fe14, 0x2607ff00, 0x0000fe16,
		0x2606ff00, 0x0000fe17, 0x2505ff00, 0x0000ff18,
		0x2504ff00, 0x0000ff19, 0x2404ff00, 0x0000ff1a,
		0x23030000, 0x0000ff1b, 0x22020000, 0x0000001c,
		0x21020000, 0x0000001d, 0x21010000, 0x0000001e,
	},
	{	/* Bank 3 */
		0x1b1b06ff, 0x0000ff06, 0x1b1a06ff, 0x0000ff07,
		0x1c1a05ff, 0x0000ff07, 0x1c1905ff, 0x0000ff08,
		0x1c1904ff, 0x0000ff09, 0x1d1804ff, 0x0000ff09,
		0x1e1703ff, 0x0000ff0a, 0x1e1703ff, 0x0000ff0a,
		0x1e1603ff, 0x0000ff0b, 0x1f1502ff, 0x0000ff0c,
		0x1e150200, 0x0000ff0c, 0x1e140200, 0x0000ff0d,
		0x1e130100, 0x0000000e, 0x1e130100, 0x0000000e,
		0x1e120100, 0x0000000f, 0x1f110000, 0x00000010,
		0x20100000, 0x00000010, 0x1f100000, 0x00000011,
		0x1e0f0000, 0x00000112, 0x1e0e0000, 0x00000113,
		0x1e0e0000, 0x00000113, 0x1e0dff00, 0x00000214,
		0x1e0cff00, 0x00000215, 0x1f0cff00, 0x00ff0215,
		0x1e0bff00, 0x00ff0316, 0x1e0aff00, 0x00ff0317,
		0x1e0aff00, 0x00ff0317, 0x1d09ff00, 0x00ff0418,
		0x1c09ff00, 0x00ff0419, 0x1c08ff00, 0x00ff0519,
		0x1c07ff00, 0x00ff051a, 0x1b07ff00, 0x00ff061a,
	},
	{	/* Bank 4 */
		0x17170900, 0x00000009, 0x18160900, 0x00000009,
		0x17160800, 0x0000010a, 0x17160800, 0x0000010a,
		0x18150700, 0x0000010b, 0x18150700, 0x0000010b,
		0x17150700, 0x0000010c, 0x19140600, 0x0000010c,
		0x18140600, 0x0000010d, 0x19130500, 0x0000020d,
		0x18130500, 0x0000020e, 0x18130500, 0x0000020e,
		0x1a120400, 0x0000020e, 0x19120400, 0x0000020f,
		0x19110400, 0x0000030f, 0x18110400, 0x00000310,
		0x1a100300, 0x00000310, 0x18100300, 0x00000411,
		0x190f0300, 0x00000411, 0x190f0200, 0x00000412,
		0x1a0e0200, 0x00000412, 0x180e0200, 0x00000513,
		0x180e0200, 0x00000513, 0x190d0200, 0x00000513,
		0x180d0100, 0x00000614, 0x190c0100, 0x00000614,
		0x170c0100, 0x00000715, 0x180b0100, 0x00000715,
		0x180b0100, 0x00000715, 0x170a0100, 0x00000816,
		0x170a0100, 0x00000816, 0x18090000, 0x00000916,
	},
	{	/* Bank 5 */
		0x12120b03, 0x0000030b, 0x12110b03, 0x0000030c,
		0x11110b03, 0x0000040c, 0x11110b03, 0x0000040c,
		0x13110a02, 0x0000040c, 0x12110a02, 0x0000040d,
		0x11110a02, 0x0000050d, 0x11110a02, 0x0000050d,
		0x13110901, 0x0000050d, 0x13100901, 0x0000050e,
		0x12100901, 0x0000060e, 0x12100901, 0x0000060e,
		0x13100801, 0x0000060e, 0x12100801, 0x0000060f,
		0x12100800, 0x0000070f, 0x130f0800, 0x0000070f,
		0x140f0700, 0x0000070f, 0x130f0700, 0x0000080f,
		0x120f0700, 0x00000810, 0x120f0600, 0x00010810,
		0x130e0600, 0x00010810, 0x120e0600, 0x00010910,
		0x120e0600, 0x00010910, 0x130e0500, 0x00010910,
		0x130d0500, 0x00010911, 0x110d0500, 0x00020a11,
		0x110d0500, 0x00020a11, 0x120d0400, 0x00020a11,
		0x130c0400, 0x00020a11, 0x110c0400, 0x00030b11,
		0x110c0400, 0x00030b11, 0x120c0300, 0x00030b11,
	},
	{	/* Bank 6 */
		0x0b0c0a08, 0x0005080a, 0x0b0c0a08, 0x0005080a,
		0x0c0b0a08, 0x0005080a, 0x0c0b0a08, 0x0005080a,
		0x0d0b0a07, 0x0005080a, 0x0d0b0a07, 0x0005080a,
		0x0d0b0a07, 0x0005080a, 0x0c0b0a07, 0x0006080a,
		0x0b0b0a07, 0x0006080b, 0x0c0b0907, 0x0006080b,
		0x0b0b0907, 0x0006090b, 0x0b0b0907, 0x0006090b,
		0x0b0b0907, 0x0006090b, 0x0b0b0907, 0x0006090b,
		0x0b0b0907, 0x0006090b, 0x0c0b0906, 0x0006090b,
		0x0c0b0906, 0x0006090b, 0x0c0b0906, 0x0006090b,
		0x0b0b0906, 0x0007090b, 0x0b0b0906, 0x0007090b,
		0x0b0b0906, 0x0007090b, 0x0b0b0906, 0x0007090b,
		0x0b0b0906, 0x0007090b, 0x0c0b0806, 0x0007090b,
		0x0b0b0806, 0x00070a0b, 0x0c0a0806, 0x00070a0b,
		0x0d0a0805, 0x00070a0b, 0x0d0a0805, 0x00070a0b,
		0x0d0a0805, 0x00070a0b, 0x0c0a0805, 0x00080a0b,
		0x0c0a0805, 0x00080a0b, 0x0c0a0805, 0x00080a0b,
	},
	{	/* Bank 7 */
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
		0x09090909, 0x000a0909, 0x09090909, 0x000a0909,
	}
};

static const unsigned int mali_c55_rsz_coef_banks_range_start[] = {
	770, 600, 460, 354, 273, 210, 162, 125
};

/*
 * Select the right filter coefficients bank based on the scaler input and the
 * scaler output sizes ratio, set by the v4l2 crop and scale selection
 * rectangles respectively.
 */
static unsigned int mali_c55_rsz_calculate_bank(struct mali_c55 *mali_c55,
						unsigned int rsz_in,
						unsigned int rsz_out)
{
	unsigned int rsz_ratio = (rsz_out * 1000U) / rsz_in;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mali_c55_rsz_coef_banks_range_start); i++)
		if (rsz_ratio >= mali_c55_rsz_coef_banks_range_start[i])
			break;

	return i;
}

static const u32 rsz_non_bypass_src_fmts[] = {
	MEDIA_BUS_FMT_RGB121212_1X36,
	MEDIA_BUS_FMT_YUV10_1X30
};

static void mali_c55_resizer_program_coefficients(struct mali_c55_resizer *rsz)
{
	struct mali_c55 *mali_c55 = rsz->mali_c55;
	unsigned int haddr = rsz->id == MALI_C55_RSZ_FR ?
			     MALI_C55_REG_FR_SCALER_HFILT :
			     MALI_C55_REG_DS_SCALER_HFILT;
	unsigned int vaddr = rsz->id == MALI_C55_RSZ_FR ?
			     MALI_C55_REG_FR_SCALER_VFILT :
			     MALI_C55_REG_DS_SCALER_VFILT;

	for (unsigned int i = 0; i < MALI_C55_RSZ_COEFS_BANKS; i++) {
		for (unsigned int j = 0; j < MALI_C55_RSZ_COEFS_ENTRIES; j++) {
			mali_c55_write(mali_c55, haddr,
				       mali_c55_rsz_filter_coeffs_h[i][j]);
			mali_c55_write(mali_c55, vaddr,
				       mali_c55_rsz_filter_coeffs_v[i][j]);

			haddr += sizeof(u32);
			vaddr += sizeof(u32);
		}
	}
}

static int mali_c55_rsz_program_crop(struct mali_c55_resizer *rsz,
				     const struct v4l2_subdev_state *state)
{
	const struct v4l2_mbus_framefmt *fmt;
	const struct v4l2_rect *crop;

	/* Verify if crop should be enabled. */
	fmt = v4l2_subdev_state_get_format(state, MALI_C55_RSZ_SINK_PAD, 0);
	crop = v4l2_subdev_state_get_crop(state, MALI_C55_RSZ_SINK_PAD, 0);

	if (fmt->width == crop->width && fmt->height == crop->height)
		return MALI_C55_BYPASS_CROP;

	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_CROP_X_START,
			       crop->left);
	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_CROP_Y_START,
			       crop->top);
	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_CROP_X_SIZE,
			       crop->width);
	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_CROP_Y_SIZE,
			       crop->height);

	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_CROP_EN,
			       MALI_C55_CROP_ENABLE);

	return 0;
}

static int mali_c55_rsz_program_resizer(struct mali_c55_resizer *rsz,
					struct v4l2_subdev_state *state)
{
	struct mali_c55 *mali_c55 = rsz->mali_c55;
	const struct v4l2_rect *crop, *scale;
	unsigned int h_bank, v_bank;
	u64 h_scale, v_scale;

	/* Verify if scaling should be enabled. */
	crop = v4l2_subdev_state_get_crop(state, MALI_C55_RSZ_SINK_PAD, 0);
	scale = v4l2_subdev_state_get_compose(state, MALI_C55_RSZ_SINK_PAD, 0);

	if (crop->width == scale->width && crop->height == scale->height)
		return MALI_C55_BYPASS_SCALER;

	/* Program the scaler coefficients if the scaler is in use. */
	mali_c55_resizer_program_coefficients(rsz);

	/* Program the V/H scaling factor in Q4.20 format. */
	h_scale = crop->width * MALI_C55_RSZ_SCALER_FACTOR;
	v_scale = crop->height * MALI_C55_RSZ_SCALER_FACTOR;

	do_div(h_scale, scale->width);
	do_div(v_scale, scale->height);

	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_SCALER_IN_WIDTH,
			       crop->width);
	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_SCALER_IN_HEIGHT,
			       crop->height);

	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_SCALER_OUT_WIDTH,
			       scale->width);
	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_SCALER_OUT_HEIGHT,
			       scale->height);

	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_SCALER_HFILT_TINC,
			       h_scale);
	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_SCALER_VFILT_TINC,
			       v_scale);

	/* Select the scaler coefficients bank to use. */
	h_bank = mali_c55_rsz_calculate_bank(mali_c55, crop->width,
					     scale->width);
	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_SCALER_HFILT_COEF,
			       h_bank);

	v_bank = mali_c55_rsz_calculate_bank(mali_c55, crop->height,
					     scale->height);
	mali_c55_cap_dev_write(rsz->cap_dev, MALI_C55_REG_SCALER_VFILT_COEF,
			       v_bank);

	return 0;
}

static void mali_c55_rsz_program(struct mali_c55_resizer *rsz,
				 struct v4l2_subdev_state *state)
{
	struct mali_c55 *mali_c55 = rsz->mali_c55;
	u32 bypass = 0;

	/* Verify if cropping and scaling should be enabled. */
	bypass |= mali_c55_rsz_program_crop(rsz, state);
	bypass |= mali_c55_rsz_program_resizer(rsz, state);

	mali_c55_ctx_update_bits(mali_c55, rsz->id == MALI_C55_RSZ_FR ?
				 MALI_C55_REG_FR_BYPASS : MALI_C55_REG_DS_BYPASS,
				 MALI_C55_BYPASS_CROP | MALI_C55_BYPASS_SCALER,
				 bypass);
}

/*
 * Inspect the routing table to know which of the two (mutually exclusive)
 * routes is enabled and return the sink pad id of the active route.
 */
static unsigned int mali_c55_rsz_get_active_sink(struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_krouting *routing = &state->routing;
	struct v4l2_subdev_route *route;

	/* A single route is enabled at a time. */
	for_each_active_route(routing, route)
		return route->sink_pad;

	return MALI_C55_RSZ_SINK_PAD;
}

/*
 * When operating in bypass mode, the ISP takes input in a 20-bit format, but
 * can only output 16-bit RAW bayer data (with the 4 least significant bits from
 * the input being lost). Return the 16-bit version of the 20-bit input formats.
 */
static u32 mali_c55_rsz_shift_mbus_code(u32 mbus_code)
{
	const struct mali_c55_isp_format_info *fmt =
		mali_c55_isp_get_mbus_config_by_code(mbus_code);

	if (!fmt)
		return -EINVAL;

	return fmt->shifted_code;
}

static int __mali_c55_rsz_set_routing(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      const struct v4l2_subdev_krouting *routing)
{
	struct mali_c55_resizer *rsz = sd_to_mali_c55_rsz(sd);
	unsigned int active_sink = UINT_MAX;
	struct v4l2_mbus_framefmt *src_fmt;
	struct v4l2_subdev_route *route;
	unsigned int active_routes = 0;
	struct v4l2_mbus_framefmt *fmt;
	int ret;

	ret = v4l2_subdev_routing_validate(sd, routing, 0);
	if (ret)
		return ret;

	/* Only a single route can be enabled at a time. */
	for_each_active_route(routing, route) {
		if (++active_routes > 1) {
			dev_dbg(rsz->mali_c55->dev,
				"Only one route can be active");
			return -EINVAL;
		}

		active_sink = route->sink_pad;
	}
	if (active_sink == UINT_MAX) {
		dev_dbg(rsz->mali_c55->dev, "One route has to be active");
		return -EINVAL;
	}

	ret = v4l2_subdev_set_routing(sd, state, routing);
	if (ret) {
		dev_dbg(rsz->mali_c55->dev, "Failed to set routing\n");
		return ret;
	}

	fmt = v4l2_subdev_state_get_format(state, active_sink, 0);
	fmt->width = MALI_C55_DEFAULT_WIDTH;
	fmt->height = MALI_C55_DEFAULT_HEIGHT;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(false,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->field = V4L2_FIELD_NONE;

	if (active_sink == MALI_C55_RSZ_SINK_PAD) {
		struct v4l2_rect *crop, *compose;

		fmt->code = MEDIA_BUS_FMT_RGB121212_1X36;

		crop = v4l2_subdev_state_get_crop(state, active_sink, 0);
		compose = v4l2_subdev_state_get_compose(state, active_sink, 0);

		crop->left = 0;
		crop->top = 0;
		crop->width = MALI_C55_DEFAULT_WIDTH;
		crop->height = MALI_C55_DEFAULT_HEIGHT;

		*compose = *crop;
	} else {
		fmt->code = MEDIA_BUS_FMT_SRGGB20_1X20;
	}

	/* Propagate the format to the source pad */
	src_fmt = v4l2_subdev_state_get_format(state, MALI_C55_RSZ_SOURCE_PAD,
					       0);
	*src_fmt = *fmt;

	/* In the event this is the bypass pad the mbus code needs correcting */
	if (active_sink == MALI_C55_RSZ_SINK_BYPASS_PAD)
		src_fmt->code = mali_c55_rsz_shift_mbus_code(src_fmt->code);

	return 0;
}

static int mali_c55_rsz_enum_mbus_code(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_mbus_code_enum *code)
{
	const struct mali_c55_isp_format_info *fmt;
	struct v4l2_mbus_framefmt *sink_fmt;
	u32 sink_pad;

	switch (code->pad) {
	case MALI_C55_RSZ_SINK_PAD:
		if (code->index)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_RGB121212_1X36;

		return 0;
	case MALI_C55_RSZ_SOURCE_PAD:
		sink_pad = mali_c55_rsz_get_active_sink(state);
		sink_fmt = v4l2_subdev_state_get_format(state, sink_pad, 0);

		/*
		 * If the active route is from the Bypass sink pad, then the
		 * source pad is a simple passthrough of the sink format,
		 * downshifted to 16-bits.
		 */

		if (sink_pad == MALI_C55_RSZ_SINK_BYPASS_PAD) {
			if (code->index)
				return -EINVAL;

			code->code = mali_c55_rsz_shift_mbus_code(sink_fmt->code);
			if (!code->code)
				return -EINVAL;

			return 0;
		}

		/*
		 * If the active route is from the non-bypass sink then we can
		 * select either RGB or conversion to YUV.
		 */

		if (code->index >= ARRAY_SIZE(rsz_non_bypass_src_fmts))
			return -EINVAL;

		code->code = rsz_non_bypass_src_fmts[code->index];

		return 0;
	case MALI_C55_RSZ_SINK_BYPASS_PAD:
		fmt = mali_c55_isp_get_mbus_config_by_index(code->index);
		if (fmt) {
			code->code = fmt->code;
			return 0;
		}

		break;
	}

	return -EINVAL;
}

static int mali_c55_rsz_enum_frame_size(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *state,
					struct v4l2_subdev_frame_size_enum *fse)
{
	const struct mali_c55_isp_format_info *fmt;
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_rect *compose;
	u32 sink_pad;

	switch (fse->pad) {
	case MALI_C55_RSZ_SINK_PAD:
		if (fse->index || fse->code != MEDIA_BUS_FMT_RGB121212_1X36)
			return -EINVAL;

		fse->max_width = MALI_C55_MAX_WIDTH;
		fse->max_height = MALI_C55_MAX_HEIGHT;
		fse->min_width = MALI_C55_MIN_WIDTH;
		fse->min_height = MALI_C55_MIN_HEIGHT;

		return 0;
	case MALI_C55_RSZ_SOURCE_PAD:
		sink_pad = mali_c55_rsz_get_active_sink(state);
		sink_fmt = v4l2_subdev_state_get_format(state, sink_pad, 0);

		if (sink_pad == MALI_C55_RSZ_SINK_BYPASS_PAD) {
			if (fse->index)
				return -EINVAL;

			fmt = mali_c55_isp_get_mbus_config_by_shifted_code(fse->code);
			if (!fmt)
				return -EINVAL;

			fse->min_width = sink_fmt->width;
			fse->max_width = sink_fmt->width;
			fse->min_height = sink_fmt->height;
			fse->max_height = sink_fmt->height;

			return 0;
		}

		if ((fse->code != MEDIA_BUS_FMT_RGB121212_1X36 &&
		     fse->code != MEDIA_BUS_FMT_YUV10_1X30) || fse->index > 1)
			return -EINVAL;

		compose = v4l2_subdev_state_get_compose(state,
							MALI_C55_RSZ_SINK_PAD,
							0);

		fse->min_width = compose->width;
		fse->max_width = compose->width;
		fse->min_height = compose->height;
		fse->max_height = compose->height;

		return 0;
	case MALI_C55_RSZ_SINK_BYPASS_PAD:
		fmt = mali_c55_isp_get_mbus_config_by_code(fse->code);
		if (fse->index || !fmt)
			return -EINVAL;

		fse->max_width = MALI_C55_MAX_WIDTH;
		fse->max_height = MALI_C55_MAX_HEIGHT;
		fse->min_width = MALI_C55_MIN_WIDTH;
		fse->min_height = MALI_C55_MIN_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

static int mali_c55_rsz_set_sink_fmt(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct v4l2_mbus_framefmt *sink_fmt;
	unsigned int active_sink;
	struct v4l2_rect *rect;

	sink_fmt = v4l2_subdev_state_get_format(state, format->pad, 0);

	/*
	 * Clamp to min/max and then reset crop and compose rectangles to the
	 * newly applied size.
	 */
	sink_fmt->width = clamp_t(unsigned int, fmt->width,
				  MALI_C55_MIN_WIDTH, MALI_C55_MAX_WIDTH);
	sink_fmt->height = clamp_t(unsigned int, fmt->height,
				   MALI_C55_MIN_HEIGHT, MALI_C55_MAX_HEIGHT);

	/*
	 * Make sure the media bus code for the bypass pad is one of the
	 * supported ISP input media bus codes. Default it to SRGGB otherwise.
	 */
	if (format->pad == MALI_C55_RSZ_SINK_BYPASS_PAD)
		sink_fmt->code = mali_c55_isp_get_mbus_config_by_code(fmt->code) ?
				 fmt->code : MEDIA_BUS_FMT_SRGGB20_1X20;

	*fmt = *sink_fmt;

	if (format->pad == MALI_C55_RSZ_SINK_PAD) {
		rect = v4l2_subdev_state_get_crop(state, format->pad);
		rect->left = 0;
		rect->top = 0;
		rect->width = fmt->width;
		rect->height = fmt->height;

		rect = v4l2_subdev_state_get_compose(state, format->pad);
		rect->left = 0;
		rect->top = 0;
		rect->width = fmt->width;
		rect->height = fmt->height;
	}

	/* If format->pad is routed to the source pad, propagate the format. */
	active_sink = mali_c55_rsz_get_active_sink(state);
	if (active_sink == format->pad) {
		/* If the bypass route is used, downshift the code to 16bpp. */
		if (active_sink == MALI_C55_RSZ_SINK_BYPASS_PAD)
			fmt->code = mali_c55_rsz_shift_mbus_code(fmt->code);

		*v4l2_subdev_state_get_format(state,
					      MALI_C55_RSZ_SOURCE_PAD, 0) = *fmt;
	}

	return 0;
}

static int mali_c55_rsz_set_source_fmt(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct v4l2_mbus_framefmt *sink_fmt, *src_fmt;
	unsigned int active_sink;

	active_sink = mali_c55_rsz_get_active_sink(state);
	sink_fmt = v4l2_subdev_state_get_format(state, active_sink, 0);
	src_fmt = v4l2_subdev_state_get_format(state, MALI_C55_RSZ_SOURCE_PAD);

	if (active_sink == MALI_C55_RSZ_SINK_PAD) {
		/*
		 * Regular processing pipe: RGB121212 can be color-space
		 * converted to YUV101010.
		 */
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(rsz_non_bypass_src_fmts); i++) {
			if (fmt->code == rsz_non_bypass_src_fmts[i])
				break;
		}

		src_fmt->code = i == ARRAY_SIZE(rsz_non_bypass_src_fmts) ?
				MEDIA_BUS_FMT_RGB121212_1X36 : fmt->code;
	} else {
		/*
		 * Bypass pipe: the source format is the same as the bypass
		 * sink pad downshifted to 16bpp.
		 */
		fmt->code = mali_c55_rsz_shift_mbus_code(sink_fmt->code);
	}

	*fmt = *src_fmt;

	return 0;
}

static int mali_c55_rsz_set_fmt(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_format *format)
{
	/*
	 * On sink pads fmt is either fixed for the 'regular' processing
	 * pad or a RAW format or 20-bit wide RGB/YUV format for the FR bypass
	 * pad.
	 *
	 * On source pad sizes are the result of crop+compose on the sink
	 * pad sizes, while the format depends on the active route.
	 */

	if (format->pad == MALI_C55_RSZ_SINK_PAD ||
	    format->pad == MALI_C55_RSZ_SINK_BYPASS_PAD)
		return mali_c55_rsz_set_sink_fmt(sd, state, format);

	return mali_c55_rsz_set_source_fmt(sd, state, format);
}

static int mali_c55_rsz_get_selection(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_selection *sel)
{
	if (sel->pad != MALI_C55_RSZ_SINK_PAD)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	sel->r = sel->target == V4L2_SEL_TGT_CROP
	       ? *v4l2_subdev_state_get_crop(state, MALI_C55_RSZ_SINK_PAD)
	       : *v4l2_subdev_state_get_compose(state, MALI_C55_RSZ_SINK_PAD);

	return 0;
}

static int mali_c55_rsz_set_crop(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_selection *sel)
{
	struct mali_c55_resizer *rsz = sd_to_mali_c55_rsz(sd);
	struct v4l2_mbus_framefmt *sink_fmt, *src_fmt;
	struct v4l2_rect *crop, *compose;

	sink_fmt = v4l2_subdev_state_get_format(state, MALI_C55_RSZ_SINK_PAD);
	crop = v4l2_subdev_state_get_crop(state, MALI_C55_RSZ_SINK_PAD);
	compose = v4l2_subdev_state_get_compose(state, MALI_C55_RSZ_SINK_PAD);

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE &&
	    v4l2_subdev_is_streaming(sd)) {
		/*
		 * At runtime the compose rectangle and output size cannot be
		 * changed so we need to clamp the crop rectangle such that the
		 * compose rectangle can fit within it.
		 */
		crop->left = clamp_t(unsigned int, sel->r.left, 0,
				     sink_fmt->width - compose->width);
		crop->top = clamp_t(unsigned int, sel->r.top, 0,
				    sink_fmt->height - compose->height);
		crop->width = clamp_t(unsigned int, sel->r.width, compose->width,
				      sink_fmt->width - crop->left);
		crop->height = clamp_t(unsigned int, sel->r.height, compose->height,
				       sink_fmt->height - crop->top);

		mali_c55_rsz_program(rsz, state);
	} else {
		/*
		 * If we're not streaming we can utilise the ISP's full range
		 * and simply need to propagate the selected rectangle to the
		 * compose target and source pad format.
		 */
		crop->left = clamp_t(unsigned int, sel->r.left, 0,
				     sink_fmt->width);
		crop->top = clamp_t(unsigned int, sel->r.top, 0,
				    sink_fmt->height);
		crop->width = clamp_t(unsigned int, sel->r.width,
				      MALI_C55_MIN_WIDTH,
				      sink_fmt->width - crop->left);
		crop->height = clamp_t(unsigned int, sel->r.height,
				       MALI_C55_MIN_HEIGHT,
				       sink_fmt->height - crop->top);

		*compose = *crop;

		src_fmt = v4l2_subdev_state_get_format(state,
						       MALI_C55_RSZ_SOURCE_PAD);
		src_fmt->width = compose->width;
		src_fmt->height = compose->height;
	}

	sel->r = *crop;
	return 0;
}

static int mali_c55_rsz_set_compose(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_selection *sel)
{
	struct mali_c55_resizer *rsz = sd_to_mali_c55_rsz(sd);
	struct mali_c55 *mali_c55 = rsz->mali_c55;
	struct v4l2_mbus_framefmt *src_fmt;
	struct v4l2_rect *compose, *crop;

	/*
	 * We cannot change the compose rectangle during streaming, as that
	 * would require a change in the output buffer size.
	 */
	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE &&
	    v4l2_subdev_is_streaming(sd))
		return -EBUSY;

	/*
	 * In the FR pipe, the scaler is an optional component that may not be
	 * fitted.
	 */
	if (rsz->id == MALI_C55_RSZ_FR &&
	    !(mali_c55->capabilities & MALI_C55_GPS_FRSCALER_FITTED))
		return -EINVAL;

	compose = v4l2_subdev_state_get_compose(state, MALI_C55_RSZ_SINK_PAD);
	crop = v4l2_subdev_state_get_crop(state, MALI_C55_RSZ_SINK_PAD);

	compose->left = 0;
	compose->top = 0;
	compose->width = clamp_t(unsigned int, sel->r.width, crop->width / 8,
				 crop->width);
	compose->height = clamp_t(unsigned int, sel->r.height, crop->height / 8,
				  crop->height);

	sel->r = *compose;

	/*
	 * We need to be sure to propagate the compose rectangle size to the
	 * source pad format.
	 */
	src_fmt = v4l2_subdev_state_get_format(state, MALI_C55_RSZ_SOURCE_PAD);
	src_fmt->width = compose->width;
	src_fmt->height = compose->height;

	return 0;
}

static int mali_c55_rsz_set_selection(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_selection *sel)
{
	if (sel->pad != MALI_C55_RSZ_SINK_PAD)
		return -EINVAL;

	if (sel->target == V4L2_SEL_TGT_CROP)
		return mali_c55_rsz_set_crop(sd, state, sel);

	if (sel->target == V4L2_SEL_TGT_COMPOSE)
		return mali_c55_rsz_set_compose(sd, state, sel);

	return -EINVAL;
}

static int mali_c55_rsz_set_routing(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    enum v4l2_subdev_format_whence which,
				    struct v4l2_subdev_krouting *routing)
{
	if (which == V4L2_SUBDEV_FORMAT_ACTIVE &&
	    media_entity_is_streaming(&sd->entity))
		return -EBUSY;

	return __mali_c55_rsz_set_routing(sd, state, routing);
}

static int mali_c55_rsz_enable_streams(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state, u32 pad,
				       u64 streams_mask)
{
	struct mali_c55_resizer *rsz = sd_to_mali_c55_rsz(sd);
	struct mali_c55 *mali_c55 = rsz->mali_c55;
	unsigned int sink_pad;

	sink_pad = mali_c55_rsz_get_active_sink(state);
	if (sink_pad == MALI_C55_RSZ_SINK_BYPASS_PAD) {
		/* Bypass FR pipe processing if the bypass route is active. */
		mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_ISP_RAW_BYPASS,
					 MALI_C55_ISP_RAW_BYPASS_FR_BYPASS_MASK,
					 MALI_C55_ISP_RAW_BYPASS_RAW_FR_BYPASS);
		return 0;
	}

	/* Disable bypass and use regular processing. */
	mali_c55_ctx_update_bits(mali_c55, MALI_C55_REG_ISP_RAW_BYPASS,
				 MALI_C55_ISP_RAW_BYPASS_FR_BYPASS_MASK, 0);
	mali_c55_rsz_program(rsz, state);

	return 0;
}

static int mali_c55_rsz_disable_streams(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *state, u32 pad,
					u64 streams_mask)
{
	return 0;
}

static const struct v4l2_subdev_pad_ops mali_c55_resizer_pad_ops = {
	.enum_mbus_code		= mali_c55_rsz_enum_mbus_code,
	.enum_frame_size	= mali_c55_rsz_enum_frame_size,
	.get_fmt		= v4l2_subdev_get_fmt,
	.set_fmt		= mali_c55_rsz_set_fmt,
	.get_selection		= mali_c55_rsz_get_selection,
	.set_selection		= mali_c55_rsz_set_selection,
	.set_routing		= mali_c55_rsz_set_routing,
	.enable_streams		= mali_c55_rsz_enable_streams,
	.disable_streams	= mali_c55_rsz_disable_streams,
};

static const struct v4l2_subdev_ops mali_c55_resizer_ops = {
	.pad	= &mali_c55_resizer_pad_ops,
};

static int mali_c55_rsz_init_state(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state)
{
	struct mali_c55_resizer *rsz = sd_to_mali_c55_rsz(sd);
	struct v4l2_subdev_route routes[2] = {
		{
			.sink_pad = MALI_C55_RSZ_SINK_PAD,
			.source_pad = MALI_C55_RSZ_SOURCE_PAD,
			.flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE,
		}, {
			.sink_pad = MALI_C55_RSZ_SINK_BYPASS_PAD,
			.source_pad = MALI_C55_RSZ_SOURCE_PAD,
		},
	};
	struct v4l2_subdev_krouting routing = {
		.num_routes = rsz->num_routes,
		.routes = routes,
	};

	return __mali_c55_rsz_set_routing(sd, state, &routing);
}

static const struct v4l2_subdev_internal_ops mali_c55_resizer_internal_ops = {
	.init_state = mali_c55_rsz_init_state,
};

static int mali_c55_register_resizer(struct mali_c55 *mali_c55,
				     unsigned int index)
{
	struct mali_c55_resizer *rsz = &mali_c55->resizers[index];
	struct v4l2_subdev *sd = &rsz->sd;
	unsigned int num_pads;
	int ret;

	rsz->id = index;
	v4l2_subdev_init(sd, &mali_c55_resizer_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_STREAMS;
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	sd->internal_ops = &mali_c55_resizer_internal_ops;

	rsz->pads[MALI_C55_RSZ_SINK_PAD].flags = MEDIA_PAD_FL_SINK;
	rsz->pads[MALI_C55_RSZ_SOURCE_PAD].flags = MEDIA_PAD_FL_SOURCE;

	if (rsz->id == MALI_C55_RSZ_FR) {
		num_pads = MALI_C55_RSZ_NUM_PADS;
		rsz->num_routes = 2;

		rsz->pads[MALI_C55_RSZ_SINK_BYPASS_PAD].flags =
			MEDIA_PAD_FL_SINK;

		snprintf(sd->name, sizeof(sd->name), "%s resizer fr",
			 MALI_C55_DRIVER_NAME);

	} else {
		num_pads = MALI_C55_RSZ_NUM_PADS - 1;
		rsz->num_routes = 1;

		snprintf(sd->name, sizeof(sd->name), "%s resizer ds",
			 MALI_C55_DRIVER_NAME);
	}

	ret = media_entity_pads_init(&sd->entity, num_pads, rsz->pads);
	if (ret)
		return ret;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto err_media_cleanup;

	ret = v4l2_device_register_subdev(&mali_c55->v4l2_dev, sd);
	if (ret)
		goto err_subdev_cleanup;

	rsz->cap_dev = &mali_c55->cap_devs[index];
	rsz->mali_c55 = mali_c55;

	return 0;

err_subdev_cleanup:
	v4l2_subdev_cleanup(sd);
err_media_cleanup:
	media_entity_cleanup(&sd->entity);

	return ret;
}

static void mali_c55_unregister_resizer(struct mali_c55_resizer *rsz)
{
	if (!rsz->mali_c55)
		return;

	v4l2_device_unregister_subdev(&rsz->sd);
	v4l2_subdev_cleanup(&rsz->sd);
	media_entity_cleanup(&rsz->sd.entity);
}

int mali_c55_register_resizers(struct mali_c55 *mali_c55)
{
	int ret;

	ret = mali_c55_register_resizer(mali_c55, MALI_C55_RSZ_FR);
	if (ret)
		return ret;

	if (mali_c55->capabilities & MALI_C55_GPS_DS_PIPE_FITTED) {
		ret = mali_c55_register_resizer(mali_c55, MALI_C55_RSZ_DS);
		if (ret)
			goto err_unregister_fr;
	}

	return 0;

err_unregister_fr:
	mali_c55_unregister_resizer(&mali_c55->resizers[MALI_C55_RSZ_FR]);

	return ret;
}

void mali_c55_unregister_resizers(struct mali_c55 *mali_c55)
{
	for (unsigned int i = 0; i < MALI_C55_NUM_RSZS; i++)
		mali_c55_unregister_resizer(&mali_c55->resizers[i]);
}
