/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Rivos, Inc.
 */
#ifndef __RISCV_KERNEL_COPY_UNALIGNED_H
#define __RISCV_KERNEL_COPY_UNALIGNED_H

#include <linux/types.h>

void __riscv_copy_words_unaligned(void *dst, const void *src, size_t size);
void __riscv_copy_bytes_unaligned(void *dst, const void *src, size_t size);

#ifdef CONFIG_RISCV_PROBE_VECTOR_UNALIGNED_ACCESS
void __riscv_copy_vec_words_unaligned(void *dst, const void *src, size_t size);
void __riscv_copy_vec_bytes_unaligned(void *dst, const void *src, size_t size);
#endif

#endif /* __RISCV_KERNEL_COPY_UNALIGNED_H */
