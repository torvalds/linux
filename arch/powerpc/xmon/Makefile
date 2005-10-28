# Makefile for xmon

ifdef CONFIG_PPC64
EXTRA_CFLAGS += -mno-minimal-toc
endif

obj-$(CONFIG_8xx)	+= start_8xx.o
obj-$(CONFIG_6xx)	+= start_32.o
obj-$(CONFIG_4xx)	+= start_32.o
obj-$(CONFIG_PPC64)	+= start_64.o
obj-y			+= xmon.o ppc-dis.o ppc-opc.o subr_prf.o setjmp.o
