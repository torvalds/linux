/*
 * Copyright (C) 2017 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config TLS
//config:	bool #No description makes it a hidden option
//config:	default n

//kbuild:lib-$(CONFIG_TLS) += tls.o
//kbuild:lib-$(CONFIG_TLS) += tls_pstm.o
//kbuild:lib-$(CONFIG_TLS) += tls_pstm_montgomery_reduce.o
//kbuild:lib-$(CONFIG_TLS) += tls_pstm_mul_comba.o
//kbuild:lib-$(CONFIG_TLS) += tls_pstm_sqr_comba.o
//kbuild:lib-$(CONFIG_TLS) += tls_rsa.o
//kbuild:lib-$(CONFIG_TLS) += tls_aes.o
////kbuild:lib-$(CONFIG_TLS) += tls_aes_gcm.o

#include "tls.h"

//Tested against kernel.org:
//TLS 1.2
#define TLS_MAJ 3
#define TLS_MIN 3
//#define CIPHER_ID TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA // ok, recvs SERVER_KEY_EXCHANGE *** matrixssl uses this on my box
//#define CIPHER_ID TLS_RSA_WITH_AES_256_CBC_SHA256 // ok, no SERVER_KEY_EXCHANGE
//#define CIPHER_ID TLS_DH_anon_WITH_AES_256_CBC_SHA // SSL_ALERT_HANDSHAKE_FAILURE
//^^^^^^^^^^^^^^^^^^^^^^^ (tested b/c this one doesn't req server certs... no luck, server refuses it)
//#define CIPHER_ID TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 // SSL_ALERT_HANDSHAKE_FAILURE
//#define CIPHER_ID TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 // SSL_ALERT_HANDSHAKE_FAILURE
//#define CIPHER_ID TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 // ok, recvs SERVER_KEY_EXCHANGE
//#define CIPHER_ID TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
//#define CIPHER_ID TLS_ECDH_ECDSA_WITH_AES_256_GCM_SHA384
//#define CIPHER_ID TLS_ECDH_ECDSA_WITH_AES_128_GCM_SHA256 // SSL_ALERT_HANDSHAKE_FAILURE
//#define CIPHER_ID TLS_ECDH_RSA_WITH_AES_256_GCM_SHA384
//#define CIPHER_ID TLS_ECDH_RSA_WITH_AES_128_GCM_SHA256 // SSL_ALERT_HANDSHAKE_FAILURE
//#define CIPHER_ID TLS_RSA_WITH_AES_256_GCM_SHA384 // ok, no SERVER_KEY_EXCHANGE
//#define CIPHER_ID TLS_RSA_WITH_AES_128_GCM_SHA256 // ok, no SERVER_KEY_EXCHANGE *** select this?

// works against "openssl s_server -cipher NULL"
// and against wolfssl-3.9.10-stable/examples/server/server.c:
//#define CIPHER_ID1 TLS_RSA_WITH_NULL_SHA256 // for testing (does everything except encrypting)

// works against wolfssl-3.9.10-stable/examples/server/server.c
// works for kernel.org
// does not work for cdn.kernel.org (e.g. downloading an actual tarball, not a web page)
//  getting alert 40 "handshake failure" at once
//  with GNU Wget 1.18, they agree on TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 (0xC02F) cipher
//  fail: openssl s_client -connect cdn.kernel.org:443 -debug -tls1_2 -no_tls1 -no_tls1_1 -cipher AES256-SHA256
//  fail: openssl s_client -connect cdn.kernel.org:443 -debug -tls1_2 -no_tls1 -no_tls1_1 -cipher AES256-GCM-SHA384
//  fail: openssl s_client -connect cdn.kernel.org:443 -debug -tls1_2 -no_tls1 -no_tls1_1 -cipher AES128-SHA256
//  ok:   openssl s_client -connect cdn.kernel.org:443 -debug -tls1_2 -no_tls1 -no_tls1_1 -cipher AES128-GCM-SHA256
//  ok:   openssl s_client -connect cdn.kernel.org:443 -debug -tls1_2 -no_tls1 -no_tls1_1 -cipher AES128-SHA
//        (TLS_RSA_WITH_AES_128_CBC_SHA - in TLS 1.2 it's mandated to be always supported)
#define CIPHER_ID1  TLS_RSA_WITH_AES_256_CBC_SHA256 // no SERVER_KEY_EXCHANGE from peer
// Works with "wget https://cdn.kernel.org/pub/linux/kernel/v4.x/linux-4.9.5.tar.xz"
#define CIPHER_ID2  TLS_RSA_WITH_AES_128_CBC_SHA


#define TLS_DEBUG      0
#define TLS_DEBUG_HASH 0
#define TLS_DEBUG_DER  0
#define TLS_DEBUG_FIXED_SECRETS 0
#if 0
# define dump_raw_out(...) dump_hex(__VA_ARGS__)
#else
# define dump_raw_out(...) ((void)0)
#endif
#if 0
# define dump_raw_in(...) dump_hex(__VA_ARGS__)
#else
# define dump_raw_in(...) ((void)0)
#endif

#if TLS_DEBUG
# define dbg(...) fprintf(stderr, __VA_ARGS__)
#else
# define dbg(...) ((void)0)
#endif

#if TLS_DEBUG_DER
# define dbg_der(...) fprintf(stderr, __VA_ARGS__)
#else
# define dbg_der(...) ((void)0)
#endif

#define RECORD_TYPE_CHANGE_CIPHER_SPEC  20 /* 0x14 */
#define RECORD_TYPE_ALERT               21 /* 0x15 */
#define RECORD_TYPE_HANDSHAKE           22 /* 0x16 */
#define RECORD_TYPE_APPLICATION_DATA    23 /* 0x17 */

#define HANDSHAKE_HELLO_REQUEST         0  /* 0x00 */
#define HANDSHAKE_CLIENT_HELLO          1  /* 0x01 */
#define HANDSHAKE_SERVER_HELLO          2  /* 0x02 */
#define HANDSHAKE_HELLO_VERIFY_REQUEST  3  /* 0x03 */
#define HANDSHAKE_NEW_SESSION_TICKET    4  /* 0x04 */
#define HANDSHAKE_CERTIFICATE           11 /* 0x0b */
#define HANDSHAKE_SERVER_KEY_EXCHANGE   12 /* 0x0c */
#define HANDSHAKE_CERTIFICATE_REQUEST   13 /* 0x0d */
#define HANDSHAKE_SERVER_HELLO_DONE     14 /* 0x0e */
#define HANDSHAKE_CERTIFICATE_VERIFY    15 /* 0x0f */
#define HANDSHAKE_CLIENT_KEY_EXCHANGE   16 /* 0x10 */
#define HANDSHAKE_FINISHED              20 /* 0x14 */

#define SSL_NULL_WITH_NULL_NULL                 0x0000
#define SSL_RSA_WITH_NULL_MD5                   0x0001
#define SSL_RSA_WITH_NULL_SHA                   0x0002
#define SSL_RSA_WITH_RC4_128_MD5                0x0004
#define SSL_RSA_WITH_RC4_128_SHA                0x0005
#define SSL_RSA_WITH_3DES_EDE_CBC_SHA           0x000A  /* 10 */
#define TLS_RSA_WITH_AES_128_CBC_SHA            0x002F  /* 47 */
#define TLS_RSA_WITH_AES_256_CBC_SHA            0x0035  /* 53 */
#define TLS_RSA_WITH_NULL_SHA256                0x003B  /* 59 */

#define TLS_EMPTY_RENEGOTIATION_INFO_SCSV       0x00FF

#define TLS_RSA_WITH_IDEA_CBC_SHA               0x0007  /* 7 */
#define SSL_DHE_RSA_WITH_3DES_EDE_CBC_SHA       0x0016  /* 22 */
#define SSL_DH_anon_WITH_RC4_128_MD5            0x0018  /* 24 */
#define SSL_DH_anon_WITH_3DES_EDE_CBC_SHA       0x001B  /* 27 */
#define TLS_DHE_RSA_WITH_AES_128_CBC_SHA        0x0033  /* 51 */
#define TLS_DHE_RSA_WITH_AES_256_CBC_SHA        0x0039  /* 57 */
#define TLS_DHE_RSA_WITH_AES_128_CBC_SHA256     0x0067  /* 103 */
#define TLS_DHE_RSA_WITH_AES_256_CBC_SHA256     0x006B  /* 107 */
#define TLS_DH_anon_WITH_AES_128_CBC_SHA        0x0034  /* 52 */
#define TLS_DH_anon_WITH_AES_256_CBC_SHA        0x003A  /* 58 */
#define TLS_RSA_WITH_AES_128_CBC_SHA256         0x003C  /* 60 */
#define TLS_RSA_WITH_AES_256_CBC_SHA256         0x003D  /* 61 */
#define TLS_RSA_WITH_SEED_CBC_SHA               0x0096  /* 150 */
#define TLS_PSK_WITH_AES_128_CBC_SHA            0x008C  /* 140 */
#define TLS_PSK_WITH_AES_128_CBC_SHA256         0x00AE  /* 174 */
#define TLS_PSK_WITH_AES_256_CBC_SHA384         0x00AF  /* 175 */
#define TLS_PSK_WITH_AES_256_CBC_SHA            0x008D  /* 141 */
#define TLS_DHE_PSK_WITH_AES_128_CBC_SHA        0x0090  /* 144 */
#define TLS_DHE_PSK_WITH_AES_256_CBC_SHA        0x0091  /* 145 */
#define TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA     0xC004  /* 49156 */
#define TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA     0xC005  /* 49157 */
#define TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA    0xC009  /* 49161 */
#define TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA    0xC00A  /* 49162 */
#define TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA     0xC012  /* 49170 */
#define TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA      0xC013  /* 49171 */
#define TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA      0xC014  /* 49172 */
#define TLS_ECDH_RSA_WITH_AES_128_CBC_SHA       0xC00E  /* 49166 */
#define TLS_ECDH_RSA_WITH_AES_256_CBC_SHA       0xC00F  /* 49167 */
#define TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256 0xC023  /* 49187 */
#define TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384 0xC024  /* 49188 */
#define TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA256  0xC025  /* 49189 */
#define TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA384  0xC026  /* 49190 */
#define TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256   0xC027  /* 49191 */
#define TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384   0xC028  /* 49192 */
#define TLS_ECDH_RSA_WITH_AES_128_CBC_SHA256    0xC029  /* 49193 */
#define TLS_ECDH_RSA_WITH_AES_256_CBC_SHA384    0xC02A  /* 49194 */

