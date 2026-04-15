// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#include <crypto/aead.h>
#include <linux/skbuff.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/udp.h>

#include "ovpnpriv.h"
#include "main.h"
#include "io.h"
#include "pktid.h"
#include "crypto_aead.h"
#include "crypto.h"
#include "peer.h"
#include "proto.h"
#include "skb.h"

#define OVPN_AUTH_TAG_SIZE	16
#define OVPN_AAD_SIZE		(OVPN_OPCODE_SIZE + OVPN_NONCE_WIRE_SIZE)

#define ALG_NAME_AES		"gcm(aes)"
#define ALG_NAME_CHACHAPOLY	"rfc7539(chacha20,poly1305)"

static int ovpn_aead_encap_overhead(const struct ovpn_crypto_key_slot *ks)
{
	return  OVPN_OPCODE_SIZE +			/* OP header size */
		sizeof(u32) +				/* Packet ID */
		crypto_aead_authsize(ks->encrypt);	/* Auth Tag */
}

/**
 * ovpn_aead_crypto_tmp_size - compute the size of a temporary object containing
 *			       an AEAD request structure with extra space for SG
 *			       and IV.
 * @tfm: the AEAD cipher handle
 * @nfrags: the number of fragments in the skb
 *
 * This function calculates the size of a contiguous memory block that includes
 * the initialization vector (IV), the AEAD request, and an array of scatterlist
 * entries. For alignment considerations, the IV is placed first, followed by
 * the request, and then the scatterlist.
 * Additional alignment is applied according to the requirements of the
 * underlying structures.
 *
 * Return: the size of the temporary memory that needs to be allocated
 */
static unsigned int ovpn_aead_crypto_tmp_size(struct crypto_aead *tfm,
					      const unsigned int nfrags)
{
	unsigned int len = OVPN_NONCE_SIZE;

	DEBUG_NET_WARN_ON_ONCE(crypto_aead_ivsize(tfm) != OVPN_NONCE_SIZE);

	/* min size for a buffer of ivsize, aligned to alignmask */
	len += crypto_aead_alignmask(tfm) & ~(crypto_tfm_ctx_alignment() - 1);
	/* round up to the next multiple of the crypto ctx alignment */
	len = ALIGN(len, crypto_tfm_ctx_alignment());

	/* reserve space for the AEAD request */
	len += sizeof(struct aead_request) + crypto_aead_reqsize(tfm);
	/* round up to the next multiple of the scatterlist alignment */
	len = ALIGN(len, __alignof__(struct scatterlist));

	/* add enough space for nfrags + 2 scatterlist entries */
	len += array_size(sizeof(struct scatterlist), nfrags + 2);
	return len;
}

/**
 * ovpn_aead_crypto_tmp_iv - retrieve the pointer to the IV within a temporary
 *			     buffer allocated using ovpn_aead_crypto_tmp_size
 * @aead: the AEAD cipher handle
 * @tmp: a pointer to the beginning of the temporary buffer
 *
 * This function retrieves a pointer to the initialization vector (IV) in the
 * temporary buffer. If the AEAD cipher specifies an IV size, the pointer is
 * adjusted using the AEAD's alignment mask to ensure proper alignment.
 *
 * Returns: a pointer to the IV within the temporary buffer
 */
static u8 *ovpn_aead_crypto_tmp_iv(struct crypto_aead *aead, void *tmp)
{
	return likely(crypto_aead_ivsize(aead)) ?
		      PTR_ALIGN((u8 *)tmp, crypto_aead_alignmask(aead) + 1) :
		      tmp;
}

/**
 * ovpn_aead_crypto_tmp_req - retrieve the pointer to the AEAD request structure
 *			      within a temporary buffer allocated using
 *			      ovpn_aead_crypto_tmp_size
 * @aead: the AEAD cipher handle
 * @iv: a pointer to the initialization vector in the temporary buffer
 *
 * This function computes the location of the AEAD request structure that
 * immediately follows the IV in the temporary buffer and it ensures the request
 * is aligned to the crypto transform context alignment.
 *
 * Returns: a pointer to the AEAD request structure
 */
static struct aead_request *ovpn_aead_crypto_tmp_req(struct crypto_aead *aead,
						     const u8 *iv)
{
	return (void *)PTR_ALIGN(iv + crypto_aead_ivsize(aead),
				 crypto_tfm_ctx_alignment());
}

/**
 * ovpn_aead_crypto_req_sg - locate the scatterlist following the AEAD request
 *			     within a temporary buffer allocated using
 *			     ovpn_aead_crypto_tmp_size
 * @aead: the AEAD cipher handle
 * @req: a pointer to the AEAD request structure in the temporary buffer
 *
 * This function computes the starting address of the scatterlist that is
 * allocated immediately after the AEAD request structure. It aligns the pointer
 * based on the alignment requirements of the scatterlist structure.
 *
 * Returns: a pointer to the scatterlist
 */
