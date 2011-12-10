#ifndef _INC_PMC93X6_EEPROM_H_
#define _INC_PMC93X6_EEPROM_H_

/*-----------------------------------------------------------------------------
 * pmc93x6_eeprom.h -
 *
 * Copyright (C) 2002-2004  SBE, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 * For further information, contact via email: support@sbei.com
 * SBE, Inc.  San Ramon, California  U.S.A.
 *-----------------------------------------------------------------------------
 */

#include <linux/types.h>

#ifdef __KERNEL__

#include "pmcc4_private.h"

void        pmc_eeprom_read_buffer (long, long, char *, int);
void        pmc_eeprom_write_buffer (long, long, char *, int);
void        pmc_init_seeprom (u_int32_t, u_int32_t);
char        pmc_verify_cksum (void *);

#endif    /*** __KERNEL__ ***/

#endif

/*** End-of-File ***/