/* RFC 5288 "AES Galois Counter Mode (GCM) Cipher Suites for TLS" */
#define TLS_RSA_WITH_AES_128_GCM_SHA256         0x009C  /* 156 */
#define TLS_RSA_WITH_AES_256_GCM_SHA384         0x009D  /* 157 */
#define TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 0xC02B  /* 49195 */
#define TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 0xC02C  /* 49196 */
#define TLS_ECDH_ECDSA_WITH_AES_128_GCM_SHA256  0xC02D  /* 49197 */
#define TLS_ECDH_ECDSA_WITH_AES_256_GCM_SHA384  0xC02E  /* 49198 */
#define TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256   0xC02F  /* 49199 */
#define TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384   0xC030  /* 49200 */
#define TLS_ECDH_RSA_WITH_AES_128_GCM_SHA256    0xC031  /* 49201 */
#define TLS_ECDH_RSA_WITH_AES_256_GCM_SHA384    0xC032  /* 49202 */

/* Might go to libbb.h */
#define TLS_MAX_CRYPTBLOCK_SIZE 16
#define TLS_MAX_OUTBUF          (1 << 14)

enum {
	SHA_INSIZE     = 64,
	SHA1_OUTSIZE   = 20,
	SHA256_OUTSIZE = 32,

	AES_BLOCKSIZE  = 16,
	AES128_KEYSIZE = 16,
	AES256_KEYSIZE = 32,

	RSA_PREMASTER_SIZE = 48,

	RECHDR_LEN = 5,

	/* 8 = 3+5. 3 extra bytes result in record data being 32-bit aligned */
	OUTBUF_PFX = 8 + AES_BLOCKSIZE, /* header + IV */
	OUTBUF_SFX = TLS_MAX_MAC_SIZE + TLS_MAX_CRYPTBLOCK_SIZE, /* MAC + padding */

	// RFC 5246
	// | 6.2.1. Fragmentation
	// |  The record layer fragments information blocks into TLSPlaintext
	// |  records carrying data in chunks of 2^14 bytes or less.  Client
	// |  message boundaries are not preserved in the record layer (i.e.,
	// |  multiple client messages of the same ContentType MAY be coalesced
	// |  into a single TLSPlaintext record, or a single message MAY be
	// |  fragmented across several records)
	// |...
	// |  length
	// |    The length (in bytes) of the following TLSPlaintext.fragment.
	// |    The length MUST NOT exceed 2^14.
	// |...
	// | 6.2.2. Record Compression and Decompression
	// |...
	// |  Compression must be lossless and may not increase the content length
	// |  by more than 1024 bytes.  If the decompression function encounters a
	// |  TLSCompressed.fragment that would decompress to a length in excess of
	// |  2^14 bytes, it MUST report a fatal decompression failure error.
	// |...
	// |  length
	// |    The length (in bytes) of the following TLSCompressed.fragment.
	// |    The length MUST NOT exceed 2^14 + 1024.
	// |...
	// | 6.2.3.  Record Payload Protection
	// |  The encryption and MAC functions translate a TLSCompressed
	// |  structure into a TLSCiphertext.  The decryption functions reverse
	// |  the process.  The MAC of the record also includes a sequence
	// |  number so that missing, extra, or repeated messages are
	// |  detectable.
	// |...
	// |  length
	// |    The length (in bytes) of the following TLSCiphertext.fragment.
	// |    The length MUST NOT exceed 2^14 + 2048.
	MAX_INBUF = RECHDR_LEN + (1 << 14) + 2048,
};

struct record_hdr {
	uint8_t type;
	uint8_t proto_maj, proto_min;
	uint8_t len16_hi, len16_lo;
};

struct tls_handshake_data {
	/* In bbox, md5/sha1/sha256 ctx's are the same structure */
	md5sha_ctx_t handshake_hash_ctx;

	uint8_t client_and_server_rand32[2 * 32];
	uint8_t master_secret[48];
//TODO: store just the DER key here, parse/use/delete it when sending client key
//this way it will stay key type agnostic here.
	psRsaKey_t server_rsa_pub_key;

	unsigned saved_client_hello_size;
	uint8_t saved_client_hello[1];
};


static unsigned get24be(const uint8_t *p)
{
	return 0x100*(0x100*p[0] + p[1]) + p[2];
}

#if TLS_DEBUG
static void dump_hex(const char *fmt, const void *vp, int len)
{
	char hexbuf[32 * 1024 + 4];
	const uint8_t *p = vp;

	bin2hex(hexbuf, (void*)p, len)[0] = '\0';
	dbg(fmt, hexbuf);
}

static void dump_tls_record(const void *vp, int len)
{
	const uint8_t *p = vp;

	while (len > 0) {
		unsigned xhdr_len;
		if (len < RECHDR_LEN) {
			dump_hex("< |%s|\n", p, len);
			return;
		}
		xhdr_len = 0x100*p[3] + p[4];
		dbg("< hdr_type:%u ver:%u.%u len:%u", p[0], p[1], p[2], xhdr_len);
		p += RECHDR_LEN;
		len -= RECHDR_LEN;
		if (len >= 4 && p[-RECHDR_LEN] == RECORD_TYPE_HANDSHAKE) {
			unsigned len24 = get24be(p + 1);
			dbg(" type:%u len24:%u", p[0], len24);
		}
		if (xhdr_len > len)
			xhdr_len = len;
		dump_hex(" |%s|\n", p, xhdr_len);
		p += xhdr_len;
		len -= xhdr_len;
	}
}
#else
# define dump_hex(...) ((void)0)
# define dump_tls_record(...) ((void)0)
#endif

void tls_get_random(void *buf, unsigned len)
{
	if (len != open_read_close("/dev/urandom", buf, len))
		xfunc_die();
}

/* Nondestructively see the current hash value */
static unsigned sha_peek(md5sha_ctx_t *ctx, void *buffer)
{
	md5sha_ctx_t ctx_copy = *ctx; /* struct copy */
	return sha_end(&ctx_copy, buffer);
}

static ALWAYS_INLINE unsigned get_handshake_hash(tls_state_t *tls, void *buffer)
{
	return sha_peek(&tls->hsd->handshake_hash_ctx, buffer);
}

#if !TLS_DEBUG_HASH
# define hash_handshake(tls, fmt, buffer, len) \
         hash_handshake(tls, buffer, len)
#endif
static void hash_handshake(tls_state_t *tls, const char *fmt, const void *buffer, unsigned len)
{
	md5sha_hash(&tls->hsd->handshake_hash_ctx, buffer, len);
#if TLS_DEBUG_HASH
	{
		uint8_t h[TLS_MAX_MAC_SIZE];
		dump_hex(fmt, buffer, len);
		dbg(" (%u bytes) ", (int)len);
		len = sha_peek(&tls->hsd->handshake_hash_ctx, h);
		if (len == SHA1_OUTSIZE)
			dump_hex("sha1:%s\n", h, len);
		else
		if (len == SHA256_OUTSIZE)
			dump_hex("sha256:%s\n", h, len);
		else
			dump_hex("sha???:%s\n", h, len);
	}
#endif
}

// RFC 2104
// HMAC(key, text) based on a hash H (say, sha256) is:
// ipad = [0x36 x INSIZE]
// opad = [0x5c x INSIZE]
// HMAC(key, text) = H((key XOR opad) + H((key XOR ipad) + text))
//
// H(key XOR opad) and H(key XOR ipad) can be precomputed
// if we often need HMAC hmac with the same key.
//
// text is often given in disjoint pieces.
typedef struct hmac_precomputed {
	md5sha_ctx_t hashed_key_xor_ipad;
	md5sha_ctx_t hashed_key_xor_opad;
} hmac_precomputed_t;

static unsigned hmac_sha_precomputed_v(
		hmac_precomputed_t *pre,
		uint8_t *out,
		va_list va)
{
	uint8_t *text;
	unsigned len;

	/* pre->hashed_key_xor_ipad contains unclosed "H((key XOR ipad) +" state */
	/* pre->hashed_key_xor_opad contains unclosed "H((key XOR opad) +" state */

	/* calculate out = H((key XOR ipad) + text) */
	while ((text = va_arg(va, uint8_t*)) != NULL) {
		unsigned text_size = va_arg(va, unsigned);
		md5sha_hash(&pre->hashed_key_xor_ipad, text, text_size);
	}
	len = sha_end(&pre->hashed_key_xor_ipad, out);

	/* out = H((key XOR opad) + out) */
	md5sha_hash(&pre->hashed_key_xor_opad, out, len);
	return sha_end(&pre->hashed_key_xor_opad, out);
}

typedef void md5sha_begin_func(md5sha_ctx_t *ctx) FAST_FUNC;
static void hmac_begin(hmac_precomputed_t *pre, uint8_t *key, unsigned key_size, md5sha_begin_func *begin)
{
	uint8_t key_xor_ipad[SHA_INSIZE];
	uint8_t key_xor_opad[SHA_INSIZE];
	uint8_t tempkey[SHA1_OUTSIZE < SHA256_OUTSIZE ? SHA256_OUTSIZE : SHA1_OUTSIZE];
	unsigned i;

	// "The authentication key can be of any length up to INSIZE, the
	// block length of the hash function.  Applications that use keys longer
	// than INSIZE bytes will first hash the key using H and then use the
	// resultant OUTSIZE byte string as the actual key to HMAC."
	if (key_size > SHA_INSIZE) {
		md5sha_ctx_t ctx;
		begin(&ctx);
		md5sha_hash(&ctx, key, key_size);
		key_size = sha_end(&ctx, tempkey);
	}

	for (i = 0; i < key_size; i++) {
		key_xor_ipad[i] = key[i] ^ 0x36;
		key_xor_opad[i] = key[i] ^ 0x5c;
	}
	for (; i < SHA_INSIZE; i++) {
		key_xor_ipad[i] = 0x36;
		key_xor_opad[i] = 0x5c;
	}

	begin(&pre->hashed_key_xor_ipad);
	begin(&pre->hashed_key_xor_opad);
	md5sha_hash(&pre->hashed_key_xor_ipad, key_xor_ipad, SHA_INSIZE);
	md5sha_hash(&pre->hashed_key_xor_opad, key_xor_opad, SHA_INSIZE);
}

