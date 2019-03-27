/* $Header: /p/tcsh/cvsroot/tcsh/tc.bind.c,v 3.46 2015/08/13 08:54:04 christos Exp $ */
/*
 * tc.bind.c: Key binding functions
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
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

RCSID("$tcsh: tc.bind.c,v 3.46 2015/08/13 08:54:04 christos Exp $")

#include "ed.h"
#include "ed.defns.h"

static	void   printkey		(const KEYCMD *, CStr *);
static	KEYCMD parsecmd		(Char *);
static  void   bad_spec		(const Char *);
static	CStr  *parsestring	(const Char *, CStr *);
static	CStr  *parsebind	(const Char *, CStr *);
static	void   print_all_keys	(void);
static	void   printkeys	(KEYCMD *, int, int);
static	void   bindkey_usage	(void);
static	void   list_functions	(void);

extern int MapsAreInited;




/*ARGSUSED*/
void
dobindkey(Char **v, struct command *c)
{
    KEYCMD *map;
    int     ntype, no, removeb, key, bindk;
    Char   *par;
    Char    p;
    KEYCMD  cmd;
    CStr    in;
    CStr    out;
    uChar   ch;

    USE(c);
    if (!MapsAreInited)
	ed_InitMaps();

    map = CcKeyMap;
    ntype = XK_CMD;
    key = removeb = bindk = 0;
    for (no = 1, par = v[no]; 
	 par != NULL && (*par++ & CHAR) == '-'; no++, par = v[no]) {
	if ((p = (*par & CHAR)) == '-') {
	    no++;
	    break;
	}
	else 
	    switch (p) {
	    case 'b':
		bindk = 1;
		break;
	    case 'k':
		key = 1;
		break;
	    case 'a':
		map = CcAltMap;
		break;
	    case 's':
		ntype = XK_STR;
		break;
	    case 'c':
		ntype = XK_EXE;
		break;
	    case 'r':
		removeb = 1;
		break;
	    case 'v':
		ed_InitVIMaps();
		return;
	    case 'e':
		ed_InitEmacsMaps();
		return;
	    case 'd':
#ifdef VIDEFAULT
		ed_InitVIMaps();
#else /* EMACSDEFAULT */
		ed_InitEmacsMaps();
#endif /* VIDEFAULT */
		return;
	    case 'l':
		list_functions();
		return;
	    default:
		bindkey_usage();
		return;
	    }
    }

    if (!v[no]) {
	print_all_keys();
	return;
    }

    if (key) {
	if (!IsArrowKey(v[no]))
	    xprintf(CGETS(20, 1, "Invalid key name `%S'\n"), v[no]);
	in.buf = Strsave(v[no++]);
	in.len = Strlen(in.buf);
    }
    else {
	if (bindk) {
	    if (parsebind(v[no++], &in) == NULL)
		return;
	}
	else {
	    if (parsestring(v[no++], &in) == NULL)
		return;
	}
    }
    cleanup_push(in.buf, xfree);

#ifndef WINNT_NATIVE
    if (in.buf[0] > 0xFF) {
	bad_spec(in.buf);
	cleanup_until(in.buf);
	return;
    }
#endif
    ch = (uChar) in.buf[0];

    if (removeb) {
	if (key)
	    (void) ClearArrowKeys(&in);
	else if (in.len > 1) {
	    (void) DeleteXkey(&in);
	}
	else if (map[ch] == F_XKEY) {
	    (void) DeleteXkey(&in);
	    map[ch] = F_UNASSIGNED;
	}
	else {
	    map[ch] = F_UNASSIGNED;
	}
	cleanup_until(in.buf);
	return;
    }
    if (!v[no]) {
	if (key)
	    PrintArrowKeys(&in);
	else
	    printkey(map, &in);
	cleanup_until(in.buf);
	return;
    }
    if (v[no + 1]) {
	bindkey_usage();
	cleanup_until(in.buf);
	return;
    }
    switch (ntype) {
    case XK_STR:
    case XK_EXE:
	if (parsestring(v[no], &out) == NULL) {
	    cleanup_until(in.buf);
	    return;
	}
	cleanup_push(out.buf, xfree);
	if (key) {
	    if (SetArrowKeys(&in, XmapStr(&out), ntype) == -1)
		xprintf(CGETS(20, 2, "Bad key name: %S\n"), in.buf);
	    else
		cleanup_ignore(out.buf);
	}
	else
	    AddXkey(&in, XmapStr(&out), ntype);
	map[ch] = F_XKEY;
	break;
    case XK_CMD:
	if ((cmd = parsecmd(v[no])) == 0) {
	    cleanup_until(in.buf);
	    return;
	}
	if (key)
	    (void) SetArrowKeys(&in, XmapCmd((int) cmd), ntype);
	else {
	    if (in.len > 1) {
		AddXkey(&in, XmapCmd((int) cmd), ntype);
		map[ch] = F_XKEY;
	    }
	    else {
		ClearXkey(map, &in);
		map[ch] = cmd;
	    }
	}
	break;
    default:
	abort();
	break;
    }
    cleanup_until(in.buf);
    if (key)
	BindArrowKeys();
}

