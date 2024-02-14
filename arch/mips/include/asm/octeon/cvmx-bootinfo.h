/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
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

/*
 * Header file containing the ABI with the bootloader.
 */

#ifndef __CVMX_BOOTINFO_H__
#define __CVMX_BOOTINFO_H__

#include "cvmx-coremask.h"

/*
 * Current major and minor versions of the CVMX bootinfo block that is
 * passed from the bootloader to the application.  This is versioned
 * so that applications can properly handle multiple bootloader
 * versions.
 */
#define CVMX_BOOTINFO_MAJ_VER 1
#define CVMX_BOOTINFO_MIN_VER 4

#if (CVMX_BOOTINFO_MAJ_VER == 1)
#define CVMX_BOOTINFO_OCTEON_SERIAL_LEN 20
/*
 * This structure is populated by the bootloader.  For binary
 * compatibility the only changes that should be made are
 * adding members to the end of the structure, and the minor
 * version should be incremented at that time.
 * If an incompatible change is made, the major version
 * must be incremented, and the minor version should be reset
 * to 0.
 */
struct cvmx_bootinfo {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t major_version;
	uint32_t minor_version;

	uint64_t stack_top;
	uint64_t heap_base;
	uint64_t heap_end;
	uint64_t desc_vaddr;

	uint32_t exception_base_addr;
	uint32_t stack_size;
	uint32_t flags;
	uint32_t core_mask;
	/* DRAM size in megabytes */
	uint32_t dram_size;
	/* physical address of free memory descriptor block*/
	uint32_t phy_mem_desc_addr;
	/* used to pass flags from app to debugger */
	uint32_t debugger_flags_base_addr;

	/* CPU clock speed, in hz */
	uint32_t eclock_hz;

	/* DRAM clock speed, in hz */
	uint32_t dclock_hz;

	uint32_t reserved0;
	uint16_t board_type;
	uint8_t board_rev_major;
	uint8_t board_rev_minor;
	uint16_t reserved1;
	uint8_t reserved2;
	uint8_t reserved3;
	char board_serial_number[CVMX_BOOTINFO_OCTEON_SERIAL_LEN];
	uint8_t mac_addr_base[6];
	uint8_t mac_addr_count;
#if (CVMX_BOOTINFO_MIN_VER >= 1)
	/*
	 * Several boards support compact flash on the Octeon boot
	 * bus.	 The CF memory spaces may be mapped to different
	 * addresses on different boards.  These are the physical
	 * addresses, so care must be taken to use the correct
	 * XKPHYS/KSEG0 addressing depending on the application's
	 * ABI.	 These values will be 0 if CF is not present.
	 */
	uint64_t compact_flash_common_base_addr;
	uint64_t compact_flash_attribute_base_addr;
	/*
	 * Base address of the LED display (as on EBT3000 board)
	 * This will be 0 if LED display not present.
	 */
	uint64_t led_display_base_addr;
#endif
#if (CVMX_BOOTINFO_MIN_VER >= 2)
	/* DFA reference clock in hz (if applicable)*/
	uint32_t dfa_ref_clock_hz;

	/*
	 * flags indicating various configuration options.  These
	 * flags supersede the 'flags' variable and should be used
	 * instead if available.
	 */
	uint32_t config_flags;
#endif
#if (CVMX_BOOTINFO_MIN_VER >= 3)
	/*
	 * Address of the OF Flattened Device Tree structure
	 * describing the board.
	 */
	uint64_t fdt_addr;
#endif
#if (CVMX_BOOTINFO_MIN_VER >= 4)
	/*
	 * Coremask used for processors with more than 32 cores
	 * or with OCI.  This replaces core_mask.
	 */
	struct cvmx_coremask ext_core_mask;
#endif
#else				/* __BIG_ENDIAN */
	/*
	 * Little-Endian: When the CPU mode is switched to
	 * little-endian, the view of the structure has some of the
	 * fields swapped.
	 */
	uint32_t minor_version;
	uint32_t major_version;

	uint64_t stack_top;
	uint64_t heap_base;
	uint64_t heap_end;
	uint64_t desc_vaddr;

