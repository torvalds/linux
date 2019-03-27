/* nto-tdep.c - general QNX Neutrino target functionality.

   Copyright 2003 Free Software Foundation, Inc.

   Contributed by QNX Software Systems Ltd.

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

#include "gdb_stat.h"
#include "gdb_string.h"
#include "nto-tdep.h"
#include "top.h"
#include "cli/cli-decode.h"
#include "cli/cli-cmds.h"
#include "inferior.h"
#include "gdbarch.h"
#include "bfd.h"
#include "elf-bfd.h"
#include "solib-svr4.h"
#include "gdbcore.h"

#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif

#ifdef __CYGWIN__
static char default_nto_target[] = "C:\\QNXsdk\\target\\qnx6";
#elif defined(__sun__) || defined(linux)
static char default_nto_target[] = "/opt/QNXsdk/target/qnx6";
#else
static char default_nto_target[] = "";
#endif

struct nto_target_ops current_nto_target;

static char *
nto_target (void)
{
  char *p = getenv ("QNX_TARGET");

#ifdef __CYGWIN__
  static char buf[PATH_MAX];
  if (p)
    cygwin_conv_to_posix_path (p, buf);
  else
    cygwin_conv_to_posix_path (default_nto_target, buf);
  return buf;
#else
  return p ? p : default_nto_target;
#endif
}

/* Take a string such as i386, rs6000, etc. and map it onto CPUTYPE_X86,
   CPUTYPE_PPC, etc. as defined in nto-share/dsmsgs.h.  */
int
nto_map_arch_to_cputype (const char *arch)
{
  if (!strcmp (arch, "i386") || !strcmp (arch, "x86"))
    return CPUTYPE_X86;
  if (!strcmp (arch, "rs6000") || !strcmp (arch, "powerpc"))
    return CPUTYPE_PPC;
  if (!strcmp (arch, "mips"))
    return CPUTYPE_MIPS;
  if (!strcmp (arch, "arm"))
    return CPUTYPE_ARM;
  if (!strcmp (arch, "sh"))
    return CPUTYPE_SH;
  return CPUTYPE_UNKNOWN;
}

int
nto_find_and_open_solib (char *solib, unsigned o_flags, char **temp_pathname)
{
  char *buf, arch_path[PATH_MAX], *nto_root, *endian;
  const char *arch;
  char *path_fmt = "%s/lib:%s/usr/lib:%s/usr/photon/lib\
:%s/usr/photon/dll:%s/lib/dll";

  nto_root = nto_target ();
  if (strcmp (TARGET_ARCHITECTURE->arch_name, "i386") == 0)
    {
      arch = "x86";
      endian = "";
    }
  else if (strcmp (TARGET_ARCHITECTURE->arch_name, "rs6000") == 0
	   || strcmp (TARGET_ARCHITECTURE->arch_name, "powerpc") == 0)
    {
      arch = "ppc";
      endian = "be";
    }
  else
    {
      arch = TARGET_ARCHITECTURE->arch_name;
      endian = TARGET_BYTE_ORDER == BFD_ENDIAN_BIG ? "be" : "le";
    }

  sprintf (arch_path, "%s/%s%s", nto_root, arch, endian);

  buf = alloca (strlen (path_fmt) + strlen (arch_path) * 5 + 1);
  sprintf (buf, path_fmt, arch_path, arch_path, arch_path, arch_path,
	   arch_path);

  return openp (buf, 1, solib, o_flags, 0, temp_pathname);
}

void
nto_init_solib_absolute_prefix (void)
{
  char buf[PATH_MAX * 2], arch_path[PATH_MAX];
  char *nto_root, *endian;
  const char *arch;

  nto_root = nto_target ();
  if (strcmp (TARGET_ARCHITECTURE->arch_name, "i386") == 0)
    {
      arch = "x86";
      endian = "";
    }
  else if (strcmp (TARGET_ARCHITECTURE->arch_name, "rs6000") == 0
	   || strcmp (TARGET_ARCHITECTURE->arch_name, "powerpc") == 0)
    {
      arch = "ppc";
      endian = "be";
    }
  else
    {
      arch = TARGET_ARCHITECTURE->arch_name;
      endian = TARGET_BYTE_ORDER == BFD_ENDIAN_BIG ? "be" : "le";
    }

  sprintf (arch_path, "%s/%s%s", nto_root, arch, endian);

  sprintf (buf, "set solib-absolute-prefix %s", arch_path);
  execute_command (buf, 0);
}

char **
nto_parse_redirection (char *pargv[], char **pin, char **pout, char **perr)
{
  char **argv;
  char *in, *out, *err, *p;
  int argc, i, n;

  for (n = 0; pargv[n]; n++);
  if (n == 0)
    return NULL;
  in = "";
  out = "";
  err = "";

  argv = xcalloc (n + 1, sizeof argv[0]);
  argc = n;
  for (i = 0, n = 0; n < argc; n++)
    {
      p = pargv[n];
      if (*p == '>')
	{
	  p++;
	  if (*p)
	    out = p;
	  else
	    out = pargv[++n];
	}
      else if (*p == '<')
	{
	  p++;
	  if (*p)
	    in = p;
	  else
	    in = pargv[++n];
	}
      else if (*p++ == '2' && *p++ == '>')
	{
	  if (*p == '&' && *(p + 1) == '1')
	    err = out;
	  else if (*p)
	    err = p;
	  else
	    err = pargv[++n];
	}
      else
	argv[i++] = pargv[n];
    }
  *pin = in;
  *pout = out;
  *perr = err;
  return argv;
}

