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
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef TVREGS_H
#define TVREGS_H

#define MREG_END_MARKER 0xffff


	#define VIDEO_CLOCK_HD_25	0x00101529
	#define VIDEO_CLOCK_SD_25	0x00500a6c
	#define VIDEO_CLOCK_HD_24	0x00140863
	#define VIDEO_CLOCK_SD_24	0x0050042d


typedef struct reg_s {
    uint reg;
    uint val;
} reg_t;

typedef struct tvinfo_s {
    uint xres;
    uint yres;
    const char *id;
} tvinfo_t;
/*
24M
25M
*/
static const  reg_t tvreg_vclk_sd[]={
	{P_HHI_VID_PLL_CNTL,VIDEO_CLOCK_SD_24},//SD.24
    {P_HHI_VID_PLL_CNTL,VIDEO_CLOCK_SD_25},//SD,25
};

static const  reg_t tvreg_vclk_hd[]={
    {P_HHI_VID_PLL_CNTL,VIDEO_CLOCK_HD_24},//HD,24
    {P_HHI_VID_PLL_CNTL,VIDEO_CLOCK_HD_25},//HD,25
};

static const  reg_t tvregs_720p[] = {
    {P_VENC_VDAC_SETTING,          0xff,  },
#ifndef CONFIG_AM_TV_OUTPUT2
    {P_HHI_VID_CLK_CNTL,           0x0,},
#endif
    {P_HHI_VID_PLL_CNTL2,          0x00040003},
    {P_HHI_VID_PLL_CNTL3,          0x40e8},
    {P_HHI_HDMI_AFC_CNTL,          0x8c0000c3},
    {P_HHI_VID_PLL_CNTL,           0x0001043e,},
    {P_HHI_VID_DIVIDER_CNTL,       0x00010843,},
#ifndef CONFIG_AM_TV_OUTPUT2
    {P_HHI_VID_CLK_DIV,            0x100},
    {P_HHI_VID_CLK_CNTL,           0x80000,},
    {P_HHI_VID_CLK_CNTL,           0x88001,},
    {P_HHI_VID_CLK_CNTL,           0x80003,},
    {P_HHI_VIID_CLK_DIV,           0x00000101,},
#endif

    {P_ENCP_VIDEO_FILT_CTRL,       0x0052,},
    {P_VENC_DVI_SETTING,           0x2029,},
    {P_ENCP_VIDEO_MODE,            0x0040,},
    {P_ENCP_VIDEO_MODE_ADV,        0x0019,},
    {P_ENCP_VIDEO_YFP1_HTIME,      648,   },
    {P_ENCP_VIDEO_YFP2_HTIME,      3207,  },
    {P_ENCP_VIDEO_MAX_PXCNT,       3299,  },
    {P_ENCP_VIDEO_HSPULS_BEGIN,    80,    },
    {P_ENCP_VIDEO_HSPULS_END,      240,   },
    {P_ENCP_VIDEO_HSPULS_SWITCH,   80,    },
    {P_ENCP_VIDEO_VSPULS_BEGIN,    688,   },
    {P_ENCP_VIDEO_VSPULS_END,      3248,  },
    {P_ENCP_VIDEO_VSPULS_BLINE,    4,     },
    {P_ENCP_VIDEO_VSPULS_ELINE,    8,     },
    {P_ENCP_VIDEO_EQPULS_BLINE,    4,     },
    {P_ENCP_VIDEO_EQPULS_ELINE,    8,     },
    {P_ENCP_VIDEO_HAVON_BEGIN,     648,   },
    {P_ENCP_VIDEO_HAVON_END,       3207,  },
    {P_ENCP_VIDEO_VAVON_BLINE,     29,    },
    {P_ENCP_VIDEO_VAVON_ELINE,     748,   },
    {P_ENCP_VIDEO_HSO_BEGIN,       256    },
    {P_ENCP_VIDEO_HSO_END,         168,   },
    {P_ENCP_VIDEO_VSO_BEGIN,       168,   },
    {P_ENCP_VIDEO_VSO_END,         256,   },
    {P_ENCP_VIDEO_VSO_BLINE,       0,     },
    {P_ENCP_VIDEO_VSO_ELINE,       5,     },
    {P_ENCP_VIDEO_MAX_LNCNT,       749,   },
    {P_VENC_VIDEO_PROG_MODE,       0x100, },
    {P_VENC_SYNC_ROUTE,            0,     },
    {P_VENC_INTCTRL,               0x200, },
    {P_ENCP_VFIFO2VD_CTL,          0,     },
    {P_VENC_VDAC_SETTING,          0,     },
    {P_VENC_UPSAMPLE_CTRL0,        0x9061,},
    {P_VENC_UPSAMPLE_CTRL1,        0xa061,},
    {P_VENC_UPSAMPLE_CTRL2,        0xb061,},
    {P_VENC_VDAC_DACSEL0,          0x0001,},
    {P_VENC_VDAC_DACSEL1,          0x0001,},
    {P_VENC_VDAC_DACSEL2,          0x0001,},
    {P_VENC_VDAC_DACSEL3,          0x0001,},
    {P_VENC_VDAC_DACSEL4,          0x0001,},
    {P_VENC_VDAC_DACSEL5,          0x0001,},
#ifndef CONFIG_AM_TV_OUTPUT2
    {P_VPU_VIU_VENC_MUX_CTRL,      0x000a,},
#endif
    {P_VENC_VDAC_FIFO_CTRL,        0x1000,},
    {P_ENCP_DACSEL_0,              0x3210,},
    {P_ENCP_DACSEL_1,              0x0054,},
    {P_ENCP_VIDEO_EN,              1,     },
    {P_ENCI_VIDEO_EN,              0,     },
    {MREG_END_MARKER,            0      }
};

