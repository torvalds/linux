/*
 * HMS: we need to test:
 * - OpenSSL versions, if we are building with them
 * - our versions
 *
 * We may need to test with(out) OPENSSL separately.
 */

#include <config.h>
#include "crypto.h"
#include <ctype.h>
#include "isc/string.h"
#include "ntp_md5.h"

#ifndef EVP_MAX_MD_SIZE
# define EVP_MAX_MD_SIZE 32
#endif

struct key *key_ptr;
size_t key_cnt = 0;

typedef struct key Key_T;

static u_int
compute_mac(
	u_char		digest[EVP_MAX_MD_SIZE],
	char const *	macname,
	void const *	pkt_data,
	u_int		pkt_size,
	void const *	key_data,
	u_int		key_size
	)
{
	u_int		len  = 0;
	size_t		slen = 0;
	int		key_type;
	
	INIT_SSL();
	key_type = keytype_from_text(macname, NULL);

#if defined(OPENSSL) && defined(ENABLE_CMAC)
	/* Check if CMAC key type specific code required */
	if (key_type == NID_cmac) {
		CMAC_CTX *	ctx    = NULL;
		u_char		keybuf[AES_128_KEY_SIZE];

		/* adjust key size (zero padded buffer) if necessary */
		if (AES_128_KEY_SIZE > key_size) {
			memcpy(keybuf, key_data, key_size);
			memset((keybuf + key_size), 0,
			       (AES_128_KEY_SIZE - key_size));
			key_data = keybuf;
		}

		if (!(ctx = CMAC_CTX_new())) {
			msyslog(LOG_ERR, "make_mac: CMAC %s CTX new failed.",   CMAC);
		}
		else if (!CMAC_Init(ctx, key_data, AES_128_KEY_SIZE,
				    EVP_aes_128_cbc(), NULL)) {
			msyslog(LOG_ERR, "make_mac: CMAC %s Init failed.",      CMAC);
		}
		else if (!CMAC_Update(ctx, pkt_data, (size_t)pkt_size)) {
			msyslog(LOG_ERR, "make_mac: CMAC %s Update failed.",    CMAC);
		}
		else if (!CMAC_Final(ctx, digest, &slen)) {
			msyslog(LOG_ERR, "make_mac: CMAC %s Final failed.",     CMAC);
			slen = 0;
		}
		len = (u_int)slen;
		
		CMAC_CTX_cleanup(ctx);
		/* Test our AES-128-CMAC implementation */
		
	} else	/* MD5 MAC handling */
#endif
	{
		EVP_MD_CTX *	ctx;
		
		if (!(ctx = EVP_MD_CTX_new())) {
			msyslog(LOG_ERR, "make_mac: MAC %s Digest CTX new failed.",
				macname);
			goto mac_fail;
		}
#ifdef OPENSSL	/* OpenSSL 1 supports return codes 0 fail, 1 okay */
#	    ifdef EVP_MD_CTX_FLAG_NON_FIPS_ALLOW
		EVP_MD_CTX_set_flags(ctx, EVP_MD_CTX_FLAG_NON_FIPS_ALLOW);
#	    endif
		/* [Bug 3457] DON'T use plain EVP_DigestInit! It would
		 *  kill the flags! */
		if (!EVP_DigestInit_ex(ctx, EVP_get_digestbynid(key_type), NULL)) {
			msyslog(LOG_ERR, "make_mac: MAC %s Digest Init failed.",
				macname);
			goto mac_fail;
		}
		if (!EVP_DigestUpdate(ctx, key_data, key_size)) {
			msyslog(LOG_ERR, "make_mac: MAC %s Digest Update key failed.",
				macname);
			goto mac_fail;
		}
		if (!EVP_DigestUpdate(ctx, pkt_data, pkt_size)) {
			msyslog(LOG_ERR, "make_mac: MAC %s Digest Update data failed.",
				macname);
			goto mac_fail;
		}
		if (!EVP_DigestFinal(ctx, digest, &len)) {
			msyslog(LOG_ERR, "make_mac: MAC %s Digest Final failed.",
				macname);
			len = 0;
		}
#else /* !OPENSSL */
		EVP_DigestInit(ctx, EVP_get_digestbynid(key_type));
		EVP_DigestUpdate(ctx, key_data, key_size);
		EVP_DigestUpdate(ctx, pkt_data, pkt_size);
		EVP_DigestFinal(ctx, digest, &len);
#endif
	  mac_fail:
		EVP_MD_CTX_free(ctx);
	}

	return len;
}

int
make_mac(
	const void *	pkt_data,
	int		pkt_size,
	int		mac_size,
	Key_T const *	cmp_key,
	void * 		digest
	)
{
	u_int		len;
	u_char		dbuf[EVP_MAX_MD_SIZE];
	
	if (cmp_key->key_len > 64 || mac_size <= 0)
		return 0;
	if (pkt_size % 4 != 0)
		return 0;

	len = compute_mac(dbuf, cmp_key->typen,
			  pkt_data, (u_int)pkt_size,
			  cmp_key->key_seq, (u_int)cmp_key->key_len);
			  

	if (len) {
		if (len > (u_int)mac_size)
			len = (u_int)mac_size;
		memcpy(digest, dbuf, len);
	}
	return (int)len;
}


