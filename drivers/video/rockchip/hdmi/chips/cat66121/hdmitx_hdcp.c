///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <hdmitx_hdcp.c>
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2012/12/20
//   @fileversion: ITE_HDMITX_SAMPLE_3.14
//******************************************/
#include "hdmitx.h"
#include "hdmitx_drv.h"
#include "sha1.h"

static BYTE countbit(BYTE b);

extern HDMITXDEV hdmiTxDev[HDMITX_MAX_DEV_COUNT] ;

#ifdef SUPPORT_SHA
_XDATA BYTE SHABuff[64] ;
_XDATA BYTE V[20] ;
_XDATA BYTE KSVList[32] ;
_XDATA BYTE Vr[20] ;
_XDATA BYTE M0[8] ;
#endif

BOOL HDMITX_EnableHDCP(BYTE bEnable)
{
#ifdef SUPPORT_HDCP
    if(bEnable)
    {
        if(ER_FAIL == hdmitx_hdcp_Authenticate())
        {
            HDCP_DEBUG_PRINTF(("ER_FAIL == hdmitx_hdcp_Authenticate\n"));
            hdmitx_hdcp_ResetAuth();
			return FALSE ;
        }
            HDCP_DEBUG_PRINTF(("hdmitx_hdcp_Authenticate SUCCESS\n"));
    }
    else
    {
        hdmiTxDev[0].bAuthenticated=FALSE;
        hdmitx_hdcp_ResetAuth();
    }
#endif
    return TRUE ;
}

#ifdef SUPPORT_HDCP

BOOL getHDMITX_AuthenticationDone(void)
{
    //HDCP_DEBUG_PRINTF((" getHDMITX_AuthenticationDone() = %s\n",hdmiTxDev[0].bAuthenticated?"TRUE":"FALSE" ));
    return hdmiTxDev[0].bAuthenticated;
}

//////////////////////////////////////////////////////////////////////
// Authentication
//////////////////////////////////////////////////////////////////////
void hdmitx_hdcp_ClearAuthInterrupt(void)
{
    // BYTE uc ;
    // uc = HDMITX_ReadI2C_Byte(REG_TX_INT_MASK2) & (~(B_TX_KSVLISTCHK_MASK|B_TX_AUTH_DONE_MASK|B_TX_AUTH_FAIL_MASK));
    HDMITX_SetI2C_Byte(REG_TX_INT_MASK2, B_TX_KSVLISTCHK_MASK|B_TX_AUTH_DONE_MASK|B_TX_AUTH_FAIL_MASK, 0);
    HDMITX_WriteI2C_Byte(REG_TX_INT_CLR0,B_TX_CLR_AUTH_FAIL|B_TX_CLR_AUTH_DONE|B_TX_CLR_KSVLISTCHK);
    HDMITX_WriteI2C_Byte(REG_TX_INT_CLR1,0);
    HDMITX_WriteI2C_Byte(REG_TX_SYS_STATUS,B_TX_INTACTDONE);
}

void hdmitx_hdcp_ResetAuth(void)
{
    HDMITX_WriteI2C_Byte(REG_TX_LISTCTRL,0);
    HDMITX_WriteI2C_Byte(REG_TX_HDCP_DESIRE,0);
    HDMITX_OrReg_Byte(REG_TX_SW_RST,B_TX_HDCP_RST_HDMITX);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERDDC|B_TX_MASTERHOST);
    hdmitx_hdcp_ClearAuthInterrupt();
    hdmitx_AbortDDC();
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_hdcp_Auth_Fire()
// Parameter: N/A
// Return: N/A
// Remark: write anything to reg21 to enable HDCP authentication by HW
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////

void hdmitx_hdcp_Auth_Fire(void)
{
    // HDCP_DEBUG_PRINTF(("hdmitx_hdcp_Auth_Fire():\n"));
    HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERDDC|B_TX_MASTERHDCP); // MASTERHDCP,no need command but fire.
    HDMITX_WriteI2C_Byte(REG_TX_AUTHFIRE,1);
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_hdcp_StartAnCipher
// Parameter: N/A
// Return: N/A
// Remark: Start the Cipher to free run for random number. When stop,An is
//         ready in Reg30.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////

void hdmitx_hdcp_StartAnCipher(void)
{
    HDMITX_WriteI2C_Byte(REG_TX_AN_GENERATE,B_TX_START_CIPHER_GEN);
    delay1ms(1); // delay 1 ms
}
//////////////////////////////////////////////////////////////////////
// Function: hdmitx_hdcp_StopAnCipher
// Parameter: N/A
// Return: N/A
// Remark: Stop the Cipher,and An is ready in Reg30.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////

