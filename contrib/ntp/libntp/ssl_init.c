/*
 * ssl_init.c	Common OpenSSL initialization code for the various
 *		programs which use it.
 *
 * Moved from ntpd/ntp_crypto.c crypto_setup()
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <ctype.h>
#include <ntp.h>
#include <ntp_debug.h>
#include <lib_strbuf.h>

#ifdef OPENSSL
# include <openssl/crypto.h>
# include <openssl/err.h>
# include <openssl/evp.h>
# include <openssl/opensslv.h>
# include "libssl_compat.h"
# ifdef HAVE_OPENSSL_CMAC_H
#  include <openssl/cmac.h>
#  define CMAC_LENGTH	16
#  define CMAC		"AES128CMAC"
# endif /*HAVE_OPENSSL_CMAC_H*/
int ssl_init_done;

#if OPENSSL_VERSION_NUMBER < 0x10100000L

static void
atexit_ssl_cleanup(void)
{
	if (!ssl_init_done) {
		return;
	}

	ssl_init_done = FALSE;
	EVP_cleanup();
	ERR_free_strings();
}

void
ssl_init(void)
{
	init_lib();

	if ( ! ssl_init_done) {
	    ERR_load_crypto_strings();
	    OpenSSL_add_all_algorithms();
	    atexit(&atexit_ssl_cleanup);
	    ssl_init_done = TRUE;
	}
}

#else /* OPENSSL_VERSION_NUMBER >= 0x10100000L */

void
ssl_init(void)
{
	init_lib();
	ssl_init_done = TRUE;
}

#endif /* OPENSSL_VERSION_NUMBER */


void
ssl_check_version(void)
{
	u_long	v;

	v = OpenSSL_version_num();
	if ((v ^ OPENSSL_VERSION_NUMBER) & ~0xff0L) {
		msyslog(LOG_WARNING,
		    "OpenSSL version mismatch. Built against %lx, you have %lx",
		    (u_long)OPENSSL_VERSION_NUMBER, v);
		fprintf(stderr,
		    "OpenSSL version mismatch. Built against %lx, you have %lx\n",
		    (u_long)OPENSSL_VERSION_NUMBER, v);
	}

	INIT_SSL();
}

#else	/* !OPENSSL */
# define MD5_LENGTH	16
#endif	/* OPENSSL */


/*
 * keytype_from_text	returns OpenSSL NID for digest by name, and
 *			optionally the associated digest length.
 *
 * Used by ntpd authreadkeys(), ntpq and ntpdc keytype()
 */
int
keytype_from_text(
	const char *	text,
	size_t *	pdigest_len
	)
{
	int		key_type;
	u_int		digest_len;
#ifdef OPENSSL	/* --*-- OpenSSL code --*-- */
	const u_long	max_digest_len = MAX_MAC_LEN - sizeof(keyid_t);
	char *		upcased;
	char *		pch;
	EVP_MD const *	md;

	/*
	 * OpenSSL digest short names are capitalized, so uppercase the
	 * digest name before passing to OBJ_sn2nid().  If it is not
	 * recognized but matches our CMAC string use NID_cmac, or if
	 * it begins with 'M' or 'm' use NID_md5 to be consistent with
	 * past behavior.
	 */
	INIT_SSL();

	/* get name in uppercase */
	LIB_GETBUF(upcased);
	strlcpy(upcased, text, LIB_BUFLENGTH);

	for (pch = upcased; '\0' != *pch; pch++) {
		*pch = (char)toupper((unsigned char)*pch);
	}

	key_type = OBJ_sn2nid(upcased);

#   ifdef ENABLE_CMAC
	if (!key_type && !strncmp(CMAC, upcased, strlen(CMAC) + 1)) {
		key_type = NID_cmac;

		if (debug) {
			fprintf(stderr, "%s:%d:%s():%s:key\n",
				__FILE__, __LINE__, __func__, CMAC);
		}
	}
#   endif /*ENABLE_CMAC*/
#else

	key_type = 0;
#endif

	if (!key_type && 'm' == tolower((unsigned char)text[0])) {
		key_type = NID_md5;
	}

	if (!key_type) {
		return 0;
	}

	if (NULL != pdigest_len) {
#ifdef OPENSSL
		md = EVP_get_digestbynid(key_type);
		digest_len = (md) ? EVP_MD_size(md) : 0;

		if (!md || digest_len <= 0) {
#   ifdef ENABLE_CMAC
		    if (key_type == NID_cmac) {
			digest_len = CMAC_LENGTH;

			if (debug) {
				fprintf(stderr, "%s:%d:%s():%s:len\n",
					__FILE__, __LINE__, __func__, CMAC);
			}
		    } else
#   endif /*ENABLE_CMAC*/
		    {
			fprintf(stderr,
				"key type %s is not supported by OpenSSL\n",
				keytype_name(key_type));
			msyslog(LOG_ERR,
				"key type %s is not supported by OpenSSL\n",
				keytype_name(key_type));
			return 0;
		    }
		}

		if (digest_len > max_digest_len) {
		    fprintf(stderr,
			    "key type %s %u octet digests are too big, max %lu\n",
			    keytype_name(key_type), digest_len,
			    max_digest_len);
		    msyslog(LOG_ERR,
			    "key type %s %u octet digests are too big, max %lu",
			    keytype_name(key_type), digest_len,
			    max_digest_len);
		    return 0;
		}
#else
		digest_len = MD5_LENGTH;
#endif
		*pdigest_len = digest_len;
	}

	return key_type;
}


/*
 * keytype_name		returns OpenSSL short name for digest by NID.
 *
 * Used by ntpq and ntpdc keytype()
 */
const char *
keytype_name(
	int nid
	)
{
	static const char unknown_type[] = "(unknown key type)";
	const char *name;

#ifdef OPENSSL
	INIT_SSL();
	name = OBJ_nid2sn(nid);

#   ifdef ENABLE_CMAC
	if (NID_cmac == nid) {
		name = CMAC;

		if (debug) {
			fprintf(stderr, "%s:%d:%s():%s:nid\n",
				__FILE__, __LINE__, __func__, CMAC);
		}
	} else
#   endif /*ENABLE_CMAC*/
	if (NULL == name) {
		name = unknown_type;
	}
#else	/* !OPENSSL follows */
	if (NID_md5 == nid)
		name = "MD5";
	else
		name = unknown_type;
#endif
	return name;
}


/*
 * Use getpassphrase() if configure.ac detected it, as Suns that
 * have it truncate the password in getpass() to 8 characters.
 */
#ifdef HAVE_GETPASSPHRASE
# define	getpass(str)	getpassphrase(str)
#endif

/*
 * getpass_keytype() -- shared between ntpq and ntpdc, only vaguely
 *			related to the rest of ssl_init.c.
 */
char *
getpass_keytype(
	int	keytype
	)
{
	char	pass_prompt[64 + 11 + 1]; /* 11 for " Password: " */

	snprintf(pass_prompt, sizeof(pass_prompt),
		 "%.64s Password: ", keytype_name(keytype));

	return getpass(pass_prompt);
}

