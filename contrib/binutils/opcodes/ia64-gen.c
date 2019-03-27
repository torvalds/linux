/* ia64-gen.c -- Generate a shrunk set of opcode tables
   Copyright 1999, 2000, 2001, 2002, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Written by Bob Manson, Cygnus Solutions, <manson@cygnus.com>

   This file is part of GDB, GAS, and the GNU binutils.

   GDB, GAS, and the GNU binutils are free software; you can redistribute
   them and/or modify them under the terms of the GNU General Public
   License as published by the Free Software Foundation; either version
   2, or (at your option) any later version.

   GDB, GAS, and the GNU binutils are distributed in the hope that they
   will be useful, but WITHOUT ANY WARRANTY; without even the implied
   warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
   the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this file; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* While the ia64-opc-* set of opcode tables are easy to maintain,
   they waste a tremendous amount of space.  ia64-gen rearranges the
   instructions into a directed acyclic graph (DAG) of instruction opcodes and 
   their possible completers, as well as compacting the set of strings used.  

   The disassembler table consists of a state machine that does
   branching based on the bits of the opcode being disassembled.  The
   state encodings have been chosen to minimize the amount of space
   required.  

   The resource table is constructed based on some text dependency tables, 
   which are also easier to maintain than the final representation.  */

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "ansidecl.h"
#include "libiberty.h"
#include "safe-ctype.h"
#include "sysdep.h"
#include "getopt.h"
#include "ia64-opc.h"
#include "ia64-opc-a.c"
#include "ia64-opc-i.c"
#include "ia64-opc-m.c"
#include "ia64-opc-b.c"
#include "ia64-opc-f.c"
#include "ia64-opc-x.c"
#include "ia64-opc-d.c"

#include <libintl.h>
#define _(String) gettext (String)

/* This is a copy of fprintf_vma from bfd/bfd-in2.h.  We have to use this
   always, because we might be compiled without BFD64 defined, if configured
   for a 32-bit target and --enable-targets=all is used.  This will work for
   both 32-bit and 64-bit hosts.  */
#define _opcode_int64_low(x) ((unsigned long) (((x) & 0xffffffff)))
#define _opcode_int64_high(x) ((unsigned long) (((x) >> 32) & 0xffffffff))
#define opcode_fprintf_vma(s,x) \
  fprintf ((s), "%08lx%08lx", _opcode_int64_high (x), _opcode_int64_low (x))

const char * program_name = NULL;
int debug = 0;

#define NELEMS(a) (sizeof (a) / sizeof ((a)[0]))
#define tmalloc(X) (X *) xmalloc (sizeof (X))

/* The main opcode table entry.  Each entry is a unique combination of
   name and flags (no two entries in the table compare as being equal
   via opcodes_eq).  */
struct main_entry
{
  /* The base name of this opcode.  The names of its completers are
     appended to it to generate the full instruction name.  */
  struct string_entry *name;
  /* The base opcode entry.  Which one to use is a fairly arbitrary choice;
     it uses the first one passed to add_opcode_entry.  */
  struct ia64_opcode *opcode;
  /* The list of completers that can be applied to this opcode.  */
  struct completer_entry *completers;
  /* Next entry in the chain.  */
  struct main_entry *next;
  /* Index in the  main table.  */
  int main_index;
} *maintable, **ordered_table;

int otlen = 0;
int ottotlen = 0;
int opcode_count = 0;

/* The set of possible completers for an opcode.  */
struct completer_entry
{
  /* This entry's index in the ia64_completer_table[] array.  */
  int num;

  /* The name of the completer.  */
  struct string_entry *name;

  /* This entry's parent.  */
  struct completer_entry *parent;

  /* Set if this is a terminal completer (occurs at the end of an
     opcode).  */
  int is_terminal;

  /* An alternative completer.  */
  struct completer_entry *alternative;

  /* Additional completers that can be appended to this one.  */
  struct completer_entry *addl_entries;

  /* Before compute_completer_bits () is invoked, this contains the actual
     instruction opcode for this combination of opcode and completers.
     Afterwards, it contains those bits that are different from its
     parent opcode.  */
  ia64_insn bits;

  /* Bits set to 1 correspond to those bits in this completer's opcode
     that are different from its parent completer's opcode (or from
     the base opcode if the entry is the root of the opcode's completer
     list).  This field is filled in by compute_completer_bits ().  */
  ia64_insn mask;

  /* Index into the opcode dependency list, or -1 if none.  */
  int dependencies;

  /* Remember the order encountered in the opcode tables.  */
  int order;
};

/* One entry in the disassembler name table.  */
struct disent
{
  /* The index into the ia64_name_dis array for this entry.  */
  int ournum;

  /* The index into the main_table[] array.  */
  int insn;

  /* The disassmbly priority of this entry.  */
  int priority;

  /* The completer_index value for this entry.  */
  int completer_index;

  /* How many other entries share this decode.  */
  int nextcnt;

  /* The next entry sharing the same decode.  */
  struct disent *nexte;

  /* The next entry in the name list.  */
  struct disent *next_ent;
} *disinsntable = NULL;

/* A state machine that will eventually be used to generate the
   disassembler table.  */
struct bittree
{
  struct disent *disent;
  struct bittree *bits[3]; /* 0, 1, and X (don't care).  */
  int bits_to_skip;
  int skip_flag;
} *bittree;

/* The string table contains all opcodes and completers sorted in
   alphabetical order.  */

/* One entry in the string table.  */
struct string_entry 
{
  /* The index in the ia64_strings[] array for this entry.  */
  int num;
  /* And the string.  */
  char *s;
} **string_table = NULL;

int strtablen = 0;
int strtabtotlen = 0;


/* Resource dependency entries.  */
struct rdep
{
  char *name;                       /* Resource name.  */
  unsigned 
    mode:2,                         /* RAW, WAW, or WAR.  */
    semantics:3;                    /* Dependency semantics.  */
  char *extra;                      /* Additional semantics info.  */
  int nchks;                   
  int total_chks;                   /* Total #of terminal insns.  */
  int *chks;                        /* Insn classes which read (RAW), write
                                       (WAW), or write (WAR) this rsrc.  */
  int *chknotes;                    /* Dependency notes for each class.  */
  int nregs;
  int total_regs;                   /* Total #of terminal insns.  */
  int *regs;                        /* Insn class which write (RAW), write2
                                       (WAW), or read (WAR) this rsrc.  */
  int *regnotes;                    /* Dependency notes for each class.  */

  int waw_special;                  /* Special WAW dependency note.  */
} **rdeps = NULL;

static int rdepslen = 0;
static int rdepstotlen = 0;

/* Array of all instruction classes.  */
struct iclass
{ 
  char *name;                       /* Instruction class name.  */
  int is_class;                     /* Is a class, not a terminal.  */
  int nsubs;                        
  int *subs;                        /* Other classes within this class.  */
  int nxsubs;                       
  int xsubs[4];                     /* Exclusions.  */
  char *comment;                    /* Optional comment.  */
  int note;                         /* Optional note.  */
  int terminal_resolved;            /* Did we match this with anything?  */
  int orphan;                       /* Detect class orphans.  */
} **ics = NULL;

static int iclen = 0;
static int ictotlen = 0;

/* An opcode dependency (chk/reg pair of dependency lists).  */
struct opdep
{
  int chk;                          /* index into dlists */
  int reg;                          /* index into dlists */
} **opdeps;

static int opdeplen = 0;
static int opdeptotlen = 0;

/* A generic list of dependencies w/notes encoded.  These may be shared.  */
struct deplist
{
  int len;
  unsigned short *deps;
} **dlists;

static int dlistlen = 0;
static int dlisttotlen = 0;


static void fail (const char *, ...) ATTRIBUTE_PRINTF_1;
static void warn (const char *, ...) ATTRIBUTE_PRINTF_1;
static struct rdep * insert_resource (const char *, enum ia64_dependency_mode);
static int  deplist_equals (struct deplist *, struct deplist *);
static short insert_deplist (int, unsigned short *);
static short insert_dependencies (int, unsigned short *, int, unsigned short *);
static void  mark_used (struct iclass *, int);
static int  fetch_insn_class (const char *, int);
static int  sub_compare (const void *, const void *);
static void load_insn_classes (void);
static void parse_resource_users (const char *, int **, int *, int **);
static int  parse_semantics (char *);
static void add_dep (const char *, const char *, const char *, int, int, char *, int);
static void load_depfile (const char *, enum ia64_dependency_mode);
static void load_dependencies (void);
static int  irf_operand (int, const char *);
static int  in_iclass_mov_x (struct ia64_opcode *, struct iclass *, const char *, const char *);
static int  in_iclass (struct ia64_opcode *, struct iclass *, const char *, const char *, int *);
static int  lookup_regindex (const char *, int);
static int  lookup_specifier (const char *);
static void print_dependency_table (void);
static struct string_entry * insert_string (char *);
static void gen_dis_table (struct bittree *);
static void print_dis_table (void);
static void generate_disassembler (void);
static void print_string_table (void);
static int  completer_entries_eq (struct completer_entry *, struct completer_entry *);
static struct completer_entry * insert_gclist (struct completer_entry *);
static int  get_prefix_len (const char *);
static void compute_completer_bits (struct main_entry *, struct completer_entry *);
static void collapse_redundant_completers (void);
static int  insert_opcode_dependencies (struct ia64_opcode *, struct completer_entry *);
static void insert_completer_entry (struct ia64_opcode *, struct main_entry *, int);
static void print_completer_entry (struct completer_entry *);
static void print_completer_table (void);
static int  opcodes_eq (struct ia64_opcode *, struct ia64_opcode *);
static void add_opcode_entry (struct ia64_opcode *);
static void print_main_table (void);
static void shrink (struct ia64_opcode *);
static void print_version (void);
static void usage (FILE *, int);
static void finish_distable (void);
static void insert_bit_table_ent (struct bittree *, int, ia64_insn, ia64_insn, int, int, int);
static void add_dis_entry (struct bittree *, ia64_insn, ia64_insn, int, struct completer_entry *, int);
static void compact_distree (struct bittree *);
static struct bittree * make_bittree_entry (void);
static struct disent * add_dis_table_ent (struct disent *, int, int, int);


static void
fail (const char *message, ...)
{
  va_list args;
  
  va_start (args, message);
  fprintf (stderr, _("%s: Error: "), program_name);
  vfprintf (stderr, message, args);
  va_end (args);
  xexit (1);
}

