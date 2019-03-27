/*	$NetBSD: print-ah.c,v 1.4 1996/05/20 00:41:16 fvdl Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* \summary: IPSEC Encapsulating Security Payload (ESP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>
#include <stdlib.h>

/* Any code in this file that depends on HAVE_LIBCRYPTO depends on
 * HAVE_OPENSSL_EVP_H too. Undefining the former when the latter isn't defined
 * is the simplest way of handling the dependency.
 */
#ifdef HAVE_LIBCRYPTO
#ifdef HAVE_OPENSSL_EVP_H
#include <openssl/evp.h>
#else
#undef HAVE_LIBCRYPTO
#endif
#endif

#include "netdissect.h"
#include "strtoaddr.h"
#include "extract.h"

#include "ascii_strcasecmp.h"

#include "ip.h"
#include "ip6.h"

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * RFC1827/2406 Encapsulated Security Payload.
 */

struct newesp {
	uint32_t	esp_spi;	/* ESP */
	uint32_t	esp_seq;	/* Sequence number */
	/*variable size*/		/* (IV and) Payload data */
	/*variable size*/		/* padding */
	/*8bit*/			/* pad size */
	/*8bit*/			/* next header */
	/*8bit*/			/* next header */
	/*variable size, 32bit bound*/	/* Authentication data */
};

#ifdef HAVE_LIBCRYPTO
union inaddr_u {
	struct in_addr in4;
	struct in6_addr in6;
};
struct sa_list {
	struct sa_list	*next;
	u_int		daddr_version;
	union inaddr_u	daddr;
	uint32_t	spi;          /* if == 0, then IKEv2 */
	int             initiator;
	u_char          spii[8];      /* for IKEv2 */
	u_char          spir[8];
	const EVP_CIPHER *evp;
	int		ivlen;
	int		authlen;
	u_char          authsecret[256];
	int             authsecret_len;
	u_char		secret[256];  /* is that big enough for all secrets? */
	int		secretlen;
};

#ifndef HAVE_EVP_CIPHER_CTX_NEW
/*
 * Allocate an EVP_CIPHER_CTX.
 * Used if we have an older version of OpenSSL that doesn't provide
 * routines to allocate and free them.
 */
static EVP_CIPHER_CTX *
EVP_CIPHER_CTX_new(void)
{
	EVP_CIPHER_CTX *ctx;

	ctx = malloc(sizeof(*ctx));
	if (ctx == NULL)
		return (NULL);
	memset(ctx, 0, sizeof(*ctx));
	return (ctx);
}

static void
EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *ctx)
{
	EVP_CIPHER_CTX_cleanup(ctx);
	free(ctx);
}
#endif

#ifdef HAVE_EVP_CIPHERINIT_EX
/*
 * Initialize the cipher by calling EVP_CipherInit_ex(), because
 * calling EVP_CipherInit() will reset the cipher context, clearing
 * the cipher, so calling it twice, with the second call having a
 * null cipher, will clear the already-set cipher.  EVP_CipherInit_ex(),
 * however, won't reset the cipher context, so you can use it to specify
 * the IV oin a second call after a first call to EVP_CipherInit_ex()
 * to set the cipher and the key.
 *
 * XXX - is there some reason why we need to make two calls?
 */
static int
set_cipher_parameters(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
		      const unsigned char *key,
		      const unsigned char *iv, int enc)
{
	return EVP_CipherInit_ex(ctx, cipher, NULL, key, iv, enc);
}
#else
/*
 * Initialize the cipher by calling EVP_CipherInit(), because we don't
 * have EVP_CipherInit_ex(); we rely on it not trashing the context.
 */
static int
set_cipher_parameters(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
		      const unsigned char *key,
		      const unsigned char *iv, int enc)
{
	return EVP_CipherInit(ctx, cipher, key, iv, enc);
}
#endif

/*
 * this will adjust ndo_packetp and ndo_snapend to new buffer!
 */
