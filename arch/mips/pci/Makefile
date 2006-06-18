#
# Makefile for the PCI specific kernel interface routines under Linux.
#

obj-y				+= pci.o

#
# PCI bus host bridge specific code
#
obj-$(CONFIG_ITE_BOARD_GEN)	+= ops-it8172.o
obj-$(CONFIG_MIPS_BONITO64)	+= ops-bonito64.o
obj-$(CONFIG_MIPS_GT64111)	+= ops-gt64111.o
obj-$(CONFIG_MIPS_GT64120)	+= ops-gt64120.o
obj-$(CONFIG_MIPS_GT96100)	+= ops-gt96100.o
obj-$(CONFIG_PCI_MARVELL)	+= ops-marvell.o
obj-$(CONFIG_MIPS_MSC)		+= ops-msc.o
obj-$(CONFIG_MIPS_NILE4)	+= ops-nile4.o
obj-$(CONFIG_MIPS_TX3927)	+= ops-tx3927.o
obj-$(CONFIG_PCI_VR41XX)	+= ops-vr41xx.o pci-vr41xx.o
obj-$(CONFIG_NEC_CMBVR4133)	+= fixup-vr4133.o

#
# These are still pretty much in the old state, watch, go blind.
#
obj-$(CONFIG_DDB5477)		+= fixup-ddb5477.o pci-ddb5477.o ops-ddb5477.o
obj-$(CONFIG_LASAT)		+= pci-lasat.o
obj-$(CONFIG_MIPS_ATLAS)	+= fixup-atlas.o
obj-$(CONFIG_MIPS_COBALT)	+= fixup-cobalt.o
obj-$(CONFIG_MIPS_EV96100)	+= fixup-ev64120.o
obj-$(CONFIG_MIPS_EV96100)	+= fixup-ev96100.o pci-ev96100.o
obj-$(CONFIG_MIPS_ITE8172)	+= fixup-ite8172g.o
obj-$(CONFIG_MIPS_IVR)		+= fixup-ivr.o
obj-$(CONFIG_SOC_AU1500)	+= fixup-au1000.o ops-au1000.o
obj-$(CONFIG_SOC_AU1550)	+= fixup-au1000.o ops-au1000.o
obj-$(CONFIG_SOC_PNX8550)	+= fixup-pnx8550.o ops-pnx8550.o
obj-$(CONFIG_MIPS_MALTA)	+= fixup-malta.o
obj-$(CONFIG_MOMENCO_JAGUAR_ATX)+= fixup-jaguar.o
obj-$(CONFIG_MOMENCO_OCELOT)	+= fixup-ocelot.o pci-ocelot.o
obj-$(CONFIG_MOMENCO_OCELOT_3)	+= fixup-ocelot3.o
obj-$(CONFIG_MOMENCO_OCELOT_C)	+= fixup-ocelot-c.o pci-ocelot-c.o
obj-$(CONFIG_MOMENCO_OCELOT_G)	+= fixup-ocelot-g.o pci-ocelot-g.o
obj-$(CONFIG_PMC_YOSEMITE)	+= fixup-yosemite.o ops-titan.o ops-titan-ht.o \
				   pci-yosemite.o
obj-$(CONFIG_SGI_IP27)		+= ops-bridge.o pci-ip27.o
obj-$(CONFIG_SGI_IP32)		+= fixup-ip32.o ops-mace.o pci-ip32.o
obj-$(CONFIG_SIBYTE_SB1250)	+= fixup-sb1250.o pci-sb1250.o
obj-$(CONFIG_SIBYTE_BCM112X)	+= fixup-sb1250.o pci-sb1250.o
obj-$(CONFIG_SIBYTE_BCM1x80)	+= pci-bcm1480.o pci-bcm1480ht.o
obj-$(CONFIG_SNI_RM200_PCI)	+= fixup-sni.o ops-sni.o
obj-$(CONFIG_TANBAC_TB0219)	+= fixup-tb0219.o
obj-$(CONFIG_TANBAC_TB0226)	+= fixup-tb0226.o
obj-$(CONFIG_TANBAC_TB0287)	+= fixup-tb0287.o
obj-$(CONFIG_TOSHIBA_JMR3927)	+= fixup-jmr3927.o pci-jmr3927.o
obj-$(CONFIG_TOSHIBA_RBTX4927)	+= fixup-rbtx4927.o ops-tx4927.o
obj-$(CONFIG_TOSHIBA_RBTX4938)	+= fixup-tx4938.o ops-tx4938.o
obj-$(CONFIG_VICTOR_MPC30X)	+= fixup-mpc30x.o
obj-$(CONFIG_ZAO_CAPCELLA)	+= fixup-capcella.o
obj-$(CONFIG_WR_PPMC)		+= fixup-wrppmc.o
