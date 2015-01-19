/******************************************************************************
 *
 * Copyright 2008, Silicon Image, Inc.  All rights reserved.
 * No part of this work may be reproduced, modified, distributed, transmitted,
 * transcribed, or translated into any language or computer format, in any form
 * or by any means without written permission of: Silicon Image, Inc., 1060
 * East Arques Avenue, Sunnyvale, California 94085
 *
 *****************************************************************************/
/**
 * @file infofrm.c
 *
 * This is a description of the file.
 *
 * $Author: $Vladimir Grekhov
 * $Rev: $
 * $Date: $ 9-14-09
 *
 *****************************************************************************/

/***** #include statements ***************************************************/
#include <stdio.h>
#include <string.h>
#include <local_types.h>
#include "spec_types.h"
#include "config.h"
#include "registers.h"  /*  defines receiver's chip registers                       */
#include "amf.h"        /*  defines accessor functions receiver's chip registers    */
#include "hal.h"        /*  macro with printf                                       */
#include "infofrm.h"

#if (CONF__SUPPORT_3D == ENABLE)

/***** local macro definitions ***********************************************/
#define SPD_CAPTURE_TO          1250
#define VSIF_LONG_CAPTURE_TO    1300
#define ISRC1_CAPTURE_TO        500
#define TWO_VIDFR_CAPTURE_TO    300//150//36
#define SPD_ISRC1_CAPTURE_TO    500
#define TIME_WO_ACP_IF          2000//600
#define ACP_CAPTURE_200MS       600//200
#define MAX_NUM_IF              3

#define IFBUFF_SIZE 32

#define SET_CLR_IF() (RegisterWrite( REG__INFM_CLR, 0x7F)) //clear verything
#define SET_PACKET_INTR_MASK() (RegisterWrite( REG__INTR3_UNMASK, 0x0B))
//0x0B =  BIT__NEW_AVI_INF|BIT__NEW_SPD_INF|BIT__NEW_MPEG_INF) 



/***** local type definitions ************************************************/
typedef uint16_t DevAddr_t;

typedef struct _IfInsCtrl_t {
    uint16_t    wTo;
    uint8_t     bBuffType;
    bool_t      fReceived;
    bool_t      fFailed;
} IfInsCtrl_t;

typedef enum {
    SelVsif
    ,SelIsrc1
    ,SelIsrc2
    ,SelSpd
    ,SelAcp
    ,MaxNumIfSel
} SelectIf_t;


typedef struct _IfCtrl_t {

    IfInsCtrl_t Ins[MaxNumIfSel];
    uint8_t     abBuff[IFBUFF_SIZE];
    uint8_t     abVsif[IF_VSIF_SIZE];
    uint8_t     abAcp[IF_ACP_SIZE];
    uint16_t    wNoAcpTotTime;
    uint8_t     bDecodeTypeMpegBuffer;
    uint8_t     bDecodeTypeSpdBuffer;
    uint8_t     bVidModeId; /* Video Mode Id aquired from Avi */

    bool_t      fAcpIntr;
    bool_t      fSpdCaptured;
    bool_t      fIsrc1Captured;
    bool_t      fSpdDecodeIsrc2;
    bool_t      fIntrOnEveryNewAvi;
    bool_t      fIntrOnEveryNewAud;
    bool_t      fIntrOnEveryNewGamut;
    bool_t      fVsifIsPresent;
    bool_t      fAcpIsPresent;
    bool_t      fMpegBuff_IsrcNotFound;
    /*
        This state is important to track changes of mpeg info frame buffer.
        When Vsif is received and Isrc capturing period is missed, when we go back
        to receive Vsif, if data weren't changed we will not receive an Vsif
        interrupt. Vsif wii receive Time Out and without acctionans applicable
        to Vsif we should set Isrc captuting again.
    */
    bool_t      fDebug;
}IfCtrl_t;

typedef struct _IfFlafs_t {

    uint8_t     bNewIfs;
    uint8_t     bChangedIfs;
    uint8_t     bStoppedIfs;

}IfFlafs_t;

/* info frames, check sum property */

typedef enum {
    IfWCheckSum         /*  generic info frame check sum (Avi, Aud, Spd, Vsif)  */
    ,IfWGamutCheckSum   /*  info frame check sum of gamut packet                */
    ,IfWOCheckSum       /*  info frame without check sum (Acp, Isrc1/2          */
} eIfCheckSumProp_t;



/***** local variable declarations *******************************************/
/**
 * @brief Typically this should do.
 */
const uint8_t IEEERegistrationId[] = { 0x03, 0x0C, 0x00 };

static IfCtrl_t IfCtrl;


/***** local function prototypes *********************************************/