void hdmitx_hdcp_StopAnCipher(void)
{
    HDMITX_WriteI2C_Byte(REG_TX_AN_GENERATE,B_TX_STOP_CIPHER_GEN);
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_hdcp_GenerateAn
// Parameter: N/A
// Return: N/A
// Remark: start An ciper random run at first,then stop it. Software can get
//         an in reg30~reg38,the write to reg28~2F
// Side-Effect:
//////////////////////////////////////////////////////////////////////

void hdmitx_hdcp_GenerateAn(void)
{
    BYTE Data[8];
    BYTE i=0;
#if 1
    hdmitx_hdcp_StartAnCipher();
    // HDMITX_WriteI2C_Byte(REG_TX_AN_GENERATE,B_TX_START_CIPHER_GEN);
    // delay1ms(1); // delay 1 ms
    // HDMITX_WriteI2C_Byte(REG_TX_AN_GENERATE,B_TX_STOP_CIPHER_GEN);

    hdmitx_hdcp_StopAnCipher();

    Switch_HDMITX_Bank(0);
    // new An is ready in reg30
    HDMITX_ReadI2C_ByteN(REG_TX_AN_GEN,Data,8);
#else
    Data[0] = 0 ;Data[1] = 0 ;Data[2] = 0 ;Data[3] = 0 ;
    Data[4] = 0 ;Data[5] = 0 ;Data[6] = 0 ;Data[7] = 0 ;
#endif
    for(i=0;i<8;i++)
    {
        HDMITX_WriteI2C_Byte(REG_TX_AN+i,Data[i]);
    }
    //HDMITX_WriteI2C_ByteN(REG_TX_AN,Data,8);
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_hdcp_GetBCaps
// Parameter: pBCaps - pointer of byte to get BCaps.
//            pBStatus - pointer of two bytes to get BStatus
// Return: ER_SUCCESS if successfully got BCaps and BStatus.
// Remark: get B status and capability from HDCP reciever via DDC bus.
// Side-Effect:
//////////////////////////////////////////////////////////////////////

SYS_STATUS hdmitx_hdcp_GetBCaps(PBYTE pBCaps ,PUSHORT pBStatus)
{
    BYTE ucdata ;
    BYTE TimeOut ;

    Switch_HDMITX_Bank(0);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERDDC|B_TX_MASTERHOST);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_HEADER,DDC_HDCP_ADDRESS);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_REQOFF,0x40); // BCaps offset
    HDMITX_WriteI2C_Byte(REG_TX_DDC_REQCOUNT,3);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_CMD,CMD_DDC_SEQ_BURSTREAD);

    for(TimeOut = 200 ; TimeOut > 0 ; TimeOut --)
    {
        delay1ms(1);

        ucdata = HDMITX_ReadI2C_Byte(REG_TX_DDC_STATUS);

        if(ucdata & B_TX_DDC_DONE)
        {
            //HDCP_DEBUG_PRINTF(("hdmitx_hdcp_GetBCaps(): DDC Done.\n"));
            break ;
        }
        if(ucdata & B_TX_DDC_ERROR)
        {
//            HDCP_DEBUG_PRINTF(("hdmitx_hdcp_GetBCaps(): DDC fail by reg16=%02X.\n",ucdata));
            return ER_FAIL ;
        }
    }
    if(TimeOut == 0)
    {
        return ER_FAIL ;
    }
#if 1
    ucdata = HDMITX_ReadI2C_Byte(REG_TX_BSTAT+1);

    *pBStatus = (USHORT)ucdata ;
    *pBStatus <<= 8 ;
    ucdata = HDMITX_ReadI2C_Byte(REG_TX_BSTAT);
    *pBStatus |= ((USHORT)ucdata&0xFF);
    *pBCaps = HDMITX_ReadI2C_Byte(REG_TX_BCAP);
#else
    *pBCaps = HDMITX_ReadI2C_Byte(0x17);
    *pBStatus = HDMITX_ReadI2C_Byte(0x17) & 0xFF ;
    *pBStatus |= (int)(HDMITX_ReadI2C_Byte(0x17)&0xFF)<<8;
    HDCP_DEBUG_PRINTF(("hdmitx_hdcp_GetBCaps(): ucdata = %02X\n",(int)HDMITX_ReadI2C_Byte(0x16)));
#endif
    return ER_SUCCESS ;
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_hdcp_GetBKSV
// Parameter: pBKSV - pointer of 5 bytes buffer for getting BKSV
// Return: ER_SUCCESS if successfuly got BKSV from Rx.
// Remark: Get BKSV from HDCP reciever.
// Side-Effect: N/A
//////////////////////////////////////////////////////////////////////

SYS_STATUS hdmitx_hdcp_GetBKSV(BYTE *pBKSV)
{
    BYTE ucdata ;
    BYTE TimeOut ;

    Switch_HDMITX_Bank(0);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERDDC|B_TX_MASTERHOST);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_HEADER,DDC_HDCP_ADDRESS);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_REQOFF,0x00); // BKSV offset
    HDMITX_WriteI2C_Byte(REG_TX_DDC_REQCOUNT,5);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_CMD,CMD_DDC_SEQ_BURSTREAD);

    for(TimeOut = 200 ; TimeOut > 0 ; TimeOut --)
    {
        delay1ms(1);

        ucdata = HDMITX_ReadI2C_Byte(REG_TX_DDC_STATUS);
        if(ucdata & B_TX_DDC_DONE)
        {
            HDCP_DEBUG_PRINTF(("hdmitx_hdcp_GetBCaps(): DDC Done.\n"));
            break ;
        }
        if(ucdata & B_TX_DDC_ERROR)
        {
            HDCP_DEBUG_PRINTF(("hdmitx_hdcp_GetBCaps(): DDC No ack or arbilose,%x,maybe cable did not connected. Fail.\n",ucdata));
            return ER_FAIL ;
        }
    }
    if(TimeOut == 0)
    {
        return ER_FAIL ;
    }
    HDMITX_ReadI2C_ByteN(REG_TX_BKSV,(PBYTE)pBKSV,5);

    return ER_SUCCESS ;
}

//////////////////////////////////////////////////////////////////////
// Function:hdmitx_hdcp_Authenticate
// Parameter: N/A
// Return: ER_SUCCESS if Authenticated without error.
// Remark: do Authentication with Rx
// Side-Effect:
//  1. hdmiTxDev[0].bAuthenticated global variable will be TRUE when authenticated.
//  2. Auth_done interrupt and AUTH_FAIL interrupt will be enabled.
//////////////////////////////////////////////////////////////////////
static BYTE countbit(BYTE b)
{
    BYTE i,count ;
    for( i = 0, count = 0 ; i < 8 ; i++ )
    {
        if( b & (1<<i) )
        {
            count++ ;
        }
    }
    return count ;
}

