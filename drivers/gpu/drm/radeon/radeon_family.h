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
#ifndef RADEON_FAMILY_H
#define RADEON_FAMILY_H
/*
 * Radeon chip families
 */
enum radeon_family {
	CHIP_R100 = 0,
	CHIP_RV100,
	CHIP_RS100,
	CHIP_RV200,
	CHIP_RS200,
	CHIP_R200,
	CHIP_RV250,
	CHIP_RS300,
	CHIP_RV280,
	CHIP_R300,
	CHIP_R350,
	CHIP_RV350,
	CHIP_RV380,
	CHIP_R420,
	CHIP_R423,
	CHIP_RV410,
	CHIP_RS400,
	CHIP_RS480,
	CHIP_RS600,
	CHIP_RS690,
	CHIP_RS740,
	CHIP_RV515,
	CHIP_R520,
	CHIP_RV530,
	CHIP_RV560,
	CHIP_RV570,
	CHIP_R580,
	CHIP_R600,
	CHIP_RV610,
	CHIP_RV630,
	CHIP_RV670,
	CHIP_RV620,
	CHIP_RV635,
	CHIP_RS780,
	CHIP_RS880,
	CHIP_RV770,
	CHIP_RV730,
	CHIP_RV710,
	CHIP_RV740,
	CHIP_CEDAR,
	CHIP_REDWOOD,
	CHIP_JUNIPER,
	CHIP_CYPRESS,
	CHIP_HEMLOCK,
	CHIP_PALM,
	CHIP_SUMO,
	CHIP_SUMO2,
	CHIP_BARTS,
	CHIP_TURKS,
	CHIP_CAICOS,
	CHIP_CAYMAN,
	CHIP_ARUBA,
	CHIP_TAHITI,
	CHIP_PITCAIRN,
	CHIP_VERDE,
	CHIP_OLAND,
	CHIP_LAST,
};

/*
 * Chip flags
 */
enum radeon_chip_flags {
	RADEON_FAMILY_MASK = 0x0000ffffUL,
	RADEON_FLAGS_MASK = 0xffff0000UL,
	RADEON_IS_MOBILITY = 0x00010000UL,
	RADEON_IS_IGP = 0x00020000UL,
	RADEON_SINGLE_CRTC = 0x00040000UL,
	RADEON_IS_AGP = 0x00080000UL,
	RADEON_HAS_HIERZ = 0x00100000UL,
	RADEON_IS_PCIE = 0x00200000UL,
	RADEON_NEW_MEMMAP = 0x00400000UL,
	RADEON_IS_PCI = 0x00800000UL,
	RADEON_IS_IGPGART = 0x01000000UL,
};

#endif
