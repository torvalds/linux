/***********************license start************************************
 * Copyright (c) 2003-2017 Cavium, Inc.
 * All rights reserved.
 *
 * License: one of 'Cavium License' or 'GNU General Public License Version 2'
 *
 * This file is provided under the terms of the Cavium License (see below)
 * or under the terms of GNU General Public License, Version 2, as
 * published by the Free Software Foundation. When using or redistributing
 * this file, you may do so under either license.
 *
 * Cavium License:  Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that the following
 * conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 *  * Neither the name of Cavium Inc. nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * This Software, including technical data, may be subject to U.S. export
 * control laws, including the U.S. Export Administration Act and its
 * associated regulations, and may be subject to export or import
 * regulations in other countries.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS
 * OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH
 * RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY
 * REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT
 * DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY)
 * WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A
 * PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET
 * ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE
 * ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES
 * WITH YOU.
 ***********************license end**************************************/

#ifndef __ZIP_REGS_H__
#define __ZIP_REGS_H__

/*
 * Configuration and status register (CSR) address and type definitions for
 * Cavium ZIP.
 */

#include <linux/kern_levels.h>

/* ZIP invocation result completion status codes */
#define ZIP_CMD_NOTDONE        0x0

/* Successful completion. */
#define ZIP_CMD_SUCCESS        0x1

/* Output truncated */
#define ZIP_CMD_DTRUNC         0x2

/* Dynamic Stop */
#define ZIP_CMD_DYNAMIC_STOP   0x3

/* Uncompress ran out of input data when IWORD0[EF] was set */
#define ZIP_CMD_ITRUNC         0x4

/* Uncompress found the reserved block type 3 */
#define ZIP_CMD_RBLOCK         0x5

/*
 * Uncompress found LEN != ZIP_CMD_NLEN in an uncompressed block in the input.
 */
#define ZIP_CMD_NLEN           0x6

/* Uncompress found a bad code in the main Huffman codes. */
#define ZIP_CMD_BADCODE        0x7

/* Uncompress found a bad code in the 19 Huffman codes encoding lengths. */
#define ZIP_CMD_BADCODE2       0x8

/* Compress found a zero-length input. */
#define ZIP_CMD_ZERO_LEN       0x9

/* The compress or decompress encountered an internal parity error. */
#define ZIP_CMD_PARITY         0xA

/*
 * Uncompress found a string identifier that precedes the uncompressed data and
 * decompression history.
 */
#define ZIP_CMD_FATAL          0xB

/**
 * enum zip_int_vec_e - ZIP MSI-X Vector Enumeration, enumerates the MSI-X
 * interrupt vectors.
 */
enum zip_int_vec_e {
	ZIP_INT_VEC_E_ECCE = 0x10,
	ZIP_INT_VEC_E_FIFE = 0x11,
	ZIP_INT_VEC_E_QUE0_DONE = 0x0,
	ZIP_INT_VEC_E_QUE0_ERR = 0x8,
	ZIP_INT_VEC_E_QUE1_DONE = 0x1,
	ZIP_INT_VEC_E_QUE1_ERR = 0x9,
	ZIP_INT_VEC_E_QUE2_DONE = 0x2,
	ZIP_INT_VEC_E_QUE2_ERR = 0xa,
	ZIP_INT_VEC_E_QUE3_DONE = 0x3,
	ZIP_INT_VEC_E_QUE3_ERR = 0xb,
	ZIP_INT_VEC_E_QUE4_DONE = 0x4,
	ZIP_INT_VEC_E_QUE4_ERR = 0xc,
	ZIP_INT_VEC_E_QUE5_DONE = 0x5,
	ZIP_INT_VEC_E_QUE5_ERR = 0xd,
	ZIP_INT_VEC_E_QUE6_DONE = 0x6,
	ZIP_INT_VEC_E_QUE6_ERR = 0xe,
	ZIP_INT_VEC_E_QUE7_DONE = 0x7,
	ZIP_INT_VEC_E_QUE7_ERR = 0xf,
	ZIP_INT_VEC_E_ENUM_LAST = 0x12,
};

/**
 * union zip_zptr_addr_s - ZIP Generic Pointer Structure for ADDR.
 *
 * It is the generic format of pointers in ZIP_INST_S.
 */
union zip_zptr_addr_s {
	u64 u_reg64;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_49_63              : 15;
		u64 addr                        : 49;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 addr                        : 49;
		u64 reserved_49_63              : 15;
#endif
	} s;

};

/**
 * union zip_zptr_ctl_s - ZIP Generic Pointer Structure for CTL.
 *
 * It is the generic format of pointers in ZIP_INST_S.
 */
