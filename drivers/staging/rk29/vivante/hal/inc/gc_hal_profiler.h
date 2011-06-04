/****************************************************************************
*
*    Copyright (C) 2005 - 2011 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/


#ifndef __gc_hal_profiler_h_
#define __gc_hal_profiler_h_

#ifdef __cplusplus
extern "C" {
#endif

#define GLVERTEX_OBJECT 10
#define GLVERTEX_OBJECT_BYTES 11

#define GLINDEX_OBJECT 20
#define GLINDEX_OBJECT_BYTES 21

#define GLTEXTURE_OBJECT 30
#define GLTEXTURE_OBJECT_BYTES 31

#if VIVANTE_PROFILER
#define gcmPROFILE_GC(Hal, Enum, Value)	gcoPROFILER_Count(Hal, Enum, Value)
#else
#define gcmPROFILE_GC(Hal, Enum, Value) do { } while (gcvFALSE)
#endif

#ifndef gcdNEW_PROFILER_FILE
#define gcdNEW_PROFILER_FILE    1
#endif

#ifdef gcdNEW_PROFILER_FILE

/* Category Constants. */
#define VPHEADER        0x010000
#define VPG_INFO        0x020000
#define VPG_TIME        0x030000
#define VPG_MEM         0x040000
#define VPG_ES11        0x050000
#define VPG_ES20        0x060000
#define VPG_VG11        0x070000
#define VPG_HAL         0x080000
#define VPG_HW          0x090000
#define VPG_GPU         0x0a0000
#define VPG_VS          0x0b0000
#define VPG_PS          0x0c0000
#define VPG_PA          0x0d0000
#define VPG_SETUP       0x0e0000
#define VPG_RA          0x0f0000
#define VPG_TX          0x100000
#define VPG_PE          0x110000
#define VPG_MC          0x120000
#define VPG_AXI         0x130000
#define VPG_PROG		0x140000
#define	VPG_PVS			0x150000
#define VPG_PPS			0x160000
#define VPG_FRAME       0x170000
#define VPG_END         0xff0000

/* Info. */
#define VPC_INFOCOMPANY         (VPG_INFO + 1)
#define VPC_INFOVERSION         (VPG_INFO + 2)
#define VPC_INFORENDERER        (VPG_INFO + 3)
#define VPC_INFOREVISION        (VPG_INFO + 4)
#define VPC_INFODRIVER          (VPG_INFO + 5)

/* Counter Constants. */
#define VPC_ELAPSETIME  0x030001
#define VPC_CPUTIME             0x030002

#define VPC_MEMMAXRES                   0x040001
#define VPC_MEMSHARED                   0x040002
#define VPC_MEMUNSHAREDDATA             0x040003
#define VPC_MEMUNSHAREDSTACK    0x040004

