/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASMARC_SETUP_H
#define __ASMARC_SETUP_H


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

struct cpuinfo_data {
	struct id_to_str info;
	int up_range;
};

extern int root_mountflags, end_mem;

void setup_processor(void);
void __init setup_arch_memory(void);

/* Helpers used in arc_*_mumbojumbo routines */
#define IS_AVAIL1(v, s)		((v) ? s : "")
#define IS_DISABLED_RUN(v)	((v) ? "" : "(disabled) ")
#define IS_USED_RUN(v)		((v) ? "" : "(not used) ")
#define IS_USED_CFG(cfg)	IS_USED_RUN(IS_ENABLED(cfg))
#define IS_AVAIL2(v, s, cfg)	IS_AVAIL1(v, s), IS_AVAIL1(v, IS_USED_CFG(cfg))

#endif /* __ASMARC_SETUP_H */
