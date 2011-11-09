/*
 * Copyright (C) 2010-2012 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/module.h>

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Joerg Roedel <joerg.roedel@amd.com>");

static int __init amd_iommu_v2_init(void)
{
	pr_info("AMD IOMMUv2 driver by Joerg Roedel <joerg.roedel@amd.com>");

	return 0;
}

static void __exit amd_iommu_v2_exit(void)
{
}

module_init(amd_iommu_v2_init);
module_exit(amd_iommu_v2_exit);
