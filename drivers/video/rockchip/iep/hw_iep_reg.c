/* 
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include "hw_iep_reg.h"
#include "iep_api.h"
#include "iep.h"
#include "hw_iep_config_addr.h"

static void iep_config_src_size(IEP_MSG *iep_msg)
{
    IEP_REGB_SRC_IMG_WIDTH(iep_msg->base, iep_msg->src.act_w - 1);
    IEP_REGB_SRC_IMG_HEIGHT(iep_msg->base, iep_msg->src.act_h - 1);
#ifdef IEP_PRINT_INFO
    IEP_DBG(" //==source image size config===================//\n\n");
    IEP_DBG("sw_src_img_height          = %d;//source image height \n", iep_msg->src.act_h - 1);
    IEP_DBG("sw_src_img_width           = %d;//source image width \n\n", iep_msg->src.act_w - 1);
#endif
}

static void iep_config_dst_size(IEP_MSG *iep_msg)
{
    IEP_REGB_DST_IMG_WIDTH(iep_msg->base, iep_msg->dst.act_w - 1);
    IEP_REGB_DST_IMG_HEIGHT(iep_msg->base, iep_msg->dst.act_h - 1);
#ifdef IEP_PRINT_INFO
    IEP_DBG(" //==destination image size config===================//\n\n");
    IEP_DBG("sw_dst_img_height          = %d;//source image height \n", iep_msg->dst.act_h - 1);
    IEP_DBG("sw_dst_img_width           = %d;//source image width \n", iep_msg->dst.act_w - 1);
#endif
}

static void iep_config_dst_width_tile(IEP_MSG *iep_msg)
{
//	IEP_REGB_DST_IMG_WIDTH_TILE0();
//	IEP_REGB_DST_IMG_WIDTH_TILE1();
//	IEP_REGB_DST_IMG_WIDTH_TILE2();
//	IEP_REGB_DST_IMG_WIDTH_TILE3();
#ifdef IEP_PRINT_INFO
    IEP_DBG("sw_dst_width_tile0         = 0;\n");
    IEP_DBG("sw_dst_width_tile1         = 0;\n");
    IEP_DBG("sw_dst_width_tile2         = 0;\n");
    IEP_DBG("sw_dst_width_tile3         = 0;\n\n");
#endif
}

static void iep_config_dst_fmt(IEP_MSG *iep_msg)
{
    unsigned int dst_fmt = 0;
    unsigned int dst_rgb_swap = 0;
    unsigned int dst_yuv_swap = 0;
    switch (iep_msg->dst.format) {
    case IEP_FORMAT_ARGB_8888 :
        IEP_REGB_DST_FMT(iep_msg->base, 0);
        IEP_REGB_DST_RGB_SWAP(iep_msg->base, 0);
        dst_fmt = 0;
        dst_rgb_swap = 0;
        dst_yuv_swap = 0;
        break;
    case IEP_FORMAT_ABGR_8888 :
        IEP_REGB_DST_FMT(iep_msg->base, 0);
        IEP_REGB_DST_RGB_SWAP(iep_msg->base, 1);
        dst_fmt = 0;
        dst_rgb_swap = 1;
        dst_yuv_swap = 0;
        break;
    case IEP_FORMAT_RGBA_8888 :
        IEP_REGB_DST_FMT(iep_msg->base, 0);
        IEP_REGB_DST_RGB_SWAP(iep_msg->base, 2);
        dst_fmt = 0;
        dst_rgb_swap = 2;
        dst_yuv_swap = 0;
        break;
    case IEP_FORMAT_BGRA_8888 :
        IEP_REGB_DST_FMT(iep_msg->base, 0);
        IEP_REGB_DST_RGB_SWAP(iep_msg->base, 3);
        dst_fmt = 0;
        dst_rgb_swap = 3;
        dst_yuv_swap = 0;
        break;
    case IEP_FORMAT_RGB_565 :
        IEP_REGB_DST_FMT(iep_msg->base, 1);
        IEP_REGB_DST_RGB_SWAP(iep_msg->base, 0);
        dst_fmt = 1;
        dst_rgb_swap = 0;
        dst_yuv_swap = 0;
        break;
    case IEP_FORMAT_BGR_565 :
        IEP_REGB_DST_FMT(iep_msg->base, 1);
        IEP_REGB_DST_RGB_SWAP(iep_msg->base, 1);
        dst_fmt = 1;
        dst_rgb_swap = 1;
        dst_yuv_swap = 0;
        break;
    case IEP_FORMAT_YCbCr_422_SP :
        IEP_REGB_DST_FMT(iep_msg->base, 2);
        IEP_REGB_DST_YUV_SWAP(iep_msg->base, 0);
        dst_fmt = 2;
        dst_yuv_swap = 0;
        break;
    case IEP_FORMAT_YCbCr_422_P :
        IEP_REGB_DST_FMT(iep_msg->base, 2);
        IEP_REGB_DST_YUV_SWAP(iep_msg->base, 2);
        dst_fmt = 2;
        dst_yuv_swap = 2;
        break;
    case IEP_FORMAT_YCbCr_420_SP :
        IEP_REGB_DST_FMT(iep_msg->base, 3);
        IEP_REGB_DST_YUV_SWAP(iep_msg->base, 0);
        dst_fmt = 3;
        dst_yuv_swap = 0;
        break;
    case IEP_FORMAT_YCbCr_420_P :
        IEP_REGB_DST_FMT(iep_msg->base, 3);
        IEP_REGB_DST_YUV_SWAP(iep_msg->base, 2);
        dst_fmt = 3;
        dst_yuv_swap = 2;
        break;
    case IEP_FORMAT_YCrCb_422_SP :
        IEP_REGB_DST_FMT(iep_msg->base, 2);
        IEP_REGB_DST_YUV_SWAP(iep_msg->base, 1);
        dst_fmt = 2;
        dst_yuv_swap = 1;
        break;
    case IEP_FORMAT_YCrCb_422_P :
        IEP_REGB_DST_FMT(iep_msg->base, 2);
        IEP_REGB_DST_YUV_SWAP(iep_msg->base, 2);
        dst_fmt = 2;
        dst_yuv_swap = 2;
        break;
    case IEP_FORMAT_YCrCb_420_SP :
        IEP_REGB_DST_FMT(iep_msg->base, 3);
        IEP_REGB_DST_YUV_SWAP(iep_msg->base, 1);
        dst_fmt = 3;
        dst_yuv_swap = 1;
        break;
    case IEP_FORMAT_YCrCb_420_P :
        IEP_REGB_DST_FMT(iep_msg->base, 3);
        IEP_REGB_DST_YUV_SWAP(iep_msg->base, 2);
        dst_fmt = 3;
        dst_yuv_swap = 2;
        break;
    default:
        break;
    }
#ifdef IEP_PRINT_INFO
    IEP_DBG(" //==destination data format config============//\n\n");
    IEP_DBG("sw_dst_yuv_swap            = %d;//0:sp uv; 1:sp vu; 2:p ; 3:p;\n", dst_yuv_swap);
    IEP_DBG("sw_dst_rgb_swap            = %d;//if ARGB 0:argb; 1,abgr; 2:rgba; 3:bgra; if rgb565: 0,2:rgb; 1,3:bgr;\n", dst_rgb_swap);
    IEP_DBG("sw_dst_fmt                 = %d;//0:argb; 1:rgb565; 2:yuv422; 3:yuv420;\n\n", dst_fmt);
#endif
}

static void iep_config_src_fmt(IEP_MSG *iep_msg)
{
    unsigned int src_fmt = 0;
    unsigned int src_rgb_swap = 0;
    unsigned int src_yuv_swap = 0;
    switch (iep_msg->src.format) {
    case IEP_FORMAT_ARGB_8888 :
        IEP_REGB_SRC_FMT(iep_msg->base, 0);
        IEP_REGB_SRC_RGB_SWAP(iep_msg->base, 0);
        src_fmt = 0;
        src_rgb_swap = 0;
        break;
    case IEP_FORMAT_ABGR_8888 :
        IEP_REGB_SRC_FMT(iep_msg->base, 0);
        IEP_REGB_SRC_RGB_SWAP(iep_msg->base, 1);
        src_fmt = 0;
        src_rgb_swap = 1;
        break;
    case IEP_FORMAT_RGBA_8888 :
        IEP_REGB_SRC_FMT(iep_msg->base, 0);
        IEP_REGB_SRC_RGB_SWAP(iep_msg->base, 2);
        src_fmt = 0;
        src_rgb_swap = 2;
        break;
    case IEP_FORMAT_BGRA_8888 :
        IEP_REGB_SRC_FMT(iep_msg->base, 0);
        IEP_REGB_SRC_RGB_SWAP(iep_msg->base, 3);
        src_fmt = 0;
        src_rgb_swap = 3;
        break;
    case IEP_FORMAT_RGB_565 :
        IEP_REGB_SRC_FMT(iep_msg->base, 1);
        IEP_REGB_SRC_RGB_SWAP(iep_msg->base, 0);
        src_fmt = 1;
        src_rgb_swap = 0;
        break;
    case IEP_FORMAT_BGR_565 :
        IEP_REGB_SRC_FMT(iep_msg->base, 1);
        IEP_REGB_SRC_RGB_SWAP(iep_msg->base, 1);
        src_fmt = 1;
        src_rgb_swap = 1;
        break;
    case IEP_FORMAT_YCbCr_422_SP :
        IEP_REGB_SRC_FMT(iep_msg->base, 2);
        IEP_REGB_SRC_YUV_SWAP(iep_msg->base, 0);
        src_fmt = 2;
        src_yuv_swap = 0;
        break;
    case IEP_FORMAT_YCbCr_422_P :
        IEP_REGB_SRC_FMT(iep_msg->base, 2);
        IEP_REGB_SRC_YUV_SWAP(iep_msg->base, 2);
        src_fmt = 2;
        src_yuv_swap = 2;
        break;
    case IEP_FORMAT_YCbCr_420_SP :
        IEP_REGB_SRC_FMT(iep_msg->base, 3);
        IEP_REGB_SRC_YUV_SWAP(iep_msg->base, 0);
        src_fmt = 3;
        src_yuv_swap = 0;
        break;
    case IEP_FORMAT_YCbCr_420_P :
        IEP_REGB_SRC_FMT(iep_msg->base, 3);
        IEP_REGB_SRC_YUV_SWAP(iep_msg->base, 2);
        src_fmt = 3;
        src_yuv_swap = 2;
        break;
    case IEP_FORMAT_YCrCb_422_SP :
        IEP_REGB_SRC_FMT(iep_msg->base, 2);
        IEP_REGB_SRC_YUV_SWAP(iep_msg->base, 1);
        src_fmt = 2;
        src_yuv_swap = 1;
        break;
    case IEP_FORMAT_YCrCb_422_P :
        IEP_REGB_SRC_FMT(iep_msg->base, 2);
        IEP_REGB_SRC_YUV_SWAP(iep_msg->base, 2);
        src_fmt = 2;
        src_yuv_swap = 2;
        break;
    case IEP_FORMAT_YCrCb_420_SP :
        IEP_REGB_SRC_FMT(iep_msg->base, 3);
        IEP_REGB_SRC_YUV_SWAP(iep_msg->base, 1);
        src_fmt = 3;
        src_yuv_swap = 1;
        break;
    case IEP_FORMAT_YCrCb_420_P :
        IEP_REGB_SRC_FMT(iep_msg->base, 3);
        IEP_REGB_SRC_YUV_SWAP(iep_msg->base, 2);
        src_fmt = 3;
        src_yuv_swap = 2;
        break;
    default:
        break;
    }
#ifdef IEP_PRINT_INFO
    IEP_DBG(" //==source data format config=================//\n\n");
    IEP_DBG("sw_src_yuv_swap            = %d;//0:sp uv; 1:sp vu; 2:p ; 3:p;\n", src_yuv_swap);
    IEP_DBG("sw_src_rgb_swap            = %d;//if ARGB 0:argb; 1,abgr; 2:rgba; 3:bgra; if rgb565: 0,2:rgb; 1,3:bgr;\n", src_rgb_swap);
    IEP_DBG("sw_src_fmt                 = %d;//0:argb; 1:rgb565; 2:yuv422; 3:yuv420;\n\n", src_fmt);
#endif
}

static void iep_config_scl(IEP_MSG *iep_msg)
{
    int scl_en;
    int scl_sel;
    //int vrt_fct;
    //int hrz_fct;

    unsigned int src_height, src_width, dst_height, dst_width;

    int div_height_dst_src;
    int div_width_dst_src;

    src_height = iep_msg->src.act_h - 1;
    src_width = iep_msg->src.act_w - 1;
    dst_height = iep_msg->dst.act_h - 1;
    dst_width = iep_msg->dst.act_w - 1;

    if ((iep_msg->src.act_w == iep_msg->dst.act_w) && (iep_msg->src.act_h == iep_msg->dst.act_h)) scl_en = 0;
    else scl_en = 1;

    if      ((iep_msg->src.act_w >= iep_msg->dst.act_w) && (iep_msg->src.act_h >= iep_msg->dst.act_h))		scl_sel = 0;
    else if ((iep_msg->src.act_w >= iep_msg->dst.act_w) && (iep_msg->src.act_h <= iep_msg->dst.act_h))		scl_sel = 1;
    else if ((iep_msg->src.act_w <= iep_msg->dst.act_w) && (iep_msg->src.act_h >= iep_msg->dst.act_h))		scl_sel = 2;
    else	scl_sel = 3;

    //for vrt_fct
    if ((scl_sel == 1) || (scl_sel == 3)) {
        div_height_dst_src = src_height * 65536 / dst_height;
    } else {
        div_height_dst_src = (dst_height + 1) * 65536 / (src_height + 1);
        if ((div_height_dst_src * (src_height + 1)) < ((dst_height + 1) * 65536)) div_height_dst_src = div_height_dst_src + 1;
    }

    if (div_height_dst_src == 65536) div_height_dst_src = 0;

    //for hrz_fct
    if ((scl_sel == 2) || (scl_sel == 3)) {
        div_width_dst_src = src_width * 65536 / dst_width;
    } else {
        div_width_dst_src = (dst_width + 1) * 65536 / (src_width + 1);
        if ((div_width_dst_src * (src_width + 1)) < ((dst_width + 1) * 65536)) div_width_dst_src = div_width_dst_src + 1;
    }

    if (div_width_dst_src == 65536) div_width_dst_src = 0;


    IEP_REGB_SCL_EN(iep_msg->base, scl_en);

    if (scl_en == 1) {
        IEP_REGB_SCL_SEL(iep_msg->base, scl_sel);
        IEP_REGB_SCL_UP_COE_SEL(iep_msg->base, iep_msg->scale_up_mode);
        IEP_REGB_SCL_VRT_FCT(iep_msg->base, div_height_dst_src);
        IEP_REGB_SCL_HRZ_FCT(iep_msg->base, div_width_dst_src);
    }
#ifdef IEP_PRINT_INFO
    IEP_DBG(" //==scaling config============================//\n\n");
    IEP_DBG("sw_scl_en                  = %d;//0:disable; 1:enable;\n", scl_en);
    IEP_DBG("sw_scl_sel                 = %d;//0:hrz down & vrt down;  1:hrz down & vrt up; 2:hrz up & vrt down;  3:hrz up & vrt up;\n", scl_sel);
    IEP_DBG("sw_scl_up_coe_sel          = %d;//select four groups of up scaling coefficient\n", iep_msg->scale_up_mode);
    IEP_DBG("sw_scl_vrt_fct             = %d;//if up-scaling,equal to floor(src_img_height/dst_image_height)*2^16; if down-scaling,equal to ceiling(dst_image_height/src_image_height)*2^16;\n", div_height_dst_src);
    IEP_DBG("sw_scl_hrz_fct             = %d;//if up-scaling,equal to floor(src_img_widht/dst_image_width)*2^16;   if down-scaling,equal to ceiling(dst_image_width/src_image_width)*2^16  ; \n\n", div_width_dst_src);
#endif
}

static void iep_config_cg_order(IEP_MSG *iep_msg)
{
    IEP_REGB_CON_GAM_ORDER(iep_msg->base, iep_msg->rgb_contrast_enhance_mode);
#ifdef IEP_PRINT_INFO
    IEP_DBG(" //==rgb enhancement & denoise config==========//\n\n");
    IEP_DBG("sw_con_gam_order           = %d;//0:CG(contrast/gamma operation)prior to DDE(de-noise/detail/edge enhance);  1:DDE prior to CG;\n", iep_msg->rgb_contrast_enhance_mode);
#endif
}

static void iep_config_cg(IEP_MSG *iep_msg)
{
    unsigned i;
    unsigned int cg_conf_addr;

    IEP_REGB_RGB_CON_GAM_EN(iep_msg->base, iep_msg->rgb_cg_en);

    if (iep_msg->rgb_cg_en) {
        cg_conf_addr = rIEP_CG_TAB_ADDR;

        for (i=0; i<192; i++) {
            WriteReg32(iep_msg->base, cg_conf_addr, iep_msg->cg_tab[i]);
            cg_conf_addr += 0x04;
        }
    }

#ifdef IEP_PRINT_INFO
    IEP_DBG("sw_rgb_con_gam_en          = 0;//0:contrast & gamma disable; 1:enable;\n", iep_msg->rgb_cg_en);
#endif
}

static void iep_config_dde(IEP_MSG *iep_msg)
{
    IEP_REGB_RGB_ENH_SEL(iep_msg->base, iep_msg->rgb_enhance_mode);
    IEP_REGB_ENH_THRESHOLD(iep_msg->base, iep_msg->enh_threshold);
	IEP_REGB_ENH_ALPHA(iep_msg->base, iep_msg->enh_alpha);
	IEP_REGB_ENH_RADIUS(iep_msg->base, iep_msg->enh_radius);
#ifdef IEP_PRINT_INFO
    IEP_DBG("sw_rgb_enh_sel             = %d;//0:no operation; 1:de-noise; 2:detail enhance; 3:edge enhance;\n", iep_msg->rgb_enhance_mode);
#endif

}

static void iep_config_color_enh(IEP_MSG *iep_msg)
{
    IEP_REGB_RGB_COLOR_ENH_EN(iep_msg->base, iep_msg->rgb_color_enhance_en);
    IEP_REGB_ENH_C_COE(iep_msg->base, iep_msg->rgb_enh_coe);
#ifdef IEP_PRINT_INFO
    IEP_DBG("sw_rgb_color_enh_en        = %d;//0:color enhance disable; 1:enable;\n\n", iep_msg->rgb_color_enhance_en);
#endif
}

static void iep_config_yuv_dns(IEP_MSG *iep_msg)
{
    IEP_REGB_YUV_DNS_EN(iep_msg->base, iep_msg->yuv_3D_denoise_en);
    IEP_REGB_YUV_DNS_LUMA_SPAT_SEL(iep_msg->base, 0);
    IEP_REGB_YUV_DNS_LUMA_TEMP_SEL(iep_msg->base, 1);
    IEP_REGB_YUV_DNS_CHROMA_SPAT_SEL(iep_msg->base, 2);
    IEP_REGB_YUV_DNS_CHROMA_TEMP_SEL(iep_msg->base, 3);
#ifdef IEP_PRINT_INFO
    IEP_DBG("//==yuv denoise config========================// \n\n");
    IEP_DBG("sw_yuv_dns_en              = %d;//0:yuv 3d denoise disable; 1:enable\n\n", iep_msg->yuv_3D_denoise_en);
#endif
}


static void iep_config_dil(IEP_MSG *iep_msg)
{
    //unsigned int value_tab0, value_tab1, value_tab2, value_tab3, value_tab4, value_tab5, value_tab6, value_tab7;
    int dein_mode;
    //double tab[256] =
    //{
    //	1.0,1.0,1.0,1.0,
    //  1.000000,0.996856,0.987464,0.971942,
    //  0.950484,0.923362,0.890916,0.853553,
    //  0.811745,0.766016,0.716942,0.665140,
    //  0.611260,0.555982,0.500000,0.444018,
    //  0.388740,0.334860,0.283058,0.233984,
    //  0.188255,0.146447,0.109084,0.076638,
    //  0.049516,0.028058,0.012536,0.003144
    //};
    switch (iep_msg->dein_mode) {
    case IEP_DEINTERLACE_MODE_DISABLE:
        dein_mode = dein_mode_bypass_dis;
        break;
    case IEP_DEINTERLACE_MODE_I2O1:
        dein_mode = iep_msg->field_order == FIELD_ORDER_TOP_FIRST ? dein_mode_I2O1T : dein_mode_I2O1B;
        break;
    case IEP_DEINTERLACE_MODE_I4O1:
#if 1
        dein_mode = iep_msg->field_order == FIELD_ORDER_TOP_FIRST ? dein_mode_I4O1B : dein_mode_I4O1T;
#else
        dein_mode = iep_msg->field_order == FIELD_ORDER_TOP_FIRST ? dein_mode_I4O1T : dein_mode_I4O1B;
#endif
        break;
    case IEP_DEINTERLACE_MODE_I4O2:
        dein_mode = dein_mode_I4O2;
        break;
    case IEP_DEINTERLACE_MODE_BYPASS:
        dein_mode = dein_mode_bypass;
        break;
    default:
        IEP_ERR("unknown deinterlace mode, set deinterlace mode (bypass)\n");
        dein_mode = dein_mode_bypass;
    }

    IEP_REGB_DIL_MODE(iep_msg->base, dein_mode);
    //hf
    IEP_REGB_DIL_HF_EN(iep_msg->base, iep_msg->dein_high_fre_en);
    if (iep_msg->dein_high_fre_en == 1) IEP_REGB_DIL_HF_FCT(iep_msg->base, iep_msg->dein_high_fre_fct);
    //ei
    IEP_REGB_DIL_EI_MODE(iep_msg->base, iep_msg->dein_ei_mode);
    IEP_REGB_DIL_EI_SMOOTH(iep_msg->base, iep_msg->dein_ei_smooth);
    IEP_REGB_DIL_EI_SEL(iep_msg->base, iep_msg->dein_ei_sel);
    if (iep_msg->dein_ei_sel == 0) IEP_REGB_DIL_EI_RADIUS(iep_msg->base, iep_msg->dein_ei_radius);

    /*
    value_tab0=(64<<24)+(64<<16)+(64<<8)+64;
    IEP_REGB_DIL_MTN_TAB0(value_tab0);
    
    value_tab1=(62<<24)+(63<<16)+(63<<8)+64;
    IEP_REGB_DIL_MTN_TAB1(value_tab1);
    
    value_tab2=(54<<24)+(57<<16)+(59<<8)+60;
    IEP_REGB_DIL_MTN_TAB2(value_tab2);
    
    value_tab3=(42<<24)+(45<<16)+(49<<8)+51;
    IEP_REGB_DIL_MTN_TAB3(value_tab3);
    
    value_tab4=(28<<24)+(32<<16)+(35<<8)+39;
    IEP_REGB_DIL_MTN_TAB4(value_tab4);
    
    value_tab5=(14<<24)+(18<<16)+(21<<8)+24;
    IEP_REGB_DIL_MTN_TAB5(value_tab5);
    
    value_tab6=(4<<24)+(6<<16)+(9<<8)+12;
    IEP_REGB_DIL_MTN_TAB6(value_tab6);
    
    value_tab6=(1<<8)+3;
    IEP_REGB_DIL_MTN_TAB7(value_tab6);	
    */
    IEP_REGB_DIL_MTN_TAB0(iep_msg->base, 0x40404040);
    IEP_REGB_DIL_MTN_TAB1(iep_msg->base, 0x3c3e3f3f);
    IEP_REGB_DIL_MTN_TAB2(iep_msg->base, 0x3336393b);
    IEP_REGB_DIL_MTN_TAB3(iep_msg->base, 0x272a2d31);
    IEP_REGB_DIL_MTN_TAB4(iep_msg->base, 0x181c2023);
    IEP_REGB_DIL_MTN_TAB5(iep_msg->base, 0x0c0e1215);
    IEP_REGB_DIL_MTN_TAB6(iep_msg->base, 0x03040609);
    IEP_REGB_DIL_MTN_TAB7(iep_msg->base, 0x00000001);
