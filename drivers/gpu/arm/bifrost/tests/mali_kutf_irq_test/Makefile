#
# (C) COPYRIGHT 2015, 2017-2018 ARM Limited. All rights reserved.
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

# linux build system bootstrap for out-of-tree module

# default to building for the host
ARCH ?= $(shell uname -m)

ifeq ($(KDIR),)
$(error Must specify KDIR to point to the kernel to target))
endif

TEST_CCFLAGS := \
	-DMALI_UNIT_TEST=$(MALI_UNIT_TEST) \
	-DMALI_CUSTOMER_RELEASE=$(MALI_CUSTOMER_RELEASE) \
	$(SCONS_CFLAGS) \
	-I$(CURDIR)/../include \
	-I$(CURDIR)/../../../../../../include \
	-I$(CURDIR)/../../../ \
	-I$(CURDIR)/../../ \
	-I$(CURDIR)/../../backend/gpu \
	-I$(CURDIR)/ \
	-I$(srctree)/drivers/staging/android \
	-I$(srctree)/include/linux

all:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(CURDIR) $(SCONS_CONFIGS) EXTRA_CFLAGS="$(TEST_CCFLAGS)" KBUILD_EXTRA_SYMBOLS="$(CURDIR)/../kutf/Module.symvers $(CURDIR)/../../Module.symvers" modules

clean:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(CURDIR) clean
