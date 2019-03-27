/* ELF attributes support (based on ARM EABI attributes).
   Copyright 2005, 2006, 2007
   Free Software Foundation, Inc.

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

#include "sysdep.h"
#include "bfd.h"
#include "libiberty.h"
#include "libbfd.h"
#include "elf-bfd.h"

/* Return the number of bytes needed by I in uleb128 format.  */
static int
uleb128_size (unsigned int i)
{
  int size;
  size = 1;
  while (i >= 0x80)
    {
      i >>= 7;
      size++;
    }
  return size;
}

/* Return TRUE if the attribute has the default value (0/"").  */
static bfd_boolean
is_default_attr (obj_attribute *attr)
{
  if ((attr->type & 1) && attr->i != 0)
    return FALSE;
  if ((attr->type & 2) && attr->s && *attr->s)
    return FALSE;

  return TRUE;
}

/* Return the size of a single attribute.  */
static bfd_vma
obj_attr_size (int tag, obj_attribute *attr)
{
  bfd_vma size;

  if (is_default_attr (attr))
    return 0;

  size = uleb128_size (tag);
  if (attr->type & 1)
    size += uleb128_size (attr->i);
  if (attr->type & 2)
    size += strlen ((char *)attr->s) + 1;
  return size;
}

/* Return the vendor name for a given object attributes section.  */
static const char *
vendor_obj_attr_name (bfd *abfd, int vendor)
{
  return (vendor == OBJ_ATTR_PROC
	  ? get_elf_backend_data (abfd)->obj_attrs_vendor
	  : "gnu");
}

/* Return the size of the object attributes section for VENDOR
   (OBJ_ATTR_PROC or OBJ_ATTR_GNU), or 0 if there are no attributes
   for that vendor to record and the vendor is OBJ_ATTR_GNU.  */
static bfd_vma
vendor_obj_attr_size (bfd *abfd, int vendor)
{
  bfd_vma size;
  obj_attribute *attr;
  obj_attribute_list *list;
  int i;
  const char *vendor_name = vendor_obj_attr_name (abfd, vendor);

  if (!vendor_name)
    return 0;

  attr = elf_known_obj_attributes (abfd)[vendor];
  size = 0;
  for (i = 4; i < NUM_KNOWN_OBJ_ATTRIBUTES; i++)
    size += obj_attr_size (i, &attr[i]);

  for (list = elf_other_obj_attributes (abfd)[vendor];
       list;
       list = list->next)
    size += obj_attr_size (list->tag, &list->attr);

  /* <size> <vendor_name> NUL 0x1 <size> */
  return ((size || vendor == OBJ_ATTR_PROC)
	  ? size + 10 + strlen (vendor_name)
	  : 0);
}

/* Return the size of the object attributes section.  */
bfd_vma
bfd_elf_obj_attr_size (bfd *abfd)
{
  bfd_vma size;

  size = vendor_obj_attr_size (abfd, OBJ_ATTR_PROC);
  size += vendor_obj_attr_size (abfd, OBJ_ATTR_GNU);

  /* 'A' <sections for each vendor> */
  return (size ? size + 1 : 0);
}

/* Write VAL in uleb128 format to P, returning a pointer to the
   following byte.  */
static bfd_byte *
write_uleb128 (bfd_byte *p, unsigned int val)
{
  bfd_byte c;
  do
    {
      c = val & 0x7f;
      val >>= 7;
      if (val)
	c |= 0x80;
      *(p++) = c;
    }
  while (val);
  return p;
}

/* Write attribute ATTR to butter P, and return a pointer to the following
   byte.  */
static bfd_byte *
write_obj_attribute (bfd_byte *p, int tag, obj_attribute *attr)
{
  /* Suppress default entries.  */
  if (is_default_attr (attr))
    return p;

  p = write_uleb128 (p, tag);
  if (attr->type & 1)
    p = write_uleb128 (p, attr->i);
  if (attr->type & 2)
    {
      int len;

      len = strlen (attr->s) + 1;
      memcpy (p, attr->s, len);
      p += len;
    }

  return p;
}

/* Write the contents of the object attributes section (length SIZE)
   for VENDOR to CONTENTS.  */