#if (CONF__SUPPORT_REPEATER3D == ENABLE)
static uint8_t GetIfTypeFromUnreqBuffer ( void );
#endif
static void SetDecodeIfBuffers ( const DevAddr_t DevAddr, const uint8_t bDecodeIfType );
static void SetIfTo ( SelectIf_t eSelectIf, const uint16_t wTo );
static bool_t GetIf( const uint8_t bIfType );
static bool_t GetIntrAcpStatus ( void );
static void SetIntrAcp ( const bool_t fEnable );
static void SetIntrOnEveryNewIf ( const bool_t fEnable, const uint8_t bIfType );
static void SetToIfBuff ( const uint8_t bIfType );
static void print_tabl (    char const *pMesg,
                            void const *pData,
                            uint16_t wCnt,
                            uint8_t bCol );
static void AviParser ( uint8_t const *pPayload );
static void VsifParser ( uint8_t const *pPayload );

static bool_t ProcIf ( DevAddr_t DevAddr, const uint8_t bIfType, uint8_t bSize );
/***** local functions *******************************************************/
#if (CONF__SUPPORT_REPEATER3D == ENABLE)
static uint8_t GetIfTypeFromUnreqBuffer ( void ){

    return RegisterRead( REG__UNREQ_TYPE );
}
#endif
#if (CONF__DEBUG_PRINT == ENABLE)
static uint8_t GetMpegDecodeAddr ( void ){
    return RegisterRead( REG__MPEG_DECODE);
}
#endif
/*****************************************************************************/
/**
 *  The description of the function print_tabl().
 *
 *  @param[in]  char const *pMesg   - label being print out
 *  @param[in]  void const *pData   - data being print out
 *  @param[in]  uint16_t    wCnt    - number bytes to be print out
 *  @param[in]  uint8_t     bCol    - number collons to print
 *
 *  @return     none.
 *  @retval     void
 *
 *****************************************************************************/
static void print_tabl ( char const *pMesg, void const *pData, uint16_t wCnt, uint8_t bCol ){
uint32_t i;
uint8_t const *pbData;

    pbData = (uint8_t *)pData;
    DEBUG_PRINT (("\n%s", pMesg ));

    if ( pbData != NULL ){
        for ( i = 0; i < wCnt; i++ ){
            if ( i%bCol == 0 ){
                DEBUG_PRINT (( "\n0x%X: ", (uint32_t) &pbData[i]));
            }
            DEBUG_PRINT (( " 0x%02X", (uint16_t)pbData[i] ));
        }
        DEBUG_PRINT (( "\n"));
    }
    else {
        DEBUG_PRINT (( "\nNULL pointer"));
    }
    DEBUG_PRINT (( "\n"));
}

static void print_mpegbuffer( void ){
uint8_t abData[16];

    RegisterReadBlock( REG__MPEG_TYPE, abData, 16 );
    print_tabl ( "Mpeg_buf", abData, 16, 8 );

}

/*****************************************************************************/
/**
 *  The description of the function AviParser().
 *  Displays parsed information
 *  @param[in]  uint8_t const *pPaylaod   - Info frame paylaod
 *
 *  @return     none.
 *  @retval     void
 *
 *****************************************************************************/
static void AviParser ( uint8_t const *pPayload ){

    DEBUG_PRINT (("Parsing Avi: \n "));
    /* check for Video Id Code*/
    IfCtrl.bVidModeId = (pPayload[IF_AVI_VIC_ADDR] & IF_AVI_VIC_SEL );
    DEBUG_PRINT (("Vic: "));
    switch (IfCtrl.bVidModeId){
        case   0: DEBUG_PRINT (("Not valid ID"));
                break;
          default:                DEBUG_PRINT (( "%d", (uint16_t) IfCtrl.bVidModeId ));
    }
    DEBUG_PRINT ( ("\n Exten. Colorimetry Info: " ));
    switch (pPayload[IF_AVI_EC_ADDR] & ExtnColorimSel){
        case xvYCC601:      DEBUG_PRINT (("xvYCC601"));
            break;
        case xvYCC709:      DEBUG_PRINT (("xvYCC709"));
            break;
        case cYCC601:       DEBUG_PRINT (("cYCC601"));
            break;
        case AdobeYCC601:   DEBUG_PRINT (("AdobeYCC601"));
            break;
        case AdobeRGB:      DEBUG_PRINT (("AdobeRGB"));
            break;
        default:            DEBUG_PRINT (("Res"));
    }
    DEBUG_PRINT ( ("\n Cont Type: " ));
    switch (pPayload[IF_AVI_CN_ADDR] & ContTypeSel ){
        case Graphics:
            if ( pPayload[IF_AVI_ITC_ADDR] & ITContSel ){
                DEBUG_PRINT (("Graphics"));
            }
            else {
                DEBUG_PRINT (("No Data"));
            }
            break;
        case Photo:     DEBUG_PRINT (("Photo"));
            break;
        case Cinema:    DEBUG_PRINT (("Cinema"));
            break;
        case Game:      DEBUG_PRINT (("Game"));
            break;
    }
}
/*****************************************************************************/
/**
 *  The description of the function VsifParser().
 *  Displays parsed information
 *  @param[in]  uint8_t const *pPaylaod   - Info frame paylaod
 *
 *  @return     none.
 *  @retval     void
 *
 *****************************************************************************/


