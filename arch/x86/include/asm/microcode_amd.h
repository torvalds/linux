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

static inline u16 find_equiv_id(struct equiv_cpu_entry *equiv_cpu_table,
				unsigned int sig)
{
	int i = 0;

	if (!equiv_cpu_table)
		return 0;

	while (equiv_cpu_table[i].installed_cpu != 0) {
		if (sig == equiv_cpu_table[i].installed_cpu)
			return equiv_cpu_table[i].equiv_cpu;

		i++;
	}
	return 0;
}

extern int __apply_microcode_amd(struct microcode_amd *mc_amd);
extern int apply_microcode_amd(int cpu);
extern enum ucode_state load_microcode_amd(int cpu, const u8 *data, size_t size);

#endif /* _ASM_X86_MICROCODE_AMD_H */
