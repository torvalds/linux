/* resres.c: read_res_file and write_res_file implementation for windres.
   Copyright 1998, 1999, 2001, 2002, 2007
   Free Software Foundation, Inc.
   Written by Anders Norlander <anorland@hem2.passagen.se>.
   Rewritten by Kai Tietz, Onevision.

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

/* FIXME: This file does not work correctly in a cross configuration.
   It assumes that it can use fread and fwrite to read and write
   integers.  It does no swapping.  */

#include "sysdep.h"
#include "bfd.h"
#include "bucomm.h"
#include "libiberty.h"
#include "windres.h"

#include <assert.h>
#include <time.h>

static rc_uint_type write_res_directory (windres_bfd *, rc_uint_type,
				    	 const rc_res_directory *, const rc_res_id *,
				    	 const rc_res_id *, rc_uint_type *, int);
static rc_uint_type write_res_resource (windres_bfd *, rc_uint_type,const rc_res_id *,
				   	const rc_res_id *, const rc_res_resource *,
				   	rc_uint_type *);
static rc_uint_type write_res_bin (windres_bfd *, rc_uint_type, const rc_res_resource *,
				   const rc_res_id *, const rc_res_id *,
				   const rc_res_res_info *);

static rc_uint_type write_res_id (windres_bfd *, rc_uint_type, const rc_res_id *);
static rc_uint_type write_res_info (windres_bfd *, rc_uint_type, const rc_res_res_info *);
static rc_uint_type write_res_data_hdr (windres_bfd *, rc_uint_type, res_hdr *);

static rc_uint_type write_res_header (windres_bfd *, rc_uint_type, rc_uint_type,
				      const rc_res_id *, const rc_res_id *,
				      const rc_res_res_info *);

static int read_resource_entry (windres_bfd *, rc_uint_type *, rc_uint_type);
static void read_res_data (windres_bfd *, rc_uint_type *, rc_uint_type, void *,
			   rc_uint_type);
static void read_res_data_hdr (windres_bfd *, rc_uint_type *, rc_uint_type, res_hdr *);
static void read_res_id (windres_bfd *, rc_uint_type *, rc_uint_type, rc_res_id *);
static unichar *read_unistring (windres_bfd *, rc_uint_type *, rc_uint_type, rc_uint_type *);
static void skip_null_resource (windres_bfd *, rc_uint_type *, rc_uint_type);
static int probe_binary (windres_bfd *wrbfd, rc_uint_type);

static unsigned long get_id_size (const rc_res_id *);

static void res_add_resource (rc_res_resource *, const rc_res_id *,
			      const rc_res_id *, rc_uint_type, int);

static void res_append_resource (rc_res_directory **, rc_res_resource *,
				 int, const rc_res_id *, int);

static rc_res_directory *resources = NULL;

static const char *filename;

extern char *program_name;

/* Read resource file */
rc_res_directory *
read_res_file (const char *fn)
{
  rc_uint_type off, flen;
  windres_bfd wrbfd;
  bfd *abfd;
  asection *sec;
  filename = fn;

  flen = (rc_uint_type) get_file_size (filename);
  if (! flen)
    fatal ("can't open '%s' for input.", filename);
  abfd = windres_open_as_binary (filename, 1);
  sec = bfd_get_section_by_name (abfd, ".data");
  if (sec == NULL)
    bfd_fatal ("bfd_get_section_by_name");
  set_windres_bfd (&wrbfd, abfd, sec,
		   (target_is_bigendian ? WR_KIND_BFD_BIN_B
					: WR_KIND_BFD_BIN_L));
  off = 0;

  if (! probe_binary (&wrbfd, flen))
    set_windres_bfd_endianess (&wrbfd, ! target_is_bigendian);

  skip_null_resource (&wrbfd, &off, flen);

  while (read_resource_entry (&wrbfd, &off, flen))
    ;

  bfd_close (abfd);

  return resources;
}

