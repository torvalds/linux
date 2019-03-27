/* Select disassembly routine for specified architecture.
   Copyright 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005, 2006 Free Software Foundation, Inc.

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
#include "dis-asm.h"

#ifdef ARCH_all
#define ARCH_alpha
#define ARCH_arc
#define ARCH_arm
#define ARCH_avr
#define ARCH_bfin
#define ARCH_cr16
#define ARCH_cris
#define ARCH_crx
#define ARCH_d10v
#define ARCH_d30v
#define ARCH_dlx
#define ARCH_fr30
#define ARCH_frv
#define ARCH_h8300
#define ARCH_h8500
#define ARCH_hppa
#define ARCH_i370
#define ARCH_i386
#define ARCH_i860
#define ARCH_i960
#define ARCH_ia64
#define ARCH_ip2k
#define ARCH_iq2000
#define ARCH_m32c
#define ARCH_m32r
#define ARCH_m68hc11
#define ARCH_m68hc12
#define ARCH_m68k
#define ARCH_m88k
#define ARCH_maxq
#define ARCH_mcore
#define ARCH_mep
#define ARCH_mips
#define ARCH_mmix
#define ARCH_mn10200
#define ARCH_mn10300
#define ARCH_mt
#define ARCH_msp430
#define ARCH_ns32k
#define ARCH_openrisc
#define ARCH_or32
#define ARCH_pdp11
#define ARCH_pj
#define ARCH_powerpc
#define ARCH_rs6000
#define ARCH_s390
#define ARCH_score
#define ARCH_sh
#define ARCH_sparc
#define ARCH_spu
#define ARCH_tic30
#define ARCH_tic4x
#define ARCH_tic54x
#define ARCH_tic80
#define ARCH_v850
#define ARCH_vax
#define ARCH_w65
#define ARCH_xstormy16
#define ARCH_xc16x
#define ARCH_xtensa
#define ARCH_z80
#define ARCH_z8k
#define INCLUDE_SHMEDIA
#endif

#ifdef ARCH_m32c
#include "m32c-desc.h"
#endif

disassembler_ftype
disassembler (abfd)
     bfd *abfd;
{
  enum bfd_architecture a = bfd_get_arch (abfd);
  disassembler_ftype disassemble;

  switch (a)
    {
      /* If you add a case to this table, also add it to the
	 ARCH_all definition right above this function.  */
#ifdef ARCH_alpha
    case bfd_arch_alpha:
      disassemble = print_insn_alpha;
      break;
#endif
#ifdef ARCH_arc
    case bfd_arch_arc:
      {
	disassemble = arc_get_disassembler (abfd);
	break;
      }
#endif
#ifdef ARCH_arm
    case bfd_arch_arm:
      if (bfd_big_endian (abfd))
	disassemble = print_insn_big_arm;
      else
	disassemble = print_insn_little_arm;
      break;
#endif
#ifdef ARCH_avr
    case bfd_arch_avr:
      disassemble = print_insn_avr;
      break;
#endif
#ifdef ARCH_bfin
    case bfd_arch_bfin:
      disassemble = print_insn_bfin;
      break;
#endif
#ifdef ARCH_cr16
    case bfd_arch_cr16:
      disassemble = print_insn_cr16;
      break;
#endif
#ifdef ARCH_cris
    case bfd_arch_cris:
      disassemble = cris_get_disassembler (abfd);
      break;
#endif
#ifdef ARCH_crx
    case bfd_arch_crx:
      disassemble = print_insn_crx;
      break;
#endif
#ifdef ARCH_d10v
    case bfd_arch_d10v:
      disassemble = print_insn_d10v;
      break;
#endif
#ifdef ARCH_d30v
    case bfd_arch_d30v:
      disassemble = print_insn_d30v;
      break;
#endif
#ifdef ARCH_dlx
    case bfd_arch_dlx:
      /* As far as I know we only handle big-endian DLX objects.  */
      disassemble = print_insn_dlx;
      break;
#endif
#ifdef ARCH_h8300
    case bfd_arch_h8300:
      if (bfd_get_mach (abfd) == bfd_mach_h8300h
	  || bfd_get_mach (abfd) == bfd_mach_h8300hn)
	disassemble = print_insn_h8300h;
      else if (bfd_get_mach (abfd) == bfd_mach_h8300s
	       || bfd_get_mach (abfd) == bfd_mach_h8300sn
	       || bfd_get_mach (abfd) == bfd_mach_h8300sx
	       || bfd_get_mach (abfd) == bfd_mach_h8300sxn)
	disassemble = print_insn_h8300s;
      else
	disassemble = print_insn_h8300;
      break;