union zip_zptr_ctl_s {
	u64 u_reg64;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_112_127            : 16;
		u64 length                      : 16;
		u64 reserved_67_95              : 29;
		u64 fw                          : 1;
		u64 nc                          : 1;
		u64 data_be                     : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 data_be                     : 1;
		u64 nc                          : 1;
		u64 fw                          : 1;
		u64 reserved_67_95              : 29;
		u64 length                      : 16;
		u64 reserved_112_127            : 16;
#endif
	} s;
};

/**
 * union zip_inst_s - ZIP Instruction Structure.
 * Each ZIP instruction has 16 words (they are called IWORD0 to IWORD15 within
 * the structure).
 */
union zip_inst_s {
	u64 u_reg64[16];
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 doneint                     : 1;
		u64 reserved_56_62              : 7;
		u64 totaloutputlength           : 24;
		u64 reserved_27_31              : 5;
		u64 exn                         : 3;
		u64 reserved_23_23              : 1;
		u64 exbits                      : 7;
		u64 reserved_12_15              : 4;
		u64 sf                          : 1;
		u64 ss                          : 2;
		u64 cc                          : 2;
		u64 ef                          : 1;
		u64 bf                          : 1;
		u64 ce                          : 1;
		u64 reserved_3_3                : 1;
		u64 ds                          : 1;
		u64 dg                          : 1;
		u64 hg                          : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 hg                          : 1;
		u64 dg                          : 1;
		u64 ds                          : 1;
		u64 reserved_3_3                : 1;
		u64 ce                          : 1;
		u64 bf                          : 1;
		u64 ef                          : 1;
		u64 cc                          : 2;
		u64 ss                          : 2;
		u64 sf                          : 1;
		u64 reserved_12_15              : 4;
		u64 exbits                      : 7;
		u64 reserved_23_23              : 1;
		u64 exn                         : 3;
		u64 reserved_27_31              : 5;
		u64 totaloutputlength           : 24;
		u64 reserved_56_62              : 7;
		u64 doneint                     : 1;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 historylength               : 16;
		u64 reserved_96_111             : 16;
		u64 adlercrc32                  : 32;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 adlercrc32                  : 32;
		u64 reserved_96_111             : 16;
		u64 historylength               : 16;
#endif
		union zip_zptr_addr_s ctx_ptr_addr;
		union zip_zptr_ctl_s ctx_ptr_ctl;
		union zip_zptr_addr_s his_ptr_addr;
		union zip_zptr_ctl_s his_ptr_ctl;
		union zip_zptr_addr_s inp_ptr_addr;
		union zip_zptr_ctl_s inp_ptr_ctl;
		union zip_zptr_addr_s out_ptr_addr;
		union zip_zptr_ctl_s out_ptr_ctl;
		union zip_zptr_addr_s res_ptr_addr;
		union zip_zptr_ctl_s res_ptr_ctl;
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_817_831            : 15;
		u64 wq_ptr                      : 49;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 wq_ptr                      : 49;
		u64 reserved_817_831            : 15;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_882_895            : 14;
		u64 tt                          : 2;
		u64 reserved_874_879            : 6;
		u64 grp                         : 10;
		u64 tag                         : 32;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 tag                         : 32;
		u64 grp                         : 10;
		u64 reserved_874_879            : 6;
		u64 tt                          : 2;
		u64 reserved_882_895            : 14;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_896_959            : 64;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 reserved_896_959            : 64;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_960_1023           : 64;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 reserved_960_1023           : 64;
#endif
	} s;
};

/**
 * union zip_nptr_s - ZIP Instruction Next-Chunk-Buffer Pointer (NPTR)
 * Structure
 *
 * ZIP_NPTR structure is used to chain all the zip instruction buffers
 * together. ZIP instruction buffers are managed (allocated and released) by
 * the software.
 */
union zip_nptr_s {
	u64 u_reg64;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_49_63              : 15;
		u64 addr                        : 49;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 addr                        : 49;
		u64 reserved_49_63              : 15;
#endif
	} s;
};

/**
 * union zip_zptr_s - ZIP Generic Pointer Structure.
 *
 * It is the generic format of pointers in ZIP_INST_S.
 */
union zip_zptr_s {
	u64 u_reg64[2];
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_49_63              : 15;
		u64 addr                        : 49;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 addr                        : 49;
		u64 reserved_49_63              : 15;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_112_127            : 16;
		u64 length                      : 16;
		u64 reserved_67_95              : 29;
		u64 fw                          : 1;
		u64 nc                          : 1;
		u64 data_be                     : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 data_be                     : 1;
		u64 nc                          : 1;
		u64 fw                          : 1;
		u64 reserved_67_95              : 29;
		u64 length                      : 16;
		u64 reserved_112_127            : 16;
#endif
	} s;
};

