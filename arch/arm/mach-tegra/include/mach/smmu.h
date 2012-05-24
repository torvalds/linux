/*
 * IOMMU API for SMMU in Tegra30
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef	MACH_SMMU_H
#define	MACH_SMMU_H

enum smmu_hwgrp {
	HWGRP_AFI,
	HWGRP_AVPC,
	HWGRP_DC,
	HWGRP_DCB,
	HWGRP_EPP,
	HWGRP_G2,
	HWGRP_HC,
	HWGRP_HDA,
	HWGRP_ISP,
	HWGRP_MPE,
	HWGRP_NV,
	HWGRP_NV2,
	HWGRP_PPCS,
	HWGRP_SATA,
	HWGRP_VDE,
	HWGRP_VI,

	HWGRP_COUNT,

	HWGRP_END = ~0,
};

#define HWG_AFI		(1 << HWGRP_AFI)
#define HWG_AVPC	(1 << HWGRP_AVPC)
#define HWG_DC		(1 << HWGRP_DC)
#define HWG_DCB		(1 << HWGRP_DCB)
#define HWG_EPP		(1 << HWGRP_EPP)
#define HWG_G2		(1 << HWGRP_G2)
#define HWG_HC		(1 << HWGRP_HC)
#define HWG_HDA		(1 << HWGRP_HDA)
#define HWG_ISP		(1 << HWGRP_ISP)
#define HWG_MPE		(1 << HWGRP_MPE)
#define HWG_NV		(1 << HWGRP_NV)
#define HWG_NV2		(1 << HWGRP_NV2)
#define HWG_PPCS	(1 << HWGRP_PPCS)
#define HWG_SATA	(1 << HWGRP_SATA)
#define HWG_VDE		(1 << HWGRP_VDE)
#define HWG_VI		(1 << HWGRP_VI)

#endif	/* MACH_SMMU_H */