static void
warn (const char *message, ...)
{
  va_list args;

  va_start (args, message);

  fprintf (stderr, _("%s: Warning: "), program_name);
  vfprintf (stderr, message, args);
  va_end (args);
}

/* Add NAME to the resource table, where TYPE is RAW or WAW.  */
static struct rdep *
insert_resource (const char *name, enum ia64_dependency_mode type)
{
  if (rdepslen == rdepstotlen)
    {
      rdepstotlen += 20;
      rdeps = (struct rdep **)
        xrealloc (rdeps, sizeof(struct rdep **) * rdepstotlen);
    }
  rdeps[rdepslen] = tmalloc(struct rdep);
  memset((void *)rdeps[rdepslen], 0, sizeof(struct rdep));
  rdeps[rdepslen]->name = xstrdup (name);
  rdeps[rdepslen]->mode = type;
  rdeps[rdepslen]->waw_special = 0;
  
  return rdeps[rdepslen++];
}

/* Are the lists of dependency indexes equivalent?  */
static int
deplist_equals (struct deplist *d1, struct deplist *d2)
{
  int i;

  if (d1->len != d2->len)
    return 0;

  for (i = 0; i < d1->len; i++)
    if (d1->deps[i] != d2->deps[i])
      return 0;

  return 1;
}

/* Add the list of dependencies to the list of dependency lists.  */
static short
insert_deplist (int count, unsigned short *deps)
{
  /* Sort the list, then see if an equivalent list exists already.
     this results in a much smaller set of dependency lists.  */
  struct deplist *list;
  char set[0x10000];
  int i;

  memset ((void *)set, 0, sizeof (set));
  for (i = 0; i < count; i++)
    set[deps[i]] = 1;

  count = 0;
  for (i = 0; i < (int) sizeof (set); i++)
    if (set[i])
      ++count;

  list = tmalloc (struct deplist);
  list->len = count;
  list->deps = (unsigned short *) malloc (sizeof (unsigned short) * count);

  for (i = 0, count = 0; i < (int) sizeof (set); i++)
    if (set[i])
      list->deps[count++] = i;

  /* Does this list exist already?  */
  for (i = 0; i < dlistlen; i++)
    if (deplist_equals (list, dlists[i]))
      {
	free (list->deps);
	free (list);
	return i;
      }

  if (dlistlen == dlisttotlen)
    {
      dlisttotlen += 20;
      dlists = (struct deplist **)
        xrealloc (dlists, sizeof(struct deplist **) * dlisttotlen);
    }
  dlists[dlistlen] = list;

  return dlistlen++;
}

/* Add the given pair of dependency lists to the opcode dependency list.  */
static short
insert_dependencies (int nchks, unsigned short *chks, 
                     int nregs, unsigned short *regs)
{
  struct opdep *pair;
  int i;
  int regind = -1;
  int chkind = -1;

  if (nregs > 0)
    regind = insert_deplist (nregs, regs);
  if (nchks > 0)
    chkind = insert_deplist (nchks, chks);

  for (i = 0; i < opdeplen; i++)
    if (opdeps[i]->chk == chkind 
	&& opdeps[i]->reg == regind)
      return i;

  pair = tmalloc (struct opdep);
  pair->chk = chkind;
  pair->reg = regind;
  
  if (opdeplen == opdeptotlen)
    {
      opdeptotlen += 20;
      opdeps = (struct opdep **)
        xrealloc (opdeps, sizeof(struct opdep **) * opdeptotlen);
    }
  opdeps[opdeplen] = pair;

  return opdeplen++;
}

static void 
mark_used (struct iclass *ic, int clear_terminals)
{
  int i;

  ic->orphan = 0;
  if (clear_terminals)
    ic->terminal_resolved = 1;

  for (i = 0; i < ic->nsubs; i++)
    mark_used (ics[ic->subs[i]], clear_terminals);

  for (i = 0; i < ic->nxsubs; i++)
    mark_used (ics[ic->xsubs[i]], clear_terminals);
}

/* Look up an instruction class; if CREATE make a new one if none found;
   returns the index into the insn class array.  */
static int
fetch_insn_class (const char *full_name, int create)
{
  char *name;
  char *notestr;
  char *xsect;
  char *comment;
  int i, note = 0;
  int ind;
  int is_class = 0;

  if (CONST_STRNEQ (full_name, "IC:"))
    {
      name = xstrdup (full_name + 3);
      is_class = 1;
    }
  else
    name = xstrdup (full_name);

  if ((xsect = strchr(name, '\\')) != NULL)
    is_class = 1;
  if ((comment = strchr(name, '[')) != NULL)
    is_class = 1;
  if ((notestr = strchr(name, '+')) != NULL)
    is_class = 1;

  /* If it is a composite class, then ignore comments and notes that come after
     the '\\', since they don't apply to the part we are decoding now.  */
  if (xsect)
    {
      if (comment > xsect)
	comment = 0;
      if (notestr > xsect)
	notestr = 0;
    }

  if (notestr)
    {
      char *nextnotestr;

      note = atoi (notestr + 1);
      if ((nextnotestr = strchr (notestr + 1, '+')) != NULL)
        {
          if (strcmp (notestr, "+1+13") == 0)
            note = 13;
          else if (!xsect || nextnotestr < xsect)
            warn (_("multiple note %s not handled\n"), notestr);
        }
    }

  /* If it's a composite class, leave the notes and comments in place so that
     we have a unique name for the composite class.  Otherwise, we remove
     them.  */
  if (!xsect)
    {
      if (notestr)
        *notestr = 0;
      if (comment)
        *comment = 0;
    }

  for (i = 0; i < iclen; i++)
    if (strcmp (name, ics[i]->name) == 0
        && ((comment == NULL && ics[i]->comment == NULL)
            || (comment != NULL && ics[i]->comment != NULL
                && strncmp (ics[i]->comment, comment, 
                            strlen (ics[i]->comment)) == 0))
        && note == ics[i]->note)
      return i;

  if (!create)
    return -1;

  /* Doesn't exist, so make a new one.  */
  if (iclen == ictotlen)
    {
      ictotlen += 20;
      ics = (struct iclass **)
        xrealloc (ics, (ictotlen) * sizeof (struct iclass *));
    }

  ind = iclen++;
  ics[ind] = tmalloc (struct iclass);
  memset ((void *)ics[ind], 0, sizeof (struct iclass));
  ics[ind]->name = xstrdup (name);
  ics[ind]->is_class = is_class;
  ics[ind]->orphan = 1;

  if (comment)
    {
      ics[ind]->comment = xstrdup (comment + 1);
      ics[ind]->comment[strlen (ics[ind]->comment)-1] = 0;
    }

  if (notestr)
    ics[ind]->note = note;

  /* If it's a composite class, there's a comment or note, look for an
     existing class or terminal with the same name.  */
  if ((xsect || comment || notestr) && is_class)
    {
      /* First, populate with the class we're based on.  */
      char *subname = name;

      if (xsect)
        *xsect = 0;
      else if (comment)
        *comment = 0;
      else if (notestr)
        *notestr = 0;

      ics[ind]->nsubs = 1;
      ics[ind]->subs = tmalloc(int);
      ics[ind]->subs[0] = fetch_insn_class (subname, 1);;
    }

  while (xsect)
    {
      char *subname = xsect + 1;

      xsect = strchr (subname, '\\');
      if (xsect)
        *xsect = 0;
      ics[ind]->xsubs[ics[ind]->nxsubs] = fetch_insn_class (subname,1);
      ics[ind]->nxsubs++;
    }
  free (name);

  return ind;
}

/* For sorting a class's sub-class list only; make sure classes appear before
   terminals.  */
static int
sub_compare (const void *e1, const void *e2)
{
  struct iclass *ic1 = ics[*(int *)e1];
  struct iclass *ic2 = ics[*(int *)e2];

  if (ic1->is_class)
    {
      if (!ic2->is_class)
        return -1;
    }
  else if (ic2->is_class)
    return 1;

  return strcmp (ic1->name, ic2->name);
}

static void
load_insn_classes (void)
{
  FILE *fp = fopen ("ia64-ic.tbl", "r");
  char buf[2048];

  if (fp == NULL)
    fail (_("can't find ia64-ic.tbl for reading\n"));

  /* Discard first line.  */
  fgets (buf, sizeof(buf), fp);

  while (!feof (fp))
    {
      int iclass;
      char *name;
      char *tmp;
      
      if (fgets (buf, sizeof (buf), fp) == NULL)
        break;
      
      while (ISSPACE (buf[strlen (buf) - 1]))
        buf[strlen (buf) - 1] = '\0';

      name = tmp = buf;
      while (*tmp != ';')
        {
          ++tmp;
          if (tmp == buf + sizeof (buf))
            abort ();
        }
      *tmp++ = '\0';

      iclass = fetch_insn_class (name, 1);
      ics[iclass]->is_class = 1;

      if (strcmp (name, "none") == 0)
        {
          ics[iclass]->is_class = 0;
          ics[iclass]->terminal_resolved = 1;
          continue;
        }

      /* For this class, record all sub-classes.  */
      while (*tmp)
        {
          char *subname;
          int sub;

          while (*tmp && ISSPACE (*tmp))
            {
              ++tmp;
              if (tmp == buf + sizeof (buf))
                abort ();
            }
          subname = tmp;
          while (*tmp && *tmp != ',')
            {
              ++tmp;
              if (tmp == buf + sizeof (buf))
                abort ();
            }
          if (*tmp == ',')
            *tmp++ = '\0';
          
          ics[iclass]->subs = (int *)
            xrealloc ((void *)ics[iclass]->subs, 
		      (ics[iclass]->nsubs + 1) * sizeof (int));

          sub = fetch_insn_class (subname, 1);
          ics[iclass]->subs = (int *)
            xrealloc (ics[iclass]->subs, (ics[iclass]->nsubs + 1) * sizeof (int));
          ics[iclass]->subs[ics[iclass]->nsubs++] = sub;
        }

      /* Make sure classes come before terminals.  */
      qsort ((void *)ics[iclass]->subs, 
             ics[iclass]->nsubs, sizeof(int), sub_compare);
    }
  fclose (fp);

  if (debug)
    printf ("%d classes\n", iclen);
}

