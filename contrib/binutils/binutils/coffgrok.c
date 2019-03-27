/* coffgrok.c
   Copyright 1994, 1995, 1997, 1998, 2000, 2001, 2002, 2003, 2004, 2007
   Free Software Foundation, Inc.

This file is part of GNU Binutils.

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
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Written by Steve Chamberlain (sac@cygnus.com)

   This module reads a coff file and builds a really simple type tree
   which can be read by other programs.  The first application is a
   coff->sysroff converter.  It can be tested with coffdump.c.

*/

#include "sysdep.h"
#include "bfd.h"
#include "libiberty.h"

#include "coff/internal.h"
#include "../bfd/libcoff.h"
#include "bucomm.h"
#include "coffgrok.h"

static int lofile = 1;
static struct coff_scope *top_scope;
static struct coff_scope *file_scope;
static struct coff_ofile *ofile;

static struct coff_symbol *last_function_symbol;
static struct coff_type *last_function_type;
static struct coff_type *last_struct;
static struct coff_type *last_enum;
static struct coff_sfile *cur_sfile;

static struct coff_symbol **tindex;


static asymbol **syms;
static long symcount;

#define N(x) ((x)->_n._n_nptr[1])

static struct coff_ptr_struct *rawsyms;
static int rawcount;
static bfd *abfd;

#define PTR_SIZE	4
#define SHORT_SIZE	2
#define INT_SIZE	4
#define LONG_SIZE	4
#define FLOAT_SIZE	4
#define DOUBLE_SIZE	8

#define INDEXOF(p)  ((struct coff_ptr_struct *)(p)-(rawsyms))

static struct coff_scope *empty_scope (void);
static struct coff_symbol *empty_symbol (void);
static void push_scope (int);
static void pop_scope (void);
static void do_sections_p1 (struct coff_ofile *);
static void do_sections_p2 (struct coff_ofile *);
static struct coff_where *do_where (int);
static struct coff_line *do_lines (int, char *);
static struct coff_type *do_type (int);
static struct coff_visible *do_visible (int);
static int do_define (int, struct coff_scope *);
static struct coff_ofile *doit (void);

static struct coff_scope *
empty_scope (void)
{
  struct coff_scope *l;
  l = (struct coff_scope *) (xcalloc (sizeof (struct coff_scope), 1));
  return l;
}

static struct coff_symbol *
empty_symbol (void)
{
  return (struct coff_symbol *) (xcalloc (sizeof (struct coff_symbol), 1));
}

/*int l;*/
static void
push_scope (int link)
{
  struct coff_scope *n = empty_scope ();
  if (link)
    {
      if (top_scope)
	{
	  if (top_scope->list_tail)
	    {
	      top_scope->list_tail->next = n;
	    }
	  else
	    {
	      top_scope->list_head = n;
	    }
	  top_scope->list_tail = n;
	}
    }
  n->parent = top_scope;

  top_scope = n;
}

static void
pop_scope (void)
{
  top_scope = top_scope->parent;
}

static void
do_sections_p1 (struct coff_ofile *head)
{
  asection *section;
  int idx;
  struct coff_section *all = (struct coff_section *) (xcalloc (abfd->section_count + 1,
					     sizeof (struct coff_section)));
  head->nsections = abfd->section_count + 1;
  head->sections = all;

  for (idx = 0, section = abfd->sections; section; section = section->next, idx++)
    {
      long relsize;
      int i = section->target_index;
      arelent **relpp;
      long relcount;

      relsize = bfd_get_reloc_upper_bound (abfd, section);
      if (relsize < 0)
	bfd_fatal (bfd_get_filename (abfd));
      if (relsize == 0)
	continue;
      relpp = (arelent **) xmalloc (relsize);
      relcount = bfd_canonicalize_reloc (abfd, section, relpp, syms);
      if (relcount < 0)
	bfd_fatal (bfd_get_filename (abfd));

      head->sections[i].name = (char *) (section->name);
      head->sections[i].code = section->flags & SEC_CODE;
      head->sections[i].data = section->flags & SEC_DATA;
      if (strcmp (section->name, ".bss") == 0)
	head->sections[i].data = 1;
      head->sections[i].address = section->lma;
      head->sections[i].size = bfd_get_section_size (section);
      head->sections[i].number = idx;
      head->sections[i].nrelocs = section->reloc_count;
      head->sections[i].relocs =
	(struct coff_reloc *) (xcalloc (section->reloc_count,
					sizeof (struct coff_reloc)));
      head->sections[i].bfd_section = section;
    }
  head->sections[0].name = "ABSOLUTE";
  head->sections[0].code = 0;
  head->sections[0].data = 0;
  head->sections[0].address = 0;
  head->sections[0].size = 0;
  head->sections[0].number = 0;
}

