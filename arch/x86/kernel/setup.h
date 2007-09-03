/*
 * Internal declarations for shared x86 setup code.
 *
 * Copyright (c) 2008 Advanced Micro Devices, Inc.
 * Contributed by Robert Richter <robert.richter@amd.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 */

#ifndef _ARCH_X86_KERNEL_SETUP_H

extern void __cpuinit amd_enable_pci_ext_cfg(struct cpuinfo_x86 *c);

#endif	/* _ARCH_X86_KERNEL_SETUP_H */