/* Extract the insn classes from the given line.  */
static void
parse_resource_users (ref, usersp, nusersp, notesp)
  const char *ref;
  int **usersp;
  int *nusersp;
  int **notesp;
{
  int c;
  char *line = xstrdup (ref);
  char *tmp = line;
  int *users = *usersp;
  int count = *nusersp;
  int *notes = *notesp;

  c = *tmp;
  while (c != 0)
    {
      char *notestr;
      int note;
      char *xsect;
      int iclass;
      int create = 0;
      char *name;
      
      while (ISSPACE (*tmp))
        ++tmp;
      name = tmp;
      while (*tmp && *tmp != ',')
        ++tmp;
      c = *tmp;
      *tmp++ = '\0';
      
      xsect = strchr (name, '\\');
      if ((notestr = strstr (name, "+")) != NULL)
        {
          char *nextnotestr;

          note = atoi (notestr + 1);
          if ((nextnotestr = strchr (notestr + 1, '+')) != NULL)
            {
              /* Note 13 always implies note 1.  */
              if (strcmp (notestr, "+1+13") == 0)
                note = 13;
              else if (!xsect || nextnotestr < xsect)
                warn (_("multiple note %s not handled\n"), notestr);
            }
          if (!xsect)
            *notestr = '\0';
        }
      else 
        note = 0;

      /* All classes are created when the insn class table is parsed;
         Individual instructions might not appear until the dependency tables
         are read.  Only create new classes if it's *not* an insn class,
         or if it's a composite class (which wouldn't necessarily be in the IC
         table).  */
      if (! CONST_STRNEQ (name, "IC:") || xsect != NULL)
        create = 1;
      
      iclass = fetch_insn_class (name, create);
      if (iclass != -1)
        {
          users = (int *)
            xrealloc ((void *) users,(count + 1) * sizeof (int));
          notes = (int *)
            xrealloc ((void *) notes,(count + 1) * sizeof (int));
          notes[count] = note;
          users[count++] = iclass;
          mark_used (ics[iclass], 0);
        }
      else if (debug)
	printf("Class %s not found\n", name);
    }
  /* Update the return values.  */
  *usersp = users;
  *nusersp = count;
  *notesp = notes;

  free (line);
}

static int
parse_semantics (char *sem)
{
  if (strcmp (sem, "none") == 0)
    return IA64_DVS_NONE;
  else if (strcmp (sem, "implied") == 0)
    return IA64_DVS_IMPLIED;
  else if (strcmp (sem, "impliedF") == 0)
    return IA64_DVS_IMPLIEDF;
  else if (strcmp (sem, "data") == 0)
    return IA64_DVS_DATA;
  else if (strcmp (sem, "instr") == 0)
    return IA64_DVS_INSTR;
  else if (strcmp (sem, "specific") == 0)
    return IA64_DVS_SPECIFIC;
  else if (strcmp (sem, "stop") == 0)
    return IA64_DVS_STOP;
  else 
    return IA64_DVS_OTHER;
}

static void
add_dep (const char *name, const char *chk, const char *reg,
         int semantics, int mode, char *extra, int flag)
{
  struct rdep *rs;

  rs = insert_resource (name, mode);

  parse_resource_users (chk, &rs->chks, &rs->nchks, &rs->chknotes);
  parse_resource_users (reg, &rs->regs, &rs->nregs, &rs->regnotes);

  rs->semantics = semantics;
  rs->extra = extra;
  rs->waw_special = flag;
}

static void
load_depfile (const char *filename, enum ia64_dependency_mode mode)
{
  FILE *fp = fopen (filename, "r");
  char buf[1024];

  if (fp == NULL)
    fail (_("can't find %s for reading\n"), filename);

  fgets (buf, sizeof(buf), fp);
  while (!feof (fp))
    {
      char *name, *tmp;
      int semantics;
      char *extra;
      char *regp, *chkp;

      if (fgets (buf, sizeof(buf), fp) == NULL)
        break;

      while (ISSPACE (buf[strlen (buf) - 1]))
        buf[strlen (buf) - 1] = '\0';

      name = tmp = buf;
      while (*tmp != ';')
        ++tmp;
      *tmp++ = '\0';
      
      while (ISSPACE (*tmp))
        ++tmp;
      regp = tmp;
      tmp = strchr (tmp, ';');
      if (!tmp)
        abort ();
      *tmp++ = 0;
      while (ISSPACE (*tmp))
        ++tmp;
      chkp = tmp;
      tmp = strchr (tmp, ';');
      if (!tmp)
        abort ();
      *tmp++ = 0;
      while (ISSPACE (*tmp))
        ++tmp;
      semantics = parse_semantics (tmp);
      extra = semantics == IA64_DVS_OTHER ? xstrdup (tmp) : NULL;

      /* For WAW entries, if the chks and regs differ, we need to enter the
         entries in both positions so that the tables will be parsed properly,
         without a lot of extra work.  */
      if (mode == IA64_DV_WAW && strcmp (regp, chkp) != 0)
        {
          add_dep (name, chkp, regp, semantics, mode, extra, 0);
          add_dep (name, regp, chkp, semantics, mode, extra, 1);
        }
      else
        {
          add_dep (name, chkp, regp, semantics, mode, extra, 0);
        }
    }
  fclose (fp);
}

static void
load_dependencies (void)
{
  load_depfile ("ia64-raw.tbl", IA64_DV_RAW);
  load_depfile ("ia64-waw.tbl", IA64_DV_WAW);
  load_depfile ("ia64-war.tbl", IA64_DV_WAR);

  if (debug)
    printf ("%d RAW/WAW/WAR dependencies\n", rdepslen);
}

/* Is the given operand an indirect register file operand?  */
static int 
irf_operand (int op, const char *field)
{
  if (!field)
    {
      return op == IA64_OPND_RR_R3 || op == IA64_OPND_DBR_R3
        || op == IA64_OPND_IBR_R3  || op == IA64_OPND_PKR_R3
	|| op == IA64_OPND_PMC_R3  || op == IA64_OPND_PMD_R3
	|| op == IA64_OPND_MSR_R3 || op == IA64_OPND_CPUID_R3;
    }
  else
    {
      return ((op == IA64_OPND_RR_R3 && strstr (field, "rr"))
              || (op == IA64_OPND_DBR_R3 && strstr (field, "dbr"))
              || (op == IA64_OPND_IBR_R3 && strstr (field, "ibr"))
              || (op == IA64_OPND_PKR_R3 && strstr (field, "pkr"))
              || (op == IA64_OPND_PMC_R3 && strstr (field, "pmc"))
              || (op == IA64_OPND_PMD_R3 && strstr (field, "pmd"))
              || (op == IA64_OPND_MSR_R3 && strstr (field, "msr"))
              || (op == IA64_OPND_CPUID_R3 && strstr (field, "cpuid")));
    }
}

/* Handle mov_ar, mov_br, mov_cr, mov_indirect, mov_ip, mov_pr, mov_psr, and
   mov_um insn classes.  */
static int
in_iclass_mov_x (struct ia64_opcode *idesc, struct iclass *ic, 
                 const char *format, const char *field)
{
  int plain_mov = strcmp (idesc->name, "mov") == 0;

  if (!format)
    return 0;

  switch (ic->name[4])
    {
    default:
      abort ();
    case 'a':
      {
        int i = strcmp (idesc->name, "mov.i") == 0;
        int m = strcmp (idesc->name, "mov.m") == 0;
        int i2627 = i && idesc->operands[0] == IA64_OPND_AR3;
        int i28 = i && idesc->operands[1] == IA64_OPND_AR3;
        int m2930 = m && idesc->operands[0] == IA64_OPND_AR3;
        int m31 = m && idesc->operands[1] == IA64_OPND_AR3;
        int pseudo0 = plain_mov && idesc->operands[1] == IA64_OPND_AR3;
        int pseudo1 = plain_mov && idesc->operands[0] == IA64_OPND_AR3;

        /* IC:mov ar */
        if (i2627)
          return strstr (format, "I26") || strstr (format, "I27");
        if (i28)
          return strstr (format, "I28") != NULL;
        if (m2930)
          return strstr (format, "M29") || strstr (format, "M30");
        if (m31)
          return strstr (format, "M31") != NULL;
        if (pseudo0 || pseudo1)
          return 1;
      }
      break;
    case 'b':
      {
        int i21 = idesc->operands[0] == IA64_OPND_B1;
        int i22 = plain_mov && idesc->operands[1] == IA64_OPND_B2;
        if (i22)
          return strstr (format, "I22") != NULL;
        if (i21)
          return strstr (format, "I21") != NULL;
      }
      break;
    case 'c':
      {
        int m32 = plain_mov && idesc->operands[0] == IA64_OPND_CR3;
        int m33 = plain_mov && idesc->operands[1] == IA64_OPND_CR3;
        if (m32)
          return strstr (format, "M32") != NULL;
        if (m33)
          return strstr (format, "M33") != NULL;
      }
      break;
    case 'i':
      if (ic->name[5] == 'n')
        {
          int m42 = plain_mov && irf_operand (idesc->operands[0], field);
          int m43 = plain_mov && irf_operand (idesc->operands[1], field);
          if (m42)
            return strstr (format, "M42") != NULL;
          if (m43)
            return strstr (format, "M43") != NULL;
        }
      else if (ic->name[5] == 'p')
        {
          return idesc->operands[1] == IA64_OPND_IP;
        }
      else
        abort ();
      break;
    case 'p':
      if (ic->name[5] == 'r')
        {
          int i25 = plain_mov && idesc->operands[1] == IA64_OPND_PR;
          int i23 = plain_mov && idesc->operands[0] == IA64_OPND_PR;
          int i24 = plain_mov && idesc->operands[0] == IA64_OPND_PR_ROT;
          if (i23)
            return strstr (format, "I23") != NULL;
          if (i24)
            return strstr (format, "I24") != NULL;
          if (i25)
            return strstr (format, "I25") != NULL;
        }
      else if (ic->name[5] == 's')
        {
          int m35 = plain_mov && idesc->operands[0] == IA64_OPND_PSR_L;
          int m36 = plain_mov && idesc->operands[1] == IA64_OPND_PSR;
          if (m35)
            return strstr (format, "M35") != NULL;
          if (m36)
            return strstr (format, "M36") != NULL;
        }
      else
        abort ();
      break;
    case 'u':
      {
        int m35 = plain_mov && idesc->operands[0] == IA64_OPND_PSR_UM;
        int m36 = plain_mov && idesc->operands[1] == IA64_OPND_PSR_UM;
        if (m35)
          return strstr (format, "M35") != NULL;
        if (m36)
          return strstr (format, "M36") != NULL;
      }
      break;
    }
  return 0;
}