/**
 * union zip_zres_s - ZIP Result Structure
 *
 * The ZIP coprocessor writes the result structure after it completes the
 * invocation. The result structure is exactly 24 bytes, and each invocation of
 * the ZIP coprocessor produces exactly one result structure.
 */
union zip_zres_s {
	u64 u_reg64[3];
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 crc32                       : 32;
		u64 adler32                     : 32;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 adler32                     : 32;
		u64 crc32                       : 32;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 totalbyteswritten           : 32;
		u64 totalbytesread              : 32;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 totalbytesread              : 32;
		u64 totalbyteswritten           : 32;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 totalbitsprocessed          : 32;
		u64 doneint                     : 1;
		u64 reserved_155_158            : 4;
		u64 exn                         : 3;
		u64 reserved_151_151            : 1;
		u64 exbits                      : 7;
		u64 reserved_137_143            : 7;
		u64 ef                          : 1;

		volatile u64 compcode           : 8;
#elif defined(__LITTLE_ENDIAN_BITFIELD)

		volatile u64 compcode           : 8;
		u64 ef                          : 1;
		u64 reserved_137_143            : 7;
		u64 exbits                      : 7;
		u64 reserved_151_151            : 1;
		u64 exn                         : 3;
		u64 reserved_155_158            : 4;
		u64 doneint                     : 1;
		u64 totalbitsprocessed          : 32;
#endif
	} s;
};

/**
 * union zip_cmd_ctl - Structure representing the register that controls
 * clock and reset.
 */
union zip_cmd_ctl {
	u64 u_reg64;
	struct zip_cmd_ctl_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_2_63               : 62;
		u64 forceclk                    : 1;
		u64 reset                       : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 reset                       : 1;
		u64 forceclk                    : 1;
		u64 reserved_2_63               : 62;
#endif
	} s;
};

#define ZIP_CMD_CTL 0x0ull

/**
 * union zip_constants - Data structure representing the register that contains
 * all of the current implementation-related parameters of the zip core in this
 * chip.
 */
union zip_constants {
	u64 u_reg64;
	struct zip_constants_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 nexec                       : 8;
		u64 reserved_49_55              : 7;
		u64 syncflush_capable           : 1;
		u64 depth                       : 16;
		u64 onfsize                     : 12;
		u64 ctxsize                     : 12;
		u64 reserved_1_7                : 7;
		u64 disabled                    : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 disabled                    : 1;
		u64 reserved_1_7                : 7;
		u64 ctxsize                     : 12;
		u64 onfsize                     : 12;
		u64 depth                       : 16;
		u64 syncflush_capable           : 1;
		u64 reserved_49_55              : 7;
		u64 nexec                       : 8;
#endif
	} s;
};

#define ZIP_CONSTANTS 0x00A0ull

/**
 * union zip_corex_bist_status - Represents registers which have the BIST
 * status of memories in zip cores.
 *
 * Each bit is the BIST result of an individual memory
 * (per bit, 0 = pass and 1 = fail).
 */
union zip_corex_bist_status {
	u64 u_reg64;
	struct zip_corex_bist_status_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_53_63              : 11;
		u64 bstatus                     : 53;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 bstatus                     : 53;
		u64 reserved_53_63              : 11;
#endif
	} s;
};

static inline u64 ZIP_COREX_BIST_STATUS(u64 param1)
{
	if (param1 <= 1)
		return 0x0520ull + (param1 & 1) * 0x8ull;
	pr_err("ZIP_COREX_BIST_STATUS: %llu\n", param1);
	return 0;
}

/**
 * union zip_ctl_bist_status - Represents register that has the BIST status of
 * memories in ZIP_CTL (instruction buffer, G/S pointer FIFO, input data
 * buffer, output data buffers).
 *
 * Each bit is the BIST result of an individual memory
 * (per bit, 0 = pass and 1 = fail).
 */
union zip_ctl_bist_status {
	u64 u_reg64;
	struct zip_ctl_bist_status_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_9_63               : 55;
		u64 bstatus                     : 9;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 bstatus                     : 9;
		u64 reserved_9_63               : 55;
#endif
	} s;
};

#define ZIP_CTL_BIST_STATUS 0x0510ull

/**
 * union zip_ctl_cfg - Represents the register that controls the behavior of
 * the ZIP DMA engines.
 *
 * It is recommended to keep default values for normal operation. Changing the
 * values of the fields may be useful for diagnostics.
 */
