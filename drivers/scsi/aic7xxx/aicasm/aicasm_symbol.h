/*
 * Aic7xxx SCSI host adapter firmware asssembler symbol table definitions
 *
 * Copyright (c) 1997 Justin T. Gibbs.
 * Copyright (c) 2002 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/aic7xxx/aic7xxx/aicasm/aicasm_symbol.h#17 $
 *
 * $FreeBSD$
 */

#ifdef __linux__
#include "../queue.h"
#else
#include <sys/queue.h>
#endif

typedef enum {
	UNINITIALIZED,
	REGISTER,
	ALIAS,
	SCBLOC,
	SRAMLOC,
	ENUM_ENTRY,
	FIELD,
	MASK,
	ENUM,
	CONST,
	DOWNLOAD_CONST,
	LABEL,
	CONDITIONAL,
	MACRO
} symtype;

typedef enum {
	RO = 0x01,
	WO = 0x02,
	RW = 0x03
}amode_t;

typedef SLIST_HEAD(symlist, symbol_node) symlist_t;

struct reg_info {
	u_int	  address;
	int	  size;
	amode_t	  mode;
	symlist_t fields;
	uint8_t	  valid_bitmask;
	uint8_t	  modes;
	int	  typecheck_masks;
};

struct field_info {
	symlist_t symrefs;
	uint8_t	  value;
	uint8_t	  mask;
};

struct const_info {
	u_int	value;
	int	define;
};

struct alias_info {
	struct symbol *parent;
};

struct label_info {
	int	address;
	int	exported;
};

struct cond_info {
	int	func_num;
};

struct macro_arg {
	STAILQ_ENTRY(macro_arg)	links;
	regex_t	arg_regex;
	char   *replacement_text;
};
STAILQ_HEAD(macro_arg_list, macro_arg) args;

struct macro_info {
	struct macro_arg_list args;
	int   narg;
	const char* body;
};

typedef struct expression_info {
        symlist_t       referenced_syms;
        int             value;
} expression_t;

typedef struct symbol {
	char	*name;
	symtype	type;
	int	count;
	union	{
		struct reg_info	  *rinfo;
		struct field_info *finfo;
		struct const_info *cinfo;
		struct alias_info *ainfo;
		struct label_info *linfo;
		struct cond_info  *condinfo;
		struct macro_info *macroinfo;
	}info;
} symbol_t;

typedef struct symbol_ref {
	symbol_t *symbol;
	int	 offset;
} symbol_ref_t;

typedef struct symbol_node {
	SLIST_ENTRY(symbol_node) links;
	symbol_t *symbol;
} symbol_node_t;

typedef struct critical_section {
	TAILQ_ENTRY(critical_section) links;
	int begin_addr;
	int end_addr;
} critical_section_t;

typedef enum {
	SCOPE_ROOT,
	SCOPE_IF,
	SCOPE_ELSE_IF,
	SCOPE_ELSE
} scope_type;

typedef struct patch_info {
	int skip_patch;
	int skip_instr;
} patch_info_t;

typedef struct scope {
	SLIST_ENTRY(scope) scope_stack_links;
	TAILQ_ENTRY(scope) scope_links;
	TAILQ_HEAD(, scope) inner_scope;
	scope_type type;
	int inner_scope_patches;
	int begin_addr;
        int end_addr;
	patch_info_t patches[2];
	int func_num;
} scope_t;

TAILQ_HEAD(cs_tailq, critical_section);
SLIST_HEAD(scope_list, scope);
TAILQ_HEAD(scope_tailq, scope);

void	symbol_delete(symbol_t *symbol);

void	symtable_open(void);

void	symtable_close(void);

symbol_t *
	symtable_get(char *name);

symbol_node_t *
	symlist_search(symlist_t *symlist, char *symname);

void
	symlist_add(symlist_t *symlist, symbol_t *symbol, int how);
#define SYMLIST_INSERT_HEAD	0x00
#define SYMLIST_SORT		0x01

void	symlist_free(symlist_t *symlist);

void	symlist_merge(symlist_t *symlist_dest, symlist_t *symlist_src1,
		      symlist_t *symlist_src2);
void	symtable_dump(FILE *ofile, FILE *dfile);
