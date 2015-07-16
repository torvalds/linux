/*
 * Amlogic Apollo
 * tv display control driver
 *
 * Copyright (C) 2009 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:   jianfeng_wang@amlogic
 *		   
 *		   
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/amlogic/vout/vinfo.h>
#include <mach/am_regs.h>

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#include <mach/vpu.h>
#endif
#include <asm/uaccess.h>
#include <linux/major.h>
#include "tvconf.h"
#include "tvmode.h"
#include "vout_log.h"
#include <linux/amlogic/amlog.h>
#include <mach/power_gate.h>

#define PIN_MUX_REG_0	  0x202c
#define P_PIN_MUX_REG_0  CBUS_REG_ADDR(PIN_MUX_REG_0)
static    disp_module_info_t    *info;
static void  parse_vdac_setting(char *para);

SET_TV_CLASS_ATTR(vdac_setting,parse_vdac_setting)

#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION

#define DEFAULT_POLICY_FR_AUTO	1

static int fr_auto_policy = DEFAULT_POLICY_FR_AUTO;
static void policy_framerate_automation_store(char* para);

SET_TV_CLASS_ATTR(policy_fr_auto, policy_framerate_automation_store)

#endif



/*****************************
*	default settings :
*	Y    -----  DAC1
*	PB  -----  DAC2
*	PR  -----  DAC0
*
*	CVBS  	---- DAC1
*	S-LUMA    ---- DAC2
*	S-CHRO	----  DAC0
******************************/

static const tvmode_t vmode_tvmode_tab[] =
{
	TVMODE_480I, TVMODE_480I_RPT, TVMODE_480CVBS, TVMODE_480P, 
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	TVMODE_480P_59HZ,
#endif
	TVMODE_480P_RPT, TVMODE_576I, TVMODE_576I_RPT, TVMODE_576CVBS, TVMODE_576P, TVMODE_576P_RPT, TVMODE_720P, 
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	TVMODE_720P_59HZ , // for 720p 59.94hz
#endif
	TVMODE_800P, TVMODE_800X480P_60HZ, TVMODE_1366X768P_60HZ,
	TVMODE_1600X900P_60HZ, TVMODE_800X600P_60HZ, TVMODE_1024X600P_60HZ,
	TVMODE_1024X768P_60HZ, TVMODE_1360X768P_60HZ, TVMODE_1440X900P_60HZ,
	TVMODE_1680X1050P_60HZ,
	TVMODE_1080I, 
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	TVMODE_1080I_59HZ,
#endif	
	TVMODE_1080P,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	TVMODE_1080P_59HZ , // for 1080p 59.94hz
#endif
    TVMODE_720P_50HZ, TVMODE_1080I_50HZ, TVMODE_1080P_50HZ,TVMODE_1080P_24HZ, 
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	TVMODE_1080P_23HZ , // for 1080p 23.97hz
#endif
	TVMODE_4K2K_30HZ, 
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
		TVMODE_4K2K_29HZ , // for 4k2k 29.97hz
#endif
	TVMODE_4K2K_25HZ, TVMODE_4K2K_24HZ, 
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
		TVMODE_4K2K_23HZ , // for 4k2k 23.97hz
#endif
	TVMODE_4K2K_SMPTE, 
    VMODE_1920x1200,
    TVMODE_VGA, TVMODE_SVGA, TVMODE_XGA, TVMODE_SXGA, TVMODE_WSXGA, TVMODE_FHDVGA,
};


