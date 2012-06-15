/*
   edid.S: EDID data template

   Copyright (C) 2012 Carsten Emde <C.Emde@osadl.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
*/


/* Manufacturer */
#define MFG_LNX1 'L'
#define MFG_LNX2 'N'
#define MFG_LNX3 'X'
#define SERIAL 0
#define YEAR 2012
#define WEEK 5

/* EDID 1.3 standard definitions */
#define XY_RATIO_16_10	0b00
#define XY_RATIO_4_3	0b01
#define XY_RATIO_5_4	0b10
#define XY_RATIO_16_9	0b11

#define mfgname2id(v1,v2,v3) \
	((((v1-'@')&0x1f)<<10)+(((v2-'@')&0x1f)<<5)+((v3-'@')&0x1f))
#define swap16(v1) ((v1>>8)+((v1&0xff)<<8))
#define msbs2(v1,v2) ((((v1>>8)&0x0f)<<4)+((v2>>8)&0x0f))
#define msbs4(v1,v2,v3,v4) \
	(((v1&0x03)>>2)+((v2&0x03)>>4)+((v3&0x03)>>6)+((v4&0x03)>>8))
#define pixdpi2mm(pix,dpi) ((pix*25)/dpi)
#define xsize pixdpi2mm(XPIX,DPI)
#define ysize pixdpi2mm(YPIX,DPI)

		.data

/* Fixed header pattern */
header:		.byte	0x00,0xff,0xff,0xff,0xff,0xff,0xff,0x00

mfg_id:		.word	swap16(mfgname2id(MFG_LNX1, MFG_LNX2, MFG_LNX3))

prod_code:	.word	0

/* Serial number. 32 bits, little endian. */
serial_number:	.long	SERIAL

/* Week of manufacture */
week:		.byte	WEEK

/* Year of manufacture, less 1990. (1990-2245)
   If week=255, it is the model year instead */
year:		.byte	YEAR-1990

version:	.byte	VERSION 	/* EDID version, usually 1 (for 1.3) */
revision:	.byte	REVISION	/* EDID revision, usually 3 (for 1.3) */

/* If Bit 7=1	Digital input. If set, the following bit definitions apply:
     Bits 6-1	Reserved, must be 0
     Bit 0	Signal is compatible with VESA DFP 1.x TMDS CRGB,
		  1 pixel per clock, up to 8 bits per color, MSB aligned,
   If Bit 7=0	Analog input. If clear, the following bit definitions apply:
     Bits 6-5	Video white and sync levels, relative to blank
		  00=+0.7/-0.3 V; 01=+0.714/-0.286 V;
		  10=+1.0/-0.4 V; 11=+0.7/0 V
   Bit 4	Blank-to-black setup (pedestal) expected
   Bit 3	Separate sync supported
   Bit 2	Composite sync (on HSync) supported
   Bit 1	Sync on green supported
   Bit 0	VSync pulse must be serrated when somposite or
		  sync-on-green is used. */
video_parms:	.byte	0x6d

/* Maximum horizontal image size, in centimetres
   (max 292 cm/115 in at 16:9 aspect ratio) */
max_hor_size:	.byte	xsize/10

/* Maximum vertical image size, in centimetres.
   If either byte is 0, undefined (e.g. projector) */
max_vert_size:	.byte	ysize/10

/* Display gamma, minus 1, times 100 (range 1.00-3.5 */
gamma:		.byte	120

/* Bit 7	DPMS standby supported
   Bit 6	DPMS suspend supported
   Bit 5	DPMS active-off supported
   Bits 4-3	Display type: 00=monochrome; 01=RGB colour;
		  10=non-RGB multicolour; 11=undefined
   Bit 2	Standard sRGB colour space. Bytes 25-34 must contain
		  sRGB standard values.
   Bit 1	Preferred timing mode specified in descriptor block 1.
   Bit 0	GTF supported with default parameter values. */
dsp_features:	.byte	0xea

/* Chromaticity coordinates. */
/* Red and green least-significant bits
   Bits 7-6	Red x value least-significant 2 bits
   Bits 5-4	Red y value least-significant 2 bits
   Bits 3-2	Green x value lst-significant 2 bits
   Bits 1-0	Green y value least-significant 2 bits */
