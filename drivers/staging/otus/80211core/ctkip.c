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
/*  Module Name : ctkip.c                                               */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains Tx and Rx functions.                       */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/
#include "cprecomp.h"

u16_t zgTkipSboxLower[256] =
    {
        0xA5,0x84,0x99,0x8D,0x0D,0xBD,0xB1,0x54,
        0x50,0x03,0xA9,0x7D,0x19,0x62,0xE6,0x9A,
        0x45,0x9D,0x40,0x87,0x15,0xEB,0xC9,0x0B,
        0xEC,0x67,0xFD,0xEA,0xBF,0xF7,0x96,0x5B,
        0xC2,0x1C,0xAE,0x6A,0x5A,0x41,0x02,0x4F,
        0x5C,0xF4,0x34,0x08,0x93,0x73,0x53,0x3F,
        0x0C,0x52,0x65,0x5E,0x28,0xA1,0x0F,0xB5,
        0x09,0x36,0x9B,0x3D,0x26,0x69,0xCD,0x9F,
        0x1B,0x9E,0x74,0x2E,0x2D,0xB2,0xEE,0xFB,
        0xF6,0x4D,0x61,0xCE,0x7B,0x3E,0x71,0x97,
        0xF5,0x68,0x00,0x2C,0x60,0x1F,0xC8,0xED,
        0xBE,0x46,0xD9,0x4B,0xDE,0xD4,0xE8,0x4A,
        0x6B,0x2A,0xE5,0x16,0xC5,0xD7,0x55,0x94,
        0xCF,0x10,0x06,0x81,0xF0,0x44,0xBA,0xE3,
        0xF3,0xFE,0xC0,0x8A,0xAD,0xBC,0x48,0x04,
        0xDF,0xC1,0x75,0x63,0x30,0x1A,0x0E,0x6D,
        0x4C,0x14,0x35,0x2F,0xE1,0xA2,0xCC,0x39,
        0x57,0xF2,0x82,0x47,0xAC,0xE7,0x2B,0x95,
        0xA0,0x98,0xD1,0x7F,0x66,0x7E,0xAB,0x83,
        0xCA,0x29,0xD3,0x3C,0x79,0xE2,0x1D,0x76,
        0x3B,0x56,0x4E,0x1E,0xDB,0x0A,0x6C,0xE4,
        0x5D,0x6E,0xEF,0xA6,0xA8,0xA4,0x37,0x8B,
        0x32,0x43,0x59,0xB7,0x8C,0x64,0xD2,0xE0,
        0xB4,0xFA,0x07,0x25,0xAF,0x8E,0xE9,0x18,
        0xD5,0x88,0x6F,0x72,0x24,0xF1,0xC7,0x51,
        0x23,0x7C,0x9C,0x21,0xDD,0xDC,0x86,0x85,
        0x90,0x42,0xC4,0xAA,0xD8,0x05,0x01,0x12,
        0xA3,0x5F,0xF9,0xD0,0x91,0x58,0x27,0xB9,
        0x38,0x13,0xB3,0x33,0xBB,0x70,0x89,0xA7,
        0xB6,0x22,0x92,0x20,0x49,0xFF,0x78,0x7A,
        0x8F,0xF8,0x80,0x17,0xDA,0x31,0xC6,0xB8,
        0xC3,0xB0,0x77,0x11,0xCB,0xFC,0xD6,0x3A
    };


