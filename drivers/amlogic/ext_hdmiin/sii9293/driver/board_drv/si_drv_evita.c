//------------------------------------------------------------------------------
// Project: 5293
// Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1140 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------

#include "si_drv_cra_cfg.h"
#include "si_drv_evita.h"
#include "si_drv_rx.h"
#include "si_rx_info.h"

bool_t SiiDrvEvitaInit(void)
{
    // Evita chip Configuration
    // HDMI Mode
    bool_t  success = true;
    SiiRegWrite(PP_PAGE_3 | 0x48, 0x00); //color space setting
    SiiRegWrite(PP_PAGE_3 | 0x49, 0x00); //output color space YCbCr
    SiiRegWrite(PP_PAGE_3 | 0x4A, 0x00); //video mode converter

    SiiRegWrite(PP_PAGE_4 | 0x3C, 0x00); //bring out of core iso mode	
    SiiRegWrite(PP_PAGE_3 | 0x08, 0x35); //power up chip
    SiiRegWrite(PP_PAGE_3 | 0x0F, 0x00); //Diaable HDCP
    SiiRegWrite(PP_PAGE_3 | 0x0D, 0x02); //Audio Mute

    SiiRegWrite(PP_PAGE_4 | 0x02, 0x01); //MCLK =256*Fs
    SiiRegWrite(PP_PAGE_4 | 0x03, 0x00); //Set N value [7:0] bit
    SiiRegWrite(PP_PAGE_4 | 0x04, 0x18); //Set N value  [15:8] bit
    SiiRegWrite(PP_PAGE_4 | 0x05, 0x00); //Set N value [19:16] bit

    SiiRegWrite(PP_PAGE_4 | 0x21, 0x02); //set 48khz Fs
    SiiRegWrite(PP_PAGE_4 | 0x22, 0x0B); //set 24-bit bus width
    SiiRegWrite(PP_PAGE_4 | 0x23, 0x10);
    SiiRegWrite(PP_PAGE_4 | 0x24, 0x0B); //set 24-bit I2S bus width
    SiiRegWrite(PP_PAGE_4 | 0x1D, 0x40); //set I2S bus format
    SiiRegWrite(PP_PAGE_4 | 0x14, 0xF1); //Enable 8-ch I2S input 
    SiiRegWrite(PP_PAGE_4 | 0x1C, 0xE4); //Assign the SD0,1,2,3 FIFO
    SiiRegWrite(PP_PAGE_4 | 0x2F, 0x03); //enable hdmi mode	

    SiiRegWrite(PP_PAGE_3 | 0x0D, 0x00); //Disable Audio Mute

    SiiRegWrite(PP_PAGE_4 | 0xDF, 0x10); //Clear AVMUT flag
    SiiRegWrite(PP_PAGE_4 | 0x3E, 0x33); //Enable the ,AVI,AUD infoFrame Packet each frame
    SiiRegWrite(PP_PAGE_4 | 0x3F, 0x0C); //Enable the ,General CTL infoFrame Packet each frame
    SiiRegWrite(PP_PAGE_3 | 0x0F, 0x01); //Enable HDCP

    SiiRegWrite(PP_PAGE_3 | 0xC7, 0x00); //Enter HW TPI mode
    SiiRegWrite(PP_PAGE_3 | 0x1A, 0x01); //Turn TMDS on

    return success;
}


void SiiDrvEvitaAviIfUpdate(void)
{
	uint8_t i, checksum;
    VideoPath_t inVideoPath = SiiDrvRxGetOutVideoPath();
    color_space_type csType;
    uint8_t ifData[17];
    bool_t YcMuxFlag = false;

    if (inVideoPath == PATH__RGB)
    {
        csType = ColorSpace_RGB;
    }
    else if (inVideoPath == PATH__YCbCr444)
    {
        csType = ColorSpace_YCbCr444;
    }
    else
    {
        csType = ColorSpace_YCbCr422;
    }

    ifData[0] = 0x82;       //type
    ifData[1] = 0x02;       //version
    ifData[2] = 0x0D;       //length
    ifData[3] = 0x00;       //checksum
    ifData[4] = csType << 5; 
    ifData[5] = 0x08;       //colorimetry;
    if (csType == ColorSpace_RGB)
    {
        ifData[6] = 0x08;       // 0x08: full range, 0x04: limited range, only for RGB
    }
    else
    {
        ifData[6] = 0;
    }
    ifData[7] = RxAVI_GetVic();
    ifData[8] = 0x00;       //0x00: limited range, 0x40: full range, only for YCbCr

    if (RxAVI_GetReplication())
    {
        switch (inVideoPath)
        {
            case PATH__YCbCr422_MUX8B_SYNC:
            case PATH__YCbCr422_MUX10B_SYNC:
            case PATH__YCbCr422_MUX8B:
            case PATH__YCbCr422_MUX10B:
                YcMuxFlag = true;
                ifData[8] |= 0x01;  //Set repetition in the infoframe
                break;
            default:
                break;
    }
    }

    ifData[9] = 0x00;
    ifData[10] = 0x00; 
    ifData[11] = 0x00;
    ifData[12] = 0x00; 
    ifData[13] = 0x00;
    ifData[14] = 0x00;
    ifData[15] = 0x00; 
    ifData[16] = 0x00;

    checksum = 0x00;
    for (i = 0; i < 17; i++)
    {
        checksum += ifData[i];
    }
    ifData[3] = 0x100 - checksum;
	// Fill AVI InfoFrame in TPI mode
	for (i = 0; i < 14; i++)
 	{
 		SiiRegWrite(PP_PAGE_3  | (0x0C + i), ifData[i+3]);
//		DEBUG_PRINT(MSG_ALWAYS, ("Configure Evita AVI InfoFrame byte %2X to %2X\n", (int)i, (int)(*pPacket[i+IF_HEADER_LENGTH-1])));
 	}

	// Check whether trigger is already active
	if ( 0x19 >= 0x0C + i )
	{
		SiiRegWrite(PP_PAGE_3  | 0x19, 0x00);
		DEBUG_PRINT(MSG_ALWAYS, ("Configure Evita AVI InfoFrame, force trigger here\n"));
	}

    // This bit will reset when overwrite infoframe
    if (YcMuxFlag)
    {
        SiiRegModify(PP_PAGE_3 | 0x60, BIT5, SET_BITS); //Enable YC de MUX
    }
}

void SiiDrvEvitaAudioIfUpdate(uint8_t *pPacket)
{
	uint8_t i = 0;
	uint8_t data_length = pPacket[IF_LENGTH_INDEX];


        SiiRegWrite(PP_PAGE_3  | 0xBF, 0xD2);

	// Fill AUDIO InfoFrame in TPI mode
	for (i = 0; i < IF_MAX_AUDIO_LENGTH+1 && i < data_length+1; i++)
 	{
 		SiiRegWrite(PP_PAGE_3  | (0xC0 + i), pPacket[i]);
//		DEBUG_PRINT(MSG_ALWAYS, ("Configure Evita AUDIO InfoFrame byte %2X to %2X\n", (int)i, (int)(*pPacket[i+IF_HEADER_LENGTH-1])));
 	}

	// Check whether trigger is already active
	if ( 0xCD >= 0xC0 + i )
	{
		SiiRegWrite(PP_PAGE_3  | 0xCD, 0x00);
		DEBUG_PRINT(MSG_ALWAYS, ("Configure Evita ADUIO InfoFrame, force trigger here\n"));
	}

}