#ifdef IEP_PRINT_INFO
    IEP_DBG(" //==deinterlace config========================//\n\n");
    IEP_DBG("sw_dil_hf_en               = %d;//0:disable  high-frequency deinterlace; 1:enahble\n", iep_msg->dein_high_fre_en);
    IEP_DBG("sw_dil_mode                = %d;//0:deinterlace disable; 1:I4O2; 2:I4O1B; 3:I4O1T; 4:I201B; 5:I201T 6:bypass\n", iep_msg->dein_mode);
    IEP_DBG("sw_dil_ei_mode             = %d;//edge interpolation\n", iep_msg->dein_ei_mode);
    IEP_DBG("sw_dil_mtn_tab0            = 63,64,64,64;//motion table  0~ 3,7bit\n");
    IEP_DBG("sw_dil_mtn_tab1            = 52,57,60,62;//motion table  0~ 3,7bit\n");
    IEP_DBG("sw_dil_mtn_tab2            = 0,0,0,0,46;//motion table  0~ 3,7bit\n");
    IEP_DBG("sw_dil_mtn_tab3            = 0;//motion table  0~ 3,7bit\n");
    IEP_DBG("sw_dil_mtn_tab4            = 0;//motion table  0~ 3,7bit\n");
    IEP_DBG("sw_dil_mtn_tab5            = 0;//motion table  0~ 3,7bit\n");
    IEP_DBG("sw_dil_mtn_tab6            = 0;//motion table  0~ 3,7bit\n");
    IEP_DBG("sw_dil_mtn_tab7            = 0;//motion table  0~ 3,7bit\n\n");
