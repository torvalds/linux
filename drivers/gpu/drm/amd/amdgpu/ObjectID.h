/*
* Copyright 2006-2007 Advanced Micro Devices, Inc.  
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*/
/* based on stg/asic_reg/drivers/inc/asic_reg/ObjectID.h ver 23 */

#ifndef _OBJECTID_H
#define _OBJECTID_H

#if defined(_X86_)
#pragma pack(1)
#endif

/****************************************************/
/* Graphics Object Type Definition                  */
/****************************************************/
#define GRAPH_OBJECT_TYPE_NONE                    0x0
#define GRAPH_OBJECT_TYPE_GPU                     0x1
#define GRAPH_OBJECT_TYPE_ENCODER                 0x2
#define GRAPH_OBJECT_TYPE_CONNECTOR               0x3
#define GRAPH_OBJECT_TYPE_ROUTER                  0x4
/* deleted */
#define GRAPH_OBJECT_TYPE_DISPLAY_PATH            0x6  
#define GRAPH_OBJECT_TYPE_GENERIC                 0x7

/****************************************************/
/* Encoder Object ID Definition                     */
/****************************************************/
#define ENCODER_OBJECT_ID_NONE                    0x00 

/* Radeon Class Display Hardware */
#define ENCODER_OBJECT_ID_INTERNAL_LVDS           0x01
#define ENCODER_OBJECT_ID_INTERNAL_TMDS1          0x02
#define ENCODER_OBJECT_ID_INTERNAL_TMDS2          0x03
#define ENCODER_OBJECT_ID_INTERNAL_DAC1           0x04
#define ENCODER_OBJECT_ID_INTERNAL_DAC2           0x05     /* TV/CV DAC */
#define ENCODER_OBJECT_ID_INTERNAL_SDVOA          0x06
#define ENCODER_OBJECT_ID_INTERNAL_SDVOB          0x07

/* External Third Party Encoders */
#define ENCODER_OBJECT_ID_SI170B                  0x08
#define ENCODER_OBJECT_ID_CH7303                  0x09
#define ENCODER_OBJECT_ID_CH7301                  0x0A
#define ENCODER_OBJECT_ID_INTERNAL_DVO1           0x0B    /* This belongs to Radeon Class Display Hardware */
#define ENCODER_OBJECT_ID_EXTERNAL_SDVOA          0x0C
#define ENCODER_OBJECT_ID_EXTERNAL_SDVOB          0x0D
#define ENCODER_OBJECT_ID_TITFP513                0x0E
#define ENCODER_OBJECT_ID_INTERNAL_LVTM1          0x0F    /* not used for Radeon */
#define ENCODER_OBJECT_ID_VT1623                  0x10
#define ENCODER_OBJECT_ID_HDMI_SI1930             0x11
#define ENCODER_OBJECT_ID_HDMI_INTERNAL           0x12
#define ENCODER_OBJECT_ID_ALMOND                  0x22
#define ENCODER_OBJECT_ID_TRAVIS                  0x23
#define ENCODER_OBJECT_ID_NUTMEG                  0x22
#define ENCODER_OBJECT_ID_HDMI_ANX9805            0x26

/* Kaleidoscope (KLDSCP) Class Display Hardware (internal) */
#define ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1   0x13
#define ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1    0x14
#define ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1    0x15
#define ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2    0x16  /* Shared with CV/TV and CRT */
#define ENCODER_OBJECT_ID_SI178                   0X17  /* External TMDS (dual link, no HDCP.) */
#define ENCODER_OBJECT_ID_MVPU_FPGA               0x18  /* MVPU FPGA chip */
#define ENCODER_OBJECT_ID_INTERNAL_DDI            0x19
#define ENCODER_OBJECT_ID_VT1625                  0x1A
#define ENCODER_OBJECT_ID_HDMI_SI1932             0x1B
#define ENCODER_OBJECT_ID_DP_AN9801               0x1C
#define ENCODER_OBJECT_ID_DP_DP501                0x1D
#define ENCODER_OBJECT_ID_INTERNAL_UNIPHY         0x1E
#define ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA   0x1F
#define ENCODER_OBJECT_ID_INTERNAL_UNIPHY1        0x20
#define ENCODER_OBJECT_ID_INTERNAL_UNIPHY2        0x21
#define ENCODER_OBJECT_ID_INTERNAL_VCE            0x24
#define ENCODER_OBJECT_ID_INTERNAL_UNIPHY3        0x25
#define ENCODER_OBJECT_ID_INTERNAL_AMCLK          0x27

