///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <hdmirx_repeater.c>
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2012/07/24
//   @fileversion: HDMIRX_SAMPLE_2.18
//******************************************/



#include "hdmirx.h"

#ifdef SUPPORT_REPEATER

RXHDCP_State_Type RHDCPState = RXHDCP_Reset ;
// BOOL repeater = FALSE ;


static BYTE RxHDCPStatusFlag = 0 ;
static BYTE cDownStream = 0 ;
static BYTE KSVOffset=0 ;

static _XDATA BYTE Vr[20] ;
static _XDATA BYTE M0[8] ;
static _XDATA BYTE SHABuff[64] ;

extern BYTE bHDCPMode ;

///////////////////////////////////////////////////////////////////////////////
// Function Prototype
///////////////////////////////////////////////////////////////////////////////
SYS_STATUS RxHDCP_WriteVR(BYTE *pVr);


///////////////////////////////////////////////////////////////////////////////
// Function Body
///////////////////////////////////////////////////////////////////////////////
void RxHDCPSetReceiver()
{
	PowerDownHDMI();
    bHDCPMode = HDCP_RECEIVER ;
	InitHDMIRX(TRUE);

}

void RxHDCPSetRepeater()
{
    PowerDownHDMI();
    bHDCPMode = HDCP_REPEATER ;
    InitHDMIRX(TRUE);
}

#ifdef REPEATER_DEBUG
void RxHDCPSetRdyTimeOut()
{
    PowerDownHDMI();
    bHDCPMode = HDCP_REPEATER | HDCP_RDY_TIMEOUT ;
    InitHDMIRX(TRUE);
}

void RxHDCPSetInvalidV()
{
    PowerDownHDMI();
    bHDCPMode = HDCP_REPEATER | HDCP_INVALID_V ;
    InitHDMIRX(FALSE);
}

void RxHDCPSetOverDownStream()
{
    PowerDownHDMI();
    bHDCPMode = HDCP_REPEATER | HDCP_OVER_DOWNSTREAM ;
    InitHDMIRX(TRUE);
}

void RxHDCPSetOverCascade()
{
    PowerDownHDMI();
    bHDCPMode = HDCP_REPEATER | HDCP_OVER_CASCADE ;
    InitHDMIRX(TRUE);
}
#endif // REPEATER_DEBUG

void RxHDCPRepeaterCapabilitySet(BYTE uc)
{
	HDMIRX_WriteI2C_Byte(REG_RX_CDEPTH_CTRL,HDMIRX_ReadI2C_Byte(REG_RX_CDEPTH_CTRL)|uc);
    HDMIRX_DEBUG_PRINTF(("RxHDCPRepeaterCapabilitySet=%2X\n",(int)uc));
}

void RxHDCPRepeaterCapabilityClear(BYTE uc)
{
	HDMIRX_WriteI2C_Byte(REG_RX_CDEPTH_CTRL,HDMIRX_ReadI2C_Byte(REG_RX_CDEPTH_CTRL)&(~uc));
    HDMIRX_DEBUG_PRINTF(("RxHDCPRepeaterCapabilityClear=%2X\n",(int)uc));
}

SYS_STATUS
RxHDCP_GenVR(BYTE Vr[])
{
	// extern void SHA_Simple(void *p, LONG len, BYTE *output);

    int i, n ;

    n = cDownStream*5+10 ;
    for(i = n ; i < 64 ; i++)
    {
        SHABuff[i] = 0 ;
    }
	for(i = 0 ; i < 64 ; i++)
	{
		HDMIRX_DEBUG_PRINTF2(("%02X ",(int)SHABuff[i]));
		if(0==((i+1)%8))HDMIRX_DEBUG_PRINTF2(("\n"));
	}
	HDMIRX_DEBUG_PRINTF(("n=%2X\n",(int)n));
    SHA_Simple(SHABuff, (LONG)n, Vr);
	HDMIRX_DEBUG_PRINTF2(("SHA[]: "));
	for(i = 0 ; i < 20 ; i++)
	{
		HDMIRX_DEBUG_PRINTF2(("%02X ",(int)Vr[i]));
	}
	HDMIRX_DEBUG_PRINTF2(("\n"));
    return ER_SUCCESS ;
}


