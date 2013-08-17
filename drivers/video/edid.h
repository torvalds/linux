/* 
 * drivers/video/edid.h - EDID/DDC Header
 *
 * Based on:
 *   1. XFree86 4.3.0, edid.h
 *      Copyright 1998 by Egbert Eich <Egbert.Eich@Physik.TU-Darmstadt.DE>
 * 
 *   2. John Fremlin <vii@users.sourceforge.net> and 
 *      Ani Joshi <ajoshi@unixbox.com>
 *
 * DDC is a Trademark of VESA (Video Electronics Standard Association).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
*/

#ifndef __EDID_H__
#define __EDID_H__

#define EDID_LENGTH				0x80
#define EDID_HEADER				0x00
#define EDID_HEADER_END				0x07

#define ID_MANUFACTURER_NAME			0x08
#define ID_MANUFACTURER_NAME_END		0x09
#define ID_MODEL				0x0a

#define ID_SERIAL_NUMBER			0x0c

#define MANUFACTURE_WEEK			0x10
#define MANUFACTURE_YEAR			0x11

#define EDID_STRUCT_VERSION			0x12
#define EDID_STRUCT_REVISION			0x13

#define EDID_STRUCT_DISPLAY                     0x14

#define DPMS_FLAGS				0x18
#define ESTABLISHED_TIMING_1			0x23
#define ESTABLISHED_TIMING_2			0x24
#define MANUFACTURERS_TIMINGS			0x25

/* standard timings supported */
#define STD_TIMING                              8
#define STD_TIMING_DESCRIPTION_SIZE             2
#define STD_TIMING_DESCRIPTIONS_START           0x26

#define DETAILED_TIMING_DESCRIPTIONS_START	0x36
#define DETAILED_TIMING_DESCRIPTION_SIZE	18
#define NO_DETAILED_TIMING_DESCRIPTIONS		4

#define DETAILED_TIMING_DESCRIPTION_1		0x36
#define DETAILED_TIMING_DESCRIPTION_2		0x48
#define DETAILED_TIMING_DESCRIPTION_3		0x5a
#define DETAILED_TIMING_DESCRIPTION_4		0x6c

#define DESCRIPTOR_DATA				5

/* vendor specific data block */
#define VSDB_LATENCY_FIELD		7
#define VSDB_LATENCY_FIELD_MASK		(1 << 7)
#define VSDB_I_LATENCY_FIELD		6
#define VSDB_I_LATENCY_FIELD_MASK	(1 << 6)
#define VSDB_VIDEO_PRESENT		5
#define VSDB_VIDEO_PRESENT_MASK		(1 << 5)
#define VSDB_VIC_LEN			5
#define VSDB_VIC_LEN_MASK		(0x7 << 5)
#define VSDB_VIC_ORDER			4
#define VSDB_VIC_ORDER_MASK		(0xf << 4)
#define VSDB_3D_MASK			2
#define VSDB_3D_LEN_MASK		0x1f
#define VSDB_3D_STRUCTURE_MASK		0xf
#define VSDB_3D_PRESENT			7
#define VSDB_3D_PRESENT_MASK		(1 << 7)
#define VSDB_3D_MT_PRESENT		5
#define VSDB_3D_MT_PRESENT_MASK		(0x3 << 5)

#define UPPER_NIBBLE( x ) \
        (((128|64|32|16) & (x)) >> 4)

#define LOWER_NIBBLE( x ) \
        ((1|2|4|8) & (x))

#define COMBINE_HI_8LO( hi, lo ) \
        ( (((unsigned)hi) << 8) | (unsigned)lo )

#define COMBINE_HI_4LO( hi, lo ) \
        ( (((unsigned)hi) << 4) | (unsigned)lo )

