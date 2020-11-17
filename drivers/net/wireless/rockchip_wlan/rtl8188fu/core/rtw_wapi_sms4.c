/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifdef CONFIG_WAPI_SUPPORT

#include <linux/unistd.h>
#include <linux/etherdevice.h>
#include <drv_types.h>
#include <rtw_wapi.h>


#ifdef CONFIG_WAPI_SW_SMS4

#define WAPI_LITTLE_ENDIAN
/* #define BIG_ENDIAN */
#define ENCRYPT  0
#define DECRYPT  1


/**********************************************************
 **********************************************************/
const u8 Sbox[256] = {
	0xd6, 0x90, 0xe9, 0xfe, 0xcc, 0xe1, 0x3d, 0xb7, 0x16, 0xb6, 0x14, 0xc2, 0x28, 0xfb, 0x2c, 0x05,
	0x2b, 0x67, 0x9a, 0x76, 0x2a, 0xbe, 0x04, 0xc3, 0xaa, 0x44, 0x13, 0x26, 0x49, 0x86, 0x06, 0x99,
	0x9c, 0x42, 0x50, 0xf4, 0x91, 0xef, 0x98, 0x7a, 0x33, 0x54, 0x0b, 0x43, 0xed, 0xcf, 0xac, 0x62,
	0xe4, 0xb3, 0x1c, 0xa9, 0xc9, 0x08, 0xe8, 0x95, 0x80, 0xdf, 0x94, 0xfa, 0x75, 0x8f, 0x3f, 0xa6,
	0x47, 0x07, 0xa7, 0xfc, 0xf3, 0x73, 0x17, 0xba, 0x83, 0x59, 0x3c, 0x19, 0xe6, 0x85, 0x4f, 0xa8,
	0x68, 0x6b, 0x81, 0xb2, 0x71, 0x64, 0xda, 0x8b, 0xf8, 0xeb, 0x0f, 0x4b, 0x70, 0x56, 0x9d, 0x35,
	0x1e, 0x24, 0x0e, 0x5e, 0x63, 0x58, 0xd1, 0xa2, 0x25, 0x22, 0x7c, 0x3b, 0x01, 0x21, 0x78, 0x87,
	0xd4, 0x00, 0x46, 0x57, 0x9f, 0xd3, 0x27, 0x52, 0x4c, 0x36, 0x02, 0xe7, 0xa0, 0xc4, 0xc8, 0x9e,
	0xea, 0xbf, 0x8a, 0xd2, 0x40, 0xc7, 0x38, 0xb5, 0xa3, 0xf7, 0xf2, 0xce, 0xf9, 0x61, 0x15, 0xa1,
	0xe0, 0xae, 0x5d, 0xa4, 0x9b, 0x34, 0x1a, 0x55, 0xad, 0x93, 0x32, 0x30, 0xf5, 0x8c, 0xb1, 0xe3,
	0x1d, 0xf6, 0xe2, 0x2e, 0x82, 0x66, 0xca, 0x60, 0xc0, 0x29, 0x23, 0xab, 0x0d, 0x53, 0x4e, 0x6f,
	0xd5, 0xdb, 0x37, 0x45, 0xde, 0xfd, 0x8e, 0x2f, 0x03, 0xff, 0x6a, 0x72, 0x6d, 0x6c, 0x5b, 0x51,
	0x8d, 0x1b, 0xaf, 0x92, 0xbb, 0xdd, 0xbc, 0x7f, 0x11, 0xd9, 0x5c, 0x41, 0x1f, 0x10, 0x5a, 0xd8,
	0x0a, 0xc1, 0x31, 0x88, 0xa5, 0xcd, 0x7b, 0xbd, 0x2d, 0x74, 0xd0, 0x12, 0xb8, 0xe5, 0xb4, 0xb0,
	0x89, 0x69, 0x97, 0x4a, 0x0c, 0x96, 0x77, 0x7e, 0x65, 0xb9, 0xf1, 0x09, 0xc5, 0x6e, 0xc6, 0x84,
	0x18, 0xf0, 0x7d, 0xec, 0x3a, 0xdc, 0x4d, 0x20, 0x79, 0xee, 0x5f, 0x3e, 0xd7, 0xcb, 0x39, 0x48
};

const u32 CK[32] = {
	0x00070e15, 0x1c232a31, 0x383f464d, 0x545b6269,
	0x70777e85, 0x8c939aa1, 0xa8afb6bd, 0xc4cbd2d9,
	0xe0e7eef5, 0xfc030a11, 0x181f262d, 0x343b4249,
	0x50575e65, 0x6c737a81, 0x888f969d, 0xa4abb2b9,
	0xc0c7ced5, 0xdce3eaf1, 0xf8ff060d, 0x141b2229,
	0x30373e45, 0x4c535a61, 0x686f767d, 0x848b9299,
	0xa0a7aeb5, 0xbcc3cad1, 0xd8dfe6ed, 0xf4fb0209,
	0x10171e25, 0x2c333a41, 0x484f565d, 0x646b7279
};

#define Rotl(_x, _y) (((_x) << (_y)) | ((_x) >> (32 - (_y))))

#define ByteSub(_A) (Sbox[(_A) >> 24 & 0xFF] << 24 | \
		     Sbox[(_A) >> 16 & 0xFF] << 16 | \
		     Sbox[(_A) >>  8 & 0xFF] <<  8 | \
		     Sbox[(_A) & 0xFF])

#define L1(_B) ((_B) ^ Rotl(_B, 2) ^ Rotl(_B, 10) ^ Rotl(_B, 18) ^ Rotl(_B, 24))
#define L2(_B) ((_B) ^ Rotl(_B, 13) ^ Rotl(_B, 23))

