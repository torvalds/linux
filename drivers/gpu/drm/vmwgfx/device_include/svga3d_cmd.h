/**********************************************************
 * Copyright 1998-2015 VMware, Inc.  All rights reserved.
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
 * svga3d_cmd.h --
 *
 *       SVGA 3d hardware cmd definitions
 */

#ifndef _SVGA3D_CMD_H_
#define _SVGA3D_CMD_H_

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE

#include "includeCheck.h"
#include "svga3d_types.h"

/*
 * Identifiers for commands in the command FIFO.
 *
 * IDs between 1000 and 1039 (inclusive) were used by obsolete versions of
 * the SVGA3D protocol and remain reserved; they should not be used in the
 * future.
 *
 * IDs between 1040 and 1999 (inclusive) are available for use by the
 * current SVGA3D protocol.
 *
 * FIFO clients other than SVGA3D should stay below 1000, or at 2000
 * and up.
 */

typedef enum {
   SVGA_3D_CMD_LEGACY_BASE                                = 1000,
   SVGA_3D_CMD_BASE                                       = 1040,

   SVGA_3D_CMD_SURFACE_DEFINE                             = 1040,
   SVGA_3D_CMD_SURFACE_DESTROY                            = 1041,
   SVGA_3D_CMD_SURFACE_COPY                               = 1042,
   SVGA_3D_CMD_SURFACE_STRETCHBLT                         = 1043,
   SVGA_3D_CMD_SURFACE_DMA                                = 1044,
   SVGA_3D_CMD_CONTEXT_DEFINE                             = 1045,
   SVGA_3D_CMD_CONTEXT_DESTROY                            = 1046,
   SVGA_3D_CMD_SETTRANSFORM                               = 1047,
   SVGA_3D_CMD_SETZRANGE                                  = 1048,
   SVGA_3D_CMD_SETRENDERSTATE                             = 1049,
   SVGA_3D_CMD_SETRENDERTARGET                            = 1050,
   SVGA_3D_CMD_SETTEXTURESTATE                            = 1051,
   SVGA_3D_CMD_SETMATERIAL                                = 1052,
   SVGA_3D_CMD_SETLIGHTDATA                               = 1053,
   SVGA_3D_CMD_SETLIGHTENABLED                            = 1054,
   SVGA_3D_CMD_SETVIEWPORT                                = 1055,
   SVGA_3D_CMD_SETCLIPPLANE                               = 1056,
   SVGA_3D_CMD_CLEAR                                      = 1057,
   SVGA_3D_CMD_PRESENT                                    = 1058,
   SVGA_3D_CMD_SHADER_DEFINE                              = 1059,
   SVGA_3D_CMD_SHADER_DESTROY                             = 1060,
   SVGA_3D_CMD_SET_SHADER                                 = 1061,
   SVGA_3D_CMD_SET_SHADER_CONST                           = 1062,
   SVGA_3D_CMD_DRAW_PRIMITIVES                            = 1063,
   SVGA_3D_CMD_SETSCISSORRECT                             = 1064,
   SVGA_3D_CMD_BEGIN_QUERY                                = 1065,
   SVGA_3D_CMD_END_QUERY                                  = 1066,
   SVGA_3D_CMD_WAIT_FOR_QUERY                             = 1067,
   SVGA_3D_CMD_PRESENT_READBACK                           = 1068,
   SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN                     = 1069,
   SVGA_3D_CMD_SURFACE_DEFINE_V2                          = 1070,
   SVGA_3D_CMD_GENERATE_MIPMAPS                           = 1071,
   SVGA_3D_CMD_VIDEO_CREATE_DECODER                       = 1072,
   SVGA_3D_CMD_VIDEO_DESTROY_DECODER                      = 1073,
   SVGA_3D_CMD_VIDEO_CREATE_PROCESSOR                     = 1074,
   SVGA_3D_CMD_VIDEO_DESTROY_PROCESSOR                    = 1075,
   SVGA_3D_CMD_VIDEO_DECODE_START_FRAME                   = 1076,
   SVGA_3D_CMD_VIDEO_DECODE_RENDER                        = 1077,
   SVGA_3D_CMD_VIDEO_DECODE_END_FRAME                     = 1078,
   SVGA_3D_CMD_VIDEO_PROCESS_FRAME                        = 1079,
   SVGA_3D_CMD_ACTIVATE_SURFACE                           = 1080,
   SVGA_3D_CMD_DEACTIVATE_SURFACE                         = 1081,
   SVGA_3D_CMD_SCREEN_DMA                                 = 1082,
   SVGA_3D_CMD_SET_UNITY_SURFACE_COOKIE                   = 1083,
   SVGA_3D_CMD_OPEN_CONTEXT_SURFACE                       = 1084,

   SVGA_3D_CMD_LOGICOPS_BITBLT                            = 1085,
   SVGA_3D_CMD_LOGICOPS_TRANSBLT                          = 1086,
   SVGA_3D_CMD_LOGICOPS_STRETCHBLT                        = 1087,
   SVGA_3D_CMD_LOGICOPS_COLORFILL                         = 1088,
   SVGA_3D_CMD_LOGICOPS_ALPHABLEND                        = 1089,
   SVGA_3D_CMD_LOGICOPS_CLEARTYPEBLEND                    = 1090,

   SVGA_3D_CMD_SET_OTABLE_BASE                            = 1091,
   SVGA_3D_CMD_READBACK_OTABLE                            = 1092,

   SVGA_3D_CMD_DEFINE_GB_MOB                              = 1093,
   SVGA_3D_CMD_DESTROY_GB_MOB                             = 1094,
   SVGA_3D_CMD_DEAD3                                      = 1095,
   SVGA_3D_CMD_UPDATE_GB_MOB_MAPPING                      = 1096,

   SVGA_3D_CMD_DEFINE_GB_SURFACE                          = 1097,
   SVGA_3D_CMD_DESTROY_GB_SURFACE                         = 1098,
   SVGA_3D_CMD_BIND_GB_SURFACE                            = 1099,
   SVGA_3D_CMD_COND_BIND_GB_SURFACE                       = 1100,
   SVGA_3D_CMD_UPDATE_GB_IMAGE                            = 1101,
   SVGA_3D_CMD_UPDATE_GB_SURFACE                          = 1102,
   SVGA_3D_CMD_READBACK_GB_IMAGE                          = 1103,
   SVGA_3D_CMD_READBACK_GB_SURFACE                        = 1104,
   SVGA_3D_CMD_INVALIDATE_GB_IMAGE                        = 1105,
   SVGA_3D_CMD_INVALIDATE_GB_SURFACE                      = 1106,

   SVGA_3D_CMD_DEFINE_GB_CONTEXT                          = 1107,
   SVGA_3D_CMD_DESTROY_GB_CONTEXT                         = 1108,
   SVGA_3D_CMD_BIND_GB_CONTEXT                            = 1109,
   SVGA_3D_CMD_READBACK_GB_CONTEXT                        = 1110,
   SVGA_3D_CMD_INVALIDATE_GB_CONTEXT                      = 1111,

   SVGA_3D_CMD_DEFINE_GB_SHADER                           = 1112,
   SVGA_3D_CMD_DESTROY_GB_SHADER                          = 1113,
   SVGA_3D_CMD_BIND_GB_SHADER                             = 1114,

   SVGA_3D_CMD_SET_OTABLE_BASE64                          = 1115,

   SVGA_3D_CMD_BEGIN_GB_QUERY                             = 1116,
   SVGA_3D_CMD_END_GB_QUERY                               = 1117,
   SVGA_3D_CMD_WAIT_FOR_GB_QUERY                          = 1118,

   SVGA_3D_CMD_NOP                                        = 1119,

   SVGA_3D_CMD_ENABLE_GART                                = 1120,
   SVGA_3D_CMD_DISABLE_GART                               = 1121,
   SVGA_3D_CMD_MAP_MOB_INTO_GART                          = 1122,
   SVGA_3D_CMD_UNMAP_GART_RANGE                           = 1123,

   SVGA_3D_CMD_DEFINE_GB_SCREENTARGET                     = 1124,
   SVGA_3D_CMD_DESTROY_GB_SCREENTARGET                    = 1125,
   SVGA_3D_CMD_BIND_GB_SCREENTARGET                       = 1126,
   SVGA_3D_CMD_UPDATE_GB_SCREENTARGET                     = 1127,

   SVGA_3D_CMD_READBACK_GB_IMAGE_PARTIAL                  = 1128,
   SVGA_3D_CMD_INVALIDATE_GB_IMAGE_PARTIAL                = 1129,

   SVGA_3D_CMD_SET_GB_SHADERCONSTS_INLINE                 = 1130,

   SVGA_3D_CMD_GB_SCREEN_DMA                              = 1131,
   SVGA_3D_CMD_BIND_GB_SURFACE_WITH_PITCH                 = 1132,
   SVGA_3D_CMD_GB_MOB_FENCE                               = 1133,
   SVGA_3D_CMD_DEFINE_GB_SURFACE_V2                       = 1134,
   SVGA_3D_CMD_DEFINE_GB_MOB64                            = 1135,
   SVGA_3D_CMD_REDEFINE_GB_MOB64                          = 1136,
   SVGA_3D_CMD_NOP_ERROR                                  = 1137,

   SVGA_3D_CMD_SET_VERTEX_STREAMS                         = 1138,
   SVGA_3D_CMD_SET_VERTEX_DECLS                           = 1139,
   SVGA_3D_CMD_SET_VERTEX_DIVISORS                        = 1140,
   SVGA_3D_CMD_DRAW                                       = 1141,
   SVGA_3D_CMD_DRAW_INDEXED                               = 1142,

   /*
    * DX10 Commands
    */
   SVGA_3D_CMD_DX_MIN                                     = 1143,
   SVGA_3D_CMD_DX_DEFINE_CONTEXT                          = 1143,
   SVGA_3D_CMD_DX_DESTROY_CONTEXT                         = 1144,
   SVGA_3D_CMD_DX_BIND_CONTEXT                            = 1145,
   SVGA_3D_CMD_DX_READBACK_CONTEXT                        = 1146,
   SVGA_3D_CMD_DX_INVALIDATE_CONTEXT                      = 1147,
   SVGA_3D_CMD_DX_SET_SINGLE_CONSTANT_BUFFER              = 1148,
   SVGA_3D_CMD_DX_SET_SHADER_RESOURCES                    = 1149,
   SVGA_3D_CMD_DX_SET_SHADER                              = 1150,
   SVGA_3D_CMD_DX_SET_SAMPLERS                            = 1151,
   SVGA_3D_CMD_DX_DRAW                                    = 1152,
   SVGA_3D_CMD_DX_DRAW_INDEXED                            = 1153,
   SVGA_3D_CMD_DX_DRAW_INSTANCED                          = 1154,
   SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED                  = 1155,
   SVGA_3D_CMD_DX_DRAW_AUTO                               = 1156,
   SVGA_3D_CMD_DX_SET_INPUT_LAYOUT                        = 1157,
   SVGA_3D_CMD_DX_SET_VERTEX_BUFFERS                      = 1158,
   SVGA_3D_CMD_DX_SET_INDEX_BUFFER                        = 1159,
   SVGA_3D_CMD_DX_SET_TOPOLOGY                            = 1160,
   SVGA_3D_CMD_DX_SET_RENDERTARGETS                       = 1161,
   SVGA_3D_CMD_DX_SET_BLEND_STATE                         = 1162,
   SVGA_3D_CMD_DX_SET_DEPTHSTENCIL_STATE                  = 1163,
   SVGA_3D_CMD_DX_SET_RASTERIZER_STATE                    = 1164,
   SVGA_3D_CMD_DX_DEFINE_QUERY                            = 1165,
   SVGA_3D_CMD_DX_DESTROY_QUERY                           = 1166,
   SVGA_3D_CMD_DX_BIND_QUERY                              = 1167,
   SVGA_3D_CMD_DX_SET_QUERY_OFFSET                        = 1168,
   SVGA_3D_CMD_DX_BEGIN_QUERY                             = 1169,
   SVGA_3D_CMD_DX_END_QUERY                               = 1170,
   SVGA_3D_CMD_DX_READBACK_QUERY                          = 1171,
   SVGA_3D_CMD_DX_SET_PREDICATION                         = 1172,
   SVGA_3D_CMD_DX_SET_SOTARGETS                           = 1173,
   SVGA_3D_CMD_DX_SET_VIEWPORTS                           = 1174,
   SVGA_3D_CMD_DX_SET_SCISSORRECTS                        = 1175,
   SVGA_3D_CMD_DX_CLEAR_RENDERTARGET_VIEW                 = 1176,
   SVGA_3D_CMD_DX_CLEAR_DEPTHSTENCIL_VIEW                 = 1177,
   SVGA_3D_CMD_DX_PRED_COPY_REGION                        = 1178,
   SVGA_3D_CMD_DX_PRED_COPY                               = 1179,
   SVGA_3D_CMD_DX_STRETCHBLT                              = 1180,
   SVGA_3D_CMD_DX_GENMIPS                                 = 1181,
   SVGA_3D_CMD_DX_UPDATE_SUBRESOURCE                      = 1182,
   SVGA_3D_CMD_DX_READBACK_SUBRESOURCE                    = 1183,
   SVGA_3D_CMD_DX_INVALIDATE_SUBRESOURCE                  = 1184,
   SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW              = 1185,
   SVGA_3D_CMD_DX_DESTROY_SHADERRESOURCE_VIEW             = 1186,
   SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW                = 1187,
   SVGA_3D_CMD_DX_DESTROY_RENDERTARGET_VIEW               = 1188,
   SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW                = 1189,
   SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_VIEW               = 1190,
   SVGA_3D_CMD_DX_DEFINE_ELEMENTLAYOUT                    = 1191,
   SVGA_3D_CMD_DX_DESTROY_ELEMENTLAYOUT                   = 1192,
   SVGA_3D_CMD_DX_DEFINE_BLEND_STATE                      = 1193,
   SVGA_3D_CMD_DX_DESTROY_BLEND_STATE                     = 1194,
   SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_STATE               = 1195,
   SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_STATE              = 1196,
   SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE                 = 1197,
   SVGA_3D_CMD_DX_DESTROY_RASTERIZER_STATE                = 1198,
   SVGA_3D_CMD_DX_DEFINE_SAMPLER_STATE                    = 1199,
   SVGA_3D_CMD_DX_DESTROY_SAMPLER_STATE                   = 1200,
   SVGA_3D_CMD_DX_DEFINE_SHADER                           = 1201,
   SVGA_3D_CMD_DX_DESTROY_SHADER                          = 1202,
   SVGA_3D_CMD_DX_BIND_SHADER                             = 1203,
   SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT                     = 1204,
   SVGA_3D_CMD_DX_DESTROY_STREAMOUTPUT                    = 1205,
   SVGA_3D_CMD_DX_SET_STREAMOUTPUT                        = 1206,
   SVGA_3D_CMD_DX_SET_COTABLE                             = 1207,
   SVGA_3D_CMD_DX_READBACK_COTABLE                        = 1208,
   SVGA_3D_CMD_DX_BUFFER_COPY                             = 1209,
   SVGA_3D_CMD_DX_TRANSFER_FROM_BUFFER                    = 1210,
   SVGA_3D_CMD_DX_SURFACE_COPY_AND_READBACK               = 1211,
   SVGA_3D_CMD_DX_MOVE_QUERY                              = 1212,
   SVGA_3D_CMD_DX_BIND_ALL_QUERY                          = 1213,
   SVGA_3D_CMD_DX_READBACK_ALL_QUERY                      = 1214,
   SVGA_3D_CMD_DX_PRED_TRANSFER_FROM_BUFFER               = 1215,
   SVGA_3D_CMD_DX_MOB_FENCE_64                            = 1216,
   SVGA_3D_CMD_DX_BIND_SHADER_ON_CONTEXT                  = 1217,
   SVGA_3D_CMD_DX_HINT                                    = 1218,
   SVGA_3D_CMD_DX_BUFFER_UPDATE                           = 1219,
   SVGA_3D_CMD_DX_SET_VS_CONSTANT_BUFFER_OFFSET           = 1220,
   SVGA_3D_CMD_DX_SET_PS_CONSTANT_BUFFER_OFFSET           = 1221,
   SVGA_3D_CMD_DX_SET_GS_CONSTANT_BUFFER_OFFSET           = 1222,

   /*
    * Reserve some IDs to be used for the DX11 shader types.
    */
   SVGA_3D_CMD_DX_RESERVED1                               = 1223,
   SVGA_3D_CMD_DX_RESERVED2                               = 1224,
   SVGA_3D_CMD_DX_RESERVED3                               = 1225,

   SVGA_3D_CMD_DX_MAX                                     = 1226,
   SVGA_3D_CMD_MAX                                        = 1226,
   SVGA_3D_CMD_FUTURE_MAX                                 = 3000
} SVGAFifo3dCmdId;

