/* srconv.c -- Sysroff conversion program
   Copyright 1994, 1995, 1996, 1998, 1999, 2000, 2001, 2002, 2003, 2004,
   2005, 2007 Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* Written by Steve Chamberlain (sac@cygnus.com)

   This program can be used to convert a coff object file
   into a Hitachi OM/LM (Sysroff) format.

   All debugging information is preserved */

#include "sysdep.h"
#include "bfd.h"
#include "bucomm.h"
#include "sysroff.h"
#include "coffgrok.h"
#include "libiberty.h"
#include "getopt.h"

#include "coff/internal.h"
#include "../bfd/libcoff.h"

/*#define FOOP1 1 */

static int addrsize;
static char *toolname;
static char **rnames;

static int get_member_id (int);
static int get_ordinary_id (int);
static char *section_translate (char *);
static char *strip_suffix (char *);
static void checksum (FILE *, unsigned char *, int, int);
static void writeINT (int, unsigned char *, int *, int, FILE *);
static void writeBITS (int, unsigned char *, int *, int);
static void writeBARRAY (barray, unsigned char *, int *, int, FILE *);
static void writeCHARS (char *, unsigned char *, int *, int, FILE *);
static void wr_tr (void);
static void wr_un (struct coff_ofile *, struct coff_sfile *, int, int);
static void wr_hd (struct coff_ofile *);
static void wr_sh (struct coff_ofile *, struct coff_section *);
static void wr_ob (struct coff_ofile *, struct coff_section *);
static void wr_rl (struct coff_ofile *, struct coff_section *);
static void wr_object_body (struct coff_ofile *);
static void wr_dps_start
  (struct coff_sfile *, struct coff_section *, struct coff_scope *, int, int);
static void wr_dps_end (struct coff_section *, struct coff_scope *, int);
static int *nints (int);
static void walk_tree_type_1
  (struct coff_sfile *, struct coff_symbol *, struct coff_type *, int);
static void walk_tree_type
  (struct coff_sfile *, struct coff_symbol *, struct coff_type *, int);
static void walk_tree_symbol
  (struct coff_sfile *, struct coff_section *, struct coff_symbol *, int);
static void walk_tree_scope
  (struct coff_section *, struct coff_sfile *, struct coff_scope *, int, int);
static void walk_tree_sfile (struct coff_section *, struct coff_sfile *);
static void wr_program_structure (struct coff_ofile *, struct coff_sfile *);
static void wr_du (struct coff_ofile *, struct coff_sfile *, int);
static void wr_dus (struct coff_ofile *, struct coff_sfile *);
static int find_base (struct coff_sfile *, struct coff_section *);
static void wr_dln (struct coff_ofile *, struct coff_sfile *, int);
static void wr_globals (struct coff_ofile *, struct coff_sfile *, int);
static void wr_debug (struct coff_ofile *);
static void wr_cs (void);
static int wr_sc (struct coff_ofile *, struct coff_sfile *);
static void wr_er (struct coff_ofile *, struct coff_sfile *, int);
static void wr_ed (struct coff_ofile *, struct coff_sfile *, int);
static void wr_unit_info (struct coff_ofile *);
static void wr_module (struct coff_ofile *);
static int align (int);
static void prescan (struct coff_ofile *);
static void show_usage (FILE *, int);
extern int main (int, char **);

static FILE *file;
static bfd *abfd;
static int debug = 0;
static int quick = 0;
static int noprescan = 0;
static struct coff_ofile *tree;
/* Obsolete ??
   static int absolute_p;
 */

static int segmented_p;
static int code;

static int ids1[20000];
static int ids2[20000];

static int base1 = 0x18;
static int base2 = 0x2018;

static int
get_member_id (int x)
{
  if (ids2[x])
    return ids2[x];

  ids2[x] = base2++;
  return ids2[x];
}

static int
get_ordinary_id (int x)
{
  if (ids1[x])
    return ids1[x];

  ids1[x] = base1++;
  return ids1[x];
}
static char *
section_translate (char *n)
{
  if (strcmp (n, ".text") == 0)
    return "P";
  if (strcmp (n, ".data") == 0)
    return "D";
  if (strcmp (n, ".bss") == 0)
    return "B";
  return n;
}

#define DATE "940201073000";	/* Just a time on my birthday */

static
char *
strip_suffix (char *name)
{
  int i;
  char *res;

  for (i = 0; name[i] != 0 && name[i] != '.'; i++)
    ;
  res = (char *) xmalloc (i + 1);
  memcpy (res, name, i);
  res[i] = 0;
  return res;
}

/* IT LEN stuff CS */
static void
checksum (FILE *file, unsigned char *ptr, int size, int code)
{
  int j;
  int last;
  int sum = 0;
  int bytes = size / 8;

  last = !(code & 0xff00);
  if (size & 0x7)
    abort ();
  ptr[0] = code | (last ? 0x80 : 0);
  ptr[1] = bytes + 1;

  for (j = 0; j < bytes; j++)
    sum += ptr[j];

  /* Glue on a checksum too.  */
  ptr[bytes] = ~sum;
  fwrite (ptr, bytes + 1, 1, file);
}


static void
writeINT (int n, unsigned char *ptr, int *idx, int size, FILE *file)
{
  int byte = *idx / 8;

  if (size == -2)
    size = addrsize;
  else if (size == -1)
    size = 0;

  if (byte > 240)
    {
      /* Lets write out that record and do another one.  */
      checksum (file, ptr, *idx, code | 0x1000);
      *idx = 16;
      byte = *idx / 8;
    }

  switch (size)
    {
    case 0:
      break;
    case 1:
      ptr[byte] = n;
      break;
    case 2:
      ptr[byte + 0] = n >> 8;
      ptr[byte + 1] = n;
      break;
    case 4:
      ptr[byte + 0] = n >> 24;
      ptr[byte + 1] = n >> 16;
      ptr[byte + 2] = n >> 8;
      ptr[byte + 3] = n >> 0;
      break;
    default:
      abort ();
    }
  *idx += size * 8;
}