#endif
#ifdef ARCH_h8500
    case bfd_arch_h8500:
      disassemble = print_insn_h8500;
      break;
#endif
#ifdef ARCH_hppa
    case bfd_arch_hppa:
      disassemble = print_insn_hppa;
      break;
#endif
#ifdef ARCH_i370
    case bfd_arch_i370:
      disassemble = print_insn_i370;
      break;
#endif
#ifdef ARCH_i386
    case bfd_arch_i386:
      disassemble = print_insn_i386;
      break;
#endif
#ifdef ARCH_i860
    case bfd_arch_i860:
      disassemble = print_insn_i860;
      break;
#endif
#ifdef ARCH_i960
    case bfd_arch_i960:
      disassemble = print_insn_i960;
      break;
#endif
#ifdef ARCH_ia64
    case bfd_arch_ia64:
      disassemble = print_insn_ia64;
      break;
#endif
#ifdef ARCH_ip2k
    case bfd_arch_ip2k:
      disassemble = print_insn_ip2k;
      break;
#endif
#ifdef ARCH_fr30
    case bfd_arch_fr30:
      disassemble = print_insn_fr30;
      break;
#endif
#ifdef ARCH_m32r
    case bfd_arch_m32r:
      disassemble = print_insn_m32r;
      break;
#endif
#if defined(ARCH_m68hc11) || defined(ARCH_m68hc12)
    case bfd_arch_m68hc11:
      disassemble = print_insn_m68hc11;
      break;
    case bfd_arch_m68hc12:
      disassemble = print_insn_m68hc12;
      break;
#endif
#ifdef ARCH_m68k
    case bfd_arch_m68k:
      disassemble = print_insn_m68k;
      break;
#endif
#ifdef ARCH_m88k
    case bfd_arch_m88k:
      disassemble = print_insn_m88k;
      break;
#endif
#ifdef ARCH_maxq
    case bfd_arch_maxq:
      disassemble = print_insn_maxq_little;
      break;
#endif
#ifdef ARCH_mt
    case bfd_arch_mt:
      disassemble = print_insn_mt;
      break;
#endif
#ifdef ARCH_msp430
    case bfd_arch_msp430:
      disassemble = print_insn_msp430;
      break;
#endif
#ifdef ARCH_ns32k
    case bfd_arch_ns32k:
      disassemble = print_insn_ns32k;
      break;
#endif
#ifdef ARCH_mcore
    case bfd_arch_mcore:
      disassemble = print_insn_mcore;
      break;
#endif
#ifdef ARCH_mep
    case bfd_arch_mep:
      disassemble = print_insn_mep;
      break;
#endif
#ifdef ARCH_mips
    case bfd_arch_mips:
      if (bfd_big_endian (abfd))
	disassemble = print_insn_big_mips;
      else
	disassemble = print_insn_little_mips;
      break;
#endif
#ifdef ARCH_mmix
    case bfd_arch_mmix:
      disassemble = print_insn_mmix;
      break;
#endif
#ifdef ARCH_mn10200
    case bfd_arch_mn10200:
      disassemble = print_insn_mn10200;
      break;
#endif
#ifdef ARCH_mn10300
    case bfd_arch_mn10300:
      disassemble = print_insn_mn10300;
      break;
#endif
#ifdef ARCH_openrisc
    case bfd_arch_openrisc:
      disassemble = print_insn_openrisc;
      break;
#endif
#ifdef ARCH_or32
    case bfd_arch_or32:
      if (bfd_big_endian (abfd))
        disassemble = print_insn_big_or32;
      else
        disassemble = print_insn_little_or32;
      break;
#endif
#ifdef ARCH_pdp11
    case bfd_arch_pdp11:
      disassemble = print_insn_pdp11;
      break;
#endif
#ifdef ARCH_pj
    case bfd_arch_pj:
      disassemble = print_insn_pj;
      break;
#endif
#ifdef ARCH_powerpc
    case bfd_arch_powerpc:
      if (bfd_big_endian (abfd))
	disassemble = print_insn_big_powerpc;
      else
	disassemble = print_insn_little_powerpc;
      break;
#endif
#ifdef ARCH_rs6000
    case bfd_arch_rs6000:
      if (bfd_get_mach (abfd) == bfd_mach_ppc_620)
	disassemble = print_insn_big_powerpc;
      else
	disassemble = print_insn_rs6000;
      break;
#endif
#ifdef ARCH_s390
    case bfd_arch_s390:
      disassemble = print_insn_s390;
      break;
