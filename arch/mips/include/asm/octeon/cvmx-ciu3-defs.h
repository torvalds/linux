/*
 * Copyright (c) 2003-2016 Cavium Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 */

#ifndef __CVMX_CIU3_DEFS_H__
#define __CVMX_CIU3_DEFS_H__

#define CVMX_CIU3_FUSE CVMX_ADD_IO_SEG(0x00010100000001A0ull)
#define CVMX_CIU3_BIST CVMX_ADD_IO_SEG(0x00010100000001C0ull)
#define CVMX_CIU3_CONST CVMX_ADD_IO_SEG(0x0001010000000220ull)
#define CVMX_CIU3_CTL CVMX_ADD_IO_SEG(0x00010100000000E0ull)
#define CVMX_CIU3_DESTX_IO_INT(offset) (CVMX_ADD_IO_SEG(0x0001010000210000ull) + ((offset) & 7) * 8)
#define CVMX_CIU3_DESTX_PP_INT(offset) (CVMX_ADD_IO_SEG(0x0001010000200000ull) + ((offset) & 255) * 8)
#define CVMX_CIU3_GSTOP CVMX_ADD_IO_SEG(0x0001010000000140ull)
#define CVMX_CIU3_IDTX_CTL(offset) (CVMX_ADD_IO_SEG(0x0001010000110000ull) + ((offset) & 255) * 8)
#define CVMX_CIU3_IDTX_IO(offset) (CVMX_ADD_IO_SEG(0x0001010000130000ull) + ((offset) & 255) * 8)
#define CVMX_CIU3_IDTX_PPX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001010000120000ull) + ((block_id) & 255) * 0x20ull)
#define CVMX_CIU3_INTR_RAM_ECC_CTL CVMX_ADD_IO_SEG(0x0001010000000260ull)
#define CVMX_CIU3_INTR_RAM_ECC_ST CVMX_ADD_IO_SEG(0x0001010000000280ull)
#define CVMX_CIU3_INTR_READY CVMX_ADD_IO_SEG(0x00010100000002A0ull)
#define CVMX_CIU3_INTR_SLOWDOWN CVMX_ADD_IO_SEG(0x0001010000000240ull)
#define CVMX_CIU3_ISCX_CTL(offset) (CVMX_ADD_IO_SEG(0x0001010080000000ull) + ((offset) & 1048575) * 8)
#define CVMX_CIU3_ISCX_W1C(offset) (CVMX_ADD_IO_SEG(0x0001010090000000ull) + ((offset) & 1048575) * 8)
#define CVMX_CIU3_ISCX_W1S(offset) (CVMX_ADD_IO_SEG(0x00010100A0000000ull) + ((offset) & 1048575) * 8)
#define CVMX_CIU3_NMI CVMX_ADD_IO_SEG(0x0001010000000160ull)
#define CVMX_CIU3_SISCX(offset) (CVMX_ADD_IO_SEG(0x0001010000220000ull) + ((offset) & 255) * 8)
#define CVMX_CIU3_TIMX(offset) (CVMX_ADD_IO_SEG(0x0001010000010000ull) + ((offset) & 15) * 8)

union cvmx_ciu3_bist {
	uint64_t u64;
	struct cvmx_ciu3_bist_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t bist                         : 9;
#else
	uint64_t bist                         : 9;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
};

union cvmx_ciu3_const {
	uint64_t u64;
	struct cvmx_ciu3_const_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t dests_io                     : 16;
	uint64_t pintsn                       : 16;
	uint64_t dests_pp                     : 16;
	uint64_t idt                          : 16;
#else
	uint64_t idt                          : 16;
	uint64_t dests_pp                     : 16;
	uint64_t pintsn                       : 16;
	uint64_t dests_io                     : 16;
#endif
	} s;
};

union cvmx_ciu3_ctl {
	uint64_t u64;
	struct cvmx_ciu3_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t mcd_sel                      : 2;
	uint64_t iscmem_le                    : 1;
	uint64_t seq_dis                      : 1;
	uint64_t cclk_dis                     : 1;
#else
	uint64_t cclk_dis                     : 1;
	uint64_t seq_dis                      : 1;
	uint64_t iscmem_le                    : 1;
	uint64_t mcd_sel                      : 2;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
};

union cvmx_ciu3_destx_io_int {
	uint64_t u64;
	struct cvmx_ciu3_destx_io_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_52_63               : 12;
	uint64_t intsn                        : 20;
	uint64_t reserved_10_31               : 22;
	uint64_t intidt                       : 8;
	uint64_t newint                       : 1;
	uint64_t intr                         : 1;
#else
	uint64_t intr                         : 1;
	uint64_t newint                       : 1;
	uint64_t intidt                       : 8;
	uint64_t reserved_10_31               : 22;
	uint64_t intsn                        : 20;
	uint64_t reserved_52_63               : 12;
#endif
	} s;
};

union cvmx_ciu3_destx_pp_int {
	uint64_t u64;
	struct cvmx_ciu3_destx_pp_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_52_63               : 12;
	uint64_t intsn                        : 20;
	uint64_t reserved_10_31               : 22;
	uint64_t intidt                       : 8;
	uint64_t newint                       : 1;
	uint64_t intr                         : 1;
#else
	uint64_t intr                         : 1;
	uint64_t newint                       : 1;
	uint64_t intidt                       : 8;
	uint64_t reserved_10_31               : 22;
	uint64_t intsn                        : 20;
	uint64_t reserved_52_63               : 12;
#endif
	} s;
};

