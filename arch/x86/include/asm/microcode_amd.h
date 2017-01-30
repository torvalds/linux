#ifndef _ASM_X86_MICROCODE_AMD_H
#define _ASM_X86_MICROCODE_AMD_H

#include <asm/microcode.h>

#define UCODE_MAGIC			0x00414d44
#define UCODE_EQUIV_CPU_TABLE_TYPE	0x00000000
#define UCODE_UCODE_TYPE		0x00000001

#define SECTION_HDR_SIZE		8
#define CONTAINER_HDR_SZ		12

struct equiv_cpu_entry {
	u32	installed_cpu;
	u32	fixed_errata_mask;
	u32	fixed_errata_compare;
	u16	equiv_cpu;
	u16	res;
} __attribute__((packed));

struct microcode_header_amd {
	u32	data_code;
	u32	patch_id;
	u16	mc_patch_data_id;
	u8	mc_patch_data_len;
	u8	init_flag;
	u32	mc_patch_data_checksum;
	u32	nb_dev_id;
	u32	sb_dev_id;
	u16	processor_rev_id;
	u8	nb_rev_id;
	u8	sb_rev_id;
	u8	bios_api_rev;
	u8	reserved1[3];
	u32	match_reg[8];
} __attribute__((packed));

struct microcode_amd {
	struct microcode_header_amd	hdr;
	unsigned int			mpb[0];
};

#define PATCH_MAX_SIZE PAGE_SIZE

#ifdef CONFIG_MICROCODE_AMD
extern void __init load_ucode_amd_bsp(unsigned int family);
extern void load_ucode_amd_ap(unsigned int family);
extern int __init save_microcode_in_initrd_amd(unsigned int family);
void reload_ucode_amd(void);
#else
static inline void __init load_ucode_amd_bsp(unsigned int family) {}
static inline void load_ucode_amd_ap(unsigned int family) {}
static inline int __init
save_microcode_in_initrd_amd(unsigned int family) { return -EINVAL; }
void reload_ucode_amd(void) {}
#endif

extern bool check_current_patch_level(u32 *rev, bool early);
#endif /* _ASM_X86_MICROCODE_AMD_H */
