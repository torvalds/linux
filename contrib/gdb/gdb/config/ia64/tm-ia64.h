/* Definitions to target GDB to GNU/Linux on an ia64 architecture.
   Copyright 1992, 1993, 2000 Free Software Foundation, Inc.

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

#ifndef TM_IA64_H
#define TM_IA64_H

#define GDB_MULTI_ARCH 1

/* Register numbers of various important registers */

/* General registers; there are 128 of these 64 bit wide registers.  The
   first 32 are static and the last 96 are stacked. */
#define IA64_GR0_REGNUM		0
#define IA64_GR1_REGNUM		(IA64_GR0_REGNUM+1)
#define IA64_GR2_REGNUM		(IA64_GR0_REGNUM+2)
#define IA64_GR3_REGNUM		(IA64_GR0_REGNUM+3)
#define IA64_GR4_REGNUM		(IA64_GR0_REGNUM+4)
#define IA64_GR5_REGNUM		(IA64_GR0_REGNUM+5)
#define IA64_GR6_REGNUM		(IA64_GR0_REGNUM+6)
#define IA64_GR7_REGNUM		(IA64_GR0_REGNUM+7)
#define IA64_GR8_REGNUM		(IA64_GR0_REGNUM+8)
#define IA64_GR9_REGNUM		(IA64_GR0_REGNUM+9)
#define IA64_GR10_REGNUM	(IA64_GR0_REGNUM+10)
#define IA64_GR11_REGNUM	(IA64_GR0_REGNUM+11)
#define IA64_GR12_REGNUM	(IA64_GR0_REGNUM+12)
#define IA64_GR31_REGNUM	(IA64_GR0_REGNUM+31)
#define IA64_GR32_REGNUM	(IA64_GR0_REGNUM+32)
#define IA64_GR127_REGNUM	(IA64_GR0_REGNUM+127)

/* Floating point registers; 128 82-bit wide registers */
#define IA64_FR0_REGNUM		128
#define IA64_FR1_REGNUM		(IA64_FR0_REGNUM+1)
#define IA64_FR2_REGNUM		(IA64_FR0_REGNUM+2)
#define IA64_FR8_REGNUM		(IA64_FR0_REGNUM+8)
#define IA64_FR9_REGNUM		(IA64_FR0_REGNUM+9)
#define IA64_FR10_REGNUM	(IA64_FR0_REGNUM+10)
#define IA64_FR11_REGNUM	(IA64_FR0_REGNUM+11)
#define IA64_FR12_REGNUM	(IA64_FR0_REGNUM+12)
#define IA64_FR13_REGNUM	(IA64_FR0_REGNUM+13)
#define IA64_FR14_REGNUM	(IA64_FR0_REGNUM+14)
#define IA64_FR15_REGNUM	(IA64_FR0_REGNUM+15)
#define IA64_FR16_REGNUM	(IA64_FR0_REGNUM+16)
#define IA64_FR31_REGNUM	(IA64_FR0_REGNUM+31)
#define IA64_FR32_REGNUM	(IA64_FR0_REGNUM+32)
#define IA64_FR127_REGNUM	(IA64_FR0_REGNUM+127)

/* Predicate registers; There are 64 of these one bit registers.
   It'd be more convenient (implementation-wise) to use a single
   64 bit word with all of these register in them.  Note that there's
   also a IA64_PR_REGNUM below which contains all the bits and is used for
   communicating the actual values to the target. */

