/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * (c) Copyright 2006 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 */

#ifndef _ASM_POWERPC_DCR_GENERIC_H
#define _ASM_POWERPC_DCR_GENERIC_H
#ifdef __KERNEL__
#ifndef __ASSEMBLY__

enum host_type_t {DCR_HOST_MMIO, DCR_HOST_NATIVE, DCR_HOST_INVALID};

typedef struct {
	enum host_type_t type;
	union {
		dcr_host_mmio_t mmio;
		dcr_host_native_t native;
	} host;
} dcr_host_t;

extern bool dcr_map_ok_generic(dcr_host_t host);

extern dcr_host_t dcr_map_generic(struct device_node *dev, unsigned int dcr_n,
			  unsigned int dcr_c);
extern void dcr_unmap_generic(dcr_host_t host, unsigned int dcr_c);

extern u32 dcr_read_generic(dcr_host_t host, unsigned int dcr_n);

extern void dcr_write_generic(dcr_host_t host, unsigned int dcr_n, u32 value);

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_DCR_GENERIC_H */


