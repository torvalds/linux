/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef __RAS_UMC_V12_0_H__
#define __RAS_UMC_V12_0_H__
#include "ras.h"

/* MCA_UMC_UMC0_MCUMC_ADDRT0 */
#define MCA_UMC_UMC0_MCUMC_ADDRT0__ErrorAddr__SHIFT                0x0
#define MCA_UMC_UMC0_MCUMC_ADDRT0__Reserved__SHIFT                 0x38
#define MCA_UMC_UMC0_MCUMC_ADDRT0__ErrorAddr_MASK                  0x00FFFFFFFFFFFFFFL
#define MCA_UMC_UMC0_MCUMC_ADDRT0__Reserved_MASK                   0xFF00000000000000L

/* MCMP1_IPIDT0 */
#define MCMP1_IPIDT0__InstanceIdLo__SHIFT                          0x0
#define MCMP1_IPIDT0__HardwareID__SHIFT                            0x20
#define MCMP1_IPIDT0__InstanceIdHi__SHIFT                          0x2c
#define MCMP1_IPIDT0__McaType__SHIFT                               0x30

#define MCMP1_IPIDT0__InstanceIdLo_MASK                            0x00000000FFFFFFFFL
#define MCMP1_IPIDT0__HardwareID_MASK                              0x00000FFF00000000L
#define MCMP1_IPIDT0__InstanceIdHi_MASK                            0x0000F00000000000L
#define MCMP1_IPIDT0__McaType_MASK                                 0xFFFF000000000000L

/* number of umc channel instance with memory map register access */
#define UMC_V12_0_CHANNEL_INSTANCE_NUM		8
/* number of umc instance with memory map register access */
#define UMC_V12_0_UMC_INSTANCE_NUM		4

/* one piece of normalized address is mapped to 8 pieces of physical address */
#define UMC_V12_0_NA_MAP_PA_NUM        8

/* bank bits in MCA error address */
#define UMC_V12_0_MCA_B0_BIT 6
#define UMC_V12_0_MCA_B1_BIT 7
#define UMC_V12_0_MCA_B2_BIT 8
#define UMC_V12_0_MCA_B3_BIT 9

/* row bits in MCA address */
#define UMC_V12_0_MCA_R0_BIT 10

/* Stack ID bits in SOC physical address */
#define UMC_V12_0_PA_SID1_BIT 37
#define UMC_V12_0_PA_SID0_BIT 36

/* bank bits in SOC physical address */
#define UMC_V12_0_PA_B3_BIT 18
#define UMC_V12_0_PA_B2_BIT 17
#define UMC_V12_0_PA_B1_BIT 20
#define UMC_V12_0_PA_B0_BIT 19

/* row bits in SOC physical address */
#define UMC_V12_0_PA_R13_BIT 35
#define UMC_V12_0_PA_R12_BIT 34
#define UMC_V12_0_PA_R11_BIT 33
#define UMC_V12_0_PA_R10_BIT 32
#define UMC_V12_0_PA_R9_BIT 31
#define UMC_V12_0_PA_R8_BIT 30
#define UMC_V12_0_PA_R7_BIT 29
#define UMC_V12_0_PA_R6_BIT 28
#define UMC_V12_0_PA_R5_BIT 27
#define UMC_V12_0_PA_R4_BIT 26
#define UMC_V12_0_PA_R3_BIT 25
#define UMC_V12_0_PA_R2_BIT 24
#define UMC_V12_0_PA_R1_BIT 23
#define UMC_V12_0_PA_R0_BIT 22

/* column bits in SOC physical address */
#define UMC_V12_0_PA_C4_BIT 21
#define UMC_V12_0_PA_C3_BIT 16
#define UMC_V12_0_PA_C2_BIT 15
#define UMC_V12_0_PA_C1_BIT 6
#define UMC_V12_0_PA_C0_BIT 5

