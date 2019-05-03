/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/uaccess.h>

static inline int setup_vdso_page(unsigned short *ptr)
{
	int err = 0;

	/* movi r1, 127 */
	err |= __put_user(0x67f1, ptr + 0);
	/* addi r1, (139 - 127) */
	err |= __put_user(0x20b1, ptr + 1);
	/* trap 0 */
	err |= __put_user(0x0008, ptr + 2);

	return err;
}
