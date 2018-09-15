/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**********************************************************
 * Copyright 2012-2015 VMware, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

/*
 * svga3d_types.h --
 *
 *       SVGA 3d hardware definitions for basic types
 */

#ifndef _SVGA3D_TYPES_H_
#define _SVGA3D_TYPES_H_

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE

#include "includeCheck.h"

/*
 * Generic Types
 */

#define SVGA3D_INVALID_ID         ((uint32)-1)

typedef uint8 SVGABool8;   /* 8-bit Bool definition */
typedef uint32 SVGA3dBool; /* 32-bit Bool definition */
typedef uint32 SVGA3dColor; /* a, r, g, b */

typedef uint32 SVGA3dSurfaceId;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 numerator;
   uint32 denominator;
}
#include "vmware_pack_end.h"
SVGA3dFraction64;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCopyRect {
   uint32               x;
   uint32               y;
   uint32               w;
   uint32               h;
   uint32               srcx;
   uint32               srcy;
}
#include "vmware_pack_end.h"
SVGA3dCopyRect;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCopyBox {
   uint32               x;
   uint32               y;
   uint32               z;
   uint32               w;
   uint32               h;
   uint32               d;
   uint32               srcx;
   uint32               srcy;
   uint32               srcz;
}
#include "vmware_pack_end.h"
SVGA3dCopyBox;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dRect {
   uint32               x;
   uint32               y;
   uint32               w;
   uint32               h;
}
#include "vmware_pack_end.h"
SVGA3dRect;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               x;
   uint32               y;
   uint32               z;
   uint32               w;
   uint32               h;
   uint32               d;
}
#include "vmware_pack_end.h"
SVGA3dBox;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               x;
   uint32               y;
   uint32               z;
}
#include "vmware_pack_end.h"
SVGA3dPoint;

/*
 * Surface formats.
 */
typedef enum SVGA3dSurfaceFormat {
   SVGA3D_FORMAT_INVALID               = 0,

   SVGA3D_X8R8G8B8                     = 1,
   SVGA3D_FORMAT_MIN                   = 1,

   SVGA3D_A8R8G8B8                     = 2,

   SVGA3D_R5G6B5                       = 3,
   SVGA3D_X1R5G5B5                     = 4,
   SVGA3D_A1R5G5B5                     = 5,
   SVGA3D_A4R4G4B4                     = 6,

   SVGA3D_Z_D32                        = 7,
   SVGA3D_Z_D16                        = 8,
   SVGA3D_Z_D24S8                      = 9,
   SVGA3D_Z_D15S1                      = 10,

   SVGA3D_LUMINANCE8                   = 11,
   SVGA3D_LUMINANCE4_ALPHA4            = 12,
   SVGA3D_LUMINANCE16                  = 13,
   SVGA3D_LUMINANCE8_ALPHA8            = 14,

   SVGA3D_DXT1                         = 15,
   SVGA3D_DXT2                         = 16,
   SVGA3D_DXT3                         = 17,
   SVGA3D_DXT4                         = 18,
   SVGA3D_DXT5                         = 19,

   SVGA3D_BUMPU8V8                     = 20,
   SVGA3D_BUMPL6V5U5                   = 21,
   SVGA3D_BUMPX8L8V8U8                 = 22,
   SVGA3D_FORMAT_DEAD1                 = 23,

   SVGA3D_ARGB_S10E5                   = 24,   /* 16-bit floating-point ARGB */
   SVGA3D_ARGB_S23E8                   = 25,   /* 32-bit floating-point ARGB */

   SVGA3D_A2R10G10B10                  = 26,

   /* signed formats */
   SVGA3D_V8U8                         = 27,
   SVGA3D_Q8W8V8U8                     = 28,
   SVGA3D_CxV8U8                       = 29,

   /* mixed formats */
   SVGA3D_X8L8V8U8                     = 30,
   SVGA3D_A2W10V10U10                  = 31,

   SVGA3D_ALPHA8                       = 32,

   /* Single- and dual-component floating point formats */
   SVGA3D_R_S10E5                      = 33,
   SVGA3D_R_S23E8                      = 34,
   SVGA3D_RG_S10E5                     = 35,
   SVGA3D_RG_S23E8                     = 36,

   SVGA3D_BUFFER                       = 37,

   SVGA3D_Z_D24X8                      = 38,

   SVGA3D_V16U16                       = 39,

   SVGA3D_G16R16                       = 40,
   SVGA3D_A16B16G16R16                 = 41,

   /* Packed Video formats */
   SVGA3D_UYVY                         = 42,
   SVGA3D_YUY2                         = 43,

   /* Planar video formats */
   SVGA3D_NV12                         = 44,

   /* Video format with alpha */
   SVGA3D_AYUV                         = 45,

   SVGA3D_R32G32B32A32_TYPELESS        = 46,
   SVGA3D_R32G32B32A32_UINT            = 47,
   SVGA3D_R32G32B32A32_SINT            = 48,
   SVGA3D_R32G32B32_TYPELESS           = 49,
   SVGA3D_R32G32B32_FLOAT              = 50,
   SVGA3D_R32G32B32_UINT               = 51,
   SVGA3D_R32G32B32_SINT               = 52,
   SVGA3D_R16G16B16A16_TYPELESS        = 53,
   SVGA3D_R16G16B16A16_UINT            = 54,
   SVGA3D_R16G16B16A16_SNORM           = 55,
   SVGA3D_R16G16B16A16_SINT            = 56,
   SVGA3D_R32G32_TYPELESS              = 57,
   SVGA3D_R32G32_UINT                  = 58,
   SVGA3D_R32G32_SINT                  = 59,
   SVGA3D_R32G8X24_TYPELESS            = 60,
   SVGA3D_D32_FLOAT_S8X24_UINT         = 61,
   SVGA3D_R32_FLOAT_X8X24              = 62,
   SVGA3D_X32_G8X24_UINT               = 63,
   SVGA3D_R10G10B10A2_TYPELESS         = 64,
   SVGA3D_R10G10B10A2_UINT             = 65,
   SVGA3D_R11G11B10_FLOAT              = 66,
   SVGA3D_R8G8B8A8_TYPELESS            = 67,
   SVGA3D_R8G8B8A8_UNORM               = 68,
   SVGA3D_R8G8B8A8_UNORM_SRGB          = 69,
   SVGA3D_R8G8B8A8_UINT                = 70,
   SVGA3D_R8G8B8A8_SINT                = 71,
   SVGA3D_R16G16_TYPELESS              = 72,
   SVGA3D_R16G16_UINT                  = 73,
   SVGA3D_R16G16_SINT                  = 74,
   SVGA3D_R32_TYPELESS                 = 75,
   SVGA3D_D32_FLOAT                    = 76,
   SVGA3D_R32_UINT                     = 77,
   SVGA3D_R32_SINT                     = 78,
   SVGA3D_R24G8_TYPELESS               = 79,
   SVGA3D_D24_UNORM_S8_UINT            = 80,
   SVGA3D_R24_UNORM_X8                 = 81,
   SVGA3D_X24_G8_UINT                  = 82,
   SVGA3D_R8G8_TYPELESS                = 83,
   SVGA3D_R8G8_UNORM                   = 84,
   SVGA3D_R8G8_UINT                    = 85,
   SVGA3D_R8G8_SINT                    = 86,
   SVGA3D_R16_TYPELESS                 = 87,
   SVGA3D_R16_UNORM                    = 88,
   SVGA3D_R16_UINT                     = 89,
   SVGA3D_R16_SNORM                    = 90,
   SVGA3D_R16_SINT                     = 91,
   SVGA3D_R8_TYPELESS                  = 92,
   SVGA3D_R8_UNORM                     = 93,
   SVGA3D_R8_UINT                      = 94,
   SVGA3D_R8_SNORM                     = 95,
   SVGA3D_R8_SINT                      = 96,
   SVGA3D_P8                           = 97,
   SVGA3D_R9G9B9E5_SHAREDEXP           = 98,
   SVGA3D_R8G8_B8G8_UNORM              = 99,
   SVGA3D_G8R8_G8B8_UNORM              = 100,
   SVGA3D_BC1_TYPELESS                 = 101,
   SVGA3D_BC1_UNORM_SRGB               = 102,
   SVGA3D_BC2_TYPELESS                 = 103,
   SVGA3D_BC2_UNORM_SRGB               = 104,
   SVGA3D_BC3_TYPELESS                 = 105,
   SVGA3D_BC3_UNORM_SRGB               = 106,
   SVGA3D_BC4_TYPELESS                 = 107,
   SVGA3D_ATI1                         = 108,   /* DX9-specific BC4_UNORM */
   SVGA3D_BC4_SNORM                    = 109,
   SVGA3D_BC5_TYPELESS                 = 110,
   SVGA3D_ATI2                         = 111,   /* DX9-specific BC5_UNORM */
   SVGA3D_BC5_SNORM                    = 112,
   SVGA3D_R10G10B10_XR_BIAS_A2_UNORM   = 113,
   SVGA3D_B8G8R8A8_TYPELESS            = 114,
   SVGA3D_B8G8R8A8_UNORM_SRGB          = 115,
   SVGA3D_B8G8R8X8_TYPELESS            = 116,
   SVGA3D_B8G8R8X8_UNORM_SRGB          = 117,

   /* Advanced depth formats. */
   SVGA3D_Z_DF16                       = 118,
   SVGA3D_Z_DF24                       = 119,
   SVGA3D_Z_D24S8_INT                  = 120,

   /* Planar video formats. */
   SVGA3D_YV12                         = 121,

   SVGA3D_R32G32B32A32_FLOAT           = 122,
   SVGA3D_R16G16B16A16_FLOAT           = 123,
   SVGA3D_R16G16B16A16_UNORM           = 124,
   SVGA3D_R32G32_FLOAT                 = 125,
   SVGA3D_R10G10B10A2_UNORM            = 126,
   SVGA3D_R8G8B8A8_SNORM               = 127,
   SVGA3D_R16G16_FLOAT                 = 128,
   SVGA3D_R16G16_UNORM                 = 129,
   SVGA3D_R16G16_SNORM                 = 130,
   SVGA3D_R32_FLOAT                    = 131,
   SVGA3D_R8G8_SNORM                   = 132,
   SVGA3D_R16_FLOAT                    = 133,
   SVGA3D_D16_UNORM                    = 134,
   SVGA3D_A8_UNORM                     = 135,
   SVGA3D_BC1_UNORM                    = 136,
   SVGA3D_BC2_UNORM                    = 137,
   SVGA3D_BC3_UNORM                    = 138,
   SVGA3D_B5G6R5_UNORM                 = 139,
   SVGA3D_B5G5R5A1_UNORM               = 140,
   SVGA3D_B8G8R8A8_UNORM               = 141,
   SVGA3D_B8G8R8X8_UNORM               = 142,
   SVGA3D_BC4_UNORM                    = 143,
   SVGA3D_BC5_UNORM                    = 144,

   SVGA3D_FORMAT_MAX
} SVGA3dSurfaceFormat;

