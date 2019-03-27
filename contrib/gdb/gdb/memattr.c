/* Memory attributes support, for GDB.

   Copyright 2001, 2002 Free Software Foundation, Inc.

   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "command.h"
#include "gdbcmd.h"
#include "memattr.h"
#include "target.h"
#include "value.h"
#include "language.h"
#include "gdb_string.h"

const struct mem_attrib default_mem_attrib =
{
  MEM_RW,			/* mode */
  MEM_WIDTH_UNSPECIFIED,
  0,				/* hwbreak */
  0,				/* cache */
  0				/* verify */
};

static struct mem_region *mem_region_chain = NULL;
static int mem_number = 0;

static struct mem_region *
create_mem_region (CORE_ADDR lo, CORE_ADDR hi,
		   const struct mem_attrib *attrib)
{
  struct mem_region *n, *new;

  /* lo == hi is a useless empty region */
  if (lo >= hi && hi != 0)
    {
      printf_unfiltered ("invalid memory region: low >= high\n");
      return NULL;
    }

  n = mem_region_chain;
  while (n)
    {
      /* overlapping node */
      if ((lo >= n->lo && (lo < n->hi || n->hi == 0)) 
	  || (hi > n->lo && (hi <= n->hi || n->hi == 0))
	  || (lo <= n->lo && (hi >= n->hi || hi == 0)))
	{
	  printf_unfiltered ("overlapping memory region\n");
	  return NULL;
	}
      n = n->next;
    }

  new = xmalloc (sizeof (struct mem_region));
  new->lo = lo;
  new->hi = hi;
  new->number = ++mem_number;
  new->enabled_p = 1;
  new->attrib = *attrib;

  /* link in new node */
  new->next = mem_region_chain;
  mem_region_chain = new;

  return new;
}

static void
delete_mem_region (struct mem_region *m)
{
  xfree (m);
}

/*
 * Look up the memory region cooresponding to ADDR.
 */
struct mem_region *
lookup_mem_region (CORE_ADDR addr)
{
  static struct mem_region region;
  struct mem_region *m;
  CORE_ADDR lo;
  CORE_ADDR hi;

  /* First we initialize LO and HI so that they describe the entire
     memory space.  As we process the memory region chain, they are
     redefined to describe the minimal region containing ADDR.  LO
     and HI are used in the case where no memory region is defined
     that contains ADDR.  If a memory region is disabled, it is
     treated as if it does not exist.  */

  lo = (CORE_ADDR) 0;
  hi = (CORE_ADDR) ~ 0;

  for (m = mem_region_chain; m; m = m->next)
    {
      if (m->enabled_p == 1)
	{
	  if (addr >= m->lo && (addr < m->hi || m->hi == 0))
	    return m;

	  if (addr >= m->hi && lo < m->hi)
	    lo = m->hi;

	  if (addr <= m->lo && hi > m->lo)
	    hi = m->lo;
	}
    }

  /* Because no region was found, we must cons up one based on what
     was learned above.  */
  region.lo = lo;
  region.hi = hi;
  region.attrib = default_mem_attrib;
  return &region;
}


static void
mem_command (char *args, int from_tty)
{
  CORE_ADDR lo, hi;
  char *tok;
  struct mem_attrib attrib;

  if (!args)
    error_no_arg ("No mem");

  tok = strtok (args, " \t");
  if (!tok)
    error ("no lo address");
  lo = parse_and_eval_address (tok);

  tok = strtok (NULL, " \t");
  if (!tok)
    error ("no hi address");
  hi = parse_and_eval_address (tok);

  attrib = default_mem_attrib;
  while ((tok = strtok (NULL, " \t")) != NULL)
    {
      if (strcmp (tok, "rw") == 0)
	attrib.mode = MEM_RW;
      else if (strcmp (tok, "ro") == 0)
	attrib.mode = MEM_RO;
      else if (strcmp (tok, "wo") == 0)
	attrib.mode = MEM_WO;

      else if (strcmp (tok, "8") == 0)
	attrib.width = MEM_WIDTH_8;
      else if (strcmp (tok, "16") == 0)
	{
	  if ((lo % 2 != 0) || (hi % 2 != 0))
	    error ("region bounds not 16 bit aligned");
	  attrib.width = MEM_WIDTH_16;
	}
      else if (strcmp (tok, "32") == 0)
	{
	  if ((lo % 4 != 0) || (hi % 4 != 0))
	    error ("region bounds not 32 bit aligned");
	  attrib.width = MEM_WIDTH_32;
	}
      else if (strcmp (tok, "64") == 0)
	{
	  if ((lo % 8 != 0) || (hi % 8 != 0))
	    error ("region bounds not 64 bit aligned");
	  attrib.width = MEM_WIDTH_64;
	}

#if 0
      else if (strcmp (tok, "hwbreak") == 0)
	attrib.hwbreak = 1;
      else if (strcmp (tok, "swbreak") == 0)
	attrib.hwbreak = 0;
#endif

      else if (strcmp (tok, "cache") == 0)
	attrib.cache = 1;
      else if (strcmp (tok, "nocache") == 0)
	attrib.cache = 0;

#if 0
      else if (strcmp (tok, "verify") == 0)
	attrib.verify = 1;
      else if (strcmp (tok, "noverify") == 0)
	attrib.verify = 0;
#endif

      else
	error ("unknown attribute: %s", tok);
    }

  create_mem_region (lo, hi, &attrib);
}


