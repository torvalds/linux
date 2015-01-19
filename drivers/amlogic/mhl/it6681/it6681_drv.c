///*****************************************
// Copyright (C) 2009-2014
// ITE Tech. Inc. All Rights Reserved
// Proprietary and Confidential
///*****************************************
// @file   <IT6681.c>
// @author Hermes.Wu@ite.com.tw
// @date   2013/05/07
// @fileversion: ITE_IT6681_6607_SAMPLE_1.06
// ******************************************/
/*
 * MHL support
 *
 * Copyright (C) 2013 ITE Tech. Inc.
 * Author: Hermes Wu <hermes.wu@ite.com.tw>
 *
 * MHL TX driver for IT6681
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "it6681_cfg.h"
#include "it6681_arch.h"
#include "it6681_debug.h"
#include "it6681_def.h"
#include "it6681_io.h"
#include "it6681_drv.h"
#include "version.h"

#define TRUE 1
#define FALSE 0

// #define SUCCESS	        1
#define RCVABORT        2
#define RCVNACK         3
#define ARBLOSE         4
#define FWTXFAIL        5
#define FWRXPKT         6
#define FAIL		   -1
#define ABORT          -2

#define AFE_SPEED_HIGH            1
#define AFE_SPEED_LOW             0

#define HDMI            0
#define DVI             1
#define RSVD            2

#define F_MODE_RGB24  0
#define F_MODE_RGB444 0
#define F_MODE_YUV422 1
#define F_MODE_YUV444 2
#define F_MODE_CLRMOD_MASK 3

BOOL i2c_write_byte(BYTE address, BYTE offset, BYTE byteno, BYTE * p_data, BYTE device);
BOOL i2c_read_byte(BYTE address, BYTE offset, BYTE byteno, BYTE * p_data, BYTE device);

//static void hdimtx_write_init(struct IT6681_REG_INI const*tdata);
//static void mhltx_write_init(struct IT6681_REG_INI const*tdata);

//static void hdmirx_Var_init(struct it6681_dev_data * it6681);
// static void chgbank( unsigned char bankno );

static void set_mhlsts( unsigned char offset, unsigned char status);
static void set_mhlint( unsigned char offset, unsigned char field);
static int read_devcap_hw(struct it6681_dev_data * it6681);
static int mscfire(int offset, int wdata);
static int mscwait(void);
static int ddcwait(void);
int ddcfire(int offset, int wdata);

//static void hdmitx_rst(struct it6681_dev_data * it6681);
#if _SUPPORT_HDCP_
static int Hdmi_HDCP_state(struct it6681_dev_data * it6681, HDCPSts_Type state);
static int hdcprd( unsigned char offset, unsigned char bytenum);
static int hdmitx_enhdcp(struct it6681_dev_data * it6681);
static int Hdmi_HDCP_state(struct it6681_dev_data * it6681, HDCPSts_Type state);
static void hdmitx_int_HDCP_AuthFail(struct it6681_dev_data * it6681);
#if _SHOW_HDCP_INFO_
static void hdcpsts(void);
#endif
#endif
static void mhl_read_mscmsg(struct it6681_dev_data * it6681);
///////////////////////////////////////////////////////////////////////////////////////////////

//static struct it6681_dev_data* get_it6681_dev_data(void);
//static int get_it6681_Aviinfo(struct it6681_dev_data * it6681);
#define _DUMP_HDMITX_
#ifdef _DUMP_HDMITX_
void DumpHDMITXReg(void);
#else
#define DumpHDMITXReg
#endif

///////////////////////////////////////////////////////////////////////////////////
//
//
//////////////////////////////////////////////////////////////////////////////////

#if _SUPPORT_RAP_
	static char const SuppRAPCode[32] = {
//	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, A, B, C, D, E, F
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0
	1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};// 1
#endif

#if _SUPPORT_RCP_
	static char const SuppRCPCode[128]= {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, // 0
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 1
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, // 2
	1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, // 3
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, // 4
	1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 5
	1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, // 6
	0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};// 7

#endif

#define SIZEOF_CSCMTX 21
static unsigned char const bCSCMtx_RGB2YUV_ITU601_16_235[SIZEOF_CSCMTX] =
{
 	0x00,0x80,0x10,
 	0xB2,0x04,0x65,0x02,0xE9,0x00,
 	0x93,0x3C,0x18,0x04,0x55,0x3F,
 	0x49,0x3D,0x9F,0x3E,0x18,0x04
};

static unsigned char const bCSCMtx_RGB2YUV_ITU601_0_255[SIZEOF_CSCMTX] =
{
 	0x10,0x80,0x10,
 	0x09,0x04,0x0E,0x02,0xC9,0x00,
 	0x0F,0x3D,0x84,0x03,0x6D,0x3F,
 	0xAB,0x3D,0xD1,0x3E,0x84,0x03
};

static unsigned char const bCSCMtx_RGB2YUV_ITU709_16_235[SIZEOF_CSCMTX] =
{
 	0x00,0x80,0x10,
 	0xB8,0x05,0xB4,0x01,0x94,0x00,
 	0x4A,0x3C,0x17,0x04,0x9F,0x3F,
 	0xD9,0x3C,0x10,0x3F,0x17,0x04
};

static unsigned char const bCSCMtx_RGB2YUV_ITU709_0_255[SIZEOF_CSCMTX] =
{
 	0x10,0x80,0x10,
 	0xEA,0x04,0x77,0x01,0x7F,0x00,
 	0xD0,0x3C,0x83,0x03,0xAD,0x3F,
 	0x4B,0x3D,0x32,0x3F,0x83,0x03
};

static unsigned char const bCSCMtx_YUV2RGB_ITU601_16_235[SIZEOF_CSCMTX] =
{
 	0x00,0x00,0x00,
 	0x00,0x08,0x6B,0x3A,0x50,0x3D,
 	0x00,0x08,0xF5,0x0A,0x02,0x00,
 	0x00,0x08,0xFD,0x3F,0xDA,0x0D
};

static unsigned char const bCSCMtx_YUV2RGB_ITU601_0_255[SIZEOF_CSCMTX] =
{
 	0x04,0x00,0xA7,
 	0x4F,0x09,0x81,0x39,0xDD,0x3C,
 	0x4F,0x09,0xC4,0x0C,0x01,0x00,
 	0x4F,0x09,0xFD,0x3F,0x1F,0x10
};

static unsigned char const bCSCMtx_YUV2RGB_ITU709_16_235[SIZEOF_CSCMTX] =
{
	0x00,0x00,0x00,
	0x00,0x08,0x55,0x3C,0x88,0x3E,
	0x00,0x08,0x51,0x0C,0x00,0x00,
	0x00,0x08,0x00,0x00,0x84,0x0E
};

static unsigned char const bCSCMtx_YUV2RGB_ITU709_0_255[SIZEOF_CSCMTX] =
{
	0x04,0x00,0xA7,
	0x4F,0x09,0xBA,0x3B,0x4B,0x3E,
	0x4F,0x09,0x57,0x0E,0x02,0x00,
	0x4F,0x09,0xFE,0x3F,0xE8,0x10
};

static unsigned char const it6681_internal_edid[] = 
{
/*
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x06, 0x8f, 0x12, 0xb0, 0x01, 0x00, 0x00, 0x00,
    0x0c, 0x14, 0x01, 0x03, 0x80, 0x1c, 0x15, 0x78, 0x0a, 0x1e, 0xac, 0x98, 0x59, 0x56, 0x85, 0x28,
    0x29, 0x52, 0x57, 0x20, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x8c, 0x0a, 0xd0, 0x8a, 0x20, 0xe0, 0x2d, 0x10, 0x10, 0x3e,
    0x96, 0x00, 0xfa, 0xbe, 0x00, 0x00, 0x00, 0x18, 0xd5, 0x09, 0x80, 0xa0, 0x20, 0xe0, 0x2d, 0x10,
    0x10, 0x60, 0xa2, 0x00, 0xfa, 0xbe, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x56,
    0x41, 0x2d, 0x31, 0x38, 0x33, 0x31, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xfd,
    0x00, 0x17, 0x3d, 0x0d, 0x2e, 0x11, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0xfa,
    0x02, 0x03, 0x30, 0xf1, 0x43, 0x84, 0x10, 0x03, 0x23, 0x09, 0x07, 0x07, 0x83, 0x01, 0x00, 0x00,
    0xe2, 0x00, 0x0f, 0xe3, 0x05, 0x03, 0x01, 0x78, 0x03, 0x0c, 0x00, 0x11, 0x00, 0x88, 0x2d, 0x20,
    0xc0, 0x0e, 0x01, 0x00, 0x00, 0x12, 0x18, 0x20, 0x28, 0x20, 0x38, 0x20, 0x58, 0x20, 0x68, 0x20,
    0x01, 0x1d, 0x00, 0x72, 0x51, 0xd0, 0x1e, 0x20, 0x6e, 0x28, 0x55, 0x00, 0xa0, 0x5a, 0x00, 0x00,
    0x00, 0x1e, 0x8c, 0x0a, 0xd0, 0x8a, 0x20, 0xe0, 0x2d, 0x10, 0x10, 0x3e, 0x96, 0x00, 0xa0, 0x5a,
    0x00, 0x00, 0x00, 0x18, 0xf3, 0x39, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c, 0x45, 0x00,
    0xe0, 0x0e, 0x11, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6a,
*/

// EDID up to 720p only
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 
    0x06, 0x8F, 0x12, 0xB0, 0x01, 0x00, 0x00, 0x00, 
    0x0C, 0x14, 0x01, 0x03, 0x80, 0x1C, 0x15, 0x78, 
    0x0A, 0x1E, 0xAC, 0x98, 0x59, 0x56, 0x85, 0x28, 
    0x29, 0x52, 0x57, 0x20, 0x00, 0x00, 
                                        0x01, 0x01, 
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 
                                                                
    
    0x1A, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20, 0x6E, 0x28, 0x55, 0x00, 0x40, 0xB4, 0x10, 0x00, 0x00, 0x1E,
    
    0xD5, 0x09, 0x80, 0xA0, 0x20, 0xE0, 0x2D, 0x10, 
    0x10, 0x60, 0xA2, 0x00, 0xFA, 0xBE, 0x00, 0x00, 
    0x00, 0x18, 
    
                            0x00, 0x00, 0x00, 0xFC, 0x00, 0x56, 
    0x41, 0x2D, 0x31, 0x38, 0x33, 0x31, 0x0A, 0x20, 
    0x20, 0x20, 0x20, 0x20, 
    
                            0x00, 0x00, 0x00, 0xFD, 
    0x00, 0x17, 0x3D, 0x0D, 0x2E, 0x11, 0x00, 0x0A, 
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0xC6,
    
    0x02, 0x03, 0x1D, 0xF1, 
    0x42, 0x84, 0x03, 
    0x23, 0x09, 0x07, 0x07, 
    0x83, 0x01, 0x00, 0x00, 
    0xE2, 0x00, 0x0F, 
    0xE3, 0x05, 0x03, 0x01, 
    
    0x65, 0x03, 0x0c, 0x00, 0x10, 0x00,
    
    0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20, 0x6E, 0x28, 0x55, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x1E, 
    0x8C, 0x0A, 0xD0, 0x8A, 0x20, 0xE0, 0x2D, 0x10, 0x10, 0x3E, 0x96, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x18, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    
    0xF0
// EDID up to 720p only (end)

// EDID up to 1080p no 3D
/*
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x06, 0x8F, 0x12, 0xB0, 0x01, 0x00, 0x00, 0x00, 
    0x0C, 0x14, 0x01, 0x03, 0x80, 0x1C, 0x15, 0x78, 0x0A, 0x1E, 0xAC, 0x98, 0x59, 0x56, 0x85, 0x28, 
    0x29, 0x52, 0x57, 0x20, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x8C, 0x0A, 0xD0, 0x8A, 0x20, 0xE0, 0x2D, 0x10, 0x10, 0x3E, 
    0x96, 0x00, 0xFA, 0xBE, 0x00, 0x00, 0x00, 0x18, 0xD5, 0x09, 0x80, 0xA0, 0x20, 0xE0, 0x2D, 0x10, 
    0x10, 0x60, 0xA2, 0x00, 0xFA, 0xBE, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x56, 
    0x41, 0x2D, 0x31, 0x38, 0x33, 0x31, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFD, 
    0x00, 0x17, 0x3D, 0x0D, 0x2E, 0x11, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0xFA, 
    
    0x02, 0x03, 0x1F, 0xF1, 0x43, 0x84, 0x10, 0x03, 0x23, 0x09, 0x07, 0x07, 0x83, 
    0x01, 0x00, 0x00, 0xE2, 0x00, 0x0F, 0xE3, 0x05, 0x03, 0x01, 
    0x67, 0x03, 0x0C, 0x00, 0x10, 0x00, 0x88, 0x2D, , 
    0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20, 0x6E, 0x28, 0x55, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x1E, 
    0x8C, 0x0A, 0xD0, 0x8A, 0x20, 0xE0, 0x2D, 0x10, 0x10, 0x3E, 0x96, 0x00, 0xA0, 0x5A, 0x00, 0x00, 0x00, 0x18, 
    0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C, 0x45, 0x00, 0xE0, 0x0E, 0x11, 0x00, 0x00, 0x1E, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, , 0x56
*/
// EDID up to 1080p no 3D (end)
};
struct it6681_dev_data it6681DEV;
DRIVER_DATA gDrv[1];

unsigned char it6681_edid_buf[IT6681_EDID_MAX_LENGTH] = {0};

struct it6681_dev_data* get_it6681_dev_data(void)
{
    return &it6681DEV;    
}

#define EnGRCLKPD 1

void it668x_debug_parse_mhl_0x16(void)
{
#if _DEBUG_MHL_1
    unsigned char rddata = mhltxrd(0x19);
        rddata = mhltxrd(0x16);

        /*
        //if( rddata&0x01 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Retry > 32 times !!!\n"));
        //if( rddata&0x02 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: DDC TimeOut !!!\n"));
        //if( rddata&0x04 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Receive Wrong Type Packet !!!\n"));
        //if( rddata&0x08 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Receive Unsupported Packet !!!\n"));
        //if( rddata&0x10 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Receive Incomplete Packet !!!\n"));
        //if( rddata&0x20 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Receive ABORT in Idle State !!!\n"));
        //if( rddata&0x40 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Receive Unexpected Packet!!!\n"));
        //if( rddata&0x80 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Receive ABORT in non-Idle State !!!\n"));

         */
#endif
}

void it668x_debug_parse_mhl_0x18(void)
{
#if _DEBUG_MHL_1
    struct it6681_dev_data *it6681 = get_it6681_dev_data();
    unsigned char rddata = mhltxrd(0x18);

    if( rddata&0x01 )
        IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Incomplete Packet !!!\n"));
    if( rddata&0x02 )
        IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: 100ms TimeOut !!!\n"));
    if( rddata&0x04 )
        IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Protocol Error !!!\n"));
    if( rddata&0x08 )
        IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Retry > 32 times !!!\n"));
    if( rddata&0x10 )
        IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Receive ABORT Packet !!!\n"));
    if( rddata&0x20 )
        IT6681_DEBUG_INT_PRINTF(("IT6681-MSC_MSG Requester Receive NACK Packet !!! ==> %dth NACK\n", it6681->MSGNackCnt));
    if( rddata&0x40 )
        IT6681_DEBUG_INT_PRINTF(("IT6681-Disable HW Retry and MSC Requester Arbitration Lose at 1st Packet !!! ==> %dth Lose\n", it6681->MSCBusyCnt));
    if( rddata&0x80 )
        IT6681_DEBUG_INT_PRINTF(("IT6681-Disable HW Retry and MSC Requester Arbitration Lose before 1st Packet !!! ==> %dth Lose\n", it6681->MSCBusyCnt));
#endif
}

void it668x_debug_parse_mhl_0x19(void)
{
#if _DEBUG_MHL_1
    unsigned char rddata = mhltxrd(0x19);
    
    if (rddata & 0x01)
        IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: TX FW Fail in the middle of the command sequence !!!\n"));
    if (rddata & 0x02)
        IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: TX Fail because FW mode RxPktFIFO not empty !!!\n"));
#endif
}

void it668x_debug_parse_mhl_0x1A(void)
{
#if _DEBUG_MHL_1
    unsigned char rddata = mhltxrd(0x19);

        rddata = mhltxrd(0x1A);

        /*
        //if( rddata&0x01 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Initial Bad Offset !!!\n"));
        //if( rddata&0x02 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Incremental Bad Offset !!!\n"));
        //if( rddata&0x04 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Invalid Command !!!\n"));
        //if( rddata&0x08 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Receive dPacket in Responder Idle State !!!\n"));
        //if( rddata&0x10 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Incomplete Packet !!!\n"));
        //if( rddata&0x20 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: 100ms TimeOut !!!\n"));
        //if( rddata&0x40 ) {
        //    it6681->MSCFailCnt--;
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-MSC_MSG Responder Busy ==> Return NACK Packet !!!\n"));
        //}
        //if( rddata&0x80 )
        //    IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Protocol Error !!!\n"));

         */
#endif
}

void it668x_debug_parse_mhl_0x1B(void)
{
#if _DEBUG_MHL_1
    unsigned char rddata = mhltxrd(0x19);
        rddata = mhltxrd(0x1B);
        if (rddata & 0x01)
            IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Retry > 32 times !!!\n"));
        if (rddata & 0x02)
        {
            IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: Receive ABORT Packet !!!\n"));
            // get_msc_errcode();
        }
#endif
}

void hdmitx_pwron( void )
{
     // MHLTX PwrOn
     hdmitxset(0x0F, 0x78, 0x38);   // PwrOn GRCLK
     hdmitxset(0x05, 0x01, 0x00);   // PwrOn PCLK

     // PLL PwrOn
     hdmitxset(0x61, 0x10, 0x00);   // PwrOn DRV
     hdmitxset(0x62, 0x44, 0x00);   // PwrOn XPLL


     //phytxset(0x61, 0x20, 0x00);   // PwrOn DRV
     //phytxset(0x62, 0x44, 0x00);   // PwrOn XPLL
     //phytxset(0x64, 0x40, 0x00);   // PwrOn IPLL


     // PLL Reset OFF
     hdmitxset(0x61, 0x10, 0x00);   // DRV_RST
     hdmitxset(0x62, 0x08, 0x08);   // XP_RESETB


     //phytxset(0x61, 0x10, 0x00);   // DRV_RST
     //phytxset(0x62, 0x08, 0x08);   // XP_RESETB
     //phytxset(0x64, 0x04, 0x04);   // IP_RESETB


     gDrv->GRCLKPD = FALSE;
     gDrv->TXAFEPD = FALSE;
     IT6681_DEBUG_PRINTF(("Power On MHLTX \n"));
}

