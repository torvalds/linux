/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AST_DRAM_TABLES_H
#define AST_DRAM_TABLES_H

/* DRAM timing tables */
struct ast_dramstruct {
	u16 index;
	u32 data;
};

/*
 * AST2500 DRAM settings modules
 */
#define REGTBL_NUM           17
#define REGIDX_010           0
#define REGIDX_014           1
#define REGIDX_018           2
#define REGIDX_020           3
#define REGIDX_024           4
#define REGIDX_02C           5
#define REGIDX_030           6
#define REGIDX_214           7
#define REGIDX_2E0           8
#define REGIDX_2E4           9
#define REGIDX_2E8           10
#define REGIDX_2EC           11
#define REGIDX_2F0           12
#define REGIDX_2F4           13
#define REGIDX_2F8           14
#define REGIDX_RFC           15
#define REGIDX_PLL           16

static const u32 ast2500_ddr3_1600_timing_table[REGTBL_NUM] = {
	0x64604D38,		     /* 0x010 */
	0x29690599,		     /* 0x014 */
	0x00000300,		     /* 0x018 */
	0x00000000,		     /* 0x020 */
	0x00000000,		     /* 0x024 */
	0x02181E70,		     /* 0x02C */
	0x00000040,		     /* 0x030 */
	0x00000024,		     /* 0x214 */
	0x02001300,		     /* 0x2E0 */
	0x0E0000A0,		     /* 0x2E4 */
	0x000E001B,		     /* 0x2E8 */
	0x35B8C105,		     /* 0x2EC */
	0x08090408,		     /* 0x2F0 */
	0x9B000800,		     /* 0x2F4 */
	0x0E400A00,		     /* 0x2F8 */
	0x9971452F,		     /* tRFC  */
	0x000071C1		     /* PLL   */
};

static const u32 ast2500_ddr4_1600_timing_table[REGTBL_NUM] = {
	0x63604E37,		     /* 0x010 */
	0xE97AFA99,		     /* 0x014 */
	0x00019000,		     /* 0x018 */
	0x08000000,		     /* 0x020 */
	0x00000400,		     /* 0x024 */
	0x00000410,		     /* 0x02C */
	0x00000101,		     /* 0x030 */
	0x00000024,		     /* 0x214 */
	0x03002900,		     /* 0x2E0 */
	0x0E0000A0,		     /* 0x2E4 */
	0x000E001C,		     /* 0x2E8 */
	0x35B8C106,		     /* 0x2EC */
	0x08080607,		     /* 0x2F0 */
	0x9B000900,		     /* 0x2F4 */
	0x0E400A00,		     /* 0x2F8 */
	0x99714545,		     /* tRFC  */
	0x000071C1		     /* PLL   */
};

#endif
