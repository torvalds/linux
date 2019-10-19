/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_OSDEP_H_
#define _ICE_OSDEP_H_

#include <linux/types.h>
#include <linux/io.h>
#ifndef CONFIG_64BIT
#include <linux/io-64-nonatomic-lo-hi.h>
#endif

#define wr32(a, reg, value)	writel((value), ((a)->hw_addr + (reg)))
#define rd32(a, reg)		readl((a)->hw_addr + (reg))
#define wr64(a, reg, value)	writeq((value), ((a)->hw_addr + (reg)))
#define rd64(a, reg)		readq((a)->hw_addr + (reg))

#define ice_flush(a)		rd32((a), GLGEN_STAT)
#define ICE_M(m, s)		((m) << (s))

struct ice_dma_mem {
	void *va;
	dma_addr_t pa;
	size_t size;
};

#define ice_hw_to_dev(ptr)	\
	(&(container_of((ptr), struct ice_pf, hw))->pdev->dev)

#ifdef CONFIG_DYNAMIC_DEBUG
#define ice_debug(hw, type, fmt, args...) \
	dev_dbg(ice_hw_to_dev(hw), fmt, ##args)

#define ice_debug_array(hw, type, rowsize, groupsize, buf, len) \
	print_hex_dump_debug(KBUILD_MODNAME " ",		\
			     DUMP_PREFIX_OFFSET, rowsize,	\
			     groupsize, buf, len, false)
#else
#define ice_debug(hw, type, fmt, args...)			\
do {								\
	if ((type) & (hw)->debug_mask)				\
		dev_info(ice_hw_to_dev(hw), fmt, ##args);	\
} while (0)

#ifdef DEBUG
#define ice_debug_array(hw, type, rowsize, groupsize, buf, len) \
do {								\
	if ((type) & (hw)->debug_mask)				\
		print_hex_dump_debug(KBUILD_MODNAME,		\
				     DUMP_PREFIX_OFFSET,	\
				     rowsize, groupsize, buf,	\
				     len, false);		\
} while (0)
#else
#define ice_debug_array(hw, type, rowsize, groupsize, buf, len) \
do {								\
	struct ice_hw *hw_l = hw;				\
	if ((type) & (hw_l)->debug_mask) {			\
		u16 len_l = len;				\
		u8 *buf_l = buf;				\
		int i;						\
		for (i = 0; i < (len_l - 16); i += 16)		\
			ice_debug(hw_l, type, "0x%04X  %16ph\n",\
				  i, ((buf_l) + i));		\
		if (i < len_l)					\
			ice_debug(hw_l, type, "0x%04X  %*ph\n", \
				  i, ((len_l) - i), ((buf_l) + i));\
	}							\
} while (0)
#endif /* DEBUG */
#endif /* CONFIG_DYNAMIC_DEBUG */

#endif /* _ICE_OSDEP_H_ */