static const vinfo_t tv_info[] = 
{
    { /* VMODE_480I */
		.name              = "480i",
		.mode              = VMODE_480I,
        .width             = 720,
        .height            = 480,
        .field_height      = 240,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_480I_RPT */
        .name              = "480i_rpt",
        .mode              = VMODE_480I_RPT,
        .width             = 720,
        .height            = 480,
        .field_height      = 240,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
     { /* VMODE_480CVBS*/
		.name              = "480cvbs",
		.mode              = VMODE_480CVBS,
        .width             = 720,
        .height            = 480,
        .field_height      = 240,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
		.video_clk         = 27000000,
    },
    { /* VMODE_480P */
		.name              = "480p",
		.mode              = VMODE_480P,
        .width             = 720,
        .height            = 480,
        .field_height      = 480,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	{ /* VMODE_480P_59HZ */
		.name              = "480p59hz",
		.mode              = VMODE_480P_59HZ,
        .width             = 720,
        .height            = 480,
        .field_height      = 480,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60000,
        .sync_duration_den = 1001,
        .video_clk         = 27000000,
    },
#endif
    { /* VMODE_480P_RPT */
        .name              = "480p_rpt",
        .mode              = VMODE_480P_RPT,
        .width             = 720,
        .height            = 480,
        .field_height      = 480,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_576I */
		.name              = "576i",
		.mode              = VMODE_576I,
        .width             = 720,
        .height            = 576,
        .field_height      = 288,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_576I_RPT */
        .name              = "576i_rpt",
        .mode              = VMODE_576I_RPT,
        .width             = 720,
        .height            = 576,
        .field_height      = 288,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_576I */
		.name              = "576cvbs",
		.mode              = VMODE_576CVBS,
        .width             = 720,
        .height            = 576,
        .field_height      = 288,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
		.video_clk         = 27000000,
    },
    { /* VMODE_576P */
		.name              = "576p",
		.mode              = VMODE_576P,
        .width             = 720,
        .height            = 576,
        .field_height      = 576,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_576P_RPT */
        .name              = "576p_rpt",
        .mode              = VMODE_576P_RPT,
        .width             = 720,
        .height            = 576,
        .field_height      = 576,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 27000000,
    },
    { /* VMODE_720P */
		.name              = "720p",
		.mode              = VMODE_720P,
        .width             = 1280,
        .height            = 720,
        .field_height      = 720,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 74250000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	{ /* VMODE_720P_59HZ */
		.name              = "720p59hz",
		.mode              = VMODE_720P_59HZ,
        .width             = 1280,
        .height            = 720,
        .field_height      = 720,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60000,
        .sync_duration_den = 1001,
        .video_clk         = 74250000,
    },
#endif
    { /* VMODE_800P */
		.name				= "800p",
		.mode 				= VMODE_800P,
		.width				= 1280,
		.height				= 800,
		.field_height		= 800,
		.aspect_ratio_num	= 16,
		.aspect_ratio_den	= 9,
		.sync_duration_num	= 60,
		.sync_duration_den	= 1,
		.video_clk			= 71000000,
	},
    { /* VMODE_800X480P_60HZ */
		.name				= "800x480p60hz",
		.mode 				= VMODE_800X480P_60HZ,
		.width				= 800,
		.height				= 480,
		.field_height		= 480,
		.aspect_ratio_num	= 5,
		.aspect_ratio_den	= 3,
		.sync_duration_num	= 60,
		.sync_duration_den	= 1,
		.video_clk			= 29760000,
	},
    { /* VMODE_1366X768P_60HZ */
		.name				= "1366x768p60hz",
		.mode 				= VMODE_1366X768P_60HZ,
		.width				= 1366,
		.height				= 768,
		.field_height		= 768,
		.aspect_ratio_num	= 16,
		.aspect_ratio_den	= 9,
		.sync_duration_num	= 60,
		.sync_duration_den	= 1,
		.video_clk			= 85800000,
	},
    { /* VMODE_1600X900P_60HZ */
		.name				= "1600x900p60hz",
		.mode 				= VMODE_1600X900P_60HZ,
		.width				= 1600,
		.height				= 900,
		.field_height		= 900,
		.aspect_ratio_num	= 16,
		.aspect_ratio_den	= 9,
		.sync_duration_num	= 60,
		.sync_duration_den	= 1,
		.video_clk			= 108000000,
	},
    { /* VMODE_800X600P_60HZ */
		.name				= "800x600p60hz",
		.mode 				= VMODE_800X600P_60HZ,
		.width				= 800,
		.height				= 600,
		.field_height		= 600,
		.aspect_ratio_num	= 4,
		.aspect_ratio_den	= 3,
		.sync_duration_num	= 60,
		.sync_duration_den	= 1,
		.video_clk			= 40000000,
	},
    { /* VMODE_1024X600P_60HZ */
		.name				= "1024x600p60hz",
		.mode 				= VMODE_1024X600P_60HZ,
		.width				= 1024,
		.height				= 600,
		.field_height		= 600,
		.aspect_ratio_num	= 17,
		.aspect_ratio_den	= 10,
		.sync_duration_num	= 60,
		.sync_duration_den	= 1,
		.video_clk			= 51450000,
	},
    { /* VMODE_1024X768P_60HZ */
		.name				= "1024x768p60hz",
		.mode 				= VMODE_1024X768P_60HZ,
		.width				= 1024,
		.height				= 768,
		.field_height		= 768,
		.aspect_ratio_num	= 4,
		.aspect_ratio_den	= 3,
		.sync_duration_num	= 60,
		.sync_duration_den	= 1,
		.video_clk			= 65000000,
	},
    { /* VMODE_1360X768P_60HZ */
		.name				= "1360x768p60hz",
		.mode 				= VMODE_1360X768P_60HZ,
		.width				= 1360,
		.height				= 768,
		.field_height		= 768,
		.aspect_ratio_num	= 16,
		.aspect_ratio_den	= 9,
		.sync_duration_num	= 60,
		.sync_duration_den	= 1,
		.video_clk			= 85500000,
	},
    { /* VMODE_1440X900P_60HZ */
		.name				= "1440x900p60hz",
		.mode 				= VMODE_1440X900P_60HZ,
		.width				= 1440,
		.height				= 900,
		.field_height		= 900,
		.aspect_ratio_num	= 8,
		.aspect_ratio_den	= 5,
		.sync_duration_num	= 60,
		.sync_duration_den	= 1,
		.video_clk			= 106500000,
	},
    { /* VMODE_1680X1050P_60HZ */
		.name				= "1680x1050p60hz",
		.mode 				= VMODE_1680X1050P_60HZ,
		.width				= 1680,
		.height				= 1050,
		.field_height		= 1050,
		.aspect_ratio_num	= 8,
		.aspect_ratio_den	= 5,
		.sync_duration_num	= 60,
		.sync_duration_den	= 1,
		.video_clk			= 146250000,
	},
    { /* VMODE_1080I */
		.name              = "1080i",
		.mode              = VMODE_1080I,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 540,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 74250000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION    
    { /* VMODE_1080I_59HZ */
		.name              = "1080i59hz",
		.mode              = VMODE_1080I_59HZ,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 540,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60000,
        .sync_duration_den = 1001,
        .video_clk         = 74250000,
    },
#endif
    { /* VMODE_1080P */
		.name              = "1080p",
		.mode              = VMODE_1080P,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 1080,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
        .video_clk         = 148500000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	{ /* VMODE_1080P_59HZ */
		.name			   = "1080p59hz",
		.mode			   = VMODE_1080P_59HZ,
		.width			   = 1920,
		.height 		   = 1080,
		.field_height	   = 1080,
		.aspect_ratio_num  = 16,
		.aspect_ratio_den  = 9,
		.sync_duration_num = 60000,
		.sync_duration_den = 1001,
		.video_clk		   = 148500000,
	},
#endif
    { /* VMODE_720P_50hz */
		.name              = "720p50hz",
		.mode              = VMODE_720P_50HZ,
        .width             = 1280,
        .height            = 720,
        .field_height      = 720,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 74250000,
    },
    { /* VMODE_1080I_50HZ */
		.name              = "1080i50hz",
		.mode              = VMODE_1080I_50HZ,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 540,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 74250000,
    },
    { /* VMODE_1080P_50HZ */
		.name              = "1080p50hz",
		.mode              = VMODE_1080P_50HZ,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 1080,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 50,
        .sync_duration_den = 1,
        .video_clk         = 148500000,
    },
    { /* VMODE_1080P_24HZ */
		.name              = "1080p24hz",
		.mode              = VMODE_1080P_24HZ,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 1080,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 24,
        .sync_duration_den = 1,
        .video_clk         = 74250000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	{ /* VMODE_1080P_23HZ */
		.name			   = "1080p23hz",
		.mode			   = VMODE_1080P_23HZ,
		.width			   = 1920,
		.height 		   = 1080,
		.field_height	   = 1080,
		.aspect_ratio_num  = 16,
		.aspect_ratio_den  = 9,
		.sync_duration_num = 2397,
		.sync_duration_den = 100,
		.video_clk		   = 74250000,
	},
#endif
    { /* VMODE_4K2K_30HZ */
        .name              = "4k2k30hz",
        .mode              = TVMODE_4K2K_30HZ,
        .width             = 3840,
        .height            = 2160,
        .field_height      = 2160,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 30,
        .sync_duration_den = 1,
        .video_clk         = 297000000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	{ /* VMODE_4K2K_29HZ */
		.name			   = "4k2k29hz",
		.mode			   = TVMODE_4K2K_29HZ,
		.width			   = 3840,
		.height 		   = 2160,
		.field_height	   = 2160,
		.aspect_ratio_num  = 16,
		.aspect_ratio_den  = 9,
		.sync_duration_num = 2997,
		.sync_duration_den = 100,
		.video_clk		   = 297000000,
	},
#endif
    { /* VMODE_4K2K_25HZ */
        .name              = "4k2k25hz",
        .mode              = TVMODE_4K2K_25HZ,
        .width             = 3840,
        .height            = 2160,
        .field_height      = 2160,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 25,
        .sync_duration_den = 1,
        .video_clk         = 297000000,
    },
    { /* VMODE_4K2K_24HZ */
        .name              = "4k2k24hz",
        .mode              = TVMODE_4K2K_24HZ,
        .width             = 3840,
        .height            = 2160,
        .field_height      = 2160,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 24,
        .sync_duration_den = 1,
        .video_clk         = 297000000,
    },
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	{ /* VMODE_4K2K_23HZ */
		.name			   = "4k2k23hz",
		.mode			   = TVMODE_4K2K_23HZ,
		.width			   = 3840,
		.height 		   = 2160,
		.field_height	   = 2160,
		.aspect_ratio_num  = 16,
		.aspect_ratio_den  = 9,
		.sync_duration_num = 2397,
		.sync_duration_den = 100,
		.video_clk		   = 297000000,
	},
#endif
    { /* VMODE_4K2K_SMPTE */
        .name              = "4k2ksmpte",
        .mode              = TVMODE_4K2K_SMPTE,
        .width             = 4096,
        .height            = 2160,
        .field_height      = 2160,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 24,
        .sync_duration_den = 1,
        .video_clk         = 297000000,
    },
    { /* VMODE_1920x1200 */
		.name              = "1920x1200",
		.mode              = VMODE_1920x1200,
        .width             = 1920,
        .height            = 1200,
        .field_height      = 1200,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
		.video_clk         = 154000000,
    }, 
    { /* VMODE_vga */
		.name              = "vga",
		.mode              = VMODE_VGA,
        .width             = 640,
        .height            = 480,
        .field_height      = 480,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
		.video_clk         = 25175000,
    }, 
    { /* VMODE_SVGA */
		.name              = "svga",
		.mode              = VMODE_SVGA,
        .width             = 800,
        .height            = 600,
        .field_height      = 600,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
		.video_clk         = 40000000,
    }, 
    { /* VMODE_XGA */
		.name              = "xga",
		.mode              = VMODE_XGA,
        .width             = 1024,
        .height            = 768,
        .field_height      = 768,
        .aspect_ratio_num  = 4,
        .aspect_ratio_den  = 3,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
		.video_clk         = 65000000,
    }, 
    { /* VMODE_sxga */
		.name              = "sxga",
		.mode              = VMODE_SXGA,
        .width             = 1280,
        .height            = 1024,
        .field_height      = 1024,
        .aspect_ratio_num  = 5,
        .aspect_ratio_den  = 4,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
		.video_clk         = 108000000,
    }, 
	{ /* VMODE_wsxga */
		.name              = "wsxga",
		.mode              = VMODE_WSXGA,
        .width             = 1440,
        .height            = 900,
        .field_height      = 900,
        .aspect_ratio_num  = 8,
        .aspect_ratio_den  = 5,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
		.video_clk         = 88750000,
    },
	{ /* VMODE_fhdvga */
		.name              = "fhdvga",
		.mode              = VMODE_FHDVGA,
        .width             = 1920,
        .height            = 1080,
        .field_height      = 1080,
        .aspect_ratio_num  = 16,
        .aspect_ratio_den  = 9,
        .sync_duration_num = 60,
        .sync_duration_den = 1,
		.video_clk         = 148500000,
    },
};

static const struct file_operations am_tv_fops = {
	.open	= NULL,  
	.read	= NULL,//am_tv_read, 
	.write	= NULL, 
	.unlocked_ioctl	= NULL,//am_tv_ioctl, 
	.release	= NULL, 	
	.poll		= NULL,
};

static const vinfo_t *get_valid_vinfo(char  *mode)
{
	const vinfo_t * vinfo = NULL;
	int  i,count=ARRAY_SIZE(tv_info);
	int mode_name_len=0;
	
	for(i=0;i<count;i++)
	{
		if(strncmp(tv_info[i].name,mode,strlen(tv_info[i].name))==0)
		{
			if((vinfo==NULL)||(strlen(tv_info[i].name)>mode_name_len)){
			    vinfo = &tv_info[i];
			    mode_name_len = strlen(tv_info[i].name);
			}
		}
	}
	return vinfo;
}

static const vinfo_t *tv_get_current_info(void)
{
	return info->vinfo;
}

tvmode_t vmode_to_tvmode(vmode_t mod) 
{
    return vmode_tvmode_tab[mod];
}

static int tv_set_current_vmode(vmode_t mod)
{
	if ((mod&VMODE_MODE_BIT_MASK)> VMODE_FHDVGA)
		return -EINVAL;
	info->vinfo = &tv_info[mod & VMODE_MODE_BIT_MASK];
	if(mod&VMODE_LOGO_BIT_MASK)  return 0;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	switch_vpu_mem_pd_vmod(info->vinfo->mode, VPU_MEM_POWER_ON);
	request_vpu_clk_vmod(info->vinfo->video_clk, info->vinfo->mode);
#endif
	tvoutc_setmode(vmode_to_tvmode(mod));
//	change_vdac_setting(get_current_vdac_setting(),mod);
	return 0;
}

static vmode_t tv_validate_vmode(char *mode)
{
	const vinfo_t *info = get_valid_vinfo(mode);
	if (info)
		return info->mode;
	
	return VMODE_MAX;
}
static int tv_vmode_is_supported(vmode_t mode)
{
	int  i,count=ARRAY_SIZE(tv_info);
	mode&=VMODE_MODE_BIT_MASK;
	for(i=0;i<count;i++)
	{
		if(tv_info[i].mode==mode)
		{
			return true;
		}
	}
	return false;
}
static int tv_module_disable(vmode_t cur_vmod)
{
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	if (info->vinfo) {
		release_vpu_clk_vmod(info->vinfo->mode);
		switch_vpu_mem_pd_vmod(info->vinfo->mode, VPU_MEM_POWER_DOWN);
	}
#endif
	//video_dac_disable();
	return 0;
}

#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION

static char* get_name_from_vmode(vmode_t mode)
{
	int i = 0, count = 0;
	
	count = ARRAY_SIZE(tv_info);
	for( i=0; i<count; i++ )
	{
		if( tv_info[i].mode == mode )
			break;
	}

	if( i == count )
		return NULL;

	return tv_info[i].name;
}

// frame_rate = 9600/duration/100 hz
static int get_vsource_frame_rate(int duration)
{
	int frame_rate = 0;

	switch( duration )
	{
		case 1600:
			frame_rate = 6000;
			break;
		case 1601:
		case 1602:
			frame_rate = 5994;
			break;
		case 1920:
			frame_rate = 5000;
			break;
		case 3200:
			frame_rate = 3000;
			break;
		case 3203:
			frame_rate = 2997;
			break;
		case 3840:
			frame_rate = 2500;
			break;
		case 4000:
			frame_rate = 2400;
			break;
		case 4004:
			frame_rate = 2397;
			break;
		default:
			break;
	}

	return frame_rate;
}

static int get_target_frame_rate(int framerate_vsource, int policy)
{
	const vinfo_t *pvinfo ;
	int framerate_target = 0;
	pvinfo = tv_get_current_info();
	switch( policy )
	{
		case 0: // not change
			break;
		case 1: // change to the frame rate of video source
			if( (framerate_vsource==2397) || (framerate_vsource==2997) || (framerate_vsource==5994) ){
				switch(pvinfo->sync_duration_num)
				{
				case 24:
					framerate_target=2397;
					break;
				case 30:
					framerate_target=2997;
					break;
				case 60:
					framerate_target=5994;
					break;	
				default:
					framerate_target = ( pvinfo->sync_duration_num > 100 ? pvinfo->sync_duration_num : pvinfo->sync_duration_num*100 );
					break;
				}
			}
			else
				framerate_target = framerate_vsource;
			break;
		case 2: // change to the frame rate of video source, but use 59.94 for 23.97/29.97
			if( (framerate_vsource==2397) || (framerate_vsource==2997) )
				framerate_target = 5994;
			else
				framerate_target = framerate_vsource;
			break;
		default:
			break;
	}
	return framerate_target;
}

extern int hdmitx_is_vmode_supported(char *mode_name);

static int get_target_vmode(int framerate_target)
{
	int is_receiver_supported = 0;
	const vinfo_t *pvinfo ;
	vmode_t mode_target = VMODE_INIT_NULL;

	printk("vout [%s] frame_rate_target = %d\n", __FUNCTION__, framerate_target);

	pvinfo = tv_get_current_info();

	mode_target = pvinfo->mode;

	if( (framerate_target==2397) || (framerate_target==2997) || (framerate_target==5994) ){
		switch( mode_target ){
			case VMODE_480P:
				mode_target = VMODE_480P_59HZ;
				break;
			case VMODE_720P:
				mode_target = VMODE_720P_59HZ;
				break;
			case VMODE_1080I:
				mode_target = VMODE_1080I_59HZ;
				break;
			case VMODE_1080P_24HZ:
				mode_target = VMODE_1080P_23HZ;
				break;
			case VMODE_1080P:
				mode_target = VMODE_1080P_59HZ;
				break;
			case VMODE_4K2K_24HZ:
				mode_target = VMODE_4K2K_23HZ;
				break;
			case VMODE_4K2K_30HZ:
				mode_target = VMODE_4K2K_29HZ;
				break;
			default:
				break;
		}
	}
	is_receiver_supported = hdmitx_is_vmode_supported(get_name_from_vmode(mode_target));
	
	switch( is_receiver_supported )
	{
		case 0: // not supported in edid
			mode_target = pvinfo->mode;
			break;
		case 1: // supported in edid
			break;
		case 2: // no edid
			mode_target = pvinfo->mode;
			break;
		default:
			break;
	}
	return mode_target;
}

// return values:
//		0: 		same vmode, need not change
//		1: 		similar vmode, just change pll to add 0.1% clock
//		0xff: 	similar vmode, just change pll to reduce 0.1% clock
//		2: 		different vmode, need change mode
static int get_exchange_mode(vmode_t mode_target)
{
	const vinfo_t *pvinfo;
	vmode_t mode_current = VMODE_INIT_NULL;
	
	pvinfo = tv_get_current_info();
	mode_current = pvinfo->mode;
	
	if( mode_current == mode_target )
		return 0;
	
	if( ((mode_current==VMODE_480P) && (mode_target==VMODE_480P_59HZ)) ||
		((mode_current==VMODE_480P_59HZ) && (mode_target==VMODE_480P)) ||
		((mode_current==VMODE_720P) && (mode_target==VMODE_720P_59HZ)) ||
		((mode_current==VMODE_720P_59HZ) && (mode_target==VMODE_720P)) ||
		((mode_current==VMODE_1080I) && (mode_target==VMODE_1080I_59HZ))||
		((mode_current==VMODE_1080I_59HZ) && (mode_target==VMODE_1080I))||
		((mode_current==VMODE_1080P) && (mode_target==VMODE_1080P_59HZ))||
		((mode_current==VMODE_1080P_59HZ) && (mode_target==VMODE_1080P))||
		((mode_current==VMODE_1080P_24HZ) && (mode_target==VMODE_1080P_23HZ))||
		((mode_current==VMODE_1080P_23HZ) && (mode_target==VMODE_1080P_24HZ) )||
		((mode_current==VMODE_4K2K_30HZ) && (mode_target==VMODE_4K2K_29HZ))||
		((mode_current==VMODE_4K2K_29HZ) && (mode_target==VMODE_4K2K_30HZ))||
		((mode_current==VMODE_4K2K_24HZ) && (mode_target==VMODE_4K2K_23HZ))||
		((mode_current==VMODE_4K2K_23HZ) && (mode_target==VMODE_4K2K_24HZ)) )
		return 0x1;

	return 2;
}

// just to fine tune the 0.1% clock
static int clock_fine_tune(void)
{
	const vinfo_t *pvinfo ;
	pvinfo = tv_get_current_info();
#if (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON8) || (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON8B)
	switch( pvinfo->mode )
	{
		case VMODE_720P_59HZ:
		case VMODE_1080I_59HZ:
		case VMODE_1080P_23HZ:
		case VMODE_1080P_59HZ:
		case VMODE_4K2K_29HZ:
		case VMODE_4K2K_23HZ:
			aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x69c84d04);
			break;
		case VMODE_720P:
		case VMODE_1080I:
		case VMODE_1080P:
		case VMODE_1080P_24HZ:
		case VMODE_4K2K_30HZ:	
		case VMODE_4K2K_24HZ:
			aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x69c84e00);
			break;
		case VMODE_480P_59HZ:
			if( (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON8B) || (IS_MESON_M8M2_CPU) ){
				aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x69c84f48);
				aml_write_reg32(P_HHI_VID_PLL_CNTL,  0x400d042c);
			}
			else{
				aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x69c8cf48);
				aml_write_reg32(P_HHI_VID_PLL_CNTL,  0x4008042c);
			}
			break;
		case VMODE_480P:
			if( (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON8B) || (IS_MESON_M8M2_CPU) ){
				aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x69c84000);
				aml_write_reg32(P_HHI_VID_PLL_CNTL,  0x400d042d);
			}
			else{
				aml_write_reg32(P_HHI_VID_PLL_CNTL2, 0x69c88000);
				aml_write_reg32(P_HHI_VID_PLL_CNTL,  0x4008042d);
			}
			break;
		default:
			break;
	}

#endif

	return 0;
}

extern void update_vmode_status(char* name);
extern void set_vout_mode_fr_auto(char * name);

static void update_current_vinfo(vmode_t mode)
{
	if ((mode&VMODE_MODE_BIT_MASK)> VMODE_FHDVGA)
		return ;

	info->vinfo = &tv_info[mode & VMODE_MODE_BIT_MASK];

	return ;
}

static int framerate_automation_set_mode(vmode_t mode_target)
{
	int auto_mode = 0;

	auto_mode = get_exchange_mode(mode_target);

	printk("vout [%s] mode_target = %d\n", __FUNCTION__, mode_target);
	printk("+++++++++++++++++%s[%d]auto_mode=%d++++++++++++++++\n",__func__,__LINE__,auto_mode);
	switch( auto_mode )
	{
		case 0:
			// need not change vmode
			break;
		case 1:
			// just change pll to adjust clock
		 	update_vmode_status(get_name_from_vmode(mode_target));
			update_current_vinfo(mode_target);
			clock_fine_tune();
			vout_notifier_call_chain(VOUT_EVENT_MODE_CHANGE,&mode_target) ;
			break;
		case 2:
			// change vmode and notify all client
			set_vout_mode_fr_auto(get_name_from_vmode(mode_target));
			break;
		default:
			break;
	}

	return 0;
}

static int framerate_automation_process(int duration)
{
	int policy=0, fr_vsource = 0, fr_target = 0;
	vmode_t mode_target = VMODE_INIT_NULL;
	const vinfo_t *pvinfo;

	printk("vout [%s] duration = %d\n", __FUNCTION__, duration);
	policy = fr_auto_policy;
	if( policy == 0 )
	{
		printk("vout frame rate automation disabled!\n");
		return 1;
	}

	fr_vsource = get_vsource_frame_rate(duration);
	fr_target = get_target_frame_rate(fr_vsource, policy);

	pvinfo = tv_get_current_info();
	if( (pvinfo->sync_duration_num==fr_target) || (pvinfo->sync_duration_num==(fr_target/100)) )
		return 0;

	mode_target = get_target_vmode(fr_target);
	
	framerate_automation_set_mode(mode_target);

	return 0;
}

#endif
static int tv_set_vframe_rate_hint(int duration)
{
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION

	printk("vout [%s] duration = %d, policy = %d!\n", __FUNCTION__, duration, fr_auto_policy);

	framerate_automation_process(duration);

#endif

	return 0;
}

#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
extern vmode_t mode_by_user ;
#endif

static int tv_set_vframe_rate_end_hint(void)
{
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION

	printk("vout [%s] return mode = %d, policy = %d!\n", __FUNCTION__, mode_by_user, fr_auto_policy);
	if( fr_auto_policy != 0 )
	{
		framerate_automation_set_mode(mode_by_user);
	}

#endif

	return 0;
}

#ifdef  CONFIG_PM
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
extern void cvbs_cntl_output(unsigned int open);
#endif
static int tv_suspend(void)
{
	video_dac_disable();
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	cvbs_cntl_output(0);
#endif

	return 0;
}
static int tv_resume(void)
{
	video_dac_enable(0xff);
	tv_set_current_vmode(info->vinfo->mode);
	return 0;
}
#endif 
static vout_server_t tv_server={
	.name = "vout_tv_server",
	.op = {	
		.get_vinfo=tv_get_current_info,
		.set_vmode=tv_set_current_vmode,
		.validate_vmode=tv_validate_vmode,
		.vmode_is_supported=tv_vmode_is_supported,
		.disable = tv_module_disable,
		.set_vframe_rate_hint = tv_set_vframe_rate_hint,
		.set_vframe_rate_end_hint = tv_set_vframe_rate_end_hint,
#ifdef  CONFIG_PM  
		.vout_suspend=tv_suspend,
		.vout_resume=tv_resume,
#endif	
	},
};

static void _init_vout(void)
{
	if (info->vinfo == NULL)
		info->vinfo = &tv_info[TVMODE_720P];
}

/***************************************************
**
**	The first digit control component Y output DAC number
**	The 2nd digit control component U output DAC number
**	The 3rd digit control component V output DAC number
**	The 4th digit control composite CVBS output DAC number
**	The 5th digit control s-video Luma output DAC number
**	The 6th digit control s-video chroma output DAC number
** 	examble :
**		echo  120120 > /sys/class/display/vdac_setting
**		the first digit from the left side .	
******************************************************/
static void  parse_vdac_setting(char *para) 
{
	int  i;
	char  *pt=strstrip(para);
	int len=strlen(pt);
	u32  vdac_sequence=get_current_vdac_setting();
	
	amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_LOW,"origin vdac setting:0x%x,strlen:%d\n",vdac_sequence,len);
	if(len!=6)
	{
		amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_HIGH,"can't parse vdac settings\n");
		return ;
	}
	vdac_sequence=0;
	for(i=0;i<6;i++)
	{
		vdac_sequence<<=4;
		vdac_sequence|=*pt -'0';
		pt++;
	}
	amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_LOW,"current vdac setting:0x%x\n",vdac_sequence);
	
	change_vdac_setting(vdac_sequence,get_current_vmode());
}

