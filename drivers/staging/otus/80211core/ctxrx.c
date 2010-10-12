/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*                                                                      */
/*  Module Name : htr.c                                                 */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains Tx and Rx functions.                       */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/
#include "cprecomp.h"

u16_t zfWlanRxValidate(zdev_t* dev, zbuf_t* buf);
u16_t zfWlanRxFilter(zdev_t* dev, zbuf_t* buf);



const u8_t zgSnapBridgeTunnel[6] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0xF8 };
const u8_t zgSnap8021h[6] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00 };
/* Table for converting IP DSCP P2-P0 bits to 802.11e Access Category */
const u8_t zcUpToAc[8] = {0, 1, 1, 0, 2, 2, 3, 3}; //WMM default
//const u8_t zcUpToAc[8] = {0, 1, 1, 0, 0, 0, 0, 0}; //For 2 TxQ
//const u8_t zcUpToAc[8] = {0, 0, 0, 0, 0, 0, 0, 0}; //For single TxQ
const u8_t zcMaxspToPktNum[4] = {8, 2, 4, 6};

u8_t zfGetEncryModeFromRxStatus(struct zsAdditionInfo* addInfo)
{
    u8_t securityByte;
    u8_t encryMode;

    securityByte = (addInfo->Tail.Data.SAIndex & 0xc0) >> 4;  /* byte4 */
    securityByte |= (addInfo->Tail.Data.DAIndex & 0xc0) >> 6; /* byte5 */

    switch( securityByte )
    {
        case ZM_NO_WEP:
        case ZM_WEP64:
        case ZM_WEP128:
        case ZM_WEP256:
#ifdef ZM_ENABLE_CENC
        case ZM_CENC:
#endif //ZM_ENABLE_CENC
        case ZM_TKIP:
        case ZM_AES:

            encryMode = securityByte;
            break;

        default:

            if ( (securityByte & 0xf8) == 0x08 )
            {
                // decrypted by software
            }

            encryMode = ZM_NO_WEP;
            break;
    }

    return encryMode;
}