static void
vendor_set_obj_attr_contents (bfd *abfd, bfd_byte *contents, bfd_vma size,
			      int vendor)
{
  bfd_byte *p;
  obj_attribute *attr;
  obj_attribute_list *list;
  int i;
  const char *vendor_name = vendor_obj_attr_name (abfd, vendor);
  size_t vendor_length = strlen (vendor_name) + 1;

  p = contents;
  bfd_put_32 (abfd, size, p);
  p += 4;
  memcpy (p, vendor_name, vendor_length);
  p += vendor_length;
  *(p++) = Tag_File;
  bfd_put_32 (abfd, size - 4 - vendor_length, p);
  p += 4;

  attr = elf_known_obj_attributes (abfd)[vendor];
  for (i = 4; i < NUM_KNOWN_OBJ_ATTRIBUTES; i++)
    p = write_obj_attribute (p, i, &attr[i]);

  for (list = elf_other_obj_attributes (abfd)[vendor];
       list;
       list = list->next)
    p = write_obj_attribute (p, list->tag, &list->attr);
}

/* Write the contents of the object attributes section to CONTENTS.  */
void
bfd_elf_set_obj_attr_contents (bfd *abfd, bfd_byte *contents, bfd_vma size)
{
  bfd_byte *p;
  int vendor;
  bfd_vma my_size;

  p = contents;
  *(p++) = 'A';
  my_size = 1;
  for (vendor = OBJ_ATTR_FIRST; vendor <= OBJ_ATTR_LAST; vendor++)
    {
      bfd_vma vendor_size = vendor_obj_attr_size (abfd, vendor);
      if (vendor_size)
	vendor_set_obj_attr_contents (abfd, p, vendor_size, vendor);
      p += vendor_size;
      my_size += vendor_size;
    }

  if (size != my_size)
    abort ();
}

/* Allocate/find an object attribute.  */
static obj_attribute *
elf_new_obj_attr (bfd *abfd, int vendor, int tag)
{
  obj_attribute *attr;
  obj_attribute_list *list;
  obj_attribute_list *p;
  obj_attribute_list **lastp;


  if (tag < NUM_KNOWN_OBJ_ATTRIBUTES)
    {
      /* Knwon tags are preallocated.  */
      attr = &elf_known_obj_attributes (abfd)[vendor][tag];
    }
  else
    {
      /* Create a new tag.  */
      list = (obj_attribute_list *)
	bfd_alloc (abfd, sizeof (obj_attribute_list));
      memset (list, 0, sizeof (obj_attribute_list));
      list->tag = tag;
      /* Keep the tag list in order.  */
      lastp = &elf_other_obj_attributes (abfd)[vendor];
      for (p = *lastp; p; p = p->next)
	{
	  if (tag < p->tag)
	    break;
	  lastp = &p->next;
	}
      list->next = *lastp;
      *lastp = list;
      attr = &list->attr;
    }

  return attr;
}

/* Return the value of an integer object attribute.  */
int
bfd_elf_get_obj_attr_int (bfd *abfd, int vendor, int tag)
{
  obj_attribute_list *p;

  if (tag < NUM_KNOWN_OBJ_ATTRIBUTES)
    {
      /* Knwon tags are preallocated.  */
      return elf_known_obj_attributes (abfd)[vendor][tag].i;
    }
  else
    {
      for (p = elf_other_obj_attributes (abfd)[vendor];
	   p;
	   p = p->next)
	{
	  if (tag == p->tag)
	    return p->attr.i;
	  if (tag < p->tag)
	    break;
	}
      return 0;
    }
}

/* Add an integer object attribute.  */
void
bfd_elf_add_obj_attr_int (bfd *abfd, int vendor, int tag, unsigned int i)
{
  obj_attribute *attr;

  attr = elf_new_obj_attr (abfd, vendor, tag);
  attr->type = 1;
  attr->i = i;
}

/* Duplicate an object attribute string value.  */
char *
_bfd_elf_attr_strdup (bfd *abfd, const char * s)
{
  char * p;
  int len;
  
  len = strlen (s) + 1;
  p = (char *) bfd_alloc (abfd, len);
  return memcpy (p, s, len);
}

/* Add a string object attribute.  */
void
bfd_elf_add_obj_attr_string (bfd *abfd, int vendor, int tag, const char *s)
{
  obj_attribute *attr;

  attr = elf_new_obj_attr (abfd, vendor, tag);
  attr->type = 2;
  attr->s = _bfd_elf_attr_strdup (abfd, s);
}

/* Add a Tag_compatibility object attribute.  */
void
bfd_elf_add_obj_attr_compat (bfd *abfd, int vendor, unsigned int i,
			     const char *s)
{
  obj_attribute_list *list;
  obj_attribute_list *p;
  obj_attribute_list **lastp;

  list = (obj_attribute_list *)
    bfd_alloc (abfd, sizeof (obj_attribute_list));
  memset (list, 0, sizeof (obj_attribute_list));
  list->tag = Tag_compatibility;
  list->attr.type = 3;
  list->attr.i = i;
  list->attr.s = _bfd_elf_attr_strdup (abfd, s);

  lastp = &elf_other_obj_attributes (abfd)[vendor];
  for (p = *lastp; p; p = p->next)
    {
      int cmp;
      if (p->tag != Tag_compatibility)
	break;
      cmp = strcmp(s, p->attr.s);
      if (cmp < 0 || (cmp == 0 && i < p->attr.i))
	break;
      lastp = &p->next;
    }
  list->next = *lastp;
  *lastp = list;
}

