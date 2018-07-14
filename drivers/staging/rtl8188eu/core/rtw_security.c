// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define  _RTW_SECURITY_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <wifi.h>
#include <osdep_intf.h>
#include <net/lib80211.h>

/*
	Need to consider the fragment  situation
*/
void rtw_wep_encrypt(struct adapter *padapter, u8 *pxmitframe)
{
	int	curfragnum, length;
	u8 *pframe;
	u8 hw_hdr_offset = 0;
	struct	pkt_attrib	 *pattrib = &((struct xmit_frame *)pxmitframe)->attrib;
	struct	security_priv	*psecuritypriv = &padapter->securitypriv;
	struct	xmit_priv		*pxmitpriv = &padapter->xmitpriv;
	const int keyindex = psecuritypriv->dot11PrivacyKeyIndex;
	void *crypto_private;
	struct sk_buff *skb;
	struct lib80211_crypto_ops *crypto_ops;

	if (((struct xmit_frame *)pxmitframe)->buf_addr == NULL)
		return;

	if ((pattrib->encrypt != _WEP40_) && (pattrib->encrypt != _WEP104_))
		return;

	hw_hdr_offset = TXDESC_SIZE +
		 (((struct xmit_frame *)pxmitframe)->pkt_offset * PACKET_OFFSET_SZ);

	pframe = ((struct xmit_frame *)pxmitframe)->buf_addr + hw_hdr_offset;

	crypto_ops = try_then_request_module(lib80211_get_crypto_ops("WEP"), "lib80211_crypt_wep");

	if (!crypto_ops)
		return;

	crypto_private = crypto_ops->init(keyindex);
	if (!crypto_private)
		return;

	if (crypto_ops->set_key(psecuritypriv->dot11DefKey[keyindex].skey,
				psecuritypriv->dot11DefKeylen[keyindex], NULL, crypto_private) < 0)
		goto free_crypto_private;

	for (curfragnum = 0; curfragnum < pattrib->nr_frags; curfragnum++) {
		if (curfragnum + 1 == pattrib->nr_frags)
			length = pattrib->last_txcmdsz;
		else
			length = pxmitpriv->frag_len;
		skb = dev_alloc_skb(length);
		if (!skb)
			goto free_crypto_private;

		skb_put_data(skb, pframe, length);

		memmove(skb->data + 4, skb->data, pattrib->hdrlen);
		skb_pull(skb, 4);
		skb_trim(skb, skb->len - 4);

		if (crypto_ops->encrypt_mpdu(skb, pattrib->hdrlen, crypto_private)) {
			kfree_skb(skb);
			goto free_crypto_private;
		}

		memcpy(pframe, skb->data, skb->len);

		pframe += skb->len;
		pframe = (u8 *)round_up((size_t)(pframe), 4);

		kfree_skb(skb);
	}

free_crypto_private:
	crypto_ops->deinit(crypto_private);
}

int rtw_wep_decrypt(struct adapter  *padapter, u8 *precvframe)
{
	struct	rx_pkt_attrib	 *prxattrib = &(((struct recv_frame *)precvframe)->attrib);

	if ((prxattrib->encrypt == _WEP40_) || (prxattrib->encrypt == _WEP104_)) {
		struct	security_priv	*psecuritypriv = &padapter->securitypriv;
		struct sk_buff *skb = ((struct recv_frame *)precvframe)->pkt;
		u8 *pframe = skb->data;
		void *crypto_private = NULL;
		int status = _SUCCESS;
		const int keyindex = prxattrib->key_index;
		struct lib80211_crypto_ops *crypto_ops = try_then_request_module(lib80211_get_crypto_ops("WEP"), "lib80211_crypt_wep");
		char iv[4], icv[4];

		if (!crypto_ops) {
			status = _FAIL;
			goto exit;
		}

		memcpy(iv, pframe + prxattrib->hdrlen, 4);
		memcpy(icv, pframe + skb->len - 4, 4);

		crypto_private = crypto_ops->init(keyindex);
		if (!crypto_private) {
			status = _FAIL;
			goto exit;
		}
		if (crypto_ops->set_key(psecuritypriv->dot11DefKey[keyindex].skey,
					psecuritypriv->dot11DefKeylen[keyindex], NULL, crypto_private) < 0) {
			status = _FAIL;
			goto exit;
		}
		if (crypto_ops->decrypt_mpdu(skb, prxattrib->hdrlen, crypto_private)) {
			status = _FAIL;
			goto exit;
		}

		memmove(pframe, pframe + 4, prxattrib->hdrlen);
		skb_push(skb, 4);
		skb_put(skb, 4);

		memcpy(pframe + prxattrib->hdrlen, iv, 4);
		memcpy(pframe + skb->len - 4, icv, 4);

exit:
		if (crypto_ops && crypto_private)
			crypto_ops->deinit(crypto_private);
		return status;
	}

	return _FAIL;
}

/* 3		===== TKIP related ===== */

static u32 secmicgetuint32(u8 *p)
/*  Convert from Byte[] to Us3232 in a portable way */
{
	s32 i;
	u32 res = 0;

	for (i = 0; i < 4; i++)
		res |= ((u32)(*p++)) << (8*i);
	return res;
}

static void secmicputuint32(u8 *p, u32 val)
/*  Convert from Us3232 to Byte[] in a portable way */
{
	long i;

	for (i = 0; i < 4; i++) {
		*p++ = (u8)(val & 0xff);
		val >>= 8;
	}
}

static void secmicclear(struct mic_data *pmicdata)
{
/*  Reset the state to the empty message. */
	pmicdata->L = pmicdata->K0;
	pmicdata->R = pmicdata->K1;
	pmicdata->nBytesInM = 0;
	pmicdata->M = 0;
}

void rtw_secmicsetkey(struct mic_data *pmicdata, u8 *key)
{
	/*  Set the key */
	pmicdata->K0 = secmicgetuint32(key);
	pmicdata->K1 = secmicgetuint32(key + 4);
	/*  and reset the message */
	secmicclear(pmicdata);
}

void rtw_secmicappendbyte(struct mic_data *pmicdata, u8 b)
{
	/*  Append the byte to our word-sized buffer */
	pmicdata->M |= ((unsigned long)b) << (8*pmicdata->nBytesInM);
	pmicdata->nBytesInM++;
	/*  Process the word if it is full. */
	if (pmicdata->nBytesInM >= 4) {
		pmicdata->L ^= pmicdata->M;
		pmicdata->R ^= ROL32(pmicdata->L, 17);
		pmicdata->L += pmicdata->R;
		pmicdata->R ^= ((pmicdata->L & 0xff00ff00) >> 8) | ((pmicdata->L & 0x00ff00ff) << 8);
		pmicdata->L += pmicdata->R;
		pmicdata->R ^= ROL32(pmicdata->L, 3);
		pmicdata->L += pmicdata->R;
		pmicdata->R ^= ROR32(pmicdata->L, 2);
		pmicdata->L += pmicdata->R;
		/*  Clear the buffer */
		pmicdata->M = 0;
		pmicdata->nBytesInM = 0;
	}
}

void rtw_secmicappend(struct mic_data *pmicdata, u8 *src, u32 nbytes)
{
	/*  This is simple */
	while (nbytes > 0) {
		rtw_secmicappendbyte(pmicdata, *src++);
		nbytes--;
	}
}