/* channel index bits in SOC physical address */
#define UMC_V12_0_PA_CH6_BIT 14
#define UMC_V12_0_PA_CH5_BIT 13
#define UMC_V12_0_PA_CH4_BIT 12
#define UMC_V12_0_PA_CH3_BIT 11
#define UMC_V12_0_PA_CH2_BIT 10
#define UMC_V12_0_PA_CH1_BIT 9
#define UMC_V12_0_PA_CH0_BIT 8

/* Pseudochannel index bits in SOC physical address */
#define UMC_V12_0_PA_PC0_BIT 7

#define UMC_V12_0_NA_C2_BIT 8

#define UMC_V12_0_SOC_PA_TO_SID(pa) \
	((((pa >> UMC_V12_0_PA_SID0_BIT) & 0x1ULL) << 0ULL) | \
	 (((pa >> UMC_V12_0_PA_SID1_BIT) & 0x1ULL) << 1ULL))

#define UMC_V12_0_SOC_PA_TO_BANK(pa) \
	((((pa >> UMC_V12_0_PA_B0_BIT) & 0x1ULL) << 0ULL) | \
	 (((pa >> UMC_V12_0_PA_B1_BIT) & 0x1ULL) << 1ULL) | \
	 (((pa >> UMC_V12_0_PA_B2_BIT) & 0x1ULL) << 2ULL) | \
	 (((pa >> UMC_V12_0_PA_B3_BIT) & 0x1ULL) << 3ULL))

#define UMC_V12_0_SOC_PA_TO_ROW(pa) \
	((((pa >> UMC_V12_0_PA_R0_BIT) & 0x1ULL) << 0ULL) | \
	 (((pa >> UMC_V12_0_PA_R1_BIT) & 0x1ULL) << 1ULL) | \
	 (((pa >> UMC_V12_0_PA_R2_BIT) & 0x1ULL) << 2ULL) | \
	 (((pa >> UMC_V12_0_PA_R3_BIT) & 0x1ULL) << 3ULL) | \
	 (((pa >> UMC_V12_0_PA_R4_BIT) & 0x1ULL) << 4ULL) | \
	 (((pa >> UMC_V12_0_PA_R5_BIT) & 0x1ULL) << 5ULL) | \
	 (((pa >> UMC_V12_0_PA_R6_BIT) & 0x1ULL) << 6ULL) | \
	 (((pa >> UMC_V12_0_PA_R7_BIT) & 0x1ULL) << 7ULL) | \
	 (((pa >> UMC_V12_0_PA_R8_BIT) & 0x1ULL) << 8ULL) | \
	 (((pa >> UMC_V12_0_PA_R9_BIT) & 0x1ULL) << 9ULL) | \
	 (((pa >> UMC_V12_0_PA_R10_BIT) & 0x1ULL) << 10ULL) | \
	 (((pa >> UMC_V12_0_PA_R11_BIT) & 0x1ULL) << 11ULL) | \
	 (((pa >> UMC_V12_0_PA_R12_BIT) & 0x1ULL) << 12ULL) | \
	 (((pa >> UMC_V12_0_PA_R13_BIT) & 0x1ULL) << 13ULL))

#define UMC_V12_0_SOC_PA_TO_COL(pa) \
	((((pa >> UMC_V12_0_PA_C0_BIT) & 0x1ULL) << 0ULL) | \
	 (((pa >> UMC_V12_0_PA_C1_BIT) & 0x1ULL) << 1ULL) | \
	 (((pa >> UMC_V12_0_PA_C2_BIT) & 0x1ULL) << 2ULL) | \
	 (((pa >> UMC_V12_0_PA_C3_BIT) & 0x1ULL) << 3ULL) | \
	 (((pa >> UMC_V12_0_PA_C4_BIT) & 0x1ULL) << 4ULL))

#define UMC_V12_0_SOC_PA_TO_CH(pa) \
	((((pa >> UMC_V12_0_PA_CH0_BIT) & 0x1ULL) << 0ULL) | \
	 (((pa >> UMC_V12_0_PA_CH1_BIT) & 0x1ULL) << 1ULL) | \
	 (((pa >> UMC_V12_0_PA_CH2_BIT) & 0x1ULL) << 2ULL) | \
	 (((pa >> UMC_V12_0_PA_CH3_BIT) & 0x1ULL) << 3ULL) | \
	 (((pa >> UMC_V12_0_PA_CH4_BIT) & 0x1ULL) << 4ULL) | \
	 (((pa >> UMC_V12_0_PA_CH5_BIT) & 0x1ULL) << 5ULL) | \
	 (((pa >> UMC_V12_0_PA_CH6_BIT) & 0x1ULL) << 6ULL))

