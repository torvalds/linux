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

u32	rtw_aes_encrypt(struct adapter *padapter, u8 *pxmitframe)
{
	int	curfragnum, length;
	u8	*pframe;	/*  *payload,*iv */
	u8   hw_hdr_offset = 0;
	struct	sta_info		*stainfo;
	struct	pkt_attrib	 *pattrib = &((struct xmit_frame *)pxmitframe)->attrib;
	struct	security_priv	*psecuritypriv = &padapter->securitypriv;
	struct	xmit_priv		*pxmitpriv = &padapter->xmitpriv;
	u32 res = _SUCCESS;
	void *crypto_private;
	struct sk_buff *skb;
	struct lib80211_crypto_ops *crypto_ops;
	const int key_idx = IS_MCAST(pattrib->ra) ? psecuritypriv->dot118021XGrpKeyid : 0;
	const int key_length = 16;
	u8 *key;

	if (((struct xmit_frame *)pxmitframe)->buf_addr == NULL)
		return _FAIL;

	hw_hdr_offset = TXDESC_SIZE +
		 (((struct xmit_frame *)pxmitframe)->pkt_offset * PACKET_OFFSET_SZ);

	pframe = ((struct xmit_frame *)pxmitframe)->buf_addr + hw_hdr_offset;

	/* 4 start to encrypt each fragment */
	if (pattrib->encrypt != _AES_)
		return res;

	if (pattrib->psta)
		stainfo = pattrib->psta;
	else
		stainfo = rtw_get_stainfo(&padapter->stapriv, &pattrib->ra[0]);

	if (!stainfo) {
		RT_TRACE(_module_rtl871x_security_c_, _drv_err_, ("%s: stainfo==NULL!!!\n", __func__));
		return _FAIL;
	}

	crypto_ops = try_then_request_module(lib80211_get_crypto_ops("CCMP"), "lib80211_crypt_ccmp");

	if (IS_MCAST(pattrib->ra))
		key = psecuritypriv->dot118021XGrpKey[key_idx].skey;
	else
		key = stainfo->dot118021x_UncstKey.skey;

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
		pframe = (u8 *)round_up((size_t)(pframe), 8);

		kfree_skb(skb);
	}

exit_crypto_ops_deinit:
	crypto_ops->deinit(crypto_private);

exit:
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