static struct scatterlist *ovpn_aead_crypto_req_sg(struct crypto_aead *aead,
						   struct aead_request *req)
{
	return (void *)ALIGN((unsigned long)(req + 1) +
			     crypto_aead_reqsize(aead),
			     __alignof__(struct scatterlist));
}

int ovpn_aead_encrypt(struct ovpn_peer *peer, struct ovpn_crypto_key_slot *ks,
		      struct sk_buff *skb)
{
	const unsigned int tag_size = crypto_aead_authsize(ks->encrypt);
	struct aead_request *req;
	struct sk_buff *trailer;
	struct scatterlist *sg;
	int nfrags, ret;
	u32 pktid, op;
	void *tmp;
	u8 *iv;

	ovpn_skb_cb(skb)->peer = peer;
	ovpn_skb_cb(skb)->ks = ks;

	/* Sample AEAD header format:
	 * 48000001 00000005 7e7046bd 444a7e28 cc6387b1 64a4d6c1 380275a...
	 * [ OP32 ] [seq # ] [             auth tag            ] [ payload ... ]
	 *          [4-byte
	 *          IV head]
	 */

	/* check that there's enough headroom in the skb for packet
	 * encapsulation
	 */
	if (unlikely(skb_cow_head(skb, OVPN_HEAD_ROOM)))
		return -ENOBUFS;

	/* get number of skb frags and ensure that packet data is writable */
	nfrags = skb_cow_data(skb, 0, &trailer);
	if (unlikely(nfrags < 0))
		return nfrags;

	if (unlikely(nfrags + 2 > (MAX_SKB_FRAGS + 2)))
		return -ENOSPC;

	/* allocate temporary memory for iv, sg and req */
	tmp = kmalloc(ovpn_aead_crypto_tmp_size(ks->encrypt, nfrags),
		      GFP_ATOMIC);
	if (unlikely(!tmp))
		return -ENOMEM;

	ovpn_skb_cb(skb)->crypto_tmp = tmp;

	iv = ovpn_aead_crypto_tmp_iv(ks->encrypt, tmp);
	req = ovpn_aead_crypto_tmp_req(ks->encrypt, iv);
	sg = ovpn_aead_crypto_req_sg(ks->encrypt, req);

	/* sg table:
	 * 0: op, wire nonce (AD, len=OVPN_OP_SIZE_V2+OVPN_NONCE_WIRE_SIZE),
	 * 1, 2, 3, ..., n: payload,
	 * n+1: auth_tag (len=tag_size)
	 */
	sg_init_table(sg, nfrags + 2);

	/* build scatterlist to encrypt packet payload */
	ret = skb_to_sgvec_nomark(skb, sg + 1, 0, skb->len);
	if (unlikely(ret < 0)) {
		netdev_err(peer->ovpn->dev,
			   "encrypt: cannot map skb to sg: %d\n", ret);
		return ret;
	}

	/* append auth_tag onto scatterlist */
	__skb_push(skb, tag_size);
	sg_set_buf(sg + ret + 1, skb->data, tag_size);

	/* obtain packet ID, which is used both as a first
	 * 4 bytes of nonce and last 4 bytes of associated data.
	 */
	ret = ovpn_pktid_xmit_next(&ks->pid_xmit, &pktid);
	if (unlikely(ret < 0))
		return ret;

	/* concat 4 bytes packet id and 8 bytes nonce tail into 12 bytes
	 * nonce
	 */
	ovpn_pktid_aead_write(pktid, ks->nonce_tail_xmit, iv);

	/* make space for packet id and push it to the front */
	__skb_push(skb, OVPN_NONCE_WIRE_SIZE);
	memcpy(skb->data, iv, OVPN_NONCE_WIRE_SIZE);

	/* add packet op as head of additional data */
	op = ovpn_opcode_compose(OVPN_DATA_V2, ks->key_id, peer->tx_id);
	__skb_push(skb, OVPN_OPCODE_SIZE);
	BUILD_BUG_ON(sizeof(op) != OVPN_OPCODE_SIZE);
	*((__force __be32 *)skb->data) = htonl(op);

	/* AEAD Additional data */
	sg_set_buf(sg, skb->data, OVPN_AAD_SIZE);

	/* setup async crypto operation */
	aead_request_set_tfm(req, ks->encrypt);
	aead_request_set_callback(req, 0, ovpn_encrypt_post, skb);
	aead_request_set_crypt(req, sg, sg,
			       skb->len - ovpn_aead_encap_overhead(ks), iv);
	aead_request_set_ad(req, OVPN_AAD_SIZE);