static unsigned hmac(tls_state_t *tls, uint8_t *out, uint8_t *key, unsigned key_size, ...)
{
	hmac_precomputed_t pre;
	va_list va;
	unsigned len;

	va_start(va, key_size);

	hmac_begin(&pre, key, key_size,
			(tls->MAC_size == SHA256_OUTSIZE)
				? sha256_begin
				: sha1_begin
	);
	len = hmac_sha_precomputed_v(&pre, out, va);

	va_end(va);
	return len;
}

static unsigned hmac_sha256(/*tls_state_t *tls,*/ uint8_t *out, uint8_t *key, unsigned key_size, ...)
{
	hmac_precomputed_t pre;
	va_list va;
	unsigned len;

	va_start(va, key_size);

	hmac_begin(&pre, key, key_size, sha256_begin);
	len = hmac_sha_precomputed_v(&pre, out, va);

	va_end(va);
	return len;
}

// RFC 5246:
// 5.  HMAC and the Pseudorandom Function
//...
// In this section, we define one PRF, based on HMAC.  This PRF with the
// SHA-256 hash function is used for all cipher suites defined in this
// document and in TLS documents published prior to this document when
// TLS 1.2 is negotiated.
// ^^^^^^^^^^^^^ IMPORTANT!
//               PRF uses sha256 regardless of cipher (at least for all ciphers
//               defined by RFC5246). It's not sha1 for AES_128_CBC_SHA!
//...
//    P_hash(secret, seed) = HMAC_hash(secret, A(1) + seed) +
//                           HMAC_hash(secret, A(2) + seed) +
//                           HMAC_hash(secret, A(3) + seed) + ...
// where + indicates concatenation.
// A() is defined as:
//    A(0) = seed
//    A(1) = HMAC_hash(secret, A(0)) = HMAC_hash(secret, seed)
//    A(i) = HMAC_hash(secret, A(i-1))
// P_hash can be iterated as many times as necessary to produce the
// required quantity of data.  For example, if P_SHA256 is being used to
// create 80 bytes of data, it will have to be iterated three times
// (through A(3)), creating 96 bytes of output data; the last 16 bytes
// of the final iteration will then be discarded, leaving 80 bytes of
// output data.
//
// TLS's PRF is created by applying P_hash to the secret as:
//
//    PRF(secret, label, seed) = P_<hash>(secret, label + seed)
//
// The label is an ASCII string.
static void prf_hmac_sha256(/*tls_state_t *tls,*/
		uint8_t *outbuf, unsigned outbuf_size,
		uint8_t *secret, unsigned secret_size,
		const char *label,
		uint8_t *seed, unsigned seed_size)
{
	uint8_t a[TLS_MAX_MAC_SIZE];
	uint8_t *out_p = outbuf;
	unsigned label_size = strlen(label);
	unsigned MAC_size = SHA256_OUTSIZE;

	/* In P_hash() calculation, "seed" is "label + seed": */
#define SEED   label, label_size, seed, seed_size
#define SECRET secret, secret_size
#define A      a, MAC_size

	/* A(1) = HMAC_hash(secret, seed) */
	hmac_sha256(/*tls,*/ a, SECRET, SEED, NULL);
//TODO: convert hmac to precomputed

	for (;;) {
		/* HMAC_hash(secret, A(1) + seed) */
		if (outbuf_size <= MAC_size) {
			/* Last, possibly incomplete, block */
			/* (use a[] as temp buffer) */
			hmac_sha256(/*tls,*/ a, SECRET, A, SEED, NULL);
			memcpy(out_p, a, outbuf_size);
			return;
		}
		/* Not last block. Store directly to result buffer */
		hmac_sha256(/*tls,*/ out_p, SECRET, A, SEED, NULL);
		out_p += MAC_size;
		outbuf_size -= MAC_size;
		/* A(2) = HMAC_hash(secret, A(1)) */
		hmac_sha256(/*tls,*/ a, SECRET, A, NULL);
	}
#undef A
#undef SECRET
#undef SEED
}

static void bad_record_die(tls_state_t *tls, const char *expected, int len)
{
	bb_error_msg("got bad TLS record (len:%d) while expecting %s", len, expected);
	if (len > 0) {
		uint8_t *p = tls->inbuf;
		if (len > 99)
			len = 99; /* don't flood, a few lines should be enough */
		do {
			fprintf(stderr, " %02x", *p++);
			len--;
		} while (len != 0);
		fputc('\n', stderr);
	}
	xfunc_die();
}

static void tls_error_die(tls_state_t *tls, int line)
{
	dump_tls_record(tls->inbuf, tls->ofs_to_buffered + tls->buffered_size);
	bb_error_msg_and_die("tls error at line %d cipher:%04x", line, tls->cipher_id);
}
#define tls_error_die(tls) tls_error_die(tls, __LINE__)

#if 0 //UNUSED
static void tls_free_inbuf(tls_state_t *tls)
{
	if (tls->buffered_size == 0) {
		free(tls->inbuf);
		tls->inbuf_size = 0;
		tls->inbuf = NULL;
	}
}
#endif

static void tls_free_outbuf(tls_state_t *tls)
{
	free(tls->outbuf);
	tls->outbuf_size = 0;
	tls->outbuf = NULL;
}

static void *tls_get_outbuf(tls_state_t *tls, int len)
{
	if (len > TLS_MAX_OUTBUF)
		xfunc_die();
	len += OUTBUF_PFX + OUTBUF_SFX;
	if (tls->outbuf_size < len) {
		tls->outbuf_size = len;
		tls->outbuf = xrealloc(tls->outbuf, len);
	}
	return tls->outbuf + OUTBUF_PFX;
}

static void xwrite_encrypted(tls_state_t *tls, unsigned size, unsigned type)
{
	uint8_t *buf = tls->outbuf + OUTBUF_PFX;
	struct record_hdr *xhdr;
	uint8_t padding_length;

	xhdr = (void*)(buf - RECHDR_LEN);
	if (CIPHER_ID1 != TLS_RSA_WITH_NULL_SHA256 /* if "no encryption" can't be selected */
	 || tls->cipher_id != TLS_RSA_WITH_NULL_SHA256 /* or if it wasn't selected */
	) {
		xhdr = (void*)(buf - RECHDR_LEN - AES_BLOCKSIZE); /* place for IV */
	}

	xhdr->type = type;
	xhdr->proto_maj = TLS_MAJ;
	xhdr->proto_min = TLS_MIN;
	/* fake unencrypted record len for MAC calculation */
	xhdr->len16_hi = size >> 8;
	xhdr->len16_lo = size & 0xff;

	/* Calculate MAC signature */
	hmac(tls, buf + size, /* result */
		tls->client_write_MAC_key, tls->MAC_size,
		&tls->write_seq64_be, sizeof(tls->write_seq64_be),
		xhdr, RECHDR_LEN,
		buf, size,
		NULL
	);
	tls->write_seq64_be = SWAP_BE64(1 + SWAP_BE64(tls->write_seq64_be));

	size += tls->MAC_size;

	// RFC 5246
	// 6.2.3.1.  Null or Standard Stream Cipher
	//
	// Stream ciphers (including BulkCipherAlgorithm.null; see Appendix A.6)
	// convert TLSCompressed.fragment structures to and from stream
	// TLSCiphertext.fragment structures.
	//
	//    stream-ciphered struct {
	//        opaque content[TLSCompressed.length];
	//        opaque MAC[SecurityParameters.mac_length];
	//    } GenericStreamCipher;
	//
	// The MAC is generated as:
	//    MAC(MAC_write_key, seq_num +
	//                          TLSCompressed.type +
	//                          TLSCompressed.version +
	//                          TLSCompressed.length +
	//                          TLSCompressed.fragment);
	// where "+" denotes concatenation.
	// seq_num
	//    The sequence number for this record.
	// MAC
	//    The MAC algorithm specified by SecurityParameters.mac_algorithm.
	//
	// Note that the MAC is computed before encryption.  The stream cipher
	// encrypts the entire block, including the MAC.
	//...
	// Appendix C.  Cipher Suite Definitions
	//...
	// MAC       Algorithm    mac_length  mac_key_length
	// --------  -----------  ----------  --------------
	// SHA       HMAC-SHA1       20            20
	// SHA256    HMAC-SHA256     32            32
	if (CIPHER_ID1 == TLS_RSA_WITH_NULL_SHA256
	 && tls->cipher_id == TLS_RSA_WITH_NULL_SHA256
	) {
		/* No encryption, only signing */
		xhdr->len16_hi = size >> 8;
		xhdr->len16_lo = size & 0xff;
		dump_raw_out(">> %s\n", xhdr, RECHDR_LEN + size);
		xwrite(tls->ofd, xhdr, RECHDR_LEN + size);
		dbg("wrote %u bytes (NULL crypt, SHA256 hash)\n", size);
		return;
	}

	// 6.2.3.2.  CBC Block Cipher
	// For block ciphers (such as 3DES or AES), the encryption and MAC
	// functions convert TLSCompressed.fragment structures to and from block
	// TLSCiphertext.fragment structures.
	//    struct {
	//        opaque IV[SecurityParameters.record_iv_length];
	//        block-ciphered struct {
	//            opaque content[TLSCompressed.length];
	//            opaque MAC[SecurityParameters.mac_length];
	//            uint8 padding[GenericBlockCipher.padding_length];
	//            uint8 padding_length;
	//        };
	//    } GenericBlockCipher;
	//...
	// IV
	//    The Initialization Vector (IV) SHOULD be chosen at random, and
	//    MUST be unpredictable.  Note that in versions of TLS prior to 1.1,
	//    there was no IV field (...).  For block ciphers, the IV length is
	//    of length SecurityParameters.record_iv_length, which is equal to the
	//    SecurityParameters.block_size.
	// padding
	//    Padding that is added to force the length of the plaintext to be
	//    an integral multiple of the block cipher's block length.
	// padding_length
	//    The padding length MUST be such that the total size of the
	//    GenericBlockCipher structure is a multiple of the cipher's block
	//    length.  Legal values range from zero to 255, inclusive.
	//...
	// Appendix C.  Cipher Suite Definitions
	//...
	//                         Key      IV   Block
	// Cipher        Type    Material  Size  Size
	// ------------  ------  --------  ----  -----
	// AES_128_CBC   Block      16      16     16
	// AES_256_CBC   Block      32      16     16

	tls_get_random(buf - AES_BLOCKSIZE, AES_BLOCKSIZE); /* IV */
	dbg("before crypt: 5 hdr + %u data + %u hash bytes\n",
			size - tls->MAC_size, tls->MAC_size);

	/* Fill IV and padding in outbuf */
	// RFC is talking nonsense:
	//    "Padding that is added to force the length of the plaintext to be
	//    an integral multiple of the block cipher's block length."
	// WRONG. _padding+padding_length_, not just _padding_,
	// pads the data.
	// IOW: padding_length is the last byte of padding[] array,
	// contrary to what RFC depicts.
	//
	// What actually happens is that there is always padding.
	// If you need one byte to reach BLOCKSIZE, this byte is 0x00.
	// If you need two bytes, they are both 0x01.
	// If you need three, they are 0x02,0x02,0x02. And so on.
	// If you need no bytes to reach BLOCKSIZE, you have to pad a full
	// BLOCKSIZE with bytes of value (BLOCKSIZE-1).
	// It's ok to have more than minimum padding, but we do minimum.
	padding_length = (~size) & (AES_BLOCKSIZE - 1);
	do {
		buf[size++] = padding_length; /* padding */
	} while ((size & (AES_BLOCKSIZE - 1)) != 0);

	/* Encrypt content+MAC+padding in place */
	aes_cbc_encrypt(
		tls->client_write_key, tls->key_size, /* selects 128/256 */
		buf - AES_BLOCKSIZE, /* IV */
		buf, size, /* plaintext */
		buf /* ciphertext */
	);

	/* Write out */
	dbg("writing 5 + %u IV + %u encrypted bytes, padding_length:0x%02x\n",
			AES_BLOCKSIZE, size, padding_length);
	size += AES_BLOCKSIZE;     /* + IV */
	xhdr->len16_hi = size >> 8;
	xhdr->len16_lo = size & 0xff;
	dump_raw_out(">> %s\n", xhdr, RECHDR_LEN + size);
	xwrite(tls->ofd, xhdr, RECHDR_LEN + size);
	dbg("wrote %u bytes\n", (int)RECHDR_LEN + size);
}