u16_t zgTkipSboxUpper[256] =
    {
        0xC6,0xF8,0xEE,0xF6,0xFF,0xD6,0xDE,0x91,
        0x60,0x02,0xCE,0x56,0xE7,0xB5,0x4D,0xEC,
        0x8F,0x1F,0x89,0xFA,0xEF,0xB2,0x8E,0xFB,
        0x41,0xB3,0x5F,0x45,0x23,0x53,0xE4,0x9B,
        0x75,0xE1,0x3D,0x4C,0x6C,0x7E,0xF5,0x83,
        0x68,0x51,0xD1,0xF9,0xE2,0xAB,0x62,0x2A,
        0x08,0x95,0x46,0x9D,0x30,0x37,0x0A,0x2F,
        0x0E,0x24,0x1B,0xDF,0xCD,0x4E,0x7F,0xEA,
        0x12,0x1D,0x58,0x34,0x36,0xDC,0xB4,0x5B,
        0xA4,0x76,0xB7,0x7D,0x52,0xDD,0x5E,0x13,
        0xA6,0xB9,0x00,0xC1,0x40,0xE3,0x79,0xB6,
        0xD4,0x8D,0x67,0x72,0x94,0x98,0xB0,0x85,
        0xBB,0xC5,0x4F,0xED,0x86,0x9A,0x66,0x11,
        0x8A,0xE9,0x04,0xFE,0xA0,0x78,0x25,0x4B,
        0xA2,0x5D,0x80,0x05,0x3F,0x21,0x70,0xF1,
        0x63,0x77,0xAF,0x42,0x20,0xE5,0xFD,0xBF,
        0x81,0x18,0x26,0xC3,0xBE,0x35,0x88,0x2E,
        0x93,0x55,0xFC,0x7A,0xC8,0xBA,0x32,0xE6,
        0xC0,0x19,0x9E,0xA3,0x44,0x54,0x3B,0x0B,
        0x8C,0xC7,0x6B,0x28,0xA7,0xBC,0x16,0xAD,
        0xDB,0x64,0x74,0x14,0x92,0x0C,0x48,0xB8,
        0x9F,0xBD,0x43,0xC4,0x39,0x31,0xD3,0xF2,
        0xD5,0x8B,0x6E,0xDA,0x01,0xB1,0x9C,0x49,
        0xD8,0xAC,0xF3,0xCF,0xCA,0xF4,0x47,0x10,
        0x6F,0xF0,0x4A,0x5C,0x38,0x57,0x73,0x97,
        0xCB,0xA1,0xE8,0x3E,0x96,0x61,0x0D,0x0F,
        0xE0,0x7C,0x71,0xCC,0x90,0x06,0xF7,0x1C,
        0xC2,0x6A,0xAE,0x69,0x17,0x99,0x3A,0x27,
        0xD9,0xEB,0x2B,0x22,0xD2,0xA9,0x07,0x33,
        0x2D,0x3C,0x15,0xC9,0x87,0xAA,0x50,0xA5,
        0x03,0x59,0x09,0x1A,0x65,0xD7,0x84,0xD0,
        0x82,0x29,0x5A,0x1E,0x7B,0xA8,0x6D,0x2C
    };

u16_t zfrotr1(u16_t a)
// rotate right by 1 bit.
{
    u16_t b;

    if (a & 0x01)
    {
        b = (a >> 1) | 0x8000;
    }
    else
    {
        b = (a >> 1) & 0x7fff;
    }
    return b;
}

/*************************************************************/
/* zfTkipSbox()                                              */
/* Returns a 16 bit value from a 64K entry table. The Table  */
/* is synthesized from two 256 entry byte wide tables.       */
/*************************************************************/
u16_t zfTkipSbox(u16_t index)
{
    u16_t   low;
    u16_t   high;
    u16_t   left, right;

    low = (index & 0xFF);
    high = ((index >> 8) & 0xFF);

    left = zgTkipSboxLower[low] + (zgTkipSboxUpper[low] << 8 );
    right = zgTkipSboxUpper[high] + (zgTkipSboxLower[high] << 8 );

    return (left ^ right);
}

