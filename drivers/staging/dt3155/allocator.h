/*
 * allocator.h -- prototypes for allocating high memory
 *
 * NOTE: this is different from my previous allocator, the one that
 *       assembles pages, which revealed itself both slow and unreliable.
 *
 * Copyright (C) 1998   rubini@linux.it (Alessandro Rubini)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

void allocator_free_dma(unsigned long address);
unsigned long allocator_allocate_dma(unsigned long kilobytes, int priority);
int allocator_init(u_long *);
void allocator_cleanup(void);
