#
# Makefile for the kernel block layer
#

obj-$(CONFIG_BLOCK) := bio.o elevator.o blk-core.o blk-tag.o blk-sysfs.o \
			blk-flush.o blk-settings.o blk-ioc.o blk-map.o \
			blk-exec.o blk-merge.o blk-softirq.o blk-timeout.o \
			blk-lib.o blk-mq.o blk-mq-tag.o \
			blk-mq-sysfs.o blk-mq-cpumap.o ioctl.o \
			genhd.o scsi_ioctl.o partition-generic.o ioprio.o \
			badblocks.o partitions/

obj-$(CONFIG_BOUNCE)	+= bounce.o
obj-$(CONFIG_BLK_DEV_BSG)	+= bsg.o
obj-$(CONFIG_BLK_DEV_BSGLIB)	+= bsg-lib.o
obj-$(CONFIG_BLK_CGROUP)	+= blk-cgroup.o
obj-$(CONFIG_BLK_DEV_THROTTLING)	+= blk-throttle.o
obj-$(CONFIG_IOSCHED_NOOP)	+= noop-iosched.o
obj-$(CONFIG_IOSCHED_DEADLINE)	+= deadline-iosched.o
obj-$(CONFIG_IOSCHED_CFQ)	+= cfq-iosched.o

obj-$(CONFIG_BLOCK_COMPAT)	+= compat_ioctl.o
obj-$(CONFIG_BLK_CMDLINE_PARSER)	+= cmdline-parser.o
obj-$(CONFIG_BLK_DEV_INTEGRITY) += bio-integrity.o blk-integrity.o t10-pi.o
obj-$(CONFIG_BLK_MQ_PCI)	+= blk-mq-pci.o
obj-$(CONFIG_BLK_DEV_ZONED)	+= blk-zoned.o
