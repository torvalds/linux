/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_OPTION_H__
#define __NVKM_OPTION_H__
#include <core/os.h>

const char *nvkm_stropt(const char *optstr, const char *opt, int *len);
bool nvkm_boolopt(const char *optstr, const char *opt, bool value);
long nvkm_longopt(const char *optstr, const char *opt, long value);
int  nvkm_dbgopt(const char *optstr, const char *sub);

/* compares unterminated string 'str' with zero-terminated string 'cmp' */
static inline int
strncasecmpz(const char *str, const char *cmp, size_t len)
{
	if (strlen(cmp) != len)
		return len;
	return strncasecmp(str, cmp, len);
}
#endif
