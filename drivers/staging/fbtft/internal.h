/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2013 Noralf Tronnes */

#ifndef __LINUX_FBTFT_INTERNAL_H
#define __LINUX_FBTFT_INTERNAL_H

void fbtft_sysfs_init(struct fbtft_par *par);
void fbtft_sysfs_exit(struct fbtft_par *par);
void fbtft_expand_debug_value(unsigned long *debug);
int fbtft_gamma_parse_str(struct fbtft_par *par, u32 *curves,
			  const char *str, int size);

#endif /* __LINUX_FBTFT_INTERNAL_H */
