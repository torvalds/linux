/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Huacai Chen (chenhuacai@loongson.cn)
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <asm/page.h>
#include <asm/numa.h>

extern struct pglist_data *node_data[];

#define NODE_DATA(nid)	(node_data[(nid)])

extern void setup_zero_pages(void);

#endif /* _ASM_MMZONE_H_ */
