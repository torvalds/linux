# SPDX-License-Identifier: GPL-2.0
# core
ssb-y					+= main.o scan.o
ssb-$(CONFIG_SSB_EMBEDDED)		+= embedded.o
ssb-$(CONFIG_SSB_SPROM)			+= sprom.o

# host support
ssb-$(CONFIG_SSB_PCIHOST)		+= pci.o pcihost_wrapper.o
ssb-$(CONFIG_SSB_PCMCIAHOST)		+= pcmcia.o bridge_pcmcia_80211.o
ssb-$(CONFIG_SSB_SDIOHOST)		+= sdio.o
ssb-$(CONFIG_SSB_HOST_SOC)		+= host_soc.o

# built-in drivers
ssb-y					+= driver_chipcommon.o
ssb-y					+= driver_chipcommon_pmu.o
ssb-$(CONFIG_SSB_SFLASH)		+= driver_chipcommon_sflash.o
ssb-$(CONFIG_SSB_DRIVER_MIPS)		+= driver_mipscore.o
ssb-$(CONFIG_SSB_DRIVER_EXTIF)		+= driver_extif.o
ssb-$(CONFIG_SSB_DRIVER_PCICORE)	+= driver_pcicore.o
ssb-$(CONFIG_SSB_DRIVER_GIGE)		+= driver_gige.o
ssb-$(CONFIG_SSB_DRIVER_GPIO)		+= driver_gpio.o

# b43 pci-ssb-bridge driver
# Not strictly a part of SSB, but kept here for convenience
ssb-$(CONFIG_SSB_B43_PCI_BRIDGE)	+= b43_pci_bridge.o

obj-$(CONFIG_SSB)			+= ssb.o