void rtw_secgetmic(struct mic_data *pmicdata, u8 *dst)
{
	/*  Append the minimum padding */
	rtw_secmicappendbyte(pmicdata, 0x5a);
	rtw_secmicappendbyte(pmicdata, 0);
	rtw_secmicappendbyte(pmicdata, 0);
	rtw_secmicappendbyte(pmicdata, 0);
	rtw_secmicappendbyte(pmicdata, 0);
	/*  and then zeroes until the length is a multiple of 4 */
	while (pmicdata->nBytesInM != 0)
		rtw_secmicappendbyte(pmicdata, 0);
	/*  The appendByte function has already computed the result. */
	secmicputuint32(dst, pmicdata->L);
	secmicputuint32(dst+4, pmicdata->R);
	/*  Reset to the empty message. */
	secmicclear(pmicdata);
}

void rtw_seccalctkipmic(u8 *key, u8 *header, u8 *data, u32 data_len, u8 *mic_code, u8 pri)
{
	struct mic_data	micdata;
	u8 priority[4] = {0x0, 0x0, 0x0, 0x0};

	rtw_secmicsetkey(&micdata, key);
	priority[0] = pri;

	/* Michael MIC pseudo header: DA, SA, 3 x 0, Priority */
	if (header[1]&1) {   /* ToDS == 1 */
			rtw_secmicappend(&micdata, &header[16], 6);  /* DA */
		if (header[1]&2)  /* From Ds == 1 */
			rtw_secmicappend(&micdata, &header[24], 6);
		else
			rtw_secmicappend(&micdata, &header[10], 6);
	} else {	/* ToDS == 0 */
		rtw_secmicappend(&micdata, &header[4], 6);   /* DA */
		if (header[1]&2)  /* From Ds == 1 */
			rtw_secmicappend(&micdata, &header[16], 6);
		else
			rtw_secmicappend(&micdata, &header[10], 6);
	}
	rtw_secmicappend(&micdata, &priority[0], 4);

	rtw_secmicappend(&micdata, data, data_len);

	rtw_secgetmic(&micdata, mic_code);
}



/* macros for extraction/creation of unsigned char/unsigned short values  */
#define RotR1(v16)   ((((v16) >> 1) & 0x7FFF) ^ (((v16) & 1) << 15))
#define   Lo8(v16)   ((u8)((v16)       & 0x00FF))
#define   Hi8(v16)   ((u8)(((v16) >> 8) & 0x00FF))
#define  Lo16(v32)   ((u16)((v32)       & 0xFFFF))
#define  Hi16(v32)   ((u16)(((v32) >> 16) & 0xFFFF))
#define  Mk16(hi, lo) ((lo) ^ (((u16)(hi)) << 8))

/* select the Nth 16-bit word of the temporal key unsigned char array TK[]   */
#define  TK16(N)     Mk16(tk[2*(N)+1], tk[2*(N)])

/* S-box lookup: 16 bits --> 16 bits */
#define _S_(v16)     (Sbox1[0][Lo8(v16)] ^ Sbox1[1][Hi8(v16)])

/* fixed algorithm "parameters" */
#define PHASE1_LOOP_CNT   8    /* this needs to be "big enough"     */
#define TA_SIZE	   6    /*  48-bit transmitter address       */
#define TK_SIZE	  16    /* 128-bit temporal key	      */
#define P1K_SIZE	 10    /*  80-bit Phase1 key		*/
#define RC4_KEY_SIZE     16    /* 128-bit RC4KEY (104 bits unknown) */

/* The hlen isn't include the IV */
u32	rtw_tkip_encrypt(struct adapter *padapter, u8 *pxmitframe)
{
	u8   hw_hdr_offset = 0;
	int			curfragnum, length;

	u8 *pframe;
	struct	sta_info		*stainfo;
	struct	pkt_attrib	 *pattrib = &((struct xmit_frame *)pxmitframe)->attrib;
	struct	security_priv	*psecuritypriv = &padapter->securitypriv;
	struct	xmit_priv		*pxmitpriv = &padapter->xmitpriv;
	u32	res = _SUCCESS;
	void *crypto_private;
	struct sk_buff *skb;
	u8 key[32];
	int key_idx;
	const int key_length = 32;
	struct lib80211_crypto_ops *crypto_ops;

	if (((struct xmit_frame *)pxmitframe)->buf_addr == NULL)
		return _FAIL;

	hw_hdr_offset = TXDESC_SIZE +
		 (((struct xmit_frame *)pxmitframe)->pkt_offset * PACKET_OFFSET_SZ);
	pframe = ((struct xmit_frame *)pxmitframe)->buf_addr + hw_hdr_offset;
	/* 4 start to encrypt each fragment */
	if (pattrib->encrypt != _TKIP_)
		return res;

	if (pattrib->psta)
		stainfo = pattrib->psta;
	else
		stainfo = rtw_get_stainfo(&padapter->stapriv, &pattrib->ra[0]);

	if (!stainfo) {
		RT_TRACE(_module_rtl871x_security_c_, _drv_err_, ("%s: stainfo==NULL!!!\n", __func__));
		return _FAIL;
	}

	crypto_ops = try_then_request_module(lib80211_get_crypto_ops("TKIP"), "lib80211_crypt_tkip");

	if (IS_MCAST(pattrib->ra)) {
		key_idx = psecuritypriv->dot118021XGrpKeyid;
		memcpy(key, psecuritypriv->dot118021XGrpKey[key_idx].skey, 16);
		memcpy(key + 16, psecuritypriv->dot118021XGrptxmickey[key_idx].skey, 16);
	} else {
		key_idx = 0;
		memcpy(key, stainfo->dot118021x_UncstKey.skey, 16);
		memcpy(key + 16, stainfo->dot11tkiptxmickey.skey, 16);
	}

	if (!crypto_ops) {
		res = _FAIL;
		goto exit;
	}

	crypto_private = crypto_ops->init(key_idx);
	if (!crypto_private) {
		res = _FAIL;
		goto exit;
	}

	if (crypto_ops->set_key(key, key_length, NULL, crypto_private) < 0) {
		res = _FAIL;
		goto exit_crypto_ops_deinit;
	}

	RT_TRACE(_module_rtl871x_security_c_, _drv_err_, ("%s: stainfo!= NULL!!!\n", __func__));

	for (curfragnum = 0; curfragnum < pattrib->nr_frags; curfragnum++) {
		if ((curfragnum+1) == pattrib->nr_frags)
			length = pattrib->last_txcmdsz;
		else
			length = pxmitpriv->frag_len;

		skb = dev_alloc_skb(length);
		if (!skb) {
			res = _FAIL;
			goto exit_crypto_ops_deinit;
		}

		skb_put_data(skb, pframe, length);

		memmove(skb->data + pattrib->iv_len, skb->data, pattrib->hdrlen);
		skb_pull(skb, pattrib->iv_len);
		skb_trim(skb, skb->len - pattrib->icv_len);

		if (crypto_ops->encrypt_mpdu(skb, pattrib->hdrlen, crypto_private)) {
			kfree_skb(skb);
			res = _FAIL;
			goto exit_crypto_ops_deinit;
		}

		memcpy(pframe, skb->data, skb->len);

		pframe += skb->len;
		pframe = (u8 *)round_up((size_t)(pframe), 4);

		kfree_skb(skb);
	}

exit_crypto_ops_deinit:
	crypto_ops->deinit(crypto_private);

exit:
	return res;
}

