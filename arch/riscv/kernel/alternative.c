// SPDX-License-Identifier: GPL-2.0-only
/*
 * alternative runtime patching
 * inspired by the ARM64 and x86 version
 *
 * Copyright (C) 2021 Sifive.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/uaccess.h>
#include <asm/alternative.h>
#include <asm/module.h>
#include <asm/sections.h>
#include <asm/vdso.h>
#include <asm/vendorid_list.h>
#include <asm/sbi.h>
#include <asm/csr.h>
#include <asm/insn.h>
#include <asm/text-patching.h>

struct cpu_manufacturer_info_t {
	unsigned long vendor_id;
	unsigned long arch_id;
	unsigned long imp_id;
	void (*patch_func)(struct alt_entry *begin, struct alt_entry *end,
				  unsigned long archid, unsigned long impid,
				  unsigned int stage);
};

static void riscv_fill_cpu_mfr_info(struct cpu_manufacturer_info_t *cpu_mfr_info)
{
#ifdef CONFIG_RISCV_M_MODE
	cpu_mfr_info->vendor_id = csr_read(CSR_MVENDORID);
	cpu_mfr_info->arch_id = csr_read(CSR_MARCHID);
	cpu_mfr_info->imp_id = csr_read(CSR_MIMPID);
#else
	cpu_mfr_info->vendor_id = sbi_get_mvendorid();
	cpu_mfr_info->arch_id = sbi_get_marchid();
	cpu_mfr_info->imp_id = sbi_get_mimpid();
#endif

	switch (cpu_mfr_info->vendor_id) {
#ifdef CONFIG_ERRATA_ANDES
	case ANDES_VENDOR_ID:
		cpu_mfr_info->patch_func = andes_errata_patch_func;
		break;
#endif
#ifdef CONFIG_ERRATA_SIFIVE
	case SIFIVE_VENDOR_ID:
		cpu_mfr_info->patch_func = sifive_errata_patch_func;
		break;
#endif
#ifdef CONFIG_ERRATA_THEAD
	case THEAD_VENDOR_ID:
		cpu_mfr_info->patch_func = thead_errata_patch_func;
		break;
#endif
	default:
		cpu_mfr_info->patch_func = NULL;
	}
}

static u32 riscv_instruction_at(void *p)
{
	u16 *parcel = p;

	return (u32)parcel[0] | (u32)parcel[1] << 16;
}

static void riscv_alternative_fix_auipc_jalr(void *ptr, u32 auipc_insn,
					     u32 jalr_insn, int patch_offset)
{
	u32 call[2] = { auipc_insn, jalr_insn };
	s32 imm;

	/* get and adjust new target address */
	imm = riscv_insn_extract_utype_itype_imm(auipc_insn, jalr_insn);
	imm -= patch_offset;

	/* update instructions */
	riscv_insn_insert_utype_itype_imm(&call[0], &call[1], imm);

	/* patch the call place again */
	patch_text_nosync(ptr, call, sizeof(u32) * 2);
}

static void riscv_alternative_fix_jal(void *ptr, u32 jal_insn, int patch_offset)
{
	s32 imm;

	/* get and adjust new target address */
	imm = riscv_insn_extract_jtype_imm(jal_insn);
	imm -= patch_offset;

	/* update instruction */
	riscv_insn_insert_jtype_imm(&jal_insn, imm);

	/* patch the call place again */
	patch_text_nosync(ptr, &jal_insn, sizeof(u32));
}