static void
writeBITS (int val, unsigned char *ptr, int *idx, int size)
{
  int byte = *idx / 8;
  int bit = *idx % 8;
  int old;

  *idx += size;

  old = ptr[byte];
  /* Turn off all about to change bits.  */
  old &= ~((~0 >> (8 - bit - size)) & ((1 << size) - 1));
  /* Turn on the bits we want.  */
  old |= (val & ((1 << size) - 1)) << (8 - bit - size);
  ptr[byte] = old;
}

static void
writeBARRAY (barray data, unsigned char *ptr, int *idx,
	     int size ATTRIBUTE_UNUSED, FILE *file)
{
  int i;

  writeINT (data.len, ptr, idx, 1, file);
  for (i = 0; i < data.len; i++)
    writeINT (data.data[i], ptr, idx, 1, file);
}

static void
writeCHARS (char *string, unsigned char *ptr, int *idx, int size, FILE *file)
{
  int i = *idx / 8;

  if (i > 240)
    {
      /* Lets write out that record and do another one.  */
      checksum (file, ptr, *idx, code | 0x1000);
      *idx = 16;
      i = *idx / 8;
    }

  if (size == 0)
    {
      /* Variable length string.  */
      size = strlen (string);
      ptr[i++] = size;
    }

  /* BUG WAITING TO HAPPEN.  */
  memcpy (ptr + i, string, size);
  i += size;
  *idx = i * 8;
}

#define SYSROFF_SWAP_OUT
#include "sysroff.c"

static char *rname_sh[] =
{
  "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"
};

static char *rname_h8300[] =
{
  "ER0", "ER1", "ER2", "ER3", "ER4", "ER5", "ER6", "ER7", "PC", "CCR"
};

static void
wr_tr (void)
{
  /* The TR block is not normal - it doesn't have any contents.  */

  static char b[] =
    {
      0xff,			/* IT */
      0x03,			/* RL */
      0xfd,			/* CS */
    };
  fwrite (b, 1, sizeof (b), file);
}

static void
wr_un (struct coff_ofile *ptr, struct coff_sfile *sfile, int first,
       int nsecs ATTRIBUTE_UNUSED)
{
  struct IT_un un;
  struct coff_symbol *s;

  un.spare1 = 0;

  if (bfd_get_file_flags (abfd) & EXEC_P)
    un.format = FORMAT_LM;
  else
    un.format = FORMAT_OM;
  un.spare1 = 0;

  /* Don't count the abs section.  */
  un.nsections = ptr->nsections - 1;

  un.nextdefs = 0;
  un.nextrefs = 0;
  /* Count all the undefined and defined variables with global scope.  */

  if (first)
    {
      for (s = ptr->symbol_list_head; s; s = s->next_in_ofile_list)
	{
	  if (s->visible->type == coff_vis_ext_def
	      || s->visible->type == coff_vis_common)
	    un.nextdefs++;

	  if (s->visible->type == coff_vis_ext_ref)
	    un.nextrefs++;
	}
    }
  un.tool = toolname;
  un.tcd = DATE;
  un.linker = "L_GX00";
  un.lcd = DATE;
  un.name = sfile->name;
  sysroff_swap_un_out (file, &un);
}

static void
wr_hd (struct coff_ofile *p)
{
  struct IT_hd hd;

  hd.spare1 = 0;
  if (bfd_get_file_flags (abfd) & EXEC_P)
    hd.mt = MTYPE_ABS_LM;
  else
    hd.mt = MTYPE_OMS_OR_LMS;

  hd.cd = DATE;

  hd.nu = p->nsources;		/* Always one unit */
  hd.code = 0;			/* Always ASCII */
  hd.ver = "0200";		/* Version 2.00 */

  switch (bfd_get_arch (abfd))
    {
    case bfd_arch_h8300:
      hd.au = 8;
      hd.si = 0;
      hd.spcsz = 32;
      hd.segsz = 0;
      hd.segsh = 0;
      switch (bfd_get_mach (abfd))
	{
	case bfd_mach_h8300:
	  hd.cpu = "H8300";
	  hd.afl = 2;
	  addrsize = 2;
	  toolname = "C_H8/300";
	  break;
	case bfd_mach_h8300h:
	  hd.cpu = "H8300H";
	  hd.afl = 4;
	  addrsize = 4;
	  toolname = "C_H8/300H";
	  break;
	case bfd_mach_h8300s:
	  hd.cpu = "H8300S";
	  hd.afl = 4;
	  addrsize = 4;
	  toolname = "C_H8/300S";
	  break;
	default:
	  abort();
	}
      rnames = rname_h8300;
      break;
    case bfd_arch_sh:
      hd.au = 8;
      hd.si = 0;
      hd.afl = 4;
      hd.spcsz = 32;
      hd.segsz = 0;
      hd.segsh = 0;
      hd.cpu = "SH";
      addrsize = 4;
      toolname = "C_SH";
      rnames = rname_sh;
      break;
    default:
      abort ();
    }

  if (! bfd_get_file_flags(abfd) & EXEC_P)
    {
      hd.ep = 0;
    }
  else
    {
      hd.ep = 1;
      hd.uan = 0;
      hd.sa = 0;
      hd.sad = 0;
      hd.address = bfd_get_start_address (abfd);
    }

  hd.os = "";
  hd.sys = "";
  hd.mn = strip_suffix (bfd_get_filename (abfd));

  sysroff_swap_hd_out (file, &hd);
}


static void
wr_sh (struct coff_ofile *p ATTRIBUTE_UNUSED, struct coff_section *sec)
{
  struct IT_sh sh;
  sh.unit = 0;
  sh.section = sec->number;
#ifdef FOOP1
  sh.section = 0;
#endif
  sysroff_swap_sh_out (file, &sh);
}


