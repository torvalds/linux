/*
   Simple utility to prepare vmlinux image for sparc.
   Resolves all BTFIXUP uses and settings and creates
   a special .s object to link to the image.
   
   Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */
   
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>

#define MAXSYMS 1024

static char *symtab = "SYMBOL TABLE:";
static char *relrec = "RELOCATION RECORDS FOR [";
static int rellen;
static int symlen;
int mode;

struct _btfixup;

typedef struct _btfixuprel {
	char *sect;
	unsigned long offset;
	struct _btfixup *f;
	int frel;
	struct _btfixuprel *next;
} btfixuprel;

typedef struct _btfixup {
	int type;
	int setinitval;
	unsigned int initval;
	char *initvalstr;
	char *name;
	btfixuprel *rel;
} btfixup;

btfixup array[MAXSYMS];
int last = 0;
char buffer[1024];
unsigned long lastfoffset = -1;
unsigned long lastfrelno;
btfixup *lastf;

static void fatal(void) __attribute__((noreturn));
static void fatal(void)
{
	fprintf(stderr, "Malformed output from objdump\n%s\n", buffer);
	exit(1);
}

static btfixup *find(int type, char *name)
{
	int i;
	for (i = 0; i < last; i++) {
		if (array[i].type == type && !strcmp(array[i].name, name))
			return array + i;
	}
	array[last].type = type;
	array[last].name = strdup(name);
	array[last].setinitval = 0;
	if (!array[last].name) fatal();
	array[last].rel = NULL;
	last++;
	if (last >= MAXSYMS) {
		fprintf(stderr, "Ugh. Something strange. More than %d different BTFIXUP symbols\n", MAXSYMS);
		exit(1);
	}
	return array + last - 1;
}

static void set_mode (char *buffer)
{
  	for (mode = 0;; mode++)
		if (buffer[mode] < '0' || buffer[mode] > '9')
			break;
	if (mode != 8 && mode != 16)
		fatal();
}