red_green_lsb:	.byte	0x5e

/* Blue and white least-significant 2 bits */
blue_white_lsb:	.byte	0xc0

/* Red x value most significant 8 bits.
   0-255 encodes 0-0.996 (255/256); 0-0.999 (1023/1024) with lsbits */
red_x_msb:	.byte	0xa4

/* Red y value most significant 8 bits */
red_y_msb:	.byte	0x59

/* Green x and y value most significant 8 bits */
green_x_y_msb:	.byte	0x4a,0x98

/* Blue x and y value most significant 8 bits */
blue_x_y_msb:	.byte	0x25,0x20

/* Default white point x and y value most significant 8 bits */
white_x_y_msb:	.byte	0x50,0x54

/* Established timings */
/* Bit 7	720x400 @ 70 Hz
   Bit 6	720x400 @ 88 Hz
   Bit 5	640x480 @ 60 Hz
   Bit 4	640x480 @ 67 Hz
   Bit 3	640x480 @ 72 Hz
   Bit 2	640x480 @ 75 Hz
   Bit 1	800x600 @ 56 Hz
   Bit 0	800x600 @ 60 Hz */
estbl_timing1:	.byte	0x00

/* Bit 7	800x600 @ 72 Hz
   Bit 6	800x600 @ 75 Hz
   Bit 5	832x624 @ 75 Hz
   Bit 4	1024x768 @ 87 Hz, interlaced (1024x768)
   Bit 3	1024x768 @ 60 Hz
   Bit 2	1024x768 @ 72 Hz
   Bit 1	1024x768 @ 75 Hz
   Bit 0	1280x1024 @ 75 Hz */
estbl_timing2:	.byte	ESTABLISHED_TIMINGS_BITS

/* Bit 7	1152x870 @ 75 Hz (Apple Macintosh II)
   Bits 6-0 	Other manufacturer-specific display mod */
estbl_timing3:	.byte	0x00

/* Standard timing */
/* X resolution, less 31, divided by 8 (256-2288 pixels) */
std_xres:	.byte	(XPIX/8)-31
/* Y resolution, X:Y pixel ratio
   Bits 7-6	X:Y pixel ratio: 00=16:10; 01=4:3; 10=5:4; 11=16:9.
   Bits 5-0	Vertical frequency, less 60 (60-123 Hz) */
std_vres:	.byte	(XY_RATIO<<6)+VFREQ-60
		.fill	7,2,0x0101	/* Unused */

descriptor1:
/* Pixel clock in 10 kHz units. (0.-655.35 MHz, little-endian) */
clock:		.word	CLOCK/10

/* Horizontal active pixels 8 lsbits (0-4095) */
x_act_lsb:	.byte	XPIX&0xff
/* Horizontal blanking pixels 8 lsbits (0-4095)
   End of active to start of next active. */
x_blk_lsb:	.byte	XBLANK&0xff
/* Bits 7-4 	Horizontal active pixels 4 msbits
   Bits 3-0	Horizontal blanking pixels 4 msbits */
x_msbs:		.byte	msbs2(XPIX,XBLANK)

/* Vertical active lines 8 lsbits (0-4095) */
y_act_lsb:	.byte	YPIX&0xff
/* Vertical blanking lines 8 lsbits (0-4095) */
y_blk_lsb:	.byte	YBLANK&0xff
/* Bits 7-4 	Vertical active lines 4 msbits
   Bits 3-0 	Vertical blanking lines 4 msbits */
y_msbs:		.byte	msbs2(YPIX,YBLANK)

/* Horizontal sync offset pixels 8 lsbits (0-1023) From blanking start */
x_snc_off_lsb:	.byte	XOFFSET&0xff
/* Horizontal sync pulse width pixels 8 lsbits (0-1023) */
x_snc_pls_lsb:	.byte	XPULSE&0xff
/* Bits 7-4 	Vertical sync offset lines 4 lsbits -63)
   Bits 3-0 	Vertical sync pulse width lines 4 lsbits -63) */
