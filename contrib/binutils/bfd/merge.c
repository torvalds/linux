/* SEC_MERGE support.
   Copyright 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Written by Jakub Jelinek <jakub@redhat.com>.

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

/* This file contains support for merging duplicate entities within sections,
   as used in ELF SHF_MERGE.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "hashtab.h"
#include "libiberty.h"

struct sec_merge_sec_info;

/* An entry in the section merge hash table.  */

struct sec_merge_hash_entry
{
  struct bfd_hash_entry root;
  /* Length of this entry.  This includes the zero terminator.  */
  unsigned int len;
  /* Start of this string needs to be aligned to
     alignment octets (not 1 << align).  */
  unsigned int alignment;
  union
  {
    /* Index within the merged section.  */
    bfd_size_type index;
    /* Entry this is a suffix of (if alignment is 0).  */
    struct sec_merge_hash_entry *suffix;
  } u;
  /* Which section is it in.  */
  struct sec_merge_sec_info *secinfo;
  /* Next entity in the hash table.  */
  struct sec_merge_hash_entry *next;
};

/* The section merge hash table.  */

struct sec_merge_hash
{
  struct bfd_hash_table table;
  /* Next available index.  */
  bfd_size_type size;
  /* First entity in the SEC_MERGE sections of this type.  */
  struct sec_merge_hash_entry *first;
  /* Last entity in the SEC_MERGE sections of this type.  */
  struct sec_merge_hash_entry *last;
  /* Entity size.  */
  unsigned int entsize;
  /* Are entries fixed size or zero terminated strings?  */
  bfd_boolean strings;
};

struct sec_merge_info
{
  /* Chain of sec_merge_infos.  */
  struct sec_merge_info *next;
  /* Chain of sec_merge_sec_infos.  */
  struct sec_merge_sec_info *chain;
  /* A hash table used to hold section content.  */
  struct sec_merge_hash *htab;
};

struct sec_merge_sec_info
{
  /* Chain of sec_merge_sec_infos.  */
  struct sec_merge_sec_info *next;
  /* The corresponding section.  */
  asection *sec;
  /* Pointer to merge_info pointing to us.  */
  void **psecinfo;
  /* A hash table used to hold section content.  */
  struct sec_merge_hash *htab;
  /* First string in this section.  */
  struct sec_merge_hash_entry *first_str;
  /* Original section content.  */
  unsigned char contents[1];
};


/* Routine to create an entry in a section merge hashtab.  */

static struct bfd_hash_entry *
sec_merge_hash_newfunc (struct bfd_hash_entry *entry,
			struct bfd_hash_table *table, const char *string)
{
  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (entry == NULL)
    entry = bfd_hash_allocate (table, sizeof (struct sec_merge_hash_entry));
  if (entry == NULL)
    return NULL;

  /* Call the allocation method of the superclass.  */
  entry = bfd_hash_newfunc (entry, table, string);

  if (entry != NULL)
    {
      /* Initialize the local fields.  */
      struct sec_merge_hash_entry *ret = (struct sec_merge_hash_entry *) entry;

      ret->u.suffix = NULL;
      ret->alignment = 0;
      ret->secinfo = NULL;
      ret->next = NULL;
    }

  return entry;
}

/* Look up an entry in a section merge hash table.  */