static void
xor_block(void *dst, void *src1, void *src2)
/* 128-bit xor: *dst = *src1 xor *src2. Pointers must be 32-bit aligned */
{
	((u32 *)dst)[0] = ((u32 *)src1)[0] ^ ((u32 *)src2)[0];
	((u32 *)dst)[1] = ((u32 *)src1)[1] ^ ((u32 *)src2)[1];
	((u32 *)dst)[2] = ((u32 *)src1)[2] ^ ((u32 *)src2)[2];
	((u32 *)dst)[3] = ((u32 *)src1)[3] ^ ((u32 *)src2)[3];
}


void SMS4Crypt(u8 *Input, u8 *Output, u32 *rk)
{
	u32 r, mid, x0, x1, x2, x3, *p;
	p = (u32 *)Input;
	x0 = p[0];
	x1 = p[1];
	x2 = p[2];
	x3 = p[3];
#ifdef WAPI_LITTLE_ENDIAN
	x0 = Rotl(x0, 16);
	x0 = ((x0 & 0x00FF00FF) << 8) | ((x0 & 0xFF00FF00) >> 8);
	x1 = Rotl(x1, 16);
	x1 = ((x1 & 0x00FF00FF) << 8) | ((x1 & 0xFF00FF00) >> 8);
	x2 = Rotl(x2, 16);
	x2 = ((x2 & 0x00FF00FF) << 8) | ((x2 & 0xFF00FF00) >> 8);
	x3 = Rotl(x3, 16);
	x3 = ((x3 & 0x00FF00FF) << 8) | ((x3 & 0xFF00FF00) >> 8);
#endif
	for (r = 0; r < 32; r += 4) {
		mid = x1 ^ x2 ^ x3 ^ rk[r + 0];
		mid = ByteSub(mid);
		x0 ^= L1(mid);
		mid = x2 ^ x3 ^ x0 ^ rk[r + 1];
		mid = ByteSub(mid);
		x1 ^= L1(mid);
		mid = x3 ^ x0 ^ x1 ^ rk[r + 2];
		mid = ByteSub(mid);
		x2 ^= L1(mid);
		mid = x0 ^ x1 ^ x2 ^ rk[r + 3];
		mid = ByteSub(mid);
		x3 ^= L1(mid);
	}
#ifdef WAPI_LITTLE_ENDIAN
	x0 = Rotl(x0, 16);
	x0 = ((x0 & 0x00FF00FF) << 8) | ((x0 & 0xFF00FF00) >> 8);
	x1 = Rotl(x1, 16);
	x1 = ((x1 & 0x00FF00FF) << 8) | ((x1 & 0xFF00FF00) >> 8);
	x2 = Rotl(x2, 16);
	x2 = ((x2 & 0x00FF00FF) << 8) | ((x2 & 0xFF00FF00) >> 8);
	x3 = Rotl(x3, 16);
	x3 = ((x3 & 0x00FF00FF) << 8) | ((x3 & 0xFF00FF00) >> 8);
#endif
	p = (u32 *)Output;
	p[0] = x3;
	p[1] = x2;
	p[2] = x1;
	p[3] = x0;
}



void SMS4KeyExt(u8 *Key, u32 *rk, u32 CryptFlag)
{
	u32 r, mid, x0, x1, x2, x3, *p;

	p = (u32 *)Key;
	x0 = p[0];
	x1 = p[1];
	x2 = p[2];
	x3 = p[3];
#ifdef WAPI_LITTLE_ENDIAN
	x0 = Rotl(x0, 16);
	x0 = ((x0 & 0xFF00FF) << 8) | ((x0 & 0xFF00FF00) >> 8);
	x1 = Rotl(x1, 16);
	x1 = ((x1 & 0xFF00FF) << 8) | ((x1 & 0xFF00FF00) >> 8);
	x2 = Rotl(x2, 16);
	x2 = ((x2 & 0xFF00FF) << 8) | ((x2 & 0xFF00FF00) >> 8);
	x3 = Rotl(x3, 16);
	x3 = ((x3 & 0xFF00FF) << 8) | ((x3 & 0xFF00FF00) >> 8);
#endif

	x0 ^= 0xa3b1bac6;
	x1 ^= 0x56aa3350;
	x2 ^= 0x677d9197;
	x3 ^= 0xb27022dc;
	for (r = 0; r < 32; r += 4) {
		mid = x1 ^ x2 ^ x3 ^ CK[r + 0];
		mid = ByteSub(mid);
		rk[r + 0] = x0 ^= L2(mid);
		mid = x2 ^ x3 ^ x0 ^ CK[r + 1];
		mid = ByteSub(mid);
		rk[r + 1] = x1 ^= L2(mid);
		mid = x3 ^ x0 ^ x1 ^ CK[r + 2];
		mid = ByteSub(mid);
		rk[r + 2] = x2 ^= L2(mid);
		mid = x0 ^ x1 ^ x2 ^ CK[r + 3];
		mid = ByteSub(mid);
		rk[r + 3] = x3 ^= L2(mid);
	}
	if (CryptFlag == DECRYPT) {
		for (r = 0; r < 16; r++)
			mid = rk[r], rk[r] = rk[31 - r], rk[31 - r] = mid;
	}
}


