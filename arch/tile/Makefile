#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture

# If building with TILERA_ROOT set (i.e. using the Tilera Multicore
# Development Environment) we can set CROSS_COMPILE based on that.
# If we're not cross-compiling, make sure we're on the right architecture.
# Only bother to test for a few common targets, to avoid useless errors.
ifeq ($(CROSS_COMPILE),)
  ifdef TILERA_ROOT
    CROSS_COMPILE := $(TILERA_ROOT)/bin/tile-
  else
    goals := $(if $(MAKECMDGOALS), $(MAKECMDGOALS), all)
    ifneq ($(strip $(filter vmlinux modules all,$(goals))),)
      HOST_ARCH := $(shell uname -m)
      ifneq ($(HOST_ARCH),$(ARCH))
$(error Set TILERA_ROOT or CROSS_COMPILE when building $(ARCH) on $(HOST_ARCH))
      endif
    endif
  endif
endif


KBUILD_CFLAGS   += $(CONFIG_DEBUG_EXTRA_FLAGS)

LIBGCC_PATH     := $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)

# Provide the path to use for "make defconfig".
KBUILD_DEFCONFIG := $(ARCH)_defconfig

# Used as a file extension when useful, e.g. head_$(BITS).o
# Not needed for (e.g.) "$(CC) -m32" since the compiler automatically
# uses the right default anyway.
export BITS
ifeq ($(CONFIG_TILEGX),y)
BITS := 64
else
BITS := 32
endif

head-y		:= arch/tile/kernel/head_$(BITS).o

libs-y		+= arch/tile/lib/
libs-y		+= $(LIBGCC_PATH)


# See arch/tile/Kbuild for content of core part of the kernel
core-y		+= arch/tile/