/* Write resource file */
void
write_res_file (const char *fn,const rc_res_directory *resdir)
{
  asection *sec;
  rc_uint_type language;
  bfd *abfd;
  windres_bfd wrbfd;
  unsigned long sec_length = 0,sec_length_wrote;
  static const bfd_byte sign[] =
  {0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
   0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  filename = fn;

  abfd = windres_open_as_binary (filename, 0);
  sec = bfd_make_section (abfd, ".data");
  if (sec == NULL)
    bfd_fatal ("bfd_make_section");
  if (! bfd_set_section_flags (abfd, sec,
			       (SEC_HAS_CONTENTS | SEC_ALLOC
			        | SEC_LOAD | SEC_DATA)))
    bfd_fatal ("bfd_set_section_flags");
  /* Requiring this is probably a bug in BFD.  */
  sec->output_section = sec;

  set_windres_bfd (&wrbfd, abfd, sec,
		   (target_is_bigendian ? WR_KIND_BFD_BIN_B
					: WR_KIND_BFD_BIN_L));

  language = -1;
  sec_length = write_res_directory ((windres_bfd *) NULL, 0x20UL, resdir,
				    (const rc_res_id *) NULL,
				    (const rc_res_id *) NULL, &language, 1);
  if (! bfd_set_section_size (abfd, sec, (sec_length + 3) & ~3))
    bfd_fatal ("bfd_set_section_size");
  if ((sec_length & 3) != 0)
    set_windres_bfd_content (&wrbfd, sign, sec_length, 4-(sec_length & 3));
  set_windres_bfd_content (&wrbfd, sign, 0, sizeof (sign));
  language = -1;
  sec_length_wrote = write_res_directory (&wrbfd, 0x20UL, resdir,
					  (const rc_res_id *) NULL,
					  (const rc_res_id *) NULL,
					  &language, 1);
  if (sec_length != sec_length_wrote)
    fatal ("res write failed with different sizes (%lu/%lu).", (long) sec_length,
    	   (long) sec_length_wrote);

  bfd_close (abfd);
  return;
}

/* Read a resource entry, returns 0 when all resources are read */
static int
read_resource_entry (windres_bfd *wrbfd, rc_uint_type *off, rc_uint_type omax)
{
  rc_res_id type;
  rc_res_id name;
  rc_res_res_info resinfo;
  res_hdr reshdr;
  void *buff;

  rc_res_resource *r;
  struct bin_res_info l;

  off[0] = (off[0] + 3) & ~3;

  /* Read header */
  if ((off[0] + 8) > omax)
    return 0;
  read_res_data_hdr (wrbfd, off, omax, &reshdr);

  /* read resource type */
  read_res_id (wrbfd, off, omax, &type);
  /* read resource id */
  read_res_id (wrbfd, off, omax, &name);

  off[0] = (off[0] + 3) & ~3;

  /* Read additional resource header */
  read_res_data (wrbfd, off, omax, &l, BIN_RES_INFO_SIZE);
  resinfo.version = windres_get_32 (wrbfd, l.version, 4);
  resinfo.memflags = windres_get_16 (wrbfd, l.memflags, 2);
  resinfo.language = windres_get_16 (wrbfd, l.language, 2);
  /* resinfo.version2 = windres_get_32 (wrbfd, l.version2, 4); */
  resinfo.characteristics = windres_get_32 (wrbfd, l.characteristics, 4);

  off[0] = (off[0] + 3) & ~3;

  /* Allocate buffer for data */
  buff = res_alloc (reshdr.data_size);
  /* Read data */
  read_res_data (wrbfd, off, omax, buff, reshdr.data_size);
  /* Convert binary data to resource */
  r = bin_to_res (wrbfd, type, buff, reshdr.data_size);
  r->res_info = resinfo;
  /* Add resource to resource directory */
  res_add_resource (r, &type, &name, resinfo.language, 0);

  return 1;
}

/* write resource directory to binary resource file */
static rc_uint_type
write_res_directory (windres_bfd *wrbfd, rc_uint_type off, const rc_res_directory *rd,
		     const rc_res_id *type, const rc_res_id *name, rc_uint_type *language,
		     int level)
{
  const rc_res_entry *re;

  for (re = rd->entries; re != NULL; re = re->next)
    {
      switch (level)
	{
	case 1:
	  /* If we're at level 1, the key of this resource is the
	     type.  This normally duplicates the information we have
	     stored with the resource itself, but we need to remember
	     the type if this is a user define resource type.  */
	  type = &re->id;
	  break;

	case 2:
	  /* If we're at level 2, the key of this resource is the name
	     we are going to use in the rc printout.  */
	  name = &re->id;
	  break;

	case 3:
	  /* If we're at level 3, then this key represents a language.
	     Use it to update the current language.  */
	  if (! re->id.named
	      && re->id.u.id != (unsigned long) *language
	      && (re->id.u.id & 0xffff) == re->id.u.id)
	    {
	      *language = re->id.u.id;
	    }
	  break;

	default:
	  break;
	}

      if (re->subdir)
	off = write_res_directory (wrbfd, off, re->u.dir, type, name, language,
				   level + 1);
      else
	{
	  if (level == 3)
	    {
	      /* This is the normal case: the three levels are
	         TYPE/NAME/LANGUAGE.  NAME will have been set at level
	         2, and represents the name to use.  We probably just
	         set LANGUAGE, and it will probably match what the
	         resource itself records if anything.  */
	      off = write_res_resource (wrbfd, off, type, name, re->u.res,
	      				language);
	    }
	  else
	    {
	      fprintf (stderr, "// Resource at unexpected level %d\n", level);
	      off = write_res_resource (wrbfd, off, type, (rc_res_id *) NULL,
	      				re->u.res, language);
	    }
	}
    }

  return off;
}

static rc_uint_type
write_res_resource (windres_bfd *wrbfd, rc_uint_type off, const rc_res_id *type,
		    const rc_res_id *name, const rc_res_resource *res,
		    rc_uint_type *language ATTRIBUTE_UNUSED)
{
  int rt;

  switch (res->type)
    {
    default:
      abort ();

    case RES_TYPE_ACCELERATOR:
      rt = RT_ACCELERATOR;
      break;

    case RES_TYPE_BITMAP:
      rt = RT_BITMAP;
      break;

    case RES_TYPE_CURSOR:
      rt = RT_CURSOR;
      break;

    case RES_TYPE_GROUP_CURSOR:
      rt = RT_GROUP_CURSOR;
      break;

    case RES_TYPE_DIALOG:
      rt = RT_DIALOG;
      break;

    case RES_TYPE_FONT:
      rt = RT_FONT;
      break;

    case RES_TYPE_FONTDIR:
      rt = RT_FONTDIR;
      break;

    case RES_TYPE_ICON:
      rt = RT_ICON;
      break;

    case RES_TYPE_GROUP_ICON:
      rt = RT_GROUP_ICON;
      break;

    case RES_TYPE_MENU:
      rt = RT_MENU;
      break;

    case RES_TYPE_MESSAGETABLE:
      rt = RT_MESSAGETABLE;
      break;

    case RES_TYPE_RCDATA:
      rt = RT_RCDATA;
      break;

    case RES_TYPE_STRINGTABLE:
      rt = RT_STRING;
      break;

    case RES_TYPE_USERDATA:
      rt = 0;
      break;

    case RES_TYPE_VERSIONINFO:
      rt = RT_VERSION;
      break;

    case RES_TYPE_TOOLBAR:
      rt = RT_TOOLBAR;
      break;
    }

  if (rt != 0
      && type != NULL
      && (type->named || type->u.id != (unsigned long) rt))
    {
      fprintf (stderr, "// Unexpected resource type mismatch: ");
      res_id_print (stderr, *type, 1);
      fprintf (stderr, " != %d", rt);
      abort ();
    }

  return write_res_bin (wrbfd, off, res, type, name, &res->res_info);
}

/* Write a resource in binary resource format */
static rc_uint_type
write_res_bin (windres_bfd *wrbfd, rc_uint_type off, const rc_res_resource *res,
	       const rc_res_id *type, const rc_res_id *name,
	       const rc_res_res_info *resinfo)
{
  rc_uint_type noff;
  rc_uint_type datasize = 0;

  noff = res_to_bin ((windres_bfd *) NULL, off, res);
  datasize = noff - off;

  off = write_res_header (wrbfd, off, datasize, type, name, resinfo);
  return res_to_bin (wrbfd, off, res);
}

/* Get number of bytes needed to store an id in binary format */
static unsigned long
get_id_size (id)
     const rc_res_id *id;
{
  if (id->named)
    return sizeof (unichar) * (id->u.n.length + 1);
  else
    return sizeof (unichar) * 2;
}

/* Write a resource header */
static rc_uint_type
write_res_header (windres_bfd *wrbfd, rc_uint_type off, rc_uint_type datasize,
		  const rc_res_id *type, const rc_res_id *name,
		  const rc_res_res_info *resinfo)
{
  res_hdr reshdr;
  reshdr.data_size = datasize;
  reshdr.header_size = 24 + get_id_size (type) + get_id_size (name);

  reshdr.header_size = (reshdr.header_size + 3) & ~3;

  off = (off + 3) & ~3;

  off = write_res_data_hdr (wrbfd, off, &reshdr);
  off = write_res_id (wrbfd, off, type);
  off = write_res_id (wrbfd, off, name);

  off = (off + 3) & ~3;

  off = write_res_info (wrbfd, off, resinfo);
  off = (off + 3) & ~3;
  return off;
}

static rc_uint_type
write_res_data_hdr (windres_bfd *wrbfd, rc_uint_type off, res_hdr *hdr)
{
  if (wrbfd)
    {
      struct bin_res_hdr brh;
      windres_put_32 (wrbfd, brh.data_size, hdr->data_size);
      windres_put_32 (wrbfd, brh.header_size, hdr->header_size);
      set_windres_bfd_content (wrbfd, &brh, off, BIN_RES_HDR_SIZE);
    }
  return off + BIN_RES_HDR_SIZE;
}

static void
read_res_data_hdr (windres_bfd *wrbfd, rc_uint_type *off, rc_uint_type omax,
		   res_hdr *reshdr)
{
  struct bin_res_hdr brh;

  if ((off[0] + BIN_RES_HDR_SIZE) > omax)
    fatal ("%s: unexpected end of file %ld/%ld", filename,(long) off[0], (long) omax);

  get_windres_bfd_content (wrbfd, &brh, off[0], BIN_RES_HDR_SIZE);
  reshdr->data_size = windres_get_32 (wrbfd, brh.data_size, 4);
  reshdr->header_size = windres_get_32 (wrbfd, brh.header_size, 4);
  off[0] += BIN_RES_HDR_SIZE;
}

/* Read data from file, abort on failure */
static void
read_res_data (windres_bfd *wrbfd, rc_uint_type *off, rc_uint_type omax, void *data,
	       rc_uint_type size)
{
  if ((off[0] + size) > omax)
    fatal ("%s: unexpected end of file %ld/%ld %ld", filename,(long) off[0],
    	   (long) omax, (long) size);
  get_windres_bfd_content (wrbfd, data, off[0], size);
  off[0] += size;
}

/* Write a resource id */
static rc_uint_type
write_res_id (windres_bfd *wrbfd, rc_uint_type off, const rc_res_id *id)
{
  if (id->named)
    {
      rc_uint_type len = (((bfd_signed_vma) id->u.n.length < 0 ? 0 : id->u.n.length) + 1);
      if (wrbfd)
	{
	  rc_uint_type i;
	  bfd_byte *d = (bfd_byte *) xmalloc (len * sizeof (unichar));
	  for (i = 0; i < (len - 1); i++)
	    windres_put_16 (wrbfd, d + (i * sizeof (unichar)), id->u.n.name[i]);
	  windres_put_16 (wrbfd, d + (i * sizeof (unichar)), 0);
	  set_windres_bfd_content (wrbfd, d, off, (len * sizeof (unichar)));
	}
      off += (len * sizeof (unichar));
    }
  else
    {
      if (wrbfd)
	{
	  struct bin_res_id bid;
	  windres_put_16 (wrbfd, bid.sig, 0xffff);
	  windres_put_16 (wrbfd, bid.id, id->u.id);
	  set_windres_bfd_content (wrbfd, &bid, off, BIN_RES_ID);
	}
      off += BIN_RES_ID;
    }
  return off;
}

/* Write resource info */
static rc_uint_type
write_res_info (windres_bfd *wrbfd, rc_uint_type off, const rc_res_res_info *info)
{
  if (wrbfd)
    {
      struct bin_res_info l;
      
      windres_put_32 (wrbfd, l.version, info->version);
      windres_put_16 (wrbfd, l.memflags, info->memflags);
      windres_put_16 (wrbfd, l.language, info->language);
      windres_put_32 (wrbfd, l.version2, info->version);
      windres_put_32 (wrbfd, l.characteristics, info->characteristics);
      set_windres_bfd_content (wrbfd, &l, off, BIN_RES_INFO_SIZE);
    }
  return off + BIN_RES_INFO_SIZE;
}

/* read a resource identifier */
static void
read_res_id (windres_bfd *wrbfd, rc_uint_type *off, rc_uint_type omax, rc_res_id *id)
{
  struct bin_res_id bid;
  unsigned short ord;
  unichar *id_s = NULL;
  rc_uint_type len;

  read_res_data (wrbfd, off, omax, &bid, BIN_RES_ID - 2);
  ord = (unsigned short) windres_get_16 (wrbfd, bid.sig, 2);
  if (ord == 0xFFFF)		/* an ordinal id */
    {
      read_res_data (wrbfd, off, omax, bid.id, BIN_RES_ID - 2);
      id->named = 0;
      id->u.id = windres_get_16 (wrbfd, bid.id, 2);
    }
  else
    /* named id */
    {
      off[0] -= 2;
      id_s = read_unistring (wrbfd, off, omax, &len);
      id->named = 1;
      id->u.n.length = len;
      id->u.n.name = id_s;
    }
}

/* Read a null terminated UNICODE string */
static unichar *
read_unistring (windres_bfd *wrbfd, rc_uint_type *off, rc_uint_type omax,
		rc_uint_type *len)
{
  unichar *s;
  bfd_byte d[2];
  unichar c;
  unichar *p;
  rc_uint_type l;
  rc_uint_type soff = off[0];

  do
    {
      read_res_data (wrbfd, &soff, omax, d, sizeof (unichar));
      c = windres_get_16 (wrbfd, d, 2);
    }
  while (c != 0);
  l = ((soff - off[0]) / sizeof (unichar));

  /* there are hardly any names longer than 256 characters, but anyway. */
  p = s = (unichar *) xmalloc (sizeof (unichar) * l);
  do
    {
      read_res_data (wrbfd, off, omax, d, sizeof (unichar));
      c = windres_get_16 (wrbfd, d, 2);
      *p++ = c;
    }
  while (c != 0);
  *len = l - 1;
  return s;
}

static int
probe_binary (windres_bfd *wrbfd, rc_uint_type omax)
{
  rc_uint_type off;
  res_hdr reshdr;

  off = 0;
  read_res_data_hdr (wrbfd, &off, omax, &reshdr);
  if (reshdr.data_size != 0)
    return 1;
  if ((reshdr.header_size != 0x20 && ! target_is_bigendian)
      || (reshdr.header_size != 0x20000000 && target_is_bigendian))
    return 1;

  /* Subtract size of HeaderSize. DataSize has to be zero. */
  off += 0x20 - BIN_RES_HDR_SIZE;
  if ((off + BIN_RES_HDR_SIZE) >= omax)
    return 1;
  read_res_data_hdr (wrbfd, &off, omax, &reshdr);
  /* off is advanced by BIN_RES_HDR_SIZE in read_res_data_hdr()
     which is part of reshdr.header_size. We shouldn't take it
     into account twice.  */
  if ((off - BIN_RES_HDR_SIZE + reshdr.data_size + reshdr.header_size) > omax)
    return 0;
  return 1;
}

/* Check if file is a win32 binary resource file, if so
   skip past the null resource. Returns 0 if successful, -1 on
   error.
 */
static void
skip_null_resource (windres_bfd *wrbfd, rc_uint_type *off, rc_uint_type omax)
{
  res_hdr reshdr;
  read_res_data_hdr (wrbfd, off, omax, &reshdr);
  if (reshdr.data_size != 0)
    goto skip_err;
  if ((reshdr.header_size != 0x20 && ! target_is_bigendian)
    || (reshdr.header_size != 0x20000000 && target_is_bigendian))
    goto skip_err;

  /* Subtract size of HeaderSize. DataSize has to be zero. */
  off[0] += 0x20 - BIN_RES_HDR_SIZE;
  if (off[0] >= omax)
    goto skip_err;

  return;

skip_err:
  fprintf (stderr, "%s: %s: Not a valid WIN32 resource file\n", program_name,
	   filename);
  xexit (1);
}

/* Add a resource to resource directory */
static void
res_add_resource (rc_res_resource *r, const rc_res_id *type, const rc_res_id *id,
		  rc_uint_type language, int dupok)
{
  rc_res_id a[3];

  a[0] = *type;
  a[1] = *id;
  a[2].named = 0;
  a[2].u.id = language;
  res_append_resource (&resources, r, 3, a, dupok);
}

/* Append a resource to resource directory.
   This is just copied from define_resource
   and modified to add an existing resource.
 */
static void
res_append_resource (rc_res_directory **resources, rc_res_resource *resource,
		     int cids, const rc_res_id *ids, int dupok)
{
  rc_res_entry *re = NULL;
  int i;

  assert (cids > 0);
  for (i = 0; i < cids; i++)
    {
      rc_res_entry **pp;

      if (*resources == NULL)
	{
	  static unsigned long timeval;

	  /* Use the same timestamp for every resource created in a
	     single run.  */
	  if (timeval == 0)
	    timeval = time (NULL);

	  *resources = ((rc_res_directory *)
			res_alloc (sizeof (rc_res_directory)));
	  (*resources)->characteristics = 0;
	  (*resources)->time = timeval;
	  (*resources)->major = 0;
	  (*resources)->minor = 0;
	  (*resources)->entries = NULL;
	}

      for (pp = &(*resources)->entries; *pp != NULL; pp = &(*pp)->next)
	if (res_id_cmp ((*pp)->id, ids[i]) == 0)
	  break;

      if (*pp != NULL)
	re = *pp;
      else
	{
	  re = (rc_res_entry *) res_alloc (sizeof (rc_res_entry));
	  re->next = NULL;
	  re->id = ids[i];
	  if ((i + 1) < cids)
	    {
	      re->subdir = 1;
	      re->u.dir = NULL;
	    }
	  else
	    {
	      re->subdir = 0;
	      re->u.res = NULL;
	    }

	  *pp = re;
	}

      if ((i + 1) < cids)
	{
	  if (! re->subdir)
	    {
	      fprintf (stderr, "%s: ", program_name);
	      res_ids_print (stderr, i, ids);
	      fprintf (stderr, ": expected to be a directory\n");
	      xexit (1);
	    }

	  resources = &re->u.dir;
	}
    }

  if (re->subdir)
    {
      fprintf (stderr, "%s: ", program_name);
      res_ids_print (stderr, cids, ids);
      fprintf (stderr, ": expected to be a leaf\n");
      xexit (1);
    }

  if (re->u.res != NULL)
    {
      if (dupok)
	return;

      fprintf (stderr, "%s: warning: ", program_name);
      res_ids_print (stderr, cids, ids);
      fprintf (stderr, ": duplicate value\n");
    }

  re->u.res = resource;
}
