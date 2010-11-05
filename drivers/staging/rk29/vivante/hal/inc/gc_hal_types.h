/****************************************************************************
*
*    Copyright (C) 2005 - 2010 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/




#ifndef __gc_hal_types_h_
#define __gc_hal_types_h_

#include "gc_hal_options.h"

#ifdef _WIN32
#pragma warning(disable:4127)	/* Conditional expression is constant (do { }
								** while(0)). */
#pragma warning(disable:4100)	/* Unreferenced formal parameter. */
#pragma warning(disable:4204)	/* Non-constant aggregate initializer (C99). */
#pragma warning(disable:4131)	/* Uses old-style declarator (for Bison and
								** Flex generated files). */
#pragma warning(disable:4206)	/* Translation unit is empty. */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
**	Platform macros.
*/

#if defined(__GNUC__)
#	define gcdHAS_ELLIPSES		1		/* GCC always has it. */
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#	define gcdHAS_ELLIPSES		1		/* C99 has it. */
#elif defined(_MSC_VER) && (_MSC_VER >= 1500)
#	define gcdHAS_ELLIPSES		1		/* MSVC 2007+ has it. */
#elif defined(UNDER_CE)
#	define gcdHAS_ELLIPSES		0		/* Windows CE doesn't have it. */
#else
#   error "gcdHAS_ELLIPSES: Platform could not be determined"
#endif

/******************************************************************************\
************************************ Keyword ***********************************
\******************************************************************************/

#if (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L))
#	define gcmINLINE			inline		/* C99 keyword. */
#elif defined(__GNUC__)
#	define gcmINLINE			__inline__	/* GNU keyword. */
#elif defined(_MSC_VER) || defined(UNDER_CE)
#	define gcmINLINE			__inline	/* Internal keyword. */
#else
#   error "gcmINLINE: Platform could not be determined"
#endif

#ifndef gcdDEBUG
#	if (defined(DBG) && DBG) || defined(DEBUG) || defined(_DEBUG)
#		define gcdDEBUG			1
#	else
#		define gcdDEBUG			0
#	endif
#endif

#ifdef _USRDLL
#	ifdef _MSC_VER
#		ifdef HAL_EXPORTS
#			define HALAPI		__declspec(dllexport)
#		else
#			define HALAPI		__declspec(dllimport)
#		endif
#		define HALDECL			__cdecl
#	else
#		ifdef HAL_EXPORTS
#			define HALAPI
#		else
#			define HALAPI		extern
#		endif
#	endif
#else
#	define HALAPI
#	define HALDECL
#endif

/******************************************************************************\
********************************** Common Types ********************************
\******************************************************************************/

#define gcvFALSE				0
#define gcvTRUE					1

#define gcvINFINITE				((gctUINT32) ~0U)

typedef int						gctBOOL;
typedef gctBOOL *				gctBOOL_PTR;

typedef int						gctINT;
typedef signed char				gctINT8;
typedef signed short			gctINT16;
typedef signed int				gctINT32;
typedef signed long long		gctINT64;

typedef gctINT *				gctINT_PTR;
typedef gctINT8 *				gctINT8_PTR;
typedef gctINT16 *				gctINT16_PTR;
typedef gctINT32 *				gctINT32_PTR;
typedef gctINT64 *				gctINT64_PTR;

typedef unsigned int			gctUINT;
typedef unsigned char			gctUINT8;
typedef unsigned short			gctUINT16;
typedef unsigned int			gctUINT32;
typedef unsigned long long		gctUINT64;

typedef gctUINT *				gctUINT_PTR;
typedef gctUINT8 *				gctUINT8_PTR;
typedef gctUINT16 *				gctUINT16_PTR;
typedef gctUINT32 *				gctUINT32_PTR;
typedef gctUINT64 *				gctUINT64_PTR;

typedef unsigned long			gctSIZE_T;
typedef gctSIZE_T *				gctSIZE_T_PTR;

#ifdef __cplusplus
#	define gcvNULL				0
#else
#	define gcvNULL				((void *) 0)
#endif

typedef float					gctFLOAT;
typedef signed int				gctFIXED_POINT;
typedef float *					gctFLOAT_PTR;

typedef void *					gctPHYS_ADDR;
typedef void *					gctHANDLE;
typedef void *					gctFILE;
typedef void *					gctSIGNAL;
typedef void *					gctWINDOW;
typedef void *					gctIMAGE;

