/*
 *	digest support for NTP, MD5 and with OpenSSL more
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ntp_fp.h"
#include "ntp_string.h"
#include "ntp_stdlib.h"
#include "ntp.h"
#include "ntp_md5.h"	/* provides OpenSSL digest API */
#include "isc/string.h"

typedef struct {
	const void *	buf;
	size_t		len;
} robuffT;

typedef struct {
	void *		buf;
	size_t		len;
} rwbuffT;

#if defined(OPENSSL) && defined(ENABLE_CMAC)
static size_t
cmac_ctx_size(
	CMAC_CTX *	ctx)
{
	size_t mlen = 0;

	if (ctx) {
		EVP_CIPHER_CTX * 	cctx;
		if (NULL != (cctx = CMAC_CTX_get0_cipher_ctx (ctx)))
			mlen = EVP_CIPHER_CTX_block_size(cctx);
	}
	return mlen;
}
#endif /*OPENSSL && ENABLE_CMAC*/

static size_t
make_mac(
	const rwbuffT *	digest,
	int		ktype,
	const robuffT *	key,
	const robuffT *	msg)
{
	/*
	 * Compute digest of key concatenated with packet. Note: the
	 * key type and digest type have been verified when the key
	 * was created.
	 */
	size_t	retlen = 0;
	
#ifdef OPENSSL
	
	INIT_SSL();

	/* Check if CMAC key type specific code required */
#   ifdef ENABLE_CMAC
	if (ktype == NID_cmac) {
		CMAC_CTX *	ctx    = NULL;
		void const *	keyptr = key->buf;
		u_char		keybuf[AES_128_KEY_SIZE];

		/* adjust key size (zero padded buffer) if necessary */
		if (AES_128_KEY_SIZE > key->len) {
			memcpy(keybuf, keyptr, key->len);
			memset((keybuf + key->len), 0,
			       (AES_128_KEY_SIZE - key->len));
			keyptr = keybuf;
		}
		
		if (NULL == (ctx = CMAC_CTX_new())) {
			msyslog(LOG_ERR, "MAC encrypt: CMAC %s CTX new failed.", CMAC);
			goto cmac_fail;
		}
		if (!CMAC_Init(ctx, keyptr, AES_128_KEY_SIZE, EVP_aes_128_cbc(), NULL)) {
			msyslog(LOG_ERR, "MAC encrypt: CMAC %s Init failed.",    CMAC);
			goto cmac_fail;
		}
		if (cmac_ctx_size(ctx) > digest->len) {
			msyslog(LOG_ERR, "MAC encrypt: CMAC %s buf too small.",  CMAC);
			goto cmac_fail;
		}
		if (!CMAC_Update(ctx, msg->buf, msg->len)) {
			msyslog(LOG_ERR, "MAC encrypt: CMAC %s Update failed.",  CMAC);
			goto cmac_fail;
		}
		if (!CMAC_Final(ctx, digest->buf, &retlen)) {
			msyslog(LOG_ERR, "MAC encrypt: CMAC %s Final failed.",   CMAC);
			retlen = 0;
		}
	  cmac_fail:
		if (ctx)
			CMAC_CTX_cleanup(ctx);
	}
	else
#   endif /*ENABLE_CMAC*/
	{	/* generic MAC handling */
		EVP_MD_CTX *	ctx   = EVP_MD_CTX_new();
		u_int		uilen = 0;
		
		if ( ! ctx) {
			msyslog(LOG_ERR, "MAC encrypt: MAC %s Digest CTX new failed.",
				OBJ_nid2sn(ktype));
			goto mac_fail;
		}
		
           #ifdef EVP_MD_CTX_FLAG_NON_FIPS_ALLOW
		/* make sure MD5 is allowd */
		EVP_MD_CTX_set_flags(ctx, EVP_MD_CTX_FLAG_NON_FIPS_ALLOW);
           #endif
		/* [Bug 3457] DON'T use plain EVP_DigestInit! It would
		 * kill the flags! */
		if (!EVP_DigestInit_ex(ctx, EVP_get_digestbynid(ktype), NULL)) {
			msyslog(LOG_ERR, "MAC encrypt: MAC %s Digest Init failed.",
				OBJ_nid2sn(ktype));
			goto mac_fail;
		}
		if ((size_t)EVP_MD_CTX_size(ctx) > digest->len) {
			msyslog(LOG_ERR, "MAC encrypt: MAC %s buf too small.",
				OBJ_nid2sn(ktype));
			goto mac_fail;
		}
		if (!EVP_DigestUpdate(ctx, key->buf, (u_int)key->len)) {
			msyslog(LOG_ERR, "MAC encrypt: MAC %s Digest Update key failed.",
				OBJ_nid2sn(ktype));
			goto mac_fail;
		}
		if (!EVP_DigestUpdate(ctx, msg->buf, (u_int)msg->len)) {
			msyslog(LOG_ERR, "MAC encrypt: MAC %s Digest Update data failed.",
				OBJ_nid2sn(ktype));
			goto mac_fail;
		}
		if (!EVP_DigestFinal(ctx, digest->buf, &uilen)) {
			msyslog(LOG_ERR, "MAC encrypt: MAC %s Digest Final failed.",
				OBJ_nid2sn(ktype));
			uilen = 0;
		}
	  mac_fail:
		retlen = (size_t)uilen;
		
		if (ctx)
			EVP_MD_CTX_free(ctx);
	}

#else /* !OPENSSL follows */
	
	if (ktype == NID_md5)
	{
		EVP_MD_CTX *	ctx   = EVP_MD_CTX_new();
		u_int		uilen = 0;

		if (digest->len < 16) {
			msyslog(LOG_ERR, "%s", "MAC encrypt: MAC md5 buf too small.");
		}
		else if ( ! ctx) {
			msyslog(LOG_ERR, "%s", "MAC encrypt: MAC md5 Digest CTX new failed.");
		}
		else {
			EVP_DigestInit(ctx, EVP_get_digestbynid(ktype));
			EVP_DigestUpdate(ctx, key->buf, key->len);
			EVP_DigestUpdate(ctx, msg->buf, msg->len);
			EVP_DigestFinal(ctx, digest->buf, &uilen);
		}
		if (ctx)
			EVP_MD_CTX_free(ctx);
		retlen = (size_t)uilen;
	}
	else
	{
		msyslog(LOG_ERR, "MAC encrypt: invalid key type %d"  , ktype);
	}
	
#endif /* !OPENSSL */

	return retlen;
}