static const  reg_t tvregs_720p_50hz[] = {
    {P_VENC_VDAC_SETTING,          0xff,  },
	{P_HHI_VID_CLK_DIV,			 1		},
    {P_HHI_VID_CLK_CNTL,         0x0421,},
    {P_ENCP_VIDEO_FILT_CTRL,       0x0052,},

    {P_VENC_DVI_SETTING,           0x202d,},
    {P_ENCP_VIDEO_MAX_PXCNT,       3959,  },
    {P_ENCP_VIDEO_MAX_LNCNT,       749,   },

     //analog vidoe position in horizontal
    {P_ENCP_VIDEO_HSPULS_BEGIN,    80,    },
    {P_ENCP_VIDEO_HSPULS_END,      240,   },
    {P_ENCP_VIDEO_HSPULS_SWITCH,   80,    },

    //DE position in horizontal
    {P_ENCP_VIDEO_HAVON_BEGIN,     648,   },
    {P_ENCP_VIDEO_HAVON_END,       3207,  },

    //ditital hsync positon in horizontal
    {P_ENCP_VIDEO_HSO_BEGIN,       128 ,},
    {P_ENCP_VIDEO_HSO_END,         208 , },

    /* vsync horizontal timing */
    {P_ENCP_VIDEO_VSPULS_BEGIN,    688,   },
    {P_ENCP_VIDEO_VSPULS_END,      3248,  },

    /* vertical timing settings */
    {P_ENCP_VIDEO_VSPULS_BLINE,    4,     },
    {P_ENCP_VIDEO_VSPULS_ELINE,    8,     },
    {P_ENCP_VIDEO_EQPULS_BLINE,    4,     },
    {P_ENCP_VIDEO_EQPULS_ELINE,    8,     },

    //DE position in vertical
    {P_ENCP_VIDEO_VAVON_BLINE,     29,    },
    {P_ENCP_VIDEO_VAVON_ELINE,     748,   },

    //adjust the vsync start point and end point
    {P_ENCP_VIDEO_VSO_BEGIN,       128,},  //168,   },
    {P_ENCP_VIDEO_VSO_END,         128, },  //256,   },

    //adjust the vsync start line and end line
    {P_ENCP_VIDEO_VSO_BLINE,       0,     },
    {P_ENCP_VIDEO_VSO_ELINE,       5,     },

    /* filter & misc settings */
    {P_ENCP_VIDEO_YFP1_HTIME,      648,   },
    {P_ENCP_VIDEO_YFP2_HTIME,      3207,  },


    {P_VENC_VIDEO_PROG_MODE,       0x100, },
    {ENCP_VIDEO_MODE,            0x0040,},  //Enable Hsync and equalization pulse switch in center
    {P_ENCP_VIDEO_MODE_ADV,        0x0019,},//bit6:swap PbPr; bit4:YPBPR gain as HDTV type;
                                                 //bit3:Data input from VFIFO;bit[2}0]:repreat pixel a time

     {P_ENCP_VIDEO_SYNC_MODE,       0x407,  },//Video input Synchronization mode ( bit[7:0] -- 4:Slave mode, 7:Master mode)
                                                 //bit[15:6] -- adjust the vsync vertical position
    {P_ENCP_VIDEO_YC_DLY,          0,     },      //Y/Cb/Cr delay
    {P_VENC_SYNC_ROUTE,            0,     },
    {P_VENC_INTCTRL,               0x200, },
    {P_ENCP_VFIFO2VD_CTL,               0,     },
    {P_VENC_VDAC_SETTING,          0,     },
    {P_ENCP_VIDEO_EN,              1,     },
    {P_ENCI_VIDEO_EN,              0,     },
    {MREG_END_MARKER,            0      }
};

static const reg_t tvregs_480i[] = {
    {P_VENC_VDAC_SETTING,            0xff,  },
    {P_ENCI_CFILT_CTRL,              0x12,},
    {P_ENCI_CFILT_CTRL2,              0x12,},
    {P_VENC_DVI_SETTING,             0,     },
    {P_ENCI_VIDEO_MODE,              0,     },
    {P_ENCI_VIDEO_MODE_ADV,          0,     },
    {P_ENCI_SYNC_HSO_BEGIN,          5,     },
    {P_ENCI_SYNC_HSO_END,            129,   },
    {P_ENCI_SYNC_VSO_EVNLN,          0x0003 },
    {P_ENCI_SYNC_VSO_ODDLN,          0x0104 },
    {P_ENCI_MACV_MAX_AMP,            0x810b },
    {P_VENC_VIDEO_PROG_MODE,         0xf0   },
    {P_ENCI_VIDEO_MODE,              0x08   },
    {P_ENCI_VIDEO_MODE_ADV,          0x26,  },
    {P_ENCI_VIDEO_SCH,               0x20,  },
    {P_ENCI_SYNC_MODE,               0x07,  },
    {P_ENCI_YC_DELAY,                0x333, },
    {P_ENCI_VFIFO2VD_PIXEL_START,    0xf3,  },
    {P_ENCI_VFIFO2VD_PIXEL_END,      0x0693,},
    {P_ENCI_VFIFO2VD_LINE_TOP_START, 0x12,  },
    {P_ENCI_VFIFO2VD_LINE_TOP_END,   0x102, },
    {P_ENCI_VFIFO2VD_LINE_BOT_START, 0x13,  },
    {P_ENCI_VFIFO2VD_LINE_BOT_END,   0x103, },
    {P_VENC_SYNC_ROUTE,              0,     },
    {P_ENCI_DBG_PX_RST,              0,     },
    {P_VENC_INTCTRL,                 0x2,   },
    {P_ENCI_VFIFO2VD_CTL,            0x4e01,},
    {P_VENC_VDAC_SETTING,          0,     },
    {P_VENC_UPSAMPLE_CTRL0,          0x0061,},
    {P_VENC_UPSAMPLE_CTRL1,          0x4061,},
    {P_VENC_UPSAMPLE_CTRL2,          0x5061,},
    {P_VENC_VDAC_DACSEL0,            0x0000,},
    {P_VENC_VDAC_DACSEL1,            0x0000,},
    {P_VENC_VDAC_DACSEL2,            0x0000,},
    {P_VENC_VDAC_DACSEL3,            0x0000,},
    {P_VENC_VDAC_DACSEL4,            0x0000,},
    {P_VENC_VDAC_DACSEL5,            0x0000,},
    {P_VENC_VDAC_FIFO_CTRL,          0x2000,},
    {P_ENCI_DACSEL_0,                0x0011 },
    {P_ENCI_DACSEL_1,                0x87   },
    {P_ENCI_VIDEO_EN,                1,     },
    {P_HHI_VDAC_CNTL0,               0x650001   },
    {P_HHI_VDAC_CNTL1,               0x1        },
    {P_ENCI_VIDEO_SAT,               0x7        },
    {P_VENC_VDAC_DAC0_FILT_CTRL0,    0x1        },
    {P_VENC_VDAC_DAC0_FILT_CTRL1,    0xfc48     },
    {MREG_END_MARKER,              0      }
};

