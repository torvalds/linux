/*************************************************************************/ /*!
@Title          SGX hw definitions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#ifndef _SGXDEFS_H_
#define	_SGXDEFS_H_

#include "sgxerrata.h"
#include "sgxfeaturedefs.h"

#if defined(SGX520)
#include "sgx520defs.h"
#else
#if defined(SGX530)
#include "sgx530defs.h"
#else
#if defined(SGX535)
#include "sgx535defs.h"
#else
#if defined(SGX535_V1_1)
#include "sgx535defs.h"
#else
#if defined(SGX540)
#include "sgx540defs.h"
#else
#if defined(SGX543)
#if defined(FIX_HW_BRN_29954)
#include "sgx543_v1.164defs.h"
#else
#include "sgx543defs.h"
#endif
#else
#if defined(SGX544)
#include "sgx544defs.h"
#else
#if defined(SGX545)
#include "sgx545defs.h"
#else
#if defined(SGX531)
#include "sgx531defs.h"
#else
#if defined(SGX554)
#include "sgx554defs.h"
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif

#if defined(SGX_FEATURE_MP)
#if defined(SGX554)
#include "sgxmpplusdefs.h"
#else
#include "sgxmpdefs.h"
#endif /* SGX554 */
#else /* SGX_FEATURE_MP */
#if defined(SGX_FEATURE_SYSTEM_CACHE)
#include "mnemedefs.h"
#endif
#endif /* SGX_FEATURE_MP */

/*****************************************************************************
 Core specific defines.
*****************************************************************************/

#endif /* _SGXDEFS_H_ */

/*****************************************************************************
 End of file (sgxdefs.h)
*****************************************************************************/
