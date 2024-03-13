/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_AUXVEC_H
#define __ASM_AUXVEC_H

/* Location of VDSO image. */
#define AT_SYSINFO_EHDR		33

#define AT_VECTOR_SIZE_ARCH 1 /* entries in ARCH_DLINFO */

#endif /* __ASM_AUXVEC_H */
