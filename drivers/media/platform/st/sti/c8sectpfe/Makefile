# SPDX-License-Identifier: GPL-2.0
c8sectpfe-y += c8sectpfe-core.o c8sectpfe-common.o c8sectpfe-dvb.o

ifneq ($(CONFIG_DEBUG_FS),)
c8sectpfe-y += c8sectpfe-debugfs.o
endif

obj-$(CONFIG_DVB_C8SECTPFE) += c8sectpfe.o

ccflags-y += -I $(srctree)/drivers/media/dvb-frontends/
ccflags-y += -I $(srctree)/drivers/media/tuners/