/*
 * SVGA3d Surface Flags --
 */
#define SVGA3D_SURFACE_CUBEMAP                (1 << 0)

/*
 * HINT flags are not enforced by the device but are useful for
 * performance.
 */
#define SVGA3D_SURFACE_HINT_STATIC            (CONST64U(1) << 1)
#define SVGA3D_SURFACE_HINT_DYNAMIC           (CONST64U(1) << 2)
#define SVGA3D_SURFACE_HINT_INDEXBUFFER       (CONST64U(1) << 3)
#define SVGA3D_SURFACE_HINT_VERTEXBUFFER      (CONST64U(1) << 4)
#define SVGA3D_SURFACE_HINT_TEXTURE           (CONST64U(1) << 5)
#define SVGA3D_SURFACE_HINT_RENDERTARGET      (CONST64U(1) << 6)
#define SVGA3D_SURFACE_HINT_DEPTHSTENCIL      (CONST64U(1) << 7)
#define SVGA3D_SURFACE_HINT_WRITEONLY         (CONST64U(1) << 8)
#define SVGA3D_SURFACE_MASKABLE_ANTIALIAS     (CONST64U(1) << 9)
#define SVGA3D_SURFACE_AUTOGENMIPMAPS         (CONST64U(1) << 10)

#define SVGA3D_SURFACE_DECODE_RENDERTARGET    (CONST64U(1) << 11)

/*
 * Is this surface using a base-level pitch for it's mob backing?
 *
 * This flag is not intended to be set by guest-drivers, but is instead
 * set by the device when the surface is bound to a mob with a specified
 * pitch.
 */
#define SVGA3D_SURFACE_MOB_PITCH              (CONST64U(1) << 12)

#define SVGA3D_SURFACE_INACTIVE               (CONST64U(1) << 13)
#define SVGA3D_SURFACE_HINT_RT_LOCKABLE       (CONST64U(1) << 14)
#define SVGA3D_SURFACE_VOLUME                 (CONST64U(1) << 15)

/*
 * Required to be set on a surface to bind it to a screen target.
 */
#define SVGA3D_SURFACE_SCREENTARGET           (CONST64U(1) << 16)

/*
 * Align images in the guest-backing mob to 16-bytes.
 */
#define SVGA3D_SURFACE_ALIGN16                (CONST64U(1) << 17)

#define SVGA3D_SURFACE_1D                     (CONST64U(1) << 18)
#define SVGA3D_SURFACE_ARRAY                  (CONST64U(1) << 19)

/*
 * Bind flags.
 * These are enforced for any surface defined with DefineGBSurface_v2.
 */
#define SVGA3D_SURFACE_BIND_VERTEX_BUFFER     (CONST64U(1) << 20)
#define SVGA3D_SURFACE_BIND_INDEX_BUFFER      (CONST64U(1) << 21)
#define SVGA3D_SURFACE_BIND_CONSTANT_BUFFER   (CONST64U(1) << 22)
#define SVGA3D_SURFACE_BIND_SHADER_RESOURCE   (CONST64U(1) << 23)
#define SVGA3D_SURFACE_BIND_RENDER_TARGET     (CONST64U(1) << 24)
#define SVGA3D_SURFACE_BIND_DEPTH_STENCIL     (CONST64U(1) << 25)
#define SVGA3D_SURFACE_BIND_STREAM_OUTPUT     (CONST64U(1) << 26)

/*
 * The STAGING flags notes that the surface will not be used directly by the
 * drawing pipeline, i.e. that it will not be bound to any bind point.
 * Staging surfaces may be used by copy operations to move data in and out
 * of other surfaces.  No bind flags may be set on surfaces with this flag.
 *
 * The HINT_INDIRECT_UPDATE flag suggests that the surface will receive
 * updates indirectly, i.e. the surface will not be updated directly, but
 * will receive copies from staging surfaces.
 */
#define SVGA3D_SURFACE_STAGING_UPLOAD         (CONST64U(1) << 27)
#define SVGA3D_SURFACE_STAGING_DOWNLOAD       (CONST64U(1) << 28)
#define SVGA3D_SURFACE_HINT_INDIRECT_UPDATE   (CONST64U(1) << 29)

/*
 * Setting this flag allow this surface to be used with the
 * SVGA_3D_CMD_DX_TRANSFER_FROM_BUFFER command.  It is only valid for
 * buffer surfaces, and no bind flags are allowed to be set on surfaces
 * with this flag.
 */
#define SVGA3D_SURFACE_TRANSFER_FROM_BUFFER   (CONST64U(1) << 30)

/*
 * Reserved for video operations.
 */
#define SVGA3D_SURFACE_RESERVED1              (CONST64U(1) << 31)

/*
 * Specifies that a surface is multisample, and therefore requires the full
 * mob-backing to store all the samples.
 */
#define SVGA3D_SURFACE_MULTISAMPLE            (CONST64U(1) << 32)

#define SVGA3D_SURFACE_FLAG_MAX               (CONST64U(1) << 33)

/*
 * Surface flags types:
 *
 * SVGA3dSurface1Flags:  Lower 32-bits of flags.
 * SVGA3dSurface2Flags:  Upper 32-bits of flags.
 * SVGA3dSurfaceAllFlags: Full 64-bits of flags.
 */
typedef uint32 SVGA3dSurface1Flags;
typedef uint32 SVGA3dSurface2Flags;
typedef uint64 SVGA3dSurfaceAllFlags;

#define SVGA3D_SURFACE_FLAGS1_MASK ((uint64_t)MAX_UINT32)
#define SVGA3D_SURFACE_FLAGS2_MASK (MAX_UINT64 & ~SVGA3D_SURFACE_FLAGS1_MASK)

#define SVGA3D_SURFACE_HB_DISALLOWED_MASK        \
        (  SVGA3D_SURFACE_MOB_PITCH    |         \
           SVGA3D_SURFACE_SCREENTARGET |         \
           SVGA3D_SURFACE_ALIGN16 |              \
           SVGA3D_SURFACE_BIND_CONSTANT_BUFFER | \
           SVGA3D_SURFACE_BIND_STREAM_OUTPUT |   \
           SVGA3D_SURFACE_STAGING_UPLOAD |       \
           SVGA3D_SURFACE_STAGING_DOWNLOAD |     \
           SVGA3D_SURFACE_HINT_INDIRECT_UPDATE | \
           SVGA3D_SURFACE_TRANSFER_FROM_BUFFER | \
           SVGA3D_SURFACE_MULTISAMPLE            \
        )

#define SVGA3D_SURFACE_HB_PRESENT_DISALLOWED_MASK   \
       (   SVGA3D_SURFACE_1D |                      \
           SVGA3D_SURFACE_MULTISAMPLE               \
        )

#define SVGA3D_SURFACE_2D_DISALLOWED_MASK           \
        (  SVGA3D_SURFACE_CUBEMAP |                 \
           SVGA3D_SURFACE_MASKABLE_ANTIALIAS |      \
           SVGA3D_SURFACE_AUTOGENMIPMAPS |          \
           SVGA3D_SURFACE_VOLUME |                  \
           SVGA3D_SURFACE_1D |                      \
           SVGA3D_SURFACE_BIND_VERTEX_BUFFER |      \
           SVGA3D_SURFACE_BIND_INDEX_BUFFER |       \
           SVGA3D_SURFACE_BIND_CONSTANT_BUFFER |    \
           SVGA3D_SURFACE_BIND_DEPTH_STENCIL |      \
           SVGA3D_SURFACE_BIND_STREAM_OUTPUT |      \
           SVGA3D_SURFACE_TRANSFER_FROM_BUFFER |    \
           SVGA3D_SURFACE_MULTISAMPLE               \
        )

#define SVGA3D_SURFACE_BASICOPS_DISALLOWED_MASK     \
        (  SVGA3D_SURFACE_CUBEMAP |                 \
           SVGA3D_SURFACE_AUTOGENMIPMAPS |          \
           SVGA3D_SURFACE_VOLUME |                  \
           SVGA3D_SURFACE_1D |                      \
           SVGA3D_SURFACE_MULTISAMPLE               \
        )

#define SVGA3D_SURFACE_SCREENTARGET_DISALLOWED_MASK \
        (  SVGA3D_SURFACE_CUBEMAP |                 \
           SVGA3D_SURFACE_AUTOGENMIPMAPS |          \
           SVGA3D_SURFACE_VOLUME |                  \
           SVGA3D_SURFACE_1D |                      \
           SVGA3D_SURFACE_BIND_VERTEX_BUFFER |      \
           SVGA3D_SURFACE_BIND_INDEX_BUFFER |       \
           SVGA3D_SURFACE_BIND_CONSTANT_BUFFER |    \
           SVGA3D_SURFACE_BIND_DEPTH_STENCIL |      \
           SVGA3D_SURFACE_BIND_STREAM_OUTPUT |      \
           SVGA3D_SURFACE_INACTIVE |                \
           SVGA3D_SURFACE_STAGING_UPLOAD |          \
           SVGA3D_SURFACE_STAGING_DOWNLOAD |        \
           SVGA3D_SURFACE_HINT_INDIRECT_UPDATE |    \
           SVGA3D_SURFACE_TRANSFER_FROM_BUFFER |    \
           SVGA3D_SURFACE_MULTISAMPLE               \
        )

