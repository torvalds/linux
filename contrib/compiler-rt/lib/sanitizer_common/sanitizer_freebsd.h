//===-- sanitizer_freebsd.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Sanitizer runtime. It contains FreeBSD-specific
// definitions.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_FREEBSD_H
#define SANITIZER_FREEBSD_H

#include "sanitizer_internal_defs.h"

// x86-64 FreeBSD 9.2 and older define 'ucontext_t' incorrectly in
// 32-bit mode.
#if SANITIZER_FREEBSD && (SANITIZER_WORDSIZE == 32)
# include <osreldate.h>
# if __FreeBSD_version <= 902001  // v9.2
#  include <link.h>
#  include <sys/param.h>
#  include <ucontext.h>

namespace __sanitizer {

typedef unsigned long long __xuint64_t;

typedef __int32_t __xregister_t;

typedef struct __xmcontext {
  __xregister_t mc_onstack;
  __xregister_t mc_gs;
  __xregister_t mc_fs;
  __xregister_t mc_es;
  __xregister_t mc_ds;
  __xregister_t mc_edi;
  __xregister_t mc_esi;
  __xregister_t mc_ebp;
  __xregister_t mc_isp;
  __xregister_t mc_ebx;
  __xregister_t mc_edx;
  __xregister_t mc_ecx;
  __xregister_t mc_eax;
  __xregister_t mc_trapno;
  __xregister_t mc_err;
  __xregister_t mc_eip;
  __xregister_t mc_cs;
  __xregister_t mc_eflags;
  __xregister_t mc_esp;
  __xregister_t mc_ss;

  int mc_len;
  int mc_fpformat;
  int mc_ownedfp;
  __xregister_t mc_flags;

  int mc_fpstate[128] __aligned(16);
  __xregister_t mc_fsbase;
  __xregister_t mc_gsbase;
  __xregister_t mc_xfpustate;
  __xregister_t mc_xfpustate_len;

  int mc_spare2[4];
} xmcontext_t;

typedef struct __xucontext {
  sigset_t  uc_sigmask;
  xmcontext_t  uc_mcontext;

  struct __ucontext *uc_link;
  stack_t uc_stack;
  int uc_flags;
  int __spare__[4];
} xucontext_t;

struct xkinfo_vmentry {
  int kve_structsize;
  int kve_type;
  __xuint64_t kve_start;
  __xuint64_t kve_end;
  __xuint64_t kve_offset;
  __xuint64_t kve_vn_fileid;
  __uint32_t kve_vn_fsid;
  int kve_flags;
  int kve_resident;
  int kve_private_resident;
  int kve_protection;
  int kve_ref_count;
  int kve_shadow_count;
  int kve_vn_type;
  __xuint64_t kve_vn_size;
  __uint32_t kve_vn_rdev;
  __uint16_t kve_vn_mode;
  __uint16_t kve_status;
  int _kve_ispare[12];
  char kve_path[PATH_MAX];
};

typedef struct {
  __uint32_t p_type;
  __uint32_t p_offset;
  __uint32_t p_vaddr;
  __uint32_t p_paddr;
  __uint32_t p_filesz;
  __uint32_t p_memsz;
  __uint32_t p_flags;
  __uint32_t p_align;
} XElf32_Phdr;

struct xdl_phdr_info {
  Elf_Addr dlpi_addr;
  const char *dlpi_name;
  const XElf32_Phdr *dlpi_phdr;
  Elf_Half dlpi_phnum;
  unsigned long long int dlpi_adds;
  unsigned long long int dlpi_subs;
  size_t dlpi_tls_modid;
  void *dlpi_tls_data;
};

typedef int (*__xdl_iterate_hdr_callback)(struct xdl_phdr_info*, size_t, void*);
typedef int xdl_iterate_phdr_t(__xdl_iterate_hdr_callback, void*);

#define xdl_iterate_phdr(callback, param) \
  (((xdl_iterate_phdr_t*) dl_iterate_phdr)((callback), (param)))

}  // namespace __sanitizer

# endif  // __FreeBSD_version <= 902001
#endif  // SANITIZER_FREEBSD && (SANITIZER_WORDSIZE == 32)

#endif  // SANITIZER_FREEBSD_H
