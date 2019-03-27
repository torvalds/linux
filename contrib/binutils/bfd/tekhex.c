/* BFD backend for Extended Tektronix Hex Format  objects.
   Copyright 1992, 1993, 1994, 1995, 1996, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2007 Free Software Foundation, Inc.
   Written by Steve Chamberlain of Cygnus Support <sac@cygnus.com>.

   This file is part of BFD, the Binary File Descriptor library.

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

/* SUBSECTION
	Tektronix Hex Format handling

   DESCRIPTION

	Tek Hex records can hold symbols and data, but not
	relocations. Their main application is communication with
	devices like PROM programmers and ICE equipment.

	It seems that the sections are described as being really big,
        the example I have says that the text section is 0..ffffffff.
	BFD would barf with this, many apps would try to alloc 4GB to
	read in the file.

	Tex Hex may contain many sections, but the data which comes in
	has no tag saying which section it belongs to, so we create
	one section for each block of data, called "blknnnn" which we
	stick all the data into.

	TekHex may come out of 	order and there is no header, so an
	initial scan is required  to discover the minimum and maximum
	addresses used to create the vma and size of the sections we
	create.
	We read in the data into pages of CHUNK_MASK+1 size and read
	them out from that whenever we need to.

	Any number of sections may be created for output, we save them
	up and output them when it's time to close the bfd.

	A TekHex record looks like:
  EXAMPLE
	%<block length><type><checksum><stuff><cr>

  DESCRIPTION
	Where
	o length
	is the number of bytes in the record not including the % sign.
	o type
	is one of:
	3) symbol record
	6) data record
	8) termination record

  The data can come out of order, and may be discontigous. This is a
  serial protocol, so big files are unlikely, so we keep a list of 8k chunks.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "libiberty.h"

typedef struct
{
  bfd_vma low;
  bfd_vma high;
} addr_range_type;

typedef struct tekhex_symbol_struct
{
  asymbol symbol;
  struct tekhex_symbol_struct *prev;
} tekhex_symbol_type;

static const char digs[] = "0123456789ABCDEF";

static char sum_block[256];

#define NOT_HEX      20
#define NIBBLE(x)    hex_value(x)
#define HEX(buffer) ((NIBBLE ((buffer)[0]) << 4) + NIBBLE ((buffer)[1]))
#define	ISHEX(x)    hex_p(x)
#define TOHEX(d, x) \
  (d)[1] = digs[(x) & 0xf]; \
  (d)[0] = digs[((x)>>4)&0xf];

/* Here's an example
   %3A6C6480004E56FFFC4E717063B0AEFFFC6D0652AEFFFC60F24E5E4E75
   %1B3709T_SEGMENT1108FFFFFFFF
   %2B3AB9T_SEGMENT7Dgcc_compiled$1087hello$c10
   %373829T_SEGMENT80int$t1$r1$$214741080char$t2$r2$0$12710
   %373769T_SEGMENT80long$int$t3$r1$$1080unsigned$int$t4$10
   %373CA9T_SEGMENT80long$unsigned$in1080short$int$t6$r1$10
   %373049T_SEGMENT80long$long$int$t71080short$unsigned$i10
   %373A29T_SEGMENT80long$long$unsign1080signed$char$t10$10
   %373D69T_SEGMENT80unsigned$char$t11080float$t12$r1$4$010
   %373D19T_SEGMENT80double$t13$r1$8$1080long$double$t14$10
   %2734D9T_SEGMENT8Bvoid$t15$151035_main10
   %2F3CA9T_SEGMENT81$1081$1681$1E81$21487main$F110
   %2832F9T_SEGMENT83i$18FFFFFFFC81$1481$214
   %07 8 10 10

   explanation:
   %3A6C6480004E56FFFC4E717063B0AEFFFC6D0652AEFFFC60F24E5E4E75
    ^ ^^ ^     ^-data
    | || +------ 4 char integer 0x8000
    | |+-------- checksum
    | +--------- type 6 (data record)
    +----------- length 3a chars
   <---------------------- 3a (58 chars) ------------------->

   %1B3709T_SEGMENT1108FFFFFFFF
         ^         ^^ ^- 8 character integer 0xffffffff
         |         |+-   1 character integer 0
         |         +--   type 1 symbol (section definition)
         +------------   9 char symbol T_SEGMENT

   %2B3AB9T_SEGMENT7Dgcc_compiled$1087hello$c10
   %373829T_SEGMENT80int$t1$r1$$214741080char$t2$r2$0$12710
   %373769T_SEGMENT80long$int$t3$r1$$1080unsigned$int$t4$10
   %373CA9T_SEGMENT80long$unsigned$in1080short$int$t6$r1$10
   %373049T_SEGMENT80long$long$int$t71080short$unsigned$i10
   %373A29T_SEGMENT80long$long$unsign1080signed$char$t10$10
   %373D69T_SEGMENT80unsigned$char$t11080float$t12$r1$4$010
   %373D19T_SEGMENT80double$t13$r1$8$1080long$double$t14$10
   %2734D9T_SEGMENT8Bvoid$t15$151035_main10
   %2F3CA9T_SEGMENT81$1081$1681$1E81$21487main$F110
   %2832F9T_SEGMENT83i$18FFFFFFFC81$1481$214
   %0781010

   Turns into
   sac@thepub$ ./objdump -dx -m m68k f

   f:     file format tekhex
   -----x--- 9/55728 -134219416 Sep 29 15:13 1995 f
   architecture: UNKNOWN!, flags 0x00000010:
   HAS_SYMS
   start address 0x00000000
   SECTION 0 [D00000000]	: size 00020000 vma 00000000 align 2**0
   ALLOC, LOAD
   SECTION 1 [D00008000]	: size 00002001 vma 00008000 align 2**0

   SECTION 2 [T_SEGMENT]	: size ffffffff vma 00000000 align 2**0

   SYMBOL TABLE:
   00000000  g       T_SEGMENT gcc_compiled$
   00000000  g       T_SEGMENT hello$c
   00000000  g       T_SEGMENT int$t1$r1$$21474
   00000000  g       T_SEGMENT char$t2$r2$0$127
   00000000  g       T_SEGMENT long$int$t3$r1$$
   00000000  g       T_SEGMENT unsigned$int$t4$
   00000000  g       T_SEGMENT long$unsigned$in
   00000000  g       T_SEGMENT short$int$t6$r1$
   00000000  g       T_SEGMENT long$long$int$t7
   00000000  g       T_SEGMENT short$unsigned$i
   00000000  g       T_SEGMENT long$long$unsign
   00000000  g       T_SEGMENT signed$char$t10$
   00000000  g       T_SEGMENT unsigned$char$t1
   00000000  g       T_SEGMENT float$t12$r1$4$0
   00000000  g       T_SEGMENT double$t13$r1$8$
   00000000  g       T_SEGMENT long$double$t14$
   00000000  g       T_SEGMENT void$t15$15
   00000000  g       T_SEGMENT _main
   00000000  g       T_SEGMENT $
   00000000  g       T_SEGMENT $
   00000000  g       T_SEGMENT $
   00000010  g       T_SEGMENT $
   00000000  g       T_SEGMENT main$F1
   fcffffff  g       T_SEGMENT i$1
   00000000  g       T_SEGMENT $
   00000010  g       T_SEGMENT $

   RELOCATION RECORDS FOR [D00000000]: (none)

   RELOCATION RECORDS FOR [D00008000]: (none)

   RELOCATION RECORDS FOR [T_SEGMENT]: (none)

   Disassembly of section D00000000:
   ...
   00008000 ($+)7ff0 linkw fp,#-4
   00008004 ($+)7ff4 nop
   00008006 ($+)7ff6 movel #99,d0
   00008008 ($+)7ff8 cmpl fp@(-4),d0
   0000800c ($+)7ffc blts 00008014 ($+)8004
   0000800e ($+)7ffe addql #1,fp@(-4)
   00008012 ($+)8002 bras 00008006 ($+)7ff6
   00008014 ($+)8004 unlk fp
   00008016 ($+)8006 rts
   ...  */

