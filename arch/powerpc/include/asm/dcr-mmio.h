/*
 * (c) Copyright 2006 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _ASM_POWERPC_DCR_MMIO_H
#define _ASM_POWERPC_DCR_MMIO_H
#ifdef __KERNEL__

#include <asm/io.h>

typedef struct {
	void __iomem *token;
	unsigned int stride;
	unsigned int base;
} dcr_host_mmio_t;

static inline bool dcr_map_ok_mmio(dcr_host_mmio_t host)
{
	return host.token != NULL;
}

extern dcr_host_mmio_t dcr_map_mmio(struct device_node *dev,
				    unsigned int dcr_n,
				    unsigned int dcr_c);
extern void dcr_unmap_mmio(dcr_host_mmio_t host, unsigned int dcr_c);

static inline u32 dcr_read_mmio(dcr_host_mmio_t host, unsigned int dcr_n)
{
	return in_be32(host.token + ((host.base + dcr_n) * host.stride));
}

static inline void dcr_write_mmio(dcr_host_mmio_t host,
				  unsigned int dcr_n,
				  u32 value)
{
	out_be32(host.token + ((host.base + dcr_n) * host.stride), value);
}

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_DCR_MMIO_H */


