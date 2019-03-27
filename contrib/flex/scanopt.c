/* flex - tool to generate fast lexical analyzers */

/*  Copyright (c) 1990 The Regents of the University of California. */
/*  All rights reserved. */

/*  This code is derived from software contributed to Berkeley by */
/*  Vern Paxson. */

/*  The United States Government has rights in this work pursuant */
/*  to contract no. DE-AC03-76SF00098 between the United States */
/*  Department of Energy and the University of California. */

/*  This file is part of flex. */

/*  Redistribution and use in source and binary forms, with or without */
/*  modification, are permitted provided that the following conditions */
/*  are met: */

/*  1. Redistributions of source code must retain the above copyright */
/*     notice, this list of conditions and the following disclaimer. */
/*  2. Redistributions in binary form must reproduce the above copyright */
/*     notice, this list of conditions and the following disclaimer in the */
/*     documentation and/or other materials provided with the distribution. */

/*  Neither the name of the University nor the names of its contributors */
/*  may be used to endorse or promote products derived from this software */
/*  without specific prior written permission. */

/*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR */
/*  IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED */
/*  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR */
/*  PURPOSE. */

#include "flexdef.h"
#include "scanopt.h"


/* Internal structures */

#ifdef HAVE_STRCASECMP
#define STRCASECMP(a,b) strcasecmp(a,b)
#else
static int STRCASECMP PROTO ((const char *, const char *));

static int STRCASECMP (a, b)
     const char *a;
     const char *b;
{
	while (tolower (*a++) == tolower (*b++)) ;
	return b - a;
}
#endif

#define ARG_NONE 0x01
#define ARG_REQ  0x02
#define ARG_OPT  0x04
#define IS_LONG  0x08

struct _aux {
	int     flags;		/* The above hex flags. */
	int     namelen;	/* Length of the actual option word, e.g., "--file[=foo]" is 4 */
	int     printlen;	/* Length of entire string, e.g., "--file[=foo]" is 12 */
};


struct _scanopt_t {
	const optspec_t *options;	/* List of options. */
	struct _aux *aux;	/* Auxiliary data about options. */
	int     optc;		/* Number of options. */
	int     argc;		/* Number of args. */
	char  **argv;		/* Array of strings. */
	int     index;		/* Used as: argv[index][subscript]. */
	int     subscript;
	char    no_err_msg;	/* If true, do not print errors. */
	char    has_long;
	char    has_short;
};

/* Accessor functions. These WOULD be one-liners, but portability calls. */
static const char *NAME PROTO ((struct _scanopt_t *, int));
static int PRINTLEN PROTO ((struct _scanopt_t *, int));
static int RVAL PROTO ((struct _scanopt_t *, int));
static int FLAGS PROTO ((struct _scanopt_t *, int));
static const char *DESC PROTO ((struct _scanopt_t *, int));
static int scanopt_err PROTO ((struct _scanopt_t *, int, int, int));
static int matchlongopt PROTO ((char *, char **, int *, char **, int *));
static int find_opt
PROTO ((struct _scanopt_t *, int, char *, int, int *, int *opt_offset));

static const char *NAME (s, i)
     struct _scanopt_t *s;
     int     i;
{
	return s->options[i].opt_fmt +
		((s->aux[i].flags & IS_LONG) ? 2 : 1);
}

static int PRINTLEN (s, i)
     struct _scanopt_t *s;
     int     i;
{
	return s->aux[i].printlen;
}

static int RVAL (s, i)
     struct _scanopt_t *s;
     int     i;
{
	return s->options[i].r_val;
}

static int FLAGS (s, i)
     struct _scanopt_t *s;
     int     i;
{
	return s->aux[i].flags;
}

static const char *DESC (s, i)
     struct _scanopt_t *s;
     int     i;
{
	return s->options[i].desc ? s->options[i].desc : "";
}

#ifndef NO_SCANOPT_USAGE
static int get_cols PROTO ((void));

static int get_cols ()
{
	char   *env;
	int     cols = 80;	/* default */

#ifdef HAVE_NCURSES_H
	initscr ();
	endwin ();
	if (COLS > 0)
		return COLS;
#endif

	if ((env = getenv ("COLUMNS")) != NULL)
		cols = atoi (env);

	return cols;
}
#endif

