/*************************************************************************/ /*!
@File
@Title          Version numbers and strings.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Version numbers and strings for PVR Consumer services
                components.
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

#ifndef _PVRVERSION_H_
#define _PVRVERSION_H_

#define PVR_STR(X) #X
#define PVR_STR2(X) PVR_STR(X)

#define PVRVERSION_MAJ               1
#define PVRVERSION_MIN               10

#define PVRVERSION_FAMILY           "sgxddk"
#define PVRVERSION_BRANCHNAME       "1.10"
#define PVRVERSION_BUILD             2209507
#define PVRVERSION_BSCONTROL        "SGX_DDK_Android"

#define PVRVERSION_STRING           "SGX_DDK_Android sgxddk 1.10@" PVR_STR2(PVRVERSION_BUILD)
#define PVRVERSION_STRING_SHORT     "1.10@" PVR_STR2(PVRVERSION_BUILD)

#define COPYRIGHT_TXT               "Copyright (c) Imagination Technologies Ltd. All Rights Reserved."

#define PVRVERSION_BUILD_HI          220
#define PVRVERSION_BUILD_LO          9507
#define PVRVERSION_STRING_NUMERIC    PVR_STR2(PVRVERSION_MAJ) "." PVR_STR2(PVRVERSION_MIN) "." PVR_STR2(PVRVERSION_BUILD_HI) "." PVR_STR2(PVRVERSION_BUILD_LO)

#endif /* _PVRVERSION_H_ */