void hdmitx_hdcp_Reset(void)
{
    BYTE uc ;
    uc = HDMITX_ReadI2C_Byte(REG_TX_SW_RST) | B_TX_HDCP_RST_HDMITX ;
    HDMITX_WriteI2C_Byte(REG_TX_SW_RST,uc);
    HDMITX_WriteI2C_Byte(REG_TX_HDCP_DESIRE,0);
    HDMITX_WriteI2C_Byte(REG_TX_LISTCTRL,0);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERHOST);
    hdmitx_ClearDDCFIFO();
    hdmitx_AbortDDC();
}

SYS_STATUS hdmitx_hdcp_Authenticate()
{
    BYTE ucdata ;
    BYTE BCaps ;
    USHORT BStatus ;
    USHORT TimeOut ;

 //   BYTE revoked = FALSE ;
    BYTE BKSV[5] ;

    hdmiTxDev[0].bAuthenticated = FALSE ;
    if(0==(B_TXVIDSTABLE&HDMITX_ReadI2C_Byte(REG_TX_SYS_STATUS)))
    {
	    HDCP_DEBUG_PRINTF(("hdmitx_hdcp_Authenticate(): Video not stable\n"));
	    return ER_FAIL;
    }
    // Authenticate should be called after AFE setup up.

    HDCP_DEBUG_PRINTF(("hdmitx_hdcp_Authenticate():\n"));
    hdmitx_hdcp_Reset();

    Switch_HDMITX_Bank(0);

    for( TimeOut = 0 ; TimeOut < 80 ; TimeOut++ )
    {
        delay1ms(15);

        if(hdmitx_hdcp_GetBCaps(&BCaps,&BStatus) != ER_SUCCESS)
        {
            HDCP_DEBUG_PRINTF(("hdmitx_hdcp_GetBCaps fail.\n"));
            return ER_FAIL ;
        }
        // HDCP_DEBUG_PRINTF(("(%d)Reg16 = %02X\n",idx++,(int)HDMITX_ReadI2C_Byte(0x16)));

        if(B_TX_HDMI_MODE == (HDMITX_ReadI2C_Byte(REG_TX_HDMI_MODE) & B_TX_HDMI_MODE ))
        {
            if((BStatus & B_TX_CAP_HDMI_MODE)==B_TX_CAP_HDMI_MODE)
            {
                break;
            }
        }
        else
        {
            if((BStatus & B_TX_CAP_HDMI_MODE)!=B_TX_CAP_HDMI_MODE)
            {
                break;
            }
        }
    }
    /*
    if((BStatus & M_TX_DOWNSTREAM_COUNT)> 6)
    {
        HDCP_DEBUG_PRINTF(("Down Stream Count %d is over maximum supported number 6,fail.\n",(int)(BStatus & M_TX_DOWNSTREAM_COUNT)));
        return ER_FAIL ;
    }
    */
	HDCP_DEBUG_PRINTF(("BCAPS = %02X BSTATUS = %04X\n", (int)BCaps, BStatus));
    hdmitx_hdcp_GetBKSV(BKSV);
    HDCP_DEBUG_PRINTF(("BKSV %02X %02X %02X %02X %02X\n",(int)BKSV[0],(int)BKSV[1],(int)BKSV[2],(int)BKSV[3],(int)BKSV[4]));

    for(TimeOut = 0, ucdata = 0 ; TimeOut < 5 ; TimeOut ++)
    {
        ucdata += countbit(BKSV[TimeOut]);
    }
    if( ucdata != 20 )
    {
        HDCP_DEBUG_PRINTF(("countbit error\n"));
        return ER_FAIL ;

    }
    Switch_HDMITX_Bank(0); // switch bank action should start on direct register writting of each function.

    HDMITX_AndReg_Byte(REG_TX_SW_RST,~(B_TX_HDCP_RST_HDMITX));

    HDMITX_WriteI2C_Byte(REG_TX_HDCP_DESIRE,B_TX_CPDESIRE);
    hdmitx_hdcp_ClearAuthInterrupt();

    hdmitx_hdcp_GenerateAn();
    HDMITX_WriteI2C_Byte(REG_TX_LISTCTRL,0);
    hdmiTxDev[0].bAuthenticated = FALSE ;

    hdmitx_ClearDDCFIFO();

    if((BCaps & B_TX_CAP_HDMI_REPEATER) == 0)
    {
        hdmitx_hdcp_Auth_Fire();
        // wait for status ;

        for(TimeOut = 250 ; TimeOut > 0 ; TimeOut --)
        {
            delay1ms(5); // delay 1ms
            ucdata = HDMITX_ReadI2C_Byte(REG_TX_AUTH_STAT);
            // HDCP_DEBUG_PRINTF(("reg46 = %02x reg16 = %02x\n",(int)ucdata,(int)HDMITX_ReadI2C_Byte(0x16)));

            if(ucdata & B_TX_AUTH_DONE)
            {
                hdmiTxDev[0].bAuthenticated = TRUE ;
                HDCP_DEBUG_PRINTF(("hdmitx_hdcp_Authenticate()-receiver: Authenticate SUCESS\n"));
                break ;
            }
            ucdata = HDMITX_ReadI2C_Byte(REG_TX_INT_STAT2);
            if(ucdata & B_TX_INT_AUTH_FAIL)
            {

                HDMITX_WriteI2C_Byte(REG_TX_INT_CLR0,B_TX_CLR_AUTH_FAIL);
                HDMITX_WriteI2C_Byte(REG_TX_INT_CLR1,0);
                HDMITX_WriteI2C_Byte(REG_TX_SYS_STATUS,B_TX_INTACTDONE);
                HDMITX_WriteI2C_Byte(REG_TX_SYS_STATUS,0);

                HDCP_DEBUG_PRINTF(("hdmitx_hdcp_Authenticate()-receiver: Authenticate fail\n"));
                hdmiTxDev[0].bAuthenticated = FALSE ;
                return ER_FAIL ;
            }
        }
        if(TimeOut == 0)
        {
             HDCP_DEBUG_PRINTF(("hdmitx_hdcp_Authenticate()-receiver: Time out. return fail\n"));
             hdmiTxDev[0].bAuthenticated = FALSE ;
             return ER_FAIL ;
        }
        return ER_SUCCESS ;
    }
    return hdmitx_hdcp_Authenticate_Repeater();
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_hdcp_VerifyIntegration
// Parameter: N/A
// Return: ER_SUCCESS if success,if AUTH_FAIL interrupt status,return fail.
// Remark: no used now.
// Side-Effect:
//////////////////////////////////////////////////////////////////////

SYS_STATUS hdmitx_hdcp_VerifyIntegration()
{
    // if any interrupt issued a Auth fail,returned the Verify Integration fail.

    if(HDMITX_ReadI2C_Byte(REG_TX_INT_STAT1) & B_TX_INT_AUTH_FAIL)
    {
        hdmitx_hdcp_ClearAuthInterrupt();
        hdmiTxDev[0].bAuthenticated = FALSE ;
        return ER_FAIL ;
    }
    if(hdmiTxDev[0].bAuthenticated == TRUE)
    {
        return ER_SUCCESS ;
    }
    return ER_FAIL ;
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_hdcp_Authenticate_Repeater
// Parameter: BCaps and BStatus
// Return: ER_SUCCESS if success,if AUTH_FAIL interrupt status,return fail.
// Remark:
// Side-Effect: as Authentication
//////////////////////////////////////////////////////////////////////

void hdmitx_hdcp_CancelRepeaterAuthenticate()
{
    HDCP_DEBUG_PRINTF(("hdmitx_hdcp_CancelRepeaterAuthenticate"));
    HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERDDC|B_TX_MASTERHOST);
    hdmitx_AbortDDC();
    HDMITX_WriteI2C_Byte(REG_TX_LISTCTRL,B_TX_LISTFAIL|B_TX_LISTDONE);
    hdmitx_hdcp_ClearAuthInterrupt();
}

void hdmitx_hdcp_ResumeRepeaterAuthenticate()
{
    HDMITX_WriteI2C_Byte(REG_TX_LISTCTRL,B_TX_LISTDONE);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERHDCP);
}