SYS_STATUS setRxHDCPKSVList(BYTE offset, BYTE *pKSVList, BYTE count)
{
	BYTE i=0;
    if(!pKSVList)
    {
        return ER_FAIL ;
    }

    HDMIRX_WriteI2C_Byte(REG_RX_BLOCK_SEL,1);
    if((offset+count)> HDMIRX_MAX_KSV)
    {
        return ER_FAIL ;
    }

    for(KSVOffset = offset ; (KSVOffset < HDMIRX_MAX_KSV)&& (KSVOffset < count); KSVOffset ++)
    {
        if (KSVOffset < 4)
        {
        	for(i = 0 ;i<5;i++)
        	{
        		HDMIRX_WriteI2C_Byte(REG_RX_KSV_FIFO00+i+KSVOffset*5,pKSVList[(KSVOffset-offset)*5+i]);
        		SHABuff[KSVOffset*5+i] = pKSVList[(KSVOffset-offset)*5+i] ;
        	}
        }
        else
        {
        	for(i = 0 ;i<5;i++)
        	{
        		HDMIRX_WriteI2C_Byte(REG_RX_KSV_FIFO40+i+KSVOffset*5-20,pKSVList[(KSVOffset-offset)*5+i]);
        		SHABuff[KSVOffset*5+i] = pKSVList[(KSVOffset-offset)*5+i] ;
        	}
        }
    }
    HDMIRX_WriteI2C_Byte(REG_RX_BLOCK_SEL,0);

	return ER_SUCCESS;
}

SYS_STATUS RxHDCPGetM0()
{
	BYTE i=0;
    if(cDownStream > HDMIRX_MAX_KSV)
    {
        return ER_FAIL ;
    }
    HDMIRX_WriteI2C_Byte(REG_RX_BLOCK_SEL,1);
	for(;i<8;i++)
	{
		SHABuff[cDownStream*5+2+i]=HDMIRX_ReadI2C_Byte(REG_RX_M0_B0+i);
		HDMIRX_DEBUG_PRINTF3(("M[%d] = %02X\n",i,(int)SHABuff[cDownStream*5+2+i]));
	}

    HDMIRX_WriteI2C_Byte(REG_RX_BLOCK_SEL,0);
    HDMIRX_DEBUG_PRINTF(("RxHDCPGetM0\n"));
	return ER_SUCCESS;
}

SYS_STATUS RxHDCPGetBstatus(USHORT *pBstatus)
{
    if(!pBstatus)
    {
        return ER_FAIL ;
    }
    HDMIRX_WriteI2C_Byte(REG_RX_BLOCK_SEL,1);
    *pBstatus=HDMIRX_ReadI2C_Byte(REG_RX_BSTATUSH);
    *pBstatus <<= 8 ;
    *pBstatus |= HDMIRX_ReadI2C_Byte(REG_RX_BSTATUSL);
    HDMIRX_WriteI2C_Byte(REG_RX_BLOCK_SEL,0);
    HDMIRX_DEBUG_PRINTF(("RxHDCPGetBstatus\n"));
	return ER_SUCCESS;
}

void setRxHDCPBStatus(WORD bstatus)
{
    HDMIRX_WriteI2C_Byte(REG_RX_BLOCK_SEL,1);
    HDMIRX_WriteI2C_Byte(REG_RX_BSTATUSH,(BYTE)((bstatus>>8)& 0x0F));
    HDMIRX_WriteI2C_Byte(REG_RX_BSTATUSL,(BYTE)(bstatus & 0xFF));
    HDMIRX_WriteI2C_Byte(REG_RX_BLOCK_SEL,0);

    cDownStream = (BYTE)(bstatus &0x7F);
    if((0==(bstatus & 0x880))&& cDownStream <= HDMIRX_MAX_KSV)
    {
        SHABuff[cDownStream*5] = (BYTE)(bstatus &0xFF);
        SHABuff[cDownStream*5+1] = (BYTE)((bstatus>>8)&0x0F)| (IsHDMIRXHDMIMode()?0x10:0);
    }

}

