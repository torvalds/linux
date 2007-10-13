#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2000, 2001  Paolo Alberelli
# Copyright (C) 2003, 2004  Paul Mundt
#
# Makefile for the sh64-specific parts of the Linux memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

obj-y := cache.o consistent.o extable.o fault.o init.o ioremap.o \
	 tlbmiss.o tlb.o

obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o

# Special flags for tlbmiss.o.  This puts restrictions on the number of
# caller-save registers that the compiler can target when building this file.
# This is required because the code is called from a context in entry.S where
# very few registers have been saved in the exception handler (for speed
# reasons).
# The caller save registers that have been saved and which can be used are
# r2,r3,r4,r5 : argument passing
# r15, r18 : SP and LINK
# tr0-4 : allow all caller-save TR's.  The compiler seems to be able to make
#         use of them, so it's probably beneficial to performance to save them
#         and have them available for it.
#
# The resources not listed below are callee save, i.e. the compiler is free to
# use any of them and will spill them to the stack itself.

CFLAGS_tlbmiss.o += -ffixed-r7 \
	-ffixed-r8 -ffixed-r9 -ffixed-r10 -ffixed-r11 -ffixed-r12 \
	-ffixed-r13 -ffixed-r14 -ffixed-r16 -ffixed-r17 -ffixed-r19 \
	-ffixed-r20 -ffixed-r21 -ffixed-r22 -ffixed-r23 \
	-ffixed-r24 -ffixed-r25 -ffixed-r26 -ffixed-r27 \
	-ffixed-r36 -ffixed-r37 -ffixed-r38 -ffixed-r39 -ffixed-r40 \
	-ffixed-r41 -ffixed-r42 -ffixed-r43  \
	-ffixed-r60 -ffixed-r61 -ffixed-r62 \
	-fomit-frame-pointer
