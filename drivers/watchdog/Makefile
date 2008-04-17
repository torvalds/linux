#
# Makefile for the WatchDog device drivers.
#

# Only one watchdog can succeed. We probe the ISA/PCI/USB based
# watchdog-cards first, then the architecture specific watchdog
# drivers and then the architecture independant "softdog" driver.
# This means that if your ISA/PCI/USB card isn't detected that
# you can fall back to an architecture specific driver and if
# that also fails then you can fall back to the software watchdog
# to give you some cover.

# ISA-based Watchdog Cards
obj-$(CONFIG_PCWATCHDOG) += pcwd.o
obj-$(CONFIG_MIXCOMWD) += mixcomwd.o
obj-$(CONFIG_WDT) += wdt.o

# PCI-based Watchdog Cards
obj-$(CONFIG_PCIPCWATCHDOG) += pcwd_pci.o
obj-$(CONFIG_WDTPCI) += wdt_pci.o

# USB-based Watchdog Cards
obj-$(CONFIG_USBPCWATCHDOG) += pcwd_usb.o

# ALPHA Architecture

# ARM Architecture
obj-$(CONFIG_AT91RM9200_WATCHDOG) += at91rm9200_wdt.o
obj-$(CONFIG_OMAP_WATCHDOG) += omap_wdt.o
obj-$(CONFIG_21285_WATCHDOG) += wdt285.o
obj-$(CONFIG_977_WATCHDOG) += wdt977.o
obj-$(CONFIG_IXP2000_WATCHDOG) += ixp2000_wdt.o
obj-$(CONFIG_IXP4XX_WATCHDOG) += ixp4xx_wdt.o
obj-$(CONFIG_KS8695_WATCHDOG) += ks8695_wdt.o
obj-$(CONFIG_S3C2410_WATCHDOG) += s3c2410_wdt.o
obj-$(CONFIG_SA1100_WATCHDOG) += sa1100_wdt.o
obj-$(CONFIG_MPCORE_WATCHDOG) += mpcore_wdt.o
obj-$(CONFIG_EP93XX_WATCHDOG) += ep93xx_wdt.o
obj-$(CONFIG_PNX4008_WATCHDOG) += pnx4008_wdt.o
obj-$(CONFIG_IOP_WATCHDOG) += iop_wdt.o
obj-$(CONFIG_DAVINCI_WATCHDOG) += davinci_wdt.o

# ARM26 Architecture

# AVR32 Architecture
obj-$(CONFIG_AT32AP700X_WDT) += at32ap700x_wdt.o

# BLACKFIN Architecture
obj-$(CONFIG_BFIN_WDT) += bfin_wdt.o

# CRIS Architecture

# FRV Architecture

# H8300 Architecture

# X86 (i386 + ia64 + x86_64) Architecture
obj-$(CONFIG_ACQUIRE_WDT) += acquirewdt.o
obj-$(CONFIG_ADVANTECH_WDT) += advantechwdt.o
obj-$(CONFIG_ALIM1535_WDT) += alim1535_wdt.o
obj-$(CONFIG_ALIM7101_WDT) += alim7101_wdt.o
obj-$(CONFIG_GEODE_WDT) += geodewdt.o
obj-$(CONFIG_SC520_WDT) += sc520_wdt.o
obj-$(CONFIG_EUROTECH_WDT) += eurotechwdt.o
obj-$(CONFIG_IB700_WDT) += ib700wdt.o
obj-$(CONFIG_IBMASR) += ibmasr.o
obj-$(CONFIG_WAFER_WDT) += wafer5823wdt.o
obj-$(CONFIG_I6300ESB_WDT) += i6300esb.o
obj-$(CONFIG_ITCO_WDT) += iTCO_wdt.o
ifeq ($(CONFIG_ITCO_VENDOR_SUPPORT),y)
obj-$(CONFIG_ITCO_WDT) += iTCO_vendor_support.o
endif
obj-$(CONFIG_IT8712F_WDT) += it8712f_wdt.o
obj-$(CONFIG_IT87_WDT) += it87_wdt.o
obj-$(CONFIG_HP_WATCHDOG) += hpwdt.o
obj-$(CONFIG_SC1200_WDT) += sc1200wdt.o
obj-$(CONFIG_SCx200_WDT) += scx200_wdt.o
obj-$(CONFIG_PC87413_WDT) += pc87413_wdt.o
obj-$(CONFIG_RDC321X_WDT) += rdc321x_wdt.o
obj-$(CONFIG_60XX_WDT) += sbc60xxwdt.o
obj-$(CONFIG_SBC8360_WDT) += sbc8360.o
obj-$(CONFIG_SBC7240_WDT) += sbc7240_wdt.o
obj-$(CONFIG_CPU5_WDT) += cpu5wdt.o
obj-$(CONFIG_SMSC37B787_WDT) += smsc37b787_wdt.o
obj-$(CONFIG_W83627HF_WDT) += w83627hf_wdt.o
obj-$(CONFIG_W83697HF_WDT) += w83697hf_wdt.o
obj-$(CONFIG_W83697UG_WDT) += w83697ug_wdt.o
obj-$(CONFIG_W83877F_WDT) += w83877f_wdt.o
obj-$(CONFIG_W83977F_WDT) += w83977f_wdt.o
obj-$(CONFIG_MACHZ_WDT) += machzwd.o
obj-$(CONFIG_SBC_EPX_C3_WATCHDOG) += sbc_epx_c3.o

# M32R Architecture

# M68K Architecture

# M68KNOMMU Architecture

# MIPS Architecture
obj-$(CONFIG_RC32434_WDT) += rc32434_wdt.o
obj-$(CONFIG_INDYDOG) += indydog.o
obj-$(CONFIG_WDT_MTX1) += mtx-1_wdt.o
obj-$(CONFIG_WDT_RM9K_GPI) += rm9k_wdt.o
obj-$(CONFIG_SIBYTE_WDOG) += sb_wdog.o
obj-$(CONFIG_AR7_WDT) += ar7_wdt.o
obj-$(CONFIG_TXX9_WDT) += txx9wdt.o

# PARISC Architecture

# POWERPC Architecture
obj-$(CONFIG_MPC5200_WDT) += mpc5200_wdt.o
obj-$(CONFIG_8xxx_WDT) += mpc8xxx_wdt.o
obj-$(CONFIG_MV64X60_WDT) += mv64x60_wdt.o
obj-$(CONFIG_BOOKE_WDT) += booke_wdt.o

# PPC64 Architecture
obj-$(CONFIG_WATCHDOG_RTAS) += wdrtas.o

# S390 Architecture

# SUPERH (sh + sh64) Architecture
obj-$(CONFIG_SH_WDT) += shwdt.o

# SPARC Architecture

# SPARC64 Architecture

# XTENSA Architecture

# Architecture Independant
obj-$(CONFIG_SOFT_WATCHDOG) += softdog.o
