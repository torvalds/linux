#
# Makefile for the kernel software RAID and LVM drivers.
#

dm-mod-y	+= dm.o dm-table.o dm-target.o dm-linear.o dm-stripe.o \
		   dm-ioctl.o dm-io.o dm-kcopyd.o dm-sysfs.o
dm-multipath-y	+= dm-path-selector.o dm-mpath.o
dm-snapshot-y	+= dm-snap.o dm-exception-store.o dm-snap-transient.o \
		    dm-snap-persistent.o
dm-mirror-y	+= dm-raid1.o
dm-log-userspace-y \
		+= dm-log-userspace-base.o dm-log-userspace-transfer.o
md-mod-y	+= md.o bitmap.o
raid456-y	+= raid5.o

# Note: link order is important.  All raid personalities
# and must come before md.o, as they each initialise 
# themselves, and md.o may use the personalities when it 
# auto-initialised.

obj-$(CONFIG_MD_LINEAR)		+= linear.o
obj-$(CONFIG_MD_RAID0)		+= raid0.o
obj-$(CONFIG_MD_RAID1)		+= raid1.o
obj-$(CONFIG_MD_RAID10)		+= raid10.o
obj-$(CONFIG_MD_RAID456)	+= raid456.o
obj-$(CONFIG_MD_MULTIPATH)	+= multipath.o
obj-$(CONFIG_MD_FAULTY)		+= faulty.o
obj-$(CONFIG_BLK_DEV_MD)	+= md-mod.o
obj-$(CONFIG_BLK_DEV_DM)	+= dm-mod.o
obj-$(CONFIG_DM_CRYPT)		+= dm-crypt.o
obj-$(CONFIG_DM_DELAY)		+= dm-delay.o
obj-$(CONFIG_DM_MULTIPATH)	+= dm-multipath.o dm-round-robin.o
obj-$(CONFIG_DM_MULTIPATH_QL)	+= dm-queue-length.o
obj-$(CONFIG_DM_MULTIPATH_ST)	+= dm-service-time.o
obj-$(CONFIG_DM_SNAPSHOT)	+= dm-snapshot.o
obj-$(CONFIG_DM_MIRROR)		+= dm-mirror.o dm-log.o dm-region-hash.o
obj-$(CONFIG_DM_LOG_USERSPACE)	+= dm-log-userspace.o
obj-$(CONFIG_DM_ZERO)		+= dm-zero.o

ifeq ($(CONFIG_DM_UEVENT),y)
dm-mod-objs			+= dm-uevent.o
endif