/*
 * FIFO command format definitions:
 */

/*
 * The data size header following cmdNum for every 3d command
 */
typedef
#include "vmware_pack_begin.h"
struct {
   uint32               id;
   uint32               size;
}
#include "vmware_pack_end.h"
SVGA3dCmdHeader;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               numMipLevels;
}
#include "vmware_pack_end.h"
SVGA3dSurfaceFace;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32                      sid;
   SVGA3dSurfaceFlags          surfaceFlags;
   SVGA3dSurfaceFormat         format;
   /*
    * If surfaceFlags has SVGA3D_SURFACE_CUBEMAP bit set, all SVGA3dSurfaceFace
    * structures must have the same value of numMipLevels field.
    * Otherwise, all but the first SVGA3dSurfaceFace structures must have the
    * numMipLevels set to 0.
    */
   SVGA3dSurfaceFace           face[SVGA3D_MAX_SURFACE_FACES];
   /*
    * Followed by an SVGA3dSize structure for each mip level in each face.
    *
    * A note on surface sizes: Sizes are always specified in pixels,
    * even if the true surface size is not a multiple of the minimum
    * block size of the surface's format. For example, a 3x3x1 DXT1
    * compressed texture would actually be stored as a 4x4x1 image in
    * memory.
    */
}
#include "vmware_pack_end.h"
SVGA3dCmdDefineSurface;       /* SVGA_3D_CMD_SURFACE_DEFINE */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32                      sid;
   SVGA3dSurfaceFlags          surfaceFlags;
   SVGA3dSurfaceFormat         format;
   /*
    * If surfaceFlags has SVGA3D_SURFACE_CUBEMAP bit set, all SVGA3dSurfaceFace
    * structures must have the same value of numMipLevels field.
    * Otherwise, all but the first SVGA3dSurfaceFace structures must have the
    * numMipLevels set to 0.
    */
   SVGA3dSurfaceFace           face[SVGA3D_MAX_SURFACE_FACES];
   uint32                      multisampleCount;
   SVGA3dTextureFilter         autogenFilter;
   /*
    * Followed by an SVGA3dSize structure for each mip level in each face.
    *
    * A note on surface sizes: Sizes are always specified in pixels,
    * even if the true surface size is not a multiple of the minimum
    * block size of the surface's format. For example, a 3x3x1 DXT1
    * compressed texture would actually be stored as a 4x4x1 image in
    * memory.
    */
}
#include "vmware_pack_end.h"
SVGA3dCmdDefineSurface_v2;     /* SVGA_3D_CMD_SURFACE_DEFINE_V2 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               sid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDestroySurface;      /* SVGA_3D_CMD_SURFACE_DESTROY */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDefineContext;       /* SVGA_3D_CMD_CONTEXT_DEFINE */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDestroyContext;      /* SVGA_3D_CMD_CONTEXT_DESTROY */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   SVGA3dClearFlag      clearFlag;
   uint32               color;
   float                depth;
   uint32               stencil;
   /* Followed by variable number of SVGA3dRect structures */
}
#include "vmware_pack_end.h"
SVGA3dCmdClear;               /* SVGA_3D_CMD_CLEAR */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dLightType      type;
   SVGA3dBool           inWorldSpace;
   float                diffuse[4];
   float                specular[4];
   float                ambient[4];
   float                position[4];
   float                direction[4];
   float                range;
   float                falloff;
   float                attenuation0;
   float                attenuation1;
   float                attenuation2;
   float                theta;
   float                phi;
}
#include "vmware_pack_end.h"
SVGA3dLightData;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               sid;
   /* Followed by variable number of SVGA3dCopyRect structures */
}
#include "vmware_pack_end.h"
SVGA3dCmdPresent;             /* SVGA_3D_CMD_PRESENT */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dRenderStateName   state;
   union {
      uint32               uintValue;
      float                floatValue;
   };
}
#include "vmware_pack_end.h"
SVGA3dRenderState;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   /* Followed by variable number of SVGA3dRenderState structures */
}
#include "vmware_pack_end.h"
SVGA3dCmdSetRenderState;      /* SVGA_3D_CMD_SETRENDERSTATE */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32                 cid;
   SVGA3dRenderTargetType type;
   SVGA3dSurfaceImageId   target;
}
#include "vmware_pack_end.h"
SVGA3dCmdSetRenderTarget;     /* SVGA_3D_CMD_SETRENDERTARGET */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dSurfaceImageId  src;
   SVGA3dSurfaceImageId  dest;
   /* Followed by variable number of SVGA3dCopyBox structures */
}
#include "vmware_pack_end.h"
SVGA3dCmdSurfaceCopy;               /* SVGA_3D_CMD_SURFACE_COPY */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dSurfaceImageId  src;
   SVGA3dSurfaceImageId  dest;
   SVGA3dBox             boxSrc;
   SVGA3dBox             boxDest;
   SVGA3dStretchBltMode  mode;
}
#include "vmware_pack_end.h"
SVGA3dCmdSurfaceStretchBlt;         /* SVGA_3D_CMD_SURFACE_STRETCHBLT */

