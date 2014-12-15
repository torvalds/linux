/******************************************************************************/
//!file     si_drv_cbus.c
//!brief    SiI5293 CBUS Driver.
//
// No part of this work may be reproduced, modified, distributed,
// transmitted, transcribed, or translated into any language or computer
// format, in any form or by any means without written permission of
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2007-2013, Silicon Image, Inc.  All rights reserved.
/******************************************************************************/

#include "si_common.h"
#include "si_drv_cbus.h"
#include "si_cbus_enums.h"
#include "si_drv_device.h"
#include "si_drv_board.h"

#include "si_edid_3d_internal.h"


#ifdef MHAWB_SUPPORT
#include "../mhawb_drv/si_drv_mhawb.h"
#include "../mhawb_drv/si_drv_hawb.h"
#endif

#define HDMI_3D_SVD_SUPPORT                                  // comment this out if the sink doesn't support 3D
#define HDMI_SVD_STRUCTURE_LENGTH_EDID      17  // number of VIC entries in EDID (should be updated per application)

ROM Mandatory3dFmt_t Mandatory_60[Mandatory3dFmt_60] = { {{VIC_1080P_24, 0}, {FRAME_SEQUENTIAL|TOP_BOTTOM}}, 
                                                         {{VIC_720P_60, 0} , {FRAME_SEQUENTIAL|TOP_BOTTOM}},
                                                         {{VIC_1080i_60, 0}, {LEFT_RIGHT}}
                                                         };

ROM Mandatory3dFmt_t Mandatory_50[Mandatory3dFmt_50] = { {{VIC_1080P_24, 0}, {FRAME_SEQUENTIAL|TOP_BOTTOM}}, 
                                                         {{VIC_720P_50, 0},  {FRAME_SEQUENTIAL|TOP_BOTTOM}},
                                                         {{VIC_1080i_50, 0}, {LEFT_RIGHT}}
                                                         };


VIC3DFormat_t VIC3DFormats[HDMI_3D_SVD_STRUCTURE_LENGTH] = {{0},};

CbusDrvInstanceData_t cbusDrvInstance;
CbusDrvInstanceData_t *pDrvCbus = &cbusDrvInstance;

static uint16_t cbusInitCbusRegsList [] =
{
    REG_CBUS_DEVICE_CAP_0,        MHL_DEV_STATE,
    REG_CBUS_DEVICE_CAP_1,        MHL_MHL_VERSION,
    REG_CBUS_DEVICE_CAP_2,        MHL_DEV_CAT,
    REG_CBUS_DEVICE_CAP_3,        MHL_ADOPTER_ID_H,
    REG_CBUS_DEVICE_CAP_4,        MHL_ADOPTER_ID_L,
    REG_CBUS_DEVICE_CAP_5,        MHL_VID_LINK_MODE,
    REG_CBUS_DEVICE_CAP_6,        MHL_AUD_LINK_MODE,
    REG_CBUS_DEVICE_CAP_7,        MHL_VIDEO_TYPE,
    REG_CBUS_DEVICE_CAP_8,        MHL_LOG_DEV_MAP,
    REG_CBUS_DEVICE_CAP_9,        MHL_BANDWIDTH,
    REG_CBUS_DEVICE_CAP_A,        MHL_FEATURE_FLAG,
    REG_CBUS_DEVICE_CAP_B,        MHL_DEVICE_ID_H,
    REG_CBUS_DEVICE_CAP_C,        MHL_DEVICE_ID_L,
    REG_CBUS_DEVICE_CAP_D,        MHL_SCRATCHPAD_SIZE,
    REG_CBUS_DEVICE_CAP_E,        MHL_INT_STAT_SIZE,
    REG_CBUS_DEVICE_CAP_F,        MHL_RESERVED,
    REG_CBUS_LNK_CNTL_8,               0x14,
};

/*****************************************************************************/
/**
 *  @brief        Returns the CBus register value
 *
 *  @param[in]        regAddr        register address, channel
 *
 *  @return    register value
 *
 *****************************************************************************/
uint8_t  SiiDrvCbusDevCapsRegisterGet ( uint16_t offset )
{
    uint8_t value;

    value = SiiRegRead( REG_CBUS_DEVICE_CAP_0 + offset );
    return ( value );
}


