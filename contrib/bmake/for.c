/*	$NetBSD: for.c,v 1.53 2017/04/16 21:04:44 riastradh Exp $	*/

/*
 * Copyright (c) 1992, The Regents of the University of California.
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

#ifndef MAKE_NATIVE
static char rcsid[] = "$NetBSD: for.c,v 1.53 2017/04/16 21:04:44 riastradh Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)for.c	8.1 (Berkeley) 6/6/93";
#else
__RCSID("$NetBSD: for.c,v 1.53 2017/04/16 21:04:44 riastradh Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * for.c --
 *	Functions to handle loops in a makefile.
 *
 * Interface:
 *	For_Eval 	Evaluate the loop in the passed line.
 *	For_Run		Run accumulated loop
 *
 */

#include    <assert.h>
#include    <ctype.h>

#include    "make.h"
#include    "hash.h"
#include    "dir.h"
#include    "buf.h"
#include    "strlist.h"

#define FOR_SUB_ESCAPE_CHAR  1
#define FOR_SUB_ESCAPE_BRACE 2
#define FOR_SUB_ESCAPE_PAREN 4

/*
 * For statements are of the form:
 *
 * .for <variable> in <varlist>
 * ...
 * .endfor
 *
 * The trick is to look for the matching end inside for for loop
 * To do that, we count the current nesting level of the for loops.
 * and the .endfor statements, accumulating all the statements between
 * the initial .for loop and the matching .endfor;
 * then we evaluate the for loop for each variable in the varlist.
 *
 * Note that any nested fors are just passed through; they get handled
 * recursively in For_Eval when we're expanding the enclosing for in
 * For_Run.
 */

static int  	  forLevel = 0;  	/* Nesting level	*/

/*
 * State of a for loop.
 */
typedef struct _For {
    Buffer	  buf;			/* Body of loop		*/
    strlist_t     vars;			/* Iteration variables	*/
    strlist_t     items;		/* Substitution items */
    char          *parse_buf;
    int           short_var;
    int           sub_next;
} For;

static For        *accumFor;            /* Loop being accumulated */



static char *
make_str(const char *ptr, int len)
{
	char *new_ptr;

	new_ptr = bmake_malloc(len + 1);
	memcpy(new_ptr, ptr, len);
	new_ptr[len] = 0;
	return new_ptr;
}

static void
For_Free(For *arg)
{
    Buf_Destroy(&arg->buf, TRUE);
    strlist_clean(&arg->vars);
    strlist_clean(&arg->items);
    free(arg->parse_buf);

    free(arg);
}

