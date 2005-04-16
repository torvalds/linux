#
# Makefile for the kernel block device drivers.
#
# 12 June 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
# 
# Note : at this point, these files are compiled on all systems.
# In the future, some of these should be built conditionally.
#

#
# NOTE that ll_rw_blk.c must come early in linkage order - it starts the
# kblockd threads
#

obj-y	:= elevator.o ll_rw_blk.o ioctl.o genhd.o scsi_ioctl.o

obj-$(CONFIG_IOSCHED_NOOP)	+= noop-iosched.o
obj-$(CONFIG_IOSCHED_AS)	+= as-iosched.o
obj-$(CONFIG_IOSCHED_DEADLINE)	+= deadline-iosched.o
obj-$(CONFIG_IOSCHED_CFQ)	+= cfq-iosched.o
obj-$(CONFIG_MAC_FLOPPY)	+= swim3.o
obj-$(CONFIG_BLK_DEV_FD)	+= floppy.o
obj-$(CONFIG_BLK_DEV_FD98)	+= floppy98.o
obj-$(CONFIG_AMIGA_FLOPPY)	+= amiflop.o
obj-$(CONFIG_ATARI_FLOPPY)	+= ataflop.o
obj-$(CONFIG_BLK_DEV_SWIM_IOP)	+= swim_iop.o
obj-$(CONFIG_ATARI_ACSI)	+= acsi.o
obj-$(CONFIG_ATARI_SLM)		+= acsi_slm.o
obj-$(CONFIG_AMIGA_Z2RAM)	+= z2ram.o
obj-$(CONFIG_BLK_DEV_RAM)	+= rd.o
obj-$(CONFIG_BLK_DEV_LOOP)	+= loop.o
obj-$(CONFIG_BLK_DEV_PS2)	+= ps2esdi.o
obj-$(CONFIG_BLK_DEV_XD)	+= xd.o
obj-$(CONFIG_BLK_CPQ_DA)	+= cpqarray.o
obj-$(CONFIG_BLK_CPQ_CISS_DA)  += cciss.o
obj-$(CONFIG_BLK_DEV_DAC960)	+= DAC960.o
obj-$(CONFIG_CDROM_PKTCDVD)	+= pktcdvd.o

obj-$(CONFIG_BLK_DEV_UMEM)	+= umem.o
obj-$(CONFIG_BLK_DEV_NBD)	+= nbd.o
obj-$(CONFIG_BLK_DEV_CRYPTOLOOP) += cryptoloop.o

obj-$(CONFIG_VIODASD)		+= viodasd.o
obj-$(CONFIG_BLK_DEV_SX8)	+= sx8.o
obj-$(CONFIG_BLK_DEV_UB)	+= ub.o

