#ifndef _PM_TYPES_H
#define _PM_TYPES_H

/*
 * Copyright (c) 2011-2015 yanggq.young@allwinnertech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
 
#ifndef __uxx_sxx_name
#define __uxx_sxx_name
typedef signed char         __s8;
typedef unsigned char       __u8;
typedef signed short        __s16;
typedef unsigned short      __u16;
typedef signed int          __s32;
typedef unsigned int        __u32;
typedef signed long long    __s64;
typedef unsigned long long  __u64;

typedef unsigned int		size_t;
//typedef unsigned int		ptrdiff_t;
//------------------------------------------------------------------------------
//return value defines
//------------------------------------------------------------------------------
#define	OK		(0)
#define	FAIL	(-1)
#define TRUE	(1)
#define	FALSE	(0)


#endif 

#endif /*_PM_TYPES_H*/