static void
tekhex_init (void)
{
  unsigned int i;
  static bfd_boolean inited = FALSE;
  int val;

  if (! inited)
    {
      inited = TRUE;
      hex_init ();
      val = 0;
      for (i = 0; i < 10; i++)
	sum_block[i + '0'] = val++;

      for (i = 'A'; i <= 'Z'; i++)
	sum_block[i] = val++;

      sum_block['$'] = val++;
      sum_block['%'] = val++;
      sum_block['.'] = val++;
      sum_block['_'] = val++;
      for (i = 'a'; i <= 'z'; i++)
	sum_block[i] = val++;
    }
}

/* The maximum number of bytes on a line is FF.  */
#define MAXCHUNK 0xff
/* The number of bytes we fit onto a line on output.  */
#define CHUNK 21

/* We cannot output our tekhexords as we see them, we have to glue them
   together, this is done in this structure : */

struct tekhex_data_list_struct
{
  unsigned char *data;
  bfd_vma where;
  bfd_size_type size;
  struct tekhex_data_list_struct *next;

};
typedef struct tekhex_data_list_struct tekhex_data_list_type;

#define CHUNK_MASK 0x1fff

struct data_struct
{
  char chunk_data[CHUNK_MASK + 1];
  char chunk_init[CHUNK_MASK + 1];
  bfd_vma vma;
  struct data_struct *next;
};

