/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthias Schmidt <matthias@dragonflybsd.org>, University of Marburg,
 * Germany.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <openssl/x509.h>
#include <openssl/md5.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

#include <string.h>
#include <syslog.h>

#include "dma.h"

static int
init_cert_file(SSL_CTX *ctx, const char *path)
{
	int error;

	/* Load certificate into ctx */
	error = SSL_CTX_use_certificate_chain_file(ctx, path);
	if (error < 1) {
		syslog(LOG_ERR, "SSL: Cannot load certificate `%s': %s", path, ssl_errstr());
		return (-1);
	}

	/* Add private key to ctx */
	error = SSL_CTX_use_PrivateKey_file(ctx, path, SSL_FILETYPE_PEM);
	if (error < 1) {
		syslog(LOG_ERR, "SSL: Cannot load private key `%s': %s", path, ssl_errstr());
		return (-1);
	}

	/*
	 * Check the consistency of a private key with the corresponding
         * certificate
	 */
	error = SSL_CTX_check_private_key(ctx);
	if (error < 1) {
		syslog(LOG_ERR, "SSL: Cannot check private key: %s", ssl_errstr());
		return (-1);
	}

	return (0);
}

int
smtp_init_crypto(int fd, int feature)
{
	SSL_CTX *ctx = NULL;
#if (OPENSSL_VERSION_NUMBER >= 0x00909000L)
	const SSL_METHOD *meth = NULL;
#else
	SSL_METHOD *meth = NULL;
#endif
	X509 *cert;
	int error;

	/* XXX clean up on error/close */
	/* Init SSL library */
	SSL_library_init();
	SSL_load_error_strings();

	// Allow any possible version
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
	meth = TLS_client_method();
#else
	meth = SSLv23_client_method();
#endif

	ctx = SSL_CTX_new(meth);
	if (ctx == NULL) {
		syslog(LOG_WARNING, "remote delivery deferred: SSL init failed: %s", ssl_errstr());
		return (1);
	}

	/* User supplied a certificate */
	if (config.certfile != NULL) {
		error = init_cert_file(ctx, config.certfile);
		if (error) {
			syslog(LOG_WARNING, "remote delivery deferred");
			return (1);
		}
	}

	/*
	 * If the user wants STARTTLS, we have to send EHLO here
	 */
	if (((feature & SECURETRANS) != 0) &&
	     (feature & STARTTLS) != 0) {
		/* TLS init phase, disable SSL_write */
		config.features |= NOSSL;

		send_remote_command(fd, "EHLO %s", hostname());
		if (read_remote(fd, 0, NULL) == 2) {
			send_remote_command(fd, "STARTTLS");
			if (read_remote(fd, 0, NULL) != 2) {
				if ((feature & TLS_OPP) == 0) {
					syslog(LOG_ERR, "remote delivery deferred: STARTTLS not available: %s", neterr);
					return (1);
				} else {
					syslog(LOG_INFO, "in opportunistic TLS mode, STARTTLS not available: %s", neterr);
					return (0);
				}
			}
		}
		/* End of TLS init phase, enable SSL_write/read */
		config.features &= ~NOSSL;
	}

	config.ssl = SSL_new(ctx);
	if (config.ssl == NULL) {
		syslog(LOG_NOTICE, "remote delivery deferred: SSL struct creation failed: %s",
		       ssl_errstr());
		return (1);
	}

	/* Set ssl to work in client mode */
	SSL_set_connect_state(config.ssl);

	/* Set fd for SSL in/output */
	error = SSL_set_fd(config.ssl, fd);
	if (error == 0) {
		syslog(LOG_NOTICE, "remote delivery deferred: SSL set fd failed: %s",
		       ssl_errstr());
		return (1);
	}

	/* Open SSL connection */
	error = SSL_connect(config.ssl);
	if (error < 0) {
		syslog(LOG_ERR, "remote delivery deferred: SSL handshake failed fatally: %s",
		       ssl_errstr());
		return (1);
	}

	/* Get peer certificate */
	cert = SSL_get_peer_certificate(config.ssl);
	if (cert == NULL) {
		syslog(LOG_WARNING, "remote delivery deferred: Peer did not provide certificate: %s",
		       ssl_errstr());
	}
	X509_free(cert);

	return (0);
}

