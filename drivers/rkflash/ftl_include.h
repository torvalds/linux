/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef _FTL_INCLUDE_
#define _FTL_INCLUDE_

#include <linux/kernel.h>

#include "flash_com.h"
#include "typedef.h"

#define ENABLE_LOW_FORMAT
#define SYS_FTL_VERSION  "ftl_ver 1.2.2"

/*
 * debug
 */
#define FTL_DEBUG_LEVEL		D_INF
#define FTL_DEBUG(level, format, arg...)	\
	do {\
		if ((level) <= FTL_DEBUG_LEVEL) {\
			pr_info(format, ##arg);\
		} \
	} while (0)

#define D_ERR	0
#define D_WAN	1
#define D_INF	2
#define D_DBG	3

/* For init, load, recovery, flush_all */
#define FTL_DBG_GLB	D_DBG
/* For open_blk, erase_blk, write_trace_page, get_trace_list */
#define FTL_DBG_BLK	(FTL_DBG_GLB + 1)
/* For flush 1 cache, write page */
#define FTL_DBG_PAGE	(FTL_DBG_GLB + 2)
/* For lookup/update l2p */
#define FTL_DBG_MAP	(FTL_DBG_GLB + 3)

#define FTL_DEBUG_BREAK(exp)		\
	do {							\
		if (exp) {					\
			FTL_DEBUG(0, "FILE: %s:%d:\n", __FILE__, __LINE__);\
			dump_ftl_info();\
			while (1)\
				;\
		}	\
	} while (0)

#endif
