/*
 *    Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *    Copyright (C) 1999,2003 Matthew Wilcox < willy at debian . org >
 *    portions from "linux/ioctl.h for Linux" by H.H. Bergman.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#ifndef _ASM_PARISC_IOCTL_H
#define _ASM_PARISC_IOCTL_H

/* ioctl command encoding: 32 bits total, command in lower 16 bits,
 * size of the parameter structure in the lower 14 bits of the
 * upper 16 bits.
 * Encoding the size of the parameter structure in the ioctl request
 * is useful for catching programs compiled with old versions
 * and to avoid overwriting user space outside the user buffer area.
 * The highest 2 bits are reserved for indicating the ``access mode''.
 * NOTE: This limits the max parameter size to 16kB -1 !
 */

/*
 * Direction bits.
 */
#define _IOC_NONE	0U
#define _IOC_WRITE	2U
#define _IOC_READ	1U

#include <asm-generic/ioctl.h>

#endif /* _ASM_PARISC_IOCTL_H */