static void VsifParser ( uint8_t const *pPayload ){

    DEBUG_PRINT (("Parsing Vsif: "));
    /* Check IEEE Registreation Id */
    if ( !memcmp ( &pPayload[IF_VSIF_IEEE_REGID_ADDR], IEEERegistrationId , sizeof(IEEERegistrationId) )){
        /* Check Hdmi Format for 3D*/
        if ( (pPayload[IF_VSIF_HDMI_FORMAT_ADDR] & IF_VSIF_HDMI_FORMAT_SEL) == IF_HDMI_FORMAT_3D ){
            DEBUG_PRINT (("\nVsif 3D: "));
            switch ( pPayload[IF_VSIF_3D_STRUCT_ADDR] >> IF_VSIF_3DSTRUCT_SEL ){
                case FramePacking:      DEBUG_PRINT (("FramePacking"));
                    break;
                case FrameAlternative:  DEBUG_PRINT (("FrameAlternative"));
                    break;
                case LineAlternative:   DEBUG_PRINT (("LineAlternative"));
                    break;
                case SiedBySideFull:    DEBUG_PRINT (("SiedBySideFull"));
                    break;
                case LDepth:            DEBUG_PRINT (("LDepth"));
                    break;
                case LDepthGraphGraphDepth: DEBUG_PRINT (("LDepthGraphGraphDepth"));
                    break;
                case SideBySideHalf:        DEBUG_PRINT (("SideBySideHalf"));
                    break;
                default:                    DEBUG_PRINT (("Reserved")); //yma change to reserved
            }
		DEBUG_PRINT ((" \n"));			
        }
        else {
            DEBUG_PRINT (("\nVsif not 3D"));
        }
    }
    else {
        DEBUG_PRINT (("\nVsif IEEE Reg. Id is not matched"));
    }
}

/*****************************************************************************/
/**
 *      The description of the function SetDecodeIfBuffers ()
 *
 *  @param[in]  DevAddr Rx Chip leaner address
 *  @param[in]  Rx Chip, decode info frame type
 *
 *  @return     none
 *  @retval     void
 *
 *****************************************************************************/
static void SetDecodeIfBuffers ( const DevAddr_t DevAddr, const uint8_t bDecodeIfType ){
    RegisterWrite( DevAddr, bDecodeIfType );
    if ( REG__MPEG_DECODE == DevAddr ){
        IfCtrl.bDecodeTypeMpegBuffer = bDecodeIfType;
    }
    else {
        IfCtrl.bDecodeTypeSpdBuffer = bDecodeIfType;
    }
}

static void SetIfTo ( SelectIf_t eSelectIf, const uint16_t wTo ){
    IfCtrl.Ins[eSelectIf].wTo = wTo;
}