/* Is the given opcode in the given insn class?  */
static int
in_iclass (struct ia64_opcode *idesc, struct iclass *ic, 
	   const char *format, const char *field, int *notep)
{
  int i;
  int resolved = 0;

  if (ic->comment)
    {
      if (CONST_STRNEQ (ic->comment, "Format"))
        {
          /* Assume that the first format seen is the most restrictive, and
             only keep a later one if it looks like it's more restrictive.  */
          if (format)
            {
              if (strlen (ic->comment) < strlen (format))
                {
                  warn (_("most recent format '%s'\nappears more restrictive than '%s'\n"),
			ic->comment, format);
                  format = ic->comment; 
                }
            }
          else
            format = ic->comment;
        }
      else if (CONST_STRNEQ (ic->comment, "Field"))
        {
          if (field)
            warn (_("overlapping field %s->%s\n"),
		  ic->comment, field);
          field = ic->comment;
        }
    }

  /* An insn class matches anything that is the same followed by completers,
     except when the absence and presence of completers constitutes different
     instructions.  */
  if (ic->nsubs == 0 && ic->nxsubs == 0)
    {
      int is_mov = CONST_STRNEQ (idesc->name, "mov");
      int plain_mov = strcmp (idesc->name, "mov") == 0;
      int len = strlen(ic->name);

      resolved = ((strncmp (ic->name, idesc->name, len) == 0)
                  && (idesc->name[len] == '\0' 
                      || idesc->name[len] == '.'));

      /* All break, nop, and hint variations must match exactly.  */
      if (resolved &&
          (strcmp (ic->name, "break") == 0
           || strcmp (ic->name, "nop") == 0
	   || strcmp (ic->name, "hint") == 0))
        resolved = strcmp (ic->name, idesc->name) == 0;

      /* Assume restrictions in the FORMAT/FIELD negate resolution,
         unless specifically allowed by clauses in this block.  */
      if (resolved && field)
        {
          /* Check Field(sf)==sN against opcode sN.  */
          if (strstr(field, "(sf)==") != NULL)
            {
              char *sf;

              if ((sf = strstr (idesc->name, ".s")) != 0)
		resolved = strcmp (sf + 1, strstr (field, "==") + 2) == 0;
            }
          /* Check Field(lftype)==XXX.  */
          else if (strstr (field, "(lftype)") != NULL)
            {
              if (strstr (idesc->name, "fault") != NULL)
                resolved = strstr (field, "fault") != NULL;
              else
                resolved = strstr (field, "fault") == NULL;
            }
          /* Handle Field(ctype)==XXX.  */
          else if (strstr (field, "(ctype)") != NULL)
            {
              if (strstr (idesc->name, "or.andcm"))
                resolved = strstr (field, "or.andcm") != NULL;
              else if (strstr (idesc->name, "and.orcm"))
                resolved = strstr (field, "and.orcm") != NULL;
              else if (strstr (idesc->name, "orcm"))
                resolved = strstr (field, "or orcm") != NULL;
              else if (strstr (idesc->name, "or"))
                resolved = strstr (field, "or orcm") != NULL;
              else if (strstr (idesc->name, "andcm"))
                resolved = strstr (field, "and andcm") != NULL;
              else if (strstr (idesc->name, "and"))
                resolved = strstr (field, "and andcm") != NULL;
              else if (strstr (idesc->name, "unc"))
                resolved = strstr (field, "unc") != NULL;
              else
                resolved = strcmp (field, "Field(ctype)==") == 0;
            }
        }

      if (resolved && format)
        {
          if (CONST_STRNEQ (idesc->name, "dep")
                   && strstr (format, "I13") != NULL)
            resolved = idesc->operands[1] == IA64_OPND_IMM8;
          else if (CONST_STRNEQ (idesc->name, "chk")
                   && strstr (format, "M21") != NULL)
            resolved = idesc->operands[0] == IA64_OPND_F2;
          else if (CONST_STRNEQ (idesc->name, "lfetch"))
            resolved = (strstr (format, "M14 M15") != NULL
                        && (idesc->operands[1] == IA64_OPND_R2
                            || idesc->operands[1] == IA64_OPND_IMM9b));
          else if (CONST_STRNEQ (idesc->name, "br.call")
                   && strstr (format, "B5") != NULL)
            resolved = idesc->operands[1] == IA64_OPND_B2;
          else if (CONST_STRNEQ (idesc->name, "br.call")
                   && strstr (format, "B3") != NULL)
            resolved = idesc->operands[1] == IA64_OPND_TGT25c;
          else if (CONST_STRNEQ (idesc->name, "brp")
                   && strstr (format, "B7") != NULL)
            resolved = idesc->operands[0] == IA64_OPND_B2;
          else if (strcmp (ic->name, "invala") == 0)
            resolved = strcmp (idesc->name, ic->name) == 0;
	  else if (CONST_STRNEQ (idesc->name, "st")
		   && (strstr (format, "M5") != NULL
		       || strstr (format, "M10") != NULL))
	    resolved = idesc->flags & IA64_OPCODE_POSTINC;
	  else if (CONST_STRNEQ (idesc->name, "ld")
		   && (strstr (format, "M2 M3") != NULL
		       || strstr (format, "M12") != NULL
		       || strstr (format, "M7 M8") != NULL))
	    resolved = idesc->flags & IA64_OPCODE_POSTINC;
          else
            resolved = 0;
        }

      /* Misc brl variations ('.cond' is optional); 
         plain brl matches brl.cond.  */
      if (!resolved
          && (strcmp (idesc->name, "brl") == 0
              || CONST_STRNEQ (idesc->name, "brl."))
          && strcmp (ic->name, "brl.cond") == 0)
        {
          resolved = 1;
        }

      /* Misc br variations ('.cond' is optional).  */
      if (!resolved 
          && (strcmp (idesc->name, "br") == 0
              || CONST_STRNEQ (idesc->name, "br."))
          && strcmp (ic->name, "br.cond") == 0)
        {
          if (format)
            resolved = (strstr (format, "B4") != NULL
                        && idesc->operands[0] == IA64_OPND_B2)
              || (strstr (format, "B1") != NULL
                  && idesc->operands[0] == IA64_OPND_TGT25c);
          else
            resolved = 1;
        }

      /* probe variations.  */
      if (!resolved && CONST_STRNEQ (idesc->name, "probe"))
        {
          resolved = strcmp (ic->name, "probe") == 0 
            && !((strstr (idesc->name, "fault") != NULL) 
                 ^ (format && strstr (format, "M40") != NULL));
        }

      /* mov variations.  */
      if (!resolved && is_mov)
        {
          if (plain_mov)
            {
              /* mov alias for fmerge.  */
              if (strcmp (ic->name, "fmerge") == 0)
                {
                  resolved = idesc->operands[0] == IA64_OPND_F1
                    && idesc->operands[1] == IA64_OPND_F3;
                }
              /* mov alias for adds (r3 or imm14).  */
              else if (strcmp (ic->name, "adds") == 0)
                {
                  resolved = (idesc->operands[0] == IA64_OPND_R1
                              && (idesc->operands[1] == IA64_OPND_R3
                                  || (idesc->operands[1] == IA64_OPND_IMM14)));
                }
              /* mov alias for addl.  */
              else if (strcmp (ic->name, "addl") == 0)
                {
                  resolved = idesc->operands[0] == IA64_OPND_R1
                    && idesc->operands[1] == IA64_OPND_IMM22;
                }
            }

          /* Some variants of mov and mov.[im].  */
          if (!resolved && CONST_STRNEQ (ic->name, "mov_"))
	    resolved = in_iclass_mov_x (idesc, ic, format, field);
        }

      /* Keep track of this so we can flag any insn classes which aren't 
         mapped onto at least one real insn.  */
      if (resolved)
	ic->terminal_resolved = 1;
    }
  else for (i = 0; i < ic->nsubs; i++)
    {
      if (in_iclass (idesc, ics[ic->subs[i]], format, field, notep))
        {
          int j;

          for (j = 0; j < ic->nxsubs; j++)
	    if (in_iclass (idesc, ics[ic->xsubs[j]], NULL, NULL, NULL))
	      return 0;

          if (debug > 1)
            printf ("%s is in IC %s\n", idesc->name, ic->name);

          resolved = 1;
          break;
        }
    }
  
  /* If it's in this IC, add the IC note (if any) to the insn.  */
  if (resolved)
    {
      if (ic->note && notep)
        {
          if (*notep && *notep != ic->note)
	    warn (_("overwriting note %d with note %d (IC:%s)\n"),
		  *notep, ic->note, ic->name);

          *notep = ic->note;
        }
    }

  return resolved;
}