int main(int argc,char **argv)
{
	char *p, *q;
	char *sect;
	int i, j, k;
	unsigned int initval;
	int shift;
	btfixup *f;
	btfixuprel *r, **rr;
	unsigned long offset;
	char *initvalstr;

	symlen = strlen(symtab);
	while (fgets (buffer, 1024, stdin) != NULL)
		if (!strncmp (buffer, symtab, symlen))
			goto main0;
	fatal();
main0:
	rellen = strlen(relrec);
	while (fgets (buffer, 1024, stdin) != NULL)
		if (!strncmp (buffer, relrec, rellen))
			goto main1;
	fatal();
main1:
	sect = malloc(strlen (buffer + rellen) + 1);
	if (!sect) fatal();
	strcpy (sect, buffer + rellen);
	p = strchr (sect, ']');
	if (!p) fatal();
	*p = 0;
	if (fgets (buffer, 1024, stdin) == NULL)
		fatal();
	while (fgets (buffer, 1024, stdin) != NULL) {
		int nbase;
		if (!strncmp (buffer, relrec, rellen))
			goto main1;
		if (mode == 0)
			set_mode (buffer);
		p = strchr (buffer, '\n');
		if (p) *p = 0;
		if (strlen (buffer) < 22+mode)
			continue;
		if (strncmp (buffer + mode, " R_SPARC_", 9))
			continue;
		nbase = 27 - 8 + mode;
		if (buffer[nbase] != '_' || buffer[nbase+1] != '_' || buffer[nbase+2] != '_')
			continue;
		switch (buffer[nbase+3]) {
			case 'f':	/* CALL */
			case 'b':	/* BLACKBOX */
			case 's':	/* SIMM13 */
			case 'a':	/* HALF */
			case 'h':	/* SETHI */
			case 'i':	/* INT */
				break;
			default:
				continue;
		}
		p = strchr (buffer + nbase+5, '+');
		if (p) *p = 0;
		shift = nbase + 5;
		if (buffer[nbase+4] == 's' && buffer[nbase+5] == '_') {
			shift = nbase + 6;
			if (strcmp (sect, ".init.text")) {
				fprintf(stderr,
				    "Wrong use of '%s' BTFIXUPSET in '%s' section.\n"
				    "BTFIXUPSET_CALL can be used only in"
				    " __init sections\n",
				    buffer + shift, sect);
				exit(1);
			}
		} else if (buffer[nbase+4] != '_')
			continue;
		if (!strcmp (sect, ".text.exit"))
			continue;
		if (strcmp (sect, ".text") &&
		    strcmp (sect, ".init.text") &&
		    strcmp (sect, ".fixup") &&
		    (strcmp (sect, "__ksymtab") || buffer[nbase+3] != 'f')) {
			if (buffer[nbase+3] == 'f')
				fprintf(stderr,
				    "Wrong use of '%s' in '%s' section.\n"
				    " It can be used only in .text, .init.text,"
				    " .fixup and __ksymtab\n",
				    buffer + shift, sect);
			else
				fprintf(stderr,
				    "Wrong use of '%s' in '%s' section.\n"
				    " It can be only used in .text, .init.text,"
				    " and .fixup\n", buffer + shift, sect);
			exit(1);
		}
		p = strstr (buffer + shift, "__btset_");
		if (p && buffer[nbase+4] == 's') {
			fprintf(stderr, "__btset_ in BTFIXUP name can only be used when defining the variable, not for setting\n%s\n", buffer);
			exit(1);
		}
		initval = 0;
		initvalstr = NULL;
		if (p) {
			if (p[8] != '0' || p[9] != 'x') {
				fprintf(stderr, "Pre-initialized values can be only initialized with hexadecimal constants starting 0x\n%s\n", buffer);
				exit(1);
			}
			initval = strtoul(p + 10, &q, 16);
			if (*q || !initval) {
				fprintf(stderr, "Pre-initialized values can be only in the form name__btset_0xXXXXXXXX where X are hex digits.\nThey cannot be name__btset_0x00000000 though. Use BTFIXUPDEF_XX instead of BTFIXUPDEF_XX_INIT then.\n%s\n", buffer);
				exit(1);
			}
			initvalstr = p + 10;
			*p = 0;
		}
		f = find(buffer[nbase+3], buffer + shift);
		if (buffer[nbase+4] == 's')
			continue;
		switch (buffer[nbase+3]) {
		case 'f':
			if (initval) {
				fprintf(stderr, "Cannot use pre-initialized fixups for calls\n%s\n", buffer);
				exit(1);
			}
			if (!strcmp (sect, "__ksymtab")) {
				if (strncmp (buffer + mode+9, "32        ", 10)) {
					fprintf(stderr, "BTFIXUP_CALL in EXPORT_SYMBOL results in relocation other than R_SPARC_32\n\%s\n", buffer);
					exit(1);
				}
			} else if (strncmp (buffer + mode+9, "WDISP30   ", 10) &&
				   strncmp (buffer + mode+9, "HI22      ", 10) &&
				   strncmp (buffer + mode+9, "LO10      ", 10)) {
				fprintf(stderr, "BTFIXUP_CALL results in relocation other than R_SPARC_WDISP30, R_SPARC_HI22 or R_SPARC_LO10\n%s\n", buffer);
				exit(1);
			}
			break;
		case 'b':
			if (initval) {
				fprintf(stderr, "Cannot use pre-initialized fixups for blackboxes\n%s\n", buffer);
				exit(1);
			}
			if (strncmp (buffer + mode+9, "HI22      ", 10)) {
				fprintf(stderr, "BTFIXUP_BLACKBOX results in relocation other than R_SPARC_HI22\n%s\n", buffer);
				exit(1);
			}
			break;
		case 's':
			if (initval + 0x1000 >= 0x2000) {
				fprintf(stderr, "Wrong initializer for SIMM13. Has to be from $fffff000 to $00000fff\n%s\n", buffer);
				exit(1);
			}
			if (strncmp (buffer + mode+9, "13        ", 10)) {
				fprintf(stderr, "BTFIXUP_SIMM13 results in relocation other than R_SPARC_13\n%s\n", buffer);
				exit(1);
			}
			break;
		case 'a':
			if (initval + 0x1000 >= 0x2000 && (initval & 0x3ff)) {
				fprintf(stderr, "Wrong initializer for HALF.\n%s\n", buffer);
				exit(1);
			}
			if (strncmp (buffer + mode+9, "13        ", 10)) {
				fprintf(stderr, "BTFIXUP_HALF results in relocation other than R_SPARC_13\n%s\n", buffer);
				exit(1);
			}
			break;
		case 'h':
			if (initval & 0x3ff) {
				fprintf(stderr, "Wrong initializer for SETHI. Cannot have set low 10 bits\n%s\n", buffer);
				exit(1);
			}
			if (strncmp (buffer + mode+9, "HI22      ", 10)) {
				fprintf(stderr, "BTFIXUP_SETHI results in relocation other than R_SPARC_HI22\n%s\n", buffer);
				exit(1);
			}
			break;
		case 'i':
			if (initval) {
				fprintf(stderr, "Cannot use pre-initialized fixups for INT\n%s\n", buffer);
				exit(1);
			}
			if (strncmp (buffer + mode+9, "HI22      ", 10) && strncmp (buffer + mode+9, "LO10      ", 10)) {
				fprintf(stderr, "BTFIXUP_INT results in relocation other than R_SPARC_HI22 and R_SPARC_LO10\n%s\n", buffer);
				exit(1);
			}
			break;
		}
		if (!f->setinitval) {
			f->initval = initval;
			if (initvalstr) {
				f->initvalstr = strdup(initvalstr);
				if (!f->initvalstr) fatal();
			}
			f->setinitval = 1;
		} else if (f->initval != initval) {
			fprintf(stderr, "Btfixup %s previously used with initializer %s which doesn't match with current initializer\n%s\n",
					f->name, f->initvalstr ? : "0x00000000", buffer);
			exit(1);
		} else if (initval && strcmp(f->initvalstr, initvalstr)) {
			fprintf(stderr, "Btfixup %s previously used with initializer %s which doesn't match with current initializer.\n"
					"Initializers have to match literally as well.\n%s\n",
					f->name, f->initvalstr, buffer);
			exit(1);
		}
		offset = strtoul(buffer, &q, 16);
		if (q != buffer + mode || (!offset && (mode == 8 ? strncmp (buffer, "00000000 ", 9) : strncmp (buffer, "0000000000000000 ", 17)))) {
			fprintf(stderr, "Malformed relocation address in\n%s\n", buffer);
			exit(1);
		}
		for (k = 0, r = f->rel, rr = &f->rel; r; rr = &r->next, r = r->next, k++)
			if (r->offset == offset && !strcmp(r->sect, sect)) {
				fprintf(stderr, "Ugh. One address has two relocation records\n");
				exit(1);
			}
		*rr = malloc(sizeof(btfixuprel));
		if (!*rr) fatal();
		(*rr)->offset = offset;
		(*rr)->f = NULL;
		if (buffer[nbase+3] == 'f') {
			lastf = f;
			lastfoffset = offset;
			lastfrelno = k;
		} else if (lastfoffset + 4 == offset) {
			(*rr)->f = lastf;
			(*rr)->frel = lastfrelno;
		}
		(*rr)->sect = sect;
		(*rr)->next = NULL;
	}
	printf("! Generated by btfixupprep. Do not edit.\n\n");
	printf("\t.section\t\".data..init\",#alloc,#write\n\t.align\t4\n\n");
	printf("\t.global\t___btfixup_start\n___btfixup_start:\n\n");
	for (i = 0; i < last; i++) {
		f = array + i;
		printf("\t.global\t___%cs_%s\n", f->type, f->name);
		if (f->type == 'f')
			printf("___%cs_%s:\n\t.word 0x%08x,0,0,", f->type, f->name, f->type << 24);
		else
			printf("___%cs_%s:\n\t.word 0x%08x,0,", f->type, f->name, f->type << 24);
		for (j = 0, r = f->rel; r != NULL; j++, r = r->next);
		if (j)
			printf("%d\n\t.word\t", j * 2);
		else
			printf("0\n");
		for (r = f->rel, j--; r != NULL; j--, r = r->next) {
			if (!strcmp (r->sect, ".text"))
				printf ("_stext+0x%08lx", r->offset);
			else if (!strcmp (r->sect, ".init.text"))
				printf ("__init_begin+0x%08lx", r->offset);
			else if (!strcmp (r->sect, "__ksymtab"))
				printf ("__start___ksymtab+0x%08lx", r->offset);
			else if (!strcmp (r->sect, ".fixup"))
				printf ("__start___fixup+0x%08lx", r->offset);
			else
				fatal();
			if (f->type == 'f' || !r->f)
				printf (",0");
			else
				printf (",___fs_%s+0x%08x", r->f->name, (4 + r->frel*2)*4 + 4);
			if (j) printf (",");
			else printf ("\n");
		}
		printf("\n");
	}
	printf("\n\t.global\t___btfixup_end\n___btfixup_end:\n");
	printf("\n\n! Define undefined references\n\n");
	for (i = 0; i < last; i++) {
		f = array + i;
		if (f->type == 'f') {
			printf("\t.global\t___f_%s\n", f->name);
			printf("___f_%s:\n", f->name);
		}
	}
	printf("\tretl\n\t nop\n\n");
	for (i = 0; i < last; i++) {
		f = array + i;
		if (f->type != 'f') {
			if (!f->initval) {
				printf("\t.global\t___%c_%s\n", f->type, f->name);
				printf("___%c_%s = 0\n", f->type, f->name);
			} else {
				printf("\t.global\t___%c_%s__btset_0x%s\n", f->type, f->name, f->initvalstr);
				printf("___%c_%s__btset_0x%s = 0x%08x\n", f->type, f->name, f->initvalstr, f->initval);
			}
		}
	}
	printf("\n\n");
    	exit(0);
}