u32 rtw_tkip_decrypt(struct adapter *padapter, u8 *precvframe)
{
	struct rx_pkt_attrib *prxattrib = &((struct recv_frame *)precvframe)->attrib;
	u32 res = _SUCCESS;

	/* 4 start to decrypt recvframe */
	if (prxattrib->encrypt == _TKIP_) {
		struct sta_info *stainfo = rtw_get_stainfo(&padapter->stapriv, prxattrib->ta);

		if (stainfo) {
			int key_idx;
			const int iv_len = 8, icv_len = 4, key_length = 32;
			void *crypto_private = NULL;
			struct sk_buff *skb = ((struct recv_frame *)precvframe)->pkt;
			u8 key[32], iv[8], icv[4], *pframe = skb->data;
			struct lib80211_crypto_ops *crypto_ops = try_then_request_module(lib80211_get_crypto_ops("TKIP"), "lib80211_crypt_tkip");
			struct security_priv *psecuritypriv = &padapter->securitypriv;

			if (IS_MCAST(prxattrib->ra)) {
				if (!psecuritypriv->binstallGrpkey) {
					res = _FAIL;
					DBG_88E("%s:rx bc/mc packets, but didn't install group key!!!!!!!!!!\n", __func__);
					goto exit;
				}
				key_idx = prxattrib->key_index;
				memcpy(key, psecuritypriv->dot118021XGrpKey[key_idx].skey, 16);
				memcpy(key + 16, psecuritypriv->dot118021XGrprxmickey[key_idx].skey, 16);
			} else {
				key_idx = 0;
				memcpy(key, stainfo->dot118021x_UncstKey.skey, 16);
				memcpy(key + 16, stainfo->dot11tkiprxmickey.skey, 16);
			}

			if (!crypto_ops) {
				res = _FAIL;
				goto exit_lib80211_tkip;
			}

			memcpy(iv, pframe + prxattrib->hdrlen, iv_len);
			memcpy(icv, pframe + skb->len - icv_len, icv_len);

			crypto_private = crypto_ops->init(key_idx);
			if (!crypto_private) {
				res = _FAIL;
				goto exit_lib80211_tkip;
			}
			if (crypto_ops->set_key(key, key_length, NULL, crypto_private) < 0) {
				res = _FAIL;
				goto exit_lib80211_tkip;
			}
			if (crypto_ops->decrypt_mpdu(skb, prxattrib->hdrlen, crypto_private)) {
				res = _FAIL;
				goto exit_lib80211_tkip;
			}

			memmove(pframe, pframe + iv_len, prxattrib->hdrlen);
			skb_push(skb, iv_len);
			skb_put(skb, icv_len);

			memcpy(pframe + prxattrib->hdrlen, iv, iv_len);
			memcpy(pframe + skb->len - icv_len, icv, icv_len);

exit_lib80211_tkip:
			if (crypto_ops && crypto_private)
				crypto_ops->deinit(crypto_private);
		} else {
			RT_TRACE(_module_rtl871x_security_c_, _drv_err_, ("rtw_tkip_decrypt: stainfo==NULL!!!\n"));
			res = _FAIL;
		}
	}
exit:
	return res;
}

/* 3			===== AES related ===== */


#define MAX_MSG_SIZE	2048
/*****************************/
/******** SBOX Table *********/
/*****************************/

