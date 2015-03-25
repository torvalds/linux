/*************************************************************************/ /*!
@File
@Title          RGX PDump panic definitions header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX PDump panic definitions header
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

#if !defined (RGX_PDUMP_PANICS_H_)
#define RGX_PDUMP_PANICS_H_


/*! Unique device specific IMG_UINT16 panic IDs to identify the cause of a
 * RGX PDump panic in a PDump script. */
typedef enum
{
	RGX_PDUMP_PANIC_UNDEFINED = 0,

	/* These panics occur when test parameters and driver configuration
	 * enable features that require the firmware and host driver to
	 * communicate. Such features are not supported with off-line playback.
	 */
	RGX_PDUMP_PANIC_ZSBUFFER_BACKING         = 101, /*!< Requests ZSBuffer to be backed with physical pages */
	RGX_PDUMP_PANIC_ZSBUFFER_UNBACKING       = 102, /*!< Requests ZSBuffer to be unbacked */
	RGX_PDUMP_PANIC_FREELIST_GROW            = 103, /*!< Requests an on-demand freelist grow/shrink */
	RGX_PDUMP_PANIC_FREELISTS_RECONSTRUCTION = 104, /*!< Requests freelists reconstruction */
} RGX_PDUMP_PANIC;
 

#endif /* RGX_PDUMP_PANICS_H_ */