#if 0 // def SUPPORT_SHA
// #define SHA_BUFF_COUNT 17
// _XDATA ULONG w[SHA_BUFF_COUNT];
//
// _XDATA ULONG sha[5] ;
//
// #define rol(x,y) (((x) << (y)) | (((ULONG)x) >> (32-y)))
//
// void SHATransform(ULONG * h)
// {
//     int t,i;
//     ULONG tmp ;
//
//     h[0] = 0x67452301 ;
//     h[1] = 0xefcdab89;
//     h[2] = 0x98badcfe;
//     h[3] = 0x10325476;
//     h[4] = 0xc3d2e1f0;
//     for( t = 0 ; t < 80 ; t++ )
//     {
//         if((t>=16)&&(t<80)) {
//             i=(t+SHA_BUFF_COUNT-3)%SHA_BUFF_COUNT;
//             tmp = w[i];
//             i=(t+SHA_BUFF_COUNT-8)%SHA_BUFF_COUNT;
//             tmp ^= w[i];
//             i=(t+SHA_BUFF_COUNT-14)%SHA_BUFF_COUNT;
//             tmp ^= w[i];
//             i=(t+SHA_BUFF_COUNT-16)%SHA_BUFF_COUNT;
//             tmp ^= w[i];
//             w[t%SHA_BUFF_COUNT] = rol(tmp,1);
//             //HDCP_DEBUG_PRINTF(("w[%2d] = %08lX\n",t,w[t%SHA_BUFF_COUNT]));
//         }
//
//         if((t>=0)&&(t<20)) {
//             tmp = rol(h[0],5) + ((h[1] & h[2]) | (h[3] & ~h[1])) + h[4] + w[t%SHA_BUFF_COUNT] + 0x5a827999;
//             //HDCP_DEBUG_PRINTF(("%08lX %08lX %08lX %08lX %08lX\n",h[0],h[1],h[2],h[3],h[4]));
//
//             h[4] = h[3];
//             h[3] = h[2];
//             h[2] = rol(h[1],30);
//             h[1] = h[0];
//             h[0] = tmp;
//
//         }
//         if((t>=20)&&(t<40)) {
//             tmp = rol(h[0],5) + (h[1] ^ h[2] ^ h[3]) + h[4] + w[t%SHA_BUFF_COUNT] + 0x6ed9eba1;
//             //HDCP_DEBUG_PRINTF(("%08lX %08lX %08lX %08lX %08lX\n",h[0],h[1],h[2],h[3],h[4]));
//             h[4] = h[3];
//             h[3] = h[2];
//             h[2] = rol(h[1],30);
//             h[1] = h[0];
//             h[0] = tmp;
//         }
//         if((t>=40)&&(t<60)) {
//             tmp = rol(h[0], 5) + ((h[1] & h[2]) | (h[1] & h[3]) | (h[2] & h[3])) + h[4] + w[t%SHA_BUFF_COUNT] +
//                 0x8f1bbcdc;
//             //HDCP_DEBUG_PRINTF(("%08lX %08lX %08lX %08lX %08lX\n",h[0],h[1],h[2],h[3],h[4]));
//             h[4] = h[3];
//             h[3] = h[2];
//             h[2] = rol(h[1],30);
//             h[1] = h[0];
//             h[0] = tmp;
//         }
//         if((t>=60)&&(t<80)) {
//             tmp = rol(h[0],5) + (h[1] ^ h[2] ^ h[3]) + h[4] + w[t%SHA_BUFF_COUNT] + 0xca62c1d6;
//             //HDCP_DEBUG_PRINTF(("%08lX %08lX %08lX %08lX %08lX\n",h[0],h[1],h[2],h[3],h[4]));
//             h[4] = h[3];
//             h[3] = h[2];
//             h[2] = rol(h[1],30);
//             h[1] = h[0];
//             h[0] = tmp;
//         }
//     }
//     HDCP_DEBUG_PRINTF(("%08lX %08lX %08lX %08lX %08lX\n",h[0],h[1],h[2],h[3],h[4]));
//
//     h[0] += 0x67452301 ;
//     h[1] += 0xefcdab89;
//     h[2] += 0x98badcfe;
//     h[3] += 0x10325476;
//     h[4] += 0xc3d2e1f0;
// //    HDCP_DEBUG_PRINTF(("%08lX %08lX %08lX %08lX %08lX\n",h[0],h[1],h[2],h[3],h[4]));
// }
//
// /* ----------------------------------------------------------------------
//  * Outer SHA algorithm: take an arbitrary length byte string,
//  * convert it into 16-word blocks with the prescribed padding at
//  * the end,and pass those blocks to the core SHA algorithm.
//  */
//
// void SHA_Simple(void *p,LONG len,BYTE *output)
// {
//     // SHA_State s;
//     int i, t ;
//     ULONG c ;
//     char *pBuff = p ;
//
//     for(i=0;i < len;i+=4)
//     {
//
//         t=i/4;
//         w[t]=0;
//         *((char *)&c)= pBuff[i];
//         *((char *)&c+1)= pBuff[i+1];
//         *((char *)&c+2)= pBuff[i+2];
//         *((char *)&c+3)= pBuff[i+3];
//         w[t]=c;
//     }
//
// 	c=0x80;
//     c<<=((3-len%4)*8);
//     w[t] |= c;
//
// /*
//     for( i = 0 ; i < len ; i++ )
//     {
//         t = i/4 ;
//         if( i%4 == 0 )
//         {
//             w[t] = 0 ;
//         }
//         c = pBuff[i] ;
//         c &= 0xFF ;
//         c <<= (3-(i%4))*8 ;
//         w[t] |= c ;
// //        HDCP_DEBUG_PRINTF(("pBuff[%d] = %02x, c = %08lX, w[%d] = %08lX\n",i,pBuff[i],c,t,w[t]));
//     }
//
//     t = i/4 ;
//     if( i%4 == 0 )
//     {
//         w[t] = 0 ;
//     }
//     c = 0x80;
//     c <<= ((3-i%4)*8);
//     w[t]|= c ;
//     */
//     t++ ;
//     for( ; t < 15 ; t++ )
//     {
//         w[t] = 0 ;
//     }
//     w[15] = len*8  ;
//
//     for( t = 0 ; t< 16 ; t++ )
//     {
//         HDCP_DEBUG_PRINTF(("w[%2d] = %08lX\n",t,w[t]));
//     }
//
//     SHATransform(sha);
//
//     for( i = 0 ; i < 5 ; i++ )
//     {
//         output[i*4] = (BYTE)(sha[i]&0xFF);
//         output[i*4+1] = (BYTE)((sha[i]>>8)&0xFF);
//         output[i*4+2] = (BYTE)((sha[i]>>16)&0xFF);
//         output[i*4+3]   = (BYTE)((sha[i]>>24)&0xFF);
//     }
// }
#endif // 0