static const reg_t tvregs_480cvbs[] = {
     {P_VENC_VDAC_SETTING,            0xff,  },
    {P_ENCI_CFILT_CTRL,              0x12,},
    {P_ENCI_CFILT_CTRL2,              0x12,},
    {P_VENC_DVI_SETTING,             0,     },
    {P_ENCI_VIDEO_MODE,              0,     },
    {P_ENCI_VIDEO_MODE_ADV,          0,     },
    {P_ENCI_SYNC_HSO_BEGIN,          5,     },
    {P_ENCI_SYNC_HSO_END,            129,   },
    {P_ENCI_SYNC_VSO_EVNLN,          0x0003 },
    {P_ENCI_SYNC_VSO_ODDLN,          0x0104 },
    {P_ENCI_MACV_MAX_AMP,            0x810b },
    {P_VENC_VIDEO_PROG_MODE,         0xf0   },
    {P_ENCI_VIDEO_MODE,              0x08   },
    {P_ENCI_VIDEO_MODE_ADV,          0x26,  },
    {P_ENCI_VIDEO_SCH,               0x20,  },
    {P_ENCI_SYNC_MODE,               0x07,  },
    {P_ENCI_YC_DELAY,                0x333, },
    {P_ENCI_VFIFO2VD_PIXEL_START,    0xf3,  },
    {P_ENCI_VFIFO2VD_PIXEL_END,      0x0693,},
    {P_ENCI_VFIFO2VD_LINE_TOP_START, 0x12,  },
    {P_ENCI_VFIFO2VD_LINE_TOP_END,   0x102, },
    {P_ENCI_VFIFO2VD_LINE_BOT_START, 0x13,  },
    {P_ENCI_VFIFO2VD_LINE_BOT_END,   0x103, },
    {P_VENC_SYNC_ROUTE,              0,     },
    {P_ENCI_DBG_PX_RST,              0,     },
    {P_VENC_INTCTRL,                 0x2,   },
    {P_ENCI_VFIFO2VD_CTL,            0x4e01,},
    {P_VENC_VDAC_SETTING,          0,     },
    {P_VENC_UPSAMPLE_CTRL0,          0x0061,},
    {P_VENC_UPSAMPLE_CTRL1,          0x4061,},
    {P_VENC_UPSAMPLE_CTRL2,          0x5061,},
    {P_VENC_VDAC_DACSEL0,            0x0000,},
    {P_VENC_VDAC_DACSEL1,            0x0000,},
    {P_VENC_VDAC_DACSEL2,            0x0000,},
    {P_VENC_VDAC_DACSEL3,            0x0000,},
    {P_VENC_VDAC_DACSEL4,            0x0000,},
    {P_VENC_VDAC_DACSEL5,            0x0000,},
    {P_VENC_VDAC_FIFO_CTRL,          0x2000,},
    {P_ENCI_DACSEL_0,                0x0011 },
    {P_ENCI_DACSEL_1,                0x11   },
    {P_ENCI_VIDEO_EN,                1,     },
    {P_HHI_VDAC_CNTL0,               0x650001   },
    {P_HHI_VDAC_CNTL1,               0x1        },
    {P_ENCI_VIDEO_SAT,               0x7        },
    {P_VENC_VDAC_DAC0_FILT_CTRL0,    0x1        },
    {P_VENC_VDAC_DAC0_FILT_CTRL1,    0xfc48     },
    {MREG_END_MARKER,              0      }
};

