///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <hdmitx_sys.c>
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2012/12/20
//   @fileversion: ITE_HDMITX_SAMPLE_3.14
//******************************************/

///////////////////////////////////////////////////////////////////////////////
// This is the sample program for CAT6611 driver usage.
///////////////////////////////////////////////////////////////////////////////
#include <linux/kernel.h>
#include <linux/delay.h>

#include "hdmitx.h"
#include "hdmitx_sys.h"

_CODE HDMITXDEV InstanceData =
{

    0,      // BYTE I2C_DEV ;
    HDMI_TX_I2C_SLAVE_ADDR,    // BYTE I2C_ADDR ;

    /////////////////////////////////////////////////
    // Interrupt Type
    /////////////////////////////////////////////////
    0x40,      // BYTE bIntType ; // = 0 ;
    /////////////////////////////////////////////////
    // Video Property
    /////////////////////////////////////////////////
    INPUT_SIGNAL_TYPE ,// BYTE bInputVideoSignalType ; // for Sync Embedded,CCIR656,InputDDR

    /////////////////////////////////////////////////
    // Audio Property
    /////////////////////////////////////////////////
    I2S_FORMAT, // BYTE bOutputAudioMode ; // = 0 ;
    FALSE , // BYTE bAudioChannelSwap ; // = 0 ;
    0x01, // BYTE bAudioChannelEnable ;
    INPUT_SAMPLE_FREQ ,// BYTE bAudFs ;
    0, // unsigned long TMDSClock ;
    FALSE, // BYTE bAuthenticated:1 ;
    FALSE, // BYTE bHDMIMode: 1;
    FALSE, // BYTE bIntPOL:1 ; // 0 = Low Active
    FALSE, // BYTE bHPD:1 ;
};