/* OpenGL ES11 Counters. */
#define VPC_ES11ACTIVETEXTURE   (VPG_ES11 + 1)
#define VPC_ES11ALPHAFUNC       (VPG_ES11 + 2)
#define VPC_ES11ALPHAFUNCX      (VPG_ES11 + 3)
#define VPC_ES11BINDBUFFER      (VPG_ES11 + 4)
#define VPC_ES11BINDTEXTURE     (VPG_ES11 + 5)
#define VPC_ES11BLENDFUNC       (VPG_ES11 + 6)
#define VPC_ES11BUFFERDATA      (VPG_ES11 + 7)
#define VPC_ES11BUFFERSUBDATA   (VPG_ES11 + 8)
#define VPC_ES11CLEAR   (VPG_ES11 + 9)
#define VPC_ES11CLEARCOLOR      (VPG_ES11 + 10)
#define VPC_ES11CLEARCOLORX     (VPG_ES11 + 11)
#define VPC_ES11CLEARDEPTHF     (VPG_ES11 + 12)
#define VPC_ES11CLEARDEPTHX     (VPG_ES11 + 13)
#define VPC_ES11CLEARSTENCIL    (VPG_ES11 + 14)
#define VPC_ES11CLIENTACTIVETEXTURE     (VPG_ES11 + 15)
#define VPC_ES11CLIPPLANEF      (VPG_ES11 + 16)
#define VPC_ES11CLIPPLANEX      (VPG_ES11 + 17)
#define VPC_ES11COLOR4F (VPG_ES11 + 18)
#define VPC_ES11COLOR4UB        (VPG_ES11 + 19)
#define VPC_ES11COLOR4X (VPG_ES11 + 20)
#define VPC_ES11COLORMASK       (VPG_ES11 + 21)
#define VPC_ES11COLORPOINTER    (VPG_ES11 + 22)
#define VPC_ES11COMPRESSEDTEXIMAGE2D    (VPG_ES11 + 23)
#define VPC_ES11COMPRESSEDTEXSUBIMAGE2D (VPG_ES11 + 24)
#define VPC_ES11COPYTEXIMAGE2D  (VPG_ES11 + 25)
#define VPC_ES11COPYTEXSUBIMAGE2D       (VPG_ES11 + 26)
#define VPC_ES11CULLFACE        (VPG_ES11 + 27)
#define VPC_ES11DELETEBUFFERS   (VPG_ES11 + 28)
#define VPC_ES11DELETETEXTURES  (VPG_ES11 + 29)
#define VPC_ES11DEPTHFUNC       (VPG_ES11 + 30)
#define VPC_ES11DEPTHMASK       (VPG_ES11 + 31)
#define VPC_ES11DEPTHRANGEF     (VPG_ES11 + 32)
#define VPC_ES11DEPTHRANGEX     (VPG_ES11 + 33)
#define VPC_ES11DISABLE (VPG_ES11 + 34)
#define VPC_ES11DISABLECLIENTSTATE      (VPG_ES11 + 35)
#define VPC_ES11DRAWARRAYS      (VPG_ES11 + 36)
#define VPC_ES11DRAWELEMENTS    (VPG_ES11 + 37)
#define VPC_ES11ENABLE  (VPG_ES11 + 38)
#define VPC_ES11ENABLECLIENTSTATE       (VPG_ES11 + 39)
#define VPC_ES11FINISH  (VPG_ES11 + 40)
#define VPC_ES11FLUSH   (VPG_ES11 + 41)
#define VPC_ES11FOGF    (VPG_ES11 + 42)
#define VPC_ES11FOGFV   (VPG_ES11 + 43)
#define VPC_ES11FOGX    (VPG_ES11 + 44)
#define VPC_ES11FOGXV   (VPG_ES11 + 45)
#define VPC_ES11FRONTFACE       (VPG_ES11 + 46)
#define VPC_ES11FRUSTUMF        (VPG_ES11 + 47)
#define VPC_ES11FRUSTUMX        (VPG_ES11 + 48)
#define VPC_ES11GENBUFFERS      (VPG_ES11 + 49)
#define VPC_ES11GENTEXTURES     (VPG_ES11 + 50)
#define VPC_ES11GETBOOLEANV     (VPG_ES11 + 51)
#define VPC_ES11GETBUFFERPARAMETERIV    (VPG_ES11 + 52)
#define VPC_ES11GETCLIPPLANEF   (VPG_ES11 + 53)
#define VPC_ES11GETCLIPPLANEX   (VPG_ES11 + 54)
#define VPC_ES11GETERROR        (VPG_ES11 + 55)
#define VPC_ES11GETFIXEDV       (VPG_ES11 + 56)
#define VPC_ES11GETFLOATV       (VPG_ES11 + 57)
#define VPC_ES11GETINTEGERV     (VPG_ES11 + 58)
#define VPC_ES11GETLIGHTFV      (VPG_ES11 + 59)
#define VPC_ES11GETLIGHTXV      (VPG_ES11 + 60)
#define VPC_ES11GETMATERIALFV   (VPG_ES11 + 61)
#define VPC_ES11GETMATERIALXV   (VPG_ES11 + 62)
#define VPC_ES11GETPOINTERV     (VPG_ES11 + 63)
#define VPC_ES11GETSTRING       (VPG_ES11 + 64)
#define VPC_ES11GETTEXENVFV     (VPG_ES11 + 65)
#define VPC_ES11GETTEXENVIV     (VPG_ES11 + 66)
#define VPC_ES11GETTEXENVXV     (VPG_ES11 + 67)
#define VPC_ES11GETTEXPARAMETERFV       (VPG_ES11 + 68)
#define VPC_ES11GETTEXPARAMETERIV       (VPG_ES11 + 69)
#define VPC_ES11GETTEXPARAMETERXV       (VPG_ES11 + 70)
#define VPC_ES11HINT    (VPG_ES11 + 71)
#define VPC_ES11ISBUFFER        (VPG_ES11 + 72)
#define VPC_ES11ISENABLED       (VPG_ES11 + 73)
#define VPC_ES11ISTEXTURE       (VPG_ES11 + 74)
#define VPC_ES11LIGHTF  (VPG_ES11 + 75)
#define VPC_ES11LIGHTFV (VPG_ES11 + 76)
#define VPC_ES11LIGHTMODELF     (VPG_ES11 + 77)
#define VPC_ES11LIGHTMODELFV    (VPG_ES11 + 78)
#define VPC_ES11LIGHTMODELX     (VPG_ES11 + 79)
#define VPC_ES11LIGHTMODELXV    (VPG_ES11 + 80)
#define VPC_ES11LIGHTX  (VPG_ES11 + 81)
#define VPC_ES11LIGHTXV (VPG_ES11 + 82)
#define VPC_ES11LINEWIDTH       (VPG_ES11 + 83)
#define VPC_ES11LINEWIDTHX      (VPG_ES11 + 84)
#define VPC_ES11LOADIDENTITY    (VPG_ES11 + 85)
#define VPC_ES11LOADMATRIXF     (VPG_ES11 + 86)
#define VPC_ES11LOADMATRIXX     (VPG_ES11 + 87)
#define VPC_ES11LOGICOP (VPG_ES11 + 88)
#define VPC_ES11MATERIALF       (VPG_ES11 + 89)
#define VPC_ES11MATERIALFV      (VPG_ES11 + 90)
#define VPC_ES11MATERIALX       (VPG_ES11 + 91)
#define VPC_ES11MATERIALXV      (VPG_ES11 + 92)
#define VPC_ES11MATRIXMODE      (VPG_ES11 + 93)
#define VPC_ES11MULTITEXCOORD4F (VPG_ES11 + 94)
#define VPC_ES11MULTITEXCOORD4X (VPG_ES11 + 95)
#define VPC_ES11MULTMATRIXF     (VPG_ES11 + 96)
#define VPC_ES11MULTMATRIXX     (VPG_ES11 + 97)
#define VPC_ES11NORMAL3F        (VPG_ES11 + 98)
#define VPC_ES11NORMAL3X        (VPG_ES11 + 99)
#define VPC_ES11NORMALPOINTER   (VPG_ES11 + 100)
#define VPC_ES11ORTHOF  (VPG_ES11 + 101)
#define VPC_ES11ORTHOX  (VPG_ES11 + 102)
#define VPC_ES11PIXELSTOREI     (VPG_ES11 + 103)
#define VPC_ES11POINTPARAMETERF (VPG_ES11 + 104)
#define VPC_ES11POINTPARAMETERFV        (VPG_ES11 + 105)
#define VPC_ES11POINTPARAMETERX (VPG_ES11 + 106)
#define VPC_ES11POINTPARAMETERXV        (VPG_ES11 + 107)
#define VPC_ES11POINTSIZE       (VPG_ES11 + 108)
#define VPC_ES11POINTSIZEX      (VPG_ES11 + 109)
#define VPC_ES11POLYGONOFFSET   (VPG_ES11 + 110)
#define VPC_ES11POLYGONOFFSETX  (VPG_ES11 + 111)
#define VPC_ES11POPMATRIX       (VPG_ES11 + 112)
#define VPC_ES11PUSHMATRIX      (VPG_ES11 + 113)
#define VPC_ES11READPIXELS      (VPG_ES11 + 114)
#define VPC_ES11ROTATEF (VPG_ES11 + 115)
#define VPC_ES11ROTATEX (VPG_ES11 + 116)
#define VPC_ES11SAMPLECOVERAGE  (VPG_ES11 + 117)
#define VPC_ES11SAMPLECOVERAGEX (VPG_ES11 + 118)
#define VPC_ES11SCALEF  (VPG_ES11 + 119)
#define VPC_ES11SCALEX  (VPG_ES11 + 120)
#define VPC_ES11SCISSOR (VPG_ES11 + 121)
#define VPC_ES11SHADEMODEL      (VPG_ES11 + 122)
#define VPC_ES11STENCILFUNC     (VPG_ES11 + 123)
#define VPC_ES11STENCILMASK     (VPG_ES11 + 124)
#define VPC_ES11STENCILOP       (VPG_ES11 + 125)
#define VPC_ES11TEXCOORDPOINTER (VPG_ES11 + 126)
#define VPC_ES11TEXENVF (VPG_ES11 + 127)
#define VPC_ES11TEXENVFV        (VPG_ES11 + 128)
#define VPC_ES11TEXENVI (VPG_ES11 + 129)
#define VPC_ES11TEXENVIV        (VPG_ES11 + 130)
#define VPC_ES11TEXENVX (VPG_ES11 + 131)
#define VPC_ES11TEXENVXV        (VPG_ES11 + 132)
#define VPC_ES11TEXIMAGE2D      (VPG_ES11 + 133)
#define VPC_ES11TEXPARAMETERF   (VPG_ES11 + 134)
#define VPC_ES11TEXPARAMETERFV  (VPG_ES11 + 135)
#define VPC_ES11TEXPARAMETERI   (VPG_ES11 + 136)
#define VPC_ES11TEXPARAMETERIV  (VPG_ES11 + 137)
#define VPC_ES11TEXPARAMETERX   (VPG_ES11 + 138)
#define VPC_ES11TEXPARAMETERXV  (VPG_ES11 + 139)
#define VPC_ES11TEXSUBIMAGE2D   (VPG_ES11 + 140)
#define VPC_ES11TRANSLATEF      (VPG_ES11 + 141)
#define VPC_ES11TRANSLATEX      (VPG_ES11 + 142)
#define VPC_ES11VERTEXPOINTER   (VPG_ES11 + 143)
#define VPC_ES11VIEWPORT        (VPG_ES11 + 144)
#define VPC_ES11CALLS   (VPG_ES11 + 145)
#define VPC_ES11DRAWCALLS       (VPG_ES11 + 146)
#define VPC_ES11STATECHANGECALLS        (VPG_ES11 + 147)
#define VPC_ES11POINTCOUNT      (VPG_ES11 + 148)
#define VPC_ES11LINECOUNT       (VPG_ES11 + 149)
#define VPC_ES11TRIANGLECOUNT   (VPG_ES11 + 150)

