/***************************************************************************\
|*                                                                           *|
|*       Copyright 1993-1999 NVIDIA, Corporation.  All rights reserved.      *|
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
 * GPL licensing note -- nVidia is allowing a liberal interpretation of
 * the documentation restriction above, to merely say that this nVidia's
 * copyright and disclaimer should be included with all code derived
 * from this source.  -- Jeff Garzik <jgarzik@pobox.com>, 01/Nov/99 
 */

/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/riva_hw.h,v 1.21 2002/10/14 18:22:46 mvojkovi Exp $ */
#ifndef __RIVA_HW_H__
#define __RIVA_HW_H__
#define RIVA_SW_VERSION 0x00010003

#ifndef Bool
typedef int Bool;
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif

/*
 * Typedefs to force certain sized values.
 */
typedef unsigned char  U008;
typedef unsigned short U016;
typedef unsigned int   U032;

/*
 * HW access macros.
 */
#include <asm/io.h>

#define NV_WR08(p,i,d)  (__raw_writeb((d), (void __iomem *)(p) + (i)))
#define NV_RD08(p,i)    (__raw_readb((void __iomem *)(p) + (i)))
#define NV_WR16(p,i,d)  (__raw_writew((d), (void __iomem *)(p) + (i)))
#define NV_RD16(p,i)    (__raw_readw((void __iomem *)(p) + (i)))
#define NV_WR32(p,i,d)  (__raw_writel((d), (void __iomem *)(p) + (i)))
#define NV_RD32(p,i)    (__raw_readl((void __iomem *)(p) + (i)))

#define VGA_WR08(p,i,d) (writeb((d), (void __iomem *)(p) + (i)))
#define VGA_RD08(p,i)   (readb((void __iomem *)(p) + (i)))

/*
 * Define different architectures.
 */
#define NV_ARCH_03  0x03
#define NV_ARCH_04  0x04
#define NV_ARCH_10  0x10
#define NV_ARCH_20  0x20
#define NV_ARCH_30  0x30
#define NV_ARCH_40  0x40

/***************************************************************************\
*                                                                           *
*                             FIFO registers.                               *
*                                                                           *
\***************************************************************************/

/*
 * Raster OPeration. Windows style ROP3.
 */
typedef volatile struct
{
    U032 reserved00[4];
#ifdef __BIG_ENDIAN
    U032 FifoFree;
#else
    U016 FifoFree;
    U016 Nop;
#endif
    U032 reserved01[0x0BB];
    U032 Rop3;
} RivaRop;
/*
 * 8X8 Monochrome pattern.
 */
typedef volatile struct
{
    U032 reserved00[4];
#ifdef __BIG_ENDIAN
    U032 FifoFree;
#else
    U016 FifoFree;
    U016 Nop;
#endif
    U032 reserved01[0x0BD];
    U032 Shape;
    U032 reserved03[0x001];
    U032 Color0;
    U032 Color1;
    U032 Monochrome[2];
} RivaPattern;
/*
 * Scissor clip rectangle.
 */
typedef volatile struct
{
    U032 reserved00[4];
#ifdef __BIG_ENDIAN
    U032 FifoFree;
#else
    U016 FifoFree;
    U016 Nop;
#endif
    U032 reserved01[0x0BB];
    U032 TopLeft;
    U032 WidthHeight;
} RivaClip;
/*
 * 2D filled rectangle.
 */
typedef volatile struct
{
    U032 reserved00[4];
#ifdef __BIG_ENDIAN
    U032 FifoFree;
#else
    U016 FifoFree;
    U016 Nop[1];
#endif
    U032 reserved01[0x0BC];
    U032 Color;
    U032 reserved03[0x03E];
    U032 TopLeft;
    U032 WidthHeight;
} RivaRectangle;
/*
 * 2D screen-screen BLT.
 */
typedef volatile struct
{
    U032 reserved00[4];
#ifdef __BIG_ENDIAN
    U032 FifoFree;
#else
    U016 FifoFree;
    U016 Nop;
#endif
    U032 reserved01[0x0BB];
    U032 TopLeftSrc;
    U032 TopLeftDst;
    U032 WidthHeight;
} RivaScreenBlt;
/*
 * 2D pixel BLT.
 */
typedef volatile struct
{
    U032 reserved00[4];
#ifdef __BIG_ENDIAN
    U032 FifoFree;
#else
    U016 FifoFree;
    U016 Nop[1];
#endif
    U032 reserved01[0x0BC];
    U032 TopLeft;
    U032 WidthHeight;
    U032 WidthHeightIn;
    U032 reserved02[0x03C];
    U032 Pixels;
} RivaPixmap;
/*
 * Filled rectangle combined with monochrome expand.  Useful for glyphs.
 */
