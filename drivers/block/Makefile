#
# Makefile for the kernel block device drivers.
#
# 12 June 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
# 

obj-$(CONFIG_MAC_FLOPPY)	+= swim3.o
obj-$(CONFIG_BLK_DEV_FD)	+= floppy.o
obj-$(CONFIG_AMIGA_FLOPPY)	+= amiflop.o
obj-$(CONFIG_ATARI_FLOPPY)	+= ataflop.o
obj-$(CONFIG_AMIGA_Z2RAM)	+= z2ram.o
obj-$(CONFIG_BLK_DEV_RAM)	+= rd.o
obj-$(CONFIG_BLK_DEV_LOOP)	+= loop.o
obj-$(CONFIG_BLK_DEV_PS2)	+= ps2esdi.o
obj-$(CONFIG_BLK_DEV_XD)	+= xd.o
obj-$(CONFIG_BLK_CPQ_DA)	+= cpqarray.o
obj-$(CONFIG_BLK_CPQ_CISS_DA)  += cciss.o
obj-$(CONFIG_BLK_DEV_DAC960)	+= DAC960.o
obj-$(CONFIG_CDROM_PKTCDVD)	+= pktcdvd.o
obj-$(CONFIG_SUNVDC)		+= sunvdc.o

obj-$(CONFIG_BLK_DEV_UMEM)	+= umem.o
obj-$(CONFIG_BLK_DEV_NBD)	+= nbd.o
obj-$(CONFIG_BLK_DEV_CRYPTOLOOP) += cryptoloop.o

obj-$(CONFIG_VIODASD)		+= viodasd.o
obj-$(CONFIG_BLK_DEV_SX8)	+= sx8.o
obj-$(CONFIG_BLK_DEV_UB)	+= ub.o

