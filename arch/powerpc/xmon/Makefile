# Makefile for xmon

ifdef CONFIG_PPC64
EXTRA_CFLAGS += -mno-minimal-toc
endif
obj-y			+= xmon.o ppc-dis.o ppc-opc.o setjmp.o start.o \
			   nonstdio.o