/*****************************************************************************/
/**
 *  The description of the function SetToIfBuff().
 *
 *  @param[in] bIfType  InfoFrame type
 *
 *  @return             none
 *  @retval             void
 *
 *****************************************************************************/
 static void SetToIfBuff ( const uint8_t bIfType ){

    switch( bIfType ){
        case IF_AVI_ID:
            break;
        case IF_VSIF_ID:
            SetDecodeIfBuffers ( REG__MPEG_DECODE,  IF_ISRC1_ID );
            IfCtrl.Ins[ SelIsrc1 ].wTo = VSIF_LONG_CAPTURE_TO;
             /* this time out is used with shared info frame buffer, so cannot set the same time */
            IfCtrl.Ins[ SelVsif ].wTo = 0;
            if ( !IfCtrl.fSpdDecodeIsrc2 ){
                IfCtrl.Ins[ SelIsrc1 ].wTo = 0;
            }
            break;
        case IF_ISRC1_ID:
            if ( !IfCtrl.fSpdDecodeIsrc2 ){ /* mpeg buffer is used for Isrc1/Isrc2/Vsif */
                IfCtrl.Ins[ SelIsrc2 ].wTo  = VSIF_LONG_CAPTURE_TO;
                IfCtrl.Ins[ SelIsrc1 ].wTo  = 0;
                IfCtrl.Ins[ SelVsif ].wTo   = 0;
                SetDecodeIfBuffers ( REG__MPEG_DECODE,  IF_ISRC2_ID );
            }
            else { /* mpeg buffer is used for Isrc1 Vsif */
                IfCtrl.Ins[ SelVsif ].wTo = TWO_VIDFR_CAPTURE_TO;
                 /* this time out is used with shared info frame buffer, so cannot set the same time*/
                IfCtrl.Ins[ SelIsrc1 ].wTo = 0;
                print_mpegbuffer();
                SetDecodeIfBuffers ( REG__MPEG_DECODE,  IF_VSIF_ID );
                IfCtrl.fDebug = 1;
            }
            break;
        case IF_ISRC2_ID:
            if ( !IfCtrl.fSpdDecodeIsrc2 ){
                IfCtrl.Ins[ SelVsif ].wTo = TWO_VIDFR_CAPTURE_TO;
                IfCtrl.Ins[ SelIsrc1 ].wTo  = 0;
                IfCtrl.Ins[ SelIsrc2 ].wTo = 0;
                SetDecodeIfBuffers ( REG__MPEG_DECODE,  IF_VSIF_ID );
            }
            break;
        case IF_SPD_ID:
            IfCtrl.Ins[ SelIsrc2].wTo = 0;
            SetDecodeIfBuffers ( REG__SPD_DECODE,  IF_ISRC2_ID );
            break;
        case IF_ACP_ID:
            IfCtrl.wNoAcpTotTime    = TIME_WO_ACP_IF;
            /* TODO disable Acp interrupt */
            /* set time out*/
            IfCtrl.Ins[ SelAcp].wTo = ACP_CAPTURE_200MS;
            break;
        default: 
			;
    }
 }


static bool_t GetIfRoll( DevAddr_t DevAddr, uint8_t *pbRetIfType ){
uint8_t bSize;
bool_t fValid = TRUE;

    *pbRetIfType = RegisterRead( DevAddr );

    switch ( *pbRetIfType ){
        case IF_SPD_ID:
                        bSize   =   IF_SPD_SIZE;
                        break;
        case IF_VSIF_ID:
                        bSize   =   IF_VSIF_SIZE;
                        break;
        case IF_ISRC1_ID:
        case IF_ISRC2_ID:
                        bSize   =   IF_ISRC_SIZE;
                        break;
        default:
            fValid = FALSE;
    }
    if ( fValid ){
        fValid = ProcIf ( DevAddr, *pbRetIfType, bSize );
    }

    return fValid;
}
/*****************************************************************************/
/**
 *      The description of the function ProcIf()
 *
 *  @param[in]  DevAddr         - device address
 *  @param[in]  bIfType         - info frame type
 *  @param[in]  bSize           - size of infoframe
 *
 *  @return     bool_t
 *  @retval     TRUE for a valid packet
 *
 *****************************************************************************/
bool_t ProcIf ( DevAddr_t DevAddr, const uint8_t bIfType, uint8_t bSize ){
bool_t fValid = FALSE;
uint8_t i;
uint8_t bAuxSize = 0;
uint8_t CheckSum = 0;
//uint8_t NumMissed;
/*
Checksum [1 byte] Checksum of infoframe. The checksum shall be calculated such that byte-sum
of all three bytes of the Packet Header and all valid uint8_ts of the InfoFrame Packet contents
(determined by InfoFrame Length), plus check sum itself, equals zero
*/


    if ( !(bIfType & 0x80) ){
        RegisterReadBlock( DevAddr, IfCtrl.abBuff, bSize );
        print_tabl ( "** IF Data", IfCtrl.abBuff, bSize, 8);
    }
    else {
        RegisterReadBlock( DevAddr, IfCtrl.abBuff, IF_HEADER_SIZE );
        if ( IfCtrl.abBuff[ IF_LENGTH_ADDR] <= bSize ){
            bAuxSize = IfCtrl.abBuff[ IF_LENGTH_ADDR];
            DevAddr += IF_HEADER_SIZE; /* first byte is check sum */
            RegisterReadBlock( DevAddr, &IfCtrl.abBuff[IF_HEADER_SIZE], bSize + 1);
            bAuxSize += (IF_HEADER_SIZE + 1); /* header excludes check sum which in counted by adding 1, payload if following of check sum */
            if ( bIfType == IF_VSIF_ID ){
                if ( !memcmp ( IfCtrl.abBuff, IfCtrl.abVsif, bAuxSize ) ){
                    DEBUG_PRINT (( "\n** Same Vsif\n"));
                }
                else {
                    print_tabl ( "** Got new Vsif", IfCtrl.abBuff, IfCtrl.abBuff[IF_LENGTH_ADDR], 8);
                    memcpy ( IfCtrl.abVsif, IfCtrl.abBuff, bSize + IF_HEADER_SIZE );
                    for ( i = 0; i < bAuxSize; i++ ){
                        CheckSum += IfCtrl.abBuff[i];
                    }
                    DEBUG_PRINT (( "\n** IF 0x%2X Checksum: ", (uint16_t) bIfType));
                    if ( !CheckSum ){ /* */
                        DEBUG_PRINT (( "OK\n" ));
                        VsifParser( &IfCtrl.abBuff[IF_PAYLOAD_ADDR] );
                        fValid = TRUE;
                    }
                    else {
                        DEBUG_PRINT (( "Failed\n" ));
                    }
                    IfCtrl.fVsifIsPresent = TRUE;
                }
            }
            else if ( bIfType == IF_GMT_ID ){

            }
            else {
                print_tabl ( "** IF Data", IfCtrl.abBuff, IfCtrl.abBuff[IF_LENGTH_ADDR], 8);
                for ( i = 0; i < bAuxSize; i++ ){
                    CheckSum += IfCtrl.abBuff[i];
                }
                DEBUG_PRINT (( "** IF 0x%2X Checksum: ", (uint16_t) bIfType));
                if ( !CheckSum ){ /**/
                    fValid = TRUE;
					DEBUG_PRINT (( "OK\n" ));
                    if ( bIfType == IF_AVI_ID ){
                        AviParser( &IfCtrl.abBuff[IF_PAYLOAD_ADDR] );
                    }                 
                }
                else {
                    DEBUG_PRINT (( "Failed\n" ));
                }
            }
        }
        else {
            DEBUG_PRINT (( "\n** GetIf: wrong parametr in info frame size\n"));
        }
    }

    return fValid;
}

