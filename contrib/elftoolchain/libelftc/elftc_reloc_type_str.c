/*-
 * Copyright (c) 2009-2015 Kai Wang
 * Copyright (c) 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Ed Maste under sponsorship
 * of the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <libelftc.h>
#include <stdio.h>

const char *
elftc_reloc_type_str(unsigned int mach, unsigned int type)
{
	static char s_type[32];

	switch(mach) {
	case EM_386:
	case EM_IAMCU:
		switch(type) {
		case 0: return "R_386_NONE";
		case 1: return "R_386_32";
		case 2: return "R_386_PC32";
		case 3: return "R_386_GOT32";
		case 4: return "R_386_PLT32";
		case 5: return "R_386_COPY";
		case 6: return "R_386_GLOB_DAT";
		case 7: return "R_386_JUMP_SLOT";
		case 8: return "R_386_RELATIVE";
		case 9: return "R_386_GOTOFF";
		case 10: return "R_386_GOTPC";
		case 11: return "R_386_32PLT"; /* Not in psabi */
		case 14: return "R_386_TLS_TPOFF";
		case 15: return "R_386_TLS_IE";
		case 16: return "R_386_TLS_GOTIE";
		case 17: return "R_386_TLS_LE";
		case 18: return "R_386_TLS_GD";
		case 19: return "R_386_TLS_LDM";
		case 20: return "R_386_16";
		case 21: return "R_386_PC16";
		case 22: return "R_386_8";
		case 23: return "R_386_PC8";
		case 24: return "R_386_TLS_GD_32";
		case 25: return "R_386_TLS_GD_PUSH";
		case 26: return "R_386_TLS_GD_CALL";
		case 27: return "R_386_TLS_GD_POP";
		case 28: return "R_386_TLS_LDM_32";
		case 29: return "R_386_TLS_LDM_PUSH";
		case 30: return "R_386_TLS_LDM_CALL";
		case 31: return "R_386_TLS_LDM_POP";
		case 32: return "R_386_TLS_LDO_32";
		case 33: return "R_386_TLS_IE_32";
		case 34: return "R_386_TLS_LE_32";
		case 35: return "R_386_TLS_DTPMOD32";
		case 36: return "R_386_TLS_DTPOFF32";
		case 37: return "R_386_TLS_TPOFF32";
		case 38: return "R_386_SIZE32";
		case 39: return "R_386_TLS_GOTDESC";
		case 40: return "R_386_TLS_DESC_CALL";
		case 41: return "R_386_TLS_DESC";
		case 42: return "R_386_IRELATIVE";
		case 43: return "R_386_GOT32X";
		}
		break;
	case EM_AARCH64:
		switch(type) {
		case 0: return "R_AARCH64_NONE";
		case 257: return "R_AARCH64_ABS64";
		case 258: return "R_AARCH64_ABS32";
		case 259: return "R_AARCH64_ABS16";
		case 260: return "R_AARCH64_PREL64";
		case 261: return "R_AARCH64_PREL32";
		case 262: return "R_AARCH64_PREL16";
		case 263: return "R_AARCH64_MOVW_UABS_G0";
		case 264: return "R_AARCH64_MOVW_UABS_G0_NC";
		case 265: return "R_AARCH64_MOVW_UABS_G1";
		case 266: return "R_AARCH64_MOVW_UABS_G1_NC";
		case 267: return "R_AARCH64_MOVW_UABS_G2";
		case 268: return "R_AARCH64_MOVW_UABS_G2_NC";
		case 269: return "R_AARCH64_MOVW_UABS_G3";
		case 270: return "R_AARCH64_MOVW_SABS_G0";
		case 271: return "R_AARCH64_MOVW_SABS_G1";
		case 272: return "R_AARCH64_MOVW_SABS_G2";
		case 273: return "R_AARCH64_LD_PREL_LO19";
		case 274: return "R_AARCH64_ADR_PREL_LO21";
		case 275: return "R_AARCH64_ADR_PREL_PG_HI21";
		case 276: return "R_AARCH64_ADR_PREL_PG_HI21_NC";
		case 277: return "R_AARCH64_ADD_ABS_LO12_NC";
		case 278: return "R_AARCH64_LDST8_ABS_LO12_NC";
		case 279: return "R_AARCH64_TSTBR14";
		case 280: return "R_AARCH64_CONDBR19";
		case 282: return "R_AARCH64_JUMP26";
		case 283: return "R_AARCH64_CALL26";
		case 284: return "R_AARCH64_LDST16_ABS_LO12_NC";
		case 285: return "R_AARCH64_LDST32_ABS_LO12_NC";
		case 286: return "R_AARCH64_LDST64_ABS_LO12_NC";
		case 287: return "R_AARCH64_MOVW_PREL_G0";
		case 288: return "R_AARCH64_MOVW_PREL_G0_NC";
		case 289: return "R_AARCH64_MOVW_PREL_G1";
		case 290: return "R_AARCH64_MOVW_PREL_G1_NC";
		case 291: return "R_AARCH64_MOVW_PREL_G2";
		case 292: return "R_AARCH64_MOVW_PREL_G2_NC";
		case 293: return "R_AARCH64_MOVW_PREL_G3";
		case 299: return "R_AARCH64_LDST128_ABS_LO12_NC";
		case 300: return "R_AARCH64_MOVW_GOTOFF_G0";
		case 301: return "R_AARCH64_MOVW_GOTOFF_G0_NC";
		case 302: return "R_AARCH64_MOVW_GOTOFF_G1";
		case 303: return "R_AARCH64_MOVW_GOTOFF_G1_NC";
		case 304: return "R_AARCH64_MOVW_GOTOFF_G2";
		case 305: return "R_AARCH64_MOVW_GOTOFF_G2_NC";
		case 306: return "R_AARCH64_MOVW_GOTOFF_G3";
		case 307: return "R_AARCH64_GOTREL64";
		case 308: return "R_AARCH64_GOTREL32";
		case 309: return "R_AARCH64_GOT_LD_PREL19";
		case 310: return "R_AARCH64_LD64_GOTOFF_LO15";
		case 311: return "R_AARCH64_ADR_GOT_PAGE";
		case 312: return "R_AARCH64_LD64_GOT_LO12_NC";
		case 313: return "R_AARCH64_LD64_GOTPAGE_LO15";
		case 560: return "R_AARCH64_TLSDESC_LD_PREL19";
		case 561: return "R_AARCH64_TLSDESC_ADR_PREL21";
		case 562: return "R_AARCH64_TLSDESC_ADR_PAGE21";
		case 563: return "R_AARCH64_TLSDESC_LD64_LO12";
		case 564: return "R_AARCH64_TLSDESC_ADD_LO12";
		case 565: return "R_AARCH64_TLSDESC_OFF_G1";
		case 566: return "R_AARCH64_TLSDESC_OFF_G0_NC";
		case 567: return "R_AARCH64_TLSDESC_LDR";
		case 568: return "R_AARCH64_TLSDESC_ADD";
		case 569: return "R_AARCH64_TLSDESC_CALL";
		case 1024: return "R_AARCH64_COPY";
		case 1025: return "R_AARCH64_GLOB_DAT";
		case 1026: return "R_AARCH64_JUMP_SLOT";
		case 1027: return "R_AARCH64_RELATIVE";
		case 1028: return "R_AARCH64_TLS_DTPREL64";
		case 1029: return "R_AARCH64_TLS_DTPMOD64";
		case 1030: return "R_AARCH64_TLS_TPREL64";
		case 1031: return "R_AARCH64_TLSDESC";
		case 1032: return "R_AARCH64_IRELATIVE";
		}
		break;
	case EM_ARM:
		switch(type) {
		case 0: return "R_ARM_NONE";
		case 1: return "R_ARM_PC24"; /* Deprecated */
		case 2: return "R_ARM_ABS32";
		case 3: return "R_ARM_REL32";
		case 4: return "R_ARM_LDR_PC_G0"; /* Also R_ARM_PC13 */
		case 5: return "R_ARM_ABS16";
		case 6: return "R_ARM_ABS12";
		case 7: return "R_ARM_THM_ABS5";
		case 8: return "R_ARM_ABS8";
		case 9: return "R_ARM_SBREL32";
		case 10: return "R_ARM_THM_CALL"; /* Also R_ARM_THM_PC22 */
		case 11: return "R_ARM_THM_PC8";
		case 12: return "R_ARM_BREL_ADJ"; /* Also R_ARM_AMP_VCALL9 */
		case 13: return "R_ARM_TLS_DESC"; /* Also R_ARM_SWI24 */
		case 14: return "R_ARM_THM_SWI8"; /* Obsolete */
		case 15: return "R_ARM_XPC25"; /* Obsolete */
		case 16: return "R_ARM_THM_XPC22"; /* Obsolete */
		case 17: return "R_ARM_TLS_DTPMOD32";
		case 18: return "R_ARM_TLS_DTPOFF32";
		case 19: return "R_ARM_TLS_TPOFF32";
		case 20: return "R_ARM_COPY";
		case 21: return "R_ARM_GLOB_DAT";
		case 22: return "R_ARM_JUMP_SLOT";
		case 23: return "R_ARM_RELATIVE";
		case 24: return "R_ARM_GOTOFF32"; /* Also R_ARM_GOTOFF */
		case 25: return "R_ARM_BASE_PREL"; /* GNU R_ARM_GOTPC */
		case 26: return "R_ARM_GOT_BREL"; /* GNU R_ARM_GOT32 */
		case 27: return "R_ARM_PLT32"; /* Deprecated */
		case 28: return "R_ARM_CALL";
		case 29: return "R_ARM_JUMP24";
		case 30: return "R_ARM_THM_JUMP24";
		case 31: return "R_ARM_BASE_ABS";
		case 32: return "R_ARM_ALU_PCREL_7_0"; /* Obsolete */
		case 33: return "R_ARM_ALU_PCREL_15_8"; /* Obsolete */
		case 34: return "R_ARM_ALU_PCREL_23_15"; /* Obsolete */
		case 35: return "R_ARM_LDR_SBREL_11_0_NC"; /* Deprecated */
		case 36: return "R_ARM_ALU_SBREL_19_12_NC"; /* Deprecated */
		case 37: return "R_ARM_ALU_SBREL_27_20_CK"; /* Deprecated */
		case 38: return "R_ARM_TARGET1";
		case 39: return "R_ARM_SBREL31"; /* Deprecated. */
		case 40: return "R_ARM_V4BX";
		case 41: return "R_ARM_TARGET2";
		case 42: return "R_ARM_PREL31";
		case 43: return "R_ARM_MOVW_ABS_NC";
		case 44: return "R_ARM_MOVT_ABS";
		case 45: return "R_ARM_MOVW_PREL_NC";
		case 46: return "R_ARM_MOVT_PREL";
		case 47: return "R_ARM_THM_MOVW_ABS_NC";
		case 48: return "R_ARM_THM_MOVT_ABS";
		case 49: return "R_ARM_THM_MOVW_PREL_NC";
		case 50: return "R_ARM_THM_MOVT_PREL";
		case 51: return "R_ARM_THM_JUMP19";
		case 52: return "R_ARM_THM_JUMP6";
		case 53: return "R_ARM_THM_ALU_PREL_11_0";
		case 54: return "R_ARM_THM_PC12";
		case 55: return "R_ARM_ABS32_NOI";
		case 56: return "R_ARM_REL32_NOI";
		case 57: return "R_ARM_ALU_PC_G0_NC";
		case 58: return "R_ARM_ALU_PC_G0";
		case 59: return "R_ARM_ALU_PC_G1_NC";
		case 60: return "R_ARM_ALU_PC_G1";
		case 61: return "R_ARM_ALU_PC_G2";
		case 62: return "R_ARM_LDR_PC_G1";
		case 63: return "R_ARM_LDR_PC_G2";
		case 64: return "R_ARM_LDRS_PC_G0";
		case 65: return "R_ARM_LDRS_PC_G1";
		case 66: return "R_ARM_LDRS_PC_G2";
		case 67: return "R_ARM_LDC_PC_G0";
		case 68: return "R_ARM_LDC_PC_G1";
		case 69: return "R_ARM_LDC_PC_G2";
		case 70: return "R_ARM_ALU_SB_G0_NC";
		case 71: return "R_ARM_ALU_SB_G0";
		case 72: return "R_ARM_ALU_SB_G1_NC";
		case 73: return "R_ARM_ALU_SB_G1";
		case 74: return "R_ARM_ALU_SB_G2";
		case 75: return "R_ARM_LDR_SB_G0";
		case 76: return "R_ARM_LDR_SB_G1";
		case 77: return "R_ARM_LDR_SB_G2";
		case 78: return "R_ARM_LDRS_SB_G0";
		case 79: return "R_ARM_LDRS_SB_G1";
		case 80: return "R_ARM_LDRS_SB_G2";
		case 81: return "R_ARM_LDC_SB_G0";
		case 82: return "R_ARM_LDC_SB_G1";
		case 83: return "R_ARM_LDC_SB_G2";
		case 84: return "R_ARM_MOVW_BREL_NC";
		case 85: return "R_ARM_MOVT_BREL";
		case 86: return "R_ARM_MOVW_BREL";
		case 87: return "R_ARM_THM_MOVW_BREL_NC";
		case 88: return "R_ARM_THM_MOVT_BREL";
		case 89: return "R_ARM_THM_MOVW_BREL";
		case 90: return "R_ARM_TLS_GOTDESC";
		case 91: return "R_ARM_TLS_CALL";
		case 92: return "R_ARM_TLS_DESCSEQ";
		case 93: return "R_ARM_THM_TLS_CALL";
		case 94: return "R_ARM_PLT32_ABS";
		case 95: return "R_ARM_GOT_ABS";
		case 96: return "R_ARM_GOT_PREL";
		case 97: return "R_ARM_GOT_BREL12";
		case 98: return "R_ARM_GOTOFF12";
		case 99: return "R_ARM_GOTRELAX";
		case 100: return "R_ARM_GNU_VTENTRY";
		case 101: return "R_ARM_GNU_VTINHERIT";
		case 102: return "R_ARM_THM_JUMP11"; /* Also R_ARM_THM_PC11 */
		case 103: return "R_ARM_THM_JUMP8"; /* Also R_ARM_THM_PC9 */
		case 104: return "R_ARM_TLS_GD32";
		case 105: return "R_ARM_TLS_LDM32";
		case 106: return "R_ARM_TLS_LDO32";
		case 107: return "R_ARM_TLS_IE32";
		case 108: return "R_ARM_TLS_LE32";
		case 109: return "R_ARM_TLS_LDO12";
		case 110: return "R_ARM_TLS_LE12";
		case 111: return "R_ARM_TLS_IE12GP";
		/* 112-127 R_ARM_PRIVATE_<n> */
		case 128: return "R_ARM_ME_TOO"; /* Obsolete */
		case 129: return "R_ARM_THM_TLS_DESCSEQ16";
		case 130: return "R_ARM_THM_TLS_DESCSEQ32";
		case 131: return "R_ARM_THM_GOT_BREL12";
		case 132: return "R_ARM_THM_ALU_ABS_G0_NC";
		case 133: return "R_ARM_THM_ALU_ABS_G1_NC";
		case 134: return "R_ARM_THM_ALU_ABS_G2_NC";
		case 135: return "R_ARM_THM_ALU_ABS_G3";
		/* 136-159 Reserved for future allocation. */
		case 160: return "R_ARM_IRELATIVE";
		/* 161-255 Reserved for future allocation. */
		case 249: return "R_ARM_RXPC25";
		case 250: return "R_ARM_RSBREL32";
		case 251: return "R_ARM_THM_RPC22";
		case 252: return "R_ARM_RREL32";
		case 253: return "R_ARM_RABS32";
		case 254: return "R_ARM_RPC24";
		case 255: return "R_ARM_RBASE";
		}
		break;
	case EM_IA_64:
		switch(type) {
		case 0: return "R_IA_64_NONE";
		case 33: return "R_IA_64_IMM14";
		case 34: return "R_IA_64_IMM22";
		case 35: return "R_IA_64_IMM64";
		case 36: return "R_IA_64_DIR32MSB";
		case 37: return "R_IA_64_DIR32LSB";
		case 38: return "R_IA_64_DIR64MSB";
		case 39: return "R_IA_64_DIR64LSB";
		case 42: return "R_IA_64_GPREL22";
		case 43: return "R_IA_64_GPREL64I";
		case 44: return "R_IA_64_GPREL32MSB";
		case 45: return "R_IA_64_GPREL32LSB";
		case 46: return "R_IA_64_GPREL64MSB";
		case 47: return "R_IA_64_GPREL64LSB";
		case 50: return "R_IA_64_LTOFF22";
		case 51: return "R_IA_64_LTOFF64I";
		case 58: return "R_IA_64_PLTOFF22";
		case 59: return "R_IA_64_PLTOFF64I";
		case 62: return "R_IA_64_PLTOFF64MSB";
		case 63: return "R_IA_64_PLTOFF64LSB";
		case 67: return "R_IA_64_FPTR64I";
		case 68: return "R_IA_64_FPTR32MSB";
		case 69: return "R_IA_64_FPTR32LSB";
		case 70: return "R_IA_64_FPTR64MSB";
		case 71: return "R_IA_64_FPTR64LSB";
		case 72: return "R_IA_64_PCREL60B";
		case 73: return "R_IA_64_PCREL21B";
		case 74: return "R_IA_64_PCREL21M";
		case 75: return "R_IA_64_PCREL21F";
		case 76: return "R_IA_64_PCREL32MSB";
		case 77: return "R_IA_64_PCREL32LSB";
		case 78: return "R_IA_64_PCREL64MSB";
		case 79: return "R_IA_64_PCREL64LSB";
		case 82: return "R_IA_64_LTOFF_FPTR22";
		case 83: return "R_IA_64_LTOFF_FPTR64I";
		case 84: return "R_IA_64_LTOFF_FPTR32MSB";
		case 85: return "R_IA_64_LTOFF_FPTR32LSB";
		case 86: return "R_IA_64_LTOFF_FPTR64MSB";
		case 87: return "R_IA_64_LTOFF_FPTR64LSB";
		case 92: return "R_IA_64_SEGREL32MSB";
		case 93: return "R_IA_64_SEGREL32LSB";
		case 94: return "R_IA_64_SEGREL64MSB";
		case 95: return "R_IA_64_SEGREL64LSB";
		case 100: return "R_IA_64_SECREL32MSB";
		case 101: return "R_IA_64_SECREL32LSB";
		case 102: return "R_IA_64_SECREL64MSB";
		case 103: return "R_IA_64_SECREL64LSB";
		case 108: return "R_IA_64_REL32MSB";
		case 109: return "R_IA_64_REL32LSB";
		case 110: return "R_IA_64_REL64MSB";
		case 111: return "R_IA_64_REL64LSB";
		case 116: return "R_IA_64_LTV32MSB";
		case 117: return "R_IA_64_LTV32LSB";
		case 118: return "R_IA_64_LTV64MSB";
		case 119: return "R_IA_64_LTV64LSB";
		case 121: return "R_IA_64_PCREL21BI";
		case 122: return "R_IA_64_PCREL22";
		case 123: return "R_IA_64_PCREL64I";
		case 128: return "R_IA_64_IPLTMSB";
		case 129: return "R_IA_64_IPLTLSB";
		case 133: return "R_IA_64_SUB";
		case 134: return "R_IA_64_LTOFF22X";
		case 135: return "R_IA_64_LDXMOV";
		case 145: return "R_IA_64_TPREL14";
		case 146: return "R_IA_64_TPREL22";
		case 147: return "R_IA_64_TPREL64I";
		case 150: return "R_IA_64_TPREL64MSB";
		case 151: return "R_IA_64_TPREL64LSB";
		case 154: return "R_IA_64_LTOFF_TPREL22";
		case 166: return "R_IA_64_DTPMOD64MSB";
		case 167: return "R_IA_64_DTPMOD64LSB";
		case 170: return "R_IA_64_LTOFF_DTPMOD22";
		case 177: return "R_IA_64_DTPREL14";
		case 178: return "R_IA_64_DTPREL22";
		case 179: return "R_IA_64_DTPREL64I";
		case 180: return "R_IA_64_DTPREL32MSB";
		case 181: return "R_IA_64_DTPREL32LSB";
		case 182: return "R_IA_64_DTPREL64MSB";
		case 183: return "R_IA_64_DTPREL64LSB";
		case 186: return "R_IA_64_LTOFF_DTPREL22";
		}
		break;
	case EM_MIPS:
		switch(type) {
		case 0: return "R_MIPS_NONE";
		case 1: return "R_MIPS_16";
		case 2: return "R_MIPS_32";
		case 3: return "R_MIPS_REL32";
		case 4: return "R_MIPS_26";
		case 5: return "R_MIPS_HI16";
		case 6: return "R_MIPS_LO16";
		case 7: return "R_MIPS_GPREL16";
		case 8: return "R_MIPS_LITERAL";
		case 9: return "R_MIPS_GOT16";
		case 10: return "R_MIPS_PC16";
		case 11: return "R_MIPS_CALL16";
		case 12: return "R_MIPS_GPREL32";
		case 16: return "R_MIPS_SHIFT5";
		case 17: return "R_MIPS_SHIFT6";
		case 18: return "R_MIPS_64";
		case 19: return "R_MIPS_GOT_DISP";
		case 20: return "R_MIPS_GOT_PAGE";
		case 21: return "R_MIPS_GOT_OFST";
		case 22: return "R_MIPS_GOT_HI16";
		case 23: return "R_MIPS_GOT_LO16";
		case 24: return "R_MIPS_SUB";
		case 28: return "R_MIPS_HIGHER";
		case 29: return "R_MIPS_HIGHEST";
		case 30: return "R_MIPS_CALLHI16";
		case 31: return "R_MIPS_CALLLO16";
		case 37: return "R_MIPS_JALR";
		case 38: return "R_MIPS_TLS_DTPMOD32";
		case 39: return "R_MIPS_TLS_DTPREL32";
		case 40: return "R_MIPS_TLS_DTPMOD64";
		case 41: return "R_MIPS_TLS_DTPREL64";
		case 42: return "R_MIPS_TLS_GD";
		case 43: return "R_MIPS_TLS_LDM";
		case 44: return "R_MIPS_TLS_DTPREL_HI16";
		case 45: return "R_MIPS_TLS_DTPREL_LO16";
		case 46: return "R_MIPS_TLS_GOTTPREL";
		case 47: return "R_MIPS_TLS_TPREL32";
		case 48: return "R_MIPS_TLS_TPREL64";
		case 49: return "R_MIPS_TLS_TPREL_HI16";
		case 50: return "R_MIPS_TLS_TPREL_LO16";
		}
		break;
	case EM_PPC:
		switch(type) {
		case 0: return "R_PPC_NONE";
		case 1: return "R_PPC_ADDR32";
		case 2: return "R_PPC_ADDR24";
		case 3: return "R_PPC_ADDR16";
		case 4: return "R_PPC_ADDR16_LO";
		case 5: return "R_PPC_ADDR16_HI";
		case 6: return "R_PPC_ADDR16_HA";
		case 7: return "R_PPC_ADDR14";
		case 8: return "R_PPC_ADDR14_BRTAKEN";
		case 9: return "R_PPC_ADDR14_BRNTAKEN";
		case 10: return "R_PPC_REL24";
		case 11: return "R_PPC_REL14";
		case 12: return "R_PPC_REL14_BRTAKEN";
		case 13: return "R_PPC_REL14_BRNTAKEN";
		case 14: return "R_PPC_GOT16";
		case 15: return "R_PPC_GOT16_LO";
		case 16: return "R_PPC_GOT16_HI";
		case 17: return "R_PPC_GOT16_HA";
		case 18: return "R_PPC_PLTREL24";
		case 19: return "R_PPC_COPY";
		case 20: return "R_PPC_GLOB_DAT";
		case 21: return "R_PPC_JMP_SLOT";
		case 22: return "R_PPC_RELATIVE";
		case 23: return "R_PPC_LOCAL24PC";
		case 24: return "R_PPC_UADDR32";
		case 25: return "R_PPC_UADDR16";
		case 26: return "R_PPC_REL32";
		case 27: return "R_PPC_PLT32";
		case 28: return "R_PPC_PLTREL32";
		case 29: return "R_PPC_PLT16_LO";
		case 30: return "R_PPC_PLT16_HI";
		case 31: return "R_PPC_PLT16_HA";
		case 32: return "R_PPC_SDAREL16";
		case 33: return "R_PPC_SECTOFF";
		case 34: return "R_PPC_SECTOFF_LO";
		case 35: return "R_PPC_SECTOFF_HI";
		case 36: return "R_PPC_SECTOFF_HA";
		case 67: return "R_PPC_TLS";
		case 68: return "R_PPC_DTPMOD32";
		case 69: return "R_PPC_TPREL16";
		case 70: return "R_PPC_TPREL16_LO";
		case 71: return "R_PPC_TPREL16_HI";
		case 72: return "R_PPC_TPREL16_HA";
		case 73: return "R_PPC_TPREL32";
		case 74: return "R_PPC_DTPREL16";
		case 75: return "R_PPC_DTPREL16_LO";
		case 76: return "R_PPC_DTPREL16_HI";
		case 77: return "R_PPC_DTPREL16_HA";
		case 78: return "R_PPC_DTPREL32";
		case 79: return "R_PPC_GOT_TLSGD16";
		case 80: return "R_PPC_GOT_TLSGD16_LO";
		case 81: return "R_PPC_GOT_TLSGD16_HI";
		case 82: return "R_PPC_GOT_TLSGD16_HA";
		case 83: return "R_PPC_GOT_TLSLD16";
		case 84: return "R_PPC_GOT_TLSLD16_LO";
		case 85: return "R_PPC_GOT_TLSLD16_HI";
		case 86: return "R_PPC_GOT_TLSLD16_HA";
		case 87: return "R_PPC_GOT_TPREL16";
		case 88: return "R_PPC_GOT_TPREL16_LO";
		case 89: return "R_PPC_GOT_TPREL16_HI";
		case 90: return "R_PPC_GOT_TPREL16_HA";
		case 101: return "R_PPC_EMB_NADDR32";
		case 102: return "R_PPC_EMB_NADDR16";
		case 103: return "R_PPC_EMB_NADDR16_LO";
		case 104: return "R_PPC_EMB_NADDR16_HI";
		case 105: return "R_PPC_EMB_NADDR16_HA";
		case 106: return "R_PPC_EMB_SDAI16";
		case 107: return "R_PPC_EMB_SDA2I16";
		case 108: return "R_PPC_EMB_SDA2REL";
		case 109: return "R_PPC_EMB_SDA21";
		case 110: return "R_PPC_EMB_MRKREF";
		case 111: return "R_PPC_EMB_RELSEC16";
		case 112: return "R_PPC_EMB_RELST_LO";
		case 113: return "R_PPC_EMB_RELST_HI";
		case 114: return "R_PPC_EMB_RELST_HA";
		case 115: return "R_PPC_EMB_BIT_FLD";
		case 116: return "R_PPC_EMB_RELSDA";
		}
		break;
	case EM_PPC64:
		switch(type) {
		case 0: return "R_PPC64_NONE";
		case 1: return "R_PPC64_ADDR32";
		case 2: return "R_PPC64_ADDR24";
		case 3: return "R_PPC64_ADDR16";
		case 4: return "R_PPC64_ADDR16_LO";
		case 5: return "R_PPC64_ADDR16_HI";
		case 6: return "R_PPC64_ADDR16_HA";
		case 7: return "R_PPC64_ADDR14";
		case 8: return "R_PPC64_ADDR14_BRTAKEN";
		case 9: return "R_PPC64_ADDR14_BRNTAKEN";
		case 10: return "R_PPC64_REL24";
		case 11: return "R_PPC64_REL14";
		case 12: return "R_PPC64_REL14_BRTAKEN";
		case 13: return "R_PPC64_REL14_BRNTAKEN";
		case 14: return "R_PPC64_GOT16";
		case 15: return "R_PPC64_GOT16_LO";
		case 16: return "R_PPC64_GOT16_HI";
		case 17: return "R_PPC64_GOT16_HA";
		case 19: return "R_PPC64_COPY";
		case 20: return "R_PPC64_GLOB_DAT";
		case 21: return "R_PPC64_JMP_SLOT";
		case 22: return "R_PPC64_RELATIVE";
		case 24: return "R_PPC64_UADDR32";
		case 25: return "R_PPC64_UADDR16";
		case 26: return "R_PPC64_REL32";
		case 27: return "R_PPC64_PLT32";
		case 28: return "R_PPC64_PLTREL32";
		case 29: return "R_PPC64_PLT16_LO";
		case 30: return "R_PPC64_PLT16_HI";
		case 31: return "R_PPC64_PLT16_HA";
		case 33: return "R_PPC64_SECTOFF";
		case 34: return "R_PPC64_SECTOFF_LO";
		case 35: return "R_PPC64_SECTOFF_HI";
		case 36: return "R_PPC64_SECTOFF_HA";
		case 37: return "R_PPC64_ADDR30";
		case 38: return "R_PPC64_ADDR64";
		case 39: return "R_PPC64_ADDR16_HIGHER";
		case 40: return "R_PPC64_ADDR16_HIGHERA";
		case 41: return "R_PPC64_ADDR16_HIGHEST";
		case 42: return "R_PPC64_ADDR16_HIGHESTA";
		case 43: return "R_PPC64_UADDR64";
		case 44: return "R_PPC64_REL64";
		case 45: return "R_PPC64_PLT64";
		case 46: return "R_PPC64_PLTREL64";
		case 47: return "R_PPC64_TOC16";
		case 48: return "R_PPC64_TOC16_LO";
		case 49: return "R_PPC64_TOC16_HI";
		case 50: return "R_PPC64_TOC16_HA";
		case 51: return "R_PPC64_TOC";
		case 52: return "R_PPC64_PLTGOT16";
		case 53: return "R_PPC64_PLTGOT16_LO";
		case 54: return "R_PPC64_PLTGOT16_HI";
		case 55: return "R_PPC64_PLTGOT16_HA";
		case 56: return "R_PPC64_ADDR16_DS";
		case 57: return "R_PPC64_ADDR16_LO_DS";
		case 58: return "R_PPC64_GOT16_DS";
		case 59: return "R_PPC64_GOT16_LO_DS";
		case 60: return "R_PPC64_PLT16_LO_DS";
		case 61: return "R_PPC64_SECTOFF_DS";
		case 62: return "R_PPC64_SECTOFF_LO_DS";
		case 63: return "R_PPC64_TOC16_DS";
		case 64: return "R_PPC64_TOC16_LO_DS";
		case 65: return "R_PPC64_PLTGOT16_DS";
		case 66: return "R_PPC64_PLTGOT16_LO_DS";
		case 67: return "R_PPC64_TLS";
		case 68: return "R_PPC64_DTPMOD64";
		case 69: return "R_PPC64_TPREL16";
		case 70: return "R_PPC64_TPREL16_LO";
		case 71: return "R_PPC64_TPREL16_HI";
		case 72: return "R_PPC64_TPREL16_HA";
		case 73: return "R_PPC64_TPREL64";
		case 74: return "R_PPC64_DTPREL16";
		case 75: return "R_PPC64_DTPREL16_LO";
		case 76: return "R_PPC64_DTPREL16_HI";
		case 77: return "R_PPC64_DTPREL16_HA";
		case 78: return "R_PPC64_DTPREL64";
		case 79: return "R_PPC64_GOT_TLSGD16";
		case 80: return "R_PPC64_GOT_TLSGD16_LO";
		case 81: return "R_PPC64_GOT_TLSGD16_HI";
		case 82: return "R_PPC64_GOT_TLSGD16_HA";
		case 83: return "R_PPC64_GOT_TLSLD16";
		case 84: return "R_PPC64_GOT_TLSLD16_LO";
		case 85: return "R_PPC64_GOT_TLSLD16_HI";
		case 86: return "R_PPC64_GOT_TLSLD16_HA";
		case 87: return "R_PPC64_GOT_TPREL16_DS";
		case 88: return "R_PPC64_GOT_TPREL16_LO_DS";
		case 89: return "R_PPC64_GOT_TPREL16_HI";
		case 90: return "R_PPC64_GOT_TPREL16_HA";
		case 91: return "R_PPC64_GOT_DTPREL16_DS";
		case 92: return "R_PPC64_GOT_DTPREL16_LO_DS";
		case 93: return "R_PPC64_GOT_DTPREL16_HI";
		case 94: return "R_PPC64_GOT_DTPREL16_HA";
		case 95: return "R_PPC64_TPREL16_DS";
		case 96: return "R_PPC64_TPREL16_LO_DS";
		case 97: return "R_PPC64_TPREL16_HIGHER";
		case 98: return "R_PPC64_TPREL16_HIGHERA";
		case 99: return "R_PPC64_TPREL16_HIGHEST";
		case 100: return "R_PPC64_TPREL16_HIGHESTA";
		case 101: return "R_PPC64_DTPREL16_DS";
		case 102: return "R_PPC64_DTPREL16_LO_DS";
		case 103: return "R_PPC64_DTPREL16_HIGHER";
		case 104: return "R_PPC64_DTPREL16_HIGHERA";
		case 105: return "R_PPC64_DTPREL16_HIGHEST";
		case 106: return "R_PPC64_DTPREL16_HIGHESTA";
		case 107: return "R_PPC64_TLSGD";
		case 108: return "R_PPC64_TLSLD";
		case 249: return "R_PPC64_REL16";
		case 250: return "R_PPC64_REL16_LO";
		case 251: return "R_PPC64_REL16_HI";
		case 252: return "R_PPC64_REL16_HA";
		}
		break;
	case EM_RISCV:
		switch(type) {
		case 0: return "R_RISCV_NONE";
		case 1: return "R_RISCV_32";
		case 2: return "R_RISCV_64";
		case 3: return "R_RISCV_RELATIVE";
		case 4: return "R_RISCV_COPY";
		case 5: return "R_RISCV_JUMP_SLOT";
		case 6: return "R_RISCV_TLS_DTPMOD32";
		case 7: return "R_RISCV_TLS_DTPMOD64";
		case 8: return "R_RISCV_TLS_DTPREL32";
		case 9: return "R_RISCV_TLS_DTPREL64";
		case 10: return "R_RISCV_TLS_TPREL32";
		case 11: return "R_RISCV_TLS_TPREL64";
		case 16: return "R_RISCV_BRANCH";
		case 17: return "R_RISCV_JAL";
		case 18: return "R_RISCV_CALL";
		case 19: return "R_RISCV_CALL_PLT";
		case 20: return "R_RISCV_GOT_HI20";
		case 21: return "R_RISCV_TLS_GOT_HI20";
		case 22: return "R_RISCV_TLS_GD_HI20";
		case 23: return "R_RISCV_PCREL_HI20";
		case 24: return "R_RISCV_PCREL_LO12_I";
		case 25: return "R_RISCV_PCREL_LO12_S";
		case 26: return "R_RISCV_HI20";
		case 27: return "R_RISCV_LO12_I";
		case 28: return "R_RISCV_LO12_S";
		case 29: return "R_RISCV_TPREL_HI20";
		case 30: return "R_RISCV_TPREL_LO12_I";
		case 31: return "R_RISCV_TPREL_LO12_S";
		case 32: return "R_RISCV_TPREL_ADD";
		case 33: return "R_RISCV_ADD8";
		case 34: return "R_RISCV_ADD16";
		case 35: return "R_RISCV_ADD32";
		case 36: return "R_RISCV_ADD64";
		case 37: return "R_RISCV_SUB8";
		case 38: return "R_RISCV_SUB16";
		case 39: return "R_RISCV_SUB32";
		case 40: return "R_RISCV_SUB64";
		case 41: return "R_RISCV_GNU_VTINHERIT";
		case 42: return "R_RISCV_GNU_VTENTRY";
		case 43: return "R_RISCV_ALIGN";
		case 44: return "R_RISCV_RVC_BRANCH";
		case 45: return "R_RISCV_RVC_JUMP";
		case 46: return "R_RISCV_RVC_LUI";
		case 47: return "R_RISCV_GPREL_I";
		case 48: return "R_RISCV_GPREL_S";
		}
		break;
	case EM_S390:
		switch (type) {
		case 0: return "R_390_NONE";
		case 1: return "R_390_8";
		case 2: return "R_390_12";
		case 3: return "R_390_16";
		case 4: return "R_390_32";
		case 5: return "R_390_PC32";
		case 6: return "R_390_GOT12";
		case 7: return "R_390_GOT32";
		case 8: return "R_390_PLT32";
		case 9: return "R_390_COPY";
		case 10: return "R_390_GLOB_DAT";
		case 11: return "R_390_JMP_SLOT";
		case 12: return "R_390_RELATIVE";
		case 13: return "R_390_GOTOFF";
		case 14: return "R_390_GOTPC";
		case 15: return "R_390_GOT16";
		case 16: return "R_390_PC16";
		case 17: return "R_390_PC16DBL";
		case 18: return "R_390_PLT16DBL";
		case 19: return "R_390_PC32DBL";
		case 20: return "R_390_PLT32DBL";
		case 21: return "R_390_GOTPCDBL";
		case 22: return "R_390_64";
		case 23: return "R_390_PC64";
		case 24: return "R_390_GOT64";
		case 25: return "R_390_PLT64";
		case 26: return "R_390_GOTENT";
		}
		break;
	case EM_SPARC:
	case EM_SPARCV9:
		switch(type) {
		case 0: return "R_SPARC_NONE";
		case 1: return "R_SPARC_8";
		case 2: return "R_SPARC_16";
		case 3: return "R_SPARC_32";
		case 4: return "R_SPARC_DISP8";
		case 5: return "R_SPARC_DISP16";
		case 6: return "R_SPARC_DISP32";
		case 7: return "R_SPARC_WDISP30";
		case 8: return "R_SPARC_WDISP22";
		case 9: return "R_SPARC_HI22";
		case 10: return "R_SPARC_22";
		case 11: return "R_SPARC_13";
		case 12: return "R_SPARC_LO10";
		case 13: return "R_SPARC_GOT10";
		case 14: return "R_SPARC_GOT13";
		case 15: return "R_SPARC_GOT22";
		case 16: return "R_SPARC_PC10";
		case 17: return "R_SPARC_PC22";
		case 18: return "R_SPARC_WPLT30";
		case 19: return "R_SPARC_COPY";
		case 20: return "R_SPARC_GLOB_DAT";
		case 21: return "R_SPARC_JMP_SLOT";
		case 22: return "R_SPARC_RELATIVE";
		case 23: return "R_SPARC_UA32";
		case 24: return "R_SPARC_PLT32";
		case 25: return "R_SPARC_HIPLT22";
		case 26: return "R_SPARC_LOPLT10";
		case 27: return "R_SPARC_PCPLT32";
		case 28: return "R_SPARC_PCPLT22";
		case 29: return "R_SPARC_PCPLT10";
		case 30: return "R_SPARC_10";
		case 31: return "R_SPARC_11";
		case 32: return "R_SPARC_64";
		case 33: return "R_SPARC_OLO10";
		case 34: return "R_SPARC_HH22";
		case 35: return "R_SPARC_HM10";
		case 36: return "R_SPARC_LM22";
		case 37: return "R_SPARC_PC_HH22";
		case 38: return "R_SPARC_PC_HM10";
		case 39: return "R_SPARC_PC_LM22";
		case 40: return "R_SPARC_WDISP16";
		case 41: return "R_SPARC_WDISP19";
		case 42: return "R_SPARC_GLOB_JMP";
		case 43: return "R_SPARC_7";
		case 44: return "R_SPARC_5";
		case 45: return "R_SPARC_6";
		case 46: return "R_SPARC_DISP64";
		case 47: return "R_SPARC_PLT64";
		case 48: return "R_SPARC_HIX22";
		case 49: return "R_SPARC_LOX10";
		case 50: return "R_SPARC_H44";
		case 51: return "R_SPARC_M44";
		case 52: return "R_SPARC_L44";
		case 53: return "R_SPARC_REGISTER";
		case 54: return "R_SPARC_UA64";
		case 55: return "R_SPARC_UA16";
		case 56: return "R_SPARC_TLS_GD_HI22";
		case 57: return "R_SPARC_TLS_GD_LO10";
		case 58: return "R_SPARC_TLS_GD_ADD";
		case 59: return "R_SPARC_TLS_GD_CALL";
		case 60: return "R_SPARC_TLS_LDM_HI22";
		case 61: return "R_SPARC_TLS_LDM_LO10";
		case 62: return "R_SPARC_TLS_LDM_ADD";
		case 63: return "R_SPARC_TLS_LDM_CALL";
		case 64: return "R_SPARC_TLS_LDO_HIX22";
		case 65: return "R_SPARC_TLS_LDO_LOX10";
		case 66: return "R_SPARC_TLS_LDO_ADD";
		case 67: return "R_SPARC_TLS_IE_HI22";
		case 68: return "R_SPARC_TLS_IE_LO10";
		case 69: return "R_SPARC_TLS_IE_LD";
		case 70: return "R_SPARC_TLS_IE_LDX";
		case 71: return "R_SPARC_TLS_IE_ADD";
		case 72: return "R_SPARC_TLS_LE_HIX22";
		case 73: return "R_SPARC_TLS_LE_LOX10";
		case 74: return "R_SPARC_TLS_DTPMOD32";
		case 75: return "R_SPARC_TLS_DTPMOD64";
		case 76: return "R_SPARC_TLS_DTPOFF32";
		case 77: return "R_SPARC_TLS_DTPOFF64";
		case 78: return "R_SPARC_TLS_TPOFF32";
		case 79: return "R_SPARC_TLS_TPOFF64";
		}
		break;
	case EM_X86_64:
		switch(type) {
		case 0: return "R_X86_64_NONE";
		case 1: return "R_X86_64_64";
		case 2: return "R_X86_64_PC32";
		case 3: return "R_X86_64_GOT32";
		case 4: return "R_X86_64_PLT32";
		case 5: return "R_X86_64_COPY";
		case 6: return "R_X86_64_GLOB_DAT";
		case 7: return "R_X86_64_JUMP_SLOT";
		case 8: return "R_X86_64_RELATIVE";
		case 9: return "R_X86_64_GOTPCREL";
		case 10: return "R_X86_64_32";
		case 11: return "R_X86_64_32S";
		case 12: return "R_X86_64_16";
		case 13: return "R_X86_64_PC16";
		case 14: return "R_X86_64_8";
		case 15: return "R_X86_64_PC8";
		case 16: return "R_X86_64_DTPMOD64";
		case 17: return "R_X86_64_DTPOFF64";
		case 18: return "R_X86_64_TPOFF64";
		case 19: return "R_X86_64_TLSGD";
		case 20: return "R_X86_64_TLSLD";
		case 21: return "R_X86_64_DTPOFF32";
		case 22: return "R_X86_64_GOTTPOFF";
		case 23: return "R_X86_64_TPOFF32";
		case 24: return "R_X86_64_PC64";
		case 25: return "R_X86_64_GOTOFF64";
		case 26: return "R_X86_64_GOTPC32";
		case 27: return "R_X86_64_GOT64";
		case 28: return "R_X86_64_GOTPCREL64";
		case 29: return "R_X86_64_GOTPC64";
		case 30: return "R_X86_64_GOTPLT64";
		case 31: return "R_X86_64_PLTOFF64";
		case 32: return "R_X86_64_SIZE32";
		case 33: return "R_X86_64_SIZE64";
		case 34: return "R_X86_64_GOTPC32_TLSDESC";
		case 35: return "R_X86_64_TLSDESC_CALL";
		case 36: return "R_X86_64_TLSDESC";
		case 37: return "R_X86_64_IRELATIVE";
		case 38: return "R_X86_64_RELATIVE64";
		case 41: return "R_X86_64_GOTPCRELX";
		case 42: return "R_X86_64_REX_GOTPCRELX";
		}
		break;
	}

	snprintf(s_type, sizeof(s_type), "<unknown: %#x>", type);
	return (s_type);
}