void WapiSMS4Cryption(u8 *Key, u8 *IV, u8 *Input, u16 InputLength,
		      u8 *Output, u16 *OutputLength, u32 CryptFlag)
{
	u32 blockNum, i, j, rk[32];
	u16 remainder;
	u8 blockIn[16], blockOut[16], tempIV[16], k;

	*OutputLength = 0;
	remainder = InputLength & 0x0F;
	blockNum = InputLength >> 4;
	if (remainder != 0)
		blockNum++;
	else
		remainder = 16;

	for (k = 0; k < 16; k++)
		tempIV[k] = IV[15 - k];

	memcpy(blockIn, tempIV, 16);

	SMS4KeyExt((u8 *)Key, rk, CryptFlag);

	for (i = 0; i < blockNum - 1; i++) {
		SMS4Crypt((u8 *)blockIn, blockOut, rk);
		xor_block(&Output[i * 16], &Input[i * 16], blockOut);
		memcpy(blockIn, blockOut, 16);
	}

	*OutputLength = i * 16;

	SMS4Crypt((u8 *)blockIn, blockOut, rk);

	for (j = 0; j < remainder; j++)
		Output[i * 16 + j] = Input[i * 16 + j] ^ blockOut[j];
	*OutputLength += remainder;

}

void WapiSMS4Encryption(u8 *Key, u8 *IV, u8 *Input, u16 InputLength,
			u8 *Output, u16 *OutputLength)
{

	WapiSMS4Cryption(Key, IV, Input, InputLength, Output, OutputLength, ENCRYPT);
}

void WapiSMS4Decryption(u8 *Key, u8 *IV, u8 *Input, u16 InputLength,
			u8 *Output, u16 *OutputLength)
{
	/* OFB mode: is also ENCRYPT flag */
	WapiSMS4Cryption(Key, IV, Input, InputLength, Output, OutputLength, ENCRYPT);
}

void WapiSMS4CalculateMic(u8 *Key, u8 *IV, u8 *Input1, u8 Input1Length,
		  u8 *Input2, u16 Input2Length, u8 *Output, u8 *OutputLength)
{
	u32 blockNum, i, remainder, rk[32];
	u8 BlockIn[16], BlockOut[16], TempBlock[16], tempIV[16], k;

	*OutputLength = 0;
	remainder = Input1Length & 0x0F;
	blockNum = Input1Length >> 4;

	for (k = 0; k < 16; k++)
		tempIV[k] = IV[15 - k];

	memcpy(BlockIn, tempIV, 16);

	SMS4KeyExt((u8 *)Key, rk, ENCRYPT);

	SMS4Crypt((u8 *)BlockIn, BlockOut, rk);

	for (i = 0; i < blockNum; i++) {
		xor_block(BlockIn, (Input1 + i * 16), BlockOut);
		SMS4Crypt((u8 *)BlockIn, BlockOut, rk);
	}

	if (remainder != 0) {
		memset(TempBlock, 0, 16);
		memcpy(TempBlock, (Input1 + blockNum * 16), remainder);

		xor_block(BlockIn, TempBlock, BlockOut);
		SMS4Crypt((u8 *)BlockIn, BlockOut, rk);
	}

	remainder = Input2Length & 0x0F;
	blockNum = Input2Length >> 4;

	for (i = 0; i < blockNum; i++) {
		xor_block(BlockIn, (Input2 + i * 16), BlockOut);
		SMS4Crypt((u8 *)BlockIn, BlockOut, rk);
	}

	if (remainder != 0) {
		memset(TempBlock, 0, 16);
		memcpy(TempBlock, (Input2 + blockNum * 16), remainder);

		xor_block(BlockIn, TempBlock, BlockOut);
		SMS4Crypt((u8 *)BlockIn, BlockOut, rk);
	}

	memcpy(Output, BlockOut, 16);
	*OutputLength = 16;
}

void SecCalculateMicSMS4(
	u8		KeyIdx,
	u8        *MicKey,
	u8        *pHeader,
	u8        *pData,
	u16       DataLen,
	u8        *MicBuffer
)
{
#if 0
	struct ieee80211_hdr_3addr_qos *header;
	u8 TempBuf[34], TempLen = 32, MicLen, QosOffset, *IV;
	u16 *pTemp, fc;

	WAPI_TRACE(WAPI_TX | WAPI_RX, "=========>%s\n", __FUNCTION__);

	header = (struct ieee80211_hdr_3addr_qos *)pHeader;
	memset(TempBuf, 0, 34);
	memcpy(TempBuf, pHeader, 2); /* FrameCtrl */
	pTemp = (u16 *)TempBuf;
	*pTemp &= 0xc78f;       /* bit4,5,6,11,12,13 */

	memcpy((TempBuf + 2), (pHeader + 4), 12); /* Addr1, Addr2 */
	memcpy((TempBuf + 14), (pHeader + 22), 2); /* SeqCtrl */
	pTemp = (u16 *)(TempBuf + 14);
	*pTemp &= 0x000f;

	memcpy((TempBuf + 16), (pHeader + 16), 6); /* Addr3 */

	fc = le16_to_cpu(header->frame_ctl);



	if (GetFrDs((u16 *)&fc) && GetToDs((u16 *)&fc)) {
		memcpy((TempBuf + 22), (pHeader + 24), 6);
		QosOffset = 30;
	} else {
		memset((TempBuf + 22), 0, 6);
		QosOffset = 24;
	}

	if ((fc & 0x0088) == 0x0088) {
		memcpy((TempBuf + 28), (pHeader + QosOffset), 2);
		TempLen += 2;
		/* IV = pHeader + QosOffset + 2 + SNAP_SIZE + sizeof(u16) + 2; */
		IV = pHeader + QosOffset + 2 + 2;
	} else {
		IV = pHeader + QosOffset + 2;
		/* IV = pHeader + QosOffset + SNAP_SIZE + sizeof(u16) + 2; */
	}

	TempBuf[TempLen - 1] = (u8)(DataLen & 0xff);
	TempBuf[TempLen - 2] = (u8)((DataLen & 0xff00) >> 8);
	TempBuf[TempLen - 4] = KeyIdx;

	WAPI_DATA(WAPI_TX, "CalculateMic - KEY", MicKey, 16);
	WAPI_DATA(WAPI_TX, "CalculateMic - IV", IV, 16);
	WAPI_DATA(WAPI_TX, "CalculateMic - TempBuf", TempBuf, TempLen);
	WAPI_DATA(WAPI_TX, "CalculateMic - pData", pData, DataLen);

	WapiSMS4CalculateMic(MicKey, IV, TempBuf, TempLen,
			     pData, DataLen, MicBuffer, &MicLen);

	if (MicLen != 16)
		WAPI_TRACE(WAPI_ERR, "%s: MIC Length Error!!\n", __FUNCTION__);

	WAPI_TRACE(WAPI_TX | WAPI_RX, "<=========%s\n", __FUNCTION__);
#endif
}

