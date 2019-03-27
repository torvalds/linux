/* ARM assembler/disassembler support.
   Copyright 2004 Free Software Foundation, Inc.

   This file is part of GDB and GAS.

   GDB and GAS are free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 1, or (at
   your option) any later version.

   GDB and GAS are distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GDB or GAS; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* The following bitmasks control CPU extensions:  */
#define ARM_EXT_V1	 0x00000001	/* All processors (core set).  */
#define ARM_EXT_V2	 0x00000002	/* Multiply instructions.  */
#define ARM_EXT_V2S	 0x00000004	/* SWP instructions.       */
#define ARM_EXT_V3	 0x00000008	/* MSR MRS.                */
#define ARM_EXT_V3M	 0x00000010	/* Allow long multiplies.  */
#define ARM_EXT_V4	 0x00000020	/* Allow half word loads.  */
#define ARM_EXT_V4T	 0x00000040	/* Thumb.                  */
#define ARM_EXT_V5	 0x00000080	/* Allow CLZ, etc.         */
#define ARM_EXT_V5T	 0x00000100	/* Improved interworking.  */
#define ARM_EXT_V5ExP	 0x00000200	/* DSP core set.           */
#define ARM_EXT_V5E	 0x00000400	/* DSP Double transfers.   */
#define ARM_EXT_V5J	 0x00000800	/* Jazelle extension.	   */
#define ARM_EXT_V6       0x00001000     /* ARM V6.                 */
#define ARM_EXT_V6K      0x00002000     /* ARM V6K.                */
#define ARM_EXT_V6Z      0x00004000     /* ARM V6Z.                */
#define ARM_EXT_V6T2	 0x00008000	/* Thumb-2.                */
#define ARM_EXT_DIV	 0x00010000	/* Integer division.       */
/* The 'M' in Arm V7M stands for Microcontroller.
   On earlier architecture variants it stands for Multiply.  */
#define ARM_EXT_V5E_NOTM 0x00020000	/* Arm V5E but not Arm V7M. */
#define ARM_EXT_V6_NOTM	 0x00040000	/* Arm V6 but not Arm V7M. */
#define ARM_EXT_V7	 0x00080000	/* Arm V7.                 */
#define ARM_EXT_V7A	 0x00100000	/* Arm V7A.                */
#define ARM_EXT_V7R	 0x00200000	/* Arm V7R.                */
#define ARM_EXT_V7M	 0x00400000	/* Arm V7M.                */

/* Co-processor space extensions.  */
#define ARM_CEXT_XSCALE   0x00000001	/* Allow MIA etc.          */
#define ARM_CEXT_MAVERICK 0x00000002	/* Use Cirrus/DSP coprocessor.  */
#define ARM_CEXT_IWMMXT   0x00000004    /* Intel Wireless MMX technology coprocessor.   */
#define ARM_CEXT_IWMMXT2  0x00000008    /* Intel Wireless MMX technology coprocessor version 2.   */

#define FPU_ENDIAN_PURE	 0x80000000	/* Pure-endian doubles.	      */
#define FPU_ENDIAN_BIG	 0		/* Double words-big-endian.   */
#define FPU_FPA_EXT_V1	 0x40000000	/* Base FPA instruction set.  */
#define FPU_FPA_EXT_V2	 0x20000000	/* LFM/SFM.		      */
#define FPU_MAVERICK	 0x10000000	/* Cirrus Maverick.	      */
#define FPU_VFP_EXT_V1xD 0x08000000	/* Base VFP instruction set.  */
#define FPU_VFP_EXT_V1	 0x04000000	/* Double-precision insns.    */
#define FPU_VFP_EXT_V2	 0x02000000	/* ARM10E VFPr1.	      */
#define FPU_VFP_EXT_V3	 0x01000000	/* VFPv3 insns.	              */
#define FPU_NEON_EXT_V1	 0x00800000	/* Neon (SIMD) insns.	      */

