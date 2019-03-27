///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuklib_mstr_width.c
/// \brief      Calculate width of a multibyte string
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "tuklib_mbstr.h"

#if defined(HAVE_MBRTOWC) && defined(HAVE_WCWIDTH)
#	include <wchar.h>
#endif


extern size_t
tuklib_mbstr_width(const char *str, size_t *bytes)
{
	const size_t len = strlen(str);
	if (bytes != NULL)
		*bytes = len;

#if !(defined(HAVE_MBRTOWC) && defined(HAVE_WCWIDTH))
	// In single-byte mode, the width of the string is the same
	// as its length.
	return len;

#else
	mbstate_t state;
	memset(&state, 0, sizeof(state));

	size_t width = 0;
	size_t i = 0;

	// Convert one multibyte character at a time to wchar_t
	// and get its width using wcwidth().
	while (i < len) {
		wchar_t wc;
		const size_t ret = mbrtowc(&wc, str + i, len - i, &state);
		if (ret < 1 || ret > len)
			return (size_t)-1;

		i += ret;

		const int wc_width = wcwidth(wc);
		if (wc_width < 0)
			return (size_t)-1;

		width += wc_width;
	}

	// Require that the string ends in the initial shift state.
	// This way the caller can be combine the string with other
	// strings without needing to worry about the shift states.
	if (!mbsinit(&state))
		return (size_t)-1;

	return width;
#endif
}