/* Generates a md5 digest of the key specified in keyid concatenated with the
 * ntp packet (exluding the MAC) and compares this digest to the digest in
 * the packet's MAC. If they're equal this function returns 1 (packet is
 * authentic) or else 0 (not authentic).
 */
int
auth_md5(
	void const *	pkt_data,
	int 		pkt_size,
	int		mac_size,
	Key_T const *	cmp_key
	)
{
	u_int		len       = 0;
	u_char const *	pkt_ptr   = pkt_data;
	u_char		dbuf[EVP_MAX_MD_SIZE];
	
	if (mac_size <= 0 || (size_t)mac_size > sizeof(dbuf))
		return FALSE;
	
	len = compute_mac(dbuf, cmp_key->typen,
			  pkt_ptr, (u_int)pkt_size,
			  cmp_key->key_seq, (u_int)cmp_key->key_len);

	pkt_ptr += pkt_size + 4;
	if (len > (u_int)mac_size)
		len = (u_int)mac_size;
	
	/* isc_tsmemcmp will be better when its easy to link with.  sntp
	 * is a 1-shot program, so snooping for timing attacks is
	 * Harder.
	 */
	return ((u_int)mac_size == len) && !memcmp(dbuf, pkt_ptr, len);
}

static int
hex_val(
	unsigned char x
	)
{
	int val;

	if ('0' <= x && x <= '9')
		val = x - '0';
	else if ('a' <= x && x <= 'f')
		val = x - 'a' + 0xa;
	else if ('A' <= x && x <= 'F')
		val = x - 'A' + 0xA;
	else
		val = -1;

	return val;
}

/* Load keys from the specified keyfile into the key structures.
 * Returns -1 if the reading failed, otherwise it returns the
 * number of keys it read
 */
int
auth_init(
	const char *keyfile,
	struct key **keys
	)
{
	FILE *keyf = fopen(keyfile, "r");
	struct key *prev = NULL;
	int scan_cnt, line_cnt = 1;
	char kbuf[200];
	char keystring[129];

	/* HMS: Is it OK to do this later, after we know we have a key file? */
	INIT_SSL();
	
	if (keyf == NULL) {
		if (debug)
			printf("sntp auth_init: Couldn't open key file %s for reading!\n", keyfile);
		return -1;
	}
	if (feof(keyf)) {
		if (debug)
			printf("sntp auth_init: Key file %s is empty!\n", keyfile);
		fclose(keyf);
		return -1;
	}
	key_cnt = 0;
	while (!feof(keyf)) {
		char * octothorpe;
		struct key *act;
		int goodline = 0;

		if (NULL == fgets(kbuf, sizeof(kbuf), keyf))
			continue;

		kbuf[sizeof(kbuf) - 1] = '\0';
		octothorpe = strchr(kbuf, '#');
		if (octothorpe)
			*octothorpe = '\0';
		act = emalloc(sizeof(*act));
		/* keep width 15 = sizeof struct key.typen - 1 synced */
		scan_cnt = sscanf(kbuf, "%d %15s %128s",
					&act->key_id, act->typen, keystring);
		if (scan_cnt == 3) {
			int len = strlen(keystring);
			goodline = 1;	/* assume best for now */
			if (len <= 20) {
				act->key_len = len;
				memcpy(act->key_seq, keystring, len + 1);
			} else if ((len & 1) != 0) {
				goodline = 0; /* it's bad */
			} else {
				int j;
				act->key_len = len >> 1;
				for (j = 0; j < len; j+=2) {
					int val;
					val = (hex_val(keystring[j]) << 4) |
					       hex_val(keystring[j+1]);
					if (val < 0) {
						goodline = 0; /* it's bad */
						break;
					}
					act->key_seq[j>>1] = (char)val;
				}
			}
			act->typei = keytype_from_text(act->typen, NULL);
			if (0 == act->typei) {
				printf("%s: line %d: key %d, %s not supported - ignoring\n",
					keyfile, line_cnt,
					act->key_id, act->typen);
				goodline = 0; /* it's bad */
			}
		}
		if (goodline) {
			act->next = NULL;
			if (NULL == prev)
				*keys = act;
			else
				prev->next = act;
			prev = act;
			key_cnt++;
		} else {
			if (debug) {
				printf("auth_init: scanf %d items, skipping line %d.",
					scan_cnt, line_cnt);
			}
			free(act);
		}
		line_cnt++;
	}
	fclose(keyf);

	key_ptr = *keys;
	return key_cnt;
}

/* Looks for the key with keyid key_id and sets the d_key pointer to the
 * address of the key. If no matching key is found the pointer is not touched.
 */
void
get_key(
	int key_id,
	struct key **d_key
	)
{
	struct key *itr_key;

	if (key_cnt == 0)
		return;
	for (itr_key = key_ptr; itr_key; itr_key = itr_key->next) {
		if (itr_key->key_id == key_id) {
			*d_key = itr_key;
			break;
		}
	}
	return;
}
