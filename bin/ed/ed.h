/*	$OpenBSD: ed.h,v 1.23 2024/07/16 05:01:10 deraadt Exp $	*/
/*	$NetBSD: ed.h,v 1.23 1995/03/21 09:04:40 cgd Exp $	*/

/* ed.h: type and constant definitions for the ed editor. */
/*
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
 */

#include <limits.h>
#include <regex.h>
#include <signal.h>

#define ERR		(-2)
#define EMOD		(-3)
#define FATAL		(-4)

#define MINBUFSZ 512		/* minimum buffer size - must be > 0 */
#define SE_MAX 30		/* max subexpressions in a regular expression */
#define LINECHARS INT_MAX	/* max chars per line */

/* gflags */
#define GLB 001		/* global command */
#define GPR 002		/* print after command */
#define GLS 004		/* list after command */
#define GNP 010		/* enumerate after command */
#define GSG 020		/* global substitute */

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

/* SPL0: enable all interrupts; check signal flags (requires reliable signals) */
#define SPL0()						\
	do {						\
		if (--mutex == 0) {			\
			if (sigint)			\
				handle_int(SIGINT);	\
		}					\
	} while (0)

/* STRTOI: convert a string to int */
#define STRTOI(i, p) { \
	long l = strtol(p, &p, 10); \
	if (l <= INT_MIN || l >= INT_MAX) { \
		seterrmsg("number out of range"); \
	    	i = 0; \
		return ERR; \
	} else \
		i = (int)l; \
}

/* REALLOC: assure at least a minimum size for buffer b */
#define REALLOC(b,n,i,err) \
if ((i) > (n)) { \
	int ti = (n); \
	char *ts; \
	SPL1(); \
	if ((ts = realloc((b), ti += max((i), MINBUFSZ))) == NULL) { \
		perror(NULL); \
		seterrmsg("out of memory"); \
		SPL0(); \
		return err; \
	} \
	(n) = ti; \
	(b) = ts; \
	SPL0(); \
}

/* REQUE: link pred before succ */
#define REQUE(pred, succ) (pred)->q_forw = (succ), (succ)->q_back = (pred)

/* INSQUE: insert elem in circular queue after pred */
#define INSQUE(elem, pred) \
{ \
	REQUE((elem), (pred)->q_forw); \
	REQUE((pred), elem); \
}

/* remque: remove_lines elem from circular queue */
#define REMQUE(elem) REQUE((elem)->q_back, (elem)->q_forw);

/* NUL_TO_NEWLINE: overwrite ASCII NULs with newlines */
#define NUL_TO_NEWLINE(s, l) translit_text(s, l, '\0', '\n')

/* NEWLINE_TO_NUL: overwrite newlines with ASCII NULs */
#define NEWLINE_TO_NUL(s, l) translit_text(s, l, '\n', '\0')

/* Local Function Declarations */
void add_line_node(line_t *);
int build_active_list(int);
void clear_active_list(void);
void clear_undo_stack(void);
int close_sbuf(void);
int delete_lines(int, int);
int display_lines(int, int, int);
int exec_command(void);
int exec_global(int, int);
int extract_addr_range(void);
int extract_subst_tail(int *, int *);
line_t *get_addressed_line_node(int);
regex_t *get_compiled_pattern(void);
char *get_extended_line(int *, int);
int get_line_node_addr(line_t *);
char *get_sbuf_line(line_t *);
int get_tty_line(void);
void handle_hup(void);
void handle_int(int);
int has_trailing_escape(char *, char *);
void init_buffers(void);
int open_sbuf(void);
int pop_undo_stack(void);
undo_t *push_undo_stack(int, int, int);
char *put_sbuf_line(char *);
int put_tty_line(char *, int, int, int);
void quit(int);
int read_file(char *, int);
int search_and_replace(regex_t *, int, int);
void seterrmsg(char *);
char *strip_escapes(char *);
char *translit_text(char *, int, int, int);
void unmark_line_node(line_t *);
void unset_active_nodes(line_t *, line_t *);
int write_file(char *, char *, int, int);

/* global buffers */
extern char *ibuf;
extern char *ibufp;
extern int ibufsz;

/* global flags */
extern int isbinary;
extern int isglobal;
extern int modified;

extern volatile sig_atomic_t mutex;
extern volatile sig_atomic_t sighup;
extern volatile sig_atomic_t sigint;

/* global vars */
extern int addr_last;
extern int current_addr;
extern int first_addr;
extern int lineno;
extern int second_addr;