static void
do_sections_p2 (struct coff_ofile *head)
{
  asection *section;
  for (section = abfd->sections; section; section = section->next)
    {
      unsigned int j;

      for (j = 0; j < section->reloc_count; j++)
	{
	  int idx;
	  int i = section->target_index;
	  struct coff_reloc *r = head->sections[i].relocs + j;
	  arelent *sr = section->relocation + j;
	  r->offset = sr->address;
	  r->addend = sr->addend;
	  idx = ((coff_symbol_type *) (sr->sym_ptr_ptr[0]))->native - rawsyms;
	  r->symbol = tindex[idx];
	}
    }
}

static struct coff_where *
do_where (int i)
{
  struct internal_syment *sym = &rawsyms[i].u.syment;
  struct coff_where *where =
    (struct coff_where *) (xmalloc (sizeof (struct coff_where)));
  where->offset = sym->n_value;

  if (sym->n_scnum == -1)
    sym->n_scnum = 0;

  switch (sym->n_sclass)
    {
    case C_FIELD:
      where->where = coff_where_member_of_struct;
      where->offset = sym->n_value / 8;
      where->bitoffset = sym->n_value % 8;
      where->bitsize = rawsyms[i + 1].u.auxent.x_sym.x_misc.x_lnsz.x_size;
      break;
    case C_MOE:
      where->where = coff_where_member_of_enum;
      break;
    case C_MOS:
    case C_MOU:
      where->where = coff_where_member_of_struct;
      break;
    case C_AUTO:
    case C_ARG:
      where->where = coff_where_stack;
      break;
    case C_EXT:
    case C_STAT:
    case C_EXTDEF:
    case C_LABEL:
      where->where = coff_where_memory;
      where->section = &ofile->sections[sym->n_scnum];
      break;
    case C_REG:
    case C_REGPARM:
      where->where = coff_where_register;
      break;
    case C_ENTAG:
      where->where = coff_where_entag;
      break;
    case C_STRTAG:
    case C_UNTAG:
      where->where = coff_where_strtag;
      break;
    case C_TPDEF:
      where->where = coff_where_typedef;
      break;
    default:
      abort ();
      break;
    }
  return where;
}

static
struct coff_line *
do_lines (int i, char *name ATTRIBUTE_UNUSED)
{
  struct coff_line *res = (struct coff_line *) xcalloc (sizeof (struct coff_line), 1);
  asection *s;
  unsigned int l;

  /* Find out if this function has any line numbers in the table */
  for (s = abfd->sections; s; s = s->next)
    {
      for (l = 0; l < s->lineno_count; l++)
	{
	  if (s->lineno[l].line_number == 0)
	    {
	      if (rawsyms + i == ((coff_symbol_type *) (&(s->lineno[l].u.sym[0])))->native)
		{
		  /* These lines are for this function - so count them and stick them on */
		  int c = 0;
		  /* Find the linenumber of the top of the function, since coff linenumbers
		     are relative to the start of the function.  */
		  int start_line = rawsyms[i + 3].u.auxent.x_sym.x_misc.x_lnsz.x_lnno;

		  l++;
		  for (c = 0; s->lineno[l + c + 1].line_number; c++)
		    ;

		  /* Add two extra records, one for the prologue and one for the epilogue */
		  c += 1;
		  res->nlines = c;
		  res->lines = (int *) (xcalloc (sizeof (int), c));
		  res->addresses = (int *) (xcalloc (sizeof (int), c));
		  res->lines[0] = start_line;
		  res->addresses[0] = rawsyms[i].u.syment.n_value - s->vma;
		  for (c = 0; s->lineno[l + c + 1].line_number; c++)
		    {
		      res->lines[c + 1] = s->lineno[l + c].line_number + start_line - 1;
		      res->addresses[c + 1] = s->lineno[l + c].u.offset;
		    }
		  return res;
		}
	    }
	}
    }
  return res;
}

