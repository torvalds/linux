/* BFD back-end for s-record objects.
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
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
	S-Record handling

   DESCRIPTION

	Ordinary S-Records cannot hold anything but addresses and
	data, so that's all that we implement.

	The only interesting thing is that S-Records may come out of
	order and there is no header, so an initial scan is required
	to discover the minimum and maximum addresses used to create
	the vma and size of the only section we create.  We
	arbitrarily call this section ".text".

	When bfd_get_section_contents is called the file is read
	again, and this time the data is placed into a bfd_alloc'd
	area.

	Any number of sections may be created for output, we save them
	up and output them when it's time to close the bfd.

	An s record looks like:

   EXAMPLE
	S<type><length><address><data><checksum>

   DESCRIPTION
	Where
	o length
	is the number of bytes following upto the checksum. Note that
	this is not the number of chars following, since it takes two
	chars to represent a byte.
	o type
	is one of:
	0) header record
	1) two byte address data record
	2) three byte address data record
	3) four byte address data record
	7) four byte address termination record
	8) three byte address termination record
	9) two byte address termination record

	o address
	is the start address of the data following, or in the case of
	a termination record, the start address of the image
	o data
	is the data.
	o checksum
	is the sum of all the raw byte data in the record, from the length
	upwards, modulo 256 and subtracted from 255.

   SUBSECTION
	Symbol S-Record handling

   DESCRIPTION
	Some ICE equipment understands an addition to the standard
	S-Record format; symbols and their addresses can be sent
	before the data.

	The format of this is:
	($$ <modulename>
		(<space> <symbol> <address>)*)
	$$

	so a short symbol table could look like:

   EXAMPLE
	$$ flash.x
	$$ flash.c
	  _port6 $0
	  _delay $4
	  _start $14
	  _etext $8036
	  _edata $8036
 	  _end $8036
	$$

   DESCRIPTION
	We allow symbols to be anywhere in the data stream - the module names
	are always ignored.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "libiberty.h"
#include "safe-ctype.h"


/* Macros for converting between hex and binary.  */

static const char digs[] = "0123456789ABCDEF";

#define NIBBLE(x)    hex_value(x)
#define HEX(buffer) ((NIBBLE ((buffer)[0])<<4) + NIBBLE ((buffer)[1]))
#define TOHEX(d, x, ch) \
	d[1] = digs[(x) & 0xf]; \
	d[0] = digs[((x)>>4)&0xf]; \
	ch += ((x) & 0xff);
#define	ISHEX(x)    hex_p(x)

/* The maximum number of address+data+crc bytes on a line is FF.  */
#define MAXCHUNK 0xff

/* Default size for a CHUNK.  */
#define DEFAULT_CHUNK 16

/* The number of data bytes we actually fit onto a line on output.
   This variable can be modified by objcopy's --srec-len parameter.
   For a 0x75 byte record you should set --srec-len=0x70.  */
unsigned int Chunk = DEFAULT_CHUNK;

/* The type of srec output (free or forced to S3).
   This variable can be modified by objcopy's --srec-forceS3
   parameter.  */
bfd_boolean S3Forced = FALSE;

/* When writing an S-record file, the S-records can not be output as
   they are seen.  This structure is used to hold them in memory.  */

struct srec_data_list_struct
{
  struct srec_data_list_struct *next;
  bfd_byte *data;
  bfd_vma where;
  bfd_size_type size;
};

typedef struct srec_data_list_struct srec_data_list_type;

/* When scanning the S-record file, a linked list of srec_symbol
   structures is built to represent the symbol table (if there is
   one).  */

struct srec_symbol
{
  struct srec_symbol *next;
  const char *name;
  bfd_vma val;
};

/* The S-record tdata information.  */

typedef struct srec_data_struct
  {
    srec_data_list_type *head;
    srec_data_list_type *tail;
    unsigned int type;
    struct srec_symbol *symbols;
    struct srec_symbol *symtail;
    asymbol *csymbols;
  }
tdata_type;

/* Initialize by filling in the hex conversion array.  */

static void
srec_init (void)
{
  static bfd_boolean inited = FALSE;

  if (! inited)
    {
      inited = TRUE;
      hex_init ();
    }
}

/* Set up the S-record tdata information.  */

