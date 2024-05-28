/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */
#ifndef __ASM_ARC_SETUP_H
#define __ASM_ARC_SETUP_H


#include <linux/types.h>
#include <uapi/asm/setup.h>

#define COMMAND_LINE_SIZE 256

/*
 * Data structure to map a ID to string
 * Used a lot for bootup reporting of hardware diversity
 */
struct id_to_str {
	int id;
	const char *str;
};

extern int root_mountflags, end_mem;

void setup_processor(void);
void __init setup_arch_memory(void);
long __init arc_get_mem_sz(void);

/* Helpers used in arc_*_mumbojumbo routines */
#define IS_AVAIL1(v, s)		((v) ? s : "")
#define IS_DISABLED_RUN(v)	((v) ? "" : "(disabled) ")
#define IS_USED_RUN(v)		((v) ? "" : "(not used) ")
#define IS_USED_CFG(cfg)	IS_USED_RUN(IS_ENABLED(cfg))
#define IS_AVAIL2(v, s, cfg)	IS_AVAIL1(v, s), IS_AVAIL1(v, IS_USED_CFG(cfg))
#define IS_AVAIL3(v, v2, s)	IS_AVAIL1(v, s), IS_AVAIL1(v, IS_DISABLED_RUN(v2))

extern void arc_mmu_init(void);
extern int arc_mmu_mumbojumbo(int cpu_id, char *buf, int len);

extern void arc_cache_init(void);
extern int arc_cache_mumbojumbo(int cpu_id, char *buf, int len);

extern void __init handle_uboot_args(void);

#endif /* __ASMARC_SETUP_H */
