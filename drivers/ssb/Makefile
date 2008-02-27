# core
ssb-y					+= main.o scan.o
ssb-$(CONFIG_SSB_EMBEDDED)		+= embedded.o

# host support
ssb-$(CONFIG_SSB_PCIHOST)		+= pci.o pcihost_wrapper.o
ssb-$(CONFIG_SSB_PCMCIAHOST)		+= pcmcia.o

# built-in drivers
ssb-y					+= driver_chipcommon.o
ssb-$(CONFIG_SSB_DRIVER_MIPS)		+= driver_mipscore.o
ssb-$(CONFIG_SSB_DRIVER_EXTIF)		+= driver_extif.o
ssb-$(CONFIG_SSB_DRIVER_PCICORE)	+= driver_pcicore.o

# b43 pci-ssb-bridge driver
# Not strictly a part of SSB, but kept here for convenience
ssb-$(CONFIG_SSB_PCIHOST)		+= b43_pci_bridge.o

obj-$(CONFIG_SSB)			+= ssb.o