static  u8 sbox_table[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

/*****************************/
/**** Function Prototypes ****/
/*****************************/

static void bitwise_xor(u8 *ina, u8 *inb, u8 *out);
static void construct_mic_iv(u8 *mic_header1, int qc_exists, int a4_exists, u8 *mpdu, uint payload_length, u8 *pn_vector);
static void construct_mic_header1(u8 *mic_header1, int header_length, u8 *mpdu);
static void construct_mic_header2(u8 *mic_header2, u8 *mpdu, int a4_exists, int qc_exists);
static void construct_ctr_preload(u8 *ctr_preload, int a4_exists, int qc_exists, u8 *mpdu, u8 *pn_vector, int c);
static void xor_128(u8 *a, u8 *b, u8 *out);
static void xor_32(u8 *a, u8 *b, u8 *out);
static u8 sbox(u8 a);
static void next_key(u8 *key, int round);
static void byte_sub(u8 *in, u8 *out);
static void shift_row(u8 *in, u8 *out);
static void mix_column(u8 *in, u8 *out);
static void aes128k128d(u8 *key, u8 *data, u8 *ciphertext);

/****************************************/
/* aes128k128d()			*/
/* Performs a 128 bit AES encrypt with  */
/* 128 bit data.			*/
/****************************************/
static void xor_128(u8 *a, u8 *b, u8 *out)
{
	int i;

	for (i = 0; i < 16; i++)
		out[i] = a[i] ^ b[i];
}

static void xor_32(u8 *a, u8 *b, u8 *out)
{
	int i;

	for (i = 0; i < 4; i++)
		out[i] = a[i] ^ b[i];
}

static u8 sbox(u8 a)
{
	return sbox_table[(int)a];
}

static void next_key(u8 *key, int round)
{
	u8 rcon;
	u8 sbox_key[4];
	u8 rcon_table[12] = {
		0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
		0x1b, 0x36, 0x36, 0x36
	};

	sbox_key[0] = sbox(key[13]);
	sbox_key[1] = sbox(key[14]);
	sbox_key[2] = sbox(key[15]);
	sbox_key[3] = sbox(key[12]);

	rcon = rcon_table[round];

	xor_32(&key[0], sbox_key, &key[0]);
	key[0] = key[0] ^ rcon;

	xor_32(&key[4], &key[0], &key[4]);
	xor_32(&key[8], &key[4], &key[8]);
	xor_32(&key[12], &key[8], &key[12]);
}

static void byte_sub(u8 *in, u8 *out)
{
	int i;
	for (i = 0; i < 16; i++)
		out[i] = sbox(in[i]);
}

static void shift_row(u8 *in, u8 *out)
{
	out[0] =  in[0];
	out[1] =  in[5];
	out[2] =  in[10];
	out[3] =  in[15];
	out[4] =  in[4];
	out[5] =  in[9];
	out[6] =  in[14];
	out[7] =  in[3];
	out[8] =  in[8];
	out[9] =  in[13];
	out[10] = in[2];
	out[11] = in[7];
	out[12] = in[12];
	out[13] = in[1];
	out[14] = in[6];
	out[15] = in[11];
}

static void mix_column(u8 *in, u8 *out)
{
	int i;
	u8 add1b[4];
	u8 add1bf7[4];
	u8 rotl[4];
	u8 swap_halves[4];
	u8 andf7[4];
	u8 rotr[4];
	u8 temp[4];
	u8 tempb[4];

	for (i = 0 ; i < 4; i++) {
		if ((in[i] & 0x80) == 0x80)
			add1b[i] = 0x1b;
		else
			add1b[i] = 0x00;
	}

	swap_halves[0] = in[2];    /* Swap halves */
	swap_halves[1] = in[3];
	swap_halves[2] = in[0];
	swap_halves[3] = in[1];

	rotl[0] = in[3];	/* Rotate left 8 bits */
	rotl[1] = in[0];
	rotl[2] = in[1];
	rotl[3] = in[2];

	andf7[0] = in[0] & 0x7f;
	andf7[1] = in[1] & 0x7f;
	andf7[2] = in[2] & 0x7f;
	andf7[3] = in[3] & 0x7f;

	for (i = 3; i > 0; i--) {    /* logical shift left 1 bit */
		andf7[i] = andf7[i] << 1;
		if ((andf7[i-1] & 0x80) == 0x80)
			andf7[i] = (andf7[i] | 0x01);
	}
	andf7[0] = andf7[0] << 1;
	andf7[0] = andf7[0] & 0xfe;

	xor_32(add1b, andf7, add1bf7);

	xor_32(in, add1bf7, rotr);

	temp[0] = rotr[0];	 /* Rotate right 8 bits */
	rotr[0] = rotr[1];
	rotr[1] = rotr[2];
	rotr[2] = rotr[3];
	rotr[3] = temp[0];

	xor_32(add1bf7, rotr, temp);
	xor_32(swap_halves, rotl, tempb);
	xor_32(temp, tempb, out);
}

static void aes128k128d(u8 *key, u8 *data, u8 *ciphertext)
{
	int round;
	int i;
	u8 intermediatea[16];
	u8 intermediateb[16];
	u8 round_key[16];

	for (i = 0; i < 16; i++)
		round_key[i] = key[i];
	for (round = 0; round < 11; round++) {
		if (round == 0) {
			xor_128(round_key, data, ciphertext);
			next_key(round_key, round);
		} else if (round == 10) {
			byte_sub(ciphertext, intermediatea);
			shift_row(intermediatea, intermediateb);
			xor_128(intermediateb, round_key, ciphertext);
		} else {    /* 1 - 9 */
			byte_sub(ciphertext, intermediatea);
			shift_row(intermediatea, intermediateb);
			mix_column(&intermediateb[0], &intermediatea[0]);
			mix_column(&intermediateb[4], &intermediatea[4]);
			mix_column(&intermediateb[8], &intermediatea[8]);
			mix_column(&intermediateb[12], &intermediatea[12]);
			xor_128(intermediatea, round_key, ciphertext);
			next_key(round_key, round);
		}
	}
}

/************************************************/
/* construct_mic_iv()			   */
/* Builds the MIC IV from header fields and PN  */
/************************************************/
static void construct_mic_iv(u8 *mic_iv, int qc_exists, int a4_exists, u8 *mpdu,
			     uint payload_length, u8 *pn_vector)
{
	int i;

	mic_iv[0] = 0x59;
	if (qc_exists && a4_exists)
		mic_iv[1] = mpdu[30] & 0x0f;    /* QoS_TC	   */
	if (qc_exists && !a4_exists)
		mic_iv[1] = mpdu[24] & 0x0f;	/* mute bits 7-4    */
	if (!qc_exists)
		mic_iv[1] = 0x00;
	for (i = 2; i < 8; i++)
		mic_iv[i] = mpdu[i + 8];	/* mic_iv[2:7] = A2[0:5] = mpdu[10:15] */
	for (i = 8; i < 14; i++)
		mic_iv[i] = pn_vector[13 - i];	/* mic_iv[8:13] = PN[5:0] */
	mic_iv[14] = (unsigned char)(payload_length / 256);
	mic_iv[15] = (unsigned char)(payload_length % 256);
}

/************************************************/
/* construct_mic_header1()		      */
/* Builds the first MIC header block from       */
/* header fields.			       */
/************************************************/
static void construct_mic_header1(u8 *mic_header1, int header_length, u8 *mpdu)
{
	mic_header1[0] = (u8)((header_length - 2) / 256);
	mic_header1[1] = (u8)((header_length - 2) % 256);
	mic_header1[2] = mpdu[0] & 0xcf;    /* Mute CF poll & CF ack bits */
	mic_header1[3] = mpdu[1] & 0xc7;    /* Mute retry, more data and pwr mgt bits */
	mic_header1[4] = mpdu[4];       /* A1 */
	mic_header1[5] = mpdu[5];
	mic_header1[6] = mpdu[6];
	mic_header1[7] = mpdu[7];
	mic_header1[8] = mpdu[8];
	mic_header1[9] = mpdu[9];
	mic_header1[10] = mpdu[10];     /* A2 */
	mic_header1[11] = mpdu[11];
	mic_header1[12] = mpdu[12];
	mic_header1[13] = mpdu[13];
	mic_header1[14] = mpdu[14];
	mic_header1[15] = mpdu[15];
}

/************************************************/
/* construct_mic_header2()		      */
/* Builds the last MIC header block from	*/
/* header fields.			       */
/************************************************/
static void construct_mic_header2(u8 *mic_header2, u8 *mpdu, int a4_exists, int qc_exists)
{
	int i;

	for (i = 0; i < 16; i++)
		mic_header2[i] = 0x00;

	mic_header2[0] = mpdu[16];    /* A3 */
	mic_header2[1] = mpdu[17];
	mic_header2[2] = mpdu[18];
	mic_header2[3] = mpdu[19];
	mic_header2[4] = mpdu[20];
	mic_header2[5] = mpdu[21];

	mic_header2[6] = 0x00;
	mic_header2[7] = 0x00; /* mpdu[23]; */

	if (!qc_exists && a4_exists) {
		for (i = 0; i < 6; i++)
			mic_header2[8+i] = mpdu[24+i];   /* A4 */
	}

	if (qc_exists && !a4_exists) {
		mic_header2[8] = mpdu[24] & 0x0f; /* mute bits 15 - 4 */
		mic_header2[9] = mpdu[25] & 0x00;
	}

	if (qc_exists && a4_exists) {
		for (i = 0; i < 6; i++)
			mic_header2[8+i] = mpdu[24+i];   /* A4 */

		mic_header2[14] = mpdu[30] & 0x0f;
		mic_header2[15] = mpdu[31] & 0x00;
	}
}

/************************************************/
/* construct_mic_header2()		      */
/* Builds the last MIC header block from	*/
/* header fields.			       */
/************************************************/
static void construct_ctr_preload(u8 *ctr_preload, int a4_exists, int qc_exists, u8 *mpdu, u8 *pn_vector, int c)
{
	int i;

	for (i = 0; i < 16; i++)
		ctr_preload[i] = 0x00;
	i = 0;

	ctr_preload[0] = 0x01;				  /* flag */
	if (qc_exists && a4_exists)
		ctr_preload[1] = mpdu[30] & 0x0f;   /* QoC_Control */
	if (qc_exists && !a4_exists)
		ctr_preload[1] = mpdu[24] & 0x0f;

	for (i = 2; i < 8; i++)
		ctr_preload[i] = mpdu[i + 8];		       /* ctr_preload[2:7] = A2[0:5] = mpdu[10:15] */
	for (i = 8; i < 14; i++)
		ctr_preload[i] =    pn_vector[13 - i];	  /* ctr_preload[8:13] = PN[5:0] */
	ctr_preload[14] =  (unsigned char)(c / 256); /* Ctr */
	ctr_preload[15] =  (unsigned char)(c % 256);
}

/************************************/
/* bitwise_xor()		    */
/* A 128 bit, bitwise exclusive or  */
/************************************/
static void bitwise_xor(u8 *ina, u8 *inb, u8 *out)
{
	int i;

	for (i = 0; i < 16; i++)
		out[i] = ina[i] ^ inb[i];
}

static int aes_cipher(u8 *key, uint hdrlen, u8 *pframe, uint plen)
{
	uint	qc_exists, a4_exists, i, j, payload_remainder,
		num_blocks, payload_index;

	u8 pn_vector[6];
	u8 mic_iv[16];
	u8 mic_header1[16];
	u8 mic_header2[16];
	u8 ctr_preload[16];

	/* Intermediate Buffers */
	u8 chain_buffer[16];
	u8 aes_out[16];
	u8 padded_buffer[16];
	u8 mic[8];
	uint	frtype  = GetFrameType(pframe);
	uint	frsubtype  = GetFrameSubType(pframe);

	frsubtype >>= 4;

	memset(mic_iv, 0, 16);
	memset(mic_header1, 0, 16);
	memset(mic_header2, 0, 16);
	memset(ctr_preload, 0, 16);
	memset(chain_buffer, 0, 16);
	memset(aes_out, 0, 16);
	memset(padded_buffer, 0, 16);

	if ((hdrlen == WLAN_HDR_A3_LEN) || (hdrlen ==  WLAN_HDR_A3_QOS_LEN))
		a4_exists = 0;
	else
		a4_exists = 1;

	if ((frtype == WIFI_DATA_CFACK) || (frtype == WIFI_DATA_CFPOLL) || (frtype == WIFI_DATA_CFACKPOLL)) {
		qc_exists = 1;
		if (hdrlen !=  WLAN_HDR_A3_QOS_LEN)
			hdrlen += 2;
	} else if ((frsubtype == 0x08) || (frsubtype == 0x09) || (frsubtype == 0x0a) || (frsubtype == 0x0b)) {
		if (hdrlen !=  WLAN_HDR_A3_QOS_LEN)
			hdrlen += 2;
		qc_exists = 1;
	} else {
		qc_exists = 0;
	}

	pn_vector[0] = pframe[hdrlen];
	pn_vector[1] = pframe[hdrlen+1];
	pn_vector[2] = pframe[hdrlen+4];
	pn_vector[3] = pframe[hdrlen+5];
	pn_vector[4] = pframe[hdrlen+6];
	pn_vector[5] = pframe[hdrlen+7];

	construct_mic_iv(mic_iv, qc_exists, a4_exists, pframe, plen, pn_vector);

	construct_mic_header1(mic_header1, hdrlen, pframe);
	construct_mic_header2(mic_header2, pframe, a4_exists, qc_exists);

	payload_remainder = plen % 16;
	num_blocks = plen / 16;

	/* Find start of payload */
	payload_index = hdrlen + 8;

	/* Calculate MIC */
	aes128k128d(key, mic_iv, aes_out);
	bitwise_xor(aes_out, mic_header1, chain_buffer);
	aes128k128d(key, chain_buffer, aes_out);
	bitwise_xor(aes_out, mic_header2, chain_buffer);
	aes128k128d(key, chain_buffer, aes_out);

	for (i = 0; i < num_blocks; i++) {
		bitwise_xor(aes_out, &pframe[payload_index], chain_buffer);/* bitwise_xor(aes_out, &message[payload_index], chain_buffer); */

		payload_index += 16;
		aes128k128d(key, chain_buffer, aes_out);
	}

	/* Add on the final payload block if it needs padding */
	if (payload_remainder > 0) {
		for (j = 0; j < 16; j++)
			padded_buffer[j] = 0x00;
		for (j = 0; j < payload_remainder; j++)
			padded_buffer[j] = pframe[payload_index++];/* padded_buffer[j] = message[payload_index++]; */
		bitwise_xor(aes_out, padded_buffer, chain_buffer);
		aes128k128d(key, chain_buffer, aes_out);
	}

	for (j = 0; j < 8; j++)
		mic[j] = aes_out[j];

	/* Insert MIC into payload */
	for (j = 0; j < 8; j++)
		pframe[payload_index+j] = mic[j];

	payload_index = hdrlen + 8;
	for (i = 0; i < num_blocks; i++) {
		construct_ctr_preload(ctr_preload, a4_exists, qc_exists, pframe, pn_vector, i+1);
		aes128k128d(key, ctr_preload, aes_out);
		bitwise_xor(aes_out, &pframe[payload_index], chain_buffer);
		for (j = 0; j < 16; j++)
			pframe[payload_index++] = chain_buffer[j];
	}

	if (payload_remainder > 0) {    /* If there is a short final block, then pad it,*/
					/* encrypt it and copy the unpadded part back   */
		construct_ctr_preload(ctr_preload, a4_exists, qc_exists, pframe, pn_vector, num_blocks+1);

		for (j = 0; j < 16; j++)
			padded_buffer[j] = 0x00;
		for (j = 0; j < payload_remainder; j++)
			padded_buffer[j] = pframe[payload_index+j];
		aes128k128d(key, ctr_preload, aes_out);
		bitwise_xor(aes_out, padded_buffer, chain_buffer);
		for (j = 0; j < payload_remainder; j++)
			pframe[payload_index++] = chain_buffer[j];
	}
	/* Encrypt the MIC */
	construct_ctr_preload(ctr_preload, a4_exists, qc_exists, pframe, pn_vector, 0);

	for (j = 0; j < 16; j++)
		padded_buffer[j] = 0x00;
	for (j = 0; j < 8; j++)
		padded_buffer[j] = pframe[j+hdrlen+8+plen];

	aes128k128d(key, ctr_preload, aes_out);
	bitwise_xor(aes_out, padded_buffer, chain_buffer);
	for (j = 0; j < 8; j++)
		pframe[payload_index++] = chain_buffer[j];
	return _SUCCESS;
}

u32	rtw_aes_encrypt(struct adapter *padapter, u8 *pxmitframe)
{	/*  exclude ICV */

	/*static*/
/*	unsigned char	message[MAX_MSG_SIZE]; */

	/* Intermediate Buffers */
	int	curfragnum, length;
	u8	*pframe, *prwskey;	/*  *payload,*iv */
	u8   hw_hdr_offset = 0;
	struct	sta_info		*stainfo;
	struct	pkt_attrib	 *pattrib = &((struct xmit_frame *)pxmitframe)->attrib;
	struct	security_priv	*psecuritypriv = &padapter->securitypriv;
	struct	xmit_priv		*pxmitpriv = &padapter->xmitpriv;

/*	uint	offset = 0; */
	u32 res = _SUCCESS;

	if (((struct xmit_frame *)pxmitframe)->buf_addr == NULL)
		return _FAIL;

	hw_hdr_offset = TXDESC_SIZE +
		 (((struct xmit_frame *)pxmitframe)->pkt_offset * PACKET_OFFSET_SZ);

	pframe = ((struct xmit_frame *)pxmitframe)->buf_addr + hw_hdr_offset;

	/* 4 start to encrypt each fragment */
	if (pattrib->encrypt == _AES_) {
		if (pattrib->psta)
			stainfo = pattrib->psta;
		else
			stainfo = rtw_get_stainfo(&padapter->stapriv, &pattrib->ra[0]);

		if (stainfo) {
			RT_TRACE(_module_rtl871x_security_c_, _drv_err_, ("%s: stainfo!= NULL!!!\n", __func__));

			if (IS_MCAST(pattrib->ra))
				prwskey = psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].skey;
			else
				prwskey = &stainfo->dot118021x_UncstKey.skey[0];
			for (curfragnum = 0; curfragnum < pattrib->nr_frags; curfragnum++) {
				if ((curfragnum+1) == pattrib->nr_frags) {	/* 4 the last fragment */
					length = pattrib->last_txcmdsz-pattrib->hdrlen-pattrib->iv_len-pattrib->icv_len;

					aes_cipher(prwskey, pattrib->hdrlen, pframe, length);
				} else{
					length = pxmitpriv->frag_len-pattrib->hdrlen-pattrib->iv_len-pattrib->icv_len;

					aes_cipher(prwskey, pattrib->hdrlen, pframe, length);
					pframe += pxmitpriv->frag_len;
					pframe = (u8 *)round_up((size_t)(pframe), 8);
				}
			}
		} else{
			RT_TRACE(_module_rtl871x_security_c_, _drv_err_, ("%s: stainfo==NULL!!!\n", __func__));
			res = _FAIL;
		}
	}


		return res;
}

