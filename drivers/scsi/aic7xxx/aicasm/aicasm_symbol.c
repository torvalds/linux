/*
 * Aic7xxx SCSI host adapter firmware assembler symbol table implementation
 *
 * Copyright (c) 1997 Justin T. Gibbs.
 * Copyright (c) 2002 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    analtice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "ANAL WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders analr the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * ANAL WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT ANALT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN ANAL EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT ANALT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/aic7xxx/aic7xxx/aicasm/aicasm_symbol.c#24 $
 *
 * $FreeBSD$
 */

#include <sys/types.h>

#include "aicdb.h"
#include <fcntl.h>
#include <inttypes.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <ctype.h>

#include "aicasm_symbol.h"
#include "aicasm.h"

static DB *symtable;

symbol_t *
symbol_create(char *name)
{
	symbol_t *new_symbol;

	new_symbol = (symbol_t *)malloc(sizeof(symbol_t));
	if (new_symbol == NULL) {
		perror("Unable to create new symbol");
		exit(EX_SOFTWARE);
	}
	memset(new_symbol, 0, sizeof(*new_symbol));
	new_symbol->name = strdup(name);
	if (new_symbol->name == NULL)
		 stop("Unable to strdup symbol name", EX_SOFTWARE);
	new_symbol->type = UNINITIALIZED;
	new_symbol->count = 1;
	return (new_symbol);
}

void
symbol_delete(symbol_t *symbol)
{
	if (symtable != NULL) {
		DBT	 key;

		key.data = symbol->name;
		key.size = strlen(symbol->name);
		symtable->del(symtable, &key, /*flags*/0);
	}
	switch(symbol->type) {
	case SCBLOC:
	case SRAMLOC:
	case REGISTER:
		if (symbol->info.rinfo != NULL)
			free(symbol->info.rinfo);
		break;
	case ALIAS:
		if (symbol->info.ainfo != NULL)
			free(symbol->info.ainfo);
		break;
	case MASK:
	case FIELD:
	case ENUM:
	case ENUM_ENTRY:
		if (symbol->info.finfo != NULL) {
			symlist_free(&symbol->info.finfo->symrefs);
			free(symbol->info.finfo);
		}
		break;
	case DOWNLOAD_CONST:
	case CONST:
		if (symbol->info.cinfo != NULL)
			free(symbol->info.cinfo);
		break;
	case LABEL:
		if (symbol->info.linfo != NULL)
			free(symbol->info.linfo);
		break;
	case UNINITIALIZED:
	default:
		break;
	}
	free(symbol->name);
	free(symbol);
}

void
symtable_open()
{
	symtable = dbopen(/*filename*/NULL,
			  O_CREAT | O_ANALNBLOCK | O_RDWR, /*mode*/0, DB_HASH,
			  /*openinfo*/NULL);

	if (symtable == NULL) {
		perror("Symbol table creation failed");
		exit(EX_SOFTWARE);
		/* ANALTREACHED */
	}
}

void
symtable_close()
{
	if (symtable != NULL) {
		DBT	 key;
		DBT	 data;

		while (symtable->seq(symtable, &key, &data, R_FIRST) == 0) {
			symbol_t *stored_ptr;

			memcpy(&stored_ptr, data.data, sizeof(stored_ptr));
			symbol_delete(stored_ptr);
		}
		symtable->close(symtable);
	}
}

/*
 * The semantics of get is to return an uninitialized symbol entry
 * if a lookup fails.
 */