#ifdef SUPPORT_SHA

SYS_STATUS hdmitx_hdcp_CheckSHA(BYTE pM0[],USHORT BStatus,BYTE pKSVList[],int cDownStream,BYTE Vr[])
{
    int i,n ;

    for(i = 0 ; i < cDownStream*5 ; i++)
    {
        SHABuff[i] = pKSVList[i] ;
    }
    SHABuff[i++] = BStatus & 0xFF ;
    SHABuff[i++] = (BStatus>>8) & 0xFF ;
    for(n = 0 ; n < 8 ; n++,i++)
    {
        SHABuff[i] = pM0[n] ;
    }
    n = i ;
    // SHABuff[i++] = 0x80 ; // end mask
    for(; i < 64 ; i++)
    {
        SHABuff[i] = 0 ;
    }
    // n = cDownStream * 5 + 2 /* for BStatus */ + 8 /* for M0 */ ;
    // n *= 8 ;
    // SHABuff[62] = (n>>8) & 0xff ;
    // SHABuff[63] = (n>>8) & 0xff ;
/*
    for(i = 0 ; i < 64 ; i++)
    {
        if(i % 16 == 0)
        {
            HDCP_DEBUG_PRINTF(("SHA[]: "));
        }
        HDCP_DEBUG_PRINTF((" %02X",SHABuff[i]));
        if((i%16)==15)
        {
            HDCP_DEBUG_PRINTF(("\n"));
        }
    }
    */
    SHA_Simple(SHABuff,n,V);
    for(i = 0 ; i < 20 ; i++)
    {
        if(V[i] != Vr[i])
        {
            HDCP_DEBUG_PRINTF(("V[] ="));
            for(i = 0 ; i < 20 ; i++)
            {
                HDCP_DEBUG_PRINTF((" %02X",(int)V[i]));
            }
            HDCP_DEBUG_PRINTF(("\nVr[] ="));
            for(i = 0 ; i < 20 ; i++)
            {
                HDCP_DEBUG_PRINTF((" %02X",(int)Vr[i]));
            }
            return ER_FAIL ;
        }
    }
    return ER_SUCCESS ;
}

