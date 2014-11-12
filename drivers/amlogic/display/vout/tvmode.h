/*
 * Amlogic Apollo
 * frame buffer driver
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
 * Author:	Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef TVMODE_H
#define TVMODE_H

typedef enum {
    TVMODE_480I  = 0,
    TVMODE_480I_RPT  ,
    TVMODE_480CVBS,
    TVMODE_480P  ,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_480P_59HZ , // for framerate automation 480p 59.94hz
#endif
    TVMODE_480P_RPT  ,
    TVMODE_576I  ,
    TVMODE_576I_RPT  ,
    TVMODE_576CVBS,
    TVMODE_576P  ,
    TVMODE_576P_RPT  ,
    TVMODE_720P  ,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_720P_59HZ , // for framerate automation 720p 59.94hz
#endif
    TVMODE_800P	,
    TVMODE_800X480P_60HZ,
    TVMODE_1366X768P_60HZ,
    TVMODE_1600X900P_60HZ,
    TVMODE_800X600P_60HZ,
    TVMODE_1024X600P_60HZ,
    TVMODE_1024X768P_60HZ,
    TVMODE_1360X768P_60HZ,
    TVMODE_1440X900P_60HZ,
    TVMODE_1680X1050P_60HZ,
    TVMODE_1080I ,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_1080I_59HZ , // for framerate automation 1080i 59.94hz
#endif
    TVMODE_1080P ,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_1080P_59HZ , // for framerate automation 1080p 59.94hz
#endif
    TVMODE_720P_50HZ ,
    TVMODE_1080I_50HZ ,
    TVMODE_1080P_50HZ ,
    TVMODE_1080P_24HZ ,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_1080P_23HZ , // for framerate automation 1080p 23.97hz
#endif
    TVMODE_4K2K_30HZ ,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_4K2K_29HZ , // for framerate automation 4k2k 29.97hz
#endif
    TVMODE_4K2K_25HZ ,
    TVMODE_4K2K_24HZ ,
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
    TVMODE_4K2K_23HZ , // for framerate automation 4k2k 23.97hz
#endif
    TVMODE_4K2K_SMPTE ,
    TVMODE_1920x1200,
    TVMODE_VGA ,
    TVMODE_SVGA,
    TVMODE_XGA,
    TVMODE_SXGA,
    TVMODE_WSXGA,
    TVMODE_FHDVGA,
    TVMODE_MAX    
} tvmode_t;

#endif /* TVMODE_H */
