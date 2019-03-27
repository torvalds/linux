/* SJLJ exception handling and frame unwind runtime interface routines.
   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2006
   Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combined
   executable.)

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "tconfig.h"
#include "tsystem.h"
#include "coretypes.h"
#include "tm.h"
#include "unwind.h"
#include "gthr.h"

#ifdef __USING_SJLJ_EXCEPTIONS__

#ifdef DONT_USE_BUILTIN_SETJMP
#ifndef inhibit_libc
#include <setjmp.h>
#else
typedef void *jmp_buf[JMP_BUF_SIZE];
extern void longjmp(jmp_buf, int) __attribute__((noreturn));
#endif
#else
#define longjmp __builtin_longjmp
#endif

/* The setjmp side is dealt with in the except.c file.  */
#undef setjmp
#define setjmp setjmp_should_not_be_used_in_this_file


/* This structure is allocated on the stack of the target function.
   This must match the definition created in except.c:init_eh.  */
struct SjLj_Function_Context
{
  /* This is the chain through all registered contexts.  It is
     filled in by _Unwind_SjLj_Register.  */
  struct SjLj_Function_Context *prev;

  /* This is assigned in by the target function before every call
     to the index of the call site in the lsda.  It is assigned by
     the personality routine to the landing pad index.  */
  int call_site;

  /* This is how data is returned from the personality routine to
     the target function's handler.  */
  _Unwind_Word data[4];

  /* These are filled in once by the target function before any
     exceptions are expected to be handled.  */
  _Unwind_Personality_Fn personality;
  void *lsda;

#ifdef DONT_USE_BUILTIN_SETJMP
  /* We don't know what sort of alignment requirements the system
     jmp_buf has.  We over estimated in except.c, and now we have
     to match that here just in case the system *didn't* have more
     restrictive requirements.  */
  jmp_buf jbuf __attribute__((aligned));
#else
  void *jbuf[];
#endif
};

struct _Unwind_Context
{
  struct SjLj_Function_Context *fc;
};

typedef struct
{
  _Unwind_Personality_Fn personality;
} _Unwind_FrameState;


/* Manage the chain of registered function contexts.  */

/* Single threaded fallback chain.  */
static struct SjLj_Function_Context *fc_static;

#if __GTHREADS
static __gthread_key_t fc_key;
static int use_fc_key = -1;

static void
fc_key_init (void)
{
  use_fc_key = __gthread_key_create (&fc_key, 0) == 0;
}

static void
fc_key_init_once (void)
{
  static __gthread_once_t once = __GTHREAD_ONCE_INIT;
  if (__gthread_once (&once, fc_key_init) != 0 || use_fc_key < 0)
    use_fc_key = 0;
}
#endif

void
_Unwind_SjLj_Register (struct SjLj_Function_Context *fc)
{
#if __GTHREADS
  if (use_fc_key < 0)
    fc_key_init_once ();

  if (use_fc_key)
    {
      fc->prev = __gthread_getspecific (fc_key);
      __gthread_setspecific (fc_key, fc);
    }
  else
#endif
    {
      fc->prev = fc_static;
      fc_static = fc;
    }
}

static inline struct SjLj_Function_Context *
_Unwind_SjLj_GetContext (void)
{
#if __GTHREADS
  if (use_fc_key < 0)
    fc_key_init_once ();

  if (use_fc_key)
    return __gthread_getspecific (fc_key);
#endif
  return fc_static;
}

static inline void
_Unwind_SjLj_SetContext (struct SjLj_Function_Context *fc)
{
#if __GTHREADS
  if (use_fc_key < 0)
    fc_key_init_once ();

  if (use_fc_key)
    __gthread_setspecific (fc_key, fc);
  else
#endif
    fc_static = fc;
}

void
_Unwind_SjLj_Unregister (struct SjLj_Function_Context *fc)
{
  _Unwind_SjLj_SetContext (fc->prev);
}


/* Get/set the return data value at INDEX in CONTEXT.  */

_Unwind_Word
_Unwind_GetGR (struct _Unwind_Context *context, int index)
{
  return context->fc->data[index];
}

/* Get the value of the CFA as saved in CONTEXT.  */