#define UMC_V12_0_SOC_PA_TO_PC(pa) (((pa >> UMC_V12_0_PA_PC0_BIT) & 0x1ULL) << 0ULL)

#define UMC_V12_0_SOC_SID_TO_PA(sid) \
	((((sid >> 0ULL) & 0x1ULL) << UMC_V12_0_PA_SID0_BIT) | \
	 (((sid >> 1ULL) & 0x1ULL) << UMC_V12_0_PA_SID1_BIT))

#define UMC_V12_0_SOC_BANK_TO_PA(bank) \
	((((bank >> 0ULL) & 0x1ULL) << UMC_V12_0_PA_B0_BIT) | \
	 (((bank >> 1ULL) & 0x1ULL) << UMC_V12_0_PA_B1_BIT) | \
	 (((bank >> 2ULL) & 0x1ULL) << UMC_V12_0_PA_B2_BIT) | \
	 (((bank >> 3ULL) & 0x1ULL) << UMC_V12_0_PA_B3_BIT))

#define UMC_V12_0_SOC_ROW_TO_PA(row) \
	((((row >> 0ULL) & 0x1ULL) << UMC_V12_0_PA_R0_BIT) | \
	 (((row >> 1ULL) & 0x1ULL) << UMC_V12_0_PA_R1_BIT) | \
	 (((row >> 2ULL) & 0x1ULL) << UMC_V12_0_PA_R2_BIT) | \
	 (((row >> 3ULL) & 0x1ULL) << UMC_V12_0_PA_R3_BIT) | \
	 (((row >> 4ULL) & 0x1ULL) << UMC_V12_0_PA_R4_BIT) | \
	 (((row >> 5ULL) & 0x1ULL) << UMC_V12_0_PA_R5_BIT) | \
	 (((row >> 6ULL) & 0x1ULL) << UMC_V12_0_PA_R6_BIT) | \
	 (((row >> 7ULL) & 0x1ULL) << UMC_V12_0_PA_R7_BIT) | \
	 (((row >> 8ULL) & 0x1ULL) << UMC_V12_0_PA_R8_BIT) | \
	 (((row >> 9ULL) & 0x1ULL) << UMC_V12_0_PA_R9_BIT) | \
	 (((row >> 10ULL) & 0x1ULL) << UMC_V12_0_PA_R10_BIT) | \
	 (((row >> 11ULL) & 0x1ULL) << UMC_V12_0_PA_R11_BIT) | \
	 (((row >> 12ULL) & 0x1ULL) << UMC_V12_0_PA_R12_BIT) | \
	 (((row >> 13ULL) & 0x1ULL) << UMC_V12_0_PA_R13_BIT))

#define UMC_V12_0_SOC_COL_TO_PA(col) \
	((((col >> 0ULL) & 0x1ULL) << UMC_V12_0_PA_C0_BIT) | \
	 (((col >> 1ULL) & 0x1ULL) << UMC_V12_0_PA_C1_BIT) | \
	 (((col >> 2ULL) & 0x1ULL) << UMC_V12_0_PA_C2_BIT) | \
	 (((col >> 3ULL) & 0x1ULL) << UMC_V12_0_PA_C3_BIT) | \
	 (((col >> 4ULL) & 0x1ULL) << UMC_V12_0_PA_C4_BIT))

