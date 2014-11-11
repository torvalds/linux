#
# Makefile for MMC/SD host controller drivers
#

ifeq ($(CONFIG_MMC_DEBUG),y)
	EXTRA_CFLAGS		+= -DDEBUG
endif

ifeq ($(CONFIG_ARCH_MESON8),y)
	obj-$(CONFIG_MMC_AML) += aml_sdhc_m8.o
endif

ifeq ($(CONFIG_ARCH_MESON8B),y)
	obj-$(CONFIG_MMC_AML) += aml_sdhc_m8.o
endif

# ifeq ($(CONFIG_ARCH_MESON6),y)
# obj-$(CONFIG_MMC_AML) += aml_sdhc.o
# endif

obj-$(CONFIG_MMC_AML)   += aml_sdio.o amlsd.o amlsd_of.o
