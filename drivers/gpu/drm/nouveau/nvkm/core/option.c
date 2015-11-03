/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#include <core/option.h>
#include <core/debug.h>

const char *
nvkm_stropt(const char *optstr, const char *opt, int *arglen)
{
	while (optstr && *optstr != '\0') {
		int len = strcspn(optstr, ",=");
		switch (optstr[len]) {
		case '=':
			if (!strncasecmpz(optstr, opt, len)) {
				optstr += len + 1;
				*arglen = strcspn(optstr, ",=");
				return *arglen ? optstr : NULL;
			}
			optstr++;
			break;
		case ',':
			optstr++;
			break;
		default:
			break;
		}
		optstr += len;
	}

	return NULL;
}

bool
nvkm_boolopt(const char *optstr, const char *opt, bool value)
{
	int arglen;

	optstr = nvkm_stropt(optstr, opt, &arglen);
	if (optstr) {
		if (!strncasecmpz(optstr, "0", arglen) ||
		    !strncasecmpz(optstr, "no", arglen) ||
		    !strncasecmpz(optstr, "off", arglen) ||
		    !strncasecmpz(optstr, "false", arglen))
			value = false;
		else
		if (!strncasecmpz(optstr, "1", arglen) ||
		    !strncasecmpz(optstr, "yes", arglen) ||
		    !strncasecmpz(optstr, "on", arglen) ||
		    !strncasecmpz(optstr, "true", arglen))
			value = true;
	}

	return value;
}

long
nvkm_longopt(const char *optstr, const char *opt, long value)
{
	long result = value;
	int arglen;
	char *s;

	optstr = nvkm_stropt(optstr, opt, &arglen);
	if (optstr && (s = kstrndup(optstr, arglen, GFP_KERNEL))) {
		int ret = kstrtol(s, 0, &value);
		if (ret == 0)
			result = value;
		kfree(s);
	}

	return result;
}

int
nvkm_dbgopt(const char *optstr, const char *sub)
{
	int mode = 1, level = CONFIG_NOUVEAU_DEBUG_DEFAULT;

	while (optstr) {
		int len = strcspn(optstr, ",=");
		switch (optstr[len]) {
		case '=':
			if (strncasecmpz(optstr, sub, len))
				mode = 0;
			optstr++;
			break;
		default:
			if (mode) {
				if (!strncasecmpz(optstr, "fatal", len))
					level = NV_DBG_FATAL;
				else if (!strncasecmpz(optstr, "error", len))
					level = NV_DBG_ERROR;
				else if (!strncasecmpz(optstr, "warn", len))
					level = NV_DBG_WARN;
				else if (!strncasecmpz(optstr, "info", len))
					level = NV_DBG_INFO;
				else if (!strncasecmpz(optstr, "debug", len))
					level = NV_DBG_DEBUG;
				else if (!strncasecmpz(optstr, "trace", len))
					level = NV_DBG_TRACE;
				else if (!strncasecmpz(optstr, "paranoia", len))
					level = NV_DBG_PARANOIA;
				else if (!strncasecmpz(optstr, "spam", len))
					level = NV_DBG_SPAM;
			}

			if (optstr[len] != '\0') {
				optstr++;
				mode = 1;
				break;
			}

			return level;
		}
		optstr += len;
	}

	return level;
}
