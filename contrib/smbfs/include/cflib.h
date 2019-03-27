/*
 * Copyright (c) 2000, Boris Popov
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 * $Id: cflib.h,v 1.3 2000/07/11 01:51:49 bp Exp $
 */
#ifndef _zzzzzz_RCFILE_H_
#define _zzzzzz_RCFILE_H_

struct rcfile;

/*
 * A unified options parser
 */
enum opt_argtype {OPTARG_STR, OPTARG_INT, OPTARG_BOOL};

struct opt_args;

typedef int opt_callback_t (struct opt_args*);

#define	OPTFL_NONE	0x0000
#define	OPTFL_HAVEMIN	0x0001
#define	OPTFL_HAVEMAX	0x0002
#define	OPTFL_MINMAX	NAFL_HAVEMIN | NAFL_HAVEMAX

struct opt_args {
	enum opt_argtype type;
	int	opt;		/* command line option */
	char *	name;		/* rc file equiv */
	int	flag;		/* OPTFL_* */
	int	ival;		/* int/bool values, or max len for str value */
	char *	str;		/* string value */
	int	min;		/* min for ival */
	int	max;		/* max for ival */
	opt_callback_t *fn;	/* call back to validate */
};

extern int cf_opterr, cf_optind, cf_optopt, cf_optreset;
extern const char *cf_optarg;

__BEGIN_DECLS

int  opt_args_parse(struct rcfile *, struct opt_args *, const char *,
	opt_callback_t *);
int  opt_args_parseopt(struct opt_args *, int, char *, opt_callback_t *);

int  cf_getopt(int, char * const *, const char *);

int  rc_open(const char *, const char *, struct rcfile **);
int  rc_close(struct rcfile *);
int  rc_merge(const char *, struct rcfile **);
int  rc_getstringptr(struct rcfile *, const char *, const char *, char **);
int  rc_getstring(struct rcfile *, const char *, const char *, size_t, char *);
int  rc_getint(struct rcfile *, const char *, const char *, int *);
int  rc_getbool(struct rcfile *, const char *, const char *, int *);

__END_DECLS

#endif	/* _zzzzzz_RCFILE_H_ */