#endif
}

static void iep_config_yuv_enh(IEP_MSG *iep_msg)
{
    IEP_REGB_YUV_ENH_EN(iep_msg->base, iep_msg->yuv_enhance_en);
    if (iep_msg->yuv_enhance_en == 1) {
        IEP_REGB_VIDEO_MODE(iep_msg->base, iep_msg->video_mode);
        if (iep_msg->video_mode == normal_mode) {
            IEP_REGB_SAT_CON(iep_msg->base, iep_msg->sat_con_int);
            IEP_REGB_CONTRAST(iep_msg->base, iep_msg->contrast_int);
            IEP_REGB_BRIGHTNESS(iep_msg->base, iep_msg->yuv_enh_brightness);
            IEP_REGB_COS_HUE(iep_msg->base, iep_msg->cos_hue_int);
            IEP_REGB_SIN_HUE(iep_msg->base, iep_msg->sin_hue_int);
        } else if (iep_msg->video_mode == color_bar) { //color bar
            IEP_REGB_COLOR_BAR_Y(iep_msg->base, iep_msg->color_bar_y);
            IEP_REGB_COLOR_BAR_U(iep_msg->base, iep_msg->color_bar_u);
            IEP_REGB_COLOR_BAR_V(iep_msg->base, iep_msg->color_bar_v);
        }

    }
#ifdef IEP_PRINT_INFO
    IEP_DBG("//==yuv contrast,saturation,hue,bright cofing==//\n\n");
    IEP_DBG("sw_yuv_enh_en              = %d;//yuv enhance enable\n", iep_msg->yuv_enhance_en);
    IEP_DBG("sw_sat_con                 = %d;//saturation*contrast*128 (0<saturation<1.992,0<contrast<1.992)\n", iep_msg->sat_con_int);
    IEP_DBG("sw_contrast                = %d;//contrast*128 (0<contrast<1.992)\n", iep_msg->contrast_int);
    IEP_DBG("sw_brightness              = %d;//brightness (-32<brightness<31)\n", iep_msg->yuv_enh_brightness);
    IEP_DBG("sw_cos_hue                 = %d;//cos(hue) (0<hue<30,triangle)\n", iep_msg->cos_hue_int);
    IEP_DBG("sw_sin_hue                 = %d;//sin(hue) (0<hue<30,triangle)\n", iep_msg->sin_hue_int);
    IEP_DBG("sw_color_bar_y             = %d;\n", iep_msg->color_bar_y);
    IEP_DBG("sw_color_bar_u             = %d;\n", iep_msg->color_bar_u);
    IEP_DBG("sw_color_bar_v             = %d;\n", iep_msg->color_bar_v);
    IEP_DBG("sw_video_mode              = %d;\n\n", iep_msg->video_mode);
#endif

}

