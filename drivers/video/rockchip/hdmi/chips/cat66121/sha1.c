///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <sha1.c>
//   @author Hermes.Wu@ite.com.tw
//   @date   2011/08/26
//   @fileversion: COMMON_FILE_1.01
//******************************************/

#include "sha1.h"


#ifndef DISABLE_HDCP

#define WCOUNT 17
ULONG    VH[5];
ULONG    w[WCOUNT];

#define rol(x,y)(((x)<< (y))| (((ULONG)x)>> (32-y)))


void SHATransform(ULONG * h)
{
    int t;
    ULONG tmp;


    h[0]=0x67452301;
    h[1]=0xefcdab89;
    h[2]=0x98badcfe;
    h[3]=0x10325476;
    h[4]=0xc3d2e1f0;

    for (t=0; t < 20; t++){
		if(t>=16)
		{
        	tmp=w[(t - 3)% WCOUNT] ^ w[(t - 8)% WCOUNT] ^ w[(t - 14)% WCOUNT] ^ w[(t - 16)% WCOUNT];
	        w[(t)% WCOUNT]=rol(tmp,1);
		}
		HDCP_DEBUG_PRINTF2(("w[%d]=%08lX\n",t,w[(t)% WCOUNT]));

        tmp=rol(h[0],5)+ ((h[1] & h[2])| (h[3] & ~h[1]))+ h[4] + w[(t)% WCOUNT] + 0x5a827999;
        HDCP_DEBUG_PRINTF2(("%08lX %08lX %08lX %08lX %08lX\n",h[0],h[1],h[2],h[3],h[4]));

        h[4]=h[3];
        h[3]=h[2];
        h[2]=rol(h[1],30);
        h[1]=h[0];
        h[0]=tmp;

    }
    for (t=20; t < 40; t++){
        tmp=w[(t - 3)% WCOUNT] ^ w[(t - 8)% WCOUNT] ^ w[(t - 14)% WCOUNT] ^ w[(t - 16)% WCOUNT];
        w[(t)% WCOUNT]=rol(tmp,1);
        HDCP_DEBUG_PRINTF2(("w[%d]=%08lX\n",t,w[(t)% WCOUNT]));
        tmp=rol(h[0],5)+ (h[1] ^ h[2] ^ h[3])+ h[4] + w[(t)% WCOUNT] + 0x6ed9eba1;
        HDCP_DEBUG_PRINTF2(("%08lX %08lX %08lX %08lX %08lX\n",h[0],h[1],h[2],h[3],h[4]));
        h[4]=h[3];
        h[3]=h[2];
        h[2]=rol(h[1],30);
        h[1]=h[0];
        h[0]=tmp;
    }
    for (t=40; t < 60; t++){
        tmp=w[(t - 3)% WCOUNT] ^ w[(t - 8)% WCOUNT] ^ w[(t - 14)% WCOUNT] ^ w[(t - 16)% WCOUNT];
        w[(t)% WCOUNT]=rol(tmp,1);
        HDCP_DEBUG_PRINTF2(("w[%d]=%08lX\n",t,w[(t)% WCOUNT]));
        tmp=rol(h[0],5)+ ((h[1] & h[2])| (h[1] & h[3])| (h[2] & h[3]))+ h[4] + w[(t)% WCOUNT] + 0x8f1bbcdc;
        HDCP_DEBUG_PRINTF2(("%08lX %08lX %08lX %08lX %08lX\n",h[0],h[1],h[2],h[3],h[4]));
        h[4]=h[3];
        h[3]=h[2];
        h[2]=rol(h[1],30);
        h[1]=h[0];
        h[0]=tmp;
    }
    for (t=60; t < 80; t++)
    {
        tmp=w[(t - 3)% WCOUNT] ^ w[(t - 8)% WCOUNT] ^ w[(t - 14)% WCOUNT] ^ w[(t - 16)% WCOUNT];
        w[(t)% WCOUNT]=rol(tmp,1);
        HDCP_DEBUG_PRINTF2(("w[%d]=%08lX\n",t,w[(t)% WCOUNT]));
        tmp=rol(h[0],5)+ (h[1] ^ h[2] ^ h[3])+ h[4] + w[(t)% WCOUNT] + 0xca62c1d6;
        HDCP_DEBUG_PRINTF2(("%08lX %08lX %08lX %08lX %08lX\n",h[0],h[1],h[2],h[3],h[4]));
        h[4]=h[3];
        h[3]=h[2];
        h[2]=rol(h[1],30);
        h[1]=h[0];
        h[0]=tmp;
    }
    HDCP_DEBUG_PRINTF2(("%08lX %08lX %08lX %08lX %08lX\n",h[0],h[1],h[2],h[3],h[4]));
    h[0] +=0x67452301;
    h[1] +=0xefcdab89;
    h[2] +=0x98badcfe;
    h[3] +=0x10325476;
    h[4] +=0xc3d2e1f0;

    HDCP_DEBUG_PRINTF2(("%08lX %08lX %08lX %08lX %08lX\n",h[0],h[1],h[2],h[3],h[4]));
}

void SHA_Simple(void *p,WORD len,BYTE *output)
{
    // SHA_State s;
    WORD i,t;
    ULONG c;
    BYTE *pBuff=p;

    for(i=0;i < len;i++)
    {
        t=i/4;
        if(i%4==0)
        {
            w[t]=0;
        }
        c=pBuff[i];
        c <<=(3-(i%4))*8;
        w[t] |=c;
        HDCP_DEBUG_PRINTF2(("pBuff[%d]=%02X,c=%08lX,w[%d]=%08lX\n",(int)i,(int)pBuff[i],c,(int)t,w[t]));
    }
    t=i/4;
    if(i%4==0)
    {
        w[t]=0;
    }
    //c=0x80 << ((3-i%4)*24);
    c=0x80;
    c <<=((3-i%4)*8);
    w[t]|=c;t++;
    for(; t < 15;t++)
    {
        w[t]=0;
    }
    w[15]=len*8;

	for(i = 0 ; i < 16 ; i++)
	{
		HDCP_DEBUG_PRINTF2(("w[%d] = %08lX\n",i,w[i]));
	}

    SHATransform(VH);

    for(i=0;i < 5;i++)
    {
        output[i*4+3]=(BYTE)((VH[i]>>24)&0xFF);
        output[i*4+2]=(BYTE)((VH[i]>>16)&0xFF);
        output[i*4+1]=(BYTE)((VH[i]>>8)&0xFF);
        output[i*4+0]=(BYTE)(VH[i]&0xFF);
    }
}
#endif
