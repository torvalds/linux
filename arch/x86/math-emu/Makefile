#
#               Makefile for wm-FPU-emu
#

#DEBUG	= -DDEBUGGING
DEBUG	=
PARANOID = -DPARANOID
EXTRA_CFLAGS	:= $(PARANOID) $(DEBUG) -fno-builtin $(MATH_EMULATION)
EXTRA_AFLAGS	:= $(PARANOID)

# From 'C' language sources:
C_OBJS =fpu_entry.o errors.o \
	fpu_arith.o fpu_aux.o fpu_etc.o fpu_tags.o fpu_trig.o \
	load_store.o get_address.o \
	poly_atan.o poly_l2.o poly_2xm1.o poly_sin.o poly_tan.o \
	reg_add_sub.o reg_compare.o reg_constant.o reg_convert.o \
	reg_ld_str.o reg_divide.o reg_mul.o

# From 80x86 assembler sources:
A_OBJS =reg_u_add.o reg_u_div.o reg_u_mul.o reg_u_sub.o \
	div_small.o reg_norm.o reg_round.o \
	wm_shrx.o wm_sqrt.o \
	div_Xsig.o polynom_Xsig.o round_Xsig.o \
	shr_Xsig.o mul_Xsig.o

obj-y =$(C_OBJS) $(A_OBJS)

proto:
	cproto -e -DMAKING_PROTO *.c >fpu_proto.h
