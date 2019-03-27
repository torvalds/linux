/* GDB Notifications to Observers.
   Copyright 2003 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef OBSERVER_H
#define OBSERVER_H

struct observer;

/* normal_stop notifications.  */

typedef void (observer_normal_stop_ftype) (void);

extern struct observer *
  observer_attach_normal_stop (observer_normal_stop_ftype *f);
extern void observer_detach_normal_stop (struct observer *observer);
extern void observer_notify_normal_stop (void);

#endif /* OBSERVER_H */
