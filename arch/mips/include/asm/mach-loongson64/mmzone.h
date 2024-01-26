/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2010 Loongson Inc. & Lemote Inc. &
 *                    Institute of Computing Technology
 * Author:  Xiang Gao, gaoxiang@ict.ac.cn
 *          Huacai Chen, chenhc@lemote.com
 *          Xiaofu Meng, Shuangshuang Zhang
 */
#ifndef _ASM_MACH_LOONGSON64_MMZONE_H
#define _ASM_MACH_LOONGSON64_MMZONE_H

#define NODE_ADDRSPACE_SHIFT 44

#define pa_to_nid(addr)  (((addr) & 0xf00000000000) >> NODE_ADDRSPACE_SHIFT)
#define nid_to_addrbase(nid) ((unsigned long)(nid) << NODE_ADDRSPACE_SHIFT)

extern struct pglist_data *__node_data[];

#define NODE_DATA(n)		(__node_data[n])

extern void __init prom_init_numa_memory(void);

#endif /* _ASM_MACH_MMZONE_H */
