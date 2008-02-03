#
# Makefile for the kernel block layer
#

obj-$(CONFIG_BLOCK) := elevator.o blk-core.o blk-tag.o blk-sysfs.o \
			blk-barrier.o blk-settings.o blk-ioc.o blk-map.o \
			blk-exec.o blk-merge.o ioctl.o genhd.o scsi_ioctl.o

obj-$(CONFIG_BLK_DEV_BSG)	+= bsg.o
obj-$(CONFIG_IOSCHED_NOOP)	+= noop-iosched.o
obj-$(CONFIG_IOSCHED_AS)	+= as-iosched.o
obj-$(CONFIG_IOSCHED_DEADLINE)	+= deadline-iosched.o
obj-$(CONFIG_IOSCHED_CFQ)	+= cfq-iosched.o

obj-$(CONFIG_BLK_DEV_IO_TRACE)	+= blktrace.o
obj-$(CONFIG_BLOCK_COMPAT)	+= compat_ioctl.o
