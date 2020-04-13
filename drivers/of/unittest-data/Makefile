# SPDX-License-Identifier: GPL-2.0
obj-y += testcases.dtb.o

obj-$(CONFIG_OF_OVERLAY) += overlay.dtb.o \
			    overlay_0.dtb.o \
			    overlay_1.dtb.o \
			    overlay_2.dtb.o \
			    overlay_3.dtb.o \
			    overlay_4.dtb.o \
			    overlay_5.dtb.o \
			    overlay_6.dtb.o \
			    overlay_7.dtb.o \
			    overlay_8.dtb.o \
			    overlay_9.dtb.o \
			    overlay_10.dtb.o \
			    overlay_11.dtb.o \
			    overlay_12.dtb.o \
			    overlay_13.dtb.o \
			    overlay_15.dtb.o \
			    overlay_bad_add_dup_node.dtb.o \
			    overlay_bad_add_dup_prop.dtb.o \
			    overlay_bad_phandle.dtb.o \
			    overlay_bad_symbol.dtb.o \
			    overlay_base.dtb.o \
			    overlay_gpio_01.dtb.o \
			    overlay_gpio_02a.dtb.o \
			    overlay_gpio_02b.dtb.o \
			    overlay_gpio_03.dtb.o \
			    overlay_gpio_04a.dtb.o \
			    overlay_gpio_04b.dtb.o

# enable creation of __symbols__ node
DTC_FLAGS_overlay += -@
DTC_FLAGS_overlay_bad_phandle += -@
DTC_FLAGS_overlay_bad_symbol += -@
DTC_FLAGS_overlay_base += -@
DTC_FLAGS_testcases += -@

# suppress warnings about intentional errors
DTC_FLAGS_testcases += -Wno-interrupts_property