static void iep_config_rgb2yuv(IEP_MSG *iep_msg)
{
    unsigned char cond1, cond2;
    unsigned int rgb2yuv_en = 0;

    //rgb in,yuv out
    cond1 = ((iep_msg->src.format <= 5) && (iep_msg->dst.format > 5)) ? 1 : 0;

    //rgb process,yuv out
    cond2 = (((iep_msg->rgb_color_enhance_en == 1) || (iep_msg->rgb_cg_en == 1) 
              || (iep_msg->rgb_enhance_mode != rgb_enhance_bypass)) && (iep_msg->dst.format > 5)) ? 1 : 0;


    if ((cond1 == 1) || (cond2 == 1)) {
        IEP_REGB_RGB_TO_YUV_EN(iep_msg->base, 1);
        rgb2yuv_en = 1;
        IEP_REGB_RGB2YUV_COE_SEL(iep_msg->base, iep_msg->rgb2yuv_mode);
        IEP_REGB_RGB2YUV_INPUT_CLIP(iep_msg->base, iep_msg->rgb2yuv_clip_en);
    } else IEP_REGB_RGB_TO_YUV_EN(iep_msg->base, 0);
#ifdef IEP_PRINT_INFO
    IEP_DBG("//==color space conversion config============//\n\n");
    IEP_DBG("sw_rgb_to_yuv_en           = %d;\n", rgb2yuv_en);
    IEP_DBG("sw_rgb2yuv_coe_sel         = %d;\n", iep_msg->rgb2yuv_mode);
    IEP_DBG("sw_rgb2yuv_input_clip      = %d;\n\n", iep_msg->rgb2yuv_clip_en);
#endif

}

static void iep_config_yuv2rgb(IEP_MSG *iep_msg)
{
    unsigned char cond1, cond2;
    unsigned int yuv2rgb_en = 0;

    //yuv in,rgb out
    cond1 = ((iep_msg->src.format > 5) && (iep_msg->dst.format <= 5)) ? 1 : 0;

    //yuv in,rgb process
    cond2 = (((iep_msg->rgb_color_enhance_en == 1) || (iep_msg->rgb_cg_en == 1) 
              || (iep_msg->rgb_enhance_mode != rgb_enhance_bypass)) && (iep_msg->src.format > 5)) ? 1 : 0;

    if ((cond1 == 1) || (cond2 == 1)) {
        IEP_REGB_YUV_TO_RGB_EN(iep_msg->base, 1);
        yuv2rgb_en = 1;
        IEP_REGB_YUV2RGB_COE_SEL(iep_msg->base, iep_msg->yuv2rgb_mode);
        IEP_REGB_YUV2RGB_INPUT_CLIP(iep_msg->base, iep_msg->yuv2rgb_clip_en);
    } else IEP_REGB_YUV_TO_RGB_EN(iep_msg->base, 0);
#ifdef IEP_PRINT_INFO
    IEP_DBG("sw_yuv_to_rgb_en           = %d;\n", yuv2rgb_en);
    IEP_DBG("sw_yuv2rgb_coe_sel         = %d;\n", iep_msg->yuv2rgb_mode);
    IEP_DBG("sw_yuv2rgb_input_clip      = %d;\n\n", iep_msg->yuv2rgb_clip_en);
#endif
}

static void iep_config_dither_up(IEP_MSG *iep_msg)
{
    unsigned int dither_up = 0;
    if ((iep_msg->src.format == IEP_FORMAT_RGB_565) || (iep_msg->src.format == IEP_FORMAT_BGR_565)) {
        IEP_REGB_DITHER_UP_EN(iep_msg->base, iep_msg->dither_up_en);
        dither_up = iep_msg->dither_up_en;
    }	    else IEP_REGB_DITHER_UP_EN(iep_msg->base, 0);
#ifdef IEP_PRINT_INFO
    IEP_DBG("//==dither config=============================//\n\n");
    IEP_DBG("sw_dither_up_en            = %d;\n", dither_up);
#endif
}

static void iep_config_dither_down(IEP_MSG *iep_msg)
{
    unsigned int dither_down = 0;
    if ((iep_msg->dst.format == IEP_FORMAT_RGB_565) || (iep_msg->dst.format == IEP_FORMAT_BGR_565)) {
        IEP_REGB_DITHER_DOWN_EN(iep_msg->base, 1);
        dither_down = 1;
    } else IEP_REGB_DITHER_DOWN_EN(iep_msg->base, 0);
#ifdef IEP_PRINT_INFO
    IEP_DBG("sw_dither_down_en            = %d;\n\n", dither_down);
#endif
}

static void iep_config_glb_alpha(IEP_MSG *iep_msg)
{
    IEP_REGB_GLB_ALPHA(iep_msg->base, iep_msg->global_alpha_value);
#ifdef IEP_PRINT_INFO
    IEP_DBG("//==global alpha for ARGB config=============//\n\n");
    IEP_DBG("sw_glb_alpha               = %d;//global alpha value for output ARGB\n\n", iep_msg->global_alpha_value);
#endif
}

