#ifndef _INC_SBEPROC_H_
#define _INC_SBEPROC_H_

/*-----------------------------------------------------------------------------
 * sbeproc.h -
 *
 * Copyright (C) 2004-2005  SBE, Inc.
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


#ifdef CONFIG_PROC_FS
void        sbecom_proc_brd_cleanup (ci_t *);
int __init  sbecom_proc_brd_init (ci_t *);

#else

static inline void sbecom_proc_brd_cleanup(ci_t *ci)
{
}

static inline int __init sbecom_proc_brd_init(ci_t *ci)
{
	return 0;
}

#endif                          /*** CONFIG_PROC_FS ***/

#endif                          /*** _INC_SBEPROC_H_ ***/