static const reg_t tvregs_480p[] = {
    {P_VENC_VDAC_SETTING,          0xff,  },
#ifndef CONFIG_AM_TV_OUTPUT2
    {P_HHI_VID_CLK_CNTL,           0x0,       },
#endif
    {P_HHI_VID_PLL_CNTL2,          0x00040003},
    {P_HHI_VID_PLL_CNTL3,          0x40e8},
    {P_HHI_HDMI_AFC_CNTL,          0x8c0000c3},
    {P_HHI_VID_PLL_CNTL,           0x0001042d,},
    {P_HHI_VID_DIVIDER_CNTL,       0x00010843,},
#ifndef CONFIG_AM_TV_OUTPUT2
    {P_HHI_VID_CLK_DIV,            0x100},

    {P_HHI_VID_CLK_CNTL,           0x88001,},
    {P_HHI_VID_CLK_CNTL,           0x80001,},

    {P_HHI_VID_CLK_DIV,            0x01000100,},
#endif
    {P_ENCP_VIDEO_FILT_CTRL,       0x2052,},
    {P_VENC_DVI_SETTING,           0x21,  },
    {P_ENCP_VIDEO_MODE,            0,     },
    {P_ENCP_VIDEO_MODE_ADV,        9,     },
    {P_ENCP_VIDEO_YFP1_HTIME,      244,   },
    {P_ENCP_VIDEO_YFP2_HTIME,      1630,  },
    {P_ENCP_VIDEO_YC_DLY,          0,     },
    {P_ENCP_VIDEO_MAX_PXCNT,       1715,  },
    {P_ENCP_VIDEO_MAX_LNCNT,       524,   },
    {P_ENCP_VIDEO_HSPULS_BEGIN,    0x22,  },
    {P_ENCP_VIDEO_HSPULS_END,      0xa0,  },
    {P_ENCP_VIDEO_HSPULS_SWITCH,   88,    },
    {P_ENCP_VIDEO_VSPULS_BEGIN,    0,     },
    {P_ENCP_VIDEO_VSPULS_END,      1589   },
    {P_ENCP_VIDEO_VSPULS_BLINE,    0,     },
    {P_ENCP_VIDEO_VSPULS_ELINE,    5,     },
    {P_ENCP_VIDEO_HAVON_BEGIN,     249,   },
    {P_ENCP_VIDEO_HAVON_END,       1689,  },
    {P_ENCP_VIDEO_VAVON_BLINE,     42,    },
    {P_ENCP_VIDEO_VAVON_ELINE,     521,   },
    {P_ENCP_VIDEO_SYNC_MODE,       0x07,  },
    {P_VENC_VIDEO_PROG_MODE,       0x0,   },
    {P_VENC_VIDEO_EXSRC,           0x0,   },
    {P_ENCP_VIDEO_HSO_BEGIN,       0x3,   },
    {P_ENCP_VIDEO_HSO_END,         0x5,   },
    {P_ENCP_VIDEO_VSO_BEGIN,       0x3,   },
    {P_ENCP_VIDEO_VSO_END,         0x5,   },
    {P_ENCP_VIDEO_VSO_BLINE,       0,     },  //added by JZD. Switch Panel to 480p first time, movie video flicks if not set this to 0
    {P_VENC_SYNC_ROUTE,            0,     },
    {P_VENC_INTCTRL,               0x200, },
    {P_ENCP_VFIFO2VD_CTL,               0,     },
    {P_VENC_VDAC_SETTING,          0,     },
    {P_VENC_UPSAMPLE_CTRL0,        0x9061,},
    {P_VENC_UPSAMPLE_CTRL1,        0xa061,},
    {P_VENC_UPSAMPLE_CTRL2,        0xb061,},
    {P_VENC_VDAC_DACSEL0,          0xf003,},
    {P_VENC_VDAC_DACSEL1,          0xf003,},
    {P_VENC_VDAC_DACSEL2,          0xf003,},
    {P_VENC_VDAC_DACSEL3,          0xf003,},
    {P_VENC_VDAC_DACSEL4,          0xf003,},
    {P_VENC_VDAC_DACSEL5,          0xf003,},
#ifndef CONFIG_AM_TV_OUTPUT2
    {P_VPU_VIU_VENC_MUX_CTRL,      0x000a,},
#endif
    {P_VENC_VDAC_FIFO_CTRL,        0x1fc0,},
    {P_ENCP_DACSEL_0,              0x3210,},
    {P_ENCP_DACSEL_1,              0x0054,},
    {P_ENCI_VIDEO_EN,              0      },
    {P_ENCP_VIDEO_EN,              1      },
    {MREG_END_MARKER,            0      }
};

static const reg_t tvregs_576i[] = {
    {VENC_VDAC_SETTING,          0xff,  },
//	{VCLK_SD},
    {P_ENCI_CFILT_CTRL,                 0x12,    },
    {P_ENCI_CFILT_CTRL2,                 0x12,    },
    {P_VENC_DVI_SETTING,                0,         },
    {P_ENCI_VIDEO_MODE,                 0,         },
    {P_ENCI_VIDEO_MODE_ADV,             0,         },
    {P_ENCI_SYNC_HSO_BEGIN,             3,         },
    {P_ENCI_SYNC_HSO_END,               129,       },
    {P_ENCI_SYNC_VSO_EVNLN,             0x0003     },
    {P_ENCI_SYNC_VSO_ODDLN,             0x0104     },
    {P_ENCI_MACV_MAX_AMP,               0x8107     },
    {P_VENC_VIDEO_PROG_MODE,            0xff       },
    {P_ENCI_VIDEO_MODE,                 0x13       },
    {P_ENCI_VIDEO_MODE_ADV,             0x26,      },
    {P_ENCI_VIDEO_SCH,                  0x28,      },
    {P_ENCI_SYNC_MODE,                  0x07,      },
    {P_ENCI_YC_DELAY,                   0x333,     },
    {P_ENCI_VFIFO2VD_PIXEL_START,       0x010b     },
    {P_ENCI_VFIFO2VD_PIXEL_END,         0x06ab     },
    {P_ENCI_VFIFO2VD_LINE_TOP_START,    0x0016     },
    {P_ENCI_VFIFO2VD_LINE_TOP_END,      0x0136     },
    {P_ENCI_VFIFO2VD_LINE_BOT_START,    0x0017     },
    {P_ENCI_VFIFO2VD_LINE_BOT_END,      0x0137     },
    {P_VENC_SYNC_ROUTE,                 0,         },
    {P_ENCI_DBG_PX_RST,                 0,         },
    {P_VENC_INTCTRL,                    0x2,       },
    {P_ENCI_VFIFO2VD_CTL,               0x4e01,    },
    {P_VENC_VDAC_SETTING,          0,     },
    {P_VENC_UPSAMPLE_CTRL0,             0x0061,    },
    {P_VENC_UPSAMPLE_CTRL1,             0x4061,    },
    {P_VENC_UPSAMPLE_CTRL2,             0x5061,    },
    {P_VENC_VDAC_DACSEL0,               0x0000,    },
    {P_VENC_VDAC_DACSEL1,               0x0000,    },
    {P_VENC_VDAC_DACSEL2,               0x0000,    },
    {P_VENC_VDAC_DACSEL3,               0x0000,    },
    {P_VENC_VDAC_DACSEL4,               0x0000,    },
    {P_VENC_VDAC_DACSEL5,               0x0000,    },
    {P_VENC_VDAC_FIFO_CTRL,             0x2000,    },
    {P_ENCI_DACSEL_0,                   0x0011     },
    {P_ENCI_DACSEL_1,                   0x87       },
    {P_ENCI_VIDEO_EN,                   1,         },
    {P_HHI_VDAC_CNTL0,                  0x650001   },
    {P_HHI_VDAC_CNTL1,                  0x1        },
    {P_ENCI_VIDEO_SAT,                  0x7        },
    {P_VENC_VDAC_DAC0_FILT_CTRL0,       0x1        },
    {P_VENC_VDAC_DAC0_FILT_CTRL1,       0xfc48     },
    {MREG_END_MARKER,            0      }
};

