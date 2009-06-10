/* MN10300 In-kernel death knells
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_KDEBUG_H
#define _ASM_KDEBUG_H

/* Grossly misnamed. */
enum die_val {
	DIE_OOPS = 1,
	DIE_BREAKPOINT,
	DIE_GPF,
};

#endif /* _ASM_KDEBUG_H */
