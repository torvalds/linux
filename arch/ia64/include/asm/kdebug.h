/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _IA64_KDEBUG_H
#define _IA64_KDEBUG_H 1
/*
 *
 * Copyright (C) Intel Corporation, 2005
 *
 * 2005-Apr     Rusty Lynch <rusty.lynch@intel.com> and Anil S Keshavamurthy
 *              <anil.s.keshavamurthy@intel.com> adopted from
 *              include/asm-x86_64/kdebug.h
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
	DIE_KDEBUG_ENTER,
	DIE_KDEBUG_LEAVE,
	DIE_KDUMP_ENTER,
	DIE_KDUMP_LEAVE,
};

#endif