#define SVGA3D_SURFACE_BUFFER_DISALLOWED_MASK       \
        (  SVGA3D_SURFACE_CUBEMAP |                 \
           SVGA3D_SURFACE_AUTOGENMIPMAPS |          \
           SVGA3D_SURFACE_VOLUME |                  \
           SVGA3D_SURFACE_1D |                      \
           SVGA3D_SURFACE_MASKABLE_ANTIALIAS |      \
           SVGA3D_SURFACE_ARRAY |                   \
           SVGA3D_SURFACE_MULTISAMPLE |             \
           SVGA3D_SURFACE_MOB_PITCH                 \
        )

#define SVGA3D_SURFACE_MULTISAMPLE_DISALLOWED_MASK  \
        (  SVGA3D_SURFACE_CUBEMAP |                 \
           SVGA3D_SURFACE_AUTOGENMIPMAPS |          \
           SVGA3D_SURFACE_VOLUME |                  \
           SVGA3D_SURFACE_1D |                      \
           SVGA3D_SURFACE_SCREENTARGET |            \
           SVGA3D_SURFACE_MOB_PITCH                 \
        )

#define SVGA3D_SURFACE_DX_ONLY_MASK             \
        (  SVGA3D_SURFACE_BIND_STREAM_OUTPUT |  \
           SVGA3D_SURFACE_STAGING_UPLOAD |      \
           SVGA3D_SURFACE_STAGING_DOWNLOAD |    \
           SVGA3D_SURFACE_TRANSFER_FROM_BUFFER  \
        )

#define SVGA3D_SURFACE_STAGING_MASK             \
        (  SVGA3D_SURFACE_STAGING_UPLOAD |      \
           SVGA3D_SURFACE_STAGING_DOWNLOAD      \
        )

#define SVGA3D_SURFACE_BIND_MASK                  \
        (  SVGA3D_SURFACE_BIND_VERTEX_BUFFER   |  \
           SVGA3D_SURFACE_BIND_INDEX_BUFFER    |  \
           SVGA3D_SURFACE_BIND_CONSTANT_BUFFER |  \
           SVGA3D_SURFACE_BIND_SHADER_RESOURCE |  \
           SVGA3D_SURFACE_BIND_RENDER_TARGET   |  \
           SVGA3D_SURFACE_BIND_DEPTH_STENCIL   |  \
           SVGA3D_SURFACE_BIND_STREAM_OUTPUT      \
        )

typedef enum {
   SVGA3DFORMAT_OP_TEXTURE                               = 0x00000001,
   SVGA3DFORMAT_OP_VOLUMETEXTURE                         = 0x00000002,
   SVGA3DFORMAT_OP_CUBETEXTURE                           = 0x00000004,
   SVGA3DFORMAT_OP_OFFSCREEN_RENDERTARGET                = 0x00000008,
   SVGA3DFORMAT_OP_SAME_FORMAT_RENDERTARGET              = 0x00000010,
   SVGA3DFORMAT_OP_ZSTENCIL                              = 0x00000040,
   SVGA3DFORMAT_OP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH   = 0x00000080,

/*
 * This format can be used as a render target if the current display mode
 * is the same depth if the alpha channel is ignored. e.g. if the device
 * can render to A8R8G8B8 when the display mode is X8R8G8B8, then the
 * format op list entry for A8R8G8B8 should have this cap.
 */
   SVGA3DFORMAT_OP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET  = 0x00000100,

/*
 * This format contains DirectDraw support (including Flip).  This flag
 * should not to be set on alpha formats.
 */
   SVGA3DFORMAT_OP_DISPLAYMODE                           = 0x00000400,

/*
 * The rasterizer can support some level of Direct3D support in this format
 * and implies that the driver can create a Context in this mode (for some
 * render target format).  When this flag is set, the SVGA3DFORMAT_OP_DISPLAYMODE
 * flag must also be set.
 */
   SVGA3DFORMAT_OP_3DACCELERATION                        = 0x00000800,

/*
 * This is set for a private format when the driver has put the bpp in
 * the structure.
 */
   SVGA3DFORMAT_OP_PIXELSIZE                             = 0x00001000,

/*
 * Indicates that this format can be converted to any RGB format for which
 * SVGA3DFORMAT_OP_MEMBEROFGROUP_ARGB is specified.
 */
   SVGA3DFORMAT_OP_CONVERT_TO_ARGB                       = 0x00002000,

/*
 * Indicates that this format can be used to create offscreen plain surfaces.
 */
   SVGA3DFORMAT_OP_OFFSCREENPLAIN                        = 0x00004000,

/*
 * Indicated that this format can be read as an SRGB texture (meaning that the
 * sampler will linearize the looked up data).
 */
   SVGA3DFORMAT_OP_SRGBREAD                              = 0x00008000,

/*
 * Indicates that this format can be used in the bumpmap instructions.
 */
   SVGA3DFORMAT_OP_BUMPMAP                               = 0x00010000,

/*
 * Indicates that this format can be sampled by the displacement map sampler.
 */
   SVGA3DFORMAT_OP_DMAP                                  = 0x00020000,

/*
 * Indicates that this format cannot be used with texture filtering.
 */
   SVGA3DFORMAT_OP_NOFILTER                              = 0x00040000,

/*
 * Indicates that format conversions are supported to this RGB format if
 * SVGA3DFORMAT_OP_CONVERT_TO_ARGB is specified in the source format.
 */
   SVGA3DFORMAT_OP_MEMBEROFGROUP_ARGB                    = 0x00080000,

/*
 * Indicated that this format can be written as an SRGB target
 * (meaning that the pixel pipe will DE-linearize data on output to format)
 */
   SVGA3DFORMAT_OP_SRGBWRITE                             = 0x00100000,

/*
 * Indicates that this format cannot be used with alpha blending.
 */
   SVGA3DFORMAT_OP_NOALPHABLEND                          = 0x00200000,

/*
 * Indicates that the device can auto-generated sublevels for resources
 * of this format.
 */
   SVGA3DFORMAT_OP_AUTOGENMIPMAP                         = 0x00400000,

/*
 * Indicates that this format can be used by vertex texture sampler.
 */
   SVGA3DFORMAT_OP_VERTEXTEXTURE                         = 0x00800000,

/*
 * Indicates that this format supports neither texture coordinate
 * wrap modes, nor mipmapping.
 */
   SVGA3DFORMAT_OP_NOTEXCOORDWRAPNORMIP                  = 0x01000000
} SVGA3dFormatOp;

#define SVGA3D_FORMAT_POSITIVE                             \
   (SVGA3DFORMAT_OP_TEXTURE                              | \
    SVGA3DFORMAT_OP_VOLUMETEXTURE                        | \
    SVGA3DFORMAT_OP_CUBETEXTURE                          | \
    SVGA3DFORMAT_OP_OFFSCREEN_RENDERTARGET               | \
    SVGA3DFORMAT_OP_SAME_FORMAT_RENDERTARGET             | \
    SVGA3DFORMAT_OP_ZSTENCIL                             | \
    SVGA3DFORMAT_OP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH  | \
    SVGA3DFORMAT_OP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET | \
    SVGA3DFORMAT_OP_DISPLAYMODE                          | \
    SVGA3DFORMAT_OP_3DACCELERATION                       | \
    SVGA3DFORMAT_OP_PIXELSIZE                            | \
    SVGA3DFORMAT_OP_CONVERT_TO_ARGB                      | \
    SVGA3DFORMAT_OP_OFFSCREENPLAIN                       | \
    SVGA3DFORMAT_OP_SRGBREAD                             | \
    SVGA3DFORMAT_OP_BUMPMAP                              | \
    SVGA3DFORMAT_OP_DMAP                                 | \
    SVGA3DFORMAT_OP_MEMBEROFGROUP_ARGB                   | \
    SVGA3DFORMAT_OP_SRGBWRITE                            | \
    SVGA3DFORMAT_OP_AUTOGENMIPMAP                        | \
    SVGA3DFORMAT_OP_VERTEXTEXTURE)

#define SVGA3D_FORMAT_NEGATIVE               \
   (SVGA3DFORMAT_OP_NOFILTER               | \
    SVGA3DFORMAT_OP_NOALPHABLEND           | \
    SVGA3DFORMAT_OP_NOTEXCOORDWRAPNORMIP)

/*
 * This structure is a conversion of SVGA3DFORMAT_OP_*
 * Entries must be located at the same position.
 */
typedef union {
   uint32 value;
   struct {
      uint32 texture : 1;
      uint32 volumeTexture : 1;
      uint32 cubeTexture : 1;
      uint32 offscreenRenderTarget : 1;
      uint32 sameFormatRenderTarget : 1;
      uint32 unknown1 : 1;
      uint32 zStencil : 1;
      uint32 zStencilArbitraryDepth : 1;
      uint32 sameFormatUpToAlpha : 1;
      uint32 unknown2 : 1;
      uint32 displayMode : 1;
      uint32 acceleration3d : 1;
      uint32 pixelSize : 1;
      uint32 convertToARGB : 1;
      uint32 offscreenPlain : 1;
      uint32 sRGBRead : 1;
      uint32 bumpMap : 1;
      uint32 dmap : 1;
      uint32 noFilter : 1;
      uint32 memberOfGroupARGB : 1;
      uint32 sRGBWrite : 1;
      uint32 noAlphaBlend : 1;
      uint32 autoGenMipMap : 1;
      uint32 vertexTexture : 1;
      uint32 noTexCoordWrapNorMip : 1;
   };
} SVGA3dSurfaceFormatCaps;

/*
 * SVGA_3D_CMD_SETRENDERSTATE Types.  All value types
 * must fit in a uint32.
 */

