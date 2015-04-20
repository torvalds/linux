/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */

/* this file defines the CHIP_  and family flags used in the pciids,
 * its is common between kms and non-kms because duplicating it and
 * changing one place is fail.
 */
#ifndef AMDGPU_FAMILY_H
#define AMDGPU_FAMILY_H
/*
 * Supported ASIC types
 */
enum amdgpu_asic_type {
	CHIP_BONAIRE = 0,
	CHIP_KAVERI,
	CHIP_KABINI,
	CHIP_HAWAII,
	CHIP_MULLINS,
	CHIP_TOPAZ,
	CHIP_TONGA,
	CHIP_CARRIZO,
	CHIP_LAST,
};

/*
 * Chip flags
 */
enum amdgpu_chip_flags {
	AMDGPU_ASIC_MASK = 0x0000ffffUL,
	AMDGPU_FLAGS_MASK  = 0xffff0000UL,
	AMDGPU_IS_MOBILITY = 0x00010000UL,
	AMDGPU_IS_APU      = 0x00020000UL,
	AMDGPU_IS_PX       = 0x00040000UL,
	AMDGPU_EXP_HW_SUPPORT = 0x00080000UL,
};

#endif
