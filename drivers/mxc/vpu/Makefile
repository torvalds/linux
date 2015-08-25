#
# Makefile for the VPU drivers.
#

obj-$(CONFIG_MXC_VPU)                  += mxc_vpu.o

ifeq ($(CONFIG_MXC_VPU_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
