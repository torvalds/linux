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
/* Ported from xf86-video-ast driver */

#ifndef AST_TABLES_H
#define AST_TABLES_H

/* Std. Table Index Definition */
#define TextModeIndex		0
#define EGAModeIndex		1
#define VGAModeIndex		2
#define HiCModeIndex		3
#define TrueCModeIndex		4

#define Charx8Dot               0x00000001
#define HalfDCLK                0x00000002
#define DoubleScanMode          0x00000004
#define LineCompareOff          0x00000008
#define HBorder                 0x00000020
#define VBorder                 0x00000010
#define WideScreenMode		0x00000100
#define NewModeInfo		0x00000200
#define NHSync			0x00000400
#define PHSync			0x00000800
#define NVSync			0x00001000
#define PVSync			0x00002000
#define SyncPP			(PVSync | PHSync)
#define SyncPN			(PVSync | NHSync)
#define SyncNP			(NVSync | PHSync)
#define SyncNN			(NVSync | NHSync)
#define AST2500PreCatchCRT		0x00004000

/* DCLK Index */
#define VCLK25_175     		0x00
#define VCLK28_322     		0x01
#define VCLK31_5       		0x02
#define VCLK36         		0x03
#define VCLK40         		0x04
#define VCLK49_5       		0x05
#define VCLK50         		0x06
#define VCLK56_25      		0x07
#define VCLK65		 	0x08
#define VCLK75	        	0x09
#define VCLK78_75      		0x0A
#define VCLK94_5       		0x0B
#define VCLK108        		0x0C
#define VCLK135        		0x0D
#define VCLK157_5      		0x0E
#define VCLK162        		0x0F
/* #define VCLK193_25     		0x10 */
#define VCLK154     		0x10
#define VCLK83_5    		0x11
#define VCLK106_5   		0x12
#define VCLK146_25  		0x13
#define VCLK148_5   		0x14
#define VCLK71      		0x15
#define VCLK88_75   		0x16
#define VCLK119     		0x17
#define VCLK85_5     		0x18
#define VCLK97_75     		0x19
#define VCLK118_25			0x1A

static const struct ast_vbios_dclk_info dclk_table[] = {
	{0x2C, 0xE7, 0x03},			/* 00: VCLK25_175	*/
	{0x95, 0x62, 0x03},			/* 01: VCLK28_322	*/
	{0x67, 0x63, 0x01},			/* 02: VCLK31_5		*/
	{0x76, 0x63, 0x01},			/* 03: VCLK36		*/
	{0xEE, 0x67, 0x01},			/* 04: VCLK40		*/
	{0x82, 0x62, 0x01},			/* 05: VCLK49_5		*/
	{0xC6, 0x64, 0x01},			/* 06: VCLK50		*/
	{0x94, 0x62, 0x01},			/* 07: VCLK56_25	*/
	{0x80, 0x64, 0x00},			/* 08: VCLK65		*/
	{0x7B, 0x63, 0x00},			/* 09: VCLK75		*/
	{0x67, 0x62, 0x00},			/* 0A: VCLK78_75	*/
	{0x7C, 0x62, 0x00},			/* 0B: VCLK94_5		*/
	{0x8E, 0x62, 0x00},			/* 0C: VCLK108		*/
	{0x85, 0x24, 0x00},			/* 0D: VCLK135		*/
	{0x67, 0x22, 0x00},			/* 0E: VCLK157_5	*/
	{0x6A, 0x22, 0x00},			/* 0F: VCLK162		*/
	{0x4d, 0x4c, 0x80},			/* 10: VCLK154		*/
	{0x68, 0x6f, 0x80},			/* 11: VCLK83.5		*/
	{0x28, 0x49, 0x80},			/* 12: VCLK106.5	*/
	{0x37, 0x49, 0x80},			/* 13: VCLK146.25	*/
	{0x1f, 0x45, 0x80},			/* 14: VCLK148.5	*/
	{0x47, 0x6c, 0x80},			/* 15: VCLK71		*/
	{0x25, 0x65, 0x80},			/* 16: VCLK88.75	*/
	{0x77, 0x58, 0x80},			/* 17: VCLK119		*/
	{0x32, 0x67, 0x80},			/* 18: VCLK85_5		*/
	{0x6a, 0x6d, 0x80},			/* 19: VCLK97_75	*/
	{0x3b, 0x2c, 0x81},			/* 1A: VCLK118_25	*/
};

