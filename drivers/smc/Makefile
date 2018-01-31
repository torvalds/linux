# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux IrDA infrared port device drivers.
#
# 9 Aug 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
#

# FIR drivers
obj-$(CONFIG_USB_IRDA)		+= irda-usb.o
obj-$(CONFIG_SIGMATEL_FIR)	+= stir4200.o
obj-$(CONFIG_NSC_FIR)		+= nsc-ircc.o
obj-$(CONFIG_WINBOND_FIR)	+= w83977af_ir.o
obj-$(CONFIG_SA1100_FIR)	+= sa1100_ir.o
obj-$(CONFIG_TOSHIBA_FIR)	+= donauboe.o
obj-$(CONFIG_SMC_IRCC_FIR)	+= smsc-ircc2.o
obj-$(CONFIG_ALI_FIR)		+= ali-ircc.o
obj-$(CONFIG_VLSI_FIR)		+= vlsi_ir.o
obj-$(CONFIG_VIA_FIR)		+= via-ircc.o

obj-$(CONFIG_RK29_SMC)        += rk29_smc.o