typedef enum {
   SVGA3D_RS_INVALID                   = 0,
   SVGA3D_RS_MIN                       = 1,
   SVGA3D_RS_ZENABLE                   = 1,     /* SVGA3dBool */
   SVGA3D_RS_ZWRITEENABLE              = 2,     /* SVGA3dBool */
   SVGA3D_RS_ALPHATESTENABLE           = 3,     /* SVGA3dBool */
   SVGA3D_RS_DITHERENABLE              = 4,     /* SVGA3dBool */
   SVGA3D_RS_BLENDENABLE               = 5,     /* SVGA3dBool */
   SVGA3D_RS_FOGENABLE                 = 6,     /* SVGA3dBool */
   SVGA3D_RS_SPECULARENABLE            = 7,     /* SVGA3dBool */
   SVGA3D_RS_STENCILENABLE             = 8,     /* SVGA3dBool */
   SVGA3D_RS_LIGHTINGENABLE            = 9,     /* SVGA3dBool */
   SVGA3D_RS_NORMALIZENORMALS          = 10,    /* SVGA3dBool */
   SVGA3D_RS_POINTSPRITEENABLE         = 11,    /* SVGA3dBool */
   SVGA3D_RS_POINTSCALEENABLE          = 12,    /* SVGA3dBool */
   SVGA3D_RS_STENCILREF                = 13,    /* uint32 */
   SVGA3D_RS_STENCILMASK               = 14,    /* uint32 */
   SVGA3D_RS_STENCILWRITEMASK          = 15,    /* uint32 */
   SVGA3D_RS_FOGSTART                  = 16,    /* float */
   SVGA3D_RS_FOGEND                    = 17,    /* float */
   SVGA3D_RS_FOGDENSITY                = 18,    /* float */
   SVGA3D_RS_POINTSIZE                 = 19,    /* float */
   SVGA3D_RS_POINTSIZEMIN              = 20,    /* float */
   SVGA3D_RS_POINTSIZEMAX              = 21,    /* float */
   SVGA3D_RS_POINTSCALE_A              = 22,    /* float */
   SVGA3D_RS_POINTSCALE_B              = 23,    /* float */
   SVGA3D_RS_POINTSCALE_C              = 24,    /* float */
   SVGA3D_RS_FOGCOLOR                  = 25,    /* SVGA3dColor */
   SVGA3D_RS_AMBIENT                   = 26,    /* SVGA3dColor */
   SVGA3D_RS_CLIPPLANEENABLE           = 27,    /* SVGA3dClipPlanes */
   SVGA3D_RS_FOGMODE                   = 28,    /* SVGA3dFogMode */
   SVGA3D_RS_FILLMODE                  = 29,    /* SVGA3dFillMode */
   SVGA3D_RS_SHADEMODE                 = 30,    /* SVGA3dShadeMode */
   SVGA3D_RS_LINEPATTERN               = 31,    /* SVGA3dLinePattern */
   SVGA3D_RS_SRCBLEND                  = 32,    /* SVGA3dBlendOp */
   SVGA3D_RS_DSTBLEND                  = 33,    /* SVGA3dBlendOp */
   SVGA3D_RS_BLENDEQUATION             = 34,    /* SVGA3dBlendEquation */
   SVGA3D_RS_CULLMODE                  = 35,    /* SVGA3dFace */
   SVGA3D_RS_ZFUNC                     = 36,    /* SVGA3dCmpFunc */
   SVGA3D_RS_ALPHAFUNC                 = 37,    /* SVGA3dCmpFunc */
   SVGA3D_RS_STENCILFUNC               = 38,    /* SVGA3dCmpFunc */
   SVGA3D_RS_STENCILFAIL               = 39,    /* SVGA3dStencilOp */
   SVGA3D_RS_STENCILZFAIL              = 40,    /* SVGA3dStencilOp */
   SVGA3D_RS_STENCILPASS               = 41,    /* SVGA3dStencilOp */
   SVGA3D_RS_ALPHAREF                  = 42,    /* float (0.0 .. 1.0) */
   SVGA3D_RS_FRONTWINDING              = 43,    /* SVGA3dFrontWinding */
   SVGA3D_RS_COORDINATETYPE            = 44,    /* SVGA3dCoordinateType */
   SVGA3D_RS_ZBIAS                     = 45,    /* float */
   SVGA3D_RS_RANGEFOGENABLE            = 46,    /* SVGA3dBool */
   SVGA3D_RS_COLORWRITEENABLE          = 47,    /* SVGA3dColorMask */
   SVGA3D_RS_VERTEXMATERIALENABLE      = 48,    /* SVGA3dBool */
   SVGA3D_RS_DIFFUSEMATERIALSOURCE     = 49,    /* SVGA3dVertexMaterial */
   SVGA3D_RS_SPECULARMATERIALSOURCE    = 50,    /* SVGA3dVertexMaterial */
   SVGA3D_RS_AMBIENTMATERIALSOURCE     = 51,    /* SVGA3dVertexMaterial */
   SVGA3D_RS_EMISSIVEMATERIALSOURCE    = 52,    /* SVGA3dVertexMaterial */
   SVGA3D_RS_TEXTUREFACTOR             = 53,    /* SVGA3dColor */
   SVGA3D_RS_LOCALVIEWER               = 54,    /* SVGA3dBool */
   SVGA3D_RS_SCISSORTESTENABLE         = 55,    /* SVGA3dBool */
   SVGA3D_RS_BLENDCOLOR                = 56,    /* SVGA3dColor */
   SVGA3D_RS_STENCILENABLE2SIDED       = 57,    /* SVGA3dBool */
   SVGA3D_RS_CCWSTENCILFUNC            = 58,    /* SVGA3dCmpFunc */
   SVGA3D_RS_CCWSTENCILFAIL            = 59,    /* SVGA3dStencilOp */
   SVGA3D_RS_CCWSTENCILZFAIL           = 60,    /* SVGA3dStencilOp */
   SVGA3D_RS_CCWSTENCILPASS            = 61,    /* SVGA3dStencilOp */
   SVGA3D_RS_VERTEXBLEND               = 62,    /* SVGA3dVertexBlendFlags */
   SVGA3D_RS_SLOPESCALEDEPTHBIAS       = 63,    /* float */
   SVGA3D_RS_DEPTHBIAS                 = 64,    /* float */


   /*
    * Output Gamma Level
    *
    * Output gamma effects the gamma curve of colors that are output from the
    * rendering pipeline.  A value of 1.0 specifies a linear color space. If the
    * value is <= 0.0, gamma correction is ignored and linear color space is
    * used.
    */

   SVGA3D_RS_OUTPUTGAMMA               = 65,    /* float */
   SVGA3D_RS_ZVISIBLE                  = 66,    /* SVGA3dBool */
   SVGA3D_RS_LASTPIXEL                 = 67,    /* SVGA3dBool */
   SVGA3D_RS_CLIPPING                  = 68,    /* SVGA3dBool */
   SVGA3D_RS_WRAP0                     = 69,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP1                     = 70,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP2                     = 71,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP3                     = 72,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP4                     = 73,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP5                     = 74,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP6                     = 75,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP7                     = 76,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP8                     = 77,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP9                     = 78,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP10                    = 79,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP11                    = 80,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP12                    = 81,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP13                    = 82,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP14                    = 83,    /* SVGA3dWrapFlags */
   SVGA3D_RS_WRAP15                    = 84,    /* SVGA3dWrapFlags */
   SVGA3D_RS_MULTISAMPLEANTIALIAS      = 85,    /* SVGA3dBool */
   SVGA3D_RS_MULTISAMPLEMASK           = 86,    /* uint32 */
   SVGA3D_RS_INDEXEDVERTEXBLENDENABLE  = 87,    /* SVGA3dBool */
   SVGA3D_RS_TWEENFACTOR               = 88,    /* float */
   SVGA3D_RS_ANTIALIASEDLINEENABLE     = 89,    /* SVGA3dBool */
   SVGA3D_RS_COLORWRITEENABLE1         = 90,    /* SVGA3dColorMask */
   SVGA3D_RS_COLORWRITEENABLE2         = 91,    /* SVGA3dColorMask */
   SVGA3D_RS_COLORWRITEENABLE3         = 92,    /* SVGA3dColorMask */
   SVGA3D_RS_SEPARATEALPHABLENDENABLE  = 93,    /* SVGA3dBool */
   SVGA3D_RS_SRCBLENDALPHA             = 94,    /* SVGA3dBlendOp */
   SVGA3D_RS_DSTBLENDALPHA             = 95,    /* SVGA3dBlendOp */
   SVGA3D_RS_BLENDEQUATIONALPHA        = 96,    /* SVGA3dBlendEquation */
   SVGA3D_RS_TRANSPARENCYANTIALIAS     = 97,    /* SVGA3dTransparencyAntialiasType */
   SVGA3D_RS_LINEWIDTH                 = 98,    /* float */
   SVGA3D_RS_MAX
} SVGA3dRenderStateName;

typedef enum {
   SVGA3D_TRANSPARENCYANTIALIAS_NORMAL            = 0,
   SVGA3D_TRANSPARENCYANTIALIAS_ALPHATOCOVERAGE   = 1,
   SVGA3D_TRANSPARENCYANTIALIAS_SUPERSAMPLE       = 2,
   SVGA3D_TRANSPARENCYANTIALIAS_MAX
} SVGA3dTransparencyAntialiasType;

typedef enum {
   SVGA3D_VERTEXMATERIAL_NONE     = 0,    /* Use the value in the current material */
   SVGA3D_VERTEXMATERIAL_DIFFUSE  = 1,    /* Use the value in the diffuse component */
   SVGA3D_VERTEXMATERIAL_SPECULAR = 2,    /* Use the value in the specular component */
   SVGA3D_VERTEXMATERIAL_MAX      = 3,
} SVGA3dVertexMaterial;

typedef enum {
   SVGA3D_FILLMODE_INVALID = 0,
   SVGA3D_FILLMODE_MIN     = 1,
   SVGA3D_FILLMODE_POINT   = 1,
   SVGA3D_FILLMODE_LINE    = 2,
   SVGA3D_FILLMODE_FILL    = 3,
   SVGA3D_FILLMODE_MAX
} SVGA3dFillModeType;


typedef
#include "vmware_pack_begin.h"
union {
   struct {
      uint16   mode;       /* SVGA3dFillModeType */
      uint16   face;       /* SVGA3dFace */
   };
   uint32 uintValue;
}
#include "vmware_pack_end.h"
SVGA3dFillMode;

typedef enum {
   SVGA3D_SHADEMODE_INVALID = 0,
   SVGA3D_SHADEMODE_FLAT    = 1,
   SVGA3D_SHADEMODE_SMOOTH  = 2,
   SVGA3D_SHADEMODE_PHONG   = 3,     /* Not supported */
   SVGA3D_SHADEMODE_MAX
} SVGA3dShadeMode;

typedef
#include "vmware_pack_begin.h"
union {
   struct {
      uint16 repeat;
      uint16 pattern;
   };
   uint32 uintValue;
}
#include "vmware_pack_end.h"
SVGA3dLinePattern;

