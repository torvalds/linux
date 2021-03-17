# SPDX-License-Identifier: GPL-2.0
#
# (C) COPYRIGHT 2010-2021 ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the
# GNU General Public License version 2 as published by the Free Software
# Foundation, and any use by you of this program is subject to the terms
# of such GNU license.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, you can access it online at
# http://www.gnu.org/licenses/gpl-2.0.html.
#
#

# Handle Android Common Kernel source naming
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
KDIR ?= $(KERNEL_SRC)

# out-of-tree
ifeq ($(KBUILD_EXTMOD),)
export CONFIG_MALI_MIDGARD?=m

ifneq ($(CONFIG_MALI_MIDGARD),n)
export CONFIG_MALI_CSF_SUPPORT?=n
export CONFIG_MALI_KUTF?=m
export CONFIG_MALI_REAL_HW?=y

# Handle default y/m in Kconfig
export CONFIG_MALI_BIFROST_GATOR_SUPPORT?=y
export CONFIG_MALI_BIFROST_DEVFREQ?=n
ifneq ($(CONFIG_PM_DEVFREQ),n)
export CONFIG_MALI_BIFROST_DEVFREQ?=y
endif

DEFINES += -DCONFIG_MALI_MIDGARD=$(CONFIG_MALI_MIDGARD) \
	-DCONFIG_MALI_CSF_SUPPORT=$(CONFIG_MALI_CSF_SUPPORT) \
	-DCONFIG_MALI_KUTF=$(CONFIG_MALI_KUTF) \
	-DCONFIG_MALI_REAL_HW=$(CONFIG_MALI_REAL_HW) \
	-DCONFIG_MALI_GATOR_SUPPORT=$(CONFIG_MALI_BIFROST_GATOR_SUPPORT) \
	-DCONFIG_MALI_DEVFREQ=$(CONFIG_MALI_BIFROST_DEVFREQ)

export DEFINES

endif
endif

KBASE_PATH_RELATIVE = $(CURDIR)


# we get the symbols from modules using KBUILD_EXTRA_SYMBOLS to prevent warnings about unknown functions
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) EXTRA_CFLAGS="-I$(CURDIR)/../../../../include -I$(CURDIR)/../../../../tests/include $(SCONS_CFLAGS)" $(SCONS_CONFIGS) KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" modules

modules_install:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