static int
lookup_regindex (const char *name, int specifier)
{
  switch (specifier)
    {
    case IA64_RS_ARX:
      if (strstr (name, "[RSC]"))
        return 16;
      if (strstr (name, "[BSP]"))
        return 17;
      else if (strstr (name, "[BSPSTORE]"))
        return 18;
      else if (strstr (name, "[RNAT]"))
        return 19;
      else if (strstr (name, "[FCR]"))
        return 21;
      else if (strstr (name, "[EFLAG]"))
        return 24;
      else if (strstr (name, "[CSD]"))
        return 25;
      else if (strstr (name, "[SSD]"))
        return 26;
      else if (strstr (name, "[CFLG]"))
        return 27;
      else if (strstr (name, "[FSR]"))
        return 28;
      else if (strstr (name, "[FIR]"))
        return 29;
      else if (strstr (name, "[FDR]"))
        return 30;
      else if (strstr (name, "[CCV]"))
        return 32;
      else if (strstr (name, "[ITC]"))
        return 44;
      else if (strstr (name, "[PFS]"))
        return 64;
      else if (strstr (name, "[LC]"))
        return 65;
      else if (strstr (name, "[EC]"))
        return 66;
      abort ();
    case IA64_RS_CRX:
      if (strstr (name, "[DCR]"))
        return 0;
      else if (strstr (name, "[ITM]"))
        return 1;
      else if (strstr (name, "[IVA]"))
        return 2;
      else if (strstr (name, "[PTA]"))
        return 8;
      else if (strstr (name, "[GPTA]"))
        return 9;
      else if (strstr (name, "[IPSR]"))
        return 16;
      else if (strstr (name, "[ISR]"))
        return 17;
      else if (strstr (name, "[IIP]"))
        return 19;
      else if (strstr (name, "[IFA]"))
        return 20;
      else if (strstr (name, "[ITIR]"))
        return 21;
      else if (strstr (name, "[IIPA]"))
        return 22;
      else if (strstr (name, "[IFS]"))
        return 23;
      else if (strstr (name, "[IIM]"))
        return 24;
      else if (strstr (name, "[IHA]"))
        return 25;
      else if (strstr (name, "[LID]"))
        return 64;
      else if (strstr (name, "[IVR]"))
        return 65;
      else if (strstr (name, "[TPR]"))
        return 66;
      else if (strstr (name, "[EOI]"))
        return 67;
      else if (strstr (name, "[ITV]"))
        return 72;
      else if (strstr (name, "[PMV]"))
        return 73;
      else if (strstr (name, "[CMCV]"))
        return 74;
      abort ();
    case IA64_RS_PSR:
      if (strstr (name, ".be"))
        return 1;
      else if (strstr (name, ".up"))
        return 2;
      else if (strstr (name, ".ac"))
        return 3;
      else if (strstr (name, ".mfl"))
        return 4;
      else if (strstr (name, ".mfh"))
        return 5;
      else if (strstr (name, ".ic"))
        return 13;
      else if (strstr (name, ".i"))
        return 14;
      else if (strstr (name, ".pk"))
        return 15;
      else if (strstr (name, ".dt"))
        return 17;
      else if (strstr (name, ".dfl"))
        return 18;
      else if (strstr (name, ".dfh"))
        return 19;
      else if (strstr (name, ".sp"))
        return 20;
      else if (strstr (name, ".pp"))
        return 21;
      else if (strstr (name, ".di"))
        return 22;
      else if (strstr (name, ".si"))
        return 23;
      else if (strstr (name, ".db"))
        return 24;
      else if (strstr (name, ".lp"))
        return 25;
      else if (strstr (name, ".tb"))
        return 26;
      else if (strstr (name, ".rt"))
        return 27;
      else if (strstr (name, ".cpl"))
        return 32;
      else if (strstr (name, ".rs"))
        return 34;
      else if (strstr (name, ".mc"))
        return 35;
      else if (strstr (name, ".it"))
        return 36;
      else if (strstr (name, ".id"))
        return 37;
      else if (strstr (name, ".da"))
        return 38;
      else if (strstr (name, ".dd"))
        return 39;
      else if (strstr (name, ".ss"))
        return 40;
      else if (strstr (name, ".ri"))
        return 41;
      else if (strstr (name, ".ed"))
        return 43;
      else if (strstr (name, ".bn"))
        return 44;
      else if (strstr (name, ".ia"))
        return 45;
      else if (strstr (name, ".vm"))
        return 46;
      else
        abort ();
    default:
      break;
    }
  return REG_NONE;
}

static int
lookup_specifier (const char *name)
{
  if (strchr (name, '%'))
    {
      if (strstr (name, "AR[K%]") != NULL)
        return IA64_RS_AR_K;
      if (strstr (name, "AR[UNAT]") != NULL)
        return IA64_RS_AR_UNAT;
      if (strstr (name, "AR%, % in 8") != NULL)
        return IA64_RS_AR;
      if (strstr (name, "AR%, % in 48") != NULL)
        return IA64_RS_ARb;
      if (strstr (name, "BR%") != NULL)
        return IA64_RS_BR;
      if (strstr (name, "CR[IRR%]") != NULL)
        return IA64_RS_CR_IRR;
      if (strstr (name, "CR[LRR%]") != NULL)
        return IA64_RS_CR_LRR;
      if (strstr (name, "CR%") != NULL)
        return IA64_RS_CR;
      if (strstr (name, "FR%, % in 0") != NULL)
        return IA64_RS_FR;
      if (strstr (name, "FR%, % in 2") != NULL)
        return IA64_RS_FRb;
      if (strstr (name, "GR%") != NULL)
        return IA64_RS_GR;
      if (strstr (name, "PR%, % in 1 ") != NULL)
        return IA64_RS_PR;
      if (strstr (name, "PR%, % in 16 ") != NULL)
	return IA64_RS_PRr;

      warn (_("don't know how to specify %% dependency %s\n"),
	    name);
    }
  else if (strchr (name, '#'))
    {
      if (strstr (name, "CPUID#") != NULL)
        return IA64_RS_CPUID;
      if (strstr (name, "DBR#") != NULL)
        return IA64_RS_DBR;
      if (strstr (name, "IBR#") != NULL)
        return IA64_RS_IBR;
      if (strstr (name, "MSR#") != NULL)
	return IA64_RS_MSR;
      if (strstr (name, "PKR#") != NULL)
        return IA64_RS_PKR;
      if (strstr (name, "PMC#") != NULL)
        return IA64_RS_PMC;
      if (strstr (name, "PMD#") != NULL)
        return IA64_RS_PMD;
      if (strstr (name, "RR#") != NULL)
        return IA64_RS_RR;
      
      warn (_("Don't know how to specify # dependency %s\n"),
	    name);
    }
  else if (CONST_STRNEQ (name, "AR[FPSR]"))
    return IA64_RS_AR_FPSR;
  else if (CONST_STRNEQ (name, "AR["))
    return IA64_RS_ARX;
  else if (CONST_STRNEQ (name, "CR["))
    return IA64_RS_CRX;
  else if (CONST_STRNEQ (name, "PSR."))
    return IA64_RS_PSR;
  else if (strcmp (name, "InService*") == 0)
    return IA64_RS_INSERVICE;
  else if (strcmp (name, "GR0") == 0)
    return IA64_RS_GR0;
  else if (strcmp (name, "CFM") == 0)
    return IA64_RS_CFM;
  else if (strcmp (name, "PR63") == 0)
    return IA64_RS_PR63;
  else if (strcmp (name, "RSE") == 0)
    return IA64_RS_RSE;

  return IA64_RS_ANY;
}

static void
print_dependency_table ()
{
  int i, j;

  if (debug) 
    {
      for (i=0;i < iclen;i++)
        {
          if (ics[i]->is_class)
            {
              if (!ics[i]->nsubs)
                {
                  if (ics[i]->comment)
		    warn (_("IC:%s [%s] has no terminals or sub-classes\n"),
			  ics[i]->name, ics[i]->comment);
		  else
		    warn (_("IC:%s has no terminals or sub-classes\n"),
			  ics[i]->name);
                }
            }
          else 
            {
              if (!ics[i]->terminal_resolved && !ics[i]->orphan)
                {
                  if (ics[i]->comment)
		    warn (_("no insns mapped directly to terminal IC %s [%s]"),
			  ics[i]->name, ics[i]->comment);
		  else
		    warn (_("no insns mapped directly to terminal IC %s\n"),
			  ics[i]->name);
                }
            }
        }

      for (i = 0; i < iclen; i++)
        {
          if (ics[i]->orphan)
            {
              mark_used (ics[i], 1);
              warn (_("class %s is defined but not used\n"),
		    ics[i]->name);
            }
        }

      if (debug > 1)
	for (i = 0; i < rdepslen; i++)
	  {  
	    static const char *mode_str[] = { "RAW", "WAW", "WAR" };

	    if (rdeps[i]->total_chks == 0)
	      warn (_("Warning: rsrc %s (%s) has no chks%s\n"), 
		    rdeps[i]->name, mode_str[rdeps[i]->mode],
		    rdeps[i]->total_regs ? "" : " or regs");
	    else if (rdeps[i]->total_regs == 0)
	      warn (_("rsrc %s (%s) has no regs\n"),
		    rdeps[i]->name, mode_str[rdeps[i]->mode]);
	  }
    }

  /* The dependencies themselves.  */
  printf ("static const struct ia64_dependency\ndependencies[] = {\n");
  for (i = 0; i < rdepslen; i++)
    {
      /* '%', '#', AR[], CR[], or PSR. indicates we need to specify the actual
         resource used.  */ 
      int specifier = lookup_specifier (rdeps[i]->name);
      int regindex = lookup_regindex (rdeps[i]->name, specifier);

      printf ("  { \"%s\", %d, %d, %d, %d, ",
              rdeps[i]->name, specifier,
              (int)rdeps[i]->mode, (int)rdeps[i]->semantics, regindex);
      if (rdeps[i]->semantics == IA64_DVS_OTHER)
	{
	  const char *quote, *rest;

	  putchar ('\"');
	  rest = rdeps[i]->extra;
	  quote = strchr (rest, '\"');
	  while (quote != NULL)
	    {
	      printf ("%.*s\\\"", (int) (quote - rest), rest);
	      rest = quote + 1;
	      quote = strchr (rest, '\"');
	    }
	  printf ("%s\", ", rest);
	}
      else
	printf ("NULL, ");
      printf("},\n");
    }
  printf ("};\n\n");

  /* And dependency lists.  */
  for (i=0;i < dlistlen;i++)
    {
      int len = 2;
      printf ("static const unsigned short dep%d[] = {\n  ", i);
      for (j=0;j < dlists[i]->len; j++)
        {
          len += printf ("%d, ", dlists[i]->deps[j]);
          if (len > 75)
            {
              printf("\n  ");
              len = 2;
            }
        }
      printf ("\n};\n\n");
    }

  /* And opcode dependency list.  */
  printf ("#define NELS(X) (sizeof(X)/sizeof(X[0]))\n");
  printf ("static const struct ia64_opcode_dependency\n");
  printf ("op_dependencies[] = {\n");
  for (i = 0; i < opdeplen; i++)
    {
      printf ("  { ");
      if (opdeps[i]->chk == -1)
        printf ("0, NULL, ");
      else 
        printf ("NELS(dep%d), dep%d, ", opdeps[i]->chk, opdeps[i]->chk);
      if (opdeps[i]->reg == -1)
        printf ("0, NULL, ");
      else 
        printf ("NELS(dep%d), dep%d, ", opdeps[i]->reg, opdeps[i]->reg);
      printf ("},\n");
    }
  printf ("};\n\n");
}


/* Add STR to the string table.  */
static struct string_entry *
insert_string (char *str)
{
  int start = 0, end = strtablen;
  int i, x;

  if (strtablen == strtabtotlen)
    {
      strtabtotlen += 20;
      string_table = (struct string_entry **)
	xrealloc (string_table, 
		  sizeof (struct string_entry **) * strtabtotlen);
    }

  if (strtablen == 0)
    {
      strtablen = 1;
      string_table[0] = tmalloc (struct string_entry);
      string_table[0]->s = xstrdup (str);
      string_table[0]->num = 0;
      return string_table[0];
    }

  if (strcmp (str, string_table[strtablen - 1]->s) > 0)
    i = end;
  else if (strcmp (str, string_table[0]->s) < 0)
    i = 0;
  else
    {
      while (1)
	{
	  int c;

	  i = (start + end) / 2;
	  c = strcmp (str, string_table[i]->s);

	  if (c < 0)
	    end = i - 1;
	  else if (c == 0)
	    return string_table[i];
	  else
	    start = i + 1;

	  if (start > end)
	    break;
	}
    }

  for (; i > 0 && i < strtablen; i--)
    if (strcmp (str, string_table[i - 1]->s) > 0)
      break;

  for (; i < strtablen; i++)
    if (strcmp (str, string_table[i]->s) < 0)
      break;

  for (x = strtablen - 1; x >= i; x--)
    {
      string_table[x + 1] = string_table[x];
      string_table[x + 1]->num = x + 1;
    }

  string_table[i] = tmalloc (struct string_entry);
  string_table[i]->s = xstrdup (str);
  string_table[i]->num = i;
  strtablen++;

  return string_table[i];
}