typedef volatile struct
{
    U032 reserved00[4];
#ifdef __BIG_ENDIAN
    U032 FifoFree;
#else
    U016 FifoFree;
    U016 Nop;
#endif
    U032 reserved01[0x0BB];
    U032 reserved03[(0x040)-1];
    U032 Color1A;
    struct
    {
        U032 TopLeft;
        U032 WidthHeight;
    } UnclippedRectangle[64];
    U032 reserved04[(0x080)-3];
    struct
    {
        U032 TopLeft;
        U032 BottomRight;
    } ClipB;
    U032 Color1B;
    struct
    {
        U032 TopLeft;
        U032 BottomRight;
    } ClippedRectangle[64];
    U032 reserved05[(0x080)-5];
    struct
    {
        U032 TopLeft;
        U032 BottomRight;
    } ClipC;
    U032 Color1C;
    U032 WidthHeightC;
    U032 PointC;
    U032 MonochromeData1C;
    U032 reserved06[(0x080)+121];
    struct
    {
        U032 TopLeft;
        U032 BottomRight;
    } ClipD;
    U032 Color1D;
    U032 WidthHeightInD;
    U032 WidthHeightOutD;
    U032 PointD;
    U032 MonochromeData1D;
    U032 reserved07[(0x080)+120];
    struct
    {
        U032 TopLeft;
        U032 BottomRight;
    } ClipE;
    U032 Color0E;
    U032 Color1E;
    U032 WidthHeightInE;
    U032 WidthHeightOutE;
    U032 PointE;
    U032 MonochromeData01E;
} RivaBitmap;
/*
 * 3D textured, Z buffered triangle.
 */
typedef volatile struct
{
    U032 reserved00[4];
#ifdef __BIG_ENDIAN
    U032 FifoFree;
#else
    U016 FifoFree;
    U016 Nop;
#endif
    U032 reserved01[0x0BC];
    U032 TextureOffset;
    U032 TextureFormat;
    U032 TextureFilter;
    U032 FogColor;
/* This is a problem on LynxOS */
#ifdef Control
#undef Control
#endif
    U032 Control;
    U032 AlphaTest;
    U032 reserved02[0x339];
    U032 FogAndIndex;
    U032 Color;
    float ScreenX;
    float ScreenY;
    float ScreenZ;
    float EyeM;
    float TextureS;
    float TextureT;
} RivaTexturedTriangle03;
typedef volatile struct
{
    U032 reserved00[4];
#ifdef __BIG_ENDIAN
    U032 FifoFree;
#else
    U016 FifoFree;
    U016 Nop;
#endif
    U032 reserved01[0x0BB];
    U032 ColorKey;
    U032 TextureOffset;
    U032 TextureFormat;
    U032 TextureFilter;
    U032 Blend;
/* This is a problem on LynxOS */
#ifdef Control
#undef Control
#endif
    U032 Control;
    U032 FogColor;
    U032 reserved02[0x39];
    struct
    {
        float ScreenX;
        float ScreenY;
        float ScreenZ;
        float EyeM;
        U032 Color;
        U032 Specular;
        float TextureS;
        float TextureT;
    } Vertex[16];
    U032 DrawTriangle3D;
} RivaTexturedTriangle05;
/*
 * 2D line.
 */
typedef volatile struct
{
    U032 reserved00[4];
#ifdef __BIG_ENDIAN
    U032 FifoFree;
#else
    U016 FifoFree;
    U016 Nop[1];
#endif
    U032 reserved01[0x0BC];
    U032 Color;             /* source color               0304-0307*/
    U032 Reserved02[0x03e];
    struct {                /* start aliased methods in array   0400-    */
        U032 point0;        /* y_x S16_S16 in pixels            0-   3*/
        U032 point1;        /* y_x S16_S16 in pixels            4-   7*/
    } Lin[16];              /* end of aliased methods in array      -047f*/
    struct {                /* start aliased methods in array   0480-    */
        U032 point0X;       /* in pixels, 0 at left                0-   3*/
        U032 point0Y;       /* in pixels, 0 at top                 4-   7*/
        U032 point1X;       /* in pixels, 0 at left                8-   b*/
        U032 point1Y;       /* in pixels, 0 at top                 c-   f*/
    } Lin32[8];             /* end of aliased methods in array      -04ff*/
    U032 PolyLin[32];       /* y_x S16_S16 in pixels         0500-057f*/
    struct {                /* start aliased methods in array   0580-    */
        U032 x;             /* in pixels, 0 at left                0-   3*/
        U032 y;             /* in pixels, 0 at top                 4-   7*/
    } PolyLin32[16];        /* end of aliased methods in array      -05ff*/
    struct {                /* start aliased methods in array   0600-    */
        U032 color;         /* source color                     0-   3*/
        U032 point;         /* y_x S16_S16 in pixels            4-   7*/
    } ColorPolyLin[16];     /* end of aliased methods in array      -067f*/
} RivaLine;
/*
 * 2D/3D surfaces
 */
typedef volatile struct
{
    U032 reserved00[4];
#ifdef __BIG_ENDIAN
    U032 FifoFree;
#else
    U016 FifoFree;
    U016 Nop;
#endif
    U032 reserved01[0x0BE];
    U032 Offset;
} RivaSurface;
typedef volatile struct
{
    U032 reserved00[4];
#ifdef __BIG_ENDIAN
    U032 FifoFree;
#else
    U016 FifoFree;
    U016 Nop;
#endif
    U032 reserved01[0x0BD];
    U032 Pitch;
    U032 RenderBufferOffset;
    U032 ZBufferOffset;
} RivaSurface3D;
    