#endif
#ifdef ARCH_score
    case bfd_arch_score:
      if (bfd_big_endian (abfd))
        disassemble = print_insn_big_score;      
      else
        disassemble = print_insn_little_score; 
     break;
#endif
#ifdef ARCH_sh
    case bfd_arch_sh:
      disassemble = print_insn_sh;
      break;
#endif
#ifdef ARCH_sparc
    case bfd_arch_sparc:
      disassemble = print_insn_sparc;
      break;
#endif
#ifdef ARCH_spu
    case bfd_arch_spu:
      disassemble = print_insn_spu;
      break;
#endif
#ifdef ARCH_tic30
    case bfd_arch_tic30:
      disassemble = print_insn_tic30;
      break;
#endif
#ifdef ARCH_tic4x
    case bfd_arch_tic4x:
      disassemble = print_insn_tic4x;
      break;
#endif
#ifdef ARCH_tic54x
    case bfd_arch_tic54x:
      disassemble = print_insn_tic54x;
      break;
#endif
#ifdef ARCH_tic80
    case bfd_arch_tic80:
      disassemble = print_insn_tic80;
      break;
#endif
#ifdef ARCH_v850
    case bfd_arch_v850:
      disassemble = print_insn_v850;
      break;
#endif
#ifdef ARCH_w65
    case bfd_arch_w65:
      disassemble = print_insn_w65;
      break;
#endif
#ifdef ARCH_xstormy16
    case bfd_arch_xstormy16:
      disassemble = print_insn_xstormy16;
      break;
#endif
#ifdef ARCH_xc16x
    case bfd_arch_xc16x:
      disassemble = print_insn_xc16x;
      break;
#endif
#ifdef ARCH_xtensa
    case bfd_arch_xtensa:
      disassemble = print_insn_xtensa;
      break;
#endif
#ifdef ARCH_z80
    case bfd_arch_z80:
      disassemble = print_insn_z80;
      break;
#endif
#ifdef ARCH_z8k
    case bfd_arch_z8k:
      if (bfd_get_mach(abfd) == bfd_mach_z8001)
	disassemble = print_insn_z8001;
      else
	disassemble = print_insn_z8002;
      break;
#endif
#ifdef ARCH_vax
    case bfd_arch_vax:
      disassemble = print_insn_vax;
      break;
#endif
#ifdef ARCH_frv
    case bfd_arch_frv:
      disassemble = print_insn_frv;
      break;
#endif
#ifdef ARCH_iq2000
    case bfd_arch_iq2000:
      disassemble = print_insn_iq2000;
      break;
#endif
#ifdef ARCH_m32c
    case bfd_arch_m32c:
      disassemble = print_insn_m32c;
      break;
#endif
    default:
      return 0;
    }
  return disassemble;
}

void
disassembler_usage (stream)
     FILE * stream ATTRIBUTE_UNUSED;
{
#ifdef ARCH_arm
  print_arm_disassembler_options (stream);
#endif
#ifdef ARCH_mips
  print_mips_disassembler_options (stream);
#endif
#ifdef ARCH_powerpc
  print_ppc_disassembler_options (stream);
#endif
#ifdef ARCH_i386
  print_i386_disassembler_options (stream);
#endif

  return;
}

void
disassemble_init_for_target (struct disassemble_info * info)
{
  if (info == NULL)
    return;

  switch (info->arch)
    {
#ifdef ARCH_arm
    case bfd_arch_arm:
      info->symbol_is_valid = arm_symbol_is_valid;
      info->disassembler_needs_relocs = TRUE;
      break;
#endif
#ifdef ARCH_ia64
    case bfd_arch_ia64:
      info->skip_zeroes = 16;
      break;
#endif
#ifdef ARCH_tic4x
    case bfd_arch_tic4x:
      info->skip_zeroes = 32;
      break;
#endif
#ifdef ARCH_mep
    case bfd_arch_mep:
      info->skip_zeroes = 256;
      info->skip_zeroes_at_end = 0;
      break;
#endif
#ifdef ARCH_m32c
    case bfd_arch_m32c:
      info->endian = BFD_ENDIAN_BIG;
      if (! info->insn_sets)
	{
	  info->insn_sets = cgen_bitset_create (ISA_MAX);
	  if (info->mach == bfd_mach_m16c)
	    cgen_bitset_set (info->insn_sets, ISA_M16C);
	  else
	    cgen_bitset_set (info->insn_sets, ISA_M32C);
	}
      break;
#endif
    default:
      break;
    }
}
