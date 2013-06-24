/*
 * exynos_tmu_data.h - Samsung EXYNOS tmu data header file
 *
 *  Copyright (C) 2013 Samsung Electronics
 *  Amit Daniel Kachhap <amit.daniel@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _EXYNOS_TMU_DATA_H
#define _EXYNOS_TMU_DATA_H

#if defined(CONFIG_CPU_EXYNOS4210)
extern struct exynos_tmu_platform_data const exynos4210_default_tmu_data;
#define EXYNOS4210_TMU_DRV_DATA (&exynos4210_default_tmu_data)
#else
#define EXYNOS4210_TMU_DRV_DATA (NULL)
#endif

#if (defined(CONFIG_SOC_EXYNOS5250) || defined(CONFIG_SOC_EXYNOS4412))
extern struct exynos_tmu_platform_data const exynos5250_default_tmu_data;
#define EXYNOS5250_TMU_DRV_DATA (&exynos5250_default_tmu_data)
#else
#define EXYNOS5250_TMU_DRV_DATA (NULL)
#endif

#endif /*_EXYNOS_TMU_DATA_H*/