/* Architectures are the sum of the base and extensions.  The ARM ARM (rev E)
   defines the following: ARMv3, ARMv3M, ARMv4xM, ARMv4, ARMv4TxM, ARMv4T,
   ARMv5xM, ARMv5, ARMv5TxM, ARMv5T, ARMv5TExP, ARMv5TE.  To these we add
   three more to cover cores prior to ARM6.  Finally, there are cores which
   implement further extensions in the co-processor space.  */
#define ARM_AEXT_V1			  ARM_EXT_V1
#define ARM_AEXT_V2	(ARM_AEXT_V1	| ARM_EXT_V2)
#define ARM_AEXT_V2S	(ARM_AEXT_V2	| ARM_EXT_V2S)
#define ARM_AEXT_V3	(ARM_AEXT_V2S	| ARM_EXT_V3)
#define ARM_AEXT_V3M	(ARM_AEXT_V3	| ARM_EXT_V3M)
#define ARM_AEXT_V4xM	(ARM_AEXT_V3	| ARM_EXT_V4)
#define ARM_AEXT_V4	(ARM_AEXT_V3M	| ARM_EXT_V4)
#define ARM_AEXT_V4TxM	(ARM_AEXT_V4xM	| ARM_EXT_V4T)
#define ARM_AEXT_V4T	(ARM_AEXT_V4	| ARM_EXT_V4T)
#define ARM_AEXT_V5xM	(ARM_AEXT_V4xM	| ARM_EXT_V5)
#define ARM_AEXT_V5	(ARM_AEXT_V4	| ARM_EXT_V5)
#define ARM_AEXT_V5TxM	(ARM_AEXT_V5xM	| ARM_EXT_V4T | ARM_EXT_V5T)
#define ARM_AEXT_V5T	(ARM_AEXT_V5	| ARM_EXT_V4T | ARM_EXT_V5T)
#define ARM_AEXT_V5TExP	(ARM_AEXT_V5T	| ARM_EXT_V5ExP)
#define ARM_AEXT_V5TE	(ARM_AEXT_V5TExP | ARM_EXT_V5E)
#define ARM_AEXT_V5TEJ	(ARM_AEXT_V5TE	| ARM_EXT_V5J)
#define ARM_AEXT_V6     (ARM_AEXT_V5TEJ | ARM_EXT_V6)
#define ARM_AEXT_V6K    (ARM_AEXT_V6    | ARM_EXT_V6K)
#define ARM_AEXT_V6Z    (ARM_AEXT_V6    | ARM_EXT_V6Z)
#define ARM_AEXT_V6ZK   (ARM_AEXT_V6    | ARM_EXT_V6K | ARM_EXT_V6Z)
#define ARM_AEXT_V6T2   (ARM_AEXT_V6    | ARM_EXT_V6T2 | ARM_EXT_V6_NOTM)
#define ARM_AEXT_V6KT2  (ARM_AEXT_V6T2 | ARM_EXT_V6K)
#define ARM_AEXT_V6ZT2  (ARM_AEXT_V6T2 | ARM_EXT_V6Z)
#define ARM_AEXT_V6ZKT2 (ARM_AEXT_V6T2 | ARM_EXT_V6K | ARM_EXT_V6Z)
#define ARM_AEXT_V7_ARM	(ARM_AEXT_V6ZKT2 | ARM_EXT_V7)
#define ARM_AEXT_V7A	(ARM_AEXT_V7_ARM | ARM_EXT_V7A)
#define ARM_AEXT_V7R	(ARM_AEXT_V7_ARM | ARM_EXT_V7R | ARM_EXT_DIV)
#define ARM_AEXT_NOTM \
  (ARM_AEXT_V4 | ARM_EXT_V5ExP | ARM_EXT_V5J | ARM_EXT_V6_NOTM)
#define ARM_AEXT_V7M \
  ((ARM_AEXT_V7_ARM | ARM_EXT_V7M | ARM_EXT_DIV) & ~(ARM_AEXT_NOTM))