static bfd_boolean
srec_mkobject (bfd *abfd)
{
  tdata_type *tdata;

  srec_init ();

  tdata = bfd_alloc (abfd, sizeof (tdata_type));
  if (tdata == NULL)
    return FALSE;

  abfd->tdata.srec_data = tdata;
  tdata->type = 1;
  tdata->head = NULL;
  tdata->tail = NULL;
  tdata->symbols = NULL;
  tdata->symtail = NULL;
  tdata->csymbols = NULL;

  return TRUE;
}

/* Read a byte from an S record file.  Set *ERRORPTR if an error
   occurred.  Return EOF on error or end of file.  */

static int
srec_get_byte (bfd *abfd, bfd_boolean *errorptr)
{
  bfd_byte c;

  if (bfd_bread (&c, (bfd_size_type) 1, abfd) != 1)
    {
      if (bfd_get_error () != bfd_error_file_truncated)
	*errorptr = TRUE;
      return EOF;
    }

  return (int) (c & 0xff);
}

/* Report a problem in an S record file.  FIXME: This probably should
   not call fprintf, but we really do need some mechanism for printing
   error messages.  */

static void
srec_bad_byte (bfd *abfd,
	       unsigned int lineno,
	       int c,
	       bfd_boolean error)
{
  if (c == EOF)
    {
      if (! error)
	bfd_set_error (bfd_error_file_truncated);
    }
  else
    {
      char buf[10];

      if (! ISPRINT (c))
	sprintf (buf, "\\%03o", (unsigned int) c);
      else
	{
	  buf[0] = c;
	  buf[1] = '\0';
	}
      (*_bfd_error_handler)
	(_("%B:%d: Unexpected character `%s' in S-record file\n"),
	 abfd, lineno, buf);
      bfd_set_error (bfd_error_bad_value);
    }
}

/* Add a new symbol found in an S-record file.  */

static bfd_boolean
srec_new_symbol (bfd *abfd, const char *name, bfd_vma val)
{
  struct srec_symbol *n;

  n = bfd_alloc (abfd, sizeof (* n));
  if (n == NULL)
    return FALSE;

  n->name = name;
  n->val = val;

  if (abfd->tdata.srec_data->symbols == NULL)
    abfd->tdata.srec_data->symbols = n;
  else
    abfd->tdata.srec_data->symtail->next = n;
  abfd->tdata.srec_data->symtail = n;
  n->next = NULL;

  ++abfd->symcount;

  return TRUE;
}

/* Read the S record file and turn it into sections.  We create a new
   section for each contiguous set of bytes.  */