void hdmitx_pwrdn( void )
{
     // Enable GRCLK
     hdmitxset(0x0F, 0x40, 0x00);

     // PLL Reset
     hdmitxset(0x61, 0x10, 0x10);   // DRV_RST
     hdmitxset(0x62, 0x08, 0x00);   // XP_RESETB

     delay1ms(1);
     delay1ms(1);

     // PLL PwrDn
     hdmitxset(0x61, 0x20, 0x20);   // PwrDn DRV
     hdmitxset(0x62, 0x44, 0x44);   // PwrDn XPLL

     //hdmitxwr(0x70, 0x00);      // Select TXCLK power-down path
//Emily it6681 mark start
     // MHLTX PwrDn

//     hdmitxset(0x05, 0x01, 0x01);   // PwrDn PCLK
//   hdmitxset(0x0F, 0x08, 0x08);   // PwrDn CRCLK
//Emily it6681 mark
//     hdmitxwr(0xE0, 0xC0);          // PwrDn GIACLK, IACLK, ACLK and SCLK
//     hdmitxset(0x72, 0x03, 0x00);   // PwrDn GTxCLK(QCLK)
//Emily it6681 mark end
     if( EnGRCLKPD && (mhltxrd(0x10)&0x01)==0x00 ) 
     {
         hdmitxset(0x0F, 0x40, 0x40);   // PwrDn GRCLK
         gDrv->GRCLKPD = TRUE;
     }

     gDrv->TXAFEPD = TRUE;
     IT6681_DEBUG_PRINTF(("Power Down MHLTX \n"));
}

void hdmirx_terminator_off(void)
{
    BYTE uc;

    uc = hdmirxrd(0x0A) | (1 << 7) | (1 << 1);
    IT6681_DEBUG_PRINTF(("hdmirx_Terminator_Off(),reg0A=%02X\n", (int)uc));
    hdmirxwr(0x0A, uc);
}

void hdmirx_terminator_on(void)
{
    BYTE uc;

    uc = hdmirxrd(0x0A)&~((1 << 7) | (1 << 1));
    IT6681_DEBUG_PRINTF(("hdmirx_Terminator_On(),reg0A=%02X\n", (int)uc));
    hdmirxwr(0x0A, uc);
}

void hdmitx_set_termination(int enabled)
{
    if (enabled)
    {
        hdmitxset(0x61, 0x10, 0x00);
    }
    else
    {
        hdmitxset(0x61, 0x10, 0x10);
    }
}

void hdmirx_hpd_low(void)
{
    if ( gDrv->ForceRxHPD && gDrv->KeepRxHPD )
    {
        return;        
    }

    hdmirxset(0x14, 0x80, 0x00);
    hdmirxset(0x14, 0x30, 0x20);
    IT6681_DEBUG_PRINTF(("hdmirx_hpd_low()\n"));

    hdmirx_terminator_off();
}

void hdmirx_hpd_high(void)
{
    // hdmirxset(0x14, 0x80, 0x00);
    // hdmirxset(0x14, 0x30, 0x30);
    hdmirxset(0x08, 0x01, 0x01);
    delay1ms(10);
    hdmirxset(0x08, 0x01, 0x00);
    //hdmirxset(0x14, 0x80, 0x80);
    hdmirxset(0x14, 0x30, 0x30);
    IT6681_DEBUG_PRINTF(("hdmirx_hpd_high()\n"));

    hdmirx_terminator_on();
}
void it668x_switch_to_mhl( void )
{
    if(gDrv->IsIT6682) set_operation_mode(MODE_MHL); // GPIO switch to MHL
    mhltxset(0x0F, 0x11, 0x11); // reset Cbus fsm
    mhltxset(0x0F, 0x11, 0x00); 
}
void it668x_switch_to_usb( void )
{
    if(gDrv->IsIT6682) set_operation_mode(MODE_USB); // GPIO switch to USB
    mhltxset(0x0F, 0x01, 0x01); // Switch to USB, keep Cbus
}
void it668x_set_vbus_output( char enabled )
{
    if(gDrv->IsIT6682) set_vbus_output(enabled); // GPIO set VBUS output
    if ( enabled )
    {
        mhltxset(0x0F, 0x02, 0x02);
    }
    else
    {
        mhltxset(0x0F, 0x02, 0x00);
    }
}


void SetMHLTXPath(void)
{
    struct it6681_dev_data *it6681 = get_it6681_dev_data();
    BYTE _data;
    hdmitxwr(0xF8, 0xFF);
    hdmitxwr(0xF8, 0xFF);
    hdmitxwr(0xF8, 0xC3);
    hdmitxwr(0xF8, 0xA5);
    if (it6681->HDCPEnable)
    {
        hdmitxset(0xE5, 0xB0, 0x20);
        hdmitxset(0xE0, 0x38, 0x20);
    }
    else
    {
        hdmitxset(0xE5, 0xB0, 0x20);
        hdmitxset(0xE0, 0x38, 0x38);
    }
    hdmitxset(0xE4, 0x08, 0x08);

    hdmitxwr(0xF8, 0xFF);
    IT6681_DEBUG_PRINTF(("###############################################\n"));
    IT6681_DEBUG_PRINTF(("# SetMHLTXPath                                #\n"));
    _data = (hdmitxrd(0xE6) & 0x18) >> 3;
    IT6681_DEBUG_PRINTF(("# MHLData Path Read Back  = %d\n", (int)_data));
    IT6681_DEBUG_PRINTF(("Register 0xE0 = %02x \n", (int)hdmitxrd(0xe0)));
    IT6681_DEBUG_PRINTF(("###############################################\n"));

}

void it6681_init_internal_edid(void)
{
    int i;

    for(i=0 ; i<IT6681_EDID_MAX_LENGTH ; i++ )
    {
        if ( i<sizeof(it6681_internal_edid) )
        {
            it6681_edid_buf[i] = it6681_internal_edid[i];
        }
        else
        {
            break;
        }
    }
}
//
////////////////////////////////////////////////////////////////////
static unsigned long cal_pclk(void /* struct it6681_dev_data *it6681 */ )
{

    unsigned char predivsel;
    unsigned int rddata, i, pwdiv;
    unsigned long sumdiv;

    unsigned long sum;

    unsigned long PCLK, RCLK;

    // PCLK Count Pre-Test
    hdmitxset(0xD7, 0xF0, 0x80); // must reset prediv value to "0"
    delay1ms(1);
    hdmitxset(0xD7, 0xF0, 0x00);

    rddata = (unsigned int)hdmitxrd(0xd7);
    rddata = (rddata & 0x0F) << 8;
    rddata += (unsigned int)hdmitxrd(0xd8);

    // rddata *=2;	 //RCLK_FREQ_20M

    IT6681_DEBUG_PRINTF(("IT6681-PCLK Count Pre-Test value=%u\n", rddata));

    if (rddata < 16)
    {
        predivsel = 7;
        pwdiv = 128;
    }
    else if (rddata < 32)
    {
        predivsel = 6;
        pwdiv = 64;
    }
    else if (rddata < 64)
    {
        predivsel = 5;
        pwdiv = 32;
    }
    else if (rddata < 128)
    {
        predivsel = 4;
        pwdiv = 16;
    }
    else if (rddata < 256)
    {
        predivsel = 3;
        pwdiv = 8;
    }
    else if (rddata < 512)
    {
        predivsel = 2;
        pwdiv = 4;
    }
    else if (rddata < 1024)
    {
        predivsel = 1;
        pwdiv = 2;
    }
    else
    {
        predivsel = 0;
        pwdiv = 1;
    }
    IT6681_DEBUG_PRINTF(("IT6681-predivsel=%X\n", (int)predivsel));

    sum = 0;

    hdmitxset(0xD7, 0x70, (predivsel << 4));

    for (i = 0; i < 4; i++)
    {

        hdmitxset(0xD7, 0x80, 0x80);
        delay1ms(1);
        hdmitxset(0xD7, 0x80, 0x00);

        rddata = (unsigned int)hdmitxrd(0xd8);
        rddata += ((((unsigned int)hdmitxrd(0xd7)) & 0x0F) << 8);

        // rddata *=2;	 //RCLK_FREQ_20M

        sum += (unsigned long)rddata;

        IT6681_DEBUG_PRINTF(("IT6681-   sum= %lu \n", sum));

    }

    sumdiv = (unsigned long)(i * pwdiv);

    IT6681_DEBUG_PRINTF(("IT6681-   sumdiv= %lu \n", sumdiv));

    sum = sum / sumdiv;

    rddata = (unsigned int)mhltxrd(0x03);

    RCLK = (unsigned long)mhltxrd(0x02);
    RCLK += (unsigned long)((rddata & 0x80) << 1);
    RCLK *= 100;
    RCLK += (unsigned long)(rddata & 0x7F);

    // HDMITX_DEBUG_PRINTF(("IT6681- RCLK =%lu  register RCLK =%lu \n", it6681->RCLK,RCLK));
#if( _Reg2x656Clk )
    PCLK = ((RCLK * 2048) / sum) / 2;
#else
    PCLK = (RCLK * 2048) / sum;
#endif

    IT6681_DEBUG_PRINTF(("IT6681-Count TxCLK=%lu MHz\n", (unsigned long)PCLK / 1000));

    return PCLK;

}

static unsigned long read_siprom(struct it6681_dev_data *it6681)
{
	unsigned short Addr;
	unsigned char BlockSel;
	unsigned char RData[16]; 
    unsigned long rddata;
    unsigned char reg20;

    reg20 = hdmitxrd(0x20);
    hdmitxwr( 0xF8, 0xC3 );
    hdmitxwr( 0xF8, 0xA5 );
    hdmitxwr( 0x20, 0x98 );	 
    hdmitxwr( 0xF8, 0xff );
 
	Addr=0x00;

    hdmitxwr( 0x30, 0 );
    hdmitxwr( 0x31, 0 );
    hdmitxwr( 0x33, 0x04 );

    RData[0] = hdmitxrd( 0x24 );
    RData[1] = hdmitxrd( 0x24 );
    RData[2] = hdmitxrd( 0x24 );
    RData[3] = hdmitxrd( 0x24 );

    IT6681_DEBUG_PRINTF(("IT6681-OCLK SIPROM 0x0 = 0x%02x 0x%02x 0x%02x, 0x%02x\n", (int)RData[0], (int)RData[1], (int)RData[2], (int)RData[3] ));   
    IT6681_DEBUG_PRINTF(("IT6681-0x20=0x%02x\n", (int)reg20));

	if(RData[0]==0xFF && RData[1]==0x00 &&  RData[2]==0xFF && RData[3]==0x00 ) BlockSel=1;
	else BlockSel=0;

	Addr= (BlockSel<<9)+0x160;
    hdmitxwr( 0x30, (Addr&0xF00)>>8 );
    hdmitxwr( 0x31, (Addr&0xFF) );
    hdmitxwr( 0x33, 0x04 );

    // Read back 3 Byte
    RData[0] = hdmitxrd( 0x24 );
    RData[1] = hdmitxrd( 0x24 );
    RData[2] = hdmitxrd( 0x24 );

    IT6681_DEBUG_PRINTF(("IT6681-OCLK SIPROM 0x%x = 0x%02x 0x%02x 0x%02x, BlockSel=%d\n", (int)Addr, (int)RData[0], (int)RData[1], (int)RData[2], (int)BlockSel ));    

    rddata = 0;
    if ( RData[2] <= 0x50 && RData[2] >= 0x40 )
    {
        rddata = (unsigned long)RData[0];
        rddata += (unsigned long)RData[1] << 8;
        rddata += ( unsigned long)RData[2] << 16;

        rddata = rddata / 100;       
        IT6681_DEBUG_PRINTF(("IT6681- cal_oclk using sipRom data OSCCLK=%lu\n", rddata ));    
    }
    else
    {
        IT6681_DEBUG_PRINTF(("IT6681-Warning ! cal_oclk sipRom data 0x14=0x%02x\n", (int)RData[0]));    
        rddata = 0;
    } 

    hdmitxwr( 0xF8, 0xC3 );
    hdmitxwr( 0xF8, 0xA5 );
    hdmitxwr( 0x20, 0x08 );
    //hdmitxwr( 0x20, reg20 );	 
    hdmitxwr( 0xF8, 0xFF );

	return rddata;
}
static void cal_oclk(struct it6681_dev_data *it6681)
{

    int i;
    unsigned long rddata;
    // float sum, OSCCLK;
    unsigned long sum, OSCCLK;
    unsigned long t1, t2, tsum;
    int oscdiv, t10usint, t10usflt;

    OSCCLK = read_siprom(it6681);
    if ( OSCCLK == 0 )
    {
    mhltxset(0x0F, 0x10, 0x10); // Disable CBUS

    sum = 0;
    tsum = 0;

    for (i = 0; i < 4; i++)
    {

        t1 = it6681_get_tick_count();
        mhltxwr(0x01, 0x41);

        delay1ms(98);

        t2 = it6681_get_tick_count();
        mhltxwr(0x01, 0x40);

        rddata = (unsigned long)mhltxrd(0x12);
        rddata += (unsigned long)mhltxrd(0x13) << 8;
        rddata += (unsigned long)mhltxrd(0x14) << 16;

        sum += rddata;
        tsum += (t2-t1);

        IT6681_DEBUG_PRINTF(("IT6681-loop=%d, rddata=%lu  sum =%lu\n", i, rddata, sum));
    }

    //sum >>= 2; // sum/4

    OSCCLK = sum / tsum;
    }
    if ( OSCCLK < 39000UL || OSCCLK > 59000UL )
    {
        OSCCLK = 49854;
    }

    IT6681_DEBUG_PRINTF(("IT6681-OSCCLK=%luKHz\n", OSCCLK));

    oscdiv = OSCCLK / 10000;

    if ((OSCCLK % 10000) > 5000)
        oscdiv++;

    IT6681_DEBUG_PRINTF(("IT6681-oscdiv=%d \n", oscdiv));

    IT6681_DEBUG_PRINTF(("IT6681-OCLK=%lukHz\n", OSCCLK / oscdiv));

    mhltxset(0x01, 0x70, oscdiv << 4);

    OSCCLK >>= 2;

    it6681->RCLK = (unsigned long)OSCCLK;

    // it6681->RCLK*= 1.1; 		//For CTS, not sure yet, marked at first
    t10usint = OSCCLK / 100;
    t10usflt = OSCCLK % 100;

    // IT6681_DEBUG_PRINTF(("IT6681-RCLK=%lukHz\n", it6681->RCLK));
    IT6681_DEBUG_PRINTF(("IT6681-T10usInt=0x%X, T10usFlt=0x%X\n", (int)t10usint, (int)t10usflt));
    // it6681->RCLK/= 1.1;

    mhltxwr(0x02, (unsigned char)(t10usint & 0xFF));
    mhltxwr(0x03, (unsigned char)(((t10usint & 0x100) >> 1) + t10usflt));
    IT6681_DEBUG_PRINTF(("IT6681-MHL reg 0x02 = %X , reg 0x03 = %X\n", (int)mhltxrd(0x02), (int)mhltxrd(0x03)));
}

static int ddcwait(void)
{
    int cbuswaitcnt;
    unsigned char Reg07;
    unsigned char Reg06;
    unsigned char rddata;
    // unsigned char MHL04;
    unsigned char MHL05;
    // unsigned char rddata;

    cbuswaitcnt = 0;
    do
    {
        cbuswaitcnt++;
        //delay1ms(CBUSWAITTIME);
        /* if( mhltxrd(0x00)&0x80 ) {
        MHL04 = mhltxrd(0x04);
        if( MHL04&0x01 ) {
        mhltxwr(0x04, 0x01);
        //IT6681dev[DevNum].PKTFailCnt = 0;
        //printf("CBUS Link Layer TX Packet Done Interrupt ...\n");
        }
        if( MHL04&0x04 ) {
        mhltxwr(0x04, 0x04);
        //IT6681dev[DevNum].PKTFailCnt = 0;
        //printf("CBUS Link Layer RX Packet Done Interrupt ...\n");
        }
        }
        else */
        Reg07 = hdmitxrd(0x07);
        if (Reg07 & 0x02)
        {
            return FAIL_HPD_CHG;
        }
    }
    while ((mhltxrd(0x1C) & 0x01) == 0x01 && cbuswaitcnt < CBUSWAITNUM);

    Reg07 = hdmitxrd(0x07);
    MHL05 = mhltxrd(0x05);

    if (cbuswaitcnt == CBUSWAITNUM || Reg07 & 0x20 || MHL05 & 0x20)
    {

        if (cbuswaitcnt == CBUSWAITNUM)
            HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: DDC Bus Wait TimeOut !!!\n"));

        if ((Reg06 & 0x20) && ((hdmitxrd(0x10) & 0x01) == 0x00)) // 20121213 add condition (hdmitxrd(0x10)&0x01)==0x00 )
        {
            hdmitxwr(0x06, 0x20);
            HDMITX_MHL_DEBUG_PRINTF(("IT6681-DDC NACK Interrupt ...\n"));

            hdmitxset(0x20, 0x01, 0x00); // Disable CP_Desired
            hdmitxset(0x04, 0x01, 0x01);

        }

        if (MHL05 & 0x20)
        {
            // IT6681dev[DevNum].DDCFailCnt++;
            // printf("DDC Req Fail Interrupt ... ==> %dth Fail\n", IT6681dev[DevNum].DDCFailCnt);
            rddata = mhltxrd(0x16);
            // mhltxwr(0x05, 0x20);

            if (rddata & 0x01)
            {
                HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: Retry > 32 times !!!\n"));
                mhltxwr(0x16, 0x01);
            }
            if (rddata & 0x02)
            {
                HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: DDC TimeOut !!!\n"));
                mhltxwr(0x16, 0x02);
            }
            if (rddata & 0x04)
            {
                HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: Receive Wrong Type Packet !!!\n"));
                mhltxwr(0x16, 0x04);
            }
            if (rddata & 0x08)
            {
                HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: Receive Unsupported Packet !!!\n"));
                mhltxwr(0x16, 0x08);
            }
            if (rddata & 0x10)
            {
                HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: Receive Incomplete Packet !!!\n"));
                mhltxwr(0x16, 0x10);
            }
            if (rddata & 0x20)
            {
                HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: Receive ABORT in Idle State !!!\n"));
                mhltxwr(0x16, 0x20);
                return RCVABORT;
            }
            if (rddata & 0x40)
            {
                HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: Receive Unexpected Packet !!!\n"));
                mhltxwr(0x16, 0x40);
            }
            if (rddata & 0x80)
            {
                HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: Receive ABORT in non-Idle State !!!\n"));
                mhltxwr(0x16, 0x80);
                return RCVABORT;
            }
        }
        else
            HDMITX_MHL_DEBUG_PRINTF(("IT6681-Unknown Issue !!!\n"));

        HDMITX_MHL_DEBUG_PRINTF(("\n\n"));

        return FAIL;
    }
    else
        return SUCCESS;
}