/* AddCount: 1 or 2.
 *  If overflow, return 1,
 *  else return 0.
 */
u8 WapiIncreasePN(u8 *PN, u8 AddCount)
{
	u8  i;

	if (NULL == PN)
		return 1;
	/* YJ,test,091102 */
	/*
	if(AddCount == 2){
		RTW_INFO("############################%s(): PN[0]=0x%x\n", __FUNCTION__, PN[0]);
		if(PN[0] == 0x48){
			PN[0] += AddCount;
			return 1;
		}else{
			PN[0] += AddCount;
			return 0;
		}
	}
	*/
	/* YJ,test,091102,end */

	for (i = 0; i < 16; i++) {
		if (PN[i] + AddCount <= 0xff) {
			PN[i] += AddCount;
			return 0;
		} else {
			PN[i] += AddCount;
			AddCount = 1;
		}
	}
	return 1;
}


void WapiGetLastRxUnicastPNForQoSData(
	u8			UserPriority,
	PRT_WAPI_STA_INFO    pWapiStaInfo,
	u8 *PNOut
)
{
	WAPI_TRACE(WAPI_RX, "===========> %s\n", __FUNCTION__);
	switch (UserPriority) {
	case 0:
	case 3:
		memcpy(PNOut, pWapiStaInfo->lastRxUnicastPNBEQueue, 16);
		break;
	case 1:
	case 2:
		memcpy(PNOut, pWapiStaInfo->lastRxUnicastPNBKQueue, 16);
		break;
	case 4:
	case 5:
		memcpy(PNOut, pWapiStaInfo->lastRxUnicastPNVIQueue, 16);
		break;
	case 6:
	case 7:
		memcpy(PNOut, pWapiStaInfo->lastRxUnicastPNVOQueue, 16);
		break;
	default:
		WAPI_TRACE(WAPI_ERR, "%s: Unknown TID\n", __FUNCTION__);
		break;
	}
	WAPI_TRACE(WAPI_RX, "<=========== %s\n", __FUNCTION__);
}


void WapiSetLastRxUnicastPNForQoSData(
	u8		UserPriority,
	u8           *PNIn,
	PRT_WAPI_STA_INFO    pWapiStaInfo
)
{
	WAPI_TRACE(WAPI_RX, "===========> %s\n", __FUNCTION__);
	switch (UserPriority) {
	case 0:
	case 3:
		memcpy(pWapiStaInfo->lastRxUnicastPNBEQueue, PNIn, 16);
		break;
	case 1:
	case 2:
		memcpy(pWapiStaInfo->lastRxUnicastPNBKQueue, PNIn, 16);
		break;
	case 4:
	case 5:
		memcpy(pWapiStaInfo->lastRxUnicastPNVIQueue, PNIn, 16);
		break;
	case 6:
	case 7:
		memcpy(pWapiStaInfo->lastRxUnicastPNVOQueue, PNIn, 16);
		break;
	default:
		WAPI_TRACE(WAPI_ERR, "%s: Unknown TID\n", __FUNCTION__);
		break;
	}
	WAPI_TRACE(WAPI_RX, "<=========== %s\n", __FUNCTION__);
}


/****************************************************************************
 FALSE not RX-Reorder
 TRUE do RX Reorder
add to support WAPI to N-mode
*****************************************************************************/
u8 WapiCheckPnInSwDecrypt(
	_adapter *padapter,
	struct sk_buff *pskb
)
{
	u8				ret = false;

#if 0
	struct ieee80211_hdr_3addr_qos *header;
	u16				fc;
	u8				*pDaddr, *pTaddr, *pRaddr;

	header = (struct ieee80211_hdr_3addr_qos *)pskb->data;
	pTaddr = header->addr2;
	pRaddr = header->addr1;
	fc = le16_to_cpu(header->frame_ctl);

	if (GetToDs(&fc))
		pDaddr = header->addr3;
	else
		pDaddr = header->addr1;

	if ((_rtw_memcmp(pRaddr, padapter->pnetdev->dev_addr, ETH_ALEN) == 0)
	    &&	!(pDaddr)
	    && (GetFrameType(&fc) == WIFI_QOS_DATA_TYPE))
		/* && ieee->pHTInfo->bCurrentHTSupport && */
		/* ieee->pHTInfo->bCurRxReorderEnable) */
		ret = false;
	else
		ret = true;
#endif
	WAPI_TRACE(WAPI_RX, "%s: return %d\n", __FUNCTION__, ret);
	return ret;
}

