/* Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Jakub Jelinek <jakub@redhat.com>.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

/* Locate the FDE entry for a given address, using PT_GNU_EH_FRAME ELF
   segment and dl_iterate_phdr to avoid register/deregister calls at
   DSO load/unload.  */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "tconfig.h"
#include "tsystem.h"
#ifndef inhibit_libc
#include <link.h>
#endif
#include "coretypes.h"
#include "tm.h"
#include "dwarf2.h"
#include "unwind.h"
#define NO_BASE_OF_ENCODED_VALUE
#include "unwind-pe.h"
#include "unwind-dw2-fde.h"
#include "unwind-compat.h"
#include "gthr.h"

#if !defined(inhibit_libc) && defined(HAVE_LD_EH_FRAME_HDR) \
    && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2) \
	|| (__GLIBC__ == 2 && __GLIBC_MINOR__ == 2 && defined(DT_CONFIG)))

#ifndef __RELOC_POINTER
# define __RELOC_POINTER(ptr, base) ((ptr) + (base))
#endif

static const fde * _Unwind_Find_registered_FDE (void *pc, struct dwarf_eh_bases *bases);

#define _Unwind_Find_FDE _Unwind_Find_registered_FDE
#include "unwind-dw2-fde.c"
#undef _Unwind_Find_FDE

#ifndef PT_GNU_EH_FRAME
#define PT_GNU_EH_FRAME (PT_LOOS + 0x474e550)
#endif

struct unw_eh_callback_data
{
  _Unwind_Ptr pc;
  void *tbase;
  void *dbase;
  void *func;
  const fde *ret;
  int check_cache;
};

struct unw_eh_frame_hdr
{
  unsigned char version;
  unsigned char eh_frame_ptr_enc;
  unsigned char fde_count_enc;
  unsigned char table_enc;
};

#define FRAME_HDR_CACHE_SIZE 8

static struct frame_hdr_cache_element
{
  _Unwind_Ptr pc_low;
  _Unwind_Ptr pc_high;
  _Unwind_Ptr load_base;
  const ElfW(Phdr) *p_eh_frame_hdr;
  const ElfW(Phdr) *p_dynamic;
  struct frame_hdr_cache_element *link;
} frame_hdr_cache[FRAME_HDR_CACHE_SIZE];

static struct frame_hdr_cache_element *frame_hdr_cache_head;

/* Like base_of_encoded_value, but take the base from a struct
   unw_eh_callback_data instead of an _Unwind_Context.  */

static _Unwind_Ptr
base_from_cb_data (unsigned char encoding, struct unw_eh_callback_data *data)
{
  if (encoding == DW_EH_PE_omit)
    return 0;

  switch (encoding & 0x70)
    {
    case DW_EH_PE_absptr:
    case DW_EH_PE_pcrel:
    case DW_EH_PE_aligned:
      return 0;

    case DW_EH_PE_textrel:
      return (_Unwind_Ptr) data->tbase;
    case DW_EH_PE_datarel:
      return (_Unwind_Ptr) data->dbase;
    default:
      gcc_unreachable ();
    }
}