#endif // SUPPORT_SHA

SYS_STATUS hdmitx_hdcp_GetKSVList(BYTE *pKSVList,BYTE cDownStream)
{
    BYTE TimeOut = 100 ;
    BYTE ucdata ;

    if( cDownStream == 0 )
    {
        return ER_SUCCESS ;
    }
    if( /* cDownStream == 0 || */ pKSVList == NULL)
    {
        return ER_FAIL ;
    }
    HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERHOST);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_HEADER,0x74);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_REQOFF,0x43);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_REQCOUNT,cDownStream * 5);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_CMD,CMD_DDC_SEQ_BURSTREAD);

    for(TimeOut = 200 ; TimeOut > 0 ; TimeOut --)
    {

        ucdata = HDMITX_ReadI2C_Byte(REG_TX_DDC_STATUS);
        if(ucdata & B_TX_DDC_DONE)
        {
            HDCP_DEBUG_PRINTF(("hdmitx_hdcp_GetKSVList(): DDC Done.\n"));
            break ;
        }
        if(ucdata & B_TX_DDC_ERROR)
        {
            HDCP_DEBUG_PRINTF(("hdmitx_hdcp_GetKSVList(): DDC Fail by REG_TX_DDC_STATUS = %x.\n",ucdata));
            return ER_FAIL ;
        }
        delay1ms(5);
    }
    if(TimeOut == 0)
    {
        return ER_FAIL ;
    }
    HDCP_DEBUG_PRINTF(("hdmitx_hdcp_GetKSVList(): KSV"));
    for(TimeOut = 0 ; TimeOut < cDownStream * 5 ; TimeOut++)
    {
        pKSVList[TimeOut] = HDMITX_ReadI2C_Byte(REG_TX_DDC_READFIFO);
        HDCP_DEBUG_PRINTF((" %02X",(int)pKSVList[TimeOut]));
    }
    HDCP_DEBUG_PRINTF(("\n"));
    return ER_SUCCESS ;
}

SYS_STATUS hdmitx_hdcp_GetVr(BYTE *pVr)
{
    BYTE TimeOut  ;
    BYTE ucdata ;

    if(pVr == NULL)
    {
        return ER_FAIL ;
    }
    HDMITX_WriteI2C_Byte(REG_TX_DDC_MASTER_CTRL,B_TX_MASTERHOST);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_HEADER,0x74);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_REQOFF,0x20);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_REQCOUNT,20);
    HDMITX_WriteI2C_Byte(REG_TX_DDC_CMD,CMD_DDC_SEQ_BURSTREAD);

    for(TimeOut = 200 ; TimeOut > 0 ; TimeOut --)
    {
        ucdata = HDMITX_ReadI2C_Byte(REG_TX_DDC_STATUS);
        if(ucdata & B_TX_DDC_DONE)
        {
            HDCP_DEBUG_PRINTF(("hdmitx_hdcp_GetVr(): DDC Done.\n"));
            break ;
        }
        if(ucdata & B_TX_DDC_ERROR)
        {
            HDCP_DEBUG_PRINTF(("hdmitx_hdcp_GetVr(): DDC fail by REG_TX_DDC_STATUS = %x.\n",(int)ucdata));
            return ER_FAIL ;
        }
        delay1ms(5);
    }
    if(TimeOut == 0)
    {
        HDCP_DEBUG_PRINTF(("hdmitx_hdcp_GetVr(): DDC fail by timeout.\n"));
        return ER_FAIL ;
    }
    Switch_HDMITX_Bank(0);

    for(TimeOut = 0 ; TimeOut < 5 ; TimeOut++)
    {
        HDMITX_WriteI2C_Byte(REG_TX_SHA_SEL ,TimeOut);
        pVr[TimeOut*4]  = (ULONG)HDMITX_ReadI2C_Byte(REG_TX_SHA_RD_BYTE1);
        pVr[TimeOut*4+1] = (ULONG)HDMITX_ReadI2C_Byte(REG_TX_SHA_RD_BYTE2);
        pVr[TimeOut*4+2] = (ULONG)HDMITX_ReadI2C_Byte(REG_TX_SHA_RD_BYTE3);
        pVr[TimeOut*4+3] = (ULONG)HDMITX_ReadI2C_Byte(REG_TX_SHA_RD_BYTE4);
//        HDCP_DEBUG_PRINTF(("V' = %02X %02X %02X %02X\n",(int)pVr[TimeOut*4],(int)pVr[TimeOut*4+1],(int)pVr[TimeOut*4+2],(int)pVr[TimeOut*4+3]));
    }
    return ER_SUCCESS ;
}