static struct bittree *
make_bittree_entry (void)
{
  struct bittree *res = tmalloc (struct bittree);

  res->disent = NULL;
  res->bits[0] = NULL;
  res->bits[1] = NULL;
  res->bits[2] = NULL;
  res->skip_flag = 0;
  res->bits_to_skip = 0;
  return res;
}
 

static struct disent *
add_dis_table_ent (which, insn, order, completer_index)
     struct disent *which;
     int insn;
     int order;
     int completer_index;
{
  int ci = 0;
  struct disent *ent;

  if (which != NULL)
    {
      ent = which;

      ent->nextcnt++;
      while (ent->nexte != NULL)
	ent = ent->nexte;

      ent = (ent->nexte = tmalloc (struct disent));
    }
  else
    {
      ent = tmalloc (struct disent);
      ent->next_ent = disinsntable;
      disinsntable = ent;
      which = ent;
    }
  ent->nextcnt = 0;
  ent->nexte = NULL;
  ent->insn = insn;
  ent->priority = order;

  while (completer_index != 1)
    {
      ci = (ci << 1) | (completer_index & 1);
      completer_index >>= 1;
    }
  ent->completer_index = ci;
  return which;
}

static void
finish_distable ()
{
  struct disent *ent = disinsntable;
  struct disent *prev = ent;

  ent->ournum = 32768;
  while ((ent = ent->next_ent) != NULL)
    {
      ent->ournum = prev->ournum + prev->nextcnt + 1;
      prev = ent;
    }
}

static void
insert_bit_table_ent (curr_ent, bit, opcode, mask, 
                      opcodenum, order, completer_index)
     struct bittree *curr_ent;
     int bit;
     ia64_insn opcode; 
     ia64_insn mask;
     int opcodenum;
     int order;
     int completer_index;
{
  ia64_insn m;
  int b;
  struct bittree *next;

  if (bit == -1)
    {
      struct disent *nent = add_dis_table_ent (curr_ent->disent, 
                                               opcodenum, order,
					       completer_index);
      curr_ent->disent = nent;
      return;
    }

  m = ((ia64_insn) 1) << bit;

  if (mask & m)
    b = (opcode & m) ? 1 : 0;
  else
    b = 2;

  next = curr_ent->bits[b];
  if (next == NULL)
    {
      next = make_bittree_entry ();
      curr_ent->bits[b] = next;
    }
  insert_bit_table_ent (next, bit - 1, opcode, mask, opcodenum, order,
			completer_index);
}

static void
add_dis_entry (first, opcode, mask, opcodenum, ent, completer_index)
     struct bittree *first;
     ia64_insn opcode;
     ia64_insn mask;
     int opcodenum;
     struct completer_entry *ent;
     int completer_index;
{
  if (completer_index & (1 << 20))
    abort ();

  while (ent != NULL)
    {
      ia64_insn newopcode = (opcode & (~ ent->mask)) | ent->bits;
      add_dis_entry (first, newopcode, mask, opcodenum, ent->addl_entries,
		     (completer_index << 1) | 1);

      if (ent->is_terminal)
	{
	  insert_bit_table_ent (bittree, 40, newopcode, mask, 
                                opcodenum, opcode_count - ent->order - 1, 
				(completer_index << 1) | 1);
	}
      completer_index <<= 1;
      ent = ent->alternative;
    }
}

/* This optimization pass combines multiple "don't care" nodes.  */
static void
compact_distree (ent)
     struct bittree *ent;
{
#define IS_SKIP(ent) \
    ((ent->bits[2] !=NULL) \
     && (ent->bits[0] == NULL && ent->bits[1] == NULL && ent->skip_flag == 0))

  int bitcnt = 0;
  struct bittree *nent = ent;
  int x;

  while (IS_SKIP (nent))
    {
      bitcnt++;
      nent = nent->bits[2];
    }

  if (bitcnt)
    {
      struct bittree *next = ent->bits[2];

      ent->bits[0] = nent->bits[0];
      ent->bits[1] = nent->bits[1];
      ent->bits[2] = nent->bits[2];
      ent->disent = nent->disent;
      ent->skip_flag = 1;
      ent->bits_to_skip = bitcnt;
      while (next != nent)
	{
	  struct bittree *b = next;
	  next = next->bits[2];
	  free (b);
	}
      free (nent);
    }

  for (x = 0; x < 3; x++)
    {
      struct bittree *i = ent->bits[x];

      if (i != NULL)
	compact_distree (i);
    }
}

static unsigned char *insn_list;
static int insn_list_len = 0;
static int tot_insn_list_len = 0;

/* Generate the disassembler state machine corresponding to the tree
   in ENT.  */
static void
gen_dis_table (ent)
     struct bittree *ent;
{
  int x;
  int our_offset = insn_list_len;
  int bitsused = 5;
  int totbits = bitsused;
  int needed_bytes;
  int zero_count = 0;
  int zero_dest = 0;	/* Initialize this with 0 to keep gcc quiet...  */

  /* If this is a terminal entry, there's no point in skipping any
     bits.  */
  if (ent->skip_flag && ent->bits[0] == NULL && ent->bits[1] == NULL &&
      ent->bits[2] == NULL)
    {
      if (ent->disent == NULL)
	abort ();
      else
	ent->skip_flag = 0;
    }

  /* Calculate the amount of space needed for this entry, or at least
     a conservatively large approximation.  */
  if (ent->skip_flag)
    totbits += 5;

  for (x = 1; x < 3; x++)
    if (ent->bits[x] != NULL)
      totbits += 16;

  if (ent->disent != NULL)
    {
      if (ent->bits[2] != NULL)
	abort ();

      totbits += 16;
    }

  /* Now allocate the space.  */
  needed_bytes = (totbits + 7) / 8;
  if ((needed_bytes + insn_list_len) > tot_insn_list_len)
    {
      tot_insn_list_len += 256;
      insn_list = (unsigned char *) xrealloc (insn_list, tot_insn_list_len);
    }
  our_offset = insn_list_len;
  insn_list_len += needed_bytes;
  memset (insn_list + our_offset, 0, needed_bytes);

  /* Encode the skip entry by setting bit 6 set in the state op field,
     and store the # of bits to skip immediately after.  */
  if (ent->skip_flag)
    {
      bitsused += 5;
      insn_list[our_offset + 0] |= 0x40 | ((ent->bits_to_skip >> 2) & 0xf);
      insn_list[our_offset + 1] |= ((ent->bits_to_skip & 3) << 6);
    }

#define IS_ONLY_IFZERO(ENT) \
  ((ENT)->bits[0] != NULL && (ENT)->bits[1] == NULL && (ENT)->bits[2] == NULL \
   && (ENT)->disent == NULL && (ENT)->skip_flag == 0)

  /* Store an "if (bit is zero)" instruction by setting bit 7 in the
     state op field.  */
  if (ent->bits[0] != NULL)
    {
      struct bittree *nent = ent->bits[0];
      zero_count = 0;

      insn_list[our_offset] |= 0x80;

      /* We can encode sequences of multiple "if (bit is zero)" tests
	 by storing the # of zero bits to check in the lower 3 bits of
	 the instruction.  However, this only applies if the state
	 solely tests for a zero bit.  */

      if (IS_ONLY_IFZERO (ent))
	{
	  while (IS_ONLY_IFZERO (nent) && zero_count < 7)
	    {
	      nent = nent->bits[0];
	      zero_count++;
	    }

	  insn_list[our_offset + 0] |= zero_count;
	}
      zero_dest = insn_list_len;
      gen_dis_table (nent);
    }

  /* Now store the remaining tests.  We also handle a sole "termination
     entry" by storing it as an "any bit" test.  */

  for (x = 1; x < 3; x++)
    {
      if (ent->bits[x] != NULL || (x == 2 && ent->disent != NULL))
	{
	  struct bittree *i = ent->bits[x];
	  int idest;
	  int currbits = 15;

	  if (i != NULL)
	    {
	      /* If the instruction being branched to only consists of
		 a termination entry, use the termination entry as the
		 place to branch to instead.  */
	      if (i->bits[0] == NULL && i->bits[1] == NULL
		  && i->bits[2] == NULL && i->disent != NULL)
		{
		  idest = i->disent->ournum;
		  i = NULL;
		}
	      else
		idest = insn_list_len - our_offset;
	    }
	  else
	    idest = ent->disent->ournum;

	  /* If the destination offset for the if (bit is 1) test is less 
	     than 256 bytes away, we can store it as 8-bits instead of 16;
	     the instruction has bit 5 set for the 16-bit address, and bit
	     4 for the 8-bit address.  Since we've already allocated 16
	     bits for the address we need to deallocate the space.

	     Note that branchings within the table are relative, and
	     there are no branches that branch past our instruction yet
	     so we do not need to adjust any other offsets.  */
	  if (x == 1)
	    {
	      if (idest <= 256)
		{
		  int start = our_offset + bitsused / 8 + 1;

		  memmove (insn_list + start,
			   insn_list + start + 1,
			   insn_list_len - (start + 1));
		  currbits = 7;
		  totbits -= 8;
		  needed_bytes--;
		  insn_list_len--;
		  insn_list[our_offset] |= 0x10;
		  idest--;
		}
	      else
		insn_list[our_offset] |= 0x20;
	    }
	  else
	    {
	      /* An instruction which solely consists of a termination
		 marker and whose disassembly name index is < 4096
		 can be stored in 16 bits.  The encoding is slightly
		 odd; the upper 4 bits of the instruction are 0x3, and
		 bit 3 loses its normal meaning.  */

	      if (ent->bits[0] == NULL && ent->bits[1] == NULL
		  && ent->bits[2] == NULL && ent->skip_flag == 0
		  && ent->disent != NULL
		  && ent->disent->ournum < (32768 + 4096))
		{
		  int start = our_offset + bitsused / 8 + 1;

		  memmove (insn_list + start,
			   insn_list + start + 1,
			   insn_list_len - (start + 1));
		  currbits = 11;
		  totbits -= 5;
		  bitsused--;
		  needed_bytes--;
		  insn_list_len--;
		  insn_list[our_offset] |= 0x30;
		  idest &= ~32768;
		}
	      else
		insn_list[our_offset] |= 0x08;
	    }

	  if (debug)
	    {
	      int id = idest;

	      if (i == NULL)
		id |= 32768;
	      else if (! (id & 32768))
		id += our_offset;

	      if (x == 1)
		printf ("%d: if (1) goto %d\n", our_offset, id);
	      else
		printf ("%d: try %d\n", our_offset, id);
	    }

	  /* Store the address of the entry being branched to.  */
	  while (currbits >= 0)
	    {
	      unsigned char *byte = insn_list + our_offset + bitsused / 8;

	      if (idest & (1 << currbits))
		*byte |= (1 << (7 - (bitsused % 8)));

	      bitsused++;
	      currbits--;
	    }

	  /* Now generate the states for the entry being branched to.  */
	  if (i != NULL)
	    gen_dis_table (i);
	}
    }

