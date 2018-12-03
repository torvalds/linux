/*
 * Copyright (c) 2011-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "coredump.h"

#include <linux/devcoredump.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/utsname.h>

#include "debug.h"
#include "hw.h"

static const struct ath10k_mem_section qca6174_hw21_register_sections[] = {
	{0x800, 0x810},
	{0x820, 0x82C},
	{0x830, 0x8F4},
	{0x90C, 0x91C},
	{0xA14, 0xA18},
	{0xA84, 0xA94},
	{0xAA8, 0xAD4},
	{0xADC, 0xB40},
	{0x1000, 0x10A4},
	{0x10BC, 0x111C},
	{0x1134, 0x1138},
	{0x1144, 0x114C},
	{0x1150, 0x115C},
	{0x1160, 0x1178},
	{0x1240, 0x1260},
	{0x2000, 0x207C},
	{0x3000, 0x3014},
	{0x4000, 0x4014},
	{0x5000, 0x5124},
	{0x6000, 0x6040},
	{0x6080, 0x60CC},
	{0x6100, 0x611C},
	{0x6140, 0x61D8},
	{0x6200, 0x6238},
	{0x6240, 0x628C},
	{0x62C0, 0x62EC},
	{0x6380, 0x63E8},
	{0x6400, 0x6440},
	{0x6480, 0x64CC},
	{0x6500, 0x651C},
	{0x6540, 0x6580},
	{0x6600, 0x6638},
	{0x6640, 0x668C},
	{0x66C0, 0x66EC},
	{0x6780, 0x67E8},
	{0x7080, 0x708C},
	{0x70C0, 0x70C8},
	{0x7400, 0x741C},
	{0x7440, 0x7454},
	{0x7800, 0x7818},
	{0x8000, 0x8004},
	{0x8010, 0x8064},
	{0x8080, 0x8084},
	{0x80A0, 0x80A4},
	{0x80C0, 0x80C4},
	{0x80E0, 0x80F4},
	{0x8100, 0x8104},
	{0x8110, 0x812C},
	{0x9000, 0x9004},
	{0x9800, 0x982C},
	{0x9830, 0x9838},
	{0x9840, 0x986C},
	{0x9870, 0x9898},
	{0x9A00, 0x9C00},
	{0xD580, 0xD59C},
	{0xF000, 0xF0E0},
	{0xF140, 0xF190},
	{0xF250, 0xF25C},
	{0xF260, 0xF268},
	{0xF26C, 0xF2A8},
	{0x10008, 0x1000C},
	{0x10014, 0x10018},
	{0x1001C, 0x10020},
	{0x10024, 0x10028},
	{0x10030, 0x10034},
	{0x10040, 0x10054},
	{0x10058, 0x1007C},
	{0x10080, 0x100C4},
	{0x100C8, 0x10114},
	{0x1012C, 0x10130},
	{0x10138, 0x10144},
	{0x10200, 0x10220},
	{0x10230, 0x10250},
	{0x10260, 0x10280},
	{0x10290, 0x102B0},
	{0x102C0, 0x102DC},
	{0x102E0, 0x102F4},
	{0x102FC, 0x1037C},
	{0x10380, 0x10390},
	{0x10800, 0x10828},
	{0x10840, 0x10844},
	{0x10880, 0x10884},
	{0x108C0, 0x108E8},
	{0x10900, 0x10928},
	{0x10940, 0x10944},
	{0x10980, 0x10984},
	{0x109C0, 0x109E8},
	{0x10A00, 0x10A28},
	{0x10A40, 0x10A50},
	{0x11000, 0x11028},
	{0x11030, 0x11034},
	{0x11038, 0x11068},
	{0x11070, 0x11074},
	{0x11078, 0x110A8},
	{0x110B0, 0x110B4},
	{0x110B8, 0x110E8},
	{0x110F0, 0x110F4},
	{0x110F8, 0x11128},
	{0x11138, 0x11144},
	{0x11178, 0x11180},
	{0x111B8, 0x111C0},
	{0x111F8, 0x11200},
	{0x11238, 0x1123C},
	{0x11270, 0x11274},
	{0x11278, 0x1127C},
	{0x112B0, 0x112B4},
	{0x112B8, 0x112BC},
	{0x112F0, 0x112F4},
	{0x112F8, 0x112FC},
	{0x11338, 0x1133C},
	{0x11378, 0x1137C},
	{0x113B8, 0x113BC},
	{0x113F8, 0x113FC},
	{0x11438, 0x11440},
	{0x11478, 0x11480},
	{0x114B8, 0x114BC},
	{0x114F8, 0x114FC},
	{0x11538, 0x1153C},
	{0x11578, 0x1157C},
	{0x115B8, 0x115BC},
	{0x115F8, 0x115FC},
	{0x11638, 0x1163C},
	{0x11678, 0x1167C},
	{0x116B8, 0x116BC},
	{0x116F8, 0x116FC},
	{0x11738, 0x1173C},
	{0x11778, 0x1177C},
	{0x117B8, 0x117BC},
	{0x117F8, 0x117FC},
	{0x17000, 0x1701C},
	{0x17020, 0x170AC},
	{0x18000, 0x18050},
	{0x18054, 0x18074},
	{0x18080, 0x180D4},
	{0x180DC, 0x18104},
	{0x18108, 0x1813C},
	{0x18144, 0x18148},
	{0x18168, 0x18174},
	{0x18178, 0x18180},
	{0x181C8, 0x181E0},
	{0x181E4, 0x181E8},
	{0x181EC, 0x1820C},
	{0x1825C, 0x18280},
	{0x18284, 0x18290},
	{0x18294, 0x182A0},
	{0x18300, 0x18304},
	{0x18314, 0x18320},
	{0x18328, 0x18350},
	{0x1835C, 0x1836C},
	{0x18370, 0x18390},
	{0x18398, 0x183AC},
	{0x183BC, 0x183D8},
	{0x183DC, 0x183F4},
	{0x18400, 0x186F4},
	{0x186F8, 0x1871C},
	{0x18720, 0x18790},
	{0x19800, 0x19830},
	{0x19834, 0x19840},
	{0x19880, 0x1989C},
	{0x198A4, 0x198B0},
	{0x198BC, 0x19900},
	{0x19C00, 0x19C88},
	{0x19D00, 0x19D20},
	{0x19E00, 0x19E7C},
	{0x19E80, 0x19E94},
	{0x19E98, 0x19EAC},
	{0x19EB0, 0x19EBC},
	{0x19F70, 0x19F74},
	{0x19F80, 0x19F8C},
	{0x19FA0, 0x19FB4},
	{0x19FC0, 0x19FD8},
	{0x1A000, 0x1A200},
	{0x1A204, 0x1A210},
	{0x1A228, 0x1A22C},
	{0x1A230, 0x1A248},
	{0x1A250, 0x1A270},
	{0x1A280, 0x1A290},
	{0x1A2A0, 0x1A2A4},
	{0x1A2C0, 0x1A2EC},
	{0x1A300, 0x1A3BC},
	{0x1A3F0, 0x1A3F4},
	{0x1A3F8, 0x1A434},
	{0x1A438, 0x1A444},
	{0x1A448, 0x1A468},
	{0x1A580, 0x1A58C},
	{0x1A644, 0x1A654},
	{0x1A670, 0x1A698},
	{0x1A6AC, 0x1A6B0},
	{0x1A6D0, 0x1A6D4},
	{0x1A6EC, 0x1A70C},
	{0x1A710, 0x1A738},
	{0x1A7C0, 0x1A7D0},
	{0x1A7D4, 0x1A7D8},
	{0x1A7DC, 0x1A7E4},
	{0x1A7F0, 0x1A7F8},
	{0x1A888, 0x1A89C},
	{0x1A8A8, 0x1A8AC},
	{0x1A8C0, 0x1A8DC},
	{0x1A8F0, 0x1A8FC},
	{0x1AE04, 0x1AE08},
	{0x1AE18, 0x1AE24},
	{0x1AF80, 0x1AF8C},
	{0x1AFA0, 0x1AFB4},
	{0x1B000, 0x1B200},
	{0x1B284, 0x1B288},
	{0x1B2D0, 0x1B2D8},
	{0x1B2DC, 0x1B2EC},
	{0x1B300, 0x1B340},
	{0x1B374, 0x1B378},
	{0x1B380, 0x1B384},
	{0x1B388, 0x1B38C},
	{0x1B404, 0x1B408},
	{0x1B420, 0x1B428},
	{0x1B440, 0x1B444},
	{0x1B448, 0x1B44C},
	{0x1B450, 0x1B458},
	{0x1B45C, 0x1B468},
	{0x1B584, 0x1B58C},
	{0x1B68C, 0x1B690},
	{0x1B6AC, 0x1B6B0},
	{0x1B7F0, 0x1B7F8},
	{0x1C800, 0x1CC00},
	{0x1CE00, 0x1CE04},
	{0x1CF80, 0x1CF84},
	{0x1D200, 0x1D800},
	{0x1E000, 0x20014},
	{0x20100, 0x20124},
	{0x21400, 0x217A8},
	{0x21800, 0x21BA8},
	{0x21C00, 0x21FA8},
	{0x22000, 0x223A8},
	{0x22400, 0x227A8},
	{0x22800, 0x22BA8},
	{0x22C00, 0x22FA8},
	{0x23000, 0x233A8},
	{0x24000, 0x24034},
	{0x26000, 0x26064},
	{0x27000, 0x27024},
	{0x34000, 0x3400C},
	{0x34400, 0x3445C},
	{0x34800, 0x3485C},
	{0x34C00, 0x34C5C},
	{0x35000, 0x3505C},
	{0x35400, 0x3545C},
	{0x35800, 0x3585C},
	{0x35C00, 0x35C5C},
	{0x36000, 0x3605C},
	{0x38000, 0x38064},
	{0x38070, 0x380E0},
	{0x3A000, 0x3A064},
	{0x40000, 0x400A4},
	{0x80000, 0x8000C},
	{0x80010, 0x80020},
};

static const struct ath10k_mem_section qca6174_hw30_register_sections[] = {
	{0x800, 0x810},
	{0x820, 0x82C},
	{0x830, 0x8F4},
	{0x90C, 0x91C},
	{0xA14, 0xA18},
	{0xA84, 0xA94},
	{0xAA8, 0xAD4},
	{0xADC, 0xB40},
	{0x1000, 0x10A4},
	{0x10BC, 0x111C},
	{0x1134, 0x1138},
	{0x1144, 0x114C},
	{0x1150, 0x115C},
	{0x1160, 0x1178},
	{0x1240, 0x1260},
	{0x2000, 0x207C},
	{0x3000, 0x3014},
	{0x4000, 0x4014},
	{0x5000, 0x5124},
	{0x6000, 0x6040},
	{0x6080, 0x60CC},
	{0x6100, 0x611C},
	{0x6140, 0x61D8},
	{0x6200, 0x6238},
	{0x6240, 0x628C},
	{0x62C0, 0x62EC},
	{0x6380, 0x63E8},
	{0x6400, 0x6440},
	{0x6480, 0x64CC},
	{0x6500, 0x651C},
	{0x6540, 0x6580},
	{0x6600, 0x6638},
	{0x6640, 0x668C},
	{0x66C0, 0x66EC},
	{0x6780, 0x67E8},
	{0x7080, 0x708C},
	{0x70C0, 0x70C8},
	{0x7400, 0x741C},
	{0x7440, 0x7454},
	{0x7800, 0x7818},
	{0x8000, 0x8004},
	{0x8010, 0x8064},
	{0x8080, 0x8084},
	{0x80A0, 0x80A4},
	{0x80C0, 0x80C4},
	{0x80E0, 0x80F4},
	{0x8100, 0x8104},
	{0x8110, 0x812C},
	{0x9000, 0x9004},
	{0x9800, 0x982C},
	{0x9830, 0x9838},
	{0x9840, 0x986C},
	{0x9870, 0x9898},
	{0x9A00, 0x9C00},
	{0xD580, 0xD59C},
	{0xF000, 0xF0E0},
	{0xF140, 0xF190},
	{0xF250, 0xF25C},
	{0xF260, 0xF268},
	{0xF26C, 0xF2A8},
	{0x10008, 0x1000C},
	{0x10014, 0x10018},
	{0x1001C, 0x10020},
	{0x10024, 0x10028},
	{0x10030, 0x10034},
	{0x10040, 0x10054},
	{0x10058, 0x1007C},
	{0x10080, 0x100C4},
	{0x100C8, 0x10114},
	{0x1012C, 0x10130},
	{0x10138, 0x10144},
	{0x10200, 0x10220},
	{0x10230, 0x10250},
	{0x10260, 0x10280},
	{0x10290, 0x102B0},
	{0x102C0, 0x102DC},
	{0x102E0, 0x102F4},
	{0x102FC, 0x1037C},
	{0x10380, 0x10390},
	{0x10800, 0x10828},
	{0x10840, 0x10844},
	{0x10880, 0x10884},
	{0x108C0, 0x108E8},
	{0x10900, 0x10928},
	{0x10940, 0x10944},
	{0x10980, 0x10984},
	{0x109C0, 0x109E8},
	{0x10A00, 0x10A28},
	{0x10A40, 0x10A50},
	{0x11000, 0x11028},
	{0x11030, 0x11034},
	{0x11038, 0x11068},
	{0x11070, 0x11074},
	{0x11078, 0x110A8},
	{0x110B0, 0x110B4},
	{0x110B8, 0x110E8},
	{0x110F0, 0x110F4},
	{0x110F8, 0x11128},
	{0x11138, 0x11144},
	{0x11178, 0x11180},
	{0x111B8, 0x111C0},
	{0x111F8, 0x11200},
	{0x11238, 0x1123C},
	{0x11270, 0x11274},
	{0x11278, 0x1127C},
	{0x112B0, 0x112B4},
	{0x112B8, 0x112BC},
	{0x112F0, 0x112F4},
	{0x112F8, 0x112FC},
	{0x11338, 0x1133C},
	{0x11378, 0x1137C},
	{0x113B8, 0x113BC},
	{0x113F8, 0x113FC},
	{0x11438, 0x11440},
	{0x11478, 0x11480},
	{0x114B8, 0x114BC},
	{0x114F8, 0x114FC},
	{0x11538, 0x1153C},
	{0x11578, 0x1157C},
	{0x115B8, 0x115BC},
	{0x115F8, 0x115FC},
	{0x11638, 0x1163C},
	{0x11678, 0x1167C},
	{0x116B8, 0x116BC},
	{0x116F8, 0x116FC},
	{0x11738, 0x1173C},
	{0x11778, 0x1177C},
	{0x117B8, 0x117BC},
	{0x117F8, 0x117FC},
	{0x17000, 0x1701C},
	{0x17020, 0x170AC},
	{0x18000, 0x18050},
	{0x18054, 0x18074},
	{0x18080, 0x180D4},
	{0x180DC, 0x18104},
	{0x18108, 0x1813C},
	{0x18144, 0x18148},
	{0x18168, 0x18174},
	{0x18178, 0x18180},
	{0x181C8, 0x181E0},
	{0x181E4, 0x181E8},
	{0x181EC, 0x1820C},
	{0x1825C, 0x18280},
	{0x18284, 0x18290},
	{0x18294, 0x182A0},
	{0x18300, 0x18304},
	{0x18314, 0x18320},
	{0x18328, 0x18350},
	{0x1835C, 0x1836C},
	{0x18370, 0x18390},
	{0x18398, 0x183AC},
	{0x183BC, 0x183D8},
	{0x183DC, 0x183F4},
	{0x18400, 0x186F4},
	{0x186F8, 0x1871C},
	{0x18720, 0x18790},
	{0x19800, 0x19830},
	{0x19834, 0x19840},
	{0x19880, 0x1989C},
	{0x198A4, 0x198B0},
	{0x198BC, 0x19900},
	{0x19C00, 0x19C88},
	{0x19D00, 0x19D20},
	{0x19E00, 0x19E7C},
	{0x19E80, 0x19E94},
	{0x19E98, 0x19EAC},
	{0x19EB0, 0x19EBC},
	{0x19F70, 0x19F74},
	{0x19F80, 0x19F8C},
	{0x19FA0, 0x19FB4},
	{0x19FC0, 0x19FD8},
	{0x1A000, 0x1A200},
	{0x1A204, 0x1A210},
	{0x1A228, 0x1A22C},
	{0x1A230, 0x1A248},
	{0x1A250, 0x1A270},
	{0x1A280, 0x1A290},
	{0x1A2A0, 0x1A2A4},
	{0x1A2C0, 0x1A2EC},
	{0x1A300, 0x1A3BC},
	{0x1A3F0, 0x1A3F4},
	{0x1A3F8, 0x1A434},
	{0x1A438, 0x1A444},
	{0x1A448, 0x1A468},
	{0x1A580, 0x1A58C},
	{0x1A644, 0x1A654},
	{0x1A670, 0x1A698},
	{0x1A6AC, 0x1A6B0},
	{0x1A6D0, 0x1A6D4},
	{0x1A6EC, 0x1A70C},
	{0x1A710, 0x1A738},
	{0x1A7C0, 0x1A7D0},
	{0x1A7D4, 0x1A7D8},
	{0x1A7DC, 0x1A7E4},
	{0x1A7F0, 0x1A7F8},
	{0x1A888, 0x1A89C},
	{0x1A8A8, 0x1A8AC},
	{0x1A8C0, 0x1A8DC},
	{0x1A8F0, 0x1A8FC},
	{0x1AE04, 0x1AE08},
	{0x1AE18, 0x1AE24},
	{0x1AF80, 0x1AF8C},
	{0x1AFA0, 0x1AFB4},
	{0x1B000, 0x1B200},
	{0x1B284, 0x1B288},
	{0x1B2D0, 0x1B2D8},
	{0x1B2DC, 0x1B2EC},
	{0x1B300, 0x1B340},
	{0x1B374, 0x1B378},
	{0x1B380, 0x1B384},
	{0x1B388, 0x1B38C},
	{0x1B404, 0x1B408},
	{0x1B420, 0x1B428},
	{0x1B440, 0x1B444},
	{0x1B448, 0x1B44C},
	{0x1B450, 0x1B458},
	{0x1B45C, 0x1B468},
	{0x1B584, 0x1B58C},
	{0x1B68C, 0x1B690},
	{0x1B6AC, 0x1B6B0},
	{0x1B7F0, 0x1B7F8},
	{0x1C800, 0x1CC00},
	{0x1CE00, 0x1CE04},
	{0x1CF80, 0x1CF84},
	{0x1D200, 0x1D800},
	{0x1E000, 0x20014},
	{0x20100, 0x20124},
	{0x21400, 0x217A8},
	{0x21800, 0x21BA8},
	{0x21C00, 0x21FA8},
	{0x22000, 0x223A8},
	{0x22400, 0x227A8},
	{0x22800, 0x22BA8},
	{0x22C00, 0x22FA8},
	{0x23000, 0x233A8},
	{0x24000, 0x24034},
	{0x26000, 0x26064},
	{0x27000, 0x27024},
	{0x34000, 0x3400C},
	{0x34400, 0x3445C},
	{0x34800, 0x3485C},
	{0x34C00, 0x34C5C},
	{0x35000, 0x3505C},
	{0x35400, 0x3545C},
	{0x35800, 0x3585C},
	{0x35C00, 0x35C5C},
	{0x36000, 0x3605C},
	{0x38000, 0x38064},
	{0x38070, 0x380E0},
	{0x3A000, 0x3A074},
	{0x40000, 0x400A4},
	{0x80000, 0x8000C},
	{0x80010, 0x80020},
};

static const struct ath10k_mem_region qca6174_hw10_mem_regions[] = {
	{
		.type = ATH10K_MEM_REGION_TYPE_DRAM,
		.start = 0x400000,
		.len = 0x70000,
		.name = "DRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_REG,

		/* RTC_SOC_BASE_ADDRESS */
		.start = 0x0,

		/* WLAN_MBOX_BASE_ADDRESS - RTC_SOC_BASE_ADDRESS */
		.len = 0x800 - 0x0,

		.name = "REG_PART1",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_REG,

		/* STEREO_BASE_ADDRESS */
		.start = 0x27000,

		/* USB_BASE_ADDRESS - STEREO_BASE_ADDRESS */
		.len = 0x60000 - 0x27000,

		.name = "REG_PART2",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
};

