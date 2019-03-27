/*
 * Copyright (c) 2000-2006, 2008, 2009, 2011, 2013 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>

SM_RCSID("@(#)$Id: tls.c,v 8.127 2013-11-27 02:51:11 gshapiro Exp $")

#if STARTTLS
# include <openssl/err.h>
# include <openssl/bio.h>
# include <openssl/pem.h>
# if !NO_DH
# include <openssl/dh.h>
# endif /* !NO_DH */
# ifndef HASURANDOMDEV
#  include <openssl/rand.h>
# endif /* ! HASURANDOMDEV */
# if !TLS_NO_RSA
static RSA *rsa_tmp = NULL;	/* temporary RSA key */
static RSA *tmp_rsa_key __P((SSL *, int, int));
# endif /* !TLS_NO_RSA */
# if !defined(OPENSSL_VERSION_NUMBER) || OPENSSL_VERSION_NUMBER < 0x00907000L
static int	tls_verify_cb __P((X509_STORE_CTX *));
# else /* !defined() || OPENSSL_VERSION_NUMBER < 0x00907000L */
static int	tls_verify_cb __P((X509_STORE_CTX *, void *));
# endif /* !defined() || OPENSSL_VERSION_NUMBER < 0x00907000L */

# if OPENSSL_VERSION_NUMBER > 0x00907000L
static int x509_verify_cb __P((int, X509_STORE_CTX *));
# endif /* OPENSSL_VERSION_NUMBER > 0x00907000L */

# if !defined(OPENSSL_VERSION_NUMBER) || OPENSSL_VERSION_NUMBER < 0x00907000L
#  define CONST097
# else /* !defined() || OPENSSL_VERSION_NUMBER < 0x00907000L */
#  define CONST097 const
# endif /* !defined() || OPENSSL_VERSION_NUMBER < 0x00907000L */
static void	apps_ssl_info_cb __P((CONST097 SSL *, int , int));
static bool	tls_ok_f __P((char *, char *, int));
static bool	tls_safe_f __P((char *, long, bool));
static int	tls_verify_log __P((int, X509_STORE_CTX *, const char *));

# if !NO_DH
# if !defined(OPENSSL_VERSION_NUMBER) || OPENSSL_VERSION_NUMBER < 0x10100001L || \
     (defined(LIBRESSL_VERSION_NUMBER) && LIBRESSL_VERSION_NUMBER < 0x20700000L)
static int
DH_set0_pqg(dh, p, q, g)
	DH *dh;
	BIGNUM *p;
	BIGNUM *q;
	BIGNUM *g;
{
	dh->p=p;
	if (q != NULL)
		dh->q=q;
	dh->g=g;
	return 1; /* success */
}
# endif /* !defined() || OPENSSL_VERSION_NUMBER < 0x00907000L */

static DH *get_dh512 __P((void));

static unsigned char dh512_p[] =
{
	0xDA,0x58,0x3C,0x16,0xD9,0x85,0x22,0x89,0xD0,0xE4,0xAF,0x75,
	0x6F,0x4C,0xCA,0x92,0xDD,0x4B,0xE5,0x33,0xB8,0x04,0xFB,0x0F,
	0xED,0x94,0xEF,0x9C,0x8A,0x44,0x03,0xED,0x57,0x46,0x50,0xD3,
	0x69,0x99,0xDB,0x29,0xD7,0x76,0x27,0x6B,0xA2,0xD3,0xD4,0x12,
	0xE2,0x18,0xF4,0xDD,0x1E,0x08,0x4C,0xF6,0xD8,0x00,0x3E,0x7C,
	0x47,0x74,0xE8,0x33
};
static unsigned char dh512_g[] =
{
	0x02
};

static DH *
get_dh512()
{
	DH *dh = NULL;
	BIGNUM *dhp_bn, *dhg_bn;

	if ((dh = DH_new()) == NULL)
		return NULL;
	dhp_bn = BN_bin2bn(dh512_p, sizeof(dh512_p), NULL);
	dhg_bn = BN_bin2bn(dh512_g, sizeof(dh512_g), NULL);
	if ((dhp_bn == NULL) || (dhg_bn == NULL) || !DH_set0_pqg(dh, dhp_bn, NULL, dhg_bn))
	{
		DH_free(dh);
		BN_free(dhp_bn);
		BN_free(dhg_bn);
		return(NULL);
	}
	return dh;
}

#  if 0

This is the data from which the C code has been generated:

-----BEGIN DH PARAMETERS-----
MIIBCAKCAQEArDcgcLpxEksQHPlolRKCUJ2szKRziseWV9cUSQNZGxoGw7KkROz4
HF9QSbg5axyNIG+QbZYtx0jp3l6/GWq1dLOj27yZkgYgaYgFrvKPiZ2jJ5xETQVH
UpZwbjRcyjyWkWYJVsx1aF4F/iY4kT0n/+iGEoimI3C9V3KXTJ2S6jIkyJ6M/CrN
EtrDynMlUMGlc7S1ouXVOTrtKeqy3S2L9eBLxVI+sChEijGIfELupdVeXihK006p
MgnABPDbkTx6OOtYmSZaGQX+OLW2FPmwvcrzgCz9t9cAsuUcBZv1LeHEqZZttyLU
oK0jjSXgFyeU4/NfyA+zuNeWzUL6bHmigwIBAg==
-----END DH PARAMETERS-----
#  endif /* 0 */

static DH *
get_dh2048()
{
	static unsigned char dh2048_p[]={
		0xAC,0x37,0x20,0x70,0xBA,0x71,0x12,0x4B,0x10,0x1C,0xF9,0x68,
		0x95,0x12,0x82,0x50,0x9D,0xAC,0xCC,0xA4,0x73,0x8A,0xC7,0x96,
		0x57,0xD7,0x14,0x49,0x03,0x59,0x1B,0x1A,0x06,0xC3,0xB2,0xA4,
		0x44,0xEC,0xF8,0x1C,0x5F,0x50,0x49,0xB8,0x39,0x6B,0x1C,0x8D,
		0x20,0x6F,0x90,0x6D,0x96,0x2D,0xC7,0x48,0xE9,0xDE,0x5E,0xBF,
		0x19,0x6A,0xB5,0x74,0xB3,0xA3,0xDB,0xBC,0x99,0x92,0x06,0x20,
		0x69,0x88,0x05,0xAE,0xF2,0x8F,0x89,0x9D,0xA3,0x27,0x9C,0x44,
		0x4D,0x05,0x47,0x52,0x96,0x70,0x6E,0x34,0x5C,0xCA,0x3C,0x96,
		0x91,0x66,0x09,0x56,0xCC,0x75,0x68,0x5E,0x05,0xFE,0x26,0x38,
		0x91,0x3D,0x27,0xFF,0xE8,0x86,0x12,0x88,0xA6,0x23,0x70,0xBD,
		0x57,0x72,0x97,0x4C,0x9D,0x92,0xEA,0x32,0x24,0xC8,0x9E,0x8C,
		0xFC,0x2A,0xCD,0x12,0xDA,0xC3,0xCA,0x73,0x25,0x50,0xC1,0xA5,
		0x73,0xB4,0xB5,0xA2,0xE5,0xD5,0x39,0x3A,0xED,0x29,0xEA,0xB2,
		0xDD,0x2D,0x8B,0xF5,0xE0,0x4B,0xC5,0x52,0x3E,0xB0,0x28,0x44,
		0x8A,0x31,0x88,0x7C,0x42,0xEE,0xA5,0xD5,0x5E,0x5E,0x28,0x4A,
		0xD3,0x4E,0xA9,0x32,0x09,0xC0,0x04,0xF0,0xDB,0x91,0x3C,0x7A,
		0x38,0xEB,0x58,0x99,0x26,0x5A,0x19,0x05,0xFE,0x38,0xB5,0xB6,
		0x14,0xF9,0xB0,0xBD,0xCA,0xF3,0x80,0x2C,0xFD,0xB7,0xD7,0x00,
		0xB2,0xE5,0x1C,0x05,0x9B,0xF5,0x2D,0xE1,0xC4,0xA9,0x96,0x6D,
		0xB7,0x22,0xD4,0xA0,0xAD,0x23,0x8D,0x25,0xE0,0x17,0x27,0x94,
		0xE3,0xF3,0x5F,0xC8,0x0F,0xB3,0xB8,0xD7,0x96,0xCD,0x42,0xFA,
		0x6C,0x79,0xA2,0x83,
		};
	static unsigned char dh2048_g[]={ 0x02, };
	DH *dh;
	BIGNUM *dhp_bn, *dhg_bn;

	if ((dh=DH_new()) == NULL)
		return(NULL);
	dhp_bn = BN_bin2bn(dh2048_p,sizeof(dh2048_p),NULL);
	dhg_bn = BN_bin2bn(dh2048_g,sizeof(dh2048_g),NULL);
	if ((dhp_bn == NULL) || (dhg_bn == NULL) || !DH_set0_pqg(dh, dhp_bn, NULL, dhg_bn))
	{
		DH_free(dh);
		BN_free(dhp_bn);
		BN_free(dhg_bn);
		return(NULL);
	}
	return(dh);
}
# endif /* !NO_DH */


