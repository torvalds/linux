/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_CR_DEFS_CLIENT_H
#define PVR_ROGUE_CR_DEFS_CLIENT_H

/* clang-format off */

/*
 * This register controls the anti-aliasing mode of the Tiling Co-Processor, independent control is
 * provided in both X & Y axis.
 * This register needs to be set based on the ISP Samples Per Pixel a core supports.
 *
 * When ISP Samples Per Pixel = 1:
 * 2xmsaa is achieved by enabling Y - TE does AA on Y plane only
 * 4xmsaa is achieved by enabling Y and X - TE does AA on X and Y plane
 * 8xmsaa not supported by XE cores
 *
 * When ISP Samples Per Pixel = 2:
 * 2xmsaa is achieved by enabling X2 - does not affect TE
 * 4xmsaa is achieved by enabling Y and X2 - TE does AA on Y plane only
 * 8xmsaa is achieved by enabling Y, X and X2 - TE does AA on X and Y plane
 * 8xmsaa not supported by XE cores
 *
 * When ISP Samples Per Pixel = 4:
 * 2xmsaa is achieved by enabling X2 - does not affect TE
 * 4xmsaa is achieved by enabling Y2 and X2 - TE does AA on Y plane only
 * 8xmsaa not supported by XE cores
 */
/* Register ROGUE_CR_TE_AA */
#define ROGUE_CR_TE_AA 0x0C00U
#define ROGUE_CR_TE_AA_MASKFULL 0x000000000000000Full
/* Y2
 * Indicates 4xmsaa when X2 and Y2 are set to 1. This does not affect TE and is only used within
 * TPW.
 */
#define ROGUE_CR_TE_AA_Y2_SHIFT 3
#define ROGUE_CR_TE_AA_Y2_CLRMSK 0xFFFFFFF7
#define ROGUE_CR_TE_AA_Y2_EN 0x00000008
/* Y
 * Anti-Aliasing in Y Plane Enabled
 */
#define ROGUE_CR_TE_AA_Y_SHIFT 2
#define ROGUE_CR_TE_AA_Y_CLRMSK 0xFFFFFFFB
#define ROGUE_CR_TE_AA_Y_EN 0x00000004
/* X
 * Anti-Aliasing in X Plane Enabled
 */
#define ROGUE_CR_TE_AA_X_SHIFT 1
#define ROGUE_CR_TE_AA_X_CLRMSK 0xFFFFFFFD
#define ROGUE_CR_TE_AA_X_EN 0x00000002
/* X2
 * 2x Anti-Aliasing Enabled, affects PPP only
 */
#define ROGUE_CR_TE_AA_X2_SHIFT                             (0U)
#define ROGUE_CR_TE_AA_X2_CLRMSK                            (0xFFFFFFFEU)
#define ROGUE_CR_TE_AA_X2_EN                                (0x00000001U)

/* MacroTile Boundaries X Plane */
/* Register ROGUE_CR_TE_MTILE1 */
#define ROGUE_CR_TE_MTILE1 0x0C08
#define ROGUE_CR_TE_MTILE1_MASKFULL 0x0000000007FFFFFFull
/* X1 default: 0x00000004
 * X1 MacroTile boundary, left tile X for second column of macrotiles (16MT mode) - 32 pixels across
 * tile
 */
#define ROGUE_CR_TE_MTILE1_X1_SHIFT 18
#define ROGUE_CR_TE_MTILE1_X1_CLRMSK 0xF803FFFF
/* X2 default: 0x00000008
 * X2 MacroTile boundary, left tile X for third(16MT) column of macrotiles - 32 pixels across tile
 */
#define ROGUE_CR_TE_MTILE1_X2_SHIFT 9U
#define ROGUE_CR_TE_MTILE1_X2_CLRMSK 0xFFFC01FF
/* X3 default: 0x0000000c
 * X3 MacroTile boundary, left tile X for fourth column of macrotiles (16MT) - 32 pixels across tile
 */
#define ROGUE_CR_TE_MTILE1_X3_SHIFT 0
#define ROGUE_CR_TE_MTILE1_X3_CLRMSK 0xFFFFFE00

/* MacroTile Boundaries Y Plane. */
/* Register ROGUE_CR_TE_MTILE2 */
#define ROGUE_CR_TE_MTILE2 0x0C10
#define ROGUE_CR_TE_MTILE2_MASKFULL 0x0000000007FFFFFFull
/* Y1 default: 0x00000004
 * X1 MacroTile boundary, ltop tile Y for second column of macrotiles (16MT mode) - 32 pixels tile
 * height
 */