	uint32_t stack_size;
	uint32_t exception_base_addr;

	uint32_t core_mask;
	uint32_t flags;

	uint32_t phy_mem_desc_addr;
	uint32_t dram_size;

	uint32_t eclock_hz;
	uint32_t debugger_flags_base_addr;

	uint32_t reserved0;
	uint32_t dclock_hz;

	uint8_t reserved3;
	uint8_t reserved2;
	uint16_t reserved1;
	uint8_t board_rev_minor;
	uint8_t board_rev_major;
	uint16_t board_type;

	char board_serial_number[CVMX_BOOTINFO_OCTEON_SERIAL_LEN];
	uint8_t mac_addr_base[6];
	uint8_t mac_addr_count;
	uint8_t pad[5];

#if (CVMX_BOOTINFO_MIN_VER >= 1)
	uint64_t compact_flash_common_base_addr;
	uint64_t compact_flash_attribute_base_addr;
	uint64_t led_display_base_addr;
#endif
#if (CVMX_BOOTINFO_MIN_VER >= 2)
	uint32_t config_flags;
	uint32_t dfa_ref_clock_hz;
#endif
#if (CVMX_BOOTINFO_MIN_VER >= 3)
	uint64_t fdt_addr;
#endif
#if (CVMX_BOOTINFO_MIN_VER >= 4)
	struct cvmx_coremask ext_core_mask;
#endif
#endif
};

#define CVMX_BOOTINFO_CFG_FLAG_PCI_HOST			(1ull << 0)
#define CVMX_BOOTINFO_CFG_FLAG_PCI_TARGET		(1ull << 1)
#define CVMX_BOOTINFO_CFG_FLAG_DEBUG			(1ull << 2)
#define CVMX_BOOTINFO_CFG_FLAG_NO_MAGIC			(1ull << 3)
/* This flag is set if the TLB mappings are not contained in the
 * 0x10000000 - 0x20000000 boot bus region. */
#define CVMX_BOOTINFO_CFG_FLAG_OVERSIZE_TLB_MAPPING	(1ull << 4)
#define CVMX_BOOTINFO_CFG_FLAG_BREAK			(1ull << 5)

#endif /*   (CVMX_BOOTINFO_MAJ_VER == 1) */

/* Type defines for board and chip types */
enum cvmx_board_types_enum {
	CVMX_BOARD_TYPE_NULL = 0,
	CVMX_BOARD_TYPE_SIM = 1,
	CVMX_BOARD_TYPE_EBT3000 = 2,
	CVMX_BOARD_TYPE_KODAMA = 3,
	CVMX_BOARD_TYPE_NIAGARA = 4,
	CVMX_BOARD_TYPE_NAC38 = 5,	/* formerly NAO38 */
	CVMX_BOARD_TYPE_THUNDER = 6,
	CVMX_BOARD_TYPE_TRANTOR = 7,
	CVMX_BOARD_TYPE_EBH3000 = 8,
	CVMX_BOARD_TYPE_EBH3100 = 9,
	CVMX_BOARD_TYPE_HIKARI = 10,
	CVMX_BOARD_TYPE_CN3010_EVB_HS5 = 11,
	CVMX_BOARD_TYPE_CN3005_EVB_HS5 = 12,
	CVMX_BOARD_TYPE_KBP = 13,
	/* Deprecated, CVMX_BOARD_TYPE_CN3010_EVB_HS5 supports the CN3020 */
	CVMX_BOARD_TYPE_CN3020_EVB_HS5 = 14,
	CVMX_BOARD_TYPE_EBT5800 = 15,
	CVMX_BOARD_TYPE_NICPRO2 = 16,
	CVMX_BOARD_TYPE_EBH5600 = 17,
	CVMX_BOARD_TYPE_EBH5601 = 18,
	CVMX_BOARD_TYPE_EBH5200 = 19,
	CVMX_BOARD_TYPE_BBGW_REF = 20,
	CVMX_BOARD_TYPE_NIC_XLE_4G = 21,
	CVMX_BOARD_TYPE_EBT5600 = 22,
	CVMX_BOARD_TYPE_EBH5201 = 23,
	CVMX_BOARD_TYPE_EBT5200 = 24,
	CVMX_BOARD_TYPE_CB5600	= 25,
	CVMX_BOARD_TYPE_CB5601	= 26,
	CVMX_BOARD_TYPE_CB5200	= 27,
	/* Special 'generic' board type, supports many boards */
	CVMX_BOARD_TYPE_GENERIC = 28,
	CVMX_BOARD_TYPE_EBH5610 = 29,
	CVMX_BOARD_TYPE_LANAI2_A = 30,
	CVMX_BOARD_TYPE_LANAI2_U = 31,
	CVMX_BOARD_TYPE_EBB5600 = 32,
	CVMX_BOARD_TYPE_EBB6300 = 33,
	CVMX_BOARD_TYPE_NIC_XLE_10G = 34,
	CVMX_BOARD_TYPE_LANAI2_G = 35,
	CVMX_BOARD_TYPE_EBT5810 = 36,
	CVMX_BOARD_TYPE_NIC10E = 37,
	CVMX_BOARD_TYPE_EP6300C = 38,
	CVMX_BOARD_TYPE_EBB6800 = 39,
	CVMX_BOARD_TYPE_NIC4E = 40,
	CVMX_BOARD_TYPE_NIC2E = 41,
	CVMX_BOARD_TYPE_EBB6600 = 42,
	CVMX_BOARD_TYPE_REDWING = 43,
	CVMX_BOARD_TYPE_NIC68_4 = 44,
	CVMX_BOARD_TYPE_NIC10E_66 = 45,
	CVMX_BOARD_TYPE_SNIC10E = 50,
	CVMX_BOARD_TYPE_MAX,