  if (debug)
    {
      if (ent->skip_flag)
	printf ("%d: skipping %d\n", our_offset, ent->bits_to_skip);
  
      if (ent->bits[0] != NULL)
	printf ("%d: if (0:%d) goto %d\n", our_offset, zero_count + 1,
		zero_dest);
    }

  if (bitsused != totbits)
    abort ();
}

static void
print_dis_table (void)
{
  int x;
  struct disent *cent = disinsntable;

  printf ("static const char dis_table[] = {\n");
  for (x = 0; x < insn_list_len; x++)
    {
      if ((x > 0) && ((x % 12) == 0))
	printf ("\n");

      printf ("0x%02x, ", insn_list[x]);
    }
  printf ("\n};\n\n");

  printf ("static const struct ia64_dis_names ia64_dis_names[] = {\n");
  while (cent != NULL)
    {
      struct disent *ent = cent;

      while (ent != NULL)
	{
	  printf ("{ 0x%x, %d, %d, %d },\n", ent->completer_index,
		  ent->insn, (ent->nexte != NULL ? 1 : 0),
                  ent->priority);
	  ent = ent->nexte;
	}
      cent = cent->next_ent;
    }
  printf ("};\n\n");
}

static void
generate_disassembler (void)
{
  int i;

  bittree = make_bittree_entry ();

  for (i = 0; i < otlen; i++)
    {
      struct main_entry *ptr = ordered_table[i];

      if (ptr->opcode->type != IA64_TYPE_DYN)
	add_dis_entry (bittree,
		       ptr->opcode->opcode, ptr->opcode->mask, 
		       ptr->main_index,
		       ptr->completers, 1);
    }

  compact_distree (bittree);
  finish_distable ();
  gen_dis_table (bittree);

  print_dis_table ();
}

static void
print_string_table (void)
{
  int x;
  char lbuf[80], buf[80];
  int blen = 0;

  printf ("static const char * const ia64_strings[] = {\n");
  lbuf[0] = '\0';

  for (x = 0; x < strtablen; x++)
    {
      int len;
      
      if (strlen (string_table[x]->s) > 75)
	abort ();

      sprintf (buf, " \"%s\",", string_table[x]->s);
      len = strlen (buf);

      if ((blen + len) > 75)
	{
	  printf (" %s\n", lbuf);
	  lbuf[0] = '\0';
	  blen = 0;
	}
      strcat (lbuf, buf);
      blen += len;
    }

  if (blen > 0)
    printf (" %s\n", lbuf);

  printf ("};\n\n");
}

static struct completer_entry **glist;
static int glistlen = 0;
static int glisttotlen = 0;

/* If the completer trees ENT1 and ENT2 are equal, return 1.  */

static int
completer_entries_eq (ent1, ent2)
     struct completer_entry *ent1, *ent2;
{
  while (ent1 != NULL && ent2 != NULL)
    {
      if (ent1->name->num != ent2->name->num
	  || ent1->bits != ent2->bits
	  || ent1->mask != ent2->mask
	  || ent1->is_terminal != ent2->is_terminal
          || ent1->dependencies != ent2->dependencies
          || ent1->order != ent2->order)
	return 0;

      if (! completer_entries_eq (ent1->addl_entries, ent2->addl_entries))
	return 0;

      ent1 = ent1->alternative;
      ent2 = ent2->alternative;
    }

  return ent1 == ent2;
}

/* Insert ENT into the global list of completers and return it.  If an
   equivalent entry (according to completer_entries_eq) already exists,
   it is returned instead.  */
static struct completer_entry *
insert_gclist (struct completer_entry *ent)
{
  if (ent != NULL)
    {
      int i;
      int x;
      int start = 0, end;

      ent->addl_entries = insert_gclist (ent->addl_entries);
      ent->alternative = insert_gclist (ent->alternative);

      i = glistlen / 2;
      end = glistlen;

      if (glisttotlen == glistlen)
	{
	  glisttotlen += 20;
	  glist = (struct completer_entry **)
	    xrealloc (glist, sizeof (struct completer_entry *) * glisttotlen);
	}

      if (glistlen == 0)
	{
	  glist[0] = ent;
	  glistlen = 1;
	  return ent;
	}

      if (ent->name->num < glist[0]->name->num)
	i = 0;
      else if (ent->name->num > glist[end - 1]->name->num)
	i = end;
      else
	{
	  int c;

	  while (1)
	    {
	      i = (start + end) / 2;
	      c = ent->name->num - glist[i]->name->num;

	      if (c < 0)
		end = i - 1;
	      else if (c == 0)
		{
		  while (i > 0 
			 && ent->name->num == glist[i - 1]->name->num)
		    i--;

		  break;
		}
	      else
		start = i + 1;

	      if (start > end)
		break;
	    }

	  if (c == 0)
	    {
	      while (i < glistlen)
		{
		  if (ent->name->num != glist[i]->name->num)
		    break;

		  if (completer_entries_eq (ent, glist[i]))
		    return glist[i];

		  i++;
		}
	    }
	}

      for (; i > 0 && i < glistlen; i--)
	if (ent->name->num >= glist[i - 1]->name->num)
	  break;

      for (; i < glistlen; i++)
	if (ent->name->num < glist[i]->name->num)
	  break;

      for (x = glistlen - 1; x >= i; x--)
	glist[x + 1] = glist[x];

      glist[i] = ent;
      glistlen++;
    }
  return ent;
}

static int
get_prefix_len (name)
     const char *name;
{
  char *c;

  if (name[0] == '\0')
    return 0;

  c = strchr (name, '.');
  if (c != NULL)
    return c - name;
  else
    return strlen (name);
}

static void
compute_completer_bits (ment, ent)
     struct main_entry *ment;
     struct completer_entry *ent;
{
  while (ent != NULL)
    {
      compute_completer_bits (ment, ent->addl_entries);

      if (ent->is_terminal)
	{
	  ia64_insn mask = 0;
	  ia64_insn our_bits = ent->bits;
	  struct completer_entry *p = ent->parent;
	  ia64_insn p_bits;
	  int x;

	  while (p != NULL && ! p->is_terminal)
	    p = p->parent;
      
	  if (p != NULL)
	    p_bits = p->bits;
	  else
	    p_bits = ment->opcode->opcode;

	  for (x = 0; x < 64; x++)
	    {
	      ia64_insn m = ((ia64_insn) 1) << x;

	      if ((p_bits & m) != (our_bits & m))
		mask |= m;
	      else
		our_bits &= ~m;
	    }
	  ent->bits = our_bits;
	  ent->mask = mask;
	}
      else
	{
	  ent->bits = 0;
	  ent->mask = 0;
	}

      ent = ent->alternative;
    }
}

/* Find identical completer trees that are used in different
   instructions and collapse their entries.  */
static void
collapse_redundant_completers (void)
{
  struct main_entry *ptr;
  int x;

  for (ptr = maintable; ptr != NULL; ptr = ptr->next)
    {
      if (ptr->completers == NULL)
	abort ();

      compute_completer_bits (ptr, ptr->completers);
      ptr->completers = insert_gclist (ptr->completers);
    }

  /* The table has been finalized, now number the indexes.  */
  for (x = 0; x < glistlen; x++)
    glist[x]->num = x;
}


/* Attach two lists of dependencies to each opcode.
   1) all resources which, when already marked in use, conflict with this
   opcode (chks) 
   2) all resources which must be marked in use when this opcode is used
   (regs).  */
static int
insert_opcode_dependencies (opc, cmp)
     struct ia64_opcode *opc;
     struct completer_entry *cmp ATTRIBUTE_UNUSED;
{
  /* Note all resources which point to this opcode.  rfi has the most chks
     (79) and cmpxchng has the most regs (54) so 100 here should be enough.  */
  int i;
  int nregs = 0;
  unsigned short regs[256];                  
  int nchks = 0;
  unsigned short chks[256];
  /* Flag insns for which no class matched; there should be none.  */
  int no_class_found = 1;

  for (i = 0; i < rdepslen; i++)
    {
      struct rdep *rs = rdeps[i];
      int j;

      if (strcmp (opc->name, "cmp.eq.and") == 0
          && CONST_STRNEQ (rs->name, "PR%")
          && rs->mode == 1)
        no_class_found = 99;

      for (j=0; j < rs->nregs;j++)
        {
          int ic_note = 0;

          if (in_iclass (opc, ics[rs->regs[j]], NULL, NULL, &ic_note))
            {
              /* We can ignore ic_note 11 for non PR resources.  */
              if (ic_note == 11 && ! CONST_STRNEQ (rs->name, "PR"))
                ic_note = 0;

              if (ic_note != 0 && rs->regnotes[j] != 0
                  && ic_note != rs->regnotes[j]
                  && !(ic_note == 11 && rs->regnotes[j] == 1))
                warn (_("IC note %d in opcode %s (IC:%s) conflicts with resource %s note %d\n"),
		      ic_note, opc->name, ics[rs->regs[j]]->name,
		      rs->name, rs->regnotes[j]);
              /* Instruction class notes override resource notes.
                 So far, only note 11 applies to an IC instead of a resource,
                 and note 11 implies note 1.  */
              if (ic_note)
                regs[nregs++] = RDEP(ic_note, i);
              else
                regs[nregs++] = RDEP(rs->regnotes[j], i);
              no_class_found = 0;
              ++rs->total_regs;
            }
        }

      for (j = 0; j < rs->nchks; j++)
        {
          int ic_note = 0;

          if (in_iclass (opc, ics[rs->chks[j]], NULL, NULL, &ic_note))
            {
              /* We can ignore ic_note 11 for non PR resources.  */
              if (ic_note == 11 && ! CONST_STRNEQ (rs->name, "PR"))
                ic_note = 0;

              if (ic_note != 0 && rs->chknotes[j] != 0
                  && ic_note != rs->chknotes[j]
                  && !(ic_note == 11 && rs->chknotes[j] == 1))
                warn (_("IC note %d for opcode %s (IC:%s) conflicts with resource %s note %d\n"),
		      ic_note, opc->name, ics[rs->chks[j]]->name,
		      rs->name, rs->chknotes[j]);
              if (ic_note)
                chks[nchks++] = RDEP(ic_note, i);
              else
                chks[nchks++] = RDEP(rs->chknotes[j], i);
              no_class_found = 0;
              ++rs->total_chks;
            }
        }
    }

