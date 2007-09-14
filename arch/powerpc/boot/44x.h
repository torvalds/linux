/*
 * PowerPC 44x related functions
 *
 * Copyright 2007 David Gibson, IBM Corporation.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef _PPC_BOOT_44X_H_
#define _PPC_BOOT_44X_H_

void ebony_init(void *mac0, void *mac1);
void bamboo_init(void *mac0, void *mac1);

#endif /* _PPC_BOOT_44X_H_ */