union zip_ctl_cfg {
	u64 u_reg64;
	struct zip_ctl_cfg_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_52_63              : 12;
		u64 ildf                        : 4;
		u64 reserved_36_47              : 12;
		u64 drtf                        : 4;
		u64 reserved_27_31              : 5;
		u64 stcf                        : 3;
		u64 reserved_19_23              : 5;
		u64 ldf                         : 3;
		u64 reserved_2_15               : 14;
		u64 busy                        : 1;
		u64 reserved_0_0                : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 reserved_0_0                : 1;
		u64 busy                        : 1;
		u64 reserved_2_15               : 14;
		u64 ldf                         : 3;
		u64 reserved_19_23              : 5;
		u64 stcf                        : 3;
		u64 reserved_27_31              : 5;
		u64 drtf                        : 4;
		u64 reserved_36_47              : 12;
		u64 ildf                        : 4;
		u64 reserved_52_63              : 12;
#endif
	} s;
};

#define ZIP_CTL_CFG 0x0560ull

/**
 * union zip_dbg_corex_inst - Represents the registers that reflect the status
 * of the current instruction that the ZIP core is executing or has executed.
 *
 * These registers are only for debug use.
 */
union zip_dbg_corex_inst {
	u64 u_reg64;
	struct zip_dbg_corex_inst_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 busy                        : 1;
		u64 reserved_35_62              : 28;
		u64 qid                         : 3;
		u64 iid                         : 32;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 iid                         : 32;
		u64 qid                         : 3;
		u64 reserved_35_62              : 28;
		u64 busy                        : 1;
#endif
	} s;
};

static inline u64 ZIP_DBG_COREX_INST(u64 param1)
{
	if (param1 <= 1)
		return 0x0640ull + (param1 & 1) * 0x8ull;
	pr_err("ZIP_DBG_COREX_INST: %llu\n", param1);
	return 0;
}

/**
 * union zip_dbg_corex_sta - Represents registers that reflect the status of
 * the zip cores.
 *
 * They are for debug use only.
 */
union zip_dbg_corex_sta {
	u64 u_reg64;
	struct zip_dbg_corex_sta_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 busy                        : 1;
		u64 reserved_37_62              : 26;
		u64 ist                         : 5;
		u64 nie                         : 32;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 nie                         : 32;
		u64 ist                         : 5;
		u64 reserved_37_62              : 26;
		u64 busy                        : 1;
#endif
	} s;
};

static inline u64 ZIP_DBG_COREX_STA(u64 param1)
{
	if (param1 <= 1)
		return 0x0680ull + (param1 & 1) * 0x8ull;
	pr_err("ZIP_DBG_COREX_STA: %llu\n", param1);
	return 0;
}

/**
 * union zip_dbg_quex_sta - Represets registers that reflect status of the zip
 * instruction queues.
 *
 * They are for debug use only.
 */
union zip_dbg_quex_sta {
	u64 u_reg64;
	struct zip_dbg_quex_sta_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 busy                        : 1;
		u64 reserved_56_62              : 7;
		u64 rqwc                        : 24;
		u64 nii                         : 32;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 nii                         : 32;
		u64 rqwc                        : 24;
		u64 reserved_56_62              : 7;
		u64 busy                        : 1;
#endif
	} s;
};

static inline u64 ZIP_DBG_QUEX_STA(u64 param1)
{
	if (param1 <= 7)
		return 0x1800ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_DBG_QUEX_STA: %llu\n", param1);
	return 0;
}

/**
 * union zip_ecc_ctl - Represents the register that enables ECC for each
 * individual internal memory that requires ECC.
 *
 * For debug purpose, it can also flip one or two bits in the ECC data.
 */
union zip_ecc_ctl {
	u64 u_reg64;
	struct zip_ecc_ctl_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_19_63              : 45;
		u64 vmem_cdis                   : 1;
		u64 vmem_fs                     : 2;
		u64 reserved_15_15              : 1;
		u64 idf1_cdis                   : 1;
		u64 idf1_fs                     : 2;
		u64 reserved_11_11              : 1;
		u64 idf0_cdis                   : 1;
		u64 idf0_fs                     : 2;
		u64 reserved_7_7                : 1;
		u64 gspf_cdis                   : 1;
		u64 gspf_fs                     : 2;
		u64 reserved_3_3                : 1;
		u64 iqf_cdis                    : 1;
		u64 iqf_fs                      : 2;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 iqf_fs                      : 2;
		u64 iqf_cdis                    : 1;
		u64 reserved_3_3                : 1;
		u64 gspf_fs                     : 2;
		u64 gspf_cdis                   : 1;
		u64 reserved_7_7                : 1;
		u64 idf0_fs                     : 2;
		u64 idf0_cdis                   : 1;
		u64 reserved_11_11              : 1;
		u64 idf1_fs                     : 2;
		u64 idf1_cdis                   : 1;
		u64 reserved_15_15              : 1;
		u64 vmem_fs                     : 2;
		u64 vmem_cdis                   : 1;
		u64 reserved_19_63              : 45;
#endif
	} s;
};