typedef enum {
   SVGA3D_BLENDOP_INVALID             = 0,
   SVGA3D_BLENDOP_MIN                 = 1,
   SVGA3D_BLENDOP_ZERO                = 1,
   SVGA3D_BLENDOP_ONE                 = 2,
   SVGA3D_BLENDOP_SRCCOLOR            = 3,
   SVGA3D_BLENDOP_INVSRCCOLOR         = 4,
   SVGA3D_BLENDOP_SRCALPHA            = 5,
   SVGA3D_BLENDOP_INVSRCALPHA         = 6,
   SVGA3D_BLENDOP_DESTALPHA           = 7,
   SVGA3D_BLENDOP_INVDESTALPHA        = 8,
   SVGA3D_BLENDOP_DESTCOLOR           = 9,
   SVGA3D_BLENDOP_INVDESTCOLOR        = 10,
   SVGA3D_BLENDOP_SRCALPHASAT         = 11,
   SVGA3D_BLENDOP_BLENDFACTOR         = 12,
   SVGA3D_BLENDOP_INVBLENDFACTOR      = 13,
   SVGA3D_BLENDOP_SRC1COLOR           = 14,
   SVGA3D_BLENDOP_INVSRC1COLOR        = 15,
   SVGA3D_BLENDOP_SRC1ALPHA           = 16,
   SVGA3D_BLENDOP_INVSRC1ALPHA        = 17,
   SVGA3D_BLENDOP_BLENDFACTORALPHA    = 18,
   SVGA3D_BLENDOP_INVBLENDFACTORALPHA = 19,
   SVGA3D_BLENDOP_MAX
} SVGA3dBlendOp;

typedef enum {
   SVGA3D_BLENDEQ_INVALID            = 0,
   SVGA3D_BLENDEQ_MIN                = 1,
   SVGA3D_BLENDEQ_ADD                = 1,
   SVGA3D_BLENDEQ_SUBTRACT           = 2,
   SVGA3D_BLENDEQ_REVSUBTRACT        = 3,
   SVGA3D_BLENDEQ_MINIMUM            = 4,
   SVGA3D_BLENDEQ_MAXIMUM            = 5,
   SVGA3D_BLENDEQ_MAX
} SVGA3dBlendEquation;

typedef enum {
   SVGA3D_DX11_LOGICOP_MIN           = 0,
   SVGA3D_DX11_LOGICOP_CLEAR         = 0,
   SVGA3D_DX11_LOGICOP_SET           = 1,
   SVGA3D_DX11_LOGICOP_COPY          = 2,
   SVGA3D_DX11_LOGICOP_COPY_INVERTED = 3,
   SVGA3D_DX11_LOGICOP_NOOP          = 4,
   SVGA3D_DX11_LOGICOP_INVERT        = 5,
   SVGA3D_DX11_LOGICOP_AND           = 6,
   SVGA3D_DX11_LOGICOP_NAND          = 7,
   SVGA3D_DX11_LOGICOP_OR            = 8,
   SVGA3D_DX11_LOGICOP_NOR           = 9,
   SVGA3D_DX11_LOGICOP_XOR           = 10,
   SVGA3D_DX11_LOGICOP_EQUIV         = 11,
   SVGA3D_DX11_LOGICOP_AND_REVERSE   = 12,
   SVGA3D_DX11_LOGICOP_AND_INVERTED  = 13,
   SVGA3D_DX11_LOGICOP_OR_REVERSE    = 14,
   SVGA3D_DX11_LOGICOP_OR_INVERTED   = 15,
   SVGA3D_DX11_LOGICOP_MAX
} SVGA3dDX11LogicOp;

typedef enum {
   SVGA3D_FRONTWINDING_INVALID = 0,
   SVGA3D_FRONTWINDING_CW      = 1,
   SVGA3D_FRONTWINDING_CCW     = 2,
   SVGA3D_FRONTWINDING_MAX
} SVGA3dFrontWinding;

typedef enum {
   SVGA3D_FACE_INVALID  = 0,
   SVGA3D_FACE_NONE     = 1,
   SVGA3D_FACE_MIN      = 1,
   SVGA3D_FACE_FRONT    = 2,
   SVGA3D_FACE_BACK     = 3,
   SVGA3D_FACE_FRONT_BACK = 4,
   SVGA3D_FACE_MAX
} SVGA3dFace;

/*
 * The order and the values should not be changed
 */

typedef enum {
   SVGA3D_CMP_INVALID              = 0,
   SVGA3D_CMP_NEVER                = 1,
   SVGA3D_CMP_LESS                 = 2,
   SVGA3D_CMP_EQUAL                = 3,
   SVGA3D_CMP_LESSEQUAL            = 4,
   SVGA3D_CMP_GREATER              = 5,
   SVGA3D_CMP_NOTEQUAL             = 6,
   SVGA3D_CMP_GREATEREQUAL         = 7,
   SVGA3D_CMP_ALWAYS               = 8,
   SVGA3D_CMP_MAX
} SVGA3dCmpFunc;

/*
 * SVGA3D_FOGFUNC_* specifies the fog equation, or PER_VERTEX which allows
 * the fog factor to be specified in the alpha component of the specular
 * (a.k.a. secondary) vertex color.
 */
typedef enum {
   SVGA3D_FOGFUNC_INVALID          = 0,
   SVGA3D_FOGFUNC_EXP              = 1,
   SVGA3D_FOGFUNC_EXP2             = 2,
   SVGA3D_FOGFUNC_LINEAR           = 3,
   SVGA3D_FOGFUNC_PER_VERTEX       = 4
} SVGA3dFogFunction;

/*
 * SVGA3D_FOGTYPE_* specifies if fog factors are computed on a per-vertex
 * or per-pixel basis.
 */
typedef enum {
   SVGA3D_FOGTYPE_INVALID          = 0,
   SVGA3D_FOGTYPE_VERTEX           = 1,
   SVGA3D_FOGTYPE_PIXEL            = 2,
   SVGA3D_FOGTYPE_MAX              = 3
} SVGA3dFogType;

/*
 * SVGA3D_FOGBASE_* selects depth or range-based fog. Depth-based fog is
 * computed using the eye Z value of each pixel (or vertex), whereas range-
 * based fog is computed using the actual distance (range) to the eye.
 */
typedef enum {
   SVGA3D_FOGBASE_INVALID          = 0,
   SVGA3D_FOGBASE_DEPTHBASED       = 1,
   SVGA3D_FOGBASE_RANGEBASED       = 2,
   SVGA3D_FOGBASE_MAX              = 3
} SVGA3dFogBase;

typedef enum {
   SVGA3D_STENCILOP_INVALID        = 0,
   SVGA3D_STENCILOP_MIN            = 1,
   SVGA3D_STENCILOP_KEEP           = 1,
   SVGA3D_STENCILOP_ZERO           = 2,
   SVGA3D_STENCILOP_REPLACE        = 3,
   SVGA3D_STENCILOP_INCRSAT        = 4,
   SVGA3D_STENCILOP_DECRSAT        = 5,
   SVGA3D_STENCILOP_INVERT         = 6,
   SVGA3D_STENCILOP_INCR           = 7,
   SVGA3D_STENCILOP_DECR           = 8,
   SVGA3D_STENCILOP_MAX
} SVGA3dStencilOp;

typedef enum {
   SVGA3D_CLIPPLANE_0              = (1 << 0),
   SVGA3D_CLIPPLANE_1              = (1 << 1),
   SVGA3D_CLIPPLANE_2              = (1 << 2),
   SVGA3D_CLIPPLANE_3              = (1 << 3),
   SVGA3D_CLIPPLANE_4              = (1 << 4),
   SVGA3D_CLIPPLANE_5              = (1 << 5),
} SVGA3dClipPlanes;

typedef enum {
   SVGA3D_CLEAR_COLOR              = 0x1,
   SVGA3D_CLEAR_DEPTH              = 0x2,
   SVGA3D_CLEAR_STENCIL            = 0x4,

   /*
    * Hint only, must be used together with SVGA3D_CLEAR_COLOR. If
    * SVGA3D_CLEAR_DEPTH or SVGA3D_CLEAR_STENCIL bit is set, this
    * bit will be ignored.
    */
   SVGA3D_CLEAR_COLORFILL          = 0x8
} SVGA3dClearFlag;

typedef enum {
   SVGA3D_RT_DEPTH                 = 0,
   SVGA3D_RT_MIN                   = 0,
   SVGA3D_RT_STENCIL               = 1,
   SVGA3D_RT_COLOR0                = 2,
   SVGA3D_RT_COLOR1                = 3,
   SVGA3D_RT_COLOR2                = 4,
   SVGA3D_RT_COLOR3                = 5,
   SVGA3D_RT_COLOR4                = 6,
   SVGA3D_RT_COLOR5                = 7,
   SVGA3D_RT_COLOR6                = 8,
   SVGA3D_RT_COLOR7                = 9,
   SVGA3D_RT_MAX,
   SVGA3D_RT_INVALID               = ((uint32)-1),
} SVGA3dRenderTargetType;

#define SVGA3D_MAX_RT_COLOR (SVGA3D_RT_COLOR7 - SVGA3D_RT_COLOR0 + 1)

typedef
#include "vmware_pack_begin.h"
union {
   struct {
      uint32  red   : 1;
      uint32  green : 1;
      uint32  blue  : 1;
      uint32  alpha : 1;
   };
   uint32 uintValue;
}
#include "vmware_pack_end.h"
SVGA3dColorMask;

typedef enum {
   SVGA3D_VBLEND_DISABLE            = 0,
   SVGA3D_VBLEND_1WEIGHT            = 1,
   SVGA3D_VBLEND_2WEIGHT            = 2,
   SVGA3D_VBLEND_3WEIGHT            = 3,
   SVGA3D_VBLEND_MAX                = 4,
} SVGA3dVertexBlendFlags;

typedef enum {
   SVGA3D_WRAPCOORD_0   = 1 << 0,
   SVGA3D_WRAPCOORD_1   = 1 << 1,
   SVGA3D_WRAPCOORD_2   = 1 << 2,
   SVGA3D_WRAPCOORD_3   = 1 << 3,
   SVGA3D_WRAPCOORD_ALL = 0xF,
} SVGA3dWrapFlags;

/*
 * SVGA_3D_CMD_TEXTURESTATE Types.  All value types
 * must fit in a uint32.
 */

