/*
 * arch/arm/mach-netx/fb.h
 *
 * Copyright (c) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

void netx_clcd_enable(struct clcd_fb *fb);
int netx_clcd_setup(struct clcd_fb *fb);
int netx_clcd_mmap(struct clcd_fb *fb, struct vm_area_struct *vma);
void netx_clcd_remove(struct clcd_fb *fb);
int netx_fb_init(struct clcd_board *board, struct clcd_panel *panel);
