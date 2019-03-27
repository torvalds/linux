/*
 * Copyright (c) 2004, 2006, 2008 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "windlocl.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/**
 * Process a input UCS4 string according a string-prep profile.
 *
 * @param in input UCS4 string to process
 * @param in_len length of the input string
 * @param out output UCS4 string
 * @param out_len length of the output string.
 * @param flags stringprep profile.
 *
 * @return returns 0 on success, an wind error code otherwise
 * @ingroup wind
 */

int
wind_stringprep(const uint32_t *in, size_t in_len,
		uint32_t *out, size_t *out_len,
		wind_profile_flags flags)
{
    size_t tmp_len = in_len * 3;
    uint32_t *tmp;
    int ret;
    size_t olen;

    if (in_len == 0) {
	*out_len = 0;
	return 0;
    }

    tmp = malloc(tmp_len * sizeof(uint32_t));
    if (tmp == NULL)
	return ENOMEM;

    ret = _wind_stringprep_map(in, in_len, tmp, &tmp_len, flags);
    if (ret) {
	free(tmp);
	return ret;
    }

    olen = *out_len;
    ret = _wind_stringprep_normalize(tmp, tmp_len, tmp, &olen);
    if (ret) {
	free(tmp);
	return ret;
    }
    ret = _wind_stringprep_prohibited(tmp, olen, flags);
    if (ret) {
	free(tmp);
	return ret;
    }
    ret = _wind_stringprep_testbidi(tmp, olen, flags);
    if (ret) {
	free(tmp);
	return ret;
    }

    /* Insignificant Character Handling for ldap-prep */
    if (flags & WIND_PROFILE_LDAP_CASE_EXACT_ATTRIBUTE) {
	ret = _wind_ldap_case_exact_attribute(tmp, olen, out, out_len);
#if 0
    } else if (flags & WIND_PROFILE_LDAP_CASE_EXACT_ASSERTION) {
    } else if (flags & WIND_PROFILE_LDAP_NUMERIC) {
    } else if (flags & WIND_PROFILE_LDAP_TELEPHONE) {
#endif
    } else {
	memcpy(out, tmp, sizeof(out[0]) * olen);
	*out_len = olen;
    }
    free(tmp);

    return ret;
}

static const struct {
    const char *name;
    wind_profile_flags flags;
} profiles[] = {
    { "nameprep", WIND_PROFILE_NAME },
    { "saslprep", WIND_PROFILE_SASL },
    { "ldapprep", WIND_PROFILE_LDAP }
};

/**
 * Try to find the profile given a name.
 *
 * @param name name of the profile.
 * @param flags the resulting profile.
 *
 * @return returns 0 on success, an wind error code otherwise
 * @ingroup wind
 */

int
wind_profile(const char *name, wind_profile_flags *flags)
{
    unsigned int i;

    for (i = 0; i < sizeof(profiles)/sizeof(profiles[0]); i++) {
	if (strcasecmp(profiles[i].name, name) == 0) {
	    *flags = profiles[i].flags;
	    return 0;
	}
    }
    return WIND_ERR_NO_PROFILE;
}