static void
printkey(const KEYCMD *map, CStr *in)
{
    struct KeyFuncs *fp;

    if (in->len < 2) {
	unsigned char *unparsed;

	unparsed = unparsestring(in, STRQQ);
	cleanup_push(unparsed, xfree);
	for (fp = FuncNames; fp->name; fp++) {
	    if (fp->func == map[(uChar) *(in->buf)]) {
		xprintf("%s\t->\t%s\n", unparsed, fp->name);
	    }
	}
	cleanup_until(unparsed);
    }
    else
	PrintXkey(in);
}

static  KEYCMD
parsecmd(Char *str)
{
    struct KeyFuncs *fp;

    for (fp = FuncNames; fp->name; fp++) {
	if (strcmp(short2str(str), fp->name) == 0) {
	    return (KEYCMD) fp->func;
	}
    }
    xprintf(CGETS(20, 3, "Bad command name: %S\n"), str);
    return 0;
}


static void
bad_spec(const Char *str)
{
    xprintf(CGETS(20, 4, "Bad key spec %S\n"), str);
}

static CStr *
parsebind(const Char *s, CStr *str)
{
    struct Strbuf b = Strbuf_INIT;

    cleanup_push(&b, Strbuf_cleanup);
    if (Iscntrl(*s)) {
	Strbuf_append1(&b, *s);
	goto end;
    }

    switch (*s) {
    case '^':
	s++;
#ifdef IS_ASCII
	Strbuf_append1(&b, (*s == '?') ? '\177' : ((*s & CHAR) & 0237));
#else
	Strbuf_append1(&b, (*s == '?') ? CTL_ESC('\177')
		       : _toebcdic[_toascii[*s & CHAR] & 0237]);
#endif
	break;

    case 'F':
    case 'M':
    case 'X':
    case 'C':
#ifdef WINNT_NATIVE
    case 'N':
#endif /* WINNT_NATIVE */
	if (s[1] != '-' || s[2] == '\0')
	    goto bad_spec;
	s += 2;
	switch (s[-2]) {
	case 'F': case 'f':	/* Turn into ^[str */
	    Strbuf_append1(&b, CTL_ESC('\033'));
	    Strbuf_append(&b, s);
	    break;

	case 'C': case 'c':	/* Turn into ^c */
#ifdef IS_ASCII
	    Strbuf_append1(&b, (*s == '?') ? '\177' : ((*s & CHAR) & 0237));
#else
	    Strbuf_append1(&b, (*s == '?') ? CTL_ESC('\177')
			   : _toebcdic[_toascii[*s & CHAR] & 0237]);
#endif
	    break;

	case 'X' : case 'x':	/* Turn into ^Xc */
#ifdef IS_ASCII
	    Strbuf_append1(&b, 'X' & 0237);
#else
	    Strbuf_append1(&b, _toebcdic[_toascii['X'] & 0237]);
#endif
	    Strbuf_append1(&b, *s);
	    break;

	case 'M' : case 'm':	/* Turn into 0x80|c */
	    if (!NoNLSRebind) {
		Strbuf_append1(&b, CTL_ESC('\033'));
	    	Strbuf_append1(&b, *s);
	    } else {
#ifdef IS_ASCII
		Strbuf_append1(&b, *s | 0x80);
#else
		Strbuf_append1(&b, _toebcdic[_toascii[*s] | 0x80]);
#endif
	    }
	    break;
#ifdef WINNT_NATIVE
	case 'N' : case 'n':	/* NT */
		{
			Char bnt;

			bnt = nt_translate_bindkey(s);
			if (bnt != 0)
			        Strbuf_append1(&b, bnt);
			else
				bad_spec(s);
		}
	    break;
#endif /* WINNT_NATIVE */

	default:
	    abort();
	}
	break;

    default:
	goto bad_spec;
    }

 end:
    cleanup_ignore(&b);
    cleanup_until(&b);
    Strbuf_terminate(&b);
    str->buf = xrealloc(b.s, (b.len + 1) * sizeof (*str->buf));
    str->len = b.len;
    return str;

 bad_spec:
    bad_spec(s);
    cleanup_until(&b);
    return NULL;
}


static CStr *
parsestring(const Char *str, CStr *buf)
{
    struct Strbuf b = Strbuf_INIT;
    const Char   *p;
    eChar  es;

    if (*str == 0) {
	xprintf("%s", CGETS(20, 5, "Null string specification\n"));
	return NULL;
    }

    cleanup_push(&b, Strbuf_cleanup);
    for (p = str; *p != 0; p++) {
	if ((*p & CHAR) == '\\' || (*p & CHAR) == '^') {
	    if ((es = parseescape(&p)) == CHAR_ERR) {
		cleanup_until(&b);
		return 0;
	    } else
		Strbuf_append1(&b, es);
	}
	else
	    Strbuf_append1(&b, *p & CHAR);
    }
    cleanup_ignore(&b);
    cleanup_until(&b);
    Strbuf_terminate(&b);
    buf->buf = xrealloc(b.s, (b.len + 1) * sizeof (*buf->buf));
    buf->len = b.len;
    return buf;
}

