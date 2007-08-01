# Fusion MPT drivers; recognized debug defines...

# enable verbose logging
# CONFIG_FUSION_LOGGING needs to be enabled in Kconfig
#EXTRA_CFLAGS += -DMPT_DEBUG_VERBOSE


#=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-} LSI_LOGIC

obj-$(CONFIG_FUSION_SPI)	+= mptbase.o mptscsih.o mptspi.o
obj-$(CONFIG_FUSION_FC)		+= mptbase.o mptscsih.o mptfc.o
obj-$(CONFIG_FUSION_SAS)	+= mptbase.o mptscsih.o mptsas.o
obj-$(CONFIG_FUSION_CTL)	+= mptctl.o
obj-$(CONFIG_FUSION_LAN)	+= mptlan.o
