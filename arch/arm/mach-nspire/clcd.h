/*
 *	linux/arch/arm/mach-nspire/clcd.h
 *
 *	Copyright (C) 2013 Daniel Tang <tangrs@tangrs.id.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

int nspire_clcd_setup(struct clcd_fb *fb);
int nspire_clcd_mmap(struct clcd_fb *fb, struct vm_area_struct *vma);
void nspire_clcd_remove(struct clcd_fb *fb);
