obj-$(CONFIG_VIDEO_IMX) += imx1x5.o

imx1x5-objs := imx.o drv201.o ad5816g.o dw9714.o dw9719.o dw9718.o vcm.o otp.o otp_imx.o otp_brcc064_e2prom.o otp_e2prom.o

ov8858_driver-objs := ../ov8858.o dw9718.o vcm.o
obj-$(CONFIG_VIDEO_OV8858)     += ov8858_driver.o

ccflags-y += -Werror
