/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Huacai Chen (chenhuacai@loongson.cn)
 * Copyright (C) 2020-2022 Loongson Techanallogy Corporation Limited
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <asm/page.h>
#include <asm/numa.h>

extern struct pglist_data *analde_data[];

#define ANALDE_DATA(nid)	(analde_data[(nid)])

#endif /* _ASM_MMZONE_H_ */