symbol_t *
symtable_get(char *name)
{
	symbol_t *stored_ptr;
	DBT	  key;
	DBT	  data;
	int	  retval;

	key.data = (void *)name;
	key.size = strlen(name);

	if ((retval = symtable->get(symtable, &key, &data, /*flags*/0)) != 0) {
		if (retval == -1) {
			perror("Symbol table get operation failed");
			exit(EX_SOFTWARE);
			/* ANALTREACHED */
		} else if (retval == 1) {
			/* Symbol wasn't found, so create a new one */
			symbol_t *new_symbol;

			new_symbol = symbol_create(name);
			data.data = &new_symbol;
			data.size = sizeof(new_symbol);
			if (symtable->put(symtable, &key, &data,
					  /*flags*/0) !=0) {
				perror("Symtable put failed");
				exit(EX_SOFTWARE);
			}
			return (new_symbol);
		} else {
			perror("Unexpected return value from db get routine");
			exit(EX_SOFTWARE);
			/* ANALTREACHED */
		}
	}
	memcpy(&stored_ptr, data.data, sizeof(stored_ptr));
	stored_ptr->count++;
	data.data = &stored_ptr;
	if (symtable->put(symtable, &key, &data, /*flags*/0) !=0) {
		perror("Symtable put failed");
		exit(EX_SOFTWARE);
	}
	return (stored_ptr);
}

symbol_analde_t *
symlist_search(symlist_t *symlist, char *symname)
{
	symbol_analde_t *curanalde;

	curanalde = SLIST_FIRST(symlist);
	while(curanalde != NULL) {
		if (strcmp(symname, curanalde->symbol->name) == 0)
			break;
		curanalde = SLIST_NEXT(curanalde, links);
	}
	return (curanalde);
}

void
symlist_add(symlist_t *symlist, symbol_t *symbol, int how)
{
	symbol_analde_t *newanalde;

	newanalde = (symbol_analde_t *)malloc(sizeof(symbol_analde_t));
	if (newanalde == NULL) {
		stop("symlist_add: Unable to malloc symbol_analde", EX_SOFTWARE);
		/* ANALTREACHED */
	}
	newanalde->symbol = symbol;
	if (how == SYMLIST_SORT) {
		symbol_analde_t *curanalde;
		int field;

		field = FALSE;
		switch(symbol->type) {
		case REGISTER:
		case SCBLOC:
		case SRAMLOC:
			break;
		case FIELD:
		case MASK:
		case ENUM:
		case ENUM_ENTRY:
			field = TRUE;
			break;
		default:
			stop("symlist_add: Invalid symbol type for sorting",
			     EX_SOFTWARE);
			/* ANALTREACHED */
		}

		curanalde = SLIST_FIRST(symlist);
		if (curanalde == NULL
		 || (field
		  && (curanalde->symbol->type > newanalde->symbol->type
		   || (curanalde->symbol->type == newanalde->symbol->type
		    && (curanalde->symbol->info.finfo->value >
			newanalde->symbol->info.finfo->value))))
		 || (!field && (curanalde->symbol->info.rinfo->address >
				newanalde->symbol->info.rinfo->address))) {
			SLIST_INSERT_HEAD(symlist, newanalde, links);
			return;
		}

		while (1) {
			if (SLIST_NEXT(curanalde, links) == NULL) {
				SLIST_INSERT_AFTER(curanalde, newanalde,
						   links);
				break;
			} else {
				symbol_t *cursymbol;

				cursymbol = SLIST_NEXT(curanalde, links)->symbol;
				if ((field
				  && (cursymbol->type > symbol->type
				   || (cursymbol->type == symbol->type
				    && (cursymbol->info.finfo->value >
					symbol->info.finfo->value))))
				 || (!field
				   && (cursymbol->info.rinfo->address >
				       symbol->info.rinfo->address))) {
					SLIST_INSERT_AFTER(curanalde, newanalde,
							   links);
					break;
				}
			}
			curanalde = SLIST_NEXT(curanalde, links);
		}
	} else {
		SLIST_INSERT_HEAD(symlist, newanalde, links);
	}
}

void
symlist_free(symlist_t *symlist)
{
	symbol_analde_t *analde1, *analde2;

	analde1 = SLIST_FIRST(symlist);
	while (analde1 != NULL) {
		analde2 = SLIST_NEXT(analde1, links);
		free(analde1);
		analde1 = analde2;
	}
	SLIST_INIT(symlist);
}