#define UMC_V12_0_SOC_CH_TO_PA(ch) \
	((((ch >> 0ULL) & 0x1ULL) << UMC_V12_0_PA_CH0_BIT) | \
	 (((ch >> 1ULL) & 0x1ULL) << UMC_V12_0_PA_CH1_BIT) | \
	 (((ch >> 2ULL) & 0x1ULL) << UMC_V12_0_PA_CH2_BIT) | \
	 (((ch >> 3ULL) & 0x1ULL) << UMC_V12_0_PA_CH3_BIT) | \
	 (((ch >> 4ULL) & 0x1ULL) << UMC_V12_0_PA_CH4_BIT) | \
	 (((ch >> 5ULL) & 0x1ULL) << UMC_V12_0_PA_CH5_BIT) | \
	 (((ch >> 6ULL) & 0x1ULL) << UMC_V12_0_PA_CH6_BIT))

#define UMC_V12_0_SOC_PC_TO_PA(pc) (((pc >> 0ULL) & 0x1ULL) << UMC_V12_0_PA_PC0_BIT)

/* bank hash settings */
#define UMC_V12_0_XOR_EN0 1
#define UMC_V12_0_XOR_EN1 1
#define UMC_V12_0_XOR_EN2 1
#define UMC_V12_0_XOR_EN3 1
#define UMC_V12_0_COL_XOR0 0x0
#define UMC_V12_0_COL_XOR1 0x0
#define UMC_V12_0_COL_XOR2 0x800
#define UMC_V12_0_COL_XOR3 0x1000
#define UMC_V12_0_ROW_XOR0 0x11111
#define UMC_V12_0_ROW_XOR1 0x22222
#define UMC_V12_0_ROW_XOR2 0x4444
#define UMC_V12_0_ROW_XOR3 0x8888

/* channel hash settings */
#define UMC_V12_0_HASH_4K 0
#define UMC_V12_0_HASH_64K 1
#define UMC_V12_0_HASH_2M 1
#define UMC_V12_0_HASH_1G 1
#define UMC_V12_0_HASH_1T 1

/* XOR some bits of PA into CH4~CH6 bits (bits 12~14 of PA),
 * hash bit is only effective when related setting is enabled
 */
#define UMC_V12_0_CHANNEL_HASH_CH4(channel_idx, pa) ((((channel_idx) >> 5) & 0x1) ^ \
				(((pa)  >> 20) & 0x1ULL & UMC_V12_0_HASH_64K) ^ \
				(((pa)  >> 27) & 0x1ULL & UMC_V12_0_HASH_2M) ^ \
				(((pa)  >> 34) & 0x1ULL & UMC_V12_0_HASH_1G) ^ \
				(((pa)  >> 41) & 0x1ULL & UMC_V12_0_HASH_1T))
#define UMC_V12_0_CHANNEL_HASH_CH5(channel_idx, pa) ((((channel_idx) >> 6) & 0x1) ^ \
				(((pa)  >> 21) & 0x1ULL & UMC_V12_0_HASH_64K) ^ \
				(((pa)  >> 28) & 0x1ULL & UMC_V12_0_HASH_2M) ^ \
				(((pa)  >> 35) & 0x1ULL & UMC_V12_0_HASH_1G) ^ \
				(((pa)  >> 42) & 0x1ULL & UMC_V12_0_HASH_1T))
#define UMC_V12_0_CHANNEL_HASH_CH6(channel_idx, pa) ((((channel_idx) >> 4) & 0x1) ^ \
				(((pa)  >> 19) & 0x1ULL & UMC_V12_0_HASH_64K) ^ \
				(((pa)  >> 26) & 0x1ULL & UMC_V12_0_HASH_2M) ^ \
				(((pa)  >> 33) & 0x1ULL & UMC_V12_0_HASH_1G) ^ \
				(((pa)  >> 40) & 0x1ULL & UMC_V12_0_HASH_1T) ^ \
				(((pa)  >> 47) & 0x1ULL & UMC_V12_0_HASH_1T))
#define UMC_V12_0_SET_CHANNEL_HASH(channel_idx, pa) do { \
		(pa) &= ~(0x7ULL << UMC_V12_0_PA_CH4_BIT); \
		(pa) |= (UMC_V12_0_CHANNEL_HASH_CH4(channel_idx, pa) << UMC_V12_0_PA_CH4_BIT); \
		(pa) |= (UMC_V12_0_CHANNEL_HASH_CH5(channel_idx, pa) << UMC_V12_0_PA_CH5_BIT); \
		(pa) |= (UMC_V12_0_CHANNEL_HASH_CH6(channel_idx, pa) << UMC_V12_0_PA_CH6_BIT); \
	} while (0)