static void
wr_ob (struct coff_ofile *p ATTRIBUTE_UNUSED, struct coff_section *section)
{
  bfd_size_type i;
  int first = 1;
  unsigned char stuff[200];

  i = 0;
  while (i < bfd_get_section_size (section->bfd_section))
    {
      struct IT_ob ob;
      int todo = 200;		/* Copy in 200 byte lumps.  */

      ob.spare = 0;
      if (i + todo > bfd_get_section_size (section->bfd_section))
	todo = bfd_get_section_size (section->bfd_section) - i;

      if (first)
	{
	  ob.saf = 1;
	  if (bfd_get_file_flags (abfd) & EXEC_P)
	    ob.address = section->address;
	  else
	    ob.address = 0;

	  first = 0;
	}
      else
	{
	  ob.saf = 0;
	}

      ob.cpf = 0;		/* Never compress.  */
      ob.data.len = todo;
      bfd_get_section_contents (abfd, section->bfd_section, stuff, i, todo);
      ob.data.data = stuff;
      sysroff_swap_ob_out (file, &ob /*, i + todo < section->size */ );
      i += todo;
    }

  /* Now fill the rest with blanks.  */
  while (i < (bfd_size_type) section->size)
    {
      struct IT_ob ob;
      int todo = 200;		/* Copy in 200 byte lumps.  */

      ob.spare = 0;
      if (i + todo > (bfd_size_type) section->size)
	todo = section->size - i;
      ob.saf = 0;

      ob.cpf = 0;		/* Never compress.  */
      ob.data.len = todo;
      memset (stuff, 0, todo);
      ob.data.data = stuff;
      sysroff_swap_ob_out (file, &ob);
      i += todo;
    }
  /* Now fill the rest with blanks.  */
}

static void
wr_rl (struct coff_ofile *ptr ATTRIBUTE_UNUSED, struct coff_section *sec)
{
  int nr = sec->nrelocs;
  int i;

  for (i = 0; i < nr; i++)
    {
      struct coff_reloc *r = sec->relocs + i;
      struct coff_symbol *ref;
      struct IT_rl rl;

      rl.apol = 0;
      rl.boundary = 0;
      rl.segment = 1;
      rl.sign = 0;
      rl.check = 0;
      rl.addr = r->offset;
      rl.bitloc = 0;
      rl.flen = 32;		/* SH Specific.  */

      /* What sort of reloc ? Look in the section to find out.  */
      ref = r->symbol;
      if (ref->visible->type == coff_vis_ext_ref)
	{
	  rl.bcount = 4;	/* Always 4 for us.  */
	  rl.op = OP_EXT_REF;
	  rl.symn = ref->er_number;
	}
      else if (ref->visible->type == coff_vis_common)
	{
	  rl.bcount = 11;	/* Always 11 for us.  */
	  rl.op = OP_SEC_REF;
	  rl.secn = ref->where->section->number;
	  rl.copcode_is_3 = 3;
	  rl.alength_is_4 = 4;
	  rl.addend = ref->where->offset - ref->where->section->address;
	  rl.aopcode_is_0x20 = 0x20;
	}
      else
	{
	  rl.bcount = 11;	/* Always 11 for us.  */
	  rl.op = OP_SEC_REF;
	  rl.secn = ref->where->section->number;
	  rl.copcode_is_3 = 3;
	  rl.alength_is_4 = 4;
	  rl.addend = -ref->where->section->address;
	  rl.aopcode_is_0x20 = 0x20;
	}

      rl.end = 0xff;

      if (   rl.op == OP_SEC_REF
	  || rl.op == OP_EXT_REF)
	sysroff_swap_rl_out (file, &rl);
    }
}

static void
wr_object_body (struct coff_ofile *p)
{
  int i;

  for (i = 1; i < p->nsections; i++)
    {
      wr_sh (p, p->sections + i);
      wr_ob (p, p->sections + i);
      wr_rl (p, p->sections + i);
    }
}

static void
wr_dps_start (struct coff_sfile *sfile,
	      struct coff_section *section ATTRIBUTE_UNUSED,
	      struct coff_scope *scope, int type, int nest)
{
  struct IT_dps dps;

  dps.end = 0;
  dps.opt = 0;
  dps.type = type;

  if (scope->sec)
    {
      dps.san = scope->sec->number;
      dps.address = scope->offset - find_base (sfile, scope->sec);
      dps.block_size = scope->size;

      if (debug)
	{
	  printf ("DPS %s %d %x\n",
		  sfile->name,
		  nest,
		  dps.address);
	}
    }
  else
    {
      dps.san = 0;
      dps.address = 0;
      dps.block_size = 0;
    }

  dps.nesting = nest;
  dps.neg = 0x1001;
  sysroff_swap_dps_out (file, &dps);
}

static void
wr_dps_end (struct coff_section *section ATTRIBUTE_UNUSED,
	    struct coff_scope *scope ATTRIBUTE_UNUSED, int type)
{
  struct IT_dps dps;

  dps.end = 1;
  dps.type = type;
  sysroff_swap_dps_out (file, &dps);
}

static int *
nints (int x)
{
  return (int *) (xcalloc (sizeof (int), x));
}