/* The struct lm_info, LM_ADDR, and nto_truncate_ptr are copied from
   solib-svr4.c to support nto_relocate_section_addresses
   which is different from the svr4 version.  */

struct lm_info
{
  /* Pointer to copy of link map from inferior.  The type is char *
     rather than void *, so that we may use byte offsets to find the
     various fields without the need for a cast.  */
  char *lm;
};

static CORE_ADDR
LM_ADDR (struct so_list *so)
{
  struct link_map_offsets *lmo = nto_fetch_link_map_offsets ();

  return (CORE_ADDR) extract_signed_integer (so->lm_info->lm +
					     lmo->l_addr_offset,
					     lmo->l_addr_size);
}

static CORE_ADDR
nto_truncate_ptr (CORE_ADDR addr)
{
  if (TARGET_PTR_BIT == sizeof (CORE_ADDR) * 8)
    /* We don't need to truncate anything, and the bit twiddling below
       will fail due to overflow problems.  */
    return addr;
  else
    return addr & (((CORE_ADDR) 1 << TARGET_PTR_BIT) - 1);
}

Elf_Internal_Phdr *
find_load_phdr (bfd *abfd)
{
  Elf_Internal_Phdr *phdr;
  unsigned int i;

  if (!elf_tdata (abfd))
    return NULL;

  phdr = elf_tdata (abfd)->phdr;
  for (i = 0; i < elf_elfheader (abfd)->e_phnum; i++, phdr++)
    {
      if (phdr->p_type == PT_LOAD && (phdr->p_flags & PF_X))
	return phdr;
    }
  return NULL;
}

void
nto_relocate_section_addresses (struct so_list *so, struct section_table *sec)
{
  /* Neutrino treats the l_addr base address field in link.h as different than
     the base address in the System V ABI and so the offset needs to be
     calculated and applied to relocations.  */
  Elf_Internal_Phdr *phdr = find_load_phdr (sec->bfd);
  unsigned vaddr = phdr ? phdr->p_vaddr : 0;

  sec->addr = nto_truncate_ptr (sec->addr + LM_ADDR (so) - vaddr);
  sec->endaddr = nto_truncate_ptr (sec->endaddr + LM_ADDR (so) - vaddr);
}

static void
fetch_core_registers (char *core_reg_sect, unsigned core_reg_size,
		      int which, CORE_ADDR reg_addr)
{
  nto_regset_t regset;

/* See corelow.c:get_core_registers for values of WHICH.  */
  if (which == 0)
    {
      memcpy ((char *) &regset, core_reg_sect,
	      min (core_reg_size, sizeof (regset)));
      nto_supply_gregset ((char *) &regset);
    }
  else if (which == 2)
    {
      memcpy ((char *) &regset, core_reg_sect,
	      min (core_reg_size, sizeof (regset)));
      nto_supply_fpregset ((char *) &regset);
    }
}

void
nto_dummy_supply_regset (char *regs)
{
  /* Do nothing.  */
}

/* Register that we are able to handle ELF file formats using standard
   procfs "regset" structures.  */
static struct core_fns regset_core_fns = {
  bfd_target_elf_flavour,	/* core_flavour */
  default_check_format,		/* check_format */
  default_core_sniffer,		/* core_sniffer */
  fetch_core_registers,		/* core_read_registers */
  NULL				/* next */
};

void
_initialize_nto_tdep (void)
{
  add_setshow_cmd ("nto-debug", class_maintenance, var_zinteger,
		   &nto_internal_debugging, "Set QNX NTO internal debugging.\n\
When non-zero, nto specific debug info is\n\
displayed. Different information is displayed\n\
for different positive values.", "Show QNX NTO internal debugging.\n",
		   NULL, NULL, &setdebuglist, &showdebuglist);

  /* We use SIG45 for pulses, or something, so nostop, noprint
     and pass them.  */
  signal_stop_update (target_signal_from_name ("SIG45"), 0);
  signal_print_update (target_signal_from_name ("SIG45"), 0);
  signal_pass_update (target_signal_from_name ("SIG45"), 1);

  /* By default we don't want to stop on these two, but we do want to pass.  */
#if defined(SIGSELECT)
  signal_stop_update (SIGSELECT, 0);
  signal_print_update (SIGSELECT, 0);
  signal_pass_update (SIGSELECT, 1);
#endif

#if defined(SIGPHOTON)
  signal_stop_update (SIGPHOTON, 0);
  signal_print_update (SIGPHOTON, 0);
  signal_pass_update (SIGPHOTON, 1);
#endif

  /* Register core file support.  */
  add_core_fns (&regset_core_fns);
}