/*****************************************************************************/
/**
 *  @brief        Returns interrupt flag
 *
 *  @return    interrupt flag
 *
 *****************************************************************************/
 bool_t  SiiDrvCbusIntrFlagGet()
{
    if ( pDrvCbus->statusFlags & SiiCBUS_INT )
    {
        return( true );
    }
    return( false );
}

/*****************************************************************************/
/**
 *  @brief        reset interrupt flags
 *
 *****************************************************************************/
 void  SiiDrvCbusIntrFlagSet()
{
    // Do not clear SiiCBUS_MHL_CONNECTION_CHG, since it will be processed out of cbus handler.
    if (pDrvCbus->statusFlags & SiiCBUS_MHL_CONNECTION_CHG)
        pDrvCbus->statusFlags = SiiCBUS_MHL_CONNECTION_CHG;
    else
        pDrvCbus->statusFlags = 0;
}

/*****************************************************************************/
/**
 *  @brief        Returns the last MSC Abort reason received by the CBUS ISR.
 *
 *  @param[in]        pData        pointer to return data buffer (1 byte).
 *
 *  @return    MSC Abort reason data
 *  @retval    true        a new MSC Abort reason data is available
 *  @retval    false        a new MSC Abort reason data is not available
 *
 *****************************************************************************/
 bool_t  SiiDrvCbusNackFromPeerGet ()
{
    if ( pDrvCbus->statusFlags & SiiCBUS_NACK_RECEIVED_FM_PEER )
    {
        pDrvCbus->statusFlags &= ~SiiCBUS_NACK_RECEIVED_FM_PEER;
        return( true );
    }
    return( false );
}

/*****************************************************************************/
/**
 *  @brief        Returns if the peer's device capability values are changed
 *
 *  @return    peer's device capability values are changed or not
 *  @retval    true        peer's device capability values are changed
 *  @retval    false        peer's device capability values are not changed
 *
 *****************************************************************************/
 bool_t  SiiDrvCbusDevCapChangedGet ()
{
    if ( pDrvCbus->statusFlags & SiiCBUS_DCAP_CHG_RECEIVED_FM_PEER )
    {
        pDrvCbus->statusFlags &= ~SiiCBUS_DCAP_CHG_RECEIVED_FM_PEER;
        return( true );
    }
    return( false );
}

/*****************************************************************************/
/**
 *  @brief        Returns if the peer has written the scratchpad
 *
 *  @return    peer has written the scratchpad or not
 *  @retval    true        peer has written the scratchpad
 *  @retval    false        peer has not written the scratchpad
 *
 *****************************************************************************/
 bool_t  SiiDrvCbusScratchpadWrtnGet ()
{
    if (pDrvCbus->statusFlags & SiiCBUS_SCRATCHPAD_WRITTEN_BY_PEER)
    {
        pDrvCbus->statusFlags &= ~SiiCBUS_SCRATCHPAD_WRITTEN_BY_PEER;
        return (true);
    }
    return (false);
}

/*****************************************************************************/
/**
 *  @brief        Returns if the peer is requesting for scratchpad write permission
 *
 *  @return    peer is requesting for scratchpad write permission or not
 *  @retval    true        peer is requesting for scratchpad write permission
 *  @retval    false        peer is not requesting for scratchpad write permission
 *
 *****************************************************************************/
bool_t  SiiDrvCbusReqWrtGet ()
{
    if ( pDrvCbus->statusFlags & SiiCBUS_REQ_WRT_RECEIVED_FM_PEER )
    {
        pDrvCbus->statusFlags &= ~SiiCBUS_REQ_WRT_RECEIVED_FM_PEER;
        return( true );
    }
    return( false );
}

/*****************************************************************************/
/**
 *  @brief        Returns if the peer has granted scratchpad write permission
 *
 *  @return    peer has granted scratchpad write permission or not
 *  @retval    true        peer has granted scratchpad write permission
 *  @retval    false        peer has not granted scratchpad write permission
 *
 *****************************************************************************/
 bool_t  SiiDrvCbusGrtWrtGet ()
{
    if ( pDrvCbus->statusFlags & SiiCBUS_GRT_WRT_RECEIVED_FM_PEER )
    {
        pDrvCbus->statusFlags &= ~SiiCBUS_GRT_WRT_RECEIVED_FM_PEER;
        return( true );
    }
    return( false );
}