/*
**  TLS_RAND_INIT -- initialize STARTTLS random generator
**
**	Parameters:
**		randfile -- name of file with random data
**		logl -- loglevel
**
**	Returns:
**		success/failure
**
**	Side Effects:
**		initializes PRNG for tls library.
*/

# define MIN_RAND_BYTES	128	/* 1024 bits */

# define RF_OK		0	/* randfile OK */
# define RF_MISS	1	/* randfile == NULL || *randfile == '\0' */
# define RF_UNKNOWN	2	/* unknown prefix for randfile */

# define RI_NONE	0	/* no init yet */
# define RI_SUCCESS	1	/* init was successful */
# define RI_FAIL	2	/* init failed */

static bool	tls_rand_init __P((char *, int));

static bool
tls_rand_init(randfile, logl)
	char *randfile;
	int logl;
{
# ifndef HASURANDOMDEV
	/* not required if /dev/urandom exists, OpenSSL does it internally */

	bool ok;
	int randdef;
	static int done = RI_NONE;

	/*
	**  initialize PRNG
	*/

	/* did we try this before? if yes: return old value */
	if (done != RI_NONE)
		return done == RI_SUCCESS;

	/* set default values */
	ok = false;
	done = RI_FAIL;
	randdef = (randfile == NULL || *randfile == '\0') ? RF_MISS : RF_OK;
#   if EGD
	if (randdef == RF_OK && sm_strncasecmp(randfile, "egd:", 4) == 0)
	{
		randfile += 4;
		if (RAND_egd(randfile) < 0)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS: RAND_egd(%s) failed: random number generator not seeded",
				   randfile);
		}
		else
			ok = true;
	}
	else
#   endif /* EGD */
	if (randdef == RF_OK && sm_strncasecmp(randfile, "file:", 5) == 0)
	{
		int fd;
		long sff;
		struct stat st;

		randfile += 5;
		sff = SFF_SAFEDIRPATH | SFF_NOWLINK
		      | SFF_NOGWFILES | SFF_NOWWFILES
		      | SFF_NOGRFILES | SFF_NOWRFILES
		      | SFF_MUSTOWN | SFF_ROOTOK | SFF_OPENASROOT;
		if (DontLockReadFiles)
			sff |= SFF_NOLOCK;
		if ((fd = safeopen(randfile, O_RDONLY, 0, sff)) >= 0)
		{
			if (fstat(fd, &st) < 0)
			{
				if (LogLevel > logl)
					sm_syslog(LOG_ERR, NOQID,
						  "STARTTLS: can't fstat(%s)",
						  randfile);
			}
			else
			{
				bool use, problem;

				use = true;
				problem = false;

				/* max. age of file: 10 minutes */
				if (st.st_mtime + 600 < curtime())
				{
					use = bitnset(DBS_INSUFFICIENTENTROPY,
						      DontBlameSendmail);
					problem = true;
					if (LogLevel > logl)
						sm_syslog(LOG_ERR, NOQID,
							  "STARTTLS: RandFile %s too old: %s",
							  randfile,
							  use ? "unsafe" :
								"unusable");
				}
				if (use && st.st_size < MIN_RAND_BYTES)
				{
					use = bitnset(DBS_INSUFFICIENTENTROPY,
						      DontBlameSendmail);
					problem = true;
					if (LogLevel > logl)
						sm_syslog(LOG_ERR, NOQID,
							  "STARTTLS: size(%s) < %d: %s",
							  randfile,
							  MIN_RAND_BYTES,
							  use ? "unsafe" :
								"unusable");
				}
				if (use)
					ok = RAND_load_file(randfile, -1) >=
					     MIN_RAND_BYTES;
				if (use && !ok)
				{
					if (LogLevel > logl)
						sm_syslog(LOG_WARNING, NOQID,
							  "STARTTLS: RAND_load_file(%s) failed: random number generator not seeded",
							  randfile);
				}
				if (problem)
					ok = false;
			}
			if (ok || bitnset(DBS_INSUFFICIENTENTROPY,
					  DontBlameSendmail))
			{
				/* add this even if fstat() failed */
				RAND_seed((void *) &st, sizeof(st));
			}
			(void) close(fd);
		}
		else
		{
			if (LogLevel > logl)
				sm_syslog(LOG_WARNING, NOQID,
					  "STARTTLS: Warning: safeopen(%s) failed",
					  randfile);
		}
	}
	else if (randdef == RF_OK)
	{
		if (LogLevel > logl)
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS: Error: no proper random file definition %s",
				  randfile);
		randdef = RF_UNKNOWN;
	}
	if (randdef == RF_MISS)
	{
		if (LogLevel > logl)
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS: Error: missing random file definition");
	}
	if (!ok && bitnset(DBS_INSUFFICIENTENTROPY, DontBlameSendmail))
	{
		int i;
		long r;
		unsigned char buf[MIN_RAND_BYTES];

		/* assert((MIN_RAND_BYTES % sizeof(long)) == 0); */
		for (i = 0; i <= sizeof(buf) - sizeof(long); i += sizeof(long))
		{
			r = get_random();
			(void) memcpy(buf + i, (void *) &r, sizeof(long));
		}
		RAND_seed(buf, sizeof(buf));
		if (LogLevel > logl)
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS: Warning: random number generator not properly seeded");
		ok = true;
	}
	done = ok ? RI_SUCCESS : RI_FAIL;
	return ok;
# else /* ! HASURANDOMDEV */
	return true;
# endif /* ! HASURANDOMDEV */
}
/*
**  INIT_TLS_LIBRARY -- Calls functions which setup TLS library for global use.
**
**	Parameters:
**		fipsmode -- use FIPS?
**
**	Returns:
**		succeeded?
*/

bool
init_tls_library(fipsmode)
	bool fipsmode;
{
	bool bv;

	/* basic TLS initialization, ignore result for now */
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
# if 0
	/* this is currently a macro for SSL_library_init */
	SSLeay_add_ssl_algorithms();
# endif /* 0 */

	bv = tls_rand_init(RandFile, 7);
# if _FFR_FIPSMODE
	if (bv && fipsmode)
	{
		if (!FIPS_mode_set(1))
		{
			unsigned long err;

			err = ERR_get_error();
			if (LogLevel > 0)
				sm_syslog(LOG_ERR, NOQID,
					"STARTTLS=init, FIPSMode=%s",
					ERR_error_string(err, NULL));
			return false;
		}
		else
		{
			if (LogLevel > 9)
				sm_syslog(LOG_INFO, NOQID,
					"STARTTLS=init, FIPSMode=ok");
		}
	}
#endif /* _FFR_FIPSMODE  */
	if (bv && CertFingerprintAlgorithm != NULL)
	{
		const EVP_MD *md;

		md = EVP_get_digestbyname(CertFingerprintAlgorithm);
		if (NULL == md)
		{
			bv = false;
			if (LogLevel > 0)
				sm_syslog(LOG_ERR, NOQID,
					"STARTTLS=init, CertFingerprintAlgorithm=%s, status=invalid"
					, CertFingerprintAlgorithm);
		}
		else
			EVP_digest = md;
	}
	return bv;
}

/*
**  TLS_SET_VERIFY -- request client certificate?
**
**	Parameters:
**		ctx -- TLS context
**		ssl -- TLS structure
**		vrfy -- request certificate?
**
**	Returns:
**		none.
**
**	Side Effects:
**		Sets verification state for TLS
**
# if TLS_VRFY_PER_CTX
**	Notice:
**		This is per TLS context, not per TLS structure;
**		the former is global, the latter per connection.
**		It would be nice to do this per connection, but this
**		doesn't work in the current TLS libraries :-(
# endif * TLS_VRFY_PER_CTX *
*/

void
tls_set_verify(ctx, ssl, vrfy)
	SSL_CTX *ctx;
	SSL *ssl;
	bool vrfy;
{
# if !TLS_VRFY_PER_CTX
	SSL_set_verify(ssl, vrfy ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, NULL);
# else /* !TLS_VRFY_PER_CTX */
	SSL_CTX_set_verify(ctx, vrfy ? SSL_VERIFY_PEER : SSL_VERIFY_NONE,
			NULL);
# endif /* !TLS_VRFY_PER_CTX */
}

/*
**  status in initialization
**  these flags keep track of the status of the initialization
**  i.e., whether a file exists (_EX) and whether it can be used (_OK)
**  [due to permissions]
*/