SYS_STATUS hdmitx_hdcp_GetM0(BYTE *pM0)
{
    int i ;

    if(!pM0)
    {
        return ER_FAIL ;
    }
    HDMITX_WriteI2C_Byte(REG_TX_SHA_SEL,5); // read m0[31:0] from reg51~reg54
    pM0[0] = HDMITX_ReadI2C_Byte(REG_TX_SHA_RD_BYTE1);
    pM0[1] = HDMITX_ReadI2C_Byte(REG_TX_SHA_RD_BYTE2);
    pM0[2] = HDMITX_ReadI2C_Byte(REG_TX_SHA_RD_BYTE3);
    pM0[3] = HDMITX_ReadI2C_Byte(REG_TX_SHA_RD_BYTE4);
    HDMITX_WriteI2C_Byte(REG_TX_SHA_SEL,0); // read m0[39:32] from reg55
    pM0[4] = HDMITX_ReadI2C_Byte(REG_TX_AKSV_RD_BYTE5);
    HDMITX_WriteI2C_Byte(REG_TX_SHA_SEL,1); // read m0[47:40] from reg55
    pM0[5] = HDMITX_ReadI2C_Byte(REG_TX_AKSV_RD_BYTE5);
    HDMITX_WriteI2C_Byte(REG_TX_SHA_SEL,2); // read m0[55:48] from reg55
    pM0[6] = HDMITX_ReadI2C_Byte(REG_TX_AKSV_RD_BYTE5);
    HDMITX_WriteI2C_Byte(REG_TX_SHA_SEL,3); // read m0[63:56] from reg55
    pM0[7] = HDMITX_ReadI2C_Byte(REG_TX_AKSV_RD_BYTE5);

    HDCP_DEBUG_PRINTF(("M[] ="));
    for(i = 0 ; i < 8 ; i++)
    {
        HDCP_DEBUG_PRINTF(("0x%02x,",(int)pM0[i]));
    }
    HDCP_DEBUG_PRINTF(("\n"));
    return ER_SUCCESS ;
}