static void iep_config_vir_line(IEP_MSG *iep_msg)
{
    unsigned int src_vir_w;
    unsigned int dst_vir_w;

    switch (iep_msg->src.format) {
    case IEP_FORMAT_ARGB_8888 :
        src_vir_w = iep_msg->src.vir_w;
        break;
    case IEP_FORMAT_ABGR_8888 :
        src_vir_w = iep_msg->src.vir_w;
        break;
    case IEP_FORMAT_RGBA_8888 :
        src_vir_w = iep_msg->src.vir_w;
        break;
    case IEP_FORMAT_BGRA_8888 :
        src_vir_w = iep_msg->src.vir_w;
        break;
    case IEP_FORMAT_RGB_565 :
        if (iep_msg->src.vir_w % 2 == 1) src_vir_w = (iep_msg->src.vir_w + 1) / 2;
        else src_vir_w = iep_msg->src.vir_w / 2;
        break;
    case IEP_FORMAT_BGR_565 :
        if (iep_msg->src.vir_w % 2 == 1) src_vir_w = iep_msg->src.vir_w / 2 + 1;
        else src_vir_w = iep_msg->src.vir_w / 2;
        break;
    case IEP_FORMAT_YCbCr_422_SP :
        if (iep_msg->src.vir_w % 4 != 0) src_vir_w = iep_msg->src.vir_w / 4 + 1;
        else src_vir_w = iep_msg->src.vir_w / 4;
        break;
    case IEP_FORMAT_YCbCr_422_P :
        if (iep_msg->src.vir_w % 4 != 0) src_vir_w = iep_msg->src.vir_w / 4 + 1;
        else src_vir_w = iep_msg->src.vir_w / 4;
        break;
    case IEP_FORMAT_YCbCr_420_SP :
        if (iep_msg->src.vir_w % 4 != 0) src_vir_w = iep_msg->src.vir_w / 4 + 1;
        else src_vir_w = iep_msg->src.vir_w / 4;
        break;
    case IEP_FORMAT_YCbCr_420_P :
        if (iep_msg->src.vir_w % 4 != 0) src_vir_w = iep_msg->src.vir_w / 4 + 1;
        else src_vir_w = iep_msg->src.vir_w / 4;
        break;
    case IEP_FORMAT_YCrCb_422_SP :
        if (iep_msg->src.vir_w % 4 != 0) src_vir_w = iep_msg->src.vir_w / 4 + 1;
        else src_vir_w = iep_msg->src.vir_w / 4;
        break;
    case IEP_FORMAT_YCrCb_422_P :
        if (iep_msg->src.vir_w % 4 != 0) src_vir_w = iep_msg->src.vir_w / 4 + 1;
        else src_vir_w = iep_msg->src.vir_w / 4;
        break;
    case IEP_FORMAT_YCrCb_420_SP :
        if (iep_msg->src.vir_w % 4 != 0) src_vir_w = iep_msg->src.vir_w / 4 + 1;
        else src_vir_w = iep_msg->src.vir_w / 4;
        break;
    case IEP_FORMAT_YCrCb_420_P :
        if (iep_msg->src.vir_w % 4 != 0) src_vir_w = iep_msg->src.vir_w / 4 + 1;
        else src_vir_w = iep_msg->src.vir_w / 4;
        break;
    default:
        IEP_ERR("Unkown format, set the source image virtual width 0\n");
        src_vir_w = 0;
        break;
    }

    switch (iep_msg->dst.format) {
    case IEP_FORMAT_ARGB_8888 :
        dst_vir_w = iep_msg->dst.vir_w;
        break;
    case IEP_FORMAT_ABGR_8888 :
        dst_vir_w = iep_msg->dst.vir_w;
        break;
    case IEP_FORMAT_RGBA_8888 :
        dst_vir_w = iep_msg->dst.vir_w;
        break;
    case IEP_FORMAT_BGRA_8888 :
        dst_vir_w = iep_msg->dst.vir_w;
        break;
    case IEP_FORMAT_RGB_565 :
        if (iep_msg->dst.vir_w % 2 == 1) dst_vir_w = (iep_msg->dst.vir_w + 1) / 2;
        else dst_vir_w = iep_msg->dst.vir_w / 2;
        break;
    case IEP_FORMAT_BGR_565 :
        if (iep_msg->dst.vir_w % 2 == 1) dst_vir_w = iep_msg->dst.vir_w / 2 + 1;
        else dst_vir_w = iep_msg->dst.vir_w / 2;
        break;
    case IEP_FORMAT_YCbCr_422_SP :
        if (iep_msg->dst.vir_w % 4 != 0) dst_vir_w = iep_msg->dst.vir_w / 4 + 1;
        else dst_vir_w = iep_msg->dst.vir_w / 4;
        break;
    case IEP_FORMAT_YCbCr_422_P :
        if (iep_msg->dst.vir_w % 4 != 0) dst_vir_w = iep_msg->dst.vir_w / 4 + 1;
        else dst_vir_w = iep_msg->dst.vir_w / 4;
        break;
    case IEP_FORMAT_YCbCr_420_SP :
        if (iep_msg->dst.vir_w % 4 != 0) dst_vir_w = iep_msg->dst.vir_w / 4 + 1;
        else dst_vir_w = iep_msg->dst.vir_w / 4;
        break;
    case IEP_FORMAT_YCbCr_420_P :
        if (iep_msg->dst.vir_w % 4 != 0) dst_vir_w = iep_msg->dst.vir_w / 4 + 1;
        else dst_vir_w = iep_msg->dst.vir_w / 4;
        break;
    case IEP_FORMAT_YCrCb_422_SP :
        if (iep_msg->dst.vir_w % 4 != 0) dst_vir_w = iep_msg->dst.vir_w / 4 + 1;
        else dst_vir_w = iep_msg->dst.vir_w / 4;
        break;
    case IEP_FORMAT_YCrCb_422_P :
        if (iep_msg->dst.vir_w % 4 != 0) dst_vir_w = iep_msg->dst.vir_w / 4 + 1;
        else dst_vir_w = iep_msg->dst.vir_w / 4;
        break;
    case IEP_FORMAT_YCrCb_420_SP :
        if (iep_msg->dst.vir_w % 4 != 0) dst_vir_w = iep_msg->dst.vir_w / 4 + 1;
        else dst_vir_w = iep_msg->dst.vir_w / 4;
        break;
    case IEP_FORMAT_YCrCb_420_P :
        if (iep_msg->dst.vir_w % 4 != 0) dst_vir_w = iep_msg->dst.vir_w / 4 + 1;
        else dst_vir_w = iep_msg->dst.vir_w / 4;
        break;
    default:
        IEP_ERR("Unkown format, set the destination image virtual width 0\n");
        dst_vir_w = 0;
        break;
    }
    IEP_REGB_DST_VIR_LINE_WIDTH(iep_msg->base, dst_vir_w);
    IEP_REGB_SRC_VIR_LINE_WIDTH(iep_msg->base, src_vir_w);
#ifdef IEP_PRINT_INFO
    IEP_DBG("/==virtual line width config==================//\n\n");
    IEP_DBG("sw_dst_vir_line_width      = %d;//destination virtual line width \n", dst_vir_w);
    IEP_DBG("sw_src_vir_line_width      = %d;//source virtual line width \n\n", src_vir_w);
#endif
}