/* Macro to check for NULL before assigning a value. */
#define SAFE_ASSIGN(ptr,val) \
    do{                      \
        if((ptr)!=NULL)      \
            *(ptr) = val;    \
    }while(0)

/* Macro to assure we reset subscript whenever we adjust s->index.*/
#define INC_INDEX(s,n)     \
    do{                    \
       (s)->index += (n);  \
       (s)->subscript= 0;  \
    }while(0)

scanopt_t *scanopt_init (options, argc, argv, flags)
     const optspec_t *options;
     int     argc;
     char  **argv;
     int     flags;
{
	int     i;
	struct _scanopt_t *s;
	s = (struct _scanopt_t *) malloc (sizeof (struct _scanopt_t));

	s->options = options;
	s->optc = 0;
	s->argc = argc;
	s->argv = (char **) argv;
	s->index = 1;
	s->subscript = 0;
	s->no_err_msg = (flags & SCANOPT_NO_ERR_MSG);
	s->has_long = 0;
	s->has_short = 0;

	/* Determine option count. (Find entry with all zeros). */
	s->optc = 0;
	while (options[s->optc].opt_fmt
	       || options[s->optc].r_val || options[s->optc].desc)
		s->optc++;

	/* Build auxiliary data */
	s->aux = (struct _aux *) malloc (s->optc * sizeof (struct _aux));

	for (i = 0; i < s->optc; i++) {
		const Char *p, *pname;
		const struct optspec_t *opt;
		struct _aux *aux;

		opt = s->options + i;
		aux = s->aux + i;

		aux->flags = ARG_NONE;

		if (opt->opt_fmt[0] == '-' && opt->opt_fmt[1] == '-') {
			aux->flags |= IS_LONG;
			pname = (const Char *)(opt->opt_fmt + 2);
			s->has_long = 1;
		}
		else {
			pname = (const Char *)(opt->opt_fmt + 1);
			s->has_short = 1;
		}
		aux->printlen = strlen (opt->opt_fmt);

		aux->namelen = 0;
		for (p = pname + 1; *p; p++) {
			/* detect required arg */
			if (*p == '=' || isspace (*p)
			    || !(aux->flags & IS_LONG)) {
				if (aux->namelen == 0)
					aux->namelen = p - pname;
				aux->flags |= ARG_REQ;
				aux->flags &= ~ARG_NONE;
			}
			/* detect optional arg. This overrides required arg. */
			if (*p == '[') {
				if (aux->namelen == 0)
					aux->namelen = p - pname;
				aux->flags &= ~(ARG_REQ | ARG_NONE);
				aux->flags |= ARG_OPT;
				break;
			}
		}
		if (aux->namelen == 0)
			aux->namelen = p - pname;
	}
	return (scanopt_t *) s;
}

#ifndef NO_SCANOPT_USAGE
/* these structs are for scanopt_usage(). */
struct usg_elem {
	int     idx;
	struct usg_elem *next;
	struct usg_elem *alias;
};
typedef struct usg_elem usg_elem;


/* Prints a usage message based on contents of optlist.
 * Parameters:
 *   scanner  - The scanner, already initialized with scanopt_init().
 *   fp       - The file stream to write to.
 *   usage    - Text to be prepended to option list.
 * Return:  Always returns 0 (zero).
 * The output looks something like this:

[indent][option, alias1, alias2...][indent][description line1
                                            description line2...]
 */