  if (no_class_found)
    warn (_("opcode %s has no class (ops %d %d %d)\n"),
	  opc->name, 
	  opc->operands[0], opc->operands[1], opc->operands[2]);

  return insert_dependencies (nchks, chks, nregs, regs);
}

static void
insert_completer_entry (opc, tabent, order)
     struct ia64_opcode *opc;
     struct main_entry *tabent;
     int order;
{
  struct completer_entry **ptr = &tabent->completers;
  struct completer_entry *parent = NULL;
  char pcopy[129], *prefix;
  int at_end = 0;

  if (strlen (opc->name) > 128)
    abort ();

  strcpy (pcopy, opc->name);
  prefix = pcopy + get_prefix_len (pcopy);

  if (prefix[0] != '\0')
    prefix++;

  while (! at_end)
    {
      int need_new_ent = 1;
      int plen = get_prefix_len (prefix);
      struct string_entry *sent;

      at_end = (prefix[plen] == '\0');
      prefix[plen] = '\0';
      sent = insert_string (prefix);

      while (*ptr != NULL)
	{
	  int cmpres = sent->num - (*ptr)->name->num;

	  if (cmpres == 0)
	    {
	      need_new_ent = 0;
	      break;
	    }
	  else
	    ptr = &((*ptr)->alternative);
	}

      if (need_new_ent)
	{
	  struct completer_entry *nent = tmalloc (struct completer_entry);

	  nent->name = sent;
	  nent->parent = parent;
	  nent->addl_entries = NULL;
	  nent->alternative = *ptr;
	  *ptr = nent;
	  nent->is_terminal = 0;
          nent->dependencies = -1;
	}

      if (! at_end)
	{
	  parent = *ptr;
	  ptr = &((*ptr)->addl_entries);
	  prefix += plen + 1;
	}
    }

  if ((*ptr)->is_terminal)
    abort ();

  (*ptr)->is_terminal = 1;
  (*ptr)->mask = (ia64_insn)-1;
  (*ptr)->bits = opc->opcode;
  (*ptr)->dependencies = insert_opcode_dependencies (opc, *ptr);
  (*ptr)->order = order;
}

static void
print_completer_entry (ent)
     struct completer_entry *ent;
{
  int moffset = 0;
  ia64_insn mask = ent->mask, bits = ent->bits;

  if (mask != 0)
    {
      while (! (mask & 1))
	{
	  moffset++;
	  mask = mask >> 1;
	  bits = bits >> 1;
	}

      if (bits & 0xffffffff00000000LL)
	abort ();
    }
  
  printf ("  { 0x%x, 0x%x, %d, %d, %d, %d, %d, %d },\n",
	  (int)bits,
	  (int)mask,
	  ent->name->num,
	  ent->alternative != NULL ? ent->alternative->num : -1,
	  ent->addl_entries != NULL ? ent->addl_entries->num : -1,
	  moffset,
	  ent->is_terminal ? 1 : 0,
          ent->dependencies);
}

static void
print_completer_table ()
{
  int x;

  printf ("static const struct ia64_completer_table\ncompleter_table[] = {\n");
  for (x = 0; x < glistlen; x++)
    print_completer_entry (glist[x]);
  printf ("};\n\n");
}

static int
opcodes_eq (opc1, opc2)
     struct ia64_opcode *opc1;
     struct ia64_opcode *opc2;
{
  int x;
  int plen1, plen2;

  if ((opc1->mask != opc2->mask) || (opc1->type != opc2->type) 
      || (opc1->num_outputs != opc2->num_outputs)
      || (opc1->flags != opc2->flags))
    return 0;

  for (x = 0; x < 5; x++)
    if (opc1->operands[x] != opc2->operands[x])
      return 0;

  plen1 = get_prefix_len (opc1->name);
  plen2 = get_prefix_len (opc2->name);

  if (plen1 == plen2 && (memcmp (opc1->name, opc2->name, plen1) == 0))
    return 1;

  return 0;
}

static void
add_opcode_entry (opc)
     struct ia64_opcode *opc;
{
  struct main_entry **place;
  struct string_entry *name;
  char prefix[129];
  int found_it = 0;

  if (strlen (opc->name) > 128)
    abort ();

  place = &maintable;
  strcpy (prefix, opc->name);
  prefix[get_prefix_len (prefix)] = '\0';
  name = insert_string (prefix);

  /* Walk the list of opcode table entries.  If it's a new
     instruction, allocate and fill in a new entry.  Note 
     the main table is alphabetical by opcode name.  */

  while (*place != NULL)
    {
      if ((*place)->name->num == name->num
	  && opcodes_eq ((*place)->opcode, opc))
	{
	  found_it = 1;
	  break;
	}
      if ((*place)->name->num > name->num)
	break;

      place = &((*place)->next);
    }
  if (! found_it)
    {
      struct main_entry *nent = tmalloc (struct main_entry);

      nent->name = name;
      nent->opcode = opc;
      nent->next = *place;
      nent->completers = 0;
      *place = nent;

      if (otlen == ottotlen)
        {
          ottotlen += 20;
          ordered_table = (struct main_entry **)
            xrealloc (ordered_table, sizeof (struct main_entry *) * ottotlen);
        }
      ordered_table[otlen++] = nent;
    }

  insert_completer_entry (opc, *place, opcode_count++);
}

static void
print_main_table (void)
{
  struct main_entry *ptr = maintable;
  int index = 0;

  printf ("static const struct ia64_main_table\nmain_table[] = {\n");
  while (ptr != NULL)
    {
      printf ("  { %d, %d, %d, 0x",
	      ptr->name->num,
	      ptr->opcode->type,
	      ptr->opcode->num_outputs);
      opcode_fprintf_vma (stdout, ptr->opcode->opcode);
      printf ("ull, 0x");
      opcode_fprintf_vma (stdout, ptr->opcode->mask);
      printf ("ull, { %d, %d, %d, %d, %d }, 0x%x, %d, },\n",
	      ptr->opcode->operands[0],
	      ptr->opcode->operands[1],
	      ptr->opcode->operands[2],
	      ptr->opcode->operands[3],
	      ptr->opcode->operands[4],
	      ptr->opcode->flags,
	      ptr->completers->num);

      ptr->main_index = index++;

      ptr = ptr->next;
    }
  printf ("};\n\n");
}

static void
shrink (table)
     struct ia64_opcode *table;
{
  int curr_opcode;

  for (curr_opcode = 0; table[curr_opcode].name != NULL; curr_opcode++)
    {
      add_opcode_entry (table + curr_opcode);
      if (table[curr_opcode].num_outputs == 2
	  && ((table[curr_opcode].operands[0] == IA64_OPND_P1
	       && table[curr_opcode].operands[1] == IA64_OPND_P2)
	      || (table[curr_opcode].operands[0] == IA64_OPND_P2
		  && table[curr_opcode].operands[1] == IA64_OPND_P1)))
	{
	  struct ia64_opcode *alias = tmalloc(struct ia64_opcode);
	  unsigned i;

	  *alias = table[curr_opcode];
	  for (i = 2; i < NELEMS (alias->operands); ++i)
	    alias->operands[i - 1] = alias->operands[i];
	  alias->operands[NELEMS (alias->operands) - 1] = IA64_OPND_NIL;
	  --alias->num_outputs;
	  alias->flags |= PSEUDO;
	  add_opcode_entry (alias);
	}
    }
}


/* Program options.  */
#define OPTION_SRCDIR	200

struct option long_options[] = 
{
  {"srcdir",  required_argument, NULL, OPTION_SRCDIR},
  {"debug",   no_argument,       NULL, 'd'},
  {"version", no_argument,       NULL, 'V'},
  {"help",    no_argument,       NULL, 'h'},
  {0,         no_argument,       NULL, 0}
};

static void
print_version (void)
{
  printf ("%s: version 1.0\n", program_name);
  xexit (0);
}

static void
usage (FILE * stream, int status)
{
  fprintf (stream, "Usage: %s [-V | --version] [-d | --debug] [--srcdir=dirname] [--help]\n",
	   program_name);
  xexit (status);
}

int
main (int argc, char **argv)
{
  extern int chdir (char *);
  char *srcdir = NULL;
  int c;
  
  program_name = *argv;
  xmalloc_set_program_name (program_name);

  while ((c = getopt_long (argc, argv, "vVdh", long_options, 0)) != EOF)
    switch (c)
      {
      case OPTION_SRCDIR:
	srcdir = optarg;
	break;
      case 'V':
      case 'v':
	print_version ();
	break;
      case 'd':
	debug = 1;
	break;
      case 'h':
      case '?':
	usage (stderr, 0);
      default:
      case 0:
	break;
      }

  if (optind != argc)
    usage (stdout, 1);

  if (srcdir != NULL) 
    if (chdir (srcdir) != 0)
      fail (_("unable to change directory to \"%s\", errno = %s\n"),
	    srcdir, strerror (errno));

  load_insn_classes ();
  load_dependencies ();

  shrink (ia64_opcodes_a);
  shrink (ia64_opcodes_b);
  shrink (ia64_opcodes_f);
  shrink (ia64_opcodes_i);
  shrink (ia64_opcodes_m);
  shrink (ia64_opcodes_x);
  shrink (ia64_opcodes_d);

  collapse_redundant_completers ();

  printf ("/* This file is automatically generated by ia64-gen.  Do not edit!  */\n");
  print_string_table ();
  print_dependency_table ();
  print_completer_table ();
  print_main_table ();

  generate_disassembler ();

  exit (0);
}