int SecSMS4HeaderFillIV(_adapter *padapter, u8 *pxmitframe)
{
	struct pkt_attrib *pattrib = &((struct xmit_frame *)pxmitframe)->attrib;
	u8 *frame = ((struct xmit_frame *)pxmitframe)->buf_addr + TXDESC_OFFSET;
	u8 *pSecHeader = NULL, *pos = NULL, *pRA = NULL;
	u8 bPNOverflow = false, bFindMatchPeer = false, hdr_len = 0;
	PWLAN_HEADER_WAPI_EXTENSION pWapiExt = NULL;
	PRT_WAPI_T         pWapiInfo = &padapter->wapiInfo;
	PRT_WAPI_STA_INFO  pWapiSta = NULL;
	int ret = 0;

	WAPI_TRACE(WAPI_TX, "=========>%s\n", __FUNCTION__);

	return ret;
#if 0
	hdr_len = sMacHdrLng;
	if (GetFrameType(pskb->data) == WIFI_QOS_DATA_TYPE)
		hdr_len += 2;
	/* hdr_len += SNAP_SIZE + sizeof(u16); */

	pos = skb_push(pskb, padapter->wapiInfo.extra_prefix_len);
	memmove(pos, pos + padapter->wapiInfo.extra_prefix_len, hdr_len);

	pSecHeader = pskb->data + hdr_len;
	pWapiExt = (PWLAN_HEADER_WAPI_EXTENSION)pSecHeader;
	pRA = pskb->data + 4;

	WAPI_DATA(WAPI_TX, "FillIV - Before Fill IV", pskb->data, pskb->len);

	/* Address 1 is always receiver's address */
	if (IS_MCAST(pRA)) {
		if (!pWapiInfo->wapiTxMsk.bTxEnable) {
			WAPI_TRACE(WAPI_ERR, "%s: bTxEnable = 0!!\n", __FUNCTION__);
			return -2;
		}
		if (pWapiInfo->wapiTxMsk.keyId <= 1) {
			pWapiExt->KeyIdx = pWapiInfo->wapiTxMsk.keyId;
			pWapiExt->Reserved = 0;
			bPNOverflow = WapiIncreasePN(pWapiInfo->lastTxMulticastPN, 1);
			memcpy(pWapiExt->PN, pWapiInfo->lastTxMulticastPN, 16);
			if (bPNOverflow) {
				/* Update MSK Notification. */
				WAPI_TRACE(WAPI_ERR, "===============>%s():multicast PN overflow\n", __FUNCTION__);
				rtw_wapi_app_event_handler(padapter, NULL, 0, pRA, false, false, true, 0, false);
			}
		} else {
			WAPI_TRACE(WAPI_ERR, "%s: Invalid Wapi Multicast KeyIdx!!\n", __FUNCTION__);
			ret = -3;
		}
	} else {
		list_for_each_entry(pWapiSta, &pWapiInfo->wapiSTAUsedList, list) {
			if (!memcmp(pWapiSta->PeerMacAddr, pRA, 6)) {
				bFindMatchPeer = true;
				break;
			}
		}
		if (bFindMatchPeer) {
			if ((!pWapiSta->wapiUskUpdate.bTxEnable) && (!pWapiSta->wapiUsk.bTxEnable)) {
				WAPI_TRACE(WAPI_ERR, "%s: bTxEnable = 0!!\n", __FUNCTION__);
				return -4;
			}
			if (pWapiSta->wapiUsk.keyId <= 1) {
				if (pWapiSta->wapiUskUpdate.bTxEnable)
					pWapiExt->KeyIdx = pWapiSta->wapiUskUpdate.keyId;
				else
					pWapiExt->KeyIdx = pWapiSta->wapiUsk.keyId;

				pWapiExt->Reserved = 0;
				bPNOverflow = WapiIncreasePN(pWapiSta->lastTxUnicastPN, 2);
				memcpy(pWapiExt->PN, pWapiSta->lastTxUnicastPN, 16);
				if (bPNOverflow) {
					/* Update USK Notification. */
					WAPI_TRACE(WAPI_ERR, "===============>%s():unicast PN overflow\n", __FUNCTION__);
					rtw_wapi_app_event_handler(padapter, NULL, 0, pWapiSta->PeerMacAddr, false, true, false, 0, false);
				}
			} else {
				WAPI_TRACE(WAPI_ERR, "%s: Invalid Wapi Unicast KeyIdx!!\n", __FUNCTION__);
				ret = -5;
			}
		} else {
			WAPI_TRACE(WAPI_ERR, "%s: Can not find Peer Sta "MAC_FMT"!!\n", __FUNCTION__, MAC_ARG(pRA));
			ret = -6;
		}
	}

	WAPI_DATA(WAPI_TX, "FillIV - After Fill IV", pskb->data, pskb->len);
	WAPI_TRACE(WAPI_TX, "<=========%s\n", __FUNCTION__);
	return ret;
#endif
}

