/*************************************************************************/ /*!
@File           rgxfw_log_helper.h
@Title          Firmware TBI logging helper function
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Platform       Generic
@Description    This file contains some helper code to make TBI logging possible
                Specifically, it uses the SFIDLIST xmacro to trace ids back to
				the original strings.
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
#ifndef _RGXFW_LOG_HELPER_H_
#define _RGXFW_LOG_HELPER_H_

#include "rgx_fwif_sf.h"

static IMG_CHAR *const groups[]= {
#define X(A,B) #B,
	RGXFW_LOG_SFGROUPLIST
#undef X
};

typedef struct {
	IMG_UINT32 id;
	IMG_CHAR *name;
} tuple; /*  pair of string format id and string formats */

/*  The tuple pairs that will be generated using XMacros will be stored here.
 *   This macro definition must match the definition of SFids in rgx_fwif_sf.h */
static const tuple SFs[]= {
#define X(a, b, c, d, e) { RGXFW_LOG_CREATESFID(a,b,e) , d },
	RGXFW_LOG_SFIDLIST
#undef X
};
 
/*  idToStringID : Search SFs tuples {id,string} for a matching id.
 *   return index to array if found or RGXFW_SF_LAST if none found.
 *   bsearch could be used as ids are in increasing order. */
static IMG_UINT32 idToStringID(IMG_UINT32 ui32CheckData)
{
	IMG_UINT32 i = 0 ;
	for ( i = 0 ; SFs[i].id != RGXFW_SF_LAST ; i++)
	{
		if ( ui32CheckData == SFs[i].id )
		{
			return i;
		}
	}
	/* Nothing found, return max value */
	return RGXFW_SF_LAST;
}

#endif /* _RGXFW_LOG_HELPER_H_ */

