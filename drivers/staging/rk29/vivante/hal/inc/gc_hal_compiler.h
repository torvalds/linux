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




/*
**	Include file the defines the front- and back-end compilers, as well as the
**	objects they use.
*/

#ifndef __gc_hal_compiler_h_
#define __gc_hal_compiler_h_

#include "gc_hal_types.h"
#include "gc_hal_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
|******************************* SHADER LANGUAGE ******************************|
\******************************************************************************/

/* Possible shader language opcodes. */
typedef enum _gcSL_OPCODE
{
	gcSL_NOP,							/* 0x00 */
	gcSL_MOV,							/* 0x01 */
	gcSL_SAT,							/* 0x02 */
	gcSL_DP3,							/* 0x03 */
	gcSL_DP4,							/* 0x04 */
	gcSL_ABS,							/* 0x05 */
	gcSL_JMP,							/* 0x06 */
	gcSL_ADD,							/* 0x07 */
	gcSL_MUL,							/* 0x08 */
	gcSL_RCP,							/* 0x09 */
	gcSL_SUB,							/* 0x0A */
	gcSL_KILL,							/* 0x0B */
	gcSL_TEXLD,							/* 0x0C */
	gcSL_CALL,							/* 0x0D */
	gcSL_RET,							/* 0x0E */
	gcSL_NORM,							/* 0x0F */
	gcSL_MAX,							/* 0x10 */
	gcSL_MIN,							/* 0x11 */
	gcSL_POW,							/* 0x12 */
	gcSL_RSQ,							/* 0x13 */
	gcSL_LOG,							/* 0x14 */
	gcSL_FRAC,							/* 0x15 */
	gcSL_FLOOR,							/* 0x16 */
	gcSL_CEIL,							/* 0x17 */
	gcSL_CROSS,							/* 0x18 */
	gcSL_TEXLDP,						/* 0x19 */
	gcSL_TEXBIAS,						/* 0x1A */
	gcSL_TEXGRAD,						/* 0x1B */
	gcSL_TEXLOD,						/* 0x1C */
	gcSL_SIN,							/* 0x1D */
	gcSL_COS,							/* 0x1E */
	gcSL_TAN,							/* 0x1F */
	gcSL_EXP,							/* 0x20 */
	gcSL_SIGN,							/* 0x21 */
	gcSL_STEP,							/* 0x22 */
	gcSL_SQRT,							/* 0x23 */
	gcSL_ACOS,							/* 0x24 */
	gcSL_ASIN,							/* 0x25 */
	gcSL_ATAN,							/* 0x26 */
	gcSL_SET,							/* 0x27 */
	gcSL_DSX,							/* 0x28 */
	gcSL_DSY,							/* 0x29 */
	gcSL_FWIDTH,						/* 0x2A */
}
gcSL_OPCODE;

typedef enum _gcSL_FORMAT
{
	gcSL_FLOAT,							/* 0 */
	gcSL_INTEGER,						/* 1 */
	gcSL_BOOLEAN,						/* 2 */
}
gcSL_FORMAT;

/* Destination write enable bits. */
typedef enum _gcSL_ENABLE
{
	gcSL_ENABLE_X						= 0x1,
	gcSL_ENABLE_Y						= 0x2,
	gcSL_ENABLE_Z						= 0x4,
	gcSL_ENABLE_W						= 0x8,
	/* Combinations. */
	gcSL_ENABLE_XY						= gcSL_ENABLE_X | gcSL_ENABLE_Y,
	gcSL_ENABLE_XYZ						= gcSL_ENABLE_X | gcSL_ENABLE_Y | gcSL_ENABLE_Z,
	gcSL_ENABLE_XYZW					= gcSL_ENABLE_X | gcSL_ENABLE_Y | gcSL_ENABLE_Z | gcSL_ENABLE_W,
	gcSL_ENABLE_XYW						= gcSL_ENABLE_X | gcSL_ENABLE_Y | gcSL_ENABLE_W,
	gcSL_ENABLE_XZ						= gcSL_ENABLE_X | gcSL_ENABLE_Z,
	gcSL_ENABLE_XZW						= gcSL_ENABLE_X | gcSL_ENABLE_Z | gcSL_ENABLE_W,
	gcSL_ENABLE_XW						= gcSL_ENABLE_X | gcSL_ENABLE_W,
	gcSL_ENABLE_YZ						= gcSL_ENABLE_Y | gcSL_ENABLE_Z,
	gcSL_ENABLE_YZW						= gcSL_ENABLE_Y | gcSL_ENABLE_Z | gcSL_ENABLE_W,
	gcSL_ENABLE_YW						= gcSL_ENABLE_Y | gcSL_ENABLE_W,
	gcSL_ENABLE_ZW						= gcSL_ENABLE_Z | gcSL_ENABLE_W,
}
gcSL_ENABLE;

/* Possible indices. */
typedef enum _gcSL_INDEXED
{
	gcSL_NOT_INDEXED,					/* 0 */
	gcSL_INDEXED_X,						/* 1 */
	gcSL_INDEXED_Y,						/* 2 */
	gcSL_INDEXED_Z,						/* 3 */
	gcSL_INDEXED_W,						/* 4 */
}
gcSL_INDEXED;

/* Opcode conditions. */
typedef enum _gcSL_CONDITION
{
	gcSL_ALWAYS,						/* 0x0 */
	gcSL_NOT_EQUAL,						/* 0x1 */
	gcSL_LESS_OR_EQUAL,					/* 0x2 */
	gcSL_LESS,							/* 0x3 */
	gcSL_EQUAL,							/* 0x4 */
	gcSL_GREATER,						/* 0x5 */
	gcSL_GREATER_OR_EQUAL,				/* 0x6 */
	gcSL_AND,							/* 0x7 */
	gcSL_OR,							/* 0x8 */
	gcSL_XOR,							/* 0x9 */
}
gcSL_CONDITION;

