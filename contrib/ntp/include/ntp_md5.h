/*
 * ntp_md5.h: deal with md5.h headers
 *
 * Use the system MD5 if available, otherwise libisc's.
 */
#ifndef NTP_MD5_H
#define NTP_MD5_H

#ifdef OPENSSL
# include <openssl/evp.h>
# include "libssl_compat.h"
# ifdef HAVE_OPENSSL_CMAC_H
#  include <openssl/cmac.h>
#  define CMAC                  "AES128CMAC"
#  define AES_128_KEY_SIZE      16
# endif /*HAVE_OPENSSL_CMAC_H*/
#else	/* !OPENSSL follows */
/*
 * Provide OpenSSL-alike MD5 API if we're not using OpenSSL
 */
# if defined HAVE_MD5_H && defined HAVE_MD5INIT
#  include <md5.h>
# else
#  include "isc/md5.h"
   typedef isc_md5_t		MD5_CTX;
#  define MD5Init(c)		isc_md5_init(c)
#  define MD5Update(c, p, s)	isc_md5_update(c, p, s)
#  define MD5Final(d, c)	isc_md5_final((c), (d))	/* swapped */
# endif

  typedef MD5_CTX			EVP_MD_CTX;

# define EVP_MD_CTX_free(c)		free(c)
# define EVP_MD_CTX_new()		calloc(1, sizeof(MD5_CTX))
# define EVP_get_digestbynid(t)		NULL
# define EVP_md5()			NULL
# define EVP_MD_CTX_init(c)
# define EVP_MD_CTX_set_flags(c, f)
# define EVP_DigestInit(c, dt)		(MD5Init(c), 1)
# define EVP_DigestInit_ex(c, dt, i)	(MD5Init(c), 1)
# define EVP_DigestUpdate(c, p, s)	MD5Update(c, (const void *)(p), \
						  s)
# define EVP_DigestFinal(c, d, pdl)	\
	do {				\
		MD5Final((d), (c));	\
		*(pdl) = 16;		\
	} while (0)
# endif	/* !OPENSSL */
#endif	/* NTP_MD5_H */
