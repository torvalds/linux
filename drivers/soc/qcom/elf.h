/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_ELF_COMMON_H
#define __QCOM_ELF_COMMON_H

#include <linux/elf.h>

/* Generic helpers for ELF use */
/* Return first section header */
static inline struct elf_shdr *elf_sheader(struct elfhdr *hdr)
{
	return (struct elf_shdr *)((size_t)hdr + (size_t)hdr->e_shoff);
}

/* Return idx section header */
static inline struct elf_shdr *elf_section(struct elfhdr *hdr, int idx)
{
	return &elf_sheader(hdr)[idx];
}

/* Return first program header */
static inline struct elf_phdr *elf_pheader(struct elfhdr *hdr)
{
	return (struct elf_phdr *)((size_t)hdr + (size_t)hdr->e_phoff);
}

/* Return idx program header */
static inline struct elf_phdr *elf_program(struct elfhdr *hdr, int idx)
{
	return &elf_pheader(hdr)[idx];
}

/* Return section's string table header */
static inline char *elf_str_table(struct elfhdr *hdr)
{
	if (hdr->e_shstrndx == SHN_UNDEF)
		return NULL;
	return (char *)hdr + elf_section(hdr, hdr->e_shstrndx)->sh_offset;
}

#endif
