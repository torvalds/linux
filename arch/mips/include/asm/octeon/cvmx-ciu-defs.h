/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2010 Cavium Networks
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
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

#ifndef __CVMX_CIU_DEFS_H__
#define __CVMX_CIU_DEFS_H__

#define CVMX_CIU_BIST (CVMX_ADD_IO_SEG(0x0001070000000730ull))
#define CVMX_CIU_BLOCK_INT (CVMX_ADD_IO_SEG(0x00010700000007C0ull))
#define CVMX_CIU_DINT (CVMX_ADD_IO_SEG(0x0001070000000720ull))
#define CVMX_CIU_FUSE (CVMX_ADD_IO_SEG(0x0001070000000728ull))
#define CVMX_CIU_GSTOP (CVMX_ADD_IO_SEG(0x0001070000000710ull))
#define CVMX_CIU_INT33_SUM0 (CVMX_ADD_IO_SEG(0x0001070000000110ull))
#define CVMX_CIU_INTX_EN0(offset) (CVMX_ADD_IO_SEG(0x0001070000000200ull) + ((offset) & 63) * 16)
#define CVMX_CIU_INTX_EN0_W1C(offset) (CVMX_ADD_IO_SEG(0x0001070000002200ull) + ((offset) & 63) * 16)
#define CVMX_CIU_INTX_EN0_W1S(offset) (CVMX_ADD_IO_SEG(0x0001070000006200ull) + ((offset) & 63) * 16)
#define CVMX_CIU_INTX_EN1(offset) (CVMX_ADD_IO_SEG(0x0001070000000208ull) + ((offset) & 63) * 16)
#define CVMX_CIU_INTX_EN1_W1C(offset) (CVMX_ADD_IO_SEG(0x0001070000002208ull) + ((offset) & 63) * 16)
#define CVMX_CIU_INTX_EN1_W1S(offset) (CVMX_ADD_IO_SEG(0x0001070000006208ull) + ((offset) & 63) * 16)
#define CVMX_CIU_INTX_EN4_0(offset) (CVMX_ADD_IO_SEG(0x0001070000000C80ull) + ((offset) & 15) * 16)
#define CVMX_CIU_INTX_EN4_0_W1C(offset) (CVMX_ADD_IO_SEG(0x0001070000002C80ull) + ((offset) & 15) * 16)
#define CVMX_CIU_INTX_EN4_0_W1S(offset) (CVMX_ADD_IO_SEG(0x0001070000006C80ull) + ((offset) & 15) * 16)
#define CVMX_CIU_INTX_EN4_1(offset) (CVMX_ADD_IO_SEG(0x0001070000000C88ull) + ((offset) & 15) * 16)
#define CVMX_CIU_INTX_EN4_1_W1C(offset) (CVMX_ADD_IO_SEG(0x0001070000002C88ull) + ((offset) & 15) * 16)
#define CVMX_CIU_INTX_EN4_1_W1S(offset) (CVMX_ADD_IO_SEG(0x0001070000006C88ull) + ((offset) & 15) * 16)
#define CVMX_CIU_INTX_SUM0(offset) (CVMX_ADD_IO_SEG(0x0001070000000000ull) + ((offset) & 63) * 8)
#define CVMX_CIU_INTX_SUM4(offset) (CVMX_ADD_IO_SEG(0x0001070000000C00ull) + ((offset) & 15) * 8)
#define CVMX_CIU_INT_DBG_SEL (CVMX_ADD_IO_SEG(0x00010700000007D0ull))
#define CVMX_CIU_INT_SUM1 (CVMX_ADD_IO_SEG(0x0001070000000108ull))
#define CVMX_CIU_MBOX_CLRX(offset) (CVMX_ADD_IO_SEG(0x0001070000000680ull) + ((offset) & 15) * 8)
#define CVMX_CIU_MBOX_SETX(offset) (CVMX_ADD_IO_SEG(0x0001070000000600ull) + ((offset) & 15) * 8)
#define CVMX_CIU_NMI (CVMX_ADD_IO_SEG(0x0001070000000718ull))
#define CVMX_CIU_PCI_INTA (CVMX_ADD_IO_SEG(0x0001070000000750ull))
#define CVMX_CIU_PP_DBG (CVMX_ADD_IO_SEG(0x0001070000000708ull))
#define CVMX_CIU_PP_POKEX(offset) (CVMX_ADD_IO_SEG(0x0001070000000580ull) + ((offset) & 15) * 8)
#define CVMX_CIU_PP_RST (CVMX_ADD_IO_SEG(0x0001070000000700ull))
#define CVMX_CIU_QLM0 (CVMX_ADD_IO_SEG(0x0001070000000780ull))
#define CVMX_CIU_QLM1 (CVMX_ADD_IO_SEG(0x0001070000000788ull))
#define CVMX_CIU_QLM2 (CVMX_ADD_IO_SEG(0x0001070000000790ull))
#define CVMX_CIU_QLM_DCOK (CVMX_ADD_IO_SEG(0x0001070000000760ull))
#define CVMX_CIU_QLM_JTGC (CVMX_ADD_IO_SEG(0x0001070000000768ull))
#define CVMX_CIU_QLM_JTGD (CVMX_ADD_IO_SEG(0x0001070000000770ull))
#define CVMX_CIU_SOFT_BIST (CVMX_ADD_IO_SEG(0x0001070000000738ull))
#define CVMX_CIU_SOFT_PRST (CVMX_ADD_IO_SEG(0x0001070000000748ull))
#define CVMX_CIU_SOFT_PRST1 (CVMX_ADD_IO_SEG(0x0001070000000758ull))
#define CVMX_CIU_SOFT_RST (CVMX_ADD_IO_SEG(0x0001070000000740ull))
#define CVMX_CIU_TIMX(offset) (CVMX_ADD_IO_SEG(0x0001070000000480ull) + ((offset) & 3) * 8)
#define CVMX_CIU_WDOGX(offset) (CVMX_ADD_IO_SEG(0x0001070000000500ull) + ((offset) & 15) * 8)