void zfGetRxIvIcvLength(zdev_t* dev, zbuf_t* buf, u8_t vap, u16_t* pIvLen,
                        u16_t* pIcvLen, struct zsAdditionInfo* addInfo)
{
    u16_t wdsPort;
    u8_t  encryMode;

    zmw_get_wlan_dev(dev);

    *pIvLen = 0;
    *pIcvLen = 0;

    encryMode = zfGetEncryModeFromRxStatus(addInfo);

    if ( wd->wlanMode == ZM_MODE_AP )
    {
        if (vap < ZM_MAX_AP_SUPPORT)
        {
            if (( wd->ap.encryMode[vap] == ZM_WEP64 ) ||
                    ( wd->ap.encryMode[vap] == ZM_WEP128 ) ||
                    ( wd->ap.encryMode[vap] == ZM_WEP256 ))
            {
                *pIvLen = 4;
                *pIcvLen = 4;
            }
            else
            {
                u16_t id;
                u16_t addr[3];

                addr[0] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET);
                addr[1] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+2);
                addr[2] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+4);

                /* Find STA's information */
                id = zfApFindSta(dev, addr);
                if (id != 0xffff)
                {
                    if (wd->ap.staTable[id].encryMode == ZM_TKIP)
                    {
                        *pIvLen = 8;
                        *pIcvLen = 4;
                    }
                    else if (wd->ap.staTable[id].encryMode == ZM_AES)
                    {
                        *pIvLen = 8;
                        *pIcvLen = 8; // AES MIC
                        //*pIcvLen = 0;
                    }
#ifdef ZM_ENABLE_CENC
                    else if (wd->ap.staTable[id].encryMode == ZM_CENC)
                    {
                        *pIvLen = 18;
                        *pIcvLen= 16;
                    }
#endif //ZM_ENABLE_CENC
                }
            }
            /* WDS port checking */
            wdsPort = vap - 0x20;
            if (wdsPort >= ZM_MAX_WDS_SUPPORT)
            {
                wdsPort = 0;
            }

            switch (wd->ap.wds.encryMode[wdsPort])
			{
			case ZM_WEP64:
			case ZM_WEP128:
			case ZM_WEP256:
                *pIvLen = 4;
                *pIcvLen = 4;
				break;
			case ZM_TKIP:
                *pIvLen = 8;
                *pIcvLen = 4;
				break;
			case ZM_AES:
                *pIvLen = 8;
                *pIcvLen = 0;
				break;
#ifdef ZM_ENABLE_CENC
            case ZM_CENC:
                *pIvLen = 18;
                *pIcvLen = 16;
				break;
#endif //ZM_ENABLE_CENC
			}/* end of switch */
        }
    }
	else if ( wd->wlanMode == ZM_MODE_PSEUDO)
    {
        /* test: 6518 for QA auto test */
        switch (encryMode)
		{
        case ZM_WEP64:
        case ZM_WEP128:
        case ZM_WEP256:
            *pIvLen = 4;
            *pIcvLen = 4;
			break;
		case ZM_TKIP:
            *pIvLen = 8;
            *pIcvLen = 4;
			break;
		case ZM_AES:
            *pIvLen = 8;
            *pIcvLen = 0;
			break;
#ifdef ZM_ENABLE_CENC
        case ZM_CENC:
            *pIvLen = 18;
            *pIcvLen = 16;
#endif //ZM_ENABLE_CENC
		}/* end of switch */
    }
    else
    {
        if ( (encryMode == ZM_WEP64)||
             (encryMode == ZM_WEP128)||
             (encryMode == ZM_WEP256) )
        {
            *pIvLen = 4;
            *pIcvLen = 4;
        }
        else if ( encryMode == ZM_TKIP )
        {
            *pIvLen = 8;
            *pIcvLen = 4;
        }
        else if ( encryMode == ZM_AES )
        {
            *pIvLen = 8;
            *pIcvLen = 8; // AES MIC
        }
#ifdef ZM_ENABLE_CENC
        else if ( encryMode == ZM_CENC)
        {
            *pIvLen = 18;
            *pIcvLen= 16;
        }
#endif //ZM_ENABLE_CENC
    }
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAgingDefragList           */
/*      Force flushing whole defrag list or aging the buffer            */
/*      in the defrag list.                                             */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      flushFlag : 1=>flushing, 0=>Aging                               */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.1      */
/*                                                                      */
/************************************************************************/
void zfAgingDefragList(zdev_t* dev, u16_t flushFlag)
{
    u16_t i, j;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    for(i=0; i<ZM_MAX_DEFRAG_ENTRIES; i++)
    {
        if (wd->defragTable.defragEntry[i].fragCount != 0 )
        {
            if (((wd->tick - wd->defragTable.defragEntry[i].tick) >
                        (ZM_DEFRAG_AGING_TIME_SEC * ZM_TICK_PER_SECOND))
               || (flushFlag != 0))
            {
                zm_msg1_rx(ZM_LV_2, "Aging defrag list :", i);
                /* Free the buffers in the defrag list */
                for (j=0; j<wd->defragTable.defragEntry[i].fragCount; j++)
                {
                    zfwBufFree(dev, wd->defragTable.defragEntry[i].fragment[j], 0);
                }
            }
        }
        wd->defragTable.defragEntry[i].fragCount = 0;
    }

    zmw_leave_critical_section(dev);

    return;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAddFirstFragToDefragList  */
/*      Add first fragment to defragment list, the first empty entry    */
/*      will be selected. If the list is full, sequentially select      */
/*      one entry for replacement.                                      */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : first fragment buffer                                     */
/*      addr : address of first fragment buffer                         */
/*      seqNum : sequence of first fragment buffer                      */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.1      */
/*                                                                      */
/************************************************************************/
void zfAddFirstFragToDefragList(zdev_t* dev, zbuf_t* buf, u8_t* addr, u16_t seqNum)
{
    u16_t i, j;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    /* Find an empty one in defrag list */
    for(i=0; i<ZM_MAX_DEFRAG_ENTRIES; i++)
    {
        if ( wd->defragTable.defragEntry[i].fragCount == 0 )
        {
            break;
        }
    }

    /* If full, sequentially replace existing one */
    if (i == ZM_MAX_DEFRAG_ENTRIES)
    {
        i = wd->defragTable.replaceNum++ & (ZM_MAX_DEFRAG_ENTRIES-1);
        /* Free the buffers in the defrag list to be replaced */
        for (j=0; j<wd->defragTable.defragEntry[i].fragCount; j++)
        {
            zfwBufFree(dev, wd->defragTable.defragEntry[i].fragment[j], 0);
        }
    }

    wd->defragTable.defragEntry[i].fragCount = 1;
    wd->defragTable.defragEntry[i].fragment[0] = buf;
    wd->defragTable.defragEntry[i].seqNum = seqNum;
    wd->defragTable.defragEntry[i].tick = wd->tick;

    for (j=0; j<6; j++)
    {
        wd->defragTable.defragEntry[i].addr[j] = addr[j];
    }

    zmw_leave_critical_section(dev);

    return;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfAddFragToDefragList       */
/*      Add middle or last fragment to defragment list.                 */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : first fragment buffer                                     */
/*      addr : address of fragment buffer                               */
/*      seqNum : sequence fragment buffer                               */
/*      fragNum : fragment number of fragment buffer                    */
/*      moreFrag : more frag bit of fragment buffer                     */
/*      addInfo : addition info of fragment buffer                      */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.1      */
/*                                                                      */
/************************************************************************/
zbuf_t* zfAddFragToDefragList(zdev_t* dev, zbuf_t* buf, u8_t* addr,
        u16_t seqNum, u8_t fragNum, u8_t moreFrag,
        struct zsAdditionInfo* addInfo)
{
    u16_t i, j, k;
    zbuf_t* returnBuf = NULL;
    u16_t defragDone = 0;
    u16_t lenErr = 0;
    u16_t startAddr, fragHead, frameLen, ivLen, icvLen;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    /* Find frag in the defrag list */
    for(i=0; i<ZM_MAX_DEFRAG_ENTRIES; i++)
    {
        if ( wd->defragTable.defragEntry[i].fragCount != 0 )
        {
            /* Compare address */
            for (j=0; j<6; j++)
            {
                if (addr[j] != wd->defragTable.defragEntry[i].addr[j])
                {
                    break;
                }
            }
            if (j == 6)
            {
                /* Compare sequence and fragment number */
                if (seqNum == wd->defragTable.defragEntry[i].seqNum)
                {
                    if ((fragNum == wd->defragTable.defragEntry[i].fragCount)
                                && (fragNum < 8))
                    {
                        /* Add frag frame to defrag list */
                        wd->defragTable.defragEntry[i].fragment[fragNum] = buf;
                        wd->defragTable.defragEntry[i].fragCount++;
                        defragDone = 1;

                        if (moreFrag == 0)
                        {
                            /* merge all fragment if more data bit is cleared */
                            returnBuf = wd->defragTable.defragEntry[i].fragment[0];
                            startAddr = zfwBufGetSize(dev, returnBuf);
                            /* skip WLAN header 24(Data) or 26(QoS Data) */
                            fragHead = 24 + ((zmw_rx_buf_readh(dev, returnBuf, 0) & 0x80) >> 6);
                            zfGetRxIvIcvLength(dev, returnBuf, 0, &ivLen, &icvLen, addInfo);
                            fragHead += ivLen; /* skip IV */
                            for(k=1; k<wd->defragTable.defragEntry[i].fragCount; k++)
                            {
                                frameLen = zfwBufGetSize(dev,
                                                         wd->defragTable.defragEntry[i].fragment[k]);
                                if ((startAddr+frameLen-fragHead) < 1560)
                                {
                                    zfRxBufferCopy(dev, returnBuf, wd->defragTable.defragEntry[i].fragment[k],
                                               startAddr, fragHead, frameLen-fragHead);
                                    startAddr += (frameLen-fragHead);
                                }
                                else
                                {
                                    lenErr = 1;
                                }
                                zfwBufFree(dev, wd->defragTable.defragEntry[i].fragment[k], 0);
                            }

                            wd->defragTable.defragEntry[i].fragCount = 0;
                            zfwBufSetSize(dev, returnBuf, startAddr);
                        }
                        break;
                    }
                }
            }
        }
    }

    zmw_leave_critical_section(dev);

    if (lenErr == 1)
    {
        zfwBufFree(dev, returnBuf, 0);
        return NULL;
    }
    if (defragDone == 0)
    {
        zfwBufFree(dev, buf, 0);
        return NULL;
    }

    return returnBuf;
}


/* return value = NULL => save or free this frame         */
zbuf_t* zfDefragment(zdev_t* dev, zbuf_t* buf, u8_t* pbIsDefrag,
                     struct zsAdditionInfo* addInfo)
{
    u8_t fragNum;
    u16_t seqNum;
    u8_t moreFragBit;
    u8_t addr[6];
    u16_t i;
    zmw_get_wlan_dev(dev);

    ZM_BUFFER_TRACE(dev, buf)

    *pbIsDefrag = FALSE;
    seqNum = zmw_buf_readh(dev, buf, 22);
    fragNum = (u8_t)(seqNum & 0xf);
    moreFragBit = (zmw_buf_readb(dev, buf, 1) & ZM_BIT_2) >> 2;

    if ((fragNum == 0) && (moreFragBit == 0))
    {
        /* Not part of a fragmentation */

        return buf;
    }
    else
    {
        wd->commTally.swRxFragmentCount++;
        seqNum = seqNum >> 4;
        for (i=0; i<6; i++)
        {
            addr[i] = zmw_rx_buf_readb(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+i);
        }

        if (fragNum == 0)
        {
            /* more frag = 1 */
            /* First part of a fragmentation */
            zm_msg1_rx(ZM_LV_2, "First Frag, seq=", seqNum);
            zfAddFirstFragToDefragList(dev, buf, addr, seqNum);
            buf = NULL;
        }
        else
        {
            /* Middle or last part of a fragmentation */
            zm_msg1_rx(ZM_LV_2, "Frag seq=", seqNum);
            zm_msg1_rx(ZM_LV_2, "Frag moreFragBit=", moreFragBit);
            buf = zfAddFragToDefragList(dev, buf, addr, seqNum, fragNum, moreFragBit, addInfo);
            if (buf != NULL)
            {
                *pbIsDefrag = TRUE;
            }
        }
    }

    return buf;
}


#if ZM_PROTOCOL_RESPONSE_SIMULATION
u16_t zfSwap(u16_t num)
{
    return ((num >> 8) + ((num & 0xff) << 8));
}


void zfProtRspSim(zdev_t* dev, zbuf_t* buf)
{
    u16_t ethType;
    u16_t arpOp;
    u16_t prot;
    u16_t temp;
    u16_t i;
    u16_t dip[2];
    u16_t dstPort;
    u16_t srcPort;

    ethType = zmw_rx_buf_readh(dev, buf, 12);
    zm_msg2_rx(ZM_LV_2, "ethType=", ethType);

    /* ARP */
    if (ethType == 0x0608)
    {
        arpOp = zmw_rx_buf_readh(dev, buf, 20);
        dip[0] = zmw_rx_buf_readh(dev, buf, 38);
        dip[1] = zmw_rx_buf_readh(dev, buf, 40);
        zm_msg2_rx(ZM_LV_2, "arpOp=", arpOp);
        zm_msg2_rx(ZM_LV_2, "ip0=", dip[0]);
        zm_msg2_rx(ZM_LV_2, "ip1=", dip[1]);

        //ARP request to 192.168.1.15
        if ((arpOp == 0x0100) && (dip[0] == 0xa8c0) && (dip[1] == 0x0f01)) {
            zm_msg0_rx(ZM_LV_2, "ARP");
            /* ARP response */
            zmw_rx_buf_writeh(dev, buf, 20, 0x0200);

            /* dst hardware address */

            /* src hardware address */
            //zmw_rx_buf_writeh(dev, buf, 6, 0xa000);
            //zmw_rx_buf_writeh(dev, buf, 8, 0x0000);
            //zmw_rx_buf_writeh(dev, buf, 10, 0x0000);

            /* dst ip address */
            for (i=0; i<5; i++)
            {
                temp = zmw_rx_buf_readh(dev, buf, 22+(i*2));
                zmw_rx_buf_writeh(dev, buf, 32+(i*2), temp);
            }

            /* src hardware address */
            zmw_rx_buf_writeh(dev, buf, 22, 0xa000);
            zmw_rx_buf_writeh(dev, buf, 24, 0x0000);
            zmw_rx_buf_writeh(dev, buf, 26, 0x0000);

            /* src ip address */
            zmw_rx_buf_writeh(dev, buf, 28, 0xa8c0);
            zmw_rx_buf_writeh(dev, buf, 30, 0x0f01);
        }
    }
    /* ICMP */
    else if (ethType == 0x0008)
    {
        zm_msg0_rx(ZM_LV_2, "IP");
        prot = zmw_rx_buf_readb(dev, buf, 23);
        dip[0] = zmw_rx_buf_readh(dev, buf, 30);
        dip[1] = zmw_rx_buf_readh(dev, buf, 32);
        zm_msg2_rx(ZM_LV_2, "prot=", prot);
        zm_msg2_rx(ZM_LV_2, "ip0=", dip[0]);
        zm_msg2_rx(ZM_LV_2, "ip1=", dip[1]);

        /* PING request to 192.168.1.15 */
        if ((prot == 0x1) && (dip[0] == 0xa8c0) && (dip[1] == 0x0f01))
        {
            zm_msg0_rx(ZM_LV_2, "ICMP");
            /* change dst */
            for (i=0; i<3; i++)
            {
                temp = zmw_rx_buf_readh(dev, buf, 6+(i*2));
                zmw_rx_buf_writeh(dev, buf, i*2, temp);
            }
            /* change src */
            zmw_rx_buf_writeh(dev, buf, 6, 0xa000);
            zmw_rx_buf_writeh(dev, buf, 8, 0x0000);
            zmw_rx_buf_writeh(dev, buf, 10, 0x0000);

            /* exchange src ip and dst ip */
            for (i=0; i<2; i++)
            {
                temp = zmw_rx_buf_readh(dev, buf, 26+(i*2));
                zmw_rx_buf_writeh(dev, buf, 30+(i*2), temp);
            }
            zmw_rx_buf_writeh(dev, buf, 26, 0xa8c0);
            zmw_rx_buf_writeh(dev, buf, 28, 0x0f01);

            /* change icmp type to echo reply */
            zmw_rx_buf_writeb(dev, buf, 34, 0x0);

            /* update icmp checksum */
            temp = zmw_rx_buf_readh(dev, buf, 36);
            temp += 8;
            zmw_rx_buf_writeh(dev, buf, 36, temp);
        }
        else if (prot == 0x6)
        {
            zm_msg0_rx(ZM_LV_2, "TCP");
            srcPort = zmw_rx_buf_readh(dev, buf, 34);
            dstPort = zmw_rx_buf_readh(dev, buf, 36);
            zm_msg2_rx(ZM_LV_2, "Src Port=", srcPort);
            zm_msg2_rx(ZM_LV_2, "Dst Port=", dstPort);
            if ((dstPort == 0x1500) || (srcPort == 0x1500))
            {
                zm_msg0_rx(ZM_LV_2, "FTP");

                /* change dst */
                for (i=0; i<3; i++)
                {
                    temp = zmw_rx_buf_readh(dev, buf, 6+(i*2));
                    zmw_rx_buf_writeh(dev, buf, i*2, temp);
                }
                /* change src */
                zmw_rx_buf_writeh(dev, buf, 6, 0xa000);
                zmw_rx_buf_writeh(dev, buf, 8, 0x0000);
                zmw_rx_buf_writeh(dev, buf, 10, 0x0000);

                /* exchange src ip and dst ip */
                for (i=0; i<2; i++)
                {
                    temp = zmw_rx_buf_readh(dev, buf, 26+(i*2));
                    zmw_rx_buf_writeh(dev, buf, 30+(i*2), temp);
                }
                zmw_rx_buf_writeh(dev, buf, 26, 0xa8c0);
                zmw_rx_buf_writeh(dev, buf, 28, 0x0f01);
#if 0
                /* Patch src port */
                temp = zmw_rx_buf_readh(dev, buf, 34);
                temp = zfSwap(zfSwap(temp) + 1);
                zmw_rx_buf_writeh(dev, buf, 34, temp);
                temp = zmw_rx_buf_readh(dev, buf, 38);
                temp = zfSwap(zfSwap(temp) + 1);
                zmw_rx_buf_writeh(dev, buf, 38, temp);

                /* Patch checksum */
                temp = zmw_rx_buf_readh(dev, buf, 50);
                temp = zfSwap(temp);
                temp = ~temp;
                temp += 2;
                temp = ~temp;
                temp = zfSwap(temp);
                zmw_rx_buf_writeh(dev, buf, 50, temp);
#endif
            }

        }
        else if (prot == 0x11)
        {
            /* change dst */
            for (i=0; i<3; i++)
            {
                temp = zmw_rx_buf_readh(dev, buf, 6+(i*2));
                zmw_rx_buf_writeh(dev, buf, i*2, temp);
            }
            /* change src */
            zmw_rx_buf_writeh(dev, buf, 6, 0xa000);
            zmw_rx_buf_writeh(dev, buf, 8, 0x0000);
            zmw_rx_buf_writeh(dev, buf, 10, 0x0000);

            zm_msg0_rx(ZM_LV_2, "UDP");
            srcPort = zmw_rx_buf_readh(dev, buf, 34);
            dstPort = zmw_rx_buf_readh(dev, buf, 36);
            zm_msg2_rx(ZM_LV_2, "Src Port=", srcPort);
            zm_msg2_rx(ZM_LV_2, "Dst Port=", dstPort);

            /* exchange src ip and dst ip */
            for (i=0; i<2; i++)
            {
                temp = zmw_rx_buf_readh(dev, buf, 26+(i*2));
                zmw_rx_buf_writeh(dev, buf, 30+(i*2), temp);
            }
            zmw_rx_buf_writeh(dev, buf, 26, 0xa8c0);
            zmw_rx_buf_writeh(dev, buf, 28, 0x0f01);

            /* exchange port */
            zmw_rx_buf_writeh(dev, buf, 34, srcPort+1);
            zmw_rx_buf_writeh(dev, buf, 36, dstPort);

            /* checksum = 0 */
            zmw_rx_buf_writeh(dev, buf, 40, 0);
        }

    }
    else if (ethType == 0x0060) /* =>0x0060 is port */
    {
        /* change src for Evl tool loop back receive */
        zmw_rx_buf_writeh(dev, buf, 6, 0xa000);
        zmw_rx_buf_writeh(dev, buf, 8, 0x0000);
        zmw_rx_buf_writeh(dev, buf, 10, 0x0000);
    }

}
#endif

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfiTxSendEth                */
/*      Called to native 802.11 management frames                       */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer pointer                                            */
/*      port : WLAN port, 0=>standard, 0x1-0x7=>VAP, 0x20-0x25=>WDS     */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      error code                                                      */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Ray             ZyDAS Technology Corporation    2005.5      */
/*                                                                      */
/************************************************************************/
u16_t zfiTxSend80211Mgmt(zdev_t* dev, zbuf_t* buf, u16_t port)
{
    u16_t err;
    //u16_t addrTblSize = 0;
    //struct zsAddrTbl addrTbl;
    u16_t hlen;
    u16_t header[(24+25+1)/2];
    int i;

    for(i=0;i<12;i++)
    {
        header[i] = zmw_buf_readh(dev, buf, i);
    }
    hlen = 24;

    zfwBufRemoveHead(dev, buf, 24);

    err = zfHpSend(dev, header, hlen, NULL, 0, NULL, 0, buf, 0,
		   ZM_EXTERNAL_ALLOC_BUF, 0, 0);
    if (err != ZM_SUCCESS)
    {
        goto zlError;
    }

    return 0;

zlError:

    zfwBufFree(dev, buf, 0);
    return 0;
}

u8_t zfiIsTxQueueFull(zdev_t* dev)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    if ((((wd->vtxqHead[0] + 1) & ZM_VTXQ_SIZE_MASK) != wd->vtxqTail[0]) )
    {
        zmw_leave_critical_section(dev);
        return 0;
    }
    else
    {
        zmw_leave_critical_section(dev);
        return 1;
    }
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfiTxSendEth                */
/*      Called to transmit Ethernet frame from upper layer.             */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer pointer                                            */
/*      port : WLAN port, 0=>standard, 0x1-0x7=>VAP, 0x20-0x25=>WDS     */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      error code                                                      */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen             ZyDAS Technology Corporation    2005.5      */
/*                                                                      */
/************************************************************************/
u16_t zfiTxSendEth(zdev_t* dev, zbuf_t* buf, u16_t port)
{
    u16_t err, ret;

    zmw_get_wlan_dev(dev);

    ZM_PERFORMANCE_TX_MSDU(dev, wd->tick);
    zm_msg1_tx(ZM_LV_2, "zfiTxSendEth(), port=", port);
    /* Return error if port is disabled */
    err = zfTxPortControl(dev, buf, port);
    if (err == ZM_PORT_DISABLED)
    {
        err = ZM_ERR_TX_PORT_DISABLED;
        goto zlError;
    }

#if 1
    if ((wd->wlanMode == ZM_MODE_AP) && (port < 0x20))
    {
        /* AP : Buffer frame for power saving STA */
        ret = zfApBufferPsFrame(dev, buf, port);
        if (ret == 1)
        {
            return ZM_SUCCESS;
        }
    }
    else
#endif
    if (wd->wlanMode == ZM_MODE_INFRASTRUCTURE)
    {
        if ( zfPowerSavingMgrIsSleeping(dev) )
        {
            /*check ZM_ENABLE_POWER_SAVE flag*/
            zfPowerSavingMgrWakeup(dev);
        }
    }
#ifdef ZM_ENABLE_IBSS_PS
    /* IBSS power-saving mode */
    else if ( wd->wlanMode == ZM_MODE_IBSS )
    {
        if ( zfStaIbssPSQueueData(dev, buf) )
        {
            return ZM_SUCCESS;
        }
    }
#endif

#if 1
    //if ( wd->bQoSEnable )
    if (1)
    {
        /* Put to VTXQ[ac] */
        ret = zfPutVtxq(dev, buf);

        /* Push VTXQ[ac] */
        zfPushVtxq(dev);
    }
    else
    {
        ret = zfTxSendEth(dev, buf, port, ZM_EXTERNAL_ALLOC_BUF, 0);
    }

    return ret;
#else
    return zfTxSendEth(dev, buf, port, ZM_EXTERNAL_ALLOC_BUF, 0);
#endif

zlError:
    zm_msg2_tx(ZM_LV_1, "Tx Comp err=", err);

    zfwBufFree(dev, buf, err);
    return err;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfTxSendEth                 */
/*      Called to transmit Ethernet frame from upper layer.             */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : buffer pointer                                            */
/*      port : WLAN port, 0=>standard, 0x10-0x17=>VAP, 0x20-0x25=>WDS   */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      error code                                                      */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen             ZyDAS Technology Corporation    2005.5      */
/*                                                                      */
/************************************************************************/
u16_t zfTxSendEth(zdev_t* dev, zbuf_t* buf, u16_t port, u16_t bufType, u16_t flag)
{
    //u16_t addrTblSize;
    //struct zsAddrTbl addrTbl;
    u16_t removeLen;
    u16_t header[(8+30+2+18)/2];    /* ctr+(4+a1+a2+a3+2+a4)+qos+iv */
    u16_t headerLen;
    u16_t mic[8/2];
    u16_t micLen;
    u16_t snap[8/2];
    u16_t snapLen;
    u16_t fragLen;
    u16_t frameLen;
    u16_t fragNum;
    struct zsFrag frag;
    u16_t i, j, id;
    u16_t offset;
    u16_t da[3];
    u16_t sa[3];
    u8_t up;
    u8_t qosType, keyIdx = 0;
    u16_t fragOff;
    u16_t newFlag;
    u8_t tkipFrameOffset = 0;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    newFlag = flag & 0xff00;
    flag = flag & 0xff;

    zm_msg1_tx(ZM_LV_2, "zfTxSendEth(), port=", port);

    /* Get IP TOS for QoS AC and IP frag offset */
    zfTxGetIpTosAndFrag(dev, buf, &up, &fragOff);

    //EOSP bit
    if (newFlag & 0x100)
    {
        up |= 0x10;
    }

#ifdef ZM_ENABLE_NATIVE_WIFI
    if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
    {
        /* DA */
        da[0] = zmw_tx_buf_readh(dev, buf, 16);
        da[1] = zmw_tx_buf_readh(dev, buf, 18);
        da[2] = zmw_tx_buf_readh(dev, buf, 20);
        /* SA */
        sa[0] = zmw_tx_buf_readh(dev, buf, 10);
        sa[1] = zmw_tx_buf_readh(dev, buf, 12);
        sa[2] = zmw_tx_buf_readh(dev, buf, 14);
    }
    else if ( wd->wlanMode == ZM_MODE_IBSS )
    {
        /* DA */
        da[0] = zmw_tx_buf_readh(dev, buf, 4);
        da[1] = zmw_tx_buf_readh(dev, buf, 6);
        da[2] = zmw_tx_buf_readh(dev, buf, 8);
        /* SA */
        sa[0] = zmw_tx_buf_readh(dev, buf, 10);
        sa[1] = zmw_tx_buf_readh(dev, buf, 12);
        sa[2] = zmw_tx_buf_readh(dev, buf, 14);
    }
    else if ( wd->wlanMode == ZM_MODE_AP )
    {
        /* DA */
        da[0] = zmw_tx_buf_readh(dev, buf, 4);
        da[1] = zmw_tx_buf_readh(dev, buf, 6);
        da[2] = zmw_tx_buf_readh(dev, buf, 8);
        /* SA */
        sa[0] = zmw_tx_buf_readh(dev, buf, 16);
        sa[1] = zmw_tx_buf_readh(dev, buf, 18);
        sa[2] = zmw_tx_buf_readh(dev, buf, 20);
    }
    else
    {
        //
    }
#else
    /* DA */
    da[0] = zmw_tx_buf_readh(dev, buf, 0);
    da[1] = zmw_tx_buf_readh(dev, buf, 2);
    da[2] = zmw_tx_buf_readh(dev, buf, 4);
    /* SA */
    sa[0] = zmw_tx_buf_readh(dev, buf, 6);
    sa[1] = zmw_tx_buf_readh(dev, buf, 8);
    sa[2] = zmw_tx_buf_readh(dev, buf, 10);
#endif
    //Decide Key Index in ATOM, No meaning in OTUS--CWYang(m)
    if (wd->wlanMode == ZM_MODE_AP)
    {
        keyIdx = wd->ap.bcHalKeyIdx[port];
        id = zfApFindSta(dev, da);
        if (id != 0xffff)
        {
            switch (wd->ap.staTable[id].encryMode)
            {
            case ZM_AES:
            case ZM_TKIP:
#ifdef ZM_ENABLE_CENC
            case ZM_CENC:
#endif //ZM_ENABLE_CENC
                keyIdx = wd->ap.staTable[id].keyIdx;
                break;
            }
        }
    }
    else
    {
        switch (wd->sta.encryMode)
        {
        case ZM_WEP64:
        case ZM_WEP128:
        case ZM_WEP256:
            keyIdx = wd->sta.keyId;
            break;
        case ZM_AES:
        case ZM_TKIP:
            if ((da[0] & 0x1))
                keyIdx = 5;
            else
                keyIdx = 4;
            break;
#ifdef ZM_ENABLE_CENC
        case ZM_CENC:
            keyIdx = wd->sta.cencKeyId;
            break;
#endif //ZM_ENABLE_CENC
        }
    }

    /* Create SNAP */
    removeLen = zfTxGenWlanSnap(dev, buf, snap, &snapLen);
    //zm_msg1_tx(ZM_LV_0, "fragOff=", fragOff);


/* ********************************************************************************************** */
/* Add 20071025 Mxzeng                                                                            */
/* ********************************************************************************************** */
/* ---------------------------------------------------------------------------------------------- */
/*  Ethernet : frameLen = zfwBufGetSize(dev, buf);                                                */
/* ---+--6--+--6--+--2--+-----20-----+-------------------------+------ Variable -------+--------- */
/*    |  DA |  SA | Type|  IP Header | TCP(20) UDP(12) ICMP(8) | Application Payload L |          */
/* ---+-----+-----+-----+------------+-------------------------+-----------------------+--------- */
/*  MSDU = 6 + 6 + 2 + ( Network Layer header ) + ( Transport Layer header ) + L                  */
/*                                                                                                */
/*  MSDU - DA - SA : frameLen -= removeLen;                                                       */
/* ---+--2--+-----20-----+-------------------------+------ Variable -------+--------------------- */
/*    | Type| IP Header  | TCP(20) UDP(12) ICMP(8) | Application Payload L |                      */
/* ---+-----+------------+-------------------------+-----------------------+--------------------- */
/*												  */
/*  MPDU : frameLen + mpduLengthOffset ;                                                          */
/* -+---2---+----2---+-6-+-6-+--6--+---2----+--1--+--1-+---1---+-------3------+-frameLen-+---4--+- */
/*  | frame |duration| DA|SA |BSSID|sequence|SNAP |SNAP|Control|    RFC 1042  |          |  FCS |  */
/*  |Control|        |   |   |     | number |DSAP |SSAP|       | encapsulation|          |      |  */
/* -+-------+--------+---+---+-----+--------+-----+----+-------+--------------+----------+------+- */
/* ----------------------------------------------------------------------------------------------- */

    if ( wd->sta.encryMode == ZM_TKIP )
        tkipFrameOffset = 8;

    fragLen = wd->fragThreshold + tkipFrameOffset;   // Fragmentation threshold for MPDU Lengths
    frameLen = zfwBufGetSize(dev, buf);    // MSDU Lengths
    frameLen -= removeLen;                 // MSDU Lengths - DA - SA

    /* #1st create MIC Length manually */
    micLen = 0;

    /* Access Category */
    if (wd->wlanMode == ZM_MODE_AP)
    {
        zfApGetStaQosType(dev, da, &qosType);
        if (qosType == 0)
        {
            up = 0;
        }
    }
    else if (wd->wlanMode == ZM_MODE_INFRASTRUCTURE)
    {
        if (wd->sta.wmeConnected == 0)
        {
            up = 0;
        }
    }
    else
    {
        /* TODO : STA QoS control field */
        up = 0;
    }

    /* #2nd Assign sequence number */
    zmw_enter_critical_section(dev);
    frag.seq[0] = ((wd->seq[zcUpToAc[up&0x7]]++) << 4);
    zmw_leave_critical_section(dev);

    /* #3rd Pass the total payload to generate MPDU length ! */
    frag.buf[0] = buf;
    frag.bufType[0] = bufType;
    frag.flag[0] = (u8_t)flag;
    fragNum = 1;

    headerLen = zfTxGenWlanHeader(dev, frag.buf[0], header, frag.seq[0],
                                  frag.flag[0], snapLen+micLen, removeLen, port, da, sa,
                                  up, &micLen, snap, snapLen, NULL);

    //zm_debug_msg1("#1 headerLen = ", headerLen);

    /* #4th Check the HeaderLen and determine whether the MPDU Lengths bigger than Fragmentation threshold  */
    /* If MPDU Lengths large than fragmentation threshold --> headerLen = 0 */
    if( headerLen != 0 )
    {
        zf80211FrameSend(dev, frag.buf[0], header, snapLen, da, sa, up,
                         headerLen, snap, mic, micLen, removeLen, frag.bufType[0],
                         zcUpToAc[up&0x7], keyIdx);
    }
    else //if( headerLen == 0 ) // Need to be fragmented
    {
        u16_t mpduLengthOffset;
        u16_t pseudSnapLen = 0;

        mpduLengthOffset = header[0] - frameLen; // For fragmentation threshold !

        micLen = zfTxGenWlanTail(dev, buf, snap, snapLen, mic); // Get snap and mic information

        fragLen = fragLen - mpduLengthOffset;

        //zm_debug_msg1("#2 frameLen = ", frameLen);
        //zm_debug_msg1("#3 fragThreshold = ", fragLen);

        /* fragmentation */
        if (frameLen >= fragLen)
        {
            //copy fragLen to frag
            i = 0;
            while( frameLen > 0 )
            {
                frag.buf[i] = zfwBufAllocate(dev, fragLen+32);
                if (frag.buf[i] != NULL)
                {
                    frag.bufType[i] = ZM_INTERNAL_ALLOC_BUF;
                    frag.seq[i] = frag.seq[0] + i;
                    offset = removeLen + i*fragLen;

                    /* Consider the offset if we consider snap length to the other fragmented frame */
                    if ( i >= 1 )
                        offset = offset + pseudSnapLen*(i-1);

                    if (frameLen > fragLen + pseudSnapLen)
                    {
                        frag.flag[i] = flag | 0x4; /* More data */
                        /* First fragment */
                        if (i == 0)
                        {
                            /* Add SNAP */
                            for (j=0; j<snapLen; j+=2)
                            {
                                zmw_tx_buf_writeh(dev, frag.buf[i], j, snap[(j>>1)]);
                            }
                            zfTxBufferCopy(dev, frag.buf[i], buf, snapLen, offset, fragLen);
                            zfwBufSetSize(dev, frag.buf[i], snapLen+fragLen);

                            /* Add pseud snap length to the other fragmented frame */
                            pseudSnapLen = snapLen;

                            frameLen -= fragLen;
                        }
                        /* Intermediate Fragment */
                        else
                        {
                            //zfTxBufferCopy(dev, frag.buf[i], buf, 0, offset, fragLen);
                            //zfwBufSetSize(dev, frag.buf[i], fragLen);

                            zfTxBufferCopy(dev, frag.buf[i], buf, 0, offset, fragLen+pseudSnapLen );
                            zfwBufSetSize(dev, frag.buf[i], fragLen+pseudSnapLen);

                            frameLen -= (fragLen+pseudSnapLen);
                        }
                        //frameLen -= fragLen;
                    }
                    else
                    {
                        /* Last fragment  */
                        zfTxBufferCopy(dev, frag.buf[i], buf, 0, offset, frameLen);
                        /* Add MIC if need */
                        if ( micLen )
                        {
                            zfCopyToRxBuffer(dev, frag.buf[i], (u8_t*) mic, frameLen, micLen);
                        }
                        zfwBufSetSize(dev, frag.buf[i], frameLen+micLen);
                        frameLen = 0;
                        frag.flag[i] = (u8_t)flag; /* No more data */
                    }
                    i++;
                }
                else
                {
                    break;
                }

                // Please pay attention to the index of the buf !!!
                // If write to null buf , the OS will crash !!!
                zfwCopyBufContext(dev, buf, frag.buf[i-1]);
            }
            fragNum = i;
            snapLen = micLen = removeLen = 0;

            zfwBufFree(dev, buf, 0);
        }

        for (i=0; i<fragNum; i++)
        {
            /* Create WLAN header(Control Setting + 802.11 header + IV) */
            headerLen = zfTxGenWlanHeader(dev, frag.buf[i], header, frag.seq[i],
                                    frag.flag[i], snapLen+micLen, removeLen, port, da, sa, up, &micLen,
                                    snap, snapLen, NULL);

            zf80211FrameSend(dev, frag.buf[i], header, snapLen, da, sa, up,
                             headerLen, snap, mic, micLen, removeLen, frag.bufType[i],
                             zcUpToAc[up&0x7], keyIdx);

        } /* for (i=0; i<fragNum; i++) */
    }

    return ZM_SUCCESS;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfTxPortControl             */
/*      Check port status.                                              */
/*                                                                      */
/*    INPUTS                                                            */
/*      buf : buffer pointer                                            */
/*      port : port number, 0=>standard, 10-17=>Virtual AP, 20-25=>WDS  */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      ZM_PORT_ENABLED or ZM_PORT_DISABLE                              */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Signature           ZyDAS Technology Corporation    2005.4      */
/*                                                                      */
/************************************************************************/
u16_t zfTxPortControl(zdev_t* dev, zbuf_t* buf, u16_t port)
{
    zmw_get_wlan_dev(dev);

    if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
    {
        if ( wd->sta.adapterState == ZM_STA_STATE_DISCONNECT )
        {
            zm_msg0_tx(ZM_LV_3, "Packets dropped due to disconnect state");
            return ZM_PORT_DISABLED;
        }
    }

    return ZM_PORT_ENABLED;
}



/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfIdlRecv                   */
/*      Do frame validation and filtering then pass to zfwRecv80211().  */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : received 802.11 frame buffer.                             */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen             ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
void zfCoreRecv(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* addInfo)
{
    u16_t ret = 0;
    u16_t bssid[3];
    struct agg_tid_rx *tid_rx;
    zmw_get_wlan_dev(dev);

    ZM_BUFFER_TRACE(dev, buf)

    /* tally */
    wd->commTally.DriverRxFrmCnt++;

    bssid[0] = zmw_buf_readh(dev, buf, 16);
    bssid[1] = zmw_buf_readh(dev, buf, 18);
    bssid[2] = zmw_buf_readh(dev, buf, 20);

    /* Validate Rx frame */
    ret = zfWlanRxValidate(dev, buf);
    if (ret != ZM_SUCCESS)
    {
        zm_msg1_rx(ZM_LV_1, "Rx invalid:", ret);
        goto zlError;
    }

#ifdef ZM_ENABLE_AGGREGATION
    //#ifndef ZM_ENABLE_FW_BA_RETRANSMISSION
    /*
     * add by honda
     */
    tid_rx = zfAggRxEnabled(dev, buf);
    if (tid_rx && wd->reorder)
    {
        zfAggRx(dev, buf, addInfo, tid_rx);

        return;
    }
    /*
     * end of add by honda
     */
    //#endif
#endif

    /* Filter Rx frame */
    ret = zfWlanRxFilter(dev, buf);
    if (ret != ZM_SUCCESS)
    {
        zm_msg1_rx(ZM_LV_1, "Rx duplicated:", ret);
        goto zlError;
    }

    /* Discard error frame except mic failure */
    if ((addInfo->Tail.Data.ErrorIndication & 0x3f) != 0)
    {
        if ( wd->XLinkMode && ((addInfo->Tail.Data.ErrorIndication & 0x3f)==0x10) &&
             zfCompareWithBssid(dev, bssid) )
        {
            // Bypass frames !!!
        }
        else
        {
            goto zlError;
        }
    }


    /* OTUS command-8212 dump rx packet */
    if (wd->rxPacketDump)
    {
        zfwDumpBuf(dev, buf);
    }

    /* Call zfwRecv80211() wrapper function to deliver Rx packet */
    /* to driver framework.                                      */

    if (wd->zfcbRecv80211 != NULL)
    {
        wd->zfcbRecv80211(dev, buf, addInfo); //CWYang(m)
    }
    else
    {
        zfiRecv80211(dev, buf, addInfo);
    }
    return;

zlError:
    zm_msg1_rx(ZM_LV_1, "Free packet, error code:", ret);

    wd->commTally.DriverDiscardedFrm++;

    /* Free Rx buffer */
    zfwBufFree(dev, buf, 0);

    return;
}


void zfShowRxEAPOL(zdev_t* dev, zbuf_t* buf, u16_t offset)
{
    u8_t   packetType, keyType, code, identifier, type, flags;
    u16_t  packetLen, keyInfo, keyLen, keyDataLen, length, Op_Code;
    u32_t  replayCounterH, replayCounterL, vendorId, VendorType;

    /* EAPOL packet type */
    packetType = zmw_rx_buf_readb(dev, buf, offset+1); // 0: EAP-Packet
                                                       // 1: EAPOL-Start
                                                       // 2: EAPOL-Logoff
                                                       // 3: EAPOL-Key
                                                       // 4: EAPOL-Encapsulated-ASF-Alert

    /* EAPOL frame format */
    /*  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15   */
    /* -----------------------------------------------   */
    /*            PAE Ethernet Type (0x888e)             */
    /* ----------------------------------------------- 2 */
    /*     Protocol Version    |         Type            */
    /* ----------------------------------------------- 4 */
    /*                       Length                      */
    /* ----------------------------------------------- 6 */
    /*                    Packet Body                    */
    /* ----------------------------------------------- N */

    /* EAPOL body length */
    packetLen = (((u16_t) zmw_rx_buf_readb(dev, buf, offset+2)) << 8) +
                zmw_rx_buf_readb(dev, buf, offset+3);

    if( packetType == 0 )
    { // EAP-Packet

        /* EAP-Packet Code */
        code = zmw_rx_buf_readb(dev, buf, offset+4); // 1 : Request
                                                     // 2 : Response
                                                     // 3 : Success
                                                     // 4 : Failure
        // An EAP packet of the type of Success and Failure has no Data field, and has a length of 4.

        /* EAP Packet format */
        /*  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15   */
        /* -----------------------------------------------   */
        /*           Code          |        Identifier       */
        /* ----------------------------------------------- 2 */
        /*                       Length                      */
        /* ----------------------------------------------- 4 */
        /*                        Data                       */
        /* ----------------------------------------------- N */

        zm_debug_msg0("EAP-Packet");
        zm_debug_msg1("Packet Length = ", packetLen);
        zm_debug_msg1("EAP-Packet Code = ", code);

        if( code == 1 )
        {
            zm_debug_msg0("EAP-Packet Request");

            /* EAP-Packet Identifier */
            identifier = zmw_rx_buf_readb(dev, buf, offset+5);
            /* EAP-Packet Length */
            length = (((u16_t) zmw_rx_buf_readb(dev, buf, offset+6)) << 8) +
                      zmw_rx_buf_readb(dev, buf, offset+7);
            /* EAP-Packet Type */
            type = zmw_rx_buf_readb(dev, buf, offset+8); // 1   : Identity
                                                         // 2   : Notification
                                                         // 3   : Nak (Response Only)
                                                         // 4   : MD5-Challenge
                                                         // 5   : One Time Password (OTP)
                                                         // 6   : Generic Token Card (GTC)
                                                         // 254 : (Expanded Types)Wi-Fi Protected Setup
                                                         // 255 : Experimental Use

            /* The data field in an EAP packet of the type of Request or Response is in the format shown bellowing */
            /*  0  1  2  3  4  5  6  7             N             */
            /* -----------------------------------------------   */
            /*           Type          |        Type Data        */
            /* -----------------------------------------------   */

            zm_debug_msg1("EAP-Packet Identifier = ", identifier);
            zm_debug_msg1("EAP-Packet Length = ", length);
            zm_debug_msg1("EAP-Packet Type = ", type);

            if( type == 1 )
            {
                zm_debug_msg0("EAP-Packet Request Identity");
            }
            else if( type == 2 )
            {
                zm_debug_msg0("EAP-Packet Request Notification");
            }
            else if( type == 4 )
            {
                zm_debug_msg0("EAP-Packet Request MD5-Challenge");
            }
            else if( type == 5 )
            {
                zm_debug_msg0("EAP-Packet Request One Time Password");
            }
            else if( type == 6 )
            {
                zm_debug_msg0("EAP-Packet Request Generic Token Card");
            }
            else if( type == 254 )
            {
                zm_debug_msg0("EAP-Packet Request Wi-Fi Protected Setup");

                /* 0                   1                   2                   3   */
                /* 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 */
                /*+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*/
                /*|     Type      |               Vendor-Id                       |*/
                /*+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*/
                /*|                          Vendor-Type                          |*/
                /*+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*/
                /*|              Vendor data...                                    */
                /*+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                        */

                /* EAP-Packet Vendor ID */
                vendorId = (((u32_t) zmw_rx_buf_readb(dev, buf, offset+9)) << 16) +
                           (((u32_t) zmw_rx_buf_readb(dev, buf, offset+10)) << 8) +
                           zmw_rx_buf_readb(dev, buf, offset+11);
                /* EAP-Packet Vendor Type */
                VendorType = (((u32_t) zmw_rx_buf_readb(dev, buf, offset+12)) << 24) +
                             (((u32_t) zmw_rx_buf_readb(dev, buf, offset+13)) << 16) +
                             (((u32_t) zmw_rx_buf_readb(dev, buf, offset+14)) << 8) +
                             zmw_rx_buf_readb(dev, buf, offset+15);
                /* EAP-Packet Op Code */
                Op_Code = (((u16_t) zmw_rx_buf_readb(dev, buf, offset+16)) << 8) +
                          zmw_rx_buf_readb(dev, buf, offset+17);
                /* EAP-Packet Flags */
                flags = zmw_rx_buf_readb(dev, buf, offset+18);

                zm_debug_msg1("EAP-Packet Vendor ID = ", vendorId);
                zm_debug_msg1("EAP-Packet Venodr Type = ", VendorType);
                zm_debug_msg1("EAP-Packet Op Code = ", Op_Code);
                zm_debug_msg1("EAP-Packet Flags = ", flags);
            }
        }
        else if( code == 2 )
        {
            zm_debug_msg0("EAP-Packet Response");

            /* EAP-Packet Identifier */
            identifier = zmw_rx_buf_readb(dev, buf, offset+5);
            /* EAP-Packet Length */
            length = (((u16_t) zmw_rx_buf_readb(dev, buf, offset+6)) << 8) +
                      zmw_rx_buf_readb(dev, buf, offset+7);
            /* EAP-Packet Type */
            type = zmw_rx_buf_readb(dev, buf, offset+8);

            zm_debug_msg1("EAP-Packet Identifier = ", identifier);
            zm_debug_msg1("EAP-Packet Length = ", length);
            zm_debug_msg1("EAP-Packet Type = ", type);

            if( type == 1 )
            {
                zm_debug_msg0("EAP-Packet Response Identity");
            }
            else if( type == 2 )
            {
                zm_debug_msg0("EAP-Packet Request Notification");
            }
            else if( type == 3 )
            {
                zm_debug_msg0("EAP-Packet Request Nak");
            }
            else if( type == 4 )
            {
                zm_debug_msg0("EAP-Packet Request MD5-Challenge");
            }
            else if( type == 5 )
            {
                zm_debug_msg0("EAP-Packet Request One Time Password");
            }
            else if( type == 6 )
            {
                zm_debug_msg0("EAP-Packet Request Generic Token Card");
            }
            else if( type == 254 )
            {
                zm_debug_msg0("EAP-Packet Response Wi-Fi Protected Setup");

                /* EAP-Packet Vendor ID */
                vendorId = (((u32_t) zmw_rx_buf_readb(dev, buf, offset+9)) << 16) +
                           (((u32_t) zmw_rx_buf_readb(dev, buf, offset+10)) << 8) +
                           zmw_rx_buf_readb(dev, buf, offset+11);
                /* EAP-Packet Vendor Type */
                VendorType = (((u32_t) zmw_rx_buf_readb(dev, buf, offset+12)) << 24) +
                             (((u32_t) zmw_rx_buf_readb(dev, buf, offset+13)) << 16) +
                             (((u32_t) zmw_rx_buf_readb(dev, buf, offset+14)) << 8) +
                             zmw_rx_buf_readb(dev, buf, offset+15);
                /* EAP-Packet Op Code */
                Op_Code = (((u16_t) zmw_rx_buf_readb(dev, buf, offset+16)) << 8) +
                          zmw_rx_buf_readb(dev, buf, offset+17);
                /* EAP-Packet Flags */
                flags = zmw_rx_buf_readb(dev, buf, offset+18);

                zm_debug_msg1("EAP-Packet Vendor ID = ", vendorId);
                zm_debug_msg1("EAP-Packet Venodr Type = ", VendorType);
                zm_debug_msg1("EAP-Packet Op Code = ", Op_Code);
                zm_debug_msg1("EAP-Packet Flags = ", flags);
            }
        }
        else if( code == 3 )
        {
            zm_debug_msg0("EAP-Packet Success");

            /* EAP-Packet Identifier */
            identifier = zmw_rx_buf_readb(dev, buf, offset+5);
            /* EAP-Packet Length */
            length = (((u16_t) zmw_rx_buf_readb(dev, buf, offset+6)) << 8) +
                      zmw_rx_buf_readb(dev, buf, offset+7);

            zm_debug_msg1("EAP-Packet Identifier = ", identifier);
            zm_debug_msg1("EAP-Packet Length = ", length);
        }
        else if( code == 4 )
        {
            zm_debug_msg0("EAP-Packet Failure");

            /* EAP-Packet Identifier */
            identifier = zmw_rx_buf_readb(dev, buf, offset+5);
            /* EAP-Packet Length */
            length = (((u16_t) zmw_rx_buf_readb(dev, buf, offset+6)) << 8) +
                      zmw_rx_buf_readb(dev, buf, offset+7);

            zm_debug_msg1("EAP-Packet Identifier = ", identifier);
            zm_debug_msg1("EAP-Packet Length = ", length);
        }
    }
    else if( packetType == 1 )
    { // EAPOL-Start
        zm_debug_msg0("EAPOL-Start");
    }
    else if( packetType == 2 )
    { // EAPOL-Logoff
        zm_debug_msg0("EAPOL-Logoff");
    }
    else if( packetType == 3 )
    { // EAPOL-Key
        /* EAPOL-Key type */
        keyType = zmw_rx_buf_readb(dev, buf, offset+4);
        /* EAPOL-Key information */
        keyInfo = (((u16_t) zmw_rx_buf_readb(dev, buf, offset+5)) << 8) +
                  zmw_rx_buf_readb(dev, buf, offset+6);
        /* EAPOL-Key length */
        keyLen = (((u16_t) zmw_rx_buf_readb(dev, buf, offset+7)) << 8) +
                 zmw_rx_buf_readb(dev, buf, offset+8);
        /* EAPOL-Key replay counter (high double word) */
        replayCounterH = (((u32_t) zmw_rx_buf_readb(dev, buf, offset+9)) << 24) +
                         (((u32_t) zmw_rx_buf_readb(dev, buf, offset+10)) << 16) +
                         (((u32_t) zmw_rx_buf_readb(dev, buf, offset+11)) << 8) +
                         zmw_rx_buf_readb(dev, buf, offset+12);
        /* EAPOL-Key replay counter (low double word) */
        replayCounterL = (((u32_t) zmw_rx_buf_readb(dev, buf, offset+13)) << 24) +
                         (((u32_t) zmw_rx_buf_readb(dev, buf, offset+14)) << 16) +
                         (((u32_t) zmw_rx_buf_readb(dev, buf, offset+15)) << 8) +
                         zmw_rx_buf_readb(dev, buf, offset+16);
        /* EAPOL-Key data length */
        keyDataLen = (((u16_t) zmw_rx_buf_readb(dev, buf, offset+97)) << 8) +
                     zmw_rx_buf_readb(dev, buf, offset+98);

        zm_debug_msg0("EAPOL-Key");
        zm_debug_msg1("packet length = ", packetLen);

        if ( keyType == 254 )
        {
            zm_debug_msg0("key type = 254 (SSN key descriptor)");
        }
        else
        {
            zm_debug_msg2("key type = 0x", keyType);
        }

        zm_debug_msg2("replay counter(L) = ", replayCounterL);

        zm_debug_msg2("key information = ", keyInfo);

        if ( keyInfo & ZM_BIT_3 )
        {
            zm_debug_msg0("    - pairwise key");
        }
        else
        {
            zm_debug_msg0("    - group key");
        }

        if ( keyInfo & ZM_BIT_6 )
        {
            zm_debug_msg0("    - Tx key installed");
        }
        else
        {
            zm_debug_msg0("    - Tx key not set");
        }

        if ( keyInfo & ZM_BIT_7 )
        {
            zm_debug_msg0("    - Ack needed");
        }
        else
        {
            zm_debug_msg0("    - Ack not needed");
        }

        if ( keyInfo & ZM_BIT_8 )
        {
            zm_debug_msg0("    - MIC set");
        }
        else
        {
            zm_debug_msg0("    - MIC not set");
        }

        if ( keyInfo & ZM_BIT_9 )
        {
            zm_debug_msg0("    - packet encrypted");
        }
        else
        {
            zm_debug_msg0("    - packet not encrypted");
        }

        zm_debug_msg1("keyLen = ", keyLen);
        zm_debug_msg1("keyDataLen = ", keyDataLen);
    }
    else if( packetType == 4 )
    {
        zm_debug_msg0("EAPOL-Encapsulated-ASF-Alert");
    }
}

void zfShowTxEAPOL(zdev_t* dev, zbuf_t* buf, u16_t offset)
{
    u8_t   packetType, keyType, code, identifier, type, flags;
    u16_t  packetLen, keyInfo, keyLen, keyDataLen, length, Op_Code;
    u32_t  replayCounterH, replayCounterL, vendorId, VendorType;

    zm_debug_msg1("EAPOL Packet size = ", zfwBufGetSize(dev, buf));

    /* EAPOL packet type */
    // 0: EAP-Packet
    // 1: EAPOL-Start
    // 2: EAPOL-Logoff
    // 3: EAPOL-Key
    // 4: EAPOL-Encapsulated-ASF-Alert

    /* EAPOL frame format */
    /*  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15   */
    /* -----------------------------------------------   */
    /*            PAE Ethernet Type (0x888e)             */
    /* ----------------------------------------------- 2 */
    /*     Protocol Version    |         Type            */
    /* ----------------------------------------------- 4 */
    /*                       Length                      */
    /* ----------------------------------------------- 6 */
    /*                    Packet Body                    */
    /* ----------------------------------------------- N */

    packetType = zmw_tx_buf_readb(dev, buf, offset+1);
    /* EAPOL body length */
    packetLen = (((u16_t) zmw_tx_buf_readb(dev, buf, offset+2)) << 8) +
                zmw_tx_buf_readb(dev, buf, offset+3);

    if( packetType == 0 )
    { // EAP-Packet
        /* EAP-Packet Code */
        code = zmw_tx_buf_readb(dev, buf, offset+4); // 1 : Request
                                                     // 2 : Response
                                                     // 3 : Success
                                                     // 4 : Failure

        // An EAP packet of the type of Success and Failure has no Data field, and has a length of 4.

        /* EAP Packet format */
        /*  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15   */
        /* -----------------------------------------------   */
        /*           Code          |        Identifier       */
        /* ----------------------------------------------- 2 */
        /*                       Length                      */
        /* ----------------------------------------------- 4 */
        /*                        Data                       */
        /* ----------------------------------------------- N */

        zm_debug_msg0("EAP-Packet");
        zm_debug_msg1("Packet Length = ", packetLen);
        zm_debug_msg1("EAP-Packet Code = ", code);

        if( code == 1 )
        {
            zm_debug_msg0("EAP-Packet Request");

            /* EAP-Packet Identifier */
            identifier = zmw_tx_buf_readb(dev, buf, offset+5);
            /* EAP-Packet Length */
            length = (((u16_t) zmw_tx_buf_readb(dev, buf, offset+6)) << 8) +
                      zmw_tx_buf_readb(dev, buf, offset+7);
            /* EAP-Packet Type */
            type = zmw_tx_buf_readb(dev, buf, offset+8); // 1   : Identity
                                                         // 2   : Notification
                                                         // 3   : Nak (Response Only)
                                                         // 4   : MD5-Challenge
                                                         // 5   : One Time Password (OTP)
                                                         // 6   : Generic Token Card (GTC)
                                                         // 254 : (Expanded Types)Wi-Fi Protected Setup
                                                         // 255 : Experimental Use

            /* The data field in an EAP packet of the type of Request or Response is in the format shown bellowing */
            /*  0  1  2  3  4  5  6  7             N             */
            /* -----------------------------------------------   */
            /*           Type          |        Type Data        */
            /* -----------------------------------------------   */

            zm_debug_msg1("EAP-Packet Identifier = ", identifier);
            zm_debug_msg1("EAP-Packet Length = ", length);
            zm_debug_msg1("EAP-Packet Type = ", type);

            if( type == 1 )
            {
                zm_debug_msg0("EAP-Packet Request Identity");
            }
            else if( type == 2 )
            {
                zm_debug_msg0("EAP-Packet Request Notification");
            }
            else if( type == 4 )
            {
                zm_debug_msg0("EAP-Packet Request MD5-Challenge");
            }
            else if( type == 5 )
            {
                zm_debug_msg0("EAP-Packet Request One Time Password");
            }
            else if( type == 6 )
            {
                zm_debug_msg0("EAP-Packet Request Generic Token Card");
            }
            else if( type == 254 )
            {
                zm_debug_msg0("EAP-Packet Request Wi-Fi Protected Setup");

                /* 0                   1                   2                   3   */
                /* 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 */
                /*+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*/
                /*|     Type      |               Vendor-Id                       |*/
                /*+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*/
                /*|                          Vendor-Type                          |*/
                /*+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+*/
                /*|              Vendor data...                                    */
                /*+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                        */

                /* EAP-Packet Vendor ID */
                vendorId = (((u32_t) zmw_tx_buf_readb(dev, buf, offset+9)) << 16) +
                           (((u32_t) zmw_tx_buf_readb(dev, buf, offset+10)) << 8) +
                           zmw_tx_buf_readb(dev, buf, offset+11);
                /* EAP-Packet Vendor Type */
                VendorType = (((u32_t) zmw_tx_buf_readb(dev, buf, offset+12)) << 24) +
                             (((u32_t) zmw_tx_buf_readb(dev, buf, offset+13)) << 16) +
                             (((u32_t) zmw_tx_buf_readb(dev, buf, offset+14)) << 8) +
                             zmw_tx_buf_readb(dev, buf, offset+15);
                /* EAP-Packet Op Code */
                Op_Code = (((u16_t) zmw_tx_buf_readb(dev, buf, offset+16)) << 8) +
                          zmw_tx_buf_readb(dev, buf, offset+17);
                /* EAP-Packet Flags */
                flags = zmw_tx_buf_readb(dev, buf, offset+18);

                zm_debug_msg1("EAP-Packet Vendor ID = ", vendorId);
                zm_debug_msg1("EAP-Packet Venodr Type = ", VendorType);
                zm_debug_msg1("EAP-Packet Op Code = ", Op_Code);
                zm_debug_msg1("EAP-Packet Flags = ", flags);
            }
        }
        else if( code == 2 )
        {
            zm_debug_msg0("EAP-Packet Response");

            /* EAP-Packet Identifier */
            identifier = zmw_tx_buf_readb(dev, buf, offset+5);
            /* EAP-Packet Length */
            length = (((u16_t) zmw_tx_buf_readb(dev, buf, offset+6)) << 8) +
                      zmw_tx_buf_readb(dev, buf, offset+7);
            /* EAP-Packet Type */
            type = zmw_tx_buf_readb(dev, buf, offset+8);

            zm_debug_msg1("EAP-Packet Identifier = ", identifier);
            zm_debug_msg1("EAP-Packet Length = ", length);
            zm_debug_msg1("EAP-Packet Type = ", type);

            if( type == 1 )
            {
                zm_debug_msg0("EAP-Packet Response Identity");
            }
            else if( type == 2 )
            {
                zm_debug_msg0("EAP-Packet Request Notification");
            }
            else if( type == 3 )
            {
                zm_debug_msg0("EAP-Packet Request Nak");
            }
            else if( type == 4 )
            {
                zm_debug_msg0("EAP-Packet Request MD5-Challenge");
            }
            else if( type == 5 )
            {
                zm_debug_msg0("EAP-Packet Request One Time Password");
            }
            else if( type == 6 )
            {
                zm_debug_msg0("EAP-Packet Request Generic Token Card");
            }
            else if( type == 254 )
            {
                zm_debug_msg0("EAP-Packet Response Wi-Fi Protected Setup");

                /* EAP-Packet Vendor ID */
                vendorId = (((u32_t) zmw_tx_buf_readb(dev, buf, offset+9)) << 16) +
                           (((u32_t) zmw_tx_buf_readb(dev, buf, offset+10)) << 8) +
                           zmw_tx_buf_readb(dev, buf, offset+11);
                /* EAP-Packet Vendor Type */
                VendorType = (((u32_t) zmw_tx_buf_readb(dev, buf, offset+12)) << 24) +
                             (((u32_t) zmw_tx_buf_readb(dev, buf, offset+13)) << 16) +
                             (((u32_t) zmw_tx_buf_readb(dev, buf, offset+14)) << 8) +
                             zmw_tx_buf_readb(dev, buf, offset+15);
                /* EAP-Packet Op Code */
                Op_Code = (((u16_t) zmw_tx_buf_readb(dev, buf, offset+16)) << 8) +
                          zmw_tx_buf_readb(dev, buf, offset+17);
                /* EAP-Packet Flags */
                flags = zmw_tx_buf_readb(dev, buf, offset+18);

                zm_debug_msg1("EAP-Packet Vendor ID = ", vendorId);
                zm_debug_msg1("EAP-Packet Venodr Type = ", VendorType);
                zm_debug_msg1("EAP-Packet Op Code = ", Op_Code);
                zm_debug_msg1("EAP-Packet Flags = ", flags);
            }
        }
        else if( code == 3 )
        {
            zm_debug_msg0("EAP-Packet Success");

            /* EAP-Packet Identifier */
            identifier = zmw_rx_buf_readb(dev, buf, offset+5);
            /* EAP-Packet Length */
            length = (((u16_t) zmw_rx_buf_readb(dev, buf, offset+6)) << 8) +
                      zmw_rx_buf_readb(dev, buf, offset+7);

            zm_debug_msg1("EAP-Packet Identifier = ", identifier);
            zm_debug_msg1("EAP-Packet Length = ", length);
        }
        else if( code == 4 )
        {
            zm_debug_msg0("EAP-Packet Failure");

            /* EAP-Packet Identifier */
            identifier = zmw_tx_buf_readb(dev, buf, offset+5);
            /* EAP-Packet Length */
            length = (((u16_t) zmw_tx_buf_readb(dev, buf, offset+6)) << 8) +
                      zmw_tx_buf_readb(dev, buf, offset+7);

            zm_debug_msg1("EAP-Packet Identifier = ", identifier);
            zm_debug_msg1("EAP-Packet Length = ", length);
        }
    }
    else if( packetType == 1 )
    { // EAPOL-Start
        zm_debug_msg0("EAPOL-Start");
    }
    else if( packetType == 2 )
    { // EAPOL-Logoff
        zm_debug_msg0("EAPOL-Logoff");
    }
    else if( packetType == 3 )
    { // EAPOL-Key
        /* EAPOL-Key type */
        keyType = zmw_tx_buf_readb(dev, buf, offset+4);
        /* EAPOL-Key information */
        keyInfo = (((u16_t) zmw_tx_buf_readb(dev, buf, offset+5)) << 8) +
                  zmw_tx_buf_readb(dev, buf, offset+6);
        /* EAPOL-Key length */
        keyLen = (((u16_t) zmw_tx_buf_readb(dev, buf, offset+7)) << 8) +
                 zmw_tx_buf_readb(dev, buf, offset+8);
        /* EAPOL-Key replay counter (high double word) */
        replayCounterH = (((u32_t) zmw_tx_buf_readb(dev, buf, offset+9)) << 24) +
                         (((u32_t) zmw_tx_buf_readb(dev, buf, offset+10)) << 16) +
                         (((u32_t) zmw_tx_buf_readb(dev, buf, offset+11)) << 8) +
                         zmw_tx_buf_readb(dev, buf, offset+12);
        /* EAPOL-Key replay counter (low double word) */
        replayCounterL = (((u32_t) zmw_tx_buf_readb(dev, buf, offset+13)) << 24) +
                         (((u32_t) zmw_tx_buf_readb(dev, buf, offset+14)) << 16) +
                         (((u32_t) zmw_tx_buf_readb(dev, buf, offset+15)) << 8) +
                         zmw_tx_buf_readb(dev, buf, offset+16);
        /* EAPOL-Key data length */
        keyDataLen = (((u16_t) zmw_tx_buf_readb(dev, buf, offset+97)) << 8) +
                     zmw_tx_buf_readb(dev, buf, offset+98);

        zm_debug_msg0("EAPOL-Key");
        zm_debug_msg1("packet length = ", packetLen);

        if ( keyType == 254 )
        {
            zm_debug_msg0("key type = 254 (SSN key descriptor)");
        }
        else
        {
            zm_debug_msg2("key type = 0x", keyType);
        }

        zm_debug_msg2("replay counter(L) = ", replayCounterL);

        zm_debug_msg2("key information = ", keyInfo);

        if ( keyInfo & ZM_BIT_3 )
        {
            zm_debug_msg0("    - pairwise key");
        }
        else
        {
            zm_debug_msg0("    - group key");
        }

        if ( keyInfo & ZM_BIT_6 )
        {
            zm_debug_msg0("    - Tx key installed");
        }
        else
        {
            zm_debug_msg0("    - Tx key not set");
        }

        if ( keyInfo & ZM_BIT_7 )
        {
            zm_debug_msg0("    - Ack needed");
        }
        else
        {
            zm_debug_msg0("    - Ack not needed");
        }

        if ( keyInfo & ZM_BIT_8 )
        {
            zm_debug_msg0("    - MIC set");
        }
        else
        {
            zm_debug_msg0("    - MIC not set");
        }

        if ( keyInfo & ZM_BIT_9 )
        {
            zm_debug_msg0("    - packet encrypted");
        }
        else
        {
            zm_debug_msg0("    - packet not encrypted");
        }

        zm_debug_msg1("keyLen = ", keyLen);
        zm_debug_msg1("keyDataLen = ", keyDataLen);
    }
    else if( packetType == 4 )
    {
        zm_debug_msg0("EAPOL-Encapsulated-ASF-Alert");
    }
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfiRecv80211                */
/*      Called to receive 802.11 frame.                                 */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : received 802.11 frame buffer.                             */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen             ZyDAS Technology Corporation    2005.5      */
/*                                                                      */
/************************************************************************/
void zfiRecv80211(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* addInfo)
{
    u8_t snapCase=0, encryMode;
    u16_t frameType, typeLengthField;
    u16_t frameCtrl;
    u16_t frameSubtype;
    u16_t ret;
    u16_t len;
    u8_t bIsDefrag = 0;
    u16_t offset, tailLen;
    u8_t vap = 0;
    u16_t da[3], sa[3];
    u16_t ii;
    u8_t uapsdTrig = 0;
    zbuf_t* psBuf;
#ifdef ZM_ENABLE_NATIVE_WIFI
    u8_t i;
#endif

    zmw_get_wlan_dev(dev);

    ZM_BUFFER_TRACE(dev, buf)

    //zm_msg2_rx(ZM_LV_2, "zfiRecv80211(), buf=", buf);

    //zm_msg2_rx(ZM_LV_0, "h[0]=", zmw_rx_buf_readh(dev, buf, 0));
    //zm_msg2_rx(ZM_LV_0, "h[2]=", zmw_rx_buf_readh(dev, buf, 2));
    //zm_msg2_rx(ZM_LV_0, "h[4]=", zmw_rx_buf_readh(dev, buf, 4));

    frameCtrl = zmw_rx_buf_readb(dev, buf, 0);
    frameType = frameCtrl & 0xf;
    frameSubtype = frameCtrl & 0xf0;

#if 0   // Move to ProcessBeacon to judge if there's a new peer station
    if ( (wd->wlanMode == ZM_MODE_IBSS)&&
         (wd->sta.ibssPartnerStatus != ZM_IBSS_PARTNER_ALIVE) )
    {
        zfStaIbssMonitoring(dev, buf);
    }
#endif

    /* If data frame */
    if (frameType == ZM_WLAN_DATA_FRAME)
    {
        wd->sta.TotalNumberOfReceivePackets++;
        wd->sta.TotalNumberOfReceiveBytes += zfwBufGetSize(dev, buf);
        //zm_debug_msg1("Receive packets     = ", wd->sta.TotalNumberOfReceivePackets);

        //zm_msg0_rx(ZM_LV_0, "Rx data");
        if (wd->wlanMode == ZM_MODE_AP)
        {
            ret = zfApUpdatePsBit(dev, buf, &vap, &uapsdTrig);
            if (ret != ZM_SUCCESS)
            {
                zfwBufFree(dev, buf, 0);
                return;
            }

            if (((uapsdTrig&0xf) != 0) && ((frameSubtype & 0x80) != 0))
            {
                u8_t ac = zcUpToAc[zmw_buf_readb(dev, buf, 24)&0x7];
                u8_t pktNum;
                u8_t mb;
                u16_t flag;
                u8_t src[6];

                //printk("QoS ctrl=%d\n", zmw_buf_readb(dev, buf, 24));
                //printk("UAPSD trigger, ac=%d\n", ac);

                if (((0x8>>ac) & uapsdTrig) != 0)
                {
                    pktNum = zcMaxspToPktNum[(uapsdTrig>>4) & 0x3];

                    for (ii=0; ii<6; ii++)
                    {
                        src[ii] = zmw_buf_readb(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+ii);
                    }

                    for (ii=0; ii<pktNum; ii++)
                    {
                        //if ((psBuf = zfQueueGet(dev, wd->ap.uapsdQ)) != NULL)
                        psBuf = zfQueueGetWithMac(dev, wd->ap.uapsdQ, src, &mb);
                        if (psBuf != NULL)
                        {
                            if ((ii+1) == pktNum)
                            {
                                //EOSP anyway
                                flag = 0x100 | (mb<<5);
                            }
                            else
                            {
                                if (mb != 0)
                                {
                                    //more data, not EOSP
                                    flag = 0x20;
                                }
                                else
                                {
                                    //no more data, EOSP
                                    flag = 0x100;
                                }
                            }
                            zfTxSendEth(dev, psBuf, 0, ZM_EXTERNAL_ALLOC_BUF, flag);
                        }

                        if ((psBuf == NULL) || (mb == 0))
                        {
                            if ((ii == 0) && (psBuf == NULL))
                            {
                                zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_QOS_NULL, (u16_t*)src, 0, 0, 0);
                            }
                            break;
                        }
                    }
                }
            }

        }
        else if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
        {
            u16_t frameCtrlMSB;
    		u8_t   bssid[6];

            /* Check Is RIFS frame and decide to enable RIFS or not */
            if( wd->sta.EnableHT )
                zfCheckIsRIFSFrame(dev, buf, frameSubtype);

            if ( zfPowerSavingMgrIsSleeping(dev) || wd->sta.psMgr.tempWakeUp == 1)
            {
                frameCtrlMSB = zmw_rx_buf_readb(dev, buf, 1);

                /* check more data */
                if ( frameCtrlMSB & ZM_BIT_5 )
                {
                    //if rx frame's AC is not delivery-enabled
                    if ((wd->sta.qosInfo&0xf) != 0xf)
                    {
                        u8_t rxAc = 0;
                        if ((frameSubtype & 0x80) != 0)
                        {
                            rxAc = zcUpToAc[zmw_buf_readb(dev, buf, 24)&0x7];
                        }

                        if (((0x8>>rxAc) & wd->sta.qosInfo) == 0)
                        {
                            zfSendPSPoll(dev);
                            wd->sta.psMgr.tempWakeUp = 0;
                        }
                    }
                }
            }
			/*increase beacon count when receive vaild data frame from AP*/
        	ZM_MAC_WORD_TO_BYTE(wd->sta.bssid, bssid);

			if (zfStaIsConnected(dev)&&
				zfRxBufferEqualToStr(dev, buf, bssid, ZM_WLAN_HEADER_A2_OFFSET, 6))
			{
                wd->sta.rxBeaconCount++;
			}
        }

        zm_msg1_rx(ZM_LV_2, "Rx VAP=", vap);

        /* handle IV, EXT-IV, ICV, and EXT-ICV */
        zfGetRxIvIcvLength(dev, buf, vap, &offset, &tailLen, addInfo);

        zfStaIbssPSCheckState(dev, buf);
        //QoS data frame
        if ((frameSubtype & 0x80) == 0x80)
        {
            offset += 2;
        }

        len = zfwBufGetSize(dev, buf);
        /* remove ICV */
        if (tailLen > 0)
        {
            if (len > tailLen)
            {
                len -= tailLen;
                zfwBufSetSize(dev, buf, len);
            }
        }

        /* Filter NULL data */
        if (((frameSubtype&0x40) != 0) || ((len = zfwBufGetSize(dev, buf))<=24))
        {
            zm_msg1_rx(ZM_LV_1, "Free Rx NULL data, len=", len);
            zfwBufFree(dev, buf, 0);
            return;
        }

        /* check and handle defragmentation */
        if ( wd->sta.bSafeMode && (wd->sta.wepStatus == ZM_ENCRYPTION_AES) && wd->sta.SWEncryptEnable )
        {
            zm_msg0_rx(ZM_LV_1, "Bypass defragmentation packets in safe mode");
        }
        else
        {
            buf = zfDefragment(dev, buf, &bIsDefrag, addInfo);
            if (buf == NULL)
            {
                /* In this case, the buffer has been freed in zfDefragment */
                return;
            }
        }

        ret = ZM_MIC_SUCCESS;

        /* If SW WEP/TKIP are not turned on */
        if ((wd->sta.SWEncryptEnable & ZM_SW_TKIP_DECRY_EN) == 0 &&
            (wd->sta.SWEncryptEnable & ZM_SW_WEP_DECRY_EN) == 0)
        {
            encryMode = zfGetEncryModeFromRxStatus(addInfo);

            /* check if TKIP */
            if ( encryMode == ZM_TKIP )
            {
                if ( bIsDefrag )
                {
                    ret = zfMicRxVerify(dev, buf);
                }
                else
                {
                    /* check MIC failure bit */
                    if ( ZM_RX_STATUS_IS_MIC_FAIL(addInfo) )
                    {
                        ret = ZM_MIC_FAILURE;
                    }
                }

                if ( ret == ZM_MIC_FAILURE )
                {
                    u8_t Unicast_Pkt = 0x0;

                    if ((zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET) & 0x1) == 0)
                    {
                        wd->commTally.swRxUnicastMicFailCount++;
                        Unicast_Pkt = 0x1;
                    }/*
                    else if (zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET) == 0xffff)
                    {
                        wd->commTally.swRxMulticastMicFailCount++;
                    }*/
                    else
                    {
                        wd->commTally.swRxMulticastMicFailCount++;
                    }
                    if ( wd->wlanMode == ZM_MODE_AP )
                    {
                        u16_t idx;
                        u8_t addr[6];

                        for (idx=0; idx<6; idx++)
                        {
                            addr[idx] = zmw_rx_buf_readb(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+idx);
                        }

                        if (wd->zfcbApMicFailureNotify != NULL)
                        {
                            wd->zfcbApMicFailureNotify(dev, addr, buf);
                        }
                    }
                    else
                    {
                        if(Unicast_Pkt)
                        {
                            zm_debug_msg0("Countermeasure : Unicast_Pkt ");
                        }
                        else
                        {
                            zm_debug_msg0("Countermeasure : Non-Unicast_Pkt ");
                        }

                        if((wd->TKIP_Group_KeyChanging == 0x0) || (Unicast_Pkt == 0x1))
                        {
                            zm_debug_msg0("Countermeasure : Do MIC Check ");
                            zfStaMicFailureHandling(dev, buf);
                        }
                        else
                        {
                            zm_debug_msg0("Countermeasure : SKIP MIC Check due to Group Keychanging ");
                        }
                    }
                    /* Discard MIC failed frame */
                    zfwBufFree(dev, buf, 0);
                    return;
                }
            }
        }
        else
        {
            u8_t IsEncryFrame;

            /* TODO: Check whether WEP bit is turned on in MAC header */
            encryMode = ZM_NO_WEP;

            IsEncryFrame = (zmw_rx_buf_readb(dev, buf, 1) & 0x40);

            if (IsEncryFrame)
            {
                /* Software decryption for TKIP */
                if (wd->sta.SWEncryptEnable & ZM_SW_TKIP_DECRY_EN)
                {
                    u16_t iv16;
                    u16_t iv32;
                    u8_t RC4Key[16];
                    u16_t IvOffset;
                    struct zsTkipSeed *rxSeed;

                    IvOffset = offset + ZM_SIZE_OF_WLAN_DATA_HEADER;

                    rxSeed = zfStaGetRxSeed(dev, buf);

                    if (rxSeed == NULL)
                    {
                        zm_debug_msg0("rxSeed is NULL");

                        /* Discard this frame */
                        zfwBufFree(dev, buf, 0);
                        return;
                    }

                    iv16 = (zmw_rx_buf_readb(dev, buf, IvOffset) << 8) + zmw_rx_buf_readb(dev, buf, IvOffset+2);
                    iv32 = zmw_rx_buf_readb(dev, buf, IvOffset+4) +
                           (zmw_rx_buf_readb(dev, buf, IvOffset+5) << 8) +
                           (zmw_rx_buf_readb(dev, buf, IvOffset+6) << 16) +
                           (zmw_rx_buf_readb(dev, buf, IvOffset+7) << 24);

                    /* TKIP Key Mixing */
                    zfTkipPhase1KeyMix(iv32, rxSeed);
                    zfTkipPhase2KeyMix(iv16, rxSeed);
                    zfTkipGetseeds(iv16, RC4Key, rxSeed);

                    /* Decrypt Data */
                    ret = zfTKIPDecrypt(dev, buf, IvOffset+ZM_SIZE_OF_IV+ZM_SIZE_OF_EXT_IV, 16, RC4Key);

                    if (ret == ZM_ICV_FAILURE)
                    {
                        zm_debug_msg0("TKIP ICV fail");

                        /* Discard ICV failed frame */
                        zfwBufFree(dev, buf, 0);
                        return;
                    }

                    /* Remove ICV from buffer */
                    zfwBufSetSize(dev, buf, len-4);

                    /* Check MIC */
                    ret = zfMicRxVerify(dev, buf);

                    if (ret == ZM_MIC_FAILURE)
                    {
                        if ((zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET) & 0x1) == 0)
                        {
                            wd->commTally.swRxUnicastMicFailCount++;
                        }
                        else if (zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET) == 0xffff)
                        {
                            wd->commTally.swRxMulticastMicFailCount++;
                        }
                        else
                        {
                            wd->commTally.swRxMulticastMicFailCount++;
                        }
                        if ( wd->wlanMode == ZM_MODE_AP )
                        {
                            u16_t idx;
                            u8_t addr[6];

                            for (idx=0; idx<6; idx++)
                            {
                                addr[idx] = zmw_rx_buf_readb(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+idx);
                            }

                            if (wd->zfcbApMicFailureNotify != NULL)
                            {
                                wd->zfcbApMicFailureNotify(dev, addr, buf);
                            }
                        }
                        else
                        {
                            zfStaMicFailureHandling(dev, buf);
                        }

                        zm_debug_msg0("MIC fail");
                        /* Discard MIC failed frame */
                        zfwBufFree(dev, buf, 0);
                        return;
                    }

                    encryMode = ZM_TKIP;
                    offset += ZM_SIZE_OF_IV + ZM_SIZE_OF_EXT_IV;
                }
                else if(wd->sta.SWEncryptEnable & ZM_SW_WEP_DECRY_EN)
                {
                    u16_t IvOffset;
                    u8_t keyLen = 5;
                    u8_t iv[3];
                    u8_t keyIdx;

                    IvOffset = offset + ZM_SIZE_OF_WLAN_DATA_HEADER;

                    /* Retrieve IV */
                    iv[0] = zmw_rx_buf_readb(dev, buf, IvOffset);
                    iv[1] = zmw_rx_buf_readb(dev, buf, IvOffset+1);
                    iv[2] = zmw_rx_buf_readb(dev, buf, IvOffset+2);

                    keyIdx = ((zmw_rx_buf_readb(dev, buf, IvOffset+3) >> 6) & 0x03);

                    IvOffset += ZM_SIZE_OF_IV;

                    if (wd->sta.SWEncryMode[keyIdx] == ZM_WEP64)
                    {
                        keyLen = 5;
                    }
                    else if (wd->sta.SWEncryMode[keyIdx] == ZM_WEP128)
                    {
                        keyLen = 13;
                    }
                    else if (wd->sta.SWEncryMode[keyIdx] == ZM_WEP256)
                    {
                        keyLen = 29;
                    }

                    zfWEPDecrypt(dev, buf, IvOffset, keyLen, wd->sta.wepKey[keyIdx], iv);

                    if (ret == ZM_ICV_FAILURE)
                    {
                        zm_debug_msg0("WEP ICV fail");

                        /* Discard ICV failed frame */
                        zfwBufFree(dev, buf, 0);
                        return;
                    }

                    encryMode = wd->sta.SWEncryMode[keyIdx];

                    /* Remove ICV from buffer */
                    zfwBufSetSize(dev, buf, len-4);

                    offset += ZM_SIZE_OF_IV;
                }
            }
        }

#ifdef ZM_ENABLE_CENC
        //else if ( encryMode == ZM_CENC ) /* check if CENC */
        if ( encryMode == ZM_CENC )
        {
            u32_t rxIV[4];

            rxIV[0] = (zmw_rx_buf_readh(dev, buf, 28) << 16)
                     + zmw_rx_buf_readh(dev, buf, 26);
            rxIV[1] = (zmw_rx_buf_readh(dev, buf, 32) << 16)
                     + zmw_rx_buf_readh(dev, buf, 30);
            rxIV[2] = (zmw_rx_buf_readh(dev, buf, 36) << 16)
                     + zmw_rx_buf_readh(dev, buf, 34);
            rxIV[3] = (zmw_rx_buf_readh(dev, buf, 40) << 16)
                     + zmw_rx_buf_readh(dev, buf, 38);

            //zm_debug_msg2("rxIV[0] = 0x", rxIV[0]);
            //zm_debug_msg2("rxIV[1] = 0x", rxIV[1]);
            //zm_debug_msg2("rxIV[2] = 0x", rxIV[2]);
            //zm_debug_msg2("rxIV[3] = 0x", rxIV[3]);

            /* destination address*/
            da[0] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET);
            da[1] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET+2);
            da[2] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET+4);

            if ( wd->wlanMode == ZM_MODE_AP )
            {
            }
            else
            {
                if ((da[0] & 0x1))
                { //multicast frame
                    /* Accumlate the PN sequence */
                    wd->sta.rxivGK[0] ++;

                    if (wd->sta.rxivGK[0] == 0)
                    {
                        wd->sta.rxivGK[1]++;
                    }

                    if (wd->sta.rxivGK[1] == 0)
                    {
                        wd->sta.rxivGK[2]++;
                    }

                    if (wd->sta.rxivGK[2] == 0)
                    {
                        wd->sta.rxivGK[3]++;
                    }

                    if (wd->sta.rxivGK[3] == 0)
                    {
                        wd->sta.rxivGK[0] = 0;
                        wd->sta.rxivGK[1] = 0;
                        wd->sta.rxivGK[2] = 0;
                    }

                    //zm_debug_msg2("wd->sta.rxivGK[0] = 0x", wd->sta.rxivGK[0]);
                    //zm_debug_msg2("wd->sta.rxivGK[1] = 0x", wd->sta.rxivGK[1]);
                    //zm_debug_msg2("wd->sta.rxivGK[2] = 0x", wd->sta.rxivGK[2]);
                    //zm_debug_msg2("wd->sta.rxivGK[3] = 0x", wd->sta.rxivGK[3]);

                    if ( !((wd->sta.rxivGK[0] == rxIV[0])
                        && (wd->sta.rxivGK[1] == rxIV[1])
                        && (wd->sta.rxivGK[2] == rxIV[2])
                        && (wd->sta.rxivGK[3] == rxIV[3])))
                    {
                        u8_t PacketDiscard = 0;
                        /* Discard PN Code Error frame */
                        if (rxIV[0] < wd->sta.rxivGK[0])
                        {
                            PacketDiscard = 1;
                        }
                        if (wd->sta.rxivGK[0] > 0xfffffff0)
                        { //boundary case
                            if ((rxIV[0] < 0xfffffff0)
                                && (((0xffffffff - wd->sta.rxivGK[0]) + rxIV[0]) > 16))
                            {
                                PacketDiscard = 1;
                            }
                        }
                        else
                        { //normal case
                            if ((rxIV[0] - wd->sta.rxivGK[0]) > 16)
                            {
                                PacketDiscard = 1;
                            }
                        }
                        // sync sta pn code with ap because of losting some packets
                        wd->sta.rxivGK[0] = rxIV[0];
                        wd->sta.rxivGK[1] = rxIV[1];
                        wd->sta.rxivGK[2] = rxIV[2];
                        wd->sta.rxivGK[3] = rxIV[3];
                        if (PacketDiscard)
                        {
                            zm_debug_msg0("Discard PN Code lost too much multicast frame");
                            zfwBufFree(dev, buf, 0);
                            return;
                        }
                    }
                }
                else
                { //unicast frame
                    /* Accumlate the PN sequence */
                    wd->sta.rxiv[0] += 2;

                    if (wd->sta.rxiv[0] == 0 || wd->sta.rxiv[0] == 1)
                    {
                        wd->sta.rxiv[1]++;
                    }

                    if (wd->sta.rxiv[1] == 0)
                    {
                        wd->sta.rxiv[2]++;
                    }

                    if (wd->sta.rxiv[2] == 0)
                    {
                        wd->sta.rxiv[3]++;
                    }

                    if (wd->sta.rxiv[3] == 0)
                    {
                        wd->sta.rxiv[0] = 0;
                        wd->sta.rxiv[1] = 0;
                        wd->sta.rxiv[2] = 0;
                    }

                    //zm_debug_msg2("wd->sta.rxiv[0] = 0x", wd->sta.rxiv[0]);
                    //zm_debug_msg2("wd->sta.rxiv[1] = 0x", wd->sta.rxiv[1]);
                    //zm_debug_msg2("wd->sta.rxiv[2] = 0x", wd->sta.rxiv[2]);
                    //zm_debug_msg2("wd->sta.rxiv[3] = 0x", wd->sta.rxiv[3]);

                    if ( !((wd->sta.rxiv[0] == rxIV[0])
                        && (wd->sta.rxiv[1] == rxIV[1])
                        && (wd->sta.rxiv[2] == rxIV[2])
                        && (wd->sta.rxiv[3] == rxIV[3])))
                    {
                        zm_debug_msg0("PN Code mismatch, lost unicast frame, sync pn code to recv packet");
                        // sync sta pn code with ap because of losting some packets
                        wd->sta.rxiv[0] = rxIV[0];
                        wd->sta.rxiv[1] = rxIV[1];
                        wd->sta.rxiv[2] = rxIV[2];
                        wd->sta.rxiv[3] = rxIV[3];
                        /* Discard PN Code Error frame */
                        //zm_debug_msg0("Discard PN Code mismatch unicast frame");
                        //zfwBufFree(dev, buf, 0);
                        //return;
                    }
                }
            }
        }