/* OpenGLES 2.x */
#define VPC_ES20ACTIVETEXTURE   (VPG_ES20 + 1)
#define VPC_ES20ATTACHSHADER    (VPG_ES20 + 2)
#define VPC_ES20BINDATTRIBLOCATION      (VPG_ES20 + 3)
#define VPC_ES20BINDBUFFER      (VPG_ES20 + 4)
#define VPC_ES20BINDFRAMEBUFFER (VPG_ES20 + 5)
#define VPC_ES20BINDRENDERBUFFER        (VPG_ES20 + 6)
#define VPC_ES20BINDTEXTURE     (VPG_ES20 + 7)
#define VPC_ES20BLENDCOLOR      (VPG_ES20 + 8)
#define VPC_ES20BLENDEQUATION   (VPG_ES20 + 9)
#define VPC_ES20BLENDEQUATIONSEPARATE   (VPG_ES20 + 10)
#define VPC_ES20BLENDFUNC       (VPG_ES20 + 11)
#define VPC_ES20BLENDFUNCSEPARATE       (VPG_ES20 + 12)
#define VPC_ES20BUFFERDATA      (VPG_ES20 + 13)
#define VPC_ES20BUFFERSUBDATA   (VPG_ES20 + 14)
#define VPC_ES20CHECKFRAMEBUFFERSTATUS  (VPG_ES20 + 15)
#define VPC_ES20CLEAR   (VPG_ES20 + 16)
#define VPC_ES20CLEARCOLOR      (VPG_ES20 + 17)
#define VPC_ES20CLEARDEPTHF     (VPG_ES20 + 18)
#define VPC_ES20CLEARSTENCIL    (VPG_ES20 + 19)
#define VPC_ES20COLORMASK       (VPG_ES20 + 20)
#define VPC_ES20COMPILESHADER   (VPG_ES20 + 21)
#define VPC_ES20COMPRESSEDTEXIMAGE2D    (VPG_ES20 + 22)
#define VPC_ES20COMPRESSEDTEXSUBIMAGE2D (VPG_ES20 + 23)
#define VPC_ES20COPYTEXIMAGE2D  (VPG_ES20 + 24)
#define VPC_ES20COPYTEXSUBIMAGE2D       (VPG_ES20 + 25)
#define VPC_ES20CREATEPROGRAM   (VPG_ES20 + 26)
#define VPC_ES20CREATESHADER    (VPG_ES20 + 27)
#define VPC_ES20CULLFACE        (VPG_ES20 + 28)
#define VPC_ES20DELETEBUFFERS   (VPG_ES20 + 29)
#define VPC_ES20DELETEFRAMEBUFFERS      (VPG_ES20 + 30)
#define VPC_ES20DELETEPROGRAM   (VPG_ES20 + 31)
#define VPC_ES20DELETERENDERBUFFERS     (VPG_ES20 + 32)
#define VPC_ES20DELETESHADER    (VPG_ES20 + 33)
#define VPC_ES20DELETETEXTURES  (VPG_ES20 + 34)
#define VPC_ES20DEPTHFUNC       (VPG_ES20 + 35)
#define VPC_ES20DEPTHMASK       (VPG_ES20 + 36)
#define VPC_ES20DEPTHRANGEF     (VPG_ES20 + 37)
#define VPC_ES20DETACHSHADER    (VPG_ES20 + 38)
#define VPC_ES20DISABLE (VPG_ES20 + 39)
#define VPC_ES20DISABLEVERTEXATTRIBARRAY        (VPG_ES20 + 40)
#define VPC_ES20DRAWARRAYS      (VPG_ES20 + 41)
#define VPC_ES20DRAWELEMENTS    (VPG_ES20 + 42)
#define VPC_ES20ENABLE  (VPG_ES20 + 43)
#define VPC_ES20ENABLEVERTEXATTRIBARRAY (VPG_ES20 + 44)
#define VPC_ES20FINISH  (VPG_ES20 + 45)
#define VPC_ES20FLUSH   (VPG_ES20 + 46)
#define VPC_ES20FRAMEBUFFERRENDERBUFFER (VPG_ES20 + 47)
#define VPC_ES20FRAMEBUFFERTEXTURE2D    (VPG_ES20 + 48)
#define VPC_ES20FRONTFACE       (VPG_ES20 + 49)
#define VPC_ES20GENBUFFERS      (VPG_ES20 + 50)
#define VPC_ES20GENERATEMIPMAP  (VPG_ES20 + 51)
#define VPC_ES20GENFRAMEBUFFERS (VPG_ES20 + 52)
#define VPC_ES20GENRENDERBUFFERS        (VPG_ES20 + 53)
#define VPC_ES20GENTEXTURES     (VPG_ES20 + 54)
#define VPC_ES20GETACTIVEATTRIB (VPG_ES20 + 55)
#define VPC_ES20GETACTIVEUNIFORM        (VPG_ES20 + 56)
#define VPC_ES20GETATTACHEDSHADERS      (VPG_ES20 + 57)
#define VPC_ES20GETATTRIBLOCATION       (VPG_ES20 + 58)
#define VPC_ES20GETBOOLEANV     (VPG_ES20 + 59)
#define VPC_ES20GETBUFFERPARAMETERIV    (VPG_ES20 + 60)
#define VPC_ES20GETERROR        (VPG_ES20 + 61)
#define VPC_ES20GETFLOATV       (VPG_ES20 + 62)
#define VPC_ES20GETFRAMEBUFFERATTACHMENTPARAMETERIV     (VPG_ES20 + 63)
#define VPC_ES20GETINTEGERV     (VPG_ES20 + 64)
#define VPC_ES20GETPROGRAMIV    (VPG_ES20 + 65)
#define VPC_ES20GETPROGRAMINFOLOG       (VPG_ES20 + 66)
#define VPC_ES20GETRENDERBUFFERPARAMETERIV      (VPG_ES20 + 67)
#define VPC_ES20GETSHADERIV     (VPG_ES20 + 68)
#define VPC_ES20GETSHADERINFOLOG        (VPG_ES20 + 69)
#define VPC_ES20GETSHADERPRECISIONFORMAT        (VPG_ES20 + 70)
#define VPC_ES20GETSHADERSOURCE (VPG_ES20 + 71)
#define VPC_ES20GETSTRING       (VPG_ES20 + 72)
#define VPC_ES20GETTEXPARAMETERFV       (VPG_ES20 + 73)
#define VPC_ES20GETTEXPARAMETERIV       (VPG_ES20 + 74)
#define VPC_ES20GETUNIFORMFV    (VPG_ES20 + 75)
#define VPC_ES20GETUNIFORMIV    (VPG_ES20 + 76)
#define VPC_ES20GETUNIFORMLOCATION      (VPG_ES20 + 77)
#define VPC_ES20GETVERTEXATTRIBFV       (VPG_ES20 + 78)
#define VPC_ES20GETVERTEXATTRIBIV       (VPG_ES20 + 79)
#define VPC_ES20GETVERTEXATTRIBPOINTERV (VPG_ES20 + 80)
#define VPC_ES20HINT    (VPG_ES20 + 81)
#define VPC_ES20ISBUFFER        (VPG_ES20 + 82)
#define VPC_ES20ISENABLED       (VPG_ES20 + 83)
#define VPC_ES20ISFRAMEBUFFER   (VPG_ES20 + 84)
#define VPC_ES20ISPROGRAM       (VPG_ES20 + 85)
#define VPC_ES20ISRENDERBUFFER  (VPG_ES20 + 86)
#define VPC_ES20ISSHADER        (VPG_ES20 + 87)
#define VPC_ES20ISTEXTURE       (VPG_ES20 + 88)
#define VPC_ES20LINEWIDTH       (VPG_ES20 + 89)
#define VPC_ES20LINKPROGRAM     (VPG_ES20 + 90)
#define VPC_ES20PIXELSTOREI     (VPG_ES20 + 91)
#define VPC_ES20POLYGONOFFSET   (VPG_ES20 + 92)
#define VPC_ES20READPIXELS      (VPG_ES20 + 93)
#define VPC_ES20RELEASESHADERCOMPILER   (VPG_ES20 + 94)
#define VPC_ES20RENDERBUFFERSTORAGE     (VPG_ES20 + 95)
#define VPC_ES20SAMPLECOVERAGE  (VPG_ES20 + 96)
#define VPC_ES20SCISSOR (VPG_ES20 + 97)
#define VPC_ES20SHADERBINARY    (VPG_ES20 + 98)
#define VPC_ES20SHADERSOURCE    (VPG_ES20 + 99)
#define VPC_ES20STENCILFUNC     (VPG_ES20 + 100)
#define VPC_ES20STENCILFUNCSEPARATE     (VPG_ES20 + 101)
#define VPC_ES20STENCILMASK     (VPG_ES20 + 102)
#define VPC_ES20STENCILMASKSEPARATE     (VPG_ES20 + 103)
#define VPC_ES20STENCILOP       (VPG_ES20 + 104)
#define VPC_ES20STENCILOPSEPARATE       (VPG_ES20 + 105)
#define VPC_ES20TEXIMAGE2D      (VPG_ES20 + 106)
#define VPC_ES20TEXPARAMETERF   (VPG_ES20 + 107)
#define VPC_ES20TEXPARAMETERFV  (VPG_ES20 + 108)
#define VPC_ES20TEXPARAMETERI   (VPG_ES20 + 109)
#define VPC_ES20TEXPARAMETERIV  (VPG_ES20 + 110)
#define VPC_ES20TEXSUBIMAGE2D   (VPG_ES20 + 111)
#define VPC_ES20UNIFORM1F       (VPG_ES20 + 112)
#define VPC_ES20UNIFORM1FV      (VPG_ES20 + 113)
#define VPC_ES20UNIFORM1I       (VPG_ES20 + 114)
#define VPC_ES20UNIFORM1IV      (VPG_ES20 + 115)
#define VPC_ES20UNIFORM2F       (VPG_ES20 + 116)
#define VPC_ES20UNIFORM2FV      (VPG_ES20 + 117)
#define VPC_ES20UNIFORM2I       (VPG_ES20 + 118)
#define VPC_ES20UNIFORM2IV      (VPG_ES20 + 119)
#define VPC_ES20UNIFORM3F       (VPG_ES20 + 120)
#define VPC_ES20UNIFORM3FV      (VPG_ES20 + 121)
#define VPC_ES20UNIFORM3I       (VPG_ES20 + 122)
#define VPC_ES20UNIFORM3IV      (VPG_ES20 + 123)
#define VPC_ES20UNIFORM4F       (VPG_ES20 + 124)
#define VPC_ES20UNIFORM4FV      (VPG_ES20 + 125)
#define VPC_ES20UNIFORM4I       (VPG_ES20 + 126)
#define VPC_ES20UNIFORM4IV      (VPG_ES20 + 127)
#define VPC_ES20UNIFORMMATRIX2FV        (VPG_ES20 + 128)
#define VPC_ES20UNIFORMMATRIX3FV        (VPG_ES20 + 129)
#define VPC_ES20UNIFORMMATRIX4FV        (VPG_ES20 + 130)
#define VPC_ES20USEPROGRAM      (VPG_ES20 + 131)
#define VPC_ES20VALIDATEPROGRAM (VPG_ES20 + 132)
#define VPC_ES20VERTEXATTRIB1F  (VPG_ES20 + 133)
#define VPC_ES20VERTEXATTRIB1FV (VPG_ES20 + 134)
#define VPC_ES20VERTEXATTRIB2F  (VPG_ES20 + 135)
#define VPC_ES20VERTEXATTRIB2FV (VPG_ES20 + 136)
#define VPC_ES20VERTEXATTRIB3F  (VPG_ES20 + 137)
#define VPC_ES20VERTEXATTRIB3FV (VPG_ES20 + 138)
#define VPC_ES20VERTEXATTRIB4F  (VPG_ES20 + 139)
#define VPC_ES20VERTEXATTRIB4FV (VPG_ES20 + 140)
#define VPC_ES20VERTEXATTRIBPOINTER     (VPG_ES20 + 141)
#define VPC_ES20VIEWPORT        (VPG_ES20 + 142)
#define VPC_ES20CALLS   (VPG_ES20 + 143)
#define VPC_ES20DRAWCALLS   (VPG_ES20 + 144)
#define VPC_ES20STATECHANGECALLS    (VPG_ES20 + 145)
#define VPC_ES20POINTCOUNT   (VPG_ES20 + 146)
#define VPC_ES20LINECOUNT    (VPG_ES20 + 147)
#define VPC_ES20TRIANGLECOUNT    (VPG_ES20 + 148)

