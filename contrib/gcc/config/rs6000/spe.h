/* PowerPC E500 user include file.
   Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Aldy Hernandez (aldyh@redhat.com).

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* As a special exception, if you include this header file into source
   files compiled by GCC, this header file does not by itself cause
   the resulting executable to be covered by the GNU General Public
   License.  This exception does not however invalidate any other
   reasons why the executable file might be covered by the GNU General
   Public License.  */

#ifndef _SPE_H
#define _SPE_H

#define __vector __attribute__((vector_size(8)))

typedef int 	 		int32_t;
typedef unsigned 		uint32_t;
typedef short    		int16_t;
typedef unsigned short  	uint16_t;
typedef long long 		int64_t;
typedef unsigned long long	uint64_t;

typedef short 			__vector __ev64_s16__;
typedef unsigned short  	__vector __ev64_u16__;
typedef int 			__vector __ev64_s32__;
typedef unsigned 		__vector __ev64_u32__;
typedef long long 		__vector __ev64_s64__;
typedef unsigned long long 	__vector __ev64_u64__;
typedef float 			__vector __ev64_fs__;

#define __v2si __ev64_opaque__
#define __v2sf __ev64_fs__

#define __ev_addw __builtin_spe_evaddw
#define __ev_addiw __builtin_spe_evaddiw
#define __ev_subfw(a,b) __builtin_spe_evsubfw ((b), (a))
#define __ev_subw __builtin_spe_evsubfw
#define __ev_subifw(a,b) __builtin_spe_evsubifw ((b), (a))
#define __ev_subiw __builtin_spe_evsubifw
#define __ev_abs __builtin_spe_evabs
#define __ev_neg __builtin_spe_evneg
#define __ev_extsb __builtin_spe_evextsb
#define __ev_extsh __builtin_spe_evextsh
#define __ev_and __builtin_spe_evand
#define __ev_or __builtin_spe_evor
#define __ev_xor __builtin_spe_evxor
#define __ev_nand __builtin_spe_evnand
#define __ev_nor __builtin_spe_evnor
#define __ev_eqv __builtin_spe_eveqv
#define __ev_andc __builtin_spe_evandc
#define __ev_orc __builtin_spe_evorc
#define __ev_rlw __builtin_spe_evrlw
#define __ev_rlwi __builtin_spe_evrlwi
#define __ev_slw __builtin_spe_evslw
#define __ev_slwi __builtin_spe_evslwi
#define __ev_srws __builtin_spe_evsrws
#define __ev_srwu __builtin_spe_evsrwu
#define __ev_srwis __builtin_spe_evsrwis
#define __ev_srwiu __builtin_spe_evsrwiu
#define __ev_cntlzw __builtin_spe_evcntlzw
#define __ev_cntlsw __builtin_spe_evcntlsw
#define __ev_rndw __builtin_spe_evrndw
#define __ev_mergehi __builtin_spe_evmergehi
#define __ev_mergelo __builtin_spe_evmergelo
#define __ev_mergelohi __builtin_spe_evmergelohi
#define __ev_mergehilo __builtin_spe_evmergehilo
#define __ev_splati __builtin_spe_evsplati
#define __ev_splatfi __builtin_spe_evsplatfi
#define __ev_divws __builtin_spe_evdivws
#define __ev_divwu __builtin_spe_evdivwu
#define __ev_mra __builtin_spe_evmra

#define __brinc __builtin_spe_brinc

/* Loads.  */

#define __ev_lddx __builtin_spe_evlddx
#define __ev_ldwx __builtin_spe_evldwx
#define __ev_ldhx __builtin_spe_evldhx
#define __ev_lwhex __builtin_spe_evlwhex
#define __ev_lwhoux __builtin_spe_evlwhoux
#define __ev_lwhosx __builtin_spe_evlwhosx
#define __ev_lwwsplatx __builtin_spe_evlwwsplatx
#define __ev_lwhsplatx __builtin_spe_evlwhsplatx
#define __ev_lhhesplatx __builtin_spe_evlhhesplatx
#define __ev_lhhousplatx __builtin_spe_evlhhousplatx
#define __ev_lhhossplatx __builtin_spe_evlhhossplatx
#define __ev_ldd __builtin_spe_evldd
#define __ev_ldw __builtin_spe_evldw
#define __ev_ldh __builtin_spe_evldh
#define __ev_lwhe __builtin_spe_evlwhe
#define __ev_lwhou __builtin_spe_evlwhou
#define __ev_lwhos __builtin_spe_evlwhos
#define __ev_lwwsplat __builtin_spe_evlwwsplat
#define __ev_lwhsplat __builtin_spe_evlwhsplat
#define __ev_lhhesplat __builtin_spe_evlhhesplat
#define __ev_lhhousplat __builtin_spe_evlhhousplat
#define __ev_lhhossplat __builtin_spe_evlhhossplat

/* Stores.  */

#define __ev_stddx __builtin_spe_evstddx
#define __ev_stdwx __builtin_spe_evstdwx
#define __ev_stdhx __builtin_spe_evstdhx
#define __ev_stwwex __builtin_spe_evstwwex
#define __ev_stwwox __builtin_spe_evstwwox
#define __ev_stwhex __builtin_spe_evstwhex
#define __ev_stwhox __builtin_spe_evstwhox
#define __ev_stdd __builtin_spe_evstdd
#define __ev_stdw __builtin_spe_evstdw
#define __ev_stdh __builtin_spe_evstdh
#define __ev_stwwe __builtin_spe_evstwwe
#define __ev_stwwo __builtin_spe_evstwwo
#define __ev_stwhe __builtin_spe_evstwhe
#define __ev_stwho __builtin_spe_evstwho

/* Fixed point complex.  */