/*-
 *-----------------------------------------------------------------------
 * For_Eval --
 *	Evaluate the for loop in the passed line. The line
 *	looks like this:
 *	    .for <variable> in <varlist>
 *
 * Input:
 *	line		Line to parse
 *
 * Results:
 *      0: Not a .for statement, parse the line
 *	1: We found a for loop
 *     -1: A .for statement with a bad syntax error, discard.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
int
For_Eval(char *line)
{
    For *new_for;
    char *ptr = line, *sub;
    int len;
    int escapes;
    unsigned char ch;
    char **words, *word_buf;
    int n, nwords;

    /* Skip the '.' and any following whitespace */
    for (ptr++; *ptr && isspace((unsigned char) *ptr); ptr++)
	continue;

    /*
     * If we are not in a for loop quickly determine if the statement is
     * a for.
     */
    if (ptr[0] != 'f' || ptr[1] != 'o' || ptr[2] != 'r' ||
	    !isspace((unsigned char) ptr[3])) {
	if (ptr[0] == 'e' && strncmp(ptr+1, "ndfor", 5) == 0) {
	    Parse_Error(PARSE_FATAL, "for-less endfor");
	    return -1;
	}
	return 0;
    }
    ptr += 3;

    /*
     * we found a for loop, and now we are going to parse it.
     */

    new_for = bmake_malloc(sizeof *new_for);
    memset(new_for, 0, sizeof *new_for);

    /* Grab the variables. Terminate on "in". */
    for (;; ptr += len) {
	while (*ptr && isspace((unsigned char) *ptr))
	    ptr++;
	if (*ptr == '\0') {
	    Parse_Error(PARSE_FATAL, "missing `in' in for");
	    For_Free(new_for);
	    return -1;
	}
	for (len = 1; ptr[len] && !isspace((unsigned char)ptr[len]); len++)
	    continue;
	if (len == 2 && ptr[0] == 'i' && ptr[1] == 'n') {
	    ptr += 2;
	    break;
	}
	if (len == 1)
	    new_for->short_var = 1;
	strlist_add_str(&new_for->vars, make_str(ptr, len), len);
    }

    if (strlist_num(&new_for->vars) == 0) {
	Parse_Error(PARSE_FATAL, "no iteration variables in for");
	For_Free(new_for);
	return -1;
    }

    while (*ptr && isspace((unsigned char) *ptr))
	ptr++;

    /*
     * Make a list with the remaining words
     * The values are substituted as ${:U<value>...} so we must \ escape
     * characters that break that syntax.
     * Variables are fully expanded - so it is safe for escape $.
     * We can't do the escapes here - because we don't know whether
     * we are substuting into ${...} or $(...).
     */
    sub = Var_Subst(NULL, ptr, VAR_GLOBAL, VARF_WANTRES);

    /*
     * Split into words allowing for quoted strings.
     */
    words = brk_string(sub, &nwords, FALSE, &word_buf);

    free(sub);
    
    if (words != NULL) {
	for (n = 0; n < nwords; n++) {
	    ptr = words[n];
	    if (!*ptr)
		continue;
	    escapes = 0;
	    while ((ch = *ptr++)) {
		switch(ch) {
		case ':':
		case '$':
		case '\\':
		    escapes |= FOR_SUB_ESCAPE_CHAR;
		    break;
		case ')':
		    escapes |= FOR_SUB_ESCAPE_PAREN;
		    break;
		case /*{*/ '}':
		    escapes |= FOR_SUB_ESCAPE_BRACE;
		    break;
		}
	    }
	    /*
	     * We have to dup words[n] to maintain the semantics of
	     * strlist.
	     */
	    strlist_add_str(&new_for->items, bmake_strdup(words[n]), escapes);
	}

	free(words);
	free(word_buf);

	if ((len = strlist_num(&new_for->items)) > 0 &&
	    len % (n = strlist_num(&new_for->vars))) {
	    Parse_Error(PARSE_FATAL,
			"Wrong number of words (%d) in .for substitution list"
			" with %d vars", len, n);
	    /*
	     * Return 'success' so that the body of the .for loop is
	     * accumulated.
	     * Remove all items so that the loop doesn't iterate.
	     */
	    strlist_clean(&new_for->items);
	}
    }

    Buf_Init(&new_for->buf, 0);
    accumFor = new_for;
    forLevel = 1;
    return 1;
}

/*
 * Add another line to a .for loop.
 * Returns 0 when the matching .endfor is reached.
 */

int
For_Accum(char *line)
{
    char *ptr = line;

    if (*ptr == '.') {

	for (ptr++; *ptr && isspace((unsigned char) *ptr); ptr++)
	    continue;

	if (strncmp(ptr, "endfor", 6) == 0 &&
		(isspace((unsigned char) ptr[6]) || !ptr[6])) {
	    if (DEBUG(FOR))
		(void)fprintf(debug_file, "For: end for %d\n", forLevel);
	    if (--forLevel <= 0)
		return 0;
	} else if (strncmp(ptr, "for", 3) == 0 &&
		 isspace((unsigned char) ptr[3])) {
	    forLevel++;
	    if (DEBUG(FOR))
		(void)fprintf(debug_file, "For: new loop %d\n", forLevel);
	}
    }

    Buf_AddBytes(&accumFor->buf, strlen(line), line);
    Buf_AddByte(&accumFor->buf, '\n');
    return 1;
}


/*-
 *-----------------------------------------------------------------------
 * For_Run --
 *	Run the for loop, imitating the actions of an include file
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */

static int
for_var_len(const char *var)
{
    char ch, var_start, var_end;
    int depth;
    int len;

    var_start = *var;
    if (var_start == 0)
	/* just escape the $ */
	return 0;

    if (var_start == '(')
	var_end = ')';
    else if (var_start == '{')
	var_end = '}';
    else
	/* Single char variable */
	return 1;

    depth = 1;
    for (len = 1; (ch = var[len++]) != 0;) {
	if (ch == var_start)
	    depth++;
	else if (ch == var_end && --depth == 0)
	    return len;
    }

    /* Variable end not found, escape the $ */
    return 0;
}