static void xwrite_handshake_record(tls_state_t *tls, unsigned size)
{
	//if (!tls->encrypt_on_write) {
		uint8_t *buf = tls->outbuf + OUTBUF_PFX;
		struct record_hdr *xhdr = (void*)(buf - RECHDR_LEN);

		xhdr->type = RECORD_TYPE_HANDSHAKE;
		xhdr->proto_maj = TLS_MAJ;
		xhdr->proto_min = TLS_MIN;
		xhdr->len16_hi = size >> 8;
		xhdr->len16_lo = size & 0xff;
		dump_raw_out(">> %s\n", xhdr, RECHDR_LEN + size);
		xwrite(tls->ofd, xhdr, RECHDR_LEN + size);
		dbg("wrote %u bytes\n", (int)RECHDR_LEN + size);
	//	return;
	//}
	//xwrite_encrypted(tls, size, RECORD_TYPE_HANDSHAKE);
}

static void xwrite_and_update_handshake_hash(tls_state_t *tls, unsigned size)
{
	if (!tls->encrypt_on_write) {
		uint8_t *buf;

		xwrite_handshake_record(tls, size);
		/* Handshake hash does not include record headers */
		buf = tls->outbuf + OUTBUF_PFX;
		hash_handshake(tls, ">> hash:%s", buf, size);
		return;
	}
	xwrite_encrypted(tls, size, RECORD_TYPE_HANDSHAKE);
}

static int tls_has_buffered_record(tls_state_t *tls)
{
	int buffered = tls->buffered_size;
	struct record_hdr *xhdr;
	int rec_size;

	if (buffered < RECHDR_LEN)
		return 0;
	xhdr = (void*)(tls->inbuf + tls->ofs_to_buffered);
	rec_size = RECHDR_LEN + (0x100 * xhdr->len16_hi + xhdr->len16_lo);
	if (buffered < rec_size)
		return 0;
	return rec_size;
}

static const char *alert_text(int code)
{
	switch (code) {
	case 20:  return "bad MAC";
	case 50:  return "decode error";
	case 51:  return "decrypt error";
	case 40:  return "handshake failure";
	case 112: return "unrecognized name";
	}
	return itoa(code);
}

static int tls_xread_record(tls_state_t *tls, const char *expected)
{
	struct record_hdr *xhdr;
	int sz;
	int total;
	int target;

 again:
	dbg("ofs_to_buffered:%u buffered_size:%u\n", tls->ofs_to_buffered, tls->buffered_size);
	total = tls->buffered_size;
	if (total != 0) {
		memmove(tls->inbuf, tls->inbuf + tls->ofs_to_buffered, total);
		//dbg("<< remaining at %d [%d] ", tls->ofs_to_buffered, total);
		//dump_raw_in("<< %s\n", tls->inbuf, total);
	}
	errno = 0;
	target = MAX_INBUF;
	for (;;) {
		int rem;

		if (total >= RECHDR_LEN && target == MAX_INBUF) {
			xhdr = (void*)tls->inbuf;
			target = RECHDR_LEN + (0x100 * xhdr->len16_hi + xhdr->len16_lo);

			if (target > MAX_INBUF /* malformed input (too long) */
			 || xhdr->proto_maj != TLS_MAJ
			 || xhdr->proto_min != TLS_MIN
			) {
				sz = total < target ? total : target;
				bad_record_die(tls, expected, sz);
			}
			dbg("xhdr type:%d ver:%d.%d len:%d\n",
				xhdr->type, xhdr->proto_maj, xhdr->proto_min,
				0x100 * xhdr->len16_hi + xhdr->len16_lo
			);
		}
		/* if total >= target, we have a full packet (and possibly more)... */
		if (total - target >= 0)
			break;
		/* input buffer is grown only as needed */
		rem = tls->inbuf_size - total;
		if (rem == 0) {
			tls->inbuf_size += MAX_INBUF / 8;
			if (tls->inbuf_size > MAX_INBUF)
				tls->inbuf_size = MAX_INBUF;
			dbg("inbuf_size:%d\n", tls->inbuf_size);
			rem = tls->inbuf_size - total;
			tls->inbuf = xrealloc(tls->inbuf, tls->inbuf_size);
		}
		sz = safe_read(tls->ifd, tls->inbuf + total, rem);
		if (sz <= 0) {
			if (sz == 0 && total == 0) {
				/* "Abrupt" EOF, no TLS shutdown (seen from kernel.org) */
				dbg("EOF (without TLS shutdown) from peer\n");
				tls->buffered_size = 0;
				goto end;
			}
			bb_perror_msg_and_die("short read, have only %d", total);
		}
		dump_raw_in("<< %s\n", tls->inbuf + total, sz);
		total += sz;
	}
	tls->buffered_size = total - target;
	tls->ofs_to_buffered = target;
	//dbg("<< stashing at %d [%d] ", tls->ofs_to_buffered, tls->buffered_size);
	//dump_hex("<< %s\n", tls->inbuf + tls->ofs_to_buffered, tls->buffered_size);

	sz = target - RECHDR_LEN;

	/* Needs to be decrypted? */
	if (tls->min_encrypted_len_on_read > tls->MAC_size) {
		uint8_t *p = tls->inbuf + RECHDR_LEN;
		int padding_len;

		if (sz & (AES_BLOCKSIZE-1)
		 || sz < (int)tls->min_encrypted_len_on_read
		) {
			bb_error_msg_and_die("bad encrypted len:%u < %u",
				sz, tls->min_encrypted_len_on_read);
		}
		/* Decrypt content+MAC+padding, moving it over IV in the process */
		sz -= AES_BLOCKSIZE; /* we will overwrite IV now */
		aes_cbc_decrypt(
			tls->server_write_key, tls->key_size, /* selects 128/256 */
			p, /* IV */
			p + AES_BLOCKSIZE, sz, /* ciphertext */
			p /* plaintext */
		);
		padding_len = p[sz - 1];
		dbg("encrypted size:%u type:0x%02x padding_length:0x%02x\n", sz, p[0], padding_len);
		padding_len++;
		sz -= tls->MAC_size + padding_len; /* drop MAC and padding */
		//if (sz < 0)
		//	bb_error_msg_and_die("bad padding size:%u", padding_len);
	} else {
		/* if nonzero, then it's TLS_RSA_WITH_NULL_SHA256: drop MAC */
		/* else: no encryption yet on input, subtract zero = NOP */
		sz -= tls->min_encrypted_len_on_read;
	}
	if (sz < 0)
		bb_error_msg_and_die("encrypted data too short");

	//dump_hex("<< %s\n", tls->inbuf, RECHDR_LEN + sz);

	xhdr = (void*)tls->inbuf;
	if (xhdr->type == RECORD_TYPE_ALERT && sz >= 2) {
		uint8_t *p = tls->inbuf + RECHDR_LEN;
		dbg("ALERT size:%d level:%d description:%d\n", sz, p[0], p[1]);
		if (p[0] == 2) { /* fatal */
			bb_error_msg_and_die("TLS %s from peer (alert code %d): %s",
				"error",
				p[1], alert_text(p[1])
			);
		}
		if (p[0] == 1) { /* warning */
			if (p[1] == 0) { /* "close_notify" warning: it's EOF */
				dbg("EOF (TLS encoded) from peer\n");
				sz = 0;
				goto end;
			}
//This possibly needs to be cached and shown only if
//a fatal alert follows
//			bb_error_msg("TLS %s from peer (alert code %d): %s",
//				"warning",
//				p[1], alert_text(p[1])
//			);
			/* discard it, get next record */
			goto again;
		}
		/* p[0] not 1 or 2: not defined in protocol */
		sz = 0;
		goto end;
	}

	/* RFC 5246 is not saying it explicitly, but sha256 hash
	 * in our FINISHED record must include data of incoming packets too!
	 */
	if (tls->inbuf[0] == RECORD_TYPE_HANDSHAKE
	 && tls->MAC_size != 0 /* do we know which hash to use? (server_hello() does not!) */
	) {
		hash_handshake(tls, "<< hash:%s", tls->inbuf + RECHDR_LEN, sz);
	}
 end:
	dbg("got block len:%u\n", sz);
	return sz;
}

