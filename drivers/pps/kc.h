/*
 * PPS kernel consumer API header
 *
 * Copyright (C) 2009-2010   Alexander Gordeev <lasaine@lvk.cs.msu.su>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef LINUX_PPS_KC_H
#define LINUX_PPS_KC_H

#include <linux/errno.h>
#include <linux/pps_kernel.h>

#ifdef CONFIG_NTP_PPS

extern int pps_kc_bind(struct pps_device *pps,
		struct pps_bind_args *bind_args);
extern void pps_kc_remove(struct pps_device *pps);
extern void pps_kc_event(struct pps_device *pps,
		struct pps_event_time *ts, int event);


#else /* CONFIG_NTP_PPS */

static inline int pps_kc_bind(struct pps_device *pps,
		struct pps_bind_args *bind_args) { return -EOPNOTSUPP; }
static inline void pps_kc_remove(struct pps_device *pps) {}
static inline void pps_kc_event(struct pps_device *pps,
		struct pps_event_time *ts, int event) {}

#endif /* CONFIG_NTP_PPS */

#endif /* LINUX_PPS_KC_H */
