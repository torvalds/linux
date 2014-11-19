typedef struct reg_s {
    uint reg;
    uint val;
} reg_t;

static  reg_t hdmi_tvenc_regs_480i[] = {
    /*1st col: recommmended, but eof/sof/vs_lines +/- 1 from spec; 2nd col: from simu */
    {ENCP_VIDEO_MODE,             0      /*0     */  },
    {ENCI_DE_H_BEGIN,             229    /*0xeb  */  },
    {ENCI_DE_H_END,               1669   /*0x68b */  },
    {ENCI_DE_V_BEGIN_EVEN,        18     /*0x11  */  },
    {ENCI_DE_V_END_EVEN,          258    /*0x101 */  },
    {ENCI_DE_V_BEGIN_ODD,         19     /*0x12  */  },
    {ENCI_DE_V_END_ODD,           259    /*0x102 */  },
    {ENCI_DVI_HSO_BEGIN,          1707   /*0x6b1 */  },
    {ENCI_DVI_HSO_END,            115    /*0x79  */  },
    {ENCI_DVI_VSO_BLINE_EVN,      0      /*0x105 */  },
    {ENCI_DVI_VSO_ELINE_EVN,      2      /*0x1   */  },
    {ENCI_DVI_VSO_BEGIN_EVN,      1707   /*0x357 */  },
    {ENCI_DVI_VSO_END_EVN,        1707   /*0x6b1 */  },
    {ENCI_DVI_VSO_BLINE_ODD,      0      /*0x105 */  },
    {ENCI_DVI_VSO_BEGIN_ODD,      849    /*0x6b1 */  },
    {ENCI_DVI_VSO_ELINE_ODD,      3      /*0x2   */  },
    {ENCI_DVI_VSO_END_ODD,        849    /*0x357 */  },
    {VENC_DVI_SETTING,            0x809c /*0x809c*/  },
    {VENC_DVI_SETTING_MORE,       0x0    /*0x0   */  },
    {0,0}
};

static  reg_t hdmi_tvenc_regs_576i[] = {
    {ENCP_VIDEO_MODE,                    0x00004000},
    {ENCI_DE_H_BEGIN,                    0x000000f9},
    {ENCI_DE_H_END,                      0x00000699},
    {ENCI_DE_V_BEGIN_EVEN,               0x00000015},
    {ENCI_DE_V_END_EVEN,                 0x00000135},
    {ENCI_DE_V_BEGIN_ODD,                0x00000016},
    {ENCI_DE_V_END_ODD,                  0x00000136},
    {ENCI_DVI_HSO_BEGIN,                 0x000006b1},
    {ENCI_DVI_HSO_END,                   0x0000006f},
    {ENCI_DVI_VSO_BLINE_EVN,             0x00000137},
    {ENCI_DVI_VSO_ELINE_EVN,             0x00000001},
    {ENCI_DVI_VSO_BEGIN_EVN,             0x00000351},
    {ENCI_DVI_VSO_END_EVN,               0x000006b1},
    {ENCI_DVI_VSO_BLINE_ODD,             0x00000137},
    {ENCI_DVI_VSO_BEGIN_ODD,             0x000006b1},
    {ENCI_DVI_VSO_ELINE_ODD,             0x00000002},
    {ENCI_DVI_VSO_END_ODD,               0x00000351},
    {VENC_DVI_SETTING,                   0x0000809c},
    {VENC_DVI_SETTING_MORE,              0x00000000},
    {0,0}
};

static  reg_t hdmi_tvenc_regs_1080i[] = {
    {ENCP_VIDEO_MODE,                    0x00005ffc},
    {ENCP_DE_H_BEGIN,                    0x00000210},
    {ENCP_DE_H_END,                      0x00001110},
    {ENCP_DE_V_BEGIN_EVEN,               0x00000014},
    {ENCP_DE_V_END_EVEN,                 0x00000230},
    {ENCP_DE_V_BEGIN_ODD,                0x00000247},
    {ENCP_DE_V_END_ODD,                  0x00000463},
    {ENCP_DVI_HSO_BEGIN,                 0x00000090},
    {ENCP_DVI_HSO_END,                   0x000000e8},
    {ENCP_DVI_VSO_BLINE_EVN,             0x00000000},
    {ENCP_DVI_VSO_ELINE_EVN,             0x00000005},
    {ENCP_DVI_VSO_BEGIN_EVN,             0x00000090},
    {ENCP_DVI_VSO_END_EVN,               0x00000090},
    {ENCP_DVI_VSO_BLINE_ODD,             0x00000232},
    {ENCP_DVI_VSO_ELINE_ODD,             0x00000237},
    {ENCP_DVI_VSO_BEGIN_ODD,             0x00000928},
    {ENCP_DVI_VSO_END_ODD,               0x00000928},
    {VENC_DVI_SETTING,                   0x000080ad},
    {VENC_DVI_SETTING_MORE,              0x00000000}, 
    {0,0}
};    

