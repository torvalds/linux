/*
 * Copyright (C) 2013 Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_FBTFT__INTERNAL_H
#define __LINUX_FBTFT_INTERNAL_H

void fbtft_sysfs_init(struct fbtft_par *par);
void fbtft_sysfs_exit(struct fbtft_par *par);
void fbtft_expand_debug_value(unsigned long *debug);
int fbtft_gamma_parse_str(struct fbtft_par *par, unsigned long *curves,
			  const char *str, int size);

#endif /* __LINUX_FBTFT_INTERNAL_H */