typedef struct tekhex_data_struct
{
  tekhex_data_list_type *head;
  unsigned int type;
  struct tekhex_symbol_struct *symbols;
  struct data_struct *data;
} tdata_type;

#define enda(x) (x->vma + x->size)

static bfd_boolean
getvalue (char **srcp, bfd_vma *valuep)
{
  char *src = *srcp;
  bfd_vma value = 0;
  unsigned int len;

  if (!ISHEX (*src))
    return FALSE;

  len = hex_value (*src++);
  if (len == 0)
    len = 16;
  while (len--)
    {
      if (!ISHEX (*src))
	return FALSE;
      value = value << 4 | hex_value (*src++);
    }

  *srcp = src;
  *valuep = value;
  return TRUE;
}

static bfd_boolean
getsym (char *dstp, char **srcp, unsigned int *lenp)
{
  char *src = *srcp;
  unsigned int i;
  unsigned int len;
  
  if (!ISHEX (*src))
    return FALSE;

  len = hex_value (*src++);
  if (len == 0)
    len = 16;
  for (i = 0; i < len; i++)
    dstp[i] = src[i];
  dstp[i] = 0;
  *srcp = src + i;
  *lenp = len;
  return TRUE;
}

static struct data_struct *
find_chunk (bfd *abfd, bfd_vma vma)
{
  struct data_struct *d = abfd->tdata.tekhex_data->data;

  vma &= ~CHUNK_MASK;
  while (d && (d->vma) != vma)
    d = d->next;

  if (!d)
    {
      /* No chunk for this address, so make one up.  */
      d = bfd_zalloc (abfd, (bfd_size_type) sizeof (struct data_struct));

      if (!d)
	return NULL;

      d->next = abfd->tdata.tekhex_data->data;
      d->vma = vma;
      abfd->tdata.tekhex_data->data = d;
    }
  return d;
}

static void
insert_byte (bfd *abfd, int value, bfd_vma addr)
{
  /* Find the chunk that this byte needs and put it in.  */
  struct data_struct *d = find_chunk (abfd, addr);

  d->chunk_data[addr & CHUNK_MASK] = value;
  d->chunk_init[addr & CHUNK_MASK] = 1;
}