static void iep_config_src_addr(IEP_MSG *iep_msg)
{
    u32 src_addr_yrgb;
    u32 src_addr_cbcr;
    u32 src_addr_cr;
    u32 src_addr_y1;
    u32 src_addr_cbcr1;
    u32 src_addr_cr1;
    u32 src_addr_y_itemp;
    u32 src_addr_cbcr_itemp;
    u32 src_addr_cr_itemp;
    u32 src_addr_y_ftemp;
    u32 src_addr_cbcr_ftemp;
    u32 src_addr_cr_ftemp;
    unsigned int offset_addr_y = 0;
    unsigned int offset_addr_uv = 0;
    unsigned int offset_addr_v = 0;
    //unsigned int offset_addr_y_w = 0;
    unsigned int offset_addr_uv_w = 0;
    unsigned int offset_addr_v_w = 0;
    //unsigned int offset_addr_y_h = 0;
    unsigned int offset_addr_uv_h = 0;
    unsigned int offset_addr_v_h = 0;

    unsigned int offset_x_equ_uv;
    unsigned int offset_x_u_byte;
    unsigned int offset_x_v_byte;
    unsigned int vir_w_euq_uv;
    unsigned int line_u_byte;
    unsigned int line_v_byte;
    unsigned int offset_y_equ_420_uv = 0;

    //**********************************************//
    //***********y addr offset**********************//
    //**********************************************//
    if (iep_msg->src.format <= 3) {
        offset_addr_y = iep_msg->src.y_off * 4 * iep_msg->src.vir_w + iep_msg->src.x_off * 4;
    } else if (iep_msg->src.format <= 5) {
        offset_addr_y = iep_msg->src.y_off * 2 * iep_msg->src.vir_w + iep_msg->src.x_off * 2;
    } else {
        offset_addr_y = iep_msg->src.y_off * iep_msg->src.vir_w + iep_msg->src.x_off;
    }

    //**********************************************//
    //***********uv addr offset*********************//
    //**********************************************//
    // note: image size align to even when image format is yuv

    //----------offset_w--------//
    if (iep_msg->src.x_off % 2 == 1) offset_x_equ_uv = iep_msg->src.x_off + 1;
    else offset_x_equ_uv = iep_msg->src.x_off;

    offset_x_u_byte = offset_x_equ_uv / 2;
    offset_x_v_byte = offset_x_equ_uv / 2;

    if ((iep_msg->src.format == IEP_FORMAT_YCbCr_422_SP) || (iep_msg->src.format == IEP_FORMAT_YCbCr_420_SP) 
        || (iep_msg->src.format == IEP_FORMAT_YCrCb_422_SP) || (iep_msg->src.format == IEP_FORMAT_YCrCb_420_SP)) offset_addr_uv_w = offset_x_u_byte + offset_x_v_byte;
    else {
        offset_addr_uv_w = offset_x_u_byte;
        offset_addr_v_w = offset_x_v_byte;
    }

    //----------offset_h--------//
    if (iep_msg->src.vir_w % 2 == 1) vir_w_euq_uv = iep_msg->src.vir_w + 1;
    else vir_w_euq_uv = iep_msg->src.vir_w;

    line_u_byte = vir_w_euq_uv / 2;
    line_v_byte = vir_w_euq_uv / 2;

    if (iep_msg->src.y_off % 2 == 1) offset_y_equ_420_uv = iep_msg->src.y_off + 1;
    else offset_y_equ_420_uv = iep_msg->src.y_off;

    switch (iep_msg->src.format) {
    case IEP_FORMAT_YCbCr_422_SP :
        offset_addr_uv_h = (line_u_byte + line_v_byte) * iep_msg->src.y_off;
        break;
    case IEP_FORMAT_YCbCr_422_P :
        offset_addr_uv_h = line_u_byte * iep_msg->src.y_off;
        offset_addr_v_h = line_v_byte * iep_msg->src.y_off;
        break;
    case IEP_FORMAT_YCbCr_420_SP :
        offset_addr_uv_h = (line_u_byte + line_v_byte) * offset_y_equ_420_uv / 2;
        break;
    case IEP_FORMAT_YCbCr_420_P :
        offset_addr_uv_h = line_u_byte * offset_y_equ_420_uv / 2;
        offset_addr_v_h = line_v_byte * offset_y_equ_420_uv / 2;
        break;
    case IEP_FORMAT_YCrCb_422_SP :
        offset_addr_uv_h = (line_u_byte + line_v_byte) * iep_msg->src.y_off;
        break;
    case IEP_FORMAT_YCrCb_422_P :
        offset_addr_uv_h = line_u_byte * iep_msg->src.y_off;
        offset_addr_v_h = line_v_byte * iep_msg->src.y_off;
        break;
    case IEP_FORMAT_YCrCb_420_SP :
        offset_addr_uv_h = (line_u_byte + line_v_byte) * offset_y_equ_420_uv / 2;
        break;
    case IEP_FORMAT_YCrCb_420_P :
        offset_addr_uv_h = line_u_byte * offset_y_equ_420_uv / 2;
        offset_addr_v_h = line_v_byte * offset_y_equ_420_uv / 2;
        break;
    default:
        break;
    }
    //----------offset u/v addr--------//

    offset_addr_uv = offset_addr_uv_w + offset_addr_uv_h;
    offset_addr_v  = offset_addr_v_w + offset_addr_v_h;
    //**********************************************//
    //***********yuv address   *********************//
    //**********************************************//
    src_addr_yrgb = ((u32)iep_msg->src.mem_addr) + offset_addr_y;
    src_addr_cbcr = ((u32)iep_msg->src.uv_addr) + offset_addr_uv;
    src_addr_cr = ((u32)iep_msg->src.v_addr) + offset_addr_v;

    // former frame when processing deinterlace
    src_addr_y1 = ((u32)iep_msg->src1.mem_addr) + offset_addr_y;
    src_addr_cbcr1 = ((u32)iep_msg->src1.uv_addr) + offset_addr_uv;
    src_addr_cr1 = ((u32)iep_msg->src1.v_addr) + offset_addr_v;

    src_addr_y_itemp = ((u32)iep_msg->src_itemp.mem_addr) + offset_addr_y;
    src_addr_cbcr_itemp = ((u32)iep_msg->src_itemp.uv_addr) + offset_addr_uv;
    src_addr_cr_itemp = ((u32)iep_msg->src_itemp.v_addr) + offset_addr_v;

    src_addr_y_ftemp = ((u32)iep_msg->src_ftemp.mem_addr) + offset_addr_y;
    src_addr_cbcr_ftemp = ((u32)iep_msg->src_ftemp.uv_addr) + offset_addr_uv;
    src_addr_cr_ftemp = ((u32)iep_msg->src_ftemp.v_addr) + offset_addr_v;

    if ((iep_msg->dein_mode == IEP_DEINTERLACE_MODE_I4O1 || iep_msg->dein_mode == IEP_DEINTERLACE_MODE_I4O2) &&
#if 1
           iep_msg->field_order == FIELD_ORDER_BOTTOM_FIRST
#else
           iep_msg->field_order == FIELD_ORDER_TOP_FIRST
#endif
          ) {
        IEP_REGB_SRC_ADDR_YRGB(iep_msg->base, src_addr_y1);
        IEP_REGB_SRC_ADDR_CBCR(iep_msg->base, src_addr_cbcr1);
        IEP_REGB_SRC_ADDR_CR(iep_msg->base, src_addr_cr1);
        IEP_REGB_SRC_ADDR_Y1(iep_msg->base, src_addr_yrgb);
        IEP_REGB_SRC_ADDR_CBCR1(iep_msg->base, src_addr_cbcr);
        IEP_REGB_SRC_ADDR_CR1(iep_msg->base, src_addr_cr);
    } else {
        IEP_REGB_SRC_ADDR_YRGB(iep_msg->base, src_addr_yrgb);
        IEP_REGB_SRC_ADDR_CBCR(iep_msg->base, src_addr_cbcr);
        IEP_REGB_SRC_ADDR_CR(iep_msg->base, src_addr_cr);
        IEP_REGB_SRC_ADDR_Y1(iep_msg->base, src_addr_y1);
        IEP_REGB_SRC_ADDR_CBCR1(iep_msg->base, src_addr_cbcr1);
        IEP_REGB_SRC_ADDR_CR1(iep_msg->base, src_addr_cr1);
    }

    if (iep_msg->yuv_3D_denoise_en) {
        IEP_REGB_SRC_ADDR_Y_ITEMP(iep_msg->base, src_addr_y_itemp);
        IEP_REGB_SRC_ADDR_CBCR_ITEMP(iep_msg->base, src_addr_cbcr_itemp);
        IEP_REGB_SRC_ADDR_Y_FTEMP(iep_msg->base, src_addr_y_ftemp);
        IEP_REGB_SRC_ADDR_CBCR_FTEMP(iep_msg->base, src_addr_cbcr_ftemp);
        if ((iep_msg->src.format == IEP_FORMAT_YCbCr_422_P) || (iep_msg->src.format == IEP_FORMAT_YCbCr_420_P) 
            || (iep_msg->src.format == IEP_FORMAT_YCrCb_422_P) || (iep_msg->src.format == IEP_FORMAT_YCrCb_420_P)) {
            IEP_REGB_SRC_ADDR_CR_ITEMP(iep_msg->base, src_addr_cr_itemp);
            IEP_REGB_SRC_ADDR_CR_FTEMP(iep_msg->base, src_addr_cr_ftemp);
        }
    }
#ifdef IEP_PRINT_INFO
    IEP_DBG("//-------source address for image-------// \n\n");
    IEP_DBG("sw_src_addr_yrgb           = 32'h%x;\n", src_addr_yrgb);
    IEP_DBG("sw_src_addr_cbcr           = 32'h%x;\n", src_addr_cbcr);
    IEP_DBG("sw_src_addr_cr             = 32'h%x;\n", src_addr_cr);
    IEP_DBG("sw_src_addr_y1             = 32'h%x;\n", src_addr_y1);
    IEP_DBG("sw_src_addr_cbcr0          = 32'h%x;\n", src_addr_cbcr1);
    IEP_DBG("sw_src_addr_cr0            = 32'h%x;\n", src_addr_cr1);
    IEP_DBG("sw_src_addr_y_itemp        = 32'h%x;\n", src_addr_y_itemp);
    IEP_DBG("sw_src_addr_cbcr_itemp     = 32'h%x;\n", src_addr_cbcr_itemp);
    IEP_DBG("sw_src_addr_cr_itemp       = 32'h%x;\n", src_addr_cr_itemp);
    IEP_DBG("sw_src_addr_y_ftemp        = 32'h%x;\n", src_addr_y_ftemp);
    IEP_DBG("sw_src_addr_cbcr_ftemp     = 32'h%x;\n", src_addr_cbcr_ftemp);
    IEP_DBG("sw_src_addr_cr_ftemp       = 32'h%x;\n\n", src_addr_cr_ftemp);
#endif
}