static  reg_t hdmi_tvenc_regs_1080i50[] = {
    {ENCP_VIDEO_MODE,                    0x00005ffc},
    {ENCP_DE_H_BEGIN,                    0x00000210},
    {ENCP_DE_H_END,                      0x00001110},
    {ENCP_DE_V_BEGIN_EVEN,               0x00000014},
    {ENCP_DE_V_END_EVEN,                 0x00000230},
    {ENCP_DE_V_BEGIN_ODD,                0x00000247},
    {ENCP_DE_V_END_ODD,                  0x00000463},
    {ENCP_DVI_HSO_BEGIN,                 0x00000090},
    {ENCP_DVI_HSO_END,                   0x000000e8},
    {ENCP_DVI_VSO_BLINE_EVN,             0x00000000},
    {ENCP_DVI_VSO_ELINE_EVN,             0x00000005},
    {ENCP_DVI_VSO_BEGIN_EVN,             0x00000090},
    {ENCP_DVI_VSO_END_EVN,               0x00000090},
    {ENCP_DVI_VSO_BLINE_ODD,             0x00000232},
    {ENCP_DVI_VSO_ELINE_ODD,             0x00000237},
    {ENCP_DVI_VSO_BEGIN_ODD,             0x00000ae0},
    {ENCP_DVI_VSO_END_ODD,               0x00000ae0},
    {VENC_DVI_SETTING,                   0x000080ad},
    {VENC_DVI_SETTING_MORE,              0x00000000}, 
    {0,0}
};    

static  reg_t hdmi_tvenc_regs_480p[] = {
    {ENCP_VIDEO_MODE,                   /*0x4000 */ 0x00004000},
    {ENCP_DE_H_BEGIN,                   /*0xdc   */ 0x000000d7},
    {ENCP_DE_H_END,                     /*0x67c  */ 0x00000677},
    {ENCP_DE_V_BEGIN_EVEN,              /*0x2a   */ 0x0000002b},
    {ENCP_DE_V_END_EVEN,                /*0x2a   */ 0x0000020b},
    {ENCP_DVI_HSO_BEGIN,                /*0x69c  */ 0x00000697},
    {ENCP_DVI_HSO_END,                  /*0x64   */ 0x0000005f},
    {ENCP_DVI_VSO_BLINE_EVN,            /*0x5    */ 0x00000006},
    {ENCP_DVI_VSO_ELINE_EVN,            /*0xb    */ 0x0000000c},
    {ENCP_DVI_VSO_BEGIN_EVN,            /*0x69c  */ 0x00000697},
    {ENCP_DVI_VSO_END_EVN,              /*0x69c  */ 0x00000697},
    {VENC_DVI_SETTING_MORE,             /*0x0    */ 0x00000000},
    {VENC_DVI_SETTING,                  /*0x80ad */ 0x000080ad},
    {0,0}
};    

static  reg_t hdmi_tvenc_regs_576p[] = {
    {ENCP_VIDEO_MODE,                    0x00004000},
    {ENCP_DE_H_BEGIN,                    0x000000ef},
    {ENCP_DE_H_END,                      0x0000068f},
    {ENCP_DE_V_BEGIN_EVEN,               0x0000002d},
    {ENCP_DE_V_END_EVEN,                 0x0000026d},
    {ENCP_DVI_HSO_BEGIN,                 0x000006a7},
    {ENCP_DVI_HSO_END,                   0x00000067},
    {ENCP_DVI_VSO_BLINE_EVN,             0x00000000},
    {ENCP_DVI_VSO_ELINE_EVN,             0x00000005},
    {ENCP_DVI_VSO_BEGIN_EVN,             0x000006a7},
    {ENCP_DVI_VSO_END_EVN,               0x000006a7},
    {VENC_DVI_SETTING_MORE,              0x00000000},
    {VENC_DVI_SETTING,                   0x000080ad},
    {0,0}
};    