int ddcfire(int offset, int wdata)
{
    int ddcreqsts, retrycnt;

    retrycnt = 0;
    do
    {
        hdmitxwr((unsigned char)offset, (unsigned char)wdata);

        ddcreqsts = ddcwait();
        if (ddcreqsts == FAIL_HPD_CHG)
        {
            HDMITX_MHL_DEBUG_PRINTF(("IT6681- ddcfire() ==>  FAIL_HPD_CHG\n"));
            return FAIL_HPD_CHG;
        }
        else if (ddcreqsts == RCVABORT)
        {
            // get_ddc_errcode();
            HDMITX_MHL_DEBUG_PRINTF(("IT6681- ddcfire() ==>   RCVABORT\n"));

        }

        if (ddcreqsts != SUCCESS)
        {
            retrycnt++;
            // idle(200000);
            delay1ms(200);
            HDMITX_MHL_DEBUG_PRINTF(("IT6681-Retry this command again ... ==> %dth retry\n", retrycnt));
        }
    }
    while (ddcreqsts != SUCCESS && retrycnt != 20);

    if (ddcreqsts != SUCCESS)
    {
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: DDC Request Maximum Retry FAil !!!\n"));
        return FAIL;
    }
    else
        return SUCCESS;
}


static int mscwait(void)
{
    int cbuswaitcnt;
    unsigned char MHL05,Reg07;
    unsigned char rddata[2];

    cbuswaitcnt = 0;
    do
    {
        cbuswaitcnt++;
        /*
        //check CBUS PKT IRQ
        if( mhltxrd(0x00)&0x80 )
        {
        MHL04 = mhltxrd(0x04);

        if( MHL04&0x01 )
        {
        mhltxwr(0x04, 0x01);

        //it6681->PKTFailCnt = 0;
        //               printf("CBUS Link Layer TX Packet Done Interrupt ...\n");
        }

        if( MHL04&0x04 ) {
        mhltxwr(0x04, 0x04);

        //it6681->PKTFailCnt = 0;
        //               printf("CBUS Link Layer RX Packet Done Interrupt ...\n");

        }
        }
        else */
        //delay1ms(CBUSWAITTIME);
        Reg07 = hdmitxrd(0x07);
        if (Reg07 & 0x02)
        {
            return FAIL;
        }

    }
    while ((mhltxrd(0x1C) & 0x02) == 0x02 && cbuswaitcnt < CBUSWAITNUM);

    MHL05 = mhltxrd(0x05);
    if ((cbuswaitcnt == CBUSWAITNUM) || MHL05 & 0x02)
    {
        
        if( cbuswaitcnt==CBUSWAITNUM )
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: MSC Wait TimeOut !!!"));

        if( MHL05&0x02 ) {
        //it6681->MSCFailCnt++;
        //printf("MSC Req Fail Interrupt ... ==> %dth Fail\n", it6681->MSCFailCnt);
        //mhltxbrd(0x18, 2, &rddata[0]);
        rddata[0] = mhltxrd(0x18);
        rddata[1] = mhltxrd(0x19);
        //          mhltxwr(0x05, 0x02);

        if( rddata[0]&0x01 ) {
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: Incomplete Packet !!!\n"));
        mhltxwr(0x18, 0x01);
        }
        if( rddata[0]&0x02 ) {
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: MSC 100ms TimeOut !!!\n"));
        mhltxwr(0x18, 0x02);
        }
        if( rddata[0]&0x04 ) {
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: Protocol Error !!!\n"));
        mhltxwr(0x18, 0x04);
        }
        if( rddata[0]&0x08 ) {
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: Retry > 32 times !!!\n"));
        mhltxwr(0x18, 0x08);
        }
        if( rddata[0]&0x10 ) {
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: Receive ABORT Packet !!!\n"));
        mhltxwr(0x18, 0x10);
        return RCVABORT;
        }
        if( rddata[0]&0x20 ) {

        //it6681->MSGNackCnt++;
        //it6681->MSCFailCnt--;

        //printf("MSC_MSG Requester Receive NACK Packet !!! ==> %dth NACK\n", it6681->MSGNackCnt);
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-MSC_MSG Requester Receive NACK Packet !!! ==> NACK\n"));
        mhltxwr(0x18, 0x20);
        return RCVNACK;
        }
        if( rddata[0]&0x40 ) {

        //it6681->MSCBusyCnt++;
        //it6681->MSCFailCnt--;
        //printf("Disable HW Retry and MSC Requester Arbitration Lose at 1st Packet !!! ==> %dth Lose\n", it6681->MSCBusyCnt);

        HDMITX_MHL_DEBUG_PRINTF(("IT6681-Disable HW Retry and MSC Requester Arbitration Lose at 1st Packet !!!\n"));
        mhltxwr(0x18, 0x40);
        return ARBLOSE;
        }
        if( rddata[0]&0x80 ) {
        //it6681->MSCBusyCnt++;
        //it6681->MSCFailCnt--;
        //printf("Disable HW Retry and MSC Requester Arbitration Lose before 1st Packet !!! ==> %dth Lose\n", it6681->MSCBusyCnt);
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-Disable HW Retry and MSC Requester Arbitration Lose before 1st Packet !!!\n"));
        mhltxwr(0x18, 0x80);
        return ARBLOSE;
        }

        if( rddata[1]&0x01 ) {
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: TX FW Fail in the middle of the command sequence !!!\n"));
        mhltxwr(0x19, 0x01);
        return FWTXFAIL;
        }
        if( rddata[1]&0x02 ) {
        //it6681->MSCFailCnt--;
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: TX Fail because FW mode RxPktFIFO not empty !!!\n"));
        mhltxwr(0x19, 0x02);
        return FWRXPKT;
        }
        }
        else
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-ERROR: Unknown Issue !!!\n"));

        //HDMITX_MHL_DEBUG_PRINTF(("\n\n"));

        //if( it6681->MSCFailCnt>0 )
        //    printf("ERROR: MSCFailCnt > 0 !!!\n");

        return FAIL;
    }
    else
        return SUCCESS;
}

static int mscfire(int offset, int wdata)
{
    int fwmodeflag = FALSE;
    int wrburstflag = FALSE;
    int mscreqsts;

    if (offset == 0x51)
    {
        if (wdata == 0x80)
            fwmodeflag = TRUE;
        if (wdata == 0x01)
            wrburstflag = TRUE;
    }

    mhltxwr((unsigned char)offset, (unsigned char)wdata);

    mscreqsts = mscwait();

    /* switch(mscreqsts)
    {

    case FWRXPKT:
    HDMITX_MHL_DEBUG_PRINTF(("IT6681-MSC FIRE() ERROR   ========> FWRXPKT   \n"));
    break;
    case SUCCESS:
    //printf("MSCFIRE() SUCCESS   \n");
    return SUCCESS;
    case RCVABORT:
    HDMITX_MHL_DEBUG_PRINTF(("IT6681-MSC FIRE() ERROR    =======> RCVABORT  \n"));
    break;
    default:
    break;
    } */
    if ( 0 != mscreqsts )
    {
        HDMITX_MHL_DEBUG_PRINTF(("IT6681-MSC FIRE() %d  \n", (int)mscreqsts));
    }


    return(mscreqsts == SUCCESS) ? SUCCESS : FAIL;
}

////////////////////////////////////////////////////////////////////
// void set_mhlint( void )
//
//
//
////////////////////////////////////////////////////////////////////

static void set_mhlint(unsigned char offset, unsigned char field)
{
    mhltxwr(0x54, (unsigned char)offset);
    mhltxwr(0x55, (unsigned char)field);
    mscfire(0x50, 0x80);
}

////////////////////////////////////////////////////////////////////
// void set_mhlsts( void )
//
//
//
////////////////////////////////////////////////////////////////////
static void set_mhlsts(unsigned char offset, unsigned char status)
{
    mhltxwr(0x54, (unsigned char)offset);
    mhltxwr(0x55, (unsigned char)status);
    mscfire(0x50, 0x80);
}

void cbus_send_mscmsg(struct it6681_dev_data *it6681)
{
    mhltxwr(0x54, it6681->txmsgdata[0]);
    mhltxwr(0x55, it6681->txmsgdata[1]);
    mscfire(0x51, 0x02);
}

void mhl_read_mscmsg(struct it6681_dev_data *it6681)
{
    it6681->rxmsgdata[0] = mhltxrd(0x60);
    it6681->rxmsgdata[1] = mhltxrd(0x61);

    switch(it6681->rxmsgdata[0])
    {
    case MSG_MSGE:
        MHL_MSC_DEBUG_PRINTF(("RX MSGE => "));
        switch(it6681->rxmsgdata[1])
        {
        case 0x00:
            MHL_MSC_DEBUG_PRINTF(("No Error\n"));
            break;
        case 0x01:
            MHL_MSC_DEBUG_PRINTF(("ERROR: Invalid sub-command code !!!\n"));
            break;
        default:
            MHL_MSC_DEBUG_PRINTF(("ERROR: Unknown MSC_MSG status code 0x%02X !!!\n", it6681->rxmsgdata[1]));
        }
        break;

#if _SUPPORT_RCP_
    case MSG_RCP:
        mhl_parse_RCPkey(it6681);
        break;
    case MSG_RCPK:
        break;
    case MSG_RCPE:
        switch(it6681->rxmsgdata[1])
        {
        case 0x00:
            MHL_MSC_DEBUG_PRINTF(("No Error\n"));
            break;
        case 0x01:
            MHL_MSC_DEBUG_PRINTF(("ERROR: Ineffective RCP Key Code !!!\n"));
            break;
        case 0x02:
            MHL_MSC_DEBUG_PRINTF(("Responder Busy ...\n"));
            break;
        default:
            MHL_MSC_DEBUG_PRINTF(("ERROR: Unknown RCP status code !!!\n"));
        }
        break;
#endif

#if _SUPPORT_RAP_
    case MSG_RAP:
        mhl_parse_RAPkey(it6681);
        break;
    case MSG_RAPK:
        MHL_MSC_DEBUG_PRINTF(("RX RAPK  => "));
        switch(it6681->rxmsgdata[1])
        {
        case 0x00:
            MHL_MSC_DEBUG_PRINTF(("No Error\n"));
            break;
        case 0x01:
            MHL_MSC_DEBUG_PRINTF(("ERROR: Unrecognized Action Code !!!\n"));
            break;
        case 0x02:
            MHL_MSC_DEBUG_PRINTF(("ERROR: Unsupported Action Code !!!\n"));
            break;
        case 0x03:
            MHL_MSC_DEBUG_PRINTF(("Responder Busy ...\n"));
            break;
        default:
            MHL_MSC_DEBUG_PRINTF(("ERROR: Unknown RAP status code 0x%02X !!!\n", it6681->rxmsgdata[1]));
        }
        break;
#endif

#if _SUPPORT_UCP_
    case MSG_UCP:
        mhl_parse_UCPkey(it6681);
        break;
    case MSG_UCPK:
        break;
    case MSG_UCPE:
        switch(it6681->rxmsgdata[1])
        {
        case 0x00:
            MHL_MSC_DEBUG_PRINTF(("No Error\n"));
            break;
        case 0x01:
            MHL_MSC_DEBUG_PRINTF(("ERROR: Ineffective UCP Key Code !!!\n"));
            break;
        default:
            MHL_MSC_DEBUG_PRINTF(("ERROR: Unknown UCP status code !!!\n"));
        }
        break;
#endif

#if _SUPPORT_UCP_MOUSE_
    case MSG_MOUSE:
        mhl_parse_MOUSEkey(it6681);
        break;
    case MSG_MOUSEK:
        break;
    case MSG_MOUSEE:
        switch(it6681->rxmsgdata[1])
        {
        case 0x00:
            MHL_MSC_DEBUG_PRINTF(("No Error\n"));
            break;
        case 0x01:
            MHL_MSC_DEBUG_PRINTF(("ERROR: Ineffective UCP MOUSE Key Code !!!\n"));
            break;
        default:
            MHL_MSC_DEBUG_PRINTF(("ERROR: Unknown UCP MOUSE  status code !!!\n"));
        }
        break;
#endif

    default:
        MHL_MSC_DEBUG_PRINTF(("ERROR: Unknown MSC_MSG sub-command code 0x%02X !!!\n", it6681->rxmsgdata[0]));
        it6681->txmsgdata[0] = MSG_MSGE;
        it6681->txmsgdata[1] = 0x01;
        cbus_send_mscmsg(it6681);
    }
}

int it6681_read_edid_block(unsigned char block, void *buffer)
{
    unsigned char offset, readBytes, segment, readLength, i;
    unsigned char *pBuffer = (unsigned char*)buffer;
    unsigned char edid_read_mode = 0x3;
    unsigned short sum;
    int ret = 0;

    hdmitxwr(0xf8, 0xc3);
    hdmitxwr(0xf8, 0xa5);
    hdmitxset(0xe0, 0x08, 0x00);
    hdmitxset(0x10, 0x01, 0x01); // Enable PC DDC Mode
    readBytes = 32;
    segment = block / 2;
    offset = 128 * (block & 0x1);
    readLength = 0;

    // IT6681_DEBUG_PRINTF(("========= Read EDID Block %d =============\n", (int)block));

    while (readLength < 128)
    {
        hdmitxwr(0x15, 0x09); // DDC FIFO Clear
        hdmitxwr(0x11, 0xA0); // EDID Address
        hdmitxwr(0x12, offset); // EDID Offset
        hdmitxwr(0x13, readBytes); // Read ByteNum
        hdmitxwr(0x14, segment); // EDID Segment

        // IT6681_DEBUG_PRINTF(("off=%d, rb=%d, seg=%d\n", (int)offset, (int)readBytes, (int)segment));
        // IT6681_DEBUG_PRINTF(("off=%d, rb=%d, seg=%d\n", (int)hdmitxrd(0x12), (int)hdmitxrd(0x13), (int)hdmitxrd(0x14)));
        
        if( edid_read_mode == 0x0 )
		{
			hdmitxset(0x1B, 0x80, 0x80);
		}
		else
		{
			hdmitxset(0x1B, 0x80, 0x0);
		}
        if (ddcfire(0x15, edid_read_mode) != SUCCESS) // EDID Read Fire
        {
            IT6681_DEBUG_PRINTF(("ERROR: DDC EDID Read Fail 2!!!\n"));
            ret = -1;
            break;
        }

	#if 1
		hdmitxbrd(0x17, pBuffer, readBytes);
        offset+=readBytes;
        readLength+=readBytes;
        pBuffer+=readBytes;
	#else
        for (i = 0; i < readBytes; i++)
        {
            *pBuffer = hdmitxrd(0x17);

            // IT6681_DEBUG_PRINTF(("%02x ", (int)*pBuffer));

            offset++;
            readLength++;
            pBuffer++;
        }
	#endif
		edid_read_mode = 0;
        // IT6681_DEBUG_PRINTF(("\n"));
    }

    if (ret == 0)
    {
        pBuffer = (unsigned char*)buffer;


        IT6681_DEBUG_PRINTF(("IT6681-Read EDID block %d:\n", (int)block));

        for( i=0 ; i<16 ; i++ )
        {
            IT6681_DEBUG_PRINTF(("IT6681-%02x - %02x %02x %02x %02x %02x %02x %02x %02x\n", (int)(i<<3),
                                 (int)pBuffer[0], (int)pBuffer[1], (int)pBuffer[2], (int)pBuffer[3],    
                                 (int)pBuffer[4], (int)pBuffer[5], (int)pBuffer[6], (int)pBuffer[7]));
            pBuffer += 8;
        }

        sum=0;
        pBuffer = (unsigned char*)buffer;
        for( i=0 ; i<127 ; i++ )
        {
            sum+=pBuffer[i];
        }
        sum = 256-(sum%256);
        if ( sum != pBuffer[127] )
        {
            IT6681_DEBUG_PRINTF(("IT6681-EDID chksum error %02x => %02x\n", (int)pBuffer[127], (int)sum));
            pBuffer[127] = sum;
        }

        if ( block == 0 )
        {
            gDrv->EdidChkSum = pBuffer[127];
        }
    }

    hdmitxwr(0x15, 0x09); // DDC FIFO Clear
    hdmitxset(0x10, 0x01, 0x00); // Disable PC DDC Mode

    // emily mark it6681 start
    // if(EnHDCP==0 && (InColorMode == OutColorMode)) hdmitxset(0xe0, 0x08, 0x08); //ENHDCP20130313

    hdmitxset(0xe0, 0x08, 0x08);
    hdmitxwr(0xf8, 0x00);
    // emily mark it6681 end
    return ret;
}

int it6681_fw_read_edid(void)
{
    unsigned char block, block_count;
    int ret = 0;

    if (0 == it6681_read_edid_block(0, &it6681_edid_buf[0]))
    {
        block_count = it6681_edid_buf[126];
        if (block_count > 0)
        {
            if (block_count > (IT6681_EDID_MAX_BLOCKS-1))
            {
                block_count = (IT6681_EDID_MAX_BLOCKS-1);
            }

            for (block = 1; block <= block_count ; block++)
            {
                if (-1 == it6681_read_edid_block(block, &it6681_edid_buf[128 * block]))
                {
                    ret = -1;
                    break;
                }
            }
        }
    }
    else
    {
        if ( gDrv->enable_internal_edid )
        {
            it6681_init_internal_edid();
            ret = 0;
        }        
        else
        {
            ret = -1;
        }
       
    }

    return ret;
}

void it6681_copy_edid( void )
{
    #if _ITE_668X_DEMO_BOARD
        it6681_copy_edid_ite_demo_board();
    #else
        it6681_fw_read_edid();
    #endif
}

////////////////////////////////////////////////////////////////////
// void setup_mhltxafe( void )
//
// Setup AFE speed based on HCLK,
//
// VCLK = PCLK*PixRpt*colordepth/8
//
// If Packet pixel mode     HCLK = VCLK*2;
//
// If not packet pixel mode HCLK = VCLK*3;
//
//
////////////////////////////////////////////////////////////////////

static void setup_mhltxafe(struct it6681_dev_data *it6681, unsigned long HCLK)
{
    int EnExtRST = 0;

    IT6681_DEBUG_PRINTF(("IT6681- setup_mhltxafe HCLK=%lu\n", (unsigned long)HCLK));

    if (HCLK > 80000)
    {
        //hdmitxset(0x62, 0x90, 0x80);
        hdmitxset(0x64, 0x89, 0x80);
        hdmitxset(0x68, 0x50, (EnExtRST << 6) + 0x00);
    }
    else
    {
        //hdmitxset(0x62, 0x90, 0x10);
        hdmitxset(0x64, 0x89, 0x09);
        hdmitxset(0x68, 0x50, (EnExtRST << 6) + 0x10);
    }
    // change for philip monitor: setting gain bit when pclk > 80
    if ( gDrv->RxClock80M )
    {
        hdmitxset(0x62, 0x90, 0x80);
    }
    else
    {
        hdmitxset(0x62, 0x90, 0x10);
    }
    //...........

    if (HCLK > 250000)
    {
        hdmitxset(0x63, 0x3F, 0x2F);
        hdmitxset(0x66, 0x80, 0x80);
        hdmitxset(0x6B, 0x0F, 0x00);
        // hdmirxset(0x0E, 0x10, 0x00); // HCLK not invert at 1080P, packed pixel
    }
    else
    {
        hdmitxset(0x63, 0x3F, 0x23);
        hdmitxset(0x66, 0x80, 0x00);
        hdmitxset(0x6B, 0x0F, 0x03);
        // hdmirxset(0x0E, 0x10, 0x10);// HCLK invert at other video modes
    }

    if (HCLK > 200000)
    {
        hdmirxset(0x0E, 0x10, 0x00);
    }
    else
    {
        hdmirxset(0x0E, 0x10, 0x10);
    }
}

