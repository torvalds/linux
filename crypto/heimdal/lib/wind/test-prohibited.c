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
#include "windlocl.h"

static uint32_t positives[] = {
    0x00A0, 0x3000,
    0x0080, 0x009F, 0x206A, 0x206F, 0xFEFF,
    0xFFF9, 0xFFFD, 0xFFFE, 0xFFFF,
    0x1D173, 0x1D17A,
    0xE000, 0xF8FF, 0xF0000, 0xFFFFD,
    0x100000, 0x10FFFD,
    0xFDD0, 0xFDEF,
    0xFFFE, 0xFFFF,
    0x1FFFE, 0x1FFFF,
    0x2FFFE, 0x2FFFF,
    0x3FFFE, 0x3FFFF,
    0x4FFFE, 0x4FFFF,
    0x5FFFE, 0x5FFFF,
    0x6FFFE, 0x6FFFF,
    0x7FFFE, 0x7FFFF,
    0x8FFFE, 0x8FFFF,
    0x9FFFE, 0x9FFFF,
    0xAFFFE, 0xAFFFF,
    0xBFFFE, 0xBFFFF,
    0xCFFFE, 0xCFFFF,
    0xDFFFE, 0xDFFFF,
    0xEFFFE, 0xEFFFF,
    0xFFFFE, 0xFFFFF,
    0x10FFFE, 0x10FFFF,
    0xD800,  0xDFFF,
    0xFFF9,
    0xFFFA,
    0xFFFB,
    0xFFFC,
    0x2FF0, 0x2FFB,
    0x0340,
    0x0341,
    0x200E,
    0x200F,
    0x202A,
    0x202B,
    0x202C,
    0x202D,
    0x202E,
    0x206A,
    0x206B,
    0x206C,
    0x206D,
    0x206E,
    0x206F,
    0xE0001,
    0xE0020,
    0xE007F,
};

static uint32_t negatives[] = {
    0x0000, 0x001F, 0x007F,
    0x0020, 0x2069, 0x2070, 0x0FFF8,
    0x1D172, 0x1D17B,
    0xF900,
    0xFDCF, 0xFDF0,
    0x10000,
    0x1FFFD, 0x20000,
    0x2FFFD, 0x30000,
    0x3FFFD, 0x40000,
    0x4FFFD, 0x50000,
    0x5FFFD, 0x60000,
    0x6FFFD, 0x70000,
    0x7FFFD, 0x80000,
    0x8FFFD, 0x90000,
    0x9FFFD, 0xA0000,
    0xAFFFD, 0xB0000,
    0xBFFFD, 0xC0000,
    0xCFFFD, 0xD0000,
    0xDFFFD, 0xE0000,
    0xEFFFD,
    0x110000,
    0xD7FF,
    0xFFF8,
    0x2FEF,  0x2FFC,
};

int
main(void)
{
    unsigned i;
    unsigned failures = 0;

    for (i = 0; i < sizeof(positives)/sizeof(positives[0]); ++i)
	if (!_wind_stringprep_error(positives[i], WIND_PROFILE_NAME)) {
	    printf ("code-point 0x%x not marked as prohibited\n",
		    positives[i]);
	    ++failures;
	}

    for (i = 0; i < sizeof(negatives)/sizeof(negatives[0]); ++i)
	if (_wind_stringprep_error(negatives[i], WIND_PROFILE_NAME)) {
	    printf ("code-point 0x%x not marked as non-prohibited\n",
		    negatives[i]);
	    ++failures;
	}
    return failures != 0;
}
