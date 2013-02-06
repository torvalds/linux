/*
 * battery-factory.h - factory mode for battery driver
 *
 *  Copyright (C) 2011 Samsung Electrnoics
 * SangYoung Son <hello.son@samsung.com>
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

#include <linux/battery/samsung_battery.h>

#ifdef CONFIG_SYSFS
extern void battery_create_attrs(struct device *dev);

/* Add function prototype for supporting factory */
extern int battery_get_info(struct battery_info *info,
					enum power_supply_property property);
extern void battery_update_info(struct battery_info *info);
extern void battery_control_info(struct battery_info *info,
					enum power_supply_property property,
					int intval);
extern void battery_event_control(struct battery_info *info);
#endif /* CONFIG_SYSFS */

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC)
extern int battery_info_proc(char *buf, char **start,
			off_t offset, int count, int *eof, void *data);
#endif
