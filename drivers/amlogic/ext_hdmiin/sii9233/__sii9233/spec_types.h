#ifndef SPEC_TYPES
#define SPEC_TYPES
/*
HDMI Info Frame packet ids
*/
/* InfoFrames Avi, Aud, Spd, Vsif
	[InfoFrame type		]
	[InfoFrame ver		]
	[InfoFrame length	]
	[Check sum			]
*/
#define IF_TYPE_ADDR		0x00
#define IF_VER_ADDR			0x01
#define IF_LENGTH_ADDR		0x02
#define IF_CHECK_SUM_ADDR	0x03
#define IF_PAYLOAD_ADDR		0x04
#define IF_HEADER_SIZE		3

#define IF_AVI_ID           0x82
#define IF_AUD_ID           0x84
#define IF_ACP_ID           0x04
#define IF_SPD_ID           0x83
#define IF_MPEG_ID          0x85
#define IF_GMT_ID           0x0A
/* Gamut format
	[GDB_Length_H	]
	[GDB_Length_L	]
	[Check sum		]
	[up to 24 uint8_ts	]
*/

#define IF_VSIF_ID          0x81
#define IF_ISRC1_ID         0x05
#define IF_ISRC2_ID         0x06

#define IF_AVI_SIZE          13
#define IF_AUD_SIZE          13
#define IF_ACP_SIZE          6
#define IF_SPD_SIZE          28
#define IF_MPEG_SIZE         28
#define IF_GMT_SIZE          28
#define IF_VSIF_SIZE         24
#define IF_ISRC_SIZE         19

#define IF_VSIF_HDMI_FORMAT_SEL		0xE0
#define IF_HDMI_FORMAT_3D			0x40
#define IF_VSIF_3DSTRUCT_SEL		0X04  //YMA ADD

#define IF_VSIF_IEEE_REGID_ADDR		0x00
#define IF_VSIF_HDMI_FORMAT_ADDR	0x03
#define IF_VSIF_3D_STRUCT_ADDR		0x04


typedef enum {
	  FramePacking				=	0
	, FrameAlternative			=	1
	, LineAlternative				=	2
	, SiedBySideFull				=	3
	, LDepth						=	4
	, LDepthGraphGraphDepth	=	5	
	, SideBySideHalf				=	8
}_3DStructute_t;


#define IF_AVI_EC_ADDR		0x02
#define IF_AVI_ITC_ADDR		0x02
#define IF_AVI_VIC_ADDR		0x03
#define IF_AVI_CN_ADDR		0x04
#define IF_AVI_VIC_SEL		0x7f


typedef enum {
	ExtnColorimSel	= 0x70	
	, xvYCC601		= 0x00
	, xvYCC709		= 0x10
	, cYCC601
	, AdobeYCC601
	, AdobeRGB
} ExtendColorimetry_t;

typedef enum {
	ITContSel		= 0x80	
	, ContTypeSel	= 0x30	
	, Graphics	= 0x00
	, Photo		= 0x10
	, Cinema
	, Game

} ContType_t;

/* 3D Mandatory Video Id Modes */
typedef enum _VidIdModes_t {
	_3D_Vic1080p24	= 32,
	_3D_Vic720p60	=  4,
	_3D_Vic720p50	= 19
} VidIdModes_t;


#endif