/*****************************************************************************/
/**
 *      The description of the function GetIf()
 *
 *  @param[in]  bIfType         - info frame type
 *  @param[in]  bBufferType     - info frame buffer (Isrc can use a differnt buffer)
 *
 *  @return     bool_t
 *  @retval     TRUE for a valid packet
 *
 *****************************************************************************/

static bool_t GetIf( const uint8_t bIfType ){
bool_t fValid = TRUE;
DevAddr_t   DevAddr =   0;
uint8_t     bSize   =   0;

    switch ( bIfType ){

        case IF_VSIF_ID:
                        bSize   =   IF_VSIF_SIZE;
                        DevAddr =   REG__MPEG_TYPE;
                        break;
        case IF_AVI_ID:
                        bSize   =   IF_AVI_SIZE;
                        DevAddr =   REG__AVI_TYPE;
                        break;
        case IF_AUD_ID:
                        bSize   =   IF_AUD_SIZE;
                        DevAddr =   REG__AUD_TYPE;
                        break;
        case IF_SPD_ID:
                        bSize   =   IF_SPD_SIZE;
                        DevAddr =   REG__SPD_TYPE;
                        break;
        case IF_GMT_ID:
                        bSize   =   IF_GMT_SIZE;
                        DevAddr =   REG__GMT_TYPE;
                        break;
        case IF_ACP_ID:
                        bSize   =   IF_ACP_SIZE;
                        DevAddr =   REG__ACP_TYPE;
                        break;
        default:
                        DEBUG_PRINT (( "\n** GetIf: unsupported type\n"));
                        fValid = FALSE;
    }
    if ( fValid ){
        fValid = ProcIf ( DevAddr, bIfType, bSize );
    }

    return fValid;
}

/*****************************************************************************/
/**
 *      The description of the function GetIntrAcpStatus ()
 *      This function checks status of Acp interrupt, it is used in polling manner
 *
 *  @param[in/out]  none
 *
 *  @return     bool_t
 *  @retval     TRUE for a valid packet
 *
 *****************************************************************************/
static bool_t GetIntrAcpStatus ( void ){
bool_t fStatus = FALSE;

    if ( RegisterRead( REG__INTR6) & BIT__NEW_ACP_PKT ){
        RegisterWrite( REG__INTR6,  BIT__NEW_ACP_PKT );
        fStatus = TRUE;
    }
    else {
        fStatus = FALSE;
    }
    return fStatus;
}
/*****************************************************************************/
/**
 *      The description of the function SetIntrAcp ()
 *      This function sets or clears Acp interrupt mode. When Acp packets
 *      are received at required time frame check is done using Acp time out
 *      by polling an interrupt status
 *
 *  @param[in]  fEnable - enab;es or disables Acp interrupts
 *
 *  @return     none
 *  @retval     void
 *
 *****************************************************************************/