#define ENCODER_OBJECT_ID_GENERAL_EXTERNAL_DVO    0xFF

/****************************************************/
/* Connector Object ID Definition                   */
/****************************************************/
#define CONNECTOR_OBJECT_ID_NONE                  0x00 
#define CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I     0x01
#define CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I       0x02
#define CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D     0x03
#define CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D       0x04
#define CONNECTOR_OBJECT_ID_VGA                   0x05
#define CONNECTOR_OBJECT_ID_COMPOSITE             0x06
#define CONNECTOR_OBJECT_ID_SVIDEO                0x07
#define CONNECTOR_OBJECT_ID_YPbPr                 0x08
#define CONNECTOR_OBJECT_ID_D_CONNECTOR           0x09
#define CONNECTOR_OBJECT_ID_9PIN_DIN              0x0A  /* Supports both CV & TV */
#define CONNECTOR_OBJECT_ID_SCART                 0x0B
#define CONNECTOR_OBJECT_ID_HDMI_TYPE_A           0x0C
#define CONNECTOR_OBJECT_ID_HDMI_TYPE_B           0x0D
#define CONNECTOR_OBJECT_ID_LVDS                  0x0E
#define CONNECTOR_OBJECT_ID_7PIN_DIN              0x0F
#define CONNECTOR_OBJECT_ID_PCIE_CONNECTOR        0x10
#define CONNECTOR_OBJECT_ID_CROSSFIRE             0x11
#define CONNECTOR_OBJECT_ID_HARDCODE_DVI          0x12
#define CONNECTOR_OBJECT_ID_DISPLAYPORT           0x13
#define CONNECTOR_OBJECT_ID_eDP                   0x14
#define CONNECTOR_OBJECT_ID_MXM                   0x15
#define CONNECTOR_OBJECT_ID_LVDS_eDP              0x16
#define CONNECTOR_OBJECT_ID_USBC                  0x17

/* deleted */

/****************************************************/
/* Router Object ID Definition                      */
/****************************************************/
#define ROUTER_OBJECT_ID_NONE											0x00
#define ROUTER_OBJECT_ID_I2C_EXTENDER_CNTL				0x01

/****************************************************/
/* Generic Object ID Definition                     */
/****************************************************/
#define GENERIC_OBJECT_ID_NONE                    0x00
#define GENERIC_OBJECT_ID_GLSYNC                  0x01
#define GENERIC_OBJECT_ID_PX2_NON_DRIVABLE        0x02
#define GENERIC_OBJECT_ID_MXM_OPM                 0x03
#define GENERIC_OBJECT_ID_STEREO_PIN              0x04        //This object could show up from Misc Object table, it follows ATOM_OBJECT format, and contains one ATOM_OBJECT_GPIO_CNTL_RECORD for the stereo pin
#define GENERIC_OBJECT_ID_BRACKET_LAYOUT          0x05

/****************************************************/
/* Graphics Object ENUM ID Definition               */
/****************************************************/
#define GRAPH_OBJECT_ENUM_ID1                     0x01
#define GRAPH_OBJECT_ENUM_ID2                     0x02
#define GRAPH_OBJECT_ENUM_ID3                     0x03
#define GRAPH_OBJECT_ENUM_ID4                     0x04
#define GRAPH_OBJECT_ENUM_ID5                     0x05
#define GRAPH_OBJECT_ENUM_ID6                     0x06
#define GRAPH_OBJECT_ENUM_ID7                     0x07

