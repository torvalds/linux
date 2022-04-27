/*************************************************************************/ /*!
@File
@Title          Services initialisation parameters header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Services initialisation parameter support for the Linux kernel.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef OS_SRVINIT_PARAM_H
#define OS_SRVINIT_PARAM_H

#if defined(__linux__) && defined(__KERNEL__)
#include "km_apphint.h"
#include "km_apphint_defs.h"

#define SrvInitParamOpen() NULL
#define SrvInitParamClose(pvState) ((void)(pvState))

#define SrvInitParamGetBOOL(state, name, value) \
	((void) pvr_apphint_get_bool(APPHINT_ID_ ## name, &value))

#define SrvInitParamGetUINT32(state, name, value) \
	((void) pvr_apphint_get_uint32(APPHINT_ID_ ## name, &value))

#define SrvInitParamGetUINT64(state, name, value) \
	((void) pvr_apphint_get_uint64(APPHINT_ID_ ## name, &value))

#define SrvInitParamGetSTRING(state, name, buffer, size) \
	((void) pvr_apphint_get_string(APPHINT_ID_ ## name, buffer, size))

#define SrvInitParamGetUINT32BitField(state, name, value) \
	((void) pvr_apphint_get_uint32(APPHINT_ID_ ## name, &value))

#define SrvInitParamGetUINT32List(state, name, value) \
	((void) pvr_apphint_get_uint32(APPHINT_ID_ ## name, &value))

#else	/* defined(__linux__) && defined(__KERNEL__) */