typedef enum {
   SVGA3D_TS_INVALID                    = 0,
   SVGA3D_TS_MIN                        = 1,
   SVGA3D_TS_BIND_TEXTURE               = 1,    /* SVGA3dSurfaceId */
   SVGA3D_TS_COLOROP                    = 2,    /* SVGA3dTextureCombiner */
   SVGA3D_TS_COLORARG1                  = 3,    /* SVGA3dTextureArgData */
   SVGA3D_TS_COLORARG2                  = 4,    /* SVGA3dTextureArgData */
   SVGA3D_TS_ALPHAOP                    = 5,    /* SVGA3dTextureCombiner */
   SVGA3D_TS_ALPHAARG1                  = 6,    /* SVGA3dTextureArgData */
   SVGA3D_TS_ALPHAARG2                  = 7,    /* SVGA3dTextureArgData */
   SVGA3D_TS_ADDRESSU                   = 8,    /* SVGA3dTextureAddress */
   SVGA3D_TS_ADDRESSV                   = 9,    /* SVGA3dTextureAddress */
   SVGA3D_TS_MIPFILTER                  = 10,   /* SVGA3dTextureFilter */
   SVGA3D_TS_MAGFILTER                  = 11,   /* SVGA3dTextureFilter */
   SVGA3D_TS_MINFILTER                  = 12,   /* SVGA3dTextureFilter */
   SVGA3D_TS_BORDERCOLOR                = 13,   /* SVGA3dColor */
   SVGA3D_TS_TEXCOORDINDEX              = 14,   /* uint32 */
   SVGA3D_TS_TEXTURETRANSFORMFLAGS      = 15,   /* SVGA3dTexTransformFlags */
   SVGA3D_TS_TEXCOORDGEN                = 16,   /* SVGA3dTextureCoordGen */
   SVGA3D_TS_BUMPENVMAT00               = 17,   /* float */
   SVGA3D_TS_BUMPENVMAT01               = 18,   /* float */
   SVGA3D_TS_BUMPENVMAT10               = 19,   /* float */
   SVGA3D_TS_BUMPENVMAT11               = 20,   /* float */
   SVGA3D_TS_TEXTURE_MIPMAP_LEVEL       = 21,   /* uint32 */
   SVGA3D_TS_TEXTURE_LOD_BIAS           = 22,   /* float */
   SVGA3D_TS_TEXTURE_ANISOTROPIC_LEVEL  = 23,   /* uint32 */
   SVGA3D_TS_ADDRESSW                   = 24,   /* SVGA3dTextureAddress */


   /*
    * Sampler Gamma Level
    *
    * Sampler gamma effects the color of samples taken from the sampler.  A
    * value of 1.0 will produce linear samples.  If the value is <= 0.0 the
    * gamma value is ignored and a linear space is used.
    */

   SVGA3D_TS_GAMMA                      = 25,   /* float */
   SVGA3D_TS_BUMPENVLSCALE              = 26,   /* float */
   SVGA3D_TS_BUMPENVLOFFSET             = 27,   /* float */
   SVGA3D_TS_COLORARG0                  = 28,   /* SVGA3dTextureArgData */
   SVGA3D_TS_ALPHAARG0                  = 29,   /* SVGA3dTextureArgData */
   SVGA3D_TS_PREGB_MAX                  = 30,   /* Max value before GBObjects */
   SVGA3D_TS_CONSTANT                   = 30,   /* SVGA3dColor */
   SVGA3D_TS_COLOR_KEY_ENABLE           = 31,   /* SVGA3dBool */
   SVGA3D_TS_COLOR_KEY                  = 32,   /* SVGA3dColor */
   SVGA3D_TS_MAX
} SVGA3dTextureStateName;

typedef enum {
   SVGA3D_TC_INVALID                   = 0,
   SVGA3D_TC_DISABLE                   = 1,
   SVGA3D_TC_SELECTARG1                = 2,
   SVGA3D_TC_SELECTARG2                = 3,
   SVGA3D_TC_MODULATE                  = 4,
   SVGA3D_TC_ADD                       = 5,
   SVGA3D_TC_ADDSIGNED                 = 6,
   SVGA3D_TC_SUBTRACT                  = 7,
   SVGA3D_TC_BLENDTEXTUREALPHA         = 8,
   SVGA3D_TC_BLENDDIFFUSEALPHA         = 9,
   SVGA3D_TC_BLENDCURRENTALPHA         = 10,
   SVGA3D_TC_BLENDFACTORALPHA          = 11,
   SVGA3D_TC_MODULATE2X                = 12,
   SVGA3D_TC_MODULATE4X                = 13,
   SVGA3D_TC_DSDT                      = 14,
   SVGA3D_TC_DOTPRODUCT3               = 15,
   SVGA3D_TC_BLENDTEXTUREALPHAPM       = 16,
   SVGA3D_TC_ADDSIGNED2X               = 17,
   SVGA3D_TC_ADDSMOOTH                 = 18,
   SVGA3D_TC_PREMODULATE               = 19,
   SVGA3D_TC_MODULATEALPHA_ADDCOLOR    = 20,
   SVGA3D_TC_MODULATECOLOR_ADDALPHA    = 21,
   SVGA3D_TC_MODULATEINVALPHA_ADDCOLOR = 22,
   SVGA3D_TC_MODULATEINVCOLOR_ADDALPHA = 23,
   SVGA3D_TC_BUMPENVMAPLUMINANCE       = 24,
   SVGA3D_TC_MULTIPLYADD               = 25,
   SVGA3D_TC_LERP                      = 26,
   SVGA3D_TC_MAX
} SVGA3dTextureCombiner;

#define SVGA3D_TC_CAP_BIT(svga3d_tc_op) (svga3d_tc_op ? (1 << (svga3d_tc_op - 1)) : 0)

typedef enum {
   SVGA3D_TEX_ADDRESS_INVALID    = 0,
   SVGA3D_TEX_ADDRESS_MIN        = 1,
   SVGA3D_TEX_ADDRESS_WRAP       = 1,
   SVGA3D_TEX_ADDRESS_MIRROR     = 2,
   SVGA3D_TEX_ADDRESS_CLAMP      = 3,
   SVGA3D_TEX_ADDRESS_BORDER     = 4,
   SVGA3D_TEX_ADDRESS_MIRRORONCE = 5,
   SVGA3D_TEX_ADDRESS_EDGE       = 6,
   SVGA3D_TEX_ADDRESS_MAX
} SVGA3dTextureAddress;

/*
 * SVGA3D_TEX_FILTER_NONE as the minification filter means mipmapping is
 * disabled, and the rasterizer should use the magnification filter instead.
 */
typedef enum {
   SVGA3D_TEX_FILTER_NONE           = 0,
   SVGA3D_TEX_FILTER_MIN            = 0,
   SVGA3D_TEX_FILTER_NEAREST        = 1,
   SVGA3D_TEX_FILTER_LINEAR         = 2,
   SVGA3D_TEX_FILTER_ANISOTROPIC    = 3,
   SVGA3D_TEX_FILTER_FLATCUBIC      = 4, /* Deprecated, not implemented */
   SVGA3D_TEX_FILTER_GAUSSIANCUBIC  = 5, /* Deprecated, not implemented */
   SVGA3D_TEX_FILTER_PYRAMIDALQUAD  = 6, /* Not currently implemented */
   SVGA3D_TEX_FILTER_GAUSSIANQUAD   = 7, /* Not currently implemented */
   SVGA3D_TEX_FILTER_MAX
} SVGA3dTextureFilter;

typedef enum {
   SVGA3D_TEX_TRANSFORM_OFF    = 0,
   SVGA3D_TEX_TRANSFORM_S      = (1 << 0),
   SVGA3D_TEX_TRANSFORM_T      = (1 << 1),
   SVGA3D_TEX_TRANSFORM_R      = (1 << 2),
   SVGA3D_TEX_TRANSFORM_Q      = (1 << 3),
   SVGA3D_TEX_PROJECTED        = (1 << 15),
} SVGA3dTexTransformFlags;

typedef enum {
   SVGA3D_TEXCOORD_GEN_OFF              = 0,
   SVGA3D_TEXCOORD_GEN_EYE_POSITION     = 1,
   SVGA3D_TEXCOORD_GEN_EYE_NORMAL       = 2,
   SVGA3D_TEXCOORD_GEN_REFLECTIONVECTOR = 3,
   SVGA3D_TEXCOORD_GEN_SPHERE           = 4,
   SVGA3D_TEXCOORD_GEN_MAX
} SVGA3dTextureCoordGen;

/*
 * Texture argument constants for texture combiner
 */
typedef enum {
   SVGA3D_TA_INVALID    = 0,
   SVGA3D_TA_TFACTOR    = 1,
   SVGA3D_TA_PREVIOUS   = 2,
   SVGA3D_TA_DIFFUSE    = 3,
   SVGA3D_TA_TEXTURE    = 4,
   SVGA3D_TA_SPECULAR   = 5,
   SVGA3D_TA_CONSTANT   = 6,
   SVGA3D_TA_MAX
} SVGA3dTextureArgData;

#define SVGA3D_TM_MASK_LEN 4

/* Modifiers for texture argument constants defined above. */
typedef enum {
   SVGA3D_TM_NONE       = 0,
   SVGA3D_TM_ALPHA      = (1 << SVGA3D_TM_MASK_LEN),
   SVGA3D_TM_ONE_MINUS  = (2 << SVGA3D_TM_MASK_LEN),
} SVGA3dTextureArgModifier;

/*
 * Vertex declarations
 *
 * Notes:
 *
 * SVGA3D_DECLUSAGE_POSITIONT is for pre-transformed vertices. If you
 * draw with any POSITIONT vertex arrays, the programmable vertex
 * pipeline will be implicitly disabled. Drawing will take place as if
 * no vertex shader was bound.
 */

typedef enum {
   SVGA3D_DECLUSAGE_POSITION     = 0,
   SVGA3D_DECLUSAGE_BLENDWEIGHT,
   SVGA3D_DECLUSAGE_BLENDINDICES,
   SVGA3D_DECLUSAGE_NORMAL,
   SVGA3D_DECLUSAGE_PSIZE,
   SVGA3D_DECLUSAGE_TEXCOORD,
   SVGA3D_DECLUSAGE_TANGENT,
   SVGA3D_DECLUSAGE_BINORMAL,
   SVGA3D_DECLUSAGE_TESSFACTOR,
   SVGA3D_DECLUSAGE_POSITIONT,
   SVGA3D_DECLUSAGE_COLOR,
   SVGA3D_DECLUSAGE_FOG,
   SVGA3D_DECLUSAGE_DEPTH,
   SVGA3D_DECLUSAGE_SAMPLE,
   SVGA3D_DECLUSAGE_MAX
} SVGA3dDeclUsage;