/* Copy the object attributes from IBFD to OBFD.  */
void
_bfd_elf_copy_obj_attributes (bfd *ibfd, bfd *obfd)
{
  obj_attribute *in_attr;
  obj_attribute *out_attr;
  obj_attribute_list *list;
  int i;
  int vendor;

  for (vendor = OBJ_ATTR_FIRST; vendor <= OBJ_ATTR_LAST; vendor++)
    {
      in_attr = &elf_known_obj_attributes (ibfd)[vendor][4];
      out_attr = &elf_known_obj_attributes (obfd)[vendor][4];
      for (i = 4; i < NUM_KNOWN_OBJ_ATTRIBUTES; i++)
	{
	  out_attr->type = in_attr->type;
	  out_attr->i = in_attr->i;
	  if (in_attr->s && *in_attr->s)
	    out_attr->s = _bfd_elf_attr_strdup (obfd, in_attr->s);
	  in_attr++;
	  out_attr++;
	}

      for (list = elf_other_obj_attributes (ibfd)[vendor];
	   list;
	   list = list->next)
	{
	  in_attr = &list->attr;
	  switch (in_attr->type)
	    {
	    case 1:
	      bfd_elf_add_obj_attr_int (obfd, vendor, list->tag, in_attr->i);
	      break;
	    case 2:
	      bfd_elf_add_obj_attr_string (obfd, vendor, list->tag,
					   in_attr->s);
	      break;
	    case 3:
	      bfd_elf_add_obj_attr_compat (obfd, vendor, in_attr->i,
					   in_attr->s);
	      break;
	    default:
	      abort ();
	    }
	}
    }
}

/* Determine whether a GNU object attribute tag takes an integer, a
   string or both.  */
static int
gnu_obj_attrs_arg_type (int tag)
{
  /* Except for Tag_compatibility, for GNU attributes we follow the
     same rule ARM ones > 32 follow: odd-numbered tags take strings
     and even-numbered tags take integers.  In addition, tag & 2 is
     nonzero for architecture-independent tags and zero for
     architecture-dependent ones.  */
  if (tag == Tag_compatibility)
    return 3;
  else
    return (tag & 1) != 0 ? 2 : 1;
}

/* Determine what arguments an attribute tag takes.  */
int
_bfd_elf_obj_attrs_arg_type (bfd *abfd, int vendor, int tag)
{
  switch (vendor)
    {
    case OBJ_ATTR_PROC:
      return get_elf_backend_data (abfd)->obj_attrs_arg_type (tag);
      break;
    case OBJ_ATTR_GNU:
      return gnu_obj_attrs_arg_type (tag);
      break;
    default:
      abort ();
    }
}

/* Parse an object attributes section.  */
void
_bfd_elf_parse_attributes (bfd *abfd, Elf_Internal_Shdr * hdr)
{
  bfd_byte *contents;
  bfd_byte *p;
  bfd_vma len;
  const char *std_section;

  contents = bfd_malloc (hdr->sh_size);
  if (!contents)
    return;
  if (!bfd_get_section_contents (abfd, hdr->bfd_section, contents, 0,
				 hdr->sh_size))
    {
      free (contents);
      return;
    }
  p = contents;
  std_section = get_elf_backend_data (abfd)->obj_attrs_vendor;
  if (*(p++) == 'A')
    {
      len = hdr->sh_size - 1;
      while (len > 0)
	{
	  int namelen;
	  bfd_vma section_len;
	  int vendor;

	  section_len = bfd_get_32 (abfd, p);
	  p += 4;
	  if (section_len > len)
	    section_len = len;
	  len -= section_len;
	  namelen = strlen ((char *)p) + 1;
	  section_len -= namelen + 4;
	  if (std_section && strcmp ((char *)p, std_section) == 0)
	    vendor = OBJ_ATTR_PROC;
	  else if (strcmp ((char *)p, "gnu") == 0)
	    vendor = OBJ_ATTR_GNU;
	  else
	    {
	      /* Other vendor section.  Ignore it.  */
	      p += namelen + section_len;
	      continue;
	    }

	  p += namelen;
	  while (section_len > 0)
	    {
	      int tag;
	      unsigned int n;
	      unsigned int val;
	      bfd_vma subsection_len;
	      bfd_byte *end;

	      tag = read_unsigned_leb128 (abfd, p, &n);
	      p += n;
	      subsection_len = bfd_get_32 (abfd, p);
	      p += 4;
	      if (subsection_len > section_len)
		subsection_len = section_len;
	      section_len -= subsection_len;
	      subsection_len -= n + 4;
	      end = p + subsection_len;
	      switch (tag)
		{
		case Tag_File:
		  while (p < end)
		    {
		      int type;

		      tag = read_unsigned_leb128 (abfd, p, &n);
		      p += n;
		      type = _bfd_elf_obj_attrs_arg_type (abfd, vendor, tag);
		      switch (type)
			{
			case 3:
			  val = read_unsigned_leb128 (abfd, p, &n);
			  p += n;
			  bfd_elf_add_obj_attr_compat (abfd, vendor, val,
						       (char *)p);
			  p += strlen ((char *)p) + 1;
			  break;
			case 2:
			  bfd_elf_add_obj_attr_string (abfd, vendor, tag,
						       (char *)p);
			  p += strlen ((char *)p) + 1;
			  break;
			case 1:
			  val = read_unsigned_leb128 (abfd, p, &n);
			  p += n;
			  bfd_elf_add_obj_attr_int (abfd, vendor, tag, val);
			  break;
			default:
			  abort ();
			}
		    }
		  break;
		case Tag_Section:
		case Tag_Symbol:
		  /* Don't have anywhere convenient to attach these.
		     Fall through for now.  */
		default:
		  /* Ignore things we don't kow about.  */
		  p += subsection_len;
		  subsection_len = 0;
		  break;
		}
	    }
	}
    }
  free (contents);
}

