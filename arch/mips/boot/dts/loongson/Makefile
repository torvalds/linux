# SPDX-License-Identifier: GPL-2.0

ifneq ($(CONFIG_BUILTIN_DTB_NAME),)
dtb-y	:= $(addsuffix .dtb, $(CONFIG_BUILTIN_DTB_NAME))
else
dtb-$(CONFIG_MACH_LOONGSON64)	+= loongson64_2core_2k1000.dtb
dtb-$(CONFIG_MACH_LOONGSON64)	+= loongson64c_4core_ls7a.dtb
dtb-$(CONFIG_MACH_LOONGSON64)	+= loongson64c_4core_rs780e.dtb
dtb-$(CONFIG_MACH_LOONGSON64)	+= loongson64c_8core_rs780e.dtb
dtb-$(CONFIG_MACH_LOONGSON64)	+= loongson64g_4core_ls7a.dtb
dtb-$(CONFIG_MACH_LOONGSON64)	+= loongson64v_4core_virtio.dtb

dtb-$(CONFIG_MACH_LOONGSON32)	+= cq-t300b.dtb
dtb-$(CONFIG_MACH_LOONGSON32)	+= ls1b-demo.dtb
dtb-$(CONFIG_MACH_LOONGSON32)	+= lsgz_1b_dev.dtb
dtb-$(CONFIG_MACH_LOONGSON32)	+= smartloong-1c.dtb
endif
