/* $Header: /p/tcsh/cvsroot/tcsh/tw.color.c,v 1.33 2015/05/28 11:53:49 christos Exp $ */
/*
 * tw.color.c: builtin color ls-F
 */
/*-
 * Copyright (c) 1998 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 */
#include "sh.h"

RCSID("$tcsh: tw.color.c,v 1.33 2015/05/28 11:53:49 christos Exp $")

#include "tw.h"
#include "ed.h"
#include "tc.h"

#ifdef COLOR_LS_F

typedef struct {
    const char	 *s;
    size_t len;
} Str;


#define VAR(suffix,variable,defaultcolor) \
{ \
    suffix, variable, { defaultcolor, sizeof(defaultcolor) - 1 }, \
      { defaultcolor, sizeof(defaultcolor) - 1 } \
}
#define NOS '\0' /* no suffix */

typedef struct {
    const char suffix;
    const char *variable;
    Str	    color;
    Str	    defaultcolor;
} Variable;

static Variable variables[] = {
    VAR('/', "di", "01;34"),	/* Directory */
    VAR('@', "ln", "01;36"),	/* Symbolic link */
    VAR('&', "or", ""),		/* Orphanned symbolic link (defaults to ln) */
    VAR('|', "pi", "33"),	/* Named pipe (FIFO) */
    VAR('=', "so", "01;35"),	/* Socket */
    VAR('>', "do", "01;35"),	/* Door (solaris fast ipc mechanism)  */
    VAR('#', "bd", "01;33"),	/* Block device */
    VAR('%', "cd", "01;33"),	/* Character device */
    VAR('*', "ex", "01;32"),	/* Executable file */
    VAR(NOS, "fi", "0"),	/* Regular file */
    VAR(NOS, "no", "0"),	/* Normal (non-filename) text */
    VAR(NOS, "mi", ""),		/* Missing file (defaults to fi) */
#ifdef IS_ASCII
    VAR(NOS, "lc", "\033["),	/* Left code (ASCII) */
#else
    VAR(NOS, "lc", "\x27["),	/* Left code (EBCDIC)*/
#endif
    VAR(NOS, "rc", "m"),	/* Right code */
    VAR(NOS, "ec", ""),		/* End code (replaces lc+no+rc) */
    VAR(NOS, "su", ""),		/* Setuid file (u+s) */
    VAR(NOS, "sg", ""),		/* Setgid file (g+s) */
    VAR(NOS, "tw", ""),		/* Sticky and other writable dir (+t,o+w) */
    VAR(NOS, "ow", ""),		/* Other writable dir (o+w) but not sticky */
    VAR(NOS, "st", ""),		/* Sticky dir (+t) but not other writable */
    VAR(NOS, "rs", "0"),	/* Reset to normal color */
    VAR(NOS, "hl", "44;37"),    /* Reg file extra hard links, obsolete? */
    VAR(NOS, "mh", "44;37"),    /* Reg file extra hard links */
    VAR(NOS, "ca", "30;41"),    /* File with capability */
};

#define nvariables (sizeof(variables)/sizeof(variables[0]))

enum FileType {
    VDir, VSym, VOrph, VPipe, VSock, VDoor, VBlock, VChr, VExe,
    VFile, VNormal, VMiss, VLeft, VRight, VEnd, VSuid, VSgid, VSticky,
    VOther, Vstird, VReset, Vhard, Vhard2, VCap
};

/*
 * Map from LSCOLORS entry index to Variable array index
 */
static const uint8_t map[] = {
    VDir,	/* Directory */
    VSym,	/* Symbolic Link */
    VSock,	/* Socket */
    VPipe,	/* Named Pipe */
    VExe,	/* Executable */
    VBlock,	/* Block Special */
    VChr,	/* Character Special */
    VSuid,	/* Setuid Executable */
    VSgid,	/* Setgid Executable */
    VSticky,	/* Directory writable to others and sticky */
    VOther,	/* Directory writable to others but not sticky */
};



