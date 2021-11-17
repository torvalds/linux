/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

struct cache_info {
	unsigned char ways;
	unsigned char line_size;
	unsigned short sets;
	unsigned short size;
#if defined(CONFIG_CPU_CACHE_ALIASING)
	unsigned short aliasing_num;
	unsigned int aliasing_mask;
#endif
};