static void
walk_tree_type_1 (struct coff_sfile *sfile, struct coff_symbol *symbol,
		  struct coff_type *type, int nest)
{
  switch (type->type)
    {
    case coff_secdef_type:
    case coff_basic_type:
      {
	struct IT_dbt dbt;

	switch (type->u.basic)
	  {
	  case T_NULL:
	  case T_VOID:
	    dbt.btype = BTYPE_VOID;
	    dbt.sign = BTYPE_UNSPEC;
	    dbt.fptype = FPTYPE_NOTSPEC;
	    break;

	  case T_CHAR:
	    dbt.btype = BTYPE_CHAR;
	    dbt.sign = BTYPE_UNSPEC;
	    dbt.fptype = FPTYPE_NOTSPEC;
	    break;

	  case T_SHORT:
	  case T_INT:
	  case T_LONG:
	    dbt.btype = BTYPE_INT;
	    dbt.sign = SIGN_SIGNED;
	    dbt.fptype = FPTYPE_NOTSPEC;
	    break;

	  case T_FLOAT:
	    dbt.btype = BTYPE_FLOAT;
	    dbt.fptype = FPTYPE_SINGLE;
	    break;

	  case T_DOUBLE:
	    dbt.btype = BTYPE_FLOAT;
	    dbt.fptype = FPTYPE_DOUBLE;
	    break;

	  case T_LNGDBL:
	    dbt.btype = BTYPE_FLOAT;
	    dbt.fptype = FPTYPE_EXTENDED;
	    break;

	  case T_UCHAR:
	    dbt.btype = BTYPE_CHAR;
	    dbt.sign = SIGN_UNSIGNED;
	    dbt.fptype = FPTYPE_NOTSPEC;
	    break;

	  case T_USHORT:
	  case T_UINT:
	  case T_ULONG:
	    dbt.btype = BTYPE_INT;
	    dbt.sign = SIGN_UNSIGNED;
	    dbt.fptype = FPTYPE_NOTSPEC;
	    break;
	  }

	dbt.bitsize = type->size;
	dbt.neg = 0x1001;
	sysroff_swap_dbt_out (file, &dbt);
	break;
      }

    case coff_pointer_type:
      {
	struct IT_dpt dpt;

	dpt.dunno = 0;
	walk_tree_type_1 (sfile, symbol, type->u.pointer.points_to, nest + 1);
	dpt.neg = 0x1001;
	sysroff_swap_dpt_out (file, &dpt);
	break;
      }

    case coff_function_type:
      {
	struct IT_dfp dfp;
	struct coff_symbol *param;

	dfp.end = 0;
	dfp.spare = 0;
	dfp.nparams = type->u.function.parameters->nvars;
	dfp.neg = 0x1001;

	walk_tree_type_1 (sfile, symbol, type->u.function.function_returns, nest + 1);

	sysroff_swap_dfp_out (file, &dfp);

	for (param = type->u.function.parameters->vars_head;
	     param;
	     param = param->next)
	  walk_tree_symbol (sfile, 0, param, nest);

	dfp.end = 1;
	sysroff_swap_dfp_out (file, &dfp);
	break;
      }

    case coff_structdef_type:
      {
	struct IT_dbt dbt;
	struct IT_dds dds;
	struct coff_symbol *member;

	dds.spare = 0;
	dbt.btype = BTYPE_STRUCT;
	dbt.bitsize = type->size;
	dbt.sign = SIGN_UNSPEC;
	dbt.fptype = FPTYPE_NOTSPEC;
	dbt.sid = get_member_id (type->u.astructdef.idx);
	dbt.neg = 0x1001;
	sysroff_swap_dbt_out (file, &dbt);
	dds.end = 0;
	dds.neg = 0x1001;
	sysroff_swap_dds_out (file, &dds);

	for (member = type->u.astructdef.elements->vars_head;
	     member;
	     member = member->next)
	  walk_tree_symbol (sfile, 0, member, nest + 1);

	dds.end = 1;
	sysroff_swap_dds_out (file, &dds);

      }
      break;

    case coff_structref_type:
      {
	struct IT_dbt dbt;

	dbt.btype = BTYPE_TAG;
	dbt.bitsize = type->size;
	dbt.sign = SIGN_UNSPEC;
	dbt.fptype = FPTYPE_NOTSPEC;

	if (type->u.astructref.ref)
	  dbt.sid = get_member_id (type->u.astructref.ref->number);
	else
	  dbt.sid = 0;

	dbt.neg = 0x1001;
	sysroff_swap_dbt_out (file, &dbt);
      }
      break;

    case coff_array_type:
      {
	struct IT_dar dar;
	int j;
	int dims = 1;		/* Only output one dimension at a time.  */

	dar.dims = dims;
	dar.variable = nints (dims);
	dar.subtype = nints (dims);
	dar.spare = nints (dims);
	dar.max_variable = nints (dims);
	dar.maxspare = nints (dims);
	dar.max = nints (dims);
	dar.min_variable = nints (dims);
	dar.min = nints (dims);
	dar.minspare = nints (dims);
	dar.neg = 0x1001;
	dar.length = type->size / type->u.array.dim;

	for (j = 0; j < dims; j++)
	  {
	    dar.variable[j] = VARIABLE_FIXED;
	    dar.subtype[j] = SUB_INTEGER;
	    dar.spare[j] = 0;
	    dar.max_variable[j] = 0;
	    dar.max[j] = type->u.array.dim;
	    dar.min_variable[j] = 0;
	    dar.min[j] = 1;	/* Why isn't this 0 ? */
	  }
	walk_tree_type_1 (sfile, symbol, type->u.array.array_of, nest + 1);
	sysroff_swap_dar_out (file, &dar);
      }
      break;

    case coff_enumdef_type:
      {
	struct IT_dbt dbt;
	struct IT_den den;
	struct coff_symbol *member;

	dbt.btype = BTYPE_ENUM;
	dbt.bitsize = type->size;
	dbt.sign = SIGN_UNSPEC;
	dbt.fptype = FPTYPE_NOTSPEC;
	dbt.sid = get_member_id (type->u.aenumdef.idx);
	dbt.neg = 0x1001;
	sysroff_swap_dbt_out (file, &dbt);

	den.end = 0;
	den.neg = 0x1001;
	den.spare = 0;
	sysroff_swap_den_out (file, &den);

	for (member = type->u.aenumdef.elements->vars_head;
	     member;
	     member = member->next)
	  walk_tree_symbol (sfile, 0, member, nest + 1);

	den.end = 1;
	sysroff_swap_den_out (file, &den);
      }
      break;

    case coff_enumref_type:
      {
	struct IT_dbt dbt;

	dbt.btype = BTYPE_TAG;
	dbt.bitsize = type->size;
	dbt.sign = SIGN_UNSPEC;
	dbt.fptype = FPTYPE_NOTSPEC;
	dbt.sid = get_member_id (type->u.aenumref.ref->number);
	dbt.neg = 0x1001;
	sysroff_swap_dbt_out (file, &dbt);
      }
      break;

    default:
      abort ();
    }
}

/* Obsolete ?
   static void
   dty_start ()
   {
   struct IT_dty dty;
   dty.end = 0;
   dty.neg = 0x1001;
   dty.spare = 0;
   sysroff_swap_dty_out (file, &dty);
   }

   static void
   dty_stop ()
   {
   struct IT_dty dty;
   dty.end = 0;
   dty.neg = 0x1001;
   dty.end = 1;
   sysroff_swap_dty_out (file, &dty);
   }


   static void
   dump_tree_structure (sfile, symbol, type, nest)
   struct coff_sfile *sfile;
   struct coff_symbol *symbol;
   struct coff_type *type;
   int nest;
   {
   if (symbol->type->type == coff_function_type)
   {


   }

   }
 */

