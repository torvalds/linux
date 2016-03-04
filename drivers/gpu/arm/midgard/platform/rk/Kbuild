#
# (C) COPYRIGHT 2012-2013 ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the
# GNU General Public License version 2 as published by the Free Software
# Foundation, and any use by you of this program is subject to the terms
# of such GNU licence.
#
# A copy of the licence is included with the program, and can also be obtained
# from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.
#
#

ccflags-y += -I$(srctree)/drivers/staging/android

ifeq ($(CONFIG_MALI_MIDGARD),y)
obj-y += mali_kbase_config_rk.o
# obj-y += mali_kbase_dvfs.o
# obj-y += mali_kbase_platform.o
else ifeq ($(CONFIG_MALI_MIDGARD),m)
SRC += platform/rk/mali_kbase_config_rk.c
# SRC += platform/rk/mali_kbase_dvfs.c
# SRC += platform/rk/mali_kbase_platform.c
endif
