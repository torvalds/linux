/* <proc_service.h> replacement for systems that don't have it.
   Copyright 2000 Free Software Foundation, Inc.

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

#ifndef GDB_PROC_SERVICE_H
#define GDB_PROC_SERVICE_H

#include <sys/types.h>

#ifdef HAVE_PROC_SERVICE_H
#include <proc_service.h>
#else

#ifdef HAVE_SYS_PROCFS_H
#include <sys/procfs.h>
#endif

#include "gregset.h"

typedef enum
{
  PS_OK,			/* Success.  */
  PS_ERR,			/* Generic error.  */
  PS_BADPID,			/* Bad process handle.  */
  PS_BADLID,			/* Bad LWP id.  */
  PS_BADADDR,			/* Bad address.  */
  PS_NOSYM,			/* Symbol not found.  */
  PS_NOFREGS			/* FPU register set not available.  */
} ps_err_e;

#ifndef HAVE_LWPID_T
typedef unsigned int lwpid_t;
#endif

typedef unsigned long paddr_t;

#ifndef HAVE_PSADDR_T
typedef unsigned long psaddr_t;
#endif

#ifndef HAVE_PRGREGSET_T
typedef gdb_gregset_t prgregset_t;
#endif

#ifndef HAVE_PRFPREGSET_T
typedef gdb_fpregset_t prfpregset_t;
#endif

#endif /* HAVE_PROC_SERVICE_H */

/* Fix-up some broken systems.  */

/* Unfortunately glibc 2.1.3 was released with a broken prfpregset_t
   type.  We let configure check for this lossage, and make
   appropriate typedefs here.  */

#ifdef PRFPREGSET_T_BROKEN
typedef gdb_fpregset_t gdb_prfpregset_t;
#else
typedef prfpregset_t gdb_prfpregset_t;
#endif

/* Structure that identifies the target process.  */
struct ps_prochandle
{
  /* The process id is all we need.  */
  pid_t pid;
};

#endif /* gdb_proc_service.h */
