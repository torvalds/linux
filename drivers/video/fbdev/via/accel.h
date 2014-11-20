/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __ACCEL_H__
#define __ACCEL_H__

#define FB_ACCEL_VIA_UNICHROME  50

/* MMIO Base Address Definition */
#define MMIO_VGABASE                0x8000
#define MMIO_CR_READ                (MMIO_VGABASE + 0x3D4)
#define MMIO_CR_WRITE               (MMIO_VGABASE + 0x3D5)
#define MMIO_SR_READ                (MMIO_VGABASE + 0x3C4)
#define MMIO_SR_WRITE               (MMIO_VGABASE + 0x3C5)

/* HW Cursor Status Define */
#define HW_Cursor_ON    0
#define HW_Cursor_OFF   1

#define CURSOR_SIZE     (8 * 1024)
#define VQ_SIZE         (256 * 1024)

#define VIA_MMIO_BLTBASE        0x200000
#define VIA_MMIO_BLTSIZE        0x200000

/* Defines for 2D registers */
#define VIA_REG_GECMD           0x000
#define VIA_REG_GEMODE          0x004
#define VIA_REG_SRCPOS          0x008
#define VIA_REG_DSTPOS          0x00C
/* width and height */
#define VIA_REG_DIMENSION       0x010
#define VIA_REG_PATADDR         0x014
#define VIA_REG_FGCOLOR         0x018
#define VIA_REG_BGCOLOR         0x01C
/* top and left of clipping */
#define VIA_REG_CLIPTL          0x020
/* bottom and right of clipping */
#define VIA_REG_CLIPBR          0x024
#define VIA_REG_OFFSET          0x028
/* color key control */
#define VIA_REG_KEYCONTROL      0x02C
#define VIA_REG_SRCBASE         0x030
#define VIA_REG_DSTBASE         0x034
/* pitch of src and dst */
#define VIA_REG_PITCH           0x038
#define VIA_REG_MONOPAT0        0x03C
#define VIA_REG_MONOPAT1        0x040
/* from 0x100 to 0x1ff */
#define VIA_REG_COLORPAT        0x100

/* defines for VIA 2D registers for vt3353/3409 (M1 engine)*/
#define VIA_REG_GECMD_M1        0x000
#define VIA_REG_GEMODE_M1       0x004
#define VIA_REG_GESTATUS_M1     0x004       /* as same as VIA_REG_GEMODE */
#define VIA_REG_PITCH_M1        0x008       /* pitch of src and dst */
#define VIA_REG_DIMENSION_M1    0x00C       /* width and height */
#define VIA_REG_DSTPOS_M1       0x010
#define VIA_REG_LINE_XY_M1      0x010
#define VIA_REG_DSTBASE_M1      0x014
#define VIA_REG_SRCPOS_M1       0x018
#define VIA_REG_LINE_K1K2_M1    0x018
#define VIA_REG_SRCBASE_M1      0x01C
#define VIA_REG_PATADDR_M1      0x020
#define VIA_REG_MONOPAT0_M1     0x024
#define VIA_REG_MONOPAT1_M1     0x028
#define VIA_REG_OFFSET_M1       0x02C
#define VIA_REG_LINE_ERROR_M1   0x02C
#define VIA_REG_CLIPTL_M1       0x040       /* top and left of clipping */
#define VIA_REG_CLIPBR_M1       0x044       /* bottom and right of clipping */
#define VIA_REG_KEYCONTROL_M1   0x048       /* color key control */
#define VIA_REG_FGCOLOR_M1      0x04C
#define VIA_REG_DSTCOLORKEY_M1  0x04C       /* as same as VIA_REG_FG */
#define VIA_REG_BGCOLOR_M1      0x050
#define VIA_REG_SRCCOLORKEY_M1  0x050       /* as same as VIA_REG_BG */
#define VIA_REG_MONOPATFGC_M1   0x058       /* Add BG color of Pattern. */
#define VIA_REG_MONOPATBGC_M1   0x05C       /* Add FG color of Pattern. */
#define VIA_REG_COLORPAT_M1     0x100       /* from 0x100 to 0x1ff */

/* VIA_REG_PITCH(0x38): Pitch Setting */
#define VIA_PITCH_ENABLE        0x80000000

/* defines for VIA HW cursor registers */
#define VIA_REG_CURSOR_MODE     0x2D0
#define VIA_REG_CURSOR_POS      0x2D4
#define VIA_REG_CURSOR_ORG      0x2D8
#define VIA_REG_CURSOR_BG       0x2DC
#define VIA_REG_CURSOR_FG       0x2E0

/* VIA_REG_GEMODE(0x04): GE mode */
#define VIA_GEM_8bpp            0x00000000
#define VIA_GEM_16bpp           0x00000100
#define VIA_GEM_32bpp           0x00000300