typedef enum {
   SVGA3D_DECLMETHOD_DEFAULT     = 0,
   SVGA3D_DECLMETHOD_PARTIALU,
   SVGA3D_DECLMETHOD_PARTIALV,
   SVGA3D_DECLMETHOD_CROSSUV,          /* Normal */
   SVGA3D_DECLMETHOD_UV,
   SVGA3D_DECLMETHOD_LOOKUP,           /* Lookup a displacement map */
   SVGA3D_DECLMETHOD_LOOKUPPRESAMPLED, /* Lookup a pre-sampled displacement */
                                       /* map */
} SVGA3dDeclMethod;

typedef enum {
   SVGA3D_DECLTYPE_FLOAT1        =  0,
   SVGA3D_DECLTYPE_FLOAT2        =  1,
   SVGA3D_DECLTYPE_FLOAT3        =  2,
   SVGA3D_DECLTYPE_FLOAT4        =  3,
   SVGA3D_DECLTYPE_D3DCOLOR      =  4,
   SVGA3D_DECLTYPE_UBYTE4        =  5,
   SVGA3D_DECLTYPE_SHORT2        =  6,
   SVGA3D_DECLTYPE_SHORT4        =  7,
   SVGA3D_DECLTYPE_UBYTE4N       =  8,
   SVGA3D_DECLTYPE_SHORT2N       =  9,
   SVGA3D_DECLTYPE_SHORT4N       = 10,
   SVGA3D_DECLTYPE_USHORT2N      = 11,
   SVGA3D_DECLTYPE_USHORT4N      = 12,
   SVGA3D_DECLTYPE_UDEC3         = 13,
   SVGA3D_DECLTYPE_DEC3N         = 14,
   SVGA3D_DECLTYPE_FLOAT16_2     = 15,
   SVGA3D_DECLTYPE_FLOAT16_4     = 16,
   SVGA3D_DECLTYPE_MAX,
} SVGA3dDeclType;

/*
 * This structure is used for the divisor for geometry instancing;
 * it's a direct translation of the Direct3D equivalent.
 */
typedef union {
   struct {
      /*
       * For index data, this number represents the number of instances to draw.
       * For instance data, this number represents the number of
       * instances/vertex in this stream
       */
      uint32 count : 30;

      /*
       * This is 1 if this is supposed to be the data that is repeated for
       * every instance.
       */
      uint32 indexedData : 1;

      /*
       * This is 1 if this is supposed to be the per-instance data.
       */
      uint32 instanceData : 1;
   };

   uint32 value;
} SVGA3dVertexDivisor;

typedef enum {
   /*
    * SVGA3D_PRIMITIVE_INVALID is a valid primitive type.
    *
    * List MIN second so debuggers will think INVALID is
    * the correct name.
    */
   SVGA3D_PRIMITIVE_INVALID                     = 0,
   SVGA3D_PRIMITIVE_MIN                         = 0,
   SVGA3D_PRIMITIVE_TRIANGLELIST                = 1,
   SVGA3D_PRIMITIVE_POINTLIST                   = 2,
   SVGA3D_PRIMITIVE_LINELIST                    = 3,
   SVGA3D_PRIMITIVE_LINESTRIP                   = 4,
   SVGA3D_PRIMITIVE_TRIANGLESTRIP               = 5,
   SVGA3D_PRIMITIVE_TRIANGLEFAN                 = 6,
   SVGA3D_PRIMITIVE_LINELIST_ADJ                = 7,
   SVGA3D_PRIMITIVE_PREDX_MAX                   = 7,
   SVGA3D_PRIMITIVE_LINESTRIP_ADJ               = 8,
   SVGA3D_PRIMITIVE_TRIANGLELIST_ADJ            = 9,
   SVGA3D_PRIMITIVE_TRIANGLESTRIP_ADJ           = 10,
   SVGA3D_PRIMITIVE_MAX
} SVGA3dPrimitiveType;

typedef enum {
   SVGA3D_COORDINATE_INVALID                   = 0,
   SVGA3D_COORDINATE_LEFTHANDED                = 1,
   SVGA3D_COORDINATE_RIGHTHANDED               = 2,
   SVGA3D_COORDINATE_MAX
} SVGA3dCoordinateType;

typedef enum {
   SVGA3D_TRANSFORM_INVALID                     = 0,
   SVGA3D_TRANSFORM_WORLD                       = 1,
   SVGA3D_TRANSFORM_MIN                         = 1,
   SVGA3D_TRANSFORM_VIEW                        = 2,
   SVGA3D_TRANSFORM_PROJECTION                  = 3,
   SVGA3D_TRANSFORM_TEXTURE0                    = 4,
   SVGA3D_TRANSFORM_TEXTURE1                    = 5,
   SVGA3D_TRANSFORM_TEXTURE2                    = 6,
   SVGA3D_TRANSFORM_TEXTURE3                    = 7,
   SVGA3D_TRANSFORM_TEXTURE4                    = 8,
   SVGA3D_TRANSFORM_TEXTURE5                    = 9,
   SVGA3D_TRANSFORM_TEXTURE6                    = 10,
   SVGA3D_TRANSFORM_TEXTURE7                    = 11,
   SVGA3D_TRANSFORM_WORLD1                      = 12,
   SVGA3D_TRANSFORM_WORLD2                      = 13,
   SVGA3D_TRANSFORM_WORLD3                      = 14,
   SVGA3D_TRANSFORM_MAX
} SVGA3dTransformType;

typedef enum {
   SVGA3D_LIGHTTYPE_INVALID                     = 0,
   SVGA3D_LIGHTTYPE_MIN                         = 1,
   SVGA3D_LIGHTTYPE_POINT                       = 1,
   SVGA3D_LIGHTTYPE_SPOT1                       = 2, /* 1-cone, in degrees */
   SVGA3D_LIGHTTYPE_SPOT2                       = 3, /* 2-cone, in radians */
   SVGA3D_LIGHTTYPE_DIRECTIONAL                 = 4,
   SVGA3D_LIGHTTYPE_MAX
} SVGA3dLightType;

typedef enum {
   SVGA3D_CUBEFACE_POSX                         = 0,
   SVGA3D_CUBEFACE_NEGX                         = 1,
   SVGA3D_CUBEFACE_POSY                         = 2,
   SVGA3D_CUBEFACE_NEGY                         = 3,
   SVGA3D_CUBEFACE_POSZ                         = 4,
   SVGA3D_CUBEFACE_NEGZ                         = 5,
} SVGA3dCubeFace;

typedef enum {
   SVGA3D_SHADERTYPE_INVALID                    = 0,
   SVGA3D_SHADERTYPE_MIN                        = 1,
   SVGA3D_SHADERTYPE_VS                         = 1,
   SVGA3D_SHADERTYPE_PS                         = 2,
   SVGA3D_SHADERTYPE_PREDX_MAX                  = 3,
   SVGA3D_SHADERTYPE_GS                         = 3,
   SVGA3D_SHADERTYPE_DX10_MAX                   = 4,
   SVGA3D_SHADERTYPE_HS                         = 4,
   SVGA3D_SHADERTYPE_DS                         = 5,
   SVGA3D_SHADERTYPE_CS                         = 6,
   SVGA3D_SHADERTYPE_MAX                        = 7
} SVGA3dShaderType;

#define SVGA3D_NUM_SHADERTYPE_PREDX \
   (SVGA3D_SHADERTYPE_PREDX_MAX - SVGA3D_SHADERTYPE_MIN)

#define SVGA3D_NUM_SHADERTYPE_DX10 \
   (SVGA3D_SHADERTYPE_DX10_MAX - SVGA3D_SHADERTYPE_MIN)

#define SVGA3D_NUM_SHADERTYPE \
   (SVGA3D_SHADERTYPE_MAX - SVGA3D_SHADERTYPE_MIN)

typedef enum {
   SVGA3D_CONST_TYPE_MIN                        = 0,
   SVGA3D_CONST_TYPE_FLOAT                      = 0,
   SVGA3D_CONST_TYPE_INT                        = 1,
   SVGA3D_CONST_TYPE_BOOL                       = 2,
   SVGA3D_CONST_TYPE_MAX                        = 3,
} SVGA3dShaderConstType;

/*
 * Register limits for shader consts.
 */
#define SVGA3D_CONSTREG_MAX            256
#define SVGA3D_CONSTINTREG_MAX         16
#define SVGA3D_CONSTBOOLREG_MAX        16

typedef enum {
   SVGA3D_STRETCH_BLT_POINT                     = 0,
   SVGA3D_STRETCH_BLT_LINEAR                    = 1,
   SVGA3D_STRETCH_BLT_MAX
} SVGA3dStretchBltMode;

typedef enum {
   SVGA3D_QUERYTYPE_INVALID                     = ((uint8)-1),
   SVGA3D_QUERYTYPE_MIN                         = 0,
   SVGA3D_QUERYTYPE_OCCLUSION                   = 0,
   SVGA3D_QUERYTYPE_TIMESTAMP                   = 1,
   SVGA3D_QUERYTYPE_TIMESTAMPDISJOINT           = 2,
   SVGA3D_QUERYTYPE_PIPELINESTATS               = 3,
   SVGA3D_QUERYTYPE_OCCLUSIONPREDICATE          = 4,
   SVGA3D_QUERYTYPE_STREAMOUTPUTSTATS           = 5,
   SVGA3D_QUERYTYPE_STREAMOVERFLOWPREDICATE     = 6,
   SVGA3D_QUERYTYPE_OCCLUSION64                 = 7,
   SVGA3D_QUERYTYPE_EVENT                       = 8,
   SVGA3D_QUERYTYPE_DX10_MAX                    = 9,
   SVGA3D_QUERYTYPE_SOSTATS_STREAM0             = 9,
   SVGA3D_QUERYTYPE_SOSTATS_STREAM1             = 10,
   SVGA3D_QUERYTYPE_SOSTATS_STREAM2             = 11,
   SVGA3D_QUERYTYPE_SOSTATS_STREAM3             = 12,
   SVGA3D_QUERYTYPE_SOP_STREAM0                 = 13,
   SVGA3D_QUERYTYPE_SOP_STREAM1                 = 14,
   SVGA3D_QUERYTYPE_SOP_STREAM2                 = 15,
   SVGA3D_QUERYTYPE_SOP_STREAM3                 = 16,
   SVGA3D_QUERYTYPE_MAX
} SVGA3dQueryType;