#define ZIP_ECC_CTL 0x0568ull

/* NCB - zip_ecce_ena_w1c */
union zip_ecce_ena_w1c {
	u64 u_reg64;
	struct zip_ecce_ena_w1c_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_37_63              : 27;
		u64 dbe                         : 5;
		u64 reserved_5_31               : 27;
		u64 sbe                         : 5;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 sbe                         : 5;
		u64 reserved_5_31               : 27;
		u64 dbe                         : 5;
		u64 reserved_37_63              : 27;
#endif
	} s;
};

#define ZIP_ECCE_ENA_W1C 0x0598ull

/* NCB - zip_ecce_ena_w1s */
union zip_ecce_ena_w1s {
	u64 u_reg64;
	struct zip_ecce_ena_w1s_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_37_63              : 27;
		u64 dbe                         : 5;
		u64 reserved_5_31               : 27;
		u64 sbe                         : 5;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 sbe                         : 5;
		u64 reserved_5_31               : 27;
		u64 dbe                         : 5;
		u64 reserved_37_63              : 27;
#endif
	} s;
};

#define ZIP_ECCE_ENA_W1S 0x0590ull

/**
 * union zip_ecce_int - Represents the register that contains the status of the
 * ECC interrupt sources.
 */
union zip_ecce_int {
	u64 u_reg64;
	struct zip_ecce_int_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_37_63              : 27;
		u64 dbe                         : 5;
		u64 reserved_5_31               : 27;
		u64 sbe                         : 5;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 sbe                         : 5;
		u64 reserved_5_31               : 27;
		u64 dbe                         : 5;
		u64 reserved_37_63              : 27;
#endif
	} s;
};

#define ZIP_ECCE_INT 0x0580ull

/* NCB - zip_ecce_int_w1s */
union zip_ecce_int_w1s {
	u64 u_reg64;
	struct zip_ecce_int_w1s_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_37_63              : 27;
		u64 dbe                         : 5;
		u64 reserved_5_31               : 27;
		u64 sbe                         : 5;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 sbe                         : 5;
		u64 reserved_5_31               : 27;
		u64 dbe                         : 5;
		u64 reserved_37_63              : 27;
#endif
	} s;
};

#define ZIP_ECCE_INT_W1S 0x0588ull

/* NCB - zip_fife_ena_w1c */
union zip_fife_ena_w1c {
	u64 u_reg64;
	struct zip_fife_ena_w1c_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_42_63              : 22;
		u64 asserts                     : 42;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 asserts                     : 42;
		u64 reserved_42_63              : 22;
#endif
	} s;
};

#define ZIP_FIFE_ENA_W1C 0x0090ull

/* NCB - zip_fife_ena_w1s */
union zip_fife_ena_w1s {
	u64 u_reg64;
	struct zip_fife_ena_w1s_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_42_63              : 22;
		u64 asserts                     : 42;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 asserts                     : 42;
		u64 reserved_42_63              : 22;
#endif
	} s;
};

#define ZIP_FIFE_ENA_W1S 0x0088ull

/* NCB - zip_fife_int */
union zip_fife_int {
	u64 u_reg64;
	struct zip_fife_int_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_42_63              : 22;
		u64 asserts                     : 42;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 asserts                     : 42;
		u64 reserved_42_63              : 22;
#endif
	} s;
};

#define ZIP_FIFE_INT 0x0078ull

/* NCB - zip_fife_int_w1s */
union zip_fife_int_w1s {
	u64 u_reg64;
	struct zip_fife_int_w1s_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_42_63              : 22;
		u64 asserts                     : 42;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 asserts                     : 42;
		u64 reserved_42_63              : 22;
#endif
	} s;
};

#define ZIP_FIFE_INT_W1S 0x0080ull

/**
 * union zip_msix_pbax - Represents the register that is the MSI-X PBA table
 *
 * The bit number is indexed by the ZIP_INT_VEC_E enumeration.
 */
union zip_msix_pbax {
	u64 u_reg64;
	struct zip_msix_pbax_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 pend                        : 64;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 pend                        : 64;
#endif
	} s;
};

static inline u64 ZIP_MSIX_PBAX(u64 param1)
{
	if (param1 == 0)
		return 0x0000838000FF0000ull;
	pr_err("ZIP_MSIX_PBAX: %llu\n", param1);
	return 0;
}

