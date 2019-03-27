/*
 * Copyright (c) 1999 - 2001, 2005 Kungliga Tekniska Högskolan
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

#include <config.h>

#include "roken.h"
#include <hex.h>

int
main(int argc, char **argv)
{
    int numerr = 0;
    int numtest = 1;
    struct test {
	void *data;
	size_t len;
	const char *result;
    } *t, tests[] = {
	{ "", 0 , "" },
	{ "a", 1, "61" },
	{ "ab", 2, "6162" },
	{ "abc", 3, "616263" },
	{ "abcd", 4, "61626364" },
	{ "abcde", 5, "6162636465" },
	{ "abcdef", 6, "616263646566" },
	{ "abcdefg", 7, "61626364656667" },
	{ "=", 1, "3D" },
	{ NULL }
    };
    for(t = tests; t->data; t++) {
	char *str;
	int len;
	len = hex_encode(t->data, t->len, &str);
	if(strcmp(str, t->result) != 0) {
	    fprintf(stderr, "failed test %d: %s != %s\n", numtest,
		    str, t->result);
	    numerr++;
	}
	free(str);
	str = strdup(t->result);
	len = strlen(str);
	len = hex_decode(t->result, str, len);
	if(len != t->len) {
	    fprintf(stderr, "failed test %d: len %lu != %lu\n", numtest,
		    (unsigned long)len, (unsigned long)t->len);
	    numerr++;
	} else if(memcmp(str, t->data, t->len) != 0) {
	    fprintf(stderr, "failed test %d: data\n", numtest);
	    numerr++;
	}
	free(str);
	numtest++;
    }

    {
	unsigned char buf[2] = { 0, 0xff } ;
	int len;

	len = hex_decode("A", buf, 1);
	if (len != 1) {
	    fprintf(stderr, "len != 1");
	    numerr++;
	}
	if (buf[0] != 10) {
	    fprintf(stderr, "buf != 10");
	    numerr++;
	}
	if (buf[1] != 0xff) {
	    fprintf(stderr, "buf != 0xff");
	    numerr++;
	}

    }

    return numerr;
}
