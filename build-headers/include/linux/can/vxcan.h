/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
#ifndef _CAN_VXCAN_H
#define _CAN_VXCAN_H

enum {
	VXCAN_INFO_UNSPEC,
	VXCAN_INFO_PEER,

	__VXCAN_INFO_MAX
#define VXCAN_INFO_MAX	(__VXCAN_INFO_MAX - 1)
};

#endif