# define TLS_S_NONE	0x00000000	/* none yet */
# define TLS_S_CERT_EX	0x00000001	/* cert file exists */
# define TLS_S_CERT_OK	0x00000002	/* cert file is ok */
# define TLS_S_KEY_EX	0x00000004	/* key file exists */
# define TLS_S_KEY_OK	0x00000008	/* key file is ok */
# define TLS_S_CERTP_EX	0x00000010	/* CA cert path exists */
# define TLS_S_CERTP_OK	0x00000020	/* CA cert path is ok */
# define TLS_S_CERTF_EX	0x00000040	/* CA cert file exists */
# define TLS_S_CERTF_OK	0x00000080	/* CA cert file is ok */
# define TLS_S_CRLF_EX	0x00000100	/* CRL file exists */
# define TLS_S_CRLF_OK	0x00000200	/* CRL file is ok */

# define TLS_S_CERT2_EX	0x00001000	/* 2nd cert file exists */
# define TLS_S_CERT2_OK	0x00002000	/* 2nd cert file is ok */
# define TLS_S_KEY2_EX	0x00004000	/* 2nd key file exists */
# define TLS_S_KEY2_OK	0x00008000	/* 2nd key file is ok */

# define TLS_S_DH_OK	0x00200000	/* DH cert is ok */
# define TLS_S_DHPAR_EX	0x00400000	/* DH param file exists */
# define TLS_S_DHPAR_OK	0x00800000	/* DH param file is ok to use */

/* Type of variable */
# define TLS_T_OTHER	0
# define TLS_T_SRV	1
# define TLS_T_CLT	2

/*
**  TLS_OK_F -- can var be an absolute filename?
**
**	Parameters:
**		var -- filename
**		fn -- what is the filename used for?
**		type -- type of variable
**
**	Returns:
**		ok?
*/

static bool
tls_ok_f(var, fn, type)
	char *var;
	char *fn;
	int type;
{
	/* must be absolute pathname */
	if (var != NULL && *var == '/')
		return true;
	if (LogLevel > 12)
		sm_syslog(LOG_WARNING, NOQID, "STARTTLS: %s%s missing",
			  type == TLS_T_SRV ? "Server" :
			  (type == TLS_T_CLT ? "Client" : ""), fn);
	return false;
}
/*
**  TLS_SAFE_F -- is a file safe to use?
**
**	Parameters:
**		var -- filename
**		sff -- flags for safefile()
**		srv -- server side?
**
**	Returns:
**		ok?
*/

static bool
tls_safe_f(var, sff, srv)
	char *var;
	long sff;
	bool srv;
{
	int ret;

	if ((ret = safefile(var, RunAsUid, RunAsGid, RunAsUserName, sff,
			    S_IRUSR, NULL)) == 0)
		return true;
	if (LogLevel > 7)
		sm_syslog(LOG_WARNING, NOQID, "STARTTLS=%s: file %s unsafe: %s",
			  srv ? "server" : "client", var, sm_errstring(ret));
	return false;
}

/*
**  TLS_OK_F -- macro to simplify calls to tls_ok_f
**
**	Parameters:
**		var -- filename
**		fn -- what is the filename used for?
**		req -- is the file required?
**		st -- status bit to set if ok
**		type -- type of variable
**
**	Side Effects:
**		uses r, ok; may change ok and status.
**
*/

# define TLS_OK_F(var, fn, req, st, type) if (ok) \
	{ \
		r = tls_ok_f(var, fn, type); \
		if (r) \
			status |= st; \
		else if (req) \
			ok = false; \
	}

/*
**  TLS_UNR -- macro to return whether a file should be unreadable
**
**	Parameters:
**		bit -- flag to test
**		req -- flags
**
**	Returns:
**		0/SFF_NORFILES
*/
# define TLS_UNR(bit, req)	(bitset(bit, req) ? SFF_NORFILES : 0)
# define TLS_OUNR(bit, req)	(bitset(bit, req) ? SFF_NOWRFILES : 0)
# define TLS_KEYSFF(req)	\
	(bitnset(DBS_GROUPREADABLEKEYFILE, DontBlameSendmail) ?	\
		TLS_OUNR(TLS_I_KEY_OUNR, req) :			\
		TLS_UNR(TLS_I_KEY_UNR, req))

/*
**  TLS_SAFE_F -- macro to simplify calls to tls_safe_f
**
**	Parameters:
**		var -- filename
**		sff -- flags for safefile()
**		req -- is the file required?
**		ex -- does the file exist?
**		st -- status bit to set if ok
**		srv -- server side?
**
**	Side Effects:
**		uses r, ok, ex; may change ok and status.
**
*/

# define TLS_SAFE_F(var, sff, req, ex, st, srv) if (ex && ok) \
	{ \
		r = tls_safe_f(var, sff, srv); \
		if (r) \
			status |= st;	\
		else if (req) \
			ok = false;	\
	}

# if _FFR_TLS_SE_OPTS
/*
**  LOAD_CERTKEY -- load cert/key for TLS session
**
**	Parameters:
**		ssl -- TLS session context
**		certfile -- filename of certificate
**		keyfile -- filename of private key
**
**	Returns:
**		succeeded?
*/

bool
load_certkey(ssl, srv, certfile, keyfile)
	SSL *ssl;
	bool srv;
	char *certfile;
	char *keyfile;
{
	bool ok;
	int r;
	long sff, status;
	unsigned long req;
	char *who;

	ok = true;
	who = srv ? "server" : "client";
	status = TLS_S_NONE;
	req = TLS_I_CERT_EX|TLS_I_KEY_EX;
	TLS_OK_F(certfile, "CertFile", bitset(TLS_I_CERT_EX, req),
		 TLS_S_CERT_EX, srv ? TLS_T_SRV : TLS_T_CLT);
	TLS_OK_F(keyfile, "KeyFile", bitset(TLS_I_KEY_EX, req),
		 TLS_S_KEY_EX, srv ? TLS_T_SRV : TLS_T_CLT);

	/* certfile etc. must be "safe". */
	sff = SFF_REGONLY | SFF_SAFEDIRPATH | SFF_NOWLINK
	     | SFF_NOGWFILES | SFF_NOWWFILES
	     | SFF_MUSTOWN | SFF_ROOTOK | SFF_OPENASROOT;
	if (DontLockReadFiles)
		sff |= SFF_NOLOCK;

	TLS_SAFE_F(certfile, sff | TLS_UNR(TLS_I_CERT_UNR, req),
		   bitset(TLS_I_CERT_EX, req),
		   bitset(TLS_S_CERT_EX, status), TLS_S_CERT_OK, srv);
	TLS_SAFE_F(keyfile, sff | TLS_KEYSFF(req),
		   bitset(TLS_I_KEY_EX, req),
		   bitset(TLS_S_KEY_EX, status), TLS_S_KEY_OK, srv);

# define SSL_use_cert(ssl, certfile) \
	SSL_use_certificate_file(ssl, certfile, SSL_FILETYPE_PEM)
# define SSL_USE_CERT "SSL_use_certificate_file"

	if (bitset(TLS_S_CERT_OK, status) &&
	    SSL_use_cert(ssl, certfile) <= 0)
	{
		if (LogLevel > 7)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=%s, error: %s(%s) failed",
				  who, SSL_USE_CERT, certfile);
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, who);
		}
		if (bitset(TLS_I_USE_CERT, req))
			return false;
	}
	if (bitset(TLS_S_KEY_OK, status) &&
	    SSL_use_PrivateKey_file(ssl, keyfile, SSL_FILETYPE_PEM) <= 0)
	{
		if (LogLevel > 7)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=%s, error: SSL_use_PrivateKey_file(%s) failed",
				  who, keyfile);
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, who);
		}
		if (bitset(TLS_I_USE_KEY, req))
			return false;
	}

	/* check the private key */
	if (bitset(TLS_S_KEY_OK, status) &&
	    (r = SSL_check_private_key(ssl)) <= 0)
	{
		/* Private key does not match the certificate public key */
		if (LogLevel > 5)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=%s, error: SSL_check_private_key failed(%s): %d",
				  who, keyfile, r);
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, who);
		}
		if (bitset(TLS_I_USE_KEY, req))
			return false;
	}

	return true;
}
# endif /* _FFR_TLS_SE_OPTS */

/*
**  INITTLS -- initialize TLS
**
**	Parameters:
**		ctx -- pointer to context
**		req -- requirements for initialization (see sendmail.h)
**		options -- options
**		srv -- server side?
**		certfile -- filename of certificate
**		keyfile -- filename of private key
**		cacertpath -- path to CAs
**		cacertfile -- file with CA(s)
**		dhparam -- parameters for DH
**
**	Returns:
**		succeeded?
*/

/*
**  The session_id_context identifies the service that created a session.
**  This information is used to distinguish between multiple TLS-based
**  servers running on the same server. We use the name of the mail system.
**  Note: the session cache is not persistent.
*/