/* VG11 Counters. */
#define VPC_VG11APPENDPATH              (VPG_VG11 + 1)
#define VPC_VG11APPENDPATHDATA          (VPG_VG11 + 2)
#define VPC_VG11CHILDIMAGE              (VPG_VG11 + 3)
#define VPC_VG11CLEAR                   (VPG_VG11 + 4)
#define VPC_VG11CLEARGLYPH              (VPG_VG11 + 5)
#define VPC_VG11CLEARIMAGE              (VPG_VG11 + 6)
#define VPC_VG11CLEARPATH               (VPG_VG11 + 7)
#define VPC_VG11COLORMATRIX             (VPG_VG11 + 8)
#define VPC_VG11CONVOLVE                (VPG_VG11 + 9)
#define VPC_VG11COPYIMAGE               (VPG_VG11 + 10)
#define VPC_VG11COPYMASK                (VPG_VG11 + 11)
#define VPC_VG11COPYPIXELS              (VPG_VG11 + 12)
#define VPC_VG11CREATEFONT              (VPG_VG11 + 13)
#define VPC_VG11CREATEIMAGE             (VPG_VG11 + 14)
#define VPC_VG11CREATEMASKLAYER         (VPG_VG11 + 15)
#define VPC_VG11CREATEPAINT             (VPG_VG11 + 16)
#define VPC_VG11CREATEPATH              (VPG_VG11 + 17)
#define VPC_VG11DESTROYFONT             (VPG_VG11 + 18)
#define VPC_VG11DESTROYIMAGE            (VPG_VG11 + 19)
#define VPC_VG11DESTROYMASKLAYER        (VPG_VG11 + 20)
#define VPC_VG11DESTROYPAINT            (VPG_VG11 + 21)
#define VPC_VG11DESTROYPATH             (VPG_VG11 + 22)
#define VPC_VG11DRAWGLYPH               (VPG_VG11 + 23)
#define VPC_VG11DRAWGLYPHS              (VPG_VG11 + 24)
#define VPC_VG11DRAWIMAGE               (VPG_VG11 + 25)
#define VPC_VG11DRAWPATH                (VPG_VG11 + 26)
#define VPC_VG11FILLMASKLAYER           (VPG_VG11 + 27)
#define VPC_VG11FINISH                  (VPG_VG11 + 28)
#define VPC_VG11FLUSH                   (VPG_VG11 + 29)
#define VPC_VG11GAUSSIANBLUR            (VPG_VG11 + 30)
#define VPC_VG11GETCOLOR                (VPG_VG11 + 31)
#define VPC_VG11GETERROR                (VPG_VG11 + 32)
#define VPC_VG11GETF                    (VPG_VG11 + 33)
#define VPC_VG11GETFV                   (VPG_VG11 + 34)
#define VPC_VG11GETI                    (VPG_VG11 + 35)
#define VPC_VG11GETIMAGESUBDATA         (VPG_VG11 + 36)
#define VPC_VG11GETIV                   (VPG_VG11 + 37)
#define VPC_VG11GETMATRIX               (VPG_VG11 + 38)
#define VPC_VG11GETPAINT                (VPG_VG11 + 39)
#define VPC_VG11GETPARAMETERF           (VPG_VG11 + 40)
#define VPC_VG11GETPARAMETERFV          (VPG_VG11 + 41)
#define VPC_VG11GETPARAMETERI           (VPG_VG11 + 42)
#define VPC_VG11GETPARAMETERIV          (VPG_VG11 + 43)
#define VPC_VG11GETPARAMETERVECTORSIZE  (VPG_VG11 + 44)
#define VPC_VG11GETPARENT               (VPG_VG11 + 45)
#define VPC_VG11GETPATHCAPABILITIES     (VPG_VG11 + 46)
#define VPC_VG11GETPIXELS               (VPG_VG11 + 47)
#define VPC_VG11GETSTRING               (VPG_VG11 + 48)
#define VPC_VG11GETVECTORSIZE           (VPG_VG11 + 49)
#define VPC_VG11HARDWAREQUERY           (VPG_VG11 + 50)
#define VPC_VG11IMAGESUBDATA            (VPG_VG11 + 51)
#define VPC_VG11INTERPOLATEPATH         (VPG_VG11 + 52)
#define VPC_VG11LOADIDENTITY            (VPG_VG11 + 53)
#define VPC_VG11LOADMATRIX              (VPG_VG11 + 54)
#define VPC_VG11LOOKUP                  (VPG_VG11 + 55)
#define VPC_VG11LOOKUPSINGLE            (VPG_VG11 + 56)
#define VPC_VG11MASK                    (VPG_VG11 + 57)
#define VPC_VG11MODIFYPATHCOORDS        (VPG_VG11 + 58)
#define VPC_VG11MULTMATRIX              (VPG_VG11 + 59)
#define VPC_VG11PAINTPATTERN            (VPG_VG11 + 60)
#define VPC_VG11PATHBOUNDS              (VPG_VG11 + 61)
#define VPC_VG11PATHLENGTH              (VPG_VG11 + 62)
#define VPC_VG11PATHTRANSFORMEDBOUNDS   (VPG_VG11 + 63)
#define VPC_VG11POINTALONGPATH          (VPG_VG11 + 64)
#define VPC_VG11READPIXELS              (VPG_VG11 + 65)
#define VPC_VG11REMOVEPATHCAPABILITIES  (VPG_VG11 + 66)
#define VPC_VG11RENDERTOMASK            (VPG_VG11 + 67)
#define VPC_VG11ROTATE                  (VPG_VG11 + 68)
#define VPC_VG11SCALE                   (VPG_VG11 + 69)
#define VPC_VG11SEPARABLECONVOLVE       (VPG_VG11 + 70)
#define VPC_VG11SETCOLOR                (VPG_VG11 + 71)
#define VPC_VG11SETF                    (VPG_VG11 + 72)
#define VPC_VG11SETFV                   (VPG_VG11 + 73)
#define VPC_VG11SETGLYPHTOIMAGE         (VPG_VG11 + 74)
#define VPC_VG11SETGLYPHTOPATH          (VPG_VG11 + 75)
#define VPC_VG11SETI                    (VPG_VG11 + 76)
#define VPC_VG11SETIV                   (VPG_VG11 + 77)
#define VPC_VG11SETPAINT                (VPG_VG11 + 78)
#define VPC_VG11SETPARAMETERF           (VPG_VG11 + 79)
#define VPC_VG11SETPARAMETERFV          (VPG_VG11 + 80)
#define VPC_VG11SETPARAMETERI           (VPG_VG11 + 81)
#define VPC_VG11SETPARAMETERIV          (VPG_VG11 + 82)
#define VPC_VG11SETPIXELS               (VPG_VG11 + 83)
#define VPC_VG11SHEAR                   (VPG_VG11 + 84)
#define VPC_VG11TRANSFORMPATH           (VPG_VG11 + 85)
#define VPC_VG11TRANSLATE               (VPG_VG11 + 86)
#define VPC_VG11WRITEPIXELS             (VPG_VG11 + 87)
#define VPC_VG11CALLS                                   (VPG_VG11 + 88)
#define VPC_VG11DRAWCALLS                               (VPG_VG11 + 89)
#define VPC_VG11STATECHANGECALLS                (VPG_VG11 + 90)
#define VPC_VG11FILLCOUNT                               (VPG_VG11 + 91)
#define VPC_VG11STROKECOUNT                             (VPG_VG11 + 92)