static void
walk_tree_type (struct coff_sfile *sfile, struct coff_symbol *symbol,
		struct coff_type *type, int nest)
{
  if (symbol->type->type == coff_function_type)
    {
      struct IT_dty dty;

      dty.end = 0;
      dty.neg = 0x1001;

      sysroff_swap_dty_out (file, &dty);
      walk_tree_type_1 (sfile, symbol, type, nest);
      dty.end = 1;
      sysroff_swap_dty_out (file, &dty);

      wr_dps_start (sfile,
		    symbol->where->section,
		    symbol->type->u.function.code,
		    BLOCK_TYPE_FUNCTION, nest);
      wr_dps_start (sfile, symbol->where->section,
		    symbol->type->u.function.code,
		    BLOCK_TYPE_BLOCK, nest);
      walk_tree_scope (symbol->where->section,
		       sfile,
		       symbol->type->u.function.code,
		       nest + 1, BLOCK_TYPE_BLOCK);

      wr_dps_end (symbol->where->section,
		  symbol->type->u.function.code,
		  BLOCK_TYPE_BLOCK);
      wr_dps_end (symbol->where->section,
		  symbol->type->u.function.code, BLOCK_TYPE_FUNCTION);
    }
  else
    {
      struct IT_dty dty;

      dty.end = 0;
      dty.neg = 0x1001;
      sysroff_swap_dty_out (file, &dty);
      walk_tree_type_1 (sfile, symbol, type, nest);
      dty.end = 1;
      sysroff_swap_dty_out (file, &dty);
    }
}

static void
walk_tree_symbol (struct coff_sfile *sfile, struct coff_section *section ATTRIBUTE_UNUSED, struct coff_symbol *symbol, int nest)
{
  struct IT_dsy dsy;

  memset (&dsy, 0, sizeof(dsy));
  dsy.nesting = nest;

  switch (symbol->type->type)
    {
    case coff_function_type:
      dsy.type = STYPE_FUNC;
      dsy.assign = 1;
      break;

    case coff_structref_type:
    case coff_pointer_type:
    case coff_array_type:
    case coff_basic_type:
    case coff_enumref_type:
      dsy.type = STYPE_VAR;
      dsy.assign = 1;
      break;

    case coff_enumdef_type:
      dsy.type = STYPE_TAG;
      dsy.assign = 0;
      dsy.magic = 2;
      break;

    case coff_structdef_type:
      dsy.type = STYPE_TAG;
      dsy.assign = 0;
      dsy.magic = symbol->type->u.astructdef.isstruct ? 0 : 1;
      break;

    case coff_secdef_type:
      return;

    default:
      abort ();
    }

  if (symbol->where->where == coff_where_member_of_struct)
    {
      dsy.assign = 0;
      dsy.type = STYPE_MEMBER;
    }

  if (symbol->where->where == coff_where_member_of_enum)
    {
      dsy.type = STYPE_ENUM;
      dsy.assign = 0;
      dsy.evallen = 4;
      dsy.evalue = symbol->where->offset;
    }

  if (symbol->type->type == coff_structdef_type
      || symbol->where->where == coff_where_entag
      || symbol->where->where == coff_where_strtag)
    {
      dsy.snumber = get_member_id (symbol->number);
    }
  else
    {
      dsy.snumber = get_ordinary_id (symbol->number);
    }

  dsy.sname = symbol->name[0] == '_' ? symbol->name + 1 : symbol->name;

  switch (symbol->visible->type)
    {
    case coff_vis_common:
    case coff_vis_ext_def:
      dsy.ainfo = AINFO_STATIC_EXT_DEF;
      break;

    case coff_vis_ext_ref:
      dsy.ainfo = AINFO_STATIC_EXT_REF;
      break;

    case coff_vis_int_def:
      dsy.ainfo = AINFO_STATIC_INT;
      break;

    case coff_vis_auto:
    case coff_vis_autoparam:
      dsy.ainfo = AINFO_AUTO;
      break;

    case coff_vis_register:
    case coff_vis_regparam:
      dsy.ainfo = AINFO_REG;
      break;
      break;

    case coff_vis_tag:
    case coff_vis_member_of_struct:
    case coff_vis_member_of_enum:
      break;

    default:
      abort ();
    }

  dsy.dlength = symbol->type->size;

  switch (symbol->where->where)
    {
    case coff_where_memory:

      dsy.section = symbol->where->section->number;
#ifdef FOOP
      dsy.section = 0;
#endif
      break;

    case coff_where_member_of_struct:
    case coff_where_member_of_enum:
    case coff_where_stack:
    case coff_where_register:
    case coff_where_unknown:
    case coff_where_strtag:
    case coff_where_entag:
    case coff_where_typedef:
      break;

    default:
      abort ();
    }

  switch (symbol->where->where)
    {
    case coff_where_memory:
      dsy.address = symbol->where->offset - find_base (sfile, symbol->where->section);
      break;

    case coff_where_stack:
      dsy.address = symbol->where->offset;
      break;

    case coff_where_member_of_struct:
      if (symbol->where->bitsize)
	{
	  int bits = (symbol->where->offset * 8 + symbol->where->bitoffset);
	  dsy.bitunit = 1;
	  dsy.field_len = symbol->where->bitsize;
	  dsy.field_off = (bits / 32) * 4;
	  dsy.field_bitoff = bits % 32;
	}
      else
	{
	  dsy.bitunit = 0;

	  dsy.field_len = symbol->type->size;
	  dsy.field_off = symbol->where->offset;
	}
      break;

    case coff_where_member_of_enum:
      /*      dsy.bitunit = 0;
         dsy.field_len  = symbol->type->size;
         dsy.field_off = symbol->where->offset; */
      break;

    case coff_where_register:
    case coff_where_unknown:
    case coff_where_strtag:
    case coff_where_entag:
    case coff_where_typedef:
      break;

    default:
      abort ();
    }

  if (symbol->where->where == coff_where_register)
    dsy.reg = rnames[symbol->where->offset];

  switch (symbol->visible->type)
    {
    case coff_vis_common:
      /* We do this 'cause common C symbols are treated as extdefs.  */
    case coff_vis_ext_def:
    case coff_vis_ext_ref:
      dsy.ename = symbol->name;
      break;

    case coff_vis_regparam:
    case coff_vis_autoparam:
      dsy.type = STYPE_PARAMETER;
      break;

    case coff_vis_int_def:
    case coff_vis_auto:
    case coff_vis_register:
    case coff_vis_tag:
    case coff_vis_member_of_struct:
    case coff_vis_member_of_enum:
      break;

    default:
      abort ();
    }

  dsy.sfn = 0;
  dsy.sln = 2;
  dsy.neg = 0x1001;

  sysroff_swap_dsy_out (file, &dsy);

  walk_tree_type (sfile, symbol, symbol->type, nest);
}

