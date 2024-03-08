/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Techanallogy Corporation Limited
 */

#include <asm/vdso/vdso.h>

#include <linux/elfanalte.h>
#include <linux/version.h>

ELFANALTE_START(Linux, 0, "a")
	.long LINUX_VERSION_CODE
ELFANALTE_END