static const struct ath10k_mem_region qca6174_hw21_mem_regions[] = {
	{
		.type = ATH10K_MEM_REGION_TYPE_DRAM,
		.start = 0x400000,
		.len = 0x70000,
		.name = "DRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_AXI,
		.start = 0xa0000,
		.len = 0x18000,
		.name = "AXI",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_REG,
		.start = 0x800,
		.len = 0x80020 - 0x800,
		.name = "REG_TOTAL",
		.section_table = {
			.sections = qca6174_hw21_register_sections,
			.size = ARRAY_SIZE(qca6174_hw21_register_sections),
		},
	},
};

static const struct ath10k_mem_region qca6174_hw30_mem_regions[] = {
	{
		.type = ATH10K_MEM_REGION_TYPE_DRAM,
		.start = 0x400000,
		.len = 0xa8000,
		.name = "DRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_AXI,
		.start = 0xa0000,
		.len = 0x18000,
		.name = "AXI",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_REG,
		.start = 0x800,
		.len = 0x80020 - 0x800,
		.name = "REG_TOTAL",
		.section_table = {
			.sections = qca6174_hw30_register_sections,
			.size = ARRAY_SIZE(qca6174_hw30_register_sections),
		},
	},

	/* IRAM dump must be put last */
	{
		.type = ATH10K_MEM_REGION_TYPE_IRAM1,
		.start = 0x00980000,
		.len = 0x00080000,
		.name = "IRAM1",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IRAM2,
		.start = 0x00a00000,
		.len = 0x00040000,
		.name = "IRAM2",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
};

static const struct ath10k_mem_region qca988x_hw20_mem_regions[] = {
	{
		.type = ATH10K_MEM_REGION_TYPE_DRAM,
		.start = 0x400000,
		.len = 0x50000,
		.name = "DRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_REG,
		.start = 0x4000,
		.len = 0x2000,
		.name = "REG_PART1",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_REG,
		.start = 0x8000,
		.len = 0x58000,
		.name = "REG_PART2",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
};

static const struct ath10k_mem_region qca99x0_hw20_mem_regions[] = {
	{
		.type = ATH10K_MEM_REGION_TYPE_DRAM,
		.start = 0x400000,
		.len = 0x60000,
		.name = "DRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_REG,
		.start = 0x98000,
		.len = 0x50000,
		.name = "IRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOSRAM,
		.start = 0xC0000,
		.len = 0x40000,
		.name = "SRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x30000,
		.len = 0x7000,
		.name = "APB REG 1",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x3f000,
		.len = 0x3000,
		.name = "APB REG 2",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x43000,
		.len = 0x3000,
		.name = "WIFI REG",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x4A000,
		.len = 0x5000,
		.name = "CE REG",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x80000,
		.len = 0x6000,
		.name = "SOC REG",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
};

static const struct ath10k_mem_region qca9984_hw10_mem_regions[] = {
	{
		.type = ATH10K_MEM_REGION_TYPE_DRAM,
		.start = 0x400000,
		.len = 0x80000,
		.name = "DRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_REG,
		.start = 0x98000,
		.len = 0x50000,
		.name = "IRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOSRAM,
		.start = 0xC0000,
		.len = 0x40000,
		.name = "SRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x30000,
		.len = 0x7000,
		.name = "APB REG 1",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x3f000,
		.len = 0x3000,
		.name = "APB REG 2",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x43000,
		.len = 0x3000,
		.name = "WIFI REG",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x4A000,
		.len = 0x5000,
		.name = "CE REG",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x80000,
		.len = 0x6000,
		.name = "SOC REG",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
};

static const struct ath10k_mem_section ipq4019_soc_reg_range[] = {
	{0x080000, 0x080004},
	{0x080020, 0x080024},
	{0x080028, 0x080050},
	{0x0800d4, 0x0800ec},
	{0x08010c, 0x080118},
	{0x080284, 0x080290},
	{0x0802a8, 0x0802b8},
	{0x0802dc, 0x08030c},
	{0x082000, 0x083fff}
};

static const struct ath10k_mem_region qca4019_hw10_mem_regions[] = {
	{
		.type = ATH10K_MEM_REGION_TYPE_DRAM,
		.start = 0x400000,
		.len = 0x68000,
		.name = "DRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_REG,
		.start = 0xC0000,
		.len = 0x40000,
		.name = "SRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_REG,
		.start = 0x98000,
		.len = 0x50000,
		.name = "IRAM",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x30000,
		.len = 0x7000,
		.name = "APB REG 1",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x3f000,
		.len = 0x3000,
		.name = "APB REG 2",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x43000,
		.len = 0x3000,
		.name = "WIFI REG",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_IOREG,
		.start = 0x4A000,
		.len = 0x5000,
		.name = "CE REG",
		.section_table = {
			.sections = NULL,
			.size = 0,
		},
	},
	{
		.type = ATH10K_MEM_REGION_TYPE_REG,
		.start = 0x080000,
		.len = 0x083fff - 0x080000,
		.name = "REG_TOTAL",
		.section_table = {
			.sections = ipq4019_soc_reg_range,
			.size = ARRAY_SIZE(ipq4019_soc_reg_range),
		},
	},
};

static const struct ath10k_hw_mem_layout hw_mem_layouts[] = {
	{
		.hw_id = QCA6174_HW_1_0_VERSION,
		.hw_rev = ATH10K_HW_QCA6174,
		.region_table = {
			.regions = qca6174_hw10_mem_regions,
			.size = ARRAY_SIZE(qca6174_hw10_mem_regions),
		},
	},
	{
		.hw_id = QCA6174_HW_1_1_VERSION,
		.hw_rev = ATH10K_HW_QCA6174,
		.region_table = {
			.regions = qca6174_hw10_mem_regions,
			.size = ARRAY_SIZE(qca6174_hw10_mem_regions),
		},
	},
	{
		.hw_id = QCA6174_HW_1_3_VERSION,
		.hw_rev = ATH10K_HW_QCA6174,
		.region_table = {
			.regions = qca6174_hw10_mem_regions,
			.size = ARRAY_SIZE(qca6174_hw10_mem_regions),
		},
	},
	{
		.hw_id = QCA6174_HW_2_1_VERSION,
		.hw_rev = ATH10K_HW_QCA6174,
		.region_table = {
			.regions = qca6174_hw21_mem_regions,
			.size = ARRAY_SIZE(qca6174_hw21_mem_regions),
		},
	},
	{
		.hw_id = QCA6174_HW_3_0_VERSION,
		.hw_rev = ATH10K_HW_QCA6174,
		.region_table = {
			.regions = qca6174_hw30_mem_regions,
			.size = ARRAY_SIZE(qca6174_hw30_mem_regions),
		},
	},
	{
		.hw_id = QCA6174_HW_3_2_VERSION,
		.hw_rev = ATH10K_HW_QCA6174,
		.region_table = {
			.regions = qca6174_hw30_mem_regions,
			.size = ARRAY_SIZE(qca6174_hw30_mem_regions),
		},
	},
	{
		.hw_id = QCA9377_HW_1_1_DEV_VERSION,
		.hw_rev = ATH10K_HW_QCA9377,
		.region_table = {
			.regions = qca6174_hw30_mem_regions,
			.size = ARRAY_SIZE(qca6174_hw30_mem_regions),
		},
	},
	{
		.hw_id = QCA988X_HW_2_0_VERSION,
		.hw_rev = ATH10K_HW_QCA988X,
		.region_table = {
			.regions = qca988x_hw20_mem_regions,
			.size = ARRAY_SIZE(qca988x_hw20_mem_regions),
		},
	},
	{
		.hw_id = QCA9984_HW_1_0_DEV_VERSION,
		.hw_rev = ATH10K_HW_QCA9984,
		.region_table = {
			.regions = qca9984_hw10_mem_regions,
			.size = ARRAY_SIZE(qca9984_hw10_mem_regions),
		},
	},
	{
		.hw_id = QCA9888_HW_2_0_DEV_VERSION,
		.hw_rev = ATH10K_HW_QCA9888,
		.region_table = {
			.regions = qca9984_hw10_mem_regions,
			.size = ARRAY_SIZE(qca9984_hw10_mem_regions),
		},
	},
	{
		.hw_id = QCA99X0_HW_2_0_DEV_VERSION,
		.hw_rev = ATH10K_HW_QCA99X0,
		.region_table = {
			.regions = qca99x0_hw20_mem_regions,
			.size = ARRAY_SIZE(qca99x0_hw20_mem_regions),
		},
	},
	{
		.hw_id = QCA4019_HW_1_0_DEV_VERSION,
		.hw_rev = ATH10K_HW_QCA4019,
		.region_table = {
			.regions = qca4019_hw10_mem_regions,
			.size = ARRAY_SIZE(qca4019_hw10_mem_regions),
		},
	},
};

static u32 ath10k_coredump_get_ramdump_size(struct ath10k *ar)
{
	const struct ath10k_hw_mem_layout *hw;
	const struct ath10k_mem_region *mem_region;
	size_t size = 0;
	int i;

	hw = ath10k_coredump_get_mem_layout(ar);

	if (!hw)
		return 0;

	mem_region = &hw->region_table.regions[0];

	for (i = 0; i < hw->region_table.size; i++) {
		size += mem_region->len;
		mem_region++;
	}

	/* reserve space for the headers */
	size += hw->region_table.size * sizeof(struct ath10k_dump_ram_data_hdr);

	/* make sure it is aligned 16 bytes for debug message print out */
	size = ALIGN(size, 16);

	return size;
}

const struct ath10k_hw_mem_layout *ath10k_coredump_get_mem_layout(struct ath10k *ar)
{
	int i;

	if (!test_bit(ATH10K_FW_CRASH_DUMP_RAM_DATA, &ath10k_coredump_mask))
		return NULL;

	if (WARN_ON(ar->target_version == 0))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(hw_mem_layouts); i++) {
		if (ar->target_version == hw_mem_layouts[i].hw_id &&
		    ar->hw_rev == hw_mem_layouts[i].hw_rev)
			return &hw_mem_layouts[i];
	}

	return NULL;
}
EXPORT_SYMBOL(ath10k_coredump_get_mem_layout);

struct ath10k_fw_crash_data *ath10k_coredump_new(struct ath10k *ar)
{
	struct ath10k_fw_crash_data *crash_data = ar->coredump.fw_crash_data;

	lockdep_assert_held(&ar->data_lock);

	if (ath10k_coredump_mask == 0)
		/* coredump disabled */
		return NULL;

	guid_gen(&crash_data->guid);
	ktime_get_real_ts64(&crash_data->timestamp);

	return crash_data;
}
EXPORT_SYMBOL(ath10k_coredump_new);

static struct ath10k_dump_file_data *ath10k_coredump_build(struct ath10k *ar)
{
	struct ath10k_fw_crash_data *crash_data = ar->coredump.fw_crash_data;
	struct ath10k_ce_crash_hdr *ce_hdr;
	struct ath10k_dump_file_data *dump_data;
	struct ath10k_tlv_dump_data *dump_tlv;
	size_t hdr_len = sizeof(*dump_data);
	size_t len, sofar = 0;
	unsigned char *buf;

	len = hdr_len;

	if (test_bit(ATH10K_FW_CRASH_DUMP_REGISTERS, &ath10k_coredump_mask))
		len += sizeof(*dump_tlv) + sizeof(crash_data->registers);

	if (test_bit(ATH10K_FW_CRASH_DUMP_CE_DATA, &ath10k_coredump_mask))
		len += sizeof(*dump_tlv) + sizeof(*ce_hdr) +
			CE_COUNT * sizeof(ce_hdr->entries[0]);

	if (test_bit(ATH10K_FW_CRASH_DUMP_RAM_DATA, &ath10k_coredump_mask))
		len += sizeof(*dump_tlv) + crash_data->ramdump_buf_len;

	sofar += hdr_len;

	/* This is going to get big when we start dumping FW RAM and such,
	 * so go ahead and use vmalloc.
	 */
	buf = vzalloc(len);
	if (!buf)
		return NULL;

	spin_lock_bh(&ar->data_lock);

	dump_data = (struct ath10k_dump_file_data *)(buf);
	strlcpy(dump_data->df_magic, "ATH10K-FW-DUMP",
		sizeof(dump_data->df_magic));
	dump_data->len = cpu_to_le32(len);

	dump_data->version = cpu_to_le32(ATH10K_FW_CRASH_DUMP_VERSION);

	guid_copy(&dump_data->guid, &crash_data->guid);
	dump_data->chip_id = cpu_to_le32(ar->chip_id);
	dump_data->bus_type = cpu_to_le32(0);
	dump_data->target_version = cpu_to_le32(ar->target_version);
	dump_data->fw_version_major = cpu_to_le32(ar->fw_version_major);
	dump_data->fw_version_minor = cpu_to_le32(ar->fw_version_minor);
	dump_data->fw_version_release = cpu_to_le32(ar->fw_version_release);
	dump_data->fw_version_build = cpu_to_le32(ar->fw_version_build);
	dump_data->phy_capability = cpu_to_le32(ar->phy_capability);
	dump_data->hw_min_tx_power = cpu_to_le32(ar->hw_min_tx_power);
	dump_data->hw_max_tx_power = cpu_to_le32(ar->hw_max_tx_power);
	dump_data->ht_cap_info = cpu_to_le32(ar->ht_cap_info);
	dump_data->vht_cap_info = cpu_to_le32(ar->vht_cap_info);
	dump_data->num_rf_chains = cpu_to_le32(ar->num_rf_chains);

	strlcpy(dump_data->fw_ver, ar->hw->wiphy->fw_version,
		sizeof(dump_data->fw_ver));

	dump_data->kernel_ver_code = 0;
	strlcpy(dump_data->kernel_ver, init_utsname()->release,
		sizeof(dump_data->kernel_ver));

	dump_data->tv_sec = cpu_to_le64(crash_data->timestamp.tv_sec);
	dump_data->tv_nsec = cpu_to_le64(crash_data->timestamp.tv_nsec);

	if (test_bit(ATH10K_FW_CRASH_DUMP_REGISTERS, &ath10k_coredump_mask)) {
		dump_tlv = (struct ath10k_tlv_dump_data *)(buf + sofar);
		dump_tlv->type = cpu_to_le32(ATH10K_FW_CRASH_DUMP_REGISTERS);
		dump_tlv->tlv_len = cpu_to_le32(sizeof(crash_data->registers));
		memcpy(dump_tlv->tlv_data, &crash_data->registers,
		       sizeof(crash_data->registers));
		sofar += sizeof(*dump_tlv) + sizeof(crash_data->registers);
	}

	if (test_bit(ATH10K_FW_CRASH_DUMP_CE_DATA, &ath10k_coredump_mask)) {
		dump_tlv = (struct ath10k_tlv_dump_data *)(buf + sofar);
		dump_tlv->type = cpu_to_le32(ATH10K_FW_CRASH_DUMP_CE_DATA);
		dump_tlv->tlv_len = cpu_to_le32(sizeof(*ce_hdr) +
						CE_COUNT * sizeof(ce_hdr->entries[0]));
		ce_hdr = (struct ath10k_ce_crash_hdr *)(dump_tlv->tlv_data);
		ce_hdr->ce_count = cpu_to_le32(CE_COUNT);
		memset(ce_hdr->reserved, 0, sizeof(ce_hdr->reserved));
		memcpy(ce_hdr->entries, crash_data->ce_crash_data,
		       CE_COUNT * sizeof(ce_hdr->entries[0]));
		sofar += sizeof(*dump_tlv) + sizeof(*ce_hdr) +
			CE_COUNT * sizeof(ce_hdr->entries[0]);
	}

	/* Gather ram dump */
	if (test_bit(ATH10K_FW_CRASH_DUMP_RAM_DATA, &ath10k_coredump_mask)) {
		dump_tlv = (struct ath10k_tlv_dump_data *)(buf + sofar);
		dump_tlv->type = cpu_to_le32(ATH10K_FW_CRASH_DUMP_RAM_DATA);
		dump_tlv->tlv_len = cpu_to_le32(crash_data->ramdump_buf_len);
		memcpy(dump_tlv->tlv_data, crash_data->ramdump_buf,
		       crash_data->ramdump_buf_len);
		sofar += sizeof(*dump_tlv) + crash_data->ramdump_buf_len;
	}

	spin_unlock_bh(&ar->data_lock);

	return dump_data;
}

int ath10k_coredump_submit(struct ath10k *ar)
{
	struct ath10k_dump_file_data *dump;

	if (ath10k_coredump_mask == 0)
		/* coredump disabled */
		return 0;

	dump = ath10k_coredump_build(ar);
	if (!dump) {
		ath10k_warn(ar, "no crash dump data found for devcoredump");
		return -ENODATA;
	}

	dev_coredumpv(ar->dev, dump, le32_to_cpu(dump->len), GFP_KERNEL);

	return 0;
}

int ath10k_coredump_create(struct ath10k *ar)
{
	if (ath10k_coredump_mask == 0)
		/* coredump disabled */
		return 0;

	ar->coredump.fw_crash_data = vzalloc(sizeof(*ar->coredump.fw_crash_data));
	if (!ar->coredump.fw_crash_data)
		return -ENOMEM;

	return 0;
}

int ath10k_coredump_register(struct ath10k *ar)
{
	struct ath10k_fw_crash_data *crash_data = ar->coredump.fw_crash_data;

	if (test_bit(ATH10K_FW_CRASH_DUMP_RAM_DATA, &ath10k_coredump_mask)) {
		crash_data->ramdump_buf_len = ath10k_coredump_get_ramdump_size(ar);

		crash_data->ramdump_buf = vzalloc(crash_data->ramdump_buf_len);
		if (!crash_data->ramdump_buf)
			return -ENOMEM;
	}

	return 0;
}

void ath10k_coredump_unregister(struct ath10k *ar)
{
	struct ath10k_fw_crash_data *crash_data = ar->coredump.fw_crash_data;

	vfree(crash_data->ramdump_buf);
}

void ath10k_coredump_destroy(struct ath10k *ar)
{
	if (ar->coredump.fw_crash_data->ramdump_buf) {
		vfree(ar->coredump.fw_crash_data->ramdump_buf);
		ar->coredump.fw_crash_data->ramdump_buf = NULL;
		ar->coredump.fw_crash_data->ramdump_buf_len = 0;
	}

	vfree(ar->coredump.fw_crash_data);
	ar->coredump.fw_crash_data = NULL;
}