static void
walk_tree_scope (struct coff_section *section, struct coff_sfile *sfile, struct coff_scope *scope, int nest, int type)
{
  struct coff_symbol *vars;
  struct coff_scope *child;

  if (scope->vars_head
      || (scope->list_head && scope->list_head->vars_head))
    {
      wr_dps_start (sfile, section, scope, type, nest);

      if (nest == 0)
	wr_globals (tree, sfile, nest + 1);

      for (vars = scope->vars_head; vars; vars = vars->next)
	walk_tree_symbol (sfile, section, vars, nest);

      for (child = scope->list_head; child; child = child->next)
	walk_tree_scope (section, sfile, child, nest + 1, BLOCK_TYPE_BLOCK);

      wr_dps_end (section, scope, type);
    }
}

static void
walk_tree_sfile (struct coff_section *section, struct coff_sfile *sfile)
{
  walk_tree_scope (section, sfile, sfile->scope, 0, BLOCK_TYPE_COMPUNIT);
}

static void
wr_program_structure (struct coff_ofile *p, struct coff_sfile *sfile)
{
  walk_tree_sfile (p->sections + 4, sfile);
}

static void
wr_du (struct coff_ofile *p, struct coff_sfile *sfile, int n)
{
  struct IT_du du;
  int lim;
  int i;
  int j;
  unsigned int *lowest = (unsigned *) nints (p->nsections);
  unsigned int *highest = (unsigned *) nints (p->nsections);

  du.format = bfd_get_file_flags (abfd) & EXEC_P ? 0 : 1;
  du.optimized = 0;
  du.stackfrmt = 0;
  du.spare = 0;
  du.unit = n;
  du.sections = p->nsections - 1;
  du.san = (int *) xcalloc (sizeof (int), du.sections);
  du.address = nints (du.sections);
  du.length = nints (du.sections);

  for (i = 0; i < du.sections; i++)
    {
      lowest[i] = ~0;
      highest[i] = 0;
    }

  lim = du.sections;
  for (j = 0; j < lim; j++)
    {
      int src = j;
      int dst = j;

      du.san[dst] = dst;

      if (sfile->section[src].init)
	{
	  du.length[dst]
	    = sfile->section[src].high - sfile->section[src].low + 1;
	  du.address[dst]
	    = sfile->section[src].low;
	}
      else
	{
	  du.length[dst] = 0;
	  du.address[dst] = 0;
	}

      if (debug)
	{
	  if (sfile->section[src].parent)
	    {
	      printf (" section %6s 0x%08x..0x%08x\n",
		      sfile->section[src].parent->name,
		      du.address[dst],
		      du.address[dst] + du.length[dst] - 1);
	    }
	}

      du.sections = dst + 1;
    }

  du.tool = "c_gcc";
  du.date = DATE;

  sysroff_swap_du_out (file, &du);
}

static void
wr_dus (struct coff_ofile *p ATTRIBUTE_UNUSED, struct coff_sfile *sfile)
{
  struct IT_dus dus;

  dus.efn = 0x1001;
  dus.ns = 1;			/* p->nsources; sac 14 jul 94 */
  dus.drb = nints (dus.ns);
  dus.fname = (char **) xcalloc (sizeof (char *), dus.ns);
  dus.spare = nints (dus.ns);
  dus.ndir = 0;
  /* Find the filenames.  */
  dus.drb[0] = 0;
  dus.fname[0] = sfile->name;

  sysroff_swap_dus_out (file, &dus);

}

/* Find the offset of the .text section for this sfile in the
   .text section for the output file.  */

static int
find_base (struct coff_sfile *sfile, struct coff_section *section)
{
  return sfile->section[section->number].low;
}

static void
wr_dln (struct coff_ofile *p ATTRIBUTE_UNUSED, struct coff_sfile *sfile,
	int n ATTRIBUTE_UNUSED)
{
  /* Count up all the linenumbers */

  struct coff_symbol *sy;
  int lc = 0;
  struct IT_dln dln;

  int idx;

  for (sy = sfile->scope->vars_head;
       sy;
       sy = sy->next)
    {
      struct coff_type *t = sy->type;
      if (t->type == coff_function_type)
	{
	  struct coff_line *l = t->u.function.lines;
	  if (l)
	    lc += l->nlines;
	}
    }

  dln.sfn = nints (lc);
  dln.sln = nints (lc);
  dln.cc = nints (lc);
  dln.section = nints (lc);

  dln.from_address = nints (lc);
  dln.to_address = nints (lc);


  dln.neg = 0x1001;

  dln.nln = lc;

  /* Run through once more and fill up the structure */
  idx = 0;
  for (sy = sfile->scope->vars_head;
       sy;
       sy = sy->next)
    {
      if (sy->type->type == coff_function_type)
	{
	  int i;
	  struct coff_line *l = sy->type->u.function.lines;
	  if (l)
	    {
	      int base = find_base (sfile, sy->where->section);
	      for (i = 0; i < l->nlines; i++)
		{
		  dln.section[idx] = sy->where->section->number;
		  dln.sfn[idx] = 0;
		  dln.sln[idx] = l->lines[i];
		  dln.from_address[idx] =
		    l->addresses[i] + sy->where->section->address - base;
		  dln.cc[idx] = 0;
		  if (idx)
		    dln.to_address[idx - 1] = dln.from_address[idx];
		  idx++;

		}
	      dln.to_address[idx - 1] = dln.from_address[idx - 1] + 2;
	    }
	}
    }
  if (lc)
    sysroff_swap_dln_out (file, &dln);
}

/* Write the global symbols out to the debug info.  */

