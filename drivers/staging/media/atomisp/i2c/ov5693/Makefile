# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_VIDEO_OV5693) += ov5693.o

# HACK! While this driver is in bad shape, don't enable several warnings
#       that would be otherwise enabled with W=1
ccflags-y += $(call cc-disable-warning, unused-but-set-variable)
ccflags-y += $(call cc-disable-warning, unused-const-variable)
ccflags-y += $(call cc-disable-warning, missing-prototypes)
ccflags-y += $(call cc-disable-warning, missing-declarations)