#define __ev_mhossf __builtin_spe_evmhossf
#define __ev_mhosmf __builtin_spe_evmhosmf
#define __ev_mhosmi __builtin_spe_evmhosmi
#define __ev_mhoumi __builtin_spe_evmhoumi
#define __ev_mhessf __builtin_spe_evmhessf
#define __ev_mhesmf __builtin_spe_evmhesmf
#define __ev_mhesmi __builtin_spe_evmhesmi
#define __ev_mheumi __builtin_spe_evmheumi
#define __ev_mhossfa __builtin_spe_evmhossfa
#define __ev_mhosmfa __builtin_spe_evmhosmfa
#define __ev_mhosmia __builtin_spe_evmhosmia
#define __ev_mhoumia __builtin_spe_evmhoumia
#define __ev_mhessfa __builtin_spe_evmhessfa
#define __ev_mhesmfa __builtin_spe_evmhesmfa
#define __ev_mhesmia __builtin_spe_evmhesmia
#define __ev_mheumia __builtin_spe_evmheumia

#define __ev_mhoumf __ev_mhoumi
#define __ev_mheumf __ev_mheumi
#define __ev_mhoumfa __ev_mhoumia
#define __ev_mheumfa __ev_mheumia

#define __ev_mhossfaaw __builtin_spe_evmhossfaaw
#define __ev_mhossiaaw __builtin_spe_evmhossiaaw
#define __ev_mhosmfaaw __builtin_spe_evmhosmfaaw
#define __ev_mhosmiaaw __builtin_spe_evmhosmiaaw
#define __ev_mhousiaaw __builtin_spe_evmhousiaaw
#define __ev_mhoumiaaw __builtin_spe_evmhoumiaaw
#define __ev_mhessfaaw __builtin_spe_evmhessfaaw
#define __ev_mhessiaaw __builtin_spe_evmhessiaaw
#define __ev_mhesmfaaw __builtin_spe_evmhesmfaaw
#define __ev_mhesmiaaw __builtin_spe_evmhesmiaaw
#define __ev_mheusiaaw __builtin_spe_evmheusiaaw
#define __ev_mheumiaaw __builtin_spe_evmheumiaaw

#define __ev_mhousfaaw __ev_mhousiaaw
#define __ev_mhoumfaaw __ev_mhoumiaaw
#define __ev_mheusfaaw __ev_mheusiaaw
#define __ev_mheumfaaw __ev_mheumiaaw

#define __ev_mhossfanw __builtin_spe_evmhossfanw
#define __ev_mhossianw __builtin_spe_evmhossianw
#define __ev_mhosmfanw __builtin_spe_evmhosmfanw
#define __ev_mhosmianw __builtin_spe_evmhosmianw
#define __ev_mhousianw __builtin_spe_evmhousianw
#define __ev_mhoumianw __builtin_spe_evmhoumianw
#define __ev_mhessfanw __builtin_spe_evmhessfanw
#define __ev_mhessianw __builtin_spe_evmhessianw
#define __ev_mhesmfanw __builtin_spe_evmhesmfanw
#define __ev_mhesmianw __builtin_spe_evmhesmianw
#define __ev_mheusianw __builtin_spe_evmheusianw
#define __ev_mheumianw __builtin_spe_evmheumianw

#define __ev_mhousfanw __ev_mhousianw
#define __ev_mhoumfanw __ev_mhoumianw
#define __ev_mheusfanw __ev_mheusianw
#define __ev_mheumfanw __ev_mheumianw

#define __ev_mhogsmfaa __builtin_spe_evmhogsmfaa
#define __ev_mhogsmiaa __builtin_spe_evmhogsmiaa
#define __ev_mhogumiaa __builtin_spe_evmhogumiaa
#define __ev_mhegsmfaa __builtin_spe_evmhegsmfaa
#define __ev_mhegsmiaa __builtin_spe_evmhegsmiaa
#define __ev_mhegumiaa __builtin_spe_evmhegumiaa

#define __ev_mhogumfaa __ev_mhogumiaa
#define __ev_mhegumfaa __ev_mhegumiaa

#define __ev_mhogsmfan __builtin_spe_evmhogsmfan
#define __ev_mhogsmian __builtin_spe_evmhogsmian
#define __ev_mhogumian __builtin_spe_evmhogumian
#define __ev_mhegsmfan __builtin_spe_evmhegsmfan
#define __ev_mhegsmian __builtin_spe_evmhegsmian
#define __ev_mhegumian __builtin_spe_evmhegumian

#define __ev_mhogumfan __ev_mhogumian
#define __ev_mhegumfan __ev_mhegumian

#define __ev_mwhssf __builtin_spe_evmwhssf
#define __ev_mwhsmf __builtin_spe_evmwhsmf
#define __ev_mwhsmi __builtin_spe_evmwhsmi
#define __ev_mwhumi __builtin_spe_evmwhumi
#define __ev_mwhssfa __builtin_spe_evmwhssfa
#define __ev_mwhsmfa __builtin_spe_evmwhsmfa
#define __ev_mwhsmia __builtin_spe_evmwhsmia
#define __ev_mwhumia __builtin_spe_evmwhumia

#define __ev_mwhumf __ev_mwhumi
#define __ev_mwhumfa __ev_mwhumia

#define __ev_mwlumi __builtin_spe_evmwlumi
#define __ev_mwlumia __builtin_spe_evmwlumia
#define __ev_mwlumiaaw __builtin_spe_evmwlumiaaw

#define __ev_mwlssiaaw __builtin_spe_evmwlssiaaw
#define __ev_mwlsmiaaw __builtin_spe_evmwlsmiaaw
#define __ev_mwlusiaaw __builtin_spe_evmwlusiaaw
#define __ev_mwlusiaaw __builtin_spe_evmwlusiaaw