static void
wr_globals (struct coff_ofile *p, struct coff_sfile *sfile,
	    int n ATTRIBUTE_UNUSED)
{
  struct coff_symbol *sy;

  for (sy = p->symbol_list_head;
       sy;
       sy = sy->next_in_ofile_list)
    {
      if (sy->visible->type == coff_vis_ext_def
	  || sy->visible->type == coff_vis_ext_ref)
	{
	  /* Only write out symbols if they belong to
	     the current source file.  */
	  if (sy->sfile == sfile)
	    walk_tree_symbol (sfile, 0, sy, 0);
	}
    }
}

static void
wr_debug (struct coff_ofile *p)
{
  struct coff_sfile *sfile;
  int n = 0;

  for (sfile = p->source_head;
       sfile;
       sfile = sfile->next)
    {
      if (debug)
	printf ("%s\n", sfile->name);

      wr_du (p, sfile, n);
      wr_dus (p, sfile);
      wr_program_structure (p, sfile);
      wr_dln (p, sfile, n);
      n++;
    }
}

static void
wr_cs (void)
{
  /* It seems that the CS struct is not normal - the size is wrong
     heres one I prepared earlier.  */
  static char b[] =
    {
    0x80,			/* IT */
    0x21,			/* RL */
    0x00,			/* number of chars in variable length part */
    0x80,			/* hd */
    0x00,			/* hs */
    0x80,			/* un */
    0x00,			/* us */
    0x80,			/* sc */
    0x00,			/* ss */
    0x80,			/* er */
    0x80,			/* ed */
    0x80,			/* sh */
    0x80,			/* ob */
    0x80,			/* rl */
    0x80,			/* du */
    0x80,			/* dps */
    0x80,			/* dsy */
    0x80,			/* dty */
    0x80,			/* dln */
    0x80,			/* dso */
    0x80,			/* dus */
    0x00,			/* dss */
    0x80,			/* dbt */
    0x00,			/* dpp */
    0x80,			/* dfp */
    0x80,			/* den */
    0x80,			/* dds */
    0x80,			/* dar */
    0x80,			/* dpt */
    0x00,			/* dul */
    0x00,			/* dse */
    0x00,			/* dot */
    0xDE			/* CS */
  };
  fwrite (b, 1, sizeof (b), file);
}

/* Write out the SC records for a unit.  Create an SC
   for all the sections which appear in the output file, even
   if there isn't an equivalent one on the input.  */

static int
wr_sc (struct coff_ofile *ptr, struct coff_sfile *sfile)
{
  int i;
  int scount = 0;
  /* First work out the total number of sections.  */
  int total_sec = ptr->nsections;
  struct myinfo
    {
      struct coff_section *sec;
      struct coff_symbol *symbol;
    };
  struct coff_symbol *symbol;
  struct myinfo *info
    = (struct myinfo *) calloc (total_sec, sizeof (struct myinfo));


  for (i = 0; i < total_sec; i++)
    {
      info[i].sec = ptr->sections + i;
      info[i].symbol = 0;
    }

  for (symbol = sfile->scope->vars_head;
       symbol;
       symbol = symbol->next)
    {

      if (symbol->type->type == coff_secdef_type)
	{
	  for (i = 0; i < total_sec; i++)
	    {
	      if (symbol->where->section == info[i].sec)
		{
		  info[i].symbol = symbol;
		  break;
		}
	    }
	}
    }

  /* Now output all the section info, and fake up some stuff for sections
     we don't have.  */
  for (i = 1; i < total_sec; i++)
    {
      struct IT_sc sc;
      char *name;

      symbol = info[i].symbol;
      sc.spare = 0;
      sc.spare1 = 0;

      if (!symbol)
	{
	  /* Don't have a symbol set aside for this section, which means
	     that nothing in this file does anything for the section.  */
	  sc.format = !(bfd_get_file_flags (abfd) & EXEC_P);
	  sc.addr = 0;
	  sc.length = 0;
	  name = info[i].sec->name;
	}
      else
	{
	  if (bfd_get_file_flags (abfd) & EXEC_P)
	    {
	      sc.format = 0;
	      sc.addr = symbol->where->offset;
	    }
	  else
	    {
	      sc.format = 1;
	      sc.addr = 0;
	    }
	  sc.length = symbol->type->size;
	  name = symbol->name;
	}

      sc.align = 4;
      sc.concat = CONCAT_SIMPLE;
      sc.read = 3;
      sc.write = 3;
      sc.exec = 3;
      sc.init = 3;
      sc.mode = 3;
      sc.spare = 0;
      sc.segadd = 0;
      sc.spare1 = 0;		/* If not zero, then it doesn't work.  */
      sc.name = section_translate (name);

      if (strlen (sc.name) == 1)
	{
	  switch (sc.name[0])
	    {
	    case 'D':
	    case 'B':
	      sc.contents = CONTENTS_DATA;
	      break;

	    default:
	      sc.contents = CONTENTS_CODE;
	    }
	}
      else
	{
	  sc.contents = CONTENTS_CODE;
	}

      sysroff_swap_sc_out (file, &sc);
      scount++;
    }
  return scount;
}

/* Write out the ER records for a unit.  */

static void
wr_er (struct coff_ofile *ptr, struct coff_sfile *sfile ATTRIBUTE_UNUSED,
       int first)
{
  int idx = 0;
  struct coff_symbol *sym;

  if (first)
    {
      for (sym = ptr->symbol_list_head; sym; sym = sym->next_in_ofile_list)
	{
	  if (sym->visible->type == coff_vis_ext_ref)
	    {
	      struct IT_er er;

	      er.spare = 0;
	      er.type = ER_NOTSPEC;
	      er.name = sym->name;
	      sysroff_swap_er_out (file, &er);
	      sym->er_number = idx++;
	    }
	}
    }
}

/* Write out the ED records for a unit.  */

static void
wr_ed (struct coff_ofile *ptr, struct coff_sfile *sfile ATTRIBUTE_UNUSED,
       int first)
{
  struct coff_symbol *s;

  if (first)
    {
      for (s = ptr->symbol_list_head; s; s = s->next_in_ofile_list)
	{
	  if (s->visible->type == coff_vis_ext_def
	      || s->visible->type == coff_vis_common)
	    {
	      struct IT_ed ed;

	      ed.section = s->where->section->number;
	      ed.spare = 0;

	      if (s->where->section->data)
		{
		  ed.type = ED_TYPE_DATA;
		}
	      else if (s->where->section->code & SEC_CODE)
		{
		  ed.type = ED_TYPE_ENTRY;
		}
	      else
		{
		  ed.type = ED_TYPE_NOTSPEC;
		  ed.type = ED_TYPE_DATA;
		}

	      ed.address = s->where->offset - s->where->section->address;
	      ed.name = s->name;
	      sysroff_swap_ed_out (file, &ed);
	    }
	}
    }
}