/****************************************************/
/* Graphics Object ID Bit definition                */
/****************************************************/
#define OBJECT_ID_MASK                            0x00FF
#define ENUM_ID_MASK                              0x0700
#define RESERVED1_ID_MASK                         0x0800
#define OBJECT_TYPE_MASK                          0x7000
#define RESERVED2_ID_MASK                         0x8000
                                                  
#define OBJECT_ID_SHIFT                           0x00
#define ENUM_ID_SHIFT                             0x08
#define OBJECT_TYPE_SHIFT                         0x0C


/****************************************************/
/* Graphics Object family definition                */
/****************************************************/
#define CONSTRUCTOBJECTFAMILYID(GRAPHICS_OBJECT_TYPE, GRAPHICS_OBJECT_ID) (GRAPHICS_OBJECT_TYPE << OBJECT_TYPE_SHIFT | \
                                                                           GRAPHICS_OBJECT_ID   << OBJECT_ID_SHIFT)
/****************************************************/
/* GPU Object ID definition - Shared with BIOS      */
/****************************************************/
#define GPU_ENUM_ID1                            ( GRAPH_OBJECT_TYPE_GPU << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT)

/****************************************************/
/* Encoder Object ID definition - Shared with BIOS  */
/****************************************************/
/*
#define ENCODER_INTERNAL_LVDS_ENUM_ID1        0x2101      
#define ENCODER_INTERNAL_TMDS1_ENUM_ID1       0x2102
#define ENCODER_INTERNAL_TMDS2_ENUM_ID1       0x2103
#define ENCODER_INTERNAL_DAC1_ENUM_ID1        0x2104
#define ENCODER_INTERNAL_DAC2_ENUM_ID1        0x2105
#define ENCODER_INTERNAL_SDVOA_ENUM_ID1       0x2106
#define ENCODER_INTERNAL_SDVOB_ENUM_ID1       0x2107
#define ENCODER_SIL170B_ENUM_ID1              0x2108  
#define ENCODER_CH7303_ENUM_ID1               0x2109
#define ENCODER_CH7301_ENUM_ID1               0x210A
#define ENCODER_INTERNAL_DVO1_ENUM_ID1        0x210B
#define ENCODER_EXTERNAL_SDVOA_ENUM_ID1       0x210C
#define ENCODER_EXTERNAL_SDVOB_ENUM_ID1       0x210D
#define ENCODER_TITFP513_ENUM_ID1             0x210E
#define ENCODER_INTERNAL_LVTM1_ENUM_ID1       0x210F
#define ENCODER_VT1623_ENUM_ID1               0x2110
#define ENCODER_HDMI_SI1930_ENUM_ID1          0x2111
#define ENCODER_HDMI_INTERNAL_ENUM_ID1        0x2112
#define ENCODER_INTERNAL_KLDSCP_TMDS1_ENUM_ID1   0x2113
#define ENCODER_INTERNAL_KLDSCP_DVO1_ENUM_ID1    0x2114
#define ENCODER_INTERNAL_KLDSCP_DAC1_ENUM_ID1    0x2115
#define ENCODER_INTERNAL_KLDSCP_DAC2_ENUM_ID1    0x2116  
#define ENCODER_SI178_ENUM_ID1                   0x2117 
#define ENCODER_MVPU_FPGA_ENUM_ID1               0x2118
#define ENCODER_INTERNAL_DDI_ENUM_ID1            0x2119
#define ENCODER_VT1625_ENUM_ID1                  0x211A
#define ENCODER_HDMI_SI1932_ENUM_ID1             0x211B
#define ENCODER_ENCODER_DP_AN9801_ENUM_ID1       0x211C
#define ENCODER_DP_DP501_ENUM_ID1                0x211D
#define ENCODER_INTERNAL_UNIPHY_ENUM_ID1         0x211E
*/
#define ENCODER_INTERNAL_LVDS_ENUM_ID1     ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_INTERNAL_LVDS << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_TMDS1_ENUM_ID1    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_INTERNAL_TMDS1 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_TMDS2_ENUM_ID1    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_INTERNAL_TMDS2 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_DAC1_ENUM_ID1     ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_INTERNAL_DAC1 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_DAC2_ENUM_ID1     ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_INTERNAL_DAC2 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_SDVOA_ENUM_ID1    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_INTERNAL_SDVOA << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_SDVOA_ENUM_ID2    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_INTERNAL_SDVOA << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_SDVOB_ENUM_ID1    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_INTERNAL_SDVOB << OBJECT_ID_SHIFT)