////////////////////////////////////////////////////////////////////
// void fire_afe(unsigned char on)
//
// turn on TMDS output
//
////////////////////////////////////////////////////////////////////
void fire_afe(unsigned char on)
{
    if (on)
    {
        hdmitxset(0x61, 0x30, 0x00); // Enable AFE output
    }
    else
    {

        hdmitxset(0x61, 0x30, 0x30); // PowerDown AFE output
    }

}


////////////////////////////////////////////////////////////////////
// void Mhl_state(MHLState_Type state)
//
//
//
////////////////////////////////////////////////////////////////////
///

static void Mhl_state(struct it6681_dev_data *it6681, MHLState_Type state)
{

    if (it6681->Mhl_state == state)
    {
        // check MHL Cbus link time out counter
        switch(state)
        {
        case MHL_USB_PWRDN:
            break;
        case MHL_Cbusdet:
            it6681->CBusDetCnt++;
            break;
        case MHL_USB:
            break;
        case MHL_1KDetect:
            it6681->Det1KFailCnt++;
            break;
        case MHL_CBUSDiscover:
            it6681->DisvFailCnt++;
            break;
        case MHL_CBUSDisDone:
            break;
        default:
            break;
        }

        return;

    }

    it6681->Mhl_state = state;
    switch(state)
    {
    case MHL_USB_PWRDN:
        HDMITX_MHL_DEBUG_PRINTF(("IT6681dev[DevNum].Mhl_state => MHL_USB_PWRDN \n"));
        /*
        hdmitx_pwrdn();

        #if( _EnCBusU3IDDQ )
        {
        hdmitxset(0x0F, 0x40, 0x00);   // Do NOT power Down GRCLK

        IT6681_DEBUG_INT_PRINTF(("IT6681-Enter IDDQ mode ...\n"));
        hdmitxwr(0xF8, 0xC3);
        hdmitxwr(0xF8, 0xA5);
        hdmitxset(0xE8, 0x60, 0x60);
        IT6681_DEBUG_PRINTF(("IT6681-Make sure the IDDQ mode is enabled %02X... \n",(int) hdmitxrd(0xE8)));
        }
        #endif
         */
    case MHL_USB:
        HDMITX_MHL_DEBUG_PRINTF(("it6681->Mhl_state => MHL_USB \n"));
        mhltxset(0x0F, 0x11, 0x11); // Switch to USB
        mhltxwr(0x08, 0x7f);
        mhltxset(0x0f, 0x14, 0x0);//
        mhltxset( 0x0a, 0x02, 0x02);//disable 1k fail interrupt
        mhltxset( 0x08, 0x80, 0x80);//disable cbus not detect interrupt
        it6681->CBusPathEn=0;
        break;
    case MHL_Cbusdet:
        // mhltxwr(0x08, 0xFF); // Disable MHL CBUS Interrupt
        mhltxwr(0x09, 0xFF);
        mhltxset(0x0F, 0x11, 0x11); // reset Cbus fsm
        mhltxset(0x0F, 0x11, 0x01); //keen in USB mode
        mhltxset(0x0a, 0x02, 0x00);//enable 1k fail interrupt
        mhltxset(0x08, 0x80, 0x00);//enable cbus not detect interrupt
        it6681->CBusDetCnt = 0;
        it6681->Det1KFailCnt = 0;
        it6681->DisvFailCnt = 0;
        it6681->CBusPathEn=0;
        break;
    case MHL_1KDetect:
        HDMITX_MHL_DEBUG_PRINTF(("it6681->Mhl_state => MHL_1KDetect \n"));
        mhltxset(0x0F, 0x10, 0x10); // reset Cbus fsm
        mhltxset(0x0F, 0x10, 0x00); //
        mhltxset(0x0a, 0x02, 0x00);//enable 1k fail interrupt
        mhltxset(0x08, 0x80, 0x00);//enable cbus not detect interrupt
        it6681->CBusDetCnt = 0;
        it6681->Det1KFailCnt = 0;
        it6681->CBusDetCnt = 0;
        it6681->CBusPathEn=0;
        break;
    case MHL_CBUSDiscover:
        // Enable MHL CBUS Interrupt
        mhltxwr(0x08, 0x00); // (_MaskRxPktDoneInt<<2)+_MaskTxPktDoneInt);
        mhltxwr(0x09, 0x00); // (_MaskDDCDoneInt<<6)+(_MaskDDCDoneInt<<4)+(_MaskMSCDoneInt<<2)+_MaskMSCDoneInt);
        hdmitxset(0x09, 0x20, 0x00); // Enable DDC NACK Interrupt
        mhltxset(0x0F, 0x00, 0x00);
        hdmitxset(0x04, 0x0D, 0x01);
        mhltxset(0x0F, 0x11, 0x00); // Switch back to MHL and enable FSM
        mhltxset(0x0a, 0x02, 0x00);//enable 1k fail interrupt
        mhltxset(0x08, 0x80, 0x00);//enable cbus not detect interrupt
        mhltxset(0x08, 0x05, 0x05);//disable RxPktDone & TxPktDone interrupt
        mhltxset(0x09, 0x15, 0x15);//disable DDCReqDone & MSCRpdDon & MSCReqDone interrupt

        break;
    case MHL_CBUSDisDone:
        HDMITX_MHL_DEBUG_PRINTF(("it6681->Mhl_state => MHL_CBUSDisDone \n"));
        break;
    default:
        break;
    }
}

////////////////////////////////////////////////////////////////////
// void hdmitx_SetCSCScale(void)
//
// chage color space based on system variable
//
// IT6681dev[DevNum].InColorMode  : input color mode (RGB444,YUV444,YUV422)
// IT6681dev[DevNum].OutColorMode : output color mode
// IT6681dev[DevNum].DynRange     : 16-235(CEA) or 0-255(VESA)
// IT6681dev[DevNum].YCbCrCoef    :	ITU709(above 720P),ITU601(480P and lower)
//
////////////////////////////////////////////////////////////////////
void hdmitx_SetCSCScale(struct it6681_dev_data *it6681)
{
    unsigned char csc = B_HDMITX_CSC_BYPASS;
    unsigned char i, colorflag;
    unsigned char const *ptCsc = &bCSCMtx_RGB2YUV_ITU709_16_235[0];

    IT6681_DEBUG_PRINTF(("IT6681-hdmitx_SetCSCScale()\n"));

    switch(it6681->InColorMode)
    {
    case F_MODE_YUV444:
        IT6681_DEBUG_PRINTF(("IT6681-Input mode is YUV444 "));

        switch(it6681->OutColorMode)
        {
        case F_MODE_YUV444:
            IT6681_DEBUG_PRINTF(("IT6681-Output mode is YUV444\n"));
            csc = B_HDMITX_CSC_BYPASS;
            break;

        case F_MODE_YUV422:
            IT6681_DEBUG_PRINTF(("IT6681-Output mode is YUV422\n"));

            csc = B_HDMITX_CSC_BYPASS;
            break;
        case F_MODE_RGB444:

            IT6681_DEBUG_PRINTF(("IT6681-Output mode is RGB24\n"));

            csc = B_HDMITX_CSC_YUV2RGB;

            break;
        }
        break;

    case F_MODE_YUV422:
        IT6681_DEBUG_PRINTF(("IT6681-Input mode is YUV422\n"));

        switch(it6681->OutColorMode)
        {
        case F_MODE_YUV444:
            IT6681_DEBUG_PRINTF(("IT6681-Output mode is YUV444\n"));
            csc = B_HDMITX_CSC_BYPASS;
            break;
        case F_MODE_YUV422:
            IT6681_DEBUG_PRINTF(("IT6681-Output mode is YUV422\n"));
            csc = B_HDMITX_CSC_BYPASS;
            break;

        case F_MODE_RGB444:

            IT6681_DEBUG_PRINTF(("IT6681-Output mode is RGB24\n"));

            csc = B_HDMITX_CSC_YUV2RGB;

            break;
        }
        break;

    case F_MODE_RGB444:
        IT6681_DEBUG_PRINTF(("IT6681-Input mode is RGB24\n"));
        switch(it6681->OutColorMode)
        {
        case F_MODE_YUV444:
            IT6681_DEBUG_PRINTF(("IT6681-Output mode is YUV444\n"));
            csc = B_HDMITX_CSC_RGB2YUV;
            break;

        case F_MODE_YUV422:
            IT6681_DEBUG_PRINTF(("IT6681-Output mode is YUV422\n"));
            csc = B_HDMITX_CSC_RGB2YUV;
            break;

        case F_MODE_RGB444:
            IT6681_DEBUG_PRINTF(("IT6681-Output mode is RGB24\n"));
            csc = B_HDMITX_CSC_BYPASS;
            break;
        }
        break;
    }

    colorflag = 0;
    if (it6681->DynRange == DynCEA) // DynCEA(16-235), DynVESA(0-255)
    {
        colorflag |= F_VIDMODE_16_235;
    }
    if (it6681->YCbCrCoef == ITU709) // ITU709, ITU601
    {
        colorflag |= F_VIDMODE_ITU709;
    }

    // set the CSC metrix registers by colorimetry and quantization
    switch(csc)
    {

    case B_HDMITX_CSC_RGB2YUV:

        IT6681_DEBUG_PRINTF(("IT6681-CSC = RGB2YUV %x ", csc));
        switch(colorflag & (F_VIDMODE_ITU709 | F_VIDMODE_16_235))
        {
				case F_VIDMODE_ITU709|F_VIDMODE_16_235:

                IT6681_DEBUG_PRINTF(("IT6681-ITU709 16-235 "));
            ptCsc = bCSCMtx_RGB2YUV_ITU709_16_235;

            break;
				case F_VIDMODE_ITU709|F_VIDMODE_0_255:
					IT6681_DEBUG_PRINTF(("IT6681-ITU709 0-255 "));
            ptCsc = bCSCMtx_RGB2YUV_ITU709_0_255;

            break;
				case F_VIDMODE_ITU601|F_VIDMODE_16_235:
					IT6681_DEBUG_PRINTF(("IT6681-ITU601 16-235 "));
            ptCsc = bCSCMtx_RGB2YUV_ITU601_16_235;

            break;
				case F_VIDMODE_ITU601|F_VIDMODE_0_255:
        default:
            IT6681_DEBUG_PRINTF(("IT6681-ITU709 0-255 "));
            ptCsc = bCSCMtx_RGB2YUV_ITU709_0_255;

            break;
        }

        break;

    case B_HDMITX_CSC_YUV2RGB:

        IT6681_DEBUG_PRINTF(("IT6681-CSC = YUV2RGB %x ", csc));

        switch(colorflag & (F_VIDMODE_ITU709 | F_VIDMODE_16_235))
        {
        case F_VIDMODE_ITU709 | F_VIDMODE_16_235 : 
            IT6681_DEBUG_PRINTF(("IT6681-ITU709 16-235 "));
            ptCsc = bCSCMtx_YUV2RGB_ITU709_16_235;

            break;
				case F_VIDMODE_ITU709|F_VIDMODE_0_255:
					IT6681_DEBUG_PRINTF(("it6681-ITU709 0-255 "));
            ptCsc = bCSCMtx_YUV2RGB_ITU709_0_255;

            break;
				case F_VIDMODE_ITU601|F_VIDMODE_16_235:
					IT6681_DEBUG_PRINTF(("IT6681-ITU601 16-235 "));
            ptCsc = &bCSCMtx_YUV2RGB_ITU601_16_235[0];

            break;
				case F_VIDMODE_ITU601|F_VIDMODE_0_255:
        default:
            IT6681_DEBUG_PRINTF(("IT6681-ITU709 16-235 "));
            ptCsc = &bCSCMtx_YUV2RGB_ITU709_16_235[0];
            break;
        }

        break;

    default:
        // csc = B_HDMITX_CSC_BYPASS;
        break;
    }

    IT6681_DEBUG_PRINTF(("csc = %d \n", csc));
    if (csc != B_HDMITX_CSC_BYPASS)
    {
        for (i = 0; i < SIZEOF_CSCMTX; i++)
        {

            hdmitxwr(0x73 + i, ptCsc[i]);
            IT6681_DEBUG_PRINTF(("IT6681-reg%02X <- %02X\n", (int)(i + 0x73), (int)ptCsc[i]));
        }
    }

    if (csc == B_HDMITX_CSC_BYPASS)
    {
        hdmitxset(0xF, 0x10, 0x10);
    }
    else
    {
        hdmitxset(0xF, 0x10, 0x00);
    }

    // ucData = hdmitxrd(0x72) &0xF0; //clear CSC setting
    // ucData |= csc ;

#if(_EnColorClip)
    if (it6681->DynRange == DynCEA) // DynCEA Enable EnColorClip
        csc |= 0x08;
#endif

    hdmitxset(0x72, 0x0F, csc);
    hdmitxset(0x88, 0xF0, 0x00);

}

////////////////////////////////////////////////////////////////////
// void set_vid_fmt( void )
//
//
//
//
////////////////////////////////////////////////////////////////////

// int EnColorClip = FALSE;
// int EnBTAFmt = FALSE;

void set_vid_fmt(struct it6681_dev_data *it6681)
{

    // int i;
    // unsigned char chksum;
    BYTE cscsel, fmtsel;
    BYTE ppmode;
    // int ppmode;
    unsigned long Pclk;
    long VCLK;
    long HCLK;
    BYTE regE4a, regE4b;
    BYTE EnColorClip;
    BYTE EnBTAFmt = 0;

    //
    // NOTE: Packet pixel mode only YUV422
    //
    Pclk = cal_pclk();

    VCLK = Pclk * it6681->PixRpt * 1;
    ppmode = FALSE;
    if (it6681->EnPackPix == 2)
    { // FORCE packet pixel mode
        IT6681_DEBUG_PRINTF(("FORCE packed pixel mode\n"));
        it6681->OutColorMode = YCbCr422;
        ppmode = TRUE;
    }
    else
    {
        if (it6681->EnPackPix == 1)
        { // AUTO packet pixel mode
            if (Pclk > 80000L)
            {
                it6681->OutColorMode = YCbCr422;
                ppmode = TRUE;
            }
        }
    }

    if (0x10 & hdmirxrd(0x06))
    {
        it6681->OutColorMode = YCbCr422;
        ppmode = TRUE;
    }

__CSC_AGAIN:
    {
        BYTE RxReg06=0;
        int count=0;
        
        while ( 0 == (RxReg06 & 0x04) )
        {
            RxReg06 = hdmirxrd(0x06);
            IT6681_DEBUG_INT_PRINTF(("waiting for mode change to HDMI.\n"));
            delay1ms(30);
            count++;
            if( count > 100 ) break;
        }
        
    }
    regE4a = hdmitxrd(0xE4);
    IT6681_DEBUG_PRINTF(("0xE4 = 0x%02x\n", (int)regE4a));
    it6681->InColorMode = (regE4a & 0xC0) >> 6;
    if (ppmode == FALSE)
    {
        it6681->OutColorMode = it6681->InColorMode;        
    }

    // config video path:
    if ( ppmode ) // is packed pixel mode
    {
        SetMHLTXPath();
    }
    else
    {
        if (it6681->InColorMode != it6681->OutColorMode) // need CSC
        {
            SetMHLTXPath();
        }
        else 
        {
            if (it6681->HDCPEnable) // enable HDCP
            {
                SetMHLTXPath();
            }
            else // otherwise, use auto TX src
            {
                hdmitxset(0xe5, 0x80, 0x80);
            }
        }
    }



    hdmitxset(0xE0, 0xC0, (it6681->OutColorMode) << 6);

    hdmitxset(0xC0, 0x01, 0x01); // force hdmi mode

    hdmitxset(0xc1, 0x70, 0); // IT6681 24bit color mode only

    // CSC
    if (it6681->InColorMode == RGB444 && (it6681->OutColorMode == YCbCr422 || it6681->OutColorMode == YCbCr444))
    {
        cscsel = RGB2YUV;
        hdmitxset(0x0F, 0x10, 0x00); // Enable QCLK
    }
    else if ((it6681->InColorMode == YCbCr422 || it6681->InColorMode == YCbCr444) && it6681->OutColorMode == RGB444)
    {
        cscsel = YUV2RGB;
        hdmitxset(0x0F, 0x10, 0x00); // Enable QCLK
    }
    else
    {
        cscsel = NOCSC;
        hdmitxset(0x0F, 0x10, 0x10); // Disable QCLK
    }

    if (it6681->YCbCrCoef == ITU601 && it6681->DynRange == DynCEA)
        fmtsel = 0;
    else if (it6681->YCbCrCoef == ITU601 && it6681->DynRange == DynVESA)
        fmtsel = 1;
    else if (it6681->YCbCrCoef == ITU709 && it6681->DynRange == DynCEA)
        fmtsel = 2;
    else // YCbCrCoef==ITU709 && DynRange==DynVESA )
        fmtsel = 3;

    hdmitx_SetCSCScale(it6681);

    EnColorClip = it6681->DynRange;
    hdmitxset(0x72, 0x0F, (EnColorClip << 3) + (EnBTAFmt << 2) + cscsel);
    hdmitxwr(0x75, 0x10);
    hdmitxset(0x88, 0xF0, 0x00);

    // hdmitx_set_avi_infoframe(it6681);  //use default infoframe table.

    // set VSDB infoframe
    // hdmitx_set_vsdb_infoframe();

    if (ppmode == TRUE)
    {
        HCLK = VCLK * 2;
        mhltxset(0x0F, 0x08, 0x08);
        mhltxset(0x0C, 0x06, 0x02); // Trigger Link mode packet
        IT6681_DEBUG_PRINTF(("IT6681- !!!! PACKET PIXEL MODE .............\n"));
        //SetMHLTXPath();

        hdmitxwr(0x63, 0x27);
        hdmitxwr(0x64, 0x80);
        // mhltxset(0x0f, 0x08, 0x08);

    }
    else
    {
        HCLK = VCLK * 3;
        mhltxset(0x0F, 0x08, 0x00);
        mhltxset(0x0C, 0x06, 0x02); // Trigger Link mode packet
        IT6681_DEBUG_PRINTF(("IT6681- !!!! NORMAL PIXEL MODE .............\n"));
        hdmitxwr(0x63, 0x1B);
        hdmitxset(0x64, 0xe0, 0x00);
        // mhltxset(0x0f, 0x08, 0x00);

    }

    regE4b = hdmitxrd(0xE4);
    IT6681_DEBUG_PRINTF(("0xE4 = 0x%02x\n", (int)regE4b));

    if (regE4a != regE4b)
    {
        goto __CSC_AGAIN;
    }
    // path read back
    IT6681_DEBUG_PRINTF(("###############################################\n"));
    IT6681_DEBUG_PRINTF(("# SetMHLTXPath                                #\n"));
    IT6681_DEBUG_PRINTF(("# MHLData Path Read Back  = %d\n", (int)((hdmitxrd(0xE6) & 0x18) >> 3)));
    IT6681_DEBUG_PRINTF(("# Register 0xE0 = %02x \n", (int)hdmitxrd(0xe0)));
    IT6681_DEBUG_PRINTF(("###############################################\n"));

    setup_mhltxafe(it6681, HCLK);

}

