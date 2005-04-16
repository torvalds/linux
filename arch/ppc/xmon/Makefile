# Makefile for xmon

ifdef CONFIG_8xx
obj-y		:= start_8xx.o
else
obj-y		:= start.o
endif
obj-y		+= xmon.o ppc-dis.o ppc-opc.o subr_prf.o setjmp.o
