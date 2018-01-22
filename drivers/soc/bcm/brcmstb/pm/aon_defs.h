/*
 * Always ON (AON) register interface between bootloader and Linux
 *
 * Copyright © 2014-2017 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __BRCMSTB_AON_DEFS_H__
#define __BRCMSTB_AON_DEFS_H__

#include <linux/compiler.h>

/* Magic number in upper 16-bits */
#define BRCMSTB_S3_MAGIC_MASK                   0xffff0000
#define BRCMSTB_S3_MAGIC_SHORT                  0x5AFE0000

enum {
	/* Restore random key for AES memory verification (off = fixed key) */
	S3_FLAG_LOAD_RANDKEY		= (1 << 0),

	/* Scratch buffer page table is present */
	S3_FLAG_SCRATCH_BUFFER_TABLE	= (1 << 1),

	/* Skip all memory verification */
	S3_FLAG_NO_MEM_VERIFY		= (1 << 2),

	/*
	 * Modification of this bit reserved for bootloader only.
	 * 1=PSCI started Linux, 0=Direct jump to Linux.
	 */
	S3_FLAG_PSCI_BOOT		= (1 << 3),

	/*
	 * Modification of this bit reserved for bootloader only.
	 * 1=64 bit boot, 0=32 bit boot.
	 */
	S3_FLAG_BOOTED64		= (1 << 4),
};

#define BRCMSTB_HASH_LEN			(128 / 8) /* 128-bit hash */

#define AON_REG_MAGIC_FLAGS			0x00
#define AON_REG_CONTROL_LOW			0x04
#define AON_REG_CONTROL_HIGH			0x08
#define AON_REG_S3_HASH				0x0c /* hash of S3 params */
#define AON_REG_CONTROL_HASH_LEN		0x1c
#define AON_REG_PANIC				0x20

#define BRCMSTB_S3_MAGIC		0x5AFEB007
#define BRCMSTB_PANIC_MAGIC		0x512E115E
#define BOOTLOADER_SCRATCH_SIZE		64
#define BRCMSTB_DTU_STATE_MAP_ENTRIES	(8*1024)
#define BRCMSTB_DTU_CONFIG_ENTRIES	(512)
#define BRCMSTB_DTU_COUNT		(2)

#define IMAGE_DESCRIPTORS_BUFSIZE	(2 * 1024)
#define S3_BOOTLOADER_RESERVED		(S3_FLAG_PSCI_BOOT | S3_FLAG_BOOTED64)

struct brcmstb_bootloader_dtu_table {
	uint32_t	dtu_state_map[BRCMSTB_DTU_STATE_MAP_ENTRIES];
	uint32_t	dtu_config[BRCMSTB_DTU_CONFIG_ENTRIES];
};

/*
 * Bootloader utilizes a custom parameter block left in DRAM for handling S3
 * warm resume
 */
struct brcmstb_s3_params {
	/* scratch memory for bootloader */
	uint8_t scratch[BOOTLOADER_SCRATCH_SIZE];

	uint32_t magic; /* BRCMSTB_S3_MAGIC */
	uint64_t reentry; /* PA */

	/* descriptors */
	uint32_t hash[BRCMSTB_HASH_LEN / 4];

	/*
	 * If 0, then ignore this parameter (there is only one set of
	 *   descriptors)
	 *
	 * If non-0, then a second set of descriptors is stored at:
	 *
	 *   descriptors + desc_offset_2
	 *
	 * The MAC result of both descriptors is XOR'd and stored in @hash
	 */
	uint32_t desc_offset_2;

	/*
	 * (Physical) address of a brcmstb_bootloader_scratch_table, for
	 * providing a large DRAM buffer to the bootloader
	 */
	uint64_t buffer_table;

	uint32_t spare[70];

	uint8_t descriptors[IMAGE_DESCRIPTORS_BUFSIZE];
	/*
	 * Must be last member of struct. See brcmstb_pm_s3_finish() for reason.
	 */
	struct brcmstb_bootloader_dtu_table dtu[BRCMSTB_DTU_COUNT];
} __packed;

#endif /* __BRCMSTB_AON_DEFS_H__ */
