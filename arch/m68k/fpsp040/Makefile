#
# Makefile for Linux arch/m68k/fpsp040 source directory
#

obj-y    := bindec.o binstr.o decbin.o do_func.o gen_except.o get_op.o \
	    kernel_ex.o res_func.o round.o sacos.o sasin.o satan.o satanh.o \
	    scosh.o setox.o sgetem.o sint.o slog2.o slogn.o \
	    smovecr.o srem_mod.o scale.o \
	    ssin.o ssinh.o stan.o stanh.o sto_res.o stwotox.o tbldo.o util.o \
	    x_bsun.o x_fline.o x_operr.o x_ovfl.o x_snan.o x_store.o \
	    x_unfl.o x_unimp.o x_unsupp.o bugfix.o skeleton.o

EXTRA_AFLAGS := -traditional
EXTRA_LDFLAGS := -x

$(OS_OBJS): fpsp.h