int     scanopt_usage (scanner, fp, usage)
     scanopt_t *scanner;
     FILE   *fp;
     const char *usage;
{
	struct _scanopt_t *s;
	int     i, columns, indent = 2;
	usg_elem *byr_val = NULL;	/* option indices sorted by r_val */
	usg_elem *store;	/* array of preallocated elements. */
	int     store_idx = 0;
	usg_elem *ue;
	int     maxlen[2];
	int     desccol = 0;
	int     print_run = 0;

	maxlen[0] = 0;
	maxlen[1] = 0;

	s = (struct _scanopt_t *) scanner;

	if (usage) {
		fprintf (fp, "%s\n", usage);
	}
	else {
		/* Find the basename of argv[0] */
		const char *p;

		p = s->argv[0] + strlen (s->argv[0]);
		while (p != s->argv[0] && *p != '/')
			--p;
		if (*p == '/')
			p++;

		fprintf (fp, _("Usage: %s [OPTIONS]...\n"), p);
	}
	fprintf (fp, "\n");

	/* Sort by r_val and string. Yes, this is O(n*n), but n is small. */
	store = (usg_elem *) malloc (s->optc * sizeof (usg_elem));
	for (i = 0; i < s->optc; i++) {

		/* grab the next preallocate node. */
		ue = store + store_idx++;
		ue->idx = i;
		ue->next = ue->alias = NULL;

		/* insert into list. */
		if (!byr_val)
			byr_val = ue;
		else {
			int     found_alias = 0;
			usg_elem **ue_curr, **ptr_if_no_alias = NULL;

			ue_curr = &byr_val;
			while (*ue_curr) {
				if (RVAL (s, (*ue_curr)->idx) ==
				    RVAL (s, ue->idx)) {
					/* push onto the alias list. */
					ue_curr = &((*ue_curr)->alias);
					found_alias = 1;
					break;
				}
				if (!ptr_if_no_alias
				    &&
				    STRCASECMP (NAME (s, (*ue_curr)->idx),
						NAME (s, ue->idx)) > 0) {
					ptr_if_no_alias = ue_curr;
				}
				ue_curr = &((*ue_curr)->next);
			}
			if (!found_alias && ptr_if_no_alias)
				ue_curr = ptr_if_no_alias;
			ue->next = *ue_curr;
			*ue_curr = ue;
		}
	}

#if 0
	if (1) {
		printf ("ORIGINAL:\n");
		for (i = 0; i < s->optc; i++)
			printf ("%2d: %s\n", i, NAME (s, i));
		printf ("SORTED:\n");
		ue = byr_val;
		while (ue) {
			usg_elem *ue2;

			printf ("%2d: %s\n", ue->idx, NAME (s, ue->idx));
			for (ue2 = ue->alias; ue2; ue2 = ue2->next)
				printf ("  +---> %2d: %s\n", ue2->idx,
					NAME (s, ue2->idx));
			ue = ue->next;
		}
	}
#endif

	/* Now build each row of output. */

	/* first pass calculate how much room we need. */
	for (ue = byr_val; ue; ue = ue->next) {
		usg_elem *ap;
		int     len = 0;
		int     nshort = 0, nlong = 0;


#define CALC_LEN(i) do {\
          if(FLAGS(s,i) & IS_LONG) \
              len +=  (nlong++||nshort) ? 2+PRINTLEN(s,i) : PRINTLEN(s,i);\
          else\
              len +=  (nshort++||nlong)? 2+PRINTLEN(s,i) : PRINTLEN(s,i);\
        }while(0)

		if (!(FLAGS (s, ue->idx) & IS_LONG))
			CALC_LEN (ue->idx);

		/* do short aliases first. */
		for (ap = ue->alias; ap; ap = ap->next) {
			if (FLAGS (s, ap->idx) & IS_LONG)
				continue;
			CALC_LEN (ap->idx);
		}

		if (FLAGS (s, ue->idx) & IS_LONG)
			CALC_LEN (ue->idx);

		/* repeat the above loop, this time for long aliases. */
		for (ap = ue->alias; ap; ap = ap->next) {
			if (!(FLAGS (s, ap->idx) & IS_LONG))
				continue;
			CALC_LEN (ap->idx);
		}

		if (len > maxlen[0])
			maxlen[0] = len;

		/* It's much easier to calculate length for description column! */
		len = strlen (DESC (s, ue->idx));
		if (len > maxlen[1])
			maxlen[1] = len;
	}

	/* Determine how much room we have, and how much we will allocate to each col.
	 * Do not address pathological cases. Output will just be ugly. */
	columns = get_cols () - 1;
	if (maxlen[0] + maxlen[1] + indent * 2 > columns) {
		/* col 0 gets whatever it wants. we'll wrap the desc col. */
		maxlen[1] = columns - (maxlen[0] + indent * 2);
		if (maxlen[1] < 14)	/* 14 is arbitrary lower limit on desc width. */
			maxlen[1] = INT_MAX;
	}
	desccol = maxlen[0] + indent * 2;