#define IA64_PR0_REGNUM		256
#define IA64_PR1_REGNUM		(IA64_PR0_REGNUM+1)
#define IA64_PR2_REGNUM		(IA64_PR0_REGNUM+2)
#define IA64_PR3_REGNUM		(IA64_PR0_REGNUM+3)
#define IA64_PR4_REGNUM		(IA64_PR0_REGNUM+4)
#define IA64_PR5_REGNUM		(IA64_PR0_REGNUM+5)
#define IA64_PR6_REGNUM		(IA64_PR0_REGNUM+6)
#define IA64_PR7_REGNUM		(IA64_PR0_REGNUM+7)
#define IA64_PR8_REGNUM		(IA64_PR0_REGNUM+8)
#define IA64_PR9_REGNUM		(IA64_PR0_REGNUM+9)
#define IA64_PR10_REGNUM	(IA64_PR0_REGNUM+10)
#define IA64_PR11_REGNUM	(IA64_PR0_REGNUM+11)
#define IA64_PR12_REGNUM	(IA64_PR0_REGNUM+12)
#define IA64_PR13_REGNUM	(IA64_PR0_REGNUM+13)
#define IA64_PR14_REGNUM	(IA64_PR0_REGNUM+14)
#define IA64_PR15_REGNUM	(IA64_PR0_REGNUM+15)
#define IA64_PR16_REGNUM	(IA64_PR0_REGNUM+16)
#define IA64_PR17_REGNUM	(IA64_PR0_REGNUM+17)
#define IA64_PR18_REGNUM	(IA64_PR0_REGNUM+18)
#define IA64_PR19_REGNUM	(IA64_PR0_REGNUM+19)
#define IA64_PR20_REGNUM	(IA64_PR0_REGNUM+20)
#define IA64_PR21_REGNUM	(IA64_PR0_REGNUM+21)
#define IA64_PR22_REGNUM	(IA64_PR0_REGNUM+22)
#define IA64_PR23_REGNUM	(IA64_PR0_REGNUM+23)
#define IA64_PR24_REGNUM	(IA64_PR0_REGNUM+24)
#define IA64_PR25_REGNUM	(IA64_PR0_REGNUM+25)
#define IA64_PR26_REGNUM	(IA64_PR0_REGNUM+26)
#define IA64_PR27_REGNUM	(IA64_PR0_REGNUM+27)
#define IA64_PR28_REGNUM	(IA64_PR0_REGNUM+28)
#define IA64_PR29_REGNUM	(IA64_PR0_REGNUM+29)
#define IA64_PR30_REGNUM	(IA64_PR0_REGNUM+30)
#define IA64_PR31_REGNUM	(IA64_PR0_REGNUM+31)
#define IA64_PR32_REGNUM	(IA64_PR0_REGNUM+32)
#define IA64_PR33_REGNUM	(IA64_PR0_REGNUM+33)
#define IA64_PR34_REGNUM	(IA64_PR0_REGNUM+34)
#define IA64_PR35_REGNUM	(IA64_PR0_REGNUM+35)
#define IA64_PR36_REGNUM	(IA64_PR0_REGNUM+36)
#define IA64_PR37_REGNUM	(IA64_PR0_REGNUM+37)
#define IA64_PR38_REGNUM	(IA64_PR0_REGNUM+38)
#define IA64_PR39_REGNUM	(IA64_PR0_REGNUM+39)
#define IA64_PR40_REGNUM	(IA64_PR0_REGNUM+40)
#define IA64_PR41_REGNUM	(IA64_PR0_REGNUM+41)
#define IA64_PR42_REGNUM	(IA64_PR0_REGNUM+42)
#define IA64_PR43_REGNUM	(IA64_PR0_REGNUM+43)
#define IA64_PR44_REGNUM	(IA64_PR0_REGNUM+44)
#define IA64_PR45_REGNUM	(IA64_PR0_REGNUM+45)
#define IA64_PR46_REGNUM	(IA64_PR0_REGNUM+46)
#define IA64_PR47_REGNUM	(IA64_PR0_REGNUM+47)
#define IA64_PR48_REGNUM	(IA64_PR0_REGNUM+48)
#define IA64_PR49_REGNUM	(IA64_PR0_REGNUM+49)
#define IA64_PR50_REGNUM	(IA64_PR0_REGNUM+50)
#define IA64_PR51_REGNUM	(IA64_PR0_REGNUM+51)
#define IA64_PR52_REGNUM	(IA64_PR0_REGNUM+52)
#define IA64_PR53_REGNUM	(IA64_PR0_REGNUM+53)
#define IA64_PR54_REGNUM	(IA64_PR0_REGNUM+54)
#define IA64_PR55_REGNUM	(IA64_PR0_REGNUM+55)
#define IA64_PR56_REGNUM	(IA64_PR0_REGNUM+56)
#define IA64_PR57_REGNUM	(IA64_PR0_REGNUM+57)
#define IA64_PR58_REGNUM	(IA64_PR0_REGNUM+58)
#define IA64_PR59_REGNUM	(IA64_PR0_REGNUM+59)
#define IA64_PR60_REGNUM	(IA64_PR0_REGNUM+60)
#define IA64_PR61_REGNUM	(IA64_PR0_REGNUM+61)
#define IA64_PR62_REGNUM	(IA64_PR0_REGNUM+62)
#define IA64_PR63_REGNUM	(IA64_PR0_REGNUM+63)


