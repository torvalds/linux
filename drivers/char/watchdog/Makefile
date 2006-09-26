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

# ARM Architecture
obj-$(CONFIG_AT91RM9200_WATCHDOG) += at91rm9200_wdt.o
obj-$(CONFIG_OMAP_WATCHDOG) += omap_wdt.o
obj-$(CONFIG_21285_WATCHDOG) += wdt285.o
obj-$(CONFIG_977_WATCHDOG) += wdt977.o
obj-$(CONFIG_IXP2000_WATCHDOG) += ixp2000_wdt.o
obj-$(CONFIG_IXP4XX_WATCHDOG) += ixp4xx_wdt.o
obj-$(CONFIG_S3C2410_WATCHDOG) += s3c2410_wdt.o
obj-$(CONFIG_SA1100_WATCHDOG) += sa1100_wdt.o
obj-$(CONFIG_MPCORE_WATCHDOG) += mpcore_wdt.o
obj-$(CONFIG_EP93XX_WATCHDOG) += ep93xx_wdt.o
obj-$(CONFIG_PNX4008_WATCHDOG) += pnx4008_wdt.o

# X86 (i386 + ia64 + x86_64) Architecture
obj-$(CONFIG_ACQUIRE_WDT) += acquirewdt.o
obj-$(CONFIG_ADVANTECH_WDT) += advantechwdt.o
obj-$(CONFIG_ALIM1535_WDT) += alim1535_wdt.o
obj-$(CONFIG_ALIM7101_WDT) += alim7101_wdt.o
obj-$(CONFIG_SC520_WDT) += sc520_wdt.o
obj-$(CONFIG_EUROTECH_WDT) += eurotechwdt.o
obj-$(CONFIG_IB700_WDT) += ib700wdt.o
obj-$(CONFIG_IBMASR) += ibmasr.o
obj-$(CONFIG_WAFER_WDT) += wafer5823wdt.o
obj-$(CONFIG_I6300ESB_WDT) += i6300esb.o
obj-$(CONFIG_I8XX_TCO) += i8xx_tco.o
obj-$(CONFIG_ITCO_WDT) += iTCO_wdt.o
obj-$(CONFIG_SC1200_WDT) += sc1200wdt.o
obj-$(CONFIG_SCx200_WDT) += scx200_wdt.o
obj-$(CONFIG_60XX_WDT) += sbc60xxwdt.o
obj-$(CONFIG_SBC8360_WDT) += sbc8360.o
obj-$(CONFIG_CPU5_WDT) += cpu5wdt.o
obj-$(CONFIG_SMSC37B787_WDT) += smsc37b787_wdt.o
obj-$(CONFIG_W83627HF_WDT) += w83627hf_wdt.o
obj-$(CONFIG_W83697HF_WDT) += w83697hf_wdt.o
obj-$(CONFIG_W83877F_WDT) += w83877f_wdt.o
obj-$(CONFIG_W83977F_WDT) += w83977f_wdt.o
obj-$(CONFIG_MACHZ_WDT) += machzwd.o
obj-$(CONFIG_SBC_EPX_C3_WATCHDOG) += sbc_epx_c3.o

# PowerPC Architecture
obj-$(CONFIG_8xx_WDT) += mpc8xx_wdt.o
obj-$(CONFIG_83xx_WDT) += mpc83xx_wdt.o
obj-$(CONFIG_MV64X60_WDT) += mv64x60_wdt.o
obj-$(CONFIG_BOOKE_WDT) += booke_wdt.o

# PPC64 Architecture
obj-$(CONFIG_WATCHDOG_RTAS) += wdrtas.o

# MIPS Architecture
obj-$(CONFIG_INDYDOG) += indydog.o

# S390 Architecture

# SUPERH Architecture
obj-$(CONFIG_SH_WDT) += shwdt.o

# SPARC64 Architecture

# Architecture Independant
obj-$(CONFIG_SOFT_WATCHDOG) += softdog.o