u32 rtw_aes_decrypt(struct adapter *padapter, u8 *precvframe)
{
	struct rx_pkt_attrib *prxattrib = &((struct recv_frame *)precvframe)->attrib;
	u32 res = _SUCCESS;

	/* 4 start to encrypt each fragment */
	if (prxattrib->encrypt == _AES_) {
		struct sta_info *stainfo = rtw_get_stainfo(&padapter->stapriv, &prxattrib->ta[0]);

		if (stainfo != NULL) {
			int key_idx;
			const int key_length = 16, iv_len = 8, icv_len = 8;
			struct sk_buff *skb = ((struct recv_frame *)precvframe)->pkt;
			void *crypto_private = NULL;
			u8 *key, *pframe = skb->data;
			struct lib80211_crypto_ops *crypto_ops = try_then_request_module(lib80211_get_crypto_ops("CCMP"), "lib80211_crypt_ccmp");
			struct security_priv *psecuritypriv = &padapter->securitypriv;
			char iv[8], icv[8];

			if (IS_MCAST(prxattrib->ra)) {
				/* in concurrent we should use sw descrypt in group key, so we remove this message */
				if (!psecuritypriv->binstallGrpkey) {
					res = _FAIL;
					DBG_88E("%s:rx bc/mc packets, but didn't install group key!!!!!!!!!!\n", __func__);
					goto exit;
				}
				key_idx = psecuritypriv->dot118021XGrpKeyid;
				key = psecuritypriv->dot118021XGrpKey[key_idx].skey;
			} else {
				key_idx = 0;
				key = stainfo->dot118021x_UncstKey.skey;
			}

			if (!crypto_ops) {
				res = _FAIL;
				goto exit_lib80211_ccmp;
			}

			memcpy(iv, pframe + prxattrib->hdrlen, iv_len);
			memcpy(icv, pframe + skb->len - icv_len, icv_len);

			crypto_private = crypto_ops->init(key_idx);
			if (!crypto_private) {
				res = _FAIL;
				goto exit_lib80211_ccmp;
			}
			if (crypto_ops->set_key(key, key_length, NULL, crypto_private) < 0) {
				res = _FAIL;
				goto exit_lib80211_ccmp;
			}
			if (crypto_ops->decrypt_mpdu(skb, prxattrib->hdrlen, crypto_private)) {
				res = _FAIL;
				goto exit_lib80211_ccmp;
			}

			memmove(pframe, pframe + iv_len, prxattrib->hdrlen);
			skb_push(skb, iv_len);
			skb_put(skb, icv_len);

			memcpy(pframe + prxattrib->hdrlen, iv, iv_len);
			memcpy(pframe + skb->len - icv_len, icv, icv_len);

exit_lib80211_ccmp:
			if (crypto_ops && crypto_private)
				crypto_ops->deinit(crypto_private);
		} else {
			RT_TRACE(_module_rtl871x_security_c_, _drv_err_, ("rtw_aes_encrypt: stainfo==NULL!!!\n"));
			res = _FAIL;
		}
	}
exit:
	return res;
}