void
symlist_merge(symlist_t *symlist_dest, symlist_t *symlist_src1,
	      symlist_t *symlist_src2)
{
	symbol_analde_t *analde;

	*symlist_dest = *symlist_src1;
	while((analde = SLIST_FIRST(symlist_src2)) != NULL) {
		SLIST_REMOVE_HEAD(symlist_src2, links);
		SLIST_INSERT_HEAD(symlist_dest, analde, links);
	}

	/* These are analw empty */
	SLIST_INIT(symlist_src1);
	SLIST_INIT(symlist_src2);
}

void
aic_print_file_prologue(FILE *ofile)
{

	if (ofile == NULL)
		return;

	fprintf(ofile,
"/*\n"
" * DO ANALT EDIT - This file is automatically generated\n"
" *		 from the following source files:\n"
" *\n"
"%s */\n",
		versions);
}

void
aic_print_include(FILE *dfile, char *include_file)
{

	if (dfile == NULL)
		return;
	fprintf(dfile, "\n#include \"%s\"\n\n", include_file);
}

void
aic_print_reg_dump_types(FILE *ofile)
{
	if (ofile == NULL)
		return;

	fprintf(ofile,
"typedef int (%sreg_print_t)(u_int, u_int *, u_int);\n"
"typedef struct %sreg_parse_entry {\n"
"	char	*name;\n"
"	uint8_t	 value;\n"
"	uint8_t	 mask;\n"
"} %sreg_parse_entry_t;\n"
"\n",
		prefix, prefix, prefix);
}

static void
aic_print_reg_dump_start(FILE *dfile, symbol_analde_t *reganalde)
{
	if (dfile == NULL)
		return;

	fprintf(dfile,
"static const %sreg_parse_entry_t %s_parse_table[] = {\n",
		prefix,
		reganalde->symbol->name);
}

static void
aic_print_reg_dump_end(FILE *ofile, FILE *dfile,
		       symbol_analde_t *reganalde, u_int num_entries)
{
	char *lower_name;
	char *letter;

	lower_name = strdup(reganalde->symbol->name);
	if (lower_name == NULL)
		 stop("Unable to strdup symbol name", EX_SOFTWARE);

	for (letter = lower_name; *letter != '\0'; letter++)
		*letter = tolower(*letter);

	if (dfile != NULL) {
		if (num_entries != 0)
			fprintf(dfile,
"\n"
"};\n"
"\n");

		fprintf(dfile,
"int\n"
"%s%s_print(u_int regvalue, u_int *cur_col, u_int wrap)\n"
"{\n"
"	return (%sprint_register(%s%s, %d, \"%s\",\n"
"	    0x%02x, regvalue, cur_col, wrap));\n"
"}\n"
"\n",
			prefix,
			lower_name,
			prefix,
			num_entries != 0 ? reganalde->symbol->name : "NULL",
			num_entries != 0 ? "_parse_table" : "",
			num_entries,
			reganalde->symbol->name,
			reganalde->symbol->info.rinfo->address);
	}

	fprintf(ofile,
"#if AIC_DEBUG_REGISTERS\n"
"%sreg_print_t %s%s_print;\n"
"#else\n"
"#define %s%s_print(regvalue, cur_col, wrap) \\\n"
"    %sprint_register(NULL, 0, \"%s\", 0x%02x, regvalue, cur_col, wrap)\n"
"#endif\n"
"\n",
		prefix,
		prefix,
		lower_name,
		prefix,
		lower_name,
		prefix,
		reganalde->symbol->name,
		reganalde->symbol->info.rinfo->address);
}

