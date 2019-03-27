/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Utility functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libelf.h>
#include <gelf.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>

#include "ctftools.h"
#include "memory.h"

static void (*terminate_cleanup)(void) = NULL;

/* returns 1 if s1 == s2, 0 otherwise */
int
streq(const char *s1, const char *s2)
{
	if (s1 == NULL) {
		if (s2 != NULL)
			return (0);
	} else if (s2 == NULL)
		return (0);
	else if (strcmp(s1, s2) != 0)
		return (0);

	return (1);
}

int
findelfsecidx(Elf *elf, const char *file, const char *tofind)
{
	Elf_Scn *scn = NULL;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;

	if (gelf_getehdr(elf, &ehdr) == NULL)
		elfterminate(file, "Couldn't read ehdr");

	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		char *name;

		if (gelf_getshdr(scn, &shdr) == NULL) {
			elfterminate(file,
			    "Couldn't read header for section %d",
			    elf_ndxscn(scn));
		}

		if ((name = elf_strptr(elf, ehdr.e_shstrndx,
		    (size_t)shdr.sh_name)) == NULL) {
			elfterminate(file,
			    "Couldn't get name for section %d",
			    elf_ndxscn(scn));
		}

		if (strcmp(name, tofind) == 0)
			return (elf_ndxscn(scn));
	}

	return (-1);
}

size_t
elf_ptrsz(Elf *elf)
{
	GElf_Ehdr ehdr;

	if (gelf_getehdr(elf, &ehdr) == NULL) {
		terminate("failed to read ELF header: %s\n",
		    elf_errmsg(-1));
	}

	if (ehdr.e_ident[EI_CLASS] == ELFCLASS32)
		return (4);
	else if (ehdr.e_ident[EI_CLASS] == ELFCLASS64)
		return (8);
	else
		terminate("unknown ELF class %d\n", ehdr.e_ident[EI_CLASS]);

	/*NOTREACHED*/
	return (0);
}

/*PRINTFLIKE2*/
static void
whine(const char *type, const char *format, va_list ap)
{
	int error = errno;

	fprintf(stderr, "%s: %s: ", type, progname);
	vfprintf(stderr, format, ap);

	if (format[strlen(format) - 1] != '\n')
		fprintf(stderr, ": %s\n", strerror(error));
}

void
set_terminate_cleanup(void (*cleanup)(void))
{
	terminate_cleanup = cleanup;
}

/*PRINTFLIKE1*/
void
terminate(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	whine("ERROR", format, ap);
	va_end(ap);

	if (terminate_cleanup)
		terminate_cleanup();

	if (getenv("CTF_ABORT_ON_TERMINATE") != NULL)
		abort();
#if defined(__FreeBSD__)
/*
 * For the time being just output the termination message, but don't
 * return an exit status that would cause the build to fail. We need
 * to get as much stuff built as possible before going back and
 * figuring out what is wrong with certain files.
 */
	exit(0);
#else
	exit(1);
#endif
}

/*PRINTFLIKE1*/
void
aborterr(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	whine("ERROR", format, ap);
	va_end(ap);

#ifdef illumos
	abort();
#else
	exit(0);
#endif
}

/*PRINTFLIKE1*/
void
warning(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	whine("WARNING", format, ap);
	va_end(ap);

	if (debug_level >= 3)
		terminate("Termination due to warning\n");
}

/*PRINTFLIKE2*/
void
vadebug(int level, const char *format, va_list ap)
{
	if (level > debug_level)
		return;

	(void) fprintf(DEBUG_STREAM, "DEBUG: ");
	(void) vfprintf(DEBUG_STREAM, format, ap);
	fflush(DEBUG_STREAM);
}

/*PRINTFLIKE2*/
void
debug(int level, const char *format, ...)
{
	va_list ap;

	if (level > debug_level)
		return;

	va_start(ap, format);
	(void) vadebug(level, format, ap);
	va_end(ap);
}

char *
mktmpname(const char *origname, const char *suffix)
{
	char *newname;

	newname = xmalloc(strlen(origname) + strlen(suffix) + 1);
	(void) strcpy(newname, origname);
	(void) strcat(newname, suffix);
	return (newname);
}

/*PRINTFLIKE2*/
void
elfterminate(const char *file, const char *fmt, ...)
{
	static char msgbuf[BUFSIZ];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msgbuf, sizeof (msgbuf), fmt, ap);
	va_end(ap);

	terminate("%s: %s: %s\n", file, msgbuf, elf_errmsg(-1));
}

const char *
tdesc_name(tdesc_t *tdp)
{
	return (tdp->t_name == NULL ? "(anon)" : tdp->t_name);
}

static char	*watch_address = NULL;
static int	watch_length = 0;

void
watch_set(void *addr, int len)
{
	watch_address = addr;
	watch_length  = len;
}

void
watch_dump(int v)
{
	char *p = watch_address;
	int i;

	if (watch_address == NULL || watch_length == 0)
		return;

	printf("%d: watch %p len %d\n",v,watch_address,watch_length);
        for (i = 0; i < watch_length; i++) {
                if (*p >= 0x20 && *p < 0x7f) {
                        printf(" %c",*p++ & 0xff);
                } else {
                        printf(" %02x",*p++ & 0xff);
                }
        }
        printf("\n");

}


