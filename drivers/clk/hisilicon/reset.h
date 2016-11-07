/*
 * Copyright (c) 2015 HiSilicon Technologies Co., Ltd.
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef	__HISI_RESET_H
#define	__HISI_RESET_H

struct device_node;
struct hisi_reset_controller;

#ifdef CONFIG_RESET_CONTROLLER
struct hisi_reset_controller *hisi_reset_init(struct platform_device *pdev);
void hisi_reset_exit(struct hisi_reset_controller *rstc);
#else
static inline
struct hisi_reset_controller *hisi_reset_init(struct platform_device *pdev)
{
	return 0;
}
static inline void hisi_reset_exit(struct hisi_reset_controller *rstc)
{}
#endif

#endif	/* __HISI_RESET_H */