/* WAPI SW Enc: must have done Coalesce! */
void SecSWSMS4Encryption(
	_adapter *padapter,
	u8 *pxmitframe
)
{
	PRT_WAPI_T		pWapiInfo = &padapter->wapiInfo;
	PRT_WAPI_STA_INFO   pWapiSta = NULL;
	u8 *pframe = ((struct xmit_frame *)pxmitframe)->buf_addr + TXDESC_SIZE;
	struct pkt_attrib *pattrib = &((struct xmit_frame *)pxmitframe)->attrib;

	u8 *SecPtr = NULL, *pRA, *pMicKey = NULL, *pDataKey = NULL, *pIV = NULL;
	u8 IVOffset, DataOffset, bFindMatchPeer = false, KeyIdx = 0, MicBuffer[16];
	u16 OutputLength;

	WAPI_TRACE(WAPI_TX, "=========>%s\n", __FUNCTION__);

	WAPI_TRACE(WAPI_TX, "hdrlen: %d\n", pattrib->hdrlen);

	return;

	DataOffset = pattrib->hdrlen + pattrib->iv_len;

	pRA = pframe + 4;


	if (IS_MCAST(pRA)) {
		KeyIdx = pWapiInfo->wapiTxMsk.keyId;
		pIV = pWapiInfo->lastTxMulticastPN;
		pMicKey = pWapiInfo->wapiTxMsk.micKey;
		pDataKey = pWapiInfo->wapiTxMsk.dataKey;
	} else {
		if (!list_empty(&(pWapiInfo->wapiSTAUsedList))) {
			list_for_each_entry(pWapiSta, &pWapiInfo->wapiSTAUsedList, list) {
				if (0 == memcmp(pWapiSta->PeerMacAddr, pRA, 6)) {
					bFindMatchPeer = true;
					break;
				}
			}

			if (bFindMatchPeer) {
				if (pWapiSta->wapiUskUpdate.bTxEnable) {
					KeyIdx = pWapiSta->wapiUskUpdate.keyId;
					WAPI_TRACE(WAPI_TX, "%s(): Use update USK!! KeyIdx=%d\n", __FUNCTION__, KeyIdx);
					pIV = pWapiSta->lastTxUnicastPN;
					pMicKey = pWapiSta->wapiUskUpdate.micKey;
					pDataKey = pWapiSta->wapiUskUpdate.dataKey;
				} else {
					KeyIdx = pWapiSta->wapiUsk.keyId;
					WAPI_TRACE(WAPI_TX, "%s(): Use USK!! KeyIdx=%d\n", __FUNCTION__, KeyIdx);
					pIV = pWapiSta->lastTxUnicastPN;
					pMicKey = pWapiSta->wapiUsk.micKey;
					pDataKey = pWapiSta->wapiUsk.dataKey;
				}
			} else {
				WAPI_TRACE(WAPI_ERR, "%s: Can not find Peer Sta!!\n", __FUNCTION__);
				return;
			}
		} else {
			WAPI_TRACE(WAPI_ERR, "%s: wapiSTAUsedList is empty!!\n", __FUNCTION__);
			return;
		}
	}

	SecPtr = pframe;
	SecCalculateMicSMS4(KeyIdx, pMicKey, SecPtr, (SecPtr + DataOffset), pattrib->pktlen, MicBuffer);

	WAPI_DATA(WAPI_TX, "Encryption - MIC", MicBuffer, padapter->wapiInfo.extra_postfix_len);

	memcpy(pframe + pattrib->hdrlen + pattrib->iv_len + pattrib->pktlen - pattrib->icv_len,
	       (u8 *)MicBuffer,
	       padapter->wapiInfo.extra_postfix_len
	      );


	WapiSMS4Encryption(pDataKey, pIV, (SecPtr + DataOffset), pattrib->pktlen + pattrib->icv_len, (SecPtr + DataOffset), &OutputLength);

	WAPI_DATA(WAPI_TX, "Encryption - After SMS4 encryption", pframe, pattrib->hdrlen + pattrib->iv_len + pattrib->pktlen);

	WAPI_TRACE(WAPI_TX, "<=========%s\n", __FUNCTION__);
}

