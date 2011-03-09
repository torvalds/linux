/*
 * $Id: sbeproc.h,v 1.2 2005/10/17 23:55:28 rickd PMCC4_3_1B $
 */

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
 * RCS info:
 * RCS revision: $Revision: 1.2 $
 * Last changed on $Date: 2005/10/17 23:55:28 $
 * Changed by $Author: rickd $
 *-----------------------------------------------------------------------------
 * $Log: sbeproc.h,v $
 * Revision 1.2  2005/10/17 23:55:28  rickd
 * sbecom_proc_brd_init() is an declared an __init function.
 *
 * Revision 1.1  2005/09/28 00:10:09  rickd
 * Remove unneeded inclusion of c4_private.h.
 *
 * Revision 1.0  2005/05/10 22:21:46  rickd
 * Initial check-in.
 *
 *-----------------------------------------------------------------------------
 */


#ifdef CONFIG_PROC_FS
#ifdef __KERNEL__
void        sbecom_proc_brd_cleanup (ci_t *);
int __init  sbecom_proc_brd_init (ci_t *);

#endif                          /*** __KERNEL__ ***/
#endif                          /*** CONFIG_PROC_FS ***/
#endif                          /*** _INC_SBEPROC_H_ ***/