/* The first pass is to find the names of all the sections, and see
  how big the data is.  */

static bfd_boolean
first_phase (bfd *abfd, int type, char *src)
{
  asection *section = bfd_abs_section_ptr;
  unsigned int len;
  bfd_vma val;
  char sym[17];			/* A symbol can only be 16chars long.  */

  switch (type)
    {
    case '6':
      /* Data record - read it and store it.  */
      {
	bfd_vma addr;

	if (!getvalue (&src, &addr))
	  return FALSE;

	while (*src)
	  {
	    insert_byte (abfd, HEX (src), addr);
	    src += 2;
	    addr++;
	  }
      }

      return TRUE;
    case '3':
      /* Symbol record, read the segment.  */
      if (!getsym (sym, &src, &len))
	return FALSE;
      section = bfd_get_section_by_name (abfd, sym);
      if (section == NULL)
	{
	  char *n = bfd_alloc (abfd, (bfd_size_type) len + 1);

	  if (!n)
	    return FALSE;
	  memcpy (n, sym, len + 1);
	  section = bfd_make_section (abfd, n);
	}
      while (*src)
	{
	  switch (*src)
	    {
	    case '1':		/* Section range.  */
	      src++;
	      if (!getvalue (&src, &section->vma))
		return FALSE;
	      if (!getvalue (&src, &val))
		return FALSE;
	      section->size = val - section->vma;
	      section->flags = SEC_HAS_CONTENTS | SEC_LOAD | SEC_ALLOC;
	      break;
	    case '0':
	    case '2':
	    case '3':
	    case '4':
	    case '6':
	    case '7':
	    case '8':
	      /* Symbols, add to section.  */
	      {
		bfd_size_type amt = sizeof (tekhex_symbol_type);
		tekhex_symbol_type *new = bfd_alloc (abfd, amt);
		char stype = (*src);

		if (!new)
		  return FALSE;
		new->symbol.the_bfd = abfd;
		src++;
		abfd->symcount++;
		abfd->flags |= HAS_SYMS;
		new->prev = abfd->tdata.tekhex_data->symbols;
		abfd->tdata.tekhex_data->symbols = new;
		if (!getsym (sym, &src, &len))
		  return FALSE;
		new->symbol.name = bfd_alloc (abfd, (bfd_size_type) len + 1);
		if (!new->symbol.name)
		  return FALSE;
		memcpy ((char *) (new->symbol.name), sym, len + 1);
		new->symbol.section = section;
		if (stype <= '4')
		  new->symbol.flags = (BSF_GLOBAL | BSF_EXPORT);
		else
		  new->symbol.flags = BSF_LOCAL;
		if (!getvalue (&src, &val))
		  return FALSE;
		new->symbol.value = val - section->vma;
		break;
	      }
	    default:
	      return FALSE;
	    }
	}
    }

  return TRUE;
}

/* Pass over a tekhex, calling one of the above functions on each
   record.  */

static bfd_boolean
pass_over (bfd *abfd, bfd_boolean (*func) (bfd *, int, char *))
{
  unsigned int chars_on_line;
  bfd_boolean eof = FALSE;

  /* To the front of the file.  */
  if (bfd_seek (abfd, (file_ptr) 0, SEEK_SET) != 0)
    return FALSE;
  while (! eof)
    {
      char src[MAXCHUNK];
      char type;

      /* Find first '%'.  */
      eof = (bfd_boolean) (bfd_bread (src, (bfd_size_type) 1, abfd) != 1);
      while (*src != '%' && !eof)
	eof = (bfd_boolean) (bfd_bread (src, (bfd_size_type) 1, abfd) != 1);

      if (eof)
	break;

      /* Fetch the type and the length and the checksum.  */
      if (bfd_bread (src, (bfd_size_type) 5, abfd) != 5)
	return FALSE;

      type = src[2];

      if (!ISHEX (src[0]) || !ISHEX (src[1]))
	break;

      /* Already read five chars.  */
      chars_on_line = HEX (src) - 5;

      if (chars_on_line >= MAXCHUNK)
	return FALSE;

      if (bfd_bread (src, (bfd_size_type) chars_on_line, abfd) != chars_on_line)
	return FALSE;

      /* Put a null at the end.  */
      src[chars_on_line] = 0;

      if (!func (abfd, type, src))
	return FALSE;
    }

  return TRUE;
}

