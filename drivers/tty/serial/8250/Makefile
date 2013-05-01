#
# Makefile for the 8250 serial device drivers.
#

obj-$(CONFIG_SERIAL_8250)		+= 8250_core.o
8250_core-y				:= 8250.o
8250_core-$(CONFIG_SERIAL_8250_PNP)	+= 8250_pnp.o
8250_core-$(CONFIG_SERIAL_8250_DMA)	+= 8250_dma.o
obj-$(CONFIG_SERIAL_8250_GSC)		+= 8250_gsc.o
obj-$(CONFIG_SERIAL_8250_PCI)		+= 8250_pci.o
obj-$(CONFIG_SERIAL_8250_HP300)		+= 8250_hp300.o
obj-$(CONFIG_SERIAL_8250_CS)		+= serial_cs.o
obj-$(CONFIG_SERIAL_8250_ACORN)		+= 8250_acorn.o
obj-$(CONFIG_SERIAL_8250_CONSOLE)	+= 8250_early.o
obj-$(CONFIG_SERIAL_8250_FOURPORT)	+= 8250_fourport.o
obj-$(CONFIG_SERIAL_8250_ACCENT)	+= 8250_accent.o
obj-$(CONFIG_SERIAL_8250_BOCA)		+= 8250_boca.o
obj-$(CONFIG_SERIAL_8250_EXAR_ST16C554)	+= 8250_exar_st16c554.o
obj-$(CONFIG_SERIAL_8250_HUB6)		+= 8250_hub6.o
obj-$(CONFIG_SERIAL_8250_FSL)		+= 8250_fsl.o
obj-$(CONFIG_SERIAL_8250_DW)		+= 8250_dw.o
obj-$(CONFIG_SERIAL_8250_EM)		+= 8250_em.o