/* VIA_REG_GECMD(0x00): 2D Engine Command  */
#define VIA_GEC_NOOP            0x00000000
#define VIA_GEC_BLT             0x00000001
#define VIA_GEC_LINE            0x00000005

/* Rotate Command */
#define VIA_GEC_ROT             0x00000008

#define VIA_GEC_SRC_XY          0x00000000
#define VIA_GEC_SRC_LINEAR      0x00000010
#define VIA_GEC_DST_XY          0x00000000
#define VIA_GEC_DST_LINRAT      0x00000020

#define VIA_GEC_SRC_FB          0x00000000
#define VIA_GEC_SRC_SYS         0x00000040
#define VIA_GEC_DST_FB          0x00000000
#define VIA_GEC_DST_SYS         0x00000080

/* source is mono */
#define VIA_GEC_SRC_MONO        0x00000100
/* pattern is mono */
#define VIA_GEC_PAT_MONO        0x00000200
/* mono src is opaque */
#define VIA_GEC_MSRC_OPAQUE     0x00000000
/* mono src is transparent */
#define VIA_GEC_MSRC_TRANS      0x00000400
/* pattern is in frame buffer */
#define VIA_GEC_PAT_FB          0x00000000
/* pattern is from reg setting */
#define VIA_GEC_PAT_REG         0x00000800

#define VIA_GEC_CLIP_DISABLE    0x00000000
#define VIA_GEC_CLIP_ENABLE     0x00001000

#define VIA_GEC_FIXCOLOR_PAT    0x00002000

#define VIA_GEC_INCX            0x00000000
#define VIA_GEC_DECY            0x00004000
#define VIA_GEC_INCY            0x00000000
#define VIA_GEC_DECX            0x00008000
/* mono pattern is opaque */
#define VIA_GEC_MPAT_OPAQUE     0x00000000
/* mono pattern is transparent */
#define VIA_GEC_MPAT_TRANS      0x00010000

#define VIA_GEC_MONO_UNPACK     0x00000000
#define VIA_GEC_MONO_PACK       0x00020000
#define VIA_GEC_MONO_DWORD      0x00000000
#define VIA_GEC_MONO_WORD       0x00040000
#define VIA_GEC_MONO_BYTE       0x00080000

#define VIA_GEC_LASTPIXEL_ON    0x00000000
#define VIA_GEC_LASTPIXEL_OFF   0x00100000
#define VIA_GEC_X_MAJOR         0x00000000
#define VIA_GEC_Y_MAJOR         0x00200000
#define VIA_GEC_QUICK_START     0x00800000

/* defines for VIA 3D registers */
#define VIA_REG_STATUS          0x400
#define VIA_REG_CR_TRANSET      0x41C
#define VIA_REG_CR_TRANSPACE	0x420
#define VIA_REG_TRANSET         0x43C
#define VIA_REG_TRANSPACE       0x440

/* VIA_REG_STATUS(0x400): Engine Status */

/* Command Regulator is busy */
#define VIA_CMD_RGTR_BUSY       0x00000080
/* 2D Engine is busy */
#define VIA_2D_ENG_BUSY         0x00000002
/* 3D Engine is busy */
#define VIA_3D_ENG_BUSY         0x00000001
/* Virtual Queue is busy */
#define VIA_VR_QUEUE_BUSY       0x00020000

/* VIA_REG_STATUS(0x400): Engine Status for H5 */
#define VIA_CMD_RGTR_BUSY_H5   0x00000010  /* Command Regulator is busy */
#define VIA_2D_ENG_BUSY_H5     0x00000002  /* 2D Engine is busy */
#define VIA_3D_ENG_BUSY_H5     0x00001FE1  /* 3D Engine is busy */
#define VIA_VR_QUEUE_BUSY_H5   0x00000004  /* Virtual Queue is busy */

/* VIA_REG_STATUS(0x400): Engine Status for VT3353/3409 */
#define VIA_CMD_RGTR_BUSY_M1   0x00000010  /* Command Regulator is busy */
#define VIA_2D_ENG_BUSY_M1     0x00000002  /* 2D Engine is busy */
#define VIA_3D_ENG_BUSY_M1     0x00001FE1  /* 3D Engine is busy */
#define VIA_VR_QUEUE_BUSY_M1   0x00000004  /* Virtual Queue is busy */

#define MAXLOOP                 0xFFFFFF

#define VIA_BITBLT_COLOR	1
#define VIA_BITBLT_MONO		2
#define VIA_BITBLT_FILL		3

int viafb_setup_engine(struct fb_info *info);
void viafb_reset_engine(struct viafb_par *viapar);
void viafb_show_hw_cursor(struct fb_info *info, int Status);
void viafb_wait_engine_idle(struct fb_info *info);

#endif /* __ACCEL_H__ */