USES_APPLE_DEPRECATED_API
int esp_print_decrypt_buffer_by_ikev2(netdissect_options *ndo,
				      int initiator,
				      u_char spii[8], u_char spir[8],
				      const u_char *buf, const u_char *end)
{
	struct sa_list *sa;
	const u_char *iv;
	unsigned int len;
	EVP_CIPHER_CTX *ctx;
	unsigned int block_size, output_buffer_size;
	u_char *output_buffer;

	/* initiator arg is any non-zero value */
	if(initiator) initiator=1;

	/* see if we can find the SA, and if so, decode it */
	for (sa = ndo->ndo_sa_list_head; sa != NULL; sa = sa->next) {
		if (sa->spi == 0
		    && initiator == sa->initiator
		    && memcmp(spii, sa->spii, 8) == 0
		    && memcmp(spir, sa->spir, 8) == 0)
			break;
	}

	if(sa == NULL) return 0;
	if(sa->evp == NULL) return 0;

	/*
	 * remove authenticator, and see if we still have something to
	 * work with
	 */
	end = end - sa->authlen;
	iv  = buf;
	buf = buf + sa->ivlen;
	len = end-buf;

	if(end <= buf) return 0;

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return 0;
	if (set_cipher_parameters(ctx, sa->evp, sa->secret, NULL, 0) < 0)
		(*ndo->ndo_warning)(ndo, "espkey init failed");
	set_cipher_parameters(ctx, NULL, NULL, iv, 0);
	/*
	 * Allocate a buffer for the decrypted data.
	 * The output buffer must be separate from the input buffer, and
	 * its size must be a multiple of the cipher block size.
	 */
	block_size = (unsigned int)EVP_CIPHER_CTX_block_size(ctx);
	output_buffer_size = len + (block_size - len % block_size);
	output_buffer = (u_char *)malloc(output_buffer_size);
	if (output_buffer == NULL) {
		(*ndo->ndo_warning)(ndo, "can't allocate memory for decryption buffer");
		EVP_CIPHER_CTX_free(ctx);
		return 0;
	}
	EVP_Cipher(ctx, output_buffer, buf, len);
	EVP_CIPHER_CTX_free(ctx);

	/*
	 * XXX - of course this is wrong, because buf is a const buffer,
	 * but changing this would require a more complicated fix.
	 */
	memcpy(__DECONST(u_char *, buf), output_buffer, len);
	free(output_buffer);

	ndo->ndo_packetp = buf;
	ndo->ndo_snapend = end;

	return 1;
}
USES_APPLE_RST

static void esp_print_addsa(netdissect_options *ndo,
			    struct sa_list *sa, int sa_def)
{
	/* copy the "sa" */

	struct sa_list *nsa;

	nsa = (struct sa_list *)malloc(sizeof(struct sa_list));
	if (nsa == NULL)
		(*ndo->ndo_error)(ndo, "ran out of memory to allocate sa structure");

	*nsa = *sa;

	if (sa_def)
		ndo->ndo_sa_default = nsa;

	nsa->next = ndo->ndo_sa_list_head;
	ndo->ndo_sa_list_head = nsa;
}


static u_int hexdigit(netdissect_options *ndo, char hex)
{
	if (hex >= '0' && hex <= '9')
		return (hex - '0');
	else if (hex >= 'A' && hex <= 'F')
		return (hex - 'A' + 10);
	else if (hex >= 'a' && hex <= 'f')
		return (hex - 'a' + 10);
	else {
		(*ndo->ndo_error)(ndo, "invalid hex digit %c in espsecret\n", hex);
		return 0;
	}
}

static u_int hex2byte(netdissect_options *ndo, char *hexstring)
{
	u_int byte;

	byte = (hexdigit(ndo, hexstring[0]) << 4) + hexdigit(ndo, hexstring[1]);
	return byte;
}

/*
 * returns size of binary, 0 on failure.
 */
