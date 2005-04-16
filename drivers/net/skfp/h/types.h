/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#include	<linux/types.h>
/*
	----------------------
	Basic SMT system types
	----------------------
*/
#ifndef _TYPES_
#define	_TYPES_

#define _packed
#ifndef far
#define far
#endif
#ifndef _far
#define _far
#endif

#define inp(p)  ioread8(p)
#define inpw(p)	ioread16(p)
#define inpd(p) ioread32(p)
#define outp(p,c)  iowrite8(c,p)
#define outpw(p,s) iowrite16(s,p)
#define outpd(p,l) iowrite32(l,p)

#endif	/* _TYPES_ */