typedef
#include "vmware_pack_begin.h"
struct {
   /*
    * If the discard flag is present in a surface DMA operation, the host may
    * discard the contents of the current mipmap level and face of the target
    * surface before applying the surface DMA contents.
    */
   uint32 discard : 1;

   /*
    * If the unsynchronized flag is present, the host may perform this upload
    * without syncing to pending reads on this surface.
    */
   uint32 unsynchronized : 1;

   /*
    * Guests *MUST* set the reserved bits to 0 before submitting the command
    * suffix as future flags may occupy these bits.
    */
   uint32 reserved : 30;
}
#include "vmware_pack_end.h"
SVGA3dSurfaceDMAFlags;

typedef
#include "vmware_pack_begin.h"
struct {
   SVGAGuestImage guest;
   SVGA3dSurfaceImageId host;
   SVGA3dTransferType transfer;
   /*
    * Followed by variable number of SVGA3dCopyBox structures. For consistency
    * in all clipping logic and coordinate translation, we define the
    * "source" in each copyBox as the guest image and the
    * "destination" as the host image, regardless of transfer
    * direction.
    *
    * For efficiency, the SVGA3D device is free to copy more data than
    * specified. For example, it may round copy boxes outwards such
    * that they lie on particular alignment boundaries.
    */
}
#include "vmware_pack_end.h"
SVGA3dCmdSurfaceDMA;                /* SVGA_3D_CMD_SURFACE_DMA */

/*
 * SVGA3dCmdSurfaceDMASuffix --
 *
 *    This is a command suffix that will appear after a SurfaceDMA command in
 *    the FIFO.  It contains some extra information that hosts may use to
 *    optimize performance or protect the guest.  This suffix exists to preserve
 *    backwards compatibility while also allowing for new functionality to be
 *    implemented.
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 suffixSize;

   /*
    * The maximum offset is used to determine the maximum offset from the
    * guestPtr base address that will be accessed or written to during this
    * surfaceDMA.  If the suffix is supported, the host will respect this
    * boundary while performing surface DMAs.
    *
    * Defaults to MAX_UINT32
    */
   uint32 maximumOffset;

   /*
    * A set of flags that describes optimizations that the host may perform
    * while performing this surface DMA operation.  The guest should never rely
    * on behaviour that is different when these flags are set for correctness.
    *
    * Defaults to 0
    */
   SVGA3dSurfaceDMAFlags flags;
}
#include "vmware_pack_end.h"
SVGA3dCmdSurfaceDMASuffix;