static struct sec_merge_hash_entry *
sec_merge_hash_lookup (struct sec_merge_hash *table, const char *string,
		       unsigned int alignment, bfd_boolean create)
{
  register const unsigned char *s;
  register unsigned long hash;
  register unsigned int c;
  struct sec_merge_hash_entry *hashp;
  unsigned int len, i;
  unsigned int index;

  hash = 0;
  len = 0;
  s = (const unsigned char *) string;
  if (table->strings)
    {
      if (table->entsize == 1)
	{
	  while ((c = *s++) != '\0')
	    {
	      hash += c + (c << 17);
	      hash ^= hash >> 2;
	      ++len;
	    }
	  hash += len + (len << 17);
	}
      else
	{
	  for (;;)
	    {
	      for (i = 0; i < table->entsize; ++i)
		if (s[i] != '\0')
		  break;
	      if (i == table->entsize)
		break;
	      for (i = 0; i < table->entsize; ++i)
		{
		  c = *s++;
		  hash += c + (c << 17);
		  hash ^= hash >> 2;
		}
	      ++len;
	    }
	  hash += len + (len << 17);
	  len *= table->entsize;
	}
      hash ^= hash >> 2;
      len += table->entsize;
    }
  else
    {
      for (i = 0; i < table->entsize; ++i)
	{
	  c = *s++;
	  hash += c + (c << 17);
	  hash ^= hash >> 2;
	}
      len = table->entsize;
    }

  index = hash % table->table.size;
  for (hashp = (struct sec_merge_hash_entry *) table->table.table[index];
       hashp != NULL;
       hashp = (struct sec_merge_hash_entry *) hashp->root.next)
    {
      if (hashp->root.hash == hash
	  && len == hashp->len
	  && memcmp (hashp->root.string, string, len) == 0)
	{
	  /* If the string we found does not have at least the required
	     alignment, we need to insert another copy.  */
	  if (hashp->alignment < alignment)
	    {
	      if (create)
		{
		  /*  Mark the less aligned copy as deleted.  */
		  hashp->len = 0;
		  hashp->alignment = 0;
		}
	      break;
	    }
	  return hashp;
	}
    }

  if (! create)
    return NULL;

  hashp = ((struct sec_merge_hash_entry *)
	   sec_merge_hash_newfunc (NULL, &table->table, string));
  if (hashp == NULL)
    return NULL;
  hashp->root.string = string;
  hashp->root.hash = hash;
  hashp->len = len;
  hashp->alignment = alignment;
  hashp->root.next = table->table.table[index];
  table->table.table[index] = (struct bfd_hash_entry *) hashp;

  return hashp;
}

/* Create a new hash table.  */

static struct sec_merge_hash *
sec_merge_init (unsigned int entsize, bfd_boolean strings)
{
  struct sec_merge_hash *table;

  table = bfd_malloc (sizeof (struct sec_merge_hash));
  if (table == NULL)
    return NULL;

  if (! bfd_hash_table_init_n (&table->table, sec_merge_hash_newfunc,
			       sizeof (struct sec_merge_hash_entry), 16699))
    {
      free (table);
      return NULL;
    }

  table->size = 0;
  table->first = NULL;
  table->last = NULL;
  table->entsize = entsize;
  table->strings = strings;

  return table;
}

/* Get the index of an entity in a hash table, adding it if it is not
   already present.  */

static struct sec_merge_hash_entry *
sec_merge_add (struct sec_merge_hash *tab, const char *str,
	       unsigned int alignment, struct sec_merge_sec_info *secinfo)
{
  register struct sec_merge_hash_entry *entry;

  entry = sec_merge_hash_lookup (tab, str, alignment, TRUE);
  if (entry == NULL)
    return NULL;

  if (entry->secinfo == NULL)
    {
      tab->size++;
      entry->secinfo = secinfo;
      if (tab->first == NULL)
	tab->first = entry;
      else
	tab->last->next = entry;
      tab->last = entry;
    }

  return entry;
}

static bfd_boolean
sec_merge_emit (bfd *abfd, struct sec_merge_hash_entry *entry)
{
  struct sec_merge_sec_info *secinfo = entry->secinfo;
  asection *sec = secinfo->sec;
  char *pad = NULL;
  bfd_size_type off = 0;
  int alignment_power = sec->output_section->alignment_power;

  if (alignment_power)
    {
      pad = bfd_zmalloc ((bfd_size_type) 1 << alignment_power);
      if (pad == NULL)
	return FALSE;
    }

  for (; entry != NULL && entry->secinfo == secinfo; entry = entry->next)
    {
      const char *str;
      bfd_size_type len;

      len = -off & (entry->alignment - 1);
      if (len != 0)
	{
	  if (bfd_bwrite (pad, len, abfd) != len)
	    goto err;
	  off += len;
	}

      str = entry->root.string;
      len = entry->len;

      if (bfd_bwrite (str, len, abfd) != len)
	goto err;

      off += len;
    }

  /* Trailing alignment needed?  */
  off = sec->size - off;
  if (off != 0
      && bfd_bwrite (pad, off, abfd) != off)
    goto err;

  if (pad != NULL)
    free (pad);
  return TRUE;

 err:
  if (pad != NULL)
    free (pad);
  return FALSE;
}

