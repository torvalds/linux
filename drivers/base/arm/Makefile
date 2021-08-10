# SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
#
# (C) COPYRIGHT 2021 ARM Limited. All rights reserved.
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

#
# Paths
#
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
KDIR ?= $(KERNEL_SRC)

ifeq ($(KDIR),)
    $(error Must specify KDIR to point to the kernel to target))
endif

vars :=
#
# Default configuration values
#
CONFIG_MALI_BASE_MODULES ?= n

ifeq ($(CONFIG_MALI_BASE_MODULES),y)
    CONFIG_MALI_CSF_SUPPORT ?= n

    ifneq ($(CONFIG_DMA_SHARED_BUFFER),n)
        CONFIG_DMA_SHARED_BUFFER_TEST_EXPORTER ?= y
    else
        # Prevent misuse when CONFIG_DMA_SHARED_BUFFER=n
        CONFIG_DMA_SHARED_BUFFER_TEST_EXPORTER = n
    endif

    CONFIG_MALI_MEMORY_GROUP_MANAGER ?= y

    ifneq ($(CONFIG_MALI_CSF_SUPPORT), n)
        CONFIG_MALI_PROTECTED_MEMORY_ALLOCATOR ?= y
    endif

else
    # Prevent misuse when CONFIG_MALI_BASE_MODULES=n
    CONFIG_DMA_SHARED_BUFFER_TEST_EXPORTER = n
    CONFIG_MALI_MEMORY_GROUP_MANAGER = n
    CONFIG_MALI_PROTECTED_MEMORY_ALLOCATOR = n

endif

CONFIGS := \
    CONFIG_MALI_BASE_MODULES \
    CONFIG_MALI_CSF_SUPPORT \
    CONFIG_DMA_SHARED_BUFFER_TEST_EXPORTER \
    CONFIG_MALI_MEMORY_GROUP_MANAGER \
    CONFIG_MALI_PROTECTED_MEMORY_ALLOCATOR


#
# MAKE_ARGS to pass the custom CONFIGs on out-of-tree build
#
# Generate the list of CONFIGs and values.
# $(value config) is the name of the CONFIG option.
# $(value $(value config)) is its value (y, m).
# When the CONFIG is not set to y or m, it defaults to n.
MAKE_ARGS := $(foreach config,$(CONFIGS), \
                    $(if $(filter y m,$(value $(value config))), \
                        $(value config)=$(value $(value config)), \
                        $(value config)=n))

#
# EXTRA_CFLAGS to define the custom CONFIGs on out-of-tree build
#
# Generate the list of CONFIGs defines with values from CONFIGS.
# $(value config) is the name of the CONFIG option.
# When set to y or m, the CONFIG gets defined to 1.
EXTRA_CFLAGS := $(foreach config,$(CONFIGS), \
                    $(if $(filter y m,$(value $(value config))), \
                        -D$(value config)=1))

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(MAKE_ARGS) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" modules

modules_install:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(MAKE_ARGS) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(MAKE_ARGS) clean
