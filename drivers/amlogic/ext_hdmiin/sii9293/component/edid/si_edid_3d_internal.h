//***************************************************************************
//!file     si_edid_3d_internal.h
//!brief    Silicon Image CEC Component.
//
// No part of this work may be reproduced, modified, distributed,
// transmitted, transcribed, or translated into any language or computer
// format, in any form or by any means without written permission of
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2002-2013, Silicon Image, Inc.  All rights reserved.
//***************************************************************************/

#ifndef __SI_EDID_3D_INTERNAL_H__
#define __SI_EDID_3D_INTERNAL_H__

#include "si_common.h"

//-------------------------------------------------------------------------------
// typedefs, and manifest constants
//-------------------------------------------------------------------------------

#define HDMI_3D_SVD_STRUCTURE_LENGTH 16                     // maximum by HDMI 1.4 spec

typedef struct svd 
{
    bit_fld_t   Vic     : 7;
    bit_fld_t   Native  : 1;
}svd_t, *p_svd_t;

typedef union VIC3DFormat
{
    uint8_t        Data;
    struct{
        bit_fld_t FrameSequential:1;    //FS_SUPP
        bit_fld_t TopBottom:1;          //TB_SUPP
        bit_fld_t LeftRight:1;          //LR_SUPP      
    }Fields;

}VIC3DFormat_t;

typedef struct Mandatory3dFmt
{
    svd_t VicCode;
    VIC3DFormat_t vic3dFmt;
}Mandatory3dFmt_t;

#define Mandatory3dFmt_60       3
#define Mandatory3dFmt_50       3

// VIC in mandatory 3D formats

#define VIC_1080P_24            32
#define VIC_1080i_50            20
#define VIC_1080i_60            5
#define VIC_720P_50             19
#define VIC_720P_60             4

// MHL 3D formats

#define FRAME_SEQUENTIAL  0x01
#define TOP_BOTTOM        0x02
#define LEFT_RIGHT        0x04

#endif // __SI_EDID_3D_INTERNAL_H__