_Unwind_Word
_Unwind_GetCFA (struct _Unwind_Context *context __attribute__((unused)))
{
  /* ??? Ideally __builtin_setjmp places the CFA in the jmpbuf.  */

#ifndef DONT_USE_BUILTIN_SETJMP
  /* This is a crude imitation of the CFA: the saved stack pointer.
     This is roughly the CFA of the frame before CONTEXT.  When using the
     DWARF-2 unwinder _Unwind_GetCFA returns the CFA of the frame described
     by CONTEXT instead; but for DWARF-2 the cleanups associated with
     CONTEXT have already been run, and for SJLJ they have not yet been.  */
  if (context->fc != NULL)
    return (_Unwind_Word) context->fc->jbuf[2];
#endif

  /* Otherwise we're out of luck for now.  */
  return (_Unwind_Word) 0;
}

void
_Unwind_SetGR (struct _Unwind_Context *context, int index, _Unwind_Word val)
{
  context->fc->data[index] = val;
}

/* Get the call-site index as saved in CONTEXT.  */

_Unwind_Ptr
_Unwind_GetIP (struct _Unwind_Context *context)
{
  return context->fc->call_site + 1;
}

_Unwind_Ptr
_Unwind_GetIPInfo (struct _Unwind_Context *context, int *ip_before_insn)
{
  *ip_before_insn = 0;
  return context->fc->call_site + 1;
}

/* Set the return landing pad index in CONTEXT.  */

void
_Unwind_SetIP (struct _Unwind_Context *context, _Unwind_Ptr val)
{
  context->fc->call_site = val - 1;
}

void *
_Unwind_GetLanguageSpecificData (struct _Unwind_Context *context)
{
  return context->fc->lsda;
}

_Unwind_Ptr
_Unwind_GetRegionStart (struct _Unwind_Context *context __attribute__((unused)) )
{
  return 0;
}

void *
_Unwind_FindEnclosingFunction (void *pc __attribute__((unused)))
{
  return NULL;
}

#ifndef __ia64__
_Unwind_Ptr
_Unwind_GetDataRelBase (struct _Unwind_Context *context __attribute__((unused)) )
{
  return 0;
}

_Unwind_Ptr
_Unwind_GetTextRelBase (struct _Unwind_Context *context __attribute__((unused)) )
{
  return 0;
}
#endif

static inline _Unwind_Reason_Code
uw_frame_state_for (struct _Unwind_Context *context, _Unwind_FrameState *fs)
{
  if (context->fc == NULL)
    {
      fs->personality = NULL;
      return _URC_END_OF_STACK;
    }
  else
    {
      fs->personality = context->fc->personality;
      return _URC_NO_REASON;
    }
}

static inline void
uw_update_context (struct _Unwind_Context *context,
		   _Unwind_FrameState *fs __attribute__((unused)) )
{
  context->fc = context->fc->prev;
}

static void
uw_advance_context (struct _Unwind_Context *context, _Unwind_FrameState *fs)
{
  _Unwind_SjLj_Unregister (context->fc);
  uw_update_context (context, fs);
}

static inline void
uw_init_context (struct _Unwind_Context *context)
{
  context->fc = _Unwind_SjLj_GetContext ();
}

static void __attribute__((noreturn))
uw_install_context (struct _Unwind_Context *current __attribute__((unused)),
                    struct _Unwind_Context *target)
{
  _Unwind_SjLj_SetContext (target->fc);
  longjmp (target->fc->jbuf, 1);
}

static inline _Unwind_Ptr
uw_identify_context (struct _Unwind_Context *context)
{
  return (_Unwind_Ptr) context->fc;
}


/* Play games with unwind symbols so that we can have call frame
   and sjlj symbols in the same shared library.  Not that you can
   use them simultaneously...  */
#define _Unwind_RaiseException		_Unwind_SjLj_RaiseException
#define _Unwind_ForcedUnwind		_Unwind_SjLj_ForcedUnwind
#define _Unwind_Resume			_Unwind_SjLj_Resume
#define _Unwind_Resume_or_Rethrow	_Unwind_SjLj_Resume_or_Rethrow

#include "unwind.inc"

#endif /* USING_SJLJ_EXCEPTIONS */
