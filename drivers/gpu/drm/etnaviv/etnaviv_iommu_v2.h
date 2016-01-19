/*
 * Copyright (C) 2014 Christian Gmeiner <christian.gmeiner@gmail.com>
  *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ETNAVIV_IOMMU_V2_H__
#define __ETNAVIV_IOMMU_V2_H__

#include <linux/iommu.h>
struct etnaviv_gpu;

struct iommu_domain *etnaviv_iommu_v2_domain_alloc(struct etnaviv_gpu *gpu);

#endif /* __ETNAVIV_IOMMU_V2_H__ */