#endif //ZM_ENABLE_CENC

        /* for tally */
        if ((zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET) & 0x1) == 0)
        {
            /* for ACU to display RxRate */
            zfWlanUpdateRxRate(dev, addInfo);

            wd->commTally.rxUnicastFrm++;
            wd->commTally.rxUnicastOctets += (len-24);
        }
        else if (zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET) == 0xffff)
        {
            wd->commTally.rxBroadcastFrm++;
            wd->commTally.rxBroadcastOctets += (len-24);
        }
        else
        {
            wd->commTally.rxMulticastFrm++;
            wd->commTally.rxMulticastOctets += (len-24);
        }
        wd->ledStruct.rxTraffic++;

        if ((frameSubtype & 0x80) == 0x80)
        {
            /* if QoS control bit-7 is 1 => A-MSDU frame */
            if ((zmw_rx_buf_readh(dev, buf, 24) & 0x80) != 0)
            {
                zfDeAmsdu(dev, buf, vap, encryMode);
                return;
            }
        }

        // Remove MIC of TKIP
        if ( encryMode == ZM_TKIP )
        {
            zfwBufSetSize(dev, buf, zfwBufGetSize(dev, buf) - 8);
        }

        /* Convert 802.11 and SNAP header to ethernet header */
        if ( (wd->wlanMode == ZM_MODE_INFRASTRUCTURE)||
             (wd->wlanMode == ZM_MODE_IBSS) )
        {
            /* destination address*/
            da[0] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET);
            da[1] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET+2);
            da[2] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A1_OFFSET+4);

            /* check broadcast frame */
            if ( (da[0] == 0xffff) && (da[1] == 0xffff) && (da[2] == 0xffff) )
            {
                // Ap send broadcast frame to the DUT !
            }
            /* check multicast frame */
            /* TODO : Remove these code, hardware should be able to block */
            /*        multicast frame on the multicast address list       */
            /*        or bypass all multicast packet by flag bAllMulticast */
            else if ((da[0] & 0x01) && (wd->sta.bAllMulticast == 0))
            {
                for(ii=0; ii<wd->sta.multicastList.size; ii++)
                {
                    if ( zfMemoryIsEqual(wd->sta.multicastList.macAddr[ii].addr,
                                         (u8_t*) da, 6))
                    {
                        break;
                    }
                }

                if ( ii == wd->sta.multicastList.size )
                {   /* not found */
                    zm_debug_msg0("discard unknown multicast frame");

                    zfwBufFree(dev, buf, 0);
                    return;
                }
            }