#define PRINT_SPACES(fp,n)\
    do{\
        int _n;\
        _n=(n);\
        while(_n-- > 0)\
            fputc(' ',(fp));\
    }while(0)


	/* Second pass (same as above loop), this time we print. */
	/* Sloppy hack: We iterate twice. The first time we print short and long options.
	   The second time we print those lines that have ONLY long options. */
	while (print_run++ < 2) {
		for (ue = byr_val; ue; ue = ue->next) {
			usg_elem *ap;
			int     nwords = 0, nchars = 0, has_short = 0;

/* TODO: get has_short schtick to work */
			has_short = !(FLAGS (s, ue->idx) & IS_LONG);
			for (ap = ue->alias; ap; ap = ap->next) {
				if (!(FLAGS (s, ap->idx) & IS_LONG)) {
					has_short = 1;
					break;
				}
			}
			if ((print_run == 1 && !has_short) ||
			    (print_run == 2 && has_short))
				continue;

			PRINT_SPACES (fp, indent);
			nchars += indent;

/* Print, adding a ", " between aliases. */
#define PRINT_IT(i) do{\
                  if(nwords++)\
                      nchars+=fprintf(fp,", ");\
                  nchars+=fprintf(fp,"%s",s->options[i].opt_fmt);\
            }while(0)

			if (!(FLAGS (s, ue->idx) & IS_LONG))
				PRINT_IT (ue->idx);

			/* print short aliases first. */
			for (ap = ue->alias; ap; ap = ap->next) {
				if (!(FLAGS (s, ap->idx) & IS_LONG))
					PRINT_IT (ap->idx);
			}


			if (FLAGS (s, ue->idx) & IS_LONG)
				PRINT_IT (ue->idx);

			/* repeat the above loop, this time for long aliases. */
			for (ap = ue->alias; ap; ap = ap->next) {
				if (FLAGS (s, ap->idx) & IS_LONG)
					PRINT_IT (ap->idx);
			}

			/* pad to desccol */
			PRINT_SPACES (fp, desccol - nchars);

			/* Print description, wrapped to maxlen[1] columns. */
			if (1) {
				const char *pstart;

				pstart = DESC (s, ue->idx);
				while (1) {
					int     n = 0;
					const char *lastws = NULL, *p;

					p = pstart;

					while (*p && n < maxlen[1]
					       && *p != '\n') {
						if (isspace ((Char)(*p))
						    || *p == '-') lastws =
								p;
						n++;
						p++;
					}

					if (!*p) {	/* hit end of desc. done. */
						fprintf (fp, "%s\n",
							 pstart);
						break;
					}
					else if (*p == '\n') {	/* print everything up to here then wrap. */
						fprintf (fp, "%.*s\n", n,
							 pstart);
						PRINT_SPACES (fp, desccol);
						pstart = p + 1;
						continue;
					}
					else {	/* we hit the edge of the screen. wrap at space if possible. */
						if (lastws) {
							fprintf (fp,
								 "%.*s\n",
								 (int)(lastws - pstart),
								 pstart);
							pstart =
								lastws + 1;
						}
						else {
							fprintf (fp,
								 "%.*s\n",
								 n,
								 pstart);
							pstart = p + 1;
						}
						PRINT_SPACES (fp, desccol);
						continue;
					}
				}
			}
		}
	}			/* end while */
	free (store);
	return 0;
}
#endif /* no scanopt_usage */