/*
 * SVGA_3D_CMD_DRAW_PRIMITIVES --
 *
 *   This command is the SVGA3D device's generic drawing entry point.
 *   It can draw multiple ranges of primitives, optionally using an
 *   index buffer, using an arbitrary collection of vertex buffers.
 *
 *   Each SVGA3dVertexDecl defines a distinct vertex array to bind
 *   during this draw call. The declarations specify which surface
 *   the vertex data lives in, what that vertex data is used for,
 *   and how to interpret it.
 *
 *   Each SVGA3dPrimitiveRange defines a collection of primitives
 *   to render using the same vertex arrays. An index buffer is
 *   optional.
 */

typedef
#include "vmware_pack_begin.h"
struct {
   /*
    * A range hint is an optional specification for the range of indices
    * in an SVGA3dArray that will be used. If 'last' is zero, it is assumed
    * that the entire array will be used.
    *
    * These are only hints. The SVGA3D device may use them for
    * performance optimization if possible, but it's also allowed to
    * ignore these values.
    */
   uint32               first;
   uint32               last;
}
#include "vmware_pack_end.h"
SVGA3dArrayRangeHint;

typedef
#include "vmware_pack_begin.h"
struct {
   /*
    * Define the origin and shape of a vertex or index array. Both
    * 'offset' and 'stride' are in bytes. The provided surface will be
    * reinterpreted as a flat array of bytes in the same format used
    * by surface DMA operations. To avoid unnecessary conversions, the
    * surface should be created with the SVGA3D_BUFFER format.
    *
    * Index 0 in the array starts 'offset' bytes into the surface.
    * Index 1 begins at byte 'offset + stride', etc. Array indices may
    * not be negative.
    */
   uint32               surfaceId;
   uint32               offset;
   uint32               stride;
}
#include "vmware_pack_end.h"
SVGA3dArray;

typedef
#include "vmware_pack_begin.h"
struct {
   /*
    * Describe a vertex array's data type, and define how it is to be
    * used by the fixed function pipeline or the vertex shader. It
    * isn't useful to have two VertexDecls with the same
    * VertexArrayIdentity in one draw call.
    */
   SVGA3dDeclType       type;
   SVGA3dDeclMethod     method;
   SVGA3dDeclUsage      usage;
   uint32               usageIndex;
}
#include "vmware_pack_end.h"
SVGA3dVertexArrayIdentity;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dVertexDecl {
   SVGA3dVertexArrayIdentity  identity;
   SVGA3dArray                array;
   SVGA3dArrayRangeHint       rangeHint;
}
#include "vmware_pack_end.h"
SVGA3dVertexDecl;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dPrimitiveRange {
   /*
    * Define a group of primitives to render, from sequential indices.
    *
    * The value of 'primitiveType' and 'primitiveCount' imply the
    * total number of vertices that will be rendered.
    */
   SVGA3dPrimitiveType  primType;
   uint32               primitiveCount;

   /*
    * Optional index buffer. If indexArray.surfaceId is
    * SVGA3D_INVALID_ID, we render without an index buffer. Rendering
    * without an index buffer is identical to rendering with an index
    * buffer containing the sequence [0, 1, 2, 3, ...].
    *
    * If an index buffer is in use, indexWidth specifies the width in
    * bytes of each index value. It must be less than or equal to
    * indexArray.stride.
    *
    * (Currently, the SVGA3D device requires index buffers to be tightly
    * packed. In other words, indexWidth == indexArray.stride)
    */
   SVGA3dArray          indexArray;
   uint32               indexWidth;

   /*
    * Optional index bias. This number is added to all indices from
    * indexArray before they are used as vertex array indices. This
    * can be used in multiple ways:
    *
    *  - When not using an indexArray, this bias can be used to
    *    specify where in the vertex arrays to begin rendering.
    *
    *  - A positive number here is equivalent to increasing the
    *    offset in each vertex array.
    *
    *  - A negative number can be used to render using a small
    *    vertex array and an index buffer that contains large
    *    values. This may be used by some applications that
    *    crop a vertex buffer without modifying their index
    *    buffer.
    *
    * Note that rendering with a negative bias value may be slower and
    * use more memory than rendering with a positive or zero bias.
    */
   int32                indexBias;
}
#include "vmware_pack_end.h"
SVGA3dPrimitiveRange;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   uint32               numVertexDecls;
   uint32               numRanges;

   /*
    * There are two variable size arrays after the
    * SVGA3dCmdDrawPrimitives structure. In order,
    * they are:
    *
    * 1. SVGA3dVertexDecl, quantity 'numVertexDecls', but no more than
    *    SVGA3D_MAX_VERTEX_ARRAYS;
    * 2. SVGA3dPrimitiveRange, quantity 'numRanges', but no more than
    *    SVGA3D_MAX_DRAW_PRIMITIVE_RANGES;
    * 3. Optionally, SVGA3dVertexDivisor, quantity 'numVertexDecls' (contains
    *    the frequency divisor for the corresponding vertex decl).
    */
}
#include "vmware_pack_end.h"
SVGA3dCmdDrawPrimitives;      /* SVGA_3D_CMD_DRAWPRIMITIVES */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 cid;

   uint32 primitiveCount;        /* How many primitives to render */
   uint32 startVertexLocation;   /* Which vertex do we start rendering at. */

   uint8 primitiveType;          /* SVGA3dPrimitiveType */
   uint8 padding[3];
}
#include "vmware_pack_end.h"
SVGA3dCmdDraw;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 cid;

   uint8 primitiveType;       /* SVGA3dPrimitiveType */

   uint32 indexBufferSid;     /* Valid index buffer sid. */
   uint32 indexBufferOffset;  /* Byte offset into the vertex buffer, almost */
			      /* always 0 for DX9 guests, non-zero for OpenGL */
                              /* guests.  We can't represent non-multiple of */
                              /* stride offsets in D3D9Renderer... */
   uint8 indexBufferStride;   /* Allowable values = 1, 2, or 4 */

   int32 baseVertexLocation;  /* Bias applied to the index when selecting a */
                              /* vertex from the streams, may be negative */

   uint32 primitiveCount;     /* How many primitives to render */
   uint32 pad0;
   uint16 pad1;
}
#include "vmware_pack_end.h"
SVGA3dCmdDrawIndexed;

typedef
#include "vmware_pack_begin.h"
struct {
   /*
    * Describe a vertex array's data type, and define how it is to be
    * used by the fixed function pipeline or the vertex shader. It
    * isn't useful to have two VertexDecls with the same
    * VertexArrayIdentity in one draw call.
    */
   uint16 streamOffset;
   uint8 stream;
   uint8 type;          /* SVGA3dDeclType */
   uint8 method;        /* SVGA3dDeclMethod */
   uint8 usage;         /* SVGA3dDeclUsage */
   uint8 usageIndex;
   uint8 padding;

}
#include "vmware_pack_end.h"
SVGA3dVertexElement;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 cid;

   uint32 numElements;

   /*
    * Followed by numElements SVGA3dVertexElement structures.
    *
    * If numElements < SVGA3D_MAX_VERTEX_ARRAYS, the remaining elements
    * are cleared and will not be used by following draws.
    */
}
#include "vmware_pack_end.h"
SVGA3dCmdSetVertexDecls;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 sid;
   uint32 stride;
   uint32 offset;
}
#include "vmware_pack_end.h"
SVGA3dVertexStream;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 cid;

   uint32 numStreams;
   /*
    * Followed by numStream SVGA3dVertexStream structures.
    *
    * If numStreams < SVGA3D_MAX_VERTEX_ARRAYS, the remaining streams
    * are cleared and will not be used by following draws.
    */
}
#include "vmware_pack_end.h"
SVGA3dCmdSetVertexStreams;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 cid;
   uint32 numDivisors;
}
#include "vmware_pack_end.h"
SVGA3dCmdSetVertexDivisors;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32                   stage;
   SVGA3dTextureStateName   name;
   union {
      uint32                value;
      float                 floatValue;
   };
}
#include "vmware_pack_end.h"
SVGA3dTextureState;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   /* Followed by variable number of SVGA3dTextureState structures */
}
#include "vmware_pack_end.h"
SVGA3dCmdSetTextureState;      /* SVGA_3D_CMD_SETTEXTURESTATE */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32                   cid;
   SVGA3dTransformType      type;
   float                    matrix[16];
}
#include "vmware_pack_end.h"
SVGA3dCmdSetTransform;          /* SVGA_3D_CMD_SETTRANSFORM */