	/* encrypt it */
	return crypto_aead_encrypt(req);
}

int ovpn_aead_decrypt(struct ovpn_peer *peer, struct ovpn_crypto_key_slot *ks,
		      struct sk_buff *skb)
{
	const unsigned int tag_size = crypto_aead_authsize(ks->decrypt);
	int ret, payload_len, nfrags;
	unsigned int payload_offset;
	struct aead_request *req;
	struct sk_buff *trailer;
	struct scatterlist *sg;
	void *tmp;
	u8 *iv;

	payload_offset = OVPN_AAD_SIZE + tag_size;
	payload_len = skb->len - payload_offset;

	ovpn_skb_cb(skb)->payload_offset = payload_offset;
	ovpn_skb_cb(skb)->peer = peer;
	ovpn_skb_cb(skb)->ks = ks;

	/* sanity check on packet size, payload size must be >= 0 */
	if (unlikely(payload_len < 0))
		return -EINVAL;

	/* Prepare the skb data buffer to be accessed up until the auth tag.
	 * This is required because this area is directly mapped into the sg
	 * list.
	 */
	if (unlikely(!pskb_may_pull(skb, payload_offset)))
		return -ENODATA;

	/* get number of skb frags and ensure that packet data is writable */
	nfrags = skb_cow_data(skb, 0, &trailer);
	if (unlikely(nfrags < 0))
		return nfrags;

	if (unlikely(nfrags + 2 > (MAX_SKB_FRAGS + 2)))
		return -ENOSPC;

	/* allocate temporary memory for iv, sg and req */
	tmp = kmalloc(ovpn_aead_crypto_tmp_size(ks->decrypt, nfrags),
		      GFP_ATOMIC);
	if (unlikely(!tmp))
		return -ENOMEM;

	ovpn_skb_cb(skb)->crypto_tmp = tmp;

	iv = ovpn_aead_crypto_tmp_iv(ks->decrypt, tmp);
	req = ovpn_aead_crypto_tmp_req(ks->decrypt, iv);
	sg = ovpn_aead_crypto_req_sg(ks->decrypt, req);

	/* sg table:
	 * 0: op, wire nonce (AD, len=OVPN_OPCODE_SIZE+OVPN_NONCE_WIRE_SIZE),
	 * 1, 2, 3, ..., n: payload,
	 * n+1: auth_tag (len=tag_size)
	 */
	sg_init_table(sg, nfrags + 2);

	/* packet op is head of additional data */
	sg_set_buf(sg, skb->data, OVPN_AAD_SIZE);

	/* build scatterlist to decrypt packet payload */
	ret = skb_to_sgvec_nomark(skb, sg + 1, payload_offset, payload_len);
	if (unlikely(ret < 0)) {
		netdev_err(peer->ovpn->dev,
			   "decrypt: cannot map skb to sg: %d\n", ret);
		return ret;
	}

	/* append auth_tag onto scatterlist */
	sg_set_buf(sg + ret + 1, skb->data + OVPN_AAD_SIZE, tag_size);

	/* copy nonce into IV buffer */
	memcpy(iv, skb->data + OVPN_OPCODE_SIZE, OVPN_NONCE_WIRE_SIZE);
	memcpy(iv + OVPN_NONCE_WIRE_SIZE, ks->nonce_tail_recv,
	       OVPN_NONCE_TAIL_SIZE);

	/* setup async crypto operation */
	aead_request_set_tfm(req, ks->decrypt);
	aead_request_set_callback(req, 0, ovpn_decrypt_post, skb);
	aead_request_set_crypt(req, sg, sg, payload_len + tag_size, iv);

	aead_request_set_ad(req, OVPN_AAD_SIZE);

	/* decrypt it */
	return crypto_aead_decrypt(req);
}