static bfd_boolean
srec_scan (bfd *abfd)
{
  int c;
  unsigned int lineno = 1;
  bfd_boolean error = FALSE;
  bfd_byte *buf = NULL;
  size_t bufsize = 0;
  asection *sec = NULL;
  char *symbuf = NULL;

  if (bfd_seek (abfd, (file_ptr) 0, SEEK_SET) != 0)
    goto error_return;

  while ((c = srec_get_byte (abfd, &error)) != EOF)
    {
      /* We only build sections from contiguous S-records, so if this
	 is not an S-record, then stop building a section.  */
      if (c != 'S' && c != '\r' && c != '\n')
	sec = NULL;

      switch (c)
	{
	default:
	  srec_bad_byte (abfd, lineno, c, error);
	  goto error_return;

	case '\n':
	  ++lineno;
	  break;

	case '\r':
	  break;

	case '$':
	  /* Starting a module name, which we ignore.  */
	  while ((c = srec_get_byte (abfd, &error)) != '\n'
		 && c != EOF)
	    ;
	  if (c == EOF)
	    {
	      srec_bad_byte (abfd, lineno, c, error);
	      goto error_return;
	    }

	  ++lineno;
	  break;

	case ' ':
	  do
	    {
	      bfd_size_type alc;
	      char *p, *symname;
	      bfd_vma symval;

	      /* Starting a symbol definition.  */
	      while ((c = srec_get_byte (abfd, &error)) != EOF
		     && (c == ' ' || c == '\t'))
		;

	      if (c == '\n' || c == '\r')
		break;

	      if (c == EOF)
		{
		  srec_bad_byte (abfd, lineno, c, error);
		  goto error_return;
		}

	      alc = 10;
	      symbuf = bfd_malloc (alc + 1);
	      if (symbuf == NULL)
		goto error_return;

	      p = symbuf;

	      *p++ = c;
	      while ((c = srec_get_byte (abfd, &error)) != EOF
		     && ! ISSPACE (c))
		{
		  if ((bfd_size_type) (p - symbuf) >= alc)
		    {
		      char *n;

		      alc *= 2;
		      n = bfd_realloc (symbuf, alc + 1);
		      if (n == NULL)
			goto error_return;
		      p = n + (p - symbuf);
		      symbuf = n;
		    }

		  *p++ = c;
		}

	      if (c == EOF)
		{
		  srec_bad_byte (abfd, lineno, c, error);
		  goto error_return;
		}

	      *p++ = '\0';
	      symname = bfd_alloc (abfd, (bfd_size_type) (p - symbuf));
	      if (symname == NULL)
		goto error_return;
	      strcpy (symname, symbuf);
	      free (symbuf);
	      symbuf = NULL;

	      while ((c = srec_get_byte (abfd, &error)) != EOF
		     && (c == ' ' || c == '\t'))
		;
	      if (c == EOF)
		{
		  srec_bad_byte (abfd, lineno, c, error);
		  goto error_return;
		}

	      /* Skip a dollar sign before the hex value.  */
	      if (c == '$')
		{
		  c = srec_get_byte (abfd, &error);
		  if (c == EOF)
		    {
		      srec_bad_byte (abfd, lineno, c, error);
		      goto error_return;
		    }
		}

	      symval = 0;
	      while (ISHEX (c))
		{
		  symval <<= 4;
		  symval += NIBBLE (c);
		  c = srec_get_byte (abfd, &error);
		}

	      if (! srec_new_symbol (abfd, symname, symval))
		goto error_return;
	    }
	  while (c == ' ' || c == '\t')
	    ;

	  if (c == '\n')
	    ++lineno;
	  else if (c != '\r')
	    {
	      srec_bad_byte (abfd, lineno, c, error);
	      goto error_return;
	    }

	  break;

	case 'S':
	  {
	    file_ptr pos;
	    char hdr[3];
	    unsigned int bytes;
	    bfd_vma address;
	    bfd_byte *data;

	    /* Starting an S-record.  */

	    pos = bfd_tell (abfd) - 1;

	    if (bfd_bread (hdr, (bfd_size_type) 3, abfd) != 3)
	      goto error_return;

	    if (! ISHEX (hdr[1]) || ! ISHEX (hdr[2]))
	      {
		if (! ISHEX (hdr[1]))
		  c = hdr[1];
		else
		  c = hdr[2];
		srec_bad_byte (abfd, lineno, c, error);
		goto error_return;
	      }

	    bytes = HEX (hdr + 1);
	    if (bytes * 2 > bufsize)
	      {
		if (buf != NULL)
		  free (buf);
		buf = bfd_malloc ((bfd_size_type) bytes * 2);
		if (buf == NULL)
		  goto error_return;
		bufsize = bytes * 2;
	      }

	    if (bfd_bread (buf, (bfd_size_type) bytes * 2, abfd) != bytes * 2)
	      goto error_return;

	    /* Ignore the checksum byte.  */
	    --bytes;

	    address = 0;
	    data = buf;
	    switch (hdr[0])
	      {
	      case '0':
	      case '5':
		/* Prologue--ignore the file name, but stop building a
		   section at this point.  */
		sec = NULL;
		break;

	      case '3':
		address = HEX (data);
		data += 2;
		--bytes;
		/* Fall through.  */
	      case '2':
		address = (address << 8) | HEX (data);
		data += 2;
		--bytes;
		/* Fall through.  */
	      case '1':
		address = (address << 8) | HEX (data);
		data += 2;
		address = (address << 8) | HEX (data);
		data += 2;
		bytes -= 2;

		if (sec != NULL
		    && sec->vma + sec->size == address)
		  {
		    /* This data goes at the end of the section we are
		       currently building.  */
		    sec->size += bytes;
		  }
		else
		  {
		    char secbuf[20];
		    char *secname;
		    bfd_size_type amt;
		    flagword flags;

		    sprintf (secbuf, ".sec%d", bfd_count_sections (abfd) + 1);
		    amt = strlen (secbuf) + 1;
		    secname = bfd_alloc (abfd, amt);
		    strcpy (secname, secbuf);
		    flags = SEC_HAS_CONTENTS | SEC_LOAD | SEC_ALLOC;
		    sec = bfd_make_section_with_flags (abfd, secname, flags);
		    if (sec == NULL)
		      goto error_return;
		    sec->vma = address;
		    sec->lma = address;
		    sec->size = bytes;
		    sec->filepos = pos;
		  }
		break;

	      case '7':
		address = HEX (data);
		data += 2;
		/* Fall through.  */
	      case '8':
		address = (address << 8) | HEX (data);
		data += 2;
		/* Fall through.  */
	      case '9':
		address = (address << 8) | HEX (data);
		data += 2;
		address = (address << 8) | HEX (data);
		data += 2;

		/* This is a termination record.  */
		abfd->start_address = address;

		if (buf != NULL)
		  free (buf);

		return TRUE;
	      }
	  }
	  break;
	}
    }

  if (error)
    goto error_return;

  if (buf != NULL)
    free (buf);

  return TRUE;

 error_return:
  if (symbuf != NULL)
    free (symbuf);
  if (buf != NULL)
    free (buf);
  return FALSE;
}