typedef
#include "vmware_pack_begin.h"
struct {
   float                min;
   float                max;
}
#include "vmware_pack_end.h"
SVGA3dZRange;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   SVGA3dZRange         zRange;
}
#include "vmware_pack_end.h"
SVGA3dCmdSetZRange;             /* SVGA_3D_CMD_SETZRANGE */

typedef
#include "vmware_pack_begin.h"
struct {
   float                diffuse[4];
   float                ambient[4];
   float                specular[4];
   float                emissive[4];
   float                shininess;
}
#include "vmware_pack_end.h"
SVGA3dMaterial;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   SVGA3dFace           face;
   SVGA3dMaterial       material;
}
#include "vmware_pack_end.h"
SVGA3dCmdSetMaterial;           /* SVGA_3D_CMD_SETMATERIAL */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   uint32               index;
   SVGA3dLightData      data;
}
#include "vmware_pack_end.h"
SVGA3dCmdSetLightData;           /* SVGA_3D_CMD_SETLIGHTDATA */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   uint32               index;
   uint32               enabled;
}
#include "vmware_pack_end.h"
SVGA3dCmdSetLightEnabled;      /* SVGA_3D_CMD_SETLIGHTENABLED */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   SVGA3dRect           rect;
}
#include "vmware_pack_end.h"
SVGA3dCmdSetViewport;           /* SVGA_3D_CMD_SETVIEWPORT */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   SVGA3dRect           rect;
}
#include "vmware_pack_end.h"
SVGA3dCmdSetScissorRect;         /* SVGA_3D_CMD_SETSCISSORRECT */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   uint32               index;
   float                plane[4];
}
#include "vmware_pack_end.h"
SVGA3dCmdSetClipPlane;           /* SVGA_3D_CMD_SETCLIPPLANE */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   uint32               shid;
   SVGA3dShaderType     type;
   /* Followed by variable number of DWORDs for shader bycode */
}
#include "vmware_pack_end.h"
SVGA3dCmdDefineShader;           /* SVGA_3D_CMD_SHADER_DEFINE */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   uint32               shid;
   SVGA3dShaderType     type;
}
#include "vmware_pack_end.h"
SVGA3dCmdDestroyShader;         /* SVGA_3D_CMD_SHADER_DESTROY */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32                  cid;
   uint32                  reg;     /* register number */
   SVGA3dShaderType        type;
   SVGA3dShaderConstType   ctype;
   uint32                  values[4];

   /*
    * Followed by a variable number of additional values.
    */
}
#include "vmware_pack_end.h"
SVGA3dCmdSetShaderConst;        /* SVGA_3D_CMD_SET_SHADER_CONST */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   SVGA3dShaderType     type;
   uint32               shid;
}
#include "vmware_pack_end.h"
SVGA3dCmdSetShader;       /* SVGA_3D_CMD_SET_SHADER */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   SVGA3dQueryType      type;
}
#include "vmware_pack_end.h"
SVGA3dCmdBeginQuery;           /* SVGA_3D_CMD_BEGIN_QUERY */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   SVGA3dQueryType      type;
   SVGAGuestPtr         guestResult;   /* Points to an SVGA3dQueryResult structure */
}
#include "vmware_pack_end.h"
SVGA3dCmdEndQuery;                  /* SVGA_3D_CMD_END_QUERY */


/*
 * SVGA3D_CMD_WAIT_FOR_QUERY --
 *
 *    Will read the SVGA3dQueryResult structure pointed to by guestResult,
 *    and if the state member is set to anything else than
 *    SVGA3D_QUERYSTATE_PENDING, this command will always be a no-op.
 *
 *    Otherwise, in addition to the query explicitly waited for,
 *    All queries with the same type and issued with the same cid, for which
 *    an SVGA_3D_CMD_END_QUERY command has previously been sent, will
 *    be finished after execution of this command.
 *
 *    A query will be identified by the gmrId and offset of the guestResult
 *    member. If the device can't find an SVGA_3D_CMD_END_QUERY that has
 *    been sent previously with an indentical gmrId and offset, it will
 *    effectively end all queries with an identical type issued with the
 *    same cid, and the SVGA3dQueryResult structure pointed to by
 *    guestResult will not be written to. This property can be used to
 *    implement a query barrier for a given cid and query type.
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;        /* Same parameters passed to END_QUERY */
   SVGA3dQueryType      type;
   SVGAGuestPtr         guestResult;
}
#include "vmware_pack_end.h"
SVGA3dCmdWaitForQuery;              /* SVGA_3D_CMD_WAIT_FOR_QUERY */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               totalSize;    /* Set by guest before query is ended. */
   SVGA3dQueryState     state;        /* Set by host or guest. See SVGA3dQueryState. */
   union {                            /* Set by host on exit from PENDING state */
      uint32            result32;
      uint32            queryCookie; /* May be used to identify which QueryGetData this
                                        result corresponds to. */
   };
}
#include "vmware_pack_end.h"
SVGA3dQueryResult;


/*
 * SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN --
 *
 *    This is a blit from an SVGA3D surface to a Screen Object.
 *    This blit must be directed at a specific screen.
 *
 *    The blit copies from a rectangular region of an SVGA3D surface
 *    image to a rectangular region of a screen.
 *
 *    This command takes an optional variable-length list of clipping
 *    rectangles after the body of the command. If no rectangles are
 *    specified, there is no clipping region. The entire destRect is
 *    drawn to. If one or more rectangles are included, they describe
 *    a clipping region. The clip rectangle coordinates are measured
 *    relative to the top-left corner of destRect.
 *
 *    The srcImage must be from mip=0 face=0.
 *
 *    This supports scaling if the src and dest are of different sizes.
 *
 * Availability:
 *    SVGA_FIFO_CAP_SCREEN_OBJECT
 */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dSurfaceImageId srcImage;
   SVGASignedRect       srcRect;
   uint32               destScreenId; /* Screen Object ID */
   SVGASignedRect       destRect;
   /* Clipping: zero or more SVGASignedRects follow */
}
#include "vmware_pack_end.h"
SVGA3dCmdBlitSurfaceToScreen;         /* SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               sid;
   SVGA3dTextureFilter  filter;
}
#include "vmware_pack_end.h"
SVGA3dCmdGenerateMipmaps;             /* SVGA_3D_CMD_GENERATE_MIPMAPS */



typedef
#include "vmware_pack_begin.h"
struct {
   uint32 sid;
}
#include "vmware_pack_end.h"
SVGA3dCmdActivateSurface;               /* SVGA_3D_CMD_ACTIVATE_SURFACE */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 sid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDeactivateSurface;             /* SVGA_3D_CMD_DEACTIVATE_SURFACE */

/*
 * Screen DMA command
 *
 * Available with SVGA_FIFO_CAP_SCREEN_OBJECT_2.  The SVGA_CAP_3D device
 * cap bit is not required.
 *
 * - refBuffer and destBuffer are 32bit BGRX; refBuffer and destBuffer could
 *   be different, but it is required that guest makes sure refBuffer has
 *   exactly the same contents that were written to when last time screen DMA
 *   command is received by host.
 *
 * - changemap is generated by lib/blit, and it has the changes from last
 *   received screen DMA or more.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdScreenDMA {
   uint32 screenId;
   SVGAGuestImage refBuffer;
   SVGAGuestImage destBuffer;
   SVGAGuestImage changeMap;
}
#include "vmware_pack_end.h"
SVGA3dCmdScreenDMA;        /* SVGA_3D_CMD_SCREEN_DMA */