static int scanopt_err (s, opt_offset, is_short, err)
     struct _scanopt_t *s;
     int     opt_offset;
     int     is_short;
     int     err;
{
	const char *optname = "";
	char    optchar[2];
	const optspec_t *opt = NULL;

	if (opt_offset >= 0)
		opt = s->options + opt_offset;

	if (!s->no_err_msg) {

		if (s->index > 0 && s->index < s->argc) {
			if (is_short) {
				optchar[0] =
					s->argv[s->index][s->subscript];
				optchar[1] = '\0';
				optname = optchar;
			}
			else {
				optname = s->argv[s->index];
			}
		}

		fprintf (stderr, "%s: ", s->argv[0]);
		switch (err) {
		case SCANOPT_ERR_ARG_NOT_ALLOWED:
			fprintf (stderr,
				 _
				 ("option `%s' doesn't allow an argument\n"),
				 optname);
			break;
		case SCANOPT_ERR_ARG_NOT_FOUND:
			fprintf (stderr,
				 _("option `%s' requires an argument\n"),
				 optname);
			break;
		case SCANOPT_ERR_OPT_AMBIGUOUS:
			fprintf (stderr, _("option `%s' is ambiguous\n"),
				 optname);
			break;
		case SCANOPT_ERR_OPT_UNRECOGNIZED:
			fprintf (stderr, _("Unrecognized option `%s'\n"),
				 optname);
			break;
		default:
			fprintf (stderr, _("Unknown error=(%d)\n"), err);
			break;
		}
	}
	return err;
}


/* Internal. Match str against the regex  ^--([^=]+)(=(.*))?
 * return 1 if *looks* like a long option.
 * 'str' is the only input argument, the rest of the arguments are output only.
 * optname will point to str + 2
 *
 */
static int matchlongopt (str, optname, optlen, arg, arglen)
     char   *str;
     char  **optname;
     int    *optlen;
     char  **arg;
     int    *arglen;
{
	char   *p;

	*optname = *arg = (char *) 0;
	*optlen = *arglen = 0;

	/* Match regex /--./   */
	p = str;
	if (p[0] != '-' || p[1] != '-' || !p[2])
		return 0;

	p += 2;
	*optname = (char *) p;

	/* find the end of optname */
	while (*p && *p != '=')
		++p;

	*optlen = p - *optname;

	if (!*p)
		/* an option with no '=...' part. */
		return 1;


	/* We saw an '=' char. The rest of p is the arg. */
	p++;
	*arg = p;
	while (*p)
		++p;
	*arglen = p - *arg;

	return 1;
}


/* Internal. Look up long or short option by name.
 * Long options must match a non-ambiguous prefix, or exact match.
 * Short options must be exact.
 * Return boolean true if found and no error.
 * Error stored in err_code or zero if no error. */
static int find_opt (s, lookup_long, optstart, len, err_code, opt_offset)
     struct _scanopt_t *s;
     int     lookup_long;
     char   *optstart;
     int     len;
     int    *err_code;
     int    *opt_offset;
{
	int     nmatch = 0, lastr_val = 0, i;

	*err_code = 0;
	*opt_offset = -1;

	if (!optstart)
		return 0;

	for (i = 0; i < s->optc; i++) {
		char   *optname;

		optname =
			(char *) (s->options[i].opt_fmt +
				  (lookup_long ? 2 : 1));

		if (lookup_long && (s->aux[i].flags & IS_LONG)) {
			if (len > s->aux[i].namelen)
				continue;

			if (strncmp (optname, optstart, len) == 0) {
				nmatch++;
				*opt_offset = i;

				/* exact match overrides all. */
				if (len == s->aux[i].namelen) {
					nmatch = 1;
					break;
				}

				/* ambiguity is ok between aliases. */
				if (lastr_val
				    && lastr_val ==
				    s->options[i].r_val) nmatch--;
				lastr_val = s->options[i].r_val;
			}
		}
		else if (!lookup_long && !(s->aux[i].flags & IS_LONG)) {
			if (optname[0] == optstart[0]) {
				nmatch++;
				*opt_offset = i;
			}
		}
	}

	if (nmatch == 0) {
		*err_code = SCANOPT_ERR_OPT_UNRECOGNIZED;
		*opt_offset = -1;
	}
	else if (nmatch > 1) {
		*err_code = SCANOPT_ERR_OPT_AMBIGUOUS;
		*opt_offset = -1;
	}

	return *err_code ? 0 : 1;
}