#ifdef HDMITX_INPUT_INFO
// HDMI_VTiming currVTiming ;
////////////////////////////////////////////////////////////////////////////////
// HDMI VTable
////////////////////////////////////////////////////////////////////////////////
static HDMI_VTiming _CODE s_VMTable[] = {

    { 1,0,640,480,800,525,25175000L,0x89,16,96,48,10,2,33,PROG,Vneg,Hneg},//640x480@60Hz
    { 2,0,720,480,858,525,27000000L,0x80,16,62,60,9,6,30,PROG,Vneg,Hneg},//720x480@60Hz
    { 3,0,720,480,858,525,27000000L,0x80,16,62,60,9,6,30,PROG,Vneg,Hneg},//720x480@60Hz
    { 4,0,1280,720,1650,750,74250000L,0x2E,110,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@60Hz
    { 5,0,1920,540,2200,562,74250000L,0x2E,88,44,148,2,5,15,INTERLACE,Vpos,Hpos},//1920x1080(I)@60Hz
    { 6,1,720,240,858,262,13500000L,0x100,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@60Hz
    { 7,1,720,240,858,262,13500000L,0x100,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@60Hz
    { 8,1,720,240,858,262,13500000L,0x100,19,62,57,4,3,15,PROG,Vneg,Hneg},//720x480(I)@60Hz
    { 9,1,720,240,858,262,13500000L,0x100,19,62,57,4,3,15,PROG,Vneg,Hneg},//720x480(I)@60Hz
    {10,2,720,240,858,262,54000000L,0x40,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@60Hz
    {11,2,720,240,858,262,54000000L,0x40,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@60Hz
    {12,2,720,240,858,262,54000000L,0x40,19,62,57,4,3,15,PROG,Vneg,Hneg},//720x480(I)@60Hz
    {13,2,720,240,858,262,54000000L,0x40,19,62,57,4,3,15,PROG,Vneg,Hneg},//720x480(I)@60Hz
    {14,1,1440,480,1716,525,54000000L,0x40,32,124,120,9,6,30,PROG,Vneg,Hneg},//1440x480@60Hz
    {15,1,1440,480,1716,525,54000000L,0x40,32,124,120,9,6,30,PROG,Vneg,Hneg},//1440x480@60Hz
    {16,0,1920,1080,2200,1125,148500000L,0x17,88,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@60Hz
    {17,0,720,576,864,625,27000000L,0x80,12,64,68,5,5,39,PROG,Vneg,Hneg},//720x576@50Hz
    {18,0,720,576,864,625,27000000L,0x80,12,64,68,5,5,39,PROG,Vneg,Hneg},//720x576@50Hz
    {19,0,1280,720,1980,750,74250000L,0x2E,440,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@50Hz
    {20,0,1920,540,2640,562,74250000L,0x2E,528,44,148,2,5,15,INTERLACE,Vpos,Hpos},//1920x1080(I)@50Hz
    {21,1,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@50Hz
    {22,1,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@50Hz
    {23,1,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,PROG,Vneg,Hneg},//1440x288@50Hz
    {24,1,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,PROG,Vneg,Hneg},//1440x288@50Hz
    {25,2,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@50Hz
    {26,2,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@50Hz
    {27,2,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,PROG,Vneg,Hneg},//1440x288@50Hz
    {28,2,720,288,864,312,13500000L,0x100,12,63,69,2,3,19,PROG,Vneg,Hneg},//1440x288@50Hz
    {29,1,1440,576,1728,625,54000000L,0x40,24,128,136,5,5,39,PROG,Vpos,Hneg},//1440x576@50Hz
    {30,1,1440,576,1728,625,54000000L,0x40,24,128,136,5,5,39,PROG,Vpos,Hneg},//1440x576@50Hz
    {31,0,1920,1080,2640,1125,148500000L,0x17,528,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@50Hz
    {32,0,1920,1080,2750,1125,74250000L,0x2E,638,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@24Hz
    {33,0,1920,1080,2640,1125,74250000L,0x2E,528,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@25Hz
    {34,0,1920,1080,2200,1125,74250000L,0x2E,88,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@30Hz

    {35,2,2880,480,1716*2,525,108000000L,0x20,32*2,124*2,120*2,9,6,30,PROG,Vneg,Hneg},//2880x480@60Hz
    {36,2,2880,480,1716*2,525,108000000L,0x20,32*2,124*2,120*2,9,6,30,PROG,Vneg,Hneg},//2880x480@60Hz
    {37,1,2880,576,3456,625,108000000L,0x20,24*2,128*2,136*2,5,5,39,PROG,Vneg,Hneg},//2880x576@50Hz
    {38,2,2880,576,3456,625,108000000L,0x20,24*2,128*2,136*2,5,5,39,PROG,Vneg,Hneg},//2880x576@50Hz

    {39,0,1920,540,2304,625,72000000L,0x17,32,168,184,23,5,57,INTERLACE,Vneg,Hpos},//1920x1080@50Hz
    // 100Hz
    {40,0,1920,540,2640,562,148500000L,0x17,528,44,148,2,5,15,INTERLACE,Vpos,Hpos},//1920x1080(I)@100Hz
    {41,0,1280,720,1980,750,148500000L,0x17,440,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@100Hz
    {42,0,720,576,864,625,   54000000L,0x40,12,64,68,5,5,39,PROG,Vneg,Hneg},//720x576@100Hz
    {43,0,720,576,864,625,   54000000L,0x40,12,64,68,5,5,39,PROG,Vneg,Hneg},//720x576@100Hz
    {44,1,720,288,864,312,   27000000L,0x80,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@100Hz
    {45,1,720,288,864,312,   27000000L,0x80,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@100Hz
    // 120Hz
    {46,0,1920,540,2200,562,148500000L,0x17,88,44,148,2,5,15,INTERLACE,Vpos,Hpos},//1920x1080(I)@120Hz
    {47,0,1280,720,1650,750,148500000L,0x17,110,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@120Hz
    {48,0, 720,480, 858,525, 54000000L,0x40,16,62,60,9,6,30,PROG,Vneg,Hneg},//720x480@120Hz
    {49,0, 720,480, 858,525, 54000000L,0x40,16,62,60,9,6,30,PROG,Vneg,Hneg},//720x480@120Hz
    {50,1, 720,240, 858,262, 27000000L,0x80,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@120Hz
    {51,1, 720,240, 858,262, 27000000L,0x80,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@120Hz

    // 200Hz
    {52,0,720,576,864,625,108000000L,0x20,12,64,68,5,5,39,PROG,Vneg,Hneg},//720x576@200Hz
    {53,0,720,576,864,625,108000000L,0x20,12,64,68,5,5,39,PROG,Vneg,Hneg},//720x576@200Hz
    {54,1,720,288,864,312, 54000000L,0x40,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@200Hz
    {55,1,720,288,864,312, 54000000L,0x40,12,63,69,2,3,19,INTERLACE,Vneg,Hneg},//1440x576(I)@200Hz
    // 240Hz
    {56,0,720,480,858,525,108000000L,0x20,16,62,60,9,6,30,PROG,Vneg,Hneg},//720x480@120Hz
    {57,0,720,480,858,525,108000000L,0x20,16,62,60,9,6,30,PROG,Vneg,Hneg},//720x480@120Hz
    {58,1,720,240,858,262, 54000000L,0x40,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@120Hz
    {59,1,720,240,858,262, 54000000L,0x40,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},//720x480(I)@120Hz
    // 720p low resolution
    {60,0,1280, 720,3300, 750, 59400000L,0x3A,1760,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@24Hz
    {61,0,1280, 720,3960, 750, 74250000L,0x2E,2420,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@25Hz
    {62,0,1280, 720,3300, 750, 74250000L,0x2E,1760,40,220,5,5,20,PROG,Vpos,Hpos},//1280x720@30Hz
    // 1080p high refresh rate
    {63,0,1920,1080,2200,1125,297000000L,0x0B, 88,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@120Hz
    {64,0,1920,1080,2640,1125,297000000L,0x0B,528,44,148,4,5,36,PROG,Vpos,Hpos},//1920x1080@100Hz
    // VESA mode
    {0,0,640,350,832,445,31500000L,0x6D,32,64,96,32,3,60,PROG,Vneg,Hpos},// 640x350@85
    {0,0,640,400,832,445,31500000L,0x6D,32,64,96,1,3,41,PROG,Vneg,Hneg},// 640x400@85
    {0,0,832,624,1152,667,57283000L,0x3C,32,64,224,1,3,39,PROG,Vneg,Hneg},// 832x624@75Hz
    {0,0,720,350,900,449,28322000L,0x7A,18,108,54,59,2,38,PROG,Vneg,Hneg},// 720x350@70Hz
    {0,0,720,400,900,449,28322000L,0x7A,18,108,54,13,2,34,PROG,Vpos,Hneg},// 720x400@70Hz
    {0,0,720,400,936,446,35500000L,0x61,36,72,108,1,3,42,PROG,Vpos,Hneg},// 720x400@85
    {0,0,640,480,800,525,25175000L,0x89,16,96,48,10,2,33,PROG,Vneg,Hneg},// 640x480@60
    {0,0,640,480,832,520,31500000L,0x6D,24,40,128,9,3,28,PROG,Vneg,Hneg},// 640x480@72
    {0,0,640,480,840,500,31500000L,0x6D,16,64,120,1,3,16,PROG,Vneg,Hneg},// 640x480@75
    {0,0,640,480,832,509,36000000L,0x60,56,56,80,1,3,25,PROG,Vneg,Hneg},// 640x480@85
    {0,0,800,600,1024,625,36000000L,0x60,24,72,128,1,2,22,PROG,Vpos,Hpos},// 800x600@56
    {0,0,800,600,1056,628,40000000L,0x56,40,128,88,1,4,23,PROG,Vpos,Hpos},// 800x600@60
    {0,0,800,600,1040,666,50000000L,0x45,56,120,64,37,6,23,PROG,Vpos,Hpos},// 800x600@72
    {0,0,800,600,1056,625,49500000L,0x45,16,80,160,1,3,21,PROG,Vpos,Hpos},// 800x600@75
    {0,0,800,600,1048,631,56250000L,0x3D,32,64,152,1,3,27,PROG,Vpos,Hpos},// 800X600@85
    {0,0,848,480,1088,517,33750000L,0x66,16,112,112,6,8,23,PROG,Vpos,Hpos},// 840X480@60
    {0,0,1024,384,1264,408,44900000L,0x4C,8,176,56,0,4,20,INTERLACE,Vpos,Hpos},//1024x768(I)@87Hz
    {0,0,1024,768,1344,806,65000000L,0x35,24,136,160,3,6,29,PROG,Vneg,Hneg},// 1024x768@60
    {0,0,1024,768,1328,806,75000000L,0x2E,24,136,144,3,6,29,PROG,Vneg,Hneg},// 1024x768@70
    {0,0,1024,768,1312,800,78750000L,0x2B,16,96,176,1,3,28,PROG,Vpos,Hpos},// 1024x768@75
    {0,0,1024,768,1376,808,94500000L,0x24,48,96,208,1,3,36,PROG,Vpos,Hpos},// 1024x768@85
    {0,0,1152,864,1600,900,108000000L,0x20,64,128,256,1,3,32,PROG,Vpos,Hpos},// 1152x864@75
    {0,0,1280,768,1440,790,68250000L,0x32,48,32,80,3,7,12,PROG,Vneg,Hpos},// 1280x768@60-R
    {0,0,1280,768,1664,798,79500000L,0x2B,64,128,192,3,7,20,PROG,Vpos,Hneg},// 1280x768@60
    {0,0,1280,768,1696,805,102250000L,0x21,80,128,208,3,7,27,PROG,Vpos,Hneg},// 1280x768@75
    {0,0,1280,768,1712,809,117500000L,0x1D,80,136,216,3,7,31,PROG,Vpos,Hneg},// 1280x768@85

    {0,0,1280,800,1440, 823, 71000000L,0x31, 48, 32, 80,3,6,14,PROG,Vpos,Hneg},// 1280x800@60Hz
    {0,0,1280,800,1680, 831, 83500000L,0x29, 72,128,200,3,6,22,PROG,Vpos,Hneg},// 1280x800@60Hz
    {0,0,1280,800,1696, 838,106500000L,0x20, 80,128,208,3,6,29,PROG,Vpos,Hneg},// 1280x800@75Hz
    {0,0,1280,800,1712, 843,122500000L,0x1C, 80,136,216,3,6,34,PROG,Vpos,Hneg},// 1280x800@85Hz


	{0,0,1280,960,1800,1000,108000000L,0x20,96,112,312,1,3,36,PROG,Vpos,Hpos},// 1280x960@60
    {0,0,1280,960,1728,1011,148500000L,0x17,64,160,224,1,3,47,PROG,Vpos,Hpos},// 1280x960@85
    {0,0,1280,1024,1688,1066,108000000L,0x20,48,112,248,1,3,38,PROG,Vpos,Hpos},// 1280x1024@60
    {0,0,1280,1024,1688,1066,135000000L,0x19,16,144,248,1,3,38,PROG,Vpos,Hpos},// 1280x1024@75
    {0,0,1280,1024,1728,1072,157500000L,0x15,64,160,224,1,3,44,PROG,Vpos,Hpos},// 1280X1024@85
    {0,0,1360,768,1792,795,85500000L,0x28,64,112,256,3,6,18,PROG,Vpos,Hpos},// 1360X768@60

    {0,0,1366,768,1792,798,85500000L,0x28, 70,143,213,3,3,24,PROG,Vpos,Hpos},// 1366X768@60
    {0,0,1366,768,1500,800,72000000L,0x30, 14, 56, 64,1,3,28,PROG,Vpos,Hpos},// 1360X768@60
    {0,0,1400,1050,1560,1080,101000000L,0x22,48,32,80,3,4,23,PROG,Vneg,Hpos},// 1400x768@60-R
    {0,0,1400,1050,1864,1089,121750000L,0x1C,88,144,232,3,4,32,PROG,Vpos,Hneg},// 1400x768@60
    {0,0,1400,1050,1896,1099,156000000L,0x16,104,144,248,3,4,42,PROG,Vpos,Hneg},// 1400x1050@75
    {0,0,1400,1050,1912,1105,179500000L,0x13,104,152,256,3,4,48,PROG,Vpos,Hneg},// 1400x1050@85
    {0,0,1440,900,1600,926,88750000L,0x26,48,32,80,3,6,17,PROG,Vneg,Hpos},// 1440x900@60-R
    {0,0,1440,900,1904,934,106500000L,0x20,80,152,232,3,6,25,PROG,Vpos,Hneg},// 1440x900@60
    {0,0,1440,900,1936,942,136750000L,0x19,96,152,248,3,6,33,PROG,Vpos,Hneg},// 1440x900@75
    {0,0,1440,900,1952,948,157000000L,0x16,104,152,256,3,6,39,PROG,Vpos,Hneg},// 1440x900@85
    {0,0,1600,1200,2160,1250,162000000L,0x15,64,192,304,1,3,46,PROG,Vpos,Hpos},// 1600x1200@60
    {0,0,1600,1200,2160,1250,175500000L,0x13,64,192,304,1,3,46,PROG,Vpos,Hpos},// 1600x1200@65
    {0,0,1600,1200,2160,1250,189000000L,0x12,64,192,304,1,3,46,PROG,Vpos,Hpos},// 1600x1200@70
    {0,0,1600,1200,2160,1250,202500000L,0x11,64,192,304,1,3,46,PROG,Vpos,Hpos},// 1600x1200@75
    {0,0,1600,1200,2160,1250,229500000L,0x0F,64,192,304,1,3,46,PROG,Vpos,Hpos},// 1600x1200@85
    {0,0,1680,1050,1840,1080,119000000L,0x1D,48,32,80,3,6,21,PROG,Vneg,Hpos},// 1680x1050@60-R
    {0,0,1680,1050,2240,1089,146250000L,0x17,104,176,280,3,6,30,PROG,Vpos,Hneg},// 1680x1050@60
    {0,0,1680,1050,2272,1099,187000000L,0x12,120,176,296,3,6,40,PROG,Vpos,Hneg},// 1680x1050@75
    {0,0,1680,1050,2288,1105,214750000L,0x10,128,176,304,3,6,46,PROG,Vpos,Hneg},// 1680x1050@85
    {0,0,1792,1344,2448,1394,204750000L,0x10,128,200,328,1,3,46,PROG,Vpos,Hneg},// 1792x1344@60
    {0,0,1792,1344,2456,1417,261000000L,0x0D,96,216,352,1,3,69,PROG,Vpos,Hneg},// 1792x1344@75
    {0,0,1856,1392,2528,1439,218250000L,0x0F,96,224,352,1,3,43,PROG,Vpos,Hneg},// 1856x1392@60
    {0,0,1856,1392,2560,1500,288000000L,0x0C,128,224,352,1,3,104,PROG,Vpos,Hneg},// 1856x1392@75
    {0,0,1920,1200,2080,1235,154000000L,0x16,48,32,80,3,6,26,PROG,Vneg,Hpos},// 1920x1200@60-R
    {0,0,1920,1200,2592,1245,193250000L,0x11,136,200,336,3,6,36,PROG,Vpos,Hneg},// 1920x1200@60
    {0,0,1920,1200,2608,1255,245250000L,0x0E,136,208,344,3,6,46,PROG,Vpos,Hneg},// 1920x1200@75
    {0,0,1920,1200,2624,1262,281250000L,0x0C,144,208,352,3,6,53,PROG,Vpos,Hneg},// 1920x1200@85
    {0,0,1920,1440,2600,1500,234000000L,0x0E,128,208,344,1,3,56,PROG,Vpos,Hneg},// 1920x1440@60
    {0,0,1920,1440,2640,1500,297000000L,0x0B,144,224,352,1,3,56,PROG,Vpos,Hneg},// 1920x1440@75
};
#define     SizeofVMTable   (sizeof(s_VMTable)/sizeof(HDMI_VTiming))

void HDMITX_MonitorInputVideoChange();
void HDMITX_MonitorInputAudioChange();

#else
#define     SizeofVMTable    0
#endif

#define DIFF(a,b) (((a)>(b))?((a)-(b)):((b)-(a)))

////////////////////////////////////////////////////////////////////////////////
// EDID
////////////////////////////////////////////////////////////////////////////////
static _XDATA RX_CAP RxCapability ;
static BOOL bChangeMode = FALSE ;
static BOOL bChangeAudio = FALSE ;

_XDATA unsigned char CommunBuff[128] ;
// _XDATA AVI_InfoFrame AviInfo;
// _XDATA Audio_InfoFrame AudioInfo ;
// _XDATA VendorSpecific_InfoFrame VS_Info;
_CODE BYTE CA[] = { 0,0,0, 02, 0x3, 0x7, 0xB, 0xF, 0x1F } ;
////////////////////////////////////////////////////////////////////////////////
// Program utility.
////////////////////////////////////////////////////////////////////////////////


BYTE bInputColorMode = INPUT_COLOR_MODE;
BYTE OutputColorDepth = INPUT_COLOR_DEPTH ;
BYTE bOutputColorMode = OUTPUT_COLOR_MODE ;

BYTE iVideoModeSelect=0 ;

ULONG VideoPixelClock ;
BYTE VIC ; // 480p60
BYTE pixelrep ; // no pixelrepeating
HDMI_Aspec aspec ;
HDMI_Colorimetry Colorimetry ;

ULONG ulAudioSampleFS = INPUT_SAMPLE_FREQ_HZ ;
// BYTE bAudioSampleFreq = INPUT_SAMPLE_FREQ ;
BYTE bOutputAudioChannel = OUTPUT_CHANNEL ;

BOOL bHDMIMode;
BYTE bAudioEnable ;
BYTE HPDStatus = FALSE;
BYTE HPDChangeStatus = FALSE;
BYTE bOutputAudioType=CNOFIG_INPUT_AUDIO_TYPE;
////////////////////////////////////////////////////////////////////////////////
// Function Prototype.
////////////////////////////////////////////////////////////////////////////////
BYTE ParseEDID();
static BOOL ParseCEAEDID(BYTE *pCEAEDID);
void ConfigAVIInfoFrame(BYTE VIC, BYTE pixelrep);
void ConfigAudioInfoFrm();
void Config_GeneralPurpose_Infoframe(BYTE *p3DInfoFrame);
void ConfigfHdmiVendorSpecificInfoFrame(BYTE _3D_Stru);
void InitHDMITX_Variable();
void HDMITX_ChangeDisplayOption(HDMI_Video_Type VideoMode, HDMI_OutputColorMode OutputColorMode);
void HDMITX_SetOutput();
int HDMITX_DevLoopProc();
////////////////////////////////////////////////////////////////////////////////
// Function Body.
////////////////////////////////////////////////////////////////////////////////

void InitHDMITX_Variable()
{
	printk( "[%s]\n", __FUNCTION__);
    HDMITX_InitTxDev(&InstanceData);
	HPDStatus = FALSE;
	HPDChangeStatus = FALSE;
}

BOOL AudioModeDetect(void)
{
    setHDMITX_AudioChannelEnable(bAudioEnable);
    return  TRUE;
}

void HDMITX_ChangeColorDepth(BYTE colorDepth)
{
#ifdef IT6615
	HDMITX_DEBUG_PRINTF(("OHDMITX_ChangeColorDepth(%02X)\n",(int)colorDepth)) ;
    OutputColorDepth = colorDepth ;
#else
    OutputColorDepth = 0 ;
#endif
}

void HDMITX_SetOutput()
{
    VIDEOPCLKLEVEL level ;
    unsigned long TMDSClock = VideoPixelClock*(pixelrep+1);
    HDMITX_DisableAudioOutput();
	HDMITX_EnableHDCP(FALSE);

    if( TMDSClock>80000000L )
    {
        level = PCLK_HIGH ;
    }
    else if(TMDSClock>20000000L)
    {
        level = PCLK_MEDIUM ;
    }
    else
    {
        level = PCLK_LOW ;
    }
#ifdef IT6615
	HDMITX_DEBUG_PRINTF(("OutputColorDepth = %02X\n",(int)OutputColorDepth)) ;
    setHDMITX_ColorDepthPhase(OutputColorDepth,0);
#endif

	setHDMITX_VideoSignalType(InstanceData.bInputVideoSignalType);
    #ifdef SUPPORT_SYNCEMBEDDED
	if(InstanceData.bInputVideoSignalType & T_MODE_SYNCEMB)
	{
	    setHDMITX_SyncEmbeddedByVIC(VIC,InstanceData.bInputVideoSignalType);
	}
    #endif

    HDMITX_DEBUG_PRINTF(("level = %d, ,bInputColorMode=%x,bOutputColorMode=%x,bHDMIMode=%x\n",(int)level,(int)bInputColorMode,(int)bOutputColorMode ,(int)bHDMIMode)) ;
	HDMITX_EnableVideoOutput(level,bInputColorMode,bOutputColorMode ,bHDMIMode);

    if( bHDMIMode )
    {
        #ifdef OUTPUT_3D_MODE
        ConfigfHdmiVendorSpecificInfoFrame(OUTPUT_3D_MODE);
        #endif
        //HDMITX_EnableHDCP(TRUE);
        ConfigAVIInfoFrame(VIC, pixelrep);

        HDMITX_SetAudioOutput();

        // if( bAudioEnable )
        // {
        //     ConfigAudioInfoFrm();
        // #ifdef SUPPORT_HBR_AUDIO
        //     HDMITX_EnableAudioOutput(T_AUDIO_HBR, CONFIG_INPUT_AUDIO_SPDIF, 768000L,8,NULL,TMDSClock);
        // #else
        //     // HDMITX_EnableAudioOutput(T_AUDIO_LPCM, FALSE, ulAudioSampleFS,OUTPUT_CHANNEL,NULL,TMDSClock);
        //     HDMITX_EnableAudioOutput(CNOFIG_INPUT_AUDIO_TYPE, CONFIG_INPUT_AUDIO_SPDIF, ulAudioSampleFS,bOutputAudioChannel,NULL,TMDSClock);
        // #endif
        // }

    }
	else
	{
		HDMITX_EnableAVIInfoFrame(FALSE ,NULL);
        HDMITX_EnableVSInfoFrame(FALSE,NULL);
	}
    setHDMITX_AVMute(FALSE);
    bChangeMode = FALSE ;
    DumpHDMITXReg() ;
}

void HDMITX_ChangeAudioOption(BYTE Option, BYTE channelNum, BYTE AudioFs)
{

    switch(Option )
    {
    case T_AUDIO_HBR :
        bOutputAudioType = T_AUDIO_HBR ;
        ulAudioSampleFS = 768000L ;
        bOutputAudioChannel = 8 ;
        return ;
    case T_AUDIO_NLPCM :
        bOutputAudioType = T_AUDIO_NLPCM ;
        bOutputAudioChannel = 2 ;
        break ;
    default:
        bOutputAudioType = T_AUDIO_LPCM ;
        if( channelNum < 1 )
        {
            bOutputAudioChannel = 1 ;
        }
        else if( channelNum > 8 )
        {
            bOutputAudioChannel = 8 ;
        }
        else
        {
            bOutputAudioChannel = channelNum ;
        }
        break ;
    }

    switch(AudioFs)
    {
    case AUDFS_44p1KHz:
        ulAudioSampleFS =  44100L ;
        break ;
    case AUDFS_88p2KHz:
        ulAudioSampleFS =  88200L ;
        break ;
    case AUDFS_176p4KHz:
        ulAudioSampleFS = 176400L ;
        break ;

    case AUDFS_48KHz:
        ulAudioSampleFS =  48000L ;
        break ;
    case AUDFS_96KHz:
        ulAudioSampleFS =  96000L ;
        break ;
    case AUDFS_192KHz:
        ulAudioSampleFS = 192000L ;
        break ;

    case AUDFS_768KHz:
        ulAudioSampleFS = 768000L ;
        break ;

    case AUDFS_32KHz:
        ulAudioSampleFS =  32000L ;
        break ;
    default:
        ulAudioSampleFS =  48000L ;
        break ;
    }
    HDMITX_DEBUG_PRINTF(("HDMITX_ChangeAudioOption():bOutputAudioType = %02X, ulAudioSampleFS = %8ld, bOutputAudioChannel = %d\n",(int)bOutputAudioType,ulAudioSampleFS,(int)bOutputAudioChannel)) ;
    bChangeAudio = TRUE ;
}

void HDMITX_SetAudioOutput()
{
	if( bAudioEnable )
	{
        ConfigAudioInfoFrm();
        // HDMITX_EnableAudioOutput(T_AUDIO_LPCM, FALSE, ulAudioSampleFS,OUTPUT_CHANNEL,NULL,TMDSClock);
        HDMITX_EnableAudioOutput(
            //CNOFIG_INPUT_AUDIO_TYPE,
            bOutputAudioType,
            CONFIG_INPUT_AUDIO_SPDIF,
            ulAudioSampleFS,
            bOutputAudioChannel,
            NULL, // pointer to cahnnel status.
            VideoPixelClock*(pixelrep+1));
        // if you have channel status , set here.
        // setHDMITX_ChStat(BYTE ucIEC60958ChStat[]);
        bChangeAudio = FALSE ;
	}
}

int HDMITX_DevLoopProc()
{
    static BYTE DevLoopCount = 0 ;
    CheckHDMITX(&HPDStatus,&HPDChangeStatus);

    if( HPDChangeStatus )
    {
        if( HPDStatus )
        {
            HDMITX_PowerOn();
            ParseEDID();
            bOutputColorMode = F_MODE_RGB444;

			if( RxCapability.ValidHDMI )
			{
				bHDMIMode = TRUE ;

				if(RxCapability.VideoMode & (1<<6))
				{
					bAudioEnable = TRUE ;
				}
				if( RxCapability.VideoMode & (1<<5))
				{
					bOutputColorMode &= ~F_MODE_CLRMOD_MASK ;
					bOutputColorMode |= F_MODE_YUV444;
				}
				else if (RxCapability.VideoMode & (1<<4))
				{
					bOutputColorMode &= ~F_MODE_CLRMOD_MASK ;
					bOutputColorMode |= F_MODE_YUV422 ;
				}
				bOutputColorMode = F_MODE_RGB444;


            #ifdef IT6615
                if(RxCapability.dc.uc & (HDMI_DC_SUPPORT_36|HDMI_DC_SUPPORT_30))
                {
            	    setHDMITX_ColorDepthPhase(OutputColorDepth,0);
                }
                else
                {
                    OutputColorDepth = B_TX_CD_NODEF;
                }
            #else
                OutputColorDepth = B_TX_CD_NODEF;
            #endif
			}
			else
			{
				bHDMIMode = FALSE ;
				bAudioEnable = FALSE ;
				OutputColorDepth = B_TX_CD_NODEF;
                // HDMITX_DisableAudioOutput();
                // HDMITX_DisableVideoOutput();
            #ifdef SUPPORT_HDCP
                HDMITX_EnableHDCP(FALSE);
            #endif
			}
    		HDMITX_DEBUG_PRINTF(("HPD change HDMITX_SetOutput();\n"));
            //HDMITX_SetOutput();
            bChangeMode=TRUE;
            bChangeAudio = TRUE ;
        }
        else
        {
            // unplug mode, ...
    		HDMITX_DEBUG_PRINTF(("HPD OFF HDMITX_DisableVideoOutput()\n"));
            HDMITX_DisableVideoOutput();
            HDMITX_PowerDown();
            bChangeAudio = FALSE ;
        }
    }
    else // no stable but need to process mode change procedure
    {
        if( DevLoopCount >= 20 )
        {
            DevLoopCount = 0 ;
        }

        #ifdef HDMITX_AUTO_MONITOR_INPUT
        if( DevLoopCount == 0 )
        {
            HDMITX_MonitorInputVideoChange();
            HDMITX_MonitorInputAudioChange();
        }
        #endif

        if(HPDStatus)
        {
            #ifdef HDMITX_AUTO_MONITOR_INPUT
            if( bChangeMode && ( VIC > 0 ) )
            #else
            if( bChangeMode )
            #endif
            {
        		HDMITX_DEBUG_PRINTF(("Mode change HDMITX_SetOutput();\n"));
                HDMITX_SetOutput();
                // HDMITX_EnableHDCP(TRUE);

                bChangeMode = FALSE ;
            }
            if(getHDMITX_LinkStatus())
            {
                // AudioModeDetect();
                #ifdef SUPPORT_HDCP
                if(getHDMITX_AuthenticationDone() ==FALSE)
                {
                    HDMITX_DEBUG_PRINTF(("getHDMITX_AuthenticationDone() ==FALSE\n") );
                    HDMITX_EnableHDCP(TRUE);
                    setHDMITX_AVMute(FALSE);
                }
                #endif
            }

            if(bChangeAudio)
            {
                HDMITX_SetAudioOutput() ;
            }
        }
    }

    DevLoopCount ++ ;
    return HPDStatus;
}

#ifdef HDMITX_AUTO_MONITOR_INPUT

void HDMITX_MonitorInputAudioChange()
{
    static ULONG prevAudioSampleFS = 0 ;
    LONG AudioFS ;

    if( !bAudioEnable )
    {
        prevAudioSampleFS = 0 ;
    }
    else
    {
        AudioFS = CalcAudFS() ;
        HDMITX_DEBUG_PRINTF1(("Audio Chagne, Audio clock = %ldHz\n",AudioFS)) ;
        if( AudioFS > 188000L ) // 192KHz
        {
            ulAudioSampleFS = 192000L ;
        }
        else if( AudioFS > 144000L ) // 176.4KHz
        {
            ulAudioSampleFS = 176400L ;
        }
        else if( AudioFS >  93000L ) // 96KHz
        {
            ulAudioSampleFS = 96000L ;
        }
        else if( AudioFS >  80000L ) // 88.2KHz
        {
            ulAudioSampleFS = 88200L ;
        }
        else if( AudioFS >  45000L ) // 48 KHz
        {
            ulAudioSampleFS = 48000L ;
        }
        else if( AudioFS >  36000L ) // 44.1KHz
        {
            ulAudioSampleFS = 44100L ;
        }
        else                         // 32KHz
        {
            ulAudioSampleFS = 32000L ;
        }

        if(!bChangeMode)
        {
            if( ulAudioSampleFS != prevAudioSampleFS )
            {
                HDMITX_DEBUG_PRINTF(("ulAudioSampleFS = %ldHz -> %ldHz\n",ulAudioSampleFS,ulAudioSampleFS)) ;
                ConfigAudioInfoFrm();
                HDMITX_EnableAudioOutput(CNOFIG_INPUT_AUDIO_TYPE, CONFIG_INPUT_AUDIO_SPDIF, ulAudioSampleFS,OUTPUT_CHANNEL,NULL,0);
                // HDMITX_EnableAudioOutput(T_AUDIO_LPCM, FALSE, ulAudioSampleFS,OUTPUT_CHANNEL,NULL,0);

            }
        }

        prevAudioSampleFS = ulAudioSampleFS ;

    }
}

int HDMITX_SearchVICIndex( ULONG PCLK, USHORT HTotal, USHORT VTotal, BYTE ScanMode )
{
    #define SEARCH_COUNT 4
    unsigned long  pclkDiff;
    int i;
    char hit;
    int iMax[SEARCH_COUNT]={0};
    char hitMax[SEARCH_COUNT]={0};
    char i2;

    for( i = 0 ; i < SizeofVMTable; i++ )
    {
        if( s_VMTable[i].VIC == 0 ) break ;

        hit=0;

        if( ScanMode == s_VMTable[i].ScanMode )
        {
            hit++;

            if( ScanMode == INTERLACE )
            {
                if( DIFF(VTotal/2, s_VMTable[i].VTotal) < 10 )
                {
                    hit++;
                }
            }
            else
            {
                if( DIFF(VTotal, s_VMTable[i].VTotal) < 10 )
                {
                    hit++;
                }
            }

            if( hit == 2 ) // match scan mode and v-total
            {
                if( DIFF(HTotal, s_VMTable[i].HTotal) < 40 )
                {
                    hit++;

                    pclkDiff = DIFF(PCLK, s_VMTable[i].PCLK);
                    pclkDiff = (pclkDiff * 100) / s_VMTable[i].PCLK;

                    if( pclkDiff < 100 )
                    {
                        hit += ( 100 - pclkDiff );
                    }
                }
            }
        }

        HDMITX_DEBUG_PRINTF(("i = %d, hit = %d\n",i,(int)hit));

        if( hit )
        {
            for( i2=0 ; i2<SEARCH_COUNT ; i2++ )
            {
                if( hitMax[i2] < hit )
                {
                    HDMITX_DEBUG_PRINTF(("replace iMax[%d] = %d => %d\n",(int)i2, iMax[i2], i ));
                    hitMax[i2] = hit;
                    iMax[i2]=i;
                    break;
                }
            }
        }
    }

    i=-1;
    hit=0;
    for( i2=0 ; i2<SEARCH_COUNT ; i2++ )
    {
        HDMITX_DEBUG_PRINTF(("[%d] i = %d, hit = %d\n",(int)i2, iMax[i2],(int)hitMax[i2]));
        if( hitMax[i2] > hit )
        {
            hit = hitMax[i2];
            i = iMax[i2];
        }
    }

    if( hit > 2 )
    {
        HDMITX_DEBUG_PRINTF(("i = %d, hit = %d\n",i,(int)hit));
        HDMITX_DEBUG_PRINTF((">> mode : %d %u x %u @%lu (%s)\n", (int)s_VMTable[i].VIC, s_VMTable[i].HActive, s_VMTable[i].VActive, s_VMTable[i].PCLK, (s_VMTable[i].ScanMode==0)?"i":"p" ));
    }
    else
    {
        i=-1;
        HDMITX_DEBUG_PRINTF(("no matched\n"));
    }

    return i;
}

void HDMITX_MonitorInputVideoChange()
{
    static ULONG prevPCLK = 0 ;
    static USHORT prevHTotal = 0 ;
    static USHORT prevVTotal = 0 ;
    static BYTE prevScanMode ;
    ULONG currPCLK ;
    ULONG diff ;
    USHORT currHTotal, currVTotal ;
    BYTE currScanMode ;
	int i ;

    currPCLK = CalcPCLK() ;
    currHTotal = hdmitx_getInputHTotal() ;
    currVTotal = hdmitx_getInputVTotal() ;
    currScanMode = hdmitx_isInputInterlace() ? INTERLACE:PROG ;
    diff = DIFF(currPCLK,prevPCLK);

    HDMITX_DEBUG_PRINTF(("HDMITX_MonitorInputVideoChange : pclk=%lu, ht=%u, vt=%u, dif=%lu\n", currPCLK, currHTotal, currVTotal, diff ));

    if( currHTotal == 0 || currVTotal == 0 || currPCLK == 0 )
    {
        bChangeMode = FALSE;
		return ;
    }

    if( diff > currPCLK/20) // 5% torrenlance
    {
        bChangeMode = TRUE ;
    }
    else
    {
        diff = DIFF(currHTotal, prevHTotal) ;
        if( diff > 20 )
        {
            bChangeMode = TRUE ;
        }
        diff = DIFF(currVTotal, prevVTotal) ;
        if( diff > 20 )
        {
            bChangeMode = TRUE ;
        }
    }

    if( bChangeMode )
    {
        HDMITX_DEBUG_PRINTF(("PCLK = %ld -> %ld\n",prevPCLK, currPCLK));
        HDMITX_DEBUG_PRINTF(("HTotal = %d -> %d\n",prevHTotal, currHTotal));
        HDMITX_DEBUG_PRINTF(("VTotal = %d -> %d\n",prevVTotal, currVTotal));
        HDMITX_DEBUG_PRINTF(("ScanMode = %s -> %s\n",prevScanMode?"P":"I", currScanMode?"P":"I"));

        HDMITX_DEBUG_PRINTF(("PCLK = %ld,(%dx%d) %s %s\n",currPCLK, currHTotal,currVTotal, (currScanMode==INTERLACE)?"INTERLACED":"PROGRESS",bChangeMode?"CHANGE MODE":"NO CHANGE MODE"));

        setHDMITX_AVMute(TRUE);

        #if 0
        for( i = 0 ; (i < SizeofVMTable) && ( s_VMTable[i].VIC != 0 ); i++ )
        {
            if( s_VMTable[i].VIC == 0 ) break ;
            if( DIFF(currPCLK, s_VMTable[i].PCLK) > (s_VMTable[i].PCLK/20))
            {
                continue ;
            }
            if( DIFF(currHTotal, s_VMTable[i].HTotal) > 40 )
            {
                continue ;
            }
            if( currScanMode != s_VMTable[i].ScanMode )
            {
                continue ;
            }
            if( currScanMode == INTERLACE )
            {
                if( DIFF(currVTotal/2, s_VMTable[i].VTotal) > 10 )
                {
                    continue ;
                }
            }
            else
            {
                if( DIFF(currVTotal, s_VMTable[i].VTotal) > 10 )
                {
                    continue ;
                }
            }
            printf("i = %d, VIC = %d\n",i,(int)s_VMTable[i].VIC) ;

            break ;
        }
        #else
        i = HDMITX_SearchVICIndex( currPCLK, currHTotal, currVTotal, currScanMode );
        #endif

        if( i >= 0 )
        {
            VIC = s_VMTable[i].VIC;
            pixelrep = s_VMTable[i].PixelRep ;
            VideoPixelClock = currPCLK ;
        }
        else
        {
            VIC = 0;
            pixelrep = 0;
            VideoPixelClock = 0 ;
        }
    }

    prevPCLK = currPCLK ;
    prevHTotal = currHTotal ;
    prevVTotal = currVTotal ;
    prevScanMode = currScanMode ;

}
#endif // HDMITX_AUTO_MONITOR_INPUT

void HDMITX_ChangeDisplayOption(HDMI_Video_Type OutputVideoTiming, HDMI_OutputColorMode OutputColorMode)
{
	printk( "[%s] Vic=%d\n", __FUNCTION__,OutputVideoTiming);
   //HDMI_Video_Type  t=HDMI_480i60_16x9;
    if((F_MODE_RGB444)==(bOutputColorMode&F_MODE_CLRMOD_MASK))//Force output RGB in RGB only case
    {
        OutputColorMode=F_MODE_RGB444;
    }
    else if ((F_MODE_YUV422)==(bOutputColorMode&F_MODE_CLRMOD_MASK))//YUV422 only
    {
        if(OutputColorMode==HDMI_YUV444){OutputColorMode=F_MODE_YUV422;}
    }
    else if ((F_MODE_YUV444)==(bOutputColorMode&F_MODE_CLRMOD_MASK))//YUV444 only
    {
        if(OutputColorMode==HDMI_YUV422){OutputColorMode=F_MODE_YUV444;}
    }
    switch(OutputVideoTiming)
	{
    case HDMI_640x480p60:
        VIC = 1 ;
        VideoPixelClock = 25000000 ;
        pixelrep = 0 ;
        aspec = HDMI_4x3 ;
        Colorimetry = HDMI_ITU601 ;
        break ;
    case HDMI_480p60:
        VIC = 2 ;
        VideoPixelClock = 27000000 ;
        pixelrep = 0 ;
        aspec = HDMI_4x3 ;
        Colorimetry = HDMI_ITU601 ;
        break ;
    case HDMI_480p60_16x9:
        VIC = 3 ;
        VideoPixelClock = 27000000 ;
        pixelrep = 0 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU601 ;
        break ;
    case HDMI_720p60:
        VIC = 4 ;
        VideoPixelClock = 74250000 ;
        pixelrep = 0 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU709 ;
        break ;
    case HDMI_1080i60:
        VIC = 5 ;
        VideoPixelClock = 74250000 ;
        pixelrep = 0 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU709 ;
        break ;
    case HDMI_480i60:
        VIC = 6 ;
        VideoPixelClock = 13500000 ;
        pixelrep = 1 ;
        aspec = HDMI_4x3 ;
        Colorimetry = HDMI_ITU601 ;
        break ;
    case HDMI_480i60_16x9:
        VIC = 7 ;
        VideoPixelClock = 13500000 ;
        pixelrep = 1 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU601 ;
        break ;
    case HDMI_1080p60:
        VIC = 16 ;
        VideoPixelClock = 148500000 ;
        pixelrep = 0 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU709 ;
        break ;
    case HDMI_576p50:
        VIC = 17 ;
        VideoPixelClock = 27000000 ;
        pixelrep = 0 ;
        aspec = HDMI_4x3 ;
        Colorimetry = HDMI_ITU601 ;
        break ;
    case HDMI_576p50_16x9:
        VIC = 18 ;
        VideoPixelClock = 27000000 ;
        pixelrep = 0 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU601 ;
        break ;
    case HDMI_720p50:
        VIC = 19 ;
        VideoPixelClock = 74250000 ;
        pixelrep = 0 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU709 ;
        break ;
    case HDMI_1080i50:
        VIC = 20 ;
        VideoPixelClock = 74250000 ;
        pixelrep = 0 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU709 ;
        break ;
    case HDMI_576i50:
        VIC = 21 ;
        VideoPixelClock = 13500000 ;
        pixelrep = 1 ;
        aspec = HDMI_4x3 ;
        Colorimetry = HDMI_ITU601 ;
        break ;
    case HDMI_576i50_16x9:
        VIC = 22 ;
        VideoPixelClock = 13500000 ;
        pixelrep = 1 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU601 ;
        break ;
    case HDMI_1080p50:
        VIC = 31 ;
        VideoPixelClock = 148500000 ;
        pixelrep = 0 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU709 ;
        break ;
    case HDMI_1080p24:
        VIC = 32 ;
        VideoPixelClock = 74250000 ;
        pixelrep = 0 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU709 ;
        break ;
    case HDMI_1080p25:
        VIC = 33 ;
        VideoPixelClock = 74250000 ;
        pixelrep = 0 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU709 ;
        break ;
    case HDMI_1080p30:
        VIC = 34 ;
        VideoPixelClock = 74250000 ;
        pixelrep = 0 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU709 ;
        break ;

    case HDMI_720p30:
        VIC = 0 ;
        VideoPixelClock = 74250000 ;
        pixelrep = 0 ;
        aspec = HDMI_16x9 ;
        Colorimetry = HDMI_ITU709 ;

    #ifdef SUPPORT_SYNCEMBEDDED
    /*
        VTiming.HActive=1280 ;
        VTiming.VActive=720 ;
        VTiming.HTotal=3300 ;
        VTiming.VTotal=750 ;
        VTiming.PCLK=VideoPixelClock ;
        VTiming.xCnt=0x2E ;
        VTiming.HFrontPorch= 1760;
        VTiming.HSyncWidth= 40 ;
        VTiming.HBackPorch= 220 ;
        VTiming.VFrontPorch= 5;
        VTiming.VSyncWidth= 5 ;
        VTiming.VBackPorch= 20 ;
        VTiming.ScanMode=PROG ;
        VTiming.VPolarity=Vneg ;
        VTiming.HPolarity=Hneg ;
    */
    #endif
        break ;
    default:
        bChangeMode = FALSE ;
        return ;
    }
    switch(OutputColorMode)
    {
    case HDMI_YUV444:
        bOutputColorMode = F_MODE_YUV444 ;
        break ;
    case HDMI_YUV422:
        bOutputColorMode = F_MODE_YUV422 ;
        break ;
    case HDMI_RGB444:
    default:
        bOutputColorMode = F_MODE_RGB444 ;
        break ;
    }
    if( Colorimetry == HDMI_ITU709 )
    {
        bInputColorMode |= F_VIDMODE_ITU709 ;
    }
    else
    {
        bInputColorMode &= ~F_VIDMODE_ITU709 ;
    }
    // if( Colorimetry != HDMI_640x480p60)
    if( OutputVideoTiming != HDMI_640x480p60)
    {
        bInputColorMode |= F_VIDMODE_16_235 ;
    }
    else
    {
        bInputColorMode &= ~F_VIDMODE_16_235 ;
    }
    bChangeMode = TRUE ;
}

void ConfigAVIInfoFrame(BYTE VIC, BYTE pixelrep)
{
    AVI_InfoFrame *AviInfo;
    AviInfo = (AVI_InfoFrame *)CommunBuff ;

    AviInfo->pktbyte.AVI_HB[0] = AVI_INFOFRAME_TYPE|0x80 ;
    AviInfo->pktbyte.AVI_HB[1] = AVI_INFOFRAME_VER ;
    AviInfo->pktbyte.AVI_HB[2] = AVI_INFOFRAME_LEN ;

    switch(bOutputColorMode)
    {
    case F_MODE_YUV444:
        // AviInfo->info.ColorMode = 2 ;
        AviInfo->pktbyte.AVI_DB[0] = (2<<5)|(1<<4);
        break ;
    case F_MODE_YUV422:
        // AviInfo->info.ColorMode = 1 ;
        AviInfo->pktbyte.AVI_DB[0] = (1<<5)|(1<<4);
        break ;
    case F_MODE_RGB444:
    default:
        // AviInfo->info.ColorMode = 0 ;
        AviInfo->pktbyte.AVI_DB[0] = (0<<5)|(1<<4);
        break ;
    }
    AviInfo->pktbyte.AVI_DB[1] = 8 ;
    AviInfo->pktbyte.AVI_DB[1] |= (aspec != HDMI_16x9)?(1<<4):(2<<4); // 4:3 or 16:9
    AviInfo->pktbyte.AVI_DB[1] |= (Colorimetry != HDMI_ITU709)?(1<<6):(2<<6); // 4:3 or 16:9
    AviInfo->pktbyte.AVI_DB[2] = 0 ;
    AviInfo->pktbyte.AVI_DB[3] = VIC ;
    AviInfo->pktbyte.AVI_DB[4] =  pixelrep & 3 ;
    AviInfo->pktbyte.AVI_DB[5] = 0 ;
    AviInfo->pktbyte.AVI_DB[6] = 0 ;
    AviInfo->pktbyte.AVI_DB[7] = 0 ;
    AviInfo->pktbyte.AVI_DB[8] = 0 ;
    AviInfo->pktbyte.AVI_DB[9] = 0 ;
    AviInfo->pktbyte.AVI_DB[10] = 0 ;
    AviInfo->pktbyte.AVI_DB[11] = 0 ;
    AviInfo->pktbyte.AVI_DB[12] = 0 ;

    HDMITX_EnableAVIInfoFrame(TRUE, (unsigned char *)AviInfo);
}

////////////////////////////////////////////////////////////////////////////////
// Function: ConfigAudioInfoFrm
// Parameter: NumChannel, number from 1 to 8
// Return: ER_SUCCESS for successfull.
// Remark: Evaluate. The speakerplacement is only for reference.
//         For production, the caller of hdmitx_SetAudioInfoFrame should program
//         Speaker placement by actual status.
// Side-Effect:
////////////////////////////////////////////////////////////////////////////////

void ConfigAudioInfoFrm()
{
    int i ;

    Audio_InfoFrame *AudioInfo ;
    AudioInfo = (Audio_InfoFrame *)CommunBuff ;

    HDMITX_DEBUG_PRINTF(("ConfigAudioInfoFrm(%d)\n",2));

    AudioInfo->pktbyte.AUD_HB[0] = AUDIO_INFOFRAME_TYPE ;
    AudioInfo->pktbyte.AUD_HB[1] = 1 ;
    AudioInfo->pktbyte.AUD_HB[2] = AUDIO_INFOFRAME_LEN ;
    AudioInfo->pktbyte.AUD_DB[0] = 1 ;
    for( i = 1 ;i < AUDIO_INFOFRAME_LEN ; i++ )
    {
        AudioInfo->pktbyte.AUD_DB[i] = 0 ;
    }
    HDMITX_EnableAudioInfoFrame(TRUE, (unsigned char *)AudioInfo);
}

void ConfigfHdmiVendorSpecificInfoFrame(BYTE _3D_Stru)
{
    VendorSpecific_InfoFrame *VS_Info;

    VS_Info=(VendorSpecific_InfoFrame *)CommunBuff ;

    VS_Info->pktbyte.VS_HB[0] = VENDORSPEC_INFOFRAME_TYPE|0x80;
    VS_Info->pktbyte.VS_HB[1] = VENDORSPEC_INFOFRAME_VER;
    VS_Info->pktbyte.VS_HB[2] = (_3D_Stru == Side_by_Side)?6:5;
    VS_Info->pktbyte.VS_DB[0] = 0x03;
    VS_Info->pktbyte.VS_DB[1] = 0x0C;
    VS_Info->pktbyte.VS_DB[2] = 0x00;
    VS_Info->pktbyte.VS_DB[3] = 0x40;
    switch(_3D_Stru)
    {
    case Side_by_Side:
    case Frame_Pcaking:
    case Top_and_Botton:
        VS_Info->pktbyte.VS_DB[4] = (_3D_Stru<<4);
        break;
    default:
        VS_Info->pktbyte.VS_DB[4] = (Frame_Pcaking<<4);
        break ;
    }
    VS_Info->pktbyte.VS_DB[5] = 0x00;
    HDMITX_EnableVSInfoFrame(TRUE,(BYTE *)VS_Info);
}

/////////////////////////////////////////////////////////////////////
// ParseEDID()
// Check EDID check sum and EDID 1.3 extended segment.
/////////////////////////////////////////////////////////////////////

BYTE ParseEDID()
{
    // collect the EDID ucdata of segment 0
    _XDATA unsigned char *EDID_Buf;
    BYTE CheckSum ;
    BYTE BlockCount ;
    BYTE err ;
    BYTE bValidCEA = FALSE ;
    BYTE i;
    #if Debug_message
        BYTE j ;
    #endif // Debug_message

    EDID_Buf = CommunBuff;
    RxCapability.ValidCEA = FALSE ;
    RxCapability.ValidHDMI = FALSE ;
    RxCapability.dc.uc = 0;

    getHDMITX_EDIDBlock(0, EDID_Buf);

    for( i = 0, CheckSum = 0 ; i < 128 ; i++ )
    {
        CheckSum += EDID_Buf[i] ; CheckSum &= 0xFF ;
    }
			//Eep_Write(0x80, 0x80, EDID_Buf);
	if( CheckSum != 0 )
	{
		return FALSE ;
	}
	if( EDID_Buf[0] != 0x00 ||
	    EDID_Buf[1] != 0xFF ||
	    EDID_Buf[2] != 0xFF ||
	    EDID_Buf[3] != 0xFF ||
	    EDID_Buf[4] != 0xFF ||
	    EDID_Buf[5] != 0xFF ||
	    EDID_Buf[6] != 0xFF ||
	    EDID_Buf[7] != 0x00)
    {
        return FALSE ;
    }
    /*
    for( i = 0 ; i < 128 ; i++ )
    {
        HDMITX_DEBUG_PRINTF(("%02X%c",(int)EDID_Buf[i],(7 == (i&7))?'\n':' '));
    }
    */

    BlockCount = EDID_Buf[0x7E] ;

    if( BlockCount == 0 )
    {
        return TRUE ; // do nothing.
    }
    else if ( BlockCount > 4 )
    {
        BlockCount = 4 ;
    }
     // read all segment for test
    for( i = 1 ; i <= BlockCount ; i++ )
    {
        err = getHDMITX_EDIDBlock(i, EDID_Buf);

        #if Debug_message
        for( j = 0 ; j < 128 ; j++ )
        {
            EDID_DEBUG_PRINTF(("%02X%c",(int)EDID_Buf[j],(7 == (j&7))?'\n':' '));
        }
        #endif // Debug_message

        if( err )
        {
           if( !bValidCEA && EDID_Buf[0] == 0x2 && EDID_Buf[1] == 0x3 )
            {
                err = ParseCEAEDID(EDID_Buf);
                EDID_DEBUG_PRINTF(("err = %s\n",err?"SUCCESS":"FAIL"));
                if( err )
                {
                    EDID_DEBUG_PRINTF(("RxCapability.IEEEOUI = %lx\n",RxCapability.IEEEOUI));

				    if(RxCapability.IEEEOUI==0x0c03)
				    {
				    	RxCapability.ValidHDMI = TRUE ;
				    	bValidCEA = TRUE ;
					}
				    else
				    {
				    	RxCapability.ValidHDMI = FALSE ;
				    }
                }
            }
        }
    }
    return err ;
}

static BOOL ParseCEAEDID(BYTE *pCEAEDID)
{
    BYTE offset,End ;
    BYTE count ;
    BYTE tag ;
    int i ;

    if( pCEAEDID[0] != 0x02 || pCEAEDID[1] != 0x03 ) return FALSE ; // not a CEA BLOCK.
    End = pCEAEDID[2]  ; // CEA description.

    RxCapability.VDOMode[0] = 0x00 ;
    RxCapability.VDOMode[1] = 0x00 ;
    RxCapability.VDOMode[2] = 0x00 ;
    RxCapability.VDOMode[3] = 0x00 ;
    RxCapability.VDOMode[4] = 0x00 ;
    RxCapability.VDOMode[5] = 0x00 ;
    RxCapability.VDOMode[6] = 0x00 ;
    RxCapability.VDOMode[7] = 0x00 ;
    RxCapability.PA[0] = 0x00 ;
    RxCapability.PA[1] = 0x00 ;

    RxCapability.VideoMode = pCEAEDID[3] ;

    RxCapability.NativeVDOMode = 0xff ;

    for( offset = 4 ; offset < End ; )
    {
        tag = pCEAEDID[offset] >> 5 ;
        count = pCEAEDID[offset] & 0x1f ;
        switch( tag )
        {
        case 0x01: // Audio Data Block ;
            RxCapability.AUDDesCount = count/3 ;
            EDID_DEBUG_PRINTF(("RxCapability.AUDDesCount = %d\n",(int)RxCapability.AUDDesCount));
            offset++ ;
            for( i = 0 ; i < RxCapability.AUDDesCount && i < MAX_AUDDES_COUNT ; i++ )
            {
                RxCapability.AUDDes[i].uc[0] = pCEAEDID[offset+i*3] ;
                RxCapability.AUDDes[i].uc[1] = pCEAEDID[offset+i*3+1] ;
                RxCapability.AUDDes[i].uc[2] = pCEAEDID[offset+i*3+2] ;
            }
            offset += count ;
            break ;

        case 0x02: // Video Data Block ;
            offset ++ ;
            for( i = 0,RxCapability.NativeVDOMode = 0xff ; i < count ; i++)
            {
            	BYTE VIC ;
            	VIC = pCEAEDID[offset+i] & (~0x80);
            	// if( FindModeTableEntryByVIC(VIC) != -1 )
            	if(VIC<64)
            	{
	                RxCapability.VDOMode[VIC/8] |= (1<<(VIC%8));
	                EDID_DEBUG_PRINTF(("VIC = %d, RxCapability.VDOMode[%d]=%02X\n",(int)VIC,(int)VIC/8,(int)RxCapability.VDOMode[VIC/8] ));
	                if(( pCEAEDID[offset+i] & 0x80 )&&(RxCapability.NativeVDOMode==0xFF))
	                {
	                    RxCapability.NativeVDOMode = VIC ;
	                    EDID_DEBUG_PRINTF(("native = %d\n",RxCapability.NativeVDOMode));
	                }
            	}
            }
            offset += count ;
            break ;

        case 0x03: // Vendor Specific Data Block ;
            offset ++ ;
            RxCapability.IEEEOUI = (ULONG)pCEAEDID[offset+2] ;
            RxCapability.IEEEOUI <<= 8 ;
            RxCapability.IEEEOUI += (ULONG)pCEAEDID[offset+1] ;
            RxCapability.IEEEOUI <<= 8 ;
            RxCapability.IEEEOUI += (ULONG)pCEAEDID[offset] ;
            EDID_DEBUG_PRINTF(("IEEEOUI = %02X %02X %02X %lx",(int)pCEAEDID[offset+2],(int)pCEAEDID[offset+1],(int)pCEAEDID[offset],RxCapability.IEEEOUI));
			if( RxCapability.IEEEOUI== 0x0C03)
			{
				BYTE nextoffset ;
				RxCapability.PA[0] = pCEAEDID[offset+3] ;
				RxCapability.PA[1] = pCEAEDID[offset+4] ;
	            if(count>5)
	            {
	                RxCapability.dc.uc = pCEAEDID[offset+5]&0x70;
	            }
	            if(count>6)
	            {
	                RxCapability.MaxTMDSClock = pCEAEDID[offset+6];
	            }
				if(count>7)
				{
					nextoffset = 8 ;
					if(pCEAEDID[offset+7] & 0x80) { nextoffset += 2 ; }  // latency
					if(pCEAEDID[offset+7] & 0x40) { nextoffset += 2 ; }  // interlace latency
					if(pCEAEDID[offset+7] & 0x20) {
						EDID_DEBUG_PRINTF(("next offset = %d",(int)nextoffset));
						RxCapability.Valid3D = (pCEAEDID[offset+nextoffset] & 0x80)?TRUE:FALSE ;
					}  // interlace latency

				}
			}
            offset += count ; // ignore the remaind.

            break ;

        case 0x04: // Speaker Data Block ;
            offset ++ ;
            RxCapability.SpeakerAllocBlk.uc[0] = pCEAEDID[offset] ;
            RxCapability.SpeakerAllocBlk.uc[1] = pCEAEDID[offset+1] ;
            RxCapability.SpeakerAllocBlk.uc[2] = pCEAEDID[offset+2] ;
            offset += 3 ;
            break ;
        case 0x05: // VESA Data Block ;
            offset += count+1 ;
            break ;
        case 0x07: // Extended Data Block ;
            offset += count+1 ; //ignore
            break ;
        default:
            offset += count+1 ; // ignore
        }
    }
    RxCapability.ValidCEA = TRUE ;
    return TRUE ;
}