/*
 * Set Unity Surface Cookie
 *
 * Associates the supplied cookie with the surface id for use with
 * Unity.  This cookie is a hint from guest to host, there is no way
 * for the guest to readback the cookie and the host is free to drop
 * the cookie association at will.  The default value for the cookie
 * on all surfaces is 0.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdSetUnitySurfaceCookie {
   uint32 sid;
   uint64 cookie;
}
#include "vmware_pack_end.h"
SVGA3dCmdSetUnitySurfaceCookie;   /* SVGA_3D_CMD_SET_UNITY_SURFACE_COOKIE */

/*
 * Open a context-specific surface in a non-context-specific manner.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdOpenContextSurface {
   uint32 sid;
}
#include "vmware_pack_end.h"
SVGA3dCmdOpenContextSurface;   /* SVGA_3D_CMD_OPEN_CONTEXT_SURFACE */


/*
 * Logic ops
 */

#define SVGA3D_LOTRANSBLT_HONORALPHA     (0x01)
#define SVGA3D_LOSTRETCHBLT_MIRRORX      (0x01)
#define SVGA3D_LOSTRETCHBLT_MIRRORY      (0x02)
#define SVGA3D_LOALPHABLEND_SRCHASALPHA  (0x01)

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdLogicOpsBitBlt {
   /*
    * All LogicOps surfaces are one-level
    * surfaces so mipmap & face should always
    * be zero.
    */
   SVGA3dSurfaceImageId src;
   SVGA3dSurfaceImageId dst;
   SVGA3dLogicOp logicOp;
   /* Followed by variable number of SVGA3dCopyBox structures */
}
#include "vmware_pack_end.h"
SVGA3dCmdLogicOpsBitBlt;   /* SVGA_3D_CMD_LOGICOPS_BITBLT */


typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdLogicOpsTransBlt {
   /*
    * All LogicOps surfaces are one-level
    * surfaces so mipmap & face should always
    * be zero.
    */
   SVGA3dSurfaceImageId src;
   SVGA3dSurfaceImageId dst;
   uint32 color;
   uint32 flags;
   SVGA3dBox srcBox;
   SVGA3dBox dstBox;
}
#include "vmware_pack_end.h"
SVGA3dCmdLogicOpsTransBlt;   /* SVGA_3D_CMD_LOGICOPS_TRANSBLT */


typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdLogicOpsStretchBlt {
   /*
    * All LogicOps surfaces are one-level
    * surfaces so mipmap & face should always
    * be zero.
    */
   SVGA3dSurfaceImageId src;
   SVGA3dSurfaceImageId dst;
   uint16 mode;
   uint16 flags;
   SVGA3dBox srcBox;
   SVGA3dBox dstBox;
}
#include "vmware_pack_end.h"
SVGA3dCmdLogicOpsStretchBlt;   /* SVGA_3D_CMD_LOGICOPS_STRETCHBLT */


typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdLogicOpsColorFill {
   /*
    * All LogicOps surfaces are one-level
    * surfaces so mipmap & face should always
    * be zero.
    */
   SVGA3dSurfaceImageId dst;
   uint32 color;
   SVGA3dLogicOp logicOp;
   /* Followed by variable number of SVGA3dRect structures. */
}
#include "vmware_pack_end.h"
SVGA3dCmdLogicOpsColorFill;   /* SVGA_3D_CMD_LOGICOPS_COLORFILL */


typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdLogicOpsAlphaBlend {
   /*
    * All LogicOps surfaces are one-level
    * surfaces so mipmap & face should always
    * be zero.
    */
   SVGA3dSurfaceImageId src;
   SVGA3dSurfaceImageId dst;
   uint32 alphaVal;
   uint32 flags;
   SVGA3dBox srcBox;
   SVGA3dBox dstBox;
}
#include "vmware_pack_end.h"
SVGA3dCmdLogicOpsAlphaBlend;   /* SVGA_3D_CMD_LOGICOPS_ALPHABLEND */

#define SVGA3D_CLEARTYPE_INVALID_GAMMA_INDEX 0xFFFFFFFF

#define SVGA3D_CLEARTYPE_GAMMA_WIDTH  512
#define SVGA3D_CLEARTYPE_GAMMA_HEIGHT 16

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdLogicOpsClearTypeBlend {
   /*
    * All LogicOps surfaces are one-level
    * surfaces so mipmap & face should always
    * be zero.
    */
   SVGA3dSurfaceImageId tmp;
   SVGA3dSurfaceImageId dst;
   SVGA3dSurfaceImageId gammaSurf;
   SVGA3dSurfaceImageId alphaSurf;
   uint32 gamma;
   uint32 color;
   uint32 color2;
   int32 alphaOffsetX;
   int32 alphaOffsetY;
   /* Followed by variable number of SVGA3dBox structures */
}
#include "vmware_pack_end.h"
SVGA3dCmdLogicOpsClearTypeBlend;   /* SVGA_3D_CMD_LOGICOPS_CLEARTYPEBLEND */


/*
 * Guest-backed objects definitions.
 */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGAMobFormat ptDepth;
   uint32 sizeInBytes;
   PPN64 base;
}
#include "vmware_pack_end.h"
SVGAOTableMobEntry;
#define SVGA3D_OTABLE_MOB_ENTRY_SIZE (sizeof(SVGAOTableMobEntry))

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dSurfaceFormat format;
   SVGA3dSurfaceFlags surfaceFlags;
   uint32 numMipLevels;
   uint32 multisampleCount;
   SVGA3dTextureFilter autogenFilter;
   SVGA3dSize size;
   SVGAMobId mobid;
   uint32 arraySize;
   uint32 mobPitch;
   uint32 pad[5];
}
#include "vmware_pack_end.h"
SVGAOTableSurfaceEntry;
#define SVGA3D_OTABLE_SURFACE_ENTRY_SIZE (sizeof(SVGAOTableSurfaceEntry))

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 cid;
   SVGAMobId mobid;
}
#include "vmware_pack_end.h"
SVGAOTableContextEntry;
#define SVGA3D_OTABLE_CONTEXT_ENTRY_SIZE (sizeof(SVGAOTableContextEntry))

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dShaderType type;
   uint32 sizeInBytes;
   uint32 offsetInBytes;
   SVGAMobId mobid;
}
#include "vmware_pack_end.h"
SVGAOTableShaderEntry;
#define SVGA3D_OTABLE_SHADER_ENTRY_SIZE (sizeof(SVGAOTableShaderEntry))

#define SVGA_STFLAG_PRIMARY (1 << 0)
typedef uint32 SVGAScreenTargetFlags;

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dSurfaceImageId image;
   uint32 width;
   uint32 height;
   int32 xRoot;
   int32 yRoot;
   SVGAScreenTargetFlags flags;
   uint32 dpi;
   uint32 pad[7];
}
#include "vmware_pack_end.h"
SVGAOTableScreenTargetEntry;
#define SVGA3D_OTABLE_SCREEN_TARGET_ENTRY_SIZE \
	(sizeof(SVGAOTableScreenTargetEntry))

typedef
#include "vmware_pack_begin.h"
struct {
   float value[4];
}
#include "vmware_pack_end.h"
SVGA3dShaderConstFloat;

typedef
#include "vmware_pack_begin.h"
struct {
   int32 value[4];
}
#include "vmware_pack_end.h"
SVGA3dShaderConstInt;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 value;
}
#include "vmware_pack_end.h"
SVGA3dShaderConstBool;