int     scanopt (svoid, arg, optindex)
     scanopt_t *svoid;
     char  **arg;
     int    *optindex;
{
	char   *optname = NULL, *optarg = NULL, *pstart;
	int     namelen = 0, arglen = 0;
	int     errcode = 0, has_next;
	const optspec_t *optp;
	struct _scanopt_t *s;
	struct _aux *auxp;
	int     is_short;
	int     opt_offset = -1;

	s = (struct _scanopt_t *) svoid;

	/* Normalize return-parameters. */
	SAFE_ASSIGN (arg, NULL);
	SAFE_ASSIGN (optindex, s->index);

	if (s->index >= s->argc)
		return 0;

	/* pstart always points to the start of our current scan. */
	pstart = s->argv[s->index] + s->subscript;
	if (!pstart)
		return 0;

	if (s->subscript == 0) {

		/* test for exact match of "--" */
		if (pstart[0] == '-' && pstart[1] == '-' && !pstart[2]) {
			SAFE_ASSIGN (optindex, s->index + 1);
			INC_INDEX (s, 1);
			return 0;
		}

		/* Match an opt. */
		if (matchlongopt
		    (pstart, &optname, &namelen, &optarg, &arglen)) {

			/* it LOOKS like an opt, but is it one?! */
			if (!find_opt
			    (s, 1, optname, namelen, &errcode,
			     &opt_offset)) {
				scanopt_err (s, opt_offset, 0, errcode);
				return errcode;
			}
			/* We handle this below. */
			is_short = 0;

			/* Check for short opt.  */
		}
		else if (pstart[0] == '-' && pstart[1]) {
			/* Pass through to below. */
			is_short = 1;
			s->subscript++;
			pstart++;
		}

		else {
			/* It's not an option. We're done. */
			return 0;
		}
	}

	/* We have to re-check the subscript status because it
	 * may have changed above. */

	if (s->subscript != 0) {

		/* we are somewhere in a run of short opts,
		 * e.g., at the 'z' in `tar -xzf` */

		optname = pstart;
		namelen = 1;
		is_short = 1;

		if (!find_opt
		    (s, 0, pstart, namelen, &errcode, &opt_offset)) {
			return scanopt_err (s, opt_offset, 1, errcode);
		}

		optarg = pstart + 1;
		if (!*optarg) {
			optarg = NULL;
			arglen = 0;
		}
		else
			arglen = strlen (optarg);
	}

	/* At this point, we have a long or short option matched at opt_offset into
	 * the s->options array (and corresponding aux array).
	 * A trailing argument is in {optarg,arglen}, if any.
	 */

	/* Look ahead in argv[] to see if there is something
	 * that we can use as an argument (if needed). */
	has_next = s->index + 1 < s->argc
		&& strcmp ("--", s->argv[s->index + 1]) != 0;

	optp = s->options + opt_offset;
	auxp = s->aux + opt_offset;

	/* case: no args allowed */
	if (auxp->flags & ARG_NONE) {
		if (optarg && !is_short) {
			scanopt_err (s, opt_offset, is_short, errcode =
				     SCANOPT_ERR_ARG_NOT_ALLOWED);
			INC_INDEX (s, 1);
			return errcode;
		}
		else if (!optarg)
			INC_INDEX (s, 1);
		else
			s->subscript++;
		return optp->r_val;
	}

	/* case: required */
	if (auxp->flags & ARG_REQ) {
		if (!optarg && !has_next)
			return scanopt_err (s, opt_offset, is_short,
					    SCANOPT_ERR_ARG_NOT_FOUND);

		if (!optarg) {
			/* Let the next argv element become the argument. */
			SAFE_ASSIGN (arg, s->argv[s->index + 1]);
			INC_INDEX (s, 2);
		}
		else {
			SAFE_ASSIGN (arg, (char *) optarg);
			INC_INDEX (s, 1);
		}
		return optp->r_val;
	}

	/* case: optional */
	if (auxp->flags & ARG_OPT) {
		SAFE_ASSIGN (arg, optarg);
		INC_INDEX (s, 1);
		return optp->r_val;
	}


	/* Should not reach here. */
	return 0;
}


int     scanopt_destroy (svoid)
     scanopt_t *svoid;
{
	struct _scanopt_t *s;

	s = (struct _scanopt_t *) svoid;
	if (s) {
		if (s->aux)
			free (s->aux);
		free (s);
	}
	return 0;
}


/* vim:set tabstop=8 softtabstop=4 shiftwidth=4: */
