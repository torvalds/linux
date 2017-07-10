/*************************************************************************/ /*!
@File           oskm_apphint.h
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    OS-independent interface for retrieving KM apphints
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
#include "img_defs.h"
#if defined(LINUX)
#include "km_apphint.h"
#else
#include "services_client_porting.h"
#endif
#if !defined(__OSKM_APPHINT_H__)
#define __OSKM_APPHINT_H__


#if defined(LINUX) && !defined(DOXYGEN)
#if defined(SUPPORT_KERNEL_SRVINIT)
static INLINE IMG_UINT os_get_km_apphint_UINT32(void *state, APPHINT_ID id, IMG_UINT32 *pAppHintDefault, IMG_UINT32 *pVal) {
	return !pvr_apphint_get_uint32(id, pVal);
}
static INLINE IMG_UINT os_get_km_apphint_UINT64(void *state, APPHINT_ID id, IMG_UINT64 *pAppHintDefault, IMG_UINT64 *pVal) {
	return !pvr_apphint_get_uint64(id, pVal);
}
static INLINE IMG_UINT os_get_km_apphint_BOOL(void *state, APPHINT_ID id, IMG_BOOL *pAppHintDefault, IMG_BOOL *pVal) {
	return !pvr_apphint_get_bool(id, pVal);
}
static INLINE IMG_UINT os_get_km_apphint_STRING(void *state, APPHINT_ID id, IMG_CHAR **pAppHintDefault, IMG_CHAR *buffer, size_t size) {
	return !pvr_apphint_get_string(id, buffer, size);
}

#define OSGetKMAppHintUINT32(state, name, appHintDefault, value) \
	os_get_km_apphint_UINT32(state, APPHINT_ID_ ## name, appHintDefault, value)

#define OSGetKMAppHintUINT64(state, name, appHintDefault, value) \
	os_get_km_apphint_UINT64(state, APPHINT_ID_ ## name, appHintDefault, value)

#define OSGetKMAppHintBOOL(state, name, appHintDefault, value) \
	os_get_km_apphint_BOOL(state, APPHINT_ID_ ## name, appHintDefault, value)

#define OSGetKMAppHintSTRING(state, name, appHintDefault, buffer, size) \
	os_get_km_apphint_STRING(state, APPHINT_ID_ ## name, appHintDefault, buffer, size)

#else
static INLINE IMG_UINT os_get_apphint_default_UINT32(IMG_UINT32 *pAppHintDefault, IMG_UINT32 *pVal) {
	*pVal = *pAppHintDefault;
	return IMG_TRUE;
}
static INLINE IMG_UINT os_get_apphint_default_UINT64(IMG_UINT64 *pAppHintDefault, IMG_UINT64 *pVal) {
	*pVal = *pAppHintDefault;
	return IMG_TRUE;
}
static INLINE IMG_UINT os_get_apphint_default_BOOL(IMG_BOOL *pAppHintDefault, IMG_BOOL *pVal) {
	*pVal = *pAppHintDefault;
	return IMG_TRUE;
}
static INLINE IMG_UINT os_get_apphint_default_STRING(IMG_CHAR **pAppHintDefault, IMG_CHAR *buffer, IMG_UINT32 size) {
	strlcpy(buffer, *pAppHintDefault, size);
	return IMG_TRUE;
}

#define OSGetKMAppHintUINT32(state, name, appHintDefault, value) \
	os_get_apphint_default_UINT32(appHintDefault, value)

#define OSGetKMAppHintUINT64(state, name, appHintDefault, value) \
	os_get_apphint_default_UINT64(appHintDefault, value)

#define OSGetKMAppHintBOOL(state, name, appHintDefault, value) \
	os_get_apphint_default_BOOL(appHintDefault, value)

#define OSGetKMAppHintSTRING(state, name, appHintDefault, buffer, size) \
	os_get_apphint_default_STRING(appHintDefault, buffer, size)

#endif

#define OSCreateKMAppHintState(state) \
	PVR_UNREFERENCED_PARAMETER(state)

#define OSFreeKMAppHintState(state) \
	PVR_UNREFERENCED_PARAMETER(state)

#else /* #if defined(LINUX) && !defined(DOXYGEN) */