/*****************************************************************************/
/**
 *  @brief        Send out the 3D Write Burst infomation
 *
 *  @return    true if adding to the cubs que successfully
 *
 *****************************************************************************/
bool_t SiiDrv3DWriteBurst()
{
    Mhl2VideoFormatData_t *pvideoFormatData = pDrvCbus->videoFormatData;
    // no need to check SpSupport since mhl source always support scatchpad from spec.
    while (BURST_ID(pvideoFormatData->burstId) == burstId_3D_VIC ||  BURST_ID(pvideoFormatData->burstId) == burstId_3D_DTD)
    {
        if (!SiiCbusWritePeersScratchpad(0x00, 16, (uint8_t *)pvideoFormatData))
        {
            return ~SUCCESS;
        }
        pvideoFormatData++;
    }
    
    return SUCCESS;
}

/*****************************************************************************/
/**
 *  @brief        Returns if the peer requesting for 3D infomation
 *
 *  @return    peer has requested 3D infomation or not
 *  @retval    true        peer has requested 3D infomation
 *  @retval    false        peer has not requested 3D infomation
 *
 *****************************************************************************/
 bool_t  SiiDrvCbus3DReqGet ()
{
    if ( pDrvCbus->statusFlags & SiiCBUS_3D_REQ_RECEIVED_FM_PEER )
    {
        pDrvCbus->statusFlags &= ~SiiCBUS_3D_REQ_RECEIVED_FM_PEER;
        return( true );
    }
    return( false );
}


/*****************************************************************************/
/**
 *  @brief        Build 3D Data Per EDID
 *
 *****************************************************************************/
static void SiiDrvBuild3DDataPerEdid(void)
{
    // TODO: Please update data here per EDID in actual application. Currently, default 3D formats are added

    // 02 / 03 / 04 / 05 / 06 here are indexes of corresponding VICs in EDID's Video Data Block

    // 02 - 1080I50
    VIC3DFormats[2].Fields.FrameSequential = 0;
    VIC3DFormats[2].Fields.TopBottom = 0;
    VIC3DFormats[2].Fields.LeftRight = 1;

    // 03 -  1080I60
    VIC3DFormats[3].Fields.FrameSequential = 0;
    VIC3DFormats[3].Fields.TopBottom = 0;
    VIC3DFormats[3].Fields.LeftRight = 1;

    // 04 -  720P50
    VIC3DFormats[4].Fields.FrameSequential = 1;
    VIC3DFormats[4].Fields.TopBottom = 1;
    VIC3DFormats[4].Fields.LeftRight = 0;

    // 05 -  720P60
    VIC3DFormats[5].Fields.FrameSequential = 1;
    VIC3DFormats[5].Fields.TopBottom = 1;
    VIC3DFormats[5].Fields.LeftRight = 0;

    // 06 -  1080P24
    VIC3DFormats[6].Fields.FrameSequential = 1;
    VIC3DFormats[6].Fields.TopBottom = 1;
    VIC3DFormats[6].Fields.LeftRight = 0;

}

/*****************************************************************************/
/**
 *  @brief        Build 3D Data for sending
 *
 *****************************************************************************/
