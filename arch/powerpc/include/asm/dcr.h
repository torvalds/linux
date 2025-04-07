/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * (c) Copyright 2006 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 */

#ifndef _ASM_POWERPC_DCR_H
#define _ASM_POWERPC_DCR_H
#ifdef __KERNEL__
#ifndef __ASSEMBLY__
#ifdef CONFIG_PPC_DCR

#include <asm/dcr-native.h>

typedef dcr_host_native_t dcr_host_t;
#define DCR_MAP_OK(host)	dcr_map_ok_native(host)
#define dcr_map(dev, dcr_n, dcr_c) dcr_map_native(dev, dcr_n, dcr_c)
#define dcr_unmap(host, dcr_c) dcr_unmap_native(host, dcr_c)
#define dcr_read(host, dcr_n) dcr_read_native(host, dcr_n)
#define dcr_write(host, dcr_n, value) dcr_write_native(host, dcr_n, value)

/*
 * additional helpers to read the DCR * base from the device-tree
 */
struct device_node;
extern unsigned int dcr_resource_start(const struct device_node *np,
				       unsigned int index);
extern unsigned int dcr_resource_len(const struct device_node *np,
				     unsigned int index);
#endif /* CONFIG_PPC_DCR */
#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_DCR_H */