static char server_session_id_context[] = "sendmail8";

# if !TLS_NO_RSA
static RSA *
sm_RSA_generate_key(num, e)
	int num;
	unsigned long e;
{
	RSA *rsa = NULL;
        BIGNUM *bn_rsa_r4;
	int rc;

	bn_rsa_r4 = BN_new();
        rc = BN_set_word(bn_rsa_r4, RSA_F4);
	if ((bn_rsa_r4 != NULL) && BN_set_word(bn_rsa_r4, RSA_F4) && (rsa = RSA_new()) != NULL)
	{
		if (!RSA_generate_key_ex(rsa, RSA_KEYLENGTH, bn_rsa_r4, NULL))
		{
			RSA_free(rsa);
			rsa = NULL;
		}
		return NULL;
	}
	BN_free(bn_rsa_r4);
	return rsa;
}
# endif /* !TLS_NO_RSA */

/* 0.9.8a and b have a problem with SSL_OP_TLS_BLOCK_PADDING_BUG */
#if (OPENSSL_VERSION_NUMBER >= 0x0090800fL)
# define SM_SSL_OP_TLS_BLOCK_PADDING_BUG	1
#else
# define SM_SSL_OP_TLS_BLOCK_PADDING_BUG	0
#endif

bool
inittls(ctx, req, options, srv, certfile, keyfile, cacertpath, cacertfile, dhparam)
	SSL_CTX **ctx;
	unsigned long req;
	unsigned long options;
	bool srv;
	char *certfile, *keyfile, *cacertpath, *cacertfile, *dhparam;
{
# if !NO_DH
	static DH *dh = NULL;
# endif /* !NO_DH */
	int r;
	bool ok;
	long sff, status;
	char *who;
	char *cf2, *kf2;
# if SM_CONF_SHM
	extern int ShmId;
# endif /* SM_CONF_SHM */
# if OPENSSL_VERSION_NUMBER > 0x00907000L
	BIO *crl_file;
	X509_CRL *crl;
	X509_STORE *store;
# endif /* OPENSSL_VERSION_NUMBER > 0x00907000L */
#if SM_SSL_OP_TLS_BLOCK_PADDING_BUG
	long rt_version;
	STACK_OF(SSL_COMP) *comp_methods;
#endif

	status = TLS_S_NONE;
	who = srv ? "server" : "client";
	if (ctx == NULL)
	{
		syserr("STARTTLS=%s, inittls: ctx == NULL", who);
		/* NOTREACHED */
		SM_ASSERT(ctx != NULL);
	}

	/* already initialized? (we could re-init...) */
	if (*ctx != NULL)
		return true;
	ok = true;

	/*
	**  look for a second filename: it must be separated by a ','
	**  no blanks allowed (they won't be skipped).
	**  we change a global variable here! this change will be undone
	**  before return from the function but only if it returns true.
	**  this isn't a problem since in a failure case this function
	**  won't be called again with the same (overwritten) values.
	**  otherwise each return must be replaced with a goto endinittls.
	*/

	cf2 = NULL;
	kf2 = NULL;
	if (certfile != NULL && (cf2 = strchr(certfile, ',')) != NULL)
	{
		*cf2++ = '\0';
		if (keyfile != NULL && (kf2 = strchr(keyfile, ',')) != NULL)
			*kf2++ = '\0';
	}

	/*
	**  Check whether files/paths are defined
	*/

	TLS_OK_F(certfile, "CertFile", bitset(TLS_I_CERT_EX, req),
		 TLS_S_CERT_EX, srv ? TLS_T_SRV : TLS_T_CLT);
	TLS_OK_F(keyfile, "KeyFile", bitset(TLS_I_KEY_EX, req),
		 TLS_S_KEY_EX, srv ? TLS_T_SRV : TLS_T_CLT);
	TLS_OK_F(cacertpath, "CACertPath", bitset(TLS_I_CERTP_EX, req),
		 TLS_S_CERTP_EX, TLS_T_OTHER);
	TLS_OK_F(cacertfile, "CACertFile", bitset(TLS_I_CERTF_EX, req),
		 TLS_S_CERTF_EX, TLS_T_OTHER);

# if OPENSSL_VERSION_NUMBER > 0x00907000L
	TLS_OK_F(CRLFile, "CRLFile", bitset(TLS_I_CRLF_EX, req),
		 TLS_S_CRLF_EX, TLS_T_OTHER);
# endif /* OPENSSL_VERSION_NUMBER > 0x00907000L */

	/*
	**  if the second file is specified it must exist
	**  XXX: it is possible here to define only one of those files
	*/

	if (cf2 != NULL)
	{
		TLS_OK_F(cf2, "CertFile", bitset(TLS_I_CERT_EX, req),
			 TLS_S_CERT2_EX, srv ? TLS_T_SRV : TLS_T_CLT);
	}
	if (kf2 != NULL)
	{
		TLS_OK_F(kf2, "KeyFile", bitset(TLS_I_KEY_EX, req),
			 TLS_S_KEY2_EX, srv ? TLS_T_SRV : TLS_T_CLT);
	}

	/*
	**  valid values for dhparam are (only the first char is checked)
	**  none	no parameters: don't use DH
	**  i		use precomputed 2048 bit parameters
	**  512		use precomputed 512 bit parameters
	**  1024	generate 1024 bit parameters
	**  2048	generate 2048 bit parameters
	**  /file/name	read parameters from /file/name
	*/

#define SET_DH_DFL	\
	do {	\
		dhparam = "I";	\
		req |= TLS_I_DHFIXED;	\
	} while (0)

	if (bitset(TLS_I_TRY_DH, req))
	{
		if (dhparam != NULL)
		{
			char c = *dhparam;

			if (c == '1')
				req |= TLS_I_DH1024;
			else if (c == 'I' || c == 'i')
				req |= TLS_I_DHFIXED;
			else if (c == '2')
				req |= TLS_I_DH2048;
			else if (c == '5')
				req |= TLS_I_DH512;
			else if (c == 'n' || c == 'N')
				req &= ~TLS_I_TRY_DH;
			else if (c != '/')
			{
				if (LogLevel > 12)
					sm_syslog(LOG_WARNING, NOQID,
						  "STARTTLS=%s, error: illegal value '%s' for DHParameters",
						  who, dhparam);
				dhparam = NULL;
			}
		}
		if (dhparam == NULL)
			SET_DH_DFL;
		else if (*dhparam == '/')
		{
			TLS_OK_F(dhparam, "DHParameters",
				 bitset(TLS_I_DHPAR_EX, req),
				 TLS_S_DHPAR_EX, TLS_T_OTHER);
		}
	}
	if (!ok)
		return ok;

	/* certfile etc. must be "safe". */
	sff = SFF_REGONLY | SFF_SAFEDIRPATH | SFF_NOWLINK
	     | SFF_NOGWFILES | SFF_NOWWFILES
	     | SFF_MUSTOWN | SFF_ROOTOK | SFF_OPENASROOT;
	if (DontLockReadFiles)
		sff |= SFF_NOLOCK;

	TLS_SAFE_F(certfile, sff | TLS_UNR(TLS_I_CERT_UNR, req),
		   bitset(TLS_I_CERT_EX, req),
		   bitset(TLS_S_CERT_EX, status), TLS_S_CERT_OK, srv);
	TLS_SAFE_F(keyfile, sff | TLS_KEYSFF(req),
		   bitset(TLS_I_KEY_EX, req),
		   bitset(TLS_S_KEY_EX, status), TLS_S_KEY_OK, srv);
	TLS_SAFE_F(cacertfile, sff | TLS_UNR(TLS_I_CERTF_UNR, req),
		   bitset(TLS_I_CERTF_EX, req),
		   bitset(TLS_S_CERTF_EX, status), TLS_S_CERTF_OK, srv);
	if (dhparam != NULL && *dhparam == '/')
	{
		TLS_SAFE_F(dhparam, sff | TLS_UNR(TLS_I_DHPAR_UNR, req),
			   bitset(TLS_I_DHPAR_EX, req),
			   bitset(TLS_S_DHPAR_EX, status), TLS_S_DHPAR_OK, srv);
		if (!bitset(TLS_S_DHPAR_OK, status))
			SET_DH_DFL;
	}
# if OPENSSL_VERSION_NUMBER > 0x00907000L
	TLS_SAFE_F(CRLFile, sff | TLS_UNR(TLS_I_CRLF_UNR, req),
		   bitset(TLS_I_CRLF_EX, req),
		   bitset(TLS_S_CRLF_EX, status), TLS_S_CRLF_OK, srv);
# endif /* OPENSSL_VERSION_NUMBER > 0x00907000L */
	if (!ok)
		return ok;
	if (cf2 != NULL)
	{
		TLS_SAFE_F(cf2, sff | TLS_UNR(TLS_I_CERT_UNR, req),
			   bitset(TLS_I_CERT_EX, req),
			   bitset(TLS_S_CERT2_EX, status), TLS_S_CERT2_OK, srv);
	}
	if (kf2 != NULL)
	{
		TLS_SAFE_F(kf2, sff | TLS_KEYSFF(req),
			   bitset(TLS_I_KEY_EX, req),
			   bitset(TLS_S_KEY2_EX, status), TLS_S_KEY2_OK, srv);
	}

	/* create a method and a new context */
	if ((*ctx = SSL_CTX_new(srv ? SSLv23_server_method() :
				      SSLv23_client_method())) == NULL)
	{
		if (LogLevel > 7)
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=%s, error: SSL_CTX_new(SSLv23_%s_method()) failed",
				  who, who);
		if (LogLevel > 9)
			tlslogerr(LOG_WARNING, who);
		return false;
	}

