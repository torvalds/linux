#
# (C) COPYRIGHT 2010-2018 ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the
# GNU General Public License version 2 as published by the Free Software
# Foundation, and any use by you of this program is subject to the terms
# of such GNU licence.
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
# SPDX-License-Identifier: GPL-2.0
#
#


KDIR ?= /lib/modules/$(shell uname -r)/build

BUSLOG_PATH_RELATIVE = $(CURDIR)/../../../..
KBASE_PATH_RELATIVE = $(CURDIR)

ifeq ($(MALI_UNIT_TEST), 1)
	EXTRA_SYMBOLS += $(KBASE_PATH_RELATIVE)/tests/internal/src/kernel_assert_module/linux/Module.symvers
endif

ifeq ($(CONFIG_MALI_FPGA_BUS_LOGGER),y)
#Add bus logger symbols
EXTRA_SYMBOLS += $(BUSLOG_PATH_RELATIVE)/drivers/base/bus_logger/Module.symvers
endif

# we get the symbols from modules using KBUILD_EXTRA_SYMBOLS to prevent warnings about unknown functions
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) EXTRA_CFLAGS="-I$(CURDIR)/../../../../include -I$(CURDIR)/../../../../tests/include $(SCONS_CFLAGS)" $(SCONS_CONFIGS) KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