static void
mem_info_command (char *args, int from_tty)
{
  struct mem_region *m;
  struct mem_attrib *attrib;

  if (!mem_region_chain)
    {
      printf_unfiltered ("There are no memory regions defined.\n");
      return;
    }

  printf_filtered ("Num ");
  printf_filtered ("Enb ");
  printf_filtered ("Low Addr   ");
  if (TARGET_ADDR_BIT > 32)
    printf_filtered ("        ");
  printf_filtered ("High Addr  ");
  if (TARGET_ADDR_BIT > 32)
    printf_filtered ("        ");
  printf_filtered ("Attrs ");
  printf_filtered ("\n");

  for (m = mem_region_chain; m; m = m->next)
    {
      char *tmp;
      printf_filtered ("%-3d %-3c\t",
		       m->number,
		       m->enabled_p ? 'y' : 'n');
      if (TARGET_ADDR_BIT <= 32)
	tmp = local_hex_string_custom ((unsigned long) m->lo, "08l");
      else
	tmp = local_hex_string_custom ((unsigned long) m->lo, "016l");
      
      printf_filtered ("%s ", tmp);

      if (TARGET_ADDR_BIT <= 32)
	{
	if (m->hi == 0)
	  tmp = "0x100000000";
	else
	  tmp = local_hex_string_custom ((unsigned long) m->hi, "08l");
	}
      else
	{
	if (m->hi == 0)
	  tmp = "0x10000000000000000";
	else
	  tmp = local_hex_string_custom ((unsigned long) m->hi, "016l");
	}

      printf_filtered ("%s ", tmp);

      /* Print a token for each attribute.

       * FIXME: Should we output a comma after each token?  It may
       * make it easier for users to read, but we'd lose the ability
       * to cut-and-paste the list of attributes when defining a new
       * region.  Perhaps that is not important.
       *
       * FIXME: If more attributes are added to GDB, the output may
       * become cluttered and difficult for users to read.  At that
       * time, we may want to consider printing tokens only if they
       * are different from the default attribute.  */

      attrib = &m->attrib;
      switch (attrib->mode)
	{
	case MEM_RW:
	  printf_filtered ("rw ");
	  break;
	case MEM_RO:
	  printf_filtered ("ro ");
	  break;
	case MEM_WO:
	  printf_filtered ("wo ");
	  break;
	}

      switch (attrib->width)
	{
	case MEM_WIDTH_8:
	  printf_filtered ("8 ");
	  break;
	case MEM_WIDTH_16:
	  printf_filtered ("16 ");
	  break;
	case MEM_WIDTH_32:
	  printf_filtered ("32 ");
	  break;
	case MEM_WIDTH_64:
	  printf_filtered ("64 ");
	  break;
	case MEM_WIDTH_UNSPECIFIED:
	  break;
	}

#if 0
      if (attrib->hwbreak)
	printf_filtered ("hwbreak");
      else
	printf_filtered ("swbreak");
#endif

      if (attrib->cache)
	printf_filtered ("cache ");
      else
	printf_filtered ("nocache ");

#if 0
      if (attrib->verify)
	printf_filtered ("verify ");
      else
	printf_filtered ("noverify ");
#endif

      printf_filtered ("\n");

      gdb_flush (gdb_stdout);
    }
}


/* Enable the memory region number NUM. */

static void
mem_enable (int num)
{
  struct mem_region *m;

  for (m = mem_region_chain; m; m = m->next)
    if (m->number == num)
      {
	m->enabled_p = 1;
	return;
      }
  printf_unfiltered ("No memory region number %d.\n", num);
}

