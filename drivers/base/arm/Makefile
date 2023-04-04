# SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
#
# (C) COPYRIGHT 2021-2022 ARM Limited. All rights reserved.
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
    CONFIG_MALI_PROTECTED_MEMORY_ALLOCATOR \


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

KBUILD_CFLAGS += -Wall -Werror

ifeq ($(CONFIG_GCOV_KERNEL), y)
    KBUILD_CFLAGS += $(call cc-option, -ftest-coverage)
    KBUILD_CFLAGS += $(call cc-option, -fprofile-arcs)
    EXTRA_CFLAGS += -DGCOV_PROFILE=1
endif

ifeq ($(CONFIG_MALI_KCOV),y)
    KBUILD_CFLAGS += $(call cc-option, -fsanitize-coverage=trace-cmp)
    EXTRA_CFLAGS += -DKCOV=1
    EXTRA_CFLAGS += -DKCOV_ENABLE_COMPARISONS=1
endif

# The following were added to align with W=1 in scripts/Makefile.extrawarn
# from the Linux source tree (v5.18.14)
KBUILD_CFLAGS += -Wextra -Wunused -Wno-unused-parameter
KBUILD_CFLAGS += -Wmissing-declarations
KBUILD_CFLAGS += -Wmissing-format-attribute
KBUILD_CFLAGS += -Wmissing-prototypes
KBUILD_CFLAGS += -Wold-style-definition
# The -Wmissing-include-dirs cannot be enabled as the path to some of the
# included directories change depending on whether it is an in-tree or
# out-of-tree build.
KBUILD_CFLAGS += $(call cc-option, -Wunused-but-set-variable)
KBUILD_CFLAGS += $(call cc-option, -Wunused-const-variable)
KBUILD_CFLAGS += $(call cc-option, -Wpacked-not-aligned)
KBUILD_CFLAGS += $(call cc-option, -Wstringop-truncation)
# The following turn off the warnings enabled by -Wextra
KBUILD_CFLAGS += -Wno-sign-compare
KBUILD_CFLAGS += -Wno-shift-negative-value
# This flag is needed to avoid build errors on older kernels
KBUILD_CFLAGS += $(call cc-option, -Wno-cast-function-type)

KBUILD_CPPFLAGS += -DKBUILD_EXTRA_WARN1

# The following were added to align with W=2 in scripts/Makefile.extrawarn
# from the Linux source tree (v5.18.14)
KBUILD_CFLAGS += -Wdisabled-optimization
# The -Wshadow flag cannot be enabled unless upstream kernels are
# patched to fix redefinitions of certain built-in functions and
# global variables.
KBUILD_CFLAGS += $(call cc-option, -Wlogical-op)
KBUILD_CFLAGS += -Wmissing-field-initializers
# -Wtype-limits must be disabled due to build failures on kernel 5.x
KBUILD_CFLAGS += -Wno-type-limit
KBUILD_CFLAGS += $(call cc-option, -Wmaybe-uninitialized)
KBUILD_CFLAGS += $(call cc-option, -Wunused-macros)

KBUILD_CPPFLAGS += -DKBUILD_EXTRA_WARN2

# This warning is disabled to avoid build failures in some kernel versions
KBUILD_CFLAGS += -Wno-ignored-qualifiers

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(MAKE_ARGS) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" modules

modules_install:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(MAKE_ARGS) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(MAKE_ARGS) clean