u8 SecSWSMS4Decryption(
	_adapter *padapter,
	u8		*precv_frame,
	struct recv_priv *precv_priv
)
{
	PRT_WAPI_T pWapiInfo = &padapter->wapiInfo;
	struct recv_frame_hdr *precv_hdr;
	PRT_WAPI_STA_INFO   pWapiSta = NULL;
	u8 IVOffset, DataOffset, bFindMatchPeer = false, bUseUpdatedKey = false;
	u8 KeyIdx, MicBuffer[16], lastRxPNforQoS[16];
	u8 *pRA, *pTA, *pMicKey, *pDataKey, *pLastRxPN, *pRecvPN, *pSecData, *pRecvMic, *pos;
	u8 TID = 0;
	u16 OutputLength, DataLen;
	u8   bQosData;
	struct sk_buff	*pskb;

	WAPI_TRACE(WAPI_RX, "=========>%s\n", __FUNCTION__);

	return 0;

	precv_hdr = &((union recv_frame *)precv_frame)->u.hdr;
	pskb = (struct sk_buff *)(precv_hdr->rx_data);
	precv_hdr->bWapiCheckPNInDecrypt = WapiCheckPnInSwDecrypt(padapter, pskb);
	WAPI_TRACE(WAPI_RX, "=========>%s: check PN  %d\n", __FUNCTION__, precv_hdr->bWapiCheckPNInDecrypt);
	WAPI_DATA(WAPI_RX, "Decryption - Before decryption", pskb->data, pskb->len);

	IVOffset = sMacHdrLng;
	bQosData = GetFrameType(pskb->data) == WIFI_QOS_DATA_TYPE;
	if (bQosData)
		IVOffset += 2;

	/* if(GetHTC()) */
	/*	IVOffset += 4; */

	/* IVOffset += SNAP_SIZE + sizeof(u16); */

	DataOffset = IVOffset + padapter->wapiInfo.extra_prefix_len;

	pRA = pskb->data + 4;
	pTA = pskb->data + 10;
	KeyIdx = *(pskb->data + IVOffset);
	pRecvPN = pskb->data + IVOffset + 2;
	pSecData = pskb->data + DataOffset;
	DataLen = pskb->len - DataOffset;
	pRecvMic = pskb->data + pskb->len - padapter->wapiInfo.extra_postfix_len;
	TID = GetTid(pskb->data);

	if (!list_empty(&(pWapiInfo->wapiSTAUsedList))) {
		list_for_each_entry(pWapiSta, &pWapiInfo->wapiSTAUsedList, list) {
			if (0 == memcmp(pWapiSta->PeerMacAddr, pTA, 6)) {
				bFindMatchPeer = true;
				break;
			}
		}
	}

	if (!bFindMatchPeer) {
		WAPI_TRACE(WAPI_ERR, "%s: Can not find Peer Sta "MAC_FMT" for Key Info!!!\n", __FUNCTION__, MAC_ARG(pTA));
		return false;
	}

	if (IS_MCAST(pRA)) {
		WAPI_TRACE(WAPI_RX, "%s: Multicast decryption !!!\n", __FUNCTION__);
		if (pWapiSta->wapiMsk.keyId == KeyIdx && pWapiSta->wapiMsk.bSet) {
			pLastRxPN = pWapiSta->lastRxMulticastPN;
			if (!WapiComparePN(pRecvPN, pLastRxPN)) {
				WAPI_TRACE(WAPI_ERR, "%s: MSK PN is not larger than last, Dropped!!!\n", __FUNCTION__);
				WAPI_DATA(WAPI_ERR, "pRecvPN:", pRecvPN, 16);
				WAPI_DATA(WAPI_ERR, "pLastRxPN:", pLastRxPN, 16);
				return false;
			}

			memcpy(pLastRxPN, pRecvPN, 16);
			pMicKey = pWapiSta->wapiMsk.micKey;
			pDataKey = pWapiSta->wapiMsk.dataKey;
		} else if (pWapiSta->wapiMskUpdate.keyId == KeyIdx && pWapiSta->wapiMskUpdate.bSet) {
			WAPI_TRACE(WAPI_RX, "%s: Use Updated MSK for Decryption !!!\n", __FUNCTION__);
			bUseUpdatedKey = true;
			memcpy(pWapiSta->lastRxMulticastPN, pRecvPN, 16);
			pMicKey = pWapiSta->wapiMskUpdate.micKey;
			pDataKey = pWapiSta->wapiMskUpdate.dataKey;
		} else {
			WAPI_TRACE(WAPI_ERR, "%s: Can not find MSK with matched KeyIdx(%d), Dropped !!!\n", __FUNCTION__, KeyIdx);
			return false;
		}
	} else {
		WAPI_TRACE(WAPI_RX, "%s: Unicast decryption !!!\n", __FUNCTION__);
		if (pWapiSta->wapiUsk.keyId == KeyIdx && pWapiSta->wapiUsk.bSet) {
			WAPI_TRACE(WAPI_RX, "%s: Use USK for Decryption!!!\n", __FUNCTION__);
			if (precv_hdr->bWapiCheckPNInDecrypt) {
				if (GetFrameType(pskb->data) == WIFI_QOS_DATA_TYPE) {
					WapiGetLastRxUnicastPNForQoSData(TID, pWapiSta, lastRxPNforQoS);
					pLastRxPN = lastRxPNforQoS;
				} else
					pLastRxPN = pWapiSta->lastRxUnicastPN;
				if (!WapiComparePN(pRecvPN, pLastRxPN))
					return false;
				if (bQosData)
					WapiSetLastRxUnicastPNForQoSData(TID, pRecvPN, pWapiSta);
				else
					memcpy(pWapiSta->lastRxUnicastPN, pRecvPN, 16);
			} else
				memcpy(precv_hdr->WapiTempPN, pRecvPN, 16);

			if (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE)) {
				if ((pRecvPN[0] & 0x1) == 0) {
					WAPI_TRACE(WAPI_ERR, "%s: Rx USK PN is not odd when Infra STA mode, Dropped !!!\n", __FUNCTION__);
					return false;
				}
			}

			pMicKey = pWapiSta->wapiUsk.micKey;
			pDataKey = pWapiSta->wapiUsk.dataKey;
		} else if (pWapiSta->wapiUskUpdate.keyId == KeyIdx && pWapiSta->wapiUskUpdate.bSet) {
			WAPI_TRACE(WAPI_RX, "%s: Use Updated USK for Decryption!!!\n", __FUNCTION__);
			if (pWapiSta->bAuthenticatorInUpdata)
				bUseUpdatedKey = true;
			else
				bUseUpdatedKey = false;

			if (bQosData)
				WapiSetLastRxUnicastPNForQoSData(TID, pRecvPN, pWapiSta);
			else
				memcpy(pWapiSta->lastRxUnicastPN, pRecvPN, 16);
			pMicKey = pWapiSta->wapiUskUpdate.micKey;
			pDataKey = pWapiSta->wapiUskUpdate.dataKey;
		} else {
			WAPI_TRACE(WAPI_ERR, "%s: No valid USK!!!KeyIdx=%d pWapiSta->wapiUsk.keyId=%d pWapiSta->wapiUskUpdate.keyId=%d\n", __FUNCTION__, KeyIdx, pWapiSta->wapiUsk.keyId,
				   pWapiSta->wapiUskUpdate.keyId);
			/* dump_buf(pskb->data,pskb->len); */
			return false;
		}
	}

	WAPI_DATA(WAPI_RX, "Decryption - DataKey", pDataKey, 16);
	WAPI_DATA(WAPI_RX, "Decryption - IV", pRecvPN, 16);
	WapiSMS4Decryption(pDataKey, pRecvPN, pSecData, DataLen, pSecData, &OutputLength);

	if (OutputLength != DataLen)
		WAPI_TRACE(WAPI_ERR, "%s:  Output Length Error!!!!\n", __FUNCTION__);

	WAPI_DATA(WAPI_RX, "Decryption - After decryption", pskb->data, pskb->len);

	DataLen -= padapter->wapiInfo.extra_postfix_len;

	SecCalculateMicSMS4(KeyIdx, pMicKey, pskb->data, pSecData, DataLen, MicBuffer);

	WAPI_DATA(WAPI_RX, "Decryption - MIC received", pRecvMic, SMS4_MIC_LEN);
	WAPI_DATA(WAPI_RX, "Decryption - MIC calculated", MicBuffer, SMS4_MIC_LEN);

	if (0 == memcmp(MicBuffer, pRecvMic, padapter->wapiInfo.extra_postfix_len)) {
		WAPI_TRACE(WAPI_RX, "%s: Check MIC OK!!\n", __FUNCTION__);
		if (bUseUpdatedKey) {
			/* delete the old key */
			if (IS_MCAST(pRA)) {
				WAPI_TRACE(WAPI_API, "%s(): AE use new update MSK!!\n", __FUNCTION__);
				pWapiSta->wapiMsk.keyId = pWapiSta->wapiMskUpdate.keyId;
				memcpy(pWapiSta->wapiMsk.dataKey, pWapiSta->wapiMskUpdate.dataKey, 16);
				memcpy(pWapiSta->wapiMsk.micKey, pWapiSta->wapiMskUpdate.micKey, 16);
				pWapiSta->wapiMskUpdate.bTxEnable = pWapiSta->wapiMskUpdate.bSet = false;
			} else {
				WAPI_TRACE(WAPI_API, "%s(): AE use new update USK!!\n", __FUNCTION__);
				pWapiSta->wapiUsk.keyId = pWapiSta->wapiUskUpdate.keyId;
				memcpy(pWapiSta->wapiUsk.dataKey, pWapiSta->wapiUskUpdate.dataKey, 16);
				memcpy(pWapiSta->wapiUsk.micKey, pWapiSta->wapiUskUpdate.micKey, 16);
				pWapiSta->wapiUskUpdate.bTxEnable = pWapiSta->wapiUskUpdate.bSet = false;
			}
		}
	} else {
		WAPI_TRACE(WAPI_ERR, "%s:  Check MIC Error, Dropped !!!!\n", __FUNCTION__);
		return false;
	}

	pos = pskb->data;
	memmove(pos + padapter->wapiInfo.extra_prefix_len, pos, IVOffset);
	skb_pull(pskb, padapter->wapiInfo.extra_prefix_len);

	WAPI_TRACE(WAPI_RX, "<=========%s\n", __FUNCTION__);

	return true;
}

