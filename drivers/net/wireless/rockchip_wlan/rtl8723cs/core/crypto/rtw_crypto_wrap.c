#include "rtw_crypto_wrap.h"

#ifndef DEBUG_CRYPTO
#define DEBUG_CRYPTO 0
#endif /* DEBUG_CRYTO */

int os_memcmp(const void *s1, const void *s2, size_t n)
{
	return _rtw_memcmp2(s1, s2, n);
}

int os_memcmp_const(const void *a, const void *b, size_t len)
{
	const u8 *aa = a;
	const u8 *bb = b;
	size_t i;
	u8 res;

	for (res = 0, i = 0; i < len; i++)
		res |= aa[i] ^ bb[i];

	return res;
}

void* os_memdup(const void *src, u32 sz)
{
	void *r = rtw_malloc(sz);

	if (r && src)
		_rtw_memcpy(r, src, sz);
	return r;
}

size_t os_strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return p - s;
}


void forced_memzero(void *ptr, size_t len)
{
	_rtw_memset(ptr, 0, len);
}

void bin_clear_free(void *bin, size_t len)
{
	if (bin) {
		forced_memzero(bin, len);
		rtw_mfree(bin, len);
	}
}

void wpa_printf(int level, const char *fmt, ...)
{
#if DEBUG_CRYPTO
#define MSG_LEN 100
	va_list args;
	u8 buf[MSG_LEN] = { 0 };
	int err;

	va_start(args, fmt);
	err = vsnprintf(buf, MSG_LEN, fmt, args);
	va_end(args);

	RTW_INFO("%s", buf);
#undef MSG_LEN
#endif /* DEBUG_CRYPTO */
}

void wpa_hexdump(int level, const char *title, const void *buf, size_t len)
{
#if DEBUG_CRYPTO
	RTW_INFO_DUMP((u8 *)title, buf, len);
#endif /* DEBUG_CRYPTO */
}

void wpa_hexdump_key(int level, const char *title, const void *buf, size_t len)
{
#if DEBUG_CRYPTO
	RTW_INFO_DUMP((u8 *)title, buf, len);
#endif /* DEBUG_CRYPTO */
}