#ifdef ZM_ENABLE_NATIVE_WIFI //Native Wifi : 1, Ethernet format : 0
            //To remove IV
            if (offset > 0)
            {
                for (i=12; i>0; i--)
                {
                    zmw_rx_buf_writeh(dev, buf, ((i-1)*2)+offset,
                            zmw_rx_buf_readh(dev, buf, (i-1)*2));
                }
                zfwBufRemoveHead(dev, buf, offset);
            }
#else

            if (zfRxBufferEqualToStr(dev, buf, zgSnapBridgeTunnel,
                                     24+offset, 6))
            {
                snapCase = 1;
            }
            else if ( zfRxBufferEqualToStr(dev, buf, zgSnap8021h,
                                           24+offset, 6) )
            {
                typeLengthField =
                    (((u16_t) zmw_rx_buf_readb(dev, buf, 30+offset)) << 8) +
                    zmw_rx_buf_readb(dev, buf, 31+offset);

                //zm_debug_msg2("tpyeLengthField = ", typeLengthField);

                //8137 : IPX, 80F3 : Appletalk
                if ( (typeLengthField != 0x8137)&&
                     (typeLengthField != 0x80F3) )
                {
                    snapCase = 2;
                }

                if ( typeLengthField == 0x888E )
                {
                    zfShowRxEAPOL(dev, buf, 32);
                }
            }
            else
            {
                //zfwDumpBuf(dev, buf);
            }

            /* source address */
            if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
            {
                /* SA = Address 3 */
                sa[0] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET);
                sa[1] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET+2);
                sa[2] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A3_OFFSET+4);
            }
            else
            {
                /* SA = Address 2 */
                sa[0] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET);
                sa[1] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+2);
                sa[2] = zmw_rx_buf_readh(dev, buf, ZM_WLAN_HEADER_A2_OFFSET+4);
            }

            if ( snapCase )
            {
                /* SA */
                zmw_rx_buf_writeh(dev, buf, 24+offset, sa[0]);
                zmw_rx_buf_writeh(dev, buf, 26+offset, sa[1]);
                zmw_rx_buf_writeh(dev, buf, 28+offset, sa[2]);

                /* DA = Address 1 */
                zmw_rx_buf_writeh(dev, buf, 18+offset, da[0]);
                zmw_rx_buf_writeh(dev, buf, 20+offset, da[1]);
                zmw_rx_buf_writeh(dev, buf, 22+offset, da[2]);
                zfwBufRemoveHead(dev, buf, 18+offset);
            }
            else
            {
                /* SA */
                zmw_rx_buf_writeh(dev, buf, 16+offset, sa[0]);
                zmw_rx_buf_writeh(dev, buf, 18+offset, sa[1]);
                zmw_rx_buf_writeh(dev, buf, 20+offset, sa[2]);

                /* DA = Address 1 */
                zmw_rx_buf_writeh(dev, buf, 10+offset, da[0]);
                zmw_rx_buf_writeh(dev, buf, 12+offset, da[1]);
                zmw_rx_buf_writeh(dev, buf, 14+offset, da[2]);
                zfwBufRemoveHead(dev, buf, 10+offset);
                /* Ethernet payload length */
                typeLengthField = zfwBufGetSize(dev, buf) - 14;
                zmw_rx_buf_writeh(dev, buf, 12, (typeLengthField<<8)+(typeLengthField>>8));
            }