/* Merge common object attributes from IBFD into OBFD.  Raise an error
   if there are conflicting attributes.  Any processor-specific
   attributes have already been merged.  This must be called from the
   bfd_elfNN_bfd_merge_private_bfd_data hook for each individual
   target, along with any target-specific merging.  Because there are
   no common attributes other than Tag_compatibility at present, and
   non-"gnu" Tag_compatibility is not expected in "gnu" sections, this
   is not presently called for targets without their own
   attributes.  */

bfd_boolean
_bfd_elf_merge_object_attributes (bfd *ibfd, bfd *obfd)
{
  obj_attribute *in_attr;
  obj_attribute *out_attr;
  obj_attribute_list *in_list;
  obj_attribute_list *out_list;
  int vendor;

  /* The only common attribute is currently Tag_compatibility,
     accepted in both processor and "gnu" sections.  */
  for (vendor = OBJ_ATTR_FIRST; vendor <= OBJ_ATTR_LAST; vendor++)
    {
      in_list = elf_other_obj_attributes (ibfd)[vendor];
      out_list = elf_other_obj_attributes (ibfd)[vendor];
      while (in_list && in_list->tag == Tag_compatibility)
	{
	  in_attr = &in_list->attr;
	  if (in_attr->i == 0)
	    continue;
	  if (in_attr->i == 1 && strcmp (in_attr->s, "gnu") != 0)
	    {
	      _bfd_error_handler
		(_("ERROR: %B: Must be processed by '%s' toolchain"),
		 ibfd, in_attr->s);
	      return FALSE;
	    }
	  if (!out_list || out_list->tag != Tag_compatibility
	      || strcmp (in_attr->s, out_list->attr.s) != 0)
	    {
	      /* Add this compatibility tag to the output.  */
	      bfd_elf_add_proc_attr_compat (obfd, in_attr->i, in_attr->s);
	      continue;
	    }
	  out_attr = &out_list->attr;
	  /* Check all the input tags with the same identifier.  */
	  for (;;)
	    {
	      if (out_list->tag != Tag_compatibility
		  || in_attr->i != out_attr->i
		  || strcmp (in_attr->s, out_attr->s) != 0)
		{
		  _bfd_error_handler
		    (_("ERROR: %B: Incompatible object tag '%s':%d"),
		     ibfd, in_attr->s, in_attr->i);
		  return FALSE;
		}
	      in_list = in_list->next;
	      if (in_list->tag != Tag_compatibility
		  || strcmp (in_attr->s, in_list->attr.s) != 0)
		break;
	      in_attr = &in_list->attr;
	      out_list = out_list->next;
	      if (out_list)
		out_attr = &out_list->attr;
	    }

	  /* Check the output doesn't have extra tags with this identifier.  */
	  if (out_list && out_list->tag == Tag_compatibility
	      && strcmp (in_attr->s, out_list->attr.s) == 0)
	    {
	      _bfd_error_handler
		(_("ERROR: %B: Incompatible object tag '%s':%d"),
		 ibfd, in_attr->s, out_list->attr.i);
	      return FALSE;
	    }
	}
    }

  return TRUE;
}