static const struct ast_vbios_dclk_info dclk_table_ast2500[] = {
	{0x2C, 0xE7, 0x03},			/* 00: VCLK25_175	*/
	{0x95, 0x62, 0x03},			/* 01: VCLK28_322	*/
	{0x67, 0x63, 0x01},			/* 02: VCLK31_5		*/
	{0x76, 0x63, 0x01},			/* 03: VCLK36		*/
	{0xEE, 0x67, 0x01},			/* 04: VCLK40		*/
	{0x82, 0x62, 0x01},			/* 05: VCLK49_5		*/
	{0xC6, 0x64, 0x01},			/* 06: VCLK50		*/
	{0x94, 0x62, 0x01},			/* 07: VCLK56_25	*/
	{0x80, 0x64, 0x00},			/* 08: VCLK65		*/
	{0x7B, 0x63, 0x00},			/* 09: VCLK75		*/
	{0x67, 0x62, 0x00},			/* 0A: VCLK78_75	*/
	{0x7C, 0x62, 0x00},			/* 0B: VCLK94_5		*/
	{0x8E, 0x62, 0x00},			/* 0C: VCLK108		*/
	{0x85, 0x24, 0x00},			/* 0D: VCLK135		*/
	{0x67, 0x22, 0x00},			/* 0E: VCLK157_5	*/
	{0x6A, 0x22, 0x00},			/* 0F: VCLK162		*/
	{0x4d, 0x4c, 0x80},			/* 10: VCLK154		*/
	{0x68, 0x6f, 0x80},			/* 11: VCLK83.5		*/
	{0x28, 0x49, 0x80},			/* 12: VCLK106.5	*/
	{0x37, 0x49, 0x80},			/* 13: VCLK146.25	*/
	{0x1f, 0x45, 0x80},			/* 14: VCLK148.5	*/
	{0x47, 0x6c, 0x80},			/* 15: VCLK71		*/
	{0x25, 0x65, 0x80},			/* 16: VCLK88.75	*/
	{0x58, 0x01, 0x42},			/* 17: VCLK119		*/
	{0x32, 0x67, 0x80},			/* 18: VCLK85_5		*/
	{0x6a, 0x6d, 0x80},			/* 19: VCLK97_75	*/
	{0x44, 0x20, 0x43},			/* 1A: VCLK118_25	*/
};

static const struct ast_vbios_stdtable vbios_stdtable[] = {
	/* MD_2_3_400 */
	{
		0x67,
		{0x00,0x03,0x00,0x02},
		{0x5f,0x4f,0x50,0x82,0x55,0x81,0xbf,0x1f,
		 0x00,0x4f,0x0d,0x0e,0x00,0x00,0x00,0x00,
		 0x9c,0x8e,0x8f,0x28,0x1f,0x96,0xb9,0xa3,
		 0xff},
		{0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,
		 0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
		 0x0c,0x00,0x0f,0x08},
		{0x00,0x00,0x00,0x00,0x00,0x10,0x0e,0x00,
		 0xff}
	},
	/* Mode12/ExtEGATable */
	{
		0xe3,
		{0x01,0x0f,0x00,0x06},
		{0x5f,0x4f,0x50,0x82,0x55,0x81,0x0b,0x3e,
		 0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
		 0xe9,0x8b,0xdf,0x28,0x00,0xe7,0x04,0xe3,
		 0xff},
		{0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,
		 0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
		 0x01,0x00,0x0f,0x00},
		{0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0f,
		 0xff}
	},
	/* ExtVGATable */
	{
		0x2f,
		{0x01,0x0f,0x00,0x0e},
		{0x5f,0x4f,0x50,0x82,0x54,0x80,0x0b,0x3e,
		 0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
		 0xea,0x8c,0xdf,0x28,0x40,0xe7,0x04,0xa3,
		 0xff},
		{0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
		 0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
		 0x01,0x00,0x00,0x00},
		{0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0f,
		 0xff}
	},
	/* ExtHiCTable */
	{
		0x2f,
		{0x01,0x0f,0x00,0x0e},
		{0x5f,0x4f,0x50,0x82,0x54,0x80,0x0b,0x3e,
		 0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
		 0xea,0x8c,0xdf,0x28,0x40,0xe7,0x04,0xa3,
		 0xff},
		{0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
		 0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
		 0x01,0x00,0x00,0x00},
		{0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0f,
		 0xff}
	},
	/* ExtTrueCTable */
	{
		0x2f,
		{0x01,0x0f,0x00,0x0e},
		{0x5f,0x4f,0x50,0x82,0x54,0x80,0x0b,0x3e,
		 0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
		 0xea,0x8c,0xdf,0x28,0x40,0xe7,0x04,0xa3,
		 0xff},
		{0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
		 0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
		 0x01,0x00,0x00,0x00},
		{0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0f,
		 0xff}
	},
};