static void SetIntrAcp ( const bool_t fEnable ){

    if ( fEnable ){
        IfCtrl.fAcpIntr = TRUE;
        RegisterModify( REG__INTR6_UNMASK, BIT__NEW_ACP_PKT, BIT__NEW_ACP_PKT );
    }
    else {
        IfCtrl.fAcpIntr = FALSE;
        RegisterModify( REG__INTR6_UNMASK, BIT__NEW_ACP_PKT, 0 );
    }

}
/*****************************************************************************/
/**
 *      The description of the function SetIntrOnEveryNewIf()
 *      This function enables or disables interrupt on every new info frame packet
 *
 *  @param[in]  fEnable - Enables interrupt on every new info packet
 *  @param[in]  bIfType - Type of info frame packet
 *
 *  @return     none
 *  @retval     void
 *
 *****************************************************************************/
static void SetIntrOnEveryNewIf ( const bool_t fEnable, const uint8_t bIfType ){
uint8_t     bBitField;
bool_t      fError = FALSE;

    if ( bIfType == IF_AVI_ID ){
        bBitField = BIT__NEW_AVI_CTRL_INF;
    }
    else if ( bIfType == IF_GMT_ID ){
        bBitField = BIT__NEW_GMT_CTRL_INF;
    }
    else if ( bIfType == IF_AUD_ID ){
        bBitField = BIT__NEW_AUD_CTRL_INF;
    }
    else if ( bIfType == IF_ACP_ID ){
        bBitField = BIT__NEW_ACP_CTRL_INF;
    }
    else {
        DEBUG_PRINT (( "\n** SetIntrOnNewIf: wrong parametr\n"));
        fError = TRUE;
    }
    if ( !fError ){
        if ( fEnable ){
            RegisterModify( REG__INTR_IF_CTRL, bBitField, bBitField );
        }
        else {
            RegisterModify( REG__INTR_IF_CTRL, bBitField, 0 );
        }
    }
}

 /***** public functions ******************************************************/


/*****************************************************************************/
/**
 *  The description of the function HdmiInitIf(). This function intializes Info Frame
 *  related registers and data structures. Function calls only once
 *
 *  @param[in,out]      none
 *
 *  @return             none
 *  @retval             void
 *
 *****************************************************************************/

void HdmiInitIf ( void ){


    /* clears IfCtrl structure */
    memset ( &IfCtrl, 0, sizeof (IfCtrl_t) );
    SET_CLR_IF(); /* set clear info frame data when rx stops receiving info frames
                    use for Mpeg (Isrc1/Isrc2/Vsif) buffer, Acp, Gdp*/

/*******************************************************************************


ACP     ACP
            This buffer used for ACP
            Started with Acp interrupt, whe Acp packet received, start polling
            to exclude

SPD SPD/ISRC2
            After receiving first SPD packet usage configured on receiving ISRC2.
            Also consider if SPD wasn't received in 1.25 sec,
            automatically switch to ISRC2 mode

MPEG ISRC1/VS
            Check for VSIF every second. When new AVI is detected set 1 sec window for
            VSIF.

UNREQ
            IRSC1/ISRC2 - receive
            VSIF        - detect, use MPEG buffer

*******************************************************************************/
    /*
        Set decode addreses: only ACP/SPD/MPEG buffers have programmable
        decode addresses
    */
    SetDecodeIfBuffers ( REG__MPEG_DECODE,  IF_ISRC1_ID );
    SetDecodeIfBuffers ( REG__SPD_DECODE,   IF_SPD_ID );
    /*
        Set info frame time outs: depending on requests after reaching time outs
        will be changed functionality of particular info frame buffer
    */
    /*
        After reaching SPD time out, this info frame buffer will be used for
        other info frames as ISRC1/2
    */

    /*  Time out is set in ms    */
    SetIfTo ( SelSpd,    SPD_CAPTURE_TO );
    SetIfTo ( SelAcp,    0 );
    SetIfTo ( SelVsif,   0 );
    SetIfTo ( SelIsrc1,  ISRC1_CAPTURE_TO );
    /*
        SPD_BUFF:   [SPD][ISRC2 when SPD captured or time outed]
        MPEG_BUFF:  [ISRC1][VSIF][ISRC2 if SPD_BUFF is SPD][ISRC1][VSIF]
    */
    IfCtrl.fAcpIntr = TRUE;		 
	SET_PACKET_INTR_MASK();
	

//    IfInitIfTest();
}

/*****************************************************************************/
/**
 *  The description of the function HdmiProcIfTo().
 *
 *  @param[in] wToStep  Info Frame elapsed time from previous to call
 *                      of this function ( measured in MS)
 *
 *  @return             none
 *  @retval             void
 *
 *****************************************************************************/