////////////////////////////////////////////////////////////////////
// int Hdmi_Video_state(HDMI_Video_state state)
//
//
//
////////////////////////////////////////////////////////////////////
static int Hdmi_Video_state(struct it6681_dev_data *it6681, HDMI_Video_state state)
{
    if (it6681->Hdmi_video_state != state)
    {
        // HPD and RXSen
        if ((hdmitxrd(0x0E) & 0x60) != 0x60)
        {
            state = HDMI_Video_REST;
        }

        it6681->Hdmi_video_state = state;

        if (state != HDMI_Video_ON)
        {
#if _SUPPORT_HDCP_
            if (it6681->HDCPEnable)
            {
                Hdmi_HDCP_state(it6681, HDCP_Off);
            }
#endif

            // aud_chg(it6681,0);
        }

        switch(state)
        {

        case HDMI_Video_REST:
            IT6681_DEBUG_PRINTF(("Hdmi_Video_state ==> HDMI_Video_REST\n"));
            hdmitxset(0x61, 0x30, 0x30);
            hdmitxset(0x04, 0x08, 0x08); // Video Clock Domain Reset

            hdmirxset(0x08, 0x01, 0x01);

            hdmitxset(0x09, 0x08, 0x00); // Enable Video UnStable Interrupt
            hdmitxset(0x09, 0x40, 0x00); // Enable Video Stable Interrupt
            // hdmitxset(0xEC, 0x04, 0x04);  // Enable Video Input FIFO auto-reset Interrupt
            // hdmitxset(0xEC, 0x84, 0x84);  // Enable Video In/Out FIFO Interrupt
            hdmitxset(0xC1, 0x01, 0x01); // Set AVMute
            break;
        case HDMI_Video_WAIT:

            IT6681_DEBUG_PRINTF(("Hdmi_Video_state ==> HDMI_Video_WAIT\n"));

            hdmirxset(0x08, 0x01, 0x00);

            hdmitxset(0x04, 0x08, 0x08); // Video Clock Domain Reset
            hdmitxset(0x09, 0x08, 0x00); // Enable Video UnStable Interrupt
            hdmitxset(0x09, 0x40, 0x00); // Enable Video Stable Interrupt
            // hdmitxset(0xEC, 0x04, 0x00);  // Enable Video Input FIFO auto-reset Interrupt
            // hdmitxset(0xEC, 0x84, 0x84);  // Enable Video In/Out FIFO Interrupt

            hdmitxset(0xC1, 0x01, 0x01); // Set AVMute
            hdmitxset(0x04, 0x08, 0x00); // Release Video Clock Domain Reset

            break;
        case HDMI_Video_ON:
            IT6681_DEBUG_PRINTF(("Hdmi_Video_state ==> HDMI_Video_ON\n"));
            //fire_afe(TRUE);
            set_vid_fmt(it6681);
            // setup_mhltxafe(it6681,cal_pclk());  // setup TX AFE here if PCLK is unknown
            // setup_mhltxafe(74250L);         //720P 74.25M

            // hdmitxwr(0xEE, 0x84);           // Clear Video In/Out FIFO Interrupt
            // hdmitxset(0xEC, 0x84, 0x00);    // Enable Video In/Out FIFO Interrupt
            // hdmitxset(0xC6, 0x03, 0x03);    // Enable General Control Packet

            mhltxset(0x0F, 0x40, 0x40);
            delay1ms(100);
            mhltxset(0x0F, 0x40, 0x00);

            fire_afe(TRUE);

            {
                BYTE RxReg06 = hdmirxrd(0x06);
                BYTE RDetCLKStb = (RxReg06 & 0x40) >> 6;
                BYTE RxCLK_Valid = (RxReg06 & 0x08) >> 3;

                if ((RDetCLKStb == 0 || RxCLK_Valid == 0))
                {
                    // do reset
                    hdmirxset(0x08, 0x01, 0x01);
                    delay1ms(100);
                    hdmirxset(0x08, 0x01, 0x00);

                    IT6681_DEBUG_INT_PRINTF(("RDetCLKStb=%d, RxCLK_Valid=%d\n", RDetCLKStb, RxCLK_Valid));
                }
            }
            hdmitxset(0x09, 0xA0, 0xA0);  
            hdmitxset(0x0A, 0xC0, 0xC0);

#if(_SUPPORT_HDCP_)
            if (it6681->HDCPEnable)
            {
                Hdmi_HDCP_state(it6681, HDCP_CPStart);
            }
            else
            {
                hdmitxset(0xC1, 0x01, 0x00); // clear AVMute
            }
#else
            hdmitxset(0xC1, 0x01, 0x00); // clear AVMute
#endif
            break;
        default:
            break;

        }

        return 0;
    }
    return -1;

}

#if _SUPPORT_HDCP_

////////////////////////////////////////////////////////////////////
// void hdcprd( void )
//
//
//
////////////////////////////////////////////////////////////////////
static int hdcprd( unsigned char offset, unsigned char bytenum)
{
    int err = 0;

    hdmitxset(0x10, 0x01, 0x01); // Enable PC DDC Mode
    hdmitxwr(0x15, 0x09); // DDC FIFO Clear

    hdmitxwr(0x11, 0x74); // HDCP Address
    hdmitxwr(0x12, (unsigned char)offset); // HDCP Offset
    hdmitxwr(0x13, (unsigned char)bytenum); // Read ByteNum

    if (ddcfire(0x15, 0x00) == FAIL) // HDCP Read Fire
    {
        HDMITX_DEBUG_HDCP_PRINTF(("IT6681-ERROR: DDC HDCP Read Fail !!!\n"));
        err = FAIL;

    }
    // rddata = hdmitxrd(0x17);
    // hdmitxwr(0x15, 0x09);           // DDC FIFO Clear
    hdmitxset(0x10, 0x01, 0x00); // Disable PC DDC Mode

    return err;
}

int hdmitx_hdcp_read_bstatus( struct it6681_dev_data *it6681, unsigned short *bStatus )
{
    int ret;

    if (it6681->ver == 0)
    {
        hdmitxset(0x1B, 0x80, 0x80);
        ret = hdcprd(0x41, 1);
        if (ret >= 0)
        {
            *bStatus = hdmitxrd(0x17) | ((hdmitxrd(0x18))<<8 );
        }
        hdmitxset(0x1B, 0x80, 0x00);
    }
    else
    {
        ret = hdcprd(0x41, 2);
        if (ret >= 0)
        {
            *bStatus = hdmitxrd(0x44) | ((hdmitxrd(0x45))<<8 );
        }
    }

    return ret;
}

static int hdmitx_enhdcp(struct it6681_dev_data *it6681)
{
    unsigned short bstatus;
    int RxHDMIMode;
    int WaitCnt;
    int ret;

    // 20121213this line is move, disable CP_desire after HDCP module resert
    // hdmitxset(0x20, 0x01, 0x00);    // Disable CP_Desired
    // idle(10000);
    // delay1ms(10);

    // Reset HDCP Module
    hdmitxset(0x04, 0x01, 0x01);
    // 20121213 disable CP_desire after HDCP module resert
    hdmitxset(0x20, 0x01, 0x00); // Disable CP_Desired

    // idle(100);
    delay1ms(1);

    hdmitxset(0x04, 0x01, 0x00);

    // set HDCP Option
    hdmitxwr(0xF8, 0xC3);
    hdmitxwr(0xF8, 0xA5);
    hdmitxset(0x20, 0x80, 0x80); // RegEnSiPROM='1'
    hdmitxset(0x37, 0x01, 0x00); // PWR_DOWN='0'
    hdmitxset(0x20, 0x80, 0x00); // RegEnSiPROM='0'
    hdmitxset(0xC4, 0x01, 0x00); // EncDis =0;
    hdmitxset(0x4B, 0x30, 0x30); // (EnRiCombRead<<5)+(EnR0CombRead<<4));
    hdmitxwr(0xF8, 0xFF);

    hdmitxset(0x20, 0x06, 0x00); // (EnSyncDetChk<<2)+(EnHDCP1p1<<1));

    hdmitxset(0x4F, 0x0F, 0x08); // (_EnHDCPAutoMute<<3)+(_EnSyncDet2FailInt<<2)+(_EnRiChk2DoneInt<<1)+_EnAutoReAuth);

    hdmitxwr(0x08, 0x1f); // also clear previous Authentication Done Interrupt Ri/Pj Done Interrupt

    hdmitxset(0x0b, 0x07, 0x00); // Enable Authentication Fail/Done/KSVListChk Interrupt
    hdmitxset(0x4F, 0x0F, 0x03); // Ri/Pj Done Interrupt mask enable=1
    // hdmitxwr(0xEE, 0x30);          // Clear Ri/Pj Done Interrupt

    // hdmitxset(0xEC, 0x30, 0x30);   //EnRiPjInt ,Enable Ri/Pj Done Interrupt

    // hdmitxset(0xED, 0x01, 0x01);   //EnSyncDetChk Disable Sync Detect Fail Interrupt

    hdmitxset(0x1F, 0x01, 0x01); // Enable An Generator
    delay1ms(1);
    hdmitxset(0x1F, 0x01, 0x00); // Stop An Generator

    hdmitxwr(0x28, hdmitxrd(0x30));
    hdmitxwr(0x29, hdmitxrd(0x31));
    hdmitxwr(0x2A, hdmitxrd(0x32));
    hdmitxwr(0x2B, hdmitxrd(0x33));
    hdmitxwr(0x2C, hdmitxrd(0x34));
    hdmitxwr(0x2D, hdmitxrd(0x35));
    hdmitxwr(0x2E, hdmitxrd(0x36));
    hdmitxwr(0x2F, hdmitxrd(0x37));

    // Enable HDCP
    // idle(100000);   // delay 100ms between video on and authentication
    // delay1ms(100);

    hdmitxset(0x20, 0x01, 0x01); // Enable CP_Desired

    WaitCnt = 0;

    for(;;)
    {
        ret = hdmitx_hdcp_read_bstatus( it6681, &bstatus );
        if ( ret < 0 )
        {
            goto hdcp_enable_err_return;
        }
        else
        {
            RxHDMIMode = ((bstatus >> 12) & 0x1);
        }

        HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Enable HDCP Fire %d => Current RX HDMI MODE=%d \n", it6681->HDCPFireCnt, RxHDMIMode));

        if ( RxHDMIMode == 0x01 )
        {
            break;
        }
        else
        {
            HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Waiting for RX HDMI_MODE change to 1 ...\n"));
            delay1ms(10);
        }

        if (WaitCnt++ == 9) // 100ms
        {
            HDMITX_DEBUG_HDCP_PRINTF(("IT6681-\nERROR: RX HDMI_MODE keeps in 0 Time-Out !!!\n\n"));
            // IT6681dev[DevNum].Hdcp_state = HDCP_CPRetry;
            goto hdcp_enable_err_return;
        }
    }

#if _CHECK_REVOCATION_BKSV
    {
        unsigned char BKSV[5];
        // add for HDCP ATC Test
        hdmitxset(0x1B, 0x80, 0x80);
        ret = hdcprd(0x00, 5);
        if (ret < 0)
            goto hdcp_enable_err_return;
    
        // hdmitxbrd(0x17, 5, &BKSV[0]);
        BKSV[0] = hdmitxrd(0x17);
        BKSV[1] = hdmitxrd(0x18);
        BKSV[2] = hdmitxrd(0x19);
        BKSV[3] = hdmitxrd(0x1A);
        BKSV[4] = hdmitxrd(0x1B);
    
        hdmitxset(0x1B, 0x80, 0x00);
    
        HDMITX_DEBUG_HDCP_PRINTF(("IT6681-BKSV = 0x%X%X%X%X%X%X%X%X%X%X \n", (int)(BKSV[4]>>4), (int)(BKSV[4]&0x0F),
                                                   (int)(BKSV[3]>>4), (int)(BKSV[3]&0x0F),
                                                   (int)(BKSV[2]>>4), (int)(BKSV[2]&0x0F),
                                                   (int)(BKSV[1]>>4), (int)(BKSV[1]&0x0F),
                                                   (int)(BKSV[0]>>4), (int)(BKSV[0]&0x0F)));
    
        if (BKSV[0] == 0x23 && BKSV[1] == 0xDE && BKSV[2] == 0x5C && BKSV[3] == 0x43 && BKSV[4] == 0x93)
        {
            HDMITX_DEBUG_HDCP_PRINTF(("IT6681-The BKSV is in revocation list for ATC test !!!\n"));
            HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Abort HDCP Authentication !!!\n"));
            goto hdcp_enable_err_return;
        }
    }
#endif

    // Clear Previous HDCP Done/Fail Interrupt when EnRiChk2DoneInt=TRUE
    hdmitxwr(0x08, 0x03);

    // HDCP Fire
    hdmitxset(0x21, 0x01, 0x01); 

    return 0;

hdcp_enable_err_return:
    return -1;
}

static int Hdmi_HDCP_state(struct it6681_dev_data *it6681, HDCPSts_Type state)
{
    // increase counter
    if (it6681->HDCPEnable == FALSE)
    {
        state = HDCP_Off;
    }

    switch(state)
    {
    case HDCP_Off:
        break;
    case HDCP_CPStart:
        it6681->HDCPFireCnt++;
        break;
    case HDCP_CPGoing:
        break;
    case HDCP_CPDone:
        break;
    case HDCP_CPFail:
        break;
    default:
        break;
    }

    if (it6681->Hdcp_state != state)
    {

        it6681->Hdcp_state = state;
        switch(state)
        {

        case HDCP_Off:
            HDMITX_DEBUG_HDCP_PRINTF(("it6681->Hdcp_state -->HDCP_Off \n"));
            hdmitxset(0x20, 0x01, 0x00); // Disable CP_Desired
            hdmitxset(0x04, 0x01, 0x01);
            hdmitxset(0x0B, 0x3f, 0x3f); // disable Authentication Fail/Done/KSVListChk Interrupt
            hdmitxwr(0x08, 0x18); // Clear Ri/Pj Done Interrupt
            hdmitxwr(0x08, 0x03); // also clear previous Authentication Done Interrupt
            hdmitxset(0x0B, 0x02, 0x00);
            hdmitxset(0x0B, 0x18, 0x18);
            // hdmitxset(0xEC, 0x30, 0x30);   //EnRiPjInt ,Enable Ri/Pj Done Interrupt
            // hdmitxset(0xED, 0x01, 0x01);   //EnSyncDetChk Disable Sync Detect Fail Interrupt
            it6681->HDCPFireCnt = 0;
            break;

        case HDCP_CPStart:
            HDMITX_DEBUG_HDCP_PRINTF(("it6681->Hdcp_state -->HDCP_CPStart \n"));
            break;
        case HDCP_CPGoing:
            HDMITX_DEBUG_HDCP_PRINTF(("it6681->Hdcp_state -->HDCP_CPGoing \n"));
            break;
        case HDCP_CPDone:
            hdmitxset(0x0B, 0x02, 0x02);
            hdmitxset(0x0B, 0x18, 0x08); //HDCP Ri read done, disable. Default here is 0x00
            hdmitxset(0xC1, 0x01, 0x00); // clear AVMute
            HDMITX_DEBUG_HDCP_PRINTF(("it6681->Hdcp_state -->HDCP_CPDone \n"));
            it6681->HDCPFireCnt = 0;
            break;
        case HDCP_CPFail:
            hdmitxset(0x20, 0x01, 0x00); // Disable CPDesired
            hdmitxset(0xC2, 0x40, 0x40); // Black Screen
            break;
        default:
            break;
        }
    }

    return 0;

}

int Hdmi_HDCP_handler(struct it6681_dev_data *it6681)
{
    int ret;
    switch(it6681->Hdcp_state)
    {

    case HDCP_Off:
        break;
    case HDCP_CPStart:
        ret = hdmitx_enhdcp(it6681);
        if (ret < 0)
            Hdmi_HDCP_state(it6681, HDCP_CPStart);
        else
            Hdmi_HDCP_state(it6681, HDCP_CPGoing);
        break;
    case HDCP_CPGoing:
        break;
    case HDCP_CPDone:
        break;
    case HDCP_CPFail:

    default:
        break;
    }
    return 0;

}

#if _SHOW_HDCP_INFO_
////////////////////////////////////////////////////////////////////
// void hdcpsts( void )
//
//
//
////////////////////////////////////////////////////////////////////

