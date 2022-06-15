# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_CAN_KVASER_USB) += kvaser_usb.o
kvaser_usb-y = kvaser_usb_core.o kvaser_usb_leaf.o kvaser_usb_hydra.o

# FIXME: temporarily silence -Warray-bounds on non W=1+ builds
ifndef KBUILD_EXTRA_WARN
CFLAGS_kvaser_usb_hydra.o += -Wno-array-bounds
endif