/**
 * union zip_msix_vecx_addr - Represents the register that is the MSI-X vector
 * table, indexed by the ZIP_INT_VEC_E enumeration.
 */
union zip_msix_vecx_addr {
	u64 u_reg64;
	struct zip_msix_vecx_addr_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_49_63              : 15;
		u64 addr                        : 47;
		u64 reserved_1_1                : 1;
		u64 secvec                      : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 secvec                      : 1;
		u64 reserved_1_1                : 1;
		u64 addr                        : 47;
		u64 reserved_49_63              : 15;
#endif
	} s;
};

static inline u64 ZIP_MSIX_VECX_ADDR(u64 param1)
{
	if (param1 <= 17)
		return 0x0000838000F00000ull + (param1 & 31) * 0x10ull;
	pr_err("ZIP_MSIX_VECX_ADDR: %llu\n", param1);
	return 0;
}

/**
 * union zip_msix_vecx_ctl - Represents the register that is the MSI-X vector
 * table, indexed by the ZIP_INT_VEC_E enumeration.
 */
union zip_msix_vecx_ctl {
	u64 u_reg64;
	struct zip_msix_vecx_ctl_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_33_63              : 31;
		u64 mask                        : 1;
		u64 reserved_20_31              : 12;
		u64 data                        : 20;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 data                        : 20;
		u64 reserved_20_31              : 12;
		u64 mask                        : 1;
		u64 reserved_33_63              : 31;
#endif
	} s;
};

static inline u64 ZIP_MSIX_VECX_CTL(u64 param1)
{
	if (param1 <= 17)
		return 0x0000838000F00008ull + (param1 & 31) * 0x10ull;
	pr_err("ZIP_MSIX_VECX_CTL: %llu\n", param1);
	return 0;
}

/**
 * union zip_quex_done - Represents the registers that contain the per-queue
 * instruction done count.
 */
union zip_quex_done {
	u64 u_reg64;
	struct zip_quex_done_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_20_63              : 44;
		u64 done                        : 20;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 done                        : 20;
		u64 reserved_20_63              : 44;
#endif
	} s;
};

static inline u64 ZIP_QUEX_DONE(u64 param1)
{
	if (param1 <= 7)
		return 0x2000ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_DONE: %llu\n", param1);
	return 0;
}

/**
 * union zip_quex_done_ack - Represents the registers on write to which will
 * decrement the per-queue instructiona done count.
 */
union zip_quex_done_ack {
	u64 u_reg64;
	struct zip_quex_done_ack_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_20_63              : 44;
		u64 done_ack                    : 20;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 done_ack                    : 20;
		u64 reserved_20_63              : 44;
#endif
	} s;
};

static inline u64 ZIP_QUEX_DONE_ACK(u64 param1)
{
	if (param1 <= 7)
		return 0x2200ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_DONE_ACK: %llu\n", param1);
	return 0;
}

/**
 * union zip_quex_done_ena_w1c - Represents the register which when written
 * 1 to will disable the DONEINT interrupt for the queue.
 */
union zip_quex_done_ena_w1c {
	u64 u_reg64;
	struct zip_quex_done_ena_w1c_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_1_63               : 63;
		u64 done_ena                    : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 done_ena                    : 1;
		u64 reserved_1_63               : 63;
#endif
	} s;
};

static inline u64 ZIP_QUEX_DONE_ENA_W1C(u64 param1)
{
	if (param1 <= 7)
		return 0x2600ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_DONE_ENA_W1C: %llu\n", param1);
	return 0;
}

/**
 * union zip_quex_done_ena_w1s - Represents the register that when written 1 to
 * will enable the DONEINT interrupt for the queue.
 */
union zip_quex_done_ena_w1s {
	u64 u_reg64;
	struct zip_quex_done_ena_w1s_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_1_63               : 63;
		u64 done_ena                    : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 done_ena                    : 1;
		u64 reserved_1_63               : 63;
#endif
	} s;
};

static inline u64 ZIP_QUEX_DONE_ENA_W1S(u64 param1)
{
	if (param1 <= 7)
		return 0x2400ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_DONE_ENA_W1S: %llu\n", param1);
	return 0;
}

/**
 * union zip_quex_done_wait - Represents the register that specifies the per
 * queue interrupt coalescing settings.
 */
union zip_quex_done_wait {
	u64 u_reg64;
	struct zip_quex_done_wait_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_48_63              : 16;
		u64 time_wait                   : 16;
		u64 reserved_20_31              : 12;
		u64 num_wait                    : 20;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 num_wait                    : 20;
		u64 reserved_20_31              : 12;
		u64 time_wait                   : 16;
		u64 reserved_48_63              : 16;
#endif
	} s;
};