SYS_STATUS hdmitx_hdcp_Authenticate_Repeater()
{
    BYTE uc ,ii;
    // BYTE revoked ;
    // int i ;
    BYTE cDownStream ;

    BYTE BCaps;
    USHORT BStatus ;
    USHORT TimeOut ;

    HDCP_DEBUG_PRINTF(("Authentication for repeater\n"));
    // emily add for test,abort HDCP
    // 2007/10/01 marked by jj_tseng@chipadvanced.com
    // HDMITX_WriteI2C_Byte(0x20,0x00);
    // HDMITX_WriteI2C_Byte(0x04,0x01);
    // HDMITX_WriteI2C_Byte(0x10,0x01);
    // HDMITX_WriteI2C_Byte(0x15,0x0F);
    // delay1ms(100);
    // HDMITX_WriteI2C_Byte(0x04,0x00);
    // HDMITX_WriteI2C_Byte(0x10,0x00);
    // HDMITX_WriteI2C_Byte(0x20,0x01);
    // delay1ms(100);
    // test07 = HDMITX_ReadI2C_Byte(0x7);
    // test06 = HDMITX_ReadI2C_Byte(0x6);
    // test08 = HDMITX_ReadI2C_Byte(0x8);
    //~jj_tseng@chipadvanced.com
    // end emily add for test
    //////////////////////////////////////
    // Authenticate Fired
    //////////////////////////////////////

    hdmitx_hdcp_GetBCaps(&BCaps,&BStatus);
    delay1ms(2);
    if((B_TX_INT_HPD_PLUG|B_TX_INT_RX_SENSE)&HDMITX_ReadI2C_Byte(REG_TX_INT_STAT1))
    {
        HDCP_DEBUG_PRINTF(("HPD Before Fire Auth\n"));
        goto hdmitx_hdcp_Repeater_Fail ;
    }
    hdmitx_hdcp_Auth_Fire();
    //delay1ms(550); // emily add for test
    for(ii=0;ii<55;ii++)    //delay1ms(550); // emily add for test
    {
        if((B_TX_INT_HPD_PLUG|B_TX_INT_RX_SENSE)&HDMITX_ReadI2C_Byte(REG_TX_INT_STAT1))
        {
            goto hdmitx_hdcp_Repeater_Fail ;
        }
        delay1ms(10);
    }
    for(TimeOut = /*250*6*/10 ; TimeOut > 0 ; TimeOut --)
    {
        HDCP_DEBUG_PRINTF(("TimeOut = %d wait part 1\n",TimeOut));
        if((B_TX_INT_HPD_PLUG|B_TX_INT_RX_SENSE)&HDMITX_ReadI2C_Byte(REG_TX_INT_STAT1))
        {
            HDCP_DEBUG_PRINTF(("HPD at wait part 1\n"));
            goto hdmitx_hdcp_Repeater_Fail ;
        }
        uc = HDMITX_ReadI2C_Byte(REG_TX_INT_STAT1);
        if(uc & B_TX_INT_DDC_BUS_HANG)
        {
            HDCP_DEBUG_PRINTF(("DDC Bus hang\n"));
            goto hdmitx_hdcp_Repeater_Fail ;
        }
        uc = HDMITX_ReadI2C_Byte(REG_TX_INT_STAT2);

        if(uc & B_TX_INT_AUTH_FAIL)
        {
            /*
            HDMITX_WriteI2C_Byte(REG_TX_INT_CLR0,B_TX_CLR_AUTH_FAIL);
            HDMITX_WriteI2C_Byte(REG_TX_INT_CLR1,0);
            HDMITX_WriteI2C_Byte(REG_TX_SYS_STATUS,B_TX_INTACTDONE);
            HDMITX_WriteI2C_Byte(REG_TX_SYS_STATUS,0);
            */
            HDCP_DEBUG_PRINTF(("hdmitx_hdcp_Authenticate_Repeater(): B_TX_INT_AUTH_FAIL.\n"));
            goto hdmitx_hdcp_Repeater_Fail ;
        }
        // emily add for test
        // test =(HDMITX_ReadI2C_Byte(0x7)&0x4)>>2 ;
        if(uc & B_TX_INT_KSVLIST_CHK)
        {
            HDMITX_WriteI2C_Byte(REG_TX_INT_CLR0,B_TX_CLR_KSVLISTCHK);
            HDMITX_WriteI2C_Byte(REG_TX_INT_CLR1,0);
            HDMITX_WriteI2C_Byte(REG_TX_SYS_STATUS,B_TX_INTACTDONE);
            HDMITX_WriteI2C_Byte(REG_TX_SYS_STATUS,0);
            HDCP_DEBUG_PRINTF(("B_TX_INT_KSVLIST_CHK\n"));
            break ;
        }
        delay1ms(5);
    }
    if(TimeOut == 0)
    {
        HDCP_DEBUG_PRINTF(("Time out for wait KSV List checking interrupt\n"));
        goto hdmitx_hdcp_Repeater_Fail ;
    }
    ///////////////////////////////////////
    // clear KSVList check interrupt.
    ///////////////////////////////////////

    for(TimeOut = 500 ; TimeOut > 0 ; TimeOut --)
    {
        HDCP_DEBUG_PRINTF(("TimeOut=%d at wait FIFO ready\n",TimeOut));
        if((B_TX_INT_HPD_PLUG|B_TX_INT_RX_SENSE)&HDMITX_ReadI2C_Byte(REG_TX_INT_STAT1))
        {
            HDCP_DEBUG_PRINTF(("HPD at wait FIFO ready\n"));
            goto hdmitx_hdcp_Repeater_Fail ;
        }
        if(hdmitx_hdcp_GetBCaps(&BCaps,&BStatus) == ER_FAIL)
        {
            HDCP_DEBUG_PRINTF(("Get BCaps fail\n"));
            goto hdmitx_hdcp_Repeater_Fail ;
        }
        if(BCaps & B_TX_CAP_KSV_FIFO_RDY)
        {
             HDCP_DEBUG_PRINTF(("FIFO Ready\n"));
             break ;
        }
        delay1ms(5);

    }
    if(TimeOut == 0)
    {
        HDCP_DEBUG_PRINTF(("Get KSV FIFO ready TimeOut\n"));
        goto hdmitx_hdcp_Repeater_Fail ;
    }
    HDCP_DEBUG_PRINTF(("Wait timeout = %d\n",TimeOut));

    hdmitx_ClearDDCFIFO();
    hdmitx_GenerateDDCSCLK();
    cDownStream =  (BStatus & M_TX_DOWNSTREAM_COUNT);

    if(/*cDownStream == 0 ||*/ cDownStream > 6 || BStatus & (B_TX_MAX_CASCADE_EXCEEDED|B_TX_DOWNSTREAM_OVER))
    {
        HDCP_DEBUG_PRINTF(("Invalid Down stream count,fail\n"));
        goto hdmitx_hdcp_Repeater_Fail ;
    }
#ifdef SUPPORT_SHA
    if(hdmitx_hdcp_GetKSVList(KSVList,cDownStream) == ER_FAIL)
    {
        goto hdmitx_hdcp_Repeater_Fail ;
    }
#if 0
    for(i = 0 ; i < cDownStream ; i++)
    {
        revoked=FALSE ; uc = 0 ;
        for( TimeOut = 0 ; TimeOut < 5 ; TimeOut++ )
        {
            // check bit count
            uc += countbit(KSVList[i*5+TimeOut]);
        }
        if( uc != 20 ) revoked = TRUE ;

        if(revoked)
        {
//            HDCP_DEBUG_PRINTF(("KSVFIFO[%d] = %02X %02X %02X %02X %02X is revoked\n",i,(int)KSVList[i*5],(int)KSVList[i*5+1],(int)KSVList[i*5+2],(int)KSVList[i*5+3],(int)KSVList[i*5+4]));
             goto hdmitx_hdcp_Repeater_Fail ;
        }
    }
#endif

    if(hdmitx_hdcp_GetVr(Vr) == ER_FAIL)
    {
        goto hdmitx_hdcp_Repeater_Fail ;
    }
    if(hdmitx_hdcp_GetM0(M0) == ER_FAIL)
    {
        goto hdmitx_hdcp_Repeater_Fail ;
    }
    // do check SHA
    if(hdmitx_hdcp_CheckSHA(M0,BStatus,KSVList,cDownStream,Vr) == ER_FAIL)
    {
        goto hdmitx_hdcp_Repeater_Fail ;
    }
    if((B_TX_INT_HPD_PLUG|B_TX_INT_RX_SENSE)&HDMITX_ReadI2C_Byte(REG_TX_INT_STAT1))
    {
        HDCP_DEBUG_PRINTF(("HPD at Final\n"));
        goto hdmitx_hdcp_Repeater_Fail ;
    }
#endif // SUPPORT_SHA

    HDCP_DEBUG_PRINTF(("hdmitx_hdcp_Authenticate()-receiver: Authenticate SUCESS\n"));
    hdmitx_hdcp_ResumeRepeaterAuthenticate();
    hdmiTxDev[0].bAuthenticated = TRUE ;
    return ER_SUCCESS ;

hdmitx_hdcp_Repeater_Fail:
    hdmitx_hdcp_CancelRepeaterAuthenticate();
    return ER_FAIL ;
}

//////////////////////////////////////////////////////////////////////
// Function: hdmitx_hdcp_ResumeAuthentication
// Parameter: N/A
// Return: N/A
// Remark: called by interrupt handler to restart Authentication and Encryption.
// Side-Effect: as Authentication and Encryption.
//////////////////////////////////////////////////////////////////////

void hdmitx_hdcp_ResumeAuthentication()
{
    setHDMITX_AVMute(TRUE);
    if(hdmitx_hdcp_Authenticate() == ER_SUCCESS)
    {
    }
    setHDMITX_AVMute(FALSE);
}

#endif // SUPPORT_HDCP