#define ARM_AEXT_V7 (ARM_AEXT_V7A & ARM_AEXT_V7R & ARM_AEXT_V7M)

/* Processors with specific extensions in the co-processor space.  */
#define ARM_ARCH_XSCALE	ARM_FEATURE (ARM_AEXT_V5TE, ARM_CEXT_XSCALE)
#define ARM_ARCH_IWMMXT	\
 ARM_FEATURE (ARM_AEXT_V5TE, ARM_CEXT_XSCALE | ARM_CEXT_IWMMXT)
#define ARM_ARCH_IWMMXT2	\
 ARM_FEATURE (ARM_AEXT_V5TE, ARM_CEXT_XSCALE | ARM_CEXT_IWMMXT | ARM_CEXT_IWMMXT2)

#define FPU_VFP_V1xD	(FPU_VFP_EXT_V1xD | FPU_ENDIAN_PURE)
#define FPU_VFP_V1	(FPU_VFP_V1xD | FPU_VFP_EXT_V1)
#define FPU_VFP_V2	(FPU_VFP_V1 | FPU_VFP_EXT_V2)
#define FPU_VFP_V3	(FPU_VFP_V2 | FPU_VFP_EXT_V3)
#define FPU_VFP_HARD	(FPU_VFP_EXT_V1xD | FPU_VFP_EXT_V1 | FPU_VFP_EXT_V2 \
                         | FPU_VFP_EXT_V3 | FPU_NEON_EXT_V1)
#define FPU_FPA		(FPU_FPA_EXT_V1 | FPU_FPA_EXT_V2)

/* Deprecated */
#define FPU_ARCH_VFP	ARM_FEATURE (0, FPU_ENDIAN_PURE)

#define FPU_ARCH_FPE	ARM_FEATURE (0, FPU_FPA_EXT_V1)
#define FPU_ARCH_FPA	ARM_FEATURE (0, FPU_FPA)

#define FPU_ARCH_VFP_V1xD ARM_FEATURE (0, FPU_VFP_V1xD)
#define FPU_ARCH_VFP_V1	  ARM_FEATURE (0, FPU_VFP_V1)
#define FPU_ARCH_VFP_V2	  ARM_FEATURE (0, FPU_VFP_V2)
#define FPU_ARCH_VFP_V3	  ARM_FEATURE (0, FPU_VFP_V3)
#define FPU_ARCH_NEON_V1  ARM_FEATURE (0, FPU_NEON_EXT_V1)
#define FPU_ARCH_VFP_V3_PLUS_NEON_V1 \
  ARM_FEATURE (0, FPU_VFP_V3 | FPU_NEON_EXT_V1)
#define FPU_ARCH_VFP_HARD ARM_FEATURE (0, FPU_VFP_HARD)

#define FPU_ARCH_ENDIAN_PURE ARM_FEATURE (0, FPU_ENDIAN_PURE)

#define FPU_ARCH_MAVERICK ARM_FEATURE (0, FPU_MAVERICK)