static
int espprint_decode_hex(netdissect_options *ndo,
			u_char *binbuf, unsigned int binbuf_len,
			char *hex)
{
	unsigned int len;
	int i;

	len = strlen(hex) / 2;

	if (len > binbuf_len) {
		(*ndo->ndo_warning)(ndo, "secret is too big: %d\n", len);
		return 0;
	}

	i = 0;
	while (hex[0] != '\0' && hex[1]!='\0') {
		binbuf[i] = hex2byte(ndo, hex);
		hex += 2;
		i++;
	}

	return i;
}

/*
 * decode the form:    SPINUM@IP <tab> ALGONAME:0xsecret
 */

USES_APPLE_DEPRECATED_API
static int
espprint_decode_encalgo(netdissect_options *ndo,
			char *decode, struct sa_list *sa)
{
	size_t i;
	const EVP_CIPHER *evp;
	int authlen = 0;
	char *colon, *p;

	colon = strchr(decode, ':');
	if (colon == NULL) {
		(*ndo->ndo_warning)(ndo, "failed to decode espsecret: %s\n", decode);
		return 0;
	}
	*colon = '\0';

	if (strlen(decode) > strlen("-hmac96") &&
	    !strcmp(decode + strlen(decode) - strlen("-hmac96"),
		    "-hmac96")) {
		p = strstr(decode, "-hmac96");
		*p = '\0';
		authlen = 12;
	}
	if (strlen(decode) > strlen("-cbc") &&
	    !strcmp(decode + strlen(decode) - strlen("-cbc"), "-cbc")) {
		p = strstr(decode, "-cbc");
		*p = '\0';
	}
	evp = EVP_get_cipherbyname(decode);

	if (!evp) {
		(*ndo->ndo_warning)(ndo, "failed to find cipher algo %s\n", decode);
		sa->evp = NULL;
		sa->authlen = 0;
		sa->ivlen = 0;
		return 0;
	}

	sa->evp = evp;
	sa->authlen = authlen;
	sa->ivlen = EVP_CIPHER_iv_length(evp);

	colon++;
	if (colon[0] == '0' && colon[1] == 'x') {
		/* decode some hex! */

		colon += 2;
		sa->secretlen = espprint_decode_hex(ndo, sa->secret, sizeof(sa->secret), colon);
		if(sa->secretlen == 0) return 0;
	} else {
		i = strlen(colon);

		if (i < sizeof(sa->secret)) {
			memcpy(sa->secret, colon, i);
			sa->secretlen = i;
		} else {
			memcpy(sa->secret, colon, sizeof(sa->secret));
			sa->secretlen = sizeof(sa->secret);
		}
	}

	return 1;
}
USES_APPLE_RST

/*
 * for the moment, ignore the auth algorith, just hard code the authenticator
 * length. Need to research how openssl looks up HMAC stuff.
 */
static int
espprint_decode_authalgo(netdissect_options *ndo,
			 char *decode, struct sa_list *sa)
{
	char *colon;

	colon = strchr(decode, ':');
	if (colon == NULL) {
		(*ndo->ndo_warning)(ndo, "failed to decode espsecret: %s\n", decode);
		return 0;
	}
	*colon = '\0';

	if(ascii_strcasecmp(colon,"sha1") == 0 ||
	   ascii_strcasecmp(colon,"md5") == 0) {
		sa->authlen = 12;
	}
	return 1;
}