void SiiDrvCbusBuild3DData(void)
{
    uint8_t i = 0;
    uint8_t j = 0;
    uint8_t k = 0;
    uint8_t * ptr;
    uint8_t checksum;
    uint8_t length = HDMI_3D_SVD_STRUCTURE_LENGTH;
    uint8_t EdidVicCnt = HDMI_SVD_STRUCTURE_LENGTH_EDID;
    
    memset(pDrvCbus->videoFormatData, 0x00, sizeof(pDrvCbus->videoFormatData));

    pDrvCbus->videoFormatData[0].burstId.low = burstId_3D_DTD;
    pDrvCbus->videoFormatData[0].totalEntries = 0;
    pDrvCbus->videoFormatData[0].sequenceIndex = 1;
    pDrvCbus->videoFormatData[0].numEntriesThisBurst = 0;

#ifdef HDMI_3D_SVD_SUPPORT
    {
        SiiDrvBuild3DDataPerEdid();

        if ( EdidVicCnt < HDMI_3D_SVD_STRUCTURE_LENGTH)
        {
            length = EdidVicCnt;
        }
        j = 1;
        for(i=0; i < length; i++)
        {
            pDrvCbus->videoFormatData[j].burstId.low = burstId_3D_VIC;
            pDrvCbus->videoFormatData[j].totalEntries = length;
            pDrvCbus->videoFormatData[j].sequenceIndex = j;
            k = pDrvCbus->videoFormatData[j].numEntriesThisBurst++;
            pDrvCbus->videoFormatData[j].videoDescriptors[k].FrameSequential = VIC3DFormats[i].Fields.FrameSequential;
            pDrvCbus->videoFormatData[j].videoDescriptors[k].TopBottom = VIC3DFormats[i].Fields.TopBottom;
            pDrvCbus->videoFormatData[j].videoDescriptors[k].LeftRight = VIC3DFormats[i].Fields.LeftRight;
            if (4 == k)
                j++;
        }
    }
#else
    {
        pDrvCbus->videoFormatData[1].burstId.low = burstId_3D_VIC;
        pDrvCbus->videoFormatData[1].totalEntries = 0;
        pDrvCbus->videoFormatData[1].sequenceIndex = 1;
        pDrvCbus->videoFormatData[1].numEntriesThisBurst = 0;
    }
#endif

    //Build checksunm
    for (i = 0; i < MaxVideoEntry; i++)
    {
        ptr = (uint8_t *)&pDrvCbus->videoFormatData[i];
        checksum = 0;
        for (j = 0; j < sizeof(Mhl2VideoFormatData_t); j++)
        {
            checksum += *(ptr+j);
        }
        pDrvCbus->videoFormatData[i].checkSum = (0-checksum);
    }
}

/*****************************************************************************/
/**
 *  @brief        Returns the last VS cmd and data bytes retrieved by the CBUS ISR.
 *
 *  @param[in]        pData        pointer to return data buffer (2 bytes).
 *
 *  @return    pData[0] - VS_CMD value
 *  @return    pData[1] - VS_DATA value
 *  @return    Status
 *  @retval    true        Data is available
 *  @retval    false        Data is not available
 *
 *****************************************************************************/
bool_t SiiDrvCbusVsDataGet ( uint8_t *pData )
{
    if ( pDrvCbus->statusFlags & SiiCBUS_MSC_MSG_RCVD )
    {
        *pData++ = pDrvCbus->vsCmd;
        *pData = pDrvCbus->vsData;
        pDrvCbus->statusFlags &= ~SiiCBUS_MSC_MSG_RCVD;
        return( true );
    }
    return ( false );
}

/*****************************************************************************/
/**
 *  @brief        Returns if the peer's device capability values are ready
 *
 *  @return    peer's device capability values are ready or not
 *  @retval    true        peer's device capability values are ready
 *  @retval    false        peer's device capability values are not ready
 *
 *****************************************************************************/
bool_t  SiiDrvCbusDevCapReadyGet ()
{
    if ( pDrvCbus->statusFlags & SiiCBUS_DCAP_RDY_RECEIVED_FM_PEER )
    {
        pDrvCbus->statusFlags &= ~SiiCBUS_DCAP_RDY_RECEIVED_FM_PEER;
        return( true );
    }
    return( false );
}

/*****************************************************************************/
/**
 *  @brief        Returns if the peer has sent PATH_EN
 *
 *  @return    peer has sent PATH_EN or not
 *  @retval    true        peer has sent PATH_EN
 *  @retval    false        peer has not sent PATH_EN
 *
 *****************************************************************************/
bool_t  SiiDrvPathEnableGet ()
{
    if ( pDrvCbus->statusFlags & SiiCBUS_PATH_EN_RECEIVED_FM_PEER )
    {
        pDrvCbus->statusFlags &= ~SiiCBUS_PATH_EN_RECEIVED_FM_PEER;
        return( true );
    }
    return( false );
}

/*****************************************************************************/
/**
 *  @brief        return response from peer
 *
 *  @param[in]        pData        pointer to return data buffer (2 bytes).
 *
 *  @return    Status
 *  @retval    true        Data is available
 *  @retval    false        Data is not available
 *
 *****************************************************************************/
bool_t  SiiDrvCbusCmdRetDataGet ( uint8_t *pData )
{
    if ( pDrvCbus->statusFlags & SiiCBUS_MSC_CMD_DONE )
    {
        *pData++ = pDrvCbus->msgData0;
        *pData = pDrvCbus->msgData1;
        pDrvCbus->statusFlags &= ~SiiCBUS_MSC_CMD_DONE;
        return( true );
    }
     return ( false );
}

