#
# Makefile for the R7780RP-1 specific parts of the kernel
#
irqinit-y			:= irq-r7780rp.o
irqinit-$(CONFIG_SH_R7785RP)	:= irq-r7785rp.o
obj-y				:= setup.o irq.o $(irqinit-y)

ifneq ($(CONFIG_SH_R7785RP),y)
obj-$(CONFIG_PUSH_SWITCH)	+= psw.o
endif
