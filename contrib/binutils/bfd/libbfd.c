/* Assorted BFD support routines, only used internally.
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2007
   Free Software Foundation, Inc.
   Written by Cygnus Support.

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
#include "libbfd.h"

#ifndef HAVE_GETPAGESIZE
#define getpagesize() 2048
#endif

/*
SECTION
	Implementation details

SUBSECTION
	Internal functions

DESCRIPTION
	These routines are used within BFD.
	They are not intended for export, but are documented here for
	completeness.
*/

/* A routine which is used in target vectors for unsupported
   operations.  */

bfd_boolean
bfd_false (bfd *ignore ATTRIBUTE_UNUSED)
{
  bfd_set_error (bfd_error_invalid_operation);
  return FALSE;
}

/* A routine which is used in target vectors for supported operations
   which do not actually do anything.  */

bfd_boolean
bfd_true (bfd *ignore ATTRIBUTE_UNUSED)
{
  return TRUE;
}

/* A routine which is used in target vectors for unsupported
   operations which return a pointer value.  */

void *
bfd_nullvoidptr (bfd *ignore ATTRIBUTE_UNUSED)
{
  bfd_set_error (bfd_error_invalid_operation);
  return NULL;
}

int
bfd_0 (bfd *ignore ATTRIBUTE_UNUSED)
{
  return 0;
}

unsigned int
bfd_0u (bfd *ignore ATTRIBUTE_UNUSED)
{
   return 0;
}

long
bfd_0l (bfd *ignore ATTRIBUTE_UNUSED)
{
  return 0;
}

/* A routine which is used in target vectors for unsupported
   operations which return -1 on error.  */

long
_bfd_n1 (bfd *ignore_abfd ATTRIBUTE_UNUSED)
{
  bfd_set_error (bfd_error_invalid_operation);
  return -1;
}

void
bfd_void (bfd *ignore ATTRIBUTE_UNUSED)
{
}

long
_bfd_norelocs_get_reloc_upper_bound (bfd *abfd ATTRIBUTE_UNUSED,
				     asection *sec ATTRIBUTE_UNUSED)
{
  return sizeof (arelent *);
}

long
_bfd_norelocs_canonicalize_reloc (bfd *abfd ATTRIBUTE_UNUSED,
				  asection *sec ATTRIBUTE_UNUSED,
				  arelent **relptr,
				  asymbol **symbols ATTRIBUTE_UNUSED)
{
  *relptr = NULL;
  return 0;
}

bfd_boolean
_bfd_nocore_core_file_matches_executable_p
  (bfd *ignore_core_bfd ATTRIBUTE_UNUSED,
   bfd *ignore_exec_bfd ATTRIBUTE_UNUSED)
{
  bfd_set_error (bfd_error_invalid_operation);
  return FALSE;
}

/* Routine to handle core_file_failing_command entry point for targets
   without core file support.  */

char *
_bfd_nocore_core_file_failing_command (bfd *ignore_abfd ATTRIBUTE_UNUSED)
{
  bfd_set_error (bfd_error_invalid_operation);
  return NULL;
}

/* Routine to handle core_file_failing_signal entry point for targets
   without core file support.  */

int
_bfd_nocore_core_file_failing_signal (bfd *ignore_abfd ATTRIBUTE_UNUSED)
{
  bfd_set_error (bfd_error_invalid_operation);
  return 0;
}

const bfd_target *
_bfd_dummy_target (bfd *ignore_abfd ATTRIBUTE_UNUSED)
{
  bfd_set_error (bfd_error_wrong_format);
  return 0;
}

/* Allocate memory using malloc.  */