static long
tekhex_canonicalize_symtab (bfd *abfd, asymbol **table)
{
  tekhex_symbol_type *p = abfd->tdata.tekhex_data->symbols;
  unsigned int c = bfd_get_symcount (abfd);

  table[c] = 0;
  while (p)
    {
      table[--c] = &(p->symbol);
      p = p->prev;
    }

  return bfd_get_symcount (abfd);
}

static long
tekhex_get_symtab_upper_bound (bfd *abfd)
{
  return (abfd->symcount + 1) * (sizeof (struct tekhex_asymbol_struct *));

}

static bfd_boolean
tekhex_mkobject (bfd *abfd)
{
  tdata_type *tdata;

  tdata = bfd_alloc (abfd, (bfd_size_type) sizeof (tdata_type));
  if (!tdata)
    return FALSE;
  abfd->tdata.tekhex_data = tdata;
  tdata->type = 1;
  tdata->head =  NULL;
  tdata->symbols = NULL;
  tdata->data = NULL;
  return TRUE;
}

/* Return TRUE if the file looks like it's in TekHex format. Just look
   for a percent sign and some hex digits.  */

static const bfd_target *
tekhex_object_p (bfd *abfd)
{
  char b[4];

  tekhex_init ();

  if (bfd_seek (abfd, (file_ptr) 0, SEEK_SET) != 0
      || bfd_bread (b, (bfd_size_type) 4, abfd) != 4)
    return NULL;

  if (b[0] != '%' || !ISHEX (b[1]) || !ISHEX (b[2]) || !ISHEX (b[3]))
    return NULL;

  tekhex_mkobject (abfd);

  if (!pass_over (abfd, first_phase))
    return NULL;

  return abfd->xvec;
}

static void
move_section_contents (bfd *abfd,
		       asection *section,
		       const void * locationp,
		       file_ptr offset,
		       bfd_size_type count,
		       bfd_boolean get)
{
  bfd_vma addr;
  char *location = (char *) locationp;
  bfd_vma prev_number = 1;	/* Nothing can have this as a high bit.  */
  struct data_struct *d = NULL;

  BFD_ASSERT (offset == 0);
  for (addr = section->vma; count != 0; count--, addr++)
    {
      /* Get high bits of address.  */
      bfd_vma chunk_number = addr & ~(bfd_vma) CHUNK_MASK;
      bfd_vma low_bits = addr & CHUNK_MASK;

      if (chunk_number != prev_number)
	/* Different chunk, so move pointer. */
	d = find_chunk (abfd, chunk_number);

      if (get)
	{
	  if (d->chunk_init[low_bits])
	    *location = d->chunk_data[low_bits];
	  else
	    *location = 0;
	}
      else
	{
	  d->chunk_data[low_bits] = *location;
	  d->chunk_init[low_bits] = (*location != 0);
	}

      location++;
    }
}

static bfd_boolean
tekhex_get_section_contents (bfd *abfd,
			     asection *section,
			     void * locationp,
			     file_ptr offset,
			     bfd_size_type count)
{
  if (section->flags & (SEC_LOAD | SEC_ALLOC))
    {
      move_section_contents (abfd, section, locationp, offset, count, TRUE);
      return TRUE;
    }

  return FALSE;
}