static  reg_t hdmi_tvenc_regs_720p[] = {
    {ENCP_VIDEO_MODE,                    0x00004040},
    {ENCP_DE_H_BEGIN,                    0x0000028a},
    {ENCP_DE_H_END,                      0x00000c8a},
    {ENCP_DE_V_BEGIN_EVEN,               0x0000001d},
    {ENCP_DE_V_END_EVEN,                 0x000002ed},
    {ENCP_DVI_HSO_BEGIN,                 0x00000082},
    {ENCP_DVI_HSO_END,                   0x000000d2},
    {ENCP_DVI_VSO_BLINE_EVN,             0x00000004},
    {ENCP_DVI_VSO_ELINE_EVN,             0x00000009},
    {ENCP_DVI_VSO_BEGIN_EVN,             0x00000082},
    {ENCP_DVI_VSO_END_EVN,               0x00000082},
    {VENC_DVI_SETTING_MORE,              0x00000000},
    {VENC_DVI_SETTING,                   0x000080ad},
    {0,0}
};    

static  reg_t hdmi_tvenc_regs_1080p[] = {
    {ENCP_VIDEO_MODE,                    0x00004040},
    {ENCP_DE_H_BEGIN,                    0x00000112},
    {ENCP_DE_H_END,                      0x00000892},
    {ENCP_DE_V_BEGIN_EVEN,               0x00000029},
    {ENCP_DE_V_END_EVEN,                 0x00000461},
    {ENCP_DVI_HSO_BEGIN,                 0x00000052},
    {ENCP_DVI_HSO_END,                   0x0000007e},
    {ENCP_DVI_VSO_BLINE_EVN,             0x00000000},
    {ENCP_DVI_VSO_ELINE_EVN,             0x00000005},
    {ENCP_DVI_VSO_BEGIN_EVN,             0x00000052},
    {ENCP_DVI_VSO_END_EVN,               0x00000052},
    {VENC_DVI_SETTING_MORE,              0x00000000},
    {VENC_DVI_SETTING,                   0x0000809d},
    {0,0}
};    

static  reg_t hdmi_tvenc_regs_720p50[] = {
    {ENCP_VIDEO_MODE,                    0x00004040},
    {ENCP_DE_H_BEGIN,                    0x0000028a},
    {ENCP_DE_H_END,                      0x00000c8a},
    {ENCP_DE_V_BEGIN_EVEN,               0x0000001d},
    {ENCP_DE_V_END_EVEN,                 0x000002ed},
    {ENCP_DVI_HSO_BEGIN,                 0x00000082},
    {ENCP_DVI_HSO_END,                   0x000000d2},
    {ENCP_DVI_VSO_BLINE_EVN,             0x00000004},
    {ENCP_DVI_VSO_ELINE_EVN,             0x00000009},
    {ENCP_DVI_VSO_BEGIN_EVN,             0x00000082},
    {ENCP_DVI_VSO_END_EVN,               0x00000082},
    {VENC_DVI_SETTING_MORE,              0x00000000},
    {VENC_DVI_SETTING,                   0x000080ad},
    {0,0}
};    

static  reg_t hdmi_tvenc_regs_640x480p60[] = {
    //{ENCP_VIDEO_MODE,                    0x00004040},
    //{ENCP_DE_H_BEGIN,                    0x00000112},
    //{ENCP_DE_H_END,                      0x00000612},
    //{ENCP_DE_V_BEGIN_EVEN,               0x00000029},
    //{ENCP_DE_V_END_EVEN,                 0x00000429},
    //{ENCP_DVI_HSO_BEGIN,                 0x00000052},
    //{ENCP_DVI_HSO_END,                   0x000000c2},
    //{ENCP_DVI_VSO_BLINE_EVN,             0x00000000},
    //{ENCP_DVI_VSO_ELINE_EVN,             0x00000003},
    //{ENCP_DVI_VSO_BEGIN_EVN,             0x00000052},
    //{ENCP_DVI_VSO_END_EVN,               0x00000052},
    //{VENC_DVI_SETTING_MORE,              0x00000000},
    //{VENC_DVI_SETTING,                   0x0000809d},
    {0,0}
};   
 
static  reg_t hdmi_tvenc_regs_1280x1024x60[] = {
    //{ENCP_VIDEO_MODE,                    0x00004040},
    //{ENCP_DE_H_BEGIN,                    0x00000112},
    //{ENCP_DE_H_END,                      0x00000612},
    //{ENCP_DE_V_BEGIN_EVEN,               0x00000029},
    //{ENCP_DE_V_END_EVEN,                 0x00000429},
    //{ENCP_DVI_HSO_BEGIN,                 0x00000052},
    //{ENCP_DVI_HSO_END,                   0x000000c2},
    //{ENCP_DVI_VSO_BLINE_EVN,             0x00000000},
    //{ENCP_DVI_VSO_ELINE_EVN,             0x00000003},
    //{ENCP_DVI_VSO_BEGIN_EVN,             0x00000052},
    //{ENCP_DVI_VSO_END_EVN,               0x00000052},
    //{VENC_DVI_SETTING_MORE,              0x00000000},
    //{VENC_DVI_SETTING,                   0x0000809d},
    {0,0}
};  