/*
 * DER parsing routines
 */
static unsigned get_der_len(uint8_t **bodyp, uint8_t *der, uint8_t *end)
{
	unsigned len, len1;

	if (end - der < 2)
		xfunc_die();
//	if ((der[0] & 0x1f) == 0x1f) /* not single-byte item code? */
//		xfunc_die();

	len = der[1]; /* maybe it's short len */
	if (len >= 0x80) {
		/* no, it's long */

		if (len == 0x80 || end - der < (int)(len - 0x7e)) {
			/* 0x80 is "0 bytes of len", invalid DER: must use short len if can */
			/* need 3 or 4 bytes for 81, 82 */
			xfunc_die();
		}

		len1 = der[2]; /* if (len == 0x81) it's "ii 81 xx", fetch xx */
		if (len > 0x82) {
			/* >0x82 is "3+ bytes of len", should not happen realistically */
			xfunc_die();
		}
		if (len == 0x82) { /* it's "ii 82 xx yy" */
			len1 = 0x100*len1 + der[3];
			der += 1; /* skip [yy] */
		}
		der += 1; /* skip [xx] */
		len = len1;
//		if (len < 0x80)
//			xfunc_die(); /* invalid DER: must use short len if can */
	}
	der += 2; /* skip [code]+[1byte] */

	if (end - der < (int)len)
		xfunc_die();
	*bodyp = der;

	return len;
}

static uint8_t *enter_der_item(uint8_t *der, uint8_t **endp)
{
	uint8_t *new_der;
	unsigned len = get_der_len(&new_der, der, *endp);
	dbg_der("entered der @%p:0x%02x len:%u inner_byte @%p:0x%02x\n", der, der[0], len, new_der, new_der[0]);
	/* Move "end" position to cover only this item */
	*endp = new_der + len;
	return new_der;
}

static uint8_t *skip_der_item(uint8_t *der, uint8_t *end)
{
	uint8_t *new_der;
	unsigned len = get_der_len(&new_der, der, end);
	/* Skip body */
	new_der += len;
	dbg_der("skipped der 0x%02x, next byte 0x%02x\n", der[0], new_der[0]);
	return new_der;
}

static void der_binary_to_pstm(pstm_int *pstm_n, uint8_t *der, uint8_t *end)
{
	uint8_t *bin_ptr;
	unsigned len = get_der_len(&bin_ptr, der, end);

	dbg_der("binary bytes:%u, first:0x%02x\n", len, bin_ptr[0]);
	pstm_init_for_read_unsigned_bin(/*pool:*/ NULL, pstm_n, len);
	pstm_read_unsigned_bin(pstm_n, bin_ptr, len);
	//return bin + len;
}

static void find_key_in_der_cert(tls_state_t *tls, uint8_t *der, int len)
{
/* Certificate is a DER-encoded data structure. Each DER element has a length,
 * which makes it easy to skip over large compound elements of any complexity
 * without parsing them. Example: partial decode of kernel.org certificate:
 *  SEQ 0x05ac/1452 bytes (Certificate): 308205ac
 *    SEQ 0x0494/1172 bytes (tbsCertificate): 30820494
 *      [ASN_CONTEXT_SPECIFIC | ASN_CONSTRUCTED | 0] 3 bytes: a003
 *        INTEGER (version): 0201 02
 *      INTEGER 0x11 bytes (serialNumber): 0211 00 9f85bf664b0cddafca508679501b2be4
 *      //^^^^^^note: matrixSSL also allows [ASN_CONTEXT_SPECIFIC | ASN_PRIMITIVE | 2] = 0x82 type
 *      SEQ 0x0d bytes (signatureAlgo): 300d
 *        OID 9 bytes: 0609 2a864886f70d01010b (OID_SHA256_RSA_SIG 42.134.72.134.247.13.1.1.11)
 *        NULL: 0500
 *      SEQ 0x5f bytes (issuer): 305f
 *        SET 11 bytes: 310b
 *          SEQ 9 bytes: 3009
 *            OID 3 bytes: 0603 550406
 *            Printable string "FR": 1302 4652
 *        SET 14 bytes: 310e
 *          SEQ 12 bytes: 300c
 *            OID 3 bytes: 0603 550408
 *            Printable string "Paris": 1305 5061726973
 *        SET 14 bytes: 310e
 *          SEQ 12 bytes: 300c
 *            OID 3 bytes: 0603 550407
 *            Printable string "Paris": 1305 5061726973
 *        SET 14 bytes: 310e
 *          SEQ 12 bytes: 300c
 *            OID 3 bytes: 0603 55040a
 *            Printable string "Gandi": 1305 47616e6469
 *        SET 32 bytes: 3120
 *          SEQ 30 bytes: 301e
 *            OID 3 bytes: 0603 550403
 *            Printable string "Gandi Standard SSL CA 2": 1317 47616e6469205374616e646172642053534c2043412032
 *      SEQ 30 bytes (validity): 301e
 *        TIME "161011000000Z": 170d 3136313031313030303030305a
 *        TIME "191011235959Z": 170d 3139313031313233353935395a
 *      SEQ 0x5b/91 bytes (subject): 305b //I did not decode this
 *          3121301f060355040b1318446f6d61696e20436f
 *          6e74726f6c2056616c6964617465643121301f06
 *          0355040b1318506f73697469766553534c204d75
 *          6c74692d446f6d61696e31133011060355040313
 *          0a6b65726e656c2e6f7267
 *      SEQ 0x01a2/418 bytes (subjectPublicKeyInfo): 308201a2
 *        SEQ 13 bytes (algorithm): 300d
 *          OID 9 bytes: 0609 2a864886f70d010101 (OID_RSA_KEY_ALG 42.134.72.134.247.13.1.1.1)
 *          NULL: 0500
 *        BITSTRING 0x018f/399 bytes (publicKey): 0382018f
 *          ????: 00
 *          //after the zero byte, it appears key itself uses DER encoding:
 *          SEQ 0x018a/394 bytes: 3082018a
 *            INTEGER 0x0181/385 bytes (modulus): 02820181
 *                  00b1ab2fc727a3bef76780c9349bf3
 *                  ...24 more blocks of 15 bytes each...
 *                  90e895291c6bc8693b65
 *            INTEGER 3 bytes (exponent): 0203 010001
 *      [ASN_CONTEXT_SPECIFIC | ASN_CONSTRUCTED | 0x3] 0x01e5 bytes (X509v3 extensions): a38201e5
 *        SEQ 0x01e1 bytes: 308201e1
 *        ...
 * Certificate is a sequence of three elements:
 *	tbsCertificate (SEQ)
 *	signatureAlgorithm (AlgorithmIdentifier)
 *	signatureValue (BIT STRING)
 *
 * In turn, tbsCertificate is a sequence of:
 *	version
 *	serialNumber
 *	signatureAlgo (AlgorithmIdentifier)
 *	issuer (Name, has complex structure)
 *	validity (Validity, SEQ of two Times)
 *	subject (Name)
 *	subjectPublicKeyInfo (SEQ)
 *	...
 *
 * subjectPublicKeyInfo is a sequence of:
 *	algorithm (AlgorithmIdentifier)
 *	publicKey (BIT STRING)
 *
 * We need Certificate.tbsCertificate.subjectPublicKeyInfo.publicKey
 */
	uint8_t *end = der + len;
	uint8_t tag_class, pc, tag_number;
	int version_present;

	/* enter "Certificate" item: [der, end) will be only Cert */
	der = enter_der_item(der, &end);

	/* enter "tbsCertificate" item: [der, end) will be only tbsCert */
	der = enter_der_item(der, &end);

	/*
	 * Skip version field only if it is present. For a v1 certificate, the
	 * version field won't be present since v1 is the default value for the
	 * version field and fields with default values should be omitted (see
	 * RFC 5280 sections 4.1 and 4.1.2.1). If the version field is present
	 * it will have a tag class of 2 (context-specific), bit 6 as 1
	 * (constructed), and a tag number of 0 (see ITU-T X.690 sections 8.1.2
	 * and 8.14).
	 */
	tag_class = der[0] >> 6; /* bits 8-7 */
	pc = (der[0] & 32) >> 5; /* bit 6 */
	tag_number = der[0] & 31; /* bits 5-1 */
	version_present = tag_class == 2 && pc == 1 && tag_number == 0;
	if (version_present) {
		der = skip_der_item(der, end); /* version */
	}

	/* skip up to subjectPublicKeyInfo */
	der = skip_der_item(der, end); /* serialNumber */
	der = skip_der_item(der, end); /* signatureAlgo */
	der = skip_der_item(der, end); /* issuer */
	der = skip_der_item(der, end); /* validity */
	der = skip_der_item(der, end); /* subject */

	/* enter subjectPublicKeyInfo */
	der = enter_der_item(der, &end);
	{ /* check subjectPublicKeyInfo.algorithm */
		static const uint8_t expected[] = {
			0x30,0x0d, // SEQ 13 bytes
			0x06,0x09, 0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01, // OID RSA_KEY_ALG 42.134.72.134.247.13.1.1.1
			//0x05,0x00, // NULL
		};
		if (memcmp(der, expected, sizeof(expected)) != 0)
			bb_error_msg_and_die("not RSA key");
	}
	/* skip subjectPublicKeyInfo.algorithm */
	der = skip_der_item(der, end);
	/* enter subjectPublicKeyInfo.publicKey */
//	die_if_not_this_der_type(der, end, 0x03); /* must be BITSTRING */
	der = enter_der_item(der, &end);

	/* parse RSA key: */
//based on getAsnRsaPubKey(), pkcs1ParsePrivBin() is also of note
	dbg("key bytes:%u, first:0x%02x\n", (int)(end - der), der[0]);
	if (end - der < 14) xfunc_die();
	/* example format:
	 * ignore bits: 00
	 * SEQ 0x018a/394 bytes: 3082018a
	 *   INTEGER 0x0181/385 bytes (modulus): 02820181 XX...XXX
	 *   INTEGER 3 bytes (exponent): 0203 010001
	 */
	if (*der != 0) /* "ignore bits", should be 0 */
		xfunc_die();
	der++;
	der = enter_der_item(der, &end); /* enter SEQ */
	/* memset(tls->hsd->server_rsa_pub_key, 0, sizeof(tls->hsd->server_rsa_pub_key)); - already is */
	der_binary_to_pstm(&tls->hsd->server_rsa_pub_key.N, der, end); /* modulus */
	der = skip_der_item(der, end);
	der_binary_to_pstm(&tls->hsd->server_rsa_pub_key.e, der, end); /* exponent */
	tls->hsd->server_rsa_pub_key.size = pstm_unsigned_bin_size(&tls->hsd->server_rsa_pub_key.N);
	dbg("server_rsa_pub_key.size:%d\n", tls->hsd->server_rsa_pub_key.size);
}

