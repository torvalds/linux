/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#ifndef _I40E_OSDEP_H_
#define _I40E_OSDEP_H_

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/tcp.h>
#include <linux/pci.h>

/* get readq/writeq support for 32 bit kernels, use the low-first version */
#include <linux/io-64-nonatomic-lo-hi.h>

/* File to be the magic between shared code and
 * actual OS primitives
 */

#define hw_dbg(hw, S, A...)	do {} while (0)

#define wr32(a, reg, value)	writel((value), ((a)->hw_addr + (reg)))
#define rd32(a, reg)		readl((a)->hw_addr + (reg))

#define wr64(a, reg, value)	writeq((value), ((a)->hw_addr + (reg)))
#define rd64(a, reg)		readq((a)->hw_addr + (reg))
#define iavf_flush(a)		readl((a)->hw_addr + IAVF_VFGEN_RSTAT)

/* memory allocation tracking */
struct i40e_dma_mem {
	void *va;
	dma_addr_t pa;
	u32 size;
};

#define i40e_allocate_dma_mem(h, m, unused, s, a) \
	iavf_allocate_dma_mem_d(h, m, s, a)
#define i40e_free_dma_mem(h, m) iavf_free_dma_mem_d(h, m)

struct i40e_virt_mem {
	void *va;
	u32 size;
};
#define i40e_allocate_virt_mem(h, m, s) iavf_allocate_virt_mem_d(h, m, s)
#define i40e_free_virt_mem(h, m) iavf_free_virt_mem_d(h, m)

#define iavf_debug(h, m, s, ...)  iavf_debug_d(h, m, s, ##__VA_ARGS__)
extern void iavf_debug_d(void *hw, u32 mask, char *fmt_str, ...)
	__attribute__ ((format(gnu_printf, 3, 4)));

typedef enum i40e_status_code iavf_status;
#endif /* _I40E_OSDEP_H_ */