static inline u64 ZIP_QUEX_DONE_WAIT(u64 param1)
{
	if (param1 <= 7)
		return 0x2800ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_DONE_WAIT: %llu\n", param1);
	return 0;
}

/**
 * union zip_quex_doorbell - Represents doorbell registers for the ZIP
 * instruction queues.
 */
union zip_quex_doorbell {
	u64 u_reg64;
	struct zip_quex_doorbell_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_20_63              : 44;
		u64 dbell_cnt                   : 20;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 dbell_cnt                   : 20;
		u64 reserved_20_63              : 44;
#endif
	} s;
};

static inline u64 ZIP_QUEX_DOORBELL(u64 param1)
{
	if (param1 <= 7)
		return 0x4000ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_DOORBELL: %llu\n", param1);
	return 0;
}

union zip_quex_err_ena_w1c {
	u64 u_reg64;
	struct zip_quex_err_ena_w1c_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_5_63               : 59;
		u64 mdbe                        : 1;
		u64 nwrp                        : 1;
		u64 nrrp                        : 1;
		u64 irde                        : 1;
		u64 dovf                        : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 dovf                        : 1;
		u64 irde                        : 1;
		u64 nrrp                        : 1;
		u64 nwrp                        : 1;
		u64 mdbe                        : 1;
		u64 reserved_5_63               : 59;
#endif
	} s;
};

static inline u64 ZIP_QUEX_ERR_ENA_W1C(u64 param1)
{
	if (param1 <= 7)
		return 0x3600ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_ERR_ENA_W1C: %llu\n", param1);
	return 0;
}

union zip_quex_err_ena_w1s {
	u64 u_reg64;
	struct zip_quex_err_ena_w1s_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_5_63               : 59;
		u64 mdbe                        : 1;
		u64 nwrp                        : 1;
		u64 nrrp                        : 1;
		u64 irde                        : 1;
		u64 dovf                        : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 dovf                        : 1;
		u64 irde                        : 1;
		u64 nrrp                        : 1;
		u64 nwrp                        : 1;
		u64 mdbe                        : 1;
		u64 reserved_5_63               : 59;
#endif
	} s;
};

static inline u64 ZIP_QUEX_ERR_ENA_W1S(u64 param1)
{
	if (param1 <= 7)
		return 0x3400ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_ERR_ENA_W1S: %llu\n", param1);
	return 0;
}

/**
 * union zip_quex_err_int - Represents registers that contain the per-queue
 * error interrupts.
 */
union zip_quex_err_int {
	u64 u_reg64;
	struct zip_quex_err_int_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_5_63               : 59;
		u64 mdbe                        : 1;
		u64 nwrp                        : 1;
		u64 nrrp                        : 1;
		u64 irde                        : 1;
		u64 dovf                        : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 dovf                        : 1;
		u64 irde                        : 1;
		u64 nrrp                        : 1;
		u64 nwrp                        : 1;
		u64 mdbe                        : 1;
		u64 reserved_5_63               : 59;
#endif
	} s;
};

static inline u64 ZIP_QUEX_ERR_INT(u64 param1)
{
	if (param1 <= 7)
		return 0x3000ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_ERR_INT: %llu\n", param1);
	return 0;
}

/* NCB - zip_que#_err_int_w1s */
union zip_quex_err_int_w1s {
	u64 u_reg64;
	struct zip_quex_err_int_w1s_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_5_63               : 59;
		u64 mdbe                        : 1;
		u64 nwrp                        : 1;
		u64 nrrp                        : 1;
		u64 irde                        : 1;
		u64 dovf                        : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 dovf                        : 1;
		u64 irde                        : 1;
		u64 nrrp                        : 1;
		u64 nwrp                        : 1;
		u64 mdbe                        : 1;
		u64 reserved_5_63               : 59;
#endif
	} s;
};

static inline u64 ZIP_QUEX_ERR_INT_W1S(u64 param1)
{
	if (param1 <= 7)
		return 0x3200ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_ERR_INT_W1S: %llu\n", param1);
	return 0;
}

/**
 * union zip_quex_gcfg - Represents the registers that reflect status of the
 * zip instruction queues,debug use only.
 */
union zip_quex_gcfg {
	u64 u_reg64;
	struct zip_quex_gcfg_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_4_63               : 60;
		u64 iqb_ldwb                    : 1;
		u64 cbw_sty                     : 1;
		u64 l2ld_cmd                    : 2;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 l2ld_cmd                    : 2;
		u64 cbw_sty                     : 1;
		u64 iqb_ldwb                    : 1;
		u64 reserved_4_63               : 60;
#endif
	} s;
};

