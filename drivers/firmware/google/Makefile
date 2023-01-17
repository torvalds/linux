# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_GOOGLE_SMI)		+= gsmi.o
obj-$(CONFIG_GOOGLE_COREBOOT_TABLE)        += coreboot_table.o
obj-$(CONFIG_GOOGLE_FRAMEBUFFER_COREBOOT)  += framebuffer-coreboot.o
obj-$(CONFIG_GOOGLE_MEMCONSOLE)            += memconsole.o
obj-$(CONFIG_GOOGLE_MEMCONSOLE_COREBOOT)   += memconsole-coreboot.o
obj-$(CONFIG_GOOGLE_MEMCONSOLE_X86_LEGACY) += memconsole-x86-legacy.o

# Must come after coreboot_table.o, as this driver depends on that bus type.
obj-$(CONFIG_GOOGLE_CBMEM)		+= cbmem.o

vpd-sysfs-y := vpd.o vpd_decode.o
obj-$(CONFIG_GOOGLE_VPD)		+= vpd-sysfs.o
