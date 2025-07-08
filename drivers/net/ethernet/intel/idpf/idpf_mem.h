/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_MEM_H_
#define _IDPF_MEM_H_

#include <linux/io.h>

struct idpf_dma_mem {
	void *va;
	dma_addr_t pa;
	size_t size;
};

#define idpf_mbx_wr32(a, reg, value)	writel((value), ((a)->mbx.vaddr + (reg)))
#define idpf_mbx_rd32(a, reg)		readl((a)->mbx.vaddr + (reg))
#define idpf_mbx_wr64(a, reg, value)	writeq((value), ((a)->mbx.vaddr + (reg)))
#define idpf_mbx_rd64(a, reg)		readq((a)->mbx.vaddr + (reg))

#endif /* _IDPF_MEM_H_ */
