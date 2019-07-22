/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *	linux/arch/arm/mach-nspire/clcd.h
 *
 *	Copyright (C) 2013 Daniel Tang <tangrs@tangrs.id.au>
 */

int nspire_clcd_setup(struct clcd_fb *fb);
int nspire_clcd_mmap(struct clcd_fb *fb, struct vm_area_struct *vma);
void nspire_clcd_remove(struct clcd_fb *fb);
