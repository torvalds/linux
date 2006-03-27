#
# Makefile for the linux kernel.
#

obj-$(CONFIG_APUS)		+= apus_setup.o
ifeq ($(CONFIG_APUS),y)
obj-$(CONFIG_PCI)		+= apus_pci.o
endif
obj-$(CONFIG_PPC_PREP)		+= prep_pci.o prep_setup.o
obj-$(CONFIG_PREP_RESIDUAL)	+= residual.o
obj-$(CONFIG_PQ2ADS)		+= pq2ads.o
obj-$(CONFIG_TQM8260)		+= tqm8260_setup.o
obj-$(CONFIG_CPCI690)		+= cpci690.o
obj-$(CONFIG_EV64260)		+= ev64260.o
obj-$(CONFIG_CHESTNUT)		+= chestnut.o
obj-$(CONFIG_GEMINI)		+= gemini_pci.o gemini_setup.o gemini_prom.o
obj-$(CONFIG_LOPEC)		+= lopec.o
obj-$(CONFIG_KATANA)		+= katana.o
obj-$(CONFIG_HDPU)		+= hdpu.o
obj-$(CONFIG_MVME5100)		+= mvme5100.o
obj-$(CONFIG_PAL4)		+= pal4_setup.o pal4_pci.o
obj-$(CONFIG_POWERPMC250)	+= powerpmc250.o
obj-$(CONFIG_PPLUS)		+= pplus.o
obj-$(CONFIG_PRPMC750)		+= prpmc750.o
obj-$(CONFIG_PRPMC800)		+= prpmc800.o
obj-$(CONFIG_RADSTONE_PPC7D)	+= radstone_ppc7d.o
obj-$(CONFIG_SANDPOINT)		+= sandpoint.o
obj-$(CONFIG_SBC82xx)		+= sbc82xx.o
obj-$(CONFIG_SPRUCE)		+= spruce.o
obj-$(CONFIG_LITE5200)		+= lite5200.o
obj-$(CONFIG_EV64360)		+= ev64360.o
obj-$(CONFIG_MPC86XADS)		+= mpc866ads_setup.o
obj-$(CONFIG_MPC885ADS)		+= mpc885ads_setup.o
obj-$(CONFIG_ADS8272)		+= mpc8272ads_setup.o