#define PIXEL_CLOCK_LO     (unsigned)block[ 0 ]
#define PIXEL_CLOCK_HI     (unsigned)block[ 1 ]
#define PIXEL_CLOCK	   (COMBINE_HI_8LO( PIXEL_CLOCK_HI,PIXEL_CLOCK_LO )*10000)
#define H_ACTIVE_LO        (unsigned)block[ 2 ]
#define H_BLANKING_LO      (unsigned)block[ 3 ]
#define H_ACTIVE_HI        UPPER_NIBBLE( (unsigned)block[ 4 ] )
#define H_ACTIVE           COMBINE_HI_8LO( H_ACTIVE_HI, H_ACTIVE_LO )
#define H_BLANKING_HI      LOWER_NIBBLE( (unsigned)block[ 4 ] )
#define H_BLANKING         COMBINE_HI_8LO( H_BLANKING_HI, H_BLANKING_LO )

#define V_ACTIVE_LO        (unsigned)block[ 5 ]
#define V_BLANKING_LO      (unsigned)block[ 6 ]
#define V_ACTIVE_HI        UPPER_NIBBLE( (unsigned)block[ 7 ] )
#define V_ACTIVE           COMBINE_HI_8LO( V_ACTIVE_HI, V_ACTIVE_LO )
#define V_BLANKING_HI      LOWER_NIBBLE( (unsigned)block[ 7 ] )
#define V_BLANKING         COMBINE_HI_8LO( V_BLANKING_HI, V_BLANKING_LO )

#define H_SYNC_OFFSET_LO   (unsigned)block[ 8 ]
#define H_SYNC_WIDTH_LO    (unsigned)block[ 9 ]

#define V_SYNC_OFFSET_LO   UPPER_NIBBLE( (unsigned)block[ 10 ] )
#define V_SYNC_WIDTH_LO    LOWER_NIBBLE( (unsigned)block[ 10 ] )

#define V_SYNC_WIDTH_HI    ((unsigned)block[ 11 ] & (1|2))
#define V_SYNC_OFFSET_HI   (((unsigned)block[ 11 ] & (4|8)) >> 2)

#define H_SYNC_WIDTH_HI    (((unsigned)block[ 11 ] & (16|32)) >> 4)
#define H_SYNC_OFFSET_HI   (((unsigned)block[ 11 ] & (64|128)) >> 6)

#define V_SYNC_WIDTH       COMBINE_HI_4LO( V_SYNC_WIDTH_HI, V_SYNC_WIDTH_LO )
#define V_SYNC_OFFSET      COMBINE_HI_4LO( V_SYNC_OFFSET_HI, V_SYNC_OFFSET_LO )

#define H_SYNC_WIDTH       COMBINE_HI_8LO( H_SYNC_WIDTH_HI, H_SYNC_WIDTH_LO )
#define H_SYNC_OFFSET      COMBINE_HI_8LO( H_SYNC_OFFSET_HI, H_SYNC_OFFSET_LO )

#define H_SIZE_LO          (unsigned)block[ 12 ]
#define V_SIZE_LO          (unsigned)block[ 13 ]

#define H_SIZE_HI          UPPER_NIBBLE( (unsigned)block[ 14 ] )
#define V_SIZE_HI          LOWER_NIBBLE( (unsigned)block[ 14 ] )

#define H_SIZE             COMBINE_HI_8LO( H_SIZE_HI, H_SIZE_LO )
#define V_SIZE             COMBINE_HI_8LO( V_SIZE_HI, V_SIZE_LO )

#define H_BORDER           (unsigned)block[ 15 ]
#define V_BORDER           (unsigned)block[ 16 ]

#define FLAGS              (unsigned)block[ 17 ]

#define INTERLACED         (FLAGS&128)
#define SYNC_TYPE          (FLAGS&3<<3)	/* bits 4,3 */
#define SYNC_SEPARATE      (3<<3)
#define HSYNC_POSITIVE     (FLAGS & 4)
#define VSYNC_POSITIVE     (FLAGS & 2)

#define V_MIN_RATE              block[ 5 ]
#define V_MAX_RATE              block[ 6 ]
#define H_MIN_RATE              block[ 7 ]
#define H_MAX_RATE              block[ 8 ]
#define MAX_PIXEL_CLOCK         (((int)block[ 9 ]) * 10)
#define GTF_SUPPORT		block[10]

#define DPMS_ACTIVE_OFF		(1 << 5)
#define DPMS_SUSPEND		(1 << 6)
#define DPMS_STANDBY		(1 << 7)

#endif /* __EDID_H__ */
