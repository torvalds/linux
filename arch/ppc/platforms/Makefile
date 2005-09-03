#
# Makefile for the linux kernel.
#

# Extra CFLAGS so we don't have to do relative includes
CFLAGS_pmac_setup.o	+= -Iarch/$(ARCH)/mm

obj-$(CONFIG_APUS)		+= apus_setup.o
ifeq ($(CONFIG_APUS),y)
obj-$(CONFIG_PCI)		+= apus_pci.o
endif
obj-$(CONFIG_PPC_PMAC)		+= pmac_pic.o pmac_setup.o pmac_time.o \
					pmac_feature.o pmac_pci.o pmac_sleep.o \
					pmac_low_i2c.o pmac_cache.o
obj-$(CONFIG_PPC_CHRP)		+= chrp_setup.o chrp_time.o chrp_pci.o \
					chrp_pegasos_eth.o
obj-$(CONFIG_PPC_PREP)		+= prep_pci.o prep_setup.o
ifeq ($(CONFIG_PPC_PMAC),y)
obj-$(CONFIG_NVRAM)		+= pmac_nvram.o
obj-$(CONFIG_CPU_FREQ_PMAC)	+= pmac_cpufreq.o
endif
obj-$(CONFIG_PMAC_BACKLIGHT)	+= pmac_backlight.o
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

ifeq ($(CONFIG_SMP),y)
obj-$(CONFIG_PPC_PMAC)		+= pmac_smp.o
obj-$(CONFIG_PPC_CHRP)		+= chrp_smp.o
endif