#define ROGUE_CR_TE_MTILE2_Y1_SHIFT 18
#define ROGUE_CR_TE_MTILE2_Y1_CLRMSK 0xF803FFFF
/* Y2 default: 0x00000008
 * X2 MacroTile boundary, top tile Y for third(16MT) column of macrotiles - 32 pixels tile height
 */
#define ROGUE_CR_TE_MTILE2_Y2_SHIFT 9
#define ROGUE_CR_TE_MTILE2_Y2_CLRMSK 0xFFFC01FF
/* Y3 default: 0x0000000c
 * X3 MacroTile boundary, top tile Y for fourth column of macrotiles (16MT) - 32 pixels tile height
 */
#define ROGUE_CR_TE_MTILE2_Y3_SHIFT 0
#define ROGUE_CR_TE_MTILE2_Y3_CLRMSK 0xFFFFFE00

/*
 * In order to perform the tiling operation and generate the display list the maximum screen size
 * must be configured in terms of the number of tiles in X & Y axis.
 */

/* Register ROGUE_CR_TE_SCREEN */
#define ROGUE_CR_TE_SCREEN 0x0C18U
#define ROGUE_CR_TE_SCREEN_MASKFULL 0x00000000001FF1FFull
/* YMAX default: 0x00000010
 * Maximum Y tile address visible on screen, 32 pixel tile height, 16Kx16K max screen size
 */
#define ROGUE_CR_TE_SCREEN_YMAX_SHIFT 12
#define ROGUE_CR_TE_SCREEN_YMAX_CLRMSK 0xFFE00FFF
/* XMAX default: 0x00000010
 * Maximum X tile address visible on screen, 32 pixel tile width, 16Kx16K max screen size
 */
#define ROGUE_CR_TE_SCREEN_XMAX_SHIFT 0
#define ROGUE_CR_TE_SCREEN_XMAX_CLRMSK 0xFFFFFE00

/*
 * In order to perform the tiling operation and generate the display list the maximum screen size
 * must be configured in terms of the number of pixels in X & Y axis since this may not be the same
 * as the number of tiles defined in the RGX_CR_TE_SCREEN register.
 */
/* Register ROGUE_CR_PPP_SCREEN */
#define ROGUE_CR_PPP_SCREEN 0x0C98
#define ROGUE_CR_PPP_SCREEN_MASKFULL 0x000000007FFF7FFFull
/* PIXYMAX
 * Screen height in pixels. (16K x 16K max screen size)
 */
#define ROGUE_CR_PPP_SCREEN_PIXYMAX_SHIFT 16
#define ROGUE_CR_PPP_SCREEN_PIXYMAX_CLRMSK 0x8000FFFF
/* PIXXMAX
 * Screen width in pixels.(16K x 16K max screen size)
 */
#define ROGUE_CR_PPP_SCREEN_PIXXMAX_SHIFT 0
#define ROGUE_CR_PPP_SCREEN_PIXXMAX_CLRMSK 0xFFFF8000

/* Register ROGUE_CR_ISP_MTILE_SIZE */
#define ROGUE_CR_ISP_MTILE_SIZE 0x0F18
#define ROGUE_CR_ISP_MTILE_SIZE_MASKFULL 0x0000000003FF03FFull
/* X
 * Macrotile width, in tiles. A value of zero corresponds to the maximum size
 */
#define ROGUE_CR_ISP_MTILE_SIZE_X_SHIFT 16
#define ROGUE_CR_ISP_MTILE_SIZE_X_CLRMSK 0xFC00FFFF
#define ROGUE_CR_ISP_MTILE_SIZE_X_ALIGNSHIFT 0
#define ROGUE_CR_ISP_MTILE_SIZE_X_ALIGNSIZE 1
/* Y
 * Macrotile height, in tiles. A value of zero corresponds to the maximum size
 */
#define ROGUE_CR_ISP_MTILE_SIZE_Y_SHIFT 0
#define ROGUE_CR_ISP_MTILE_SIZE_Y_CLRMSK 0xFFFFFC00
#define ROGUE_CR_ISP_MTILE_SIZE_Y_ALIGNSHIFT 0
#define ROGUE_CR_ISP_MTILE_SIZE_Y_ALIGNSIZE 1

/* clang-format on */

#endif /* PVR_ROGUE_CR_DEFS_CLIENT_H */
