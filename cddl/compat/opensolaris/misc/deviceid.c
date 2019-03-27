/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libgeom.h>
#include <devid.h>

int
devid_str_decode(char *devidstr, ddi_devid_t *retdevid, char **retminor_name)
{

	if (strlcpy(retdevid->devid, devidstr, sizeof(retdevid->devid)) >=
	    sizeof(retdevid->devid)) {
		return (EINVAL);
	}
	*retminor_name = strdup("");
	if (*retminor_name == NULL)
		return (ENOMEM);
	return (0);
}

int
devid_deviceid_to_nmlist(char *search_path, ddi_devid_t devid, char *minor_name,
    devid_nmlist_t **retlist)
{
	char path[MAXPATHLEN];
	char *dst;

	if (g_get_name(devid.devid, path, sizeof(path)) == -1)
		return (errno);
	*retlist = malloc(sizeof(**retlist));
	if (*retlist == NULL)
		return (ENOMEM);
	if (strlcpy((*retlist)[0].devname, path,
	    sizeof((*retlist)[0].devname)) >= sizeof((*retlist)[0].devname)) {
		free(*retlist);
		return (ENAMETOOLONG);
	}
	return (0);
}

void
devid_str_free(char *str)
{

	free(str);
}

void
devid_free(ddi_devid_t devid)
{
	/* Do nothing. */
}

void
devid_free_nmlist(devid_nmlist_t *list)
{

	free(list);
}

int
devid_get(int fd, ddi_devid_t *retdevid)
{

	return (ENOENT);
}

int
devid_get_minor_name(int fd, char **retminor_name)
{

	*retminor_name = strdup("");
	if (*retminor_name == NULL)
		return (ENOMEM);
	return (0);
}

char *
devid_str_encode(ddi_devid_t devid, char *minor_name)
{

	return (strdup(devid.devid));
}