/* Possible source operand types. */
typedef enum _gcSL_TYPE
{
	gcSL_NONE,							/* 0x0 */
	gcSL_TEMP,							/* 0x1 */
	gcSL_ATTRIBUTE,						/* 0x2 */
	gcSL_UNIFORM,						/* 0x3 */
	gcSL_SAMPLER,						/* 0x4 */
	gcSL_CONSTANT,						/* 0x5 */
	gcSL_OUTPUT,						/* 0x6 */
	gcSL_PHYSICAL,						/* 0x7 */
}
gcSL_TYPE;

/* Swizzle generator macro. */
#define gcmSWIZZLE(Component1, Component2, Component3, Component4) \
( \
	(gcSL_SWIZZLE_ ## Component1 << 0) | \
	(gcSL_SWIZZLE_ ## Component2 << 2) | \
	(gcSL_SWIZZLE_ ## Component3 << 4) | \
	(gcSL_SWIZZLE_ ## Component4 << 6)   \
)

/* Possible swizzle values. */
typedef enum _gcSL_SWIZZLE
{
	gcSL_SWIZZLE_X,						/* 0x0 */
	gcSL_SWIZZLE_Y,						/* 0x1 */
	gcSL_SWIZZLE_Z,						/* 0x2 */
	gcSL_SWIZZLE_W,						/* 0x3 */
	/* Combinations. */
	gcSL_SWIZZLE_XXXX = gcmSWIZZLE(X, X, X, X),
	gcSL_SWIZZLE_YYYY = gcmSWIZZLE(Y, Y, Y, Y),
	gcSL_SWIZZLE_ZZZZ = gcmSWIZZLE(Z, Z, Z, Z),
	gcSL_SWIZZLE_WWWW = gcmSWIZZLE(W, W, W, W),
	gcSL_SWIZZLE_XYYY = gcmSWIZZLE(X, Y, Y, Y),
	gcSL_SWIZZLE_XZZZ = gcmSWIZZLE(X, Z, Z, Z),
	gcSL_SWIZZLE_XWWW = gcmSWIZZLE(X, W, W, W),
	gcSL_SWIZZLE_YZZZ = gcmSWIZZLE(Y, Z, Z, Z),
	gcSL_SWIZZLE_YWWW = gcmSWIZZLE(Y, W, W, W),
	gcSL_SWIZZLE_ZWWW = gcmSWIZZLE(Z, W, W, W),
	gcSL_SWIZZLE_XYZZ = gcmSWIZZLE(X, Y, Z, Z),
	gcSL_SWIZZLE_XYWW = gcmSWIZZLE(X, Y, W, W),
	gcSL_SWIZZLE_XZWW = gcmSWIZZLE(X, Z, W, W),
	gcSL_SWIZZLE_YZWW = gcmSWIZZLE(Y, Z, W, W),
	gcSL_SWIZZLE_XXYZ = gcmSWIZZLE(X, X, Y, Z),
	gcSL_SWIZZLE_XYZW = gcmSWIZZLE(X, Y, Z, W),
	gcSL_SWIZZLE_XYXY = gcmSWIZZLE(X, Y, X, Y),
}
gcSL_SWIZZLE;


/******************************************************************************\
|*********************************** SHADERS **********************************|
\******************************************************************************/

/* Shader types. */
#define gcSHADER_TYPE_UNKNOWN			0
#define gcSHADER_TYPE_VERTEX			1
#define gcSHADER_TYPE_FRAGMENT			2

/* gcSHADER objects. */
typedef struct _gcSHADER *				gcSHADER;
typedef struct _gcATTRIBUTE *			gcATTRIBUTE;
typedef struct _gcUNIFORM *				gcUNIFORM;
typedef struct _gcOUTPUT *				gcOUTPUT;
typedef struct _gcsFUNCTION *			gcFUNCTION;
typedef struct _gcsHINT *				gcsHINT_PTR;
typedef struct _gcSHADER_PROFILER *     gcSHADER_PROFILER;
typedef struct _gcVARIABLE *			gcVARIABLE;

/* gcSHADER_TYPE enumeration. */
typedef enum _gcSHADER_TYPE
{
	gcSHADER_FLOAT_X1,					/* 0x00 */
	gcSHADER_FLOAT_X2,					/* 0x01 */
	gcSHADER_FLOAT_X3,					/* 0x02 */
	gcSHADER_FLOAT_X4,					/* 0x03 */
	gcSHADER_FLOAT_2X2,					/* 0x04 */
	gcSHADER_FLOAT_3X3,					/* 0x05 */
	gcSHADER_FLOAT_4X4,					/* 0x06 */
	gcSHADER_BOOLEAN_X1,				/* 0x07 */
	gcSHADER_BOOLEAN_X2,				/* 0x08 */
	gcSHADER_BOOLEAN_X3,				/* 0x09 */
	gcSHADER_BOOLEAN_X4,				/* 0x0A */
	gcSHADER_INTEGER_X1,				/* 0x0B */
	gcSHADER_INTEGER_X2,				/* 0x0C */
	gcSHADER_INTEGER_X3,				/* 0x0D */
	gcSHADER_INTEGER_X4,				/* 0x0E */
	gcSHADER_SAMPLER_1D,				/* 0x0F */
	gcSHADER_SAMPLER_2D,				/* 0x10 */
	gcSHADER_SAMPLER_3D,				/* 0x11 */
	gcSHADER_SAMPLER_CUBIC,				/* 0x12 */
	gcSHADER_FIXED_X1,					/* 0x13 */
	gcSHADER_FIXED_X2,					/* 0x14 */
	gcSHADER_FIXED_X3,					/* 0x15 */
	gcSHADER_FIXED_X4,					/* 0x16 */
}
gcSHADER_TYPE;

/* Shader flags. */
typedef enum _gceSHADER_FLAGS
{
	gcvSHADER_DEAD_CODE					= 0x01,
	gcvSHADER_RESOURCE_USAGE			= 0x02,
	gcvSHADER_OPTIMIZER					= 0x04,
	gcvSHADER_USE_GL_Z					= 0x08,
	gcvSHADER_USE_GL_POSITION			= 0x10,
	gcvSHADER_USE_GL_FACE				= 0x20,
	gcvSHADER_USE_GL_POINT_COORD		= 0x40,
}
gceSHADER_FLAGS;

/* Function argument qualifier */
typedef enum _gceINPUT_OUTPUT
{
	gcvFUNCTION_INPUT,
	gcvFUNCTION_OUTPUT,
	gcvFUNCTION_INOUT
}
gceINPUT_OUTPUT;

/*******************************************************************************
**                             gcSHADER_Construct
********************************************************************************
**
**	Construct a new gcSHADER object.
**
**	INPUT:
**
**		gcoOS Hal
**			Pointer to an gcoHAL object.
**
**		gctINT ShaderType
**			Type of gcSHADER object to cerate.  'ShaderType' can be one of the
**			following:
**
**				gcSHADER_TYPE_VERTEX	Vertex shader.
**				gcSHADER_TYPE_FRAGMENT	Fragment shader.
**
**	OUTPUT:
**
**		gcSHADER * Shader
**			Pointer to a variable receiving the gcSHADER object pointer.
*/
gceSTATUS
gcSHADER_Construct(
	IN gcoHAL Hal,
	IN gctINT ShaderType,
	OUT gcSHADER * Shader
	);

/*******************************************************************************
**                              gcSHADER_Destroy
********************************************************************************
**
**	Destroy a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_Destroy(
	IN gcSHADER Shader
	);

/*******************************************************************************
**                                gcSHADER_Load
********************************************************************************
**
**	Load a gcSHADER object from a binary buffer.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctPOINTER Buffer
**			Pointer to a binary buffer containg the shader data to load.
**
**		gctSIZE_T BufferSize
**			Number of bytes inside the binary buffer pointed to by 'Buffer'.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_Load(
	IN gcSHADER Shader,
	IN gctPOINTER Buffer,
	IN gctSIZE_T BufferSize
	);

/*******************************************************************************
**                                gcSHADER_Save
********************************************************************************
**
**	Save a gcSHADER object to a binary buffer.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctPOINTER Buffer
**			Pointer to a binary buffer to be used as storage for the gcSHADER
**			object.  If 'Buffer' is gcvNULL, the gcSHADER object will not be saved,
**			but the number of bytes required to hold the binary output for the
**			gcSHADER object will be returned.
**
**		gctSIZE_T * BufferSize
**			Pointer to a variable holding the number of bytes allocated in
**			'Buffer'.  Only valid if 'Buffer' is not gcvNULL.
**
**	OUTPUT:
**
**		gctSIZE_T * BufferSize
**			Pointer to a variable receiving the number of bytes required to hold
**			the binary form of the gcSHADER object.
*/
gceSTATUS
gcSHADER_Save(
	IN gcSHADER Shader,
	IN gctPOINTER Buffer,
	IN OUT gctSIZE_T * BufferSize
	);

/*******************************************************************************
**							  gcSHADER_AddAttribute
********************************************************************************
**
**	Add an attribute to a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctCONST_STRING Name
**			Name of the attribute to add.
**
**		gcSHADER_TYPE Type
**			Type of the attribute to add.
**
**		gctSIZE_T Length
**			Array length of the attribute to add.  'Length' must be at least 1.
**
**		gctBOOL IsTexture
**			gcvTRUE if the attribute is used as a texture coordinate, gcvFALSE if not.
**
**	OUTPUT:
**
**		gcATTRIBUTE * Attribute
**			Pointer to a variable receiving the gcATTRIBUTE object pointer.
*/
gceSTATUS
gcSHADER_AddAttribute(
	IN gcSHADER Shader,
	IN gctCONST_STRING Name,
	IN gcSHADER_TYPE Type,
	IN gctSIZE_T Length,
	IN gctBOOL IsTexture,
	OUT gcATTRIBUTE * Attribute
	);

/*******************************************************************************
**                         gcSHADER_GetAttributeCount
********************************************************************************
**
**	Get the number of attributes for this shader.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**	OUTPUT:
**
**		gctSIZE_T * Count
**			Pointer to a variable receiving the number of attributes.
*/
gceSTATUS
gcSHADER_GetAttributeCount(
	IN gcSHADER Shader,
	OUT gctSIZE_T * Count
	);

/*******************************************************************************
**                            gcSHADER_GetAttribute
********************************************************************************
**
**	Get the gcATTRIBUTE object poniter for an indexed attribute for this shader.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctUINT Index
**			Index of the attribute to retrieve.
**
**	OUTPUT:
**
**		gcATTRIBUTE * Attribute
**			Pointer to a variable receiving the gcATTRIBUTE object pointer.
*/
gceSTATUS
gcSHADER_GetAttribute(
	IN gcSHADER Shader,
	IN gctUINT Index,
	OUT gcATTRIBUTE * Attribute
	);

/*******************************************************************************
**                            gcSHADER_GetPositionAttribute
********************************************************************************
**
**	Get the gcATTRIBUTE object pointer for the attribute that defines the
**	position.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**	OUTPUT:
**
**		gctUINT * Index
**			Pointer to a variable receiving the index of te gcATTRIBUTE object
**			used as a position.
**
**		gcATTRIBUTE * Attribute
**			Pointer to a variable receiving the gcATTRIBUTE object pointer.
*/
gceSTATUS
gcSHADER_GetPositionAttribute(
	IN gcSHADER Shader,
	OUT gctUINT * Index,
	OUT gcATTRIBUTE * Attribute
	);

/*******************************************************************************
**							   gcSHADER_AddUniform
********************************************************************************
**
**	Add an uniform to a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctCONST_STRING Name
**			Name of the uniform to add.
**
**		gcSHADER_TYPE Type
**			Type of the uniform to add.
**
**		gctSIZE_T Length
**			Array length of the uniform to add.  'Length' must be at least 1.
**
**	OUTPUT:
**
**		gcUNIFORM * Uniform
**			Pointer to a variable receiving the gcUNIFORM object pointer.
*/
gceSTATUS
gcSHADER_AddUniform(
	IN gcSHADER Shader,
	IN gctCONST_STRING Name,
	IN gcSHADER_TYPE Type,
	IN gctSIZE_T Length,
	OUT gcUNIFORM * Uniform
	);

/*******************************************************************************
**                          gcSHADER_GetUniformCount
********************************************************************************
**
**	Get the number of uniforms for this shader.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**	OUTPUT:
**
**		gctSIZE_T * Count
**			Pointer to a variable receiving the number of uniforms.
*/
gceSTATUS
gcSHADER_GetUniformCount(
	IN gcSHADER Shader,
	OUT gctSIZE_T * Count
	);

/*******************************************************************************
**                             gcSHADER_GetUniform
********************************************************************************
**
**	Get the gcUNIFORM object pointer for an indexed uniform for this shader.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctUINT Index
**			Index of the uniform to retrieve.
**
**	OUTPUT:
**
**		gcUNIFORM * Uniform
**			Pointer to a variable receiving the gcUNIFORM object pointer.
*/
gceSTATUS
gcSHADER_GetUniform(
	IN gcSHADER Shader,
	IN gctUINT Index,
	OUT gcUNIFORM * Uniform
	);

/*******************************************************************************
**							   gcSHADER_AddOutput
********************************************************************************
**
**	Add an output to a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctCONST_STRING Name
**			Name of the output to add.
**
**		gcSHADER_TYPE Type
**			Type of the output to add.
**
**		gctSIZE_T Length
**			Array length of the output to add.  'Length' must be at least 1.
**
**		gctUINT16 TempRegister
**			Temporary register index that holds the output value.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddOutput(
	IN gcSHADER Shader,
	IN gctCONST_STRING Name,
	IN gcSHADER_TYPE Type,
	IN gctSIZE_T Length,
	IN gctUINT16 TempRegister
	);

gceSTATUS
gcSHADER_AddOutputIndexed(
	IN gcSHADER Shader,
	IN gctCONST_STRING Name,
	IN gctSIZE_T Index,
	IN gctUINT16 TempIndex
	);

/*******************************************************************************
**							 gcSHADER_GetOutputCount
********************************************************************************
**
**	Get the number of outputs for this shader.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**	OUTPUT:
**
**		gctSIZE_T * Count
**			Pointer to a variable receiving the number of outputs.
*/
gceSTATUS
gcSHADER_GetOutputCount(
	IN gcSHADER Shader,
	OUT gctSIZE_T * Count
	);

/*******************************************************************************
**							   gcSHADER_GetOutput
********************************************************************************
**
**	Get the gcOUTPUT object pointer for an indexed output for this shader.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctUINT Index
**			Index of output to retrieve.
**
**	OUTPUT:
**
**		gcOUTPUT * Output
**			Pointer to a variable receiving the gcOUTPUT object pointer.
*/
gceSTATUS
gcSHADER_GetOutput(
	IN gcSHADER Shader,
	IN gctUINT Index,
	OUT gcOUTPUT * Output
	);

/*******************************************************************************
**							   gcSHADER_AddVariable
********************************************************************************
**
**	Add a variable to a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctCONST_STRING Name
**			Name of the variable to add.
**
**		gcSHADER_TYPE Type
**			Type of the variable to add.
**
**		gctSIZE_T Length
**			Array length of the variable to add.  'Length' must be at least 1.
**
**		gctUINT16 TempRegister
**			Temporary register index that holds the variable value.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddVariable(
	IN gcSHADER Shader,
	IN gctCONST_STRING Name,
	IN gcSHADER_TYPE Type,
	IN gctSIZE_T Length,
	IN gctUINT16 TempRegister
	);

/*******************************************************************************
**							 gcSHADER_GetVariableCount
********************************************************************************
**
**	Get the number of variables for this shader.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**	OUTPUT:
**
**		gctSIZE_T * Count
**			Pointer to a variable receiving the number of variables.
*/
gceSTATUS
gcSHADER_GetVariableCount(
	IN gcSHADER Shader,
	OUT gctSIZE_T * Count
	);

/*******************************************************************************
**							   gcSHADER_GetVariable
********************************************************************************
**
**	Get the gcVARIABLE object pointer for an indexed variable for this shader.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctUINT Index
**			Index of variable to retrieve.
**
**	OUTPUT:
**
**		gcVARIABLE * Variable
**			Pointer to a variable receiving the gcVARIABLE object pointer.
*/
gceSTATUS
gcSHADER_GetVariable(
	IN gcSHADER Shader,
	IN gctUINT Index,
	OUT gcVARIABLE * Variable
	);

/*******************************************************************************
**							   gcSHADER_AddOpcode
********************************************************************************
**
**	Add an opcode to a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gcSL_OPCODE Opcode
**			Opcode to add.
**
**		gctUINT16 TempRegister
**			Temporary register index that acts as the target of the opcode.
**
**		gctUINT8 Enable
**			Write enable bits for the temporary register that acts as the target
**			of the opcode.
**
**		gcSL_FORMAT Format
**			Format of the temporary register.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddOpcode(
	IN gcSHADER Shader,
	IN gcSL_OPCODE Opcode,
	IN gctUINT16 TempRegister,
	IN gctUINT8 Enable,
	IN gcSL_FORMAT Format
	);

gceSTATUS
gcSHADER_AddOpcode2(
	IN gcSHADER Shader,
	IN gcSL_OPCODE Opcode,
	IN gcSL_CONDITION Condition,
	IN gctUINT16 TempRegister,
	IN gctUINT8 Enable,
	IN gcSL_FORMAT Format
	);

/*******************************************************************************
**							gcSHADER_AddOpcodeIndexed
********************************************************************************
**
**	Add an opcode to a gcSHADER object that writes to an dynamically indexed
**	target.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gcSL_OPCODE Opcode
**			Opcode to add.
**
**		gctUINT16 TempRegister
**			Temporary register index that acts as the target of the opcode.
**
**		gctUINT8 Enable
**			Write enable bits  for the temporary register that acts as the
**			target of the opcode.
**
**		gcSL_INDEXED Mode
**			Location of the dynamic index inside the temporary register.  Valid
**			values can be:
**
**				gcSL_INDEXED_X - Use x component of the temporary register.
**				gcSL_INDEXED_Y - Use y component of the temporary register.
**				gcSL_INDEXED_Z - Use z component of the temporary register.
**				gcSL_INDEXED_W - Use w component of the temporary register.
**
**		gctUINT16 IndexRegister
**			Temporary register index that holds the dynamic index.
**
**		gcSL_FORMAT Format
**			Format of the temporary register.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddOpcodeIndexed(
	IN gcSHADER Shader,
	IN gcSL_OPCODE Opcode,
	IN gctUINT16 TempRegister,
	IN gctUINT8 Enable,
	IN gcSL_INDEXED Mode,
	IN gctUINT16 IndexRegister,
	IN gcSL_FORMAT Format
	);

/*******************************************************************************
**						  gcSHADER_AddOpcodeConditional
********************************************************************************
**
**	Add an conditional opcode to a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gcSL_OPCODE Opcode
**			Opcode to add.
**
**		gcSL_CONDITION Condition
**			Condition that needs to evaluate to gcvTRUE in order for the opcode to
**			execute.
**
**		gctUINT Label
**			Target label if 'Condition' evaluates to gcvTRUE.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddOpcodeConditional(
	IN gcSHADER Shader,
	IN gcSL_OPCODE Opcode,
	IN gcSL_CONDITION Condition,
	IN gctUINT Label
	);

/*******************************************************************************
**								gcSHADER_AddLabel
********************************************************************************
**
**	Define a label at the current instruction of a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctUINT Label
**			Label to define.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddLabel(
	IN gcSHADER Shader,
	IN gctUINT Label
	);

/*******************************************************************************
**							   gcSHADER_AddSource
********************************************************************************
**
**	Add a source operand to a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gcSL_TYPE Type
**			Type of the source operand.
**
**		gctUINT16 SourceIndex
**			Index of the source operand.
**
**		gctUINT8 Swizzle
**			x, y, z, and w swizzle values packed into one 8-bit value.
**
**		gcSL_FORMAT Format
**			Format of the source operand.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddSource(
	IN gcSHADER Shader,
	IN gcSL_TYPE Type,
	IN gctUINT16 SourceIndex,
	IN gctUINT8 Swizzle,
	IN gcSL_FORMAT Format
	);

/*******************************************************************************
**							gcSHADER_AddSourceIndexed
********************************************************************************
**
**	Add a dynamically indexed source operand to a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gcSL_TYPE Type
**			Type of the source operand.
**
**		gctUINT16 SourceIndex
**			Index of the source operand.
**
**		gctUINT8 Swizzle
**			x, y, z, and w swizzle values packed into one 8-bit value.
**
**		gcSL_INDEXED Mode
**			Addressing mode for the index.
**
**		gctUINT16 IndexRegister
**			Temporary register index that holds the dynamic index.
**
**		gcSL_FORMAT Format
**			Format of the source operand.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddSourceIndexed(
	IN gcSHADER Shader,
	IN gcSL_TYPE Type,
	IN gctUINT16 SourceIndex,
	IN gctUINT8 Swizzle,
	IN gcSL_INDEXED Mode,
	IN gctUINT16 IndexRegister,
	IN gcSL_FORMAT Format
	);

/*******************************************************************************
**						   gcSHADER_AddSourceAttribute
********************************************************************************
**
**	Add an attribute as a source operand to a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gcATTRIBUTE Attribute
**			Pointer to a gcATTRIBUTE object.
**
**		gctUINT8 Swizzle
**			x, y, z, and w swizzle values packed into one 8-bit value.
**
**		gctINT Index
**			Static index into the attribute in case the attribute is a matrix
**			or array.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddSourceAttribute(
	IN gcSHADER Shader,
	IN gcATTRIBUTE Attribute,
	IN gctUINT8 Swizzle,
	IN gctINT Index
	);

/*******************************************************************************
**						   gcSHADER_AddSourceAttributeIndexed
********************************************************************************
**
**	Add an indexed attribute as a source operand to a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gcATTRIBUTE Attribute
**			Pointer to a gcATTRIBUTE object.
**
**		gctUINT8 Swizzle
**			x, y, z, and w swizzle values packed into one 8-bit value.
**
**		gctINT Index
**			Static index into the attribute in case the attribute is a matrix
**			or array.
**
**		gcSL_INDEXED Mode
**			Addressing mode of the dynamic index.
**
**		gctUINT16 IndexRegister
**			Temporary register index that holds the dynamic index.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddSourceAttributeIndexed(
	IN gcSHADER Shader,
	IN gcATTRIBUTE Attribute,
	IN gctUINT8 Swizzle,
	IN gctINT Index,
	IN gcSL_INDEXED Mode,
	IN gctUINT16 IndexRegister
	);

/*******************************************************************************
**							gcSHADER_AddSourceUniform
********************************************************************************
**
**	Add a uniform as a source operand to a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gcUNIFORM Uniform
**			Pointer to a gcUNIFORM object.
**
**		gctUINT8 Swizzle
**			x, y, z, and w swizzle values packed into one 8-bit value.
**
**		gctINT Index
**			Static index into the uniform in case the uniform is a matrix or
**			array.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddSourceUniform(
	IN gcSHADER Shader,
	IN gcUNIFORM Uniform,
	IN gctUINT8 Swizzle,
	IN gctINT Index
	);

/*******************************************************************************
**						gcSHADER_AddSourceUniformIndexed
********************************************************************************
**
**	Add an indexed uniform as a source operand to a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gcUNIFORM Uniform
**			Pointer to a gcUNIFORM object.
**
**		gctUINT8 Swizzle
**			x, y, z, and w swizzle values packed into one 8-bit value.
**
**		gctINT Index
**			Static index into the uniform in case the uniform is a matrix or
**			array.
**
**		gcSL_INDEXED Mode
**			Addressing mode of the dynamic index.
**
**		gctUINT16 IndexRegister
**			Temporary register index that holds the dynamic index.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddSourceUniformIndexed(
	IN gcSHADER Shader,
	IN gcUNIFORM Uniform,
	IN gctUINT8 Swizzle,
	IN gctINT Index,
	IN gcSL_INDEXED Mode,
	IN gctUINT16 IndexRegister
	);

gceSTATUS
gcSHADER_AddSourceSamplerIndexed(
	IN gcSHADER Shader,
	IN gctUINT8 Swizzle,
	IN gcSL_INDEXED Mode,
	IN gctUINT16 IndexRegister
	);

/*******************************************************************************
**						   gcSHADER_AddSourceConstant
********************************************************************************
**
**	Add a constant floating pointer value as a source operand to a gcSHADER
**	object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctFLOAT Constant
**			Floating pointer constant.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_AddSourceConstant(
	IN gcSHADER Shader,
	IN gctFLOAT Constant
	);

/*******************************************************************************
**								  gcSHADER_Pack
********************************************************************************
**
**	Pack a dynamically created gcSHADER object by trimming the allocated arrays
**	and resolving all the labeling.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_Pack(
	IN gcSHADER Shader
	);

/*******************************************************************************
**								gcSHADER_SetOptimizationOption
********************************************************************************
**
**	Set optimization option of a gcSHADER object.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object.
**
**		gctUINT OptimizationOption
**			Optimization option.  Can be one of the following:
**
**				0						- No optimization.
**				1						- Full optimization.
**				Other value				- For optimizer testing.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcSHADER_SetOptimizationOption(
	IN gcSHADER Shader,
	IN gctUINT OptimizationOption
	);

gceSTATUS
gcSHADER_AddFunction(
	IN gcSHADER Shader,
	IN gctCONST_STRING Name,
	OUT gcFUNCTION * Function
	);

gceSTATUS
gcSHADER_BeginFunction(
	IN gcSHADER Shader,
	IN gcFUNCTION Function
	);

gceSTATUS
gcSHADER_EndFunction(
	IN gcSHADER Shader,
	IN gcFUNCTION Function
	);

/*******************************************************************************
**                             gcATTRIBUTE_GetType
********************************************************************************
**
**	Get the type and array length of a gcATTRIBUTE object.
**
**	INPUT:
**
**		gcATTRIBUTE Attribute
**			Pointer to a gcATTRIBUTE object.
**
**	OUTPUT:
**
**		gcSHADER_TYPE * Type
**			Pointer to a variable receiving the type of the attribute.  'Type'
**			can be gcvNULL, in which case no type will be returned.
**
**		gctSIZE_T * ArrayLength
**			Pointer to a variable receiving the length of the array if the
**			attribute was declared as an array.  If the attribute was not
**			declared as an array, the array length will be 1.  'ArrayLength' can
**			be gcvNULL, in which case no array length will be returned.
*/
gceSTATUS
gcATTRIBUTE_GetType(
	IN gcATTRIBUTE Attribute,
	OUT gcSHADER_TYPE * Type,
	OUT gctSIZE_T * ArrayLength
	);

/*******************************************************************************
**                            gcATTRIBUTE_GetName
********************************************************************************
**
**	Get the name of a gcATTRIBUTE object.
**
**	INPUT:
**
**		gcATTRIBUTE Attribute
**			Pointer to a gcATTRIBUTE object.
**
**	OUTPUT:
**
**		gctSIZE_T * Length
**			Pointer to a variable receiving the length of the attribute name.
**			'Length' can be gcvNULL, in which case no length will be returned.
**
**		gctCONST_STRING * Name
**			Pointer to a variable receiving the pointer to the attribute name.
**			'Name' can be gcvNULL, in which case no name will be returned.
*/
gceSTATUS
gcATTRIBUTE_GetName(
	IN gcATTRIBUTE Attribute,
	OUT gctSIZE_T * Length,
	OUT gctCONST_STRING * Name
	);

/*******************************************************************************
**                            gcATTRIBUTE_IsEnabled
********************************************************************************
**
**	Query the enabled state of a gcATTRIBUTE object.
**
**	INPUT:
**
**		gcATTRIBUTE Attribute
**			Pointer to a gcATTRIBUTE object.
**
**	OUTPUT:
**
**		gctBOOL * Enabled
**			Pointer to a variable receiving the enabled state of the attribute.
*/
gceSTATUS
gcATTRIBUTE_IsEnabled(
	IN gcATTRIBUTE Attribute,
	OUT gctBOOL * Enabled
	);

/*******************************************************************************
**                              gcUNIFORM_GetType
********************************************************************************
**
**	Get the type and array length of a gcUNIFORM object.
**
**	INPUT:
**
**		gcUNIFORM Uniform
**			Pointer to a gcUNIFORM object.
**
**	OUTPUT:
**
**		gcSHADER_TYPE * Type
**			Pointer to a variable receiving the type of the uniform.  'Type' can
**			be gcvNULL, in which case no type will be returned.
**
**		gctSIZE_T * ArrayLength
**			Pointer to a variable receiving the length of the array if the
**			uniform was declared as an array.  If the uniform was not declared
**			as an array, the array length will be 1.  'ArrayLength' can be gcvNULL,
**			in which case no array length will be returned.
*/
gceSTATUS
gcUNIFORM_GetType(
	IN gcUNIFORM Uniform,
	OUT gcSHADER_TYPE * Type,
	OUT gctSIZE_T * ArrayLength
	);

/*******************************************************************************
**                              gcUNIFORM_GetName
********************************************************************************
**
**	Get the name of a gcUNIFORM object.
**
**	INPUT:
**
**		gcUNIFORM Uniform
**			Pointer to a gcUNIFORM object.
**
**	OUTPUT:
**
**		gctSIZE_T * Length
**			Pointer to a variable receiving the length of the uniform name.
**			'Length' can be gcvNULL, in which case no length will be returned.
**
**		gctCONST_STRING * Name
**			Pointer to a variable receiving the pointer to the uniform name.
**			'Name' can be gcvNULL, in which case no name will be returned.
*/
gceSTATUS
gcUNIFORM_GetName(
	IN gcUNIFORM Uniform,
	OUT gctSIZE_T * Length,
	OUT gctCONST_STRING * Name
	);

/*******************************************************************************
**                              gcUNIFORM_GetSampler
********************************************************************************
**
**	Get the physical sampler number for a sampler gcUNIFORM object.
**
**	INPUT:
**
**		gcUNIFORM Uniform
**			Pointer to a gcUNIFORM object.
**
**	OUTPUT:
**
**		gctUINT32 * Sampler
**			Pointer to a variable receiving the physical sampler.
*/
gceSTATUS
gcUNIFORM_GetSampler(
	IN gcUNIFORM Uniform,
	OUT gctUINT32 * Sampler
	);

/*******************************************************************************
**							   gcUNIFORM_SetValue
********************************************************************************
**
**	Set the value of a uniform in integer.
**
**	INPUT:
**
**		gcUNIFORM Uniform
**			Pointer to a gcUNIFORM object.
**
**		gctSIZE_T Count
**			Number of entries to program if the uniform has been declared as an
**			array.
**
**		const gctINT * Value
**			Pointer to a buffer holding the integer values for the uniform.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcUNIFORM_SetValue(
	IN gcUNIFORM Uniform,
	IN gctSIZE_T Count,
	IN const gctINT * Value
	);

/*******************************************************************************
**							   gcUNIFORM_SetValueX
********************************************************************************
**
**	Set the value of a uniform in fixed point.
**
**	INPUT:
**
**		gcUNIFORM Uniform
**			Pointer to a gcUNIFORM object.
**
**		gctSIZE_T Count
**			Number of entries to program if the uniform has been declared as an
**			array.
**
**		const gctFIXED_POINT * Value
**			Pointer to a buffer holding the fixed point values for the uniform.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcUNIFORM_SetValueX(
	IN gcUNIFORM Uniform,
	IN gctSIZE_T Count,
	IN gctFIXED_POINT * Value
	);

/*******************************************************************************
**							   gcUNIFORM_SetValueF
********************************************************************************
**
**	Set the value of a uniform in floating point.
**
**	INPUT:
**
**		gcUNIFORM Uniform
**			Pointer to a gcUNIFORM object.
**
**		gctSIZE_T Count
**			Number of entries to program if the uniform has been declared as an
**			array.
**
**		const gctFLOAT * Value
**			Pointer to a buffer holding the floating point values for the
**			uniform.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gcUNIFORM_SetValueF(
	IN gcUNIFORM Uniform,
	IN gctSIZE_T Count,
	IN const gctFLOAT * Value
	);

/*******************************************************************************
**								gcOUTPUT_GetType
********************************************************************************
**
**	Get the type and array length of a gcOUTPUT object.
**
**	INPUT:
**
**		gcOUTPUT Output
**			Pointer to a gcOUTPUT object.
**
**	OUTPUT:
**
**		gcSHADER_TYPE * Type
**			Pointer to a variable receiving the type of the output.  'Type' can
**			be gcvNULL, in which case no type will be returned.
**
**		gctSIZE_T * ArrayLength
**			Pointer to a variable receiving the length of the array if the
**			output was declared as an array.  If the output was not declared
**			as an array, the array length will be 1.  'ArrayLength' can be gcvNULL,
**			in which case no array length will be returned.
*/
gceSTATUS
gcOUTPUT_GetType(
	IN gcOUTPUT Output,
	OUT gcSHADER_TYPE * Type,
	OUT gctSIZE_T * ArrayLength
	);

/*******************************************************************************
**							   gcOUTPUT_GetIndex
********************************************************************************
**
**	Get the index of a gcOUTPUT object.
**
**	INPUT:
**
**		gcOUTPUT Output
**			Pointer to a gcOUTPUT object.
**
**	OUTPUT:
**
**		gctUINT * Index
**			Pointer to a variable receiving the temporary register index of the
**			output.  'Index' can be gcvNULL,. in which case no index will be
**			returned.
*/
gceSTATUS
gcOUTPUT_GetIndex(
	IN gcOUTPUT Output,
	OUT gctUINT * Index
	);

/*******************************************************************************
**								gcOUTPUT_GetName
********************************************************************************
**
**	Get the name of a gcOUTPUT object.
**
**	INPUT:
**
**		gcOUTPUT Output
**			Pointer to a gcOUTPUT object.
**
**	OUTPUT:
**
**		gctSIZE_T * Length
**			Pointer to a variable receiving the length of the output name.
**			'Length' can be gcvNULL, in which case no length will be returned.
**
**		gctCONST_STRING * Name
**			Pointer to a variable receiving the pointer to the output name.
**			'Name' can be gcvNULL, in which case no name will be returned.
*/
gceSTATUS
gcOUTPUT_GetName(
	IN gcOUTPUT Output,
	OUT gctSIZE_T * Length,
	OUT gctCONST_STRING * Name
	);

/*******************************************************************************
*********************************************************** F U N C T I O N S **
*******************************************************************************/

gceSTATUS
gcFUNCTION_AddArgument(
	IN gcFUNCTION Function,
	IN gctUINT16 TempIndex,
	IN gctUINT8 Enable,
	IN gctUINT8 Qualifier
	);

gceSTATUS
gcFUNCTION_GetArgument(
	IN gcFUNCTION Function,
	IN gctUINT16 Index,
	OUT gctUINT16_PTR Temp,
	OUT gctUINT8_PTR Enable,
	OUT gctUINT8_PTR Swizzle
	);

gceSTATUS
gcFUNCTION_GetLabel(
	IN gcFUNCTION Function,
	OUT gctUINT_PTR Label
	);

/*******************************************************************************
**                              gcCompileShader
********************************************************************************
**
**	Compile a shader.
**
**	INPUT:
**
**		gcoOS Hal
**			Pointer to an gcoHAL object.
**
**		gctINT ShaderType
**			Shader type to compile.  Can be one of the following values:
**
**				gcSHADER_TYPE_VERTEX
**					Compile a vertex shader.
**
**				gcSHADER_TYPE_FRAGMENT
**					Compile a fragment shader.
**
**		gctSIZE_T SourceSize
**			Size of the source buffer in bytes.
**
**		gctCONST_STRING Source
**			Pointer to the buffer containing the shader source code.
**
**	OUTPUT:
**
**		gcSHADER * Binary
**			Pointer to a variable receiving the pointer to a gcSHADER object
**			containg the compiled shader code.
**
**		gctSTRING * Log
**			Pointer to a variable receiving a string pointer containging the
**			compile log.
*/
gceSTATUS
gcCompileShader(
	IN gcoHAL Hal,
	IN gctINT ShaderType,
	IN gctSIZE_T SourceSize,
	IN gctCONST_STRING Source,
	OUT gcSHADER * Binary,
	OUT gctSTRING * Log
	);

/*******************************************************************************
**                              gcOptimizeShader
********************************************************************************
**
**	Optimize a shader.
**
**	INPUT:
**
**		gcSHADER Shader
**			Pointer to a gcSHADER object holding information about the compiled
**			shader.
**
**		gctFILE LogFile
**			Pointer to an open FILE object.
*/
gceSTATUS
gcOptimizeShader(
	IN gcSHADER Shader,
	IN gctFILE LogFile
	);

/*******************************************************************************
**                                gcLinkShaders
********************************************************************************
**
**	Link two shaders and generate a harwdare specific state buffer by compiling
**	the compiler generated code through the resource allocator and code
**	generator.
**
**	INPUT:
**
**		gcSHADER VertexShader
**			Pointer to a gcSHADER object holding information about the compiled
**			vertex shader.
**
**		gcSHADER FragmentShader
**			Pointer to a gcSHADER object holding information about the compiled
**			fragment shader.
**
**		gceSHADER_FLAGS Flags
**			Compiler flags.  Can be any of the following:
**
**				gcvSHADER_DEAD_CODE       - Dead code elimination.
**				gcvSHADER_RESOURCE_USAGE  - Resource usage optimizaion.
**				gcvSHADER_OPTIMIZER       - Full optimization.
**				gcvSHADER_USE_GL_Z        - Use OpenGL ES Z coordinate.
**				gcvSHADER_USE_GL_POSITION - Use OpenGL ES gl_Position.
**				gcvSHADER_USE_GL_FACE     - Use OpenGL ES gl_FaceForward.
**
**	OUTPUT:
**
**		gctSIZE_T * StateBufferSize
**			Pointer to a variable receicing the number of bytes in the buffer
**			returned in 'StateBuffer'.
**
**		gctPOINTER * StateBuffer
**			Pointer to a variable receiving a buffer pointer that contains the
**			states required to download the shaders into the hardware.
**
**		gcsHINT_PTR * Hints
**			Pointer to a variable receiving a gcsHINT structure pointer that
**			contains information required when loading the shader states.
*/
gceSTATUS
gcLinkShaders(
	IN gcSHADER VertexShader,
	IN gcSHADER FragmentShader,
	IN gceSHADER_FLAGS Flags,
	OUT gctSIZE_T * StateBufferSize,
	OUT gctPOINTER * StateBuffer,
	OUT gcsHINT_PTR * Hints
	);

/*******************************************************************************
**                                gcLoadShaders
********************************************************************************
**
**	Load a pre-compiled and pre-linked shader program into the hardware.
**
**	INPUT:
**
**		gcoHAL Hal
**			Pointer to a gcoHAL object.
**
**		gctSIZE_T StateBufferSize
**			The number of bytes in the 'StateBuffer'.
**
**		gctPOINTER StateBuffer
**			Pointer to the states that make up the shader program.
**
**		gcsHINT_PTR Hints
**			Pointer to a gcsHINT structure that contains information required
**			when loading the shader states.
**
**		gcePRIMITIVE PrimitiveType
**			Primitive type to be rendered.
*/
gceSTATUS
gcLoadShaders(
	IN gcoHAL Hal,
	IN gctSIZE_T StateBufferSize,
	IN gctPOINTER StateBuffer,
	IN gcsHINT_PTR Hints,
	IN gcePRIMITIVE PrimitiveType
	);

/*******************************************************************************
**                                gcSaveProgram
********************************************************************************
**
**	Save pre-compiled shaders and pre-linked programs to a binary file.
**
**	INPUT:
**
**		gcSHADER VertexShader
**			Pointer to vertex shader object.
**
**		gcSHADER FragmentShader
**			Pointer to fragment shader object.
**
**		gctSIZE_T ProgramBufferSize
**			Number of bytes in 'ProgramBuffer'.
**
**		gctPOINTER ProgramBuffer
**			Pointer to buffer containing the program states.
**
**		gcsHINT_PTR Hints
**			Pointer to HINTS structure for program states.
**
**	OUTPUT:
**
**		gctPOINTER * Binary
**			Pointer to a variable receiving the binary data to be saved.
**
**		gctSIZE_T * BinarySize
**			Pointer to a variable receiving the number of bytes inside 'Binary'.
*/
gceSTATUS
gcSaveProgram(
	IN gcSHADER VertexShader,
	IN gcSHADER FragmentShader,
	IN gctSIZE_T ProgramBufferSize,
	IN gctPOINTER ProgramBuffer,
	IN gcsHINT_PTR Hints,
	OUT gctPOINTER * Binary,
	OUT gctSIZE_T * BinarySize
	);

/*******************************************************************************
**                                gcLoadProgram
********************************************************************************
**
**	Load pre-compiled shaders and pre-linked programs from a binary file.
**
**	INPUT:
**
**		gctPOINTER Binary
**			Pointer to the binary data loaded.
**
**		gctSIZE_T BinarySize
**			Number of bytes in 'Binary'.
**
**	OUTPUT:
**
**		gcSHADER * VertexShader
**			Pointer to a variable receiving the vertex shader object.
**
**		gcSHADER * FragmentShader
**			Pointer to a variable receiving the fragment shader object.
**
**		gctSIZE_T * ProgramBufferSize
**			Pointer to a variable receicing the number of bytes in the buffer
**			returned in 'ProgramBuffer'.
**
**		gctPOINTER * ProgramBuffer
**			Pointer to a variable receiving a buffer pointer that contains the
**			states required to download the shaders into the hardware.
**
**		gcsHINT_PTR * Hints
**			Pointer to a variable receiving a gcsHINT structure pointer that
**			contains information required when loading the shader states.
*/
gceSTATUS
gcLoadProgram(
	IN gctPOINTER Binary,
	IN gctSIZE_T BinarySize,
	OUT gcSHADER * VertexShader,
	OUT gcSHADER * FragmentShader,
	OUT gctSIZE_T * ProgramBufferSize,
	OUT gctPOINTER * ProgramBuffer,
	OUT gcsHINT_PTR * Hints
	);

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_compiler_h_ */

