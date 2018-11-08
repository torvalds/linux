# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the kernel block layer
#

obj-$(CONFIG_BLOCK) := bio.o elevator.o blk-core.o blk-tag.o blk-sysfs.o \
			blk-flush.o blk-settings.o blk-ioc.o blk-map.o \
			blk-exec.o blk-merge.o blk-softirq.o blk-timeout.o \
			blk-lib.o blk-mq.o blk-mq-tag.o blk-stat.o \
			blk-mq-sysfs.o blk-mq-cpumap.o blk-mq-sched.o ioctl.o \
			genhd.o partition-generic.o ioprio.o \
			badblocks.o partitions/ blk-rq-qos.o

obj-$(CONFIG_BOUNCE)		+= bounce.o
obj-$(CONFIG_BLK_SCSI_REQUEST)	+= scsi_ioctl.o
obj-$(CONFIG_BLK_DEV_BSG)	+= bsg.o
obj-$(CONFIG_BLK_DEV_BSGLIB)	+= bsg-lib.o
obj-$(CONFIG_BLK_CGROUP)	+= blk-cgroup.o
obj-$(CONFIG_BLK_DEV_THROTTLING)	+= blk-throttle.o
obj-$(CONFIG_BLK_CGROUP_IOLATENCY)	+= blk-iolatency.o
obj-$(CONFIG_IOSCHED_NOOP)	+= noop-iosched.o
obj-$(CONFIG_IOSCHED_DEADLINE)	+= deadline-iosched.o
obj-$(CONFIG_IOSCHED_CFQ)	+= cfq-iosched.o
obj-$(CONFIG_MQ_IOSCHED_DEADLINE)	+= mq-deadline.o
obj-$(CONFIG_MQ_IOSCHED_KYBER)	+= kyber-iosched.o
bfq-y				:= bfq-iosched.o bfq-wf2q.o bfq-cgroup.o
obj-$(CONFIG_IOSCHED_BFQ)	+= bfq.o

obj-$(CONFIG_BLOCK_COMPAT)	+= compat_ioctl.o
obj-$(CONFIG_BLK_CMDLINE_PARSER)	+= cmdline-parser.o
obj-$(CONFIG_BLK_DEV_INTEGRITY) += bio-integrity.o blk-integrity.o t10-pi.o
obj-$(CONFIG_BLK_MQ_PCI)	+= blk-mq-pci.o
obj-$(CONFIG_BLK_MQ_VIRTIO)	+= blk-mq-virtio.o
obj-$(CONFIG_BLK_MQ_RDMA)	+= blk-mq-rdma.o
obj-$(CONFIG_BLK_DEV_ZONED)	+= blk-zoned.o
obj-$(CONFIG_BLK_WBT)		+= blk-wbt.o
obj-$(CONFIG_BLK_DEBUG_FS)	+= blk-mq-debugfs.o
obj-$(CONFIG_BLK_DEBUG_FS_ZONED)+= blk-mq-debugfs-zoned.o
obj-$(CONFIG_BLK_SED_OPAL)	+= sed-opal.o
obj-$(CONFIG_BLK_PM)		+= blk-pm.o