u32	rtw_sms4_encrypt(_adapter *padapter, u8 *pxmitframe)
{

	u8	*pframe;
	u32 res = _SUCCESS;

	WAPI_TRACE(WAPI_TX, "=========>%s\n", __FUNCTION__);

	if ((!padapter->WapiSupport) || (!padapter->wapiInfo.bWapiEnable)) {
		WAPI_TRACE(WAPI_TX, "<========== %s, WAPI not supported or enabled!\n", __FUNCTION__);
		return _FAIL;
	}

	if (((struct xmit_frame *)pxmitframe)->buf_addr == NULL)
		return _FAIL;

	pframe = ((struct xmit_frame *)pxmitframe)->buf_addr + TXDESC_OFFSET;

	SecSWSMS4Encryption(padapter, pxmitframe);

	WAPI_TRACE(WAPI_TX, "<=========%s\n", __FUNCTION__);
	return res;
}

u32	rtw_sms4_decrypt(_adapter *padapter, u8 *precvframe)
{
	u8	*pframe;
	u32 res = _SUCCESS;

	WAPI_TRACE(WAPI_RX, "=========>%s\n", __FUNCTION__);

	if ((!padapter->WapiSupport) || (!padapter->wapiInfo.bWapiEnable)) {
		WAPI_TRACE(WAPI_RX, "<========== %s, WAPI not supported or enabled!\n", __FUNCTION__);
		return _FAIL;
	}


	/* drop packet when hw decrypt fail
	* return tempraily */
	return _FAIL;

	/* pframe=(unsigned char *)((union recv_frame*)precvframe)->u.hdr.rx_data; */

	if (false == SecSWSMS4Decryption(padapter, precvframe, &padapter->recvpriv)) {
		WAPI_TRACE(WAPI_ERR, "%s():SMS4 decrypt frame error\n", __FUNCTION__);
		return _FAIL;
	}

	WAPI_TRACE(WAPI_RX, "<=========%s\n", __FUNCTION__);
	return res;
}

#else

u32	rtw_sms4_encrypt(_adapter *padapter, u8 *pxmitframe)
{
	WAPI_TRACE(WAPI_TX, "=========>Dummy %s\n", __FUNCTION__);
	WAPI_TRACE(WAPI_TX, "<=========Dummy %s\n", __FUNCTION__);
	return _SUCCESS;
}

u32	rtw_sms4_decrypt(_adapter *padapter, u8 *precvframe)
{
	WAPI_TRACE(WAPI_RX, "=========>Dummy %s\n", __FUNCTION__);
	WAPI_TRACE(WAPI_RX, "<=========Dummy %s\n", __FUNCTION__);
	return _SUCCESS;
}

#endif

#endif