union cvmx_ciu3_gstop {
	uint64_t u64;
	struct cvmx_ciu3_gstop_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t gstop                        : 1;
#else
	uint64_t gstop                        : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
};

union cvmx_ciu3_idtx_ctl {
	uint64_t u64;
	struct cvmx_ciu3_idtx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_52_63               : 12;
	uint64_t intsn                        : 20;
	uint64_t reserved_4_31                : 28;
	uint64_t intr                         : 1;
	uint64_t newint                       : 1;
	uint64_t ip_num                       : 2;
#else
	uint64_t ip_num                       : 2;
	uint64_t newint                       : 1;
	uint64_t intr                         : 1;
	uint64_t reserved_4_31                : 28;
	uint64_t intsn                        : 20;
	uint64_t reserved_52_63               : 12;
#endif
	} s;
};

union cvmx_ciu3_idtx_io {
	uint64_t u64;
	struct cvmx_ciu3_idtx_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t io                           : 5;
#else
	uint64_t io                           : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
};

union cvmx_ciu3_idtx_ppx {
	uint64_t u64;
	struct cvmx_ciu3_idtx_ppx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t pp                           : 48;
#else
	uint64_t pp                           : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
};

union cvmx_ciu3_intr_ram_ecc_ctl {
	uint64_t u64;
	struct cvmx_ciu3_intr_ram_ecc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t flip_synd                    : 2;
	uint64_t ecc_ena                      : 1;
#else
	uint64_t ecc_ena                      : 1;
	uint64_t flip_synd                    : 2;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
};

union cvmx_ciu3_intr_ram_ecc_st {
	uint64_t u64;
	struct cvmx_ciu3_intr_ram_ecc_st_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_52_63               : 12;
	uint64_t addr                         : 20;
	uint64_t reserved_6_31                : 26;
	uint64_t sisc_dbe                     : 1;
	uint64_t sisc_sbe                     : 1;
	uint64_t idt_dbe                      : 1;
	uint64_t idt_sbe                      : 1;
	uint64_t isc_dbe                      : 1;
	uint64_t isc_sbe                      : 1;
#else
	uint64_t isc_sbe                      : 1;
	uint64_t isc_dbe                      : 1;
	uint64_t idt_sbe                      : 1;
	uint64_t idt_dbe                      : 1;
	uint64_t sisc_sbe                     : 1;
	uint64_t sisc_dbe                     : 1;
	uint64_t reserved_6_31                : 26;
	uint64_t addr                         : 20;
	uint64_t reserved_52_63               : 12;
#endif
	} s;
};

union cvmx_ciu3_intr_ready {
	uint64_t u64;
	struct cvmx_ciu3_intr_ready_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_46_63               : 18;
	uint64_t index                        : 14;
	uint64_t reserved_1_31                : 31;
	uint64_t ready                        : 1;
#else
	uint64_t ready                        : 1;
	uint64_t reserved_1_31                : 31;
	uint64_t index                        : 14;
	uint64_t reserved_46_63               : 18;
#endif
	} s;
};

union cvmx_ciu3_intr_slowdown {
	uint64_t u64;
	struct cvmx_ciu3_intr_slowdown_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t ctl                          : 3;
#else
	uint64_t ctl                          : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
};

union cvmx_ciu3_iscx_ctl {
	uint64_t u64;
	struct cvmx_ciu3_iscx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_24_63               : 40;
	uint64_t idt                          : 8;
	uint64_t imp                          : 1;
	uint64_t reserved_2_14                : 13;
	uint64_t en                           : 1;
	uint64_t raw                          : 1;
#else
	uint64_t raw                          : 1;
	uint64_t en                           : 1;
	uint64_t reserved_2_14                : 13;
	uint64_t imp                          : 1;
	uint64_t idt                          : 8;
	uint64_t reserved_24_63               : 40;
#endif
	} s;
};

union cvmx_ciu3_iscx_w1c {
	uint64_t u64;
	struct cvmx_ciu3_iscx_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t en                           : 1;
	uint64_t raw                          : 1;
#else
	uint64_t raw                          : 1;
	uint64_t en                           : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
};

union cvmx_ciu3_iscx_w1s {
	uint64_t u64;
	struct cvmx_ciu3_iscx_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t en                           : 1;
	uint64_t raw                          : 1;
#else
	uint64_t raw                          : 1;
	uint64_t en                           : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
};

union cvmx_ciu3_nmi {
	uint64_t u64;
	struct cvmx_ciu3_nmi_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t nmi                          : 48;
#else
	uint64_t nmi                          : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
};

union cvmx_ciu3_siscx {
	uint64_t u64;
	struct cvmx_ciu3_siscx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t en                           : 64;
#else
	uint64_t en                           : 64;
#endif
	} s;
};

union cvmx_ciu3_timx {
	uint64_t u64;
	struct cvmx_ciu3_timx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_37_63               : 27;
	uint64_t one_shot                     : 1;
	uint64_t len                          : 36;
#else
	uint64_t len                          : 36;
	uint64_t one_shot                     : 1;
	uint64_t reserved_37_63               : 27;
#endif
	} s;
};

#endif