/*****************************************************************************/
/**
 *  @brief        Returns the last Bus Status data retrieved by the CBUS ISR.
 *
 *  @param[in]        pData        pointer to return data buffer (1 bytes).
 *
 *  @return    Status
 *  @retval    true        Data is available
 *  @retval    false        Data is not available
 *
 *****************************************************************************/
bool_t  SiiDrvCbusBusStatusGet ( uint8_t *pData )
{
    if ( pDrvCbus->statusFlags & SiiCBUS_CBUS_CONNECTION_CHG )
    {
        *pData = pDrvCbus->busConnected;
        pDrvCbus->statusFlags &= ~SiiCBUS_CBUS_CONNECTION_CHG;
        return( true );
    }
    return ( false );
}

/*****************************************************************************/
/**
 *  @brief        Returns the last Bus Status data retrieved by the CBUS ISR.
 *
 *  @param[in]        pData        pointer to return data buffer (1 bytes).
 *
 *  @return    Status
 *  @retval    true        Data is available
 *  @retval    false        Data is not available
 *
 *****************************************************************************/
bool_t  SiiDrvCbusMhlStatusGet ( uint8_t *pData )
{
    if ( pDrvCbus->statusFlags & SiiCBUS_MHL_CONNECTION_CHG )
    {
        *pData = pDrvCbus->mhlConnected;
        pDrvCbus->statusFlags &= ~SiiCBUS_MHL_CONNECTION_CHG;
        return( true );
    }
    return ( false );
}

/*****************************************************************************/
/**
 *  @brief        Returns the last DDC Abort reason received by the CBUS ISR.
 *
 *  @param[in]        pData        pointer to return data buffer (1 bytes).
 *
 *  @return    Status
 *  @retval    true        Data is available
 *  @retval    false        Data is not available
 *
 *****************************************************************************/
bool_t  SiiDrvCbusDdcAbortReasonGet ( uint8_t *pData )
{
    if ( pDrvCbus->statusFlags & SiiCBUS_DDC_ABORT )
    {
        *pData = pDrvCbus->ddcAbortReason;
        pDrvCbus->statusFlags &= ~SiiCBUS_DDC_ABORT;
        return( true );
    }
    return ( false );
}

/*****************************************************************************/
/**
 *  @brief        Returns the last MSC Abort reason received by the CBUS ISR.
 *
 *  @param[in]        pData        pointer to return data buffer (1 bytes).
 *
 *  @return    Status
 *  @retval    true        Data is available
 *  @retval    false        Data is not available
 *
 *****************************************************************************/
bool_t  SiiDrvCbusMscAbortReasonGet ( uint8_t *pData )
{
    if ( pDrvCbus->statusFlags & SiiCBUS_MSC_ABORT )
    {
        *pData = pDrvCbus->mscAbortReason;
        pDrvCbus->statusFlags &= ~SiiCBUS_MSC_ABORT;
        return( true );
    }
    return ( false );
}

/*****************************************************************************/
/**
 *  @brief        Returns the last MSC Abort reason received by the CBUS ISR.
 *
 *  @param[in]        pData        pointer to return data buffer (1 bytes).
 *
 *  @return    Status
 *  @retval    true        Data is available
 *  @retval    false        Data is not available
 *
 *****************************************************************************/
bool_t  SiiDrvCbusMscAbortResReasonGet ( uint8_t *pData )
{
    if ( pDrvCbus->statusFlags & SiiCBUS_MSC_ABORT_RES )
	{
    	*pData = pDrvCbus->mscAbortResReason;
		pDrvCbus->statusFlags &= ~SiiCBUS_MSC_ABORT_RES;
		return( true );
	}
    return ( false );
}


/*****************************************************************************/
/**
 *  @brief        Wrapper to call SiiDrvSwitchDeviceRXTermControl()
 *
 *  @param[in]        terminate        terminate cbus or not
 *
 *****************************************************************************/
void  SiiDrvCbusTermCtrl ( bool_t terminate )
{
    SiiDrvSwitchDeviceRXTermControl(terminate ? SiiTERM_MHL : SiiTERM_DISABLE );
}