/* Check whether an existing file is an S-record file.  */

static const bfd_target *
srec_object_p (bfd *abfd)
{
  void * tdata_save;
  bfd_byte b[4];

  srec_init ();

  if (bfd_seek (abfd, (file_ptr) 0, SEEK_SET) != 0
      || bfd_bread (b, (bfd_size_type) 4, abfd) != 4)
    return NULL;

  if (b[0] != 'S' || !ISHEX (b[1]) || !ISHEX (b[2]) || !ISHEX (b[3]))
    {
      bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  tdata_save = abfd->tdata.any;
  if (! srec_mkobject (abfd) || ! srec_scan (abfd))
    {
      if (abfd->tdata.any != tdata_save && abfd->tdata.any != NULL)
	bfd_release (abfd, abfd->tdata.any);
      abfd->tdata.any = tdata_save;
      return NULL;
    }

  if (abfd->symcount > 0)
    abfd->flags |= HAS_SYMS;

  return abfd->xvec;
}

/* Check whether an existing file is an S-record file with symbols.  */

static const bfd_target *
symbolsrec_object_p (bfd *abfd)
{
  void * tdata_save;
  char b[2];

  srec_init ();

  if (bfd_seek (abfd, (file_ptr) 0, SEEK_SET) != 0
      || bfd_bread (b, (bfd_size_type) 2, abfd) != 2)
    return NULL;

  if (b[0] != '$' || b[1] != '$')
    {
      bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  tdata_save = abfd->tdata.any;
  if (! srec_mkobject (abfd) || ! srec_scan (abfd))
    {
      if (abfd->tdata.any != tdata_save && abfd->tdata.any != NULL)
	bfd_release (abfd, abfd->tdata.any);
      abfd->tdata.any = tdata_save;
      return NULL;
    }

  if (abfd->symcount > 0)
    abfd->flags |= HAS_SYMS;

  return abfd->xvec;
}

/* Read in the contents of a section in an S-record file.  */

static bfd_boolean
srec_read_section (bfd *abfd, asection *section, bfd_byte *contents)
{
  int c;
  bfd_size_type sofar = 0;
  bfd_boolean error = FALSE;
  bfd_byte *buf = NULL;
  size_t bufsize = 0;

  if (bfd_seek (abfd, section->filepos, SEEK_SET) != 0)
    goto error_return;

  while ((c = srec_get_byte (abfd, &error)) != EOF)
    {
      bfd_byte hdr[3];
      unsigned int bytes;
      bfd_vma address;
      bfd_byte *data;

      if (c == '\r' || c == '\n')
	continue;

      /* This is called after srec_scan has already been called, so we
	 ought to know the exact format.  */
      BFD_ASSERT (c == 'S');

      if (bfd_bread (hdr, (bfd_size_type) 3, abfd) != 3)
	goto error_return;

      BFD_ASSERT (ISHEX (hdr[1]) && ISHEX (hdr[2]));

      bytes = HEX (hdr + 1);

      if (bytes * 2 > bufsize)
	{
	  if (buf != NULL)
	    free (buf);
	  buf = bfd_malloc ((bfd_size_type) bytes * 2);
	  if (buf == NULL)
	    goto error_return;
	  bufsize = bytes * 2;
	}

      if (bfd_bread (buf, (bfd_size_type) bytes * 2, abfd) != bytes * 2)
	goto error_return;

      address = 0;
      data = buf;
      switch (hdr[0])
	{
	default:
	  BFD_ASSERT (sofar == section->size);
	  if (buf != NULL)
	    free (buf);
	  return TRUE;

	case '3':
	  address = HEX (data);
	  data += 2;
	  --bytes;
	  /* Fall through.  */
	case '2':
	  address = (address << 8) | HEX (data);
	  data += 2;
	  --bytes;
	  /* Fall through.  */
	case '1':
	  address = (address << 8) | HEX (data);
	  data += 2;
	  address = (address << 8) | HEX (data);
	  data += 2;
	  bytes -= 2;

	  if (address != section->vma + sofar)
	    {
	      /* We've come to the end of this section.  */
	      BFD_ASSERT (sofar == section->size);
	      if (buf != NULL)
		free (buf);
	      return TRUE;
	    }

	  /* Don't consider checksum.  */
	  --bytes;

	  while (bytes-- != 0)
	    {
	      contents[sofar] = HEX (data);
	      data += 2;
	      ++sofar;
	    }

	  break;
	}
    }

  if (error)
    goto error_return;

  BFD_ASSERT (sofar == section->size);

  if (buf != NULL)
    free (buf);

  return TRUE;

 error_return:
  if (buf != NULL)
    free (buf);
  return FALSE;
}

/* Get the contents of a section in an S-record file.  */

static bfd_boolean
srec_get_section_contents (bfd *abfd,
			   asection *section,
			   void * location,
			   file_ptr offset,
			   bfd_size_type count)
{
  if (section->used_by_bfd == NULL)
    {
      section->used_by_bfd = bfd_alloc (abfd, section->size);
      if (section->used_by_bfd == NULL && section->size != 0)
	return FALSE;

      if (! srec_read_section (abfd, section, section->used_by_bfd))
	return FALSE;
    }

  memcpy (location, (bfd_byte *) section->used_by_bfd + offset,
	  (size_t) count);

  return TRUE;
}

/* Set the architecture.  We accept an unknown architecture here.  */

static bfd_boolean
srec_set_arch_mach (bfd *abfd, enum bfd_architecture arch, unsigned long mach)
{
  if (arch != bfd_arch_unknown)
    return bfd_default_set_arch_mach (abfd, arch, mach);

  abfd->arch_info = & bfd_default_arch_struct;
  return TRUE;
}

/* We have to save up all the Srecords for a splurge before output.  */

static bfd_boolean
srec_set_section_contents (bfd *abfd,
			   sec_ptr section,
			   const void * location,
			   file_ptr offset,
			   bfd_size_type bytes_to_do)
{
  tdata_type *tdata = abfd->tdata.srec_data;
  srec_data_list_type *entry;

  entry = bfd_alloc (abfd, sizeof (* entry));
  if (entry == NULL)
    return FALSE;

  if (bytes_to_do
      && (section->flags & SEC_ALLOC)
      && (section->flags & SEC_LOAD))
    {
      bfd_byte *data;

      data = bfd_alloc (abfd, bytes_to_do);
      if (data == NULL)
	return FALSE;
      memcpy ((void *) data, location, (size_t) bytes_to_do);

      /* Ff S3Forced is TRUE then always select S3 records,
	 regardless of the siez of the addresses.  */
      if (S3Forced)
	tdata->type = 3;
      else if ((section->lma + offset + bytes_to_do - 1) <= 0xffff)
	;  /* The default, S1, is OK.  */
      else if ((section->lma + offset + bytes_to_do - 1) <= 0xffffff
	       && tdata->type <= 2)
	tdata->type = 2;
      else
	tdata->type = 3;

      entry->data = data;
      entry->where = section->lma + offset;
      entry->size = bytes_to_do;

      /* Sort the records by address.  Optimize for the common case of
	 adding a record to the end of the list.  */
      if (tdata->tail != NULL
	  && entry->where >= tdata->tail->where)
	{
	  tdata->tail->next = entry;
	  entry->next = NULL;
	  tdata->tail = entry;
	}
      else
	{
	  srec_data_list_type **look;

	  for (look = &tdata->head;
	       *look != NULL && (*look)->where < entry->where;
	       look = &(*look)->next)
	    ;
	  entry->next = *look;
	  *look = entry;
	  if (entry->next == NULL)
	    tdata->tail = entry;
	}
    }
  return TRUE;
}

/* Write a record of type, of the supplied number of bytes. The
   supplied bytes and length don't have a checksum. That's worked out
   here.  */

static bfd_boolean
srec_write_record (bfd *abfd,
		   unsigned int type,
		   bfd_vma address,
		   const bfd_byte *data,
		   const bfd_byte *end)
{
  char buffer[2 * MAXCHUNK + 6];
  unsigned int check_sum = 0;
  const bfd_byte *src = data;
  char *dst = buffer;
  char *length;
  bfd_size_type wrlen;

  *dst++ = 'S';
  *dst++ = '0' + type;

  length = dst;
  dst += 2;			/* Leave room for dst.  */

  switch (type)
    {
    case 3:
    case 7:
      TOHEX (dst, (address >> 24), check_sum);
      dst += 2;
    case 8:
    case 2:
      TOHEX (dst, (address >> 16), check_sum);
      dst += 2;
    case 9:
    case 1:
    case 0:
      TOHEX (dst, (address >> 8), check_sum);
      dst += 2;
      TOHEX (dst, (address), check_sum);
      dst += 2;
      break;

    }
  for (src = data; src < end; src++)
    {
      TOHEX (dst, *src, check_sum);
      dst += 2;
    }

  /* Fill in the length.  */
  TOHEX (length, (dst - length) / 2, check_sum);
  check_sum &= 0xff;
  check_sum = 255 - check_sum;
  TOHEX (dst, check_sum, check_sum);
  dst += 2;

  *dst++ = '\r';
  *dst++ = '\n';
  wrlen = dst - buffer;

  return bfd_bwrite ((void *) buffer, wrlen, abfd) == wrlen;
}

static bfd_boolean
srec_write_header (bfd *abfd)
{
  unsigned int len = strlen (abfd->filename);

  /* I'll put an arbitrary 40 char limit on header size.  */
  if (len > 40)
    len = 40;

  return srec_write_record (abfd, 0, (bfd_vma) 0,
			    (bfd_byte *) abfd->filename,
			    (bfd_byte *) abfd->filename + len);
}

static bfd_boolean
srec_write_section (bfd *abfd,
		    tdata_type *tdata,
		    srec_data_list_type *list)
{
  unsigned int octets_written = 0;
  bfd_byte *location = list->data;

  /* Validate number of data bytes to write.  The srec length byte
     counts the address, data and crc bytes.  S1 (tdata->type == 1)
     records have two address bytes, S2 (tdata->type == 2) records
     have three, and S3 (tdata->type == 3) records have four.
     The total length can't exceed 255, and a zero data length will
     spin for a long time.  */
  if (Chunk == 0)
    Chunk = 1;
  else if (Chunk > MAXCHUNK - tdata->type - 2)
    Chunk = MAXCHUNK - tdata->type - 2;

  while (octets_written < list->size)
    {
      bfd_vma address;
      unsigned int octets_this_chunk = list->size - octets_written;

      if (octets_this_chunk > Chunk)
	octets_this_chunk = Chunk;

      address = list->where + octets_written / bfd_octets_per_byte (abfd);

      if (! srec_write_record (abfd,
			       tdata->type,
			       address,
			       location,
			       location + octets_this_chunk))
	return FALSE;

      octets_written += octets_this_chunk;
      location += octets_this_chunk;
    }

  return TRUE;
}

static bfd_boolean
srec_write_terminator (bfd *abfd, tdata_type *tdata)
{
  return srec_write_record (abfd, 10 - tdata->type,
			    abfd->start_address, NULL, NULL);
}

static bfd_boolean
srec_write_symbols (bfd *abfd)
{
  /* Dump out the symbols of a bfd.  */
  int i;
  int count = bfd_get_symcount (abfd);

  if (count)
    {
      bfd_size_type len;
      asymbol **table = bfd_get_outsymbols (abfd);

      len = strlen (abfd->filename);
      if (bfd_bwrite ("$$ ", (bfd_size_type) 3, abfd) != 3
	  || bfd_bwrite (abfd->filename, len, abfd) != len
	  || bfd_bwrite ("\r\n", (bfd_size_type) 2, abfd) != 2)
	return FALSE;

      for (i = 0; i < count; i++)
	{
	  asymbol *s = table[i];
	  if (! bfd_is_local_label (abfd, s)
	      && (s->flags & BSF_DEBUGGING) == 0)
	    {
	      /* Just dump out non debug symbols.  */
	      char buf[43], *p;

	      len = strlen (s->name);
	      if (bfd_bwrite ("  ", (bfd_size_type) 2, abfd) != 2
		  || bfd_bwrite (s->name, len, abfd) != len)
		return FALSE;

	      sprintf_vma (buf + 2, (s->value
				     + s->section->output_section->lma
				     + s->section->output_offset));
	      p = buf + 2;
	      while (p[0] == '0' && p[1] != 0)
		p++;
	      len = strlen (p);
	      p[len] = '\r';
	      p[len + 1] = '\n';
	      *--p = '$';
	      *--p = ' ';
	      len += 4;
	      if (bfd_bwrite (p, len, abfd) != len)
		return FALSE;
	    }
	}
      if (bfd_bwrite ("$$ \r\n", (bfd_size_type) 5, abfd) != 5)
	return FALSE;
    }

  return TRUE;
}

static bfd_boolean
internal_srec_write_object_contents (bfd *abfd, int symbols)
{
  tdata_type *tdata = abfd->tdata.srec_data;
  srec_data_list_type *list;

  if (symbols)
    {
      if (! srec_write_symbols (abfd))
	return FALSE;
    }

  if (! srec_write_header (abfd))
    return FALSE;

  /* Now wander though all the sections provided and output them.  */
  list = tdata->head;

  while (list != (srec_data_list_type *) NULL)
    {
      if (! srec_write_section (abfd, tdata, list))
	return FALSE;
      list = list->next;
    }
  return srec_write_terminator (abfd, tdata);
}

static bfd_boolean
srec_write_object_contents (bfd *abfd)
{
  return internal_srec_write_object_contents (abfd, 0);
}

static bfd_boolean
symbolsrec_write_object_contents (bfd *abfd)
{
  return internal_srec_write_object_contents (abfd, 1);
}

static int
srec_sizeof_headers (bfd *abfd ATTRIBUTE_UNUSED,
		     struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  return 0;
}

/* Return the amount of memory needed to read the symbol table.  */

static long
srec_get_symtab_upper_bound (bfd *abfd)
{
  return (bfd_get_symcount (abfd) + 1) * sizeof (asymbol *);
}

/* Return the symbol table.  */

static long
srec_canonicalize_symtab (bfd *abfd, asymbol **alocation)
{
  bfd_size_type symcount = bfd_get_symcount (abfd);
  asymbol *csymbols;
  unsigned int i;

  csymbols = abfd->tdata.srec_data->csymbols;
  if (csymbols == NULL)
    {
      asymbol *c;
      struct srec_symbol *s;

      csymbols = bfd_alloc (abfd, symcount * sizeof (asymbol));
      if (csymbols == NULL && symcount != 0)
	return 0;
      abfd->tdata.srec_data->csymbols = csymbols;

      for (s = abfd->tdata.srec_data->symbols, c = csymbols;
	   s != NULL;
	   s = s->next, ++c)
	{
	  c->the_bfd = abfd;
	  c->name = s->name;
	  c->value = s->val;
	  c->flags = BSF_GLOBAL;
	  c->section = bfd_abs_section_ptr;
	  c->udata.p = NULL;
	}
    }

  for (i = 0; i < symcount; i++)
    *alocation++ = csymbols++;
  *alocation = NULL;

  return symcount;
}

static void
srec_get_symbol_info (bfd *ignore_abfd ATTRIBUTE_UNUSED,
		      asymbol *symbol,
		      symbol_info *ret)
{
  bfd_symbol_info (symbol, ret);
}

static void
srec_print_symbol (bfd *abfd,
		   void * afile,
		   asymbol *symbol,
		   bfd_print_symbol_type how)
{
  FILE *file = (FILE *) afile;

  switch (how)
    {
    case bfd_print_symbol_name:
      fprintf (file, "%s", symbol->name);
      break;
    default:
      bfd_print_symbol_vandf (abfd, (void *) file, symbol);
      fprintf (file, " %-5s %s",
	       symbol->section->name,
	       symbol->name);
    }
}

#define	srec_close_and_cleanup                    _bfd_generic_close_and_cleanup
#define srec_bfd_free_cached_info                 _bfd_generic_bfd_free_cached_info
#define srec_new_section_hook                     _bfd_generic_new_section_hook
#define srec_bfd_is_target_special_symbol         ((bfd_boolean (*) (bfd *, asymbol *)) bfd_false)
#define srec_bfd_is_local_label_name              bfd_generic_is_local_label_name
#define srec_get_lineno                           _bfd_nosymbols_get_lineno
#define srec_find_nearest_line                    _bfd_nosymbols_find_nearest_line
#define srec_find_inliner_info                    _bfd_nosymbols_find_inliner_info
#define srec_make_empty_symbol                    _bfd_generic_make_empty_symbol
#define srec_bfd_make_debug_symbol                _bfd_nosymbols_bfd_make_debug_symbol
#define srec_read_minisymbols                     _bfd_generic_read_minisymbols
#define srec_minisymbol_to_symbol                 _bfd_generic_minisymbol_to_symbol
#define srec_get_section_contents_in_window       _bfd_generic_get_section_contents_in_window
#define srec_bfd_get_relocated_section_contents   bfd_generic_get_relocated_section_contents
#define srec_bfd_relax_section                    bfd_generic_relax_section
#define srec_bfd_gc_sections                      bfd_generic_gc_sections
#define srec_bfd_merge_sections                   bfd_generic_merge_sections
#define srec_bfd_is_group_section                 bfd_generic_is_group_section
#define srec_bfd_discard_group                    bfd_generic_discard_group
#define srec_section_already_linked               _bfd_generic_section_already_linked
#define srec_bfd_link_hash_table_create           _bfd_generic_link_hash_table_create
#define srec_bfd_link_hash_table_free             _bfd_generic_link_hash_table_free
#define srec_bfd_link_add_symbols                 _bfd_generic_link_add_symbols
#define srec_bfd_link_just_syms                   _bfd_generic_link_just_syms
#define srec_bfd_final_link                       _bfd_generic_final_link
#define srec_bfd_link_split_section               _bfd_generic_link_split_section

const bfd_target srec_vec =
{
  "srec",			/* Name.  */
  bfd_target_srec_flavour,
  BFD_ENDIAN_UNKNOWN,		/* Target byte order.  */
  BFD_ENDIAN_UNKNOWN,		/* Target headers byte order.  */
  (HAS_RELOC | EXEC_P |		/* Object flags.  */
   HAS_LINENO | HAS_DEBUG |
   HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),
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
  bfd_getb16, bfd_getb_signed_16, bfd_putb16,	/* Hdrs.  */

  {
    _bfd_dummy_target,
    srec_object_p,		/* bfd_check_format.  */
    _bfd_dummy_target,
    _bfd_dummy_target,
  },
  {
    bfd_false,
    srec_mkobject,
    _bfd_generic_mkarchive,
    bfd_false,
  },
  {				/* bfd_write_contents.  */
    bfd_false,
    srec_write_object_contents,
    _bfd_write_archive_contents,
    bfd_false,
  },

  BFD_JUMP_TABLE_GENERIC (srec),
  BFD_JUMP_TABLE_COPY (_bfd_generic),
  BFD_JUMP_TABLE_CORE (_bfd_nocore),
  BFD_JUMP_TABLE_ARCHIVE (_bfd_noarchive),
  BFD_JUMP_TABLE_SYMBOLS (srec),
  BFD_JUMP_TABLE_RELOCS (_bfd_norelocs),
  BFD_JUMP_TABLE_WRITE (srec),
  BFD_JUMP_TABLE_LINK (srec),
  BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

  NULL,

  NULL
};

const bfd_target symbolsrec_vec =
{
  "symbolsrec",			/* Name.  */
  bfd_target_srec_flavour,
  BFD_ENDIAN_UNKNOWN,		/* Target byte order.  */
  BFD_ENDIAN_UNKNOWN,		/* Target headers byte order.  */
  (HAS_RELOC | EXEC_P |		/* Object flags.  */
   HAS_LINENO | HAS_DEBUG |
   HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),
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
    symbolsrec_object_p,	/* bfd_check_format.  */
    _bfd_dummy_target,
    _bfd_dummy_target,
  },
  {
    bfd_false,
    srec_mkobject,
    _bfd_generic_mkarchive,
    bfd_false,
  },
  {				/* bfd_write_contents.  */
    bfd_false,
    symbolsrec_write_object_contents,
    _bfd_write_archive_contents,
    bfd_false,
  },

  BFD_JUMP_TABLE_GENERIC (srec),
  BFD_JUMP_TABLE_COPY (_bfd_generic),
  BFD_JUMP_TABLE_CORE (_bfd_nocore),
  BFD_JUMP_TABLE_ARCHIVE (_bfd_noarchive),
  BFD_JUMP_TABLE_SYMBOLS (srec),
  BFD_JUMP_TABLE_RELOCS (_bfd_norelocs),
  BFD_JUMP_TABLE_WRITE (srec),
  BFD_JUMP_TABLE_LINK (srec),
  BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

  NULL,

  NULL
};