/* Branch registers: 8 64-bit registers for holding branch targets */
#define IA64_BR0_REGNUM		320
#define IA64_BR1_REGNUM		(IA64_BR0_REGNUM+1)
#define IA64_BR2_REGNUM		(IA64_BR0_REGNUM+2)
#define IA64_BR3_REGNUM		(IA64_BR0_REGNUM+3)
#define IA64_BR4_REGNUM		(IA64_BR0_REGNUM+4)
#define IA64_BR5_REGNUM		(IA64_BR0_REGNUM+5)
#define IA64_BR6_REGNUM		(IA64_BR0_REGNUM+6)
#define IA64_BR7_REGNUM		(IA64_BR0_REGNUM+7)

/* Virtual frame pointer; this matches IA64_FRAME_POINTER_REGNUM in
   gcc/config/ia64/ia64.h. */
#define IA64_VFP_REGNUM		328

/* Virtual return address pointer; this matches IA64_RETURN_ADDRESS_POINTER_REGNUM
   in gcc/config/ia64/ia64.h. */
#define IA64_VRAP_REGNUM	329

/* Predicate registers: There are 64 of these 1-bit registers.  We
   define a single register which is used to communicate these values
   to/from the target.  We will somehow contrive to make it appear that
   IA64_PR0_REGNUM thru IA64_PR63_REGNUM hold the actual values. */
#define IA64_PR_REGNUM		330

/* Instruction pointer: 64 bits wide */
#define IA64_IP_REGNUM		331

/* Process Status Register */
#define IA64_PSR_REGNUM		332

/* Current Frame Marker (Raw form may be the cr.ifs) */
#define IA64_CFM_REGNUM		333

/* Application registers; 128 64-bit wide registers possible, but some
  of them are reserved */
#define IA64_AR0_REGNUM		334
#define IA64_KR0_REGNUM		(IA64_AR0_REGNUM+0)
#define IA64_KR7_REGNUM		(IA64_KR0_REGNUM+7)

#define IA64_RSC_REGNUM		(IA64_AR0_REGNUM+16)
#define IA64_BSP_REGNUM		(IA64_AR0_REGNUM+17)
#define IA64_BSPSTORE_REGNUM	(IA64_AR0_REGNUM+18)
#define IA64_RNAT_REGNUM	(IA64_AR0_REGNUM+19)
#define IA64_FCR_REGNUM		(IA64_AR0_REGNUM+21)
#define IA64_EFLAG_REGNUM	(IA64_AR0_REGNUM+24)
#define IA64_CSD_REGNUM		(IA64_AR0_REGNUM+25)
#define IA64_SSD_REGNUM		(IA64_AR0_REGNUM+26)
#define IA64_CFLG_REGNUM	(IA64_AR0_REGNUM+27)
#define IA64_FSR_REGNUM		(IA64_AR0_REGNUM+28)
#define IA64_FIR_REGNUM		(IA64_AR0_REGNUM+29)
#define IA64_FDR_REGNUM		(IA64_AR0_REGNUM+30)
#define IA64_CCV_REGNUM		(IA64_AR0_REGNUM+32)
#define IA64_UNAT_REGNUM	(IA64_AR0_REGNUM+36)
#define IA64_FPSR_REGNUM	(IA64_AR0_REGNUM+40)
#define IA64_ITC_REGNUM		(IA64_AR0_REGNUM+44)
#define IA64_PFS_REGNUM		(IA64_AR0_REGNUM+64)
#define IA64_LC_REGNUM		(IA64_AR0_REGNUM+65)
#define IA64_EC_REGNUM		(IA64_AR0_REGNUM+66)

/* NAT (Not A Thing) Bits for the general registers; there are 128 of these */
#define IA64_NAT0_REGNUM	462
#define IA64_NAT31_REGNUM	(IA64_NAT0_REGNUM+31)
#define IA64_NAT32_REGNUM	(IA64_NAT0_REGNUM+32)
#define IA64_NAT127_REGNUM	(IA64_NAT0_REGNUM+127)

#endif /* TM_IA64_H */