static bfd_boolean
tekhex_set_arch_mach (bfd *abfd,
		      enum bfd_architecture arch,
		      unsigned long machine)
{
  return bfd_default_set_arch_mach (abfd, arch, machine);
}

/* We have to save up all the Tekhexords for a splurge before output.  */

static bfd_boolean
tekhex_set_section_contents (bfd *abfd,
			     sec_ptr section,
			     const void * locationp,
			     file_ptr offset,
			     bfd_size_type bytes_to_do)
{
  if (! abfd->output_has_begun)
    {
      /* The first time around, allocate enough sections to hold all the chunks.  */
      asection *s = abfd->sections;
      bfd_vma vma;

      for (s = abfd->sections; s; s = s->next)
	{
	  if (s->flags & SEC_LOAD)
	    {
	      for (vma = s->vma & ~(bfd_vma) CHUNK_MASK;
		   vma < s->vma + s->size;
		   vma += CHUNK_MASK)
		find_chunk (abfd, vma);
	    }
	}
    }

  if (section->flags & (SEC_LOAD | SEC_ALLOC))
    {
      move_section_contents (abfd, section, locationp, offset, bytes_to_do,
			     FALSE);
      return TRUE;
    }

  return FALSE;
}

static void
writevalue (char **dst, bfd_vma value)
{
  char *p = *dst;
  int len;
  int shift;

  for (len = 8, shift = 28; shift; shift -= 4, len--)
    {
      if ((value >> shift) & 0xf)
	{
	  *p++ = len + '0';
	  while (len)
	    {
	      *p++ = digs[(value >> shift) & 0xf];
	      shift -= 4;
	      len--;
	    }
	  *dst = p;
	  return;

	}
    }
  *p++ = '1';
  *p++ = '0';
  *dst = p;
}

static void
writesym (char **dst, const char *sym)
{
  char *p = *dst;
  int len = (sym ? strlen (sym) : 0);

  if (len >= 16)
    {
      *p++ = '0';
      len = 16;
    }
  else
    {
      if (len == 0)
	{
	  *p++ = '1';
	  sym = "$";
	  len = 1;
	}
      else
	*p++ = digs[len];
    }

  while (len--)
    *p++ = *sym++;

  *dst = p;
}

static void
out (bfd *abfd, int type, char *start, char *end)
{
  int sum = 0;
  char *s;
  char front[6];
  bfd_size_type wrlen;

  front[0] = '%';
  TOHEX (front + 1, end - start + 5);
  front[3] = type;

  for (s = start; s < end; s++)
    sum += sum_block[(unsigned char) *s];

  sum += sum_block[(unsigned char) front[1]];	/* Length.  */
  sum += sum_block[(unsigned char) front[2]];
  sum += sum_block[(unsigned char) front[3]];	/* Type.  */
  TOHEX (front + 4, sum);
  if (bfd_bwrite (front, (bfd_size_type) 6, abfd) != 6)
    abort ();
  end[0] = '\n';
  wrlen = end - start + 1;
  if (bfd_bwrite (start, wrlen, abfd) != wrlen)
    abort ();
}

