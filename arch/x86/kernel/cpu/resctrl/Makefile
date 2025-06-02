# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_X86_CPU_RESCTRL)		+= core.o rdtgroup.o monitor.o
obj-$(CONFIG_X86_CPU_RESCTRL)		+= ctrlmondata.o
obj-$(CONFIG_RESCTRL_FS_PSEUDO_LOCK)	+= pseudo_lock.o
CFLAGS_pseudo_lock.o = -I$(src)