# if OPENSSL_VERSION_NUMBER > 0x00907000L
	if (CRLFile != NULL)
	{
		/* get a pointer to the current certificate validation store */
		store = SSL_CTX_get_cert_store(*ctx);	/* does not fail */
		crl_file = BIO_new(BIO_s_file());
		if (crl_file != NULL)
		{
			if (BIO_read_filename(crl_file, CRLFile) >= 0)
			{
				crl = PEM_read_bio_X509_CRL(crl_file, NULL,
							NULL, NULL);
				BIO_free(crl_file);
				X509_STORE_add_crl(store, crl);
				X509_CRL_free(crl);
				X509_STORE_set_flags(store,
					X509_V_FLAG_CRL_CHECK|X509_V_FLAG_CRL_CHECK_ALL);
				X509_STORE_set_verify_cb_func(store,
						x509_verify_cb);
			}
			else
			{
				if (LogLevel > 9)
				{
					sm_syslog(LOG_WARNING, NOQID,
						  "STARTTLS=%s, error: PEM_read_bio_X509_CRL(%s)=failed",
						  who, CRLFile);
				}

				/* avoid memory leaks */
				BIO_free(crl_file);
				return false;
			}

		}
		else if (LogLevel > 9)
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=%s, error: BIO_new=failed", who);
	}
	else
		store = NULL;
#  if _FFR_CRLPATH
	if (CRLPath != NULL && store != NULL)
	{
		X509_LOOKUP *lookup;

		lookup = X509_STORE_add_lookup(store, X509_LOOKUP_hash_dir());
		if (lookup == NULL)
		{
			if (LogLevel > 9)
			{
				sm_syslog(LOG_WARNING, NOQID,
					  "STARTTLS=%s, error: X509_STORE_add_lookup(hash)=failed",
					  who, CRLFile);
			}
			return false;
		}
		X509_LOOKUP_add_dir(lookup, CRLPath, X509_FILETYPE_PEM);
		X509_STORE_set_flags(store,
			X509_V_FLAG_CRL_CHECK|X509_V_FLAG_CRL_CHECK_ALL);
	}
#  endif /* _FFR_CRLPATH */
# endif /* OPENSSL_VERSION_NUMBER > 0x00907000L */

# if TLS_NO_RSA
	/* turn off backward compatibility, required for no-rsa */
	SSL_CTX_set_options(*ctx, SSL_OP_NO_SSLv2);
# endif /* TLS_NO_RSA */


# if !TLS_NO_RSA
	/*
	**  Create a temporary RSA key
	**  XXX  Maybe we shouldn't create this always (even though it
	**  is only at startup).
	**  It is a time-consuming operation and it is not always necessary.
	**  maybe we should do it only on demand...
	*/

	if (bitset(TLS_I_RSA_TMP, req)
#  if SM_CONF_SHM
	    && ShmId != SM_SHM_NO_ID &&
	    (rsa_tmp = sm_RSA_generate_key(RSA_KEYLENGTH, RSA_F4)) == NULL
#  else /* SM_CONF_SHM */
	    && 0	/* no shared memory: no need to generate key now */
#  endif /* SM_CONF_SHM */
	   )
	{
		if (LogLevel > 7)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=%s, error: RSA_generate_key failed",
				  who);
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, who);
		}
		return false;
	}
# endif /* !TLS_NO_RSA */

	/*
	**  load private key
	**  XXX change this for DSA-only version
	*/

	if (bitset(TLS_S_KEY_OK, status) &&
	    SSL_CTX_use_PrivateKey_file(*ctx, keyfile,
					 SSL_FILETYPE_PEM) <= 0)
	{
		if (LogLevel > 7)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=%s, error: SSL_CTX_use_PrivateKey_file(%s) failed",
				  who, keyfile);
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, who);
		}
		if (bitset(TLS_I_USE_KEY, req))
			return false;
	}

#if _FFR_TLS_USE_CERTIFICATE_CHAIN_FILE
# define SSL_CTX_use_cert(ssl_ctx, certfile) \
	SSL_CTX_use_certificate_chain_file(ssl_ctx, certfile)
# define SSL_CTX_USE_CERT "SSL_CTX_use_certificate_chain_file"
#else
# define SSL_CTX_use_cert(ssl_ctx, certfile) \
	SSL_CTX_use_certificate_file(ssl_ctx, certfile, SSL_FILETYPE_PEM)
# define SSL_CTX_USE_CERT "SSL_CTX_use_certificate_file"
#endif

	/* get the certificate file */
	if (bitset(TLS_S_CERT_OK, status) &&
	    SSL_CTX_use_cert(*ctx, certfile) <= 0)
	{
		if (LogLevel > 7)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=%s, error: %s(%s) failed",
				  who, SSL_CTX_USE_CERT, certfile);
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, who);
		}
		if (bitset(TLS_I_USE_CERT, req))
			return false;
	}

	/* check the private key */
	if (bitset(TLS_S_KEY_OK, status) &&
	    (r = SSL_CTX_check_private_key(*ctx)) <= 0)
	{
		/* Private key does not match the certificate public key */
		if (LogLevel > 5)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=%s, error: SSL_CTX_check_private_key failed(%s): %d",
				  who, keyfile, r);
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, who);
		}
		if (bitset(TLS_I_USE_KEY, req))
			return false;
	}

	/* XXX this code is pretty much duplicated from above! */

	/* load private key */
	if (bitset(TLS_S_KEY2_OK, status) &&
	    SSL_CTX_use_PrivateKey_file(*ctx, kf2, SSL_FILETYPE_PEM) <= 0)
	{
		if (LogLevel > 7)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=%s, error: SSL_CTX_use_PrivateKey_file(%s) failed",
				  who, kf2);
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, who);
		}
	}

	/* get the certificate file */
	if (bitset(TLS_S_CERT2_OK, status) &&
	    SSL_CTX_use_cert(*ctx, cf2) <= 0)
	{
		if (LogLevel > 7)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=%s, error: %s(%s) failed",
				  who, SSL_CTX_USE_CERT, cf2);
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, who);
		}
	}

	/* also check the private key */
	if (bitset(TLS_S_KEY2_OK, status) &&
	    (r = SSL_CTX_check_private_key(*ctx)) <= 0)
	{
		/* Private key does not match the certificate public key */
		if (LogLevel > 5)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=%s, error: SSL_CTX_check_private_key 2 failed: %d",
				  who, r);
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, who);
		}
	}

	/* SSL_CTX_set_quiet_shutdown(*ctx, 1); violation of standard? */

#if SM_SSL_OP_TLS_BLOCK_PADDING_BUG

	/*
	**  In OpenSSL 0.9.8[ab], enabling zlib compression breaks the
	**  padding bug work-around, leading to false positives and
	**  failed connections. We may not interoperate with systems
	**  with the bug, but this is better than breaking on all 0.9.8[ab]
	**  systems that have zlib support enabled.
	**  Note: this checks the runtime version of the library, not
	**  just the compile time version.
	*/

	rt_version = SSLeay();
	if (rt_version >= 0x00908000L && rt_version <= 0x0090802fL)
	{
		comp_methods = SSL_COMP_get_compression_methods();
		if (comp_methods != NULL && sk_SSL_COMP_num(comp_methods) > 0)
			options &= ~SSL_OP_TLS_BLOCK_PADDING_BUG;
	}
#endif
	SSL_CTX_set_options(*ctx, (long) options);

