/*************************************************************************/ /*!
@Title          Resource Handle Manager
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provide resource handle management
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
#include "dbgdrvif.h"
#include "dbgdriv.h"

/* max number of streams held in SID info table */
#define MAX_SID_ENTRIES		8

typedef struct _SID_INFO
{
	PDBG_STREAM	psStream;
} SID_INFO, *PSID_INFO;

static SID_INFO gaSID_Xlat_Table[MAX_SID_ENTRIES];

IMG_SID PStream2SID(PDBG_STREAM psStream)
{
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		IMG_INT32 iIdx;

		for (iIdx = 0; iIdx < MAX_SID_ENTRIES; iIdx++)
		{
			if (psStream == gaSID_Xlat_Table[iIdx].psStream)
			{
				/* idx is one based */
				return (IMG_SID)iIdx+1;
			}
		}
	}

	return (IMG_SID)0;
}


PDBG_STREAM SID2PStream(IMG_SID hStream)
{
	/* changed to zero based */
	IMG_INT32 iIdx = (IMG_INT32)hStream-1;

	if (iIdx >= 0 && iIdx < MAX_SID_ENTRIES)
	{
		return gaSID_Xlat_Table[iIdx].psStream;
	}
	else
	{
    	return (PDBG_STREAM)IMG_NULL;
    }
}


IMG_BOOL AddSIDEntry(PDBG_STREAM psStream)
{
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		IMG_INT32 iIdx;

		for (iIdx = 0; iIdx < MAX_SID_ENTRIES; iIdx++)
		{
			if (psStream == gaSID_Xlat_Table[iIdx].psStream)
			{
				/* already created */
				return IMG_TRUE;
			}

			if (gaSID_Xlat_Table[iIdx].psStream == (PDBG_STREAM)IMG_NULL)
			{
				/* free entry */
				gaSID_Xlat_Table[iIdx].psStream = psStream;
				return IMG_TRUE;
			}
		}
	}

	return IMG_FALSE;
}

IMG_BOOL RemoveSIDEntry(PDBG_STREAM psStream)
{
	if (psStream != (PDBG_STREAM)IMG_NULL)
	{
		IMG_INT32 iIdx;

		for (iIdx = 0; iIdx < MAX_SID_ENTRIES; iIdx++)
		{
			if (psStream == gaSID_Xlat_Table[iIdx].psStream)
			{
				gaSID_Xlat_Table[iIdx].psStream = (PDBG_STREAM)IMG_NULL;
				return IMG_TRUE;
			}
		}
	}

	return IMG_FALSE;
}


/******************************************************************************
 End of file (handle.c)
******************************************************************************/