static
struct coff_type *
do_type (int i)
{
  struct internal_syment *sym = &rawsyms[i].u.syment;
  union internal_auxent *aux = &rawsyms[i + 1].u.auxent;
  struct coff_type *res =
    (struct coff_type *) xmalloc (sizeof (struct coff_type));
  int type = sym->n_type;
  int which_dt = 0;
  int dimind = 0;

  res->type = coff_basic_type;
  res->u.basic = type & 0xf;

  switch (type & 0xf)
    {
    case T_NULL:
    case T_VOID:
      if (sym->n_numaux && sym->n_sclass == C_STAT)
	{
	  /* This is probably a section definition */
	  res->type = coff_secdef_type;
	  res->size = aux->x_scn.x_scnlen;
	}
      else
	{
	  if (type == 0)
	    {
	      /* Don't know what this is, let's make it a simple int */
	      res->size = INT_SIZE;
	      res->u.basic = T_UINT;
	    }
	  else
	    {
	      /* Else it could be a function or pointer to void */
	      res->size = 0;
	    }
	}
      break;


      break;
    case T_UCHAR:
    case T_CHAR:
      res->size = 1;
      break;
    case T_USHORT:
    case T_SHORT:
      res->size = SHORT_SIZE;
      break;
    case T_UINT:
    case T_INT:
      res->size = INT_SIZE;
      break;
    case T_ULONG:
    case T_LONG:
      res->size = LONG_SIZE;
      break;
    case T_FLOAT:
      res->size = FLOAT_SIZE;
      break;
    case T_DOUBLE:
      res->size = DOUBLE_SIZE;
      break;
    case T_STRUCT:
    case T_UNION:
      if (sym->n_numaux)
	{
	  if (aux->x_sym.x_tagndx.p)
	    {
	      /* Referring to a struct defined elsewhere */
	      res->type = coff_structref_type;
	      res->u.astructref.ref = tindex[INDEXOF (aux->x_sym.x_tagndx.p)];
	      res->size = res->u.astructref.ref ?
		res->u.astructref.ref->type->size : 0;
	    }
	  else
	    {
	      /* A definition of a struct */
	      last_struct = res;
	      res->type = coff_structdef_type;
	      res->u.astructdef.elements = empty_scope ();
	      res->u.astructdef.idx = 0;
	      res->u.astructdef.isstruct = (type & 0xf) == T_STRUCT;
	      res->size = aux->x_sym.x_misc.x_lnsz.x_size;
	    }
	}
      else
	{
	  /* No auxents - it's anonymous */
	  res->type = coff_structref_type;
	  res->u.astructref.ref = 0;
	  res->size = 0;
	}
      break;
    case T_ENUM:
      if (aux->x_sym.x_tagndx.p)
	{
	  /* Referring to a enum defined elsewhere */
	  res->type = coff_enumref_type;
	  res->u.aenumref.ref = tindex[INDEXOF (aux->x_sym.x_tagndx.p)];
	  res->size = res->u.aenumref.ref->type->size;
	}
      else
	{
	  /* A definition of an enum */
	  last_enum = res;
	  res->type = coff_enumdef_type;
	  res->u.aenumdef.elements = empty_scope ();
	  res->size = aux->x_sym.x_misc.x_lnsz.x_size;
	}
      break;
    case T_MOE:
      break;
    }

  for (which_dt = 5; which_dt >= 0; which_dt--)
    {
      switch ((type >> ((which_dt * 2) + 4)) & 0x3)
	{
	case 0:
	  break;
	case DT_ARY:
	  {
	    struct coff_type *ptr = ((struct coff_type *)
				     xmalloc (sizeof (struct coff_type)));
	    int els = (dimind < DIMNUM
		       ? aux->x_sym.x_fcnary.x_ary.x_dimen[dimind]
		       : 0);
	    ++dimind;
	    ptr->type = coff_array_type;
	    ptr->size = els * res->size;
	    ptr->u.array.dim = els;
	    ptr->u.array.array_of = res;
	    res = ptr;
	    break;
	  }
	case DT_PTR:
	  {
	    struct coff_type *ptr =
	      (struct coff_type *) xmalloc (sizeof (struct coff_type));
	    ptr->size = PTR_SIZE;
	    ptr->type = coff_pointer_type;
	    ptr->u.pointer.points_to = res;
	    res = ptr;
	    break;
	  }
	case DT_FCN:
	  {
	    struct coff_type *ptr
	      = (struct coff_type *) xmalloc (sizeof (struct coff_type));
	    ptr->size = 0;
	    ptr->type = coff_function_type;
	    ptr->u.function.function_returns = res;
	    ptr->u.function.parameters = empty_scope ();
	    ptr->u.function.lines = do_lines (i, sym->_n._n_nptr[1]);
	    ptr->u.function.code = 0;
	    last_function_type = ptr;
	    res = ptr;
	    break;
	  }
	}
    }
  return res;
}