/* Register a SEC_MERGE section as a candidate for merging.
   This function is called for all non-dynamic SEC_MERGE input sections.  */

bfd_boolean
_bfd_add_merge_section (bfd *abfd, void **psinfo, asection *sec,
			void **psecinfo)
{
  struct sec_merge_info *sinfo;
  struct sec_merge_sec_info *secinfo;
  unsigned int align;
  bfd_size_type amt;

  if ((abfd->flags & DYNAMIC) != 0
      || (sec->flags & SEC_MERGE) == 0)
    abort ();

  if (sec->size == 0
      || (sec->flags & SEC_EXCLUDE) != 0
      || sec->entsize == 0)
    return TRUE;

  if ((sec->flags & SEC_RELOC) != 0)
    {
      /* We aren't prepared to handle relocations in merged sections.  */
      return TRUE;
    }

  align = sec->alignment_power;
  if ((sec->entsize < (unsigned) 1 << align
       && ((sec->entsize & (sec->entsize - 1))
	   || !(sec->flags & SEC_STRINGS)))
      || (sec->entsize > (unsigned) 1 << align
	  && (sec->entsize & (((unsigned) 1 << align) - 1))))
    {
      /* Sanity check.  If string character size is smaller than
	 alignment, then we require character size to be a power
	 of 2, otherwise character size must be integer multiple
	 of alignment.  For non-string constants, alignment must
	 be smaller than or equal to entity size and entity size
	 must be integer multiple of alignment.  */
      return TRUE;
    }

  for (sinfo = (struct sec_merge_info *) *psinfo; sinfo; sinfo = sinfo->next)
    if ((secinfo = sinfo->chain)
	&& ! ((secinfo->sec->flags ^ sec->flags) & (SEC_MERGE | SEC_STRINGS))
	&& secinfo->sec->entsize == sec->entsize
	&& secinfo->sec->alignment_power == sec->alignment_power
	&& secinfo->sec->output_section == sec->output_section)
      break;

  if (sinfo == NULL)
    {
      /* Initialize the information we need to keep track of.  */
      sinfo = bfd_alloc (abfd, sizeof (struct sec_merge_info));
      if (sinfo == NULL)
	goto error_return;
      sinfo->next = (struct sec_merge_info *) *psinfo;
      sinfo->chain = NULL;
      *psinfo = sinfo;
      sinfo->htab = sec_merge_init (sec->entsize, (sec->flags & SEC_STRINGS));
      if (sinfo->htab == NULL)
	goto error_return;
    }

  /* Read the section from abfd.  */

  amt = sizeof (struct sec_merge_sec_info) + sec->size - 1;
  *psecinfo = bfd_alloc (abfd, amt);
  if (*psecinfo == NULL)
    goto error_return;

  secinfo = (struct sec_merge_sec_info *) *psecinfo;
  if (sinfo->chain)
    {
      secinfo->next = sinfo->chain->next;
      sinfo->chain->next = secinfo;
    }
  else
    secinfo->next = secinfo;
  sinfo->chain = secinfo;
  secinfo->sec = sec;
  secinfo->psecinfo = psecinfo;
  secinfo->htab = sinfo->htab;
  secinfo->first_str = NULL;

  sec->rawsize = sec->size;
  if (! bfd_get_section_contents (sec->owner, sec, secinfo->contents,
				  0, sec->size))
    goto error_return;

  return TRUE;

 error_return:
  *psecinfo = NULL;
  return FALSE;
}

/* Record one section into the hash table.  */
static bfd_boolean
record_section (struct sec_merge_info *sinfo,
		struct sec_merge_sec_info *secinfo)
{
  asection *sec = secinfo->sec;
  struct sec_merge_hash_entry *entry;
  bfd_boolean nul;
  unsigned char *p, *end;
  bfd_vma mask, eltalign;
  unsigned int align, i;