union cvmx_ciu_bist {
	uint64_t u64;
	struct cvmx_ciu_bist_s {
		uint64_t reserved_5_63:59;
		uint64_t bist:5;
	} s;
	struct cvmx_ciu_bist_cn30xx {
		uint64_t reserved_4_63:60;
		uint64_t bist:4;
	} cn30xx;
	struct cvmx_ciu_bist_cn30xx cn31xx;
	struct cvmx_ciu_bist_cn30xx cn38xx;
	struct cvmx_ciu_bist_cn30xx cn38xxp2;
	struct cvmx_ciu_bist_cn50xx {
		uint64_t reserved_2_63:62;
		uint64_t bist:2;
	} cn50xx;
	struct cvmx_ciu_bist_cn52xx {
		uint64_t reserved_3_63:61;
		uint64_t bist:3;
	} cn52xx;
	struct cvmx_ciu_bist_cn52xx cn52xxp1;
	struct cvmx_ciu_bist_cn30xx cn56xx;
	struct cvmx_ciu_bist_cn30xx cn56xxp1;
	struct cvmx_ciu_bist_cn30xx cn58xx;
	struct cvmx_ciu_bist_cn30xx cn58xxp1;
	struct cvmx_ciu_bist_s cn63xx;
	struct cvmx_ciu_bist_s cn63xxp1;
};

union cvmx_ciu_block_int {
	uint64_t u64;
	struct cvmx_ciu_block_int_s {
		uint64_t reserved_43_63:21;
		uint64_t ptp:1;
		uint64_t dpi:1;
		uint64_t dfm:1;
		uint64_t reserved_34_39:6;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t reserved_31_31:1;
		uint64_t iob:1;
		uint64_t reserved_29_29:1;
		uint64_t agl:1;
		uint64_t reserved_27_27:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t reserved_23_24:2;
		uint64_t asxpcs0:1;
		uint64_t reserved_21_21:1;
		uint64_t pip:1;
		uint64_t reserved_18_19:2;
		uint64_t lmc0:1;
		uint64_t l2c:1;
		uint64_t reserved_15_15:1;
		uint64_t rad:1;
		uint64_t usb:1;
		uint64_t pow:1;
		uint64_t tim:1;
		uint64_t pko:1;
		uint64_t ipd:1;
		uint64_t reserved_8_8:1;
		uint64_t zip:1;
		uint64_t dfa:1;
		uint64_t fpa:1;
		uint64_t key:1;
		uint64_t sli:1;
		uint64_t reserved_2_2:1;
		uint64_t gmx0:1;
		uint64_t mio:1;
	} s;
	struct cvmx_ciu_block_int_s cn63xx;
	struct cvmx_ciu_block_int_s cn63xxp1;
};

union cvmx_ciu_dint {
	uint64_t u64;
	struct cvmx_ciu_dint_s {
		uint64_t reserved_16_63:48;
		uint64_t dint:16;
	} s;
	struct cvmx_ciu_dint_cn30xx {
		uint64_t reserved_1_63:63;
		uint64_t dint:1;
	} cn30xx;
	struct cvmx_ciu_dint_cn31xx {
		uint64_t reserved_2_63:62;
		uint64_t dint:2;
	} cn31xx;
	struct cvmx_ciu_dint_s cn38xx;
	struct cvmx_ciu_dint_s cn38xxp2;
	struct cvmx_ciu_dint_cn31xx cn50xx;
	struct cvmx_ciu_dint_cn52xx {
		uint64_t reserved_4_63:60;
		uint64_t dint:4;
	} cn52xx;
	struct cvmx_ciu_dint_cn52xx cn52xxp1;
	struct cvmx_ciu_dint_cn56xx {
		uint64_t reserved_12_63:52;
		uint64_t dint:12;
	} cn56xx;
	struct cvmx_ciu_dint_cn56xx cn56xxp1;
	struct cvmx_ciu_dint_s cn58xx;
	struct cvmx_ciu_dint_s cn58xxp1;
	struct cvmx_ciu_dint_cn63xx {
		uint64_t reserved_6_63:58;
		uint64_t dint:6;
	} cn63xx;
	struct cvmx_ciu_dint_cn63xx cn63xxp1;
};

union cvmx_ciu_fuse {
	uint64_t u64;
	struct cvmx_ciu_fuse_s {
		uint64_t reserved_16_63:48;
		uint64_t fuse:16;
	} s;
	struct cvmx_ciu_fuse_cn30xx {
		uint64_t reserved_1_63:63;
		uint64_t fuse:1;
	} cn30xx;
	struct cvmx_ciu_fuse_cn31xx {
		uint64_t reserved_2_63:62;
		uint64_t fuse:2;
	} cn31xx;
	struct cvmx_ciu_fuse_s cn38xx;
	struct cvmx_ciu_fuse_s cn38xxp2;
	struct cvmx_ciu_fuse_cn31xx cn50xx;
	struct cvmx_ciu_fuse_cn52xx {
		uint64_t reserved_4_63:60;
		uint64_t fuse:4;
	} cn52xx;
	struct cvmx_ciu_fuse_cn52xx cn52xxp1;
	struct cvmx_ciu_fuse_cn56xx {
		uint64_t reserved_12_63:52;
		uint64_t fuse:12;
	} cn56xx;
	struct cvmx_ciu_fuse_cn56xx cn56xxp1;
	struct cvmx_ciu_fuse_s cn58xx;
	struct cvmx_ciu_fuse_s cn58xxp1;
	struct cvmx_ciu_fuse_cn63xx {
		uint64_t reserved_6_63:58;
		uint64_t fuse:6;
	} cn63xx;
	struct cvmx_ciu_fuse_cn63xx cn63xxp1;
};

union cvmx_ciu_gstop {
	uint64_t u64;
	struct cvmx_ciu_gstop_s {
		uint64_t reserved_1_63:63;
		uint64_t gstop:1;
	} s;
	struct cvmx_ciu_gstop_s cn30xx;
	struct cvmx_ciu_gstop_s cn31xx;
	struct cvmx_ciu_gstop_s cn38xx;
	struct cvmx_ciu_gstop_s cn38xxp2;
	struct cvmx_ciu_gstop_s cn50xx;
	struct cvmx_ciu_gstop_s cn52xx;
	struct cvmx_ciu_gstop_s cn52xxp1;
	struct cvmx_ciu_gstop_s cn56xx;
	struct cvmx_ciu_gstop_s cn56xxp1;
	struct cvmx_ciu_gstop_s cn58xx;
	struct cvmx_ciu_gstop_s cn58xxp1;
	struct cvmx_ciu_gstop_s cn63xx;
	struct cvmx_ciu_gstop_s cn63xxp1;
};