#define ENCODER_SIL170B_ENUM_ID1           ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_SI170B << OBJECT_ID_SHIFT)

#define ENCODER_CH7303_ENUM_ID1            ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_CH7303 << OBJECT_ID_SHIFT)

#define ENCODER_CH7301_ENUM_ID1            ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_CH7301 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_DVO1_ENUM_ID1     ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_INTERNAL_DVO1 << OBJECT_ID_SHIFT)

#define ENCODER_EXTERNAL_SDVOA_ENUM_ID1    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_EXTERNAL_SDVOA << OBJECT_ID_SHIFT)

#define ENCODER_EXTERNAL_SDVOA_ENUM_ID2    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_EXTERNAL_SDVOA << OBJECT_ID_SHIFT)


#define ENCODER_EXTERNAL_SDVOB_ENUM_ID1    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_EXTERNAL_SDVOB << OBJECT_ID_SHIFT)


#define ENCODER_TITFP513_ENUM_ID1          ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_TITFP513 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_LVTM1_ENUM_ID1    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_INTERNAL_LVTM1 << OBJECT_ID_SHIFT)

#define ENCODER_VT1623_ENUM_ID1            ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_VT1623 << OBJECT_ID_SHIFT)

#define ENCODER_HDMI_SI1930_ENUM_ID1       ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_HDMI_SI1930 << OBJECT_ID_SHIFT)

#define ENCODER_HDMI_INTERNAL_ENUM_ID1     ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_HDMI_INTERNAL << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_KLDSCP_TMDS1_ENUM_ID1   ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                   GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                   ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1 << OBJECT_ID_SHIFT)


#define ENCODER_INTERNAL_KLDSCP_TMDS1_ENUM_ID2   ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                   GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                   ENCODER_OBJECT_ID_INTERNAL_KLDSCP_TMDS1 << OBJECT_ID_SHIFT)


#define ENCODER_INTERNAL_KLDSCP_DVO1_ENUM_ID1    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                   GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                   ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DVO1 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_KLDSCP_DAC1_ENUM_ID1    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                   GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                   ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC1 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_KLDSCP_DAC2_ENUM_ID1    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                   GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                   ENCODER_OBJECT_ID_INTERNAL_KLDSCP_DAC2 << OBJECT_ID_SHIFT)  // Shared with CV/TV and CRT

#define ENCODER_SI178_ENUM_ID1                    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                   GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                   ENCODER_OBJECT_ID_SI178 << OBJECT_ID_SHIFT)  

#define ENCODER_MVPU_FPGA_ENUM_ID1                ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                   GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                   ENCODER_OBJECT_ID_MVPU_FPGA << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_DDI_ENUM_ID1     (  GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_INTERNAL_DDI << OBJECT_ID_SHIFT) 

#define ENCODER_VT1625_ENUM_ID1            ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_VT1625 << OBJECT_ID_SHIFT)

#define ENCODER_HDMI_SI1932_ENUM_ID1       ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_HDMI_SI1932 << OBJECT_ID_SHIFT)

#define ENCODER_DP_DP501_ENUM_ID1            ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_DP_DP501 << OBJECT_ID_SHIFT)

#define ENCODER_DP_AN9801_ENUM_ID1            ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                             GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                             ENCODER_OBJECT_ID_DP_AN9801 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_UNIPHY_ENUM_ID1         ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 ENCODER_OBJECT_ID_INTERNAL_UNIPHY << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_UNIPHY_ENUM_ID2         ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 ENCODER_OBJECT_ID_INTERNAL_UNIPHY << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_KLDSCP_LVTMA_ENUM_ID1   ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 ENCODER_OBJECT_ID_INTERNAL_KLDSCP_LVTMA << OBJECT_ID_SHIFT)  

