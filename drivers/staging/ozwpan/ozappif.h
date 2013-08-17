/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#ifndef _OZAPPIF_H
#define _OZAPPIF_H

#include "ozeventdef.h"

#define OZ_IOCTL_MAGIC	0xf4

struct oz_mac_addr {
	unsigned char a[6];
};

#define OZ_MAX_PDS	8

struct oz_pd_list {
	int count;
	struct oz_mac_addr addr[OZ_MAX_PDS];
};

#define OZ_MAX_BINDING_LEN	32

struct oz_binding_info {
	char name[OZ_MAX_BINDING_LEN];
};

struct oz_test {
	int action;
};

#define OZ_IOCTL_GET_PD_LIST	_IOR(OZ_IOCTL_MAGIC, 0, struct oz_pd_list)
#define OZ_IOCTL_SET_ACTIVE_PD	_IOW(OZ_IOCTL_MAGIC, 1, struct oz_mac_addr)
#define OZ_IOCTL_GET_ACTIVE_PD	_IOR(OZ_IOCTL_MAGIC, 2, struct oz_mac_addr)
#define OZ_IOCTL_CLEAR_EVENTS	_IO(OZ_IOCTL_MAGIC, 3)
#define OZ_IOCTL_GET_EVENTS	_IOR(OZ_IOCTL_MAGIC, 4, struct oz_evtlist)
#define OZ_IOCTL_ADD_BINDING	_IOW(OZ_IOCTL_MAGIC, 5, struct oz_binding_info)
#define OZ_IOCTL_TEST		_IOWR(OZ_IOCTL_MAGIC, 6, struct oz_test)
#define OZ_IOCTL_SET_EVENT_MASK	_IOW(OZ_IOCTL_MAGIC, 7, unsigned long)
#define OZ_IOCTL_REMOVE_BINDING	_IOW(OZ_IOCTL_MAGIC, 8, struct oz_binding_info)
#define OZ_IOCTL_MAX		9


#endif /* _OZAPPIF_H */