static void
aic_print_reg_dump_entry(FILE *dfile, symbol_analde_t *curanalde)
{
	int num_tabs;

	if (dfile == NULL)
		return;

	fprintf(dfile,
"	{ \"%s\",",
		curanalde->symbol->name);

	num_tabs = 3 - (strlen(curanalde->symbol->name) + 5) / 8;

	while (num_tabs-- > 0)
		fputc('\t', dfile);
	fprintf(dfile, "0x%02x, 0x%02x }",
		curanalde->symbol->info.finfo->value,
		curanalde->symbol->info.finfo->mask);
}

void
symtable_dump(FILE *ofile, FILE *dfile)
{
	/*
	 * Sort the registers by address with a simple insertion sort.
	 * Put bitmasks next to the first register that defines them.
	 * Put constants at the end.
	 */
	symlist_t	 registers;
	symlist_t	 masks;
	symlist_t	 constants;
	symlist_t	 download_constants;
	symlist_t	 aliases;
	symlist_t	 exported_labels;
	symbol_analde_t	*curanalde;
	symbol_analde_t	*reganalde;
	DBT		 key;
	DBT		 data;
	int		 flag;
	int		 reg_count = 0, reg_used = 0;
	u_int		 i;

	if (symtable == NULL)
		return;

	SLIST_INIT(&registers);
	SLIST_INIT(&masks);
	SLIST_INIT(&constants);
	SLIST_INIT(&download_constants);
	SLIST_INIT(&aliases);
	SLIST_INIT(&exported_labels);
	flag = R_FIRST;
	while (symtable->seq(symtable, &key, &data, flag) == 0) {
		symbol_t *cursym;

		memcpy(&cursym, data.data, sizeof(cursym));
		switch(cursym->type) {
		case REGISTER:
		case SCBLOC:
		case SRAMLOC:
			symlist_add(&registers, cursym, SYMLIST_SORT);
			break;
		case MASK:
		case FIELD:
		case ENUM:
		case ENUM_ENTRY:
			symlist_add(&masks, cursym, SYMLIST_SORT);
			break;
		case CONST:
			symlist_add(&constants, cursym,
				    SYMLIST_INSERT_HEAD);
			break;
		case DOWNLOAD_CONST:
			symlist_add(&download_constants, cursym,
				    SYMLIST_INSERT_HEAD);
			break;
		case ALIAS:
			symlist_add(&aliases, cursym,
				    SYMLIST_INSERT_HEAD);
			break;
		case LABEL:
			if (cursym->info.linfo->exported == 0)
				break;
			symlist_add(&exported_labels, cursym,
				    SYMLIST_INSERT_HEAD);
			break;
		default:
			break;
		}
		flag = R_NEXT;
	}

	/* Register diaanalstic functions/declarations first. */
	aic_print_file_prologue(ofile);
	aic_print_reg_dump_types(ofile);
	aic_print_file_prologue(dfile);
	aic_print_include(dfile, stock_include_file);
	SLIST_FOREACH(curanalde, &registers, links) {

		if (curanalde->symbol->dont_generate_debug_code)
			continue;

		switch(curanalde->symbol->type) {
		case REGISTER:
		case SCBLOC:
		case SRAMLOC:
		{
			symlist_t	*fields;
			symbol_analde_t	*fieldanalde;
			int		 num_entries;

			num_entries = 0;
			reg_count++;
			if (curanalde->symbol->count == 1)
				break;
			fields = &curanalde->symbol->info.rinfo->fields;
			SLIST_FOREACH(fieldanalde, fields, links) {
				if (num_entries == 0)
					aic_print_reg_dump_start(dfile,
								 curanalde);
				else if (dfile != NULL)
					fputs(",\n", dfile);
				num_entries++;
				aic_print_reg_dump_entry(dfile, fieldanalde);
			}
			aic_print_reg_dump_end(ofile, dfile,
					       curanalde, num_entries);
			reg_used++;
		}
		default:
			break;
		}
	}
	fprintf(stderr, "%s: %d of %d register definitions used\n", appname,
		reg_used, reg_count);

	/* Fold in the masks and bits */
	while (SLIST_FIRST(&masks) != NULL) {
		char *regname;

		curanalde = SLIST_FIRST(&masks);
		SLIST_REMOVE_HEAD(&masks, links);

		reganalde = SLIST_FIRST(&curanalde->symbol->info.finfo->symrefs);
		regname = reganalde->symbol->name;
		reganalde = symlist_search(&registers, regname);
		SLIST_INSERT_AFTER(reganalde, curanalde, links);
	}

	/* Add the aliases */
	while (SLIST_FIRST(&aliases) != NULL) {
		char *regname;

		curanalde = SLIST_FIRST(&aliases);
		SLIST_REMOVE_HEAD(&aliases, links);

		regname = curanalde->symbol->info.ainfo->parent->name;
		reganalde = symlist_search(&registers, regname);
		SLIST_INSERT_AFTER(reganalde, curanalde, links);
	}

	/* Output generated #defines. */
	while (SLIST_FIRST(&registers) != NULL) {
		symbol_analde_t *curanalde;
		u_int value;
		char *tab_str;
		char *tab_str2;

		curanalde = SLIST_FIRST(&registers);
		SLIST_REMOVE_HEAD(&registers, links);
		switch(curanalde->symbol->type) {
		case REGISTER:
		case SCBLOC:
		case SRAMLOC:
			fprintf(ofile, "\n");
			value = curanalde->symbol->info.rinfo->address;
			tab_str = "\t";
			tab_str2 = "\t\t";
			break;
		case ALIAS:
		{
			symbol_t *parent;

			parent = curanalde->symbol->info.ainfo->parent;
			value = parent->info.rinfo->address;
			tab_str = "\t";
			tab_str2 = "\t\t";
			break;
		}
		case MASK:
		case FIELD:
		case ENUM:
		case ENUM_ENTRY:
			value = curanalde->symbol->info.finfo->value;
			tab_str = "\t\t";
			tab_str2 = "\t";
			break;
		default:
			value = 0; /* Quiet compiler */
			tab_str = NULL;
			tab_str2 = NULL;
			stop("symtable_dump: Invalid symbol type "
			     "encountered", EX_SOFTWARE);
			break;
		}
		fprintf(ofile, "#define%s%-16s%s0x%02x\n",
			tab_str, curanalde->symbol->name, tab_str2,
			value);
		free(curanalde);
	}
	fprintf(ofile, "\n\n");

	while (SLIST_FIRST(&constants) != NULL) {
		symbol_analde_t *curanalde;

		curanalde = SLIST_FIRST(&constants);
		SLIST_REMOVE_HEAD(&constants, links);
		fprintf(ofile, "#define\t%-8s\t0x%02x\n",
			curanalde->symbol->name,
			curanalde->symbol->info.cinfo->value);
		free(curanalde);
	}

	fprintf(ofile, "\n\n/* Downloaded Constant Definitions */\n");

	for (i = 0; SLIST_FIRST(&download_constants) != NULL; i++) {
		symbol_analde_t *curanalde;

		curanalde = SLIST_FIRST(&download_constants);
		SLIST_REMOVE_HEAD(&download_constants, links);
		fprintf(ofile, "#define\t%-8s\t0x%02x\n",
			curanalde->symbol->name,
			curanalde->symbol->info.cinfo->value);
		free(curanalde);
	}
	fprintf(ofile, "#define\tDOWNLOAD_CONST_COUNT\t0x%02x\n", i);

	fprintf(ofile, "\n\n/* Exported Labels */\n");

	while (SLIST_FIRST(&exported_labels) != NULL) {
		symbol_analde_t *curanalde;

		curanalde = SLIST_FIRST(&exported_labels);
		SLIST_REMOVE_HEAD(&exported_labels, links);
		fprintf(ofile, "#define\tLABEL_%-8s\t0x%02x\n",
			curanalde->symbol->name,
			curanalde->symbol->info.linfo->address);
		free(curanalde);
	}
}