/*
 * TLS Handshake routines
 */
static int tls_xread_handshake_block(tls_state_t *tls, int min_len)
{
	struct record_hdr *xhdr;
	int len = tls_xread_record(tls, "handshake record");

	xhdr = (void*)tls->inbuf;
	if (len < min_len
	 || xhdr->type != RECORD_TYPE_HANDSHAKE
	) {
		bad_record_die(tls, "handshake record", len);
	}
	dbg("got HANDSHAKE\n");
	return len;
}

static ALWAYS_INLINE void fill_handshake_record_hdr(void *buf, unsigned type, unsigned len)
{
	struct handshake_hdr {
		uint8_t type;
		uint8_t len24_hi, len24_mid, len24_lo;
	} *h = buf;

	len -= 4;
	h->type = type;
	h->len24_hi  = len >> 16;
	h->len24_mid = len >> 8;
	h->len24_lo  = len & 0xff;
}

static void send_client_hello_and_alloc_hsd(tls_state_t *tls, const char *sni)
{
	struct client_hello {
		uint8_t type;
		uint8_t len24_hi, len24_mid, len24_lo;
		uint8_t proto_maj, proto_min;
		uint8_t rand32[32];
		uint8_t session_id_len;
		/* uint8_t session_id[]; */
		uint8_t cipherid_len16_hi, cipherid_len16_lo;
		uint8_t cipherid[2 * (2 + !!CIPHER_ID2)]; /* actually variable */
		uint8_t comprtypes_len;
		uint8_t comprtypes[1]; /* actually variable */
		/* Extensions (SNI shown):
		 * hi,lo // len of all extensions
		 *   00,00 // extension_type: "Server Name"
		 *   00,0e // list len (there can be more than one SNI)
		 *     00,0c // len of 1st Server Name Indication
		 *       00    // name type: host_name
		 *       00,09   // name len
		 *       "localhost" // name
		 */
// GNU Wget 1.18 to cdn.kernel.org sends these extensions:
// 0055
//   0005 0005 0100000000 - status_request
//   0000 0013 0011 00 000e 63646e 2e 6b65726e656c 2e 6f7267 - server_name
//   ff01 0001 00 - renegotiation_info
//   0023 0000 - session_ticket
//   000a 0008 0006001700180019 - supported_groups
//   000b 0002 0100 - ec_point_formats
//   000d 0016 0014 0401 0403 0501 0503 0601 0603 0301 0303 0201 0203 - signature_algorithms
// wolfssl library sends this option, RFC 7627 (closes a security weakness, some servers may require it. TODO?):
//   0017 0000 - extended master secret
	};
	struct client_hello *record;
	int len;
	int sni_len = sni ? strnlen(sni, 127 - 9) : 0;

	len = sizeof(*record);
	if (sni_len)
		len += 11 + sni_len;
	record = tls_get_outbuf(tls, len);
	memset(record, 0, len);

	fill_handshake_record_hdr(record, HANDSHAKE_CLIENT_HELLO, len);
	record->proto_maj = TLS_MAJ;	/* the "requested" version of the protocol, */
	record->proto_min = TLS_MIN;	/* can be higher than one in record headers */
	tls_get_random(record->rand32, sizeof(record->rand32));
	if (TLS_DEBUG_FIXED_SECRETS)
		memset(record->rand32, 0x11, sizeof(record->rand32));
	/* record->session_id_len = 0; - already is */

	/* record->cipherid_len16_hi = 0; */
	record->cipherid_len16_lo = sizeof(record->cipherid);
	/* RFC 5746 Renegotiation Indication Extension - some servers will refuse to work with us otherwise */
	/*record->cipherid[0] = TLS_EMPTY_RENEGOTIATION_INFO_SCSV >> 8; - zero */
	record->cipherid[1] = TLS_EMPTY_RENEGOTIATION_INFO_SCSV & 0xff;
	if ((CIPHER_ID1 >> 8) != 0) record->cipherid[2] = CIPHER_ID1 >> 8;
	/*************************/ record->cipherid[3] = CIPHER_ID1 & 0xff;
#if CIPHER_ID2
	if ((CIPHER_ID2 >> 8) != 0) record->cipherid[4] = CIPHER_ID2 >> 8;
	/*************************/ record->cipherid[5] = CIPHER_ID2 & 0xff;
#endif

	record->comprtypes_len = 1;
	/* record->comprtypes[0] = 0; */

	if (sni_len) {
		uint8_t *p = (void*)(record + 1);
		//p[0] = 0;         //
		p[1] = sni_len + 9; //ext_len
		//p[2] = 0;             //
		//p[3] = 0;             //extension_type
		//p[4] = 0;         //
		p[5] = sni_len + 5; //list len
		//p[6] = 0;             //
		p[7] = sni_len + 3;     //len of 1st SNI
		//p[8] = 0;         //name type
		//p[9] = 0;             //
		p[10] = sni_len;        //name len
		memcpy(&p[11], sni, sni_len);
	}

	dbg(">> CLIENT_HELLO\n");
	/* Can hash it only when we know which MAC hash to use */
	/*xwrite_and_update_handshake_hash(tls, len); - WRONG! */
	xwrite_handshake_record(tls, len);

	tls->hsd = xzalloc(sizeof(*tls->hsd) + len);
	tls->hsd->saved_client_hello_size = len;
	memcpy(tls->hsd->saved_client_hello, record, len);
	memcpy(tls->hsd->client_and_server_rand32, record->rand32, sizeof(record->rand32));
}

static void get_server_hello(tls_state_t *tls)
{
	struct server_hello {
		struct record_hdr xhdr;
		uint8_t type;
		uint8_t len24_hi, len24_mid, len24_lo;
		uint8_t proto_maj, proto_min;
		uint8_t rand32[32]; /* first 4 bytes are unix time in BE format */
		uint8_t session_id_len;
		uint8_t session_id[32];
		uint8_t cipherid_hi, cipherid_lo;
		uint8_t comprtype;
		/* extensions may follow, but only those which client offered in its Hello */
	};

	struct server_hello *hp;
	uint8_t *cipherid;
	unsigned cipher;
	int len, len24;

	len = tls_xread_handshake_block(tls, 74 - 32);

	hp = (void*)tls->inbuf;
	// 74 bytes:
	// 02  000046 03|03   58|78|cf|c1 50|a5|49|ee|7e|29|48|71|fe|97|fa|e8|2d|19|87|72|90|84|9d|37|a3|f0|cb|6f|5f|e3|3c|2f |20  |d8|1a|78|96|52|d6|91|01|24|b3|d6|5b|b7|d0|6c|b3|e1|78|4e|3c|95|de|74|a0|ba|eb|a7|3a|ff|bd|a2|bf |00|9c |00|
	//SvHl len=70 maj.min unixtime^^^ 28randbytes^^^^^^^^^^^^^^^^^^^^^^^^^^^^_^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^_^^^ slen sid32bytes^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ cipSel comprSel
	if (hp->type != HANDSHAKE_SERVER_HELLO
	 || hp->len24_hi  != 0
	 || hp->len24_mid != 0
	 /* hp->len24_lo checked later */
	 || hp->proto_maj != TLS_MAJ
	 || hp->proto_min != TLS_MIN
	) {
		bad_record_die(tls, "'server hello'", len);
	}

	cipherid = &hp->cipherid_hi;
	len24 = hp->len24_lo;
	if (hp->session_id_len != 32) {
		if (hp->session_id_len != 0)
			bad_record_die(tls, "'server hello'", len);

		// session_id_len == 0: no session id
		// "The server
		// may return an empty session_id to indicate that the session will
		// not be cached and therefore cannot be resumed."
		cipherid -= 32;
		len24 += 32; /* what len would be if session id would be present */
	}

	if (len24 < 70
//	 || cipherid[0]  != (CIPHER_ID >> 8)
//	 || cipherid[1]  != (CIPHER_ID & 0xff)
//	 || cipherid[2]  != 0 /* comprtype */
	) {
		bad_record_die(tls, "'server hello'", len);
	}
	dbg("<< SERVER_HELLO\n");

	memcpy(tls->hsd->client_and_server_rand32 + 32, hp->rand32, sizeof(hp->rand32));

	tls->cipher_id = cipher = 0x100 * cipherid[0] + cipherid[1];
	dbg("server chose cipher %04x\n", cipher);

	if (cipher == TLS_RSA_WITH_AES_128_CBC_SHA) {
		tls->key_size = AES128_KEYSIZE;
		tls->MAC_size = SHA1_OUTSIZE;
	}
	else { /* TLS_RSA_WITH_AES_256_CBC_SHA256 */
		tls->key_size = AES256_KEYSIZE;
		tls->MAC_size = SHA256_OUTSIZE;
	}
	/* Handshake hash eventually destined to FINISHED record
	 * is sha256 regardless of cipher
	 * (at least for all ciphers defined by RFC5246).
	 * It's not sha1 for AES_128_CBC_SHA - only MAC is sha1, not this hash.
	 */
	sha256_begin(&tls->hsd->handshake_hash_ctx);
	hash_handshake(tls, ">> client hello hash:%s",
		tls->hsd->saved_client_hello, tls->hsd->saved_client_hello_size
	);
	hash_handshake(tls, "<< server hello hash:%s",
		tls->inbuf + RECHDR_LEN, len
	);
}