y_snc_lsb:	.byte	((YOFFSET-63)<<4)+(YPULSE-63)
/* Bits 7-6 	Horizontal sync offset pixels 2 msbits
   Bits 5-4 	Horizontal sync pulse width pixels 2 msbits
   Bits 3-2 	Vertical sync offset lines 2 msbits
   Bits 1-0 	Vertical sync pulse width lines 2 msbits */
xy_snc_msbs:	.byte	msbs4(XOFFSET,XPULSE,YOFFSET,YPULSE)

/* Horizontal display size, mm, 8 lsbits (0-4095 mm, 161 in) */
x_dsp_size:	.byte	xsize&0xff

/* Vertical display size, mm, 8 lsbits (0-4095 mm, 161 in) */
y_dsp_size:	.byte	ysize&0xff

/* Bits 7-4 	Horizontal display size, mm, 4 msbits
   Bits 3-0 	Vertical display size, mm, 4 msbits */
dsp_size_mbsb:	.byte	msbs2(xsize,ysize)

/* Horizontal border pixels (each side; total is twice this) */
x_border:	.byte	0
/* Vertical border lines (each side; total is twice this) */
y_border:	.byte	0

/* Bit 7 	Interlaced
   Bits 6-5 	Stereo mode: 00=No stereo; other values depend on bit 0:
   Bit 0=0: 01=Field sequential, sync=1 during right; 10=similar,
     sync=1 during left; 11=4-way interleaved stereo
   Bit 0=1 2-way interleaved stereo: 01=Right image on even lines;
     10=Left image on even lines; 11=side-by-side
   Bits 4-3 	Sync type: 00=Analog composite; 01=Bipolar analog composite;
     10=Digital composite (on HSync); 11=Digital separate
   Bit 2 	If digital separate: Vertical sync polarity (1=positive)
   Other types: VSync serrated (HSync during VSync)
   Bit 1 	If analog sync: Sync on all 3 RGB lines (else green only)
   Digital: HSync polarity (1=positive)
   Bit 0 	2-way line-interleaved stereo, if bits 4-3 are not 00. */
features:	.byte	0x18+(VSYNC_POL<<2)+(HSYNC_POL<<1)

descriptor2:	.byte	0,0	/* Not a detailed timing descriptor */
		.byte	0	/* Must be zero */
		.byte	0xff	/* Descriptor is monitor serial number (text) */
		.byte	0	/* Must be zero */
start1:		.ascii	"Linux #0"
end1:		.byte	0x0a	/* End marker */
		.fill	12-(end1-start1), 1, 0x20 /* Padded spaces */
descriptor3:	.byte	0,0	/* Not a detailed timing descriptor */
		.byte	0	/* Must be zero */
		.byte	0xfd	/* Descriptor is monitor range limits */
		.byte	0	/* Must be zero */
start2:		.byte	VFREQ-1	/* Minimum vertical field rate (1-255 Hz) */
		.byte	VFREQ+1	/* Maximum vertical field rate (1-255 Hz) */
		.byte	(CLOCK/(XPIX+XBLANK))-1 /* Minimum horizontal line rate
						    (1-255 kHz) */
		.byte	(CLOCK/(XPIX+XBLANK))+1 /* Maximum horizontal line rate
						    (1-255 kHz) */
		.byte	(CLOCK/10000)+1	/* Maximum pixel clock rate, rounded up
					   to 10 MHz multiple (10-2550 MHz) */
		.byte	0	/* No extended timing information type */
end2:		.byte	0x0a	/* End marker */
		.fill	12-(end2-start2), 1, 0x20 /* Padded spaces */
descriptor4:	.byte	0,0	/* Not a detailed timing descriptor */
		.byte	0	/* Must be zero */
		.byte	0xfc	/* Descriptor is text */
		.byte	0	/* Must be zero */
start3:		.ascii	TIMING_NAME
end3:		.byte	0x0a	/* End marker */
		.fill	12-(end3-start3), 1, 0x20 /* Padded spaces */
extensions:	.byte	0	/* Number of extensions to follow */
checksum:	.byte	CRC	/* Sum of all bytes must be 0 */