/*****************************************************************************/
/**
 *  @brief        Write the specified Sideband Channel command to the CBUS.
 *                   Command can be a MSC_MSG command (RCP/MCW/RAP), or another command 
 *                   such as READ_DEVCAP, GET_VENDOR_ID, SET_HPD, CLR_HPD, etc.
 *
 *  @param[in]        pReq        Pointer to a cbus_req_t structure containing the
 *                                 command to write
 *
 *  @return    a cbus_req_t structure containing the
 *             command to write
 *  @retval    true        successful write
 *  @retval    false        write failed
 *
 *****************************************************************************/
 bool_t SiiDrvInternalCBusWriteCommand ( cbus_req_t *pReq  )
{
    uint8_t i, startbit;
    bool_t  success = true;

    DEBUG_PRINT(
        MSG_DBG, "CBUS:: Send MSC cmd %02X, %02X, %02X\n",
        (int)pReq->command, (int)pReq->offsetData, (int)pReq->msgData[0]
        );

    /****************************************************************************************/
    /* Setup for the command - write appropriate registers and determine the correct        */
    /*                         start bit.                                                   */
    /****************************************************************************************/

    // Set the offset and outgoing data byte right away
    SiiRegWrite( REG_CBUS_PRI_ADDR_CMD, pReq->offsetData);   // set offset
    SiiRegWrite( REG_CBUS_PRI_WR_DATA_1ST, pReq->msgData[0] );

    startbit = 0x00;
    switch ( pReq->command )
    {
        case MHL_SET_INT:   // Set one interrupt register = 0x60
            SiiRegWrite( REG_CBUS_PRI_ADDR_CMD, pReq->offsetData + 0x20 );   // set offset
            startbit = MSC_START_BIT_SET_INT_WRITE_STAT;
            break;

        case MHL_WRITE_STAT:    // Write one status register = 0x60 | 0x80
            SiiRegWrite( REG_CBUS_PRI_ADDR_CMD, pReq->offsetData + 0x30 );   // set offset
            startbit = MSC_START_BIT_SET_INT_WRITE_STAT;
            break;

        case MHL_READ_DEVCAP:
            startbit = MSC_START_BIT_READ_DEV_CAP_REG;
            break;

        case MHL_GET_STATE:
        case MHL_GET_VENDOR_ID:
        case MHL_SET_HPD:
        case MHL_CLR_HPD:
        case MHL_GET_SC1_ERRORCODE:      // 0x69 - Get channel 1 command error code
        case MHL_GET_DDC_ERRORCODE:      // 0x6A - Get DDC channel command error code.
        case MHL_GET_MSC_ERRORCODE:      // 0x6B - Get MSC command error code.
        case MHL_GET_SC3_ERRORCODE:      // 0x6D - Get channel 3 command error code.
            SiiRegWrite( REG_CBUS_PRI_ADDR_CMD, pReq->command );
            startbit = MSC_START_BIT_MSC_CMD;
            break;

        case MHL_MSC_MSG:
            SiiRegWrite( REG_CBUS_PRI_WR_DATA_2ND, pReq->msgData[1] );
            SiiRegWrite( REG_CBUS_PRI_ADDR_CMD, pReq->command );
            DEBUG_PRINT( MSG_DBG, "CBUS:: MSG_MSC CMD:    0x%02X\n", (int)pReq->command );
            DEBUG_PRINT( MSG_DBG, "CBUS:: MSG_MSC Data 0: 0x%02X\n", (int)pReq->msgData[0] );
            DEBUG_PRINT( MSG_DBG, "CBUS:: MSG_MSC Data 1: 0x%02X\n", (int)pReq->msgData[1] );
            startbit = MSC_START_BIT_MSC_MSG_CMD;
            break;

        case MHL_WRITE_BURST:
            SiiRegWrite( REG_CBUS_PRI_ADDR_CMD, pReq->offsetData + 0x40);
            SiiRegWrite( REG_MSC_WRITE_BURST_LEN, pReq->length - 1);

            // Now copy all bytes from array to local scratchpad

            for ( i = 0; i < pReq->length; i++ )
            {
                SiiRegWrite( REG_CBUS_SCRATCHPAD_0 + i, pReq->msgData[i] );
            }
            startbit = MSC_START_BIT_WRITE_BURST;
            break;

        default:
            success = false;
            break;
    }

    /****************************************************************************************/
    /* Trigger the CBUS command transfer using the determined start bit.                    */
    /****************************************************************************************/

    if ( success )
    {
        SiiRegWrite( REG_CBUS_PRI_START, startbit );
    }

    return( success );
}