static bfd_boolean
tekhex_write_object_contents (bfd *abfd)
{
  char buffer[100];
  asymbol **p;
  asection *s;
  struct data_struct *d;

  tekhex_init ();

  /* And the raw data.  */
  for (d = abfd->tdata.tekhex_data->data;
       d != NULL;
       d = d->next)
    {
      int low;

      const int span = 32;
      int addr;

      /* Write it in blocks of 32 bytes.  */
      for (addr = 0; addr < CHUNK_MASK + 1; addr += span)
	{
	  int need = 0;

	  /* Check to see if necessary.  */
	  for (low = 0; !need && low < span; low++)
	    if (d->chunk_init[addr + low])
	      need = 1;

	  if (need)
	    {
	      char *dst = buffer;

	      writevalue (&dst, addr + d->vma);
	      for (low = 0; low < span; low++)
		{
		  TOHEX (dst, d->chunk_data[addr + low]);
		  dst += 2;
		}
	      out (abfd, '6', buffer, dst);
	    }
	}
    }

  /* Write all the section headers for the sections.  */
  for (s = abfd->sections; s != NULL; s = s->next)
    {
      char *dst = buffer;

      writesym (&dst, s->name);
      *dst++ = '1';
      writevalue (&dst, s->vma);
      writevalue (&dst, s->vma + s->size);
      out (abfd, '3', buffer, dst);
    }

  /* And the symbols.  */
  if (abfd->outsymbols)
    {
      for (p = abfd->outsymbols; *p; p++)
	{
	  int section_code = bfd_decode_symclass (*p);

	  if (section_code != '?')
	    {
	      /* Do not include debug symbols.  */
	      asymbol *sym = *p;
	      char *dst = buffer;

	      writesym (&dst, sym->section->name);

	      switch (section_code)
		{
		case 'A':
		  *dst++ = '2';
		  break;
		case 'a':
		  *dst++ = '6';
		  break;
		case 'D':
		case 'B':
		case 'O':
		  *dst++ = '4';
		  break;
		case 'd':
		case 'b':
		case 'o':
		  *dst++ = '8';
		  break;
		case 'T':
		  *dst++ = '3';
		  break;
		case 't':
		  *dst++ = '7';
		  break;
		case 'C':
		case 'U':
		  bfd_set_error (bfd_error_wrong_format);
		  return FALSE;
		}

	      writesym (&dst, sym->name);
	      writevalue (&dst, sym->value + sym->section->vma);
	      out (abfd, '3', buffer, dst);
	    }
	}
    }

  /* And the terminator.  */
  if (bfd_bwrite ("%0781010\n", (bfd_size_type) 9, abfd) != 9)
    abort ();
  return TRUE;
}

static int
tekhex_sizeof_headers (bfd *abfd ATTRIBUTE_UNUSED,
		       struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  return 0;
}

static asymbol *
tekhex_make_empty_symbol (bfd *abfd)
{
  bfd_size_type amt = sizeof (struct tekhex_symbol_struct);
  tekhex_symbol_type *new = bfd_zalloc (abfd, amt);

  if (!new)
    return NULL;
  new->symbol.the_bfd = abfd;
  new->prev =  NULL;
  return &(new->symbol);
}

static void
tekhex_get_symbol_info (bfd *abfd ATTRIBUTE_UNUSED,
			asymbol *symbol,
			symbol_info *ret)
{
  bfd_symbol_info (symbol, ret);
}

static void
tekhex_print_symbol (bfd *abfd,
		     void * filep,
		     asymbol *symbol,
		     bfd_print_symbol_type how)
{
  FILE *file = (FILE *) filep;

  switch (how)
    {
    case bfd_print_symbol_name:
      fprintf (file, "%s", symbol->name);
      break;
    case bfd_print_symbol_more:
      break;

    case bfd_print_symbol_all:
      {
	const char *section_name = symbol->section->name;

	bfd_print_symbol_vandf (abfd, (void *) file, symbol);

	fprintf (file, " %-5s %s",
		 section_name, symbol->name);
      }
    }
}