static const reg_t tvregs_576cvbs[] = {
    {VENC_VDAC_SETTING,          0xff,  },
//	{VCLK_SD},
    {P_ENCI_CFILT_CTRL,                 0x12,    },
    {P_ENCI_CFILT_CTRL2,                 0x12,    },
    {P_VENC_DVI_SETTING,                0,         },
    {P_ENCI_VIDEO_MODE,                 0,         },
    {P_ENCI_VIDEO_MODE_ADV,             0,         },
    {P_ENCI_SYNC_HSO_BEGIN,             3,         },
    {P_ENCI_SYNC_HSO_END,               129,       },
    {P_ENCI_SYNC_VSO_EVNLN,             0x0003     },
    {P_ENCI_SYNC_VSO_ODDLN,             0x0104     },
    {P_ENCI_MACV_MAX_AMP,               0x8107     },
    {P_VENC_VIDEO_PROG_MODE,            0xff       },
    {P_ENCI_VIDEO_MODE,                 0x13       },
    {P_ENCI_VIDEO_MODE_ADV,             0x26,      },
    {P_ENCI_VIDEO_SCH,                  0x28,      },
    {P_ENCI_SYNC_MODE,                  0x07,      },
    {P_ENCI_YC_DELAY,                   0x333,     },
    {P_ENCI_VFIFO2VD_PIXEL_START,       0x010b     },
    {P_ENCI_VFIFO2VD_PIXEL_END,         0x06ab     },
    {P_ENCI_VFIFO2VD_LINE_TOP_START,    0x0016     },
    {P_ENCI_VFIFO2VD_LINE_TOP_END,      0x0136     },
    {P_ENCI_VFIFO2VD_LINE_BOT_START,    0x0017     },
    {P_ENCI_VFIFO2VD_LINE_BOT_END,      0x0137     },
    {P_VENC_SYNC_ROUTE,                 0,         },
    {P_ENCI_DBG_PX_RST,                 0,         },
    {P_VENC_INTCTRL,                    0x2,       },
    {P_ENCI_VFIFO2VD_CTL,               0x4e01,    },
    {P_VENC_VDAC_SETTING,          0,     },
    {P_VENC_UPSAMPLE_CTRL0,             0x0061,    },
    {P_VENC_UPSAMPLE_CTRL1,             0x4061,    },
    {P_VENC_UPSAMPLE_CTRL2,             0x5061,    },
    {P_VENC_VDAC_DACSEL0,               0x0000,    },
    {P_VENC_VDAC_DACSEL1,               0x0000,    },
    {P_VENC_VDAC_DACSEL2,               0x0000,    },
    {P_VENC_VDAC_DACSEL3,               0x0000,    },
    {P_VENC_VDAC_DACSEL4,               0x0000,    },
    {P_VENC_VDAC_DACSEL5,               0x0000,    },
    {P_VENC_VDAC_FIFO_CTRL,             0x2000,    },
    {P_ENCI_DACSEL_0,                   0x0011     },
    {P_ENCI_DACSEL_1,                   0x11       },
    {P_ENCI_VIDEO_EN,                   1,         },
    {MREG_END_MARKER,            0      }
};