typedef
#include "vmware_pack_begin.h"
struct {
   uint16 streamOffset;
   uint8 stream;
   uint8 type;
   uint8 methodUsage;
   uint8 usageIndex;
}
#include "vmware_pack_end.h"
SVGAGBVertexElement;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 sid;
   uint16 stride;
   uint32 offset;
}
#include "vmware_pack_end.h"
SVGAGBVertexStream;
typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dRect viewport;
   SVGA3dRect scissorRect;
   SVGA3dZRange zRange;

   SVGA3dSurfaceImageId renderTargets[SVGA3D_RT_MAX];
   SVGAGBVertexElement decl1[4];

   uint32 renderStates[SVGA3D_RS_MAX];
   SVGAGBVertexElement decl2[18];
   uint32 pad0[2];

   struct {
      SVGA3dFace face;
      SVGA3dMaterial material;
   } material;

   float clipPlanes[SVGA3D_NUM_CLIPPLANES][4];
   float matrices[SVGA3D_TRANSFORM_MAX][16];

   SVGA3dBool lightEnabled[SVGA3D_NUM_LIGHTS];
   SVGA3dLightData lightData[SVGA3D_NUM_LIGHTS];

   /*
    * Shaders currently bound
    */
   uint32 shaders[SVGA3D_NUM_SHADERTYPE_PREDX];
   SVGAGBVertexElement decl3[10];
   uint32 pad1[3];

   uint32 occQueryActive;
   uint32 occQueryValue;

   /*
    * Int/Bool Shader constants
    */
   SVGA3dShaderConstInt pShaderIValues[SVGA3D_CONSTINTREG_MAX];
   SVGA3dShaderConstInt vShaderIValues[SVGA3D_CONSTINTREG_MAX];
   uint16 pShaderBValues;
   uint16 vShaderBValues;


   SVGAGBVertexStream streams[SVGA3D_MAX_VERTEX_ARRAYS];
   SVGA3dVertexDivisor divisors[SVGA3D_MAX_VERTEX_ARRAYS];
   uint32 numVertexDecls;
   uint32 numVertexStreams;
   uint32 numVertexDivisors;
   uint32 pad2[30];

   /*
    * Texture Stages
    *
    * SVGA3D_TS_INVALID through SVGA3D_TS_CONSTANT are in the
    * textureStages array.
    * SVGA3D_TS_COLOR_KEY is in tsColorKey.
    */
   uint32 tsColorKey[SVGA3D_NUM_TEXTURE_UNITS];
   uint32 textureStages[SVGA3D_NUM_TEXTURE_UNITS][SVGA3D_TS_CONSTANT + 1];
   uint32 tsColorKeyEnable[SVGA3D_NUM_TEXTURE_UNITS];

   /*
    * Float Shader constants.
    */
   SVGA3dShaderConstFloat pShaderFValues[SVGA3D_CONSTREG_MAX];
   SVGA3dShaderConstFloat vShaderFValues[SVGA3D_CONSTREG_MAX];
}
#include "vmware_pack_end.h"
SVGAGBContextData;
#define SVGA3D_CONTEXT_DATA_SIZE (sizeof(SVGAGBContextData))

/*
 * SVGA3dCmdSetOTableBase --
 *
 * This command allows the guest to specify the base PPN of the
 * specified object table.
 */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGAOTableType type;
   PPN baseAddress;
   uint32 sizeInBytes;
   uint32 validSizeInBytes;
   SVGAMobFormat ptDepth;
}
#include "vmware_pack_end.h"
SVGA3dCmdSetOTableBase;  /* SVGA_3D_CMD_SET_OTABLE_BASE */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGAOTableType type;
   PPN64 baseAddress;
   uint32 sizeInBytes;
   uint32 validSizeInBytes;
   SVGAMobFormat ptDepth;
}
#include "vmware_pack_end.h"
SVGA3dCmdSetOTableBase64;  /* SVGA_3D_CMD_SET_OTABLE_BASE64 */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGAOTableType type;
}
#include "vmware_pack_end.h"
SVGA3dCmdReadbackOTable;  /* SVGA_3D_CMD_READBACK_OTABLE */

/*
 * Define a memory object (Mob) in the OTable.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDefineGBMob {
   SVGAMobId mobid;
   SVGAMobFormat ptDepth;
   PPN base;
   uint32 sizeInBytes;
}
#include "vmware_pack_end.h"
SVGA3dCmdDefineGBMob;   /* SVGA_3D_CMD_DEFINE_GB_MOB */


/*
 * Destroys an object in the OTable.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDestroyGBMob {
   SVGAMobId mobid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDestroyGBMob;   /* SVGA_3D_CMD_DESTROY_GB_MOB */


/*
 * Define a memory object (Mob) in the OTable with a PPN64 base.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDefineGBMob64 {
   SVGAMobId mobid;
   SVGAMobFormat ptDepth;
   PPN64 base;
   uint32 sizeInBytes;
}
#include "vmware_pack_end.h"
SVGA3dCmdDefineGBMob64;   /* SVGA_3D_CMD_DEFINE_GB_MOB64 */

/*
 * Redefine an object in the OTable with PPN64 base.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdRedefineGBMob64 {
   SVGAMobId mobid;
   SVGAMobFormat ptDepth;
   PPN64 base;
   uint32 sizeInBytes;
}
#include "vmware_pack_end.h"
SVGA3dCmdRedefineGBMob64;   /* SVGA_3D_CMD_REDEFINE_GB_MOB64 */

/*
 * Notification that the page tables have been modified.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdUpdateGBMobMapping {
   SVGAMobId mobid;
}
#include "vmware_pack_end.h"
SVGA3dCmdUpdateGBMobMapping;   /* SVGA_3D_CMD_UPDATE_GB_MOB_MAPPING */

/*
 * Define a guest-backed surface.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDefineGBSurface {
   uint32 sid;
   SVGA3dSurfaceFlags surfaceFlags;
   SVGA3dSurfaceFormat format;
   uint32 numMipLevels;
   uint32 multisampleCount;
   SVGA3dTextureFilter autogenFilter;
   SVGA3dSize size;
}
#include "vmware_pack_end.h"
SVGA3dCmdDefineGBSurface;   /* SVGA_3D_CMD_DEFINE_GB_SURFACE */

/*
 * Destroy a guest-backed surface.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDestroyGBSurface {
   uint32 sid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDestroyGBSurface;   /* SVGA_3D_CMD_DESTROY_GB_SURFACE */

/*
 * Bind a guest-backed surface to a mob.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdBindGBSurface {
   uint32 sid;
   SVGAMobId mobid;
}
#include "vmware_pack_end.h"
SVGA3dCmdBindGBSurface;   /* SVGA_3D_CMD_BIND_GB_SURFACE */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdBindGBSurfaceWithPitch {
   uint32 sid;
   SVGAMobId mobid;
   uint32 baseLevelPitch;
}
#include "vmware_pack_end.h"
SVGA3dCmdBindGBSurfaceWithPitch;   /* SVGA_3D_CMD_BIND_GB_SURFACE_WITH_PITCH */

/*
 * Conditionally bind a mob to a guest-backed surface if testMobid
 * matches the currently bound mob.  Optionally issue a
 * readback/update on the surface while it is still bound to the old
 * mobid if the mobid is changed by this command.
 */

#define SVGA3D_COND_BIND_GB_SURFACE_FLAG_READBACK (1 << 0)
#define SVGA3D_COND_BIND_GB_SURFACE_FLAG_UPDATE   (1 << 1)

typedef
#include "vmware_pack_begin.h"
struct{
   uint32 sid;
   SVGAMobId testMobid;
   SVGAMobId mobid;
   uint32 flags;
}
#include "vmware_pack_end.h"
SVGA3dCmdCondBindGBSurface;          /* SVGA_3D_CMD_COND_BIND_GB_SURFACE */

/*
 * Update an image in a guest-backed surface.
 * (Inform the device that the guest-contents have been updated.)
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdUpdateGBImage {
   SVGA3dSurfaceImageId image;
   SVGA3dBox box;
}
#include "vmware_pack_end.h"
SVGA3dCmdUpdateGBImage;   /* SVGA_3D_CMD_UPDATE_GB_IMAGE */

/*
 * Update an entire guest-backed surface.
 * (Inform the device that the guest-contents have been updated.)
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdUpdateGBSurface {
   uint32 sid;
}
#include "vmware_pack_end.h"
SVGA3dCmdUpdateGBSurface;   /* SVGA_3D_CMD_UPDATE_GB_SURFACE */

/*
 * Readback an image in a guest-backed surface.
 * (Request the device to flush the dirty contents into the guest.)
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdReadbackGBImage {
   SVGA3dSurfaceImageId image;
}
#include "vmware_pack_end.h"
SVGA3dCmdReadbackGBImage;   /* SVGA_3D_CMD_READBACK_GB_IMAGE */

/*
 * Readback an entire guest-backed surface.
 * (Request the device to flush the dirty contents into the guest.)
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdReadbackGBSurface {
   uint32 sid;
}
#include "vmware_pack_end.h"
SVGA3dCmdReadbackGBSurface;   /* SVGA_3D_CMD_READBACK_GB_SURFACE */