/* AES tables*/
const u32 Te0[256] = {
	0xc66363a5U, 0xf87c7c84U, 0xee777799U, 0xf67b7b8dU,
	0xfff2f20dU, 0xd66b6bbdU, 0xde6f6fb1U, 0x91c5c554U,
	0x60303050U, 0x02010103U, 0xce6767a9U, 0x562b2b7dU,
	0xe7fefe19U, 0xb5d7d762U, 0x4dababe6U, 0xec76769aU,
	0x8fcaca45U, 0x1f82829dU, 0x89c9c940U, 0xfa7d7d87U,
	0xeffafa15U, 0xb25959ebU, 0x8e4747c9U, 0xfbf0f00bU,
	0x41adadecU, 0xb3d4d467U, 0x5fa2a2fdU, 0x45afafeaU,
	0x239c9cbfU, 0x53a4a4f7U, 0xe4727296U, 0x9bc0c05bU,
	0x75b7b7c2U, 0xe1fdfd1cU, 0x3d9393aeU, 0x4c26266aU,
	0x6c36365aU, 0x7e3f3f41U, 0xf5f7f702U, 0x83cccc4fU,
	0x6834345cU, 0x51a5a5f4U, 0xd1e5e534U, 0xf9f1f108U,
	0xe2717193U, 0xabd8d873U, 0x62313153U, 0x2a15153fU,
	0x0804040cU, 0x95c7c752U, 0x46232365U, 0x9dc3c35eU,
	0x30181828U, 0x379696a1U, 0x0a05050fU, 0x2f9a9ab5U,
	0x0e070709U, 0x24121236U, 0x1b80809bU, 0xdfe2e23dU,
	0xcdebeb26U, 0x4e272769U, 0x7fb2b2cdU, 0xea75759fU,
	0x1209091bU, 0x1d83839eU, 0x582c2c74U, 0x341a1a2eU,
	0x361b1b2dU, 0xdc6e6eb2U, 0xb45a5aeeU, 0x5ba0a0fbU,
	0xa45252f6U, 0x763b3b4dU, 0xb7d6d661U, 0x7db3b3ceU,
	0x5229297bU, 0xdde3e33eU, 0x5e2f2f71U, 0x13848497U,
	0xa65353f5U, 0xb9d1d168U, 0x00000000U, 0xc1eded2cU,
	0x40202060U, 0xe3fcfc1fU, 0x79b1b1c8U, 0xb65b5bedU,
	0xd46a6abeU, 0x8dcbcb46U, 0x67bebed9U, 0x7239394bU,
	0x944a4adeU, 0x984c4cd4U, 0xb05858e8U, 0x85cfcf4aU,
	0xbbd0d06bU, 0xc5efef2aU, 0x4faaaae5U, 0xedfbfb16U,
	0x864343c5U, 0x9a4d4dd7U, 0x66333355U, 0x11858594U,
	0x8a4545cfU, 0xe9f9f910U, 0x04020206U, 0xfe7f7f81U,
	0xa05050f0U, 0x783c3c44U, 0x259f9fbaU, 0x4ba8a8e3U,
	0xa25151f3U, 0x5da3a3feU, 0x804040c0U, 0x058f8f8aU,
	0x3f9292adU, 0x219d9dbcU, 0x70383848U, 0xf1f5f504U,
	0x63bcbcdfU, 0x77b6b6c1U, 0xafdada75U, 0x42212163U,
	0x20101030U, 0xe5ffff1aU, 0xfdf3f30eU, 0xbfd2d26dU,
	0x81cdcd4cU, 0x180c0c14U, 0x26131335U, 0xc3ecec2fU,
	0xbe5f5fe1U, 0x359797a2U, 0x884444ccU, 0x2e171739U,
	0x93c4c457U, 0x55a7a7f2U, 0xfc7e7e82U, 0x7a3d3d47U,
	0xc86464acU, 0xba5d5de7U, 0x3219192bU, 0xe6737395U,
	0xc06060a0U, 0x19818198U, 0x9e4f4fd1U, 0xa3dcdc7fU,
	0x44222266U, 0x542a2a7eU, 0x3b9090abU, 0x0b888883U,
	0x8c4646caU, 0xc7eeee29U, 0x6bb8b8d3U, 0x2814143cU,
	0xa7dede79U, 0xbc5e5ee2U, 0x160b0b1dU, 0xaddbdb76U,
	0xdbe0e03bU, 0x64323256U, 0x743a3a4eU, 0x140a0a1eU,
	0x924949dbU, 0x0c06060aU, 0x4824246cU, 0xb85c5ce4U,
	0x9fc2c25dU, 0xbdd3d36eU, 0x43acacefU, 0xc46262a6U,
	0x399191a8U, 0x319595a4U, 0xd3e4e437U, 0xf279798bU,
	0xd5e7e732U, 0x8bc8c843U, 0x6e373759U, 0xda6d6db7U,
	0x018d8d8cU, 0xb1d5d564U, 0x9c4e4ed2U, 0x49a9a9e0U,
	0xd86c6cb4U, 0xac5656faU, 0xf3f4f407U, 0xcfeaea25U,
	0xca6565afU, 0xf47a7a8eU, 0x47aeaee9U, 0x10080818U,
	0x6fbabad5U, 0xf0787888U, 0x4a25256fU, 0x5c2e2e72U,
	0x381c1c24U, 0x57a6a6f1U, 0x73b4b4c7U, 0x97c6c651U,
	0xcbe8e823U, 0xa1dddd7cU, 0xe874749cU, 0x3e1f1f21U,
	0x964b4bddU, 0x61bdbddcU, 0x0d8b8b86U, 0x0f8a8a85U,
	0xe0707090U, 0x7c3e3e42U, 0x71b5b5c4U, 0xcc6666aaU,
	0x904848d8U, 0x06030305U, 0xf7f6f601U, 0x1c0e0e12U,
	0xc26161a3U, 0x6a35355fU, 0xae5757f9U, 0x69b9b9d0U,
	0x17868691U, 0x99c1c158U, 0x3a1d1d27U, 0x279e9eb9U,
	0xd9e1e138U, 0xebf8f813U, 0x2b9898b3U, 0x22111133U,
	0xd26969bbU, 0xa9d9d970U, 0x078e8e89U, 0x339494a7U,
	0x2d9b9bb6U, 0x3c1e1e22U, 0x15878792U, 0xc9e9e920U,
	0x87cece49U, 0xaa5555ffU, 0x50282878U, 0xa5dfdf7aU,
	0x038c8c8fU, 0x59a1a1f8U, 0x09898980U, 0x1a0d0d17U,
	0x65bfbfdaU, 0xd7e6e631U, 0x844242c6U, 0xd06868b8U,
	0x824141c3U, 0x299999b0U, 0x5a2d2d77U, 0x1e0f0f11U,
	0x7bb0b0cbU, 0xa85454fcU, 0x6dbbbbd6U, 0x2c16163aU,
};

