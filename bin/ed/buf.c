/* buf.c: This file contains the scratch-file buffer routines for the
   ed line editor. */
/*-
 * Copyright (c) 1993 Andrew Moore, Talke Studio.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/file.h>
#include <sys/stat.h>

#include "ed.h"


static FILE *sfp;			/* scratch file pointer */
static off_t sfseek;			/* scratch file position */
static int seek_write;			/* seek before writing */
static line_t buffer_head;		/* incore buffer */

/* get_sbuf_line: get a line of text from the scratch file; return pointer
   to the text */
char *
get_sbuf_line(line_t *lp)
{
	static char *sfbuf = NULL;	/* buffer */
	static size_t sfbufsz;		/* buffer size */

	size_t len;

	if (lp == &buffer_head)
		return NULL;
	seek_write = 1;				/* force seek on write */
	/* out of position */
	if (sfseek != lp->seek) {
		sfseek = lp->seek;
		if (fseeko(sfp, sfseek, SEEK_SET) < 0) {
			fprintf(stderr, "%s\n", strerror(errno));
			errmsg = "cannot seek temp file";
			return NULL;
		}
	}
	len = lp->len;
	REALLOC(sfbuf, sfbufsz, len + 1, NULL);
	if (fread(sfbuf, sizeof(char), len, sfp) != len) {
		fprintf(stderr, "%s\n", strerror(errno));
		errmsg = "cannot read temp file";
		return NULL;
	}
	sfseek += len;				/* update file position */
	sfbuf[len] = '\0';
	return sfbuf;
}


/* put_sbuf_line: write a line of text to the scratch file and add a line node
   to the editor buffer;  return a pointer to the end of the text */
const char *
put_sbuf_line(const char *cs)
{
	line_t *lp;
	size_t len;
	const char *s;

	if ((lp = (line_t *) malloc(sizeof(line_t))) == NULL) {
		fprintf(stderr, "%s\n", strerror(errno));
		errmsg = "out of memory";
		return NULL;
	}
	/* assert: cs is '\n' terminated */
	for (s = cs; *s != '\n'; s++)
		;
	if (s - cs >= LINECHARS) {
		errmsg = "line too long";
		free(lp);
		return NULL;
	}
	len = s - cs;
	/* out of position */
	if (seek_write) {
		if (fseeko(sfp, (off_t)0, SEEK_END) < 0) {
			fprintf(stderr, "%s\n", strerror(errno));
			errmsg = "cannot seek temp file";
			free(lp);
			return NULL;
		}
		sfseek = ftello(sfp);
		seek_write = 0;
	}
	/* assert: SPL1() */
	if (fwrite(cs, sizeof(char), len, sfp) != len) {
		sfseek = -1;
		fprintf(stderr, "%s\n", strerror(errno));
		errmsg = "cannot write temp file";
		free(lp);
		return NULL;
	}
	lp->len = len;
	lp->seek  = sfseek;
	add_line_node(lp);
	sfseek += len;			/* update file position */
	return ++s;
}


/* add_line_node: add a line node in the editor buffer after the current line */
void
add_line_node(line_t *lp)
{
	line_t *cp;

	cp = get_addressed_line_node(current_addr);				/* this get_addressed_line_node last! */
	INSQUE(lp, cp);
	addr_last++;
	current_addr++;
}


/* get_line_node_addr: return line number of pointer */
long
get_line_node_addr(line_t *lp)
{
	line_t *cp = &buffer_head;
	long n = 0;

	while (cp != lp && (cp = cp->q_forw) != &buffer_head)
		n++;
	if (n && cp == &buffer_head) {
		errmsg = "invalid address";
		return ERR;
	 }
	 return n;
}


/* get_addressed_line_node: return pointer to a line node in the editor buffer */
line_t *
get_addressed_line_node(long n)
{
	static line_t *lp = &buffer_head;
	static long on = 0;

	SPL1();
	if (n > on)
		if (n <= (on + addr_last) >> 1)
			for (; on < n; on++)
				lp = lp->q_forw;
		else {
			lp = buffer_head.q_back;
			for (on = addr_last; on > n; on--)
				lp = lp->q_back;
		}
	else
		if (n >= on >> 1)
			for (; on > n; on--)
				lp = lp->q_back;
		else {
			lp = &buffer_head;
			for (on = 0; on < n; on++)
				lp = lp->q_forw;
		}
	SPL0();
	return lp;
}

static char sfn[15] = "";			/* scratch file name */

/* open_sbuf: open scratch file */
int
open_sbuf(void)
{
	int fd;
	int u;

	isbinary = newline_added = 0;
	u = umask(077);
	strcpy(sfn, "/tmp/ed.XXXXXX");
	if ((fd = mkstemp(sfn)) == -1 ||
	    (sfp = fdopen(fd, "w+")) == NULL) {
		if (fd != -1)
			close(fd);
		perror(sfn);
		errmsg = "cannot open temp file";
		umask(u);
		return ERR;
	}
	umask(u);
	return 0;
}


/* close_sbuf: close scratch file */
int
close_sbuf(void)
{
	if (sfp) {
		if (fclose(sfp) < 0) {
			fprintf(stderr, "%s: %s\n", sfn, strerror(errno));
			errmsg = "cannot close temp file";
			return ERR;
		}
		sfp = NULL;
		unlink(sfn);
	}
	sfseek = seek_write = 0;
	return 0;
}


/* quit: remove_lines scratch file and exit */
void
quit(int n)
{
	if (sfp) {
		fclose(sfp);
		unlink(sfn);
	}
	exit(n);
}


static unsigned char ctab[256];		/* character translation table */

/* init_buffers: open scratch buffer; initialize line queue */
void
init_buffers(void)
{
	int i = 0;

	/* Read stdin one character at a time to avoid i/o contention
	   with shell escapes invoked by nonterminal input, e.g.,
	   ed - <<EOF
	   !cat
	   hello, world
	   EOF */
	setbuffer(stdin, stdinbuf, 1);

	/* Ensure stdout is line buffered. This avoids bogus delays
	   of output if stdout is piped through utilities to a terminal. */
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (open_sbuf() < 0)
		quit(2);
	REQUE(&buffer_head, &buffer_head);
	for (i = 0; i < 256; i++)
		ctab[i] = i;
}


/* translit_text: translate characters in a string */
char *
translit_text(char *s, int len, int from, int to)
{
	static int i = 0;

	unsigned char *us;

	ctab[i] = i;			/* restore table to initial state */
	ctab[i = from] = to;
	for (us = (unsigned char *) s; len-- > 0; us++)
		*us = ctab[*us];
	return s;
}