/*****************************************************************************/
/**
 *  @brief        Check CBUS registers for a CBUS event
 *
 *****************************************************************************/
bool_t SiiDrvCbusProcessInterrupts()
{
    uint8_t intStatus0, intStatus1, intStatusTemp;

    // Read CBUS interrupt status. Return if nothing happening on the interrupt front
    intStatus0 = SiiRegRead( REG_CBUS_INTR_0 );
    intStatus1 = SiiRegRead( REG_CBUS_INTR_1 );

    // mask out the interrupts that we don't care about
    intStatus1 &= ~(BIT_HEARTBEAT_TIMEOUT | BIT_SET_CAP_ID_RSVD | BIT_CBUS_PKT_RCVD | BIT_CEC_ABORT);

    if(!(intStatus0 || intStatus1))
        return false;
    // clear the interrupts
    SiiRegWrite( REG_CBUS_INTR_0, intStatus0 );
    SiiRegWrite( REG_CBUS_INTR_1, intStatus1 );
    DEBUG_PRINT( MSG_DBG, "CBUS:: INTR_0:: %02X -- INTR_1:: %02X\n", (int)intStatus0, (int)intStatus1);

    // An interrupt occurred, save the status.
    pDrvCbus->statusFlags |= SiiCBUS_INT ;

    if( intStatus0 )
    {
        if ( intStatus0 & BIT_MSC_CMD_DONE_WITH_NACK )
        {
            pDrvCbus->statusFlags |= SiiCBUS_NACK_RECEIVED_FM_PEER;
        }

        if ( intStatus0 & BIT_MSC_SET_INT_RCVD )
        {
            intStatusTemp = SiiRegRead( REG_CBUS_SET_INT_0 );
            if( intStatusTemp & BIT0 )
            {
                pDrvCbus->statusFlags |= SiiCBUS_DCAP_CHG_RECEIVED_FM_PEER;
            }
            if ( intStatusTemp & BIT1 )
            {
                pDrvCbus->statusFlags |= SiiCBUS_SCRATCHPAD_WRITTEN_BY_PEER;
            }
            if( intStatusTemp & BIT2 )
            {
                pDrvCbus->statusFlags |= SiiCBUS_REQ_WRT_RECEIVED_FM_PEER;
            }
            if( intStatusTemp & BIT3 )
            {
                pDrvCbus->statusFlags |= SiiCBUS_GRT_WRT_RECEIVED_FM_PEER;
            }
            if( intStatusTemp & BIT4 )
            {
                pDrvCbus->statusFlags |= SiiCBUS_3D_REQ_RECEIVED_FM_PEER;
            }
            SiiRegWrite( REG_CBUS_SET_INT_0, intStatusTemp );   // Clear received interrupts
        }

        // This step is redundant as we do get DSCR_CHG interrupt up there
        if ( intStatus0 & BIT_MSC_WRITE_BURST_RCVD )
        {
            pDrvCbus->statusFlags |= SiiCBUS_SCRATCHPAD_WRITTEN_BY_PEER;
        }

        // Get any VS or MSC data received
        if ( intStatus0 & BIT_MSC_MSG_RCVD )
        {
            pDrvCbus->statusFlags |= SiiCBUS_MSC_MSG_RCVD;
            pDrvCbus->vsCmd  = SiiRegRead( REG_CBUS_PRI_VS_CMD );
            pDrvCbus->vsData = SiiRegRead( REG_CBUS_PRI_VS_DATA );
        }

        if ( intStatus0 & BIT_MSC_WRITE_STAT_RCVD )
        {
            // see if device capability values are changed
            intStatusTemp = SiiRegRead( REG_CBUS_WRITE_STAT_0 );
            if( intStatusTemp & BIT0 )
            {
                pDrvCbus->statusFlags |= SiiCBUS_DCAP_RDY_RECEIVED_FM_PEER;
            }

            intStatusTemp = SiiRegRead( REG_CBUS_WRITE_STAT_1 );
            if( intStatusTemp & BIT3 )
            {
                pDrvCbus->statusFlags |= SiiCBUS_PATH_EN_RECEIVED_FM_PEER;
            }
        }

        if ( intStatus0 & BIT_MSC_CMD_DONE )
        {
            pDrvCbus->statusFlags |= SiiCBUS_MSC_CMD_DONE;
            pDrvCbus->msgData0  = SiiRegRead( REG_CBUS_PRI_RD_DATA_1ST );
            pDrvCbus->msgData1  = SiiRegRead( REG_CBUS_PRI_RD_DATA_2ND );
        }

        // CBUS connection status has changed
        if ( intStatus0 & BIT_CONNECT_CHG )
        {
            pDrvCbus->statusFlags |= SiiCBUS_CBUS_CONNECTION_CHG;
            pDrvCbus->busConnected = SiiRegRead( REG_CBUS_BUS_STATUS ) & BIT_BUS_CONNECTED;
        }
    }

    if(intStatus1)
    {
        if ( intStatus1 & BIT_DDC_ABORT )
        {
            pDrvCbus->ddcAbortReason = SiiRegRead( REG_DDC_ABORT_REASON );
            pDrvCbus->statusFlags |= SiiCBUS_DDC_ABORT;
            SiiRegWrite( REG_DDC_ABORT_REASON, pDrvCbus->ddcAbortReason );
        }

        // MSC_ABORT happened as Responder
        if ( intStatus1 & BIT_MSC_ABORT_RES )
        {
            pDrvCbus->mscAbortResReason = SiiRegRead( REG_MSC_ABORT_RES_REASON );
            pDrvCbus->statusFlags |= SiiCBUS_MSC_ABORT_RES;
            SiiRegWrite( REG_MSC_ABORT_RES_REASON, pDrvCbus->mscAbortResReason );
        }

        // MSC_ABORT happened at this device itself
        if ( intStatus1 & BIT_MSC_ABORT )
        {
            pDrvCbus->mscAbortReason = SiiRegRead( REG_MSC_ABORT_REASON );
            pDrvCbus->statusFlags |= SiiCBUS_MSC_ABORT;
            SiiRegWrite( REG_MSC_ABORT_REASON, pDrvCbus->mscAbortReason );
        }

        // MHL connection status has changed
        if ( intStatus1 & BIT_MHL_CONNECT_CHG )
        {
            pDrvCbus->statusFlags |= SiiCBUS_MHL_CONNECTION_CHG;
            pDrvCbus->mhlConnected =  SiiRegRead( REG_CBUS_BUS_STATUS ) & BIT_MHL_CONNECTED;
        }
    }

    return true;
}

