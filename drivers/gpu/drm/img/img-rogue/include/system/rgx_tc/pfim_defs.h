/******************************************************************************
@Title          Odin PFIM definitions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Odin register defs for PDP-FBDC Interface Module
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
******************************************************************************/

#ifndef _PFIM_DEFS_H_
#define _PFIM_DEFS_H_

/* Supported FBC modes */
#define ODIN_PFIM_MOD_LINEAR       (0x00)
#define ODIN_PFIM_FBCDC_8X8_V12    (0x01)
#define ODIN_PFIM_FBCDC_16X4_V12   (0x02)
#define ODIN_PFIM_FBCDC_MAX        (0x03)

/* Supported pixel formats */
#define ODN_PFIM_PIXFMT_NONE       (0x00)
#define ODN_PFIM_PIXFMT_ARGB8888   (0x0C)
#define ODN_PFIM_PIXFMT_RGB565     (0x05)

/* Tile types */
#define ODN_PFIM_TILETYPE_8X8      (0x01)
#define ODN_PFIM_TILETYPE_16X4     (0x02)
#define ODN_PFIM_TILETYPE_32x2     (0x03)

#define PFIM_ROUNDUP(X, Y)         (((X) + ((Y) - 1U)) & ~((Y) - 1U))
#define PFIM_RND_TAG               (0x10)

#endif /* _PFIM_DEFS_H_ */

/******************************************************************************
 End of file (pfim_defs.h)
******************************************************************************/