# if !NO_DH
	/* Diffie-Hellman initialization */
	if (bitset(TLS_I_TRY_DH, req))
	{
#if _FFR_TLS_EC
		EC_KEY *ecdh;
#endif /* _FFR_TLS_EC */

		if (tTd(96, 8))
			sm_dprintf("inittls: req=%#lx, status=%#lx\n",
				req, status);
		if (bitset(TLS_S_DHPAR_OK, status))
		{
			BIO *bio;

			if ((bio = BIO_new_file(dhparam, "r")) != NULL)
			{
				dh = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
				BIO_free(bio);
				if (dh == NULL && LogLevel > 7)
				{
					unsigned long err;

					err = ERR_get_error();
					sm_syslog(LOG_WARNING, NOQID,
						  "STARTTLS=%s, error: cannot read DH parameters(%s): %s",
						  who, dhparam,
						  ERR_error_string(err, NULL));
					if (LogLevel > 9)
						tlslogerr(LOG_WARNING, who);
					SET_DH_DFL;
				}
			}
			else
			{
				if (LogLevel > 5)
				{
					sm_syslog(LOG_WARNING, NOQID,
						  "STARTTLS=%s, error: BIO_new_file(%s) failed",
						  who, dhparam);
					if (LogLevel > 9)
						tlslogerr(LOG_WARNING, who);
				}
			}
		}
		if (dh == NULL && bitset(TLS_I_DH1024|TLS_I_DH2048, req))
		{
			int bits;
			DSA *dsa;

			bits = bitset(TLS_I_DH2048, req) ? 2048 : 1024;
			if (tTd(96, 2))
				sm_dprintf("inittls: Generating %d bit DH parameters\n", bits);

			dsa=DSA_new();
			/* this takes a while! */
			(void)DSA_generate_parameters_ex(dsa, bits, NULL, 0,
							 NULL, NULL, NULL);
			dh = DSA_dup_DH(dsa);
			DSA_free(dsa);
		}
		else if (dh == NULL && bitset(TLS_I_DHFIXED, req))
		{
			if (tTd(96, 2))
				sm_dprintf("inittls: Using precomputed 2048 bit DH parameters\n");
			dh = get_dh2048();
		}
		else if (dh == NULL && bitset(TLS_I_DH512, req))
		{
			if (tTd(96, 2))
				sm_dprintf("inittls: Using precomputed 512 bit DH parameters\n");
			dh = get_dh512();
		}

		if (dh == NULL)
		{
			if (LogLevel > 9)
			{
				unsigned long err;

				err = ERR_get_error();
				sm_syslog(LOG_WARNING, NOQID,
					  "STARTTLS=%s, error: cannot read or set DH parameters(%s): %s",
					  who, dhparam,
					  ERR_error_string(err, NULL));
			}
			if (bitset(TLS_I_REQ_DH, req))
				return false;
		}
		else
		{
			/* important to avoid small subgroup attacks */
			SSL_CTX_set_options(*ctx, SSL_OP_SINGLE_DH_USE);

			SSL_CTX_set_tmp_dh(*ctx, dh);
			if (LogLevel > 13)
				sm_syslog(LOG_INFO, NOQID,
					  "STARTTLS=%s, Diffie-Hellman init, key=%d bit (%c)",
					  who, 8 * DH_size(dh), *dhparam);
			DH_free(dh);
		}

#if _FFR_TLS_EC
		ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
		if (ecdh != NULL)
		{
			SSL_CTX_set_options(*ctx, SSL_OP_SINGLE_ECDH_USE);
			SSL_CTX_set_tmp_ecdh(*ctx, ecdh);
			EC_KEY_free(ecdh);
		}
#endif /* _FFR_TLS_EC */

	}
# endif /* !NO_DH */


	/* XXX do we need this cache here? */
	if (bitset(TLS_I_CACHE, req))
	{
		SSL_CTX_sess_set_cache_size(*ctx, 1);
		SSL_CTX_set_timeout(*ctx, 1);
		SSL_CTX_set_session_id_context(*ctx,
			(void *) &server_session_id_context,
			sizeof(server_session_id_context));
		(void) SSL_CTX_set_session_cache_mode(*ctx,
				SSL_SESS_CACHE_SERVER);
	}
	else
	{
		(void) SSL_CTX_set_session_cache_mode(*ctx,
				SSL_SESS_CACHE_OFF);
	}

	/* load certificate locations and default CA paths */
	if (bitset(TLS_S_CERTP_EX, status) && bitset(TLS_S_CERTF_EX, status))
	{
		if ((r = SSL_CTX_load_verify_locations(*ctx, cacertfile,
						       cacertpath)) == 1)
		{
# if !TLS_NO_RSA
			if (bitset(TLS_I_RSA_TMP, req))
				SSL_CTX_set_tmp_rsa_callback(*ctx, tmp_rsa_key);
# endif /* !TLS_NO_RSA */

			/*
			**  We have to install our own verify callback:
			**  SSL_VERIFY_PEER requests a client cert but even
			**  though *FAIL_IF* isn't set, the connection
			**  will be aborted if the client presents a cert
			**  that is not "liked" (can't be verified?) by
			**  the TLS library :-(
			*/

			/*
			**  XXX currently we could call tls_set_verify()
			**  but we hope that that function will later on
			**  only set the mode per connection.
			*/
			SSL_CTX_set_verify(*ctx,
				bitset(TLS_I_NO_VRFY, req) ? SSL_VERIFY_NONE
							   : SSL_VERIFY_PEER,
				NULL);

			/* install verify callback */
			SSL_CTX_set_cert_verify_callback(*ctx, tls_verify_cb,
							 NULL);
			SSL_CTX_set_client_CA_list(*ctx,
				SSL_load_client_CA_file(cacertfile));
		}
		else
		{
			/*
			**  can't load CA data; do we care?
			**  the data is necessary to authenticate the client,
			**  which in turn would be necessary
			**  if we want to allow relaying based on it.
			*/
			if (LogLevel > 5)
			{
				sm_syslog(LOG_WARNING, NOQID,
					  "STARTTLS=%s, error: load verify locs %s, %s failed: %d",
					  who, cacertpath, cacertfile, r);
				if (LogLevel > 9)
					tlslogerr(LOG_WARNING, who);
			}
			if (bitset(TLS_I_VRFY_LOC, req))
				return false;
		}
	}

	/* XXX: make this dependent on an option? */
	if (tTd(96, 9))
		SSL_CTX_set_info_callback(*ctx, apps_ssl_info_cb);

	/* install our own cipher list */
	if (CipherList != NULL && *CipherList != '\0')
	{
		if (SSL_CTX_set_cipher_list(*ctx, CipherList) <= 0)
		{
			if (LogLevel > 7)
			{
				sm_syslog(LOG_WARNING, NOQID,
					  "STARTTLS=%s, error: SSL_CTX_set_cipher_list(%s) failed, list ignored",
					  who, CipherList);

				if (LogLevel > 9)
					tlslogerr(LOG_WARNING, who);
			}
			/* failure if setting to this list is required? */
		}
	}

	if (LogLevel > 12)
		sm_syslog(LOG_INFO, NOQID, "STARTTLS=%s, init=%d", who, ok);

# if 0
	/*
	**  this label is required if we want to have a "clean" exit
	**  see the comments above at the initialization of cf2
	*/

    endinittls:
# endif /* 0 */

	/* undo damage to global variables */
	if (cf2 != NULL)
		*--cf2 = ',';
	if (kf2 != NULL)
		*--kf2 = ',';

	return ok;
}

/*
**  CERT_FP -- get cert fingerprint
**
**	Parameters:
**		cert -- TLS cert
**		mac -- macro storage
**		macro -- where to store cert fp
**
**	Returns:
**		<=0: cert fp calculation failed
**		>0: cert fp calculation ok
*/

static int
cert_fp(cert, evp_digest, mac, macro)
	X509 *cert;
	const EVP_MD *evp_digest;
	MACROS_T *mac;
	char *macro;
{
	unsigned int n;
	int r;
	unsigned char md[EVP_MAX_MD_SIZE];
	char md5h[EVP_MAX_MD_SIZE * 3];
	static const char hexcodes[] = "0123456789ABCDEF";

	n = 0;
	if (X509_digest(cert, EVP_digest, md, &n) == 0 || n <= 0)
	{
		macdefine(mac, A_TEMP, macid(macro), "");
		return 0;
	}

	SM_ASSERT((n * 3) + 2 < sizeof(md5h));
	for (r = 0; r < (int) n; r++)
	{
		md5h[r * 3] = hexcodes[(md[r] & 0xf0) >> 4];
		md5h[(r * 3) + 1] = hexcodes[(md[r] & 0x0f)];
		md5h[(r * 3) + 2] = ':';
	}
	md5h[(n * 3) - 1] = '\0';
	macdefine(mac, A_TEMP, macid(macro), md5h);
	return 1;
}

/*
**  TLS_GET_INFO -- get information about TLS connection
**
**	Parameters:
**		ssl -- TLS connection structure
**		srv -- server or client
**		host -- hostname of other side
**		mac -- macro storage
**		certreq -- did we ask for a cert?
**
**	Returns:
**		result of authentication.
**
**	Side Effects:
**		sets various TLS related macros.
*/

