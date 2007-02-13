# Fusion MPT drivers; recognized debug defines...
#  MPT general:
#EXTRA_CFLAGS += -DMPT_DEBUG
#EXTRA_CFLAGS += -DMPT_DEBUG_MSG_FRAME
#EXTRA_CFLAGS += -DMPT_DEBUG_SG
#EXTRA_CFLAGS += -DMPT_DEBUG_EVENTS
#EXTRA_CFLAGS += -DMPT_DEBUG_VERBOSE_EVENTS
#EXTRA_CFLAGS += -DMPT_DEBUG_INIT
#EXTRA_CFLAGS += -DMPT_DEBUG_EXIT
#EXTRA_CFLAGS += -DMPT_DEBUG_FAIL
#EXTRA_CFLAGS += -DMPT_DEBUG_DV
#EXTRA_CFLAGS += -DMPT_DEBUG_TM
#EXTRA_CFLAGS += -DMPT_DEBUG_REPLY

#
# driver/module specifics...
#
#  For mptbase:
#CFLAGS_mptbase.o += -DMPT_DEBUG_HANDSHAKE
#CFLAGS_mptbase.o += -DMPT_DEBUG_CONFIG
#CFLAGS_mptbase.o += -DMPT_DEBUG_DL
#CFLAGS_mptbase.o += -DMPT_DEBUG_IRQ
#CFLAGS_mptbase.o += -DMPT_DEBUG_RESET
#
#  For mptscsih:
#CFLAGS_mptscsih.o += -DMPT_DEBUG_SCSI
#
#  For mptctl:
#CFLAGS_mptctl.o += -DMPT_DEBUG_IOCTL
#
#  For mptfc:
#CFLAGS_mptfc.o += -DMPT_DEBUG_FC

#  For mptsas:
#CFLAGS_mptsas.o += -DMPT_DEBUG_SAS
#CFLAGS_mptsas.o += -DMPT_DEBUG_SAS_WIDE


#=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-} LSI_LOGIC

obj-$(CONFIG_FUSION_SPI)	+= mptbase.o mptscsih.o mptspi.o
obj-$(CONFIG_FUSION_FC)		+= mptbase.o mptscsih.o mptfc.o
obj-$(CONFIG_FUSION_SAS)	+= mptbase.o mptscsih.o mptsas.o
obj-$(CONFIG_FUSION_CTL)	+= mptctl.o
obj-$(CONFIG_FUSION_LAN)	+= mptlan.o