void HdmiProcIfTo ( uint16_t wToStep  ){
SelectIf_t id;

    if ( IfCtrl.fAcpIsPresent ){
        IfCtrl.wNoAcpTotTime += wToStep;
    }
    for ( id = 0; id < MaxNumIfSel; id++ ){
        if ( IfCtrl.Ins[id].wTo ){
            if ( IfCtrl.Ins[id].wTo > wToStep ){
                IfCtrl.Ins[id].wTo -= wToStep;
            }
            else {
                IfCtrl.Ins[id].wTo = 0;
            }
            if ( !IfCtrl.Ins[id].wTo ) { /* serve time outs */
                switch ( id ){
                    case SelVsif:
                                    if ( IfCtrl.fVsifIsPresent && (!IfCtrl.fMpegBuff_IsrcNotFound) ){
                                        IfCtrl.fVsifIsPresent = FALSE;
                                        memset ( IfCtrl.abVsif, 0, IF_VSIF_SIZE );
                                        IfCtrl.fDebug = 0;
                                        print_mpegbuffer();
                                        DEBUG_PRINT (( "\n** TX stops sending Vsif %X", (uint16_t)GetMpegDecodeAddr() ));
                                    }
                                    if ( !IfCtrl.fSpdDecodeIsrc2 && IfCtrl.fIsrc1Captured  ){
                                        SetDecodeIfBuffers ( REG__MPEG_DECODE,  IF_ISRC2_ID );
                                        IfCtrl.Ins[SelIsrc2].wTo      = TWO_VIDFR_CAPTURE_TO;
                                    }
                                    else {
                                        SetDecodeIfBuffers ( REG__MPEG_DECODE,  IF_ISRC1_ID );
                                        IfCtrl.Ins[SelIsrc1].wTo      = ISRC1_CAPTURE_TO;
                                    }
                                    break;
                    case SelIsrc1:
                                    SetDecodeIfBuffers ( REG__MPEG_DECODE,  IF_VSIF_ID );
                                    IfCtrl.Ins[SelVsif].wTo      = TWO_VIDFR_CAPTURE_TO;
                                    IfCtrl.fMpegBuff_IsrcNotFound = TRUE;
                                    break;
                    case SelIsrc2:
                                    if ( !IfCtrl.fSpdDecodeIsrc2 ){ /* this tell us that we share buffer with Vsif */
                                        IfCtrl.fMpegBuff_IsrcNotFound = TRUE;
                                        SetDecodeIfBuffers ( REG__MPEG_DECODE,  IF_ISRC1_ID );
                                        IfCtrl.Ins[SelIsrc1].wTo      = ISRC1_CAPTURE_TO;
                                    }
                                    else {
                                    /*  do nothing as only ISRC2 used for detection */
                                    }
                                    break;
                    case SelSpd:
                                    SetDecodeIfBuffers ( REG__SPD_DECODE,  IF_ISRC2_ID );
                                    IfCtrl.fSpdDecodeIsrc2     = TRUE;
                                    /* As no SPD for such a long time, allocate SPD_BUFF for ISRC2*/
                                    break;
                    case SelAcp:
                                    if ( GetIntrAcpStatus ()){
										IfCtrl.fAcpIsPresent = TRUE;
										IfCtrl.wNoAcpTotTime = 0;                                        
                                        IfCtrl.Ins[SelAcp].wTo = ACP_CAPTURE_200MS;
                                    }
                                    else {
                                        if ( IfCtrl.wNoAcpTotTime >= TIME_WO_ACP_IF ){
                                            DEBUG_PRINT ( ( "\n** TX stops sending Acp\n"));
                                            IfCtrl.fAcpIsPresent = FALSE;
                                            memset ( IfCtrl.abAcp, 0, IF_ACP_SIZE);
                                            IfCtrl.fAcpIntr = TRUE;
											SetIntrAcp ( IfCtrl.fAcpIntr );
                                        }
                                        else {
                                            IfCtrl.Ins[SelAcp].wTo = ACP_CAPTURE_200MS;
                                        }
                                    }
                                    break;
					default:
									break;
                }
            }
        }
    }
}


/*****************************************************************************/
/**
 *  The description of the function InterInfoFrmProc(). Processing info frame interrupts
 *
 *
 *  @param[in] bNewInfoFrm  new info frames
 *  @param[in] bNoInfoFrm   no info frames
 *
 *  @return             none
 *  @retval             void
 *
 *****************************************************************************/
 /*
        SPD_BUFF:   [SPD][IFRC2 when SPD captured]
        MPEG_BUFF:  [ISRC1][VSIF][ISRC2 if SPD_BUFF is SPD][ISRC1][VSIF]
 */