static const reg_t tvregs_576p[] = {
    {P_VENC_VDAC_SETTING,          0xff,  },
//	{VCLK_SD},
	{P_HHI_VID_CLK_DIV,			 4,		},
    {P_HHI_VID_CLK_CNTL,        	 0x0561,},
    {P_ENCP_VIDEO_FILT_CTRL,       0x52,      },
    {P_VENC_DVI_SETTING,           0x21,      },
    {P_ENCP_VIDEO_MODE,            0,     },
    {P_ENCP_VIDEO_MODE_ADV,        9,         },
    {P_ENCP_VIDEO_YFP1_HTIME,      235,       },
    {P_ENCP_VIDEO_YFP2_HTIME,      1674,      },
    {P_ENCP_VIDEO_YC_DLY,          0xf,       },
    {P_ENCP_VIDEO_MAX_PXCNT,       1727,      },
    {P_ENCP_VIDEO_MAX_LNCNT,       624,       },
    {P_ENCP_VIDEO_HSPULS_BEGIN,    0,         },
    {P_ENCP_VIDEO_HSPULS_END,      0x80,      },
    {P_ENCP_VIDEO_VSPULS_BEGIN,    0,         },
    {P_ENCP_VIDEO_VSPULS_END,      1599       },
    {P_ENCP_VIDEO_VSPULS_BLINE,    0,         },
    {P_ENCP_VIDEO_VSPULS_ELINE,    4,         },
    {P_ENCP_VIDEO_HAVON_BEGIN,     235,       },
    {P_ENCP_VIDEO_HAVON_END,       1674,      },
    {P_ENCP_VIDEO_VAVON_BLINE,     44,        },
    {P_ENCP_VIDEO_VAVON_ELINE,     619,       },
    {P_ENCP_VIDEO_SYNC_MODE,       0x07,      },
    {P_VENC_VIDEO_PROG_MODE,       0x0,       },
    {P_VENC_VIDEO_EXSRC,           0x0,       },
    {P_ENCP_VIDEO_HSO_BEGIN,       0x80,      },
    {P_ENCP_VIDEO_HSO_END,         0x0,       },
    {P_ENCP_VIDEO_VSO_BEGIN,       0x0,       },
    {P_ENCP_VIDEO_VSO_END,         0x5,       },
    {P_VENC_SYNC_ROUTE,            0,         },
    {P_VENC_INTCTRL,               0x200,     },
    {P_ENCP_VFIFO2VD_CTL,               0,         },
    {P_VENC_VDAC_SETTING,          0,     },
    {P_ENCI_VIDEO_EN,              0          },
    {P_ENCP_VIDEO_EN,              1          },
    {MREG_END_MARKER,            0      }
};

static const reg_t tvregs_1080i[] = {
    {P_VENC_VDAC_SETTING,          0xff,  },
//	{VCLK_HD},
	{P_HHI_VID_CLK_DIV,			 1		},
    {P_HHI_VID_CLK_CNTL,        	 0x0421,},
    {P_ENCP_VIDEO_FILT_CTRL,       0x0052,},
    {P_VENC_DVI_SETTING,           0x2029,},
    {P_ENCP_VIDEO_MAX_PXCNT,       4399,  },
    {P_ENCP_VIDEO_MAX_LNCNT,       1124,  },
    {P_ENCP_VIDEO_HSPULS_BEGIN,    88,    },
    {P_ENCP_VIDEO_HSPULS_END,      264,   },
    {P_ENCP_VIDEO_HSPULS_SWITCH,   88,    },
    {P_ENCP_VIDEO_HAVON_BEGIN,     516,   },
    {P_ENCP_VIDEO_HAVON_END,       4355,  },
    {P_ENCP_VIDEO_HSO_BEGIN,       264,   },
    {P_ENCP_VIDEO_HSO_END,         176,   },
    {P_ENCP_VIDEO_EQPULS_BEGIN,    2288,  },
    {P_ENCP_VIDEO_EQPULS_END,      2464,  },
    {P_ENCP_VIDEO_VSPULS_BEGIN,    440,   },
    {P_ENCP_VIDEO_VSPULS_END,      2200,  },
    {P_ENCP_VIDEO_VSPULS_BLINE,    0,     },
    {P_ENCP_VIDEO_VSPULS_ELINE,    4,     },
    {P_ENCP_VIDEO_EQPULS_BLINE,    0,     },
    {P_ENCP_VIDEO_EQPULS_ELINE,    4,     },
    {P_ENCP_VIDEO_VAVON_BLINE,     20,    },
    {P_ENCP_VIDEO_VAVON_ELINE,     559,   },
    {P_ENCP_VIDEO_VSO_BEGIN,       88,    },
    {P_ENCP_VIDEO_VSO_END,         88,    },
    {P_ENCP_VIDEO_VSO_BLINE,       0,     },
    {P_ENCP_VIDEO_VSO_ELINE,       5,     },
    {P_ENCP_VIDEO_YFP1_HTIME,      516,   },
    {P_ENCP_VIDEO_YFP2_HTIME,      4355,  },
    {P_VENC_VIDEO_PROG_MODE,       0x100, },
    {P_ENCP_VIDEO_OFLD_VOAV_OFST,  0x11   },
    {P_ENCP_VIDEO_MODE,            0x1ffc,},
    {P_ENCP_VIDEO_MODE_ADV,        0x0019,},
    {P_ENCP_VIDEO_SYNC_MODE,       0x207, },
    {P_VENC_SYNC_ROUTE,            0,     },
    {P_VENC_INTCTRL,               0x200, },
    {P_ENCP_VFIFO2VD_CTL,               0,     },
    {P_VENC_VDAC_SETTING,          0,     },
    {P_ENCI_VIDEO_EN,              0,     },
    {P_ENCP_VIDEO_EN,              1,     },
    {MREG_END_MARKER,            0      }
};