static void hdcpsts(void)
{

    unsigned char An1, An2, An3, An4, An5, An6, An7, An8;
    unsigned char AKSV1, AKSV2, AKSV3, AKSV4, AKSV5;
    unsigned char BKSV1, BKSV2, BKSV3, BKSV4, BKSV5;
    unsigned char ARi1, ARi2, BRi1, BRi2;
    unsigned char BCaps, BStatus1, BStatus2;
    unsigned char AuthStatus;

    BKSV1 = hdmitxrd(0x3B);
    BKSV2 = hdmitxrd(0x3C);
    BKSV3 = hdmitxrd(0x3D);
    BKSV4 = hdmitxrd(0x3E);
    BKSV5 = hdmitxrd(0x3F);

    BRi1 = hdmitxrd(0x40);
    BRi2 = hdmitxrd(0x41);

    AKSV1 = hdmitxrd(0x23);
    AKSV2 = hdmitxrd(0x24);
    AKSV3 = hdmitxrd(0x25);
    AKSV4 = hdmitxrd(0x26);
    AKSV5 = hdmitxrd(0x27);

    An1 = hdmitxrd(0x28);
    An2 = hdmitxrd(0x29);
    An3 = hdmitxrd(0x2A);
    An4 = hdmitxrd(0x2B);
    An5 = hdmitxrd(0x2C);
    An6 = hdmitxrd(0x2D);
    An7 = hdmitxrd(0x2E);
    An8 = hdmitxrd(0x2F);

    ARi1 = hdmitxrd(0x38);
    ARi2 = hdmitxrd(0x39);

    AuthStatus = hdmitxrd(0x46);

    BCaps = hdmitxrd(0x43);

    BStatus1 = hdmitxrd(0x44);

    BStatus2 = hdmitxrd(0x45);

    HDMITX_DEBUG_HDCP_PRINTF(("IT6681-An   = 0x%X%X%X%X%X%X%X%X%X%X%X%X%X%X%X%X \n",(int)An8>>4, (int)An8&0x0F,
                                                           (int)An7>>4, (int)An7&0x0F,
                                                           (int)An6>>4, (int)An6&0x0F,
                                                           (int)An5>>4, (int)An5&0x0F,
                                                           (int)An4>>4, (int)An4&0x0F,
                                                           (int)An3>>4, (int)An3&0x0F,
                                                           (int)An2>>4, (int)An2&0x0F,
                                                           (int)An1>>4, (int)An1&0x0F));

    HDMITX_DEBUG_HDCP_PRINTF(("IT6681-AKSV = 0x%X%X%X%X%X%X%X%X%X%X \n", (int)AKSV5>>4, (int)AKSV5&0x0F,
                                               (int)AKSV4>>4, (int)AKSV4&0x0F,
                                               (int)AKSV3>>4, (int)AKSV3&0x0F,
                                               (int)AKSV2>>4, (int)AKSV2&0x0F,
                                               (int)AKSV1>>4, (int)AKSV1&0x0F));
    HDMITX_DEBUG_HDCP_PRINTF(("IT6681-BKSV = 0x%X%X%X%X%X%X%X%X%X%X \n", (int)BKSV5>>4, (int)BKSV5&0x0F,
                                               (int)BKSV4>>4, (int)BKSV4&0x0F,
                                               (int)BKSV3>>4, (int)BKSV3&0x0F,
                                               (int)BKSV2>>4, (int)BKSV2&0x0F,
                                               (int)BKSV1>>4, (int)BKSV1&0x0F));

    HDMITX_DEBUG_HDCP_PRINTF(("IT6681-AR0 = 0x%X%X%X%X \n", (int)ARi2 >> 4, (int)ARi2 & 0x0F, (int)ARi1 >> 4, (int)ARi1 & 0x0F));
    HDMITX_DEBUG_HDCP_PRINTF(("IT6681-BR0 = 0x%X%X%X%X \n", (int)BRi2 >> 4, (int)BRi2 & 0x0F, (int)BRi1 >> 4, (int)BRi1 & 0x0F));

    HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Rx HDCP Fast Reauthentication = %d \n", BCaps & 0x01));
    HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Rx HDCP 1.1 Features = %d ", (int)(BCaps & 0x02) >> 1));

    if ((BCaps & 0x02) /* && EnHDCP1p1 */ )

        HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Enabled\n"));
    else
        HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Disabled\n"));

    if (BCaps & 0x10)
        HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Rx HDCP Maximum DDC Speed = 400KHz\n"));
    else
        HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Rx HDCP Maximum DDC Speed = 100KHz\n"));

    HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Rx HDCP Repeater = %d \n", (int)(BCaps & 0x40) >> 6));

    HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Tx Authentication Status = 0x%X \n", (int)AuthStatus));

    if ((AuthStatus & 0x80) != 0x80)
    {
        if (AuthStatus & 0x01)
            HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Auth Fail: DDC NAck too may times !!!\n"));

        if (AuthStatus & 0x02)
            HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Auth Fail: BKSV Invalid !!!\n"));

        if (AuthStatus & 0x04)
            HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Auth Fail: Link Check Fail (AR0/=BR0) !!!\n"));

        if (AuthStatus & 0x08)
            HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Auth Fail: Link Integrity Ri Check Fail !!!\n"));

        if (AuthStatus & 0x10)
            HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Auth Fail: Link Integrity Pj Check Fail !!!\n"));

        if (AuthStatus & 0x20)
            HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Auth Fail: Link Integrity Sync Detect Fail !!!\n"));

        if (AuthStatus & 0x40)
            HDMITX_DEBUG_HDCP_PRINTF(("IT6681-Auth Fail: DDC Bus Hang TimeOut !!!\n"));
    }

    HDMITX_DEBUG_HDCP_PRINTF(("\n\n"));

}

#endif

////////////////////////////////////////////////////////////////////
// void hdmitx_int_HDCP_AuthFail( void )
//
//
//
////////////////////////////////////////////////////////////////////
static void hdmitx_int_HDCP_AuthFail(struct it6681_dev_data *it6681)
{

#if(_SHOW_HDCP_INFO_)
    /*
    unsigned char rddata;
    unsigned char ARi1, ARi2, BRi1, BRi2, preARi1, preARi2,preBRi1, preBRi2;
    hdcpsts();


    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681-\nAuthentication Fail Status:\n"));

    rddata = hdmitxrd(0x46);

    if( rddata&0x01 )
    {
    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681- => DDC NACK Timeout !!!\n"));
    }
    if( rddata&0x02 )
    {
    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681- => Invalid BKSV !!!\n"));
    }
    if( rddata&0x04 )
    {
    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681- => R0 Link Integrity Check Error !!!\n"));
    }
    if( rddata&0x08 )
    {
    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681- => Ri Link Integrity Check Error !!!\n"));
    }
    if( rddata&0x10 )
    {
    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681- => Pj Link Integrity Check Error !!!\n"));
    }
    if( rddata&0x20 )
    {
    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681- => HDCP 1.2 Sync Detect Fail !!!\n"));
    }
    if( rddata&0x40 )
    {
    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681- => DDC Bus Hang Timeout !!!\n"));
    }
    if( rddata&0x20 )
    {
    ARi1 = hdmitxrd(0x38);
    ARi2 = hdmitxrd(0x39);
    BRi1 = hdmitxrd(0x40);
    BRi2 = hdmitxrd(0x41);

    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681-Current ARi = 0x%X%X%X%X \n", (int)ARi2>>4, (int)ARi2&0x0F, (int)ARi1>>4, (int)ARi1&0x0F));
    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681-Current BRi = 0x%X%X%X%X \n", (int)BRi2>>4, (int)BRi2&0x0F, (int)BRi1>>4, (int)BRi1&0x0F));

    hdmitxset(0x50, 0x08, 0x00);
    preARi1 = hdmitxrd(0x56);
    preARi2 = hdmitxrd(0x57);
    hdmitxset(0x50, 0x08, 0x08);
    preBRi1 = hdmitxrd(0x56);
    preBRi2 = hdmitxrd(0x57);
    hdmitxset(0x50, 0x08, 0x00);

    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681-Previous ARi = 0x%X%X%X%X \n", (int)preARi2>>4, (int)preARi2&0x0F, (int)preARi1>>4, (int)preARi1&0x0F));
    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681-Previous BRi = 0x%X%X%X%X \n", (int)preBRi2>>4, (int)preBRi2&0x0F, (int)preBRi1>>4, (int)preBRi1&0x0F));
    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681-\n"));

    if( BRi1==preARi1 && BRi2==preARi2 )
    {

    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681-CurBRi==PreARi ==> Sync Detection Error because of Miss CTLx signal\n"));
    }
    if( ARi1==preBRi1 && ARi2==preBRi2 )
    {
    HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681-CurARi==PreBRi ==> Sync Detection Error because of Extra CTLx signal\n"));
    }
    } */
#endif
}

#endif

////////////////////////////////////////////////////////////////////
// void mhl_cbus_rediscover( void )
//
//
//
////////////////////////////////////////////////////////////////////
/* static void mhl_cbus_rediscover(void)
{
HDMITX_MHL_DEBUG_PRINTF(("IT6681- mhl_cbus_rediscover() Start ...\n"));
mhltxset(0x0F, 0x10, 0x10);
mhltxwr(0x08, 0xFF);
mhltxwr(0x09, 0xFF);
mhltxset(0x0F, 0x10, 0x00);
} */



////////////////////////////////////////////////////////////////////
// void read_devcap_hw( void )
//
//
//
////////////////////////////////////////////////////////////////////
static int read_devcap_hw(struct it6681_dev_data *it6681)
{
    unsigned char offset;

    HDMITX_MHL_DEBUG_PRINTF(("IT6681-\nRead Device Capability Start ...\n"));

    // take care of this line ,if RxPOW is "1" turn off VBUSout
    // if _EnVBUSOut is turn on, this code is necessary for turn off Vbus
    // duto some dongle device did not responce RxPOW correctly
    // this code is marked

    mhltxwr(0x54, 0x02);

    if (mscfire(0x50, 0x40) == FAIL)
    {
        gDrv->RxPOW = 0;    
        return -1;
    }
    else
    {    
        gDrv->RxPOW = (mhltxrd(0x56)&0x10)>>4;
    }

#if(_ForceVBUSOut)
    mhltxset(0x0F, 0x04, 0); // For dongle device, some dongle does not report RxPOW correctly
    if(gDrv->IT6682_MCU2VBUSOUT) set_vbus_output(1);
#else
	mhltxset(0x0F, 0x04, gDrv->RxPOW <<2);
    if(gDrv->IT6682_MCU2VBUSOUT) set_vbus_output((gDrv->RxPOW==0));
#endif

    HDMITX_MHL_DEBUG_PRINTF(("Current RxPOW = %d \n", (int)gDrv->RxPOW));

    for (offset = 0; offset < 0x10; offset++)
    {

        mhltxwr(0x54, offset);

        if (mscfire(0x50, 0x40) == SUCCESS)
            it6681->Mhl_devcap[offset] = mhltxrd(0x56);
        else
            return -1;

        HDMITX_MHL_DEBUG_PRINTF(("IT6681-DevCap[%X]=%X\n", (int)offset, (int)it6681->Mhl_devcap[offset]));
    }

    HDMITX_MHL_DEBUG_PRINTF(("IT6681-Read Device Capability End...\n"));

    mhltxwr(0x5E, it6681->Mhl_devcap[3]);
    mhltxwr(0x5E, it6681->Mhl_devcap[4]);

    // it6681->RxMHLVer = rddata[1];

    // parse_devcap(&rddata[0]);

    // read_edid();
    //it6681_copy_edid();
    //hdmirx_hpd_high();

    return 0;

}



////////////////////////////////////////////////////////////////////
// void hdmitx_irq( void )
//
//
//
////////////////////////////////////////////////////////////////////

void hdmitx_irq(struct it6681_dev_data *it6681)
{
    unsigned char RegF0;
    unsigned char Reg06, Reg07, Reg08;
    unsigned char MHL04, MHL05, MHL06, MHL10;
    unsigned char MHLA0, MHLA1, MHLA2, MHLA3;
    unsigned char rddata;

    RegF0 = hdmitxrd(0xF0);

    if (RegF0 & 0x10)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-Detect U3 Wakeup Interrupt ...\n"));

        hdmitxset(0xE8, 0x60, 0x00);

        if(gDrv->IT6682_MCU2Switch) set_operation_mode(MODE_MHL); // switch to MHL
        Mhl_state(it6681, MHL_Cbusdet);
    }

    if ((RegF0 & 0x0F) == 0x00)
    {
        // no interrupt actived
        goto exit_hdmitx_irq;
    }
    else
    {
        Reg06 = 0x00;
        Reg07 = 0x00;
        Reg08 = 0x00;
        MHL04 = 0x00;
        MHL05 = 0x00;
        MHL06 = 0x00;
        MHL10 = 0x00;
        MHLA0 = 0x00;
        MHLA1 = 0x00;
        MHLA2 = 0x00;
        MHLA3 = 0x00;
    }

    // HDMI TX Interrupt
    if (RegF0 & 0x01)
    {
        Reg06 = hdmitxrd(0x06);
        Reg07 = hdmitxrd(0x07);
        Reg08 = hdmitxrd(0x08);

        // IT6681_DEBUG_INT_PRINTF(("IT6681-Reg06=0x%02X 0x%02X 0x%02X\n", (int)Reg06, (int)Reg07, (int) Reg08));

        // W1C all int staus
        hdmitxwr(0x06, Reg06);
        hdmitxwr(0x07, Reg07);
        hdmitxwr(0x08, Reg08);
    }

    // CBUS Interrupt
    if (RegF0 & 0x04)
    {
        MHL04 = mhltxrd(0x04);
        MHL05 = mhltxrd(0x05);
        MHL06 = mhltxrd(0x06);

        mhltxwr(0x04, MHL04);
        mhltxwr(0x05, MHL05);
        mhltxwr(0x06, MHL06);
    }

    // SET_INT interrupt
    if (RegF0 & 0x08)
    {
        MHLA0 = mhltxrd(0xA0);
        MHLA1 = mhltxrd(0xA1);
        MHLA2 = mhltxrd(0xA2);
        MHLA3 = mhltxrd(0xA3);

        mhltxwr(0xA0, MHLA0);
        mhltxwr(0xA1, MHLA1);
        mhltxwr(0xA2, MHLA2);
        mhltxwr(0xA3, MHLA3);
    }

    if (Reg07 & 0x01)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-Int RecVidParaChg\n"));
    }

    if (Reg07 & 0x02)
    {
        BYTE hpd;

        rddata = hdmitxrd(0x0E);
        hpd = ((rddata & 0x40) >> 6);
        IT6681_DEBUG_INT_PRINTF(("IT6681-HPD Change Interrupt HPD ===> %d\n", (int)hpd));

        Hdmi_Video_state(it6681, HDMI_Video_WAIT);
        if (hpd == 0)
        {
            hdmirx_hpd_low();
            gDrv->TXHPD = 0;
        }
        else
        {
            gDrv->TXHPD = 1;
        }
    }

    if (Reg07 & 0x04)
    {
        BYTE rxSen;

        rddata = hdmitxrd(0x0E);
        rxSen = ((rddata & 0x20) >> 5);
        IT6681_DEBUG_INT_PRINTF(("IT6681-RxSen Change Interrupt => RxSen = %d\n", (int)rxSen));

        Hdmi_Video_state(it6681, HDMI_Video_WAIT);
        if (rxSen == 0)
        {
            if ( gDrv->TXHPD == 0 )
            {
                if(gDrv->IT6682_MCU2VBUSOUT) set_vbus_output(0); // not output VBUS
            }
            //hdmirx_hpd_low();
        }
    }

    if (Reg07 & 0x10)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-DDC FIFO Error Interrupt ...\n"));
        hdmitxset(0x10, 0x01, 0x01);
        hdmitxwr(0x15, 0x09);
        hdmitxset(0x10, 0x01, 0x00);
    }

    if (Reg07 & 0x20)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-DDC NACK Interrupt ...\n"));
#if(_SUPPORT_HDCP_)
        if (it6681->HDCPEnable)
        {
            Hdmi_HDCP_state(it6681, HDCP_Off);
        }
#endif
    }

    if (Reg07 & 0x40)
    {
        // IT6681_DEBUG_INT_PRINTF(("IT6681-Int V2HBufErrStus\n"));
    }

    if (Reg07 & 0x80)
    {
        // IT6681_DEBUG_INT_PRINTF(("IT6681-Int  V2HBufRstStus\n"));
    }

    if (Reg08 & 0x01)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-HDCP Authentication Fail Interrupt ...\n\n"));

#if(_SUPPORT_HDCP_)
        if (it6681->HDCPEnable)
        {
            hdmitx_int_HDCP_AuthFail(it6681);

            if (it6681->HDCPFireCnt > _HDCPFireMax)
            {
                Hdmi_HDCP_state(it6681, HDCP_CPFail);
                HDMITX_DEBUG_HDCP_INT_PRINTF(("IT6681-ERROR: Set Black Screen because of HDCPFireCnt>%d !!!\n", _HDCPFireMax));
            }
            else
            {
                Hdmi_HDCP_state(it6681, HDCP_CPStart);
            }
        }
#endif
    }

    if (Reg08 & 0x02)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-HDCP Authentication Done Interrupt ...\n\n"));
#if(_SUPPORT_HDCP_)
        if (it6681->HDCPEnable)
        {
            Hdmi_HDCP_state(it6681, HDCP_CPDone);
        }
#endif
    }

    if (Reg08 & 0x04)
    {
#if(_SUPPORT_HDCP_REPEATER_)
        IT6681_DEBUG_INT_PRINTF(("IT6681-HDCP KSV List Check Interrupt ... %d\n", (int)it6681->ksvchkcnt));
        hdmitx_int_HDCP_KSVLIST();
#endif
    }

    if (Reg08 & 0x08)
    {
        int ARi1, ARi2, BRi1, BRi2;

        IT6681_DEBUG_INT_PRINTF(("IT6681-Int HDCP Ri Check Done\n"));

        ARi1 = hdmitxrd(0x38);
        ARi2 = hdmitxrd(0x39);
        BRi1 = hdmitxrd(0x40);
        BRi2 = hdmitxrd(0x41);

        IT6681_DEBUG_INT_PRINTF(("ARi = 0x%X%X%X%X \n", ARi2 >> 4, ARi2 & 0x0F, ARi1 >> 4, ARi1 & 0x0F));
        IT6681_DEBUG_INT_PRINTF(("BRi = 0x%X%X%X%X \n", BRi2 >> 4, BRi2 & 0x0F, BRi1 >> 4, BRi1 & 0x0F));
    }

    if (Reg08 & 0x10)
    {
        int APj, BPj;

        IT6681_DEBUG_INT_PRINTF(("IT6681-Int HDCP Pj Check Done\n"));

        APj = hdmitxrd(0x3A);
        BPj = hdmitxrd(0x42);

        IT6681_DEBUG_INT_PRINTF(("APj = 0x%02X \n", APj));
        IT6681_DEBUG_INT_PRINTF(("BPj = 0x%02X \n", BPj));
    }

    if (Reg08 & 0x20)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-HDCP 1.2 Sync Detect Fail Interrupt ...\n"));
#if(_SUPPORT_HDCP_)
        // hdmitx_int_HDCP_V12detFail();
#endif
    }

    if (Reg08 & 0x40)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-Int RXPBUFRstStus\n"));
    }

    if ((Reg06 & 0x08) || (Reg06 & 0x40))
    {
        BYTE Reg0E;
        Reg0E = hdmitxrd(0x0E);

        IT6681_DEBUG_INT_PRINTF(("IT6681-Reg06=0x%02X\n", (int)Reg06));
        IT6681_DEBUG_INT_PRINTF(("IT6681-Current VideoStable(0x0E)=%X\n", (int)Reg0E));

        if ((hdmitxrd(0x0E) & 0x10) == 0x10)
        {
            IT6681_DEBUG_INT_PRINTF(("IT6681-Video Stable On Interrupt ...\n"));
            Hdmi_Video_state(it6681, HDMI_Video_ON);

        }
        else
        {
            Hdmi_Video_state(it6681, HDMI_Video_WAIT);
            IT6681_DEBUG_INT_PRINTF(("IT6681-Video Stable Off Interrupt ...\n"));
        }
    }

    if (MHL04 & 0x01)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-CBUS Link Layer TX Packet Done Interrupt ...\n"));
    }

    if (MHL04 & 0x02)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: CBUS Link Layer TX Packet Fail Interrupt ... \n"));
        IT6681_DEBUG_INT_PRINTF(("IT6681- TX Packet error Status reg15=%X\n", (int)mhltxrd(0x15)));

        rddata = mhltxrd(0x15);
        if (rddata & 0x10)
            IT6681_DEBUG_INT_PRINTF(("IT6681-TX Packet Fail when Retry > 32 times !!!\n"));
        if (rddata & 0x20)
            IT6681_DEBUG_INT_PRINTF(("IT6681-TX Packet TimeOut !!!\n"));
        if (rddata & 0x40)
            IT6681_DEBUG_INT_PRINTF(("IT6681-TX Initiator Arbitration Error !!!\n"));
        if (rddata & 0x80)
            IT6681_DEBUG_INT_PRINTF(("IT6681-TX CBUS Hang !!!\n"));

        mhltxwr(0x15, rddata & 0xF0);