typedef void *					gctPOINTER;
typedef const void *			gctCONST_POINTER;

typedef char					gctCHAR;
typedef char *					gctSTRING;
typedef const char *			gctCONST_STRING;

typedef struct _gcsCOUNT_STRING
{
	gctSIZE_T					Length;
	gctCONST_STRING				String;
}
gcsCOUNT_STRING;

/* Fixed point constants. */
#define gcvZERO_X				((gctFIXED_POINT) 0x00000000)
#define gcvHALF_X				((gctFIXED_POINT) 0x00008000)
#define gcvONE_X				((gctFIXED_POINT) 0x00010000)
#define gcvNEGONE_X				((gctFIXED_POINT) 0xFFFF0000)
#define gcvTWO_X				((gctFIXED_POINT) 0x00020000)

/******************************************************************************\
******************************* Fixed Point Math *******************************
\******************************************************************************/

#define gcmXMultiply(x1, x2) \
	(gctFIXED_POINT) (((gctINT64) (x1) * (x2)) >> 16)

#define gcmXDivide(x1, x2) \
	(gctFIXED_POINT) ((((gctINT64) (x1)) << 16) / (x2))

#define gcmXMultiplyDivide(x1, x2, x3) \
	(gctFIXED_POINT) ((gctINT64) (x1) * (x2) / (x3))

/* 2D Engine profile. */
struct gcs2D_PROFILE
{
	/* Cycle count.
	   32bit counter incremented every 2D clock cycle.
	   Wraps back to 0 when the counter overflows.
	*/
	gctUINT32 cycleCount;

	/* Pixels rendered by the 2D engine.
	   Resets to 0 every time it is read. */
	gctUINT32 pixelsRendered;
};


/* Macro to combine four characters into a Charcater Code. */
#define gcmCC(c1, c2, c3, c4) \
( \
	(char) (c1) \
	| \
	((char) (c2) <<  8) \
	| \
	((char) (c3) << 16) \
	| \
	((char) (c4) << 24) \
)

#define gcmPRINTABLE(c)			((((c) >= ' ') && ((c) <= '}')) ? (c) : ' ')

#define gcmCC_PRINT(cc) \
	gcmPRINTABLE((char) ( (cc)        & 0xFF)), \
	gcmPRINTABLE((char) (((cc) >>  8) & 0xFF)), \
	gcmPRINTABLE((char) (((cc) >> 16) & 0xFF)), \
	gcmPRINTABLE((char) (((cc) >> 24) & 0xFF))

/******************************************************************************\
****************************** Function Parameters *****************************
\******************************************************************************/

#define IN
#define OUT
#define OPTIONAL

/******************************************************************************\
********************************* Status Codes *********************************
\******************************************************************************/

