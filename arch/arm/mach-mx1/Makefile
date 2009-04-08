#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			+= generic.o clock.o devices.o

# Support for CMOS sensor interface
obj-$(CONFIG_MX1_VIDEO)	+= ksym_mx1.o mx1_camera_fiq.o

# Specific board support
obj-$(CONFIG_ARCH_MX1ADS) += mx1ads.o
obj-$(CONFIG_MACH_SCB9328) += scb9328.o