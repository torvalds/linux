// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2005 ASPEED Technology Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the authors not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  The authors makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * THE AUTHORS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "ast_drv.h"
#include "ast_vbios.h"

/* 4:3 */

static const struct ast_vbios_enhtable res_640x480[] = {
	{ 800, 640, 8, 96, 525, 480, 2, 2, VCLK25_175,		/* 60 Hz */
	  (SyncNN | HBorder | VBorder | Charx8Dot), 60, 1, 0x2e },
	{ 832, 640, 16, 40, 520, 480, 1, 3, VCLK31_5,		/* 72 Hz */
	  (SyncNN | HBorder | VBorder | Charx8Dot), 72, 2, 0x2e  },
	{ 840, 640, 16, 64, 500, 480, 1, 3, VCLK31_5,		/* 75 Hz */
	  (SyncNN | Charx8Dot), 75, 3, 0x2e },
	{ 832, 640, 56, 56, 509, 480, 1, 3, VCLK36,		/* 85 Hz */
	  (SyncNN | Charx8Dot), 85, 4, 0x2e },
	AST_VBIOS_INVALID_MODE,					/* end */
};

static const struct ast_vbios_enhtable res_800x600[] = {
	{ 1024, 800, 24, 72, 625, 600, 1, 2, VCLK36,		/* 56 Hz */
	  (SyncPP | Charx8Dot), 56, 1, 0x30 },
	{ 1056, 800, 40, 128, 628, 600, 1, 4, VCLK40,		/* 60 Hz */
	  (SyncPP | Charx8Dot), 60, 2, 0x30 },
	{ 1040, 800, 56, 120, 666, 600, 37, 6, VCLK50,		/* 72 Hz */
	  (SyncPP | Charx8Dot), 72, 3, 0x30 },
	{ 1056, 800, 16, 80, 625, 600, 1, 3, VCLK49_5,		/* 75 Hz */
	  (SyncPP | Charx8Dot), 75, 4, 0x30 },
	{ 1048, 800, 32, 64, 631, 600, 1, 3, VCLK56_25,		/* 85 Hz */
	  (SyncPP | Charx8Dot), 84, 5, 0x30 },
	AST_VBIOS_INVALID_MODE,					/* end */
};

static const struct ast_vbios_enhtable res_1024x768[] = {
	{ 1344, 1024, 24, 136, 806, 768, 3, 6, VCLK65,		/* 60 Hz */
	  (SyncNN | Charx8Dot), 60, 1, 0x31 },
	{ 1328, 1024, 24, 136, 806, 768, 3, 6, VCLK75,		/* 70 Hz */
	  (SyncNN | Charx8Dot), 70, 2, 0x31 },
	{ 1312, 1024, 16, 96, 800, 768, 1, 3, VCLK78_75,	/* 75 Hz */
	  (SyncPP | Charx8Dot), 75, 3, 0x31 },
	{ 1376, 1024, 48, 96, 808, 768, 1, 3, VCLK94_5,		/* 85 Hz */
	  (SyncPP | Charx8Dot), 84, 4, 0x31 },
	AST_VBIOS_INVALID_MODE,					/* end */
};

static const struct ast_vbios_enhtable res_1152x864[] = {
	{ 1600, 1152, 64, 128,  900,  864, 1, 3, VCLK108,	/* 75 Hz */
	  (SyncPP | Charx8Dot | NewModeInfo), 75, 1, 0x3b },
	AST_VBIOS_INVALID_MODE,					/* end */
};

static const struct ast_vbios_enhtable res_1280x1024[] = {
	{ 1688, 1280, 48, 112, 1066, 1024, 1, 3, VCLK108,	/* 60 Hz */
	  (SyncPP | Charx8Dot), 60, 1, 0x32 },
	{ 1688, 1280, 16, 144, 1066, 1024, 1, 3, VCLK135,	/* 75 Hz */
	  (SyncPP | Charx8Dot), 75, 2, 0x32 },
	{ 1728, 1280, 64, 160, 1072, 1024, 1, 3, VCLK157_5,	/* 85 Hz */
	  (SyncPP | Charx8Dot), 85, 3, 0x32 },
	AST_VBIOS_INVALID_MODE,					/* end */
};

static const struct ast_vbios_enhtable res_1600x1200[] = {
	{ 2160, 1600, 64, 192, 1250, 1200, 1, 3, VCLK162,	/* 60 Hz */
	  (SyncPP | Charx8Dot), 60, 1, 0x33 },
	AST_VBIOS_INVALID_MODE,					/* end */
};

/* 16:9 */

