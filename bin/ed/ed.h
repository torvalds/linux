/* ed.h: type and constant definitions for the ed editor. */
/*-
 * Copyright (c) 1993 Andrew Moore
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ed.h,v 1.5 1994/02/01 00:34:39 alm Exp
 * $FreeBSD$
 */

#include <sys/param.h>
#include <errno.h>
#include <limits.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ERR		(-2)
#define EMOD		(-3)
#define FATAL		(-4)

#define MINBUFSZ 512		/* minimum buffer size - must be > 0 */
#define SE_MAX 30		/* max subexpressions in a regular expression */
#ifdef INT_MAX
# define LINECHARS INT_MAX	/* max chars per line */
#else
# define LINECHARS MAXINT	/* max chars per line */
#endif

/* gflags */
#define GLB 001		/* global command */
#define GPR 002		/* print after command */
#define GLS 004		/* list after command */
#define GNP 010		/* enumerate after command */
#define GSG 020		/* global substitute */

typedef regex_t pattern_t;

/* Line node */
typedef struct	line {
	struct line	*q_forw;
	struct line	*q_back;
	off_t		seek;		/* address of line in scratch buffer */
	int		len;		/* length of line */
} line_t;


typedef struct undo {

/* type of undo nodes */
#define UADD	0
#define UDEL 	1
#define UMOV	2
#define VMOV	3

	int type;			/* command type */
	line_t	*h;			/* head of list */
	line_t  *t;			/* tail of list */
} undo_t;

#ifndef max
# define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
# define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#define INC_MOD(l, k)	((l) + 1 > (k) ? 0 : (l) + 1)
#define DEC_MOD(l, k)	((l) - 1 < 0 ? (k) : (l) - 1)

/* SPL1: disable some interrupts (requires reliable signals) */
#define SPL1() mutex++

/* SPL0: enable all interrupts; check sigflags (requires reliable signals) */
#define SPL0() \
if (--mutex == 0) { \
	if (sigflags & (1 << (SIGHUP - 1))) handle_hup(SIGHUP); \
	if (sigflags & (1 << (SIGINT - 1))) handle_int(SIGINT); \
}

/* STRTOL: convert a string to long */
#define STRTOL(i, p) { \
	if (((i = strtol(p, &p, 10)) == LONG_MIN || i == LONG_MAX) && \
	    errno == ERANGE) { \
		errmsg = "number out of range"; \
	    	i = 0; \
		return ERR; \
	} \
}

#if defined(sun) || defined(NO_REALLOC_NULL)
/* REALLOC: assure at least a minimum size for buffer b */
#define REALLOC(b,n,i,err) \
if ((i) > (n)) { \
	size_t ti = (n); \
	char *ts; \
	SPL1(); \
	if ((b) != NULL) { \
		if ((ts = (char *) realloc((b), ti += max((i), MINBUFSZ))) == NULL) { \
			fprintf(stderr, "%s\n", strerror(errno)); \
			errmsg = "out of memory"; \
			SPL0(); \
			return err; \
		} \
	} else { \
		if ((ts = (char *) malloc(ti += max((i), MINBUFSZ))) == NULL) { \
			fprintf(stderr, "%s\n", strerror(errno)); \
			errmsg = "out of memory"; \
			SPL0(); \
			return err; \
		} \
	} \
	(n) = ti; \
	(b) = ts; \
	SPL0(); \
}
#else /* NO_REALLOC_NULL */
/* REALLOC: assure at least a minimum size for buffer b */
#define REALLOC(b,n,i,err) \
if ((i) > (n)) { \
	size_t ti = (n); \
	char *ts; \
	SPL1(); \
	if ((ts = (char *) realloc((b), ti += max((i), MINBUFSZ))) == NULL) { \
		fprintf(stderr, "%s\n", strerror(errno)); \
		errmsg = "out of memory"; \
		SPL0(); \
		return err; \
	} \
	(n) = ti; \
	(b) = ts; \
	SPL0(); \
}
#endif /* NO_REALLOC_NULL */

