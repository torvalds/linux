/*
 * Device Mapper Uevent Support
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright IBM Corporation, 2007
 * 	Author: Mike Anderson <andmike@linux.vnet.ibm.com>
 */
#ifndef DM_UEVENT_H
#define DM_UEVENT_H

enum dm_uevent_type {
	DM_UEVENT_PATH_FAILED,
	DM_UEVENT_PATH_REINSTATED,
};

#ifdef CONFIG_DM_UEVENT

extern int dm_uevent_init(void);
extern void dm_uevent_exit(void);
extern void dm_send_uevents(struct list_head *events, struct kobject *kobj);
extern void dm_path_uevent(enum dm_uevent_type event_type,
			   struct dm_target *ti, const char *path,
			   unsigned nr_valid_paths);

#else

static inline int dm_uevent_init(void)
{
	return 0;
}
static inline void dm_uevent_exit(void)
{
}
static inline void dm_send_uevents(struct list_head *events,
				   struct kobject *kobj)
{
}
static inline void dm_path_uevent(enum dm_uevent_type event_type,
				  struct dm_target *ti, const char *path,
				  unsigned nr_valid_paths)
{
}

#endif	/* CONFIG_DM_UEVENT */

#endif	/* DM_UEVENT_H */