const u32 Td0[256] = {
	0x51f4a750U, 0x7e416553U, 0x1a17a4c3U, 0x3a275e96U,
	0x3bab6bcbU, 0x1f9d45f1U, 0xacfa58abU, 0x4be30393U,
	0x2030fa55U, 0xad766df6U, 0x88cc7691U, 0xf5024c25U,
	0x4fe5d7fcU, 0xc52acbd7U, 0x26354480U, 0xb562a38fU,
	0xdeb15a49U, 0x25ba1b67U, 0x45ea0e98U, 0x5dfec0e1U,
	0xc32f7502U, 0x814cf012U, 0x8d4697a3U, 0x6bd3f9c6U,
	0x038f5fe7U, 0x15929c95U, 0xbf6d7aebU, 0x955259daU,
	0xd4be832dU, 0x587421d3U, 0x49e06929U, 0x8ec9c844U,
	0x75c2896aU, 0xf48e7978U, 0x99583e6bU, 0x27b971ddU,
	0xbee14fb6U, 0xf088ad17U, 0xc920ac66U, 0x7dce3ab4U,
	0x63df4a18U, 0xe51a3182U, 0x97513360U, 0x62537f45U,
	0xb16477e0U, 0xbb6bae84U, 0xfe81a01cU, 0xf9082b94U,
	0x70486858U, 0x8f45fd19U, 0x94de6c87U, 0x527bf8b7U,
	0xab73d323U, 0x724b02e2U, 0xe31f8f57U, 0x6655ab2aU,
	0xb2eb2807U, 0x2fb5c203U, 0x86c57b9aU, 0xd33708a5U,
	0x302887f2U, 0x23bfa5b2U, 0x02036abaU, 0xed16825cU,
	0x8acf1c2bU, 0xa779b492U, 0xf307f2f0U, 0x4e69e2a1U,
	0x65daf4cdU, 0x0605bed5U, 0xd134621fU, 0xc4a6fe8aU,
	0x342e539dU, 0xa2f355a0U, 0x058ae132U, 0xa4f6eb75U,
	0x0b83ec39U, 0x4060efaaU, 0x5e719f06U, 0xbd6e1051U,
	0x3e218af9U, 0x96dd063dU, 0xdd3e05aeU, 0x4de6bd46U,
	0x91548db5U, 0x71c45d05U, 0x0406d46fU, 0x605015ffU,
	0x1998fb24U, 0xd6bde997U, 0x894043ccU, 0x67d99e77U,
	0xb0e842bdU, 0x07898b88U, 0xe7195b38U, 0x79c8eedbU,
	0xa17c0a47U, 0x7c420fe9U, 0xf8841ec9U, 0x00000000U,
	0x09808683U, 0x322bed48U, 0x1e1170acU, 0x6c5a724eU,
	0xfd0efffbU, 0x0f853856U, 0x3daed51eU, 0x362d3927U,
	0x0a0fd964U, 0x685ca621U, 0x9b5b54d1U, 0x24362e3aU,
	0x0c0a67b1U, 0x9357e70fU, 0xb4ee96d2U, 0x1b9b919eU,
	0x80c0c54fU, 0x61dc20a2U, 0x5a774b69U, 0x1c121a16U,
	0xe293ba0aU, 0xc0a02ae5U, 0x3c22e043U, 0x121b171dU,
	0x0e090d0bU, 0xf28bc7adU, 0x2db6a8b9U, 0x141ea9c8U,
	0x57f11985U, 0xaf75074cU, 0xee99ddbbU, 0xa37f60fdU,
	0xf701269fU, 0x5c72f5bcU, 0x44663bc5U, 0x5bfb7e34U,
	0x8b432976U, 0xcb23c6dcU, 0xb6edfc68U, 0xb8e4f163U,
	0xd731dccaU, 0x42638510U, 0x13972240U, 0x84c61120U,
	0x854a247dU, 0xd2bb3df8U, 0xaef93211U, 0xc729a16dU,
	0x1d9e2f4bU, 0xdcb230f3U, 0x0d8652ecU, 0x77c1e3d0U,
	0x2bb3166cU, 0xa970b999U, 0x119448faU, 0x47e96422U,
	0xa8fc8cc4U, 0xa0f03f1aU, 0x567d2cd8U, 0x223390efU,
	0x87494ec7U, 0xd938d1c1U, 0x8ccaa2feU, 0x98d40b36U,
	0xa6f581cfU, 0xa57ade28U, 0xdab78e26U, 0x3fadbfa4U,
	0x2c3a9de4U, 0x5078920dU, 0x6a5fcc9bU, 0x547e4662U,
	0xf68d13c2U, 0x90d8b8e8U, 0x2e39f75eU, 0x82c3aff5U,
	0x9f5d80beU, 0x69d0937cU, 0x6fd52da9U, 0xcf2512b3U,
	0xc8ac993bU, 0x10187da7U, 0xe89c636eU, 0xdb3bbb7bU,
	0xcd267809U, 0x6e5918f4U, 0xec9ab701U, 0x834f9aa8U,
	0xe6956e65U, 0xaaffe67eU, 0x21bccf08U, 0xef15e8e6U,
	0xbae79bd9U, 0x4a6f36ceU, 0xea9f09d4U, 0x29b07cd6U,
	0x31a4b2afU, 0x2a3f2331U, 0xc6a59430U, 0x35a266c0U,
	0x744ebc37U, 0xfc82caa6U, 0xe090d0b0U, 0x33a7d815U,
	0xf104984aU, 0x41ecdaf7U, 0x7fcd500eU, 0x1791f62fU,
	0x764dd68dU, 0x43efb04dU, 0xccaa4d54U, 0xe49604dfU,
	0x9ed1b5e3U, 0x4c6a881bU, 0xc12c1fb8U, 0x4665517fU,
	0x9d5eea04U, 0x018c355dU, 0xfa877473U, 0xfb0b412eU,
	0xb3671d5aU, 0x92dbd252U, 0xe9105633U, 0x6dd64713U,
	0x9ad7618cU, 0x37a10c7aU, 0x59f8148eU, 0xeb133c89U,
	0xcea927eeU, 0xb761c935U, 0xe11ce5edU, 0x7a47b13cU,
	0x9cd2df59U, 0x55f2733fU, 0x1814ce79U, 0x73c737bfU,
	0x53f7cdeaU, 0x5ffdaa5bU, 0xdf3d6f14U, 0x7844db86U,
	0xcaaff381U, 0xb968c43eU, 0x3824342cU, 0xc2a3405fU,
	0x161dc372U, 0xbce2250cU, 0x283c498bU, 0xff0d9541U,
	0x39a80171U, 0x080cb3deU, 0xd8b4e49cU, 0x6456c190U,
	0x7bcb8461U, 0xd532b670U, 0x486c5c74U, 0xd0b85742U,
};