#define ENCODER_INTERNAL_UNIPHY1_ENUM_ID1         ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 ENCODER_OBJECT_ID_INTERNAL_UNIPHY1 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_UNIPHY1_ENUM_ID2         ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 ENCODER_OBJECT_ID_INTERNAL_UNIPHY1 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_UNIPHY2_ENUM_ID1         ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 ENCODER_OBJECT_ID_INTERNAL_UNIPHY2 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_UNIPHY2_ENUM_ID2         ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 ENCODER_OBJECT_ID_INTERNAL_UNIPHY2 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_UNIPHY3_ENUM_ID1         ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 ENCODER_OBJECT_ID_INTERNAL_UNIPHY3 << OBJECT_ID_SHIFT)

#define ENCODER_INTERNAL_UNIPHY3_ENUM_ID2         ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 ENCODER_OBJECT_ID_INTERNAL_UNIPHY3 << OBJECT_ID_SHIFT)

#define ENCODER_GENERAL_EXTERNAL_DVO_ENUM_ID1    ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                  GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                  ENCODER_OBJECT_ID_GENERAL_EXTERNAL_DVO << OBJECT_ID_SHIFT)

#define ENCODER_ALMOND_ENUM_ID1                  ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                  GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                  ENCODER_OBJECT_ID_ALMOND << OBJECT_ID_SHIFT)

#define ENCODER_ALMOND_ENUM_ID2                  ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                  GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                  ENCODER_OBJECT_ID_ALMOND << OBJECT_ID_SHIFT)

#define ENCODER_TRAVIS_ENUM_ID1                  ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                  GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                  ENCODER_OBJECT_ID_TRAVIS << OBJECT_ID_SHIFT)

#define ENCODER_TRAVIS_ENUM_ID2                  ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                  GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                  ENCODER_OBJECT_ID_TRAVIS << OBJECT_ID_SHIFT)

#define ENCODER_NUTMEG_ENUM_ID1                  ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                  GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                  ENCODER_OBJECT_ID_NUTMEG << OBJECT_ID_SHIFT)

#define ENCODER_VCE_ENUM_ID1                     ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                  GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                  ENCODER_OBJECT_ID_INTERNAL_VCE << OBJECT_ID_SHIFT)

#define ENCODER_HDMI_ANX9805_ENUM_ID1            ( GRAPH_OBJECT_TYPE_ENCODER << OBJECT_TYPE_SHIFT |\
                                                  GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                  ENCODER_OBJECT_ID_HDMI_ANX9805 << OBJECT_ID_SHIFT)

/****************************************************/
/* Connector Object ID definition - Shared with BIOS */
/****************************************************/
/*
#define CONNECTOR_SINGLE_LINK_DVI_I_ENUM_ID1        0x3101
#define CONNECTOR_DUAL_LINK_DVI_I_ENUM_ID1          0x3102
#define CONNECTOR_SINGLE_LINK_DVI_D_ENUM_ID1        0x3103
#define CONNECTOR_DUAL_LINK_DVI_D_ENUM_ID1          0x3104
#define CONNECTOR_VGA_ENUM_ID1                      0x3105
#define CONNECTOR_COMPOSITE_ENUM_ID1                0x3106
#define CONNECTOR_SVIDEO_ENUM_ID1                   0x3107
#define CONNECTOR_YPbPr_ENUM_ID1                    0x3108
#define CONNECTOR_D_CONNECTORE_ENUM_ID1             0x3109
#define CONNECTOR_9PIN_DIN_ENUM_ID1                 0x310A
#define CONNECTOR_SCART_ENUM_ID1                    0x310B
#define CONNECTOR_HDMI_TYPE_A_ENUM_ID1              0x310C
#define CONNECTOR_HDMI_TYPE_B_ENUM_ID1              0x310D
#define CONNECTOR_LVDS_ENUM_ID1                     0x310E
#define CONNECTOR_7PIN_DIN_ENUM_ID1                 0x310F
#define CONNECTOR_PCIE_CONNECTOR_ENUM_ID1           0x3110
*/
#define CONNECTOR_LVDS_ENUM_ID1                ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_LVDS << OBJECT_ID_SHIFT)

#define CONNECTOR_LVDS_ENUM_ID2                ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_LVDS << OBJECT_ID_SHIFT)