static void
wr_unit_info (struct coff_ofile *ptr)
{
  struct coff_sfile *sfile;
  int first = 1;

  for (sfile = ptr->source_head;
       sfile;
       sfile = sfile->next)
    {
      long p1;
      long p2;
      int nsecs;

      p1 = ftell (file);
      wr_un (ptr, sfile, first, 0);
      nsecs = wr_sc (ptr, sfile);
      p2 = ftell (file);
      fseek (file, p1, SEEK_SET);
      wr_un (ptr, sfile, first, nsecs);
      fseek (file, p2, SEEK_SET);
      wr_er (ptr, sfile, first);
      wr_ed (ptr, sfile, first);
      first = 0;
    }
}

static void
wr_module (struct coff_ofile *p)
{
  wr_cs ();
  wr_hd (p);
  wr_unit_info (p);
  wr_object_body (p);
  wr_debug (p);
  wr_tr ();
}

static int
align (int x)
{
  return (x + 3) & ~3;
}

/* Find all the common variables and turn them into
   ordinary defs - dunno why, but thats what hitachi does with 'em.  */

static void
prescan (struct coff_ofile *tree)
{
  struct coff_symbol *s;
  struct coff_section *common_section;

  /* Find the common section - always section 3.  */
  common_section = tree->sections + 3;

  for (s = tree->symbol_list_head;
       s;
       s = s->next_in_ofile_list)
    {
      if (s->visible->type == coff_vis_common)
	{
	  struct coff_where *w = s->where;
	  /*      s->visible->type = coff_vis_ext_def; leave it as common */
	  common_section->size = align (common_section->size);
	  w->offset = common_section->size + common_section->address;
	  w->section = common_section;
	  common_section->size += s->type->size;
	  common_section->size = align (common_section->size);
	}
    }
}

char *program_name;

static void
show_usage (FILE *file, int status)
{
  fprintf (file, _("Usage: %s [option(s)] in-file [out-file]\n"), program_name);
  fprintf (file, _("Convert a COFF object file into a SYSROFF object file\n"));
  fprintf (file, _(" The options are:\n\
  -q --quick       (Obsolete - ignored)\n\
  -n --noprescan   Do not perform a scan to convert commons into defs\n\
  -d --debug       Display information about what is being done\n\
  @<file>          Read options from <file>\n\
  -h --help        Display this information\n\
  -v --version     Print the program's version number\n"));

  if (REPORT_BUGS_TO[0] && status == 0)
    fprintf (file, _("Report bugs to %s\n"), REPORT_BUGS_TO);
  exit (status);
}

int
main (int ac, char **av)
{
  int opt;
  static struct option long_options[] =
  {
    {"debug", no_argument, 0, 'd'},
    {"quick", no_argument, 0, 'q'},
    {"noprescan", no_argument, 0, 'n'},
    {"help", no_argument, 0, 'h'},
    {"version", no_argument, 0, 'V'},
    {NULL, no_argument, 0, 0}
  };
  char **matching;
  char *input_file;
  char *output_file;

#if defined (HAVE_SETLOCALE) && defined (HAVE_LC_MESSAGES)
  setlocale (LC_MESSAGES, "");
#endif
#if defined (HAVE_SETLOCALE)
  setlocale (LC_CTYPE, "");
#endif
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  program_name = av[0];
  xmalloc_set_program_name (program_name);

  expandargv (&ac, &av);

  while ((opt = getopt_long (ac, av, "dHhVvqn", long_options,
			     (int *) NULL))
	 != EOF)
    {
      switch (opt)
	{
	case 'q':
	  quick = 1;
	  break;
	case 'n':
	  noprescan = 1;
	  break;
	case 'd':
	  debug = 1;
	  break;
	case 'H':
	case 'h':
	  show_usage (stdout, 0);
	  /*NOTREACHED */
	case 'v':
	case 'V':
	  print_version ("srconv");
	  exit (0);
	  /*NOTREACHED */
	case 0:
	  break;
	default:
	  show_usage (stderr, 1);
	  /*NOTREACHED */
	}
    }

  /* The input and output files may be named on the command line.  */
  output_file = NULL;
  if (optind < ac)
    {
      input_file = av[optind];
      ++optind;
      if (optind < ac)
	{
	  output_file = av[optind];
	  ++optind;
	  if (optind < ac)
	    show_usage (stderr, 1);
	  if (strcmp (input_file, output_file) == 0)
	    {
	      fatal (_("input and output files must be different"));
	    }
	}
    }
  else
    input_file = 0;

  if (!input_file)
    {
      fatal (_("no input file specified"));
    }

  if (!output_file)
    {
      /* Take a .o off the input file and stick on a .obj.  If
         it doesn't end in .o, then stick a .obj on anyway */

      int len = strlen (input_file);

      output_file = xmalloc (len + 5);
      strcpy (output_file, input_file);

      if (len > 3
	  && output_file[len - 2] == '.'
	  && output_file[len - 1] == 'o')
	{
	  output_file[len] = 'b';
	  output_file[len + 1] = 'j';
	  output_file[len + 2] = 0;
	}
      else
	{
	  strcat (output_file, ".obj");
	}
    }

  abfd = bfd_openr (input_file, 0);

  if (!abfd)
    bfd_fatal (input_file);

  if (!bfd_check_format_matches (abfd, bfd_object, &matching))
    {
      bfd_nonfatal (input_file);

      if (bfd_get_error () == bfd_error_file_ambiguously_recognized)
	{
	  list_matching_formats (matching);
	  free (matching);
	}
      exit (1);
    }

  file = fopen (output_file, FOPEN_WB);

  if (!file)
    fatal (_("unable to open output file %s"), output_file);

  if (debug)
    printf ("ids %d %d\n", base1, base2);

  tree = coff_grok (abfd);

  if (!noprescan)
    prescan (tree);

  wr_module (tree);
  return 0;
}