static void esp_print_decode_ikeline(netdissect_options *ndo, char *line,
				     const char *file, int lineno)
{
	/* it's an IKEv2 secret, store it instead */
	struct sa_list sa1;

	char *init;
	char *icookie, *rcookie;
	int   ilen, rlen;
	char *authkey;
	char *enckey;

	init = strsep(&line, " \t");
	icookie = strsep(&line, " \t");
	rcookie = strsep(&line, " \t");
	authkey = strsep(&line, " \t");
	enckey  = strsep(&line, " \t");

	/* if any fields are missing */
	if(!init || !icookie || !rcookie || !authkey || !enckey) {
		(*ndo->ndo_warning)(ndo, "print_esp: failed to find all fields for ikev2 at %s:%u",
				    file, lineno);

		return;
	}

	ilen = strlen(icookie);
	rlen = strlen(rcookie);

	if((init[0]!='I' && init[0]!='R')
	   || icookie[0]!='0' || icookie[1]!='x'
	   || rcookie[0]!='0' || rcookie[1]!='x'
	   || ilen!=18
	   || rlen!=18) {
		(*ndo->ndo_warning)(ndo, "print_esp: line %s:%u improperly formatted.",
				    file, lineno);

		(*ndo->ndo_warning)(ndo, "init=%s icookie=%s(%u) rcookie=%s(%u)",
				    init, icookie, ilen, rcookie, rlen);

		return;
	}

	sa1.spi = 0;
	sa1.initiator = (init[0] == 'I');
	if(espprint_decode_hex(ndo, sa1.spii, sizeof(sa1.spii), icookie+2)!=8)
		return;

	if(espprint_decode_hex(ndo, sa1.spir, sizeof(sa1.spir), rcookie+2)!=8)
		return;

	if(!espprint_decode_encalgo(ndo, enckey, &sa1)) return;

	if(!espprint_decode_authalgo(ndo, authkey, &sa1)) return;

	esp_print_addsa(ndo, &sa1, FALSE);
}

/*
 *
 * special form: file /name
 * causes us to go read from this file instead.
 *
 */
static void esp_print_decode_onesecret(netdissect_options *ndo, char *line,
				       const char *file, int lineno)
{
	struct sa_list sa1;
	int sa_def;

	char *spikey;
	char *decode;

	spikey = strsep(&line, " \t");
	sa_def = 0;
	memset(&sa1, 0, sizeof(struct sa_list));

	/* if there is only one token, then it is an algo:key token */
	if (line == NULL) {
		decode = spikey;
		spikey = NULL;
		/* sa1.daddr.version = 0; */
		/* memset(&sa1.daddr, 0, sizeof(sa1.daddr)); */
		/* sa1.spi = 0; */
		sa_def    = 1;
	} else
		decode = line;

	if (spikey && ascii_strcasecmp(spikey, "file") == 0) {
		/* open file and read it */
		FILE *secretfile;
		char  fileline[1024];
		int   subfile_lineno=0;
		char  *nl;
		char *filename = line;

		secretfile = fopen(filename, FOPEN_READ_TXT);
		if (secretfile == NULL) {
			(*ndo->ndo_error)(ndo, "print_esp: can't open %s: %s\n",
			    filename, strerror(errno));
			return;
		}

		while (fgets(fileline, sizeof(fileline)-1, secretfile) != NULL) {
			subfile_lineno++;
			/* remove newline from the line */
			nl = strchr(fileline, '\n');
			if (nl)
				*nl = '\0';
			if (fileline[0] == '#') continue;
			if (fileline[0] == '\0') continue;

			esp_print_decode_onesecret(ndo, fileline, filename, subfile_lineno);
		}
		fclose(secretfile);

		return;
	}

	if (spikey && ascii_strcasecmp(spikey, "ikev2") == 0) {
		esp_print_decode_ikeline(ndo, line, file, lineno);
		return;
	}

	if (spikey) {

		char *spistr, *foo;
		uint32_t spino;

		spistr = strsep(&spikey, "@");

		spino = strtoul(spistr, &foo, 0);
		if (spistr == foo || !spikey) {
			(*ndo->ndo_warning)(ndo, "print_esp: failed to decode spi# %s\n", foo);
			return;
		}

		sa1.spi = spino;

		if (strtoaddr6(spikey, &sa1.daddr.in6) == 1) {
			sa1.daddr_version = 6;
		} else if (strtoaddr(spikey, &sa1.daddr.in4) == 1) {
			sa1.daddr_version = 4;
		} else {
			(*ndo->ndo_warning)(ndo, "print_esp: can not decode IP# %s\n", spikey);
			return;
		}
	}

	if (decode) {
		/* skip any blank spaces */
		while (isspace((unsigned char)*decode))
			decode++;

		if(!espprint_decode_encalgo(ndo, decode, &sa1)) {
			return;
		}
	}

	esp_print_addsa(ndo, &sa1, sa_def);
}