static void iep_config_dst_addr(IEP_MSG *iep_msg)
{
    IEP_REGB_DST_ADDR_YRGB(iep_msg->base, (u32)iep_msg->dst.mem_addr);
    IEP_REGB_DST_ADDR_CBCR(iep_msg->base, (u32)iep_msg->dst.uv_addr);
    IEP_REGB_DST_ADDR_Y1(iep_msg->base, (u32)iep_msg->dst1.mem_addr);
    IEP_REGB_DST_ADDR_CBCR1(iep_msg->base, (u32)iep_msg->dst1.uv_addr);
    IEP_REGB_DST_ADDR_CR(iep_msg->base, (u32)iep_msg->dst.v_addr);
    IEP_REGB_DST_ADDR_CR1(iep_msg->base, (u32)iep_msg->dst1.v_addr);

    if (iep_msg->yuv_3D_denoise_en) {
        IEP_REGB_DST_ADDR_Y_ITEMP(iep_msg->base, (u32)iep_msg->dst_itemp.mem_addr);
        IEP_REGB_DST_ADDR_CBCR_ITEMP(iep_msg->base, (u32)iep_msg->dst_itemp.uv_addr);
        IEP_REGB_DST_ADDR_Y_FTEMP(iep_msg->base, (u32)iep_msg->dst_ftemp.mem_addr);
        IEP_REGB_DST_ADDR_CBCR_FTEMP(iep_msg->base, (u32)iep_msg->dst_ftemp.uv_addr);
        if ((iep_msg->dst.format == IEP_FORMAT_YCbCr_422_P) || (iep_msg->dst.format == IEP_FORMAT_YCbCr_420_P) 
            || (iep_msg->dst.format == IEP_FORMAT_YCrCb_422_P) || (iep_msg->dst.format == IEP_FORMAT_YCrCb_420_P)) {
            IEP_REGB_DST_ADDR_CR_ITEMP(iep_msg->base, (u32)iep_msg->dst_itemp.v_addr);
            IEP_REGB_DST_ADDR_CR_FTEMP(iep_msg->base, (u32)iep_msg->dst_ftemp.v_addr);
        }
    }
#ifdef IEP_PRINT_INFO
    IEP_DBG("//-------destination address for image-------// \n\n");
    IEP_DBG("sw_dst_addr_yrgb           = 32'h%x;\n", (u32)iep_msg->dst.mem_addr);
    IEP_DBG("sw_dst_addr_cbcr           = 32'h%x;\n", (u32)iep_msg->dst.uv_addr);
    IEP_DBG("sw_dst_addr_cr             = 32'h%x;\n", (u32)iep_msg->dst.v_addr);
    IEP_DBG("sw_dst_addr_y1             = 32'h%x;\n", (u32)iep_msg->dst1.mem_addr);
    IEP_DBG("sw_dst_addr_cbcr0          = 32'h%x;\n", (u32)iep_msg->dst1.uv_addr);
    IEP_DBG("sw_dst_addr_cr0            = 32'h%x;\n", (u32)iep_msg->dst1.v_addr);
    IEP_DBG("sw_dst_addr_y_itemp        = 32'h%x;\n", (u32)iep_msg->dst_itemp.mem_addr);
    IEP_DBG("sw_dst_addr_cbcr_itemp     = 32'h%x;\n", (u32)iep_msg->dst_itemp.uv_addr);
    IEP_DBG("sw_dst_addr_cr_itemp       = 32'h%x;\n", (u32)iep_msg->dst_itemp.v_addr);
    IEP_DBG("sw_dst_addr_y_ftemp        = 32'h%x;\n", (u32)iep_msg->dst_ftemp.mem_addr);
    IEP_DBG("sw_dst_addr_cbcr_ftemp     = 32'h%x;\n", (u32)iep_msg->dst_ftemp.uv_addr);
    IEP_DBG("sw_dst_addr_cr_ftemp       = 32'h%x;\n\n", (u32)iep_msg->dst_ftemp.v_addr);
#endif
}

void iep_config_lcdc_path(IEP_MSG *iep_msg)
{
    IEP_REGB_LCDC_PATH_EN(iep_msg->base, iep_msg->lcdc_path_en);

#ifdef IEP_PRINT_INFO
    IEP_DBG("//==write back or lcdc direct path config=====// \n\n");
    IEP_DBG("sw_lcdc_path_en            = %d;//lcdc direct path enable,c model don't care this value\n\n", iep_msg->lcdc_path_en);
#endif
}

int iep_probe_int(void *base)
{
    return ReadReg32(base, rIEP_INT) & 1;
}

void iep_config_frame_end_int_clr(void *base)
{
    IEP_REGB_FRAME_END_INT_CLR(base, 1);
}

void iep_config_frame_end_int_en(void *base)
{
    IEP_REGB_FRAME_END_INT_CLR(base, 1);
    IEP_REGB_FRAME_END_INT_EN(base, 1);
}

#if defined(CONFIG_IEP_MMU)
struct iep_mmu_int_status iep_probe_mmu_int_status(void *base)
{
    uint32_t mmu_int_sts = IEP_REGB_MMU_INT_STATUS(base);
    struct iep_mmu_int_status sts;

    memcpy(&sts, &mmu_int_sts, 4);

    return sts;
}

void iep_config_mmu_page_fault_int_en(void *base, bool en)
{
    IEP_REGB_MMU_INT_MASK_PAGE_FAULT_INT_EN(base, en);
}

void iep_config_mmu_page_fault_int_clr(void *base)
{
    IEP_REGB_MMU_INT_CLEAR_PAGE_FAULT_CLEAR(base, 1);
}

void iep_config_mmu_read_bus_error_int_clr(void *base)
{
    IEP_REGB_MMU_INT_CLEAR_READ_BUS_ERROR_CLEAR(base, 1);
}

uint32_t iep_probe_mmu_page_fault_addr(void *base)
{
    return IEP_REGB_MMU_PAGE_FAULT_ADDR(base);
}

void iep_config_mmu_cmd(void *base, enum iep_mmu_cmd cmd)
{
    IEP_REGB_MMU_CMD(base, cmd);
}

void iep_config_mmu_dte_addr(void *base, uint32_t addr) {
    IEP_REGB_MMU_DTE_ADDR(base, addr);
}
#endif

void iep_config_misc(IEP_MSG *iep_msg)
{
//	IEP_REGB_V_REVERSE_DISP();
//	IEP_REGB_H_REVERSE_DISP();
#ifdef IEP_PRINT_INFO
    IEP_DBG("//==misc config==========================//\n\n");
    IEP_DBG("sw_v_reverse_disp          = 0;\n");
    IEP_DBG("sw_u_reverse_disp          = 0;\n\n");
#endif
}

