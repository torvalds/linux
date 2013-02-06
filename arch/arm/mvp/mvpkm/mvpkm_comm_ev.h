/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Hypervisor Support
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
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
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 * @file
 *
 * @brief mvpkm kernel hooks for comm event signaling
 */

#ifndef _MVPKM_COMM_EV_H
#define _MVPKM_COMM_EV_H

extern int (*CommTranspEvProcess)(CommTranspID* id, CommTranspIOEvent event);

/**
 * @brief Forward any guest signal requests to the commkm module
 * @param id transport channel id
 * @param event comm event type
 */

static inline void
Mvpkm_CommEvSignal(CommTranspID *id, CommTranspIOEvent event)
{
   if (CommTranspEvProcess) {
      CommTranspEvProcess(id, event);
   }
}

void
Mvpkm_CommEvRegisterProcessCB(int (*commProcessFunc)(CommTranspID*,
                                                     CommTranspIOEvent));
void Mvpkm_CommEvUnregisterProcessCB(void);



#endif