int
tls_get_info(ssl, srv, host, mac, certreq)
	SSL *ssl;
	bool srv;
	char *host;
	MACROS_T *mac;
	bool certreq;
{
	const SSL_CIPHER *c;
	int b, r;
	long verifyok;
	char *s, *who;
	char bitstr[16];
	X509 *cert;

	c = SSL_get_current_cipher(ssl);

	/* cast is just workaround for compiler warning */
	macdefine(mac, A_TEMP, macid("{cipher}"),
		  (char *) SSL_CIPHER_get_name(c));
	b = SSL_CIPHER_get_bits(c, &r);
	(void) sm_snprintf(bitstr, sizeof(bitstr), "%d", b);
	macdefine(mac, A_TEMP, macid("{cipher_bits}"), bitstr);
	(void) sm_snprintf(bitstr, sizeof(bitstr), "%d", r);
	macdefine(mac, A_TEMP, macid("{alg_bits}"), bitstr);
	s = (char *) SSL_get_version(ssl);
	if (s == NULL)
		s = "UNKNOWN";
	macdefine(mac, A_TEMP, macid("{tls_version}"), s);

	who = srv ? "server" : "client";
	cert = SSL_get_peer_certificate(ssl);
	verifyok = SSL_get_verify_result(ssl);
	if (LogLevel > 14)
		sm_syslog(LOG_INFO, NOQID,
			  "STARTTLS=%s, get_verify: %ld get_peer: 0x%lx",
			  who, verifyok, (unsigned long) cert);
	if (cert != NULL)
	{
		X509_NAME *subj, *issuer;
		char buf[MAXNAME];

		subj = X509_get_subject_name(cert);
		issuer = X509_get_issuer_name(cert);
		X509_NAME_oneline(subj, buf, sizeof(buf));
		macdefine(mac, A_TEMP, macid("{cert_subject}"),
			 xtextify(buf, "<>\")"));
		X509_NAME_oneline(issuer, buf, sizeof(buf));
		macdefine(mac, A_TEMP, macid("{cert_issuer}"),
			 xtextify(buf, "<>\")"));

# define LL_BADCERT	8

#define CERTFPMACRO (CertFingerprintAlgorithm != NULL ? "{cert_fp}" : "{cert_md5}")

#define CHECK_X509_NAME(which)	\
	do {	\
		if (r == -1)	\
		{		\
			sm_strlcpy(buf, "BadCertificateUnknown", sizeof(buf)); \
			if (LogLevel > LL_BADCERT)	\
				sm_syslog(LOG_INFO, NOQID,	\
					"STARTTLS=%s, relay=%.100s, field=%s, status=failed to extract CN",	\
					who,	\
					host == NULL ? "local" : host,	\
					which);	\
		}		\
		else if ((size_t)r >= sizeof(buf) - 1)	\
		{		\
			sm_strlcpy(buf, "BadCertificateTooLong", sizeof(buf)); \
			if (LogLevel > 7)	\
				sm_syslog(LOG_INFO, NOQID,	\
					"STARTTLS=%s, relay=%.100s, field=%s, status=CN too long",	\
					who,	\
					host == NULL ? "local" : host,	\
					which);	\
		}		\
		else if ((size_t)r > strlen(buf))	\
		{		\
			sm_strlcpy(buf, "BadCertificateContainsNUL",	\
				sizeof(buf));	\
			if (LogLevel > 7)	\
				sm_syslog(LOG_INFO, NOQID,	\
					"STARTTLS=%s, relay=%.100s, field=%s, status=CN contains NUL",	\
					who,	\
					host == NULL ? "local" : host,	\
					which);	\
		}		\
	} while (0)

		r = X509_NAME_get_text_by_NID(subj, NID_commonName, buf,
			sizeof buf);
		CHECK_X509_NAME("cn_subject");
		macdefine(mac, A_TEMP, macid("{cn_subject}"),
			 xtextify(buf, "<>\")"));
		r = X509_NAME_get_text_by_NID(issuer, NID_commonName, buf,
			sizeof buf);
		CHECK_X509_NAME("cn_issuer");
		macdefine(mac, A_TEMP, macid("{cn_issuer}"),
			 xtextify(buf, "<>\")"));
		(void) cert_fp(cert, EVP_digest, mac, CERTFPMACRO);
	}
	else
	{
		macdefine(mac, A_PERM, macid("{cert_subject}"), "");
		macdefine(mac, A_PERM, macid("{cert_issuer}"), "");
		macdefine(mac, A_PERM, macid("{cn_subject}"), "");
		macdefine(mac, A_PERM, macid("{cn_issuer}"), "");
		macdefine(mac, A_TEMP, macid(CERTFPMACRO), "");
	}
	switch (verifyok)
	{
	  case X509_V_OK:
		if (cert != NULL)
		{
			s = "OK";
			r = TLS_AUTH_OK;
		}
		else
		{
			s = certreq ? "NO" : "NOT",
			r = TLS_AUTH_NO;
		}
		break;
	  default:
		s = "FAIL";
		r = TLS_AUTH_FAIL;
		break;
	}
	macdefine(mac, A_PERM, macid("{verify}"), s);
	if (cert != NULL)
		X509_free(cert);

	/* do some logging */
	if (LogLevel > 8)
	{
		char *vers, *s1, *s2, *cbits, *algbits;

		vers = macget(mac, macid("{tls_version}"));
		cbits = macget(mac, macid("{cipher_bits}"));
		algbits = macget(mac, macid("{alg_bits}"));
		s1 = macget(mac, macid("{verify}"));
		s2 = macget(mac, macid("{cipher}"));

		/* XXX: maybe cut off ident info? */
		sm_syslog(LOG_INFO, NOQID,
			  "STARTTLS=%s, relay=%.100s, version=%.16s, verify=%.16s, cipher=%.64s, bits=%.6s/%.6s",
			  who,
			  host == NULL ? "local" : host,
			  vers, s1, s2, /* sm_snprintf() can deal with NULL */
			  algbits == NULL ? "0" : algbits,
			  cbits == NULL ? "0" : cbits);
		if (LogLevel > 11)
		{
			/*
			**  Maybe run xuntextify on the strings?
			**  That is easier to read but makes it maybe a bit
			**  more complicated to figure out the right values
			**  for the access map...
			*/

			s1 = macget(mac, macid("{cert_subject}"));
			s2 = macget(mac, macid("{cert_issuer}"));
			sm_syslog(LOG_INFO, NOQID,
				  "STARTTLS=%s, cert-subject=%.256s, cert-issuer=%.256s, verifymsg=%s",
				  who, s1, s2,
				  X509_verify_cert_error_string(verifyok));
		}
	}
	return r;
}
/*
**  ENDTLS -- shutdown secure connection
**
**	Parameters:
**		ssl -- SSL connection information.
**		side -- server/client (for logging).
**
**	Returns:
**		success? (EX_* code)
*/

int
endtls(ssl, side)
	SSL *ssl;
	char *side;
{
	int ret = EX_OK;

	if (ssl != NULL)
	{
		int r;

		if ((r = SSL_shutdown(ssl)) < 0)
		{
			if (LogLevel > 11)
			{
				sm_syslog(LOG_WARNING, NOQID,
					  "STARTTLS=%s, SSL_shutdown failed: %d",
					  side, r);
				tlslogerr(LOG_WARNING, side);
			}
			ret = EX_SOFTWARE;
		}
# if !defined(OPENSSL_VERSION_NUMBER) || OPENSSL_VERSION_NUMBER > 0x0090602fL

		/*
		**  Bug in OpenSSL (at least up to 0.9.6b):
		**  From: Lutz.Jaenicke@aet.TU-Cottbus.DE
		**  Message-ID: <20010723152244.A13122@serv01.aet.tu-cottbus.de>
		**  To: openssl-users@openssl.org
		**  Subject: Re: SSL_shutdown() woes (fwd)
		**
		**  The side sending the shutdown alert first will
		**  not care about the answer of the peer but will
		**  immediately return with a return value of "0"
		**  (ssl/s3_lib.c:ssl3_shutdown()). SSL_get_error will evaluate
		**  the value of "0" and as the shutdown alert of the peer was
		**  not received (actually, the program did not even wait for
		**  the answer), an SSL_ERROR_SYSCALL is flagged, because this
		**  is the default rule in case everything else does not apply.
		**
		**  For your server the problem is different, because it
		**  receives the shutdown first (setting SSL_RECEIVED_SHUTDOWN),
		**  then sends its response (SSL_SENT_SHUTDOWN), so for the
		**  server the shutdown was successfull.
		**
		**  As is by know, you would have to call SSL_shutdown() once
		**  and ignore an SSL_ERROR_SYSCALL returned. Then call
		**  SSL_shutdown() again to actually get the server's response.
		**
		**  In the last discussion, Bodo Moeller concluded that a
		**  rewrite of the shutdown code would be necessary, but
		**  probably with another API, as the change would not be
		**  compatible to the way it is now.  Things do not become
		**  easier as other programs do not follow the shutdown
		**  guidelines anyway, so that a lot error conditions and
		**  compitibility issues would have to be caught.
		**
		**  For now the recommondation is to ignore the error message.
		*/

		else if (r == 0)
		{
			if (LogLevel > 15)
			{
				sm_syslog(LOG_WARNING, NOQID,
					  "STARTTLS=%s, SSL_shutdown not done",
					  side);
				tlslogerr(LOG_WARNING, side);
			}
			ret = EX_SOFTWARE;
		}
# endif /* !defined(OPENSSL_VERSION_NUMBER) || OPENSSL_VERSION_NUMBER > 0x0090602fL */
		SSL_free(ssl);
		ssl = NULL;
	}
	return ret;
}