  align = sec->alignment_power;
  end = secinfo->contents + sec->size;
  nul = FALSE;
  mask = ((bfd_vma) 1 << align) - 1;
  if (sec->flags & SEC_STRINGS)
    {
      for (p = secinfo->contents; p < end; )
	{
	  eltalign = p - secinfo->contents;
	  eltalign = ((eltalign ^ (eltalign - 1)) + 1) >> 1;
	  if (!eltalign || eltalign > mask)
	    eltalign = mask + 1;
	  entry = sec_merge_add (sinfo->htab, (char *) p, (unsigned) eltalign,
				 secinfo);
	  if (! entry)
	    goto error_return;
	  p += entry->len;
	  if (sec->entsize == 1)
	    {
	      while (p < end && *p == 0)
		{
		  if (!nul && !((p - secinfo->contents) & mask))
		    {
		      nul = TRUE;
		      entry = sec_merge_add (sinfo->htab, "",
					     (unsigned) mask + 1, secinfo);
		      if (! entry)
			goto error_return;
		    }
		  p++;
		}
	    }
	  else
	    {
	      while (p < end)
		{
		  for (i = 0; i < sec->entsize; i++)
		    if (p[i] != '\0')
		      break;
		  if (i != sec->entsize)
		    break;
		  if (!nul && !((p - secinfo->contents) & mask))
		    {
		      nul = TRUE;
		      entry = sec_merge_add (sinfo->htab, (char *) p,
					     (unsigned) mask + 1, secinfo);
		      if (! entry)
			goto error_return;
		    }
		  p += sec->entsize;
		}
	    }
	}
    }
  else
    {
      for (p = secinfo->contents; p < end; p += sec->entsize)
	{
	  entry = sec_merge_add (sinfo->htab, (char *) p, 1, secinfo);
	  if (! entry)
	    goto error_return;
	}
    }

  return TRUE;

error_return:
  for (secinfo = sinfo->chain; secinfo; secinfo = secinfo->next)
    *secinfo->psecinfo = NULL;
  return FALSE;
}

static int
strrevcmp (const void *a, const void *b)
{
  struct sec_merge_hash_entry *A = *(struct sec_merge_hash_entry **) a;
  struct sec_merge_hash_entry *B = *(struct sec_merge_hash_entry **) b;
  unsigned int lenA = A->len;
  unsigned int lenB = B->len;
  const unsigned char *s = (const unsigned char *) A->root.string + lenA - 1;
  const unsigned char *t = (const unsigned char *) B->root.string + lenB - 1;
  int l = lenA < lenB ? lenA : lenB;

  while (l)
    {
      if (*s != *t)
	return (int) *s - (int) *t;
      s--;
      t--;
      l--;
    }
  return lenA - lenB;
}

/* Like strrevcmp, but for the case where all strings have the same
   alignment > entsize.  */

static int
strrevcmp_align (const void *a, const void *b)
{
  struct sec_merge_hash_entry *A = *(struct sec_merge_hash_entry **) a;
  struct sec_merge_hash_entry *B = *(struct sec_merge_hash_entry **) b;
  unsigned int lenA = A->len;
  unsigned int lenB = B->len;
  const unsigned char *s = (const unsigned char *) A->root.string + lenA - 1;
  const unsigned char *t = (const unsigned char *) B->root.string + lenB - 1;
  int l = lenA < lenB ? lenA : lenB;
  int tail_align = (lenA & (A->alignment - 1)) - (lenB & (A->alignment - 1));

  if (tail_align != 0)
    return tail_align;

  while (l)
    {
      if (*s != *t)
	return (int) *s - (int) *t;
      s--;
      t--;
      l--;
    }
  return lenA - lenB;
}

static inline int
is_suffix (const struct sec_merge_hash_entry *A,
	   const struct sec_merge_hash_entry *B)
{
  if (A->len <= B->len)
    /* B cannot be a suffix of A unless A is equal to B, which is guaranteed
       not to be equal by the hash table.  */
    return 0;

  return memcmp (A->root.string + (A->len - B->len),
		 B->root.string, B->len) == 0;
}

/* This is a helper function for _bfd_merge_sections.  It attempts to
   merge strings matching suffixes of longer strings.  */
