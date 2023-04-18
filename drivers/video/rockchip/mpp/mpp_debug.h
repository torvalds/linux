/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *	Randy Li, randy.li@rock-chips.com
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */
#ifndef __ROCKCHIP_MPP_DEBUG_H__
#define __ROCKCHIP_MPP_DEBUG_H__

#include <linux/types.h>

/*
 * debug flag usage:
 * +------+-------------------+
 * | 8bit |      24bit        |
 * +------+-------------------+
 *  0~23 bit is for different information type
 * 24~31 bit is for information print format
 */

#define DEBUG_POWER				0x00000001
#define DEBUG_CLOCK				0x00000002
#define DEBUG_IRQ_STATUS			0x00000004
#define DEBUG_IOMMU				0x00000008
#define DEBUG_IOCTL				0x00000010
#define DEBUG_FUNCTION				0x00000020
#define DEBUG_REGISTER				0x00000040
#define DEBUG_EXTRA_INFO			0x00000080
#define DEBUG_TIMING				0x00000100
#define DEBUG_TASK_INFO				0x00000200
#define DEBUG_DUMP_ERR_REG			0x00000400
#define DEBUG_LINK_TABLE			0x00000800

#define DEBUG_SET_REG				0x00001000
#define DEBUG_GET_REG				0x00002000
#define DEBUG_PPS_FILL				0x00004000
#define DEBUG_IRQ_CHECK				0x00008000
#define DEBUG_CACHE_32B				0x00010000

#define DEBUG_RESET				0x00020000
#define DEBUG_SET_REG_L2			0x00040000
#define DEBUG_GET_REG_L2			0x00080000
#define DEBUG_GET_PERF_VAL			0x00100000
#define DEBUG_SRAM_INFO				0x00200000

#define DEBUG_SESSION				0x00400000
#define DEBUG_DEVICE				0x00800000

#define DEBUG_CCU				0x01000000
#define DEBUG_CORE				0x02000000

#define PRINT_FUNCTION				0x80000000
#define PRINT_LINE				0x40000000

/* reuse old debug bit flag */
#define DEBUG_PART_TIMING			0x00000080
#define DEBUG_SLICE				0x00000002

extern unsigned int mpp_dev_debug;

#define mpp_debug_unlikely(type)				\
		(unlikely(mpp_dev_debug & (type)))

#define mpp_debug_func(type, fmt, args...)			\
	do {							\
		if (unlikely(mpp_dev_debug & (type))) {		\
			pr_info("%s:%d: " fmt,			\
				 __func__, __LINE__, ##args);	\
		}						\
	} while (0)
#define mpp_debug(type, fmt, args...)				\
	do {							\
		if (unlikely(mpp_dev_debug & (type))) {		\
			pr_info(fmt, ##args);			\
		}						\
	} while (0)

#define mpp_debug_enter()					\
	do {							\
		if (unlikely(mpp_dev_debug & DEBUG_FUNCTION)) {	\
			pr_info("%s:%d: enter\n",		\
				 __func__, __LINE__);		\
		}						\
	} while (0)

#define mpp_debug_leave()					\
	do {							\
		if (unlikely(mpp_dev_debug & DEBUG_FUNCTION)) {	\
			pr_info("%s:%d: leave\n",		\
				 __func__, __LINE__);		\
		}						\
	} while (0)

#define mpp_err(fmt, args...)					\
		pr_err("%s:%d: " fmt, __func__, __LINE__, ##args)

#define mpp_dbg_link(fmt, args...)				\
	do {							\
		if (unlikely(mpp_dev_debug & DEBUG_LINK_TABLE)) {		\
			pr_info("%s:%d: " fmt,			\
				 __func__, __LINE__, ##args);	\
		}						\
	} while (0)

#define mpp_dbg_session(fmt, args...)				\
	do {							\
		if (unlikely(mpp_dev_debug & DEBUG_SESSION)) {	\
			pr_info(fmt, ##args);			\
		}						\
	} while (0)

#define mpp_dbg_ccu(fmt, args...)				\
	do {							\
		if (unlikely(mpp_dev_debug & DEBUG_CCU)) {	\
			pr_info("%s:%d: " fmt,			\
				 __func__, __LINE__, ##args);	\
		}						\
	} while (0)

#define mpp_dbg_core(fmt, args...)				\
	do {							\
		if (unlikely(mpp_dev_debug & DEBUG_CORE)) {	\
			pr_info(fmt, ##args);			\
		}						\
	} while (0)

#define mpp_dbg_slice(fmt, args...)				\
	do {							\
		if (unlikely(mpp_dev_debug & DEBUG_SLICE)) {	\
			pr_info(fmt, ##args);			\
		}						\
	} while (0)

#endif