#define __ev_mwlssianw __builtin_spe_evmwlssianw
#define __ev_mwlsmianw __builtin_spe_evmwlsmianw
#define __ev_mwlusianw __builtin_spe_evmwlusianw
#define __ev_mwlumianw __builtin_spe_evmwlumianw

#define __ev_mwssf __builtin_spe_evmwssf
#define __ev_mwsmf __builtin_spe_evmwsmf
#define __ev_mwsmi __builtin_spe_evmwsmi
#define __ev_mwumi __builtin_spe_evmwumi
#define __ev_mwssfa __builtin_spe_evmwssfa
#define __ev_mwsmfa __builtin_spe_evmwsmfa
#define __ev_mwsmia __builtin_spe_evmwsmia
#define __ev_mwumia __builtin_spe_evmwumia

#define __ev_mwumf __ev_mwumi
#define __ev_mwumfa __ev_mwumia

#define __ev_mwssfaa __builtin_spe_evmwssfaa
#define __ev_mwsmfaa __builtin_spe_evmwsmfaa
#define __ev_mwsmiaa __builtin_spe_evmwsmiaa
#define __ev_mwumiaa __builtin_spe_evmwumiaa

#define __ev_mwumfaa __ev_mwumiaa

#define __ev_mwssfan __builtin_spe_evmwssfan
#define __ev_mwsmfan __builtin_spe_evmwsmfan
#define __ev_mwsmian __builtin_spe_evmwsmian
#define __ev_mwumian __builtin_spe_evmwumian

#define __ev_mwumfan __ev_mwumian

#define __ev_addssiaaw __builtin_spe_evaddssiaaw
#define __ev_addsmiaaw __builtin_spe_evaddsmiaaw
#define __ev_addusiaaw __builtin_spe_evaddusiaaw
#define __ev_addumiaaw __builtin_spe_evaddumiaaw

#define __ev_addusfaaw __ev_addusiaaw
#define __ev_addumfaaw __ev_addumiaaw
#define __ev_addsmfaaw __ev_addsmiaaw
#define __ev_addssfaaw __ev_addssiaaw

#define __ev_subfssiaaw __builtin_spe_evsubfssiaaw
#define __ev_subfsmiaaw __builtin_spe_evsubfsmiaaw
#define __ev_subfusiaaw __builtin_spe_evsubfusiaaw
#define __ev_subfumiaaw __builtin_spe_evsubfumiaaw

#define __ev_subfusfaaw __ev_subfusiaaw
#define __ev_subfumfaaw __ev_subfumiaaw
#define __ev_subfsmfaaw __ev_subfsmiaaw
#define __ev_subfssfaaw __ev_subfssiaaw

/* Floating Point SIMD Instructions  */

#define __ev_fsabs __builtin_spe_evfsabs
#define __ev_fsnabs __builtin_spe_evfsnabs
#define __ev_fsneg __builtin_spe_evfsneg
#define __ev_fsadd __builtin_spe_evfsadd
#define __ev_fssub __builtin_spe_evfssub
#define __ev_fsmul __builtin_spe_evfsmul
#define __ev_fsdiv __builtin_spe_evfsdiv
#define __ev_fscfui __builtin_spe_evfscfui
#define __ev_fscfsi __builtin_spe_evfscfsi
#define __ev_fscfuf __builtin_spe_evfscfuf
#define __ev_fscfsf __builtin_spe_evfscfsf
#define __ev_fsctui __builtin_spe_evfsctui
#define __ev_fsctsi __builtin_spe_evfsctsi
#define __ev_fsctuf __builtin_spe_evfsctuf
#define __ev_fsctsf __builtin_spe_evfsctsf
#define __ev_fsctuiz __builtin_spe_evfsctuiz
#define __ev_fsctsiz __builtin_spe_evfsctsiz

/* NOT SUPPORTED IN FIRST e500, support via two instructions:  */

#define __ev_mwhusfaaw  __ev_mwhusiaaw
#define __ev_mwhumfaaw  __ev_mwhumiaaw
#define __ev_mwhusfanw  __ev_mwhusianw
#define __ev_mwhumfanw  __ev_mwhumianw
#define __ev_mwhgumfaa  __ev_mwhgumiaa
#define __ev_mwhgumfan  __ev_mwhgumian

#define __ev_mwhgssfaa __internal_ev_mwhgssfaa
#define __ev_mwhgsmfaa __internal_ev_mwhgsmfaa
#define __ev_mwhgsmiaa __internal_ev_mwhgsmiaa
#define __ev_mwhgumiaa __internal_ev_mwhgumiaa
#define __ev_mwhgssfan __internal_ev_mwhgssfan
#define __ev_mwhgsmfan __internal_ev_mwhgsmfan
#define __ev_mwhgsmian __internal_ev_mwhgsmian
#define __ev_mwhgumian __internal_ev_mwhgumian
#define __ev_mwhssiaaw __internal_ev_mwhssiaaw
#define __ev_mwhssfaaw __internal_ev_mwhssfaaw
#define __ev_mwhsmfaaw __internal_ev_mwhsmfaaw
#define __ev_mwhsmiaaw __internal_ev_mwhsmiaaw
#define __ev_mwhusiaaw __internal_ev_mwhusiaaw
#define __ev_mwhumiaaw __internal_ev_mwhumiaaw
#define __ev_mwhssfanw __internal_ev_mwhssfanw
#define __ev_mwhssianw __internal_ev_mwhssianw
#define __ev_mwhsmfanw __internal_ev_mwhsmfanw
#define __ev_mwhsmianw __internal_ev_mwhsmianw
#define __ev_mwhusianw __internal_ev_mwhusianw
#define __ev_mwhumianw __internal_ev_mwhumianw