#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
//
// 0: disable frame_rate_automation feature
// 1: enable frame_rate_automation feature, same with frame rate of video source
// 2: enable frame_rate_automation feature, use 59.94 instead of 23.97/29.97
//
static void policy_framerate_automation_store(char* para)
{
	int policy = 0;

	policy = simple_strtoul(para, NULL, 10);

	if( (policy>=0) && (policy<3) )
	{
		fr_auto_policy = policy;
	}

	return ;
}

#endif

static  struct  class_attribute   *tv_attr[]={
&class_TV_attr_vdac_setting,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
&class_TV_attr_policy_fr_auto,
#endif
};
static int  create_tv_attr(disp_module_info_t* info)
{
	//create base class for display
	int  i;

	info->base_class=class_create(THIS_MODULE,info->name);
	if(IS_ERR(info->base_class))
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create tv display class fail\n");
		return  -1 ;
	}
	//create  class attr
	for(i=0;i<ARRAY_SIZE(tv_attr);i++)
	{
		if ( class_create_file(info->base_class,tv_attr[i]))
		{
			amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create disp attribute %s fail\n",tv_attr[i]->attr.name);
		}
	}
	sprintf(vdac_setting,"%x",get_current_vdac_setting());

#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
	sprintf(policy_fr_auto, "%d", DEFAULT_POLICY_FR_AUTO);