#if(_EnCBusReDisv)
        if (it6681->PKTFailCnt > CBUSFAILMAX)
        {
            IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: CBUS Link Layer Error ==> Retry CBUS Discovery Process !!!\n"));
            Mhl_state(it6681, MHL_Cbusdet);
        }
#endif
    }

    if (MHL04 & 0x04)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-CBUS Link Layer RX Packet Done Interrupt ...\n"));
    }

    if (MHL04 & 0x08)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: CBUS Link Layer RX Packet Fail Interrupt ... \n"));
        IT6681_DEBUG_INT_PRINTF(("IT6681- TX Packet error Status reg15=%X\n", (int)mhltxrd(0x15)));

        rddata = mhltxrd(0x15);

        if (rddata & 0x01)
            IT6681_DEBUG_INT_PRINTF(("IT6681-RX Packet Fail !!!\n"));
        if (rddata & 0x02)
            IT6681_DEBUG_INT_PRINTF(("IT6681-RX Packet TimeOut !!!\n"));
        if (rddata & 0x04)
            IT6681_DEBUG_INT_PRINTF(("IT6681-RX Parity Check Error !!!\n"));
        if (rddata & 0x08)
            IT6681_DEBUG_INT_PRINTF(("IT6681-Bi-Phase Error !!!\n"));

        mhltxwr(0x15, rddata & 0x0F);

#if( _EnCBusReDisv)
        if (it6681->PKTFailCnt > CBUSFAILMAX)
        {
            IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: CBUS Link Layer Error ==> Retry CBUS Discovery Process !!!\n"));

            Mhl_state(it6681, MHL_Cbusdet);
        }
#endif
    }

    if (MHL04 & 0x10)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-MSC RX MSC_MSG Interrupt ...\n"));
        mhl_read_mscmsg(it6681);
    }

    if (MHL04 & 0x20)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-MSC RX WRITE_STAT Interrupt ...\n"));
    }

    if (MHL04 & 0x40)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-MSC RX WRITE_BURST Interrupt  ...\n"));
    }

    if (MHL05 & 0x01)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-MSC Req Done Interrupt ...\n"));
    }

    if (MHL04 & 0x80)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-CBUS Low and VBus5V not Detect Interrupt ... ==> %dth Fail\n", (int)it6681->CBusDetCnt));

        if (it6681->CBusDetCnt > CBUSDETMAX)
        {
            IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: CBUS Low and VBus5V Detect Error ==> Switch to USB Mode !!!\n"));
            if(gDrv->IT6682_MCU2Switch) set_operation_mode(MODE_USB); // switch to USB
            Mhl_state(it6681, MHL_USB);
        }
        else
        {
            Mhl_state(it6681, MHL_Cbusdet);
        }
    }

    if (MHL05 & 0x02)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-MSC Req Fail Interrupt (Unexpected) ...\n"));
        IT6681_DEBUG_INT_PRINTF(("IT6681-MSC Req Fail reg18= %02X \n", (int)mhltxrd(0x18)));

        it668x_debug_parse_mhl_0x18();
        it668x_debug_parse_mhl_0x19();

        rddata = mhltxrd(0x18);
        mhltxwr(0x18, rddata);
        rddata = mhltxrd(0x19);
        mhltxwr(0x19, rddata);

#if( _EnCBusReDisv)
        if (it6681->MSCFailCnt > CBUSFAILMAX)
        {
            IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: CBUS MSC Channel Error ==> Retry CBUS Discovery Process !!!\n"));
            Mhl_state(it6681, MHL_Cbusdet);
        }
#endif
    }

    if (MHL05 & 0x04)
    {
        mhltxwr(0x05, 0x04);
        IT6681_DEBUG_INT_PRINTF(("IT6681-MSC Rpd Done Interrupt ...\n"));
    }

    if (MHL05 & 0x08)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-MSC Rpd Fail Interrupt ...\n"));
        IT6681_DEBUG_INT_PRINTF(("IT6681-MSC Rpd Fail status reg1A=%X reg1B=%X\n", (int)mhltxrd(0x1A), (int)mhltxrd(0x1B)));

        it668x_debug_parse_mhl_0x1A();
        it668x_debug_parse_mhl_0x1B();

        rddata = mhltxrd(0x1A);
        mhltxwr(0x1A, rddata);
        rddata = mhltxrd(0x1B);
        mhltxwr(0x1B, rddata);

#if(_EnCBusReDisv)
        if (it6681->MSCFailCnt > CBUSFAILMAX)
        {
            IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: CBUS MSC Channel Error ==> Retry CBUS Discovery Process !!!\n"));
            Mhl_state(it6681, MHL_Cbusdet);
        }
#endif
    }

    if (MHL05 & 0x10)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-DDC Req Done Interrupt ...\n"));
    }

    if (MHL05 & 0x20)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-DDC Req Fail Interrupt (Hardware) ... \n"));
        IT6681_DEBUG_INT_PRINTF(("IT6681-DDC Req Fail reg16=%X\n", (int)mhltxrd(0x16)));

        it668x_debug_parse_mhl_0x16();

        rddata = mhltxrd(0x16);
        mhltxwr(0x16, rddata);

#if(_EnCBusReDisv)
        if (it6681->DDCFailCnt > CBUSFAILMAX)
        {
            IT6681_DEBUG_INT_PRINTF(("IT6681-ERROR: CBUS DDC Channel Error ==> Retry CBUS Discovery Process !!!\n"));
            Mhl_state(it6681, MHL_Cbusdet);
        }
#endif
    }

    if (MHL05 & 0x40)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-DDC Rpd Done Interrupt ...\n"));
    }

    if (MHL05 & 0x80)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-DDC Rpd Fail Interrupt ...\n"));
    }

    if (MHL06 & 0x01)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-CBUS 1K Detect Done Interrupt ...\n"));
        if(gDrv->IT6682_MCU2Switch) set_operation_mode(MODE_MHL); // switch to MHL
        if(gDrv->IT6682_MCU2VBUSOUT) set_vbus_output(1);
        Mhl_state(it6681, MHL_CBUSDiscover);
    }

    if (MHL06 & 0x02)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-CBUS 1K Detect Fail Interrupt ... ==> %dth Fail\n", (int)it6681->Det1KFailCnt));
        if(gDrv->IT6682_MCU2VBUSOUT) set_vbus_output(0);

        gDrv->KeepRxHPD = 0;
        hdmirx_hpd_low();

        if (it6681->Det1KFailCnt > DISVFAILMAX)
        {
            IT6681_DEBUG_INT_PRINTF(("it6681-ERROR: CBUS 1K Detect Error ==> Switch to USB Mode !!!\n"));
            //it6681_disable_cbus_1k_detection();
            if(gDrv->IT6682_MCU2Switch) set_operation_mode(MODE_USB); // switch to USB
            Mhl_state(it6681, MHL_USB);
        }
        else
        {
            Mhl_state(it6681, MHL_1KDetect);
        }
    }

    if (MHL06 & 0x04)
    {
        IT6681_DEBUG_INT_PRINTF(("it6681-CBUS Discovery Done Interrupt ...\n"));
        Mhl_state(it6681, MHL_CBUSDisDone);
    }

    if (MHL06 & 0x08)
    {
        IT6681_DEBUG_INT_PRINTF(("it6681-CBUS Discovery Fail Interrupt ... ==> %dth Fail\n", (int)it6681->DisvFailCnt));
        if(gDrv->IT6682_MCU2VBUSOUT) set_vbus_output(0);

        if (it6681->DisvFailCnt > DISVFAILMAX)
        {
            IT6681_DEBUG_INT_PRINTF(("it6681-ERROR: CBUS Discovery Error ==> Switch to USB Mode !!!\n"));

            if(gDrv->IT6682_MCU2Switch) set_operation_mode(MODE_USB); // switch to USB
            Mhl_state(it6681, MHL_USB);
        }
        else
        {
            Mhl_state(it6681, MHL_CBUSDiscover);

        }
    }

    if (MHL06 & 0x10)
    {
        BYTE cnt = 0;
        IT6681_DEBUG_INT_PRINTF(("IT6681-CBUS PATH_EN Change Interrupt ...\n"));

#if(_EnHWPathEn == FALSE )
        {
            rddata = (mhltxrd(0xB1) & 0x08) >> 3;
            IT6681_DEBUG_INT_PRINTF(("it6681-Current RX PATH_EN status = %d \n", (int)rddata));
            IT6681_DEBUG_INT_PRINTF(("it6681-Send PATH_EN=%d by Firmware ...\n", (int)rddata));

            if (rddata)
                mhltxset(0x0C, 0x06, 0x02); // 20121213 re-work , change write mask from 0x02 to 0x06
            // set_mhlsts(MHLSts01B, DCAP_RDY);
            else
                mhltxset(0x0C, 0x06, 0x04); // 20121213 re-work , change write mask from 0x04 to 0x06
        }
#endif

        while (!(((MHL10 & 0x10) == 0x10) && ((MHL10 & 0x1) == 0x1)))
        {
            delay1ms(100);
            cnt++;
            if (cnt > 30)
            {
                break;
            }

            MHL10 = mhltxrd(0x10);
        }

        if (((MHL10 & 0x10) == 0x10) && ((MHL10 & 0x1) == 0x1))
        {
            it6681->CBusPathEn = 1;
        }
        else
        {
            it6681->CBusPathEn = 0;
        }
    }

    if (MHL06 & 0x20)
    {
        IT6681_DEBUG_INT_PRINTF(("it6681-CBUS MUTE Change Interrupt ...\n"));
        IT6681_DEBUG_INT_PRINTF(("it6681-Current CBUS MUTE Status = %X \n", (int)(mhltxrd(0xB1) & 0x10) >> 4));
    }

    if (MHL06 & 0x40)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-CBUS DCapRdy Change Interrupt ...\n"));

        if (mhltxrd(0xB0) & 0x01)
        {
            BYTE retry=5;
retry_devcap:
            if ( 0 == read_devcap_hw(it6681) ) // READ_DEVCAP hardware mode
            {
                it6681_copy_edid();    
            }
            else
            {
                retry--;
                if ( retry > 0 )
                {                
                    goto retry_devcap;
                }

            }

            IT6681_DEBUG_INT_PRINTF(("IT6681-Set DCAP_RDY to 1 ...\n"));
            gDrv->TXCanReadDevCap = 1;

            set_mhlsts(MHLSts00B, DCAP_RDY);
             
            hdmirx_hpd_high();
            /*
            //if( En3D==TRUE && RxMHLVer>=0x20 ) {
            //    set_mhlint(MHLInt00B, V3D_REQ);
            //}
             */
        }
        else
        {
            gDrv->TXCanReadDevCap = 0;
            IT6681_DEBUG_INT_PRINTF(("IT6681-DCapRdy Change from '1' to '0'\n"));
        }

    }

    if (MHL06 & 0x80)
    {
        BYTE vbus;

        vbus = (mhltxrd(0x10) & 0x08) >> 3;

        IT6681_DEBUG_INT_PRINTF(("IT6681-VBUS Status Change Interrupt ...\n"));
        IT6681_DEBUG_INT_PRINTF(("IT6681-Current VBUS Status = %X\n", (int)vbus));

        if (vbus == 0)
        {
            hdmirx_hpd_low();
        }
        else
        {
            it6681->Det1KFailCnt = 0;
        }

#if(_AutoSwBack)
        if ((mhltxrd(0x10) & 0x08))
        {
            if (mhltxrd(0x0F) & 0x01)
            {
                if(gDrv->IT6682_MCU2Switch) set_operation_mode(MODE_MHL); // switch to MHL
                Mhl_state(it6681, MHL_Cbusdet);
            }
        }
#endif
    }

    if (MHLA0 & 0x01)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-MHL Device Capability Change Interrupt ...\n"));

        if (mhltxrd(0xB0) & 0x01)
        {
            read_devcap_hw(it6681); 
        }
        else
        {    
            IT6681_DEBUG_INT_PRINTF(("IT6681-MHL Device Capability is still not Ready !!! \n"));
        }
    }

    if (MHLA0 & 0x02)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-MHL DSCR_CHG Interrupt ......\n"));
    }

    if (MHLA0 & 0x04)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-MHL REQ_WRT Interrupt  ...\n"));
        IT6681_DEBUG_INT_PRINTF(("IT6681-Set GRT_WRT to 1  ...\n"));

        set_mhlint(MHLInt00B, GRT_WRT);
    }

    if (MHLA0 & 0x08)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-[**]MHL GNT_WRT Interrupt  ...\n"));
    }

    if (MHLA1 & 0x02)
    {
        IT6681_DEBUG_INT_PRINTF(("IT6681-MHL EDID Change Interrupt ...\n"));
        // update edid
    }

    gDrv->LEDTick = it6681_get_tick_count() + 20;

exit_hdmitx_irq:

    return;
}


////////////////////////////////////////////////////////////////////
// void HDMITX_SET_SignalType()
////unsigned char DynRange,
// unsigned char colorcoef
//
////////////////////////////////////////////////////////////////////
//void HDMITX_SET_SignalType(unsigned char DynRange, unsigned char colorcoef, unsigned char pixrep)
//{
//    struct it6681_dev_data *it6681 = get_it6681_dev_data();
//
//    it6681->DynRange = DynRange; // DynCEA, DynVESA
//    it6681->YCbCrCoef = colorcoef; // ITU709, ITU601
//    it6681->PixRpt = pixrep;
//
//}

void it6681_set_packed_pixel_mode(unsigned char mode)
{
    struct it6681_dev_data *it6681 = get_it6681_dev_data();

    it6681->EnPackPix = mode;
}

void it6681_set_hdcp(unsigned char mode)
{
#if(_SUPPORT_HDCP_)
    struct it6681_dev_data *it6681 = get_it6681_dev_data();
    if (it6681)
    {
        it6681->HDCPEnable = mode;
    }
#endif
}

void it668x_set_trans_mode( char mode )
{
    struct it6681_dev_data *it6681 = get_it6681_dev_data();

    it6681->NonDirectTransMode = mode;
}

#if _SUPPORT_RCP_

void mhl_parse_RCPkey(struct it6681_dev_data *it6681)
{
    //parse_rcpkey( it6811->rxmsgdata[1]);

    if( SuppRCPCode[it6681->rxmsgdata[1]] ){
        
        it6681->txmsgdata[0] = MSG_RCPK;
        it6681->txmsgdata[1] = it6681->rxmsgdata[1];
        MHL_MSC_DEBUG_PRINTF(("Send a RCPK with action code = 0x%02X\n", it6681->txmsgdata[1]));
        
        mhl_RCP_handler(it6681);
    }
    else {
        it6681->txmsgdata[0] = MSG_RCPE;
        it6681->txmsgdata[1] = 0x01;
        
        MHL_MSC_DEBUG_PRINTF(("Send a RCPE with status code = 0x%02X\n", it6681->txmsgdata[1]));
    }

    cbus_send_mscmsg(it6681);
}
#endif

#if _SUPPORT_RAP_ 
void mhl_parse_RAPkey(struct it6681_dev_data *it6681)
{
    //parse_rapkey( it6811->rxmsgdata[1]);
        
    it6681->txmsgdata[0] = MSG_RAPK;    
    
    if( SuppRAPCode[it6681->rxmsgdata[1]]) {
        it6681->txmsgdata[1] = 0x00;
    }
    else{
        it6681->txmsgdata[1] = 0x02;
    }       
        
    switch( it6681->rxmsgdata[1] ) {
        case 0x00: 
            MHL_MSC_DEBUG_PRINTF(("Poll\n")); 
            break;
        case 0x10: 
            MHL_MSC_DEBUG_PRINTF(("Change to CONTENT_ON state\n"));
            hdmitx_set_termination(1);
            break;
        case 0x11: 
            MHL_MSC_DEBUG_PRINTF(("Change to CONTENT_OFF state\n"));
            hdmitx_set_termination(0);
            break;
        default  : 

        it6681->txmsgdata[1] = 0x01;
        MHL_MSC_DEBUG_PRINTF(("ERROR: Unknown RAP action code 0x%02X !!!\n", it6681->rxmsgdata[1]));
        MHL_MSC_DEBUG_PRINTF(("Send a RAPK with status code = 0x%02X\n", it6681->txmsgdata[1]));
     }

    cbus_send_mscmsg(it6681);
}
#endif

#if _SUPPORT_UCP_
void mhl_parse_UCPkey(struct it6681_dev_data *it6681)
{

    //parse_ucpkey( it6811->rxmsgdata[1] );
    
    if( (it6681->rxmsgdata[1]&0x80)==0x00 ) {
        it6681->txmsgdata[0] = MSG_UCPK;
        it6681->txmsgdata[1] = it6681->rxmsgdata[1];
        
        mhl_UCP_handler(it6681);
    }
    else {
        it6681->txmsgdata[0] = MSG_UCPE;
        it6681->txmsgdata[1] = 0x01;
    }

    cbus_send_mscmsg(it6681);
}
#endif

#if _SUPPORT_UCP_MOUSE_
void mhl_parse_MOUSEkey(struct it6681_dev_data *it6681)
{
    unsigned char opt,sig;
    unsigned char key =0;
    int x =0,y=0;
    
    opt = (it6681->rxmsgdata[1]&0xC0)>>6;
    sig = (it6681->rxmsgdata[1]&0x20)>>5;
    if(opt <3){
        
        it6681->txmsgdata[0] = MSG_MOUSEK;
        it6681->txmsgdata[1] = it6681->rxmsgdata[1];
        
        
        if(opt==0){
        
            key = it6681->rxmsgdata[1]&0x3F;
        
        }
        else if(opt == 1){
        
            x = (int)(it6681->rxmsgdata[1]&0x1F);
            x = x*2;  //scalling move
            if(sig) x = (-1)*x;
                
                
        }
        else{
        
            y = (int)(it6681->rxmsgdata[1]&0x1F);
            y = y*2;  //scalling move
            if(sig) y = (-1)*y;
        
        }
        
        mhl_UCP_mouse_handler(key,x,y);
    
    }
    else {
        it6681->txmsgdata[0] = MSG_MOUSEE;
        it6681->txmsgdata[1] = 0x01;
    }

    cbus_send_mscmsg(it6681);
}
#endif

