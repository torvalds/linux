#include <stdio.h>
#include <stdarg.h>

int
pcap_vsnprintf(char *str, size_t str_size, const char *format, va_list args)
{
	int ret;

	ret = _vsnprintf_s(str, str_size, _TRUNCATE, format, args);

	/*
	 * XXX - _vsnprintf() and _snprintf() do *not* guarantee
	 * that str is null-terminated, but C99's vsnprintf()
	 * and snprintf() do, and we want to offer C99 behavior,
	 * so forcibly null-terminate the string.
	 */
	str[str_size - 1] = '\0';
	return (ret);
}

int
pcap_snprintf(char *str, size_t str_size, const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	ret = pcap_vsnprintf(str, str_size, format, args);
	va_end(args);
	return (ret);
}
