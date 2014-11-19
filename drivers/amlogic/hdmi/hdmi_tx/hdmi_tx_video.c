#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>

#include <linux/amlogic/hdmi_tx/hdmi_info_global.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_module.h>

static unsigned char hdmi_output_rgb = 0;
static void hdmitx_set_spd_info(hdmitx_dev_t* hdmitx_device);
static void hdmi_set_vend_spec_infofram(hdmitx_dev_t* hdmitx_device, HDMI_Video_Codes_t VideoCode);

static Hdmi_tx_video_para_t hdmi_tx_video_params[] =
{
    {
        .VIC            = HDMI_640x480p60,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_4_3,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_480p60,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_4_3,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_480p60_16x9,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_480p60_16x9_rpt,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = HDMI_4_TIMES_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_720p60,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
#ifdef DOUBLE_CLK_720P_1080I
        .repeat_time    = HDMI_2_TIMES_REPEAT,
#else
        .repeat_time    = NO_REPEAT,
#endif
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_1080i60,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
#ifdef DOUBLE_CLK_720P_1080I
        .repeat_time    = HDMI_2_TIMES_REPEAT,
#else
        .repeat_time    = NO_REPEAT,
#endif
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_480i60,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = HDMI_2_TIMES_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_4_3,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_480i60_16x9,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = HDMI_2_TIMES_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_480i60_16x9_rpt,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = HDMI_4_TIMES_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_1440x480p60,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_4_3,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_1080p60,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_576p50,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_4_3,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_576p50_16x9,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_576p50_16x9_rpt,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = HDMI_4_TIMES_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_720p50,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
     { 
        .VIC            = HDMI_1280x1024,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_4_3,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,   
        .sc             = SC_SCALE_HORIZ_VERT,
    },
     { 
        .VIC            = HDMI_1920x1200,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_4_3,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,   
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_1080i50,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_576i50,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = HDMI_2_TIMES_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_4_3,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_576i50_16x9,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = HDMI_2_TIMES_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_576i50_16x9_rpt,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = HDMI_4_TIMES_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU601,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_1080p50,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_1080p24,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_1080p25,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_1080p30,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_4k2k_30,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_4k2k_25,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_4k2k_24,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
    {
        .VIC            = HDMI_4k2k_smpte_24,
        .color_prefer   = COLOR_SPACE_RGB444,
        .color_depth    = COLOR_24BIT,
        .bar_info       = B_BAR_VERT_HORIZ,
        .repeat_time    = NO_REPEAT,
        .aspect_ratio   = TV_ASPECT_RATIO_16_9,
        .cc             = CC_ITU709,
        .ss             = SS_SCAN_UNDER,
        .sc             = SC_SCALE_HORIZ_VERT,
    },
};

static Hdmi_tx_video_para_t *hdmi_get_video_param(HDMI_Video_Codes_t VideoCode)
{
    Hdmi_tx_video_para_t * video_param=NULL;
    int  i;
    int count=ARRAY_SIZE(hdmi_tx_video_params);
    for(i=0;i<count;i++){
        if(hdmi_tx_video_params[i].VIC == VideoCode){
            break;
        }
    }
    if(i<count){
        video_param = &(hdmi_tx_video_params[i]);
    }
    return video_param;
}

static void hdmi_tx_construct_avi_packet(Hdmi_tx_video_para_t *video_param, char* AVI_DB)
{
    unsigned char color, bar_info, aspect_ratio, cc, ss, sc, ec = 0;
    ss = video_param->ss;
    bar_info = video_param->bar_info;
    if(video_param->color == COLOR_SPACE_YUV444){
        color = 2;
    }
    else if(video_param->color == COLOR_SPACE_YUV422){
        color = 1;
    }
    else{ //(video_param->color == COLOR_SPACE_RGB444)
        color = 0;
    }
    AVI_DB[0] = (ss) | (bar_info << 2) | (1<<4) | (color << 5);
    //AVI_DB[0] = (1<<4) | (color << 5);

    aspect_ratio = video_param->aspect_ratio;
    cc = video_param->cc;
//HDMI CT 7-24
    //AVI_DB[1] = (aspect_ratio) | (aspect_ratio << 4) | (cc << 6);
    AVI_DB[1] = 8 | (aspect_ratio << 4) | (cc << 6);

    sc = video_param->sc;
    if(video_param->cc == CC_ITU601)
        ec = 0;
    if(video_param->cc == CC_ITU709)
        ec = 1;    // according to CEA-861-D, all other values are reserved
    AVI_DB[2] = (sc) | (ec << 4);
    //AVI_DB[2] = 0;

    AVI_DB[3] = video_param->VIC;
    if((video_param->VIC == HDMI_4k2k_30) || (video_param->VIC == HDMI_4k2k_25) || (video_param->VIC == HDMI_4k2k_24) || (video_param->VIC == HDMI_4k2k_smpte_24))
        AVI_DB[3] = 0;      // HDMI Spec V1.4b P151

    AVI_DB[4] = video_param->repeat_time;
}

/************************************
*    hdmitx protocol level interface
*************************************/

void hdmitx_init_parameters(HDMI_TX_INFO_t *info)
{
    memset(info, 0, sizeof(HDMI_TX_INFO_t));

    info->video_out_changing_flag = 1;

    info->audio_flag = 1;
    info->audio_info.type = CT_REFER_TO_STREAM;
    info->audio_info.format = AF_I2S;
    info->audio_info.fs = FS_44K1;
    info->audio_info.ss = SS_16BITS;
    info->audio_info.channels = CC_2CH;
    info->audio_info.audio_mclk = MCLK_256_Fs;
    info->audio_out_changing_flag = 1;

    info->auto_hdcp_ri_flag = 1;     // If == 1, turn on Auto Ri Checking
    info->hw_sha_calculator_flag = 1;    // If  == 1, use the HW SHA calculator, otherwise, use SW SHA calculator

}

//HDMI Identifier = 0x000c03
//If not, treated as a DVI Device
static int is_dvi_device(rx_cap_t* pRXCap)
{
    hdmitx_dev_t *hdmitx_device = container_of(pRXCap, struct hdmi_tx_dev_s, RXCap);

#ifndef CONFIG_AML_HDMI_TX_CTS_DVI
    hdmi_print(IMP, SYS "fixed HDMI mode output\n");
    return 0;
#endif
    if(hdmitx_device->tv_no_edid)
        return 0;

    if(pRXCap->IEEEOUI != 0x000c03)
        return 1;
    else
        return 0;
}

void hdmitx_output_rgb(void)
{
    hdmi_output_rgb = 1;
}

int hdmitx_set_display(hdmitx_dev_t* hdmitx_device, HDMI_Video_Codes_t VideoCode)
{
    Hdmi_tx_video_para_t *param;
    HDMI_Video_Codes_t vic;
    int i,ret=-1;
    unsigned char AVI_DB[32];
    unsigned char AVI_HB[32];
    AVI_HB[0] = TYPE_AVI_INFOFRAMES ;
    AVI_HB[1] = AVI_INFOFRAMES_VERSION ;
    AVI_HB[2] = AVI_INFOFRAMES_LENGTH ;
    for(i=0;i<32;i++){
        AVI_DB[i]=0;
    }

    vic = hdmitx_device->HWOp.GetState(hdmitx_device, STAT_VIDEO_VIC, 0);
    hdmi_print(IMP, SYS "already init VIC = %d  Now VIC = %d\n", vic, VideoCode);
    if((vic != HDMI_Unkown) && (vic == VideoCode)) {
        hdmitx_device->cur_VIC = vic;
        return 1;;
    }

    param = hdmi_get_video_param(VideoCode);
    hdmitx_device->cur_video_param = param;
    if(param){
        param->color = param->color_prefer;
        if(hdmi_output_rgb){
            param->color = COLOR_SPACE_RGB444;
        }
        else{
//HDMI CT 7-24 Pixel Encoding - YCbCr to YCbCr Sink
            switch(hdmitx_device->RXCap.native_Mode & 0x30)
            {
                case 0x20:    //bit5==1, then support YCBCR444 + RGB
                case 0x30:
                    param->color = COLOR_SPACE_YUV444;
                    break;
                case 0x10:    //bit4==1, then support YCBCR422 + RGB
                    param->color = COLOR_SPACE_YUV422;
                    break;
                default:
                    param->color = COLOR_SPACE_RGB444;
            }
        }
        if(hdmitx_device->HWOp.SetDispMode(hdmitx_device, param)>=0){
//HDMI CT 7-33 DVI Sink, no HDMI VSDB nor any other VSDB, No GB or DI expected
//TMDS_MODE[hdmi_config]
//0: DVI Mode       1: HDMI Mode
            //if(hdmitx_device->hdmi_info.output_state==CABLE_PLUGIN_DVI_OUT)
            if(is_dvi_device(&hdmitx_device->RXCap)) {
                hdmi_print(1,"Sink is DVI device\n");
                hdmitx_device->HWOp.CntlConfig(hdmitx_device, CONF_HDMI_DVI_MODE, DVI_MODE);        //todo ColorFormat
            }
            else {
                hdmi_print(1,"Sink is HDMI device\n");
                hdmitx_device->HWOp.CntlConfig(hdmitx_device, CONF_HDMI_DVI_MODE, HDMI_MODE);
            }
//check system status by reading EDID_STATUS
            switch(hdmitx_device->HWOp.CntlConfig(hdmitx_device, CONF_SYSTEM_ST, 0))
            {
                case 0:
                    hdmi_print(1,"No sink attached\n");
                    break;
                case 1:
                    hdmi_print(1,"Source reading EDID\n");
                    break;
                case 2:
                    hdmi_print(1,"Source in DVI Mode\n");
                    break;
                case 3:
                    hdmi_print(1,"Source in HDMI Mode\n");
                    break;
                default:
                    hdmi_print(1,"EDID Status error\n");
            }

            hdmi_tx_construct_avi_packet(param, (char*)AVI_DB);

            if((VideoCode == HDMI_4k2k_30) || (VideoCode == HDMI_4k2k_25) || (VideoCode == HDMI_4k2k_24) || (VideoCode == HDMI_4k2k_smpte_24)) {
                hdmi_set_vend_spec_infofram(hdmitx_device, VideoCode);
            }
            else {
                hdmi_set_vend_spec_infofram(hdmitx_device, 0);
            }

            hdmitx_device->HWOp.SetPacket(HDMI_PACKET_AVI, AVI_DB, AVI_HB);
            ret = 0;
        }
    }
    else{
        if(hdmitx_device->HWOp.SetDispMode) {
            hdmitx_device->HWOp.SetDispMode(hdmitx_device, NULL); //disable HDMI
        }
    }
    hdmitx_set_spd_info(hdmitx_device);
    return ret;
}

static void hdmi_set_vend_spec_infofram(hdmitx_dev_t* hdmitx_device, HDMI_Video_Codes_t VideoCode)
{
    int i;
    unsigned char VEN_DB[6];
    unsigned char VEN_HB[3];
    VEN_HB[0] = 0x81;
    VEN_HB[1] = 0x01;
    VEN_HB[2] = 0x6;

    for(i = 0; i < 0x6; i++){
        VEN_DB[i] = 0;
    }
    VEN_DB[0] = 0x03;
    VEN_DB[1] = 0x0c;
    VEN_DB[2] = 0x00;

    VEN_DB[3] = 0x20;         // 4k x 2k  Spec P156
    if(VideoCode == 0) {       // For non-4kx2k mode setting
        hdmitx_device->HWOp.SetPacket(HDMI_PACKET_VEND, NULL, VEN_HB);
        return ;
    }
    if(VideoCode == HDMI_4k2k_30)
        VEN_DB[4] = 0x1;
    else if(VideoCode == HDMI_4k2k_25)
        VEN_DB[4] = 0x2;
    else if(VideoCode == HDMI_4k2k_24)
        VEN_DB[4] = 0x3;
    else if(VideoCode == HDMI_4k2k_smpte_24)
        VEN_DB[4] = 0x4;
    else {
        // nothing
    }
    hdmitx_device->HWOp.SetPacket(HDMI_PACKET_VEND, VEN_DB, VEN_HB);
}

int hdmi_set_3d(hdmitx_dev_t* hdmitx_device, int type, unsigned int param)
{
    int i;
    unsigned char VEN_DB[6];
    unsigned char VEN_HB[3];
    VEN_HB[0] = 0x81 ;
    VEN_HB[1] = 0x01 ;
    VEN_HB[2] = 0x6 ;
    if(type==0xf){
        hdmitx_device->HWOp.SetPacket(HDMI_PACKET_VEND, NULL, VEN_HB);
    }
    else{
        for(i=0;i<0x6;i++){
            VEN_DB[i]=0;
        }
        VEN_DB[0]=0x03;
        VEN_DB[1]=0x0c;
        VEN_DB[2]=0x00;

        VEN_DB[3]=0x40;
        VEN_DB[4]=type<<4;
        VEN_DB[5]=param<<4;
        hdmitx_device->HWOp.SetPacket(HDMI_PACKET_VEND, VEN_DB, VEN_HB);
    }
    return 0;

}

// Set Source Product Descriptor InfoFrame
static void hdmitx_set_spd_info(hdmitx_dev_t* hdmitx_device)
{
    unsigned char SPD_DB[25] = {0x00};
    unsigned char SPD_HB[3] = {0x83, 0x1, 0x19};
    unsigned int len = 0;
    struct vendor_info_data *vend_data;
    if(hdmitx_device->config_data.vend_data) {
        vend_data = hdmitx_device->config_data.vend_data;
    }
    else {
        hdmi_print(INF, SYS "packet: can\'t get vendor data\n");
        return;
    }
    if(vend_data->vendor_name) {
        len = strlen(vend_data->vendor_name);
        strncpy(&SPD_DB[0], vend_data->vendor_name, (len > 8) ? 8 : len);
    }
    if(vend_data->product_desc) {
        len = strlen(vend_data->product_desc);
        strncpy(&SPD_DB[8], vend_data->product_desc, (len > 16) ? 16 : len);
    }
    hdmitx_device->HWOp.SetPacket(HDMI_SOURCE_DESCRIPTION, SPD_DB, SPD_HB);
}

