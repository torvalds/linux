/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SETUP_DATA_H
#define _ASM_X86_SETUP_DATA_H

#include <uapi/asm/setup_data.h>

#ifndef __ASSEMBLER__

struct pci_setup_rom {
	struct setup_data data;
	uint16_t vendor;
	uint16_t devid;
	uint64_t pcilen;
	unsigned long segment;
	unsigned long bus;
	unsigned long device;
	unsigned long function;
	uint8_t romdata[];
};

/* kexec external ABI */
struct efi_setup_data {
	u64 fw_vendor;
	u64 __unused;
	u64 tables;
	u64 smbios;
	u64 reserved[8];
};

#endif /* __ASSEMBLER__ */

#endif /* _ASM_X86_SETUP_DATA_H */