static void
merge_strings (struct sec_merge_info *sinfo)
{
  struct sec_merge_hash_entry **array, **a, *e;
  struct sec_merge_sec_info *secinfo;
  bfd_size_type size, amt;
  unsigned int alignment = 0;

  /* Now sort the strings */
  amt = sinfo->htab->size * sizeof (struct sec_merge_hash_entry *);
  array = bfd_malloc (amt);
  if (array == NULL)
    goto alloc_failure;

  for (e = sinfo->htab->first, a = array; e; e = e->next)
    if (e->alignment)
      {
	*a++ = e;
	/* Adjust the length to not include the zero terminator.  */
	e->len -= sinfo->htab->entsize;
	if (alignment != e->alignment)
	  {
	    if (alignment == 0)
	      alignment = e->alignment;
	    else
	      alignment = (unsigned) -1;
	  }
      }

  sinfo->htab->size = a - array;
  if (sinfo->htab->size != 0)
    {
      qsort (array, (size_t) sinfo->htab->size,
	     sizeof (struct sec_merge_hash_entry *),
	     (alignment != (unsigned) -1 && alignment > sinfo->htab->entsize
	      ? strrevcmp_align : strrevcmp));

      /* Loop over the sorted array and merge suffixes */
      e = *--a;
      e->len += sinfo->htab->entsize;
      while (--a >= array)
	{
	  struct sec_merge_hash_entry *cmp = *a;

	  cmp->len += sinfo->htab->entsize;
	  if (e->alignment >= cmp->alignment
	      && !((e->len - cmp->len) & (cmp->alignment - 1))
	      && is_suffix (e, cmp))
	    {
	      cmp->u.suffix = e;
	      cmp->alignment = 0;
	    }
	  else
	    e = cmp;
	}
    }

alloc_failure:
  if (array)
    free (array);

  /* Now assign positions to the strings we want to keep.  */
  size = 0;
  secinfo = sinfo->htab->first->secinfo;
  for (e = sinfo->htab->first; e; e = e->next)
    {
      if (e->secinfo != secinfo)
	{
	  secinfo->sec->size = size;
	  secinfo = e->secinfo;
	}
      if (e->alignment)
	{
	  if (e->secinfo->first_str == NULL)
	    {
	      e->secinfo->first_str = e;
	      size = 0;
	    }
	  size = (size + e->alignment - 1) & ~((bfd_vma) e->alignment - 1);
	  e->u.index = size;
	  size += e->len;
	}
    }
  secinfo->sec->size = size;
  if (secinfo->sec->alignment_power != 0)
    {
      bfd_size_type align = (bfd_size_type) 1 << secinfo->sec->alignment_power;
      secinfo->sec->size = (secinfo->sec->size + align - 1) & -align;
    }

  /* And now adjust the rest, removing them from the chain (but not hashtable)
     at the same time.  */
  for (a = &sinfo->htab->first, e = *a; e; e = e->next)
    if (e->alignment)
      a = &e->next;
    else
      {
	*a = e->next;
	if (e->len)
	  {
	    e->secinfo = e->u.suffix->secinfo;
	    e->alignment = e->u.suffix->alignment;
	    e->u.index = e->u.suffix->u.index + (e->u.suffix->len - e->len);
	  }
      }
}

/* This function is called once after all SEC_MERGE sections are registered
   with _bfd_merge_section.  */