#endif

	return   0;
}
static int __init tv_init_module(void)
{
	int  ret ;

	info=(disp_module_info_t*)kmalloc(sizeof(disp_module_info_t),GFP_KERNEL) ;
    printk("%s\n", __func__);

	if (!info)
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"can't alloc display info struct\n");
		return -ENOMEM;
	}
	
	memset(info, 0, sizeof(disp_module_info_t));

	sprintf(info->name,TV_CLASS_NAME) ;
	ret=register_chrdev(0,info->name,&am_tv_fops);
	if(ret <0) 
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"register char dev tv error\n");
		return  ret ;
	}
	info->major=ret;
	_init_vout();
	amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"major number %d for disp\n",ret);
	if(vout_register_server(&tv_server))
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"register tv module server fail\n");
	}
	else
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"register tv module server ok\n");
	}
	create_tv_attr(info);
	return 0;

}
static __exit void tv_exit_module(void)
{
	int i;
	
	if(info->base_class)
	{
		for(i=0;i<ARRAY_SIZE(tv_attr);i++)
		{
			class_remove_file(info->base_class,tv_attr[i]) ;
		}
		
		class_destroy(info->base_class);
	}	
	if(info)
	{
		unregister_chrdev(info->major,info->name)	;
		kfree(info);
	}
	vout_unregister_server(&tv_server);
	
	amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"exit tv module\n");
}

#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)
extern void cvbs_config_vdac(unsigned int flag, unsigned int cfg);

static int __init vdac_config_bootargs_setup(char* line)
{
    unsigned int cfg = 0x00;

    printk("cvbs trimming line = %s\n", line);
    cfg = simple_strtoul(line, NULL, 16);

    cvbs_config_vdac((cfg&0xff00)>>8, cfg&0xff);

    return 1;
}

__setup("vdaccfg=", vdac_config_bootargs_setup);
#endif

extern void cvbs_performance_config(unsigned int index);
static int __init cvbs_performance_setup(char* line)
{
	unsigned int cfg = 0x1;

	printk("cvbs performance line = %s\n", line);
	cfg = simple_strtoul(line, NULL, 10);

	cvbs_performance_config(cfg);
	return 0;
}
__setup("cvbsdrv=", cvbs_performance_setup);

arch_initcall(tv_init_module);
module_exit(tv_exit_module);

MODULE_DESCRIPTION("display configure  module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("jianfeng_wang <jianfeng.wang@amlogic.com>");