static const reg_t tvregs_1080i_50hz[] = {
    {P_VENC_VDAC_SETTING,          0xff,  },
//	{VCLK_HD},
	{P_HHI_VID_CLK_DIV,			 1		},
    {P_HHI_VID_CLK_CNTL,        	 0x0421,},
    {P_ENCP_VIDEO_FILT_CTRL,       0x0052,},

    {P_VENC_DVI_SETTING,           0x202d,},
    {P_ENCP_VIDEO_MAX_PXCNT,       5279,  },
    {P_ENCP_VIDEO_MAX_LNCNT,       1124,  },

    //analog vidoe position in horizontal
    {P_ENCP_VIDEO_HSPULS_BEGIN,    88,    },
    {P_ENCP_VIDEO_HSPULS_END,      264,   },
    {P_ENCP_VIDEO_HSPULS_SWITCH,   88,    },

    //DE position in horizontal
    {P_ENCP_VIDEO_HAVON_BEGIN,     526,   },
    {P_ENCP_VIDEO_HAVON_END,       4365,  },

    //ditital hsync positon in horizontal
    {P_ENCP_VIDEO_HSO_BEGIN,       142,   },
    {P_ENCP_VIDEO_HSO_END,         230,   },

    /* vsync horizontal timing */
    {P_ENCP_VIDEO_EQPULS_BEGIN,    2728,  },
    {P_ENCP_VIDEO_EQPULS_END,      2904,  },
    {P_ENCP_VIDEO_VSPULS_BEGIN,    440,   },
    {P_ENCP_VIDEO_VSPULS_END,      2200,  },

    {P_ENCP_VIDEO_VSPULS_BLINE,    0,     },
    {P_ENCP_VIDEO_VSPULS_ELINE,    4,     },
    {P_ENCP_VIDEO_EQPULS_BLINE,    0,     },
    {P_ENCP_VIDEO_EQPULS_ELINE,    4,     },

    //DE position in vertical
    {P_ENCP_VIDEO_VAVON_BLINE,     20,    },
    {P_ENCP_VIDEO_VAVON_ELINE,     559,   },

    //adjust vsync start point and end point
    {P_ENCP_VIDEO_VSO_BEGIN,       142,    },
    {P_ENCP_VIDEO_VSO_END,         142,    },

    //adjust the vsync start line and end line
    {P_ENCP_VIDEO_VSO_BLINE,       0,     },
    {P_ENCP_VIDEO_VSO_ELINE,       5,     },

    /* filter & misc settings */
    {P_ENCP_VIDEO_YFP1_HTIME,      526,   },
    {P_ENCP_VIDEO_YFP2_HTIME,      4365,  },

    {P_VENC_VIDEO_PROG_MODE,       0x100, },  // Select clk108 as DAC clock, progressive mode
    {P_ENCP_VIDEO_OFLD_VOAV_OFST,  0x11   },//bit[15:12]: Odd field VSO  offset begin,
                                                        //bit[11:8]: Odd field VSO  offset end,
                                                        //bit[7:4]: Odd field VAVON offset begin,
                                                        //bit[3:0]: Odd field VAVON offset end,
    {P_ENCP_VIDEO_MODE,            0x1ffc,},//Enable Hsync and equalization pulse switch in center
    {P_ENCP_VIDEO_MODE_ADV,        0x0019,}, //bit6:swap PbPr; bit4:YPBPR gain as HDTV type;
                                                 //bit3:Data input from VFIFO;bit[2}0]:repreat pixel a time
    {P_ENCP_VIDEO_SYNC_MODE,       0x7, }, //bit[15:8] -- adjust the vsync vertical position
    {P_VENC_SYNC_ROUTE,            0,     },
    {P_VENC_INTCTRL,               0x200, },
    {P_ENCP_VFIFO2VD_CTL,               0,     },
    {P_VENC_VDAC_SETTING,          0,     },
    {P_ENCI_VIDEO_EN,              0,     },
    {P_ENCP_VIDEO_EN,              1,     },
    {MREG_END_MARKER,            0      }
};

static const reg_t tvregs_1080p[] = {
    {P_VENC_VDAC_SETTING,          0xff,  },
//	{VCLK_HD},
#ifndef CONFIG_AM_TV_OUTPUT2
    {P_HHI_VID_CLK_DIV,			 1		},
    {P_HHI_VID_CLK_CNTL,        	 0x0421,},
#endif
    {P_ENCP_VIDEO_FILT_CTRL,       0x1052,},
    {P_VENC_DVI_SETTING,           0x0001,},
    {P_ENCP_VIDEO_MODE,            0x0040,},
    {P_ENCP_VIDEO_MODE_ADV,        0x0018,},
    {P_ENCP_VIDEO_YFP1_HTIME,      140,   },
    {P_ENCP_VIDEO_YFP2_HTIME,      2060,  },
    {P_ENCP_VIDEO_MAX_PXCNT,       2199,  },
    {P_ENCP_VIDEO_HSPULS_BEGIN,    2156,  },//1980
    {P_ENCP_VIDEO_HSPULS_END,      44,    },
    {P_ENCP_VIDEO_HSPULS_SWITCH,   44,    },
    {P_ENCP_VIDEO_VSPULS_BEGIN,    140,   },
    {P_ENCP_VIDEO_VSPULS_END,      2059,  },
    {P_ENCP_VIDEO_VSPULS_BLINE,    0,     },
    {P_ENCP_VIDEO_VSPULS_ELINE,    4,     },//35
    {P_ENCP_VIDEO_HAVON_BEGIN,     148,   },
    {P_ENCP_VIDEO_HAVON_END,       2067,  },
    {P_ENCP_VIDEO_VAVON_BLINE,     41,    },
    {P_ENCP_VIDEO_VAVON_ELINE,     1120,  },
    {P_ENCP_VIDEO_HSO_BEGIN,       44,    },
    {P_ENCP_VIDEO_HSO_END,         2156,  },
    {P_ENCP_VIDEO_VSO_BEGIN,       2100,  },
    {P_ENCP_VIDEO_VSO_END,         2164,  },
    {P_ENCP_VIDEO_VSO_BLINE,       0,     },
    {P_ENCP_VIDEO_VSO_ELINE,       5,     },
    {P_ENCP_VIDEO_MAX_LNCNT,       1124,  },
#ifndef CONFIG_AM_TV_OUTPUT2
    {P_VPU_VIU_VENC_MUX_CTRL,      0x000a,},      //New Add. If not set, when system boots up, switch panel to HDMI 1080P, nothing on TV.
#endif
    {P_VENC_VIDEO_PROG_MODE,       0x100, },
    {P_VENC_SYNC_ROUTE,            0,     },
    {P_VENC_INTCTRL,               0x200, },
    {P_ENCP_VFIFO2VD_CTL,               0,     },
    {P_VENC_VDAC_SETTING,          0,     },
    {P_ENCI_VIDEO_EN,              0,     },
    {P_ENCP_VIDEO_EN,              1,     },
    {MREG_END_MARKER,            0      }
};