/* HAL Counters. */
#define VPC_HALVERTBUFNEWBYTEALLOC              (VPG_HAL + 1)
#define VPC_HALVERTBUFTOTALBYTEALLOC    (VPG_HAL + 2)
#define VPC_HALVERTBUFNEWOBJALLOC               (VPG_HAL + 3)
#define VPC_HALVERTBUFTOTALOBJALLOC             (VPG_HAL + 4)
#define VPC_HALINDBUFNEWBYTEALLOC               (VPG_HAL + 5)
#define VPC_HALINDBUFTOTALBYTEALLOC             (VPG_HAL + 6)
#define VPC_HALINDBUFNEWOBJALLOC                (VPG_HAL + 7)
#define VPC_HALINDBUFTOTALOBJALLOC              (VPG_HAL + 8)
#define VPC_HALTEXBUFNEWBYTEALLOC               (VPG_HAL + 9)
#define VPC_HALTEXBUFTOTALBYTEALLOC             (VPG_HAL + 10)
#define VPC_HALTEXBUFNEWOBJALLOC                (VPG_HAL + 11)
#define VPC_HALTEXBUFTOTALOBJALLOC              (VPG_HAL + 12)

#define VPC_VG11TRANSLATE               (VPG_VG11 + 86)
#define VPC_VG11WRITEPIXELS             (VPG_VG11 + 87)
#define VPC_VG11CALLS                                   (VPG_VG11 + 88)
#define VPC_VG11DRAWCALLS                               (VPG_VG11 + 89)
#define VPC_VG11STATECHANGECALLS                (VPG_VG11 + 90)
#define VPC_VG11FILLCOUNT                               (VPG_VG11 + 91)
#define VPC_VG11STROKECOUNT                             (VPG_VG11 + 92)

