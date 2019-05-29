/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-netx/fb.h
 *
 * Copyright (c) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 */

void netx_clcd_enable(struct clcd_fb *fb);
int netx_clcd_setup(struct clcd_fb *fb);
int netx_clcd_mmap(struct clcd_fb *fb, struct vm_area_struct *vma);
void netx_clcd_remove(struct clcd_fb *fb);
int netx_fb_init(struct clcd_board *board, struct clcd_panel *panel);