const u8 Td4s[256] = {
	0x52U, 0x09U, 0x6aU, 0xd5U, 0x30U, 0x36U, 0xa5U, 0x38U,
	0xbfU, 0x40U, 0xa3U, 0x9eU, 0x81U, 0xf3U, 0xd7U, 0xfbU,
	0x7cU, 0xe3U, 0x39U, 0x82U, 0x9bU, 0x2fU, 0xffU, 0x87U,
	0x34U, 0x8eU, 0x43U, 0x44U, 0xc4U, 0xdeU, 0xe9U, 0xcbU,
	0x54U, 0x7bU, 0x94U, 0x32U, 0xa6U, 0xc2U, 0x23U, 0x3dU,
	0xeeU, 0x4cU, 0x95U, 0x0bU, 0x42U, 0xfaU, 0xc3U, 0x4eU,
	0x08U, 0x2eU, 0xa1U, 0x66U, 0x28U, 0xd9U, 0x24U, 0xb2U,
	0x76U, 0x5bU, 0xa2U, 0x49U, 0x6dU, 0x8bU, 0xd1U, 0x25U,
	0x72U, 0xf8U, 0xf6U, 0x64U, 0x86U, 0x68U, 0x98U, 0x16U,
	0xd4U, 0xa4U, 0x5cU, 0xccU, 0x5dU, 0x65U, 0xb6U, 0x92U,
	0x6cU, 0x70U, 0x48U, 0x50U, 0xfdU, 0xedU, 0xb9U, 0xdaU,
	0x5eU, 0x15U, 0x46U, 0x57U, 0xa7U, 0x8dU, 0x9dU, 0x84U,
	0x90U, 0xd8U, 0xabU, 0x00U, 0x8cU, 0xbcU, 0xd3U, 0x0aU,
	0xf7U, 0xe4U, 0x58U, 0x05U, 0xb8U, 0xb3U, 0x45U, 0x06U,
	0xd0U, 0x2cU, 0x1eU, 0x8fU, 0xcaU, 0x3fU, 0x0fU, 0x02U,
	0xc1U, 0xafU, 0xbdU, 0x03U, 0x01U, 0x13U, 0x8aU, 0x6bU,
	0x3aU, 0x91U, 0x11U, 0x41U, 0x4fU, 0x67U, 0xdcU, 0xeaU,
	0x97U, 0xf2U, 0xcfU, 0xceU, 0xf0U, 0xb4U, 0xe6U, 0x73U,
	0x96U, 0xacU, 0x74U, 0x22U, 0xe7U, 0xadU, 0x35U, 0x85U,
	0xe2U, 0xf9U, 0x37U, 0xe8U, 0x1cU, 0x75U, 0xdfU, 0x6eU,
	0x47U, 0xf1U, 0x1aU, 0x71U, 0x1dU, 0x29U, 0xc5U, 0x89U,
	0x6fU, 0xb7U, 0x62U, 0x0eU, 0xaaU, 0x18U, 0xbeU, 0x1bU,
	0xfcU, 0x56U, 0x3eU, 0x4bU, 0xc6U, 0xd2U, 0x79U, 0x20U,
	0x9aU, 0xdbU, 0xc0U, 0xfeU, 0x78U, 0xcdU, 0x5aU, 0xf4U,
	0x1fU, 0xddU, 0xa8U, 0x33U, 0x88U, 0x07U, 0xc7U, 0x31U,
	0xb1U, 0x12U, 0x10U, 0x59U, 0x27U, 0x80U, 0xecU, 0x5fU,
	0x60U, 0x51U, 0x7fU, 0xa9U, 0x19U, 0xb5U, 0x4aU, 0x0dU,
	0x2dU, 0xe5U, 0x7aU, 0x9fU, 0x93U, 0xc9U, 0x9cU, 0xefU,
	0xa0U, 0xe0U, 0x3bU, 0x4dU, 0xaeU, 0x2aU, 0xf5U, 0xb0U,
	0xc8U, 0xebU, 0xbbU, 0x3cU, 0x83U, 0x53U, 0x99U, 0x61U,
	0x17U, 0x2bU, 0x04U, 0x7eU, 0xbaU, 0x77U, 0xd6U, 0x26U,
	0xe1U, 0x69U, 0x14U, 0x63U, 0x55U, 0x21U, 0x0cU, 0x7dU,
};
const u8 rcons[] = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36
	/* for 128-bit blocks, Rijndael never uses more than 10 rcon values */
};

/**
 * Expand the cipher key into the encryption key schedule.
 *
 * @return	the number of rounds for the given cipher key size.
 */
#define ROUND(i, d, s) \
do {									\
	d##0 = TE0(s##0) ^ TE1(s##1) ^ TE2(s##2) ^ TE3(s##3) ^ rk[4 * i]; \
	d##1 = TE0(s##1) ^ TE1(s##2) ^ TE2(s##3) ^ TE3(s##0) ^ rk[4 * i + 1]; \
	d##2 = TE0(s##2) ^ TE1(s##3) ^ TE2(s##0) ^ TE3(s##1) ^ rk[4 * i + 2]; \
	d##3 = TE0(s##3) ^ TE1(s##0) ^ TE2(s##1) ^ TE3(s##2) ^ rk[4 * i + 3]; \
} while (0)
