# SPDX-License-Identifier: GPL-2.0-only
carl9170-objs := main.o usb.o cmd.o mac.o phy.o led.o fw.o tx.o rx.o
carl9170-$(CONFIG_CARL9170_DEBUGFS) += debug.o

obj-$(CONFIG_CARL9170) += carl9170.o

# FIXME: temporarily silence -Warray-bounds on non W=1+ builds
ifndef KBUILD_EXTRA_WARN
CFLAGS_cmd.o += -Wno-array-bounds
endif
