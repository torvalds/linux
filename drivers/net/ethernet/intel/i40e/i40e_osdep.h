/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#ifndef _I40E_OSDEP_H_
#define _I40E_OSDEP_H_

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/tcp.h>
#include <linux/pci.h>
#include <linux/highuid.h>

/* get readq/writeq support for 32 bit kernels, use the low-first version */
#include <linux/io-64-nonatomic-lo-hi.h>

/* File to be the magic between shared code and
 * actual OS primitives
 */

struct i40e_hw;
struct device *i40e_hw_to_dev(struct i40e_hw *hw);

#define hw_dbg(hw, S, A...) dev_dbg(i40e_hw_to_dev(hw), S, ##A)

#define wr32(a, reg, value)	writel((value), ((a)->hw_addr + (reg)))
#define rd32(a, reg)		readl((a)->hw_addr + (reg))

#define rd64(a, reg)		readq((a)->hw_addr + (reg))
#define i40e_flush(a)		readl((a)->hw_addr + I40E_GLGEN_STAT)

/* memory allocation tracking */
struct i40e_dma_mem {
	void *va;
	dma_addr_t pa;
	u32 size;
};

struct i40e_virt_mem {
	void *va;
	u32 size;
};

#define i40e_debug(h, m, s, ...)				\
do {								\
	if (((m) & (h)->debug_mask))				\
		pr_info("i40e %02x:%02x.%x " s,			\
			(h)->bus.bus_id, (h)->bus.device,	\
			(h)->bus.func, ##__VA_ARGS__);		\
} while (0)

#endif /* _I40E_OSDEP_H_ */
