/*
 * Copyright (C) 2013 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_STRING_H
#define __ASM_STRING_H

#define __HAVE_ARCH_STRRCHR
extern char *strrchr(const char *, int c);

#define __HAVE_ARCH_STRCHR
extern char *strchr(const char *, int c);

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMCHR
extern void *memchr(const void *, int, __kernel_size_t);

#define __HAVE_ARCH_MEMSET
extern void *memset(void *, int, __kernel_size_t);

#endif