void it6681_reset(void)
{
    struct it6681_dev_data *it6681 = get_it6681_dev_data();

    hdmirxset( 0x08, 0xfd, 0xfd );
    delay1ms(2);
    hdmirxset( 0x08, 0xc1, 0xc1 );
    delay1ms(2);
    hdmirxwr( 0x2b, IT6681_HDMI_TX_ADDR|0x1 );

    if ( hdmitxrd(0x02) == 0x82 ) 
    {
        gDrv->IsIT6682 = 1;
        gDrv->IT6682_MCU2Switch = 1;
        gDrv->IT6682_MCU2VBUSOUT = 1;
    }
    
    hdmirxset( 0x14, 0x80, 0x00 );
    hdmirxset( 0x14, 0x70, 0x20 );
            
    hdmitxset( 0x0f, 0x40, 0x00 );
    hdmitxset( 0x04, 0x09, 0x09 );
    hdmitxset( 0x62, 0x08, 0x00 );

    delay1ms(2);
    
    // switch to USB
    if(gDrv->IT6682_MCU2Switch) set_operation_mode(MODE_USB); 

    mhltxset( 0x0f, 0x10, 0x10 );
    hdmitxset( 0x04, 0x20, 0x20 );
    hdmitxset( 0x04, 0x40, 0x40 );
    
    hdmirxset( 0x08, 0x01, 0x00 );
    delay1ms(10);
    
    hdmirxset( 0x0e, 0x03, 0x01 );
    
    hdmirxwr( 0x2f, 0x8f );
    hdmirxset( 0x2d, 0x0c, 0x0c );
    hdmitxset( 0xe5, 0x02, 0x02 );
    hdmitxset( 0xe5, 0x01, 0x00 );
    hdmitxwr( 0xf8, 0xc3 );
    hdmitxwr( 0xf8, 0xa5 );
    hdmitxset( 0xea, 0x02, 0x00 );
    hdmitxset( 0xe0, 0x08, 0x08 );
    
    hdmitxset( 0xf4, 0xf8, 0x00 );
    hdmitxset( 0x05, 0x10, 0x00 );
    
    hdmitxwr( 0xf8, 0xff );
    
    hdmitxwr( 0x8d, IT6681_MHL_ADDR|0x1 );
    
    // HDCP TimeBase
    hdmitxwr(0x47, TimeLoMax&0xFF);
    hdmitxwr(0x48, (TimeLoMax&0xFF00)>>8);
    hdmitxset(0x49, 0xC3, (0<<6)+((TimeLoMax&0x30000)>>16));
    
    hdmirxset(0x0f, 0x10, 0x00);    //Rx Clock change detect Interrupt, disable
    hdmitxwr( 0x06, 0xff );
    hdmitxwr( 0x07, 0xff );
    hdmitxwr( 0x08, 0xff );
    hdmitxwr( 0xee, 0xff );
    hdmitxwr( 0x09, 0xa0 );
    hdmitxwr( 0x0a, 0x00 );
    hdmitxset( 0x0b, 0x1f, 0x00 );
    mhltxwr( 0x04, 0xff );
    mhltxwr( 0x05, 0xff );
    mhltxwr( 0x06, 0xff );
    mhltxwr( 0x0a, 0x00 );
    mhltxwr( 0x08, 0x7f );
    
    cal_oclk(it6681);

    //mhltxset( 0x01, 0x70, 0x50 );
    //mhltxwr( 0x02, 0x7d );
    //mhltxwr( 0x03, 0x33 );
    mhltxset( 0x01, 0x0c, 0x08 );
    mhltxwr( 0x52, 0x00 );
    mhltxwr( 0x53, 0x80 );
    hdmitxset( 0xe8, 0x02, 0x02 );
    hdmitxwr( 0x66, 0x1c );
    
    // usbextVbusDet
    hdmitxset( 0xe9, 0x04, 0x04 );
    
    mhltxset( 0x00, 0x8f, 0x05 );
    mhltxset( 0x01, 0x80, 0x80 );
    mhltxset( 0x0c, 0xf1, 0x90 );
    mhltxset( 0x0e, 0x80, 0x00 );
    mhltxset( 0x36, 0xfc, 0xb4 );
    mhltxset( 0x38, 0x01, 0x01 );
    mhltxset( 0x5c, 0xbc, 0x94 );
    mhltxset( 0x28, 0x9f, 0x96 );
    mhltxset( 0x28, 0x40, 0x40 );
    mhltxset( 0x29, 0x0e, 0x04 );
    mhltxwr( 0x2a, 0xa7 );
    mhltxwr( 0x2b, 0x72 );
    mhltxwr( 0x2c, 0x20 );
    mhltxset( 0x66, 0x03, 0x02 );
    mhltxwr( 0x32, 0x0c );

    // Device Capability Initialization
    mhltxwr( 0x81, 0x21 );
    mhltxset( 0x82, 0x10, 0x10 );
    mhltxwr( 0x83, 0x02 );
    mhltxwr( 0x84, 0x45 );
    mhltxwr( 0x8b, 0x66 );
    mhltxwr( 0x8c, 0x81+gDrv->IsIT6682 );
    
    // does not output VBUS, until 1K detect is done
    if( gDrv->IT6682_MCU2VBUSOUT ) set_vbus_output(0);

    // switch to MHL
    if( gDrv->IT6682_MCU2Switch ) set_operation_mode( MODE_MHL );
    mhltxset( 0x0f, 0xbb, 0xa2 );
}

int it6681_fwinit(void)
{
    struct it6681_dev_data *it6681 = get_it6681_dev_data();

    int ret = 0;
    BYTE uc1 = 0x00;
    BYTE uc2 = 0x00;
    int i;

    gDrv->OclkTick1 = 0;
    gDrv->OclkTick2 = 0;
    gDrv->OclkSum = 0;
    gDrv->OclkTickSum = 0;
    gDrv->EdidStored = 0;
    gDrv->IT6682_MCU2Switch = 0;
    gDrv->IT6682_MCU2VBUSOUT = 0;
    gDrv->IsIT6682 = 0;
    gDrv->KeepRxHPD = 0;
    gDrv->ForceRxHPD = 0;
    gDrv->enable_internal_edid = 0;

    if ( gDrv->enable_internal_edid )
    {
        it6681_init_internal_edid();
    }

    // identify device
    for (i = 0; i < 100; i++)
    {
        uc1 = hdmirxrd(0x02);
        uc2 = hdmirxrd(0x03);

        IT6681_DEBUG_PRINTF(("hdmirx_ini() - hdmirxrd %02x %02x\n", (int)uc1, (int)uc2));

        if (uc1 == 0x81 && (uc2 & 0x0F) == 0x6)
        {
            it6681->ver = (uc2 & 0xF0) >> 4;

            IT6681_DEBUG_PRINTF((VERSION_STRING"\n"));
            //IT6681_DEBUG_PRINTF((("\n")));
            IT6681_DEBUG_PRINTF(("IT6681-###############################################\n"));
            IT6681_DEBUG_PRINTF(("IT6681-#            MHLTX Initialization             #\n"));
            IT6681_DEBUG_PRINTF(("IT6681-###############################################\n"));
            IT6681_DEBUG_PRINTF(("IT6681- Ver = %d \n", (int)it6681->ver));

            break;
        }
        delay1ms(50);
    }

    //hdmirxset(0x08, 0xFD, 0xFD); //emily mark 0328
    //delay1ms(2);
    //hdmirxset(0x08, 0xc1, 0xC1);
    //delay1ms(2);
    //hdmirxwr(0x2b, IT6681_HDMI_TX_ADDR | 0x1); // HDMITX Slave address enable

    //it6811_set_reg_table(tIt6681init_hdmi, hdmitxset);

    //oclk_calc_begin();

    // initial HDMI RX
    //it6811_set_reg_table(it6681_hdmirx_reg_init, hdmirxset);

    // initial HDMI TX
    //it6811_set_reg_table(tIt6681init_hdmi2, hdmitxset);


    it6681->PixRpt = 1;
    it6681->EnPackPix = 0; // TRUE for MHL PackedPixel Mode , FALSE for 24-bit Mode

    it6681->InColorMode = RGB444; // YCbCr422 RGB444 YCbCr444
    it6681->OutColorMode = RGB444;

    // it6681->ColorDepth = VID8BIT;
    it6681->DynRange = DynCEA; // DynCEA, DynVESA
    it6681->YCbCrCoef = ITU709; // ITU709, ITU601


    it6681->CBusPathEn = 0;

#if(_SUPPORT_HDCP_)
    it6681->HDCPEnable = TRUE;
#endif

    //oclk_calc_end();
    //oclk_calc_result();

    //hdmitx_ini();
    //hdmitx_rst2();

    //cal_oclk(it6681);
    //it6811_set_reg_table(tIt6681init_mhl, mhltxset);

    //hdmitx_rst2();

    it6681_reset();

    Mhl_state(it6681, MHL_Cbusdet);

    hdmitx_pwron();

    Hdmi_Video_state(it6681, HDMI_Video_REST);

    return ret;
}

void hdmirx_irq(void)
{
    BYTE RDetCLKStb = 0;
    BYTE RxCLK_Valid = 0;
    BYTE RxReg05;
    BYTE RxReg06;

    RxReg05 = hdmirxrd(0x05);
    hdmirxwr(0x05, 0xff);
    RxReg06 = hdmirxrd(0x06);
    hdmirxwr(0x06, 0xff);

    // emily says:
    // When PCLK> 80 Mz,  ignore RxCLKChg_Det interrupt.
    // if(RX reg06, D[4]==1) , ignore (RX reg05, D[4])
    if (RxReg06 & 0x10)
    {
        RxReg05 &= ~0x10;
        gDrv->RxClock80M = 1;

        //if ( gDrv->RxClock80M == 0 )
        //{
        //    gDrv->RxClock80M = 1;
        //    hdmirxset(0x0f, 0x10, 0x00);
        //}
    }
    else
    {
        gDrv->RxClock80M = 0;

        //if ( gDrv->RxClock80M == 1 )
        //{
        //    gDrv->RxClock80M = 0;
        //    hdmirxset(0x0f, 0x10, 0x10);
        //}
    }

    if (RxReg05 & 0x01)
    {
        IT6681_DEBUG_INT_PRINTF(("PWR5V change\n"));

        if (RxReg06 & 0x01)
        {
            IT6681_DEBUG_INT_PRINTF(("PWR5V is ON\n"));
        }
        else
        {
            IT6681_DEBUG_INT_PRINTF(("PWR5V is OFF\n"));
        }
    }
    else
    {
    }

    if (RxReg05 & 0x02)
    {
        IT6681_DEBUG_INT_PRINTF(("HDMI/DVI mode change to :"));

        if (RxReg06 & 0x04)
        {
            IT6681_DEBUG_INT_PRINTF(("HDMI mode. \n"));
        }
        else
        {
            IT6681_DEBUG_INT_PRINTF(("DVI mode. \n"));
        }
    }

    if (RxReg05 & 0x04)
    {
        // This interrupt is ignored. 2013/06
        IT6681_DEBUG_PRINTF(("!!!!!!!!!! HDMI RX ECC Error !!!!!!!!!!\n"));
        hdmirxset(0x0f, 0x04, 0x00);
        // hdmirxset(0x08,0x24, 0x24);
        // delay1ms(1);
        // hdmirxset(0x08,0x24, 0x00);
    }

    if (RxReg05 & 0x10)
    {
        IT6681_DEBUG_INT_PRINTF(("HDMI RX clock change detected\n"));
    }

    if (RxReg05 & 0x20)
    {
        IT6681_DEBUG_INT_PRINTF(("HDMI RX TimerInt\n"));
    }

    if (RxReg05 & 0x40)
    {
        IT6681_DEBUG_INT_PRINTF(("HDMI RX AutoEQ update\n"));
    }

    if (RxReg05 & 0x80)
    {
        IT6681_DEBUG_INT_PRINTF(("HDMI RX RX clock stable change\n"));
    }

    if (RxReg05 & 0x90)
    {
        RDetCLKStb = (RxReg06 & 0x40) >> 6;
        RxCLK_Valid = (RxReg06 & 0x08) >> 3;

        if (RxCLK_Valid == 1)
        {
            IT6681_DEBUG_INT_PRINTF(("Clock is valid, "));
        }
        else
        {
            IT6681_DEBUG_INT_PRINTF(("Clock is NOT valid,"));
        }

        if (RDetCLKStb == 1)
        {
            IT6681_DEBUG_INT_PRINTF(("Clock is stable \n"));
        }
        else
        {
            IT6681_DEBUG_INT_PRINTF(("Clock is NOT stable \n"));
        }

        if ((RDetCLKStb == 0 || RxCLK_Valid == 0))
        {
            // do reset
            hdmirxset(0x08, 0x24, 0x24);
            delay1ms(100);
            hdmirxset(0x08, 0x24, 0x00);

            if (gDrv->m_VState == VSTATE_VideoOn)
            {
                gDrv->m_VState = VSTATE_Off;
            }
        }
        else
        {
            gDrv->m_VState = VSTATE_VideoOn;
        }
    }

    if ((RxReg06 & 0x04) == 0 && RDetCLKStb == 1 && RxCLK_Valid == 1)
    {
        IT6681_DEBUG_INT_PRINTF(("!!!!!!!!!! RxCLK correct, but DVI mode detected  !!!!!!!!!!\n"));

        hdmirxset(0x08, 0x24, 0x24);
        delay1ms(100);
        hdmirxset(0x08, 0x24, 0x00);
    }
}

void it6681_irq(void)
{
    hdmirx_irq();
    hdmitx_irq(get_it6681_dev_data());
}

void it6681_poll(void)
{
#if _SUPPORT_HDCP_
    struct it6681_dev_data *it6681 = get_it6681_dev_data();

    if (it6681->HDCPEnable)
    {
        Hdmi_HDCP_handler(it6681);
    }
#endif
}

void it6681_disable_cbus_1k_detection(void)
{
    mhltxset( 0x29, 0x08, 0x08 );
}
void it6681_enable_cbus_1k_detection(void)
{
    mhltxset( 0x29, 0x08, 0x00 );
}
void DumpHDMITXReg8051(void)
{
    int i, j;
    int ij;
    unsigned char ucData = 0;
    IT6681_DEBUG_PRINTF(("########################################################################\n"));
    IT6681_DEBUG_PRINTF(("#Dump IT6681\r\n"));
    IT6681_DEBUG_PRINTF(("########################################################################\n" "       "));

    IT6681_DEBUG_PRINTF((""));
    for (j = 0; j < 16; j++)
    {
        IT6681_DEBUG_PRINTF((" %02X", (int)j));
        if ((j == 3) || (j == 7) || (j == 11))
        {
            IT6681_DEBUG_PRINTF(("  "));
        }
    }
    IT6681_DEBUG_PRINTF(("\n        -----------------------------------------------------\n"));

    for (i = 0; i < 0x100; i += 16)
    {
        IT6681_DEBUG_PRINTF(("[%3X]  ", i));
        for (j = 0; j < 16; j++)
        {
            ij = (i + j) & 0xFF;
            if (ij != 0x17)
            {
                ucData = hdmitxrd((unsigned char)ij);
                IT6681_DEBUG_PRINTF((" %02X", (int)ucData));
            }
            else
            {
                IT6681_DEBUG_PRINTF((" XX")); // for DDC FIFO
            }
            if ((j == 3) || (j == 7) || (j == 11))
            {
                IT6681_DEBUG_PRINTF((" -"));
            }
        }
        IT6681_DEBUG_PRINTF(("\n"));
        if ((i % 0x40) == 0x30)
        {
            IT6681_DEBUG_PRINTF(("        -----------------------------------------------------\n"));
        }
    }

    IT6681_DEBUG_PRINTF(("        MHL Register\n"));
    IT6681_DEBUG_PRINTF(("        -----------------------------------------------------\n"));

    for (i = 0; i < 0x100; i += 16)
    {
        IT6681_DEBUG_PRINTF(("[%3X]  ", i));
        for (j = 0; j < 16; j++)
        {
            ij = (i + j) & 0xFF;

            if (ij == 0x17 || ij == 0x59 || ij == 0x5B)
            {
                IT6681_DEBUG_PRINTF((" XX")); // for FIFO
            }
            else
            {
                ucData = mhltxrd((unsigned char)((i + j) & 0xFF));
                IT6681_DEBUG_PRINTF((" %02X", (int)ucData));
            }

            if ((j == 3) || (j == 7) || (j == 11))
            {
                IT6681_DEBUG_PRINTF((" -"));
            }
        }
        IT6681_DEBUG_PRINTF(("\n"));
        if ((i % 0x40) == 0x30)
        {
            IT6681_DEBUG_PRINTF(("        -----------------------------------------------------\n"));
        }
    }
}

void DumpHDMITXReg(void)
{
    int i, j;
    int ij;
    unsigned char ucData = 0;
    char str[128]={0};
    char str0[128]={0};

    IT6681_DEBUG_PRINTF(("############################################################\n"));
    IT6681_DEBUG_PRINTF(("#Dump IT6681\n"));
    IT6681_DEBUG_PRINTF(("############################################################\n"));

    strcpy(str, ".      ");
    for (j = 0; j < 16; j++)
    {
        sprintf(str0, " %02X", j);
        strcat(str, str0);
        if ((j == 3) || (j == 7) || (j == 11))
        {
            strcat(str, "  ");
        }
    }

    IT6681_DEBUG_PRINTF(("%s", str));
    IT6681_DEBUG_PRINTF(("\n        -----------------------------------------------------\n"));

    for (i = 0; i < 0x100; i += 16)
    {
        sprintf(str, "[%3X]  ", i);
        for (j = 0; j < 16; j++)
        {
            ij = (i + j) & 0xFF;
            if (ij != 0x17)
            {
                ucData = hdmitxrd((unsigned char)ij);
                sprintf(str0, " %02X", ucData);
                strcat(str, str0);
            }
            else
            {
                strcat(str, " XX");
            }
            if ((j == 3) || (j == 7) || (j == 11))
            {
                strcat(str, " -");
            }
        }

        IT6681_DEBUG_PRINTF(("%s\n", str));
        if ((i % 0x40) == 0x30)
        {
            IT6681_DEBUG_PRINTF(("        -----------------------------------------------------\n"));
        }
    }

    IT6681_DEBUG_PRINTF(("        MHL Register\n"));
    IT6681_DEBUG_PRINTF(("        -----------------------------------------------------\n"));

    for (i = 0; i < 0x100; i += 16)
    {
        sprintf(str, "[%3X]  ", i);
        for (j = 0; j < 16; j++)
        {
            ij = (i + j) & 0xFF;

            if (ij == 0x17 || ij == 0x59 || ij == 0x5B)
            {
                strcat(str, " XX");
            }
            else
            {
                ucData = mhltxrd((unsigned char)((i + j) & 0xFF));
                sprintf(str0, " %02X", ucData);
                strcat(str, str0);
            }

            if ((j == 3) || (j == 7) || (j == 11))
            {
                strcat(str, " -");
            }
        }

        IT6681_DEBUG_PRINTF(("%s\n", str));
        if ((i % 0x40) == 0x30)
        {
            IT6681_DEBUG_PRINTF(("        -----------------------------------------------------\n"));
        }
    }

    IT6681_DEBUG_PRINTF(("        RX Register\n"));
    IT6681_DEBUG_PRINTF(("        -----------------------------------------------------\n"));

    for (i = 0; i < 0x40; i += 16)
    {
        sprintf(str, "[%3X]  ", i);
        for (j = 0; j < 16; j++)
        {
            ucData = hdmirxrd((BYTE)((i + j) & 0xFF));
            sprintf(str0, " %02X", ucData);
            strcat(str, str0);
            if ((j == 3) || (j == 7) || (j == 11))
            {
                strcat(str, " -");
            }
        }

        IT6681_DEBUG_PRINTF(("%s\n", str));
        if ((i % 0x40) == 0x30)
        {
            IT6681_DEBUG_PRINTF(("        -------------------------------------------------------\n"));
        }
    }
}