/***************************************************************************\
*                                                                           *
*                        Virtualized RIVA H/W interface.                    *
*                                                                           *
\***************************************************************************/

#define FP_ENABLE  1
#define FP_DITHER  2

struct _riva_hw_inst;
struct _riva_hw_state;
/*
 * Virtialized chip interface. Makes RIVA 128 and TNT look alike.
 */
typedef struct _riva_hw_inst
{
    /*
     * Chip specific settings.
     */
    U032 Architecture;
    U032 Version;
    U032 Chipset;
    U032 CrystalFreqKHz;
    U032 RamAmountKBytes;
    U032 MaxVClockFreqKHz;
    U032 RamBandwidthKBytesPerSec;
    U032 EnableIRQ;
    U032 IO;
    U032 VBlankBit;
    U032 FifoFreeCount;
    U032 FifoEmptyCount;
    U032 CursorStart;
    U032 flatPanel;
    Bool twoHeads;
    /*
     * Non-FIFO registers.
     */
    volatile U032 __iomem *PCRTC0;
    volatile U032 __iomem *PCRTC;
    volatile U032 __iomem *PRAMDAC0;
    volatile U032 __iomem *PFB;
    volatile U032 __iomem *PFIFO;
    volatile U032 __iomem *PGRAPH;
    volatile U032 __iomem *PEXTDEV;
    volatile U032 __iomem *PTIMER;
    volatile U032 __iomem *PMC;
    volatile U032 __iomem *PRAMIN;
    volatile U032 __iomem *FIFO;
    volatile U032 __iomem *CURSOR;
    volatile U008 __iomem *PCIO0;
    volatile U008 __iomem *PCIO;
    volatile U008 __iomem *PVIO;
    volatile U008 __iomem *PDIO0;
    volatile U008 __iomem *PDIO;
    volatile U032 __iomem *PRAMDAC;
    /*
     * Common chip functions.
     */
    int  (*Busy)(struct _riva_hw_inst *);
    void (*LoadStateExt)(struct _riva_hw_inst *,struct _riva_hw_state *);
    void (*UnloadStateExt)(struct _riva_hw_inst *,struct _riva_hw_state *);
    void (*SetStartAddress)(struct _riva_hw_inst *,U032);
    void (*SetSurfaces2D)(struct _riva_hw_inst *,U032,U032);
    void (*SetSurfaces3D)(struct _riva_hw_inst *,U032,U032);
    int  (*ShowHideCursor)(struct _riva_hw_inst *,int);
    void (*LockUnlock)(struct _riva_hw_inst *, int);
    /*
     * Current extended mode settings.
     */
    struct _riva_hw_state *CurrentState;
    /*
     * FIFO registers.
     */
    RivaRop                 __iomem *Rop;
    RivaPattern             __iomem *Patt;
    RivaClip                __iomem *Clip;
    RivaPixmap              __iomem *Pixmap;
    RivaScreenBlt           __iomem *Blt;
    RivaBitmap              __iomem *Bitmap;
    RivaLine                __iomem *Line;
    RivaTexturedTriangle03  __iomem *Tri03;
    RivaTexturedTriangle05  __iomem *Tri05;
} RIVA_HW_INST;
/*
 * Extended mode state information.
 */
typedef struct _riva_hw_state
{
    U032 bpp;
    U032 width;
    U032 height;
    U032 interlace;
    U032 repaint0;
    U032 repaint1;
    U032 screen;
    U032 scale;
    U032 dither;
    U032 extra;
    U032 pixel;
    U032 horiz;
    U032 arbitration0;
    U032 arbitration1;
    U032 vpll;
    U032 vpll2;
    U032 pllsel;
    U032 general;
    U032 crtcOwner;
    U032 head; 
    U032 head2; 
    U032 config;
    U032 cursorConfig;	
    U032 cursor0;
    U032 cursor1;
    U032 cursor2;
    U032 offset0;
    U032 offset1;
    U032 offset2;
    U032 offset3;
    U032 pitch0;
    U032 pitch1;
    U032 pitch2;
    U032 pitch3;
} RIVA_HW_STATE;

/*
 * function prototypes
 */

extern int CalcStateExt
(
    RIVA_HW_INST  *chip,
    RIVA_HW_STATE *state,
    int            bpp,
    int            width,
    int            hDisplaySize,
    int            height,
    int            dotClock
);

/*
 * External routines.
 */
int RivaGetConfig(RIVA_HW_INST *, unsigned int);
/*
 * FIFO Free Count. Should attempt to yield processor if RIVA is busy.
 */

#define RIVA_FIFO_FREE(hwinst,hwptr,cnt)                            \
{                                                                   \
    while ((hwinst).FifoFreeCount < (cnt)) {                        \
	mb();mb();						    \
        (hwinst).FifoFreeCount = NV_RD32(&(hwinst).hwptr->FifoFree, 0) >> 2;     \
    }								    \
    (hwinst).FifoFreeCount -= (cnt);                                \
}
#endif /* __RIVA_HW_H__ */