	/*
	 * The range from CVMX_BOARD_TYPE_MAX to
	 * CVMX_BOARD_TYPE_CUST_DEFINED_MIN is reserved for future
	 * SDK use.
	 */

	/*
	 * Set aside a range for customer boards.  These numbers are managed
	 * by Cavium.
	 */
	CVMX_BOARD_TYPE_CUST_DEFINED_MIN = 10000,
	CVMX_BOARD_TYPE_CUST_WSX16 = 10001,
	CVMX_BOARD_TYPE_CUST_NS0216 = 10002,
	CVMX_BOARD_TYPE_CUST_NB5 = 10003,
	CVMX_BOARD_TYPE_CUST_WMR500 = 10004,
	CVMX_BOARD_TYPE_CUST_ITB101 = 10005,
	CVMX_BOARD_TYPE_CUST_NTE102 = 10006,
	CVMX_BOARD_TYPE_CUST_AGS103 = 10007,
	CVMX_BOARD_TYPE_CUST_GST104 = 10008,
	CVMX_BOARD_TYPE_CUST_GCT105 = 10009,
	CVMX_BOARD_TYPE_CUST_AGS106 = 10010,
	CVMX_BOARD_TYPE_CUST_SGM107 = 10011,
	CVMX_BOARD_TYPE_CUST_GCT108 = 10012,
	CVMX_BOARD_TYPE_CUST_AGS109 = 10013,
	CVMX_BOARD_TYPE_CUST_GCT110 = 10014,
	CVMX_BOARD_TYPE_CUST_L2_AIR_SENDER = 10015,
	CVMX_BOARD_TYPE_CUST_L2_AIR_RECEIVER = 10016,
	CVMX_BOARD_TYPE_CUST_L2_ACCTON2_TX = 10017,
	CVMX_BOARD_TYPE_CUST_L2_ACCTON2_RX = 10018,
	CVMX_BOARD_TYPE_CUST_L2_WSTRNSNIC_TX = 10019,
	CVMX_BOARD_TYPE_CUST_L2_WSTRNSNIC_RX = 10020,
	CVMX_BOARD_TYPE_CUST_L2_ZINWELL = 10021,
	CVMX_BOARD_TYPE_CUST_DEFINED_MAX = 20000,

