/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPIASM_SCHID_H
#define _UAPIASM_SCHID_H

#include <linux/types.h>

#ifndef __ASSEMBLY__

struct subchannel_id {
	__u32 cssid : 8;
	__u32 : 4;
	__u32 m : 1;
	__u32 ssid : 2;
	__u32 one : 1;
	__u32 sch_no : 16;
} __attribute__ ((packed, aligned(4)));

#endif /* __ASSEMBLY__ */

#endif /* _UAPIASM_SCHID_H */