void riscv_alternative_fix_offsets(void *alt_ptr, unsigned int len,
				      int patch_offset)
{
	int num_insn = len / sizeof(u32);
	int i;

	for (i = 0; i < num_insn; i++) {
		u32 insn = riscv_instruction_at(alt_ptr + i * sizeof(u32));

		/*
		 * May be the start of an auipc + jalr pair
		 * Needs to check that at least one more instruction
		 * is in the list.
		 */
		if (riscv_insn_is_auipc(insn) && i < num_insn - 1) {
			u32 insn2 = riscv_instruction_at(alt_ptr + (i + 1) * sizeof(u32));

			if (!riscv_insn_is_jalr(insn2))
				continue;

			/* if instruction pair is a call, it will use the ra register */
			if (RV_EXTRACT_RD_REG(insn) != 1)
				continue;

			riscv_alternative_fix_auipc_jalr(alt_ptr + i * sizeof(u32),
							 insn, insn2, patch_offset);
			i++;
		}

		if (riscv_insn_is_jal(insn)) {
			s32 imm = riscv_insn_extract_jtype_imm(insn);

			/* Don't modify jumps inside the alternative block */
			if ((alt_ptr + i * sizeof(u32) + imm) >= alt_ptr &&
			    (alt_ptr + i * sizeof(u32) + imm) < (alt_ptr + len))
				continue;

			riscv_alternative_fix_jal(alt_ptr + i * sizeof(u32),
						  insn, patch_offset);
		}
	}
}

/*
 * This is called very early in the boot process (directly after we run
 * a feature detect on the boot CPU). No need to worry about other CPUs
 * here.
 */
static void __init_or_module _apply_alternatives(struct alt_entry *begin,
						 struct alt_entry *end,
						 unsigned int stage)
{
	struct cpu_manufacturer_info_t cpu_mfr_info;

	riscv_fill_cpu_mfr_info(&cpu_mfr_info);

	riscv_cpufeature_patch_func(begin, end, stage);

	if (!cpu_mfr_info.patch_func)
		return;

	cpu_mfr_info.patch_func(begin, end,
				cpu_mfr_info.arch_id,
				cpu_mfr_info.imp_id,
				stage);
}

#ifdef CONFIG_MMU
static void __init apply_vdso_alternatives(void)
{
	const Elf_Ehdr *hdr;
	const Elf_Shdr *shdr;
	const Elf_Shdr *alt;
	struct alt_entry *begin, *end;

	hdr = (Elf_Ehdr *)vdso_start;
	shdr = (void *)hdr + hdr->e_shoff;
	alt = find_section(hdr, shdr, ".alternative");
	if (!alt)
		return;

	begin = (void *)hdr + alt->sh_offset,
	end = (void *)hdr + alt->sh_offset + alt->sh_size,

	_apply_alternatives((struct alt_entry *)begin,
			    (struct alt_entry *)end,
			    RISCV_ALTERNATIVES_BOOT);
}
#else
static void __init apply_vdso_alternatives(void) { }
#endif

void __init apply_boot_alternatives(void)
{
	/* If called on non-boot cpu things could go wrong */
	WARN_ON(smp_processor_id() != 0);

	_apply_alternatives((struct alt_entry *)__alt_start,
			    (struct alt_entry *)__alt_end,
			    RISCV_ALTERNATIVES_BOOT);

	apply_vdso_alternatives();
}

/*
 * apply_early_boot_alternatives() is called from setup_vm() with MMU-off.
 *
 * Following requirements should be honoured for it to work correctly:
 * 1) It should use PC-relative addressing for accessing kernel symbols.
 *    To achieve this we always use GCC cmodel=medany.
 * 2) The compiler instrumentation for FTRACE will not work for setup_vm()
 *    so disable compiler instrumentation when FTRACE is enabled.
 *
 * Currently, the above requirements are honoured by using custom CFLAGS
 * for alternative.o in kernel/Makefile.
 */
void __init apply_early_boot_alternatives(void)
{
#ifdef CONFIG_RISCV_ALTERNATIVE_EARLY
	_apply_alternatives((struct alt_entry *)__alt_start,
			    (struct alt_entry *)__alt_end,
			    RISCV_ALTERNATIVES_EARLY_BOOT);
#endif
}

#ifdef CONFIG_MODULES
void apply_module_alternatives(void *start, size_t length)
{
	_apply_alternatives((struct alt_entry *)start,
			    (struct alt_entry *)(start + length),
			    RISCV_ALTERNATIVES_MODULE);
}
#endif