static void
for_substitute(Buffer *cmds, strlist_t *items, unsigned int item_no, char ech)
{
    const char *item = strlist_str(items, item_no);
    int len;
    char ch;

    /* If there were no escapes, or the only escape is the other variable
     * terminator, then just substitute the full string */
    if (!(strlist_info(items, item_no) &
	    (ech == ')' ? ~FOR_SUB_ESCAPE_BRACE : ~FOR_SUB_ESCAPE_PAREN))) {
	Buf_AddBytes(cmds, strlen(item), item);
	return;
    }

    /* Escape ':', '$', '\\' and 'ech' - removed by :U processing */
    while ((ch = *item++) != 0) {
	if (ch == '$') {
	    len = for_var_len(item);
	    if (len != 0) {
		Buf_AddBytes(cmds, len + 1, item - 1);
		item += len;
		continue;
	    }
	    Buf_AddByte(cmds, '\\');
	} else if (ch == ':' || ch == '\\' || ch == ech)
	    Buf_AddByte(cmds, '\\');
	Buf_AddByte(cmds, ch);
    }
}

static char *
For_Iterate(void *v_arg, size_t *ret_len)
{
    For *arg = v_arg;
    int i, len;
    char *var;
    char *cp;
    char *cmd_cp;
    char *body_end;
    char ch;
    Buffer cmds;

    if (arg->sub_next + strlist_num(&arg->vars) > strlist_num(&arg->items)) {
	/* No more iterations */
	For_Free(arg);
	return NULL;
    }

    free(arg->parse_buf);
    arg->parse_buf = NULL;

    /*
     * Scan the for loop body and replace references to the loop variables
     * with variable references that expand to the required text.
     * Using variable expansions ensures that the .for loop can't generate
     * syntax, and that the later parsing will still see a variable.
     * We assume that the null variable will never be defined.
     *
     * The detection of substitions of the loop control variable is naive.
     * Many of the modifiers use \ to escape $ (not $) so it is possible
     * to contrive a makefile where an unwanted substitution happens.
     */

    cmd_cp = Buf_GetAll(&arg->buf, &len);
    body_end = cmd_cp + len;
    Buf_Init(&cmds, len + 256);
    for (cp = cmd_cp; (cp = strchr(cp, '$')) != NULL;) {
	char ech;
	ch = *++cp;
	if ((ch == '(' && (ech = ')', 1)) || (ch == '{' && (ech = '}', 1))) {
	    cp++;
	    /* Check variable name against the .for loop variables */
	    STRLIST_FOREACH(var, &arg->vars, i) {
		len = strlist_info(&arg->vars, i);
		if (memcmp(cp, var, len) != 0)
		    continue;
		if (cp[len] != ':' && cp[len] != ech && cp[len] != '\\')
		    continue;
		/* Found a variable match. Replace with :U<value> */
		Buf_AddBytes(&cmds, cp - cmd_cp, cmd_cp);
		Buf_AddBytes(&cmds, 2, ":U");
		cp += len;
		cmd_cp = cp;
		for_substitute(&cmds, &arg->items, arg->sub_next + i, ech);
		break;
	    }
	    continue;
	}
	if (ch == 0)
	    break;
	/* Probably a single character name, ignore $$ and stupid ones. {*/
	if (!arg->short_var || strchr("}):$", ch) != NULL) {
	    cp++;
	    continue;
	}
	STRLIST_FOREACH(var, &arg->vars, i) {
	    if (var[0] != ch || var[1] != 0)
		continue;
	    /* Found a variable match. Replace with ${:U<value>} */
	    Buf_AddBytes(&cmds, cp - cmd_cp, cmd_cp);
	    Buf_AddBytes(&cmds, 3, "{:U");
	    cmd_cp = ++cp;
	    for_substitute(&cmds, &arg->items, arg->sub_next + i, /*{*/ '}');
	    Buf_AddBytes(&cmds, 1, "}");
	    break;
	}
    }
    Buf_AddBytes(&cmds, body_end - cmd_cp, cmd_cp);

    cp = Buf_Destroy(&cmds, FALSE);
    if (DEBUG(FOR))
	(void)fprintf(debug_file, "For: loop body:\n%s", cp);

    arg->sub_next += strlist_num(&arg->vars);

    arg->parse_buf = cp;
    *ret_len = strlen(cp);
    return cp;
}

void
For_Run(int lineno)
{ 
    For *arg;
  
    arg = accumFor;
    accumFor = NULL;

    if (strlist_num(&arg->items) == 0) {
        /* Nothing to expand - possibly due to an earlier syntax error. */
        For_Free(arg);
        return;
    }
 
    Parse_SetInput(NULL, lineno, -1, For_Iterate, arg);
}