/* HAL Counters. */
#define VPC_HALVERTBUFNEWBYTEALLOC              (VPG_HAL + 1)
#define VPC_HALVERTBUFTOTALBYTEALLOC    (VPG_HAL + 2)
#define VPC_HALVERTBUFNEWOBJALLOC               (VPG_HAL + 3)
#define VPC_HALVERTBUFTOTALOBJALLOC             (VPG_HAL + 4)
#define VPC_HALINDBUFNEWBYTEALLOC               (VPG_HAL + 5)
#define VPC_HALINDBUFTOTALBYTEALLOC             (VPG_HAL + 6)
#define VPC_HALINDBUFNEWOBJALLOC                (VPG_HAL + 7)
#define VPC_HALINDBUFTOTALOBJALLOC              (VPG_HAL + 8)
#define VPC_HALTEXBUFNEWBYTEALLOC               (VPG_HAL + 9)
#define VPC_HALTEXBUFTOTALBYTEALLOC             (VPG_HAL + 10)
#define VPC_HALTEXBUFNEWOBJALLOC                (VPG_HAL + 11)
#define VPC_HALTEXBUFTOTALOBJALLOC              (VPG_HAL + 12)

/* HW: GPU Counters. */
#define VPC_GPUCYCLES                                   (VPG_GPU + 1)
#define VPC_GPUREAD64BYTE                               (VPG_GPU + 2)
#define VPC_GPUWRITE64BYTE                              (VPG_GPU + 3)

