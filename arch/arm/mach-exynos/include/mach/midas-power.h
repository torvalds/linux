/*
 * midas-power.h - Power Management of MIDAS Project
 *
 *  Copyright (C) 2011 Samsung Electrnoics
 *  Chiwoong Byun <woong.byun@samsung.com>
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
 */

#ifndef __MIDAS_POWER_H
#define __MIDAS_POWER_H __FILE__

#if defined(CONFIG_MFD_S5M_CORE)
extern struct s5m_platform_data exynos4_s5m8767_info;
#else
extern struct max77686_platform_data exynos4_max77686_info;
extern struct max8997_platform_data exynos4_max8997_info;
#endif

void midas_power_init(void);
void midas_power_set_muic_pdata(void *, int);
void midas_power_gpio_init(void);
#endif /* __MIDAS_POWER_H */
