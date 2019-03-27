/*
 * Copyright (c) 2004 Kungliga Tekniska Högskolan
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
#include <stdio.h>
#include <string.h>

#include "windlocl.h"
#include "punycode_examples.h"

int
main(void)
{
    unsigned i;
    unsigned failures = 0;

    for (i = 0; i < punycode_examples_size; ++i) {
	char buf[256];
	int ret;
	const struct punycode_example *e = &punycode_examples[i];
	size_t len;

	len = sizeof(buf);
	ret = wind_punycode_label_toascii(e->val, e->len, buf, &len);
	if (ret) {
	    printf("punycode %u (%s) failed: %d\n", i, e->description, ret);
	    ++failures;
	    continue;
	}
	if (strncmp(buf, "xn--", 4) == 0) {
	    memmove(buf, buf + 4, len - 4);
	    len -= 4;
	}
	if (len != strlen(e->pc)) {
	    printf("punycode %u (%s) wrong len, actual: %u, expected: %u\n",
		   i, e->description,
		   (unsigned int)len, (unsigned int)strlen(e->pc));
	    printf("buf %s != pc: %s\n", buf, e->pc);
	    ++failures;
	    continue;
	}
	if (strncasecmp(buf, e->pc, len) != 0) {
	    printf("punycode %u (%s) wrong contents, "
		   "actual: \"%.*s\", expected: \"%s\"\n",
		   i, e->description, (unsigned int)len, buf, e->pc);
	    ++failures;
	    continue;
	}
    }
    return failures != 0;
}
