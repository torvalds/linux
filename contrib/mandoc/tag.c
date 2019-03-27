/*	$Id: tag.c,v 1.19 2018/02/23 16:47:10 schwarze Exp $ */
/*
 * Copyright (c) 2015, 2016 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/types.h>

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mandoc_aux.h"
#include "mandoc_ohash.h"
#include "tag.h"

struct tag_entry {
	size_t	*lines;
	size_t	 maxlines;
	size_t	 nlines;
	int	 prio;
	char	 s[];
};

static	void	 tag_signal(int) __attribute__((__noreturn__));

static struct ohash	 tag_data;
static struct tag_files	 tag_files;


/*
 * Prepare for using a pager.
 * Not all pagers are capable of using a tag file,
 * but for simplicity, create it anyway.
 */
struct tag_files *
tag_init(void)
{
	struct sigaction	 sa;
	int			 ofd;

	ofd = -1;
	tag_files.tfd = -1;
	tag_files.tcpgid = -1;

	/* Clean up when dying from a signal. */

	memset(&sa, 0, sizeof(sa));
	sigfillset(&sa.sa_mask);
	sa.sa_handler = tag_signal;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/*
	 * POSIX requires that a process calling tcsetpgrp(3)
	 * from the background gets a SIGTTOU signal.
	 * In that case, do not stop.
	 */

	sa.sa_handler = SIG_IGN;
	sigaction(SIGTTOU, &sa, NULL);

	/* Save the original standard output for use by the pager. */

	if ((tag_files.ofd = dup(STDOUT_FILENO)) == -1)
		goto fail;

	/* Create both temporary output files. */

	(void)strlcpy(tag_files.ofn, "/tmp/man.XXXXXXXXXX",
	    sizeof(tag_files.ofn));
	(void)strlcpy(tag_files.tfn, "/tmp/man.XXXXXXXXXX",
	    sizeof(tag_files.tfn));
	if ((ofd = mkstemp(tag_files.ofn)) == -1)
		goto fail;
	if ((tag_files.tfd = mkstemp(tag_files.tfn)) == -1)
		goto fail;
	if (dup2(ofd, STDOUT_FILENO) == -1)
		goto fail;
	close(ofd);

	/*
	 * Set up the ohash table to collect output line numbers
	 * where various marked-up terms are documented.
	 */

	mandoc_ohash_init(&tag_data, 4, offsetof(struct tag_entry, s));
	return &tag_files;

fail:
	tag_unlink();
	if (ofd != -1)
		close(ofd);
	if (tag_files.ofd != -1)
		close(tag_files.ofd);
	if (tag_files.tfd != -1)
		close(tag_files.tfd);
	*tag_files.ofn = '\0';
	*tag_files.tfn = '\0';
	tag_files.ofd = -1;
	tag_files.tfd = -1;
	return NULL;
}

/*
 * Set the line number where a term is defined,
 * unless it is already defined at a higher priority.
 */
void
tag_put(const char *s, int prio, size_t line)
{
	struct tag_entry	*entry;
	size_t			 len;
	unsigned int		 slot;

	/* Sanity checks. */

	if (tag_files.tfd <= 0)
		return;
	if (s[0] == '\\' && (s[1] == '&' || s[1] == 'e'))
		s += 2;
	if (*s == '\0' || strchr(s, ' ') != NULL)
		return;

	slot = ohash_qlookup(&tag_data, s);
	entry = ohash_find(&tag_data, slot);

	if (entry == NULL) {

		/* Build a new entry. */

		len = strlen(s) + 1;
		entry = mandoc_malloc(sizeof(*entry) + len);
		memcpy(entry->s, s, len);
		entry->lines = NULL;
		entry->maxlines = entry->nlines = 0;
		ohash_insert(&tag_data, slot, entry);

	} else {

		/* Handle priority 0 entries. */

		if (prio == 0) {
			if (entry->prio == 0)
				entry->prio = -1;
			return;
		}

		/* A better entry is already present, ignore the new one. */

		if (entry->prio > 0 && entry->prio < prio)
			return;

		/* The existing entry is worse, clear it. */

		if (entry->prio < 1 || entry->prio > prio)
			entry->nlines = 0;
	}

	/* Remember the new line. */

	if (entry->maxlines == entry->nlines) {
		entry->maxlines += 4;
		entry->lines = mandoc_reallocarray(entry->lines,
		    entry->maxlines, sizeof(*entry->lines));
	}
	entry->lines[entry->nlines++] = line;
	entry->prio = prio;
}

/*
 * Write out the tags file using the previously collected
 * information and clear the ohash table while going along.
 */
void
tag_write(void)
{
	FILE			*stream;
	struct tag_entry	*entry;
	size_t			 i;
	unsigned int		 slot;

	if (tag_files.tfd <= 0)
		return;
	stream = fdopen(tag_files.tfd, "w");
	entry = ohash_first(&tag_data, &slot);
	while (entry != NULL) {
		if (stream != NULL && entry->prio >= 0)
			for (i = 0; i < entry->nlines; i++)
				fprintf(stream, "%s %s %zu\n",
				    entry->s, tag_files.ofn, entry->lines[i]);
		free(entry->lines);
		free(entry);
		entry = ohash_next(&tag_data, &slot);
	}
	ohash_delete(&tag_data);
	if (stream != NULL)
		fclose(stream);
	else
		close(tag_files.tfd);
	tag_files.tfd = -1;
}

void
tag_unlink(void)
{
	pid_t	 tc_pgid;

	if (tag_files.tcpgid != -1) {
		tc_pgid = tcgetpgrp(tag_files.ofd);
		if (tc_pgid == tag_files.pager_pid ||
		    tc_pgid == getpgid(0) ||
		    getpgid(tc_pgid) == -1)
			(void)tcsetpgrp(tag_files.ofd, tag_files.tcpgid);
	}
	if (*tag_files.ofn != '\0')
		unlink(tag_files.ofn);
	if (*tag_files.tfn != '\0')
		unlink(tag_files.tfn);
}

static void
tag_signal(int signum)
{
	struct sigaction	 sa;

	tag_unlink();
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_DFL;
	sigaction(signum, &sa, NULL);
	kill(getpid(), signum);
	/* NOTREACHED */
	_exit(1);
}