static inline __ev64_opaque__
__internal_ev_mwhssfaaw (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhssf (a, b);
  return __ev_addssiaaw (t);
}

static inline __ev64_opaque__
__internal_ev_mwhssiaaw (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;
  
  t = __ev_mwhsmi (a, b);
  return __ev_addssiaaw (t);
}

static inline __ev64_opaque__
__internal_ev_mwhsmfaaw (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhsmf (a, b);
  return __ev_addsmiaaw (t);
}
 
static inline __ev64_opaque__
__internal_ev_mwhsmiaaw (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhsmi (a, b);
  return __ev_addsmiaaw (t);
}
 
static inline __ev64_opaque__
__internal_ev_mwhusiaaw (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhumi (a, b);
  return __ev_addusiaaw (t);
}
 
static inline __ev64_opaque__
__internal_ev_mwhumiaaw (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhumi (a, b);
  return __ev_addumiaaw (t);
}
 
static inline __ev64_opaque__
__internal_ev_mwhssfanw (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhssf (a, b);
  return __ev_subfssiaaw (t);
}

static inline __ev64_opaque__
__internal_ev_mwhssianw (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhsmi (a, b);
  return __ev_subfssiaaw (t);
}
 
static inline __ev64_opaque__
__internal_ev_mwhsmfanw (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhsmf (a, b);
  return __ev_subfsmiaaw (t);
}
 
static inline __ev64_opaque__
__internal_ev_mwhsmianw (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhsmi (a, b);
  return __ev_subfsmiaaw (t);
}
 
static inline __ev64_opaque__
__internal_ev_mwhusianw (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhumi (a, b);
  return __ev_subfusiaaw (t);
}
 
static inline __ev64_opaque__
__internal_ev_mwhumianw (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhumi (a, b);
  return __ev_subfumiaaw (t);
}

static inline __ev64_opaque__
__internal_ev_mwhgssfaa (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhssf (a, b);
  return __ev_mwsmiaa (t, ((__ev64_s32__){1, 1}));
}

static inline __ev64_opaque__
__internal_ev_mwhgsmfaa (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhsmf (a, b);
  return __ev_mwsmiaa (t, ((__ev64_s32__){1, 1}));
}

static inline __ev64_opaque__
__internal_ev_mwhgsmiaa (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhsmi (a, b);
  return __ev_mwsmiaa (t, ((__ev64_s32__){1, 1}));
}

static inline __ev64_opaque__
__internal_ev_mwhgumiaa (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhumi (a, b);
  return __ev_mwumiaa (t, ((__ev64_s32__){1, 1}));
}

static inline __ev64_opaque__
__internal_ev_mwhgssfan (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhssf (a, b);
  return __ev_mwsmian (t, ((__ev64_s32__){1, 1}));
}

static inline __ev64_opaque__
__internal_ev_mwhgsmfan (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhsmf (a, b);
  return __ev_mwsmian (t, ((__ev64_s32__){1, 1}));
}

static inline __ev64_opaque__
__internal_ev_mwhgsmian (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhsmi (a, b);
  return __ev_mwsmian (t, ((__ev64_s32__){1, 1}));
}

static inline __ev64_opaque__
__internal_ev_mwhgumian (__ev64_opaque__ a, __ev64_opaque__ b)
{
  __ev64_opaque__ t;

  t = __ev_mwhumi (a, b);
  return __ev_mwumian (t, ((__ev64_s32__){1, 1}));
}

/* END OF NOT SUPPORTED */

/* __ev_create* functions.  */

#define __ev_create_ufix32_u32 __ev_create_u32
#define __ev_create_sfix32_s32 __ev_create_s32

static inline __ev64_opaque__
__ev_create_s16 (int16_t a, int16_t b, int16_t c, int16_t d)
{
  union
  {
    __ev64_opaque__ v;
    int16_t i[4];
  } u;

  u.i[0] = a;
  u.i[1] = b;
  u.i[2] = c;
  u.i[3] = d;

  return u.v;
}

static inline __ev64_opaque__
__ev_create_u16 (uint16_t a, uint16_t b, uint16_t c, uint16_t d)
				  
{
  union
  {
    __ev64_opaque__ v;
    uint16_t i[4];
  } u;

  u.i[0] = a;
  u.i[1] = b;
  u.i[2] = c;
  u.i[3] = d;

  return u.v;
}

static inline __ev64_opaque__
__ev_create_s32 (int32_t a, int32_t b)
{
  union
  {
    __ev64_opaque__ v;
   int32_t i[2];
  } u;

  u.i[0] = a;
  u.i[1] = b;

  return u.v;
}

static inline __ev64_opaque__
__ev_create_u32 (uint32_t a, uint32_t b)
{
  union
  {
    __ev64_opaque__ v;
    uint32_t i[2];
  } u;

  u.i[0] = a;
  u.i[1] = b;

  return u.v;
}

static inline __ev64_opaque__
__ev_create_fs (float a, float b)
{
  union
  {
    __ev64_opaque__ v;
    float f[2];
  } u;

  u.f[0] = a;
  u.f[1] = b;
  
  return u.v;
}

static inline __ev64_opaque__
__ev_create_sfix32_fs (float a, float b)
{
  __ev64_opaque__ ev;

  ev = (__ev64_opaque__) __ev_create_fs (a, b);
  return (__ev64_opaque__) __builtin_spe_evfsctsf ((__v2sf) ev);
}

static inline __ev64_opaque__
__ev_create_ufix32_fs (float a, float b)
{
  __ev64_opaque__ ev;

  ev = (__ev64_opaque__) __ev_create_fs (a, b);
  return (__ev64_opaque__) __builtin_spe_evfsctuf ((__v2sf) ev);
}

