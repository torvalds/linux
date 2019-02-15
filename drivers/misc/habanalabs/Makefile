#
# Makefile for HabanaLabs AI accelerators driver
#

obj-m	:= habanalabs.o

habanalabs-y := habanalabs_drv.o device.o context.o asid.o habanalabs_ioctl.o \
		command_buffer.o hw_queue.o irq.o sysfs.o hwmon.o

include $(src)/goya/Makefile
habanalabs-y += $(HL_GOYA_FILES)