/* REQUE: link pred before succ */
#define REQUE(pred, succ) (pred)->q_forw = (succ), (succ)->q_back = (pred)

/* INSQUE: insert elem in circular queue after pred */
#define INSQUE(elem, pred) \
{ \
	REQUE((elem), (pred)->q_forw); \
	REQUE((pred), elem); \
}

/* REMQUE: remove_lines elem from circular queue */
#define REMQUE(elem) REQUE((elem)->q_back, (elem)->q_forw);

/* NUL_TO_NEWLINE: overwrite ASCII NULs with newlines */
#define NUL_TO_NEWLINE(s, l) translit_text(s, l, '\0', '\n')

/* NEWLINE_TO_NUL: overwrite newlines with ASCII NULs */
#define NEWLINE_TO_NUL(s, l) translit_text(s, l, '\n', '\0')


/* Local Function Declarations */
void add_line_node(line_t *);
int append_lines(long);
int apply_subst_template(const char *, regmatch_t *, int, int);
int build_active_list(int);
int cbc_decode(unsigned char *, FILE *);
int cbc_encode(unsigned char *, int, FILE *);
int check_addr_range(long, long);
void clear_active_list(void);
void clear_undo_stack(void);
int close_sbuf(void);
int copy_lines(long);
int delete_lines(long, long);
int display_lines(long, long, int);
line_t *dup_line_node(line_t *);
int exec_command(void);
long exec_global(int, int);
int extract_addr_range(void);
char *extract_pattern(int);
int extract_subst_tail(int *, long *);
char *extract_subst_template(void);
int filter_lines(long, long, char *);
line_t *get_addressed_line_node(long);
pattern_t *get_compiled_pattern(void);
char *get_extended_line(int *, int);
char *get_filename(void);
int get_keyword(void);
long get_line_node_addr(line_t *);
long get_matching_node_addr(pattern_t *, int);
long get_marked_node_addr(int);
char *get_sbuf_line(line_t *);
int get_shell_command(void);
int get_stream_line(FILE *);
int get_tty_line(void);
void handle_hup(int);
void handle_int(int);
void handle_winch(int);
int has_trailing_escape(char *, char *);
int hex_to_binary(int, int);
void init_buffers(void);
int is_legal_filename(char *);
int join_lines(long, long);
int mark_line_node(line_t *, int);
int move_lines(long);
line_t *next_active_node(void);
long next_addr(void);
int open_sbuf(void);
char *parse_char_class(char *);
int pop_undo_stack(void);
undo_t *push_undo_stack(int, long, long);
const char *put_sbuf_line(const char *);
int put_stream_line(FILE *, const char *, int);
int put_tty_line(const char *, int, long, int);
void quit(int);
long read_file(char *, long);
long read_stream(FILE *, long);
int search_and_replace(pattern_t *, int, int);
int set_active_node(line_t *);
void signal_hup(int);
void signal_int(int);
char *strip_escapes(char *);
int substitute_matching_text(pattern_t *, line_t *, int, int);
char *translit_text(char *, int, int, int);
void unmark_line_node(line_t *);
void unset_active_nodes(line_t *, line_t *);
long write_file(char *, const char *, long, long);
long write_stream(FILE *, long, long);

/* global buffers */
extern char stdinbuf[];
extern char *ibuf;
extern char *ibufp;
extern int ibufsz;

/* global flags */
extern int isbinary;
extern int isglobal;
extern int modified;
extern int mutex;
extern int sigflags;

/* global vars */
extern long addr_last;
extern long current_addr;
extern const char *errmsg;
extern long first_addr;
extern int lineno;
extern long second_addr;
extern long u_addr_last;
extern long u_current_addr;
extern long rows;
extern int cols;
extern int newline_added;
extern int scripted;
extern int patlock;