typedef uint8 SVGA3dQueryTypeUint8;

#define SVGA3D_NUM_QUERYTYPE  (SVGA3D_QUERYTYPE_MAX - SVGA3D_QUERYTYPE_MIN)

/*
 * This is the maximum number of queries per context that can be active
 * simultaneously between a beginQuery and endQuery.
 */
#define SVGA3D_MAX_QUERY 64

/*
 * Query result buffer formats
 */
typedef
#include "vmware_pack_begin.h"
struct {
   uint32 samplesRendered;
}
#include "vmware_pack_end.h"
SVGADXOcclusionQueryResult;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 passed;
}
#include "vmware_pack_end.h"
SVGADXEventQueryResult;

typedef
#include "vmware_pack_begin.h"
struct {
   uint64 timestamp;
}
#include "vmware_pack_end.h"
SVGADXTimestampQueryResult;

typedef
#include "vmware_pack_begin.h"
struct {
   uint64 realFrequency;
   uint32 disjoint;
}
#include "vmware_pack_end.h"
SVGADXTimestampDisjointQueryResult;

typedef
#include "vmware_pack_begin.h"
struct {
   uint64 inputAssemblyVertices;
   uint64 inputAssemblyPrimitives;
   uint64 vertexShaderInvocations;
   uint64 geometryShaderInvocations;
   uint64 geometryShaderPrimitives;
   uint64 clipperInvocations;
   uint64 clipperPrimitives;
   uint64 pixelShaderInvocations;
   uint64 hullShaderInvocations;
   uint64 domainShaderInvocations;
   uint64 computeShaderInvocations;
}
#include "vmware_pack_end.h"
SVGADXPipelineStatisticsQueryResult;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 anySamplesRendered;
}
#include "vmware_pack_end.h"
SVGADXOcclusionPredicateQueryResult;

typedef
#include "vmware_pack_begin.h"
struct {
   uint64 numPrimitivesWritten;
   uint64 numPrimitivesRequired;
}
#include "vmware_pack_end.h"
SVGADXStreamOutStatisticsQueryResult;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 overflowed;
}
#include "vmware_pack_end.h"
SVGADXStreamOutPredicateQueryResult;

typedef
#include "vmware_pack_begin.h"
struct {
   uint64 samplesRendered;
}
#include "vmware_pack_end.h"
SVGADXOcclusion64QueryResult;

/*
 * SVGADXQueryResultUnion is not intended for use in the protocol, but is
 * very helpful when working with queries generically.
 */
typedef
#include "vmware_pack_begin.h"
union SVGADXQueryResultUnion {
   SVGADXOcclusionQueryResult occ;
   SVGADXEventQueryResult event;
   SVGADXTimestampQueryResult ts;
   SVGADXTimestampDisjointQueryResult tsDisjoint;
   SVGADXPipelineStatisticsQueryResult pipelineStats;
   SVGADXOcclusionPredicateQueryResult occPred;
   SVGADXStreamOutStatisticsQueryResult soStats;
   SVGADXStreamOutPredicateQueryResult soPred;
   SVGADXOcclusion64QueryResult occ64;
}
#include "vmware_pack_end.h"
SVGADXQueryResultUnion;

typedef enum {
   SVGA3D_QUERYSTATE_PENDING     = 0,      /* Query is not finished yet */
   SVGA3D_QUERYSTATE_SUCCEEDED   = 1,      /* Completed successfully */
   SVGA3D_QUERYSTATE_FAILED      = 2,      /* Completed unsuccessfully */
   SVGA3D_QUERYSTATE_NEW         = 3,      /* Never submitted (guest only) */
} SVGA3dQueryState;

typedef enum {
   SVGA3D_WRITE_HOST_VRAM        = 1,
   SVGA3D_READ_HOST_VRAM         = 2,
} SVGA3dTransferType;

typedef enum {
   SVGA3D_LOGICOP_INVALID   = 0,
   SVGA3D_LOGICOP_MIN       = 1,
   SVGA3D_LOGICOP_COPY      = 1,
   SVGA3D_LOGICOP_NOT       = 2,
   SVGA3D_LOGICOP_AND       = 3,
   SVGA3D_LOGICOP_OR        = 4,
   SVGA3D_LOGICOP_XOR       = 5,
   SVGA3D_LOGICOP_NXOR      = 6,
   SVGA3D_LOGICOP_ROP3MIN   = 30,   /* 7-29 are reserved for future logic ops. */
   SVGA3D_LOGICOP_ROP3MAX   = (SVGA3D_LOGICOP_ROP3MIN + 255),
   SVGA3D_LOGICOP_MAX       = (SVGA3D_LOGICOP_ROP3MAX + 1),
} SVGA3dLogicOp;

typedef
#include "vmware_pack_begin.h"
struct {
   union {
      struct {
         uint16  function;       /* SVGA3dFogFunction */
         uint8   type;           /* SVGA3dFogType */
         uint8   base;           /* SVGA3dFogBase */
      };
      uint32     uintValue;
   };
}
#include "vmware_pack_end.h"
SVGA3dFogMode;

/*
 * Uniquely identify one image (a 1D/2D/3D array) from a surface. This
 * is a surface ID as well as face/mipmap indices.
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dSurfaceImageId {
   uint32 sid;
   uint32 face;
   uint32 mipmap;
}
#include "vmware_pack_end.h"
SVGA3dSurfaceImageId;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dSubSurfaceId {
   uint32 sid;
   uint32 subResourceId;
}
#include "vmware_pack_end.h"
SVGA3dSubSurfaceId;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               width;
   uint32               height;
   uint32               depth;
}
#include "vmware_pack_end.h"
SVGA3dSize;

/*
 * Guest-backed objects definitions.
 */
typedef enum {
   SVGA_OTABLE_MOB             = 0,
   SVGA_OTABLE_MIN             = 0,
   SVGA_OTABLE_SURFACE         = 1,
   SVGA_OTABLE_CONTEXT         = 2,
   SVGA_OTABLE_SHADER          = 3,
   SVGA_OTABLE_SCREENTARGET    = 4,

   SVGA_OTABLE_DX9_MAX         = 5,

   SVGA_OTABLE_DXCONTEXT       = 5,
   SVGA_OTABLE_DX_MAX          = 6,

   SVGA_OTABLE_RESERVED1       = 6,
   SVGA_OTABLE_RESERVED2       = 7,

   /*
    * Additions to this table need to be tied to HW-version features and
    * checkpointed accordingly.
    */
   SVGA_OTABLE_DEVEL_MAX       = 8,
   SVGA_OTABLE_MAX             = 8
} SVGAOTableType;

typedef enum {
   SVGA_COTABLE_MIN             = 0,
   SVGA_COTABLE_RTVIEW          = 0,
   SVGA_COTABLE_DSVIEW          = 1,
   SVGA_COTABLE_SRVIEW          = 2,
   SVGA_COTABLE_ELEMENTLAYOUT   = 3,
   SVGA_COTABLE_BLENDSTATE      = 4,
   SVGA_COTABLE_DEPTHSTENCIL    = 5,
   SVGA_COTABLE_RASTERIZERSTATE = 6,
   SVGA_COTABLE_SAMPLER         = 7,
   SVGA_COTABLE_STREAMOUTPUT    = 8,
   SVGA_COTABLE_DXQUERY         = 9,
   SVGA_COTABLE_DXSHADER        = 10,
   SVGA_COTABLE_DX10_MAX        = 11,
   SVGA_COTABLE_UAVIEW          = 11,
   SVGA_COTABLE_MAX             = 12,
} SVGACOTableType;

/*
 * The largest size (number of entries) allowed in a COTable.
 */
#define SVGA_COTABLE_MAX_IDS (MAX_UINT16 - 2)

typedef enum SVGAMobFormat {
   SVGA3D_MOBFMT_INVALID     = SVGA3D_INVALID_ID,
   SVGA3D_MOBFMT_PTDEPTH_0   = 0,
   SVGA3D_MOBFMT_MIN         = 0,
   SVGA3D_MOBFMT_PTDEPTH_1   = 1,
   SVGA3D_MOBFMT_PTDEPTH_2   = 2,
   SVGA3D_MOBFMT_RANGE       = 3,
   SVGA3D_MOBFMT_PTDEPTH64_0 = 4,
   SVGA3D_MOBFMT_PTDEPTH64_1 = 5,
   SVGA3D_MOBFMT_PTDEPTH64_2 = 6,
   SVGA3D_MOBFMT_PREDX_MAX   = 7,
   SVGA3D_MOBFMT_EMPTY       = 7,
   SVGA3D_MOBFMT_MAX,

   /*
    * This isn't actually used by the guest, but is a mob-format used
    * internally by the SVGA device (and is therefore not binary compatible).
    */
   SVGA3D_MOBFMT_HB,
} SVGAMobFormat;

#define SVGA3D_MOB_EMPTY_BASE 1

/*
 * Multisample pattern types.
 */

typedef enum SVGA3dMSPattern {
   SVGA3D_MS_PATTERN_NONE     = 0,
   SVGA3D_MS_PATTERN_MIN      = 0,
   SVGA3D_MS_PATTERN_STANDARD = 1,
   SVGA3D_MS_PATTERN_CENTER   = 2,
   SVGA3D_MS_PATTERN_MAX      = 3,
} SVGA3dMSPattern;

/*
 * Precision settings for each sample.
 */

typedef enum SVGA3dMSQualityLevel {
   SVGA3D_MS_QUALITY_NONE = 0,
   SVGA3D_MS_QUALITY_MIN  = 0,
   SVGA3D_MS_QUALITY_FULL = 1,
   SVGA3D_MS_QUALITY_MAX  = 2,
} SVGA3dMSQualityLevel;

#endif /* _SVGA3D_TYPES_H_ */
