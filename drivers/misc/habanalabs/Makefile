#
# Makefile for HabanaLabs AI accelerators driver
#

obj-m	:= habanalabs.o

habanalabs-y := habanalabs_drv.o device.o context.o asid.o habanalabs_ioctl.o \
		command_buffer.o hw_queue.o irq.o sysfs.o hwmon.o memory.o \
		command_submission.o mmu.o

habanalabs-$(CONFIG_DEBUG_FS) += debugfs.o

include $(src)/goya/Makefile
habanalabs-y += $(HL_GOYA_FILES)
