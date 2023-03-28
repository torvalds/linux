/* SPDX-License-Identifier:  GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_S390_UAPI_FS3270_H
#define __ASM_S390_UAPI_FS3270_H

#include <linux/types.h>
#include <asm/ioctl.h>

/* ioctls for fullscreen 3270 */
#define TUBICMD		_IO('3', 3)	/* set ccw command for fs reads. */
#define TUBOCMD		_IO('3', 4)	/* set ccw command for fs writes. */
#define TUBGETI		_IO('3', 7)	/* get ccw command for fs reads. */
#define TUBGETO		_IO('3', 8)	/* get ccw command for fs writes. */
#define TUBGETMOD	_IO('3', 13)	/* get characteristics like model, cols, rows */

/* For TUBGETMOD */
struct raw3270_iocb {
	__u16 model;
	__u16 line_cnt;
	__u16 col_cnt;
	__u16 pf_cnt;
	__u16 re_cnt;
	__u16 map;
};

#endif /* __ASM_S390_UAPI_FS3270_H */