static void get_server_cert(tls_state_t *tls)
{
	struct record_hdr *xhdr;
	uint8_t *certbuf;
	int len, len1;

	len = tls_xread_handshake_block(tls, 10);

	xhdr = (void*)tls->inbuf;
	certbuf = (void*)(xhdr + 1);
	if (certbuf[0] != HANDSHAKE_CERTIFICATE)
		bad_record_die(tls, "certificate", len);
	dbg("<< CERTIFICATE\n");
	// 4392 bytes:
	// 0b  00|11|24 00|11|21 00|05|b0 30|82|05|ac|30|82|04|94|a0|03|02|01|02|02|11|00|9f|85|bf|66|4b|0c|dd|af|ca|50|86|79|50|1b|2b|e4|30|0d...
	//Cert len=4388 ChainLen CertLen^ DER encoded X509 starts here. openssl x509 -in FILE -inform DER -noout -text
	len1 = get24be(certbuf + 1);
	if (len1 > len - 4) tls_error_die(tls);
	len = len1;
	len1 = get24be(certbuf + 4);
	if (len1 > len - 3) tls_error_die(tls);
	len = len1;
	len1 = get24be(certbuf + 7);
	if (len1 > len - 3) tls_error_die(tls);
	len = len1;

	if (len)
		find_key_in_der_cert(tls, certbuf + 10, len);
}

static void send_empty_client_cert(tls_state_t *tls)
{
	struct client_empty_cert {
		uint8_t type;
		uint8_t len24_hi, len24_mid, len24_lo;
		uint8_t cert_chain_len24_hi, cert_chain_len24_mid, cert_chain_len24_lo;
	};
	struct client_empty_cert *record;

	record = tls_get_outbuf(tls, sizeof(*record));
//FIXME: can just memcpy a ready-made one.
	fill_handshake_record_hdr(record, HANDSHAKE_CERTIFICATE, sizeof(*record));
	record->cert_chain_len24_hi = 0;
	record->cert_chain_len24_mid = 0;
	record->cert_chain_len24_lo = 0;

	dbg(">> CERTIFICATE\n");
	xwrite_and_update_handshake_hash(tls, sizeof(*record));
}

static void send_client_key_exchange(tls_state_t *tls)
{
	struct client_key_exchange {
		uint8_t type;
		uint8_t len24_hi, len24_mid, len24_lo;
		/* keylen16 exists for RSA (in TLS, not in SSL), but not for some other key types */
		uint8_t keylen16_hi, keylen16_lo;
		uint8_t key[4 * 1024]; // size??
	};
//FIXME: better size estimate
	struct client_key_exchange *record = tls_get_outbuf(tls, sizeof(*record));
	uint8_t rsa_premaster[RSA_PREMASTER_SIZE];
	int len;

	tls_get_random(rsa_premaster, sizeof(rsa_premaster));
	if (TLS_DEBUG_FIXED_SECRETS)
		memset(rsa_premaster, 0x44, sizeof(rsa_premaster));
	// RFC 5246
	// "Note: The version number in the PreMasterSecret is the version
	// offered by the client in the ClientHello.client_version, not the
	// version negotiated for the connection."
	rsa_premaster[0] = TLS_MAJ;
	rsa_premaster[1] = TLS_MIN;
	dump_hex("premaster:%s\n", rsa_premaster, sizeof(rsa_premaster));
	len = psRsaEncryptPub(/*pool:*/ NULL,
		/* psRsaKey_t* */ &tls->hsd->server_rsa_pub_key,
		rsa_premaster, /*inlen:*/ sizeof(rsa_premaster),
		record->key, sizeof(record->key),
		data_param_ignored
	);
	record->keylen16_hi = len >> 8;
	record->keylen16_lo = len & 0xff;
	len += 2;
	record->type = HANDSHAKE_CLIENT_KEY_EXCHANGE;
	record->len24_hi  = 0;
	record->len24_mid = len >> 8;
	record->len24_lo  = len & 0xff;
	len += 4;

	dbg(">> CLIENT_KEY_EXCHANGE\n");
	xwrite_and_update_handshake_hash(tls, len);

	// RFC 5246
	// For all key exchange methods, the same algorithm is used to convert
	// the pre_master_secret into the master_secret.  The pre_master_secret
	// should be deleted from memory once the master_secret has been
	// computed.
	//      master_secret = PRF(pre_master_secret, "master secret",
	//                          ClientHello.random + ServerHello.random)
	//                          [0..47];
	// The master secret is always exactly 48 bytes in length.  The length
	// of the premaster secret will vary depending on key exchange method.
	prf_hmac_sha256(/*tls,*/
		tls->hsd->master_secret, sizeof(tls->hsd->master_secret),
		rsa_premaster, sizeof(rsa_premaster),
		"master secret",
		tls->hsd->client_and_server_rand32, sizeof(tls->hsd->client_and_server_rand32)
	);
	dump_hex("master secret:%s\n", tls->hsd->master_secret, sizeof(tls->hsd->master_secret));

	// RFC 5246
	// 6.3.  Key Calculation
	//
	// The Record Protocol requires an algorithm to generate keys required
	// by the current connection state (see Appendix A.6) from the security
	// parameters provided by the handshake protocol.
	//
	// The master secret is expanded into a sequence of secure bytes, which
	// is then split to a client write MAC key, a server write MAC key, a
	// client write encryption key, and a server write encryption key.  Each
	// of these is generated from the byte sequence in that order.  Unused
	// values are empty.  Some AEAD ciphers may additionally require a
	// client write IV and a server write IV (see Section 6.2.3.3).
	//
	// When keys and MAC keys are generated, the master secret is used as an
	// entropy source.
	//
	// To generate the key material, compute
	//
	//    key_block = PRF(SecurityParameters.master_secret,
	//                    "key expansion",
	//                    SecurityParameters.server_random +
	//                    SecurityParameters.client_random);
	//
	// until enough output has been generated.  Then, the key_block is
	// partitioned as follows:
	//
	//    client_write_MAC_key[SecurityParameters.mac_key_length]
	//    server_write_MAC_key[SecurityParameters.mac_key_length]
	//    client_write_key[SecurityParameters.enc_key_length]
	//    server_write_key[SecurityParameters.enc_key_length]
	//    client_write_IV[SecurityParameters.fixed_iv_length]
	//    server_write_IV[SecurityParameters.fixed_iv_length]
	{
		uint8_t tmp64[64];

		/* make "server_rand32 + client_rand32" */
		memcpy(&tmp64[0] , &tls->hsd->client_and_server_rand32[32], 32);
		memcpy(&tmp64[32], &tls->hsd->client_and_server_rand32[0] , 32);

		prf_hmac_sha256(/*tls,*/
			tls->client_write_MAC_key, 2 * (tls->MAC_size + tls->key_size),
			// also fills:
			// server_write_MAC_key[]
			// client_write_key[]
			// server_write_key[]
			tls->hsd->master_secret, sizeof(tls->hsd->master_secret),
			"key expansion",
			tmp64, 64
		);
		tls->client_write_key = tls->client_write_MAC_key + (2 * tls->MAC_size);
		tls->server_write_key = tls->client_write_key + tls->key_size;
		dump_hex("client_write_MAC_key:%s\n",
			tls->client_write_MAC_key, tls->MAC_size
		);
		dump_hex("client_write_key:%s\n",
			tls->client_write_key, tls->key_size
		);
	}
}

static const uint8_t rec_CHANGE_CIPHER_SPEC[] = {
	RECORD_TYPE_CHANGE_CIPHER_SPEC, TLS_MAJ, TLS_MIN, 00, 01,
	01
};

static void send_change_cipher_spec(tls_state_t *tls)
{
	dbg(">> CHANGE_CIPHER_SPEC\n");
	xwrite(tls->ofd, rec_CHANGE_CIPHER_SPEC, sizeof(rec_CHANGE_CIPHER_SPEC));
}