void InterInfoFrmProc ( uint8_t bNewInfoFrm, uint8_t bNoInfoFrm ){
uint8_t bIfType;

    if ( bNewInfoFrm ){
        if ( bNewInfoFrm & BIT__NEW_AVI_INF ){
            if ( IfCtrl.fIntrOnEveryNewAvi ){
                /* set Avi interrupt on change */
                IfCtrl.fIntrOnEveryNewAvi =  FALSE;
                SetIntrOnEveryNewIf ( FALSE, IF_AVI_ID );
            }
            DEBUG_PRINT (("* Got Avi\n"));
            SetToIfBuff ( IF_AVI_ID );
            if ( GetIf ( IF_AVI_ID )){
                DEBUG_PRINT ((" valid" ));
            }
        }
        if ( bNewInfoFrm & BIT__VIRT_NEW_GDB_INF ){
            if ( IfCtrl.fIntrOnEveryNewGamut ){
                /* set Gamut interrupt on change */
                IfCtrl.fIntrOnEveryNewGamut =  FALSE;
                SetIntrOnEveryNewIf ( FALSE, IF_GMT_ID );
            }
            DEBUG_PRINT (( "\n* Got Gdb\n"));
            /* TODO: add processing */
        }
        if ( bNewInfoFrm & BIT__NEW_MPEG_INF ){
//            DEBUG_PRINT (( "\n* Got if in Mpeg buff: " );
            /* this buffer is used for Vsif and Isrc1/2*/
#if 0
            if ( IfCtrl.bDecodeTypeMpegBuffer == IF_VSIF_ID ){
                if ( GetIf( IF_VSIF_ID, IfCtrl.bDecodeTypeMpegBuffer ) ){
                }
            }
            else if (   (IfCtrl.bDecodeTypeMpegBuffer == IF_ISRC1_ID) ||
                (IfCtrl.bDecodeTypeMpegBuffer == IF_ISRC2_ID) ){
            }
#endif
            IfCtrl.fMpegBuff_IsrcNotFound = FALSE;
            if ( GetIfRoll( REG__MPEG_TYPE, &bIfType )){
            }
            SetToIfBuff ( bIfType );
        }
        if ( bNewInfoFrm & BIT__NEW_AUD_INF ){
            if ( IfCtrl.fIntrOnEveryNewAud ){
                /* set Aud interrupt on change */
                IfCtrl.fIntrOnEveryNewAud =  FALSE;
                SetIntrOnEveryNewIf ( FALSE, IF_AUD_ID );
            }
            DEBUG_PRINT ( ( "\n* Got Aud\n"));
            SetToIfBuff ( IF_AUD_ID );
        }
        if ( bNewInfoFrm & BIT__VIRT_NEW_ACP_INF ){

            DEBUG_PRINT (( "\n* Got Acp\n" ));
            GetIf ( IF_ACP_ID );
            SetToIfBuff ( IF_ACP_ID );
			IfCtrl.fAcpIsPresent = TRUE;
            if ( IfCtrl.fAcpIntr ){
                /*  Disable Acp Interrupts until IfCtrl.fAcpIsPresent  */

                IfCtrl.fAcpIntr = FALSE;
				SetIntrAcp ( IfCtrl.fAcpIntr );
                SetIntrOnEveryNewIf ( TRUE, IF_ACP_ID );
            }
        }
        if ( bNewInfoFrm & BIT__NEW_SPD_INF ){
            DEBUG_PRINT (( "\n* Got Spd buff: type %x\n", (uint16_t)IfCtrl.bDecodeTypeSpdBuffer ));
            /* this buffer is used for Spd and Isrc2*/
            if ( GetIfRoll( REG__SPD_TYPE, &bIfType )){
            }
            SetToIfBuff ( bIfType );
        }
    }
    if ( bNoInfoFrm ){
        if ( bNoInfoFrm & BIT__VIRT_NO_AVI_INF ){
            DEBUG_PRINT (( "* Lost Avi\n" ));
            /* set interrupt on every new Avi packet */
            IfCtrl.fIntrOnEveryNewAvi = TRUE;
            SetIntrOnEveryNewIf ( TRUE, IF_AVI_ID );
        }
        if ( bNoInfoFrm & BIT__NO_GDB_INF ){
            DEBUG_PRINT (( "* Lost Gdb\n" ));
            /* set interrupt on every new Gamut packet */
            IfCtrl.fIntrOnEveryNewGamut = TRUE;
            SetIntrOnEveryNewIf ( TRUE, IF_GMT_ID );
        }
        if ( bNoInfoFrm & BIT__NO_AUD_INF ){
            DEBUG_PRINT (( "* Lost Aud\n" ));
            /* set interrupt on every new Audio packet */
            IfCtrl.fIntrOnEveryNewAud = TRUE;
            SetIntrOnEveryNewIf ( TRUE, IF_AUD_ID );
        }
    }
}

#endif //#if (CONF__SUPPORT_3D == ENABLE)

