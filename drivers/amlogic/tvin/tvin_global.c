/*
 * TVIN global definition
 * enum, structure & global parameters used in all TVIN modules.
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *         Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/amlogic/tvin/tvin.h>
#include "tvin_global.h"


const char *tvin_color_fmt_str(enum tvin_color_fmt_e color_fmt)
{
        switch (color_fmt)
        {
                case TVIN_RGB444:
                        return "COLOR_FMT_RGB444";
                        break;
                case TVIN_YUV422:
                        return "COLOR_FMT_YUV422";
                        break;
                case TVIN_YUV444:
                        return "COLOR_FMT_YUV444";
                        break;
                case TVIN_YUYV422:
                        return "COLOR_FMT_TVIN_YUYV422";
                        break;
                case TVIN_YVYU422:
                        return "COLOR_FMT_TVIN_YVYU422";
                        break;
                case TVIN_VYUY422:
                        return "COLOR_FMT_TVIN_VYUY422";
                        break;
                case TVIN_UYVY422:
                        return "COLOR_FMT_TVIN_UYVY422";
                        break;
                case TVIN_NV12:
                        return "COLOR_FMT_TVIN_NV12";
                        break;
                case TVIN_NV21:
                        return "COLOR_FMT_TVIN_NV21";
                        break;
		case TVIN_BGGR:
			return "COLOR_FMT_TVIN_BGGR";
			break;
		case TVIN_RGGB:
			return "COLOR_FMT_TVIN_RGGB";
			break;
		case TVIN_GBRG:
			return "COLOR_FMT_TVIN_GBRG";
			break;
        	case TVIN_GRBG:
			return "COLOR_FMT_TVIN_GRBG";
			break;
                default:
                        return "COLOR_FMT_NULL";
                        break;
        }
}

EXPORT_SYMBOL(tvin_color_fmt_str);

const char *tvin_aspect_ratio_str(enum tvin_aspect_ratio_e aspect_ratio)
{
        switch (aspect_ratio)
        {
                case TVIN_ASPECT_1x1:
                        return "TVIN_ASPECT_1x1";
                        break;
                case TVIN_ASPECT_4x3:
                        return "TVIN_ASPECT_4x3";
                        break;
                case TVIN_ASPECT_16x9:
                        return "TVIN_ASPECT_16x9";
                        break;
                default:
                        return "TVIN_ASPECT_NULL";
                        break;
        }
}

EXPORT_SYMBOL(tvin_aspect_ratio_str);


const char * tvin_port_str(enum tvin_port_e port)
{
        switch (port)
        {
                case TVIN_PORT_MPEG0:
                        return "TVIN_PORT_MPEG0";
                        break;
                case TVIN_PORT_BT656:
                        return "TVIN_PORT_BT656";
                        break;
                case TVIN_PORT_BT601:
                        return "TVIN_PORT_BT601";
                        break;
                case TVIN_PORT_CAMERA:
                        return "TVIN_PORT_CAMERA";
                        break;
                case TVIN_PORT_VGA0:
                        return "TVIN_PORT_VGA0";
                        break;
                case TVIN_PORT_VGA1:
                        return "TVIN_PORT_VGA1";
                        break;
                case TVIN_PORT_VGA2:
                        return "TVIN_PORT_VGA2";
                        break;
                case TVIN_PORT_VGA3:
                        return "TVIN_PORT_VGA3";
                        break;
                case TVIN_PORT_VGA4:
                        return "TVIN_PORT_VGA4";
                        break;
                case TVIN_PORT_VGA5:
                        return "TVIN_PORT_VGA5";
                        break;
                case TVIN_PORT_VGA6:
                        return "TVIN_PORT_VGA6";
                        break;
                case TVIN_PORT_VGA7:
                        return "TVIN_PORT_VGA7";
                        break;
                case TVIN_PORT_COMP0:
                        return "TVIN_PORT_COMP0";
                        break;
                case TVIN_PORT_COMP1:
                        return "TVIN_PORT_COMP1";
                        break;
                case TVIN_PORT_COMP2:
                        return "TVIN_PORT_COMP2";
                        break;
                case TVIN_PORT_COMP3:
                        return "TVIN_PORT_COMP3";
                        break;
                case TVIN_PORT_COMP4:
                        return "TVIN_PORT_COMP4";
                        break;
                case TVIN_PORT_COMP5:
                        return "TVIN_PORT_COMP5";
                        break;
                case TVIN_PORT_COMP6:
                        return "TVIN_PORT_COMP6";
                        break;
                case TVIN_PORT_COMP7:
                        return "TVIN_PORT_COMP7";
                        break;
                case TVIN_PORT_CVBS0:
                        return "TVIN_PORT_CVBS0";
                        break;
                case TVIN_PORT_CVBS1:
                        return "TVIN_PORT_CVBS1";
                        break;
                case TVIN_PORT_CVBS2:
                        return "TVIN_PORT_CVBS2";
                        break;
                case TVIN_PORT_CVBS3:
                        return "TVIN_PORT_CVBS3";
                        break;
                case TVIN_PORT_CVBS4:
                        return "TVIN_PORT_CVBS4";
                        break;
                case TVIN_PORT_CVBS5:
                        return "TVIN_PORT_CVBS5";
                        break;
                case TVIN_PORT_CVBS6:
                        return "TVIN_PORT_CVBS6";
                        break;
                case TVIN_PORT_CVBS7:
                        return "TVIN_PORT_CVBS7";
                        break;
                case TVIN_PORT_SVIDEO0:
                        return "TVIN_PORT_SVIDEO0";
                        break;
                case TVIN_PORT_SVIDEO1:
                        return "TVIN_PORT_SVIDEO1";
                        break;
                case TVIN_PORT_SVIDEO2:
                        return "TVIN_PORT_SVIDEO2";
                        break;
                case TVIN_PORT_SVIDEO3:
                        return "TVIN_PORT_SVIDEO3";
                        break;
                case TVIN_PORT_SVIDEO4:
                        return "TVIN_PORT_SVIDEO4";
                        break;
                case TVIN_PORT_SVIDEO5:
                        return "TVIN_PORT_SVIDEO5";
                        break;
                case TVIN_PORT_SVIDEO6:
                        return "TVIN_PORT_SVIDEO6";
                        break;
                case TVIN_PORT_SVIDEO7:
                        return "TVIN_PORT_SVIDEO7";
                        break;
                case TVIN_PORT_HDMI0:
                        return "TVIN_PORT_HDMI0";
                        break;
                case TVIN_PORT_HDMI1:
                        return "TVIN_PORT_HDMI1";
                        break;
                case TVIN_PORT_HDMI2:
                        return "TVIN_PORT_HDMI2";
                        break;
                case TVIN_PORT_HDMI3:
                        return "TVIN_PORT_HDMI3";
                        break;
                case TVIN_PORT_HDMI4:
                        return "TVIN_PORT_HDMI4";
                        break;
                case TVIN_PORT_HDMI5:
                        return "TVIN_PORT_HDMI5";
                        break;
                case TVIN_PORT_HDMI6:
                        return "TVIN_PORT_HDMI6";
                        break;
                case TVIN_PORT_HDMI7:
                        return "TVIN_PORT_HDMI7";
                        break;
                case TVIN_PORT_DVIN0:
                        return "TVIN_PORT_DVIN0";
                        break;
                case TVIN_PORT_VIU:
                        return "TVIN_PORT_VIU";
                        break;
                case TVIN_PORT_MIPI:
                        return "TVIN_PORT_MIPI";
                        break;
		case TVIN_PORT_ISP:
			return "TVIN_PORT_ISP";
			break;
                case TVIN_PORT_MAX:
                        return "TVIN_PORT_MAX";
                        break;
                default:
                        return "TVIN_PORT_NULL";
                        break;
        }
}

EXPORT_SYMBOL(tvin_port_str);

const char *tvin_sig_status_str(enum tvin_sig_status_e status)
{
        switch (status)
        {
                case TVIN_SIG_STATUS_NULL:
                        return "TVIN_SIG_STATUS_NULL";
                        break;
                case TVIN_SIG_STATUS_NOSIG:
                        return "TVIN_SIG_STATUS_NOSIG";
                        break;
                case TVIN_SIG_STATUS_UNSTABLE:
                        return "TVIN_SIG_STATUS_UNSTABLE";
                        break;
                case TVIN_SIG_STATUS_NOTSUP:
                        return "TVIN_SIG_STATUS_NOTSUP";
                        break;
                case TVIN_SIG_STATUS_STABLE:
                        return "TVIN_SIG_STATUS_STABLE";
                        break;
                default:
                        return "TVIN_SIG_STATUS_NULL";
                        break;
        }
}

EXPORT_SYMBOL(tvin_sig_status_str);

const char *tvin_trans_fmt_str(enum tvin_trans_fmt trans_fmt)
{
        switch (trans_fmt)
        {
                case TVIN_TFMT_2D:
                        return "TVIN_TFMT_2D";
                        break;
                case TVIN_TFMT_3D_LRH_OLOR:
                        return "TVIN_TFMT_3D_LRH_OLOR";
                        break;
                case TVIN_TFMT_3D_LRH_OLER:
                        return "TVIN_TFMT_3D_LRH_OLER";
                        break;
                case TVIN_TFMT_3D_LRH_ELOR:
                        return "TVIN_TFMT_3D_LRH_ELOR";
                        break;
                case TVIN_TFMT_3D_LRH_ELER:
                        return "TVIN_TFMT_3D_LRH_ELER";
                        break;
                case TVIN_TFMT_3D_TB:
                        return "TVIN_TFMT_3D_TB";
                        break;
                case TVIN_TFMT_3D_FP:
                        return "TVIN_TFMT_3D_FP";
                        break;
                case TVIN_TFMT_3D_FA:
                        return "TVIN_TFMT_3D_FA";
                        break;
                case TVIN_TFMT_3D_LA:
                        return "TVIN_TFMT_3D_LA";
                        break;
                case TVIN_TFMT_3D_LRF:
                        return "TVIN_TFMT_3D_LRF";
                        break;
                case TVIN_TFMT_3D_LD:
                        return "TVIN_TFMT_3D_LD";
                        break;
                case TVIN_TFMT_3D_LDGD:
                        return "TVIN_TFMT_3D_LDGD";
                        break;
		case TVIN_TFMT_3D_DET_TB:
			return "TVIN_TFMT_3D_DET_TB";
			break;
		case TVIN_TFMT_3D_DET_LR:
			return "TVIN_TFMT_3D_DET_LR";
			break;
		case TVIN_TFMT_3D_DET_INTERLACE:
			return "TVIN_TFMT_3D_DET_INTERLACE";
			break;
		case TVIN_TFMT_3D_DET_CHESSBOARD:
			return "TVIN_TFMT_3D_DET_CHESSBOARD";
			break;
                default:
                        return "TVIN_TFMT_NULL";
                        break;
        }
}

EXPORT_SYMBOL(tvin_trans_fmt_str);


MODULE_LICENSE("GPL");