// 7.4.9.  Finished
// A Finished message is always sent immediately after a change
// cipher spec message to verify that the key exchange and
// authentication processes were successful.  It is essential that a
// change cipher spec message be received between the other handshake
// messages and the Finished message.
//...
// The Finished message is the first one protected with the just
// negotiated algorithms, keys, and secrets.  Recipients of Finished
// messages MUST verify that the contents are correct.  Once a side
// has sent its Finished message and received and validated the
// Finished message from its peer, it may begin to send and receive
// application data over the connection.
//...
// struct {
//     opaque verify_data[verify_data_length];
// } Finished;
//
// verify_data
//    PRF(master_secret, finished_label, Hash(handshake_messages))
//       [0..verify_data_length-1];
//
// finished_label
//    For Finished messages sent by the client, the string
//    "client finished".  For Finished messages sent by the server,
//    the string "server finished".
//
// Hash denotes a Hash of the handshake messages.  For the PRF
// defined in Section 5, the Hash MUST be the Hash used as the basis
// for the PRF.  Any cipher suite which defines a different PRF MUST
// also define the Hash to use in the Finished computation.
//
// In previous versions of TLS, the verify_data was always 12 octets
// long.  In the current version of TLS, it depends on the cipher
// suite.  Any cipher suite which does not explicitly specify
// verify_data_length has a verify_data_length equal to 12.  This
// includes all existing cipher suites.
static void send_client_finished(tls_state_t *tls)
{
	struct finished {
		uint8_t type;
		uint8_t len24_hi, len24_mid, len24_lo;
		uint8_t prf_result[12];
	};
	struct finished *record = tls_get_outbuf(tls, sizeof(*record));
	uint8_t handshake_hash[TLS_MAX_MAC_SIZE];
	unsigned len;

	fill_handshake_record_hdr(record, HANDSHAKE_FINISHED, sizeof(*record));

	len = get_handshake_hash(tls, handshake_hash);
	prf_hmac_sha256(/*tls,*/
		record->prf_result, sizeof(record->prf_result),
		tls->hsd->master_secret, sizeof(tls->hsd->master_secret),
		"client finished",
		handshake_hash, len
	);
	dump_hex("from secret: %s\n", tls->hsd->master_secret, sizeof(tls->hsd->master_secret));
	dump_hex("from labelSeed: %s", "client finished", sizeof("client finished")-1);
	dump_hex("%s\n", handshake_hash, sizeof(handshake_hash));
	dump_hex("=> digest: %s\n", record->prf_result, sizeof(record->prf_result));

	dbg(">> FINISHED\n");
	xwrite_encrypted(tls, sizeof(*record), RECORD_TYPE_HANDSHAKE);
}

void FAST_FUNC tls_handshake(tls_state_t *tls, const char *sni)
{
	// Client              RFC 5246                Server
	// (*) - optional messages, not always sent
	//
	// ClientHello          ------->
	//                                        ServerHello
	//                                       Certificate*
	//                                 ServerKeyExchange*
	//                                CertificateRequest*
	//                      <-------      ServerHelloDone
	// Certificate*
	// ClientKeyExchange
	// CertificateVerify*
	// [ChangeCipherSpec]
	// Finished             ------->
	//                                 [ChangeCipherSpec]
	//                      <-------             Finished
	// Application Data     <------>     Application Data
	int len;
	int got_cert_req;

	send_client_hello_and_alloc_hsd(tls, sni);
	get_server_hello(tls);

	// RFC 5246
	// The server MUST send a Certificate message whenever the agreed-
	// upon key exchange method uses certificates for authentication
	// (this includes all key exchange methods defined in this document
	// except DH_anon).  This message will always immediately follow the
	// ServerHello message.
	//
	// IOW: in practice, Certificate *always* follows.
	// (for example, kernel.org does not even accept DH_anon cipher id)
	get_server_cert(tls);

	len = tls_xread_handshake_block(tls, 4);
	if (tls->inbuf[RECHDR_LEN] == HANDSHAKE_SERVER_KEY_EXCHANGE) {
		// 459 bytes:
		// 0c   00|01|c7 03|00|17|41|04|87|94|2e|2f|68|d0|c9|f4|97|a8|2d|ef|ed|67|ea|c6|f3|b3|56|47|5d|27|b6|bd|ee|70|25|30|5e|b0|8e|f6|21|5a...
		//SvKey len=455^
		// with TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA: 461 bytes:
		// 0c   00|01|c9 03|00|17|41|04|cd|9b|b4|29|1f|f6|b0|c2|84|82|7f|29|6a|47|4e|ec|87|0b|c1|9c|69|e1|f8|c6|d0|53|e9|27|90|a5|c8|02|15|75...
		dbg("<< SERVER_KEY_EXCHANGE len:%u\n", len);
//probably need to save it
		len = tls_xread_handshake_block(tls, 4);
	}

	got_cert_req = (tls->inbuf[RECHDR_LEN] == HANDSHAKE_CERTIFICATE_REQUEST);
	if (got_cert_req) {
		dbg("<< CERTIFICATE_REQUEST\n");
		// RFC 5246: "If no suitable certificate is available,
		// the client MUST send a certificate message containing no
		// certificates.  That is, the certificate_list structure has a
		// length of zero. ...
		// Client certificates are sent using the Certificate structure
		// defined in Section 7.4.2."
		// (i.e. the same format as server certs)

		/*send_empty_client_cert(tls); - WRONG (breaks handshake hash calc) */
		/* need to hash _all_ server replies first, up to ServerHelloDone */
		len = tls_xread_handshake_block(tls, 4);
	}

	if (tls->inbuf[RECHDR_LEN] != HANDSHAKE_SERVER_HELLO_DONE) {
		bad_record_die(tls, "'server hello done'", len);
	}
	// 0e 000000 (len:0)
	dbg("<< SERVER_HELLO_DONE\n");

	if (got_cert_req)
		send_empty_client_cert(tls);

	send_client_key_exchange(tls);

	send_change_cipher_spec(tls);
	/* from now on we should send encrypted */
	/* tls->write_seq64_be = 0; - already is */
	tls->encrypt_on_write = 1;

	send_client_finished(tls);

	/* Get CHANGE_CIPHER_SPEC */
	len = tls_xread_record(tls, "switch to encrypted traffic");
	if (len != 1 || memcmp(tls->inbuf, rec_CHANGE_CIPHER_SPEC, 6) != 0)
		bad_record_die(tls, "switch to encrypted traffic", len);
	dbg("<< CHANGE_CIPHER_SPEC\n");
	if (CIPHER_ID1 == TLS_RSA_WITH_NULL_SHA256
	 && tls->cipher_id == TLS_RSA_WITH_NULL_SHA256
	) {
		tls->min_encrypted_len_on_read = tls->MAC_size;
	} else {
		unsigned mac_blocks = (unsigned)(tls->MAC_size + AES_BLOCKSIZE-1) / AES_BLOCKSIZE;
		/* all incoming packets now should be encrypted and have
		 * at least IV + (MAC padded to blocksize):
		 */
		tls->min_encrypted_len_on_read = AES_BLOCKSIZE + (mac_blocks * AES_BLOCKSIZE);
		dbg("min_encrypted_len_on_read: %u", tls->min_encrypted_len_on_read);
	}

	/* Get (encrypted) FINISHED from the server */
	len = tls_xread_record(tls, "'server finished'");
	if (len < 4 || tls->inbuf[RECHDR_LEN] != HANDSHAKE_FINISHED)
		bad_record_die(tls, "'server finished'", len);
	dbg("<< FINISHED\n");
	/* application data can be sent/received */

	/* free handshake data */
//	if (PARANOIA)
//		memset(tls->hsd, 0, tls->hsd->hsd_size);
	free(tls->hsd);
	tls->hsd = NULL;
}

static void tls_xwrite(tls_state_t *tls, int len)
{
	dbg(">> DATA\n");
	xwrite_encrypted(tls, len, RECORD_TYPE_APPLICATION_DATA);
}

// To run a test server using openssl:
// openssl req -x509 -newkey rsa:$((4096/4*3)) -keyout key.pem -out server.pem -nodes -days 99999 -subj '/CN=localhost'
// openssl s_server -key key.pem -cert server.pem -debug -tls1_2 -no_tls1 -no_tls1_1
//
// Unencryped SHA256 example:
// openssl req -x509 -newkey rsa:$((4096/4*3)) -keyout key.pem -out server.pem -nodes -days 99999 -subj '/CN=localhost'
// openssl s_server -key key.pem -cert server.pem -debug -tls1_2 -no_tls1 -no_tls1_1 -cipher NULL
// openssl s_client -connect 127.0.0.1:4433 -debug -tls1_2 -no_tls1 -no_tls1_1 -cipher NULL-SHA256

void FAST_FUNC tls_run_copy_loop(tls_state_t *tls, unsigned flags)
{
	int inbuf_size;
	const int INBUF_STEP = 4 * 1024;
	struct pollfd pfds[2];

	pfds[0].fd = STDIN_FILENO;
	pfds[0].events = POLLIN;
	pfds[1].fd = tls->ifd;
	pfds[1].events = POLLIN;

	inbuf_size = INBUF_STEP;
	for (;;) {
		int nread;

		if (safe_poll(pfds, 2, -1) < 0)
			bb_perror_msg_and_die("poll");

		if (pfds[0].revents) {
			void *buf;

			dbg("STDIN HAS DATA\n");
			buf = tls_get_outbuf(tls, inbuf_size);
			nread = safe_read(STDIN_FILENO, buf, inbuf_size);
			if (nread < 1) {
				/* We'd want to do this: */
				/* Close outgoing half-connection so they get EOF,
				 * but leave incoming alone so we can see response
				 */
				//shutdown(tls->ofd, SHUT_WR);
				/* But TLS has no way to encode this,
				 * doubt it's ok to do it "raw"
				 */
				pfds[0].fd = -1;
				tls_free_outbuf(tls); /* mem usage optimization */
				if (flags & TLSLOOP_EXIT_ON_LOCAL_EOF)
					break;
			} else {
				if (nread == inbuf_size) {
					/* TLS has per record overhead, if input comes fast,
					 * read, encrypt and send bigger chunks
					 */
					inbuf_size += INBUF_STEP;
					if (inbuf_size > TLS_MAX_OUTBUF)
						inbuf_size = TLS_MAX_OUTBUF;
				}
				tls_xwrite(tls, nread);
			}
		}
		if (pfds[1].revents) {
			dbg("NETWORK HAS DATA\n");
 read_record:
			nread = tls_xread_record(tls, "encrypted data");
			if (nread < 1) {
				/* TLS protocol has no real concept of one-sided shutdowns:
				 * if we get "TLS EOF" from the peer, writes will fail too
				 */
				//pfds[1].fd = -1;
				//close(STDOUT_FILENO);
				//tls_free_inbuf(tls); /* mem usage optimization */
				//continue;
				break;
			}
			if (tls->inbuf[0] != RECORD_TYPE_APPLICATION_DATA)
				bad_record_die(tls, "encrypted data", nread);
			xwrite(STDOUT_FILENO, tls->inbuf + RECHDR_LEN, nread);
			/* We may already have a complete next record buffered,
			 * can process it without network reads (and possible blocking)
			 */
			if (tls_has_buffered_record(tls))
				goto read_record;
		}
	}
}