#if defined(__cplusplus)
extern "C" {
#endif

#include "img_defs.h"
#include "img_types.h"

/*! Lookup item. */
typedef struct
{
	const IMG_CHAR *pszValue;       /*!< looked up name */
	IMG_UINT32 ui32Value;           /*!< looked up value */
} SRV_INIT_PARAM_UINT32_LOOKUP;

/*************************************************************************/ /*!
@Brief          SrvInitParamOpen

@Description    Establish a connection to the Parameter resource store which is
                used to hold configuration information associated with the
                server instance.

@Return         (void *) Handle to Parameter resource store to be used for
                subsequent parameter value queries

*/ /**************************************************************************/
void *SrvInitParamOpen(void);

/*************************************************************************/ /*!
@Brief          SrvInitParamClose

@Description    Remove a pre-existing connection to the Parameter resource store
                given by 'pvState' and release any temporary storage associated
                with the 'pvState' mapping handle

@Input          pvState             Handle to Parameter resource store

*/ /**************************************************************************/
void SrvInitParamClose(void *pvState);

/*************************************************************************/ /*!
@Brief          _SrvInitParamGetBOOL

@Description    Get the current BOOL value for parameter 'pszName' from the
                Parameter resource store attached to 'pvState'

@Input          pvState             Handle to Parameter resource store

@Input          pszName             Name of parameter to look-up

@Input          pbDefault           Value to return if parameter not found

@Output         pbValue             Value of parameter 'pszName' or 'pbDefault'
                                    if not found

*/ /**************************************************************************/
void _SrvInitParamGetBOOL(
	void *pvState,
	const IMG_CHAR *pszName,
	const IMG_BOOL *pbDefault,
	IMG_BOOL *pbValue
);

/*! Get the BOOL value for parameter 'name' from the parameter resource store
 *  attached to 'state'. */
#define SrvInitParamGetBOOL(state, name, value) \
		_SrvInitParamGetBOOL(state, # name, & __SrvInitParam_ ## name, &(value))

/*! Initialise FLAG type parameter identified by 'name'. */
#define SrvInitParamInitFLAG(name, defval, dummy) \
	static const IMG_BOOL __SrvInitParam_ ## name = defval;

/*! Initialise BOOL type parameter identified by 'name'. */
#define SrvInitParamInitBOOL(name, defval, dummy) \
	static const IMG_BOOL __SrvInitParam_ ## name = defval;

/*************************************************************************/ /*!
@Brief          _SrvInitParamGetUINT32

@Description    Get the current IMG_UINT32 value for parameter 'pszName'
                from the Parameter resource store attached to 'pvState'

@Input          pvState             Handle to Parameter resource store

@Input          pszName             Name of parameter to look-up

@Input          pui32Default        Value to return if parameter not found

@Output         pui32Value            Value of parameter 'pszName' or
                                    'pui32Default' if not found

*/ /**************************************************************************/
void _SrvInitParamGetUINT32(
	void *pvState,
	const IMG_CHAR *pszName,
	const IMG_UINT32 *pui32Default,
	IMG_UINT32 *pui32Value
);

/*! Get the UINT32 value for parameter 'name' from the parameter resource store
 *  attached to 'state'. */
#define SrvInitParamGetUINT32(state, name, value) \
		_SrvInitParamGetUINT32(state, # name, & __SrvInitParam_ ## name, &(value))

/*! Initialise UINT32 type parameter identified by 'name'. */
#define SrvInitParamInitUINT32(name, defval, dummy) \
	static const IMG_UINT32 __SrvInitParam_ ## name = defval;

/*! Initialise UINT64 type parameter identified by 'name'. */
#define SrvInitParamInitUINT64(name, defval, dummy) \
	static const IMG_UINT64 __SrvInitParam_ ## name = defval;

/*! @cond Doxygen_Suppress */
#define SrvInitParamUnreferenced(name) \
		PVR_UNREFERENCED_PARAMETER( __SrvInitParam_ ## name )
/*! @endcond */

/*************************************************************************/ /*!
@Brief          _SrvInitParamGetUINT32BitField

@Description    Get the current IMG_UINT32 bitfield value for parameter
                'pszBasename' from the Parameter resource store
                attached to 'pvState'

@Input          pvState             Handle to Parameter resource store

@Input          pszBaseName         Bitfield parameter name to search for

@Input          uiDefault           Default return value if parameter not found

@Input          psLookup            Bitfield array to traverse

@Input          uiSize              number of elements in 'psLookup'

@Output         puiValue            Value of bitfield or 'uiDefault' if
                                    parameter not found
*/ /**************************************************************************/
void _SrvInitParamGetUINT32BitField(
	void *pvState,
	const IMG_CHAR *pszBaseName,
	IMG_UINT32 uiDefault,
	const SRV_INIT_PARAM_UINT32_LOOKUP *psLookup,
	IMG_UINT32 uiSize,
	IMG_UINT32 *puiValue
);

/*! Initialise UINT32 bitfield type parameter identified by 'name' with
 *  'inival' value and 'lookup' look up array. */
#define SrvInitParamInitUINT32Bitfield(name, inival, lookup) \
	static IMG_UINT32 __SrvInitParam_ ## name = inival; \
	static SRV_INIT_PARAM_UINT32_LOOKUP * \
		__SrvInitParamLookup_ ## name = &lookup[0]; \
	static const IMG_UINT32 __SrvInitParamSize_ ## name = \
					ARRAY_SIZE(lookup);

/*! Get the UINT32 bitfield value for parameter 'name' from the parameter
 *  resource store attached to 'state'. */
#define SrvInitParamGetUINT32BitField(state, name, value) \
		_SrvInitParamGetUINT32BitField(state, # name, __SrvInitParam_ ## name, __SrvInitParamLookup_ ## name, __SrvInitParamSize_ ## name, &(value))

/*************************************************************************/ /*!
@Brief          _SrvInitParamGetUINT32List

@Description    Get the current IMG_UINT32 list value for the specified
                parameter 'pszName' from the Parameter resource store
                attached to 'pvState'

@Input          pvState             Handle to Parameter resource store

@Input          pszName             Parameter list name to search for

@Input          uiDefault           Default value to return if 'pszName' is
                                    not set within 'pvState'

@Input          psLookup            parameter list to traverse

@Input          uiSize              number of elements in 'psLookup' list

@Output         puiValue            value of located list element or
                                    'uiDefault' if parameter not found

*/ /**************************************************************************/
void _SrvInitParamGetUINT32List(
	void *pvState,
	const IMG_CHAR *pszName,
	IMG_UINT32 uiDefault,
	const SRV_INIT_PARAM_UINT32_LOOKUP *psLookup,
	IMG_UINT32 uiSize,
	IMG_UINT32 *puiValue
);

/*! Get the UINT32 list value for parameter 'name' from the parameter
 *  resource store attached to 'state'. */
#define SrvInitParamGetUINT32List(state, name, value) \
		_SrvInitParamGetUINT32List(state, # name, __SrvInitParam_ ## name, __SrvInitParamLookup_ ## name, __SrvInitParamSize_ ## name, &(value))

/*! Initialise UINT32 list type parameter identified by 'name' with
 *  'defval' default value and 'lookup' look up list. */
#define SrvInitParamInitUINT32List(name, defval, lookup) \
	static IMG_UINT32 __SrvInitParam_ ## name = defval; \
	static SRV_INIT_PARAM_UINT32_LOOKUP * \
		__SrvInitParamLookup_ ## name = &lookup[0]; \
	static const IMG_UINT32 __SrvInitParamSize_ ## name = \
					ARRAY_SIZE(lookup);

/*************************************************************************/ /*!
@Brief          _SrvInitParamGetSTRING

@Description    Get the contents of the specified parameter string 'pszName'
                from the Parameter resource store attached to 'pvState'

@Input          pvState             Handle to Parameter resource store

@Input          pszName             Parameter string name to search for

@Input          psDefault           Default string to return if 'pszName' is
                                    not set within 'pvState'

@Input          size                Size of output 'pBuffer'

@Output         pBuffer             Output copy of 'pszName' contents or
                                    copy of 'psDefault' if 'pszName' is not
                                    set within 'pvState'

*/ /**************************************************************************/
void _SrvInitParamGetSTRING(
	void *pvState,
	const IMG_CHAR *pszName,
	const IMG_CHAR *psDefault,
	IMG_CHAR *pBuffer,
	size_t size
);

/*! Initialise STRING type parameter identified by 'name' with 'defval' default
 *  value. */
#define SrvInitParamInitSTRING(name, defval, dummy) \
	static const IMG_CHAR *__SrvInitParam_ ## name = defval;

/*! Get the STRING value for parameter 'name' from the parameter resource store
 *  attached to 'state'. */
#define SrvInitParamGetSTRING(state, name, buffer, size) \
		_SrvInitParamGetSTRING(state, # name,  __SrvInitParam_ ## name, buffer, size)

#if defined(__cplusplus)
}
#endif

#endif /* defined(__linux__) && defined(__KERNEL__) */

#endif /* OS_SRVINIT_PARAM_H */