/*****************************************************************************/
/**
 *  @brief        Initialize the CBUS hardware for the current instance.
 *
 *  @return    Status
 *  @retval    true        Success
 *  @retval    false        Failure
 *
 *  @note: Requires that SiiDrvCbusInstanceSet() is called prior to this call
 * 
 *****************************************************************************/
 bool_t SiiDrvCbusInitialize ( void )
{
    uint_t  index;

    SiiRegModify(RX_A__SWRST2, RX_M__SWRST2__CBUS_SRST, SET_BITS);
    SiiRegModify(RX_A__SWRST2, RX_M__SWRST2__CBUS_SRST, CLEAR_BITS);

    memset( pDrvCbus, 0, sizeof( CbusDrvInstanceData_t ));

    // Setup local DEVCAP registers for read by the peer
    for ( index = 0; index < (sizeof( cbusInitCbusRegsList) / 2); index += 2 )
    {
        SiiRegWrite( cbusInitCbusRegsList[ index], cbusInitCbusRegsList[ index + 1] );
    }

    // Audio link mode update by switch status
    if ( SiiSpdifEnableGet() || !SiiTdmEnableGet() )
    {
        SiiRegWrite(REG_CBUS_DEVICE_CAP_6, MHL_AUD_LINK_MODE_2CH);
    }
    
    // Enable the VS commands, all interrupts, and clear legacy
    SiiRegWrite( REG_CBUS_INTR_0_MASK, 0xFF );      // Enable desired interrupts
    SiiRegWrite( REG_CBUS_INTR_1_MASK, 0xCC );      // Enable desired interrupts
    SiiRegWrite( RX_CBUS_CH_RST_CTRL, 0x00 );       // MHL: Tri-state CBUS

#ifdef MHAWB_SUPPORT
    SiiDrvHawbEnable(false);
#endif
    return( true );
}


