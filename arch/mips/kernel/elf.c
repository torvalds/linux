/*
 * Copyright (C) 2014 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/elf.h>
#include <linux/sched.h>

enum {
	FP_ERROR = -1,
	FP_DOUBLE_64A = -2,
};

int arch_elf_pt_proc(void *_ehdr, void *_phdr, struct file *elf,
		     bool is_interp, struct arch_elf_state *state)
{
	struct elfhdr *ehdr = _ehdr;
	struct elf_phdr *phdr = _phdr;
	struct mips_elf_abiflags_v0 abiflags;
	int ret;

	if (config_enabled(CONFIG_64BIT) &&
	    (ehdr->e_ident[EI_CLASS] != ELFCLASS32))
		return 0;
	if (phdr->p_type != PT_MIPS_ABIFLAGS)
		return 0;
	if (phdr->p_filesz < sizeof(abiflags))
		return -EINVAL;

	ret = kernel_read(elf, phdr->p_offset, (char *)&abiflags,
			  sizeof(abiflags));
	if (ret < 0)
		return ret;
	if (ret != sizeof(abiflags))
		return -EIO;

	/* Record the required FP ABIs for use by mips_check_elf */
	if (is_interp)
		state->interp_fp_abi = abiflags.fp_abi;
	else
		state->fp_abi = abiflags.fp_abi;

	return 0;
}

static inline unsigned get_fp_abi(struct elfhdr *ehdr, int in_abi)
{
	/* If the ABI requirement is provided, simply return that */
	if (in_abi != -1)
		return in_abi;

	/* If the EF_MIPS_FP64 flag was set, return MIPS_ABI_FP_64 */
	if (ehdr->e_flags & EF_MIPS_FP64)
		return MIPS_ABI_FP_64;

	/* Default to MIPS_ABI_FP_DOUBLE */
	return MIPS_ABI_FP_DOUBLE;
}

int arch_check_elf(void *_ehdr, bool has_interpreter,
		   struct arch_elf_state *state)
{
	struct elfhdr *ehdr = _ehdr;
	unsigned fp_abi, interp_fp_abi, abi0, abi1;

	/* Ignore non-O32 binaries */
	if (config_enabled(CONFIG_64BIT) &&
	    (ehdr->e_ident[EI_CLASS] != ELFCLASS32))
		return 0;

	fp_abi = get_fp_abi(ehdr, state->fp_abi);

	if (has_interpreter) {
		interp_fp_abi = get_fp_abi(ehdr, state->interp_fp_abi);

		abi0 = min(fp_abi, interp_fp_abi);
		abi1 = max(fp_abi, interp_fp_abi);
	} else {
		abi0 = abi1 = fp_abi;
	}

	state->overall_abi = FP_ERROR;

	if (abi0 == abi1) {
		state->overall_abi = abi0;
	} else if (abi0 == MIPS_ABI_FP_ANY) {
		state->overall_abi = abi1;
	} else if (abi0 == MIPS_ABI_FP_DOUBLE) {
		switch (abi1) {
		case MIPS_ABI_FP_XX:
			state->overall_abi = MIPS_ABI_FP_DOUBLE;
			break;

		case MIPS_ABI_FP_64A:
			state->overall_abi = FP_DOUBLE_64A;
			break;
		}
	} else if (abi0 == MIPS_ABI_FP_SINGLE ||
		   abi0 == MIPS_ABI_FP_SOFT) {
		/* Cannot link with other ABIs */
	} else if (abi0 == MIPS_ABI_FP_OLD_64) {
		switch (abi1) {
		case MIPS_ABI_FP_XX:
		case MIPS_ABI_FP_64:
		case MIPS_ABI_FP_64A:
			state->overall_abi = MIPS_ABI_FP_64;
			break;
		}
	} else if (abi0 == MIPS_ABI_FP_XX ||
		   abi0 == MIPS_ABI_FP_64 ||
		   abi0 == MIPS_ABI_FP_64A) {
		state->overall_abi = MIPS_ABI_FP_64;
	}

	switch (state->overall_abi) {
	case MIPS_ABI_FP_64:
	case MIPS_ABI_FP_64A:
	case FP_DOUBLE_64A:
		if (!config_enabled(CONFIG_MIPS_O32_FP64_SUPPORT))
			return -ELIBBAD;
		break;

	case FP_ERROR:
		return -ELIBBAD;
	}

	return 0;
}

void mips_set_personality_fp(struct arch_elf_state *state)
{
	if (config_enabled(CONFIG_FP32XX_HYBRID_FPRS)) {
		/*
		 * Use hybrid FPRs for all code which can correctly execute
		 * with that mode.
		 */
		switch (state->overall_abi) {
		case MIPS_ABI_FP_DOUBLE:
		case MIPS_ABI_FP_SINGLE:
		case MIPS_ABI_FP_SOFT:
		case MIPS_ABI_FP_XX:
		case MIPS_ABI_FP_ANY:
			/* FR=1, FRE=1 */
			clear_thread_flag(TIF_32BIT_FPREGS);
			set_thread_flag(TIF_HYBRID_FPREGS);
			return;
		}
	}

	switch (state->overall_abi) {
	case MIPS_ABI_FP_DOUBLE:
	case MIPS_ABI_FP_SINGLE:
	case MIPS_ABI_FP_SOFT:
		/* FR=0 */
		set_thread_flag(TIF_32BIT_FPREGS);
		clear_thread_flag(TIF_HYBRID_FPREGS);
		break;

	case FP_DOUBLE_64A:
		/* FR=1, FRE=1 */
		clear_thread_flag(TIF_32BIT_FPREGS);
		set_thread_flag(TIF_HYBRID_FPREGS);
		break;

	case MIPS_ABI_FP_64:
	case MIPS_ABI_FP_64A:
		/* FR=1, FRE=0 */
		clear_thread_flag(TIF_32BIT_FPREGS);
		clear_thread_flag(TIF_HYBRID_FPREGS);
		break;

	case MIPS_ABI_FP_XX:
	case MIPS_ABI_FP_ANY:
		if (!config_enabled(CONFIG_MIPS_O32_FP64_SUPPORT))
			set_thread_flag(TIF_32BIT_FPREGS);
		else
			clear_thread_flag(TIF_32BIT_FPREGS);

		clear_thread_flag(TIF_HYBRID_FPREGS);
		break;

	default:
	case FP_ERROR:
		BUG();
	}
}