static inline __ev64_opaque__
__ev_create_s64 (int64_t a)
{
  union
  {
    __ev64_opaque__ v;
    int64_t i;
  } u;

  u.i = a;
  return u.v;
}

static inline __ev64_opaque__
__ev_create_u64 (uint64_t a)
{
  union
  {
    __ev64_opaque__ v;
    uint64_t i;
  } u;

  u.i = a;
  return u.v;
}

static inline uint64_t
__ev_convert_u64 (__ev64_opaque__ a)
{
  return (uint64_t) a;
}

static inline int64_t
__ev_convert_s64 (__ev64_opaque__ a)
{
  return (int64_t) a;
}

/* __ev_get_* functions.  */

#define __ev_get_upper_u32(a) __ev_get_u32_internal ((a), 0)
#define __ev_get_lower_u32(a) __ev_get_u32_internal ((a), 1)
#define __ev_get_upper_s32(a) __ev_get_s32_internal ((a), 0)
#define __ev_get_lower_s32(a) __ev_get_s32_internal ((a), 1)
#define __ev_get_upper_fs(a) __ev_get_fs_internal ((a), 0)
#define __ev_get_lower_fs(a) __ev_get_fs_internal ((a), 1)
#define __ev_get_upper_ufix32_u32 __ev_get_upper_u32
#define __ev_get_lower_ufix32_u32 __ev_get_lower_u32
#define __ev_get_upper_sfix32_s32 __ev_get_upper_s32
#define __ev_get_lower_sfix32_s32 __ev_get_lower_s32
#define __ev_get_upper_sfix32_fs(a)  __ev_get_sfix32_fs ((a), 0)
#define __ev_get_lower_sfix32_fs(a)  __ev_get_sfix32_fs ((a), 1)
#define __ev_get_upper_ufix32_fs(a)  __ev_get_ufix32_fs ((a), 0)
#define __ev_get_lower_ufix32_fs(a)  __ev_get_ufix32_fs ((a), 1)

#define __ev_get_u32 __ev_get_u32_internal
#define __ev_get_s32 __ev_get_s32_internal
#define __ev_get_fs __ev_get_fs_internal
#define __ev_get_u16 __ev_get_u16_internal
#define __ev_get_s16 __ev_get_s16_internal

#define __ev_get_ufix32_u32 __ev_get_u32
#define __ev_get_sfix32_s32 __ev_get_s32
#define __ev_get_ufix32_fs     __ev_get_ufix32_fs_internal
#define __ev_get_sfix32_fs     __ev_get_sfix32_fs_internal

static inline uint32_t
__ev_get_u32_internal (__ev64_opaque__ a, uint32_t pos)
{
  union
  {
    __ev64_opaque__ v;
    uint32_t i[2];
  } u;

  u.v = a;
  return u.i[pos];
}

static inline int32_t
__ev_get_s32_internal (__ev64_opaque__ a, uint32_t pos)
{
  union
  {
    __ev64_opaque__ v;
    int32_t i[2];
  } u;

  u.v = a;
  return u.i[pos];
}

static inline float
__ev_get_fs_internal (__ev64_opaque__ a, uint32_t pos)
{
  union
  {
    __ev64_opaque__ v;
    float f[2];
  } u;

  u.v = a;
  return u.f[pos];
}

static inline float
__ev_get_sfix32_fs_internal (__ev64_opaque__ a, uint32_t pos)
{
  __ev64_fs__ v;

  v = __builtin_spe_evfscfsf ((__v2sf) a);
  return __ev_get_fs_internal ((__ev64_opaque__) v, pos);
}

static inline float
__ev_get_ufix32_fs_internal (__ev64_opaque__ a, uint32_t pos)
{
  __ev64_fs__ v;

  v = __builtin_spe_evfscfuf ((__v2sf) a);
  return __ev_get_fs_internal ((__ev64_opaque__) v, pos);
}

static inline uint16_t
__ev_get_u16_internal (__ev64_opaque__ a, uint32_t pos)
{
  union
  {
    __ev64_opaque__ v;
    uint16_t i[4];
  } u;

  u.v = a;
  return u.i[pos];
}

static inline int16_t
__ev_get_s16_internal (__ev64_opaque__ a, uint32_t pos)
{
  union
  {
    __ev64_opaque__ v;
    int16_t i[4];
  } u;

  u.v = a;
  return u.i[pos];
}

/* __ev_set_* functions.  */

#define __ev_set_u32 __ev_set_u32_internal
#define __ev_set_s32 __ev_set_s32_internal
#define __ev_set_fs __ev_set_fs_internal
#define __ev_set_u16 __ev_set_u16_internal
#define __ev_set_s16 __ev_set_s16_internal

#define __ev_set_ufix32_u32 __ev_set_u32
#define __ev_set_sfix32_s32 __ev_set_s32

#define __ev_set_sfix32_fs  __ev_set_sfix32_fs_internal
#define __ev_set_ufix32_fs  __ev_set_ufix32_fs_internal

#define __ev_set_upper_u32(a, b) __ev_set_u32 (a, b, 0)
#define __ev_set_lower_u32(a, b) __ev_set_u32 (a, b, 1)
#define __ev_set_upper_s32(a, b) __ev_set_s32 (a, b, 0)
#define __ev_set_lower_s32(a, b) __ev_set_s32 (a, b, 1)
#define __ev_set_upper_fs(a, b) __ev_set_fs (a, b, 0)
#define __ev_set_lower_fs(a, b) __ev_set_fs (a, b, 1)
#define __ev_set_upper_ufix32_u32 __ev_set_upper_u32
#define __ev_set_lower_ufix32_u32 __ev_set_lower_u32
#define __ev_set_upper_sfix32_s32 __ev_set_upper_s32
#define __ev_set_lower_sfix32_s32 __ev_set_lower_s32
#define __ev_set_upper_sfix32_fs(a, b)  __ev_set_sfix32_fs (a, b, 0)
#define __ev_set_lower_sfix32_fs(a, b)  __ev_set_sfix32_fs (a, b, 1)
#define __ev_set_upper_ufix32_fs(a, b)  __ev_set_ufix32_fs (a, b, 0)
#define __ev_set_lower_ufix32_fs(a, b)  __ev_set_ufix32_fs (a, b, 1)

