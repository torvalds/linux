/* SPDX-License-Identifier: GPL-2.0 */
/*
 * LoongArch specific ACPICA environments and implementation
 *
 * Author: Jianmin Lv <lvjianmin@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef _ASM_LOONGARCH_ACENV_H
#define _ASM_LOONGARCH_ACENV_H

#ifdef CONFIG_ARCH_STRICT_ALIGN
#define ACPI_MISALIGNMENT_NOT_SUPPORTED
#endif /* CONFIG_ARCH_STRICT_ALIGN */

#endif /* _ASM_LOONGARCH_ACENV_H */