static const struct ast_vbios_enhtable res_640x480[] = {
	{ 800, 640, 8, 96, 525, 480, 2, 2, VCLK25_175,	/* 60Hz */
	  (SyncNN | HBorder | VBorder | Charx8Dot), 60, 1, 0x2E },
	{ 832, 640, 16, 40, 520, 480, 1, 3, VCLK31_5,	/* 72Hz */
	  (SyncNN | HBorder | VBorder | Charx8Dot), 72, 2, 0x2E  },
	{ 840, 640, 16, 64, 500, 480, 1, 3, VCLK31_5,	/* 75Hz */
	  (SyncNN | Charx8Dot) , 75, 3, 0x2E },
	{ 832, 640, 56, 56, 509, 480, 1, 3, VCLK36,	/* 85Hz */
	  (SyncNN | Charx8Dot) , 85, 4, 0x2E },
	{ 832, 640, 56, 56, 509, 480, 1, 3, VCLK36,	/* end */
	  (SyncNN | Charx8Dot) , 0xFF, 4, 0x2E },
};

static const struct ast_vbios_enhtable res_800x600[] = {
	{1024, 800, 24, 72, 625, 600, 1, 2, VCLK36,	/* 56Hz */
	 (SyncPP | Charx8Dot), 56, 1, 0x30 },
	{1056, 800, 40, 128, 628, 600, 1, 4, VCLK40,	/* 60Hz */
	 (SyncPP | Charx8Dot), 60, 2, 0x30 },
	{1040, 800, 56, 120, 666, 600, 37, 6, VCLK50,	/* 72Hz */
	 (SyncPP | Charx8Dot), 72, 3, 0x30 },
	{1056, 800, 16, 80, 625, 600, 1, 3, VCLK49_5,	/* 75Hz */
	 (SyncPP | Charx8Dot), 75, 4, 0x30 },
	{1048, 800, 32, 64, 631, 600, 1, 3, VCLK56_25,	/* 85Hz */
	 (SyncPP | Charx8Dot), 84, 5, 0x30 },
	{1048, 800, 32, 64, 631, 600, 1, 3, VCLK56_25,	/* end */
	 (SyncPP | Charx8Dot), 0xFF, 5, 0x30 },
};


static const struct ast_vbios_enhtable res_1024x768[] = {
	{1344, 1024, 24, 136, 806, 768, 3, 6, VCLK65,	/* 60Hz */
	 (SyncNN | Charx8Dot), 60, 1, 0x31 },
	{1328, 1024, 24, 136, 806, 768, 3, 6, VCLK75,	/* 70Hz */
	 (SyncNN | Charx8Dot), 70, 2, 0x31 },
	{1312, 1024, 16, 96, 800, 768, 1, 3, VCLK78_75,	/* 75Hz */
	 (SyncPP | Charx8Dot), 75, 3, 0x31 },
	{1376, 1024, 48, 96, 808, 768, 1, 3, VCLK94_5,	/* 85Hz */
	 (SyncPP | Charx8Dot), 84, 4, 0x31 },
	{1376, 1024, 48, 96, 808, 768, 1, 3, VCLK94_5,	/* end */
	 (SyncPP | Charx8Dot), 0xFF, 4, 0x31 },
};

static const struct ast_vbios_enhtable res_1280x1024[] = {
	{1688, 1280, 48, 112, 1066, 1024, 1, 3, VCLK108,	/* 60Hz */
	 (SyncPP | Charx8Dot), 60, 1, 0x32 },
	{1688, 1280, 16, 144, 1066, 1024, 1, 3, VCLK135,	/* 75Hz */
	 (SyncPP | Charx8Dot), 75, 2, 0x32 },
	{1728, 1280, 64, 160, 1072, 1024, 1, 3, VCLK157_5,	/* 85Hz */
	 (SyncPP | Charx8Dot), 85, 3, 0x32 },
	{1728, 1280, 64, 160, 1072, 1024, 1, 3, VCLK157_5,	/* end */
	 (SyncPP | Charx8Dot), 0xFF, 3, 0x32 },
};

