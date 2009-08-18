/* MN10300 Setup declarations
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_SETUP_H
#define _ASM_SETUP_H

#ifdef __KERNEL__
extern void __init unit_setup(void);
extern void __init unit_init_IRQ(void);
#endif
#endif /* _ASM_SETUP_H */