#define IEP_RESET_TIMEOUT   1000
void iep_soft_rst(void *base)
{
    unsigned int rst_state = 0;
    int i = 0;
    WriteReg32(base, rIEP_SOFT_RST, 2);
    WriteReg32(base, rIEP_SOFT_RST, 1);
    while (i++ < IEP_RESET_TIMEOUT) {
        rst_state = ReadReg32(base, IEP_STATUS);
        if ((rst_state & 0x200) == 0x200) {
            break;
        }

        udelay(1);
    }
    WriteReg32(base, IEP_SOFT_RST, 2);

    if (i == IEP_RESET_TIMEOUT) IEP_DBG("soft reset timeout.\n");
}

void iep_config_done(void *base)
{
    WriteReg32(base, rIEP_CONF_DONE, 1);
}

void iep_config_frm_start(void *base)
{
    IEP_REGB_FRM_START(base, 1);
}

struct iep_status iep_get_status(void *base)
{
    uint32_t sts_int = IEP_REGB_STATUS(base);
    struct iep_status sts;

    memcpy(&sts, &sts_int, 4);

    return sts;
}

int iep_get_deinterlace_mode(void *base)
{
    int cfg = ReadReg32(base, IEP_CONFIG0);
    return (cfg >> 8) & 0x7;
}

void iep_set_deinterlace_mode(int mode, void *base)
{
    int cfg;

    if (mode > dein_mode_bypass) {
        IEP_ERR("invalid deinterlace mode\n");
        return;
    }

    cfg = ReadReg32(base, RAW_IEP_CONFIG0);
    cfg = (cfg & (~(7<<8))) | (mode << 8);
    WriteReg32(base, IEP_CONFIG0, cfg);

    //IEP_REGB_DIL_MODE(base, mode);
}

void iep_switch_input_address(void *base) {
    u32 src_addr_yrgb  = ReadReg32(base, IEP_SRC_ADDR_YRGB);
    u32 src_addr_cbcr  = ReadReg32(base, IEP_SRC_ADDR_CBCR);
    u32 src_addr_cr    = ReadReg32(base, IEP_SRC_ADDR_CR);

    u32 src_addr_y1    = ReadReg32(base, IEP_SRC_ADDR_Y1);
    u32 src_addr_cbcr1 = ReadReg32(base, IEP_SRC_ADDR_CBCR1);
    u32 src_addr_cr1   = ReadReg32(base, IEP_SRC_ADDR_CR1);

    IEP_REGB_SRC_ADDR_YRGB (base, src_addr_y1);
    IEP_REGB_SRC_ADDR_CBCR (base, src_addr_cbcr1);
    IEP_REGB_SRC_ADDR_CR   (base, src_addr_cr1);
    IEP_REGB_SRC_ADDR_Y1   (base, src_addr_yrgb);
    IEP_REGB_SRC_ADDR_CBCR1(base, src_addr_cbcr);
    IEP_REGB_SRC_ADDR_CR1  (base, src_addr_cr);
}

#if defined(CONFIG_IEP_IOMMU)
static int iep_bufid_to_iova(iep_service_info *pservice, u8 *tbl, int size, struct iep_reg *reg)
{
    int i;
    int usr_fd = 0;
    int offset = 0;
    
    if (tbl == NULL || size <= 0) {
        dev_err(pservice->iommu_dev, "input arguments invalidate\n");
        return -1;
    }
   
    for (i=0; i<size; i++) {
        usr_fd = reg->reg[tbl[i]] & 0x3FF;
        offset = reg->reg[tbl[i]] >> 10;
        if (usr_fd != 0) {
            struct ion_handle *hdl;
            int ret;
            struct iep_mem_region *mem_region;
            
            hdl = ion_import_dma_buf(pservice->ion_client, usr_fd);
            if (IS_ERR(hdl)) {
                dev_err(pservice->iommu_dev, "import dma-buf from fd %d failed, reg[%d]\n", usr_fd, tbl[i]);
                return PTR_ERR(hdl);
            }

            mem_region = kzalloc(sizeof(struct iep_mem_region), GFP_KERNEL);
 
            if (mem_region == NULL) {
                dev_err(pservice->iommu_dev, "allocate memory for iommu memory region failed\n");
                ion_free(pservice->ion_client, hdl);
                return -1;
            }
            
            mem_region->hdl = hdl;
            
            ret = ion_map_iommu(pservice->iommu_dev, pservice->ion_client, mem_region->hdl, &mem_region->iova, &mem_region->len);
            if (ret < 0) {
                dev_err(pservice->iommu_dev, "ion map iommu failed\n");
                kfree(mem_region);
                ion_free(pservice->ion_client, hdl);
                return ret;
            }
            
            reg->reg[tbl[i]] = mem_region->iova + offset;
            INIT_LIST_HEAD(&mem_region->reg_lnk);
            list_add_tail(&mem_region->reg_lnk, &reg->mem_region_list);
        }
    }
    
    return 0;
}

static u8 addr_tbl_iep[] = {
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55
};

static int iep_reg_address_translate(iep_service_info *pservice, struct iep_reg *reg)
{
    return iep_bufid_to_iova(pservice, addr_tbl_iep, sizeof(addr_tbl_iep), reg);
}
#endif

/**
 * generating a series of registers copy from iep message
 */
extern iep_service_info iep_service;
void iep_config(iep_session *session, IEP_MSG *iep_msg)
{
    struct iep_reg *reg = kzalloc(sizeof(struct iep_reg), GFP_KERNEL);

    reg->session = session;
    iep_msg->base = reg->reg;
    atomic_set(&reg->session->done, 0);

    INIT_LIST_HEAD(&reg->session_link);
    INIT_LIST_HEAD(&reg->status_link);
    
#if defined(CONFIG_IEP_IOMMU)
    INIT_LIST_HEAD(&reg->mem_region_list);    
#endif

    //write config
    iep_config_src_size(iep_msg);
    iep_config_dst_size(iep_msg);
    iep_config_dst_width_tile(iep_msg); //not implement
    iep_config_dst_fmt(iep_msg);
    iep_config_src_fmt(iep_msg);
    iep_config_scl(iep_msg);
    iep_config_cg_order(iep_msg);

    iep_config_cg(iep_msg);
    iep_config_dde(iep_msg);            //not implement
    iep_config_color_enh(iep_msg);      //not implement
    iep_config_yuv_dns(iep_msg);
    iep_config_dil(iep_msg);
    iep_config_yuv_enh(iep_msg);
    iep_config_rgb2yuv(iep_msg);
    iep_config_yuv2rgb(iep_msg);
    iep_config_dither_up(iep_msg);
    iep_config_dither_down(iep_msg);
    iep_config_glb_alpha(iep_msg);
    iep_config_vir_line(iep_msg);
    iep_config_src_addr(iep_msg);
    iep_config_dst_addr(iep_msg);
    iep_config_lcdc_path(iep_msg);
    iep_config_misc(iep_msg);           //not implement

    if (iep_msg->lcdc_path_en) {
        reg->dpi_en     = true;
        reg->act_width  = iep_msg->dst.act_w;
        reg->act_height = iep_msg->dst.act_h;
        reg->off_x      = iep_msg->off_x;
        reg->off_y      = iep_msg->off_y;
        reg->vir_width  = iep_msg->width;
        reg->vir_height = iep_msg->height;
        reg->layer      = iep_msg->layer;
        reg->format     = iep_msg->dst.format;
    } else {
        reg->dpi_en     = false;
    }

#if defined(CONFIG_IEP_MMU)
    if (iep_msg->vir_addr_enable) {
        iep_config_mmu_cmd(iep_msg->base, MMU_ENABLE_PAGING);
        iep_config_mmu_page_fault_int_en(iep_msg->base, 1);
    } else {
        iep_config_mmu_cmd(iep_msg->base, MMU_DISABLE_PAGING);
        iep_config_mmu_page_fault_int_en(iep_msg->base, 0);
    }
    iep_config_mmu_dte_addr(iep_msg->base, (uint32_t)virt_to_phys((uint32_t*)session->dte_table));
#endif

#if defined(CONFIG_IEP_IOMMU)
    if (iep_service.iommu_dev) {
        if (0 > iep_reg_address_translate(&iep_service, reg)) {
            IEP_ERR("error: translate reg address failed\n");
            kfree(reg);
            return;
        }
    }
#endif

    mutex_lock(&iep_service.lock);

    list_add_tail(&reg->status_link, &iep_service.waiting);
    list_add_tail(&reg->session_link, &session->waiting);
    mutex_unlock(&iep_service.lock);
}