static const struct ast_vbios_enhtable res_1600x1200[] = {
	{2160, 1600, 64, 192, 1250, 1200, 1, 3, VCLK162,	/* 60Hz */
	 (SyncPP | Charx8Dot), 60, 1, 0x33 },
	{2160, 1600, 64, 192, 1250, 1200, 1, 3, VCLK162,	/* end */
	 (SyncPP | Charx8Dot), 0xFF, 1, 0x33 },
};

/* 16:9 */
static const struct ast_vbios_enhtable res_1360x768[] = {
	{1792, 1360, 64, 112, 795, 768, 3, 6, VCLK85_5,		/* 60Hz */
	 (SyncPP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 60, 1, 0x39 },
	{1792, 1360, 64, 112, 795, 768, 3, 6, VCLK85_5,	         /* end */
	 (SyncPP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 0xFF, 1, 0x39 },
};

static const struct ast_vbios_enhtable res_1600x900[] = {
	{1760, 1600, 48, 32, 926, 900, 3, 5, VCLK97_75,		/* 60Hz CVT RB */
	 (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 60, 1, 0x3A },
	{2112, 1600, 88, 168, 934, 900, 3, 5, VCLK118_25,	/* 60Hz CVT */
	 (SyncPN | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 60, 2, 0x3A },
	{2112, 1600, 88, 168, 934, 900, 3, 5, VCLK118_25,	/* 60Hz CVT */
	 (SyncPN | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 0xFF, 2, 0x3A },
};

static const struct ast_vbios_enhtable res_1920x1080[] = {
	{2200, 1920, 88, 44, 1125, 1080, 4, 5, VCLK148_5,	/* 60Hz */
	 (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 60, 1, 0x38 },
	{2200, 1920, 88, 44, 1125, 1080, 4, 5, VCLK148_5,	/* 60Hz */
	 (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 0xFF, 1, 0x38 },
};


/* 16:10 */
static const struct ast_vbios_enhtable res_1280x800[] = {
	{1440, 1280, 48, 32,  823,  800, 3, 6, VCLK71,		/* 60Hz RB */
	 (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 60, 1, 0x35 },
	{1680, 1280, 72,128,  831,  800, 3, 6, VCLK83_5,	/* 60Hz */
	 (SyncPN | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 60, 2, 0x35 },
	{1680, 1280, 72,128,  831,  800, 3, 6, VCLK83_5,	/* 60Hz */
	 (SyncPN | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 0xFF, 2, 0x35 },

};

static const struct ast_vbios_enhtable res_1440x900[] = {
	{1600, 1440, 48, 32,  926,  900, 3, 6, VCLK88_75,	/* 60Hz RB */
	 (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 60, 1, 0x36 },
	{1904, 1440, 80,152,  934,  900, 3, 6, VCLK106_5,	/* 60Hz */
	 (SyncPN | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 60, 2, 0x36 },
	{1904, 1440, 80,152,  934,  900, 3, 6, VCLK106_5,	/* 60Hz */
	 (SyncPN | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 0xFF, 2, 0x36 },
};

static const struct ast_vbios_enhtable res_1680x1050[] = {
	{1840, 1680, 48, 32, 1080, 1050, 3, 6, VCLK119,		/* 60Hz RB */
	 (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 60, 1, 0x37 },
	{2240, 1680,104,176, 1089, 1050, 3, 6, VCLK146_25,	/* 60Hz */
	 (SyncPN | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 60, 2, 0x37 },
	{2240, 1680,104,176, 1089, 1050, 3, 6, VCLK146_25,	/* 60Hz */
	 (SyncPN | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo), 0xFF, 2, 0x37 },
};

static const struct ast_vbios_enhtable res_1920x1200[] = {
	{2080, 1920, 48, 32, 1235, 1200, 3, 6, VCLK154,		/* 60Hz RB*/
	 (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 60, 1, 0x34 },
	{2080, 1920, 48, 32, 1235, 1200, 3, 6, VCLK154,		/* 60Hz RB */
	 (SyncNP | Charx8Dot | LineCompareOff | WideScreenMode | NewModeInfo |
	  AST2500PreCatchCRT), 0xFF, 1, 0x34 },
};

#endif