/*
 * MD5authencrypt - generate message digest
 *
 * Returns length of MAC including key ID and digest.
 */
size_t
MD5authencrypt(
	int		type,	/* hash algorithm */
	const u_char *	key,	/* key pointer */
	size_t		klen,	/* key length */
	u_int32 *	pkt,	/* packet pointer */
	size_t		length	/* packet length */
	)
{
	u_char	digest[EVP_MAX_MD_SIZE];
	rwbuffT digb = { digest, sizeof(digest) };
	robuffT keyb = { key, klen };
	robuffT msgb = { pkt, length };	
	size_t	dlen = 0;

	dlen = make_mac(&digb, type, &keyb, &msgb);
	/* If the MAC is longer than the MAX then truncate it. */
	if (dlen > MAX_MDG_LEN)
		dlen = MAX_MDG_LEN;
	memcpy((u_char *)pkt + length + KEY_MAC_LEN, digest, dlen);
	return (dlen + KEY_MAC_LEN);
}


/*
 * MD5authdecrypt - verify MD5 message authenticator
 *
 * Returns one if digest valid, zero if invalid.
 */
int
MD5authdecrypt(
	int		type,	/* hash algorithm */
	const u_char *	key,	/* key pointer */
	size_t		klen,	/* key length */
	u_int32	*	pkt,	/* packet pointer */
	size_t		length,	/* packet length */
	size_t		size	/* MAC size */
	)
{
	u_char	digest[EVP_MAX_MD_SIZE];
	rwbuffT digb = { digest, sizeof(digest) };
	robuffT keyb = { key, klen };
	robuffT msgb = { pkt, length };	
	size_t	dlen = 0;

	dlen = make_mac(&digb, type, &keyb, &msgb);
	
	/* If the MAC is longer than the MAX then truncate it. */
	if (dlen > MAX_MDG_LEN)
		dlen = MAX_MDG_LEN;
	if (size != (size_t)dlen + KEY_MAC_LEN) {
		msyslog(LOG_ERR,
		    "MAC decrypt: MAC length error");
		return (0);
	}
	return !isc_tsmemcmp(digest,
		 (u_char *)pkt + length + KEY_MAC_LEN, dlen);
}

/*
 * Calculate the reference id from the address. If it is an IPv4
 * address, use it as is. If it is an IPv6 address, do a md5 on
 * it and use the bottom 4 bytes.
 * The result is in network byte order.
 */
u_int32
addr2refid(sockaddr_u *addr)
{
	u_char		digest[EVP_MAX_MD_SIZE];
	u_int32		addr_refid;
	EVP_MD_CTX	*ctx;
	u_int		len;

	if (IS_IPV4(addr))
		return (NSRCADR(addr));

	INIT_SSL();

	ctx = EVP_MD_CTX_new();
#   ifdef EVP_MD_CTX_FLAG_NON_FIPS_ALLOW
	/* MD5 is not used as a crypto hash here. */
	EVP_MD_CTX_set_flags(ctx, EVP_MD_CTX_FLAG_NON_FIPS_ALLOW);
#   endif
	/* [Bug 3457] DON'T use plain EVP_DigestInit! It would kill the
	 * flags! */
	if (!EVP_DigestInit_ex(ctx, EVP_md5(), NULL)) {
		msyslog(LOG_ERR,
		    "MD5 init failed");
		EVP_MD_CTX_free(ctx);	/* pedantic... but safe */
		exit(1);
	}

	EVP_DigestUpdate(ctx, (u_char *)PSOCK_ADDR6(addr),
	    sizeof(struct in6_addr));
	EVP_DigestFinal(ctx, digest, &len);
	EVP_MD_CTX_free(ctx);
	memcpy(&addr_refid, digest, sizeof(addr_refid));
	return (addr_refid);
}