static const struct ast_vbios_enhtable res_1360x768[] = {
	{ 1792, 1360, 64, 112, 795, 768, 3, 6, VCLK85_5,	/* 60 Hz */
	  (SyncPP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 60, 1, 0x39 },
	AST_VBIOS_INVALID_MODE,					/* end */
};

static const struct ast_vbios_enhtable res_1600x900[] = {
	{ 1760, 1600, 48, 32, 926, 900, 3, 5, VCLK97_75,	/* 60 Hz CVT RB */
	  (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 60, 1, 0x3a },
	{ 2112, 1600, 88, 168, 934, 900, 3, 5, VCLK118_25,	/* 60 Hz CVT */
	  (SyncPN | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 60, 2, 0x3a },
	AST_VBIOS_INVALID_MODE,					/* end */
};

static const struct ast_vbios_enhtable res_1920x1080[] = {
	{ 2200, 1920, 88, 44, 1125, 1080, 4, 5, VCLK148_5,	/* 60 Hz */
	  (SyncPP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 60, 1, 0x38 },
	AST_VBIOS_INVALID_MODE,					/* end */
};

/* 16:10 */

static const struct ast_vbios_enhtable res_1280x800[] = {
	{ 1440, 1280, 48, 32, 823, 800, 3, 6, VCLK71,		/* 60 Hz RB */
	  (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 60, 1, 0x35 },
	{ 1680, 1280, 72, 128, 831, 800, 3, 6, VCLK83_5,	/* 60 Hz */
	  (SyncPN | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 60, 2, 0x35 },
	AST_VBIOS_INVALID_MODE,					/* end */
};

static const struct ast_vbios_enhtable res_1440x900[] = {
	{ 1600, 1440, 48, 32, 926, 900, 3, 6, VCLK88_75,	/* 60 Hz RB */
	  (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 60, 1, 0x36 },
	{ 1904, 1440, 80, 152, 934, 900, 3, 6, VCLK106_5,	/* 60 Hz */
	  (SyncPN | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 60, 2, 0x36 },
	AST_VBIOS_INVALID_MODE,					/* end */
};

static const struct ast_vbios_enhtable res_1680x1050[] = {
	{ 1840, 1680, 48, 32, 1080, 1050, 3, 6, VCLK119,	/* 60 Hz RB */
	  (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 60, 1, 0x37 },
	{ 2240, 1680, 104, 176, 1089, 1050, 3, 6, VCLK146_25,	/* 60 Hz */
	  (SyncPN | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 60, 2, 0x37 },
	AST_VBIOS_INVALID_MODE,					/* end */
};

static const struct ast_vbios_enhtable res_1920x1200[] = {
	{ 2080, 1920, 48, 32, 1235, 1200, 3, 6, VCLK154,	/* 60 Hz RB*/
	  (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 60, 1, 0x34 },
	AST_VBIOS_INVALID_MODE,					/* end */
};

/*
 * VBIOS mode tables
 */

static const struct ast_vbios_enhtable *res_table_wuxga[] = {
	&res_1920x1200[0],
	NULL,
};

static const struct ast_vbios_enhtable *res_table_fullhd[] = {
	&res_1920x1080[0],
	NULL,
};

static const struct ast_vbios_enhtable *res_table_wsxga_p[] = {
	&res_1280x800[0],
	&res_1360x768[0],
	&res_1440x900[0],
	&res_1600x900[0],
	&res_1680x1050[0],
	NULL,
};

static const struct ast_vbios_enhtable *res_table[] = {
	&res_640x480[0],
	&res_800x600[0],
	&res_1024x768[0],
	&res_1152x864[0],
	&res_1280x1024[0],
	&res_1600x1200[0],
	NULL,
};

static const struct ast_vbios_enhtable *
__ast_vbios_find_mode_table(const struct ast_vbios_enhtable **vmode_tables,
			    unsigned int hdisplay,
			    unsigned int vdisplay)
{
	while (*vmode_tables) {
		if ((*vmode_tables)->hde == hdisplay && (*vmode_tables)->vde == vdisplay)
			return *vmode_tables;
		++vmode_tables;
	}

	return NULL;
}

static const struct ast_vbios_enhtable *ast_vbios_find_mode_table(const struct ast_device *ast,
								  unsigned int hdisplay,
								  unsigned int vdisplay)
{
	const struct ast_vbios_enhtable *vmode_table = NULL;

	if (ast->support_wuxga)
		vmode_table = __ast_vbios_find_mode_table(res_table_wuxga, hdisplay, vdisplay);
	if (!vmode_table && ast->support_fullhd)
		vmode_table = __ast_vbios_find_mode_table(res_table_fullhd, hdisplay, vdisplay);
	if (!vmode_table && ast->support_wsxga_p)
		vmode_table = __ast_vbios_find_mode_table(res_table_wsxga_p, hdisplay, vdisplay);
	if (!vmode_table)
		vmode_table = __ast_vbios_find_mode_table(res_table, hdisplay, vdisplay);

	return vmode_table;
}

const struct ast_vbios_enhtable *ast_vbios_find_mode(const struct ast_device *ast,
						     const struct drm_display_mode *mode)
{
	const struct ast_vbios_enhtable *best_vmode = NULL;
	const struct ast_vbios_enhtable *vmode_table;
	const struct ast_vbios_enhtable *vmode;
	u32 refresh_rate;

	vmode_table = ast_vbios_find_mode_table(ast, mode->hdisplay, mode->vdisplay);
	if (!vmode_table)
		return NULL;

	refresh_rate = drm_mode_vrefresh(mode);

	for (vmode = vmode_table; ast_vbios_mode_is_valid(vmode); ++vmode) {
		if (((mode->flags & DRM_MODE_FLAG_NVSYNC) && (vmode->flags & PVSync)) ||
		    ((mode->flags & DRM_MODE_FLAG_PVSYNC) && (vmode->flags & NVSync)) ||
		    ((mode->flags & DRM_MODE_FLAG_NHSYNC) && (vmode->flags & PHSync)) ||
		    ((mode->flags & DRM_MODE_FLAG_PHSYNC) && (vmode->flags & NHSync))) {
			continue;
		}
		if (vmode->refresh_rate <= refresh_rate &&
		    (!best_vmode || vmode->refresh_rate > best_vmode->refresh_rate))
			best_vmode = vmode;
	}

	return best_vmode;
}
