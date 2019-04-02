#ifndef _IA64_KDE_H
#define _IA64_KDE_H 1
/*
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) Intel Corporation, 2005
 *
 * 2005-Apr     Rusty Lynch <rusty.lynch@intel.com> and Anil S Keshavamurthy
 *              <anil.s.keshavamurthy@intel.com> adopted from
 *              include/asm-x86_64/kde.h
 *
 * 2005-Oct	Keith Owens <kaos@sgi.com>.  Expand notify_die to cover more
 *		events.
 */

enum die_val {
	DIE_BREAK = 1,
	DIE_FAULT,
	DIE_OOPS,
	DIE_MACHINE_HALT,
	DIE_MACHINE_RESTART,
	DIE_MCA_MONARCH_ENTER,
	DIE_MCA_MONARCH_PROCESS,
	DIE_MCA_MONARCH_LEAVE,
	DIE_MCA_SLAVE_ENTER,
	DIE_MCA_SLAVE_PROCESS,
	DIE_MCA_SLAVE_LEAVE,
	DIE_MCA_RENDZVOUS_ENTER,
	DIE_MCA_RENDZVOUS_PROCESS,
	DIE_MCA_RENDZVOUS_LEAVE,
	DIE_MCA_NEW_TIMEOUT,
	DIE_INIT_ENTER,
	DIE_INIT_MONARCH_ENTER,
	DIE_INIT_MONARCH_PROCESS,
	DIE_INIT_MONARCH_LEAVE,
	DIE_INIT_SLAVE_ENTER,
	DIE_INIT_SLAVE_PROCESS,
	DIE_INIT_SLAVE_LEAVE,
	DIE_KDE_ENTER,
	DIE_KDE_LEAVE,
	DIE_KDUMP_ENTER,
	DIE_KDUMP_LEAVE,
};

#endif