union cvmx_ciu_intx_en0 {
	uint64_t u64;
	struct cvmx_ciu_intx_en0_s {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} s;
	struct cvmx_ciu_intx_en0_cn30xx {
		uint64_t reserved_59_63:5;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_47_47:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn30xx;
	struct cvmx_ciu_intx_en0_cn31xx {
		uint64_t reserved_59_63:5;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn31xx;
	struct cvmx_ciu_intx_en0_cn38xx {
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn38xx;
	struct cvmx_ciu_intx_en0_cn38xx cn38xxp2;
	struct cvmx_ciu_intx_en0_cn30xx cn50xx;
	struct cvmx_ciu_intx_en0_cn52xx {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn52xx;
	struct cvmx_ciu_intx_en0_cn52xx cn52xxp1;
	struct cvmx_ciu_intx_en0_cn56xx {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn56xx;
	struct cvmx_ciu_intx_en0_cn56xx cn56xxp1;
	struct cvmx_ciu_intx_en0_cn38xx cn58xx;
	struct cvmx_ciu_intx_en0_cn38xx cn58xxp1;
	struct cvmx_ciu_intx_en0_cn52xx cn63xx;
	struct cvmx_ciu_intx_en0_cn52xx cn63xxp1;
};

union cvmx_ciu_intx_en0_w1c {
	uint64_t u64;
	struct cvmx_ciu_intx_en0_w1c_s {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} s;
	struct cvmx_ciu_intx_en0_w1c_cn52xx {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn52xx;
	struct cvmx_ciu_intx_en0_w1c_s cn56xx;
	struct cvmx_ciu_intx_en0_w1c_cn58xx {
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn58xx;
	struct cvmx_ciu_intx_en0_w1c_cn52xx cn63xx;
	struct cvmx_ciu_intx_en0_w1c_cn52xx cn63xxp1;
};

union cvmx_ciu_intx_en0_w1s {
	uint64_t u64;
	struct cvmx_ciu_intx_en0_w1s_s {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} s;
	struct cvmx_ciu_intx_en0_w1s_cn52xx {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn52xx;
	struct cvmx_ciu_intx_en0_w1s_s cn56xx;
	struct cvmx_ciu_intx_en0_w1s_cn58xx {
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn58xx;
	struct cvmx_ciu_intx_en0_w1s_cn52xx cn63xx;
	struct cvmx_ciu_intx_en0_w1s_cn52xx cn63xxp1;
};

union cvmx_ciu_intx_en1 {
	uint64_t u64;
	struct cvmx_ciu_intx_en1_s {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
	} s;
	struct cvmx_ciu_intx_en1_cn30xx {
		uint64_t reserved_1_63:63;
		uint64_t wdog:1;
	} cn30xx;
	struct cvmx_ciu_intx_en1_cn31xx {
		uint64_t reserved_2_63:62;
		uint64_t wdog:2;
	} cn31xx;
	struct cvmx_ciu_intx_en1_cn38xx {
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
	} cn38xx;
	struct cvmx_ciu_intx_en1_cn38xx cn38xxp2;
	struct cvmx_ciu_intx_en1_cn31xx cn50xx;
	struct cvmx_ciu_intx_en1_cn52xx {
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
	} cn52xx;
	struct cvmx_ciu_intx_en1_cn52xxp1 {
		uint64_t reserved_19_63:45;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
	} cn52xxp1;
	struct cvmx_ciu_intx_en1_cn56xx {
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
	} cn56xx;
	struct cvmx_ciu_intx_en1_cn56xx cn56xxp1;
	struct cvmx_ciu_intx_en1_cn38xx cn58xx;
	struct cvmx_ciu_intx_en1_cn38xx cn58xxp1;
	struct cvmx_ciu_intx_en1_cn63xx {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
	} cn63xx;
	struct cvmx_ciu_intx_en1_cn63xx cn63xxp1;
};

union cvmx_ciu_intx_en1_w1c {
	uint64_t u64;
	struct cvmx_ciu_intx_en1_w1c_s {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
	} s;
	struct cvmx_ciu_intx_en1_w1c_cn52xx {
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
	} cn52xx;
	struct cvmx_ciu_intx_en1_w1c_cn56xx {
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
	} cn56xx;
	struct cvmx_ciu_intx_en1_w1c_cn58xx {
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
	} cn58xx;
	struct cvmx_ciu_intx_en1_w1c_cn63xx {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
	} cn63xx;
	struct cvmx_ciu_intx_en1_w1c_cn63xx cn63xxp1;
};

union cvmx_ciu_intx_en1_w1s {
	uint64_t u64;
	struct cvmx_ciu_intx_en1_w1s_s {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
	} s;
	struct cvmx_ciu_intx_en1_w1s_cn52xx {
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
	} cn52xx;
	struct cvmx_ciu_intx_en1_w1s_cn56xx {
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
	} cn56xx;
	struct cvmx_ciu_intx_en1_w1s_cn58xx {
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
	} cn58xx;
	struct cvmx_ciu_intx_en1_w1s_cn63xx {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
	} cn63xx;
	struct cvmx_ciu_intx_en1_w1s_cn63xx cn63xxp1;
};

union cvmx_ciu_intx_en4_0 {
	uint64_t u64;
	struct cvmx_ciu_intx_en4_0_s {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} s;
	struct cvmx_ciu_intx_en4_0_cn50xx {
		uint64_t reserved_59_63:5;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_47_47:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn50xx;
	struct cvmx_ciu_intx_en4_0_cn52xx {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn52xx;
	struct cvmx_ciu_intx_en4_0_cn52xx cn52xxp1;
	struct cvmx_ciu_intx_en4_0_cn56xx {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn56xx;
	struct cvmx_ciu_intx_en4_0_cn56xx cn56xxp1;
	struct cvmx_ciu_intx_en4_0_cn58xx {
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn58xx;
	struct cvmx_ciu_intx_en4_0_cn58xx cn58xxp1;
	struct cvmx_ciu_intx_en4_0_cn52xx cn63xx;
	struct cvmx_ciu_intx_en4_0_cn52xx cn63xxp1;
};

union cvmx_ciu_intx_en4_0_w1c {
	uint64_t u64;
	struct cvmx_ciu_intx_en4_0_w1c_s {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} s;
	struct cvmx_ciu_intx_en4_0_w1c_cn52xx {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn52xx;
	struct cvmx_ciu_intx_en4_0_w1c_s cn56xx;
	struct cvmx_ciu_intx_en4_0_w1c_cn58xx {
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn58xx;
	struct cvmx_ciu_intx_en4_0_w1c_cn52xx cn63xx;
	struct cvmx_ciu_intx_en4_0_w1c_cn52xx cn63xxp1;
};

union cvmx_ciu_intx_en4_0_w1s {
	uint64_t u64;
	struct cvmx_ciu_intx_en4_0_w1s_s {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} s;
	struct cvmx_ciu_intx_en4_0_w1s_cn52xx {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn52xx;
	struct cvmx_ciu_intx_en4_0_w1s_s cn56xx;
	struct cvmx_ciu_intx_en4_0_w1s_cn58xx {
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t reserved_44_44:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn58xx;
	struct cvmx_ciu_intx_en4_0_w1s_cn52xx cn63xx;
	struct cvmx_ciu_intx_en4_0_w1s_cn52xx cn63xxp1;
};

union cvmx_ciu_intx_en4_1 {
	uint64_t u64;
	struct cvmx_ciu_intx_en4_1_s {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
	} s;
	struct cvmx_ciu_intx_en4_1_cn50xx {
		uint64_t reserved_2_63:62;
		uint64_t wdog:2;
	} cn50xx;
	struct cvmx_ciu_intx_en4_1_cn52xx {
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
	} cn52xx;
	struct cvmx_ciu_intx_en4_1_cn52xxp1 {
		uint64_t reserved_19_63:45;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
	} cn52xxp1;
	struct cvmx_ciu_intx_en4_1_cn56xx {
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
	} cn56xx;
	struct cvmx_ciu_intx_en4_1_cn56xx cn56xxp1;
	struct cvmx_ciu_intx_en4_1_cn58xx {
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
	} cn58xx;
	struct cvmx_ciu_intx_en4_1_cn58xx cn58xxp1;
	struct cvmx_ciu_intx_en4_1_cn63xx {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
	} cn63xx;
	struct cvmx_ciu_intx_en4_1_cn63xx cn63xxp1;
};

union cvmx_ciu_intx_en4_1_w1c {
	uint64_t u64;
	struct cvmx_ciu_intx_en4_1_w1c_s {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
	} s;
	struct cvmx_ciu_intx_en4_1_w1c_cn52xx {
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
	} cn52xx;
	struct cvmx_ciu_intx_en4_1_w1c_cn56xx {
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
	} cn56xx;
	struct cvmx_ciu_intx_en4_1_w1c_cn58xx {
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
	} cn58xx;
	struct cvmx_ciu_intx_en4_1_w1c_cn63xx {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
	} cn63xx;
	struct cvmx_ciu_intx_en4_1_w1c_cn63xx cn63xxp1;
};

union cvmx_ciu_intx_en4_1_w1s {
	uint64_t u64;
	struct cvmx_ciu_intx_en4_1_w1s_s {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
	} s;
	struct cvmx_ciu_intx_en4_1_w1s_cn52xx {
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
	} cn52xx;
	struct cvmx_ciu_intx_en4_1_w1s_cn56xx {
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
	} cn56xx;
	struct cvmx_ciu_intx_en4_1_w1s_cn58xx {
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
	} cn58xx;
	struct cvmx_ciu_intx_en4_1_w1s_cn63xx {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
	} cn63xx;
	struct cvmx_ciu_intx_en4_1_w1s_cn63xx cn63xxp1;
};

union cvmx_ciu_intx_sum0 {
	uint64_t u64;
	struct cvmx_ciu_intx_sum0_s {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} s;
	struct cvmx_ciu_intx_sum0_cn30xx {
		uint64_t reserved_59_63:5;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_47_47:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn30xx;
	struct cvmx_ciu_intx_sum0_cn31xx {
		uint64_t reserved_59_63:5;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn31xx;
	struct cvmx_ciu_intx_sum0_cn38xx {
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn38xx;
	struct cvmx_ciu_intx_sum0_cn38xx cn38xxp2;
	struct cvmx_ciu_intx_sum0_cn30xx cn50xx;
	struct cvmx_ciu_intx_sum0_cn52xx {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn52xx;
	struct cvmx_ciu_intx_sum0_cn52xx cn52xxp1;
	struct cvmx_ciu_intx_sum0_cn56xx {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn56xx;
	struct cvmx_ciu_intx_sum0_cn56xx cn56xxp1;
	struct cvmx_ciu_intx_sum0_cn38xx cn58xx;
	struct cvmx_ciu_intx_sum0_cn38xx cn58xxp1;
	struct cvmx_ciu_intx_sum0_cn52xx cn63xx;
	struct cvmx_ciu_intx_sum0_cn52xx cn63xxp1;
};

union cvmx_ciu_intx_sum4 {
	uint64_t u64;
	struct cvmx_ciu_intx_sum4_s {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} s;
	struct cvmx_ciu_intx_sum4_cn50xx {
		uint64_t reserved_59_63:5;
		uint64_t mpi:1;
		uint64_t pcm:1;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t reserved_47_47:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn50xx;
	struct cvmx_ciu_intx_sum4_cn52xx {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn52xx;
	struct cvmx_ciu_intx_sum4_cn52xx cn52xxp1;
	struct cvmx_ciu_intx_sum4_cn56xx {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn56xx;
	struct cvmx_ciu_intx_sum4_cn56xx cn56xxp1;
	struct cvmx_ciu_intx_sum4_cn58xx {
		uint64_t reserved_56_63:8;
		uint64_t timer:4;
		uint64_t key_zero:1;
		uint64_t ipd_drp:1;
		uint64_t gmx_drp:2;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} cn58xx;
	struct cvmx_ciu_intx_sum4_cn58xx cn58xxp1;
	struct cvmx_ciu_intx_sum4_cn52xx cn63xx;
	struct cvmx_ciu_intx_sum4_cn52xx cn63xxp1;
};

union cvmx_ciu_int33_sum0 {
	uint64_t u64;
	struct cvmx_ciu_int33_sum0_s {
		uint64_t bootdma:1;
		uint64_t mii:1;
		uint64_t ipdppthr:1;
		uint64_t powiq:1;
		uint64_t twsi2:1;
		uint64_t reserved_57_58:2;
		uint64_t usb:1;
		uint64_t timer:4;
		uint64_t reserved_51_51:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_49_49:1;
		uint64_t gmx_drp:1;
		uint64_t trace:1;
		uint64_t rml:1;
		uint64_t twsi:1;
		uint64_t wdog_sum:1;
		uint64_t pci_msi:4;
		uint64_t pci_int:4;
		uint64_t uart:2;
		uint64_t mbox:2;
		uint64_t gpio:16;
		uint64_t workq:16;
	} s;
	struct cvmx_ciu_int33_sum0_s cn63xx;
	struct cvmx_ciu_int33_sum0_s cn63xxp1;
};

union cvmx_ciu_int_dbg_sel {
	uint64_t u64;
	struct cvmx_ciu_int_dbg_sel_s {
		uint64_t reserved_19_63:45;
		uint64_t sel:3;
		uint64_t reserved_10_15:6;
		uint64_t irq:2;
		uint64_t reserved_3_7:5;
		uint64_t pp:3;
	} s;
	struct cvmx_ciu_int_dbg_sel_s cn63xx;
};

union cvmx_ciu_int_sum1 {
	uint64_t u64;
	struct cvmx_ciu_int_sum1_s {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t wdog:16;
	} s;
	struct cvmx_ciu_int_sum1_cn30xx {
		uint64_t reserved_1_63:63;
		uint64_t wdog:1;
	} cn30xx;
	struct cvmx_ciu_int_sum1_cn31xx {
		uint64_t reserved_2_63:62;
		uint64_t wdog:2;
	} cn31xx;
	struct cvmx_ciu_int_sum1_cn38xx {
		uint64_t reserved_16_63:48;
		uint64_t wdog:16;
	} cn38xx;
	struct cvmx_ciu_int_sum1_cn38xx cn38xxp2;
	struct cvmx_ciu_int_sum1_cn31xx cn50xx;
	struct cvmx_ciu_int_sum1_cn52xx {
		uint64_t reserved_20_63:44;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
	} cn52xx;
	struct cvmx_ciu_int_sum1_cn52xxp1 {
		uint64_t reserved_19_63:45;
		uint64_t mii1:1;
		uint64_t usb1:1;
		uint64_t uart2:1;
		uint64_t reserved_4_15:12;
		uint64_t wdog:4;
	} cn52xxp1;
	struct cvmx_ciu_int_sum1_cn56xx {
		uint64_t reserved_12_63:52;
		uint64_t wdog:12;
	} cn56xx;
	struct cvmx_ciu_int_sum1_cn56xx cn56xxp1;
	struct cvmx_ciu_int_sum1_cn38xx cn58xx;
	struct cvmx_ciu_int_sum1_cn38xx cn58xxp1;
	struct cvmx_ciu_int_sum1_cn63xx {
		uint64_t rst:1;
		uint64_t reserved_57_62:6;
		uint64_t dfm:1;
		uint64_t reserved_53_55:3;
		uint64_t lmc0:1;
		uint64_t srio1:1;
		uint64_t srio0:1;
		uint64_t pem1:1;
		uint64_t pem0:1;
		uint64_t ptp:1;
		uint64_t agl:1;
		uint64_t reserved_37_45:9;
		uint64_t agx0:1;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t usb:1;
		uint64_t dfa:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t zip:1;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t l2c:1;
		uint64_t pow:1;
		uint64_t fpa:1;
		uint64_t iob:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t mii1:1;
		uint64_t reserved_6_17:12;
		uint64_t wdog:6;
	} cn63xx;
	struct cvmx_ciu_int_sum1_cn63xx cn63xxp1;
};

union cvmx_ciu_mbox_clrx {
	uint64_t u64;
	struct cvmx_ciu_mbox_clrx_s {
		uint64_t reserved_32_63:32;
		uint64_t bits:32;
	} s;
	struct cvmx_ciu_mbox_clrx_s cn30xx;
	struct cvmx_ciu_mbox_clrx_s cn31xx;
	struct cvmx_ciu_mbox_clrx_s cn38xx;
	struct cvmx_ciu_mbox_clrx_s cn38xxp2;
	struct cvmx_ciu_mbox_clrx_s cn50xx;
	struct cvmx_ciu_mbox_clrx_s cn52xx;
	struct cvmx_ciu_mbox_clrx_s cn52xxp1;
	struct cvmx_ciu_mbox_clrx_s cn56xx;
	struct cvmx_ciu_mbox_clrx_s cn56xxp1;
	struct cvmx_ciu_mbox_clrx_s cn58xx;
	struct cvmx_ciu_mbox_clrx_s cn58xxp1;
	struct cvmx_ciu_mbox_clrx_s cn63xx;
	struct cvmx_ciu_mbox_clrx_s cn63xxp1;
};

union cvmx_ciu_mbox_setx {
	uint64_t u64;
	struct cvmx_ciu_mbox_setx_s {
		uint64_t reserved_32_63:32;
		uint64_t bits:32;
	} s;
	struct cvmx_ciu_mbox_setx_s cn30xx;
	struct cvmx_ciu_mbox_setx_s cn31xx;
	struct cvmx_ciu_mbox_setx_s cn38xx;
	struct cvmx_ciu_mbox_setx_s cn38xxp2;
	struct cvmx_ciu_mbox_setx_s cn50xx;
	struct cvmx_ciu_mbox_setx_s cn52xx;
	struct cvmx_ciu_mbox_setx_s cn52xxp1;
	struct cvmx_ciu_mbox_setx_s cn56xx;
	struct cvmx_ciu_mbox_setx_s cn56xxp1;
	struct cvmx_ciu_mbox_setx_s cn58xx;
	struct cvmx_ciu_mbox_setx_s cn58xxp1;
	struct cvmx_ciu_mbox_setx_s cn63xx;
	struct cvmx_ciu_mbox_setx_s cn63xxp1;
};

union cvmx_ciu_nmi {
	uint64_t u64;
	struct cvmx_ciu_nmi_s {
		uint64_t reserved_16_63:48;
		uint64_t nmi:16;
	} s;
	struct cvmx_ciu_nmi_cn30xx {
		uint64_t reserved_1_63:63;
		uint64_t nmi:1;
	} cn30xx;
	struct cvmx_ciu_nmi_cn31xx {
		uint64_t reserved_2_63:62;
		uint64_t nmi:2;
	} cn31xx;
	struct cvmx_ciu_nmi_s cn38xx;
	struct cvmx_ciu_nmi_s cn38xxp2;
	struct cvmx_ciu_nmi_cn31xx cn50xx;
	struct cvmx_ciu_nmi_cn52xx {
		uint64_t reserved_4_63:60;
		uint64_t nmi:4;
	} cn52xx;
	struct cvmx_ciu_nmi_cn52xx cn52xxp1;
	struct cvmx_ciu_nmi_cn56xx {
		uint64_t reserved_12_63:52;
		uint64_t nmi:12;
	} cn56xx;
	struct cvmx_ciu_nmi_cn56xx cn56xxp1;
	struct cvmx_ciu_nmi_s cn58xx;
	struct cvmx_ciu_nmi_s cn58xxp1;
	struct cvmx_ciu_nmi_cn63xx {
		uint64_t reserved_6_63:58;
		uint64_t nmi:6;
	} cn63xx;
	struct cvmx_ciu_nmi_cn63xx cn63xxp1;
};

union cvmx_ciu_pci_inta {
	uint64_t u64;
	struct cvmx_ciu_pci_inta_s {
		uint64_t reserved_2_63:62;
		uint64_t intr:2;
	} s;
	struct cvmx_ciu_pci_inta_s cn30xx;
	struct cvmx_ciu_pci_inta_s cn31xx;
	struct cvmx_ciu_pci_inta_s cn38xx;
	struct cvmx_ciu_pci_inta_s cn38xxp2;
	struct cvmx_ciu_pci_inta_s cn50xx;
	struct cvmx_ciu_pci_inta_s cn52xx;
	struct cvmx_ciu_pci_inta_s cn52xxp1;
	struct cvmx_ciu_pci_inta_s cn56xx;
	struct cvmx_ciu_pci_inta_s cn56xxp1;
	struct cvmx_ciu_pci_inta_s cn58xx;
	struct cvmx_ciu_pci_inta_s cn58xxp1;
	struct cvmx_ciu_pci_inta_s cn63xx;
	struct cvmx_ciu_pci_inta_s cn63xxp1;
};

union cvmx_ciu_pp_dbg {
	uint64_t u64;
	struct cvmx_ciu_pp_dbg_s {
		uint64_t reserved_16_63:48;
		uint64_t ppdbg:16;
	} s;
	struct cvmx_ciu_pp_dbg_cn30xx {
		uint64_t reserved_1_63:63;
		uint64_t ppdbg:1;
	} cn30xx;
	struct cvmx_ciu_pp_dbg_cn31xx {
		uint64_t reserved_2_63:62;
		uint64_t ppdbg:2;
	} cn31xx;
	struct cvmx_ciu_pp_dbg_s cn38xx;
	struct cvmx_ciu_pp_dbg_s cn38xxp2;
	struct cvmx_ciu_pp_dbg_cn31xx cn50xx;
	struct cvmx_ciu_pp_dbg_cn52xx {
		uint64_t reserved_4_63:60;
		uint64_t ppdbg:4;
	} cn52xx;
	struct cvmx_ciu_pp_dbg_cn52xx cn52xxp1;
	struct cvmx_ciu_pp_dbg_cn56xx {
		uint64_t reserved_12_63:52;
		uint64_t ppdbg:12;
	} cn56xx;
	struct cvmx_ciu_pp_dbg_cn56xx cn56xxp1;
	struct cvmx_ciu_pp_dbg_s cn58xx;
	struct cvmx_ciu_pp_dbg_s cn58xxp1;
	struct cvmx_ciu_pp_dbg_cn63xx {
		uint64_t reserved_6_63:58;
		uint64_t ppdbg:6;
	} cn63xx;
	struct cvmx_ciu_pp_dbg_cn63xx cn63xxp1;
};

union cvmx_ciu_pp_pokex {
	uint64_t u64;
	struct cvmx_ciu_pp_pokex_s {
		uint64_t poke:64;
	} s;
	struct cvmx_ciu_pp_pokex_s cn30xx;
	struct cvmx_ciu_pp_pokex_s cn31xx;
	struct cvmx_ciu_pp_pokex_s cn38xx;
	struct cvmx_ciu_pp_pokex_s cn38xxp2;
	struct cvmx_ciu_pp_pokex_s cn50xx;
	struct cvmx_ciu_pp_pokex_s cn52xx;
	struct cvmx_ciu_pp_pokex_s cn52xxp1;
	struct cvmx_ciu_pp_pokex_s cn56xx;
	struct cvmx_ciu_pp_pokex_s cn56xxp1;
	struct cvmx_ciu_pp_pokex_s cn58xx;
	struct cvmx_ciu_pp_pokex_s cn58xxp1;
	struct cvmx_ciu_pp_pokex_s cn63xx;
	struct cvmx_ciu_pp_pokex_s cn63xxp1;
};

union cvmx_ciu_pp_rst {
	uint64_t u64;
	struct cvmx_ciu_pp_rst_s {
		uint64_t reserved_16_63:48;
		uint64_t rst:15;
		uint64_t rst0:1;
	} s;
	struct cvmx_ciu_pp_rst_cn30xx {
		uint64_t reserved_1_63:63;
		uint64_t rst0:1;
	} cn30xx;
	struct cvmx_ciu_pp_rst_cn31xx {
		uint64_t reserved_2_63:62;
		uint64_t rst:1;
		uint64_t rst0:1;
	} cn31xx;
	struct cvmx_ciu_pp_rst_s cn38xx;
	struct cvmx_ciu_pp_rst_s cn38xxp2;
	struct cvmx_ciu_pp_rst_cn31xx cn50xx;
	struct cvmx_ciu_pp_rst_cn52xx {
		uint64_t reserved_4_63:60;
		uint64_t rst:3;
		uint64_t rst0:1;
	} cn52xx;
	struct cvmx_ciu_pp_rst_cn52xx cn52xxp1;
	struct cvmx_ciu_pp_rst_cn56xx {
		uint64_t reserved_12_63:52;
		uint64_t rst:11;
		uint64_t rst0:1;
	} cn56xx;
	struct cvmx_ciu_pp_rst_cn56xx cn56xxp1;
	struct cvmx_ciu_pp_rst_s cn58xx;
	struct cvmx_ciu_pp_rst_s cn58xxp1;
	struct cvmx_ciu_pp_rst_cn63xx {
		uint64_t reserved_6_63:58;
		uint64_t rst:5;
		uint64_t rst0:1;
	} cn63xx;
	struct cvmx_ciu_pp_rst_cn63xx cn63xxp1;
};

union cvmx_ciu_qlm0 {
	uint64_t u64;
	struct cvmx_ciu_qlm0_s {
		uint64_t g2bypass:1;
		uint64_t reserved_53_62:10;
		uint64_t g2deemph:5;
		uint64_t reserved_45_47:3;
		uint64_t g2margin:5;
		uint64_t reserved_32_39:8;
		uint64_t txbypass:1;
		uint64_t reserved_21_30:10;
		uint64_t txdeemph:5;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
	} s;
	struct cvmx_ciu_qlm0_s cn63xx;
	struct cvmx_ciu_qlm0_cn63xxp1 {
		uint64_t reserved_32_63:32;
		uint64_t txbypass:1;
		uint64_t reserved_20_30:11;
		uint64_t txdeemph:4;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
	} cn63xxp1;
};

union cvmx_ciu_qlm1 {
	uint64_t u64;
	struct cvmx_ciu_qlm1_s {
		uint64_t g2bypass:1;
		uint64_t reserved_53_62:10;
		uint64_t g2deemph:5;
		uint64_t reserved_45_47:3;
		uint64_t g2margin:5;
		uint64_t reserved_32_39:8;
		uint64_t txbypass:1;
		uint64_t reserved_21_30:10;
		uint64_t txdeemph:5;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
	} s;
	struct cvmx_ciu_qlm1_s cn63xx;
	struct cvmx_ciu_qlm1_cn63xxp1 {
		uint64_t reserved_32_63:32;
		uint64_t txbypass:1;
		uint64_t reserved_20_30:11;
		uint64_t txdeemph:4;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
	} cn63xxp1;
};

union cvmx_ciu_qlm2 {
	uint64_t u64;
	struct cvmx_ciu_qlm2_s {
		uint64_t reserved_32_63:32;
		uint64_t txbypass:1;
		uint64_t reserved_21_30:10;
		uint64_t txdeemph:5;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
	} s;
	struct cvmx_ciu_qlm2_s cn63xx;
	struct cvmx_ciu_qlm2_cn63xxp1 {
		uint64_t reserved_32_63:32;
		uint64_t txbypass:1;
		uint64_t reserved_20_30:11;
		uint64_t txdeemph:4;
		uint64_t reserved_13_15:3;
		uint64_t txmargin:5;
		uint64_t reserved_4_7:4;
		uint64_t lane_en:4;
	} cn63xxp1;
};

union cvmx_ciu_qlm_dcok {
	uint64_t u64;
	struct cvmx_ciu_qlm_dcok_s {
		uint64_t reserved_4_63:60;
		uint64_t qlm_dcok:4;
	} s;
	struct cvmx_ciu_qlm_dcok_cn52xx {
		uint64_t reserved_2_63:62;
		uint64_t qlm_dcok:2;
	} cn52xx;
	struct cvmx_ciu_qlm_dcok_cn52xx cn52xxp1;
	struct cvmx_ciu_qlm_dcok_s cn56xx;
	struct cvmx_ciu_qlm_dcok_s cn56xxp1;
};

union cvmx_ciu_qlm_jtgc {
	uint64_t u64;
	struct cvmx_ciu_qlm_jtgc_s {
		uint64_t reserved_11_63:53;
		uint64_t clk_div:3;
		uint64_t reserved_6_7:2;
		uint64_t mux_sel:2;
		uint64_t bypass:4;
	} s;
	struct cvmx_ciu_qlm_jtgc_cn52xx {
		uint64_t reserved_11_63:53;
		uint64_t clk_div:3;
		uint64_t reserved_5_7:3;
		uint64_t mux_sel:1;
		uint64_t reserved_2_3:2;
		uint64_t bypass:2;
	} cn52xx;
	struct cvmx_ciu_qlm_jtgc_cn52xx cn52xxp1;
	struct cvmx_ciu_qlm_jtgc_s cn56xx;
	struct cvmx_ciu_qlm_jtgc_s cn56xxp1;
	struct cvmx_ciu_qlm_jtgc_cn63xx {
		uint64_t reserved_11_63:53;
		uint64_t clk_div:3;
		uint64_t reserved_6_7:2;
		uint64_t mux_sel:2;
		uint64_t reserved_3_3:1;
		uint64_t bypass:3;
	} cn63xx;
	struct cvmx_ciu_qlm_jtgc_cn63xx cn63xxp1;
};

union cvmx_ciu_qlm_jtgd {
	uint64_t u64;
	struct cvmx_ciu_qlm_jtgd_s {
		uint64_t capture:1;
		uint64_t shift:1;
		uint64_t update:1;
		uint64_t reserved_44_60:17;
		uint64_t select:4;
		uint64_t reserved_37_39:3;
		uint64_t shft_cnt:5;
		uint64_t shft_reg:32;
	} s;
	struct cvmx_ciu_qlm_jtgd_cn52xx {
		uint64_t capture:1;
		uint64_t shift:1;
		uint64_t update:1;
		uint64_t reserved_42_60:19;
		uint64_t select:2;
		uint64_t reserved_37_39:3;
		uint64_t shft_cnt:5;
		uint64_t shft_reg:32;
	} cn52xx;
	struct cvmx_ciu_qlm_jtgd_cn52xx cn52xxp1;
	struct cvmx_ciu_qlm_jtgd_s cn56xx;
	struct cvmx_ciu_qlm_jtgd_cn56xxp1 {
		uint64_t capture:1;
		uint64_t shift:1;
		uint64_t update:1;
		uint64_t reserved_37_60:24;
		uint64_t shft_cnt:5;
		uint64_t shft_reg:32;
	} cn56xxp1;
	struct cvmx_ciu_qlm_jtgd_cn63xx {
		uint64_t capture:1;
		uint64_t shift:1;
		uint64_t update:1;
		uint64_t reserved_43_60:18;
		uint64_t select:3;
		uint64_t reserved_37_39:3;
		uint64_t shft_cnt:5;
		uint64_t shft_reg:32;
	} cn63xx;
	struct cvmx_ciu_qlm_jtgd_cn63xx cn63xxp1;
};

union cvmx_ciu_soft_bist {
	uint64_t u64;
	struct cvmx_ciu_soft_bist_s {
		uint64_t reserved_1_63:63;
		uint64_t soft_bist:1;
	} s;
	struct cvmx_ciu_soft_bist_s cn30xx;
	struct cvmx_ciu_soft_bist_s cn31xx;
	struct cvmx_ciu_soft_bist_s cn38xx;
	struct cvmx_ciu_soft_bist_s cn38xxp2;
	struct cvmx_ciu_soft_bist_s cn50xx;
	struct cvmx_ciu_soft_bist_s cn52xx;
	struct cvmx_ciu_soft_bist_s cn52xxp1;
	struct cvmx_ciu_soft_bist_s cn56xx;
	struct cvmx_ciu_soft_bist_s cn56xxp1;
	struct cvmx_ciu_soft_bist_s cn58xx;
	struct cvmx_ciu_soft_bist_s cn58xxp1;
	struct cvmx_ciu_soft_bist_s cn63xx;
	struct cvmx_ciu_soft_bist_s cn63xxp1;
};

union cvmx_ciu_soft_prst {
	uint64_t u64;
	struct cvmx_ciu_soft_prst_s {
		uint64_t reserved_3_63:61;
		uint64_t host64:1;
		uint64_t npi:1;
		uint64_t soft_prst:1;
	} s;
	struct cvmx_ciu_soft_prst_s cn30xx;
	struct cvmx_ciu_soft_prst_s cn31xx;
	struct cvmx_ciu_soft_prst_s cn38xx;
	struct cvmx_ciu_soft_prst_s cn38xxp2;
	struct cvmx_ciu_soft_prst_s cn50xx;
	struct cvmx_ciu_soft_prst_cn52xx {
		uint64_t reserved_1_63:63;
		uint64_t soft_prst:1;
	} cn52xx;
	struct cvmx_ciu_soft_prst_cn52xx cn52xxp1;
	struct cvmx_ciu_soft_prst_cn52xx cn56xx;
	struct cvmx_ciu_soft_prst_cn52xx cn56xxp1;
	struct cvmx_ciu_soft_prst_s cn58xx;
	struct cvmx_ciu_soft_prst_s cn58xxp1;
	struct cvmx_ciu_soft_prst_cn52xx cn63xx;
	struct cvmx_ciu_soft_prst_cn52xx cn63xxp1;
};

union cvmx_ciu_soft_prst1 {
	uint64_t u64;
	struct cvmx_ciu_soft_prst1_s {
		uint64_t reserved_1_63:63;
		uint64_t soft_prst:1;
	} s;
	struct cvmx_ciu_soft_prst1_s cn52xx;
	struct cvmx_ciu_soft_prst1_s cn52xxp1;
	struct cvmx_ciu_soft_prst1_s cn56xx;
	struct cvmx_ciu_soft_prst1_s cn56xxp1;
	struct cvmx_ciu_soft_prst1_s cn63xx;
	struct cvmx_ciu_soft_prst1_s cn63xxp1;
};

union cvmx_ciu_soft_rst {
	uint64_t u64;
	struct cvmx_ciu_soft_rst_s {
		uint64_t reserved_1_63:63;
		uint64_t soft_rst:1;
	} s;
	struct cvmx_ciu_soft_rst_s cn30xx;
	struct cvmx_ciu_soft_rst_s cn31xx;
	struct cvmx_ciu_soft_rst_s cn38xx;
	struct cvmx_ciu_soft_rst_s cn38xxp2;
	struct cvmx_ciu_soft_rst_s cn50xx;
	struct cvmx_ciu_soft_rst_s cn52xx;
	struct cvmx_ciu_soft_rst_s cn52xxp1;
	struct cvmx_ciu_soft_rst_s cn56xx;
	struct cvmx_ciu_soft_rst_s cn56xxp1;
	struct cvmx_ciu_soft_rst_s cn58xx;
	struct cvmx_ciu_soft_rst_s cn58xxp1;
	struct cvmx_ciu_soft_rst_s cn63xx;
	struct cvmx_ciu_soft_rst_s cn63xxp1;
};

union cvmx_ciu_timx {
	uint64_t u64;
	struct cvmx_ciu_timx_s {
		uint64_t reserved_37_63:27;
		uint64_t one_shot:1;
		uint64_t len:36;
	} s;
	struct cvmx_ciu_timx_s cn30xx;
	struct cvmx_ciu_timx_s cn31xx;
	struct cvmx_ciu_timx_s cn38xx;
	struct cvmx_ciu_timx_s cn38xxp2;
	struct cvmx_ciu_timx_s cn50xx;
	struct cvmx_ciu_timx_s cn52xx;
	struct cvmx_ciu_timx_s cn52xxp1;
	struct cvmx_ciu_timx_s cn56xx;
	struct cvmx_ciu_timx_s cn56xxp1;
	struct cvmx_ciu_timx_s cn58xx;
	struct cvmx_ciu_timx_s cn58xxp1;
	struct cvmx_ciu_timx_s cn63xx;
	struct cvmx_ciu_timx_s cn63xxp1;
};

union cvmx_ciu_wdogx {
	uint64_t u64;
	struct cvmx_ciu_wdogx_s {
		uint64_t reserved_46_63:18;
		uint64_t gstopen:1;
		uint64_t dstop:1;
		uint64_t cnt:24;
		uint64_t len:16;
		uint64_t state:2;
		uint64_t mode:2;
	} s;
	struct cvmx_ciu_wdogx_s cn30xx;
	struct cvmx_ciu_wdogx_s cn31xx;
	struct cvmx_ciu_wdogx_s cn38xx;
	struct cvmx_ciu_wdogx_s cn38xxp2;
	struct cvmx_ciu_wdogx_s cn50xx;
	struct cvmx_ciu_wdogx_s cn52xx;
	struct cvmx_ciu_wdogx_s cn52xxp1;
	struct cvmx_ciu_wdogx_s cn56xx;
	struct cvmx_ciu_wdogx_s cn56xxp1;
	struct cvmx_ciu_wdogx_s cn58xx;
	struct cvmx_ciu_wdogx_s cn58xxp1;
	struct cvmx_ciu_wdogx_s cn63xx;
	struct cvmx_ciu_wdogx_s cn63xxp1;
};

#endif
