/*************************************************************************/ /*!
@Title          Hardware defs for MNEME.
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

#ifndef _MNEMEDEFS_KM_H_
#define _MNEMEDEFS_KM_H_

/* Register MNE_CR_CTRL */
#define MNE_CR_CTRL                         0x0D00
#define MNE_CR_CTRL_BYP_CC_N_MASK           0x00010000U
#define MNE_CR_CTRL_BYP_CC_N_SHIFT          16
#define MNE_CR_CTRL_BYP_CC_N_SIGNED         0
#define MNE_CR_CTRL_BYP_CC_MASK             0x00008000U
#define MNE_CR_CTRL_BYP_CC_SHIFT            15
#define MNE_CR_CTRL_BYP_CC_SIGNED           0
#define MNE_CR_CTRL_USE_INVAL_REQ_MASK      0x00007800U
#define MNE_CR_CTRL_USE_INVAL_REQ_SHIFT     11
#define MNE_CR_CTRL_USE_INVAL_REQ_SIGNED    0
#define MNE_CR_CTRL_BYPASS_ALL_MASK         0x00000400U
#define MNE_CR_CTRL_BYPASS_ALL_SHIFT        10
#define MNE_CR_CTRL_BYPASS_ALL_SIGNED       0
#define MNE_CR_CTRL_BYPASS_MASK             0x000003E0U
#define MNE_CR_CTRL_BYPASS_SHIFT            5
#define MNE_CR_CTRL_BYPASS_SIGNED           0
#define MNE_CR_CTRL_PAUSE_MASK              0x00000010U
#define MNE_CR_CTRL_PAUSE_SHIFT             4
#define MNE_CR_CTRL_PAUSE_SIGNED            0
/* Register MNE_CR_USE_INVAL */
#define MNE_CR_USE_INVAL                    0x0D04
#define MNE_CR_USE_INVAL_ADDR_MASK          0xFFFFFFFFU
#define MNE_CR_USE_INVAL_ADDR_SHIFT         0
#define MNE_CR_USE_INVAL_ADDR_SIGNED        0
/* Register MNE_CR_STAT */
#define MNE_CR_STAT                         0x0D08
#define MNE_CR_STAT_PAUSED_MASK             0x00000400U
#define MNE_CR_STAT_PAUSED_SHIFT            10
#define MNE_CR_STAT_PAUSED_SIGNED           0
#define MNE_CR_STAT_READS_MASK              0x000003FFU
#define MNE_CR_STAT_READS_SHIFT             0
#define MNE_CR_STAT_READS_SIGNED            0
/* Register MNE_CR_STAT_STATS */
#define MNE_CR_STAT_STATS                   0x0D0C
#define MNE_CR_STAT_STATS_RST_MASK          0x000FFFF0U
#define MNE_CR_STAT_STATS_RST_SHIFT         4
#define MNE_CR_STAT_STATS_RST_SIGNED        0
#define MNE_CR_STAT_STATS_SEL_MASK          0x0000000FU
#define MNE_CR_STAT_STATS_SEL_SHIFT         0
#define MNE_CR_STAT_STATS_SEL_SIGNED        0
/* Register MNE_CR_STAT_STATS_OUT */
#define MNE_CR_STAT_STATS_OUT               0x0D10
#define MNE_CR_STAT_STATS_OUT_VALUE_MASK    0xFFFFFFFFU
#define MNE_CR_STAT_STATS_OUT_VALUE_SHIFT   0
#define MNE_CR_STAT_STATS_OUT_VALUE_SIGNED  0
/* Register MNE_CR_EVENT_STATUS */
#define MNE_CR_EVENT_STATUS                 0x0D14
#define MNE_CR_EVENT_STATUS_INVAL_MASK      0x00000001U
#define MNE_CR_EVENT_STATUS_INVAL_SHIFT     0
#define MNE_CR_EVENT_STATUS_INVAL_SIGNED    0
/* Register MNE_CR_EVENT_CLEAR */
#define MNE_CR_EVENT_CLEAR                  0x0D18
#define MNE_CR_EVENT_CLEAR_INVAL_MASK       0x00000001U
#define MNE_CR_EVENT_CLEAR_INVAL_SHIFT      0
#define MNE_CR_EVENT_CLEAR_INVAL_SIGNED     0
/* Register MNE_CR_CTRL_INVAL */
#define MNE_CR_CTRL_INVAL                   0x0D20
#define MNE_CR_CTRL_INVAL_PREQ_PDS_MASK     0x00000008U
#define MNE_CR_CTRL_INVAL_PREQ_PDS_SHIFT    3
#define MNE_CR_CTRL_INVAL_PREQ_PDS_SIGNED   0
#define MNE_CR_CTRL_INVAL_PREQ_USEC_MASK    0x00000004U
#define MNE_CR_CTRL_INVAL_PREQ_USEC_SHIFT   2
#define MNE_CR_CTRL_INVAL_PREQ_USEC_SIGNED  0
#define MNE_CR_CTRL_INVAL_PREQ_CACHE_MASK   0x00000002U
#define MNE_CR_CTRL_INVAL_PREQ_CACHE_SHIFT  1
#define MNE_CR_CTRL_INVAL_PREQ_CACHE_SIGNED 0
#define MNE_CR_CTRL_INVAL_ALL_MASK          0x00000001U
#define MNE_CR_CTRL_INVAL_ALL_SHIFT         0
#define MNE_CR_CTRL_INVAL_ALL_SIGNED        0

#endif /* _MNEMEDEFS_KM_H_ */