#define ARM_ARCH_V1	ARM_FEATURE (ARM_AEXT_V1, 0)
#define ARM_ARCH_V2	ARM_FEATURE (ARM_AEXT_V2, 0)
#define ARM_ARCH_V2S	ARM_FEATURE (ARM_AEXT_V2S, 0)
#define ARM_ARCH_V3	ARM_FEATURE (ARM_AEXT_V3, 0)
#define ARM_ARCH_V3M	ARM_FEATURE (ARM_AEXT_V3M, 0)
#define ARM_ARCH_V4xM	ARM_FEATURE (ARM_AEXT_V4xM, 0)
#define ARM_ARCH_V4	ARM_FEATURE (ARM_AEXT_V4, 0)
#define ARM_ARCH_V4TxM	ARM_FEATURE (ARM_AEXT_V4TxM, 0)
#define ARM_ARCH_V4T	ARM_FEATURE (ARM_AEXT_V4T, 0)
#define ARM_ARCH_V5xM	ARM_FEATURE (ARM_AEXT_V5xM, 0)
#define ARM_ARCH_V5	ARM_FEATURE (ARM_AEXT_V5, 0)
#define ARM_ARCH_V5TxM	ARM_FEATURE (ARM_AEXT_V5TxM, 0)
#define ARM_ARCH_V5T	ARM_FEATURE (ARM_AEXT_V5T, 0)
#define ARM_ARCH_V5TExP	ARM_FEATURE (ARM_AEXT_V5TExP, 0)
#define ARM_ARCH_V5TE	ARM_FEATURE (ARM_AEXT_V5TE, 0)
#define ARM_ARCH_V5TEJ	ARM_FEATURE (ARM_AEXT_V5TEJ, 0)
#define ARM_ARCH_V6	ARM_FEATURE (ARM_AEXT_V6, 0)
#define ARM_ARCH_V6K	ARM_FEATURE (ARM_AEXT_V6K, 0)
#define ARM_ARCH_V6Z	ARM_FEATURE (ARM_AEXT_V6Z, 0)
#define ARM_ARCH_V6ZK	ARM_FEATURE (ARM_AEXT_V6ZK, 0)
#define ARM_ARCH_V6T2	ARM_FEATURE (ARM_AEXT_V6T2, 0)
#define ARM_ARCH_V6KT2	ARM_FEATURE (ARM_AEXT_V6KT2, 0)
#define ARM_ARCH_V6ZT2	ARM_FEATURE (ARM_AEXT_V6ZT2, 0)
#define ARM_ARCH_V6ZKT2	ARM_FEATURE (ARM_AEXT_V6ZKT2, 0)
#define ARM_ARCH_V7	ARM_FEATURE (ARM_AEXT_V7, 0)
#define ARM_ARCH_V7A	ARM_FEATURE (ARM_AEXT_V7A, 0)
#define ARM_ARCH_V7R	ARM_FEATURE (ARM_AEXT_V7R, 0)
#define ARM_ARCH_V7M	ARM_FEATURE (ARM_AEXT_V7M, 0)

/* Some useful combinations:  */
#define ARM_ARCH_NONE	ARM_FEATURE (0, 0)
#define FPU_NONE	ARM_FEATURE (0, 0)
#define ARM_ANY		ARM_FEATURE (-1, 0)	/* Any basic core.  */
#define FPU_ANY_HARD	ARM_FEATURE (0, FPU_FPA | FPU_VFP_HARD | FPU_MAVERICK)
#define ARM_ARCH_THUMB2 ARM_FEATURE (ARM_EXT_V6T2 | ARM_EXT_V7 | ARM_EXT_V7A | ARM_EXT_V7R | ARM_EXT_V7M | ARM_EXT_DIV, 0)

/* There are too many feature bits to fit in a single word, so use a
   structure.  For simplicity we put all core features in one word and
   everything else in the other.  */
typedef struct
{
  unsigned long core;
  unsigned long coproc;
} arm_feature_set;

#define ARM_CPU_HAS_FEATURE(CPU,FEAT) \
  (((CPU).core & (FEAT).core) != 0 || ((CPU).coproc & (FEAT).coproc) != 0)

#define ARM_MERGE_FEATURE_SETS(TARG,F1,F2)	\
  do {						\
    (TARG).core = (F1).core | (F2).core;	\
    (TARG).coproc = (F1).coproc | (F2).coproc;	\
  } while (0)

#define ARM_CLEAR_FEATURE(TARG,F1,F2)		\
  do {						\
    (TARG).core = (F1).core &~ (F2).core;	\
    (TARG).coproc = (F1).coproc &~ (F2).coproc;	\
  } while (0)

#define ARM_FEATURE(core, coproc) {(core), (coproc)}
