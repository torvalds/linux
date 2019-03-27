/*
 * Copyright (c) 2005 Kungliga Tekniska Högskolan
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

/*
 * Write datablob to a filename, don't care about errors.
 */

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_dumpdata (const char *filename, const void *buf, size_t size)
{
    int fd;

    fd = open(filename, O_WRONLY|O_TRUNC|O_CREAT, 0640);
    if (fd < 0)
	return;
    net_write(fd, buf, size);
    close(fd);
}

/*
 * Read all data from a filename, care about errors.
 */

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rk_undumpdata(const char *filename, void **buf, size_t *size)
{
    struct stat sb;
    int fd, ret;
    ssize_t sret;

    *buf = NULL;

    fd = open(filename, O_RDONLY, 0);
    if (fd < 0)
	return errno;
    if (fstat(fd, &sb) != 0){
	ret = errno;
	goto out;
    }
    *buf = malloc(sb.st_size);
    if (*buf == NULL) {
	ret = ENOMEM;
	goto out;
    }
    *size = sb.st_size;

    sret = net_read(fd, *buf, *size);
    if (sret < 0)
	ret = errno;
    else if (sret != (ssize_t)*size) {
	ret = EINVAL;
	free(*buf);
	*buf = NULL;
    } else
	ret = 0;

 out:
    close(fd);
    return ret;
}
