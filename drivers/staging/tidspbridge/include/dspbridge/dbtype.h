/*
 * dbtype.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * This header defines data types for DSP/BIOS Bridge APIs and device
 * driver modules. It also defines the Hungarian prefix to use for each
 * base type.
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef DBTYPE_
#define DBTYPE_

/*===========================================================================*/
/*  Argument specification syntax */
/*===========================================================================*/

#ifndef IN
#define IN			/* Following parameter is for input. */
#endif

#ifndef OUT
#define OUT			/* Following parameter is for output. */
#endif

#ifndef OPTIONAL
#define OPTIONAL	/* Function may optionally use previous parameter. */
#endif

#ifndef CONST
#define CONST   const
#endif

/*===========================================================================*/
/*  NULL character   (normally used for string termination) */
/*===========================================================================*/

#ifndef NULL_CHAR
#define NULL_CHAR    '\0'	/* Null character. */
#endif

/*===========================================================================*/
/*  Basic Type definitions (with Prefixes for Hungarian notation) */
/*===========================================================================*/

#ifndef OMAPBRIDGE_TYPES
#define OMAPBRIDGE_TYPES
typedef volatile unsigned short reg_uword16;
#endif

#define TEXT(x) x

#define DLLIMPORT
#define DLLEXPORT

/* Define DSPAPIDLL correctly in dspapi.h */
#define _DSPSYSDLL32_

#endif /* DBTYPE_ */
