#
# Makefile for the R7780RP-1 specific parts of the kernel
#
irqinit-$(CONFIG_SH_R7780MP)	:= irq-r7780mp.o
irqinit-$(CONFIG_SH_R7785RP)	:= irq-r7785rp.o
irqinit-$(CONFIG_SH_R7780RP)	:= irq-r7780rp.o
obj-y				:= setup.o $(irqinit-y)

ifneq ($(CONFIG_SH_R7785RP),y)
obj-$(CONFIG_PUSH_SWITCH)	+= psw.o
endif
