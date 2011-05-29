#ifndef _LINUX_XGIFB
#define _LINUX_XGIFB
#include <asm/ioctl.h>
#include <asm/types.h>

#define DISPTYPE_CRT1       0x00000008L
#define DISPTYPE_CRT2       0x00000004L
#define DISPTYPE_LCD        0x00000002L
#define DISPTYPE_TV         0x00000001L
#define DISPTYPE_DISP1      DISPTYPE_CRT1
#define DISPTYPE_DISP2      (DISPTYPE_CRT2 | DISPTYPE_LCD | DISPTYPE_TV)
#define DISPMODE_SINGLE	    0x00000020L
#define DISPMODE_MIRROR	    0x00000010L
#define DISPMODE_DUALVIEW   0x00000040L

#define HASVB_NONE      	0x00
#define HASVB_301       	0x01
#define HASVB_LVDS      	0x02
#define HASVB_TRUMPION  	0x04
#define HASVB_LVDS_CHRONTEL	0x10
#define HASVB_302       	0x20
#define HASVB_303       	0x40
#define HASVB_CHRONTEL  	0x80

#ifndef XGIFB_ID
#define XGIFB_ID          0x53495346    /* Identify myself with 'XGIF' */
#endif

enum XGI_CHIP_TYPE {
    XG40 = 32,
    XG41,
    XG42,
    XG45,
    XG20 = 48,
    XG21,
    XG27,
};

enum xgi_tvtype {
	TVMODE_NTSC = 0,
	TVMODE_PAL,
	TVMODE_HIVISION,
	TVTYPE_PALM,	// vicki@030226
    	TVTYPE_PALN,	// vicki@030226
    	TVTYPE_NTSCJ,	// vicki@030226
	TVMODE_TOTAL
};

enum xgi_tv_plug {	/* vicki@030226 */
//	TVPLUG_Legacy = 0,
//	TVPLUG_COMPOSITE,
//	TVPLUG_SVIDEO,
//	TVPLUG_SCART,
//	TVPLUG_TOTAL
    	TVPLUG_UNKNOWN = 0,
    	TVPLUG_COMPOSITE = 1,
    	TVPLUG_SVIDEO = 2,
    	TVPLUG_COMPOSITE_AND_SVIDEO = 3,
    	TVPLUG_SCART = 4,
    	TVPLUG_YPBPR_525i = 5,
    	TVPLUG_YPBPR_525P = 6,
    	TVPLUG_YPBPR_750P = 7,
    	TVPLUG_YPBPR_1080i = 8,
	TVPLUG_TOTAL
};

struct video_info{
        int           chip_id;
        unsigned int  video_size;
        unsigned long video_base;
        char  *       video_vbase;
        unsigned long mmio_base;
	unsigned long mmio_size;
        char  *       mmio_vbase;
        unsigned long vga_base;
        unsigned long mtrr;

        int    video_bpp;
        int    video_cmap_len;
        int    video_width;
        int    video_height;
        int    video_vwidth;
        int    video_vheight;
        int    org_x;
        int    org_y;
        int    video_linelength;
        unsigned int refresh_rate;

        unsigned long disp_state;
        unsigned char hasVB;
        unsigned char TV_type;
        unsigned char TV_plug;

	enum XGI_CHIP_TYPE chip;
        unsigned char revision_id;

        unsigned short DstColor;
        unsigned long  XGI310_AccelDepth;
        unsigned long  CommandReg;

        unsigned int   pcibus;
        unsigned int   pcislot;
        unsigned int   pcifunc;

        unsigned short subsysvendor;
        unsigned short subsysdevice;

        char reserved[236];
};


extern struct video_info xgi_video_info;

#endif