typedef enum _gceSTATUS
{
	gcvSTATUS_OK					= 	0,
	gcvSTATUS_FALSE					= 	0,
	gcvSTATUS_TRUE					= 	1,
	gcvSTATUS_NO_MORE_DATA 			= 	2,
	gcvSTATUS_CACHED				= 	3,
	gcvSTATUS_MIPMAP_TOO_LARGE		= 	4,
	gcvSTATUS_NAME_NOT_FOUND		=	5,
	gcvSTATUS_NOT_OUR_INTERRUPT		=	6,
	gcvSTATUS_MISMATCH				=	7,
	gcvSTATUS_MIPMAP_TOO_SMALL		=	8,
	gcvSTATUS_LARGER				=	9,
	gcvSTATUS_SMALLER				=	10,
	gcvSTATUS_CHIP_NOT_READY		= 	11,
	gcvSTATUS_NEED_CONVERSION		=	12,
	gcvSTATUS_SKIP					=	13,
	gcvSTATUS_DATA_TOO_LARGE		=	14,
	gcvSTATUS_INVALID_CONFIG		=	15,
	gcvSTATUS_CHANGED				=	16,

	gcvSTATUS_INVALID_ARGUMENT		= 	-1,
	gcvSTATUS_INVALID_OBJECT 		= 	-2,
	gcvSTATUS_OUT_OF_MEMORY 		= 	-3,
	gcvSTATUS_MEMORY_LOCKED			= 	-4,
	gcvSTATUS_MEMORY_UNLOCKED		= 	-5,
	gcvSTATUS_HEAP_CORRUPTED		= 	-6,
	gcvSTATUS_GENERIC_IO			= 	-7,
	gcvSTATUS_INVALID_ADDRESS		= 	-8,
	gcvSTATUS_CONTEXT_LOSSED		= 	-9,
	gcvSTATUS_TOO_COMPLEX			= 	-10,
	gcvSTATUS_BUFFER_TOO_SMALL		= 	-11,
	gcvSTATUS_INTERFACE_ERROR		= 	-12,
	gcvSTATUS_NOT_SUPPORTED			= 	-13,
	gcvSTATUS_MORE_DATA				= 	-14,
	gcvSTATUS_TIMEOUT				= 	-15,
	gcvSTATUS_OUT_OF_RESOURCES		= 	-16,
	gcvSTATUS_INVALID_DATA			= 	-17,
	gcvSTATUS_INVALID_MIPMAP		= 	-18,
	gcvSTATUS_NOT_FOUND				=	-19,
	gcvSTATUS_NOT_ALIGNED			=	-20,
	gcvSTATUS_INVALID_REQUEST		=	-21,
	gcvSTATUS_GPU_NOT_RESPONDING	=	-22,

	/* Linker errors. */
	gcvSTATUS_GLOBAL_TYPE_MISMATCH	=	-1000,
	gcvSTATUS_TOO_MANY_ATTRIBUTES	=	-1001,
	gcvSTATUS_TOO_MANY_UNIFORMS		=	-1002,
	gcvSTATUS_TOO_MANY_VARYINGS		=	-1003,
	gcvSTATUS_UNDECLARED_VARYING	=	-1004,
	gcvSTATUS_VARYING_TYPE_MISMATCH	=	-1005,
	gcvSTATUS_MISSING_MAIN			=	-1006,
	gcvSTATUS_NAME_MISMATCH			=	-1007,
	gcvSTATUS_INVALID_INDEX			=	-1008,
}
gceSTATUS;

/******************************************************************************\
********************************* Status Macros ********************************
\******************************************************************************/

#define gcmIS_ERROR(status)			(status < 0)
#define gcmNO_ERROR(status)			(status >= 0)
#define gcmIS_SUCCESS(status)		(status == gcvSTATUS_OK)

/******************************************************************************\
********************************* Field Macros *********************************
\******************************************************************************/

#define __gcmSTART(reg_field) \
	(0 ? reg_field)

#define __gcmEND(reg_field) \
	(1 ? reg_field)

#define __gcmGETSIZE(reg_field) \
	(__gcmEND(reg_field) - __gcmSTART(reg_field) + 1)

#define __gcmALIGN(data, reg_field) \
	(((gctUINT32) (data)) << __gcmSTART(reg_field))

#define __gcmMASK(reg_field) \
	((gctUINT32) ((__gcmGETSIZE(reg_field) == 32) \
		?  ~0 \
		: (~(~0 << __gcmGETSIZE(reg_field)))))