#endif  // ZM_ENABLE_NATIVE_WIFI
        }
        else if (wd->wlanMode == ZM_MODE_AP)
        {
            //if ((zmw_rx_buf_readb(dev, buf, 1) & 0x3) != 3)
            if (vap < ZM_MAX_AP_SUPPORT)
            /* AP mode */
            {
#ifdef ZM_ENABLE_NATIVE_WIFI //Native Wifi : 1, Ethernet format : 0
                //To remove IV
                if (offset > 0)
                {
                    for (i=12; i>0; i--)
                    {
                        zmw_rx_buf_writeh(dev, buf, ((i-1)*2)+offset,
                                zmw_rx_buf_readh(dev, buf, (i-1)*2));
                    }
                    zfwBufRemoveHead(dev, buf, offset);
                }
#else
                /* SA = Address 2 */
                zmw_rx_buf_writeh(dev, buf, 24+offset, zmw_rx_buf_readh(dev, buf,
                        ZM_WLAN_HEADER_A2_OFFSET));
                zmw_rx_buf_writeh(dev, buf, 26+offset, zmw_rx_buf_readh(dev, buf,
                        ZM_WLAN_HEADER_A2_OFFSET+2));
                zmw_rx_buf_writeh(dev, buf, 28+offset, zmw_rx_buf_readh(dev, buf,
                        ZM_WLAN_HEADER_A2_OFFSET+4));
                /* DA = Address 3 */
                /* Seq : Read 20 write 22, read 18 write 20, read 16 write 18 */
                /* sequence must not be inverted */
                zmw_rx_buf_writeh(dev, buf, 22+offset, zmw_rx_buf_readh(dev, buf,
                        ZM_WLAN_HEADER_A3_OFFSET+4));
                zmw_rx_buf_writeh(dev, buf, 20+offset, zmw_rx_buf_readh(dev, buf,
                        ZM_WLAN_HEADER_A3_OFFSET+2));
                zmw_rx_buf_writeh(dev, buf, 18+offset, zmw_rx_buf_readh(dev, buf,
                        ZM_WLAN_HEADER_A3_OFFSET));
                zfwBufRemoveHead(dev, buf, 18+offset);
#endif  // ZM_ENABLE_NATIVE_WIFI
                #if 1
                ret = zfIntrabssForward(dev, buf, vap);
                if (ret == 1)
                {
                    /* Free Rx buffer if intra-BSS unicast frame */
                    zm_msg0_rx(ZM_LV_2, "Free intra-BSS unicast frame");
                    zfwBufFree(dev, buf, 0);
                    return;
                }
                #endif
            }
            else
            /* WDS mode */
            {
                zm_msg0_rx(ZM_LV_2, "Rx WDS data");

                /* SA = Address 4 */
                zmw_rx_buf_writeh(dev, buf, 30+offset, zmw_rx_buf_readh(dev, buf,
                        ZM_WLAN_HEADER_A4_OFFSET));
                zmw_rx_buf_writeh(dev, buf, 32+offset, zmw_rx_buf_readh(dev, buf,
                        ZM_WLAN_HEADER_A4_OFFSET+2));
                zmw_rx_buf_writeh(dev, buf, 34+offset, zmw_rx_buf_readh(dev, buf,
                        ZM_WLAN_HEADER_A4_OFFSET+4));
                /* DA = Address 3 */
                /* Seq : Read 20 write 22, read 18 write 20, read 16 write 18 */
                /* sequence must not be inverted */
                zmw_rx_buf_writeh(dev, buf, 28+offset, zmw_rx_buf_readh(dev, buf,
                        ZM_WLAN_HEADER_A3_OFFSET+4));
                zmw_rx_buf_writeh(dev, buf, 26+offset, zmw_rx_buf_readh(dev, buf,
                        ZM_WLAN_HEADER_A3_OFFSET+2));
                zmw_rx_buf_writeh(dev, buf, 24+offset, zmw_rx_buf_readh(dev, buf,
                        ZM_WLAN_HEADER_A3_OFFSET));
                zfwBufRemoveHead(dev, buf, 24+offset);
            }
        }
        else if (wd->wlanMode == ZM_MODE_PSEUDO)
        {
			/* WDS test: remove add4 */
            if (wd->enableWDS)
            {
                offset += 6;
            }

            /* SA = Address 2 */
            zmw_rx_buf_writeh(dev, buf, 24+offset, zmw_rx_buf_readh(dev, buf,
                              ZM_WLAN_HEADER_A2_OFFSET));
            zmw_rx_buf_writeh(dev, buf, 26+offset, zmw_rx_buf_readh(dev, buf,
                              ZM_WLAN_HEADER_A2_OFFSET+2));
            zmw_rx_buf_writeh(dev, buf, 28+offset, zmw_rx_buf_readh(dev, buf,
                              ZM_WLAN_HEADER_A2_OFFSET+4));
            /* DA = Address 1 */
            zmw_rx_buf_writeh(dev, buf, 18+offset, zmw_rx_buf_readh(dev, buf,
                              ZM_WLAN_HEADER_A1_OFFSET));
            zmw_rx_buf_writeh(dev, buf, 20+offset, zmw_rx_buf_readh(dev, buf,
                              ZM_WLAN_HEADER_A1_OFFSET+2));
            zmw_rx_buf_writeh(dev, buf, 22+offset, zmw_rx_buf_readh(dev, buf,
                              ZM_WLAN_HEADER_A1_OFFSET+4));
            zfwBufRemoveHead(dev, buf, 18+offset);
        }
        else
        {
            zm_assert(0);
        }

        /* Call zfwRecvEth() to notify upper layer */
        //zm_msg2_rx(ZM_LV_2, "Call zfwRecvEth(), buf=", buf);
        //zfwDumpBuf(dev, buf);

        #if ZM_PROTOCOL_RESPONSE_SIMULATION == 1
        zfProtRspSim(dev, buf);
        #endif
        //zfwDumpBuf(dev, buf);

        /* tally */
    	wd->commTally.NotifyNDISRxFrmCnt++;

    	if (wd->zfcbRecvEth != NULL)
    	{
            wd->zfcbRecvEth(dev, buf, vap);
            ZM_PERFORMANCE_RX_MSDU(dev, wd->tick)
        }
    }
    /* if management frame */
    else if (frameType == ZM_WLAN_MANAGEMENT_FRAME)
    {
        zm_msg2_rx(ZM_LV_2, "Rx management,FC=", frameCtrl);
        /* Call zfProcessManagement() to handle management frame */
        zfProcessManagement(dev, buf, addInfo); //CWYang(m)
        zfwBufFree(dev, buf, 0);
    }
    /* PsPoll */
    else if ((wd->wlanMode == ZM_MODE_AP) && (frameCtrl == 0xa4))
    {
        zm_msg0_rx(ZM_LV_0, "Rx PsPoll");
        zfApProcessPsPoll(dev, buf);
        zfwBufFree(dev, buf, 0);
    }
    else
    {
        zm_msg0_rx(ZM_LV_1, "Rx discard!!");
        wd->commTally.DriverDiscardedFrm++;

        zfwBufFree(dev, buf, 0);
    }
    return;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfWlanRxValidate            */
