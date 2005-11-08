/***************************************************************************\
|*                                                                           *|
|*       Copyright 1993-2003 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NOTICE TO USER:   The source code  is copyrighted under  U.S. and     *|
|*     international laws.  Users and possessors of this source code are     *|
|*     hereby granted a nonexclusive,  royalty-free copyright license to     *|
|*     use this code in individual and commercial software.                  *|
|*                                                                           *|
|*     Any use of this source code must include,  in the user documenta-     *|
|*     tion and  internal comments to the code,  notices to the end user     *|
|*     as follows:                                                           *|
|*                                                                           *|
|*       Copyright 1993-1999 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NVIDIA, CORPORATION MAKES NO REPRESENTATION ABOUT THE SUITABILITY     *|
|*     OF  THIS SOURCE  CODE  FOR ANY PURPOSE.  IT IS  PROVIDED  "AS IS"     *|
|*     WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.  NVIDIA, CORPOR-     *|
|*     ATION DISCLAIMS ALL WARRANTIES  WITH REGARD  TO THIS SOURCE CODE,     *|
|*     INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGE-     *|
|*     MENT,  AND FITNESS  FOR A PARTICULAR PURPOSE.   IN NO EVENT SHALL     *|
|*     NVIDIA, CORPORATION  BE LIABLE FOR ANY SPECIAL,  INDIRECT,  INCI-     *|
|*     DENTAL, OR CONSEQUENTIAL DAMAGES,  OR ANY DAMAGES  WHATSOEVER RE-     *|
|*     SULTING FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN ACTION     *|
|*     OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF     *|
|*     OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.     *|
|*                                                                           *|
|*     U.S. Government  End  Users.   This source code  is a "commercial     *|
|*     item,"  as that  term is  defined at  48 C.F.R. 2.101 (OCT 1995),     *|
|*     consisting  of "commercial  computer  software"  and  "commercial     *|
|*     computer  software  documentation,"  as such  terms  are  used in     *|
|*     48 C.F.R. 12.212 (SEPT 1995)  and is provided to the U.S. Govern-     *|
|*     ment only as  a commercial end item.   Consistent with  48 C.F.R.     *|
|*     12.212 and  48 C.F.R. 227.7202-1 through  227.7202-4 (JUNE 1995),     *|
|*     all U.S. Government End Users  acquire the source code  with only     *|
|*     those rights set forth herein.                                        *|
|*                                                                           *|
 \***************************************************************************/

/*
 * GPL Licensing Note - According to Mark Vojkovich, author of the Xorg/
 * XFree86 'nv' driver, this source code is provided under MIT-style licensing
 * where the source code is provided "as is" without warranty of any kind.
 * The only usage restriction is for the copyright notices to be retained
 * whenever code is used.
 *
 * Antonino Daplas <adaplas@pol.net> 2005-03-11
 */

#ifndef __NV_LOCAL_H__
#define __NV_LOCAL_H__

/*
 * This file includes any environment or machine specific values to access the
 * HW.  Put all affected includes, typdefs, etc. here so the riva_hw.* files
 * can stay generic in nature.
 */

/*
 * HW access macros.  These assume memory-mapped I/O, and not normal I/O space.
 */
#define NV_WR08(p,i,d)  (__raw_writeb((d), (void __iomem *)(p) + (i)))
#define NV_RD08(p,i)    (__raw_readb((void __iomem *)(p) + (i)))
#define NV_WR16(p,i,d)  (__raw_writew((d), (void __iomem *)(p) + (i)))
#define NV_RD16(p,i)    (__raw_readw((void __iomem *)(p) + (i)))
#define NV_WR32(p,i,d)  (__raw_writel((d), (void __iomem *)(p) + (i)))
#define NV_RD32(p,i)    (__raw_readl((void __iomem *)(p) + (i)))

/* VGA I/O is now always done through MMIO */
#define VGA_WR08(p,i,d) (writeb((d), (void __iomem *)(p) + (i)))
#define VGA_RD08(p,i)   (readb((void __iomem *)(p) + (i)))

#define NVDmaNext(par, data) \
     NV_WR32(&(par)->dmaBase[(par)->dmaCurrent++], 0, (data))

#define NVDmaStart(par, tag, size) {          \
     if((par)->dmaFree <= (size))             \
        NVDmaWait(par, size);                 \
     NVDmaNext(par, ((size) << 18) | (tag));  \
     (par)->dmaFree -= ((size) + 1);          \
}

#if defined(__i386__)
#define _NV_FENCE() outb(0, 0x3D0);
#else
#define _NV_FENCE() mb();
#endif

#define WRITE_PUT(par, data) {                   \
  _NV_FENCE()                                    \
  NV_RD08((par)->FbStart, 0);                    \
  NV_WR32(&(par)->FIFO[0x0010], 0, (data) << 2); \
  mb();                                          \
}

#define READ_GET(par) (NV_RD32(&(par)->FIFO[0x0011], 0) >> 2)

#ifdef __LITTLE_ENDIAN
#define reverse_order(l)        \
do {                            \
	u8 *a = (u8 *)(l);      \
	*a = byte_rev[*a], a++; \
	*a = byte_rev[*a], a++; \
	*a = byte_rev[*a], a++; \
	*a = byte_rev[*a];      \
} while(0)
#else
#define reverse_order(l) do { } while(0)
#endif                          /* __LITTLE_ENDIAN */

#endif				/* __NV_LOCAL_H__ */
