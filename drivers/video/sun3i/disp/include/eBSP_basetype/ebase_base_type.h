/*
*********************************************************************************************************
*											        eBase
*
*
*
*						        (c) Copyright 2006-2010, holigun China
*											All	Rights Reserved
*
* File    	: 	ebase_basetype.h
* Date		:	2010-06-22
* By      	: 	holigun
* Version 	: 	V1.00
*********************************************************************************************************
*/


#ifndef _EBASE_BASETYPE_H_
#define _EBASE_BASETYPE_H_

#define ARM_GCC_COMPLIER

/*
#ifdef ARM_GCC_COMPLIER
typedef unsigned long long    u64;
#else
typedef unsigned __int64    u64;
#endif
typedef unsigned int        u32;
typedef unsigned short      u16;
typedef unsigned char       u8;


#ifdef ARM_GCC_COMPLIER
typedef signed long long    s64;
#else
typedef signed __int64      s64;
#endif
typedef signed int          s32;
typedef signed short        s16;
typedef signed char         s8;


#ifdef ARM_GCC_COMPLIER
typedef signed long long    __u64;
#else
typedef unsigned __int64    __u64;
#endif
typedef unsigned int        __u32;
typedef unsigned short      __u16;
typedef unsigned char       __u8;

#ifdef ARM_GCC_COMPLIER
typedef signed long long 	 __s64;
#else
typedef signed __int64      __s64;
#endif
typedef signed int          __s32;
typedef signed short        __s16;
typedef signed char         __s8;
*/
//typedef signed char         __bool;

typedef unsigned int        __stk;                  //Each stack entry is 32-bit wide
typedef unsigned int        __cpu_sr;               //Define size of CPU status register (PSR = 32 bits)*/


typedef float               __fp32;                 /* Single precision floating point  */
typedef double              __fp64;                 /* Double precision floating point  */

typedef unsigned int        __hdle;

typedef unsigned int        __size;
typedef unsigned int        __size_t;

typedef unsigned int        __sector_t;

typedef unsigned int        EBSP_CPSR_REG;


#ifndef NULL
#define NULL	0
#endif // NULL

/*
#define SZ_512       0x00000200U
#define SZ_1K        0x00000400U
#define SZ_2K        0x00000800U
#define SZ_4K        0x00001000U
#define SZ_8K        0x00002000U
#define SZ_16K       0x00004000U
#define SZ_32K       0x00008000U
#define SZ_64K       0x00010000U
#define SZ_128K      0x00020000U
#define SZ_256K      0x00040000U
#define SZ_512K      0x00080000U
#define SZ_1M        0x00100000U
#define SZ_2M        0x00200000U
#define SZ_4M        0x00400000U
#define SZ_8M        0x00800000U
#define SZ_16M       0x01000000U
#define SZ_32M       0x02000000U
#define SZ_64M       0x04000000U
#define SZ_128M      0x08000000U
#define SZ_256M      0x10000000U
#define SZ_512M      0x20000000U
#define SZ_1G        0x40000000U
#define SZ_2G        0x80000000U

#define SZ_4G        0x0100000000ULL
#define SZ_8G        0x0200000000ULL
#define SZ_16G       0x0400000000ULL
#define SZ_32G       0x0800000000ULL
#define SZ_64G       0x1000000000ULL
*/
#endif	//_EBASE_BASETYPE_H_

