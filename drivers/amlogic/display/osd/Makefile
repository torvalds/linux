#
# Makefile for the AML Apollo framebuffer device driver
#


obj-$(CONFIG_FB_AM) += fb.o 

fb-objs =  osd_hw.o osd_main.o osd_dev.o  osd_antiflicker.o osd_clone.o

ifeq ($(CONFIG_ARCH_MESON8),y)
fb-objs += osd_prot.o
endif

obj-$(CONFIG_VSYNC_RDMA) += osd_rdma.o

EXTRA_CFLAGS += -Iinclude/linux/amlogic/osd/
ifeq ($(CONFIG_FB_AMLOGIC_UMP),y)
fb-objs += osd_ump.o
endif

ifeq ($(CONFIG_FB_AMLOGIC_UMP),y)
EXTRA_CFLAGS += -I../hardware/arm/gpu/ump/include/
endif
 