/* HW: Shader Counters. */
#define VPC_VSINSTCOUNT                         (VPG_VS + 1)
#define VPC_VSBRANCHINSTCOUNT           (VPG_VS + 2)
#define VPC_VSTEXLDINSTCOUNT            (VPG_VS + 3)
#define VPC_VSRENDEREDVERTCOUNT         (VPG_VS + 4)
/* HW: PS Count. */
#define VPC_PSINSTCOUNT             (VPG_PS + 1)
#define VPC_PSBRANCHINSTCOUNT       (VPG_PS + 2)
#define VPC_PSTEXLDINSTCOUNT        (VPG_PS + 3)
#define VPC_PSRENDEREDPIXCOUNT          (VPG_PS + 4)


/* HW: PA Counters. */
#define VPC_PAINVERTCOUNT                       (VPG_PA + 1)
#define VPC_PAINPRIMCOUNT                       (VPG_PA + 2)
#define VPC_PAOUTPRIMCOUNT                      (VPG_PA + 3)
#define VPC_PADEPTHCLIPCOUNT            (VPG_PA + 4)
#define VPC_PATRIVIALREJCOUNT           (VPG_PA + 5)
#define VPC_PACULLCOUNT                         (VPG_PA + 6)

/* HW: Setup Counters. */
#define VPC_SETRIANGLECOUNT                     (VPG_SETUP + 1)
#define VPC_SELINECOUNT                         (VPG_SETUP + 2)

/* HW: RA Counters. */
#define VPC_RAVALIDPIXCOUNT                     (VPG_RA + 1)
#define VPC_RATOTALQUADCOUNT            (VPG_RA + 2)
#define VPC_RAVALIDQUADCOUNTEZ          (VPG_RA + 3)
#define VPC_RATOTALPRIMCOUNT            (VPG_RA + 4)
#define VPC_RAPIPECACHEMISSCOUNT        (VPG_RA + 5)
#define VPC_RAPREFCACHEMISSCOUNT        (VPG_RA + 6)
#define VPC_RAEEZCULLCOUNT                      (VPG_RA + 7)

/* HW: TEX Counters. */
#define VPC_TXTOTBILINEARREQ            (VPG_TX + 1)
#define VPC_TXTOTTRILINEARREQ           (VPG_TX + 2)
#define VPC_TXTOTDISCARDTEXREQ          (VPG_TX + 3)
#define VPC_TXTOTTEXREQ                         (VPG_TX + 4)
#define VPC_TXMEMREADCOUNT                      (VPG_TX + 5)
#define VPC_TXMEMREADIN8BCOUNT          (VPG_TX + 6)
#define VPC_TXCACHEMISSCOUNT            (VPG_TX + 7)
#define VPC_TXCACHEHITTEXELCOUNT        (VPG_TX + 8)
#define VPC_TXCACHEMISSTEXELCOUNT       (VPG_TX + 9)

/* HW: PE Counters. */
#define VPC_PEKILLEDBYCOLOR                     (VPG_PE + 1)
#define VPC_PEKILLEDBYDEPTH                     (VPG_PE + 2)
#define VPC_PEDRAWNBYCOLOR                      (VPG_PE + 3)
#define VPC_PEDRAWNBYDEPTH                      (VPG_PE + 4)

/* HW: MC Counters. */
#define VPC_MCREADREQ8BPIPE                     (VPG_MC + 1)
#define VPC_MCREADREQ8BIP                       (VPG_MC + 2)
#define VPC_MCWRITEREQ8BPIPE            (VPG_MC + 3)

/* HW: AXI Counters. */
#define VPC_AXIREADREQSTALLED           (VPG_AXI + 1)
#define VPC_AXIWRITEREQSTALLED          (VPG_AXI + 2)
#define VPC_AXIWRITEDATASTALLED         (VPG_AXI + 3)

/* PROGRAM: Shader program counters. */
#define	VPC_PVSINSTRCOUNT			(VPG_PVS + 1)
#define VPC_PVSALUINSTRCOUNT		(VPG_PVS + 2)
#define VPC_PVSTEXINSTRCOUNT		(VPG_PVS + 3)
#define VPC_PVSATTRIBCOUNT			(VPG_PVS + 4)
#define VPC_PVSUNIFORMCOUNT			(VPG_PVS + 5)
#define VPC_PVSFUNCTIONCOUNT		(VPG_PVS + 6)

#define	VPC_PPSINSTRCOUNT			(VPG_PPS + 1)
#define VPC_PPSALUINSTRCOUNT		(VPG_PPS + 2)
#define VPC_PPSTEXINSTRCOUNT		(VPG_PPS + 3)
#define VPC_PPSATTRIBCOUNT			(VPG_PPS + 4)
#define VPC_PPSUNIFORMCOUNT			(VPG_PPS + 5)
#define VPC_PPSFUNCTIONCOUNT		(VPG_PPS + 6)

#endif


