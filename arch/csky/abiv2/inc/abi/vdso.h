/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ABI_CSKY_VDSO_H
#define __ABI_CSKY_VDSO_H

#include <linux/uaccess.h>

static inline int setup_vdso_page(unsigned short *ptr)
{
	int err = 0;

	/* movi r7, 173 */
	err |= __put_user(0xea07, ptr);
	err |= __put_user(0x008b,      ptr+1);

	/* trap 0 */
	err |= __put_user(0xc000,   ptr+2);
	err |= __put_user(0x2020,   ptr+3);

	return err;
}

#endif /* __ABI_CSKY_STRING_H */