/*
 * (addr / 256) * 4096, the higher 26 bits in ErrorAddr
 * is the index of 4KB block
 */
#define ADDR_OF_4KB_BLOCK(addr)			(((addr) & ~0xffULL) << 4)
/*
 * (addr / 256) * 8192, the higher 26 bits in ErrorAddr
 * is the index of 8KB block
 */
#define ADDR_OF_8KB_BLOCK(addr)			(((addr) & ~0xffULL) << 5)
/*
 * (addr / 256) * 32768, the higher 26 bits in ErrorAddr
 * is the index of 8KB block
 */
#define ADDR_OF_32KB_BLOCK(addr)			(((addr) & ~0xffULL) << 7)
/* channel index is the index of 256B block */
#define ADDR_OF_256B_BLOCK(channel_index)	((channel_index) << 8)
/* offset in 256B block */
#define OFFSET_IN_256B_BLOCK(addr)		((addr) & 0xffULL)


#define UMC_V12_ADDR_MASK_BAD_COLS(addr) \
	((addr) & ~((0x3ULL << UMC_V12_0_PA_C2_BIT) | \
			(0x1ULL << UMC_V12_0_PA_C4_BIT) | \
			(0x1ULL << UMC_V12_0_PA_R13_BIT)))

#define ACA_IPID_HI_2_UMC_AID(_ipid_hi) (((_ipid_hi) >> 2) & 0x3)
#define ACA_IPID_LO_2_UMC_CH(_ipid_lo)  \
	(((((_ipid_lo) >> 20) & 0x1) * 4) + (((_ipid_lo) >> 12) & 0xF))
#define ACA_IPID_LO_2_UMC_INST(_ipid_lo) (((_ipid_lo) >> 21) & 0x7)

#define ACA_IPID_2_DIE_ID(ipid)  ((REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdHi) >> 2) & 0x03)
#define ACA_IPID_2_UMC_CH(ipid) \
	(ACA_IPID_LO_2_UMC_CH(REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdLo)))

#define ACA_IPID_2_UMC_INST(ipid) \
	(ACA_IPID_LO_2_UMC_INST(REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdLo)))

#define ACA_IPID_2_SOCKET_ID(ipid) \
	(((REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdLo) & 0x1) << 2) | \
	 (REG_GET_FIELD(ipid, MCMP1_IPIDT0, InstanceIdHi) & 0x03))

#define ACA_ADDR_2_ERR_ADDR(addr) \
	REG_GET_FIELD(addr, MCA_UMC_UMC0_MCUMC_ADDRT0, ErrorAddr)

/* R13 bit shift should be considered, double the number */
#define UMC_V12_0_BAD_PAGE_NUM_PER_CHANNEL (UMC_V12_0_NA_MAP_PA_NUM * 2)


/* C2, C3, C4, R13, four MCA bits are looped in page retirement */
#define UMC_V12_0_RETIRE_LOOP_BITS 4

/* invalid node instance value */
#define UMC_INV_AID_NODE 0xffff

#define UMC_V12_0_AID_NUM_MAX     4
#define UMC_V12_0_SOCKET_NUM_MAX  8

#define UMC_V12_0_TOTAL_CHANNEL_NUM \
	(UMC_V12_0_AID_NUM_MAX * UMC_V12_0_UMC_INSTANCE_NUM * UMC_V12_0_CHANNEL_INSTANCE_NUM)

/* one device has 192GB HBM */
#define SOCKET_LFB_SIZE   0x3000000000ULL

extern const struct ras_umc_ip_func ras_umc_func_v12_0;

int ras_umc_get_badpage_count(struct ras_core_context *ras_core);
int ras_umc_get_badpage_record(struct ras_core_context *ras_core, uint32_t index, void *record);
#endif