#define CONNECTOR_eDP_ENUM_ID1                 ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_eDP << OBJECT_ID_SHIFT)

#define CONNECTOR_eDP_ENUM_ID2                 ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_eDP << OBJECT_ID_SHIFT)

#define CONNECTOR_SINGLE_LINK_DVI_I_ENUM_ID1   ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I << OBJECT_ID_SHIFT)

#define CONNECTOR_SINGLE_LINK_DVI_I_ENUM_ID2   ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_I << OBJECT_ID_SHIFT)

#define CONNECTOR_DUAL_LINK_DVI_I_ENUM_ID1     ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I << OBJECT_ID_SHIFT)

#define CONNECTOR_DUAL_LINK_DVI_I_ENUM_ID2     ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_I << OBJECT_ID_SHIFT)

#define CONNECTOR_SINGLE_LINK_DVI_D_ENUM_ID1   ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D << OBJECT_ID_SHIFT)

#define CONNECTOR_SINGLE_LINK_DVI_D_ENUM_ID2   ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D << OBJECT_ID_SHIFT)

#define CONNECTOR_SINGLE_LINK_DVI_D_ENUM_ID3   ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID3 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D << OBJECT_ID_SHIFT)

#define CONNECTOR_SINGLE_LINK_DVI_D_ENUM_ID4   ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID4 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D << OBJECT_ID_SHIFT)

#define CONNECTOR_SINGLE_LINK_DVI_D_ENUM_ID5   ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID5 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D << OBJECT_ID_SHIFT)

#define CONNECTOR_SINGLE_LINK_DVI_D_ENUM_ID6   ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID6 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_SINGLE_LINK_DVI_D << OBJECT_ID_SHIFT)

#define CONNECTOR_DUAL_LINK_DVI_D_ENUM_ID1     ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D << OBJECT_ID_SHIFT)

#define CONNECTOR_DUAL_LINK_DVI_D_ENUM_ID2     ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D << OBJECT_ID_SHIFT)

#define CONNECTOR_DUAL_LINK_DVI_D_ENUM_ID3     ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID3 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D << OBJECT_ID_SHIFT)

#define CONNECTOR_DUAL_LINK_DVI_D_ENUM_ID4     ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID4 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_DUAL_LINK_DVI_D << OBJECT_ID_SHIFT)

#define CONNECTOR_VGA_ENUM_ID1                 ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_VGA << OBJECT_ID_SHIFT)

#define CONNECTOR_VGA_ENUM_ID2                 ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_VGA << OBJECT_ID_SHIFT)

#define CONNECTOR_COMPOSITE_ENUM_ID1           ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_COMPOSITE << OBJECT_ID_SHIFT)

#define CONNECTOR_COMPOSITE_ENUM_ID2           ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_COMPOSITE << OBJECT_ID_SHIFT)

#define CONNECTOR_SVIDEO_ENUM_ID1              ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_SVIDEO << OBJECT_ID_SHIFT)

#define CONNECTOR_SVIDEO_ENUM_ID2              ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_SVIDEO << OBJECT_ID_SHIFT)

#define CONNECTOR_YPbPr_ENUM_ID1               ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_YPbPr << OBJECT_ID_SHIFT)

#define CONNECTOR_YPbPr_ENUM_ID2               ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_YPbPr << OBJECT_ID_SHIFT)

#define CONNECTOR_D_CONNECTOR_ENUM_ID1         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_D_CONNECTOR << OBJECT_ID_SHIFT)

#define CONNECTOR_D_CONNECTOR_ENUM_ID2         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_D_CONNECTOR << OBJECT_ID_SHIFT)

#define CONNECTOR_9PIN_DIN_ENUM_ID1            ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_9PIN_DIN << OBJECT_ID_SHIFT)

#define CONNECTOR_9PIN_DIN_ENUM_ID2            ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_9PIN_DIN << OBJECT_ID_SHIFT)

#define CONNECTOR_SCART_ENUM_ID1               ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_SCART << OBJECT_ID_SHIFT)

#define CONNECTOR_SCART_ENUM_ID2               ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_SCART << OBJECT_ID_SHIFT)