static void
print_all_keys(void)
{
    int     prev, i;
    CStr nilstr;
    nilstr.buf = NULL;
    nilstr.len = 0;


    xprintf("%s", CGETS(20, 6, "Standard key bindings\n"));
    prev = 0;
    for (i = 0; i < 256; i++) {
	if (CcKeyMap[prev] == CcKeyMap[i])
	    continue;
	printkeys(CcKeyMap, prev, i - 1);
	prev = i;
    }
    printkeys(CcKeyMap, prev, i - 1);

    xprintf("%s", CGETS(20, 7, "Alternative key bindings\n"));
    prev = 0;
    for (i = 0; i < 256; i++) {
	if (CcAltMap[prev] == CcAltMap[i])
	    continue;
	printkeys(CcAltMap, prev, i - 1);
	prev = i;
    }
    printkeys(CcAltMap, prev, i - 1);
    xprintf("%s", CGETS(20, 8, "Multi-character bindings\n"));
    PrintXkey(NULL);	/* print all Xkey bindings */
    xprintf("%s", CGETS(20, 9, "Arrow key bindings\n"));
    PrintArrowKeys(&nilstr);
}

static void
printkeys(KEYCMD *map, int first, int last)
{
    struct KeyFuncs *fp;
    Char    firstbuf[2], lastbuf[2];
    CStr fb, lb;
    unsigned char *unparsed;
    fb.buf = firstbuf;
    lb.buf = lastbuf;

    firstbuf[0] = (Char) first;
    firstbuf[1] = 0;
    lastbuf[0] = (Char) last;
    lastbuf[1] = 0;
    fb.len = 1;
    lb.len = 1;

    unparsed = unparsestring(&fb, STRQQ);
    cleanup_push(unparsed, xfree);
    if (map[first] == F_UNASSIGNED) {
	if (first == last)
	    xprintf(CGETS(20, 10, "%-15s->  is undefined\n"), unparsed);
	cleanup_until(unparsed);
	return;
    }

    for (fp = FuncNames; fp->name; fp++) {
	if (fp->func == map[first]) {
	    if (first == last)
		xprintf("%-15s->  %s\n", unparsed, fp->name);
	    else {
		unsigned char *p;

		p = unparsestring(&lb, STRQQ);
		cleanup_push(p, xfree);
		xprintf("%-4s to %-7s->  %s\n", unparsed, p, fp->name);
	    }
	    cleanup_until(unparsed);
	    return;
	}
    }
    xprintf(CGETS(20, 11, "BUG!!! %s isn't bound to anything.\n"), unparsed);
    if (map == CcKeyMap)
	xprintf("CcKeyMap[%d] == %d\n", first, CcKeyMap[first]);
    else
	xprintf("CcAltMap[%d] == %d\n", first, CcAltMap[first]);
    cleanup_until(unparsed);
}

static void
bindkey_usage(void)
{
    xprintf("%s", CGETS(20, 12,
	    "Usage: bindkey [options] [--] [KEY [COMMAND]]\n"));
    xprintf("%s", CGETS(20, 13,
    	    "    -a   list or bind KEY in alternative key map\n"));
    xprintf("%s", CGETS(20, 14,
	    "    -b   interpret KEY as a C-, M-, F- or X- key name\n"));
    xprintf("%s", CGETS(20, 15,
            "    -s   interpret COMMAND as a literal string to be output\n"));
    xprintf("%s", CGETS(20, 16,
            "    -c   interpret COMMAND as a builtin or external command\n"));
    xprintf("%s", CGETS(20, 17,
	    "    -v   bind all keys to vi bindings\n"));
    xprintf("%s", CGETS(20, 18,
	    "    -e   bind all keys to emacs bindings\n"));
    xprintf(CGETS(20, 19,
	    "    -d   bind all keys to default editor's bindings (%s)\n"),
#ifdef VIDEFAULT
	    "vi"
#else /* EMACSDEFAULT */
	    "emacs"
#endif /* VIDEFAULT */
	    );
    xprintf("%s", CGETS(20, 20,
	    "    -l   list editor commands with descriptions\n"));
    xprintf("%s", CGETS(20, 21,
	    "    -r   remove KEY's binding\n"));
    xprintf("%s", CGETS(20, 22,
	    "    -k   interpret KEY as a symbolic arrow-key name\n"));
    xprintf("%s", CGETS(20, 23,
	    "    --   force a break from option processing\n"));
    xprintf("%s", CGETS(20, 24,
	    "    -u   (or any invalid option) this message\n"));
    xprintf("\n");
    xprintf("%s", CGETS(20, 25,
	    "Without KEY or COMMAND, prints all bindings\n"));
    xprintf("%s", CGETS(20, 26,
	    "Without COMMAND, prints the binding for KEY.\n"));
}

static void
list_functions(void)
{
    struct KeyFuncs *fp;

    for (fp = FuncNames; fp->name; fp++) {
	xprintf("%s\n          %s\n", fp->name, fp->desc);
    }
}
