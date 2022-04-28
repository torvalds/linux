/*************************************************************************/ /*!
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

/* bonnie_tcf.h - Bonnie TCF register definitions */

/* tab size 4 */

#ifndef BONNIE_TCF_DEFS_H
#define BONNIE_TCF_DEFS_H

#define BONNIE_TCF_OFFSET_BONNIETC_REGBANK							0x00000000
#define BONNIE_TCF_OFFSET_TC_IFACE_COUNTERS							0x00004000
#define BONNIE_TCF_OFFSET_TC_TEST_MODULE_IMGV4_RTM_TOP				0x00008000
#define BONNIE_TCF_OFFSET_TC_TEST_MODULE_TCF_SCRATCH_PAD_SECN		0x0000C000
#define BONNIE_TCF_OFFSET_TC_TEST_MODULE_TCF_SCRATCH_PAD_DBG		0x00010000
#define BONNIE_TCF_OFFSET_MULTI_CLK_ALIGN							0x00014000
#define BONNIE_TCF_OFFSET_ALIGN_DATA_TX								0x00018000
#define BONNIE_TCF_OFFSET_SAI_RX_1									0x0001C000
#define BONNIE_TCF_OFFSET_SAI_RX_SDR								0x00040000
#define BONNIE_TCF_OFFSET_SAI_TX_1									0x00044000
#define BONNIE_TCF_OFFSET_SAI_TX_SDR								0x00068000

#define BONNIE_TCF_OFFSET_SAI_RX_DELTA								0x00004000
#define BONNIE_TCF_OFFSET_SAI_TX_DELTA								0x00004000

#define BONNIE_TCF_OFFSET_SAI_CLK_TAPS								0x0000000C
#define BONNIE_TCF_OFFSET_SAI_EYES									0x00000010
#define BONNIE_TCF_OFFSET_SAI_TRAIN_ACK								0x00000018


#endif /* BONNIE_TCF_DEFS_H */