enum ansi {
    ANSI_RESET_ON = 0,		/* reset colors/styles (white on black) */
    ANSI_BOLD_ON = 1,		/* bold on */
    ANSI_ITALICS_ON = 3,	/* italics on */
    ANSI_UNDERLINE_ON = 4,	/* underline on */
    ANSI_INVERSE_ON = 7,	/* inverse on */
    ANSI_STRIKETHROUGH_ON = 9,	/* strikethrough on */
    ANSI_BOLD_OFF = 21,		/* bold off */
    ANSI_ITALICS_OFF = 23,	/* italics off */
    ANSI_UNDERLINE_OFF = 24,	/* underline off */
    ANSI_INVERSE_OFF = 27,	/* inverse off */
    ANSI_STRIKETHROUGH_OFF = 29,/* strikethrough off */
    ANSI_FG_BLACK = 30,		/* fg black */
    ANSI_FG_RED = 31,		/* fg red */
    ANSI_FG_GREEN = 32,		/* fg green */
    ANSI_FG_YELLOW = 33,	/* fg yellow */
    ANSI_FG_BLUE = 34,		/* fg blue */
    ANSI_FG_MAGENTA = 35,	/* fg magenta */
    ANSI_FG_CYAN = 36,		/* fg cyan */
    ANSI_FG_WHITE = 37,		/* fg white */
    ANSI_FG_DEFAULT = 39,	/* fg default (white) */
    ANSI_BG_BLACK = 40,		/* bg black */
    ANSI_BG_RED = 41,		/* bg red */
    ANSI_BG_GREEN = 42,		/* bg green */
    ANSI_BG_YELLOW = 43,	/* bg yellow */
    ANSI_BG_BLUE = 44,		/* bg blue */
    ANSI_BG_MAGENTA = 45,	/* bg magenta */
    ANSI_BG_CYAN = 46,		/* bg cyan */
    ANSI_BG_WHITE = 47,		/* bg white */
    ANSI_BG_DEFAULT = 49,	/* bg default (black) */
};
#define TCSH_BOLD	0x80
	
typedef struct {
    Str	    extension;	/* file extension */
    Str	    color;	/* color string */
} Extension;

static Extension *extensions = NULL;
static size_t nextensions = 0;

static char *colors = NULL;
int	     color_context_ls = FALSE;	/* do colored ls */
static int  color_context_lsmF = FALSE; /* do colored ls-F */

static int getstring (char **, const Char **, Str *, int);
static void put_color (const Str *);
static void print_color (const Char *, size_t, Char);

/* set_color_context():
 */
void
set_color_context(void)
{
    struct varent *vp = adrof(STRcolor);

    if (vp == NULL || vp->vec == NULL) {
	color_context_ls = FALSE;
	color_context_lsmF = FALSE;
    } else if (!vp->vec[0] || vp->vec[0][0] == '\0') {
	color_context_ls = TRUE;
	color_context_lsmF = TRUE;
    } else {
	size_t i;

	color_context_ls = FALSE;
	color_context_lsmF = FALSE;
	for (i = 0; vp->vec[i]; i++)
	    if (Strcmp(vp->vec[i], STRls) == 0)
		color_context_ls = TRUE;
	    else if (Strcmp(vp->vec[i], STRlsmF) == 0)
		color_context_lsmF = TRUE;
    }
}


/* getstring():
 */
static	int
getstring(char **dp, const Char **sp, Str *pd, int f)
{
    const Char *s = *sp;
    char *d = *dp;
    eChar sc;

    while (*s && (*s & CHAR) != (Char)f && (*s & CHAR) != ':') {
	if ((*s & CHAR) == '\\' || (*s & CHAR) == '^') {
	    if ((sc = parseescape(&s)) == CHAR_ERR)
		return 0;
	}
	else
	    sc = *s++ & CHAR;
	d += one_wctomb(d, sc);
    }

    pd->s = *dp;
    pd->len = d - *dp;
    *sp = s;
    *dp = d;
    return *s == (Char)f;
}

static void
init(size_t colorlen, size_t extnum)
{
    size_t i;

    xfree(extensions);
    for (i = 0; i < nvariables; i++)
	variables[i].color = variables[i].defaultcolor;
    if (colorlen == 0 && extnum == 0) {
	extensions = NULL;
	colors = NULL;
    } else {
	extensions = xmalloc(colorlen + extnum * sizeof(*extensions));
	colors = extnum * sizeof(*extensions) + (char *)extensions;
    }
    nextensions = 0;
}

static int
color(Char x)
{
    static const char ccolors[] = "abcdefghx";
    char *p;
    if (Isupper(x)) {
	x = Tolower(x);
    }

    if (x == '\0' || (p = strchr(ccolors, x)) == NULL)
	return -1;
    return 30 + (p - ccolors);
}

static void
makecolor(char **c, int fg, int bg, Str *v)
{
    int l;
    if (fg & 0x80)
	l = xsnprintf(*c, 12, "%.2d;%.2d;%.2d;%.2d", ANSI_BOLD_ON,
	    fg & ~TCSH_BOLD, (10 + bg) & ~TCSH_BOLD, ANSI_BOLD_OFF);
	l = xsnprintf(*c, 6, "%.2d;%.2d",
	    fg & ~TCSH_BOLD, (10 + bg) & ~TCSH_BOLD);

    v->s = *c;
    v->len = l;
    *c += l + 1;
}

/* parseLSCOLORS():
 * 	Parse the LSCOLORS environment variable
 */