USES_APPLE_DEPRECATED_API
static void esp_init(netdissect_options *ndo _U_)
{
	/*
	 * 0.9.6 doesn't appear to define OPENSSL_API_COMPAT, so
	 * we check whether it's undefined or it's less than the
	 * value for 1.1.0.
	 */
#if !defined(OPENSSL_API_COMPAT) || OPENSSL_API_COMPAT < 0x10100000L
	OpenSSL_add_all_algorithms();
#endif
	EVP_add_cipher_alias(SN_des_ede3_cbc, "3des");
}
USES_APPLE_RST

void esp_print_decodesecret(netdissect_options *ndo)
{
	char *line;
	char *p;
	static int initialized = 0;

	if (!initialized) {
		esp_init(ndo);
		initialized = 1;
	}

	p = ndo->ndo_espsecret;

	while (p && p[0] != '\0') {
		/* pick out the first line or first thing until a comma */
		if ((line = strsep(&p, "\n,")) == NULL) {
			line = p;
			p = NULL;
		}

		esp_print_decode_onesecret(ndo, line, "cmdline", 0);
	}

	ndo->ndo_espsecret = NULL;
}

#endif

#ifdef HAVE_LIBCRYPTO
USES_APPLE_DEPRECATED_API
#endif
int
esp_print(netdissect_options *ndo,
	  const u_char *bp, const int length, const u_char *bp2
#ifndef HAVE_LIBCRYPTO
	_U_
#endif
	,
	int *nhdr
#ifndef HAVE_LIBCRYPTO
	_U_
#endif
	,
	int *padlen
#ifndef HAVE_LIBCRYPTO
	_U_
#endif
	)
{
	register const struct newesp *esp;
	register const u_char *ep;
#ifdef HAVE_LIBCRYPTO
	const struct ip *ip;
	struct sa_list *sa = NULL;
	const struct ip6_hdr *ip6 = NULL;
	int advance;
	int len;
	u_char *secret;
	int ivlen = 0;
	const u_char *ivoff;
	const u_char *p;
	EVP_CIPHER_CTX *ctx;
	unsigned int block_size, output_buffer_size;
	u_char *output_buffer;
#endif

	esp = (const struct newesp *)bp;

#ifdef HAVE_LIBCRYPTO
	secret = NULL;
	advance = 0;
#endif

#if 0
	/* keep secret out of a register */
	p = (u_char *)&secret;
#endif

	/* 'ep' points to the end of available data. */
	ep = ndo->ndo_snapend;

	if ((const u_char *)(esp + 1) >= ep) {
		ND_PRINT((ndo, "[|ESP]"));
		goto fail;
	}
	ND_PRINT((ndo, "ESP(spi=0x%08x", EXTRACT_32BITS(&esp->esp_spi)));
	ND_PRINT((ndo, ",seq=0x%x)", EXTRACT_32BITS(&esp->esp_seq)));
	ND_PRINT((ndo, ", length %u", length));

#ifndef HAVE_LIBCRYPTO
	goto fail;
#else
	/* initiailize SAs */
	if (ndo->ndo_sa_list_head == NULL) {
		if (!ndo->ndo_espsecret)
			goto fail;

		esp_print_decodesecret(ndo);
	}

	if (ndo->ndo_sa_list_head == NULL)
		goto fail;

	ip = (const struct ip *)bp2;
	switch (IP_V(ip)) {
	case 6:
		ip6 = (const struct ip6_hdr *)bp2;
		/* we do not attempt to decrypt jumbograms */
		if (!EXTRACT_16BITS(&ip6->ip6_plen))
			goto fail;
		/* if we can't get nexthdr, we do not need to decrypt it */
		len = sizeof(struct ip6_hdr) + EXTRACT_16BITS(&ip6->ip6_plen);

		/* see if we can find the SA, and if so, decode it */
		for (sa = ndo->ndo_sa_list_head; sa != NULL; sa = sa->next) {
			if (sa->spi == EXTRACT_32BITS(&esp->esp_spi) &&
			    sa->daddr_version == 6 &&
			    UNALIGNED_MEMCMP(&sa->daddr.in6, &ip6->ip6_dst,
				   sizeof(struct in6_addr)) == 0) {
				break;
			}
		}
		break;
	case 4:
		/* nexthdr & padding are in the last fragment */
		if (EXTRACT_16BITS(&ip->ip_off) & IP_MF)
			goto fail;
		len = EXTRACT_16BITS(&ip->ip_len);

		/* see if we can find the SA, and if so, decode it */
		for (sa = ndo->ndo_sa_list_head; sa != NULL; sa = sa->next) {
			if (sa->spi == EXTRACT_32BITS(&esp->esp_spi) &&
			    sa->daddr_version == 4 &&
			    UNALIGNED_MEMCMP(&sa->daddr.in4, &ip->ip_dst,
				   sizeof(struct in_addr)) == 0) {
				break;
			}
		}
		break;
	default:
		goto fail;
	}

	/* if we didn't find the specific one, then look for
	 * an unspecified one.
	 */
	if (sa == NULL)
		sa = ndo->ndo_sa_default;

	/* if not found fail */
	if (sa == NULL)
		goto fail;

	/* if we can't get nexthdr, we do not need to decrypt it */
	if (ep - bp2 < len)
		goto fail;
	if (ep - bp2 > len) {
		/* FCS included at end of frame (NetBSD 1.6 or later) */
		ep = bp2 + len;
	}

	/* pointer to the IV, if there is one */
	ivoff = (const u_char *)(esp + 1) + 0;
	/* length of the IV, if there is one; 0, if there isn't */
	ivlen = sa->ivlen;
	secret = sa->secret;
	ep = ep - sa->authlen;

	if (sa->evp) {
		ctx = EVP_CIPHER_CTX_new();
		if (ctx != NULL) {
			if (set_cipher_parameters(ctx, sa->evp, secret, NULL, 0) < 0)
				(*ndo->ndo_warning)(ndo, "espkey init failed");

			p = ivoff;
			set_cipher_parameters(ctx, NULL, NULL, p, 0);
			len = ep - (p + ivlen);

			/*
			 * Allocate a buffer for the decrypted data.
			 * The output buffer must be separate from the
			 * input buffer, and its size must be a multiple
			 * of the cipher block size.
			 */
			block_size = (unsigned int)EVP_CIPHER_CTX_block_size(ctx);
			output_buffer_size = len + (block_size - len % block_size);
			output_buffer = (u_char *)malloc(output_buffer_size);
			if (output_buffer == NULL) {
				(*ndo->ndo_warning)(ndo, "can't allocate memory for decryption buffer");
				EVP_CIPHER_CTX_free(ctx);
				return -1;
			}

			EVP_Cipher(ctx, output_buffer, p + ivlen, len);
			EVP_CIPHER_CTX_free(ctx);
			/*
			 * XXX - of course this is wrong, because buf is a
			 * const buffer, but changing this would require a
			 * more complicated fix.
			 */
			memcpy(__DECONST(u_char *, p + ivlen), output_buffer, len);
			free(output_buffer);
			advance = ivoff - (const u_char *)esp + ivlen;
		} else
			advance = sizeof(struct newesp);
	} else
		advance = sizeof(struct newesp);

	/* sanity check for pad length */
	if (ep - bp < *(ep - 2))
		goto fail;

	if (padlen)
		*padlen = *(ep - 2) + 2;

	if (nhdr)
		*nhdr = *(ep - 1);

	ND_PRINT((ndo, ": "));
	return advance;
#endif

fail:
	return -1;
}
#ifdef HAVE_LIBCRYPTO
USES_APPLE_RST
#endif

/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
