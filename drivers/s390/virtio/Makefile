# Makefile for kvm guest drivers on s390
#
# Copyright IBM Corp. 2008
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License (version 2 only)
# as published by the Free Software Foundation.

s390-virtio-objs := virtio_ccw.o
ifdef CONFIG_S390_GUEST_OLD_TRANSPORT
s390-virtio-objs += kvm_virtio.o
endif
obj-$(CONFIG_S390_GUEST) += $(s390-virtio-objs)