static struct coff_visible *
do_visible (int i)
{
  struct internal_syment *sym = &rawsyms[i].u.syment;
  struct coff_visible *visible =
    (struct coff_visible *) (xmalloc (sizeof (struct coff_visible)));
  enum coff_vis_type t;
  switch (sym->n_sclass)
    {
    case C_MOS:
    case C_MOU:
    case C_FIELD:
      t = coff_vis_member_of_struct;
      break;
    case C_MOE:
      t = coff_vis_member_of_enum;
      break;

    case C_REGPARM:
      t = coff_vis_regparam;
      break;

    case C_REG:
      t = coff_vis_register;
      break;
    case C_STRTAG:
    case C_UNTAG:
    case C_ENTAG:
    case C_TPDEF:
      t = coff_vis_tag;
      break;
    case C_AUTOARG:
    case C_ARG:
      t = coff_vis_autoparam;
      break;
    case C_AUTO:


      t = coff_vis_auto;
      break;
    case C_LABEL:
    case C_STAT:
      t = coff_vis_int_def;
      break;
    case C_EXT:
      if (sym->n_scnum == N_UNDEF)
	{
	  if (sym->n_value)
	    t = coff_vis_common;
	  else
	    t = coff_vis_ext_ref;
	}
      else
	t = coff_vis_ext_def;
      break;
    default:
      abort ();
      break;

    }
  visible->type = t;
  return visible;
}

static int
do_define (int i, struct coff_scope *b)
{
  static int symbol_index;
  struct internal_syment *sym = &rawsyms[i].u.syment;

  /* Define a symbol and attach to block b */
  struct coff_symbol *s = empty_symbol ();

  s->number = ++symbol_index;
  s->name = sym->_n._n_nptr[1];
  s->sfile = cur_sfile;
  /* Glue onto the ofile list */
  if (lofile >= 0)
    {
      if (ofile->symbol_list_tail)
	ofile->symbol_list_tail->next_in_ofile_list = s;
      else
	ofile->symbol_list_head = s;
      ofile->symbol_list_tail = s;
      /* And the block list */
    }
  if (b->vars_tail)
    b->vars_tail->next = s;
  else
    b->vars_head = s;

  b->vars_tail = s;
  b->nvars++;
  s->type = do_type (i);
  s->where = do_where (i);
  s->visible = do_visible (i);

  tindex[i] = s;

  /* We remember the lowest address in each section for each source file */

  if (s->where->where == coff_where_memory
      && s->type->type == coff_secdef_type)
    {
      struct coff_isection *is = cur_sfile->section + s->where->section->number;

      if (!is->init)
	{
	  is->low = s->where->offset;
	  is->high = s->where->offset + s->type->size;
	  is->init = 1;
	  is->parent = s->where->section;
	}

    }

  if (s->type->type == coff_function_type)
    last_function_symbol = s;

  return i + sym->n_numaux + 1;
}


