/*
 * include/ssl_applink.c -- common NTP code for openssl/applink.c
 *
 * Each program which uses OpenSSL should include this file in _one_
 * of its source files and call ssl_applink() before any OpenSSL
 * functions.
 */

#if defined(OPENSSL) && defined(SYS_WINNT)
# ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable: 4152)
#  ifndef OPENSSL_NO_AUTOLINK
#   include "msvc_ssl_autolib.h"
#  endif
# endif
# if OPENSSL_VERSION_NUMBER < 0x10100000L
#  include <openssl/applink.c>
# endif
# ifdef _MSC_VER
#  pragma warning(pop)
# endif
#endif

#if defined(OPENSSL) && defined(_MSC_VER) && defined(_DEBUG)
#define WRAP_DBG_MALLOC
#endif

#ifdef WRAP_DBG_MALLOC
static void *wrap_dbg_malloc(size_t s, const char *f, int l);
static void *wrap_dbg_realloc(void *p, size_t s, const char *f, int l);
static void wrap_dbg_free(void *p);
static void wrap_dbg_free_ex(void *p, const char *f, int l);
#endif


#if defined(OPENSSL) && defined(SYS_WINNT)

void ssl_applink(void);

void
ssl_applink(void)
{
#if OPENSSL_VERSION_NUMBER >= 0x10100000L

#   ifdef WRAP_DBG_MALLOC
	CRYPTO_set_mem_functions(wrap_dbg_malloc, wrap_dbg_realloc, wrap_dbg_free_ex);
#   else
	OPENSSL_malloc_init();
#   endif

#  else

#   ifdef WRAP_DBG_MALLOC
	CRYPTO_set_mem_ex_functions(wrap_dbg_malloc, wrap_dbg_realloc, wrap_dbg_free);
#   else
	CRYPTO_malloc_init();
#   endif

#endif /* OpenSSL version cascade */
}
#else	/* !OPENSSL || !SYS_WINNT */
#define ssl_applink()	do {} while (0)
#endif


#ifdef WRAP_DBG_MALLOC
/*
 * OpenSSL malloc overriding uses different parameters
 * for DEBUG malloc/realloc/free (lacking block type).
 * Simple wrappers convert.
 */
static void *wrap_dbg_malloc(size_t s, const char *f, int l)
{
	void *ret;

	ret = _malloc_dbg(s, _NORMAL_BLOCK, f, l);
	return ret;
}

static void *wrap_dbg_realloc(void *p, size_t s, const char *f, int l)
{
	void *ret;

	ret = _realloc_dbg(p, s, _NORMAL_BLOCK, f, l);
	return ret;
}

static void wrap_dbg_free(void *p)
{
	_free_dbg(p, _NORMAL_BLOCK);
}

static void wrap_dbg_free_ex(void *p, const char *f, int l)
{
	(void)f;
	(void)l;
	_free_dbg(p, _NORMAL_BLOCK);
}
#endif	/* WRAP_DBG_MALLOC */