static inline u64 ZIP_QUEX_GCFG(u64 param1)
{
	if (param1 <= 7)
		return 0x1A00ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_GCFG: %llu\n", param1);
	return 0;
}

/**
 * union zip_quex_map - Represents the registers that control how each
 * instruction queue maps to zip cores.
 */
union zip_quex_map {
	u64 u_reg64;
	struct zip_quex_map_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_2_63               : 62;
		u64 zce                         : 2;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 zce                         : 2;
		u64 reserved_2_63               : 62;
#endif
	} s;
};

static inline u64 ZIP_QUEX_MAP(u64 param1)
{
	if (param1 <= 7)
		return 0x1400ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_MAP: %llu\n", param1);
	return 0;
}

/**
 * union zip_quex_sbuf_addr - Represents the registers that set the buffer
 * parameters for the instruction queues.
 *
 * When quiescent (i.e. outstanding doorbell count is 0), it is safe to rewrite
 * this register to effectively reset the command buffer state machine.
 * These registers must be programmed after SW programs the corresponding
 * ZIP_QUE(0..7)_SBUF_CTL.
 */
union zip_quex_sbuf_addr {
	u64 u_reg64;
	struct zip_quex_sbuf_addr_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_49_63              : 15;
		u64 ptr                         : 42;
		u64 off                         : 7;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 off                         : 7;
		u64 ptr                         : 42;
		u64 reserved_49_63              : 15;
#endif
	} s;
};

static inline u64 ZIP_QUEX_SBUF_ADDR(u64 param1)
{
	if (param1 <= 7)
		return 0x1000ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_SBUF_ADDR: %llu\n", param1);
	return 0;
}

/**
 * union zip_quex_sbuf_ctl - Represents the registers that set the buffer
 * parameters for the instruction queues.
 *
 * When quiescent (i.e. outstanding doorbell count is 0), it is safe to rewrite
 * this register to effectively reset the command buffer state machine.
 * These registers must be programmed before SW programs the corresponding
 * ZIP_QUE(0..7)_SBUF_ADDR.
 */
union zip_quex_sbuf_ctl {
	u64 u_reg64;
	struct zip_quex_sbuf_ctl_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_45_63              : 19;
		u64 size                        : 13;
		u64 inst_be                     : 1;
		u64 reserved_24_30              : 7;
		u64 stream_id                   : 8;
		u64 reserved_12_15              : 4;
		u64 aura                        : 12;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 aura                        : 12;
		u64 reserved_12_15              : 4;
		u64 stream_id                   : 8;
		u64 reserved_24_30              : 7;
		u64 inst_be                     : 1;
		u64 size                        : 13;
		u64 reserved_45_63              : 19;
#endif
	} s;
};

static inline u64 ZIP_QUEX_SBUF_CTL(u64 param1)
{
	if (param1 <= 7)
		return 0x1200ull + (param1 & 7) * 0x8ull;
	pr_err("ZIP_QUEX_SBUF_CTL: %llu\n", param1);
	return 0;
}

/**
 * union zip_que_ena - Represents queue enable register
 *
 * If a queue is disabled, ZIP_CTL stops fetching instructions from the queue.
 */
union zip_que_ena {
	u64 u_reg64;
	struct zip_que_ena_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_8_63               : 56;
		u64 ena                         : 8;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 ena                         : 8;
		u64 reserved_8_63               : 56;
#endif
	} s;
};

#define ZIP_QUE_ENA 0x0500ull

/**
 * union zip_que_pri - Represents the register that defines the priority
 * between instruction queues.
 */
union zip_que_pri {
	u64 u_reg64;
	struct zip_que_pri_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_8_63               : 56;
		u64 pri                         : 8;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 pri                         : 8;
		u64 reserved_8_63               : 56;
#endif
	} s;
};

#define ZIP_QUE_PRI 0x0508ull

/**
 * union zip_throttle - Represents the register that controls the maximum
 * number of in-flight X2I data fetch transactions.
 *
 * Writing 0 to this register causes the ZIP module to temporarily suspend NCB
 * accesses; it is not recommended for normal operation, but may be useful for
 * diagnostics.
 */
union zip_throttle {
	u64 u_reg64;
	struct zip_throttle_s {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved_6_63               : 58;
		u64 ld_infl                     : 6;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		u64 ld_infl                     : 6;
		u64 reserved_6_63               : 58;
#endif
	} s;
};

#define ZIP_THROTTLE 0x0010ull

#endif /* _CSRS_ZIP__ */