#define __ev_set_acc_vec64 __builtin_spe_evmra

static inline __ev64_opaque__
__ev_set_acc_u64 (uint64_t a)
{
  __ev64_opaque__ ev32;
  ev32 = __ev_create_u64 (a);
  __ev_mra (ev32);
  return ev32;
}

static inline __ev64_opaque__
__ev_set_acc_s64 (int64_t a)
{
  __ev64_opaque__ ev32;
  ev32 = __ev_create_s64 (a);
  __ev_mra (ev32);
  return ev32;
}

static inline __ev64_opaque__
__ev_set_u32_internal (__ev64_opaque__ a, uint32_t b, uint32_t pos)
{
  union
  {
    __ev64_opaque__ v;
    uint32_t i[2];
  } u;

  u.v = a;
  u.i[pos] = b;
  return u.v;
}

static inline __ev64_opaque__
__ev_set_s32_internal (__ev64_opaque__ a, int32_t b, uint32_t pos)
{
  union
  {
    __ev64_opaque__ v;
    int32_t i[2];
  } u;

  u.v = a;
  u.i[pos] = b;
  return u.v;
}

static inline __ev64_opaque__
__ev_set_fs_internal (__ev64_opaque__ a, float b, uint32_t pos)
{
  union
  {
    __ev64_opaque__ v;
    float f[2];
  } u;

  u.v = a;
  u.f[pos] = b;
  return u.v;
}

static inline __ev64_opaque__
__ev_set_sfix32_fs_internal (__ev64_opaque__ a, float b, uint32_t pos)
{
  __ev64_opaque__ v;
  float other;

  /* Get other half.  */
  other = __ev_get_fs_internal (a, pos ^ 1);

  /* Make an sfix32 with 'b'.  */
  v = __ev_create_sfix32_fs (b, b);

  /* Set other half to what it used to be.  */
  return __ev_set_fs_internal (v, other, pos ^ 1);
}

static inline __ev64_opaque__
__ev_set_ufix32_fs_internal (__ev64_opaque__ a, float b, uint32_t pos)
{
  __ev64_opaque__ v;
  float other;

  /* Get other half.  */
  other = __ev_get_fs_internal (a, pos ^ 1);

  /* Make an ufix32 with 'b'.  */
  v = __ev_create_ufix32_fs (b, b);

  /* Set other half to what it used to be.  */
  return __ev_set_fs_internal (v, other, pos ^ 1);
}

static inline __ev64_opaque__
__ev_set_u16_internal (__ev64_opaque__ a, uint16_t b, uint32_t pos)
{
  union
  {
    __ev64_opaque__ v;
    uint16_t i[4];
  } u;

  u.v = a;
  u.i[pos] = b;
  return u.v;
}

static inline __ev64_opaque__
__ev_set_s16_internal (__ev64_opaque__ a, int16_t b, uint32_t pos)
{
  union
  {
    __ev64_opaque__ v;
    int16_t i[4];
  } u;

  u.v = a;
  u.i[pos] = b;
  return u.v;
}

/* Predicates.  */

#define __pred_all	0
#define __pred_any	1
#define __pred_upper	2
#define __pred_lower	3

#define __ev_any_gts(a, b)		__builtin_spe_evcmpgts (__pred_any, (a), (b))
#define __ev_all_gts(a, b)		__builtin_spe_evcmpgts (__pred_all, (a), (b))
#define __ev_upper_gts(a, b)		__builtin_spe_evcmpgts (__pred_upper, (a), (b))
#define __ev_lower_gts(a, b)		__builtin_spe_evcmpgts (__pred_lower, (a), (b))
#define __ev_select_gts			__builtin_spe_evsel_gts

#define __ev_any_gtu(a, b)		__builtin_spe_evcmpgtu (__pred_any, (a), (b))
#define __ev_all_gtu(a, b)		__builtin_spe_evcmpgtu (__pred_all, (a), (b))
#define __ev_upper_gtu(a, b)		__builtin_spe_evcmpgtu (__pred_upper, (a), (b))
#define __ev_lower_gtu(a, b)		__builtin_spe_evcmpgtu (__pred_lower, (a), (b))
#define __ev_select_gtu			__builtin_spe_evsel_gtu

#define __ev_any_lts(a, b)		__builtin_spe_evcmplts (__pred_any, (a), (b))
#define __ev_all_lts(a, b)		__builtin_spe_evcmplts (__pred_all, (a), (b))
#define __ev_upper_lts(a, b)		__builtin_spe_evcmplts (__pred_upper, (a), (b))
#define __ev_lower_lts(a, b)		__builtin_spe_evcmplts (__pred_lower, (a), (b))
#define __ev_select_lts(a, b, c, d) 	((__v2si) __builtin_spe_evsel_lts ((a), (b), (c), (d)))