u8_t zfTkipPhase1KeyMix(u32_t iv32, struct zsTkipSeed* pSeed)
{
    u16_t   tsc0;
    u16_t   tsc1;
    u16_t   i, j;
#if 0
    /* Need not proceed this function with the same iv32 */
    if ( iv32 == pSeed->iv32 )
    {
        return 1;
    }
#endif
    tsc0 = (u16_t) ((iv32 >> 16) & 0xffff); /* msb */
    tsc1 = (u16_t) (iv32 & 0xffff);

    /* Phase 1, step 1 */
    pSeed->ttak[0] = tsc1;
    pSeed->ttak[1] = tsc0;
    pSeed->ttak[2] = (u16_t) (pSeed->ta[0] + (pSeed->ta[1] <<8));
    pSeed->ttak[3] = (u16_t) (pSeed->ta[2] + (pSeed->ta[3] <<8));
    pSeed->ttak[4] = (u16_t) (pSeed->ta[4] + (pSeed->ta[5] <<8));

    /* Phase 1, step 2 */
    for (i=0; i<8; i++)
    {
        j = 2*(i & 1);
        pSeed->ttak[0] =(pSeed->ttak[0] + zfTkipSbox(pSeed->ttak[4]
                         ^ ZM_BYTE_TO_WORD(pSeed->tk[1+j], pSeed->tk[j])))
                        & 0xffff;
        pSeed->ttak[1] =(pSeed->ttak[1] + zfTkipSbox(pSeed->ttak[0]
                         ^ ZM_BYTE_TO_WORD(pSeed->tk[5+j], pSeed->tk[4+j] )))
                        & 0xffff;
        pSeed->ttak[2] =(pSeed->ttak[2] + zfTkipSbox(pSeed->ttak[1]
                         ^ ZM_BYTE_TO_WORD(pSeed->tk[9+j], pSeed->tk[8+j] )))
                        & 0xffff;
        pSeed->ttak[3] =(pSeed->ttak[3] + zfTkipSbox(pSeed->ttak[2]
                         ^ ZM_BYTE_TO_WORD(pSeed->tk[13+j], pSeed->tk[12+j])))
                        & 0xffff;
        pSeed->ttak[4] =(pSeed->ttak[4] + zfTkipSbox(pSeed->ttak[3]
                         ^ ZM_BYTE_TO_WORD(pSeed->tk[1+j], pSeed->tk[j]  )))
                        & 0xffff;
        pSeed->ttak[4] =(pSeed->ttak[4] + i) & 0xffff;
    }

    if ( iv32 == (pSeed->iv32+1) )
    {
        pSeed->iv32tmp = iv32;
        return 1;
    }

    return 0;
}

u8_t zfTkipPhase2KeyMix(u16_t iv16, struct zsTkipSeed* pSeed)
{
    u16_t tsc2;

    tsc2 = iv16;

    /* Phase 2, Step 1 */
    pSeed->ppk[0] = pSeed->ttak[0];
    pSeed->ppk[1] = pSeed->ttak[1];
    pSeed->ppk[2] = pSeed->ttak[2];
    pSeed->ppk[3] = pSeed->ttak[3];
    pSeed->ppk[4] = pSeed->ttak[4];
    pSeed->ppk[5] = (pSeed->ttak[4] + tsc2) & 0xffff;

    /* Phase2, Step 2 */
    pSeed->ppk[0] = pSeed->ppk[0]
                + zfTkipSbox(pSeed->ppk[5] ^ ZM_BYTE_TO_WORD(pSeed->tk[1],pSeed->tk[0]));
    pSeed->ppk[1] = pSeed->ppk[1]
                + zfTkipSbox(pSeed->ppk[0] ^ ZM_BYTE_TO_WORD(pSeed->tk[3],pSeed->tk[2]));
    pSeed->ppk[2] = pSeed->ppk[2]
                + zfTkipSbox(pSeed->ppk[1] ^ ZM_BYTE_TO_WORD(pSeed->tk[5],pSeed->tk[4]));
    pSeed->ppk[3] = pSeed->ppk[3]
                + zfTkipSbox(pSeed->ppk[2] ^ ZM_BYTE_TO_WORD(pSeed->tk[7],pSeed->tk[6]));
    pSeed->ppk[4] = pSeed->ppk[4]
                + zfTkipSbox(pSeed->ppk[3] ^ ZM_BYTE_TO_WORD(pSeed->tk[9],pSeed->tk[8]));
    pSeed->ppk[5] = pSeed->ppk[5]
                + zfTkipSbox(pSeed->ppk[4] ^ ZM_BYTE_TO_WORD(pSeed->tk[11],pSeed->tk[10]));

    pSeed->ppk[0] = pSeed->ppk[0]
                + zfrotr1(pSeed->ppk[5] ^ ZM_BYTE_TO_WORD(pSeed->tk[13],pSeed->tk[12]));
    pSeed->ppk[1] = pSeed->ppk[1]
                + zfrotr1(pSeed->ppk[0] ^ ZM_BYTE_TO_WORD(pSeed->tk[15],pSeed->tk[14]));
    pSeed->ppk[2] = pSeed->ppk[2] + zfrotr1(pSeed->ppk[1]);
    pSeed->ppk[3] = pSeed->ppk[3] + zfrotr1(pSeed->ppk[2]);
    pSeed->ppk[4] = pSeed->ppk[4] + zfrotr1(pSeed->ppk[3]);
    pSeed->ppk[5] = pSeed->ppk[5] + zfrotr1(pSeed->ppk[4]);

    if (iv16 == 0)
    {
        if (pSeed->iv16 == 0xffff)
        {
            pSeed->iv16tmp=0;
            return 1;
        }
        else
            return 0;
    }
    else if (iv16 == (pSeed->iv16+1))
    {
        pSeed->iv16tmp = iv16;
        return 1;
    }
    else
        return 0;
}