/*******************************************************************************
**
**	gcmFIELDMASK
**
**		Get aligned field mask.
**
**	ARGUMENTS:
**
**		reg		Name of register.
**		field	Name of field within register.
*/
#define gcmFIELDMASK(reg, field) \
( \
	__gcmALIGN(__gcmMASK(reg##_##field), reg##_##field) \
)

/*******************************************************************************
**
**	gcmGETFIELD
**
**		Extract the value of a field from specified data.
**
**	ARGUMENTS:
**
**		data	Data value.
**		reg		Name of register.
**		field	Name of field within register.
*/
#define gcmGETFIELD(data, reg, field) \
( \
	((((gctUINT32) (data)) >> __gcmSTART(reg##_##field)) \
		& __gcmMASK(reg##_##field)) \
)

/*******************************************************************************
**
**	gcmSETFIELD
**
**		Set the value of a field within specified data.
**
**	ARGUMENTS:
**
**		data	Data value.
**		reg		Name of register.
**		field	Name of field within register.
**		value	Value for field.
*/
#define gcmSETFIELD(data, reg, field, value) \
( \
	(((gctUINT32) (data)) \
		& ~__gcmALIGN(__gcmMASK(reg##_##field), reg##_##field)) \
		|  __gcmALIGN((gctUINT32) (value) \
			& __gcmMASK(reg##_##field), reg##_##field) \
)

/*******************************************************************************
**
**	gcmSETFIELDVALUE
**
**		Set the value of a field within specified data with a
**		predefined value.
**
**	ARGUMENTS:
**
**		data	Data value.
**		reg		Name of register.
**		field	Name of field within register.
**		value	Name of the value within the field.
*/
#define gcmSETFIELDVALUE(data, reg, field, value) \
( \
	(((gctUINT32) (data)) \
		& ~__gcmALIGN(__gcmMASK(reg##_##field), reg##_##field)) \
		|  __gcmALIGN(reg##_##field##_##value \
			& __gcmMASK(reg##_##field), reg##_##field) \
)

/*******************************************************************************
**
**	gcmSETMASKEDFIELD
**
**		Set the value of a masked field with specified data.
**
**	ARGUMENTS:
**
**		reg		Name of register.
**		field	Name of field within register.
**		value	Value for field.
*/
#define gcmSETMASKEDFIELD(reg, field, value) \
( \
	gcmSETFIELD(~0, reg, field, value) & \
	gcmSETFIELDVALUE(~0, reg, MASK_ ## field, ENABLED) \
)

/*******************************************************************************
**
**	gcmVERIFYFIELDVALUE
**
**		Verify if the value of a field within specified data equals a
**		predefined value.
**
**	ARGUMENTS:
**
**		data	Data value.
**		reg		Name of register.
**		field	Name of field within register.
**		value	Name of the value within the field.
*/
#define gcmVERIFYFIELDVALUE(data, reg, field, value) \
( \
	(((gctUINT32) (data)) >> __gcmSTART(reg##_##field) & \
							 __gcmMASK(reg##_##field)) \
		== \
	(reg##_##field##_##value & __gcmMASK(reg##_##field)) \
)

/*******************************************************************************
**	Bit field macros.
*/

#define __gcmSTARTBIT(Field) \
	( 1 ? Field )

#define __gcmBITSIZE(Field) \
	( 0 ? Field )

#define __gcmBITMASK(Field) \
( \
	(1 << __gcmBITSIZE(Field)) - 1 \
)

#define gcmGETBITS(Value, Type, Field) \
( \
	( ((Type) (Value)) >> __gcmSTARTBIT(Field) ) \
	& \
	__gcmBITMASK(Field) \
)

#define gcmSETBITS(Value, Type, Field, NewValue) \
( \
	( ((Type) (Value)) \
	& ~(__gcmBITMASK(Field) << __gcmSTARTBIT(Field)) \
	) \
	| \
	( ( ((Type) (NewValue)) \
	  & __gcmBITMASK(Field) \
	  ) << __gcmSTARTBIT(Field) \
	) \
)

/******************************************************************************\
******************************** Min/Max Macros ********************************
\******************************************************************************/

#define gcmMIN(x, y)			(((x) <= (y)) ?  (x) :  (y))
#define gcmMAX(x, y)			(((x) >= (y)) ?  (x) :  (y))
#define gcmCLAMP(x, min, max)	(((x) < (min)) ? (min) : \
								 ((x) > (max)) ? (max) : (x))
#define gcmABS(x)				(((x) < 0)    ? -(x) :  (x))
#define gcmNEG(x)				(((x) < 0)    ?  (x) : -(x))

/*******************************************************************************
**
**	gcmPTR2INT
**
**		Convert a pointer to an integer value.
**
**	ARGUMENTS:
**
**		p		Pointer value.
*/
#if defined(_WIN32) || (defined(__LP64__) && __LP64__)
#	define gcmPTR2INT(p) \
	( \
		(gctUINT32) (gctUINT64) (p) \
	)
#else
# 	define gcmPTR2INT(p) \
	( \
		(gctUINT32) (p) \
	)
#endif

/*******************************************************************************
**
**	gcmINT2PTR
**
**		Convert an integer value into a pointer.
**
**	ARGUMENTS:
**
**		v		Integer value.
*/
#define gcmINT2PTR(i) \
( \
	(gctPOINTER) (i) \
)

/*******************************************************************************
**
**	gcmOFFSETOF
**
**		Compute the byte offset of a field inside a structure.
**
**	ARGUMENTS:
**
**		s		Structure name.
**		field	Field name.
*/
#define gcmOFFSETOF(s, field) \
( \
	gcmPTR2INT(& (((struct s *) 0)->field)) \
)

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_types_h_ */