static int
_Unwind_IteratePhdrCallback (struct dl_phdr_info *info, size_t size, void *ptr)
{
  struct unw_eh_callback_data *data = (struct unw_eh_callback_data *) ptr;
  const ElfW(Phdr) *phdr, *p_eh_frame_hdr, *p_dynamic;
  long n, match;
#ifdef __FRV_FDPIC__
  struct elf32_fdpic_loadaddr load_base;
#else
  _Unwind_Ptr load_base;
#endif
  const unsigned char *p;
  const struct unw_eh_frame_hdr *hdr;
  _Unwind_Ptr eh_frame;
  struct object ob;
  
  struct ext_dl_phdr_info
    {
      ElfW(Addr) dlpi_addr;
      const char *dlpi_name;
      const ElfW(Phdr) *dlpi_phdr;
      ElfW(Half) dlpi_phnum;
      unsigned long long int dlpi_adds;
      unsigned long long int dlpi_subs;
    };

  match = 0;
  phdr = info->dlpi_phdr;
  load_base = info->dlpi_addr;
  p_eh_frame_hdr = NULL;
  p_dynamic = NULL;

  struct frame_hdr_cache_element *prev_cache_entry = NULL,
    *last_cache_entry = NULL;

  if (data->check_cache && size >= sizeof (struct ext_dl_phdr_info))
    {
      static unsigned long long adds = -1ULL, subs;
      struct ext_dl_phdr_info *einfo = (struct ext_dl_phdr_info *) info;

      /* We use a least recently used cache replacement policy.  Also,
	 the most recently used cache entries are placed at the head
	 of the search chain.  */

      if (einfo->dlpi_adds == adds && einfo->dlpi_subs == subs)
	{
	  /* Find data->pc in shared library cache.
	     Set load_base, p_eh_frame_hdr and p_dynamic
	     plus match from the cache and goto
	     "Read .eh_frame_hdr header." below.  */

	  struct frame_hdr_cache_element *cache_entry;

	  for (cache_entry = frame_hdr_cache_head;
	       cache_entry;
	       cache_entry = cache_entry->link)
	    {
	      if (data->pc >= cache_entry->pc_low
		  && data->pc < cache_entry->pc_high)
		{
		  load_base = cache_entry->load_base;
		  p_eh_frame_hdr = cache_entry->p_eh_frame_hdr;
		  p_dynamic = cache_entry->p_dynamic;

		  /* And move the entry we're using to the head.  */
		  if (cache_entry != frame_hdr_cache_head)
		    {
		      prev_cache_entry->link = cache_entry->link;
		      cache_entry->link = frame_hdr_cache_head;
		      frame_hdr_cache_head = cache_entry;
		    }
		  goto found;
		}
		  
	      last_cache_entry = cache_entry;
	      /* Exit early if we found an unused entry.  */
	      if ((cache_entry->pc_low | cache_entry->pc_high) == 0)
		break;
	      if (cache_entry->link != NULL)
		prev_cache_entry = cache_entry;		  
	    }
	}
      else
	{
	  adds = einfo->dlpi_adds;
	  subs = einfo->dlpi_subs;
	  /* Initialize the cache.  Create a chain of cache entries,
	     with the final one terminated by a NULL link.  */
	  int i;
	  for (i = 0; i < FRAME_HDR_CACHE_SIZE; i++)
	    {
	      frame_hdr_cache[i].pc_low = 0;
	      frame_hdr_cache[i].pc_high = 0;
	      frame_hdr_cache[i].link = &frame_hdr_cache[i+1];
	    }
	  frame_hdr_cache[i-1].link = NULL;
	  frame_hdr_cache_head = &frame_hdr_cache[0];
	  data->check_cache = 0;
	}
    }

  /* Make sure struct dl_phdr_info is at least as big as we need.  */
  if (size < offsetof (struct dl_phdr_info, dlpi_phnum)
	     + sizeof (info->dlpi_phnum))
    return -1;
 
  _Unwind_Ptr pc_low = 0, pc_high = 0;

  /* See if PC falls into one of the loaded segments.  Find the eh_frame
     segment at the same time.  */
  for (n = info->dlpi_phnum; --n >= 0; phdr++)
    {
      if (phdr->p_type == PT_LOAD)
	{
	  _Unwind_Ptr vaddr = (_Unwind_Ptr)
	    __RELOC_POINTER (phdr->p_vaddr, load_base);
	  if (data->pc >= vaddr && data->pc < vaddr + phdr->p_memsz)
	    {
	      match = 1;
	      pc_low = vaddr;
	      pc_high =  vaddr + phdr->p_memsz;
	    }
	}
      else if (phdr->p_type == PT_GNU_EH_FRAME)
	p_eh_frame_hdr = phdr;
      else if (phdr->p_type == PT_DYNAMIC)
	p_dynamic = phdr;
    }
  
  if (!match)
    return 0;

  if (size >= sizeof (struct ext_dl_phdr_info))
    {
      /* Move the cache entry we're about to overwrite to the head of
	 the list.  If either last_cache_entry or prev_cache_entry are
	 NULL, that cache entry is already at the head.  */
      if (last_cache_entry != NULL && prev_cache_entry != NULL)
	{
	  prev_cache_entry->link = last_cache_entry->link;
	  last_cache_entry->link = frame_hdr_cache_head;
	  frame_hdr_cache_head = last_cache_entry;
	}

      frame_hdr_cache_head->load_base = load_base;
      frame_hdr_cache_head->p_eh_frame_hdr = p_eh_frame_hdr;
      frame_hdr_cache_head->p_dynamic = p_dynamic;
      frame_hdr_cache_head->pc_low = pc_low;
      frame_hdr_cache_head->pc_high = pc_high;
    }

 found:

  if (!p_eh_frame_hdr)
    return 0;

  /* Read .eh_frame_hdr header.  */
  hdr = (const struct unw_eh_frame_hdr *)
    __RELOC_POINTER (p_eh_frame_hdr->p_vaddr, load_base);
  if (hdr->version != 1)
    return 1;

#ifdef CRT_GET_RFIB_DATA
# ifdef __i386__
  data->dbase = NULL;
  if (p_dynamic)
    {
      /* For dynamically linked executables and shared libraries,
	 DT_PLTGOT is the gp value for that object.  */
      ElfW(Dyn) *dyn = (ElfW(Dyn) *)
	__RELOC_POINTER (p_dynamic->p_vaddr, load_base);
      for (; dyn->d_tag != DT_NULL ; dyn++)
	if (dyn->d_tag == DT_PLTGOT)
	  {
	    /* On IA-32, _DYNAMIC is writable and GLIBC has relocated it.  */
	    data->dbase = (void *) dyn->d_un.d_ptr;
	    break;
	  }
    }
# elif defined __FRV_FDPIC__ && defined __linux__
  data->dbase = load_base.got_value;
# else
#  error What is DW_EH_PE_datarel base on this platform?
# endif
#endif

  p = read_encoded_value_with_base (hdr->eh_frame_ptr_enc,
				    base_from_cb_data (hdr->eh_frame_ptr_enc,
						       data),
				    (const unsigned char *) (hdr + 1),
				    &eh_frame);

  /* We require here specific table encoding to speed things up.
     Also, DW_EH_PE_datarel here means using PT_GNU_EH_FRAME start
     as base, not the processor specific DW_EH_PE_datarel.  */
  if (hdr->fde_count_enc != DW_EH_PE_omit
      && hdr->table_enc == (DW_EH_PE_datarel | DW_EH_PE_sdata4))
    {
      _Unwind_Ptr fde_count;

      p = read_encoded_value_with_base (hdr->fde_count_enc,
					base_from_cb_data (hdr->fde_count_enc,
							   data),
					p, &fde_count);
      /* Shouldn't happen.  */
      if (fde_count == 0)
	return 1;
      if ((((_Unwind_Ptr) p) & 3) == 0)
	{
	  struct fde_table {
	    signed initial_loc __attribute__ ((mode (SI)));
	    signed fde __attribute__ ((mode (SI)));
	  };
	  const struct fde_table *table = (const struct fde_table *) p;
	  size_t lo, hi, mid;
	  _Unwind_Ptr data_base = (_Unwind_Ptr) hdr;
	  fde *f;
	  unsigned int f_enc, f_enc_size;
	  _Unwind_Ptr range;

	  mid = fde_count - 1;
	  if (data->pc < table[0].initial_loc + data_base)
	    return 1;
	  else if (data->pc < table[mid].initial_loc + data_base)
	    {
	      lo = 0;
	      hi = mid;

	      while (lo < hi)
		{
		  mid = (lo + hi) / 2;
		  if (data->pc < table[mid].initial_loc + data_base)
		    hi = mid;
		  else if (data->pc >= table[mid + 1].initial_loc + data_base)
		    lo = mid + 1;
		  else
		    break;
		}

	      gcc_assert (lo < hi);
	    }

	  f = (fde *) (table[mid].fde + data_base);
	  f_enc = get_fde_encoding (f);
	  f_enc_size = size_of_encoded_value (f_enc);
	  read_encoded_value_with_base (f_enc & 0x0f, 0,
					&f->pc_begin[f_enc_size], &range);
	  if (data->pc < table[mid].initial_loc + data_base + range)
	    data->ret = f;
	  data->func = (void *) (table[mid].initial_loc + data_base);
	  return 1;
	}
    }

  /* We have no sorted search table, so need to go the slow way.
     As soon as GLIBC will provide API so to notify that a library has been
     removed, we could cache this (and thus use search_object).  */
  ob.pc_begin = NULL;
  ob.tbase = data->tbase;
  ob.dbase = data->dbase;
  ob.u.single = (fde *) eh_frame;
  ob.s.i = 0;
  ob.s.b.mixed_encoding = 1;  /* Need to assume worst case.  */
  data->ret = linear_search_fdes (&ob, (fde *) eh_frame, (void *) data->pc);
  if (data->ret != NULL)
    {
      _Unwind_Ptr func;
      unsigned int encoding = get_fde_encoding (data->ret);
      
      read_encoded_value_with_base (encoding,
				    base_from_cb_data (encoding, data),
				    data->ret->pc_begin, &func);
      data->func = (void *) func;
    }
  return 1;
}

const fde *
_Unwind_Find_FDE (void *pc, struct dwarf_eh_bases *bases)
{
  struct unw_eh_callback_data data;
  const fde *ret;

  ret = _Unwind_Find_registered_FDE (pc, bases);
  if (ret != NULL)
    return ret;

  data.pc = (_Unwind_Ptr) pc;
  data.tbase = NULL;
  data.dbase = NULL;
  data.func = NULL;
  data.ret = NULL;
  data.check_cache = 1;

  if (dl_iterate_phdr (_Unwind_IteratePhdrCallback, &data) < 0)
    return NULL;

  if (data.ret)
    {
      bases->tbase = data.tbase;
      bases->dbase = data.dbase;
      bases->func = data.func;
    }
  return data.ret;
}

#else
/* Prevent multiple include of header files.  */
#define _Unwind_Find_FDE _Unwind_Find_FDE
#include "unwind-dw2-fde.c"
#endif

#if defined (USE_GAS_SYMVER) && defined (SHARED) && defined (USE_LIBUNWIND_EXCEPTIONS)
alias (_Unwind_Find_FDE);
#endif