void *
bfd_malloc (bfd_size_type size)
{
  void *ptr;

  if (size != (size_t) size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  ptr = malloc ((size_t) size);
  if (ptr == NULL && (size_t) size != 0)
    bfd_set_error (bfd_error_no_memory);

  return ptr;
}

/* Allocate memory using malloc, nmemb * size with overflow checking.  */

void *
bfd_malloc2 (bfd_size_type nmemb, bfd_size_type size)
{
  void *ptr;

  if ((nmemb | size) >= HALF_BFD_SIZE_TYPE
      && size != 0
      && nmemb > ~(bfd_size_type) 0 / size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  size *= nmemb;

  if (size != (size_t) size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  ptr = malloc ((size_t) size);
  if (ptr == NULL && (size_t) size != 0)
    bfd_set_error (bfd_error_no_memory);

  return ptr;
}

/* Reallocate memory using realloc.  */

void *
bfd_realloc (void *ptr, bfd_size_type size)
{
  void *ret;

  if (size != (size_t) size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  if (ptr == NULL)
    ret = malloc ((size_t) size);
  else
    ret = realloc (ptr, (size_t) size);

  if (ret == NULL && (size_t) size != 0)
    bfd_set_error (bfd_error_no_memory);

  return ret;
}

/* Reallocate memory using realloc, nmemb * size with overflow checking.  */

void *
bfd_realloc2 (void *ptr, bfd_size_type nmemb, bfd_size_type size)
{
  void *ret;

  if ((nmemb | size) >= HALF_BFD_SIZE_TYPE
      && size != 0
      && nmemb > ~(bfd_size_type) 0 / size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  size *= nmemb;

  if (size != (size_t) size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  if (ptr == NULL)
    ret = malloc ((size_t) size);
  else
    ret = realloc (ptr, (size_t) size);

  if (ret == NULL && (size_t) size != 0)
    bfd_set_error (bfd_error_no_memory);

  return ret;
}

/* Allocate memory using malloc and clear it.  */

void *
bfd_zmalloc (bfd_size_type size)
{
  void *ptr;

  if (size != (size_t) size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  ptr = malloc ((size_t) size);

  if ((size_t) size != 0)
    {
      if (ptr == NULL)
	bfd_set_error (bfd_error_no_memory);
      else
	memset (ptr, 0, (size_t) size);
    }

  return ptr;
}

/* Allocate memory using malloc (nmemb * size) with overflow checking
   and clear it.  */

void *
bfd_zmalloc2 (bfd_size_type nmemb, bfd_size_type size)
{
  void *ptr;

  if ((nmemb | size) >= HALF_BFD_SIZE_TYPE
      && size != 0
      && nmemb > ~(bfd_size_type) 0 / size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  size *= nmemb;

  if (size != (size_t) size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  ptr = malloc ((size_t) size);

  if ((size_t) size != 0)
    {
      if (ptr == NULL)
	bfd_set_error (bfd_error_no_memory);
      else
	memset (ptr, 0, (size_t) size);
    }

  return ptr;
}

/*
INTERNAL_FUNCTION
	bfd_write_bigendian_4byte_int

SYNOPSIS
	bfd_boolean bfd_write_bigendian_4byte_int (bfd *, unsigned int);

DESCRIPTION
	Write a 4 byte integer @var{i} to the output BFD @var{abfd}, in big
	endian order regardless of what else is going on.  This is useful in
	archives.

*/
bfd_boolean
bfd_write_bigendian_4byte_int (bfd *abfd, unsigned int i)
{
  bfd_byte buffer[4];
  bfd_putb32 ((bfd_vma) i, buffer);
  return bfd_bwrite (buffer, (bfd_size_type) 4, abfd) == 4;
}


/** The do-it-yourself (byte) sex-change kit */

/* The middle letter e.g. get<b>short indicates Big or Little endian
   target machine.  It doesn't matter what the byte order of the host
   machine is; these routines work for either.  */

/* FIXME: Should these take a count argument?
   Answer (gnu@cygnus.com):  No, but perhaps they should be inline
                             functions in swap.h #ifdef __GNUC__.
                             Gprof them later and find out.  */

/*
FUNCTION
	bfd_put_size
FUNCTION
	bfd_get_size

DESCRIPTION
	These macros as used for reading and writing raw data in
	sections; each access (except for bytes) is vectored through
	the target format of the BFD and mangled accordingly. The
	mangling performs any necessary endian translations and
	removes alignment restrictions.  Note that types accepted and
	returned by these macros are identical so they can be swapped
	around in macros---for example, @file{libaout.h} defines <<GET_WORD>>
	to either <<bfd_get_32>> or <<bfd_get_64>>.

	In the put routines, @var{val} must be a <<bfd_vma>>.  If we are on a
	system without prototypes, the caller is responsible for making
	sure that is true, with a cast if necessary.  We don't cast
	them in the macro definitions because that would prevent <<lint>>
	or <<gcc -Wall>> from detecting sins such as passing a pointer.
	To detect calling these with less than a <<bfd_vma>>, use
	<<gcc -Wconversion>> on a host with 64 bit <<bfd_vma>>'s.

.
.{* Byte swapping macros for user section data.  *}
.
.#define bfd_put_8(abfd, val, ptr) \
.  ((void) (*((unsigned char *) (ptr)) = (val) & 0xff))
.#define bfd_put_signed_8 \
.  bfd_put_8
.#define bfd_get_8(abfd, ptr) \
.  (*(unsigned char *) (ptr) & 0xff)
.#define bfd_get_signed_8(abfd, ptr) \
.  (((*(unsigned char *) (ptr) & 0xff) ^ 0x80) - 0x80)
.
.#define bfd_put_16(abfd, val, ptr) \
.  BFD_SEND (abfd, bfd_putx16, ((val),(ptr)))
.#define bfd_put_signed_16 \
.  bfd_put_16
.#define bfd_get_16(abfd, ptr) \
.  BFD_SEND (abfd, bfd_getx16, (ptr))
.#define bfd_get_signed_16(abfd, ptr) \
.  BFD_SEND (abfd, bfd_getx_signed_16, (ptr))
.
.#define bfd_put_32(abfd, val, ptr) \
.  BFD_SEND (abfd, bfd_putx32, ((val),(ptr)))
.#define bfd_put_signed_32 \
.  bfd_put_32
.#define bfd_get_32(abfd, ptr) \
.  BFD_SEND (abfd, bfd_getx32, (ptr))
.#define bfd_get_signed_32(abfd, ptr) \
.  BFD_SEND (abfd, bfd_getx_signed_32, (ptr))
.
.#define bfd_put_64(abfd, val, ptr) \
.  BFD_SEND (abfd, bfd_putx64, ((val), (ptr)))
.#define bfd_put_signed_64 \
.  bfd_put_64
.#define bfd_get_64(abfd, ptr) \
.  BFD_SEND (abfd, bfd_getx64, (ptr))
.#define bfd_get_signed_64(abfd, ptr) \
.  BFD_SEND (abfd, bfd_getx_signed_64, (ptr))
.
.#define bfd_get(bits, abfd, ptr)			\
.  ((bits) == 8 ? (bfd_vma) bfd_get_8 (abfd, ptr)	\
.   : (bits) == 16 ? bfd_get_16 (abfd, ptr)		\
.   : (bits) == 32 ? bfd_get_32 (abfd, ptr)		\
.   : (bits) == 64 ? bfd_get_64 (abfd, ptr)		\
.   : (abort (), (bfd_vma) - 1))
.
.#define bfd_put(bits, abfd, val, ptr)			\
.  ((bits) == 8 ? bfd_put_8  (abfd, val, ptr)		\
.   : (bits) == 16 ? bfd_put_16 (abfd, val, ptr)		\
.   : (bits) == 32 ? bfd_put_32 (abfd, val, ptr)		\
.   : (bits) == 64 ? bfd_put_64 (abfd, val, ptr)		\
.   : (abort (), (void) 0))
.
*/

/*
FUNCTION
	bfd_h_put_size
	bfd_h_get_size

DESCRIPTION
	These macros have the same function as their <<bfd_get_x>>
	brethren, except that they are used for removing information
	for the header records of object files. Believe it or not,
	some object files keep their header records in big endian
	order and their data in little endian order.
.
.{* Byte swapping macros for file header data.  *}
.
.#define bfd_h_put_8(abfd, val, ptr) \
.  bfd_put_8 (abfd, val, ptr)
.#define bfd_h_put_signed_8(abfd, val, ptr) \
.  bfd_put_8 (abfd, val, ptr)
.#define bfd_h_get_8(abfd, ptr) \
.  bfd_get_8 (abfd, ptr)
.#define bfd_h_get_signed_8(abfd, ptr) \
.  bfd_get_signed_8 (abfd, ptr)
.
.#define bfd_h_put_16(abfd, val, ptr) \
.  BFD_SEND (abfd, bfd_h_putx16, (val, ptr))
.#define bfd_h_put_signed_16 \
.  bfd_h_put_16
.#define bfd_h_get_16(abfd, ptr) \
.  BFD_SEND (abfd, bfd_h_getx16, (ptr))
.#define bfd_h_get_signed_16(abfd, ptr) \
.  BFD_SEND (abfd, bfd_h_getx_signed_16, (ptr))
.
.#define bfd_h_put_32(abfd, val, ptr) \
.  BFD_SEND (abfd, bfd_h_putx32, (val, ptr))
.#define bfd_h_put_signed_32 \
.  bfd_h_put_32
.#define bfd_h_get_32(abfd, ptr) \
.  BFD_SEND (abfd, bfd_h_getx32, (ptr))
.#define bfd_h_get_signed_32(abfd, ptr) \
.  BFD_SEND (abfd, bfd_h_getx_signed_32, (ptr))
.
.#define bfd_h_put_64(abfd, val, ptr) \
.  BFD_SEND (abfd, bfd_h_putx64, (val, ptr))
.#define bfd_h_put_signed_64 \
.  bfd_h_put_64
.#define bfd_h_get_64(abfd, ptr) \
.  BFD_SEND (abfd, bfd_h_getx64, (ptr))
.#define bfd_h_get_signed_64(abfd, ptr) \
.  BFD_SEND (abfd, bfd_h_getx_signed_64, (ptr))
.
.{* Aliases for the above, which should eventually go away.  *}
.
.#define H_PUT_64  bfd_h_put_64
.#define H_PUT_32  bfd_h_put_32
.#define H_PUT_16  bfd_h_put_16
.#define H_PUT_8   bfd_h_put_8
.#define H_PUT_S64 bfd_h_put_signed_64
.#define H_PUT_S32 bfd_h_put_signed_32
.#define H_PUT_S16 bfd_h_put_signed_16
.#define H_PUT_S8  bfd_h_put_signed_8
.#define H_GET_64  bfd_h_get_64
.#define H_GET_32  bfd_h_get_32
.#define H_GET_16  bfd_h_get_16
.#define H_GET_8   bfd_h_get_8
.#define H_GET_S64 bfd_h_get_signed_64
.#define H_GET_S32 bfd_h_get_signed_32
.#define H_GET_S16 bfd_h_get_signed_16
.#define H_GET_S8  bfd_h_get_signed_8
.
.*/

/* Sign extension to bfd_signed_vma.  */
#define COERCE16(x) (((bfd_signed_vma) (x) ^ 0x8000) - 0x8000)
#define COERCE32(x) (((bfd_signed_vma) (x) ^ 0x80000000) - 0x80000000)
#define EIGHT_GAZILLION ((bfd_int64_t) 1 << 63)
#define COERCE64(x) \
  (((bfd_int64_t) (x) ^ EIGHT_GAZILLION) - EIGHT_GAZILLION)

bfd_vma
bfd_getb16 (const void *p)
{
  const bfd_byte *addr = p;
  return (addr[0] << 8) | addr[1];
}

bfd_vma
bfd_getl16 (const void *p)
{
  const bfd_byte *addr = p;
  return (addr[1] << 8) | addr[0];
}

bfd_signed_vma
bfd_getb_signed_16 (const void *p)
{
  const bfd_byte *addr = p;
  return COERCE16 ((addr[0] << 8) | addr[1]);
}

bfd_signed_vma
bfd_getl_signed_16 (const void *p)
{
  const bfd_byte *addr = p;
  return COERCE16 ((addr[1] << 8) | addr[0]);
}

void
bfd_putb16 (bfd_vma data, void *p)
{
  bfd_byte *addr = p;
  addr[0] = (data >> 8) & 0xff;
  addr[1] = data & 0xff;
}

void
bfd_putl16 (bfd_vma data, void *p)
{
  bfd_byte *addr = p;
  addr[0] = data & 0xff;
  addr[1] = (data >> 8) & 0xff;
}

bfd_vma
bfd_getb32 (const void *p)
{
  const bfd_byte *addr = p;
  unsigned long v;

  v = (unsigned long) addr[0] << 24;
  v |= (unsigned long) addr[1] << 16;
  v |= (unsigned long) addr[2] << 8;
  v |= (unsigned long) addr[3];
  return v;
}

bfd_vma
bfd_getl32 (const void *p)
{
  const bfd_byte *addr = p;
  unsigned long v;

  v = (unsigned long) addr[0];
  v |= (unsigned long) addr[1] << 8;
  v |= (unsigned long) addr[2] << 16;
  v |= (unsigned long) addr[3] << 24;
  return v;
}

bfd_signed_vma
bfd_getb_signed_32 (const void *p)
{
  const bfd_byte *addr = p;
  unsigned long v;

  v = (unsigned long) addr[0] << 24;
  v |= (unsigned long) addr[1] << 16;
  v |= (unsigned long) addr[2] << 8;
  v |= (unsigned long) addr[3];
  return COERCE32 (v);
}

bfd_signed_vma
bfd_getl_signed_32 (const void *p)
{
  const bfd_byte *addr = p;
  unsigned long v;

  v = (unsigned long) addr[0];
  v |= (unsigned long) addr[1] << 8;
  v |= (unsigned long) addr[2] << 16;
  v |= (unsigned long) addr[3] << 24;
  return COERCE32 (v);
}

bfd_uint64_t
bfd_getb64 (const void *p ATTRIBUTE_UNUSED)
{
#ifdef BFD_HOST_64_BIT
  const bfd_byte *addr = p;
  bfd_uint64_t v;

  v  = addr[0]; v <<= 8;
  v |= addr[1]; v <<= 8;
  v |= addr[2]; v <<= 8;
  v |= addr[3]; v <<= 8;
  v |= addr[4]; v <<= 8;
  v |= addr[5]; v <<= 8;
  v |= addr[6]; v <<= 8;
  v |= addr[7];

  return v;
#else
  BFD_FAIL();
  return 0;
#endif
}

bfd_uint64_t
bfd_getl64 (const void *p ATTRIBUTE_UNUSED)
{
#ifdef BFD_HOST_64_BIT
  const bfd_byte *addr = p;
  bfd_uint64_t v;

  v  = addr[7]; v <<= 8;
  v |= addr[6]; v <<= 8;
  v |= addr[5]; v <<= 8;
  v |= addr[4]; v <<= 8;
  v |= addr[3]; v <<= 8;
  v |= addr[2]; v <<= 8;
  v |= addr[1]; v <<= 8;
  v |= addr[0];

  return v;
#else
  BFD_FAIL();
  return 0;
#endif

}

bfd_int64_t
bfd_getb_signed_64 (const void *p ATTRIBUTE_UNUSED)
{
#ifdef BFD_HOST_64_BIT
  const bfd_byte *addr = p;
  bfd_uint64_t v;

  v  = addr[0]; v <<= 8;
  v |= addr[1]; v <<= 8;
  v |= addr[2]; v <<= 8;
  v |= addr[3]; v <<= 8;
  v |= addr[4]; v <<= 8;
  v |= addr[5]; v <<= 8;
  v |= addr[6]; v <<= 8;
  v |= addr[7];

  return COERCE64 (v);
#else
  BFD_FAIL();
  return 0;
#endif
}

bfd_int64_t
bfd_getl_signed_64 (const void *p ATTRIBUTE_UNUSED)
{
#ifdef BFD_HOST_64_BIT
  const bfd_byte *addr = p;
  bfd_uint64_t v;

  v  = addr[7]; v <<= 8;
  v |= addr[6]; v <<= 8;
  v |= addr[5]; v <<= 8;
  v |= addr[4]; v <<= 8;
  v |= addr[3]; v <<= 8;
  v |= addr[2]; v <<= 8;
  v |= addr[1]; v <<= 8;
  v |= addr[0];

  return COERCE64 (v);
#else
  BFD_FAIL();
  return 0;
#endif
}

void
bfd_putb32 (bfd_vma data, void *p)
{
  bfd_byte *addr = p;
  addr[0] = (data >> 24) & 0xff;
  addr[1] = (data >> 16) & 0xff;
  addr[2] = (data >>  8) & 0xff;
  addr[3] = data & 0xff;
}

void
bfd_putl32 (bfd_vma data, void *p)
{
  bfd_byte *addr = p;
  addr[0] = data & 0xff;
  addr[1] = (data >>  8) & 0xff;
  addr[2] = (data >> 16) & 0xff;
  addr[3] = (data >> 24) & 0xff;
}

void
bfd_putb64 (bfd_uint64_t data ATTRIBUTE_UNUSED, void *p ATTRIBUTE_UNUSED)
{
#ifdef BFD_HOST_64_BIT
  bfd_byte *addr = p;
  addr[0] = (data >> (7*8)) & 0xff;
  addr[1] = (data >> (6*8)) & 0xff;
  addr[2] = (data >> (5*8)) & 0xff;
  addr[3] = (data >> (4*8)) & 0xff;
  addr[4] = (data >> (3*8)) & 0xff;
  addr[5] = (data >> (2*8)) & 0xff;
  addr[6] = (data >> (1*8)) & 0xff;
  addr[7] = (data >> (0*8)) & 0xff;
#else
  BFD_FAIL();
#endif
}

void
bfd_putl64 (bfd_uint64_t data ATTRIBUTE_UNUSED, void *p ATTRIBUTE_UNUSED)
{
#ifdef BFD_HOST_64_BIT
  bfd_byte *addr = p;
  addr[7] = (data >> (7*8)) & 0xff;
  addr[6] = (data >> (6*8)) & 0xff;
  addr[5] = (data >> (5*8)) & 0xff;
  addr[4] = (data >> (4*8)) & 0xff;
  addr[3] = (data >> (3*8)) & 0xff;
  addr[2] = (data >> (2*8)) & 0xff;
  addr[1] = (data >> (1*8)) & 0xff;
  addr[0] = (data >> (0*8)) & 0xff;
#else
  BFD_FAIL();
#endif
}

void
bfd_put_bits (bfd_uint64_t data, void *p, int bits, bfd_boolean big_p)
{
  bfd_byte *addr = p;
  int i;
  int bytes;

  if (bits % 8 != 0)
    abort ();

  bytes = bits / 8;
  for (i = 0; i < bytes; i++)
    {
      int index = big_p ? bytes - i - 1 : i;

      addr[index] = data & 0xff;
      data >>= 8;
    }
}

bfd_uint64_t
bfd_get_bits (const void *p, int bits, bfd_boolean big_p)
{
  const bfd_byte *addr = p;
  bfd_uint64_t data;
  int i;
  int bytes;

  if (bits % 8 != 0)
    abort ();

  data = 0;
  bytes = bits / 8;
  for (i = 0; i < bytes; i++)
    {
      int index = big_p ? i : bytes - i - 1;

      data = (data << 8) | addr[index];
    }

  return data;
}

/* Default implementation */

bfd_boolean
_bfd_generic_get_section_contents (bfd *abfd,
				   sec_ptr section,
				   void *location,
				   file_ptr offset,
				   bfd_size_type count)
{
  bfd_size_type sz;
  if (count == 0)
    return TRUE;

  sz = section->rawsize ? section->rawsize : section->size;
  if (offset + count > sz)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  if (bfd_seek (abfd, section->filepos + offset, SEEK_SET) != 0
      || bfd_bread (location, count, abfd) != count)
    return FALSE;

  return TRUE;
}

bfd_boolean
_bfd_generic_get_section_contents_in_window
  (bfd *abfd ATTRIBUTE_UNUSED,
   sec_ptr section ATTRIBUTE_UNUSED,
   bfd_window *w ATTRIBUTE_UNUSED,
   file_ptr offset ATTRIBUTE_UNUSED,
   bfd_size_type count ATTRIBUTE_UNUSED)
{
#ifdef USE_MMAP
  bfd_size_type sz;

  if (count == 0)
    return TRUE;
  if (abfd->xvec->_bfd_get_section_contents
      != _bfd_generic_get_section_contents)
    {
      /* We don't know what changes the bfd's get_section_contents
	 method may have to make.  So punt trying to map the file
	 window, and let get_section_contents do its thing.  */
      /* @@ FIXME : If the internal window has a refcount of 1 and was
	 allocated with malloc instead of mmap, just reuse it.  */
      bfd_free_window (w);
      w->i = bfd_zmalloc (sizeof (bfd_window_internal));
      if (w->i == NULL)
	return FALSE;
      w->i->data = bfd_malloc (count);
      if (w->i->data == NULL)
	{
	  free (w->i);
	  w->i = NULL;
	  return FALSE;
	}
      w->i->mapped = 0;
      w->i->refcount = 1;
      w->size = w->i->size = count;
      w->data = w->i->data;
      return bfd_get_section_contents (abfd, section, w->data, offset, count);
    }
  sz = section->rawsize ? section->rawsize : section->size;
  if (offset + count > sz
      || ! bfd_get_file_window (abfd, section->filepos + offset, count, w,
				TRUE))
    return FALSE;
  return TRUE;
#else
  abort ();
#endif
}

/* This generic function can only be used in implementations where creating
   NEW sections is disallowed.  It is useful in patching existing sections
   in read-write files, though.  See other set_section_contents functions
   to see why it doesn't work for new sections.  */
bfd_boolean
_bfd_generic_set_section_contents (bfd *abfd,
				   sec_ptr section,
				   const void *location,
				   file_ptr offset,
				   bfd_size_type count)
{
  if (count == 0)
    return TRUE;

  if (bfd_seek (abfd, section->filepos + offset, SEEK_SET) != 0
      || bfd_bwrite (location, count, abfd) != count)
    return FALSE;

  return TRUE;
}

/*
INTERNAL_FUNCTION
	bfd_log2

SYNOPSIS
	unsigned int bfd_log2 (bfd_vma x);

DESCRIPTION
	Return the log base 2 of the value supplied, rounded up.  E.g., an
	@var{x} of 1025 returns 11.  A @var{x} of 0 returns 0.
*/

unsigned int
bfd_log2 (bfd_vma x)
{
  unsigned int result = 0;

  while ((x = (x >> 1)) != 0)
    ++result;
  return result;
}

bfd_boolean
bfd_generic_is_local_label_name (bfd *abfd, const char *name)
{
  char locals_prefix = (bfd_get_symbol_leading_char (abfd) == '_') ? 'L' : '.';

  return name[0] == locals_prefix;
}

/*  Can be used from / for bfd_merge_private_bfd_data to check that
    endianness matches between input and output file.  Returns
    TRUE for a match, otherwise returns FALSE and emits an error.  */
bfd_boolean
_bfd_generic_verify_endian_match (bfd *ibfd, bfd *obfd)
{
  if (ibfd->xvec->byteorder != obfd->xvec->byteorder
      && ibfd->xvec->byteorder != BFD_ENDIAN_UNKNOWN
      && obfd->xvec->byteorder != BFD_ENDIAN_UNKNOWN)
    {
      const char *msg;

      if (bfd_big_endian (ibfd))
	msg = _("%B: compiled for a big endian system and target is little endian");
      else
	msg = _("%B: compiled for a little endian system and target is big endian");

      (*_bfd_error_handler) (msg, ibfd);

      bfd_set_error (bfd_error_wrong_format);
      return FALSE;
    }

  return TRUE;
}

/* Give a warning at runtime if someone compiles code which calls
   old routines.  */

void
warn_deprecated (const char *what,
		 const char *file,
		 int line,
		 const char *func)
{
  /* Poor man's tracking of functions we've already warned about.  */
  static size_t mask = 0;

  if (~(size_t) func & ~mask)
    {
      /* Note: separate sentences in order to allow
	 for translation into other languages.  */
      if (func)
	fprintf (stderr, _("Deprecated %s called at %s line %d in %s\n"),
		 what, file, line, func);
      else
	fprintf (stderr, _("Deprecated %s called\n"), what);
      mask |= ~(size_t) func;
    }
}

/* Helper function for reading uleb128 encoded data.  */

bfd_vma
read_unsigned_leb128 (bfd *abfd ATTRIBUTE_UNUSED,
		      bfd_byte *buf,
		      unsigned int *bytes_read_ptr)
{
  bfd_vma result;
  unsigned int num_read;
  unsigned int shift;
  unsigned char byte;

  result = 0;
  shift = 0;
  num_read = 0;
  do
    {
      byte = bfd_get_8 (abfd, buf);
      buf++;
      num_read++;
      result |= (((bfd_vma) byte & 0x7f) << shift);
      shift += 7;
    }
  while (byte & 0x80);
  *bytes_read_ptr = num_read;
  return result;
}

/* Helper function for reading sleb128 encoded data.  */

bfd_signed_vma
read_signed_leb128 (bfd *abfd ATTRIBUTE_UNUSED,
		    bfd_byte *buf,
		    unsigned int *bytes_read_ptr)
{
  bfd_vma result;
  unsigned int shift;
  unsigned int num_read;
  unsigned char byte;

  result = 0;
  shift = 0;
  num_read = 0;
  do
    {
      byte = bfd_get_8 (abfd, buf);
      buf ++;
      num_read ++;
      result |= (((bfd_vma) byte & 0x7f) << shift);
      shift += 7;
    }
  while (byte & 0x80);
  if (shift < 8 * sizeof (result) && (byte & 0x40))
    result |= (((bfd_vma) -1) << shift);
  *bytes_read_ptr = num_read;
  return result;
}

bfd_boolean
_bfd_generic_find_line (bfd *abfd ATTRIBUTE_UNUSED,
		       asymbol **symbols ATTRIBUTE_UNUSED,
		       asymbol *symbol ATTRIBUTE_UNUSED,
		       const char **filename_ptr ATTRIBUTE_UNUSED,
		       unsigned int *linenumber_ptr ATTRIBUTE_UNUSED)
{
  return FALSE;
}

bfd_boolean
_bfd_generic_init_private_section_data (bfd *ibfd ATTRIBUTE_UNUSED,
					asection *isec ATTRIBUTE_UNUSED,
					bfd *obfd ATTRIBUTE_UNUSED,
					asection *osec ATTRIBUTE_UNUSED,
					struct bfd_link_info *link_info ATTRIBUTE_UNUSED)
{
  return TRUE;
}