static const Char *xv;	/* setjmp clobbering */
void
parseLSCOLORS(const Char *value)
{
    size_t i, len, clen;
    jmp_buf_t osetexit;
    size_t omark;
    xv = value;

    if (xv == NULL) {
	init(0, 0);
	return;
    }

    len = Strlen(xv);
    len >>= 1;
    clen = len * 12;	/* "??;??;??;??\0" */
    init(clen, 0);

    /* Prevent from crashing if unknown parameters are given. */
    omark = cleanup_push_mark();
    getexit(osetexit);

    /* init pointers */

    if (setexit() == 0) {
	const Char *v = xv;
	char *c = colors;

	int fg, bg;
	for (i = 0; i < len; i++) {
	    fg = color(*v++);
	    if (fg == -1)
		stderror(ERR_BADCOLORVAR, v[-1], '?');

	    bg = color(*v++);
	    if (bg == -1)
		stderror(ERR_BADCOLORVAR, '?', v[-1]);
	    makecolor(&c, fg, bg, &variables[map[i]].color);
	}

    }
    cleanup_pop_mark(omark);
    resexit(osetexit);
}

/* parseLS_COLORS():
 *	Parse the LS_COLORS environment variable
 */
void
parseLS_COLORS(const Char *value)
{
    size_t  i, len;
    const Char	 *v;		/* pointer in value */
    char   *c;			/* pointer in colors */
    Extension *volatile e;	/* pointer in extensions */
    jmp_buf_t osetexit;
    size_t omark;

    (void) &e;


    if (value == NULL) {
	init(0, 0);
	return;
    }

    len = Strlen(value);
    /* allocate memory */
    i = 1;
    for (v = value; *v; v++)
	if ((*v & CHAR) == ':')
	    i++;

    init(len, i);

    /* init pointers */
    v = value;
    c = colors;
    e = extensions;

    /* Prevent from crashing if unknown parameters are given. */

    omark = cleanup_push_mark();
    getexit(osetexit);

    if (setexit() == 0) {

	/* parse */
	while (*v) {
	    switch (*v & CHAR) {
	    case ':':
		v++;
		continue;

	    case '*':		/* :*ext=color: */
		v++;
		if (getstring(&c, &v, &e->extension, '=') &&
		    0 < e->extension.len) {
		    v++;
		    getstring(&c, &v, &e->color, ':');
		    e++;
		    continue;
		}
		break;

	    default:		/* :vl=color: */
		if (v[0] && v[1] && (v[2] & CHAR) == '=') {
		    for (i = 0; i < nvariables; i++)
			if ((Char)variables[i].variable[0] == (v[0] & CHAR) &&
			    (Char)variables[i].variable[1] == (v[1] & CHAR))
			    break;
		    if (i < nvariables) {
			v += 3;
			getstring(&c, &v, &variables[i].color, ':');
			continue;
		    }
		    else
			stderror(ERR_BADCOLORVAR, v[0], v[1]);
		}
		break;
	    }
	    while (*v && (*v & CHAR) != ':')
		v++;
	}
    }

    cleanup_pop_mark(omark);
    resexit(osetexit);

    nextensions = e - extensions;
}

/* put_color():
 */
static void
put_color(const Str *colorp)
{
    size_t  i;
    const char	 *c = colorp->s;
    int	   original_output_raw = output_raw;

    output_raw = TRUE;
    cleanup_push(&original_output_raw, output_raw_restore);
    for (i = colorp->len; 0 < i; i--)
	xputchar(*c++);
    cleanup_until(&original_output_raw);
}


/* print_color():
 */
static void
print_color(const Char *fname, size_t len, Char suffix)
{
    size_t  i;
    char   *filename = short2str(fname);
    char   *last = filename + len;
    Str	   *colorp = &variables[VFile].color;

    switch (suffix) {
    case '>':			/* File is a symbolic link pointing to
				 * a directory */
	colorp = &variables[VDir].color;
	break;
    case '+':			/* File is a hidden directory [aix] or
				 * context dependent [hpux] */
    case ':':			/* File is network special [hpux] */
	break;
    default:
	for (i = 0; i < nvariables; i++)
	    if (variables[i].suffix != NOS &&
		(Char)variables[i].suffix == suffix) {
		colorp = &variables[i].color;
		break;
	    }
	if (i == nvariables) {
	    for (i = 0; i < nextensions; i++)
		if (len >= extensions[i].extension.len
		    && strncmp(last - extensions[i].extension.len,
			       extensions[i].extension.s,
			       extensions[i].extension.len) == 0) {
		  colorp = &extensions[i].color;
		break;
	    }
	}
	break;
    }

    put_color(&variables[VLeft].color);
    put_color(colorp);
    put_color(&variables[VRight].color);
}


/* print_with_color():
 */
void
print_with_color(const Char *filename, size_t len, Char suffix)
{
    if (color_context_lsmF &&
	(haderr ? (didfds ? is2atty : isdiagatty) :
	 (didfds ? is1atty : isoutatty))) {
	print_color(filename, len, suffix);
	xprintf("%S", filename);
	if (0 < variables[VEnd].color.len)
	    put_color(&variables[VEnd].color);
	else {
	    put_color(&variables[VLeft].color);
	    put_color(&variables[VNormal].color);
	    put_color(&variables[VRight].color);
	}
    }
    else
	xprintf("%S", filename);
    xputwchar(suffix);
}


#endif /* COLOR_LS_F */
