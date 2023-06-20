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
			    overlay_16.dtb.o \
			    overlay_17.dtb.o \
			    overlay_18.dtb.o \
			    overlay_19.dtb.o \
			    overlay_20.dtb.o \
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
DTC_FLAGS_testcases += -Wno-interrupts_property \
	-Wno-node_name_vs_property_name \
	-Wno-interrupt_map

# Apply overlays statically with fdtoverlay.  This is a build time test that
# the overlays can be applied successfully by fdtoverlay.  This does not
# guarantee that the overlays can be applied successfully at run time by
# unittest, but it provides a bit of build time test coverage for those
# who do not execute unittest.
#
# The overlays are applied on top of static_base_1.dtb and static_base_2.dtb to
# create static_test_1.dtb and static_test_2.dtb.  If fdtoverlay detects an
# error than the kernel build will fail.  static_test_1.dtb and
# static_test_2.dtb are not consumed by unittest.
#
# Some unittest overlays deliberately contain errors that unittest checks for.
# These overlays will cause fdtoverlay to fail, and are thus not included
# in the static test:
#			  overlay_bad_add_dup_node.dtbo \
#			  overlay_bad_add_dup_prop.dtbo \
#			  overlay_bad_phandle.dtbo \
#			  overlay_bad_symbol.dtbo \

apply_static_overlay_1 := overlay_0.dtbo \
			  overlay_1.dtbo \
			  overlay_2.dtbo \
			  overlay_3.dtbo \
			  overlay_4.dtbo \
			  overlay_5.dtbo \
			  overlay_6.dtbo \
			  overlay_7.dtbo \
			  overlay_8.dtbo \
			  overlay_9.dtbo \
			  overlay_10.dtbo \
			  overlay_11.dtbo \
			  overlay_12.dtbo \
			  overlay_13.dtbo \
			  overlay_15.dtbo \
			  overlay_16.dtbo \
			  overlay_17.dtbo \
			  overlay_18.dtbo \
			  overlay_19.dtbo \
			  overlay_20.dtbo \
			  overlay_gpio_01.dtbo \
			  overlay_gpio_02a.dtbo \
			  overlay_gpio_02b.dtbo \
			  overlay_gpio_03.dtbo \
			  overlay_gpio_04a.dtbo \
			  overlay_gpio_04b.dtbo

apply_static_overlay_2 := overlay.dtbo

DTC_FLAGS_static_base_1 += -Wno-interrupts_property \
	-Wno-node_name_vs_property_name \
	-Wno-interrupt_map

static_test_1-dtbs := static_base_1.dtb $(apply_static_overlay_1)
static_test_2-dtbs := static_base_2.dtb $(apply_static_overlay_2)

dtb-$(CONFIG_OF_OVERLAY) += static_test_1.dtb static_test_2.dtb