/*
 * Readback a sub rect of an image in a guest-backed surface.  After
 * issuing this command the driver is required to issue an update call
 * of the same region before issuing any other commands that reference
 * this surface or rendering is not guaranteed.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdReadbackGBImagePartial {
   SVGA3dSurfaceImageId image;
   SVGA3dBox box;
   uint32 invertBox;
}
#include "vmware_pack_end.h"
SVGA3dCmdReadbackGBImagePartial; /* SVGA_3D_CMD_READBACK_GB_IMAGE_PARTIAL */


/*
 * Invalidate an image in a guest-backed surface.
 * (Notify the device that the contents can be lost.)
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdInvalidateGBImage {
   SVGA3dSurfaceImageId image;
}
#include "vmware_pack_end.h"
SVGA3dCmdInvalidateGBImage;   /* SVGA_3D_CMD_INVALIDATE_GB_IMAGE */

/*
 * Invalidate an entire guest-backed surface.
 * (Notify the device that the contents if all images can be lost.)
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdInvalidateGBSurface {
   uint32 sid;
}
#include "vmware_pack_end.h"
SVGA3dCmdInvalidateGBSurface; /* SVGA_3D_CMD_INVALIDATE_GB_SURFACE */

/*
 * Invalidate a sub rect of an image in a guest-backed surface.  After
 * issuing this command the driver is required to issue an update call
 * of the same region before issuing any other commands that reference
 * this surface or rendering is not guaranteed.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdInvalidateGBImagePartial {
   SVGA3dSurfaceImageId image;
   SVGA3dBox box;
   uint32 invertBox;
}
#include "vmware_pack_end.h"
SVGA3dCmdInvalidateGBImagePartial; /* SVGA_3D_CMD_INVALIDATE_GB_IMAGE_PARTIAL */


/*
 * Define a guest-backed context.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDefineGBContext {
   uint32 cid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDefineGBContext;   /* SVGA_3D_CMD_DEFINE_GB_CONTEXT */

/*
 * Destroy a guest-backed context.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDestroyGBContext {
   uint32 cid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDestroyGBContext;   /* SVGA_3D_CMD_DESTROY_GB_CONTEXT */

/*
 * Bind a guest-backed context.
 *
 * validContents should be set to 0 for new contexts,
 * and 1 if this is an old context which is getting paged
 * back on to the device.
 *
 * For new contexts, it is recommended that the driver
 * issue commands to initialize all interesting state
 * prior to rendering.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdBindGBContext {
   uint32 cid;
   SVGAMobId mobid;
   uint32 validContents;
}
#include "vmware_pack_end.h"
SVGA3dCmdBindGBContext;   /* SVGA_3D_CMD_BIND_GB_CONTEXT */

/*
 * Readback a guest-backed context.
 * (Request that the device flush the contents back into guest memory.)
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdReadbackGBContext {
   uint32 cid;
}
#include "vmware_pack_end.h"
SVGA3dCmdReadbackGBContext;   /* SVGA_3D_CMD_READBACK_GB_CONTEXT */

/*
 * Invalidate a guest-backed context.
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdInvalidateGBContext {
   uint32 cid;
}
#include "vmware_pack_end.h"
SVGA3dCmdInvalidateGBContext;   /* SVGA_3D_CMD_INVALIDATE_GB_CONTEXT */

/*
 * Define a guest-backed shader.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDefineGBShader {
   uint32 shid;
   SVGA3dShaderType type;
   uint32 sizeInBytes;
}
#include "vmware_pack_end.h"
SVGA3dCmdDefineGBShader;   /* SVGA_3D_CMD_DEFINE_GB_SHADER */

/*
 * Bind a guest-backed shader.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdBindGBShader {
   uint32 shid;
   SVGAMobId mobid;
   uint32 offsetInBytes;
}
#include "vmware_pack_end.h"
SVGA3dCmdBindGBShader;   /* SVGA_3D_CMD_BIND_GB_SHADER */

/*
 * Destroy a guest-backed shader.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDestroyGBShader {
   uint32 shid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDestroyGBShader;   /* SVGA_3D_CMD_DESTROY_GB_SHADER */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32                  cid;
   uint32                  regStart;
   SVGA3dShaderType        shaderType;
   SVGA3dShaderConstType   constType;

   /*
    * Followed by a variable number of shader constants.
    *
    * Note that FLOAT and INT constants are 4-dwords in length, while
    * BOOL constants are 1-dword in length.
    */
}
#include "vmware_pack_end.h"
SVGA3dCmdSetGBShaderConstInline;   /* SVGA_3D_CMD_SET_GB_SHADERCONSTS_INLINE */


typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   SVGA3dQueryType      type;
}
#include "vmware_pack_end.h"
SVGA3dCmdBeginGBQuery;           /* SVGA_3D_CMD_BEGIN_GB_QUERY */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   SVGA3dQueryType      type;
   SVGAMobId mobid;
   uint32 offset;
}
#include "vmware_pack_end.h"
SVGA3dCmdEndGBQuery;                  /* SVGA_3D_CMD_END_GB_QUERY */


/*
 * SVGA_3D_CMD_WAIT_FOR_GB_QUERY --
 *
 *    The semantics of this command are identical to the
 *    SVGA_3D_CMD_WAIT_FOR_QUERY except that the results are written
 *    to a Mob instead of a GMR.
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32               cid;
   SVGA3dQueryType      type;
   SVGAMobId mobid;
   uint32 offset;
}
#include "vmware_pack_end.h"
SVGA3dCmdWaitForGBQuery;          /* SVGA_3D_CMD_WAIT_FOR_GB_QUERY */


typedef
#include "vmware_pack_begin.h"
struct {
   SVGAMobId mobid;
   uint32 mustBeZero;
   uint32 initialized;
}
#include "vmware_pack_end.h"
SVGA3dCmdEnableGart;              /* SVGA_3D_CMD_ENABLE_GART */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGAMobId mobid;
   uint32 gartOffset;
}
#include "vmware_pack_end.h"
SVGA3dCmdMapMobIntoGart;          /* SVGA_3D_CMD_MAP_MOB_INTO_GART */


typedef
#include "vmware_pack_begin.h"
struct {
   uint32 gartOffset;
   uint32 numPages;
}
#include "vmware_pack_end.h"
SVGA3dCmdUnmapGartRange;          /* SVGA_3D_CMD_UNMAP_GART_RANGE */


/*
 * Screen Targets
 */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 stid;
   uint32 width;
   uint32 height;
   int32 xRoot;
   int32 yRoot;
   SVGAScreenTargetFlags flags;

   /*
    * The physical DPI that the guest expects this screen displayed at.
    *
    * Guests which are not DPI-aware should set this to zero.
    */
   uint32 dpi;
}
#include "vmware_pack_end.h"
SVGA3dCmdDefineGBScreenTarget;    /* SVGA_3D_CMD_DEFINE_GB_SCREENTARGET */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 stid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDestroyGBScreenTarget;  /* SVGA_3D_CMD_DESTROY_GB_SCREENTARGET */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 stid;
   SVGA3dSurfaceImageId image;
}
#include "vmware_pack_end.h"
SVGA3dCmdBindGBScreenTarget;  /* SVGA_3D_CMD_BIND_GB_SCREENTARGET */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 stid;
   SVGA3dRect rect;
}
#include "vmware_pack_end.h"
SVGA3dCmdUpdateGBScreenTarget;  /* SVGA_3D_CMD_UPDATE_GB_SCREENTARGET */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdGBScreenDMA {
   uint32 screenId;
   uint32 dead;
   SVGAMobId destMobID;
   uint32 destPitch;
   SVGAMobId changeMapMobID;
}
#include "vmware_pack_end.h"
SVGA3dCmdGBScreenDMA;        /* SVGA_3D_CMD_GB_SCREEN_DMA */

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 value;
   uint32 mobId;
   uint32 mobOffset;
}
#include "vmware_pack_end.h"
SVGA3dCmdGBMobFence;  /* SVGA_3D_CMD_GB_MOB_FENCE*/

#endif /* _SVGA3D_CMD_H_ */