#define __ev_any_ltu(a, b)		__builtin_spe_evcmpltu (__pred_any, (a), (b))
#define __ev_all_ltu(a, b)		__builtin_spe_evcmpltu (__pred_all, (a), (b))
#define __ev_upper_ltu(a, b)		__builtin_spe_evcmpltu (__pred_upper, (a), (b))
#define __ev_lower_ltu(a, b)		__builtin_spe_evcmpltu (__pred_lower, (a), (b))
#define __ev_select_ltu 		__builtin_spe_evsel_ltu
#define __ev_any_eq(a, b)		__builtin_spe_evcmpeq (__pred_any, (a), (b))
#define __ev_all_eq(a, b)		__builtin_spe_evcmpeq (__pred_all, (a), (b))
#define __ev_upper_eq(a, b)		__builtin_spe_evcmpeq (__pred_upper, (a), (b))
#define __ev_lower_eq(a, b)		__builtin_spe_evcmpeq (__pred_lower, (a), (b))
#define __ev_select_eq			__builtin_spe_evsel_eq

#define __ev_any_fs_gt(a, b)		__builtin_spe_evfscmpgt (__pred_any, (a), (b))
#define __ev_all_fs_gt(a, b)		__builtin_spe_evfscmpgt (__pred_all, (a), (b))
#define __ev_upper_fs_gt(a, b)		__builtin_spe_evfscmpgt (__pred_upper, (a), (b))
#define __ev_lower_fs_gt(a, b)		__builtin_spe_evfscmpgt (__pred_lower, (a), (b))
#define __ev_select_fs_gt		__builtin_spe_evsel_fsgt

#define __ev_any_fs_lt(a, b)		__builtin_spe_evfscmplt (__pred_any, (a), (b))
#define __ev_all_fs_lt(a, b)		__builtin_spe_evfscmplt (__pred_all, (a), (b))
#define __ev_upper_fs_lt(a, b)		__builtin_spe_evfscmplt (__pred_upper, (a), (b))
#define __ev_lower_fs_lt(a, b)		__builtin_spe_evfscmplt (__pred_lower, (a), (b))
#define __ev_select_fs_lt		__builtin_spe_evsel_fslt

#define __ev_any_fs_eq(a, b)		__builtin_spe_evfscmpeq (__pred_any, (a), (b))
#define __ev_all_fs_eq(a, b)		__builtin_spe_evfscmpeq (__pred_all, (a), (b))
#define __ev_upper_fs_eq(a, b)		__builtin_spe_evfscmpeq (__pred_upper, (a), (b))
#define __ev_lower_fs_eq(a, b)		__builtin_spe_evfscmpeq (__pred_lower, (a), (b))
#define __ev_select_fs_eq		__builtin_spe_evsel_fseq

#define __ev_any_fs_tst_gt(a, b)	__builtin_spe_evfststgt (__pred_any, (a), (b))
#define __ev_all_fs_tst_gt(a, b)	__builtin_spe_evfststgt (__pred_all, (a), (b))
#define __ev_upper_fs_tst_gt(a, b)	__builtin_spe_evfststgt (__pred_upper, (a), (b))
#define __ev_lower_fs_tst_gt(a, b)	__builtin_spe_evfststgt (__pred_lower, (a), (b))
#define __ev_select_fs_tst_gt           __builtin_spe_evsel_fststgt

#define __ev_any_fs_tst_lt(a, b)	__builtin_spe_evfststlt (__pred_any, (a), (b))
#define __ev_all_fs_tst_lt(a, b)	__builtin_spe_evfststlt (__pred_all, (a), (b))
#define __ev_upper_fs_tst_lt(a, b)	__builtin_spe_evfststlt (__pred_upper, (a), (b))
#define __ev_lower_fs_tst_lt(a, b)	__builtin_spe_evfststlt (__pred_lower, (a), (b))
#define __ev_select_fs_tst_lt		__builtin_spe_evsel_fststlt

#define __ev_any_fs_tst_eq(a, b)	__builtin_spe_evfststeq (__pred_any, (a), (b))
#define __ev_all_fs_tst_eq(a, b)	__builtin_spe_evfststeq (__pred_all, (a), (b))
#define __ev_upper_fs_tst_eq(a, b)	__builtin_spe_evfststeq (__pred_upper, (a), (b))
#define __ev_lower_fs_tst_eq(a, b)	__builtin_spe_evfststeq (__pred_lower, (a), (b))
#define __ev_select_fs_tst_eq		__builtin_spe_evsel_fststeq

/* SPEFSCR accessor functions.  */

#define __SPEFSCR_SOVH		0x80000000
#define __SPEFSCR_OVH		0x40000000
#define __SPEFSCR_FGH		0x20000000
#define __SPEFSCR_FXH		0x10000000
#define __SPEFSCR_FINVH		0x08000000
#define __SPEFSCR_FDBZH		0x04000000
#define __SPEFSCR_FUNFH		0x02000000
#define __SPEFSCR_FOVFH		0x01000000
/* 2 unused bits.  */
#define __SPEFSCR_FINXS		0x00200000
#define __SPEFSCR_FINVS		0x00100000
#define __SPEFSCR_FDBZS		0x00080000
#define __SPEFSCR_FUNFS		0x00040000
#define __SPEFSCR_FOVFS		0x00020000
#define __SPEFSCR_MODE		0x00010000
#define __SPEFSCR_SOV		0x00008000
#define __SPEFSCR_OV		0x00004000
#define __SPEFSCR_FG		0x00002000
#define __SPEFSCR_FX		0x00001000
#define __SPEFSCR_FINV		0x00000800
#define __SPEFSCR_FDBZ		0x00000400
#define __SPEFSCR_FUNF		0x00000200
#define __SPEFSCR_FOVF		0x00000100
/* 1 unused bit.  */
#define __SPEFSCR_FINXE		0x00000040
#define __SPEFSCR_FINVE		0x00000020
#define __SPEFSCR_FDBZE		0x00000010
#define __SPEFSCR_FUNFE		0x00000008
#define __SPEFSCR_FOVFE		0x00000004
#define __SPEFSCR_FRMC		0x00000003