# if !TLS_NO_RSA
/*
**  TMP_RSA_KEY -- return temporary RSA key
**
**	Parameters:
**		s -- TLS connection structure
**		export --
**		keylength --
**
**	Returns:
**		temporary RSA key.
*/

#   ifndef MAX_RSA_TMP_CNT
#    define MAX_RSA_TMP_CNT	1000	/* XXX better value? */
#   endif /* ! MAX_RSA_TMP_CNT */

/* ARGUSED0 */
static RSA *
tmp_rsa_key(s, export, keylength)
	SSL *s;
	int export;
	int keylength;
{
#   if SM_CONF_SHM
	extern int ShmId;
	extern int *PRSATmpCnt;

	if (ShmId != SM_SHM_NO_ID && rsa_tmp != NULL &&
	    ++(*PRSATmpCnt) < MAX_RSA_TMP_CNT)
		return rsa_tmp;
#   endif /* SM_CONF_SHM */

	if (rsa_tmp != NULL)
		RSA_free(rsa_tmp);
	rsa_tmp = sm_RSA_generate_key(RSA_KEYLENGTH, RSA_F4);
	if (rsa_tmp == NULL)
	{
		if (LogLevel > 0)
			sm_syslog(LOG_ERR, NOQID,
				  "STARTTLS=server, tmp_rsa_key: RSA_generate_key failed!");
	}
	else
	{
#   if SM_CONF_SHM
#    if 0
		/*
		**  XXX we can't (yet) share the new key...
		**	The RSA structure contains pointers hence it can't be
		**	easily kept in shared memory.  It must be transformed
		**	into a continous memory region first, then stored,
		**	and later read out again (each time re-transformed).
		*/

		if (ShmId != SM_SHM_NO_ID)
			*PRSATmpCnt = 0;
#    endif /* 0 */
#   endif /* SM_CONF_SHM */
		if (LogLevel > 9)
			sm_syslog(LOG_ERR, NOQID,
				  "STARTTLS=server, tmp_rsa_key: new temp RSA key");
	}
	return rsa_tmp;
}
# endif /* !TLS_NO_RSA */
/*
**  APPS_SSL_INFO_CB -- info callback for TLS connections
**
**	Parameters:
**		s -- TLS connection structure
**		where -- state in handshake
**		ret -- return code of last operation
**
**	Returns:
**		none.
*/

static void
apps_ssl_info_cb(s, where, ret)
	CONST097 SSL *s;
	int where;
	int ret;
{
	int w;
	char *str;
	BIO *bio_err = NULL;

	if (LogLevel > 14)
		sm_syslog(LOG_INFO, NOQID,
			  "STARTTLS: info_callback where=0x%x, ret=%d",
			  where, ret);

	w = where & ~SSL_ST_MASK;
	if (bio_err == NULL)
		bio_err = BIO_new_fp(stderr, BIO_NOCLOSE);

	if (bitset(SSL_ST_CONNECT, w))
		str = "SSL_connect";
	else if (bitset(SSL_ST_ACCEPT, w))
		str = "SSL_accept";
	else
		str = "undefined";

	if (bitset(SSL_CB_LOOP, where))
	{
		if (LogLevel > 12)
			sm_syslog(LOG_NOTICE, NOQID,
				"STARTTLS: %s:%s",
				str, SSL_state_string_long(s));
	}
	else if (bitset(SSL_CB_ALERT, where))
	{
		str = bitset(SSL_CB_READ, where) ? "read" : "write";
		if (LogLevel > 12)
			sm_syslog(LOG_NOTICE, NOQID,
				"STARTTLS: SSL3 alert %s:%s:%s",
				str, SSL_alert_type_string_long(ret),
				SSL_alert_desc_string_long(ret));
	}
	else if (bitset(SSL_CB_EXIT, where))
	{
		if (ret == 0)
		{
			if (LogLevel > 7)
				sm_syslog(LOG_WARNING, NOQID,
					"STARTTLS: %s:failed in %s",
					str, SSL_state_string_long(s));
		}
		else if (ret < 0)
		{
			if (LogLevel > 7)
				sm_syslog(LOG_WARNING, NOQID,
					"STARTTLS: %s:error in %s",
					str, SSL_state_string_long(s));
		}
	}
}
/*
**  TLS_VERIFY_LOG -- log verify error for TLS certificates
**
**	Parameters:
**		ok -- verify ok?
**		ctx -- x509 context
**		name -- from where is this called?
**
**	Returns:
**		1 -- ok
*/

static int
tls_verify_log(ok, ctx, name)
	int ok;
	X509_STORE_CTX *ctx;
	const char *name;
{
	X509 *cert;
	int reason, depth;
	char buf[512];

	cert = X509_STORE_CTX_get_current_cert(ctx);
	reason = X509_STORE_CTX_get_error(ctx);
	depth = X509_STORE_CTX_get_error_depth(ctx);
	X509_NAME_oneline(X509_get_subject_name(cert), buf, sizeof(buf));
	sm_syslog(LOG_INFO, NOQID,
		  "STARTTLS: %s cert verify: depth=%d %s, state=%d, reason=%s",
		  name, depth, buf, ok, X509_verify_cert_error_string(reason));
	return 1;
}

/*
**  TLS_VERIFY_CB -- verify callback for TLS certificates
**
**	Parameters:
**		ctx -- x509 context
**
**	Returns:
**		accept connection?
**		currently: always yes.
*/

static int
#  if !defined(OPENSSL_VERSION_NUMBER) || OPENSSL_VERSION_NUMBER < 0x00907000L
tls_verify_cb(ctx)
	X509_STORE_CTX *ctx;
#  else /* !defined() || OPENSSL_VERSION_NUMBER < 0x00907000L */
tls_verify_cb(ctx, unused)
	X509_STORE_CTX *ctx;
	void *unused;
#  endif /* !defined() || OPENSSL_VERSION_NUMBER < 0x00907000L */
{
	int ok;

	/*
	**  man SSL_CTX_set_cert_verify_callback():
	**  callback should return 1 to indicate verification success
	**  and 0 to indicate verification failure.
	*/

	ok = X509_verify_cert(ctx);
	if (ok <= 0)
	{
		if (LogLevel > 13)
			return tls_verify_log(ok, ctx, "TLS");
	}
	return 1;
}
/*
**  TLSLOGERR -- log the errors from the TLS error stack
**
**	Parameters:
**		level -- syslog level
**		who -- server/client (for logging).
**
**	Returns:
**		none.
*/

void
tlslogerr(level, who)
	int level;
	const char *who;
{
	unsigned long l;
	int line, flags;
	unsigned long es;
	char *file, *data;
	char buf[256];

	es = CRYPTO_thread_id();
	while ((l = ERR_get_error_line_data((const char **) &file, &line,
					    (const char **) &data, &flags))
		!= 0)
	{
		sm_syslog(level, NOQID,
			  "STARTTLS=%s: %lu:%s:%s:%d:%s", who, es,
			  ERR_error_string(l, buf),
			  file, line,
			  bitset(ERR_TXT_STRING, flags) ? data : "");
	}
}

# if OPENSSL_VERSION_NUMBER > 0x00907000L
/*
**  X509_VERIFY_CB -- verify callback
**
**	Parameters:
**		ctx -- x509 context
**
**	Returns:
**		accept connection?
**		currently: always yes.
*/

static int
x509_verify_cb(ok, ctx)
	int ok;
	X509_STORE_CTX *ctx;
{
	if (ok == 0)
	{
		if (LogLevel > 13)
			tls_verify_log(ok, ctx, "x509");
		if (X509_STORE_CTX_get_error(ctx) == X509_V_ERR_UNABLE_TO_GET_CRL)
		{
			X509_STORE_CTX_set_error(ctx, 0);
			return 1;	/* override it */
		}
	}
	return ok;
}
# endif /* OPENSSL_VERSION_NUMBER > 0x00907000L */
#endif /* STARTTLS */
