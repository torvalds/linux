/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARC_ASM_DEFINES_H__
#define __ARC_ASM_DEFINES_H__

#if defined(CONFIG_ARC_MMU_V1)
#define CONFIG_ARC_MMU_VER 1
#elif defined(CONFIG_ARC_MMU_V2)
#define CONFIG_ARC_MMU_VER 2
#elif defined(CONFIG_ARC_MMU_V3)
#define CONFIG_ARC_MMU_VER 3
#endif

#ifdef CONFIG_ARC_HAS_LLSC
#define __CONFIG_ARC_HAS_LLSC_VAL 1
#else
#define __CONFIG_ARC_HAS_LLSC_VAL 0
#endif

#ifdef CONFIG_ARC_HAS_SWAPE
#define __CONFIG_ARC_HAS_SWAPE_VAL 1
#else
#define __CONFIG_ARC_HAS_SWAPE_VAL 0
#endif

#ifdef CONFIG_ARC_HAS_RTSC
#define __CONFIG_ARC_HAS_RTSC_VAL 1
#else
#define __CONFIG_ARC_HAS_RTSC_VAL 0
#endif

#ifdef CONFIG_ARC_MMU_SASID
#define __CONFIG_ARC_MMU_SASID_VAL 1
#else
#define __CONFIG_ARC_MMU_SASID_VAL 0
#endif

#ifdef CONFIG_ARC_HAS_ICACHE
#define __CONFIG_ARC_HAS_ICACHE	1
#else
#define __CONFIG_ARC_HAS_ICACHE	0
#endif

#ifdef CONFIG_ARC_HAS_DCACHE
#define __CONFIG_ARC_HAS_DCACHE	1
#else
#define __CONFIG_ARC_HAS_DCACHE	0
#endif

#endif /* __ARC_ASM_DEFINES_H__ */