void zfTkipInit(u8_t* key, u8_t* ta, struct zsTkipSeed* pSeed, u8_t* initIv)
{
    u16_t  iv16;
    u32_t  iv32;
    u16_t  i;

    /* clear memory */
    zfZeroMemory((u8_t*) pSeed, sizeof(struct zsTkipSeed));
    /* set key to seed */
    zfMemoryCopy(pSeed->ta, ta, 6);
    zfMemoryCopy(pSeed->tk, key, 16);

    iv16 = *initIv++;
    iv16 += *initIv<<8;
    initIv++;

    iv32=0;

    for(i=0; i<4; i++)      // initiv is little endian
    {
        iv32 += *initIv<<(i*8);
        *initIv++;
    }

    pSeed->iv32 = iv32+1; // Force Recalculating on Tkip Phase1
    zfTkipPhase1KeyMix(iv32, pSeed);

    pSeed->iv16 = iv16;
    pSeed->iv32 = iv32;
}

u32_t zfGetU32t(u8_t* p)
{
    u32_t res=0;
    u16_t i;

    for( i=0; i<4; i++ )
    {
        res |= (*p++) << (8*i);
    }

    return res;

}

void zfPutU32t(u8_t* p, u32_t value)
{
    u16_t i;

    for(i=0; i<4; i++)
    {
        *p++ = (u8_t) (value & 0xff);
        value >>= 8;
    }
}

void zfMicClear(struct zsMicVar* pMic)
{
    pMic->left = pMic->k0;
    pMic->right = pMic->k1;
    pMic->nBytes = 0;
    pMic->m = 0;
}

void zfMicSetKey(u8_t* key, struct zsMicVar* pMic)
{
    pMic->k0 = zfGetU32t(key);
    pMic->k1 = zfGetU32t(key+4);
    zfMicClear(pMic);
}

void zfMicAppendByte(u8_t b, struct zsMicVar* pMic)
{
    // Append the byte to our word-sized buffer
    pMic->m |= b << (8* pMic->nBytes);
    pMic->nBytes++;

    // Process the word if it is full.
    if ( pMic->nBytes >= 4 )
    {
        pMic->left ^= pMic->m;
        pMic->right ^= ZM_ROL32(pMic->left, 17 );
        pMic->left += pMic->right;
        pMic->right ^= ((pMic->left & 0xff00ff00) >> 8) |
                       ((pMic->left & 0x00ff00ff) << 8);
        pMic->left += pMic->right;
        pMic->right ^= ZM_ROL32( pMic->left, 3 );
        pMic->left += pMic->right;
        pMic->right ^= ZM_ROR32( pMic->left, 2 );
        pMic->left += pMic->right;
        // Clear the buffer
        pMic->m = 0;
        pMic->nBytes = 0;
    }
}

void zfMicGetMic(u8_t* dst, struct zsMicVar* pMic)
{
    // Append the minimum padding
    zfMicAppendByte(0x5a, pMic);
    zfMicAppendByte(0, pMic);
    zfMicAppendByte(0, pMic);
    zfMicAppendByte(0, pMic);
    zfMicAppendByte(0, pMic);

    // and then zeroes until the length is a multiple of 4
    while( pMic->nBytes != 0 )
    {
        zfMicAppendByte(0, pMic);
    }

    // The appendByte function has already computed the result.
    zfPutU32t(dst, pMic->left);
    zfPutU32t(dst+4, pMic->right);

    // Reset to the empty message.
    zfMicClear(pMic);

}

