/*
 * Copyright (C) 2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2009 PetaLogix
 * Copyright (C) 2007 LynuxWorks, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/errno.h>
#include <linux/linkage.h>

/*
 * int __strncpy_user(char *to, char *from, int len);
 *
 * Returns:
 *  -EFAULT  for an exception
 *  len      if we hit the buffer limit
 *  bytes copied
 */

	.text
.globl __strncpy_user;
.type  __strncpy_user, @function
.align 4;
__strncpy_user:

	/*
	 * r5 - to
	 * r6 - from
	 * r7 - len
	 * r3 - temp count
	 * r4 - temp val
	 */
	addik	r3,r7,0		/* temp_count = len */
	beqi	r3,3f
1:
	lbu	r4,r6,r0
	sb	r4,r5,r0

	addik	r3,r3,-1
	beqi	r3,2f		/* break on len */

	addik	r5,r5,1
	bneid	r4,1b
	addik	r6,r6,1		/* delay slot */
	addik	r3,r3,1		/* undo "temp_count--" */
2:
	rsubk	r3,r3,r7	/* temp_count = len - temp_count */
3:
	rtsd	r15,8
	nop
	.size   __strncpy_user, . - __strncpy_user

	.section	.fixup, "ax"
	.align	2
4:
	brid	3b
	addik	r3,r0, -EFAULT

	.section	__ex_table, "a"
	.word	1b,4b

/*
 * int __strnlen_user(char __user *str, int maxlen);
 *
 * Returns:
 *  0 on error
 *  maxlen + 1  if no NUL byte found within maxlen bytes
 *  size of the string (including NUL byte)
 */

	.text
.globl __strnlen_user;
.type  __strnlen_user, @function
.align 4;
__strnlen_user:
	addik	r3,r6,0
	beqi	r3,3f
1:
	lbu	r4,r5,r0
	beqid	r4,2f		/* break on NUL */
	addik	r3,r3,-1	/* delay slot */

	bneid	r3,1b
	addik	r5,r5,1		/* delay slot */

	addik	r3,r3,-1	/* for break on len */
2:
	rsubk	r3,r3,r6
3:
	rtsd	r15,8
	nop
	.size   __strnlen_user, . - __strnlen_user

	.section	.fixup,"ax"
4:
	brid	3b
	addk	r3,r0,r0

	.section	__ex_table,"a"
	.word	1b,4b

/*
 * int __copy_tofrom_user(char *to, char *from, int len)
 * Return:
 *   0 on success
 *   number of not copied bytes on error
 */
	.text
.globl __copy_tofrom_user;
.type  __copy_tofrom_user, @function
.align 4;
__copy_tofrom_user:
	/*
	 * r5 - to
	 * r6 - from
	 * r7, r3 - count
	 * r4 - tempval
	 */
	beqid	r7, 3f /* zero size is not likely */
	andi	r3, r7, 0x3 /* filter add count */
	bneid	r3, 4f /* if is odd value then byte copying */
	or	r3, r5, r6 /* find if is any to/from unaligned */
	andi	r3, r3, 0x3 /* mask unaligned */
	bneid	r3, 1f /* it is unaligned -> then jump */
	or	r3, r0, r0

/* at least one 4 byte copy */
5:	lw	r4, r6, r3
6:	sw	r4, r5, r3
	addik	r7, r7, -4
	bneid	r7, 5b
	addik	r3, r3, 4
	addik	r3, r7, 0
	rtsd	r15, 8
	nop
4:	or	r3, r0, r0
1:	lbu	r4,r6,r3
2:	sb	r4,r5,r3
	addik	r7,r7,-1
	bneid	r7,1b
	addik	r3,r3,1		/* delay slot */
3:
	addik	r3,r7,0
	rtsd	r15,8
	nop
	.size   __copy_tofrom_user, . - __copy_tofrom_user

	.section	__ex_table,"a"
	.word	1b,3b,2b,3b,5b,3b,6b,3b
