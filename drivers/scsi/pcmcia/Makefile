# SPDX-License-Identifier: GPL-2.0

ccflags-y		:= -Idrivers/scsi

# 16-bit client drivers
obj-$(CONFIG_PCMCIA_QLOGIC)	+= qlogic_cs.o
obj-$(CONFIG_PCMCIA_FDOMAIN)	+= fdomain_cs.o
obj-$(CONFIG_PCMCIA_AHA152X)	+= aha152x_cs.o
obj-$(CONFIG_PCMCIA_NINJA_SCSI)	+= nsp_cs.o
obj-$(CONFIG_PCMCIA_SYM53C500)	+= sym53c500_cs.o

aha152x_cs-objs	:= aha152x_stub.o aha152x_core.o
fdomain_cs-objs	:= fdomain_stub.o fdomain_core.o
qlogic_cs-objs	:= qlogic_stub.o
