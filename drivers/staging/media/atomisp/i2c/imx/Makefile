# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_VIDEO_IMX) += imx1x5.o

imx1x5-objs := imx.o drv201.o ad5816g.o dw9714.o dw9719.o dw9718.o vcm.o otp.o otp_imx.o otp_brcc064_e2prom.o otp_e2prom.o

ov8858_driver-objs := ../ov8858.o dw9718.o vcm.o
obj-$(CONFIG_VIDEO_OV8858)     += ov8858_driver.o

# HACK! While this driver is in bad shape, don't enable several warnings
#       that would be otherwise enabled with W=1
ccflags-y += $(call cc-disable-warning, unused-but-set-variable)
ccflags-y += $(call cc-disable-warning, unused-const-variable)
ccflags-y += $(call cc-disable-warning, missing-prototypes)
ccflags-y += $(call cc-disable-warning, missing-declarations)