/* HW profile information. */
typedef struct _gcsPROFILER_COUNTERS
{
    /* HW static counters. */
    gctUINT32       gpuClock;
    gctUINT32       axiClock;
    gctUINT32       shaderClock;

    /* HW vairable counters. */
    gctUINT32       gpuClockStart;
    gctUINT32       gpuClockEnd;

    /* HW vairable counters. */
    gctUINT32       gpuCyclesCounter;
    gctUINT32       gpuTotalRead64BytesPerFrame;
    gctUINT32       gpuTotalWrite64BytesPerFrame;

    /* PE */
    gctUINT32       pe_pixel_count_killed_by_color_pipe;
    gctUINT32       pe_pixel_count_killed_by_depth_pipe;
    gctUINT32       pe_pixel_count_drawn_by_color_pipe;
    gctUINT32       pe_pixel_count_drawn_by_depth_pipe;

    /* SH */
    gctUINT32       ps_inst_counter;
    gctUINT32       rendered_pixel_counter;
    gctUINT32       vs_inst_counter;
    gctUINT32       rendered_vertice_counter;
    gctUINT32       vtx_branch_inst_counter;
    gctUINT32       vtx_texld_inst_counter;
    gctUINT32       pxl_branch_inst_counter;
    gctUINT32       pxl_texld_inst_counter;

    /* PA */
    gctUINT32       pa_input_vtx_counter;
    gctUINT32       pa_input_prim_counter;
    gctUINT32       pa_output_prim_counter;
    gctUINT32       pa_depth_clipped_counter;
    gctUINT32       pa_trivial_rejected_counter;
    gctUINT32       pa_culled_counter;

    /* SE */
    gctUINT32       se_culled_triangle_count;
    gctUINT32       se_culled_lines_count;

    /* RA */
    gctUINT32       ra_valid_pixel_count;
    gctUINT32       ra_total_quad_count;
    gctUINT32       ra_valid_quad_count_after_early_z;
    gctUINT32       ra_total_primitive_count;
    gctUINT32       ra_pipe_cache_miss_counter;
    gctUINT32       ra_prefetch_cache_miss_counter;
	gctUINT32       ra_eez_culled_counter;

    /* TX */
    gctUINT32       tx_total_bilinear_requests;
    gctUINT32       tx_total_trilinear_requests;
    gctUINT32       tx_total_discarded_texture_requests;
    gctUINT32       tx_total_texture_requests;
    gctUINT32       tx_mem_read_count;
    gctUINT32       tx_mem_read_in_8B_count;
    gctUINT32       tx_cache_miss_count;
    gctUINT32       tx_cache_hit_texel_count;
    gctUINT32       tx_cache_miss_texel_count;

    /* MC */
    gctUINT32       mc_total_read_req_8B_from_pipeline;
    gctUINT32       mc_total_read_req_8B_from_IP;
    gctUINT32       mc_total_write_req_8B_from_pipeline;

    /* HI */
    gctUINT32       hi_axi_cycles_read_request_stalled;
    gctUINT32       hi_axi_cycles_write_request_stalled;
    gctUINT32       hi_axi_cycles_write_data_stalled;
}
gcsPROFILER_COUNTERS;

/* HAL profile information. */
typedef struct _gcsPROFILER
{
    gctUINT32       enable;
	gctBOOL			enableHal;
	gctBOOL			enableHW;
	gctBOOL			enableSH;

    gctBOOL         useSocket;
    gctINT          sockFd;

    gctFILE         file;

    /* Aggregate Information */

    /* Clock Info */
    gctUINT64       frameStart;
    gctUINT64       frameEnd;

    /* Current frame information */
    gctUINT32       frameNumber;
    gctUINT64       frameStartTimeusec;
    gctUINT64       frameEndTimeusec;
    gctUINT64       frameStartCPUTimeusec;
    gctUINT64       frameEndCPUTimeusec;

/* #if PROFILE_HAL_COUNTERS */
    gctUINT32       vertexBufferTotalBytesAlloc;
    gctUINT32       vertexBufferNewBytesAlloc;
    int             vertexBufferTotalObjectsAlloc;
    int             vertexBufferNewObjectsAlloc;

    gctUINT32       indexBufferTotalBytesAlloc;
    gctUINT32       indexBufferNewBytesAlloc;
    int             indexBufferTotalObjectsAlloc;
    int             indexBufferNewObjectsAlloc;

    gctUINT32       textureBufferTotalBytesAlloc;
    gctUINT32       textureBufferNewBytesAlloc;
    int             textureBufferTotalObjectsAlloc;
    int             textureBufferNewObjectsAlloc;

    gctUINT32       numCommits;
    gctUINT32       drawPointCount;
    gctUINT32       drawLineCount;
    gctUINT32       drawTriangleCount;
    gctUINT32       drawVertexCount;
    gctUINT32       redundantStateChangeCalls;
/* #endif */
}
gcsPROFILER;

/* Memory profile information. */
struct _gcsMemProfile
{
    /* Memory Usage */
    gctUINT32       videoMemUsed;
    gctUINT32       systemMemUsed;
    gctUINT32       commitBufferSize;
    gctUINT32       contextBufferCopyBytes;
};

/* Shader profile information. */
struct _gcsSHADER_PROFILER
{
    gctUINT32       shaderLength;
    gctUINT32       shaderALUCycles;
    gctUINT32       shaderTexLoadCycles;
    gctUINT32       shaderTempRegCount;
    gctUINT32       shaderSamplerRegCount;
    gctUINT32       shaderInputRegCount;
    gctUINT32       shaderOutputRegCount;
};

/* Initialize the gcsProfiler. */
gceSTATUS
gcoPROFILER_Initialize(
    IN gcoHAL Hal
    );

/* Destroy the gcProfiler. */
gceSTATUS
gcoPROFILER_Destroy(
    IN gcoHAL Hal
    );

/* Write data to profiler. */
gceSTATUS
gcoPROFILER_Write(
    IN gcoHAL Hal,
    IN gctSIZE_T ByteCount,
    IN gctCONST_POINTER Data
    );

/* Flush data out. */
gceSTATUS
gcoPROFILER_Flush(
    IN gcoHAL Hal
    );

/* Call to signal end of frame. */
gceSTATUS
gcoPROFILER_EndFrame(
    IN gcoHAL Hal
    );

/* Increase profile counter Enum by Value. */
gceSTATUS
gcoPROFILER_Count(
	IN gcoHAL Hal,
	IN gctUINT32 Enum,
	IN gctINT Value
	);

/* Profile input vertex shader. */
gceSTATUS
gcoPROFILER_ShaderVS(
    IN gcoHAL Hal,
    IN gctPOINTER Vs
    );

/* Profile input fragment shader. */
gceSTATUS
gcoPROFILER_ShaderFS(
    IN gcoHAL Hal,
    IN gctPOINTER Fs
    );

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_profiler_h_ */

