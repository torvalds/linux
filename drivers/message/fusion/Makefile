#
# Makefile for the LSI Logic Fusion MPT (Message Passing Technology) drivers.
#
# Note! If you want to turn on various debug defines for an extended period of
# time but don't want them lingering around in the Makefile when you pass it on
# to someone else, use the MPT_CFLAGS env variable (thanks Steve). -nromer

#=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-{ LSI_LOGIC

#  Architecture-specific...
#			# intel
#EXTRA_CFLAGS += -g
#			# sparc64
#EXTRA_CFLAGS += -gstabs+

EXTRA_CFLAGS += ${MPT_CFLAGS}

# Fusion MPT drivers; recognized debug defines...
#  MPT general:
#EXTRA_CFLAGS += -DMPT_DEBUG_SCSI
#EXTRA_CFLAGS += -DMPT_DEBUG
#EXTRA_CFLAGS += -DMPT_DEBUG_MSG_FRAME
#EXTRA_CFLAGS += -DMPT_DEBUG_SG

#
# driver/module specifics...
#
#  For mptbase:
#CFLAGS_mptbase.o += -DMPT_DEBUG_HANDSHAKE
#CFLAGS_mptbase.o += -DMPT_DEBUG_IRQ
#
#  For mptscsih:
#CFLAGS_mptscsih.o += -DMPT_DEBUG_SCANDV
#CFLAGS_mptscsih.o += -DMPT_DEBUG_RESET
#CFLAGS_mptscsih.o += -DMPT_DEBUG_NEH
#
#  For mptctl:
#CFLAGS_mptctl.o += -DMPT_DEBUG_IOCTL
#
#  For mptlan:
#CFLAGS_mptlan.o += -DMPT_LAN_IO_DEBUG
#
#  For isense:

#  EXP...
##mptscsih-objs	:= scsihost.o scsiherr.o

#=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-} LSI_LOGIC

obj-$(CONFIG_FUSION)		+= mptbase.o mptscsih.o
obj-$(CONFIG_FUSION_CTL)	+= mptctl.o
obj-$(CONFIG_FUSION_LAN)	+= mptlan.o