/*
 * hmac_md5() taken out of RFC 2104.  This RFC was written by H. Krawczyk,
 * M. Bellare and R. Canetti.
 *
 * text      pointer to data stream
 * text_len  length of data stream
 * key       pointer to authentication key
 * key_len   length of authentication key
 * digest    caller digest to be filled int
 */
void
hmac_md5(unsigned char *text, int text_len, unsigned char *key, int key_len,
    unsigned char* digest)
{
        MD5_CTX context;
        unsigned char k_ipad[65];    /* inner padding -
                                      * key XORd with ipad
                                      */
        unsigned char k_opad[65];    /* outer padding -
                                      * key XORd with opad
                                      */
        unsigned char tk[16];
        int i;
        /* if key is longer than 64 bytes reset it to key=MD5(key) */
        if (key_len > 64) {

                MD5_CTX      tctx;

                MD5_Init(&tctx);
                MD5_Update(&tctx, key, key_len);
                MD5_Final(tk, &tctx);

                key = tk;
                key_len = 16;
        }

        /*
         * the HMAC_MD5 transform looks like:
         *
         * MD5(K XOR opad, MD5(K XOR ipad, text))
         *
         * where K is an n byte key
         * ipad is the byte 0x36 repeated 64 times
	 *
         * opad is the byte 0x5c repeated 64 times
         * and text is the data being protected
         */

        /* start out by storing key in pads */
        bzero( k_ipad, sizeof k_ipad);
        bzero( k_opad, sizeof k_opad);
        bcopy( key, k_ipad, key_len);
        bcopy( key, k_opad, key_len);

        /* XOR key with ipad and opad values */
        for (i=0; i<64; i++) {
                k_ipad[i] ^= 0x36;
                k_opad[i] ^= 0x5c;
        }
        /*
         * perform inner MD5
         */
        MD5_Init(&context);                   /* init context for 1st
                                              * pass */
        MD5_Update(&context, k_ipad, 64);     /* start with inner pad */
        MD5_Update(&context, text, text_len); /* then text of datagram */
        MD5_Final(digest, &context);          /* finish up 1st pass */
        /*
         * perform outer MD5
         */
        MD5_Init(&context);                   /* init context for 2nd
                                              * pass */
        MD5_Update(&context, k_opad, 64);     /* start with outer pad */
        MD5_Update(&context, digest, 16);     /* then results of 1st
                                              * hash */
        MD5_Final(digest, &context);          /* finish up 2nd pass */
}

/*
 * CRAM-MD5 authentication
 */
int
smtp_auth_md5(int fd, char *login, char *password)
{
	unsigned char digest[BUF_SIZE];
	char buffer[BUF_SIZE], ascii_digest[33];
	char *temp;
	int len, i;
	static char hextab[] = "0123456789abcdef";

	temp = calloc(BUF_SIZE, 1);
	memset(buffer, 0, sizeof(buffer));
	memset(digest, 0, sizeof(digest));
	memset(ascii_digest, 0, sizeof(ascii_digest));

	/* Send AUTH command according to RFC 2554 */
	send_remote_command(fd, "AUTH CRAM-MD5");
	if (read_remote(fd, sizeof(buffer), buffer) != 3) {
		syslog(LOG_DEBUG, "smarthost authentication:"
		       " AUTH cram-md5 not available: %s", neterr);
		/* if cram-md5 is not available */
		free(temp);
		return (-1);
	}

	/* skip 3 char status + 1 char space */
	base64_decode(buffer + 4, temp);
	hmac_md5((unsigned char *)temp, strlen(temp),
		 (unsigned char *)password, strlen(password), digest);
	free(temp);

	ascii_digest[32] = 0;
	for (i = 0; i < 16; i++) {
		ascii_digest[2*i] = hextab[digest[i] >> 4];
		ascii_digest[2*i+1] = hextab[digest[i] & 15];
	}

	/* prepare answer */
	snprintf(buffer, BUF_SIZE, "%s %s", login, ascii_digest);

	/* encode answer */
	len = base64_encode(buffer, strlen(buffer), &temp);
	if (len < 0) {
		syslog(LOG_ERR, "can not encode auth reply: %m");
		return (-1);
	}

	/* send answer */
	send_remote_command(fd, "%s", temp);
	free(temp);
	if (read_remote(fd, 0, NULL) != 2) {
		syslog(LOG_WARNING, "remote delivery deferred:"
				" AUTH cram-md5 failed: %s", neterr);
		return (-2);
	}

	return (0);
}