#define	tekhex_close_and_cleanup                    _bfd_generic_close_and_cleanup
#define tekhex_bfd_free_cached_info                 _bfd_generic_bfd_free_cached_info
#define tekhex_new_section_hook                     _bfd_generic_new_section_hook
#define tekhex_bfd_is_target_special_symbol ((bfd_boolean (*) (bfd *, asymbol *)) bfd_false)
#define tekhex_bfd_is_local_label_name               bfd_generic_is_local_label_name
#define tekhex_get_lineno                           _bfd_nosymbols_get_lineno
#define tekhex_find_nearest_line                    _bfd_nosymbols_find_nearest_line
#define tekhex_find_inliner_info                    _bfd_nosymbols_find_inliner_info
#define tekhex_bfd_make_debug_symbol                _bfd_nosymbols_bfd_make_debug_symbol
#define tekhex_read_minisymbols                     _bfd_generic_read_minisymbols
#define tekhex_minisymbol_to_symbol                 _bfd_generic_minisymbol_to_symbol
#define tekhex_bfd_get_relocated_section_contents   bfd_generic_get_relocated_section_contents
#define tekhex_bfd_relax_section                    bfd_generic_relax_section
#define tekhex_bfd_gc_sections                      bfd_generic_gc_sections
#define tekhex_bfd_merge_sections                   bfd_generic_merge_sections
#define tekhex_bfd_is_group_section                 bfd_generic_is_group_section
#define tekhex_bfd_discard_group                    bfd_generic_discard_group
#define tekhex_section_already_linked               _bfd_generic_section_already_linked
#define tekhex_bfd_link_hash_table_create           _bfd_generic_link_hash_table_create
#define tekhex_bfd_link_hash_table_free             _bfd_generic_link_hash_table_free
#define tekhex_bfd_link_add_symbols                 _bfd_generic_link_add_symbols
#define tekhex_bfd_link_just_syms                   _bfd_generic_link_just_syms
#define tekhex_bfd_final_link                       _bfd_generic_final_link
#define tekhex_bfd_link_split_section               _bfd_generic_link_split_section
#define tekhex_get_section_contents_in_window       _bfd_generic_get_section_contents_in_window

const bfd_target tekhex_vec =
{
  "tekhex",			/* Name.  */
  bfd_target_tekhex_flavour,
  BFD_ENDIAN_UNKNOWN,		/* Target byte order.  */
  BFD_ENDIAN_UNKNOWN,		/* Target headers byte order.  */
  (EXEC_P |			/* Object flags.  */
   HAS_SYMS | HAS_LINENO | HAS_DEBUG |
   HAS_RELOC | HAS_LOCALS | WP_TEXT | D_PAGED),
  (SEC_CODE | SEC_DATA | SEC_ROM | SEC_HAS_CONTENTS
   | SEC_ALLOC | SEC_LOAD | SEC_RELOC),	/* Section flags.  */
  0,				/* Leading underscore.  */
  ' ',				/* AR_pad_char.  */
  16,				/* AR_max_namelen.  */
  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
  bfd_getb32, bfd_getb_signed_32, bfd_putb32,
  bfd_getb16, bfd_getb_signed_16, bfd_putb16,	/* Data.  */
  bfd_getb64, bfd_getb_signed_64, bfd_putb64,
  bfd_getb32, bfd_getb_signed_32, bfd_putb32,
  bfd_getb16, bfd_getb_signed_16, bfd_putb16,	/* Headers.  */

  {
    _bfd_dummy_target,
    tekhex_object_p,		/* bfd_check_format.  */
    _bfd_dummy_target,
    _bfd_dummy_target,
  },
  {
    bfd_false,
    tekhex_mkobject,
    _bfd_generic_mkarchive,
    bfd_false,
  },
  {				/* bfd_write_contents.  */
    bfd_false,
    tekhex_write_object_contents,
    _bfd_write_archive_contents,
    bfd_false,
  },

  BFD_JUMP_TABLE_GENERIC (tekhex),
  BFD_JUMP_TABLE_COPY (_bfd_generic),
  BFD_JUMP_TABLE_CORE (_bfd_nocore),
  BFD_JUMP_TABLE_ARCHIVE (_bfd_noarchive),
  BFD_JUMP_TABLE_SYMBOLS (tekhex),
  BFD_JUMP_TABLE_RELOCS (_bfd_norelocs),
  BFD_JUMP_TABLE_WRITE (tekhex),
  BFD_JUMP_TABLE_LINK (tekhex),
  BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

  NULL,

  NULL
};