u8_t zfMicRxVerify(zdev_t* dev, zbuf_t* buf)
{
    struct zsMicVar*  pMicKey;
    struct zsMicVar    MyMicKey;
    u8_t   mic[8];
    u8_t   da[6];
    u8_t   sa[6];
    u8_t   bValue;
    u16_t  i, payloadOffset, tailOffset;

    zmw_get_wlan_dev(dev);

    /* need not check MIC if pMicKEy is equal to NULL */
    if ( wd->wlanMode == ZM_MODE_AP )
    {
        pMicKey = zfApGetRxMicKey(dev, buf);

        if ( pMicKey != NULL )
        {
            zfCopyFromRxBuffer(dev, buf, sa, ZM_WLAN_HEADER_A2_OFFSET, 6);
            zfCopyFromRxBuffer(dev, buf, da, ZM_WLAN_HEADER_A3_OFFSET, 6);
        }
        else
        {
            return ZM_MIC_SUCCESS;
        }
    }
    else if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
    {
        pMicKey = zfStaGetRxMicKey(dev, buf);

        if ( pMicKey != NULL )
        {
            zfCopyFromRxBuffer(dev, buf, sa, ZM_WLAN_HEADER_A3_OFFSET, 6);
            zfCopyFromRxBuffer(dev, buf, da, ZM_WLAN_HEADER_A1_OFFSET, 6);
        }
        else
        {
            return ZM_MIC_SUCCESS;
        }
    }
    else
    {
        return ZM_MIC_SUCCESS;
    }

    MyMicKey.k0=pMicKey->k0;
    MyMicKey.k1=pMicKey->k1;
    pMicKey = &MyMicKey;

    zfMicClear(pMicKey);
    tailOffset = zfwBufGetSize(dev, buf);
    tailOffset -= 8;

    /* append DA */
    for(i=0; i<6; i++)
    {
        zfMicAppendByte(da[i], pMicKey);
    }
    /* append SA */
    for(i=0; i<6; i++)
    {
        zfMicAppendByte(sa[i], pMicKey);
    }

    /* append for alignment */
    if ((zmw_rx_buf_readb(dev, buf, 0) & 0x80) != 0)
        zfMicAppendByte(zmw_rx_buf_readb(dev, buf,24)&0x7, pMicKey);
    else
        zfMicAppendByte(0, pMicKey);
    zfMicAppendByte(0, pMicKey);
    zfMicAppendByte(0, pMicKey);
    zfMicAppendByte(0, pMicKey);

    /* append payload */
    payloadOffset = ZM_SIZE_OF_WLAN_DATA_HEADER +
                    ZM_SIZE_OF_IV +
                    ZM_SIZE_OF_EXT_IV;

    if ((zmw_rx_buf_readb(dev, buf, 0) & 0x80) != 0)
    {
        /* Qos Packet, Plcpheader + 2 */
        if (wd->wlanMode == ZM_MODE_AP)
        {
            /* TODO : Rx Qos element offset in software MIC check */
        }
        else if (wd->wlanMode == ZM_MODE_INFRASTRUCTURE)
        {
            if (wd->sta.wmeConnected != 0)
            {
                payloadOffset += 2;
            }
        }
    }

    for(i=payloadOffset; i<tailOffset; i++)
    {
        bValue = zmw_rx_buf_readb(dev, buf, i);
        zfMicAppendByte(bValue, pMicKey);
    }

    zfMicGetMic(mic, pMicKey);

    if ( !zfRxBufferEqualToStr(dev, buf, mic, tailOffset, 8) )
    {
        return ZM_MIC_FAILURE;
    }

    return ZM_MIC_SUCCESS;
}