SYS_STATUS RxHDCP_WriteVR(BYTE *pVr)
{
	BYTE i;
    if(!pVr)
    {
        return ER_FAIL ;
    }
    HDMIRX_WriteI2C_Byte(REG_RX_BLOCK_SEL,1);

	for(i = 0 ; i < 20 ; i++ )
	{
		HDMIRX_WriteI2C_Byte(REG_RX_SHA1_H00+i,*(pVr+i));
		if(!(i%5))HDMIRX_DEBUG_PRINTF2(("\n"));
		HDMIRX_DEBUG_PRINTF2(("SHA1[%2X]=%2X ,",(int)i,(int)*(pVr+i))) ;
	}
    HDMIRX_WriteI2C_Byte(REG_RX_BLOCK_SEL,0);
	DumpHDMIRXReg();
	return ER_SUCCESS;
}

void setRxHDCPEvent(BYTE event)
{
    RxHDCPStatusFlag |= event ;
}


BOOL getRxHDCPEvent(BYTE event)
{
    BOOL retv ;
    retv = (RxHDCPStatusFlag & event)?TRUE:FALSE ;
    RxHDCPStatusFlag &= ~event ;
    return retv ;
}


void setRxHDCPCalcSHA()
{
#if 1
    SwitchRxHDCPState(RXHDCP_UpdateKSVList);
#else
    RxHDCPGetM0();
    RxHDCP_GenVR(Vr);
    RxHDCP_WriteVR(Vr);
    SwitchRxHDCPState(RXHDCP_Ready);
#endif
}

char _CODE *RXHDCPStr[] =
{
    "RXHDCP_Reset = 0",
    "RXHDCP_AuthStart",
    "RXHDCP_AuthDone",
    "RXHDCP_UpdateKSVList",
    "RXHDCP_Ready",
    "RXHDCP_FailReady",
    "RXHDCP_Reserved"

};

void SwitchRxHDCPState(RXHDCP_State_Type state)
{
    if(RHDCPState == state)
    {
        return ;
    }

    HDMIRX_DEBUG_PRINTF(("SwitchRxHDCPState():%s->%s\n",RXHDCPStr[RHDCPState],RXHDCPStr[state])) ;

    switch(state)
    {
    case RXHDCP_Reset:
    	RxHDCPRepeaterCapabilityClear(B_KSV_READY);
        break ;

    case RXHDCP_AuthStart:
    	RxHDCPRepeaterCapabilityClear(B_KSV_READY);
	    setRxHDCPEvent(EVENT_RXHDCP_AUTH_START);
        break ;

    case RXHDCP_AuthDone:
	    setRxHDCPEvent(EVENT_RXHDCP_AUTH_DONE);
	    KSVOffset=0 ;
	    cDownStream = 0 ;
        break ;

    case RXHDCP_UpdateKSVList:
        break ;

    case RXHDCP_Ready:
        RxHDCPRepeaterCapabilitySet(B_KSV_READY);
        break ;

    case RXHDCP_FailReady:
        RxHDCPRepeaterCapabilitySet(B_KSV_READY);
        break ;
    }

    RHDCPState = state ;
}

void setRxHDCPFailReady()
{
    SwitchRxHDCPState(RXHDCP_FailReady);
}

void RxHDCP_Handler()
{
    if(0==(bHDCPMode & HDCP_REPEATER))
    {
        return ;
    }
    switch(RHDCPState)
    {
    case RXHDCP_Reset:
        if(HDMIRX_ReadI2C_Byte(REG_RX_HDCP_STATUS)& B_R0_READY)
        {
            SwitchRxHDCPState(RXHDCP_AuthDone);
        }
        break ;

    case RXHDCP_AuthStart:
        if(HDMIRX_ReadI2C_Byte(REG_RX_HDCP_STATUS)& B_R0_READY)
        {
            SwitchRxHDCPState(RXHDCP_AuthDone);
        }
        break ;

    case RXHDCP_AuthDone:
        break ;

    case RXHDCP_UpdateKSVList:
        RxHDCPGetM0();
        RxHDCP_GenVR(Vr);
        RxHDCP_WriteVR(Vr);
        SwitchRxHDCPState(RXHDCP_Ready);
        break ;

    }
}


#endif // SUPPORT_REPEATER