static
struct coff_ofile *
doit (void)
{
  int i;
  int infile = 0;
  struct coff_ofile *head =
    (struct coff_ofile *) xmalloc (sizeof (struct coff_ofile));
  ofile = head;
  head->source_head = 0;
  head->source_tail = 0;
  head->nsources = 0;
  head->symbol_list_tail = 0;
  head->symbol_list_head = 0;
  do_sections_p1 (head);
  push_scope (1);

  for (i = 0; i < rawcount;)
    {
      struct internal_syment *sym = &rawsyms[i].u.syment;
      switch (sym->n_sclass)
	{
	case C_FILE:
	  {
	    /* new source file announced */
	    struct coff_sfile *n =
	      (struct coff_sfile *) xmalloc (sizeof (struct coff_sfile));
	    n->section = (struct coff_isection *) xcalloc (sizeof (struct coff_isection), abfd->section_count + 1);
	    cur_sfile = n;
	    n->name = sym->_n._n_nptr[1];
	    n->next = 0;

	    if (infile)
	      {
		pop_scope ();
	      }
	    infile = 1;
	    push_scope (1);
	    file_scope = n->scope = top_scope;

	    if (head->source_tail)
	      head->source_tail->next = n;
	    else
	      head->source_head = n;
	    head->source_tail = n;
	    head->nsources++;
	    i += sym->n_numaux + 1;
	  }
	  break;
	case C_FCN:
	  {
	    char *name = sym->_n._n_nptr[1];
	    if (name[1] == 'b')
	      {
		/* Function start */
		push_scope (0);
		last_function_type->u.function.code = top_scope;
		top_scope->sec = ofile->sections + sym->n_scnum;
		top_scope->offset = sym->n_value;
	      }
	    else
	      {
		top_scope->size = sym->n_value - top_scope->offset + 1;
		pop_scope ();

	      }
	    i += sym->n_numaux + 1;
	  }
	  break;

	case C_BLOCK:
	  {
	    char *name = sym->_n._n_nptr[1];
	    if (name[1] == 'b')
	      {
		/* Block start */
		push_scope (1);
		top_scope->sec = ofile->sections + sym->n_scnum;
		top_scope->offset = sym->n_value;

	      }
	    else
	      {
		top_scope->size = sym->n_value - top_scope->offset + 1;
		pop_scope ();
	      }
	    i += sym->n_numaux + 1;
	  }
	  break;
	case C_REGPARM:
	case C_ARG:
	  i = do_define (i, last_function_symbol->type->u.function.parameters);
	  break;
	case C_MOS:
	case C_MOU:
	case C_FIELD:
	  i = do_define (i, last_struct->u.astructdef.elements);
	  break;
	case C_MOE:
	  i = do_define (i, last_enum->u.aenumdef.elements);
	  break;
	case C_STRTAG:
	case C_ENTAG:
	case C_UNTAG:
	  /* Various definition */
	  i = do_define (i, top_scope);
	  break;
	case C_EXT:
	case C_LABEL:
	  i = do_define (i, file_scope);
	  break;
	case C_STAT:
	case C_TPDEF:
	case C_AUTO:
	case C_REG:
	  i = do_define (i, top_scope);
	  break;
	default:
	  abort ();
	case C_EOS:
	  i += sym->n_numaux + 1;
	  break;
	}
    }
  do_sections_p2 (head);
  return head;
}

struct coff_ofile *
coff_grok (bfd *inabfd)
{
  long storage;
  struct coff_ofile *p;
  abfd = inabfd;
  storage = bfd_get_symtab_upper_bound (abfd);

  if (storage < 0)
    bfd_fatal (abfd->filename);

  syms = (asymbol **) xmalloc (storage);
  symcount = bfd_canonicalize_symtab (abfd, syms);
  if (symcount < 0)
    bfd_fatal (abfd->filename);
  rawsyms = obj_raw_syments (abfd);
  rawcount = obj_raw_syment_count (abfd);;
  tindex = (struct coff_symbol **) (xcalloc (sizeof (struct coff_symbol *), rawcount));

  p = doit ();
  return p;
}