#define CONNECTOR_HDMI_TYPE_A_ENUM_ID1         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_HDMI_TYPE_A << OBJECT_ID_SHIFT)

#define CONNECTOR_HDMI_TYPE_A_ENUM_ID2         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_HDMI_TYPE_A << OBJECT_ID_SHIFT)

#define CONNECTOR_HDMI_TYPE_A_ENUM_ID3         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID3 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_HDMI_TYPE_A << OBJECT_ID_SHIFT)

#define CONNECTOR_HDMI_TYPE_A_ENUM_ID4         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID4 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_HDMI_TYPE_A << OBJECT_ID_SHIFT)

#define CONNECTOR_HDMI_TYPE_A_ENUM_ID5         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID5 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_HDMI_TYPE_A << OBJECT_ID_SHIFT)

#define CONNECTOR_HDMI_TYPE_A_ENUM_ID6         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID6 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_HDMI_TYPE_A << OBJECT_ID_SHIFT)

#define CONNECTOR_HDMI_TYPE_B_ENUM_ID1         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_HDMI_TYPE_B << OBJECT_ID_SHIFT)

#define CONNECTOR_HDMI_TYPE_B_ENUM_ID2         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_HDMI_TYPE_B << OBJECT_ID_SHIFT)

#define CONNECTOR_7PIN_DIN_ENUM_ID1            ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_7PIN_DIN << OBJECT_ID_SHIFT)

#define CONNECTOR_7PIN_DIN_ENUM_ID2            ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_7PIN_DIN << OBJECT_ID_SHIFT)

#define CONNECTOR_PCIE_CONNECTOR_ENUM_ID1      ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_PCIE_CONNECTOR << OBJECT_ID_SHIFT)

#define CONNECTOR_PCIE_CONNECTOR_ENUM_ID2      ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_PCIE_CONNECTOR << OBJECT_ID_SHIFT)

#define CONNECTOR_CROSSFIRE_ENUM_ID1           ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_CROSSFIRE << OBJECT_ID_SHIFT)

#define CONNECTOR_CROSSFIRE_ENUM_ID2           ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_CROSSFIRE << OBJECT_ID_SHIFT)


#define CONNECTOR_HARDCODE_DVI_ENUM_ID1        ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_HARDCODE_DVI << OBJECT_ID_SHIFT)

#define CONNECTOR_HARDCODE_DVI_ENUM_ID2        ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_HARDCODE_DVI << OBJECT_ID_SHIFT)

#define CONNECTOR_DISPLAYPORT_ENUM_ID1         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_DISPLAYPORT << OBJECT_ID_SHIFT)

#define CONNECTOR_DISPLAYPORT_ENUM_ID2         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_DISPLAYPORT << OBJECT_ID_SHIFT)

#define CONNECTOR_DISPLAYPORT_ENUM_ID3         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID3 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_DISPLAYPORT << OBJECT_ID_SHIFT)

#define CONNECTOR_DISPLAYPORT_ENUM_ID4         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID4 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_DISPLAYPORT << OBJECT_ID_SHIFT)

#define CONNECTOR_DISPLAYPORT_ENUM_ID5         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID5 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_DISPLAYPORT << OBJECT_ID_SHIFT)

#define CONNECTOR_DISPLAYPORT_ENUM_ID6         ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID6 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_DISPLAYPORT << OBJECT_ID_SHIFT)

#define CONNECTOR_MXM_ENUM_ID1                 ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_MXM << OBJECT_ID_SHIFT)          //Mapping to MXM_DP_A

#define CONNECTOR_MXM_ENUM_ID2                 ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_MXM << OBJECT_ID_SHIFT)          //Mapping to MXM_DP_B

#define CONNECTOR_MXM_ENUM_ID3                 ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID3 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_MXM << OBJECT_ID_SHIFT)          //Mapping to MXM_DP_C

#define CONNECTOR_MXM_ENUM_ID4                 ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID4 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_MXM << OBJECT_ID_SHIFT)          //Mapping to MXM_DP_D

