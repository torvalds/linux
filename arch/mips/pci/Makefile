#
# Makefile for the PCI specific kernel interface routines under Linux.
#

obj-y				+= pci.o pci-dac.o

#
# PCI bus host bridge specific code
#
obj-$(CONFIG_MIPS_BONITO64)	+= ops-bonito64.o
obj-$(CONFIG_PCI_GT64XXX_PCI0)	+= ops-gt64xxx_pci0.o
obj-$(CONFIG_MIPS_MSC)		+= ops-msc.o
obj-$(CONFIG_MIPS_TX3927)	+= ops-tx3927.o
obj-$(CONFIG_PCI_VR41XX)	+= ops-vr41xx.o pci-vr41xx.o
obj-$(CONFIG_NEC_CMBVR4133)	+= fixup-vr4133.o
obj-$(CONFIG_MARKEINS)		+= ops-emma2rh.o pci-emma2rh.o fixup-emma2rh.o

#
# These are still pretty much in the old state, watch, go blind.
#
obj-$(CONFIG_BASLER_EXCITE)	+= ops-titan.o pci-excite.o fixup-excite.o
obj-$(CONFIG_DDB5477)		+= fixup-ddb5477.o pci-ddb5477.o ops-ddb5477.o
obj-$(CONFIG_MIPS_ATLAS)	+= fixup-atlas.o
obj-$(CONFIG_MIPS_COBALT)	+= fixup-cobalt.o
obj-$(CONFIG_SOC_AU1500)	+= fixup-au1000.o ops-au1000.o
obj-$(CONFIG_SOC_AU1550)	+= fixup-au1000.o ops-au1000.o
obj-$(CONFIG_SOC_PNX8550)	+= fixup-pnx8550.o ops-pnx8550.o
obj-$(CONFIG_LEMOTE_FULONG)	+= fixup-lm2e.o ops-bonito64.o
obj-$(CONFIG_MIPS_MALTA)	+= fixup-malta.o
obj-$(CONFIG_MOMENCO_OCELOT)	+= fixup-ocelot.o pci-ocelot.o
obj-$(CONFIG_PMC_MSP7120_GW)	+= fixup-pmcmsp.o ops-pmcmsp.o
obj-$(CONFIG_PMC_MSP7120_EVAL)	+= fixup-pmcmsp.o ops-pmcmsp.o
obj-$(CONFIG_PMC_MSP7120_FPGA)	+= fixup-pmcmsp.o ops-pmcmsp.o
obj-$(CONFIG_PMC_YOSEMITE)	+= fixup-yosemite.o ops-titan.o ops-titan-ht.o \
				   pci-yosemite.o
obj-$(CONFIG_SGI_IP27)		+= ops-bridge.o pci-ip27.o
obj-$(CONFIG_SGI_IP32)		+= fixup-ip32.o ops-mace.o pci-ip32.o
obj-$(CONFIG_SIBYTE_SB1250)	+= fixup-sb1250.o pci-sb1250.o
obj-$(CONFIG_SIBYTE_BCM112X)	+= fixup-sb1250.o pci-sb1250.o
obj-$(CONFIG_SIBYTE_BCM1x80)	+= pci-bcm1480.o pci-bcm1480ht.o
obj-$(CONFIG_SNI_RM)		+= fixup-sni.o ops-sni.o
obj-$(CONFIG_TANBAC_TB0219)	+= fixup-tb0219.o
obj-$(CONFIG_TANBAC_TB0226)	+= fixup-tb0226.o
obj-$(CONFIG_TANBAC_TB0287)	+= fixup-tb0287.o
obj-$(CONFIG_TOSHIBA_JMR3927)	+= fixup-jmr3927.o pci-jmr3927.o
obj-$(CONFIG_TOSHIBA_RBTX4927)	+= fixup-rbtx4927.o ops-tx4927.o
obj-$(CONFIG_TOSHIBA_RBTX4938)	+= fixup-tx4938.o ops-tx4938.o
obj-$(CONFIG_VICTOR_MPC30X)	+= fixup-mpc30x.o
obj-$(CONFIG_ZAO_CAPCELLA)	+= fixup-capcella.o
obj-$(CONFIG_WR_PPMC)		+= fixup-wrppmc.o