/* Initialize a struct crypto_aead object */
static struct crypto_aead *ovpn_aead_init(const char *title,
					  const char *alg_name,
					  const unsigned char *key,
					  unsigned int keylen)
{
	struct crypto_aead *aead;
	int ret;

	aead = crypto_alloc_aead(alg_name, 0, 0);
	if (IS_ERR(aead)) {
		ret = PTR_ERR(aead);
		pr_err("%s crypto_alloc_aead failed, err=%d\n", title, ret);
		aead = NULL;
		goto error;
	}

	ret = crypto_aead_setkey(aead, key, keylen);
	if (ret) {
		pr_err("%s crypto_aead_setkey size=%u failed, err=%d\n", title,
		       keylen, ret);
		goto error;
	}

	ret = crypto_aead_setauthsize(aead, OVPN_AUTH_TAG_SIZE);
	if (ret) {
		pr_err("%s crypto_aead_setauthsize failed, err=%d\n", title,
		       ret);
		goto error;
	}

	/* basic AEAD assumption
	 * all current algorithms use OVPN_NONCE_SIZE.
	 * ovpn_aead_crypto_tmp_size and ovpn_aead_encrypt/decrypt
	 * expect this.
	 */
	if (crypto_aead_ivsize(aead) != OVPN_NONCE_SIZE) {
		pr_err("%s IV size must be %d\n", title, OVPN_NONCE_SIZE);
		ret = -EINVAL;
		goto error;
	}

	pr_debug("********* Cipher %s (%s)\n", alg_name, title);
	pr_debug("*** IV size=%u\n", crypto_aead_ivsize(aead));
	pr_debug("*** req size=%u\n", crypto_aead_reqsize(aead));
	pr_debug("*** block size=%u\n", crypto_aead_blocksize(aead));
	pr_debug("*** auth size=%u\n", crypto_aead_authsize(aead));
	pr_debug("*** alignmask=0x%x\n", crypto_aead_alignmask(aead));

	return aead;

error:
	crypto_free_aead(aead);
	return ERR_PTR(ret);
}

void ovpn_aead_crypto_key_slot_destroy(struct ovpn_crypto_key_slot *ks)
{
	if (!ks)
		return;

	crypto_free_aead(ks->encrypt);
	crypto_free_aead(ks->decrypt);
	kfree(ks);
}

struct ovpn_crypto_key_slot *
ovpn_aead_crypto_key_slot_new(const struct ovpn_key_config *kc)
{
	struct ovpn_crypto_key_slot *ks = NULL;
	const char *alg_name;
	int ret;

	/* validate crypto alg */
	switch (kc->cipher_alg) {
	case OVPN_CIPHER_ALG_AES_GCM:
		alg_name = ALG_NAME_AES;
		break;
	case OVPN_CIPHER_ALG_CHACHA20_POLY1305:
		alg_name = ALG_NAME_CHACHAPOLY;
		break;
	default:
		return ERR_PTR(-EOPNOTSUPP);
	}

	if (kc->encrypt.nonce_tail_size != OVPN_NONCE_TAIL_SIZE ||
	    kc->decrypt.nonce_tail_size != OVPN_NONCE_TAIL_SIZE)
		return ERR_PTR(-EINVAL);

	/* build the key slot */
	ks = kmalloc_obj(*ks);
	if (!ks)
		return ERR_PTR(-ENOMEM);

	ks->encrypt = NULL;
	ks->decrypt = NULL;
	kref_init(&ks->refcount);
	ks->key_id = kc->key_id;

	ks->encrypt = ovpn_aead_init("encrypt", alg_name,
				     kc->encrypt.cipher_key,
				     kc->encrypt.cipher_key_size);
	if (IS_ERR(ks->encrypt)) {
		ret = PTR_ERR(ks->encrypt);
		ks->encrypt = NULL;
		goto destroy_ks;
	}

	ks->decrypt = ovpn_aead_init("decrypt", alg_name,
				     kc->decrypt.cipher_key,
				     kc->decrypt.cipher_key_size);
	if (IS_ERR(ks->decrypt)) {
		ret = PTR_ERR(ks->decrypt);
		ks->decrypt = NULL;
		goto destroy_ks;
	}

	memcpy(ks->nonce_tail_xmit, kc->encrypt.nonce_tail,
	       OVPN_NONCE_TAIL_SIZE);
	memcpy(ks->nonce_tail_recv, kc->decrypt.nonce_tail,
	       OVPN_NONCE_TAIL_SIZE);

	/* init packet ID generation/validation */
	ovpn_pktid_xmit_init(&ks->pid_xmit);
	ovpn_pktid_recv_init(&ks->pid_recv);

	return ks;

destroy_ks:
	ovpn_aead_crypto_key_slot_destroy(ks);
	return ERR_PTR(ret);
}

enum ovpn_cipher_alg ovpn_aead_crypto_alg(struct ovpn_crypto_key_slot *ks)
{
	const char *alg_name;

	if (!ks->encrypt)
		return OVPN_CIPHER_ALG_NONE;

	alg_name = crypto_tfm_alg_name(crypto_aead_tfm(ks->encrypt));

	if (!strcmp(alg_name, ALG_NAME_AES))
		return OVPN_CIPHER_ALG_AES_GCM;
	else if (!strcmp(alg_name, ALG_NAME_CHACHAPOLY))
		return OVPN_CIPHER_ALG_CHACHA20_POLY1305;
	else
		return OVPN_CIPHER_ALG_NONE;
}