void zfTkipGetseeds(u16_t iv16, u8_t *RC4Key, struct zsTkipSeed *Seed)
{
    RC4Key[0]  = ZM_HI8(iv16);
    RC4Key[1]  = (ZM_HI8(iv16) | 0x20) & 0x7f;
    RC4Key[2]  = ZM_LO8(iv16);
    RC4Key[3]  = ((Seed->ppk[5] ^ ZM_BYTE_TO_WORD(Seed->tk[1],Seed->tk[0]))>>1) & 0xff;
    RC4Key[4]  = Seed->ppk[0] & 0xff;
    RC4Key[5]  = Seed->ppk[0] >> 8;
    RC4Key[6]  = Seed->ppk[1] & 0xff;
    RC4Key[7]  = Seed->ppk[1] >> 8;
    RC4Key[8]  = Seed->ppk[2] & 0xff;
    RC4Key[9]  = Seed->ppk[2] >> 8;
    RC4Key[10] = Seed->ppk[3] & 0xff;
    RC4Key[11] = Seed->ppk[3] >> 8;
    RC4Key[12] = Seed->ppk[4] & 0xff;
    RC4Key[13] = Seed->ppk[4] >> 8;
    RC4Key[14] = Seed->ppk[5] & 0xff;
    RC4Key[15] = Seed->ppk[5] >> 8;
}

void zfCalTxMic(zdev_t *dev, zbuf_t *buf, u8_t *snap, u16_t snapLen, u16_t offset, u16_t *da, u16_t *sa, u8_t up, u8_t *mic)
{
    struct zsMicVar*  pMicKey;
    u16_t  i;
    u16_t len;
    u8_t bValue;
    u8_t qosType;
    u8_t *pDa = (u8_t *)da;
    u8_t *pSa = (u8_t *)sa;

    zmw_get_wlan_dev(dev);

    /* need not check MIC if pMicKEy is equal to NULL */
    if ( wd->wlanMode == ZM_MODE_AP )
    {
        pMicKey = zfApGetTxMicKey(dev, buf, &qosType);

        if ( pMicKey == NULL )
            return;
    }
    else if ( wd->wlanMode == ZM_MODE_INFRASTRUCTURE )
    {
        pMicKey = zfStaGetTxMicKey(dev, buf);

        if ( pMicKey == NULL )
        {
            zm_debug_msg0("pMicKey is NULL");
            return;
        }
    }
    else
    {
        return;
    }

    zfMicClear(pMicKey);
    len = zfwBufGetSize(dev, buf);

    /* append DA */
    for(i = 0; i < 6; i++)
    {
        zfMicAppendByte(pDa[i], pMicKey);
    }

    /* append SA */
    for(i = 0; i < 6; i++)
    {
        zfMicAppendByte(pSa[i], pMicKey);
    }

    if (up != 0)
        zfMicAppendByte((up&0x7), pMicKey);
    else
        zfMicAppendByte(0, pMicKey);

    zfMicAppendByte(0, pMicKey);
    zfMicAppendByte(0, pMicKey);
    zfMicAppendByte(0, pMicKey);

    /* For Snap header */
    for(i = 0; i < snapLen; i++)
    {
        zfMicAppendByte(snap[i], pMicKey);
    }

    for(i = offset; i < len; i++)
    {
        bValue = zmw_tx_buf_readb(dev, buf, i);
        zfMicAppendByte(bValue, pMicKey);
    }

    zfMicGetMic(mic, pMicKey);
}

void zfTKIPEncrypt(zdev_t *dev, zbuf_t *buf, u8_t *snap, u16_t snapLen, u16_t offset, u8_t keyLen, u8_t* key, u32_t* icv)
{
    u8_t iv[3];

    iv[0] = key[0];
    iv[1] = key[1];
    iv[2] = key[2];

    keyLen -= 3;

    zfWEPEncrypt(dev, buf, snap, snapLen, offset, keyLen, &key[3], iv);
}

u16_t zfTKIPDecrypt(zdev_t *dev, zbuf_t *buf, u16_t offset, u8_t keyLen, u8_t* key)
{
    u16_t ret = ZM_ICV_SUCCESS;
    u8_t iv[3];

    iv[0] = key[0];
    iv[1] = key[1];
    iv[2] = key[2];

    keyLen -= 3;

    ret = zfWEPDecrypt(dev, buf, offset, keyLen, &key[3], iv);

    return ret;
}