bfd_boolean
_bfd_merge_sections (bfd *abfd ATTRIBUTE_UNUSED,
		     struct bfd_link_info *info ATTRIBUTE_UNUSED,
		     void *xsinfo,
		     void (*remove_hook) (bfd *, asection *))
{
  struct sec_merge_info *sinfo;

  for (sinfo = (struct sec_merge_info *) xsinfo; sinfo; sinfo = sinfo->next)
    {
      struct sec_merge_sec_info * secinfo;

      if (! sinfo->chain)
	continue;

      /* Move sinfo->chain to head of the chain, terminate it.  */
      secinfo = sinfo->chain;
      sinfo->chain = secinfo->next;
      secinfo->next = NULL;

      /* Record the sections into the hash table.  */
      for (secinfo = sinfo->chain; secinfo; secinfo = secinfo->next)
	if (secinfo->sec->flags & SEC_EXCLUDE)
	  {
	    *secinfo->psecinfo = NULL;
	    if (remove_hook)
	      (*remove_hook) (abfd, secinfo->sec);
	  }
	else if (! record_section (sinfo, secinfo))
	  break;

      if (secinfo)
	continue;

      if (sinfo->htab->first == NULL)
	continue;

      if (sinfo->htab->strings)
	merge_strings (sinfo);
      else
	{
	  struct sec_merge_hash_entry *e;
	  bfd_size_type size = 0;

	  /* Things are much simpler for non-strings.
	     Just assign them slots in the section.  */
	  secinfo = NULL;
	  for (e = sinfo->htab->first; e; e = e->next)
	    {
	      if (e->secinfo->first_str == NULL)
		{
		  if (secinfo)
		    secinfo->sec->size = size;
		  e->secinfo->first_str = e;
		  size = 0;
		}
	      size = (size + e->alignment - 1)
		     & ~((bfd_vma) e->alignment - 1);
	      e->u.index = size;
	      size += e->len;
	      secinfo = e->secinfo;
	    }
	  secinfo->sec->size = size;
	}

	/* Finally remove all input sections which have not made it into
	   the hash table at all.  */
	for (secinfo = sinfo->chain; secinfo; secinfo = secinfo->next)
	  if (secinfo->first_str == NULL)
	    secinfo->sec->flags |= SEC_EXCLUDE | SEC_KEEP;
    }

  return TRUE;
}

/* Write out the merged section.  */

bfd_boolean
_bfd_write_merged_section (bfd *output_bfd, asection *sec, void *psecinfo)
{
  struct sec_merge_sec_info *secinfo;
  file_ptr pos;

  secinfo = (struct sec_merge_sec_info *) psecinfo;

  if (secinfo->first_str == NULL)
    return TRUE;

  pos = sec->output_section->filepos + sec->output_offset;
  if (bfd_seek (output_bfd, pos, SEEK_SET) != 0)
    return FALSE;

  if (! sec_merge_emit (output_bfd, secinfo->first_str))
    return FALSE;

  return TRUE;
}

/* Adjust an address in the SEC_MERGE section.  Given OFFSET within
   *PSEC, this returns the new offset in the adjusted SEC_MERGE
   section and writes the new section back into *PSEC.  */

bfd_vma
_bfd_merged_section_offset (bfd *output_bfd ATTRIBUTE_UNUSED, asection **psec,
			    void *psecinfo, bfd_vma offset)
{
  struct sec_merge_sec_info *secinfo;
  struct sec_merge_hash_entry *entry;
  unsigned char *p;
  asection *sec = *psec;

  secinfo = (struct sec_merge_sec_info *) psecinfo;

  if (offset >= sec->rawsize)
    {
      if (offset > sec->rawsize)
	{
	  (*_bfd_error_handler)
	    (_("%s: access beyond end of merged section (%ld)"),
	     bfd_get_filename (sec->owner), (long) offset);
	}
      return secinfo->first_str ? sec->size : 0;
    }

  if (secinfo->htab->strings)
    {
      if (sec->entsize == 1)
	{
	  p = secinfo->contents + offset - 1;
	  while (p >= secinfo->contents && *p)
	    --p;
	  ++p;
	}
      else
	{
	  p = secinfo->contents + (offset / sec->entsize) * sec->entsize;
	  p -= sec->entsize;
	  while (p >= secinfo->contents)
	    {
	      unsigned int i;

	      for (i = 0; i < sec->entsize; ++i)
		if (p[i] != '\0')
		  break;
	      if (i == sec->entsize)
		break;
	      p -= sec->entsize;
	    }
	  p += sec->entsize;
	}
    }
  else
    {
      p = secinfo->contents + (offset / sec->entsize) * sec->entsize;
    }
  entry = sec_merge_hash_lookup (secinfo->htab, (char *) p, 0, FALSE);
  if (!entry)
    {
      if (! secinfo->htab->strings)
	abort ();
      /* This should only happen if somebody points into the padding
	 after a NUL character but before next entity.  */
      if (*p)
	abort ();
      if (! secinfo->htab->first)
	abort ();
      entry = secinfo->htab->first;
      p = (secinfo->contents + (offset / sec->entsize + 1) * sec->entsize
	   - entry->len);
    }

  *psec = entry->secinfo->sec;
  return entry->u.index + (secinfo->contents + offset - p);
}