static INLINE IMG_BOOL os_get_km_apphint_STRING(void *state, IMG_CHAR *name, IMG_CHAR **pAppHintDefault, IMG_CHAR *buffer, size_t size) {
	PVR_UNREFERENCED_PARAMETER(size);
	return PVRSRVGetAppHint(state, name, IMG_STRING_TYPE, pAppHintDefault, buffer);
}

/**************************************************************************/ /*!
@def OSGetKMAppHintUINT32(state, name, appHintDefault, value)
@Description    Interface for retrieval of uint32 km app hint.
				For non-linux operating systems, this macro implements a call
				from server code to PVRSRVGetAppHint() declared in
				services_client_porting.h, effectively making it 'shared' code.
@Input          state             App hint state
@Input          name              Name used to identify app hint
@Input          appHintDefault    Default value to be returned if no
								  app hint is found.
@Output         value             Pointer to returned app hint value.
 */ /**************************************************************************/
#define OSGetKMAppHintUINT32(state, name, appHintDefault, value) \
	PVRSRVGetAppHint(state, # name, IMG_UINT_TYPE, appHintDefault, value)

/**************************************************************************/ /*!
@def OSGetKMAppHintUINT64(state, name, appHintDefault, value)
@Description    Interface for retrieval of uint64 km app hint.
				For non-linux operating systems, this macro implements a call
				from server code to PVRSRVGetAppHint() declared in
				services_client_porting.h, effectively making it 'shared' code.
@Input          state             App hint state
@Input          name              Name used to identify app hint
@Input          appHintDefault    Default value to be returned if no
								  app hint is found.
@Output         value             Pointer to returned app hint value.
 */ /**************************************************************************/
#define OSGetKMAppHintUINT64(state, name, appHintDefault, value) \
	PVRSRVGetAppHint(state, # name, IMG_UINT_TYPE, appHintDefault, value)

/**************************************************************************/ /*!
@def OSGetKMAppHintBOOL(state, name, appHintDefault, value)
@Description    Interface for retrieval of IMG_BOOL km app hint.
				For non-linux operating systems, this macro implements a call
				from server code to PVRSRVGetAppHint() declared in
				services_client_porting.h, effectively making it 'shared' code.
@Input          state             App hint state
@Input          name              Name used to identify app hint
@Input          appHintDefault    Default value to be returned if no
								  app hint is found.
@Output         value             Pointer to returned app hint value.
 */ /**************************************************************************/
#define OSGetKMAppHintBOOL(state, name, appHintDefault, value) \
	PVRSRVGetAppHint(state, # name, IMG_UINT_TYPE, appHintDefault, value)

/**************************************************************************/ /*!
@def OSGetKMAppHintSTRING(state, name, appHintDefault, buffer, size)
@Description    Interface for retrieval of string km app hint.
				For non-linux operating systems, this macro implements a call
				from server code to PVRSRVGetAppHint() declared in
				services_client_porting.h, effectively making it 'shared' code.
@Input          state             App hint state
@Input          name              Name used to identify app hint
@Input          appHintDefault    Default value to be returned if no
								  app hint is found.
@Output         buffer            Buffer used to return app hint string.
@Input			size			  Size of the buffer.
 */ /**************************************************************************/
#define OSGetKMAppHintSTRING(state, name, appHintDefault, buffer, size) \
	os_get_km_apphint_STRING(state, # name, appHintDefault, buffer, size)

/**************************************************************************/ /*!
@def OSCreateKMAppHintState(state)
@Description    Creates the app hint state.
				For non-linux operating systems, this macro implements a call
				from server code to PVRSRVCreateAppHintState() declared in
				services_client_porting.h, effectively making it 'shared' code.
@Output          state             App hint state
 */ /**************************************************************************/
#define OSCreateKMAppHintState(state) \
	PVRSRVCreateAppHintState(IMG_SRV_UM, 0, state)

/**************************************************************************/ /*!
@def OSFreeKMAppHintState
@Description    Free the app hint state.
				For non-linux operating systems, this macro implements a call
				from server code to PVRSRVCreateAppHintState() declared in
				services_client_porting.h, effectively making it 'shared' code.
@Output          state             App hint state
 */ /**************************************************************************/
#define OSFreeKMAppHintState(state) \
	PVRSRVFreeAppHintState(IMG_SRV_UM, state)

#endif /* #if defined(LINUX) */

#endif /* __OSKM_APPHINT_H__ */

/******************************************************************************
 End of file (oskm_apphint.h)
******************************************************************************/