#define CONNECTOR_MXM_ENUM_ID5                 ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID5 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_MXM << OBJECT_ID_SHIFT)          //Mapping to MXM_LVDS_TXxx

#define CONNECTOR_MXM_ENUM_ID6                 ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID6 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_MXM << OBJECT_ID_SHIFT)          //Mapping to MXM_LVDS_UXxx

#define CONNECTOR_MXM_ENUM_ID7                 ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID7 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_MXM << OBJECT_ID_SHIFT)          //Mapping to MXM_DAC

#define CONNECTOR_LVDS_eDP_ENUM_ID1            ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_LVDS_eDP << OBJECT_ID_SHIFT)

#define CONNECTOR_LVDS_eDP_ENUM_ID2            ( GRAPH_OBJECT_TYPE_CONNECTOR << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 CONNECTOR_OBJECT_ID_LVDS_eDP << OBJECT_ID_SHIFT)

/****************************************************/
/* Router Object ID definition - Shared with BIOS   */
/****************************************************/
#define ROUTER_I2C_EXTENDER_CNTL_ENUM_ID1      ( GRAPH_OBJECT_TYPE_ROUTER << OBJECT_TYPE_SHIFT |\
                                                GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                ROUTER_OBJECT_ID_I2C_EXTENDER_CNTL << OBJECT_ID_SHIFT)

/* deleted */

/****************************************************/
/* Generic Object ID definition - Shared with BIOS  */
/****************************************************/
#define GENERICOBJECT_GLSYNC_ENUM_ID1           (GRAPH_OBJECT_TYPE_GENERIC << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 GENERIC_OBJECT_ID_GLSYNC << OBJECT_ID_SHIFT)

#define GENERICOBJECT_PX2_NON_DRIVABLE_ID1       (GRAPH_OBJECT_TYPE_GENERIC << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 GENERIC_OBJECT_ID_PX2_NON_DRIVABLE<< OBJECT_ID_SHIFT)

#define GENERICOBJECT_PX2_NON_DRIVABLE_ID2       (GRAPH_OBJECT_TYPE_GENERIC << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 GENERIC_OBJECT_ID_PX2_NON_DRIVABLE<< OBJECT_ID_SHIFT)

#define GENERICOBJECT_MXM_OPM_ENUM_ID1           (GRAPH_OBJECT_TYPE_GENERIC << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 GENERIC_OBJECT_ID_MXM_OPM << OBJECT_ID_SHIFT)

#define GENERICOBJECT_STEREO_PIN_ENUM_ID1        (GRAPH_OBJECT_TYPE_GENERIC << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 GENERIC_OBJECT_ID_STEREO_PIN << OBJECT_ID_SHIFT)

#define GENERICOBJECT_BRACKET_LAYOUT_ENUM_ID1    (GRAPH_OBJECT_TYPE_GENERIC << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID1 << ENUM_ID_SHIFT |\
                                                 GENERIC_OBJECT_ID_BRACKET_LAYOUT << OBJECT_ID_SHIFT)

#define GENERICOBJECT_BRACKET_LAYOUT_ENUM_ID2    (GRAPH_OBJECT_TYPE_GENERIC << OBJECT_TYPE_SHIFT |\
                                                 GRAPH_OBJECT_ENUM_ID2 << ENUM_ID_SHIFT |\
                                                 GENERIC_OBJECT_ID_BRACKET_LAYOUT << OBJECT_ID_SHIFT)
/****************************************************/
/* Object Cap definition - Shared with BIOS         */
/****************************************************/
#define GRAPHICS_OBJECT_CAP_I2C                 0x00000001L
#define GRAPHICS_OBJECT_CAP_TABLE_ID            0x00000002L


#define GRAPHICS_OBJECT_I2CCOMMAND_TABLE_ID                   0x01
#define GRAPHICS_OBJECT_HOTPLUGDETECTIONINTERUPT_TABLE_ID     0x02
#define GRAPHICS_OBJECT_ENCODER_OUTPUT_PROTECTION_TABLE_ID    0x03

#if defined(_X86_)
#pragma pack()
#endif

#endif  /*GRAPHICTYPE */