#define __ev_get_spefscr_sovh() (__builtin_spe_mfspefscr () & __SPEFSCR_SOVH)
#define __ev_get_spefscr_ovh() (__builtin_spe_mfspefscr () & __SPEFSCR_OVH)
#define __ev_get_spefscr_fgh() (__builtin_spe_mfspefscr () & __SPEFSCR_FGH)
#define __ev_get_spefscr_fxh() (__builtin_spe_mfspefscr () & __SPEFSCR_FXH)
#define __ev_get_spefscr_finvh() (__builtin_spe_mfspefscr () & __SPEFSCR_FINVH)
#define __ev_get_spefscr_fdbzh() (__builtin_spe_mfspefscr () & __SPEFSCR_FDBZH)
#define __ev_get_spefscr_funfh() (__builtin_spe_mfspefscr () & __SPEFSCR_FUNFH)
#define __ev_get_spefscr_fovfh() (__builtin_spe_mfspefscr () & __SPEFSCR_FOVFH)
#define __ev_get_spefscr_finxs() (__builtin_spe_mfspefscr () & __SPEFSCR_FINXS)
#define __ev_get_spefscr_finvs() (__builtin_spe_mfspefscr () & __SPEFSCR_FINVS)
#define __ev_get_spefscr_fdbzs() (__builtin_spe_mfspefscr () & __SPEFSCR_FDBZS)
#define __ev_get_spefscr_funfs() (__builtin_spe_mfspefscr () & __SPEFSCR_FUNFS)
#define __ev_get_spefscr_fovfs() (__builtin_spe_mfspefscr () & __SPEFSCR_FOVFS)
#define __ev_get_spefscr_mode() (__builtin_spe_mfspefscr () & __SPEFSCR_MODE)
#define __ev_get_spefscr_sov() (__builtin_spe_mfspefscr () & __SPEFSCR_SOV)
#define __ev_get_spefscr_ov() (__builtin_spe_mfspefscr () & __SPEFSCR_OV)
#define __ev_get_spefscr_fg() (__builtin_spe_mfspefscr () & __SPEFSCR_FG)
#define __ev_get_spefscr_fx() (__builtin_spe_mfspefscr () & __SPEFSCR_FX)
#define __ev_get_spefscr_finv() (__builtin_spe_mfspefscr () & __SPEFSCR_FINV)
#define __ev_get_spefscr_fdbz() (__builtin_spe_mfspefscr () & __SPEFSCR_FDBZ)
#define __ev_get_spefscr_funf() (__builtin_spe_mfspefscr () & __SPEFSCR_FUNF)
#define __ev_get_spefscr_fovf() (__builtin_spe_mfspefscr () & __SPEFSCR_FOVF)
#define __ev_get_spefscr_finxe() (__builtin_spe_mfspefscr () & __SPEFSCR_FINXE)
#define __ev_get_spefscr_finve() (__builtin_spe_mfspefscr () & __SPEFSCR_FINVE)
#define __ev_get_spefscr_fdbze() (__builtin_spe_mfspefscr () & __SPEFSCR_FDBZE)
#define __ev_get_spefscr_funfe() (__builtin_spe_mfspefscr () & __SPEFSCR_FUNFE)
#define __ev_get_spefscr_fovfe() (__builtin_spe_mfspefscr () & __SPEFSCR_FOVFE)
#define __ev_get_spefscr_frmc() (__builtin_spe_mfspefscr () & __SPEFSCR_FRMC)

static inline void
__ev_clr_spefscr_field (int mask)
{
  int i;

  i = __builtin_spe_mfspefscr ();
  i &= ~mask;
  __builtin_spe_mtspefscr (i);
}

#define __ev_clr_spefscr_sovh() __ev_clr_spefscr_field (__SPEFSCR_SOVH)
#define __ev_clr_spefscr_sov() __ev_clr_spefscr_field (__SPEFSCR_SOV)
#define __ev_clr_spefscr_finxs() __ev_clr_spefscr_field (__SPEFSCR_FINXS)
#define __ev_clr_spefscr_finvs() __ev_clr_spefscr_field (__SPEFSCR_FINVS)
#define __ev_clr_spefscr_fdbzs() __ev_clr_spefscr_field (__SPEFSCR_FDBZS)
#define __ev_clr_spefscr_funfs() __ev_clr_spefscr_field (__SPEFSCR_FUNFS)
#define __ev_clr_spefscr_fovfs() __ev_clr_spefscr_field (__SPEFSCR_FOVFS)

/* Set rounding mode:
     rnd = 0 (nearest)
     rnd = 1 (zero)
     rnd = 2 (+inf)
     rnd = 3 (-inf).  */

static inline void
__ev_set_spefscr_frmc (int rnd)
{
  int i;

  i = __builtin_spe_mfspefscr ();
  i &= ~__SPEFSCR_FRMC;
  i |= rnd;
  __builtin_spe_mtspefscr (i);
}

/* The SPE PIM says these are declared in <spe.h>, although they are
   not provided by GCC: they must be taken from a separate
   library.  */
extern short int atosfix16 (const char *);
extern int atosfix32 (const char *);
extern long long atosfix64 (const char *);

extern unsigned short atoufix16 (const char *);
extern unsigned int atoufix32 (const char *);
extern unsigned long long atoufix64 (const char *);

extern short int strtosfix16 (const char *, char **);
extern int strtosfix32 (const char *, char **);
extern long long strtosfix64 (const char *, char **);

extern unsigned short int strtoufix16 (const char *, char **);
extern unsigned int strtoufix32 (const char *, char **);
extern unsigned long long strtoufix64 (const char *, char **);

#endif /* _SPE_H */