static void
mem_enable_command (char *args, int from_tty)
{
  char *p = args;
  char *p1;
  int num;
  struct mem_region *m;

  dcache_invalidate (target_dcache);

  if (p == 0)
    {
      for (m = mem_region_chain; m; m = m->next)
	m->enabled_p = 1;
    }
  else
    while (*p)
      {
	p1 = p;
	while (*p1 >= '0' && *p1 <= '9')
	  p1++;
	if (*p1 && *p1 != ' ' && *p1 != '\t')
	  error ("Arguments must be memory region numbers.");

	num = atoi (p);
	mem_enable (num);

	p = p1;
	while (*p == ' ' || *p == '\t')
	  p++;
      }
}


/* Disable the memory region number NUM. */

static void
mem_disable (int num)
{
  struct mem_region *m;

  for (m = mem_region_chain; m; m = m->next)
    if (m->number == num)
      {
	m->enabled_p = 0;
	return;
      }
  printf_unfiltered ("No memory region number %d.\n", num);
}

static void
mem_disable_command (char *args, int from_tty)
{
  char *p = args;
  char *p1;
  int num;
  struct mem_region *m;

  dcache_invalidate (target_dcache);

  if (p == 0)
    {
      for (m = mem_region_chain; m; m = m->next)
	m->enabled_p = 0;
    }
  else
    while (*p)
      {
	p1 = p;
	while (*p1 >= '0' && *p1 <= '9')
	  p1++;
	if (*p1 && *p1 != ' ' && *p1 != '\t')
	  error ("Arguments must be memory region numbers.");

	num = atoi (p);
	mem_disable (num);

	p = p1;
	while (*p == ' ' || *p == '\t')
	  p++;
      }
}

/* Clear memory region list */

static void
mem_clear (void)
{
  struct mem_region *m;

  while ((m = mem_region_chain) != 0)
    {
      mem_region_chain = m->next;
      delete_mem_region (m);
    }
}

/* Delete the memory region number NUM. */

static void
mem_delete (int num)
{
  struct mem_region *m1, *m;

  if (!mem_region_chain)
    {
      printf_unfiltered ("No memory region number %d.\n", num);
      return;
    }

  if (mem_region_chain->number == num)
    {
      m1 = mem_region_chain;
      mem_region_chain = m1->next;
      delete_mem_region (m1);
    }
  else
    for (m = mem_region_chain; m->next; m = m->next)
      {
	if (m->next->number == num)
	  {
	    m1 = m->next;
	    m->next = m1->next;
	    delete_mem_region (m1);
	    break;
	  }
      }
}

static void
mem_delete_command (char *args, int from_tty)
{
  char *p = args;
  char *p1;
  int num;

  dcache_invalidate (target_dcache);

  if (p == 0)
    {
      if (query ("Delete all memory regions? "))
	mem_clear ();
      dont_repeat ();
      return;
    }

  while (*p)
    {
      p1 = p;
      while (*p1 >= '0' && *p1 <= '9')
	p1++;
      if (*p1 && *p1 != ' ' && *p1 != '\t')
	error ("Arguments must be memory region numbers.");

      num = atoi (p);
      mem_delete (num);

      p = p1;
      while (*p == ' ' || *p == '\t')
	p++;
    }

  dont_repeat ();
}

extern initialize_file_ftype _initialize_mem; /* -Wmissing-prototype */

void
_initialize_mem (void)
{
  add_com ("mem", class_vars, mem_command,
	   "Define attributes for memory region.\n\
Usage: mem <lo addr> <hi addr> [<mode> <width> <cache>], \n\
where <mode>  may be rw (read/write), ro (read-only) or wo (write-only), \n\
      <width> may be 8, 16, 32, or 64, and \n\
      <cache> may be cache or nocache");

  add_cmd ("mem", class_vars, mem_enable_command,
	   "Enable memory region.\n\
Arguments are the code numbers of the memory regions to enable.\n\
Usage: enable mem <code number>\n\
Do \"info mem\" to see current list of code numbers.", &enablelist);

  add_cmd ("mem", class_vars, mem_disable_command,
	   "Disable memory region.\n\
Arguments are the code numbers of the memory regions to disable.\n\
Usage: disable mem <code number>\n\
Do \"info mem\" to see current list of code numbers.", &disablelist);

  add_cmd ("mem", class_vars, mem_delete_command,
	   "Delete memory region.\n\
Arguments are the code numbers of the memory regions to delete.\n\
Usage: delete mem <code number>\n\
Do \"info mem\" to see current list of code numbers.", &deletelist);

  add_info ("mem", mem_info_command,
	    "Memory region attributes");
}
