/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MICROCODE_INTEL_H
#define _ASM_X86_MICROCODE_INTEL_H

#include <asm/microcode.h>

struct microcode_header_intel {
	unsigned int            hdrver;
	unsigned int            rev;
	unsigned int            date;
	unsigned int            sig;
	unsigned int            cksum;
	unsigned int            ldrver;
	unsigned int            pf;
	unsigned int            datasize;
	unsigned int            totalsize;
	unsigned int            metasize;
	unsigned int            reserved[2];
};

struct microcode_intel {
	struct microcode_header_intel hdr;
	unsigned int            bits[];
};

#define MC_HEADER_SIZE			(sizeof(struct microcode_header_intel))
#define MC_HEADER_TYPE_MICROCODE	1
#define MC_HEADER_TYPE_IFS		2
#define DEFAULT_UCODE_DATASIZE		(2000)

#define get_datasize(mc) \
	(((struct microcode_intel *)mc)->hdr.datasize ? \
	 ((struct microcode_intel *)mc)->hdr.datasize : DEFAULT_UCODE_DATASIZE)

static inline u32 intel_get_microcode_revision(void)
{
	u32 rev, dummy;

	native_wrmsrl(MSR_IA32_UCODE_REV, 0);

	/* As documented in the SDM: Do a CPUID 1 here */
	native_cpuid_eax(1);

	/* get the current revision from MSR 0x8B */
	native_rdmsr(MSR_IA32_UCODE_REV, dummy, rev);

	return rev;
}

#ifdef CONFIG_CPU_SUP_INTEL
extern void __init load_ucode_intel_bsp(void);
extern void load_ucode_intel_ap(void);
extern void show_ucode_info_early(void);
extern int __init save_microcode_in_initrd_intel(void);
void reload_ucode_intel(void);
#else
static inline __init void load_ucode_intel_bsp(void) {}
static inline void load_ucode_intel_ap(void) {}
static inline void show_ucode_info_early(void) {}
static inline int __init save_microcode_in_initrd_intel(void) { return -EINVAL; }
static inline void reload_ucode_intel(void) {}
#endif

#endif /* _ASM_X86_MICROCODE_INTEL_H */