static  reg_t hdmi_tvenc_regs_1920x1200[] = {//60hz
    //{ENCP_VIDEO_MODE,                    0x00004040},
    //{ENCP_DE_H_BEGIN,                    0x00000112},
    //{ENCP_DE_H_END,                      0x00000612},
    //{ENCP_DE_V_BEGIN_EVEN,               0x00000029},
    //{ENCP_DE_V_END_EVEN,                 0x00000429},
    //{ENCP_DVI_HSO_BEGIN,                 0x00000052},
    //{ENCP_DVI_HSO_END,                   0x000000c2},
    //{ENCP_DVI_VSO_BLINE_EVN,             0x00000000},
    //{ENCP_DVI_VSO_ELINE_EVN,             0x00000003},
    //{ENCP_DVI_VSO_BEGIN_EVN,             0x00000052},
    //{ENCP_DVI_VSO_END_EVN,               0x00000052},
    //{VENC_DVI_SETTING_MORE,              0x00000000},
    //{VENC_DVI_SETTING,                   0x0000809d},
    {0,0}
};  
static  reg_t hdmi_tvenc_regs_1080p50[] = {
    {ENCP_VIDEO_MODE,                    0x00004040},
    {ENCP_DE_H_BEGIN,                    0x00000112},
    {ENCP_DE_H_END,                      0x00000892},
    {ENCP_DE_V_BEGIN_EVEN,               0x00000029},
    {ENCP_DE_V_END_EVEN,                 0x00000461},
    {ENCP_DVI_HSO_BEGIN,                 0x00000052},
    {ENCP_DVI_HSO_END,                   0x0000007e},
    {ENCP_DVI_VSO_BLINE_EVN,             0x00000000},
    {ENCP_DVI_VSO_ELINE_EVN,             0x00000005},
    {ENCP_DVI_VSO_BEGIN_EVN,             0x00000052},
    {ENCP_DVI_VSO_END_EVN,               0x00000052},
    {VENC_DVI_SETTING_MORE,              0x00000000},
    {VENC_DVI_SETTING,                   0x0000809d},
    {0,0}
};    

typedef struct hdmi_tvenc_config_
{
    int vic;
    reg_t* reg_set;
}hdmi_tvenc_config_t;

static const hdmi_tvenc_config_t hdmi_tvenc_configs[] = {
    {HDMI_640x480p60  ,          hdmi_tvenc_regs_640x480p60},
    {HDMI_480p60,                hdmi_tvenc_regs_480p},
    {HDMI_480p60_16x9,           hdmi_tvenc_regs_480p},
    {HDMI_720p60,                hdmi_tvenc_regs_720p},
    {HDMI_1080i60,               hdmi_tvenc_regs_1080i},
    {HDMI_480i60,                hdmi_tvenc_regs_480i},
    {HDMI_480i60_16x9,           hdmi_tvenc_regs_480i},
    {HDMI_1440x480p60  ,         NULL          },
    {HDMI_1440x480p60_16x9  ,    NULL          },
    {HDMI_1080p60 ,              hdmi_tvenc_regs_1080p},
    {HDMI_576p50,                hdmi_tvenc_regs_576p},
    {HDMI_576p50_16x9,           hdmi_tvenc_regs_576p},
    {HDMI_720p50,                hdmi_tvenc_regs_720p50},
    {HDMI_1280x1024,             hdmi_tvenc_regs_1280x1024x60},
    {HDMI_1920x1200,             hdmi_tvenc_regs_1920x1200},
    {HDMI_1080i50,               hdmi_tvenc_regs_1080i50},
    {HDMI_576i50,                hdmi_tvenc_regs_576i},
    {HDMI_576i50_16x9,           hdmi_tvenc_regs_576i},
    {HDMI_1080p50 ,              hdmi_tvenc_regs_1080p50},
    {HDMI_1080p24,               hdmi_tvenc_regs_1080p},
    {HDMI_1080p25,               hdmi_tvenc_regs_1080p},
    {HDMI_1080p30,               hdmi_tvenc_regs_1080p},
    {HDMI_Unkown,               NULL},
};


