/****************************************************************
Copyright (C) Lucent Technologies 1997
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name Lucent Technologies or any of
its entities not be used in advertising or publicity pertaining
to distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
****************************************************************/

/*
 * this program makes the table to link function names
 * and type indices that is used by execute() in run.c.
 * it finds the indices in ytab.h, produced by yacc.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "awk.h"
#include "ytab.h"

struct xx
{	int token;
	const char *name;
	const char *pname;
} proc[] = {
	{ PROGRAM, "program", NULL },
	{ BOR, "boolop", " || " },
	{ AND, "boolop", " && " },
	{ NOT, "boolop", " !" },
	{ NE, "relop", " != " },
	{ EQ, "relop", " == " },
	{ LE, "relop", " <= " },
	{ LT, "relop", " < " },
	{ GE, "relop", " >= " },
	{ GT, "relop", " > " },
	{ ARRAY, "array", NULL },
	{ INDIRECT, "indirect", "$(" },
	{ SUBSTR, "substr", "substr" },
	{ SUB, "sub", "sub" },
	{ GSUB, "gsub", "gsub" },
	{ INDEX, "sindex", "sindex" },
	{ SPRINTF, "awksprintf", "sprintf " },
	{ ADD, "arith", " + " },
	{ MINUS, "arith", " - " },
	{ MULT, "arith", " * " },
	{ DIVIDE, "arith", " / " },
	{ MOD, "arith", " % " },
	{ UMINUS, "arith", " -" },
	{ POWER, "arith", " **" },
	{ PREINCR, "incrdecr", "++" },
	{ POSTINCR, "incrdecr", "++" },
	{ PREDECR, "incrdecr", "--" },
	{ POSTDECR, "incrdecr", "--" },
	{ CAT, "cat", " " },
	{ PASTAT, "pastat", NULL },
	{ PASTAT2, "dopa2", NULL },
	{ MATCH, "matchop", " ~ " },
	{ NOTMATCH, "matchop", " !~ " },
	{ MATCHFCN, "matchop", "matchop" },
	{ INTEST, "intest", "intest" },
	{ PRINTF, "awkprintf", "printf" },
	{ PRINT, "printstat", "print" },
	{ CLOSE, "closefile", "closefile" },
	{ DELETE, "awkdelete", "awkdelete" },
	{ SPLIT, "split", "split" },
	{ ASSIGN, "assign", " = " },
	{ ADDEQ, "assign", " += " },
	{ SUBEQ, "assign", " -= " },
	{ MULTEQ, "assign", " *= " },
	{ DIVEQ, "assign", " /= " },
	{ MODEQ, "assign", " %= " },
	{ POWEQ, "assign", " ^= " },
	{ CONDEXPR, "condexpr", " ?: " },
	{ IF, "ifstat", "if(" },
	{ WHILE, "whilestat", "while(" },
	{ FOR, "forstat", "for(" },
	{ DO, "dostat", "do" },
	{ IN, "instat", "instat" },
	{ NEXT, "jump", "next" },
	{ NEXTFILE, "jump", "nextfile" },
	{ EXIT, "jump", "exit" },
	{ BREAK, "jump", "break" },
	{ CONTINUE, "jump", "continue" },
	{ RETURN, "jump", "ret" },
	{ BLTIN, "bltin", "bltin" },
	{ CALL, "call", "call" },
	{ ARG, "arg", "arg" },
	{ VARNF, "getnf", "NF" },
	{ GETLINE, "awkgetline", "getline" },
	{ 0, "", "" },
};

#define SIZE	(LASTTOKEN - FIRSTTOKEN + 1)
const char *table[SIZE];
char *names[SIZE];

int main(int argc, char *argv[])
{
	const struct xx *p;
	int i, n, tok;
	char c;
	FILE *fp;
	char buf[200], name[200], def[200];

	printf("#include <stdio.h>\n");
	printf("#include \"awk.h\"\n");
	printf("#include \"ytab.h\"\n\n");
	for (i = SIZE; --i >= 0; )
		names[i] = "";

	if ((fp = fopen("ytab.h", "r")) == NULL) {
		fprintf(stderr, "maketab can't open ytab.h!\n");
		exit(1);
	}
	printf("static char *printname[%d] = {\n", SIZE);
	i = 0;
	while (fgets(buf, sizeof buf, fp) != NULL) {
		n = sscanf(buf, "%1c %s %s %d", &c, def, name, &tok);
		if (c != '#' || (n != 4 && strcmp(def,"define") != 0))	/* not a valid #define */
			continue;
		if (tok < FIRSTTOKEN || tok > LASTTOKEN) {
			/* fprintf(stderr, "maketab funny token %d %s ignored\n", tok, buf); */
			continue;
		}
		names[tok-FIRSTTOKEN] = (char *) malloc(strlen(name)+1);
		strcpy(names[tok-FIRSTTOKEN], name);
		printf("\t(char *) \"%s\",\t/* %d */\n", name, tok);
		i++;
	}
	printf("};\n\n");

	for (p=proc; p->token!=0; p++)
		table[p->token-FIRSTTOKEN] = p->name;
	printf("\nCell *(*proctab[%d])(Node **, int) = {\n", SIZE);
	for (i=0; i<SIZE; i++)
		if (table[i]==NULL)
			printf("\tnullproc,\t/* %s */\n", names[i]);
		else
			printf("\t%s,\t/* %s */\n", table[i], names[i]);
	printf("};\n\n");

	printf("char *tokname(int n)\n");	/* print a tokname() function */
	printf("{\n");
	printf("	static char buf[100];\n\n");
	printf("	if (n < FIRSTTOKEN || n > LASTTOKEN) {\n");
	printf("		sprintf(buf, \"token %%d\", n);\n");
	printf("		return buf;\n");
	printf("	}\n");
	printf("	return printname[n-FIRSTTOKEN];\n");
	printf("}\n");
	return 0;
}