	/*
	 * Set aside a range for customer private use.	The SDK won't
	 * use any numbers in this range.
	 */
	CVMX_BOARD_TYPE_CUST_PRIVATE_MIN = 20001,
	CVMX_BOARD_TYPE_UBNT_E100 = 20002,
	CVMX_BOARD_TYPE_UBNT_E200 = 20003,
	CVMX_BOARD_TYPE_UBNT_E220 = 20005,
	CVMX_BOARD_TYPE_CUST_DSR1000N = 20006,
	CVMX_BOARD_TYPE_UBNT_E300 = 20300,
	CVMX_BOARD_TYPE_KONTRON_S1901 = 21901,
	CVMX_BOARD_TYPE_CUST_PRIVATE_MAX = 30000,

	/* The remaining range is reserved for future use. */
};

enum cvmx_chip_types_enum {
	CVMX_CHIP_TYPE_NULL = 0,
	CVMX_CHIP_SIM_TYPE_DEPRECATED = 1,
	CVMX_CHIP_TYPE_OCTEON_SAMPLE = 2,
	CVMX_CHIP_TYPE_MAX,
};

/* Compatibility alias for NAC38 name change, planned to be removed
 * from SDK 1.7 */
#define CVMX_BOARD_TYPE_NAO38	CVMX_BOARD_TYPE_NAC38

/* Functions to return string based on type */
#define ENUM_BRD_TYPE_CASE(x) \
	case x: return (&#x[16]);	/* Skip CVMX_BOARD_TYPE_ */
static inline const char *cvmx_board_type_to_string(enum
						    cvmx_board_types_enum type)
{
	switch (type) {
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NULL)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_SIM)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBT3000)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_KODAMA)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIAGARA)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NAC38)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_THUNDER)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_TRANTOR)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH3000)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH3100)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_HIKARI)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CN3010_EVB_HS5)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CN3005_EVB_HS5)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_KBP)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CN3020_EVB_HS5)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBT5800)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NICPRO2)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH5600)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH5601)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH5200)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_BBGW_REF)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC_XLE_4G)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBT5600)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH5201)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBT5200)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CB5600)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CB5601)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CB5200)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_GENERIC)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH5610)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_LANAI2_A)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_LANAI2_U)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBB5600)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBB6300)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC_XLE_10G)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_LANAI2_G)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBT5810)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC10E)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EP6300C)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBB6800)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC4E)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC2E)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBB6600)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_REDWING)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC68_4)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC10E_66)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_SNIC10E)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MAX)

			/* Customer boards listed here */
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_DEFINED_MIN)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_WSX16)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_NS0216)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_NB5)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_WMR500)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_ITB101)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_NTE102)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_AGS103)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_GST104)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_GCT105)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_AGS106)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_SGM107)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_GCT108)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_AGS109)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_GCT110)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_AIR_SENDER)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_AIR_RECEIVER)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_ACCTON2_TX)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_ACCTON2_RX)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_WSTRNSNIC_TX)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_WSTRNSNIC_RX)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_ZINWELL)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_DEFINED_MAX)

		    /* Customer private range */
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_PRIVATE_MIN)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_UBNT_E100)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_UBNT_E200)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_UBNT_E220)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_DSR1000N)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_UBNT_E300)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_KONTRON_S1901)
		ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_PRIVATE_MAX)
	}
	return NULL;
}

#define ENUM_CHIP_TYPE_CASE(x) \
	case x: return (&#x[15]);	/* Skip CVMX_CHIP_TYPE */
static inline const char *cvmx_chip_type_to_string(enum
						   cvmx_chip_types_enum type)
{
	switch (type) {
		ENUM_CHIP_TYPE_CASE(CVMX_CHIP_TYPE_NULL)
		ENUM_CHIP_TYPE_CASE(CVMX_CHIP_SIM_TYPE_DEPRECATED)
		ENUM_CHIP_TYPE_CASE(CVMX_CHIP_TYPE_OCTEON_SAMPLE)
		ENUM_CHIP_TYPE_CASE(CVMX_CHIP_TYPE_MAX)
	}
	return "Unsupported Chip";
}

#endif /* __CVMX_BOOTINFO_H__ */