/*      Validate Rx frame.                                              */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : received 802.11 frame buffer.                             */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      Error code                                                      */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen             ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
u16_t zfWlanRxValidate(zdev_t* dev, zbuf_t* buf)
{
    u16_t frameType;
    u16_t frameCtrl;
    u16_t frameLen;
    u16_t ret;
    u8_t  frameSubType;

    zmw_get_wlan_dev(dev);

    frameCtrl = zmw_rx_buf_readh(dev, buf, 0);
    frameType = frameCtrl & 0xC;
    frameSubType = (frameCtrl & 0xF0) >> 4;

    frameLen = zfwBufGetSize(dev, buf);

    /* Accept Data/Management frame with protocol version = 0 */
    if ((frameType == 0x8) || (frameType == 0x0))
    {

        /* TODO : check rx status => erro bit */

        /* Check Minimum Length with Wep */
        if ((frameCtrl & 0x4000) != 0)
        {
            /* Minimum Length =                                       */
            /*     PLCP(5)+Header(24)+IV(4)+ICV(4)+CRC(4)+RxStatus(8) */
            if (frameLen < 32)
            {
                return ZM_ERR_MIN_RX_ENCRYPT_FRAME_LENGTH;
            }
        }
        else if ( frameSubType == 0x5 || frameSubType == 0x8 )
        {
            /* Minimum Length = PLCP(5)+MACHeader(24)+Timestamp(8)+BeaconInterval(2)+Cap(2)+CRC(4)+RxStatus(8) */
            if (frameLen < 36)
            {
                return ZM_ERR_MIN_RX_FRAME_LENGTH;
            }
        }
        else
        {
            /* Minimum Length = PLCP(5)+MACHeader(24)+CRC(4)+RxStatus(8) */
            if (frameLen < 24)
            {
                return ZM_ERR_MIN_RX_FRAME_LENGTH;
            }
        }

        /* Check if frame Length > ZM_WLAN_MAX_RX_SIZE. */
        if (frameLen > ZM_WLAN_MAX_RX_SIZE)
        {
            return ZM_ERR_MAX_RX_FRAME_LENGTH;
        }
    }
    else if ((frameCtrl&0xff) == 0xa4)
    {
        /* PsPoll */
        //zm_msg0_rx(ZM_LV_0, "rx pspoll");
    }
    else if ((frameCtrl&0xff) == ZM_WLAN_FRAME_TYPE_BAR)
    {
        if (wd->sta.enableDrvBA == 1)
        {
            zfAggRecvBAR(dev, buf);
        }

        return ZM_ERR_RX_BAR_FRAME;
    }
    else
    {
        return ZM_ERR_RX_FRAME_TYPE;
    }

    if ( wd->wlanMode == ZM_MODE_AP )
    {
    }
    else if ( wd->wlanMode != ZM_MODE_PSEUDO )
    {
        ret = zfStaRxValidateFrame(dev, buf);
        if (ret != ZM_SUCCESS)
        {
            //zm_debug_msg1("discard frame, code = ", ret);
            return ret;
        }
    }

    return ZM_SUCCESS;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfWlanRxFilter              */
/*      Filter duplicated frame.                                        */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : received 802.11 frame buffer.                             */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      Error code                                                      */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen             ZyDAS Technology Corporation    2005.10     */
/*                                                                      */
/************************************************************************/
u16_t zfWlanRxFilter(zdev_t* dev, zbuf_t* buf)
{
    u16_t src[3];
    u16_t dst0;
    u16_t frameType;
    u16_t seq;
    u16_t offset;
    u16_t index;
    u16_t col;
    u16_t i;
    u8_t up = 0; /* User priority */

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    ZM_BUFFER_TRACE(dev, buf)

    /* RX PREFIX */
    offset = 0;

    frameType = zmw_rx_buf_readh(dev, buf, offset);

    // Don't divide 2^4 because we don't want the fragmentation pkt to be treated as
    // duplicated frames
    seq = zmw_rx_buf_readh(dev, buf, offset+22);
    dst0 = zmw_rx_buf_readh(dev, buf, offset+4);
    src[0] = zmw_rx_buf_readh(dev, buf, offset+10);
    src[1] = zmw_rx_buf_readh(dev, buf, offset+12);
    src[2] = zmw_rx_buf_readh(dev, buf, offset+14);

    /* QoS data frame */
    if ((frameType & 0x88) == 0x88)
    {
        up = zmw_rx_buf_readb(dev, buf, offset+24);
        up &= 0x7;
    }

    index = (src[2]+up) & (ZM_FILTER_TABLE_ROW-1);

    /* TBD : filter frame with source address == own MAC address */
    if ((wd->macAddr[0] == src[0]) && (wd->macAddr[1] == src[1])
            && (wd->macAddr[2] == src[2]))
    {
        //zm_msg0_rx(ZM_LV_0, "Rx filter=>src is own MAC");
        wd->trafTally.rxSrcIsOwnMac++;
#if 0
        return ZM_ERR_RX_SRC_ADDR_IS_OWN_MAC;
#endif
    }

    zm_msg2_rx(ZM_LV_2, "Rx seq=", seq);

    /* Filter unicast frame only */
    if ((dst0 & 0x1) == 0)
    {
        zmw_enter_critical_section(dev);

        for(i=0; i<ZM_FILTER_TABLE_COL; i++)
        {
            if ((wd->rxFilterTbl[i][index].addr[0] == src[0])
                    && (wd->rxFilterTbl[i][index].addr[1] == src[1])
                    && (wd->rxFilterTbl[i][index].addr[2] == src[2])
                    && (wd->rxFilterTbl[i][index].up == up))
            {
                if (((frameType&0x800)==0x800)
                        &&(wd->rxFilterTbl[i][index].seq==seq))
                {
                    zmw_leave_critical_section(dev);
                    /* hit : duplicated frame */
                    zm_msg0_rx(ZM_LV_1, "Rx filter hit=>duplicated");
                    wd->trafTally.rxDuplicate++;
                    return ZM_ERR_RX_DUPLICATE;
                }
                else
                {
                    /* hit : not duplicated frame, update sequence number */
                    wd->rxFilterTbl[i][index].seq = seq;
                    zmw_leave_critical_section(dev);
                    zm_msg0_rx(ZM_LV_2, "Rx filter hit");
                    return ZM_SUCCESS;
                }
            }
        } /* for(i=0; i<ZM_FILTER_TABLE_COL; i++) */

        /* miss : add to table */
        zm_msg0_rx(ZM_LV_1, "Rx filter miss");
        /* TODO : Random select a column */
        col = (u16_t)(wd->tick & (ZM_FILTER_TABLE_COL-1));
        wd->rxFilterTbl[col][index].addr[0] = src[0];
        wd->rxFilterTbl[col][index].addr[1] = src[1];
        wd->rxFilterTbl[col][index].addr[2] = src[2];
        wd->rxFilterTbl[col][index].seq = seq;
        wd->rxFilterTbl[col][index].up = up;

        zmw_leave_critical_section(dev);
    } /* if ((dst0 & 0x1) == 0) */

    return ZM_SUCCESS;
}



u16_t zfTxGenWlanTail(zdev_t* dev, zbuf_t* buf, u16_t* snap, u16_t snaplen,
                      u16_t* mic)
{
    struct zsMicVar*  pMicKey;
    u16_t  i, length, payloadOffset;
    u8_t   bValue, qosType = 0;
    u8_t   snapByte[12];

    zmw_get_wlan_dev(dev);

    if ( wd->wlanMode == ZM_MODE_AP )
    {
        pMicKey = zfApGetTxMicKey(dev, buf, &qosType);

        if ( pMicKey == NULL )
        {
            return 0;
        }
    }
    else if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
    {
        pMicKey = zfStaGetTxMicKey(dev, buf);

        if ( pMicKey == NULL )
        {
            return 0;
        }
    }
    else
    {
        return 0;
    }

    length = zfwBufGetSize(dev, buf);

    zfMicClear(pMicKey);

    /* append DA and SA */
#ifdef ZM_ENABLE_NATIVE_WIFI
    for(i=16; i<22; i++)
    { // VISTA DA
        bValue = zmw_tx_buf_readb(dev, buf, i);
        zfMicAppendByte(bValue, pMicKey);
    }
    for(i=10; i<16; i++)
    { // VISTA SA
        bValue = zmw_tx_buf_readb(dev, buf, i);
        zfMicAppendByte(bValue, pMicKey);
    }
#else
    for(i=0; i<12; i++)
    {
        bValue = zmw_tx_buf_readb(dev, buf, i);
        zfMicAppendByte(bValue, pMicKey);
    }
#endif

    /* append for alignment */
    if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
    {
        if (wd->sta.wmeConnected != 0)
            zfMicAppendByte(zmw_tx_buf_readb(dev, buf, ZM_80211_FRAME_IP_OFFSET + 1) >> 5, pMicKey);
        else
            zfMicAppendByte(0, pMicKey);
    }
    else if ( wd->wlanMode == ZM_MODE_AP )
    {
        if (qosType == 1)
            zfMicAppendByte(zmw_tx_buf_readb(dev, buf, ZM_80211_FRAME_IP_OFFSET + 1) >> 5, pMicKey);
        else
            zfMicAppendByte(0, pMicKey);
    }
    else
    {
        /* TODO : Qos Software MIC in IBSS Mode */
        zfMicAppendByte(0, pMicKey);
    }
    zfMicAppendByte(0, pMicKey);
    zfMicAppendByte(0, pMicKey);
    zfMicAppendByte(0, pMicKey);

    if ( snaplen == 0 )
    {
        payloadOffset = ZM_80211_FRAME_IP_OFFSET;
    }
    else
    {
        payloadOffset = ZM_80211_FRAME_TYPE_OFFSET;

        for(i=0; i<(snaplen>>1); i++)
        {
            snapByte[i*2] = (u8_t) (snap[i] & 0xff);
            snapByte[i*2+1] = (u8_t) ((snap[i] >> 8) & 0xff);
        }

        for(i=0; i<snaplen; i++)
        {
            zfMicAppendByte(snapByte[i], pMicKey);
        }
    }

    for(i=payloadOffset; i<length; i++)
    {
        bValue = zmw_tx_buf_readb(dev, buf, i);
        zfMicAppendByte(bValue, pMicKey);
    }

    zfMicGetMic( (u8_t*) mic, pMicKey);

    return ZM_SIZE_OF_MIC;
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfTxGetIpTosAndFrag         */
/*      Get IP TOS and frag offset from Tx buffer                       */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : Tx buffer pointer                                         */
/*      up : pointer for returning user priority                        */
/*      fragOff : pointer for returning ip frag offset                  */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2006.6      */
/*                                                                      */
/************************************************************************/
void zfTxGetIpTosAndFrag(zdev_t* dev, zbuf_t* buf, u8_t* up, u16_t* fragOff)
{
    u8_t ipv;
    u16_t len;
	u16_t etherType;
    u8_t tos;

    *up = 0;
    *fragOff = 0;

    len = zfwBufGetSize(dev, buf);

    if (len >= 34) //Minimum IPv4 packet size, 14(Ether header)+20(IPv4 header)
    {
        etherType = (((u16_t)zmw_tx_buf_readb(dev, buf, ZM_80211_FRAME_TYPE_OFFSET))<<8)
                    + zmw_tx_buf_readb(dev, buf, ZM_80211_FRAME_TYPE_OFFSET + 1);

        /* protocol type = IP */
        if (etherType == 0x0800)
        {
            ipv = zmw_tx_buf_readb(dev, buf, ZM_80211_FRAME_IP_OFFSET) >> 4;
            if (ipv == 0x4) //IPv4
            {
                tos = zmw_tx_buf_readb(dev, buf, ZM_80211_FRAME_IP_OFFSET + 1);
                *up = (tos >> 5);
                *fragOff = zmw_tx_buf_readh(dev, buf, ZM_80211_FRAME_IP_OFFSET + 6);
            }
            /* TODO : handle VLAN tag and IPv6 packet */
        }
    }
    return;
}

#ifdef ZM_ENABLE_NATIVE_WIFI
u16_t zfTxGenWlanSnap(zdev_t* dev, zbuf_t* buf, u16_t* snap, u16_t* snaplen)
{
    snap[0] = zmw_buf_readh(dev, buf, ZM_80211_FRAME_HEADER_LEN + 0);
    snap[1] = zmw_buf_readh(dev, buf, ZM_80211_FRAME_HEADER_LEN + 2);
    snap[2] = zmw_buf_readh(dev, buf, ZM_80211_FRAME_HEADER_LEN + 4);
    *snaplen = 6;

    return ZM_80211_FRAME_HEADER_LEN + *snaplen;
}
#else
u16_t zfTxGenWlanSnap(zdev_t* dev, zbuf_t* buf, u16_t* snap, u16_t* snaplen)
{
    u16_t removed;
	   u16_t etherType;
   	u16_t len;

	   len = zfwBufGetSize(dev, buf);
    if (len < 14) //Minimum Ethernet packet size, 14(Ether header)
    {
        /* TODO : Assert? */
        *snaplen = 0;
        return 0;
    }

    /* Generate RFC1042 header */
    etherType = (((u16_t)zmw_tx_buf_readb(dev, buf, 12))<<8)
                + zmw_tx_buf_readb(dev, buf, 13);

    //zm_debug_msg2("ethernet type or length = ", etherType);

    if (etherType > 1500)
    {
        /* ETHERNET format */
        removed = 12;
        snap[0] = 0xaaaa;
        snap[1] = 0x0003;
        if ((etherType ==0x8137) || (etherType == 0x80f3))
        {
            /* Bridge Tunnel */
            snap[2] = 0xF800;
        }
        else
        {
            /* RFC 1042 */
            snap[2] = 0x0000;
        }
        *snaplen = 6;

        if ( etherType == 0x888E )
        {
            zfShowTxEAPOL(dev, buf, 14);
        }
    }
    else
    {
        /* 802.3 format */
        removed = 14;
        *snaplen = 0;
    }

    return removed;
}
#endif

u8_t zfIsVtxqEmpty(zdev_t* dev)
{
    u8_t isEmpty = TRUE;
    u8_t i;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);

    if (wd->vmmqHead != wd->vmmqTail)
    {
        isEmpty = FALSE;
        goto check_done;
    }

    for(i=0; i < 4; i++)
    {
        if (wd->vtxqHead[i] != wd->vtxqTail[i])
        {
            isEmpty = FALSE;
            goto check_done;
        }
    }

check_done:
    zmw_leave_critical_section(dev);
    return isEmpty;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfPutVtxq                   */
/*      Put Tx buffer to virtual TxQ                                    */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : Tx buffer pointer                                         */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      ZM_SUCCESS or error code                                        */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2006.6      */
/*                                                                      */
/************************************************************************/
u16_t zfPutVtxq(zdev_t* dev, zbuf_t* buf)
{
    u8_t ac;
    u8_t up;
    u16_t fragOff;
#ifdef ZM_AGG_TALLY
    struct aggTally *agg_tal;
#endif
#ifdef ZM_ENABLE_AGGREGATION
    #ifndef ZM_BYPASS_AGGR_SCHEDULING
	u16_t ret;
    u16_t tid;
    #endif
#endif

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    zfTxGetIpTosAndFrag(dev, buf, &up, &fragOff);

    if ( wd->zfcbClassifyTxPacket != NULL )
    {
        ac = wd->zfcbClassifyTxPacket(dev, buf);
    }
    else
    {
        ac = zcUpToAc[up&0x7] & 0x3;
    }

    /*
     * add by honda
     * main A-MPDU aggregation function
     */
#ifdef ZM_AGG_TALLY
    agg_tal = &wd->agg_tal;
    agg_tal->got_packets_sum++;

#endif

#ifdef ZM_ENABLE_AGGREGATION
    #ifndef ZM_BYPASS_AGGR_SCHEDULING
    tid = up&0x7;
    if(wd->enableAggregation==0)
    {
        if( (wd->wlanMode == ZM_MODE_AP) ||
            (wd->wlanMode == ZM_MODE_INFRASTRUCTURE && wd->sta.EnableHT) ||
            (wd->wlanMode == ZM_MODE_PSEUDO) ) {
            // (infrastructure_mode && connect_to_11n_ap) || (ap_mode && is_11n_ap)
            //ret = zfAggPutVtxq(dev, buf);


            ret = zfAggTx(dev, buf, tid);
            if (ZM_SUCCESS == ret)
            {
                //zfwBufFree(dev, buf, ZM_SUCCESS);

                return ZM_SUCCESS;
            }
            if (ZM_ERR_EXCEED_PRIORITY_THRESHOLD == ret)
            {
                wd->commTally.txQosDropCount[ac]++;
                zfwBufFree(dev, buf, ZM_SUCCESS);

                zm_msg1_tx(ZM_LV_1, "Packet discarded, VTXQ full, ac=", ac);

                return ZM_ERR_EXCEED_PRIORITY_THRESHOLD;
            }
            if (ZM_ERR_TX_BUFFER_UNAVAILABLE == ret)
            {
                /*
                * do nothing
                * continue following procession, put into VTXQ
                * return ZM_SUCCESS;
                */
            }
        }
    }
    #endif
#endif
    /*
     * end of add by honda
     */

    /* First Ip frag */
    if ((fragOff & 0xff3f) == 0x0020)
    {
        /* Don't let ip frag in if VTXQ unable to hold */
        /* whole ip frag burst(assume 20 frag)         */
        zmw_enter_critical_section(dev);
        if (((wd->vtxqHead[ac] - wd->vtxqTail[ac])& ZM_VTXQ_SIZE_MASK)
                > (ZM_VTXQ_SIZE-20))
        {
            wd->qosDropIpFrag[ac] = 1;
        }
        else
        {
            wd->qosDropIpFrag[ac] = 0;
        }
        zmw_leave_critical_section(dev);

        if (wd->qosDropIpFrag[ac] == 1)
        {
            //zm_debug_msg2("vtQ full, drop buf = ", buf);
            wd->commTally.txQosDropCount[ac]++;
            zfwBufFree(dev, buf, ZM_SUCCESS);
            zm_msg1_tx(ZM_LV_1, "Packet discarded, first ip frag, ac=", ac);
            //VTXQ[] can not hold whold ip frag burst(assume 20 frags)
            return ZM_ERR_EXCEED_PRIORITY_THRESHOLD;
        }
    }
    else if ((fragOff & 0xff3f) == 0)
    {
        wd->qosDropIpFrag[ac] = 0;
    }

    if (((fragOff &= 0xff1f) != 0) && (wd->qosDropIpFrag[ac] == 1))
    {
        wd->commTally.txQosDropCount[ac]++;
        zfwBufFree(dev, buf, ZM_SUCCESS);
        zm_msg1_tx(ZM_LV_1, "Packet discarded, ip frag, ac=", ac);
        //Discard following ip frags
        return ZM_ERR_EXCEED_PRIORITY_THRESHOLD;
    }

    zmw_enter_critical_section(dev);
    if (((wd->vtxqHead[ac] + 1) & ZM_VTXQ_SIZE_MASK) != wd->vtxqTail[ac])
    {
        wd->vtxq[ac][wd->vtxqHead[ac]] = buf;
        wd->vtxqHead[ac] = ((wd->vtxqHead[ac] + 1) & ZM_VTXQ_SIZE_MASK);
        zmw_leave_critical_section(dev);
        return ZM_SUCCESS;
    }
    else
    {
        zmw_leave_critical_section(dev);

        wd->commTally.txQosDropCount[ac]++;
        zfwBufFree(dev, buf, ZM_SUCCESS);
        zm_msg1_tx(ZM_LV_1, "Packet discarded, VTXQ full, ac=", ac);
        return ZM_ERR_EXCEED_PRIORITY_THRESHOLD; //VTXQ[] Full
    }
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfGetVtxq                   */
/*      Get Tx buffer from virtual TxQ                                  */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      Tx buffer pointer                                               */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2006.6      */
/*                                                                      */
/************************************************************************/
zbuf_t* zfGetVtxq(zdev_t* dev, u8_t ac)
{
    zbuf_t* buf;

    zmw_get_wlan_dev(dev);

    zmw_declare_for_critical_section();

    ac &= 0x3;
    zmw_enter_critical_section(dev);
    if (wd->vtxqHead[ac] != wd->vtxqTail[ac])
    {
        buf = wd->vtxq[ac][wd->vtxqTail[ac]];
        wd->vtxqTail[ac] = ((wd->vtxqTail[ac] + 1) & ZM_VTXQ_SIZE_MASK);
        zmw_leave_critical_section(dev);
        return buf;
    }
    else
    {
        zmw_leave_critical_section(dev);
        return 0; //VTXQ[] empty
    }
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfPutVmmq                   */
/*      Put Tx buffer to virtual MmQ                                    */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*      buf : Tx buffer pointer                                         */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      ZM_SUCCESS or error code                                        */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2006.12     */
/*                                                                      */
/************************************************************************/
u16_t zfPutVmmq(zdev_t* dev, zbuf_t* buf)
{
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    if (((wd->vmmqHead + 1) & ZM_VMMQ_SIZE_MASK) != wd->vmmqTail)
    {
        wd->vmmq[wd->vmmqHead] = buf;
        wd->vmmqHead = ((wd->vmmqHead + 1) & ZM_VMMQ_SIZE_MASK);
        zmw_leave_critical_section(dev);
        return ZM_SUCCESS;
    }
    else
    {
        zmw_leave_critical_section(dev);

        zfwBufFree(dev, buf, ZM_SUCCESS);
        zm_msg0_mm(ZM_LV_0, "Packet discarded, VMmQ full");
        return ZM_ERR_VMMQ_FULL; //VTXQ[] Full
    }
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfGetVmmq                   */
/*      Get Tx buffer from virtual MmQ                                  */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      Tx buffer pointer                                               */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2006.12     */
/*                                                                      */
/************************************************************************/
zbuf_t* zfGetVmmq(zdev_t* dev)
{
    zbuf_t* buf;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();

    zmw_enter_critical_section(dev);
    if (wd->vmmqHead != wd->vmmqTail)
    {
        buf = wd->vmmq[wd->vmmqTail];
        wd->vmmqTail = ((wd->vmmqTail + 1) & ZM_VMMQ_SIZE_MASK);
        zmw_leave_critical_section(dev);
        return buf;
    }
    else
    {
        zmw_leave_critical_section(dev);
        return 0; //VTXQ[] empty
    }
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfPushVtxq                  */
/*      Service Virtual TxQ (weighted round robin)                      */
/*      Get Tx buffer form virtual TxQ and put to hardware TxD queue    */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        ZyDAS Technology Corporation    2006.6      */
/*                                                                      */
/************************************************************************/
void zfPushVtxq(zdev_t* dev)
{
    zbuf_t* buf;
    u16_t i;
    u16_t txed;
    u32_t freeTxd;
    u16_t err;
    u16_t skipFlag = 0;
    zmw_get_wlan_dev(dev);
    zmw_declare_for_critical_section();



    //zm_debug_msg1("zfHpGetFreeTxdCount = ", zfHpGetFreeTxdCount(dev));

    if (wd->halState == ZM_HAL_STATE_INIT)
    {
        if (!wd->modeMDKEnable)
        {
            zm_debug_msg0("HAL is not ready for Tx");
        }
        return;
    }
    else if (wd->sta.DFSDisableTx)
    {
        zm_debug_msg0("return because 802.11h DFS Disable Tx");
        return;
    }
    else if (wd->sta.flagFreqChanging != 0)
    {
        //Hold until RF frequency changed
        return;
    }
    else if (( wd->sta.flagKeyChanging ) && ( wd->wlanMode != ZM_MODE_AP ))
    {
        return;
    }
#ifdef ZM_ENABLE_POWER_SAVE
    else if ( zfPowerSavingMgrIsSleeping(dev) )
    {
        //zm_debug_msg0("Packets queued since the MAC is in power-saving mode\n");
        return;
    }
#endif

    zmw_enter_critical_section(dev);
    if (wd->vtxqPushing != 0)
    {
        skipFlag = 1;
    }
    else
    {
        wd->vtxqPushing = 1;
    }
    zmw_leave_critical_section(dev);

    if (skipFlag == 1)
    {
        return;
    }

    while (1)
    {
        txed = 0;

        /* 2006.12.20, Serve Management queue */
        while( zfHpGetFreeTxdCount(dev) > 0 )
        {
            buf = zfGetVmmq(dev);
            if (buf != 0)
            {
                txed = 1;
                //zm_debug_msg2("send buf = ", buf);
                err = zfHpSend(dev, NULL, 0, NULL, 0, NULL, 0, buf, 0,
			       ZM_INTERNAL_ALLOC_BUF, 0, 0xff);
                if (err != ZM_SUCCESS)
                {
                    zfwBufFree(dev, buf, 0);
                }
            }
            else
            {
                break;
            }
        }
        if ((wd->sta.bScheduleScan) || ((wd->sta.bChannelScan == TRUE) && (zfStaIsConnected(dev))))
        {
            //Hold until Scan Stop
            wd->vtxqPushing = 0;
            return;
        }

#ifdef ZM_ENABLE_AGGREGATION
    #ifndef ZM_BYPASS_AGGR_SCHEDULING
        if( (wd->wlanMode == ZM_MODE_AP) ||
            (wd->wlanMode == ZM_MODE_INFRASTRUCTURE && wd->sta.EnableHT) ||
            (wd->wlanMode == ZM_MODE_PSEUDO) ) {

            zfAggTxScheduler(dev, 0);

            if (txed == 0) {
                wd->vtxqPushing = 0;
                return;
            }
            else {
                continue;
            }
        }
    #endif
#endif

        /* Service VTxQ[3] */
        for (i=0; i<4; i++)
        {
            freeTxd = zfHpGetFreeTxdCount(dev);
            if (freeTxd >= 3)
            {
                buf = zfGetVtxq(dev, 3);
                if (buf != 0)
                {
                    txed = 1;
                    //zm_debug_msg2("send buf = ", buf);
                    zfTxSendEth(dev, buf, 0, ZM_EXTERNAL_ALLOC_BUF, 0);
                    ZM_PERFORMANCE_TX_MPDU(dev, wd->tick);
                }
            }
            else
            {
                break;
            }
        }

        /* Service VTxQ[2] */
        for (i=0; i<3; i++)
        {
            freeTxd = zfHpGetFreeTxdCount(dev);
            if (freeTxd >= (zfHpGetMaxTxdCount(dev)*1/4))
            {
                buf = zfGetVtxq(dev, 2);
                if (buf != 0)
                {
                    txed = 1;
                    zfTxSendEth(dev, buf, 0, ZM_EXTERNAL_ALLOC_BUF, 0);
                    ZM_PERFORMANCE_TX_MPDU(dev, wd->tick);
                }
                if (wd->sta.ac0PriorityHigherThanAc2 == 1)
                {
                    buf = zfGetVtxq(dev, 0);
                    if (buf != 0)
                    {
                        txed = 1;
                        zfTxSendEth(dev, buf, 0, ZM_EXTERNAL_ALLOC_BUF, 0);
                        ZM_PERFORMANCE_TX_MPDU(dev, wd->tick);
                    }
                }
            }
            else
            {
                break;
            }
        }

        /* Service VTxQ[0] */
        for (i=0; i<2; i++)
        {
            freeTxd = zfHpGetFreeTxdCount(dev);
            if (freeTxd >= (zfHpGetMaxTxdCount(dev)*2/4))
            {
                buf = zfGetVtxq(dev, 0);
                if (buf != 0)
                {
                    txed = 1;
                    zfTxSendEth(dev, buf, 0, ZM_EXTERNAL_ALLOC_BUF, 0);
                    ZM_PERFORMANCE_TX_MPDU(dev, wd->tick);
                }
            }
            else
            {
                break;
            }

        }

        /* Service VTxQ[1] */
        freeTxd = zfHpGetFreeTxdCount(dev);
        if (freeTxd >= (zfHpGetMaxTxdCount(dev)*3/4))
        {
            buf = zfGetVtxq(dev, 1);
            if (buf != 0)
            {
                txed = 1;
                zfTxSendEth(dev, buf, 0, ZM_EXTERNAL_ALLOC_BUF, 0);
                ZM_PERFORMANCE_TX_MPDU(dev, wd->tick);
            }
        }

        /* All VTxQs are either empty or exceed their threshold */
        if (txed == 0)
        {
            wd->vtxqPushing = 0;
            return;
        }
    } //while (1)
}


/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                  zfFlushVtxq                 */
/*      Flush Virtual TxQ and MmQ                                       */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.1      */
/*                                                                      */
/************************************************************************/
void zfFlushVtxq(zdev_t* dev)
{
    zbuf_t* buf;
    u8_t i;
    zmw_get_wlan_dev(dev);

    /* Flush MmQ */
    while ((buf = zfGetVmmq(dev)) != 0)
    {
        zfwBufFree(dev, buf, 0);
        zm_debug_msg0("zfFlushVtxq: [Vmmq]");
        wd->queueFlushed  |= 0x10;
    }

    /* Flush VTxQ */
    for (i=0; i<4; i++)
    {
        while ((buf = zfGetVtxq(dev, i)) != 0)
        {
            zfwBufFree(dev, buf, 0);
            zm_debug_msg1("zfFlushVtxq: [zfGetVtxq]- ", i);
            wd->queueFlushed |= (1<<i);
        }
    }
}

void zf80211FrameSend(zdev_t* dev, zbuf_t* buf, u16_t* header, u16_t snapLen,
                           u16_t* da, u16_t* sa, u8_t up, u16_t headerLen, u16_t* snap,
                           u16_t* tail, u16_t tailLen, u16_t offset, u16_t bufType,
                           u8_t ac, u8_t keyIdx)
{
    u16_t err;
    u16_t fragLen;

    zmw_get_wlan_dev(dev);

    fragLen = zfwBufGetSize(dev, buf);
    if ((da[0]&0x1) == 0)
    {
        wd->commTally.txUnicastFrm++;
        wd->commTally.txUnicastOctets += (fragLen+snapLen);
    }
    else if (da[0] == 0xffff)
    {
        wd->commTally.txBroadcastFrm++;
        wd->commTally.txBroadcastOctets += (fragLen+snapLen);
    }
    else
    {
        wd->commTally.txMulticastFrm++;
        wd->commTally.txMulticastOctets += (fragLen+snapLen);
    }
    wd->ledStruct.txTraffic++;

    err = zfHpSend(dev, header, headerLen, snap, snapLen,
                        tail, tailLen, buf, offset,
                        bufType, ac, keyIdx);
    if (err != ZM_SUCCESS)
    {
        if (bufType == ZM_EXTERNAL_ALLOC_BUF)
        {
            zfwBufFree(dev, buf, err);
        }
        else if (bufType == ZM_INTERNAL_ALLOC_BUF)
        {
            zfwBufFree(dev, buf, 0);
        }
        else
        {
            zm_assert(0);
        }
    }
}

void zfCheckIsRIFSFrame(zdev_t* dev, zbuf_t* buf, u16_t frameSubtype)
{
    zmw_get_wlan_dev(dev);

    /* #2 Record the sequence number to determine whether the unicast frame is separated by RIFS or not */
    if (frameSubtype & 0x80)
    {   //QoS data frame
        u16_t sequenceNum;
        u16_t qosControlField;

        sequenceNum = ( zmw_buf_readh(dev, buf, 22) >> 4 ); // Discard fragment number !
        qosControlField = zmw_buf_readh(dev, buf, 24); // Don't consider WDS (Wireless Distribution System)
        //DbgPrint("The QoS Control Field                              : %d", qosControlField);
        //DbgPrint("The RIFS Count                                     : %d", wd->sta.rifsCount);

        if( qosControlField & ZM_BIT_5 )
        {// ACK policy is "No ACK"
            /* RIFS-Like frame */
            wd->sta.rifsLikeFrameSequence[wd->sta.rifsLikeFrameCnt]   = sequenceNum;

            if( wd->sta.rifsState == ZM_RIFS_STATE_DETECTING )
            {
                if( wd->sta.rifsLikeFrameSequence[2] != 0 )
                {// RIFS-like Pattern collected
                    if( ( wd->sta.rifsLikeFrameSequence[2] - wd->sta.rifsLikeFrameSequence[1] == 2 ) &&
                        ( wd->sta.rifsLikeFrameSequence[1] - wd->sta.rifsLikeFrameSequence[0] == 2 ) )
                    {
                        /* RIFS pattern matched */

                        /* #3 Enable RIFS function if the RIFS pattern matched  */
                        zfHpEnableRifs(dev, ((wd->sta.currentFrequency<3000)?1:0), wd->sta.EnableHT, wd->sta.HT2040);

                        // Set RIFS timer
                        wd->sta.rifsTimer = wd->tick;

                        wd->sta.rifsCount++;

                        // Set state to be Detected
                        wd->sta.rifsState = ZM_RIFS_STATE_DETECTED;
                    }
                }
            }
            else
            {// state = Detected
                // Reset RIFS timer
                if( (wd->tick - wd->sta.rifsTimer) < ZM_RIFS_TIMER_TIMEOUT )
                    wd->sta.rifsTimer = wd->tick;
            }

            //DbgPrint("SN1 = %d, SN2 = %d, SN3 = %d\n", wd->sta.rifsLikeFrameSequence[0],
            //                                           wd->sta.rifsLikeFrameSequence[1],
            //                                           wd->sta.rifsLikeFrameSequence[2]);

            // Update RIFS-like sequence number
            if( wd->sta.rifsLikeFrameSequence[2] != 0 )
            {
                wd->sta.rifsLikeFrameSequence[0] = wd->sta.rifsLikeFrameSequence[1];
                wd->sta.rifsLikeFrameSequence[1] = wd->sta.rifsLikeFrameSequence[2];
                wd->sta.rifsLikeFrameSequence[2] = 0;
            }

            // Only record three adjacent frame
            if( wd->sta.rifsLikeFrameCnt < 2 )
                wd->sta.rifsLikeFrameCnt++;
        }
    }

    /* #4 Disable RIFS function if the timer TIMEOUT  */
    if( wd->sta.rifsState == ZM_RIFS_STATE_DETECTED )
    {
        if( ( wd->tick - wd->sta.rifsTimer ) > ZM_RIFS_TIMER_TIMEOUT )
        {// TIMEOUT
            // Disable RIFS
            zfHpDisableRifs(dev);

            // Reset RIFS-like sequence number FIFO
            wd->sta.rifsLikeFrameSequence[0] = 0;
            wd->sta.rifsLikeFrameSequence[1] = 0;
            wd->sta.rifsLikeFrameSequence[2] = 0;
            wd->sta.rifsLikeFrameCnt = 0;

            // Set state to be Detecting
            wd->sta.rifsState = ZM_RIFS_STATE_DETECTING;
        }
    }
}