static const reg_t tvregs_1080p_50hz[] = {
    {P_VENC_VDAC_SETTING,          0xff,  },
//	{VCLK_HD},
	{P_HHI_VID_CLK_DIV,			 1		},
    {P_HHI_VID_CLK_CNTL,        	 0x0421,},
    {P_ENCP_VIDEO_FILT_CTRL,       0x1052,},

    // bit 13    1          (delayed prog_vs)
    // bit 5:4:  2          (pixel[0])
    // bit 3:    1          invert vsync or not
    // bit 2:    1          invert hsync or not
    // bit1:     1          (select viu sync)
    // bit0:     1          (progressive)
    {P_VENC_DVI_SETTING,           0x000d,},
    {P_ENCP_VIDEO_MAX_PXCNT,       2639,  },
    {P_ENCP_VIDEO_MAX_LNCNT,       1124,  },
    /* horizontal timing settings */
    {P_ENCP_VIDEO_HSPULS_BEGIN,    44,  },//1980
    {P_ENCP_VIDEO_HSPULS_END,      132,    },
    {P_ENCP_VIDEO_HSPULS_SWITCH,   44,    },

    //DE position in horizontal
    {P_ENCP_VIDEO_HAVON_BEGIN,     271,   },
    {P_ENCP_VIDEO_HAVON_END,       2190,  },

    //ditital hsync positon in horizontal
    {P_ENCP_VIDEO_HSO_BEGIN,       79 ,    },
    {P_ENCP_VIDEO_HSO_END,         123,  },

    /* vsync horizontal timing */
    {P_ENCP_VIDEO_VSPULS_BEGIN,    220,   },
    {P_ENCP_VIDEO_VSPULS_END,      2140,  },

    /* vertical timing settings */
    {P_ENCP_VIDEO_VSPULS_BLINE,    0,     },
    {P_ENCP_VIDEO_VSPULS_ELINE,    4,     },//35
    {P_ENCP_VIDEO_EQPULS_BLINE,    0,     },
    {P_ENCP_VIDEO_EQPULS_ELINE,    4,     },//35
    {P_ENCP_VIDEO_VAVON_BLINE,     41,    },
    {P_ENCP_VIDEO_VAVON_ELINE,     1120,  },

    //adjust the hsync & vsync start point and end point
    {P_ENCP_VIDEO_VSO_BEGIN,       79,  },
    {P_ENCP_VIDEO_VSO_END,         79,  },

    //adjust the vsync start line and end line
    {P_ENCP_VIDEO_VSO_BLINE,       0,     },
    {P_ENCP_VIDEO_VSO_ELINE,       5,     },

    {P_ENCP_VIDEO_YFP1_HTIME,      271,   },
    {P_ENCP_VIDEO_YFP2_HTIME,      2190,  },
    {P_VENC_VIDEO_PROG_MODE,       0x100, },
    {P_ENCP_VIDEO_MODE,            0x0040,},
    {P_ENCP_VIDEO_MODE_ADV,        0x0018,},

    {P_ENCP_VIDEO_SYNC_MODE,       0x7, }, //bit[15:8] -- adjust the vsync vertical position

    {P_ENCP_VIDEO_YC_DLY,          0,     },      //Y/Cb/Cr delay

    {P_ENCP_VIDEO_RGB_CTRL, 2,},       // enable sync on B

    {P_VENC_SYNC_ROUTE,            0,     },
    {P_VENC_INTCTRL,               0x200, },
    {P_ENCP_VFIFO2VD_CTL,               0,     },
    {P_VENC_VDAC_SETTING,          0,     },
    {P_ENCI_VIDEO_EN,              0,     },
    {P_ENCP_VIDEO_EN,              1,     },
    {MREG_END_MARKER,            0      }
};


/* The sequence of register tables items must match the enum define in tvmode.h */
static const reg_t *tvregsTab[] = {
    tvregs_480i,
    tvregs_480cvbs,		
    tvregs_480p,
    tvregs_576i,
    tvregs_576cvbs,
    tvregs_576p,
    tvregs_720p,
    tvregs_1080i,       //Adjust tvregs_* sequences and match the enum define in tvmode.h
    tvregs_1080p,
    tvregs_720p_50hz,
    tvregs_1080i_50hz,
    tvregs_1080p_50hz
};

static const tvinfo_t tvinfoTab[] = {
    {.xres =  720, .yres =  480, .id = "480i"},
    {.xres =  720, .yres =  480, .id = "480cvbs"},		
    {.xres =  720, .yres =  480, .id = "480p"},
    {.xres =  720, .yres =  576, .id = "576i"},
    {.xres =  720, .yres =  576, .id = "576cvbs"},
    {.xres =  720, .yres =  576, .id = "576p"},
    {.xres = 1280, .yres =  720, .id = "720p"},
    {.xres = 1920, .yres = 1080, .id = "1080i"},
    {.xres = 1920, .yres = 1080, .id = "1080p"},
    {.xres = 1280, .yres =  720, .id = "720p50hz"},
    {.xres = 1920, .yres = 1080, .id = "1080i50hz"},
    {.xres = 1920, .yres = 1080, .id = "1080p50hz"}
};

static inline void setreg(const reg_t *r)
{
    aml_write_reg32(r->reg, r->val);
    
// For debug only. 
#if 0 
    printk("[0x%x] = 0x%x\n", r->reg, r->val);
#endif
}

#endif /* TVREGS_H */

