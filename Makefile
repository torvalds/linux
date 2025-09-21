# SPDX-License-Identifier: GPL-2.0
VERSION = 6
PATCHLEVEL = 17
SUBLEVEL = 0
EXTRAVERSION = -rc7
NAME = Baby Opossum Posse

# *DOCUMENTATION*
# To see a list of typical targets execute "make help"
# More info can be located in ./README
# Comments in this file are targeted only to the developer, do not
# expect to learn how to build the kernel reading this file.

ifeq ($(filter output-sync,$(.FEATURES)),)
$(error GNU Make >= 4.0 is required. Your Make version is $(MAKE_VERSION))
endif

$(if $(filter __%, $(MAKECMDGOALS)), \
	$(error targets prefixed with '__' are only for internal use))

# That's our default target when none is given on the command line
PHONY := __all
__all:

# We are using a recursive build, so we need to do a little thinking
# to get the ordering right.
#
# Most importantly: sub-Makefiles should only ever modify files in
# their own directory. If in some directory we have a dependency on
# a file in another dir (which doesn't happen often, but it's often
# unavoidable when linking the built-in.a targets which finally
# turn into vmlinux), we will call a sub make in that other dir, and
# after that we are sure that everything which is in that other dir
# is now up to date.
#
# The only cases where we need to modify files which have global
# effects are thus separated out and done before the recursive
# descending is started. They are now explicitly listed as the
# prepare rule.

this-makefile := $(lastword $(MAKEFILE_LIST))
abs_srctree := $(realpath $(dir $(this-makefile)))
abs_output := $(CURDIR)

ifneq ($(sub_make_done),1)

# Do not use make's built-in rules and variables
# (this increases performance and avoids hard-to-debug behaviour)
MAKEFLAGS += -rR

# Avoid funny character set dependencies
unexport LC_ALL
LC_COLLATE=C
LC_NUMERIC=C
export LC_COLLATE LC_NUMERIC

# Avoid interference with shell env settings
unexport GREP_OPTIONS

# Beautify output
# ---------------------------------------------------------------------------
#
# Most of build commands in Kbuild start with "cmd_". You can optionally define
# "quiet_cmd_*". If defined, the short log is printed. Otherwise, no log from
# that command is printed by default.
#
# e.g.)
#    quiet_cmd_depmod = DEPMOD  $(MODLIB)
#          cmd_depmod = $(srctree)/scripts/depmod.sh $(DEPMOD) $(KERNELRELEASE)
#
# A simple variant is to prefix commands with $(Q) - that's useful
# for commands that shall be hidden in non-verbose mode.
#
#    $(Q)$(MAKE) $(build)=scripts/basic
#
# If KBUILD_VERBOSE contains 1, the whole command is echoed.
# If KBUILD_VERBOSE contains 2, the reason for rebuilding is printed.
#
# To put more focus on warnings, be less verbose as default
# Use 'make V=1' to see the full commands

ifeq ("$(origin V)", "command line")
  KBUILD_VERBOSE = $(V)
endif

quiet = quiet_
Q = @

ifneq ($(findstring 1, $(KBUILD_VERBOSE)),)
  quiet =
  Q =
endif

# If the user is running make -s (silent mode), suppress echoing of
# commands
ifneq ($(findstring s,$(firstword -$(MAKEFLAGS))),)
quiet=silent_
override KBUILD_VERBOSE :=
endif

export quiet Q KBUILD_VERBOSE

# Call a source code checker (by default, "sparse") as part of the
# C compilation.
#
# Use 'make C=1' to enable checking of only re-compiled files.
# Use 'make C=2' to enable checking of *all* source files, regardless
# of whether they are re-compiled or not.
#
# See the file "Documentation/dev-tools/sparse.rst" for more details,
# including where to get the "sparse" utility.

ifeq ("$(origin C)", "command line")
  KBUILD_CHECKSRC = $(C)
endif
ifndef KBUILD_CHECKSRC
  KBUILD_CHECKSRC = 0
endif

export KBUILD_CHECKSRC

# Enable "clippy" (a linter) as part of the Rust compilation.
#
# Use 'make CLIPPY=1' to enable it.
ifeq ("$(origin CLIPPY)", "command line")
  KBUILD_CLIPPY := $(CLIPPY)
endif

export KBUILD_CLIPPY

# Use make M=dir or set the environment variable KBUILD_EXTMOD to specify the
# directory of external module to build. Setting M= takes precedence.
ifeq ("$(origin M)", "command line")
  KBUILD_EXTMOD := $(M)
endif

ifeq ("$(origin MO)", "command line")
  KBUILD_EXTMOD_OUTPUT := $(MO)
endif

$(if $(word 2, $(KBUILD_EXTMOD)), \
	$(error building multiple external modules is not supported))

$(foreach x, % :, $(if $(findstring $x, $(KBUILD_EXTMOD)), \
	$(error module directory path cannot contain '$x')))

# Remove trailing slashes
ifneq ($(filter %/, $(KBUILD_EXTMOD)),)
KBUILD_EXTMOD := $(shell dirname $(KBUILD_EXTMOD).)
endif

export KBUILD_EXTMOD

ifeq ("$(origin W)", "command line")
  KBUILD_EXTRA_WARN := $(W)
endif

export KBUILD_EXTRA_WARN

# Kbuild will save output files in the current working directory.
# This does not need to match to the root of the kernel source tree.
#
# For example, you can do this:
#
#  cd /dir/to/store/output/files; make -f /dir/to/kernel/source/Makefile
#
# If you want to save output files in a different location, there are
# two syntaxes to specify it.
#
# 1) O=
# Use "make O=dir/to/store/output/files/"
#
# 2) Set KBUILD_OUTPUT
# Set the environment variable KBUILD_OUTPUT to point to the output directory.
# export KBUILD_OUTPUT=dir/to/store/output/files/; make
#
# The O= assignment takes precedence over the KBUILD_OUTPUT environment
# variable.

ifeq ("$(origin O)", "command line")
  KBUILD_OUTPUT := $(O)
endif

ifdef KBUILD_EXTMOD
    ifdef KBUILD_OUTPUT
        objtree := $(realpath $(KBUILD_OUTPUT))
        $(if $(objtree),,$(error specified kernel directory "$(KBUILD_OUTPUT)" does not exist))
    else
        objtree := $(abs_srctree)
    endif
    # If Make is invoked from the kernel directory (either kernel
    # source directory or kernel build directory), external modules
    # are built in $(KBUILD_EXTMOD) for backward compatibility,
    # otherwise, built in the current directory.
    output := $(or $(KBUILD_EXTMOD_OUTPUT),$(if $(filter $(CURDIR),$(objtree) $(abs_srctree)),$(KBUILD_EXTMOD)))
    # KBUILD_EXTMOD might be a relative path. Remember its absolute path before
    # Make changes the working directory.
    srcroot := $(realpath $(KBUILD_EXTMOD))
    $(if $(srcroot),,$(error specified external module directory "$(KBUILD_EXTMOD)" does not exist))
else
    objtree := .
    output := $(KBUILD_OUTPUT)
endif

export objtree srcroot

# Do we want to change the working directory?
ifneq ($(output),)
# $(realpath ...) gets empty if the path does not exist. Run 'mkdir -p' first.
$(shell mkdir -p "$(output)")
# $(realpath ...) resolves symlinks
abs_output := $(realpath $(output))
$(if $(abs_output),,$(error failed to create output directory "$(output)"))
endif

ifneq ($(words $(subst :, ,$(abs_srctree))), 1)
$(error source directory cannot contain spaces or colons)
endif

export sub_make_done := 1

endif # sub_make_done

ifeq ($(abs_output),$(CURDIR))
# Suppress "Entering directory ..." if we are at the final work directory.
no-print-directory := --no-print-directory
else
# Recursion to show "Entering directory ..."
need-sub-make := 1
endif

ifeq ($(filter --no-print-directory, $(MAKEFLAGS)),)
# If --no-print-directory is unset, recurse once again to set it.
# You may end up recursing into __sub-make twice. This is needed due to the
# behavior change in GNU Make 4.4.1.
need-sub-make := 1
endif

ifeq ($(need-sub-make),1)

PHONY += $(MAKECMDGOALS) __sub-make

$(filter-out $(this-makefile), $(MAKECMDGOALS)) __all: __sub-make
	@:

# Invoke a second make in the output directory, passing relevant variables
__sub-make:
	$(Q)$(MAKE) $(no-print-directory) -C $(abs_output) \
	-f $(abs_srctree)/Makefile $(MAKECMDGOALS)

else # need-sub-make

# We process the rest of the Makefile if this is the final invocation of make

ifndef KBUILD_EXTMOD
srcroot := $(abs_srctree)
endif

ifeq ($(srcroot),$(CURDIR))
building_out_of_srctree :=
else
export building_out_of_srctree := 1
endif

ifdef KBUILD_ABS_SRCTREE
    # Do nothing. Use the absolute path.
else ifeq ($(srcroot),$(CURDIR))
    # Building in the source.
    srcroot := .
else ifeq ($(srcroot)/,$(dir $(CURDIR)))
    # Building in a subdirectory of the source.
    srcroot := ..
endif

export srctree := $(if $(KBUILD_EXTMOD),$(abs_srctree),$(srcroot))

ifdef building_out_of_srctree
export VPATH := $(srcroot)
else
VPATH :=
endif

# To make sure we do not include .config for any of the *config targets
# catch them early, and hand them over to scripts/kconfig/Makefile
# It is allowed to specify more targets when calling make, including
# mixing *config targets and build targets.
# For example 'make oldconfig all'.
# Detect when mixed targets is specified, and make a second invocation
# of make so .config is not included in this case either (for *config).

version_h := include/generated/uapi/linux/version.h

clean-targets := %clean mrproper cleandocs
no-dot-config-targets := $(clean-targets) \
			 cscope gtags TAGS tags help% %docs check% coccicheck \
			 $(version_h) headers headers_% archheaders archscripts \
			 %asm-generic kernelversion %src-pkg dt_binding_check \
			 outputmakefile rustavailable rustfmt rustfmtcheck
no-sync-config-targets := $(no-dot-config-targets) %install modules_sign kernelrelease \
			  image_name
single-targets := %.a %.i %.ko %.lds %.ll %.lst %.mod %.o %.rsi %.s %/

config-build	:=
mixed-build	:=
need-config	:= 1
may-sync-config	:= 1
single-build	:=

ifneq ($(filter $(no-dot-config-targets), $(MAKECMDGOALS)),)
    ifeq ($(filter-out $(no-dot-config-targets), $(MAKECMDGOALS)),)
        need-config :=
    endif
endif

ifneq ($(filter $(no-sync-config-targets), $(MAKECMDGOALS)),)
    ifeq ($(filter-out $(no-sync-config-targets), $(MAKECMDGOALS)),)
        may-sync-config :=
    endif
endif

need-compiler := $(may-sync-config)

ifneq ($(KBUILD_EXTMOD),)
    may-sync-config :=
endif

ifeq ($(KBUILD_EXTMOD),)
    ifneq ($(filter %config,$(MAKECMDGOALS)),)
        config-build := 1
        ifneq ($(words $(MAKECMDGOALS)),1)
            mixed-build := 1
        endif
    endif
endif

# We cannot build single targets and the others at the same time
ifneq ($(filter $(single-targets), $(MAKECMDGOALS)),)
    single-build := 1
    ifneq ($(filter-out $(single-targets), $(MAKECMDGOALS)),)
        mixed-build := 1
    endif
endif

# For "make -j clean all", "make -j mrproper defconfig all", etc.
ifneq ($(filter $(clean-targets),$(MAKECMDGOALS)),)
    ifneq ($(filter-out $(clean-targets),$(MAKECMDGOALS)),)
        mixed-build := 1
    endif
endif

# install and modules_install need also be processed one by one
ifneq ($(filter install,$(MAKECMDGOALS)),)
    ifneq ($(filter modules_install,$(MAKECMDGOALS)),)
        mixed-build := 1
    endif
endif

ifdef mixed-build
# ===========================================================================
# We're called with mixed targets (*config and build targets).
# Handle them one by one.

PHONY += $(MAKECMDGOALS) __build_one_by_one

$(MAKECMDGOALS): __build_one_by_one
	@:

__build_one_by_one:
	$(Q)set -e; \
	for i in $(MAKECMDGOALS); do \
		$(MAKE) -f $(srctree)/Makefile $$i; \
	done

else # !mixed-build

include $(srctree)/scripts/Kbuild.include

# Read KERNELRELEASE from include/config/kernel.release (if it exists)
KERNELRELEASE = $(call read-file, $(objtree)/include/config/kernel.release)
KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if $(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)
export VERSION PATCHLEVEL SUBLEVEL KERNELRELEASE KERNELVERSION

include $(srctree)/scripts/subarch.include

# Cross compiling and selecting different set of gcc/bin-utils
# ---------------------------------------------------------------------------
#
# When performing cross compilation for other architectures ARCH shall be set
# to the target architecture. (See arch/* for the possibilities).
# ARCH can be set during invocation of make:
# make ARCH=arm64
# Another way is to have ARCH set in the environment.
# The default ARCH is the host where make is executed.

# CROSS_COMPILE specify the prefix used for all executables used
# during compilation. Only gcc and related bin-utils executables
# are prefixed with $(CROSS_COMPILE).
# CROSS_COMPILE can be set on the command line
# make CROSS_COMPILE=aarch64-linux-gnu-
# Alternatively CROSS_COMPILE can be set in the environment.
# Default value for CROSS_COMPILE is not to prefix executables
# Note: Some architectures assign CROSS_COMPILE in their arch/*/Makefile
ARCH		?= $(SUBARCH)

# Architecture as present in compile.h
UTS_MACHINE 	:= $(ARCH)
SRCARCH 	:= $(ARCH)

# Additional ARCH settings for x86
ifeq ($(ARCH),i386)
        SRCARCH := x86
endif
ifeq ($(ARCH),x86_64)
        SRCARCH := x86
endif

# Additional ARCH settings for sparc
ifeq ($(ARCH),sparc32)
       SRCARCH := sparc
endif
ifeq ($(ARCH),sparc64)
       SRCARCH := sparc
endif

# Additional ARCH settings for parisc
ifeq ($(ARCH),parisc64)
       SRCARCH := parisc
endif

export cross_compiling :=
ifneq ($(SRCARCH),$(SUBARCH))
cross_compiling := 1
endif

KCONFIG_CONFIG	?= .config
export KCONFIG_CONFIG

# SHELL used by kbuild
CONFIG_SHELL := sh

HOST_LFS_CFLAGS := $(shell getconf LFS_CFLAGS 2>/dev/null)
HOST_LFS_LDFLAGS := $(shell getconf LFS_LDFLAGS 2>/dev/null)
HOST_LFS_LIBS := $(shell getconf LFS_LIBS 2>/dev/null)

ifneq ($(LLVM),)
ifneq ($(filter %/,$(LLVM)),)
LLVM_PREFIX := $(LLVM)
else ifneq ($(filter -%,$(LLVM)),)
LLVM_SUFFIX := $(LLVM)
endif

HOSTCC	= $(LLVM_PREFIX)clang$(LLVM_SUFFIX)
HOSTCXX	= $(LLVM_PREFIX)clang++$(LLVM_SUFFIX)
else
HOSTCC	= gcc
HOSTCXX	= g++
endif
HOSTRUSTC = rustc
HOSTPKG_CONFIG	= pkg-config

# the KERNELDOC macro needs to be exported, as scripts/Makefile.build
# has a logic to call it
KERNELDOC       = $(srctree)/scripts/kernel-doc.py
export KERNELDOC

KBUILD_USERHOSTCFLAGS := -Wall -Wmissing-prototypes -Wstrict-prototypes \
			 -O2 -fomit-frame-pointer -std=gnu11
KBUILD_USERCFLAGS  := $(KBUILD_USERHOSTCFLAGS) $(USERCFLAGS)
KBUILD_USERLDFLAGS := $(USERLDFLAGS)

# These flags apply to all Rust code in the tree, including the kernel and
# host programs.
export rust_common_flags := --edition=2021 \
			    -Zbinary_dep_depinfo=y \
			    -Astable_features \
			    -Dnon_ascii_idents \
			    -Dunsafe_op_in_unsafe_fn \
			    -Wmissing_docs \
			    -Wrust_2018_idioms \
			    -Wunreachable_pub \
			    -Wclippy::all \
			    -Wclippy::as_ptr_cast_mut \
			    -Wclippy::as_underscore \
			    -Wclippy::cast_lossless \
			    -Wclippy::ignored_unit_patterns \
			    -Wclippy::mut_mut \
			    -Wclippy::needless_bitwise_bool \
			    -Aclippy::needless_lifetimes \
			    -Wclippy::no_mangle_with_rust_abi \
			    -Wclippy::ptr_as_ptr \
			    -Wclippy::ptr_cast_constness \
			    -Wclippy::ref_as_ptr \
			    -Wclippy::undocumented_unsafe_blocks \
			    -Wclippy::unnecessary_safety_comment \
			    -Wclippy::unnecessary_safety_doc \
			    -Wrustdoc::missing_crate_level_docs \
			    -Wrustdoc::unescaped_backticks

KBUILD_HOSTCFLAGS   := $(KBUILD_USERHOSTCFLAGS) $(HOST_LFS_CFLAGS) \
		       $(HOSTCFLAGS) -I $(srctree)/scripts/include
KBUILD_HOSTCXXFLAGS := -Wall -O2 $(HOST_LFS_CFLAGS) $(HOSTCXXFLAGS) \
		       -I $(srctree)/scripts/include
KBUILD_HOSTRUSTFLAGS := $(rust_common_flags) -O -Cstrip=debuginfo \
			-Zallow-features= $(HOSTRUSTFLAGS)
KBUILD_HOSTLDFLAGS  := $(HOST_LFS_LDFLAGS) $(HOSTLDFLAGS)
KBUILD_HOSTLDLIBS   := $(HOST_LFS_LIBS) $(HOSTLDLIBS)
KBUILD_PROCMACROLDFLAGS := $(or $(PROCMACROLDFLAGS),$(KBUILD_HOSTLDFLAGS))

# Make variables (CC, etc...)
CPP		= $(CC) -E
ifneq ($(LLVM),)
CC		= $(LLVM_PREFIX)clang$(LLVM_SUFFIX)
LD		= $(LLVM_PREFIX)ld.lld$(LLVM_SUFFIX)
AR		= $(LLVM_PREFIX)llvm-ar$(LLVM_SUFFIX)
NM		= $(LLVM_PREFIX)llvm-nm$(LLVM_SUFFIX)
OBJCOPY		= $(LLVM_PREFIX)llvm-objcopy$(LLVM_SUFFIX)
OBJDUMP		= $(LLVM_PREFIX)llvm-objdump$(LLVM_SUFFIX)
READELF		= $(LLVM_PREFIX)llvm-readelf$(LLVM_SUFFIX)
STRIP		= $(LLVM_PREFIX)llvm-strip$(LLVM_SUFFIX)
else
CC		= $(CROSS_COMPILE)gcc
LD		= $(CROSS_COMPILE)ld
AR		= $(CROSS_COMPILE)ar
NM		= $(CROSS_COMPILE)nm
OBJCOPY		= $(CROSS_COMPILE)objcopy
OBJDUMP		= $(CROSS_COMPILE)objdump
READELF		= $(CROSS_COMPILE)readelf
STRIP		= $(CROSS_COMPILE)strip
endif
RUSTC		= rustc
RUSTDOC		= rustdoc
RUSTFMT		= rustfmt
CLIPPY_DRIVER	= clippy-driver
BINDGEN		= bindgen
PAHOLE		= pahole
RESOLVE_BTFIDS	= $(objtree)/tools/bpf/resolve_btfids/resolve_btfids
LEX		= flex
YACC		= bison
AWK		= awk
INSTALLKERNEL  := installkernel
PERL		= perl
PYTHON3		= python3
CHECK		= sparse
BASH		= bash
KGZIP		= gzip
KBZIP2		= bzip2
KLZOP		= lzop
LZMA		= lzma
LZ4		= lz4
XZ		= xz
ZSTD		= zstd
TAR		= tar

CHECKFLAGS     := -D__linux__ -Dlinux -D__STDC__ -Dunix -D__unix__ \
		  -Wbitwise -Wno-return-void -Wno-unknown-attribute $(CF)
NOSTDINC_FLAGS :=
CFLAGS_MODULE   =
RUSTFLAGS_MODULE =
AFLAGS_MODULE   =
LDFLAGS_MODULE  =
CFLAGS_KERNEL	=
RUSTFLAGS_KERNEL =
AFLAGS_KERNEL	=
LDFLAGS_vmlinux =

# Use USERINCLUDE when you must reference the UAPI directories only.
USERINCLUDE    := \
		-I$(srctree)/arch/$(SRCARCH)/include/uapi \
		-I$(objtree)/arch/$(SRCARCH)/include/generated/uapi \
		-I$(srctree)/include/uapi \
		-I$(objtree)/include/generated/uapi \
                -include $(srctree)/include/linux/compiler-version.h \
                -include $(srctree)/include/linux/kconfig.h

# Use LINUXINCLUDE when you must reference the include/ directory.
# Needed to be compatible with the O= option
LINUXINCLUDE    := \
		-I$(srctree)/arch/$(SRCARCH)/include \
		-I$(objtree)/arch/$(SRCARCH)/include/generated \
		-I$(srctree)/include \
		-I$(objtree)/include \
		$(USERINCLUDE)

KBUILD_AFLAGS   := -D__ASSEMBLY__ -fno-PIE

KBUILD_CFLAGS :=
KBUILD_CFLAGS += -std=gnu11
KBUILD_CFLAGS += -fshort-wchar
KBUILD_CFLAGS += -funsigned-char
KBUILD_CFLAGS += -fno-common
KBUILD_CFLAGS += -fno-PIE
KBUILD_CFLAGS += -fno-strict-aliasing

KBUILD_CPPFLAGS := -D__KERNEL__
KBUILD_RUSTFLAGS := $(rust_common_flags) \
		    -Cpanic=abort -Cembed-bitcode=n -Clto=n \
		    -Cforce-unwind-tables=n -Ccodegen-units=1 \
		    -Csymbol-mangling-version=v0 \
		    -Crelocation-model=static \
		    -Zfunction-sections=n \
		    -Wclippy::float_arithmetic

KBUILD_AFLAGS_KERNEL :=
KBUILD_CFLAGS_KERNEL :=
KBUILD_RUSTFLAGS_KERNEL :=
KBUILD_AFLAGS_MODULE  := -DMODULE
KBUILD_CFLAGS_MODULE  := -DMODULE
KBUILD_RUSTFLAGS_MODULE := --cfg MODULE
KBUILD_LDFLAGS_MODULE :=
KBUILD_LDFLAGS :=
CLANG_FLAGS :=

ifeq ($(KBUILD_CLIPPY),1)
	RUSTC_OR_CLIPPY_QUIET := CLIPPY
	RUSTC_OR_CLIPPY = $(CLIPPY_DRIVER)
else
	RUSTC_OR_CLIPPY_QUIET := RUSTC
	RUSTC_OR_CLIPPY = $(RUSTC)
endif

# Allows the usage of unstable features in stable compilers.
export RUSTC_BOOTSTRAP := 1

# Allows finding `.clippy.toml` in out-of-srctree builds.
export CLIPPY_CONF_DIR := $(srctree)

export ARCH SRCARCH CONFIG_SHELL BASH HOSTCC KBUILD_HOSTCFLAGS CROSS_COMPILE LD CC HOSTPKG_CONFIG
export RUSTC RUSTDOC RUSTFMT RUSTC_OR_CLIPPY_QUIET RUSTC_OR_CLIPPY BINDGEN
export HOSTRUSTC KBUILD_HOSTRUSTFLAGS
export CPP AR NM STRIP OBJCOPY OBJDUMP READELF PAHOLE RESOLVE_BTFIDS LEX YACC AWK INSTALLKERNEL
export PERL PYTHON3 CHECK CHECKFLAGS MAKE UTS_MACHINE HOSTCXX
export KGZIP KBZIP2 KLZOP LZMA LZ4 XZ ZSTD TAR
export KBUILD_HOSTCXXFLAGS KBUILD_HOSTLDFLAGS KBUILD_HOSTLDLIBS KBUILD_PROCMACROLDFLAGS LDFLAGS_MODULE
export KBUILD_USERCFLAGS KBUILD_USERLDFLAGS

export KBUILD_CPPFLAGS NOSTDINC_FLAGS LINUXINCLUDE OBJCOPYFLAGS KBUILD_LDFLAGS
export KBUILD_CFLAGS CFLAGS_KERNEL CFLAGS_MODULE
export KBUILD_RUSTFLAGS RUSTFLAGS_KERNEL RUSTFLAGS_MODULE
export KBUILD_AFLAGS AFLAGS_KERNEL AFLAGS_MODULE
export KBUILD_AFLAGS_MODULE KBUILD_CFLAGS_MODULE KBUILD_RUSTFLAGS_MODULE KBUILD_LDFLAGS_MODULE
export KBUILD_AFLAGS_KERNEL KBUILD_CFLAGS_KERNEL KBUILD_RUSTFLAGS_KERNEL

# Files to ignore in find ... statements

export RCS_FIND_IGNORE := \( -name SCCS -o -name BitKeeper -o -name .svn -o    \
			  -name CVS -o -name .pc -o -name .hg -o -name .git \) \
			  -prune -o

# ===========================================================================
# Rules shared between *config targets and build targets

# Basic helpers built in scripts/basic/
PHONY += scripts_basic
scripts_basic:
	$(Q)$(MAKE) $(build)=scripts/basic

PHONY += outputmakefile
ifdef building_out_of_srctree
# Before starting out-of-tree build, make sure the source tree is clean.
# outputmakefile generates a Makefile in the output directory, if using a
# separate output directory. This allows convenient use of make in the
# output directory.
# At the same time when output Makefile generated, generate .gitignore to
# ignore whole output directory

ifdef KBUILD_EXTMOD
print_env_for_makefile = \
	echo "export KBUILD_OUTPUT = $(objtree)"; \
	echo "export KBUILD_EXTMOD = $(realpath $(srcroot))" ; \
	echo "export KBUILD_EXTMOD_OUTPUT = $(CURDIR)"
else
print_env_for_makefile = \
	echo "export KBUILD_OUTPUT = $(CURDIR)"
endif

quiet_cmd_makefile = GEN     Makefile
      cmd_makefile = { \
	echo "\# Automatically generated by $(abs_srctree)/Makefile: don't edit"; \
	$(print_env_for_makefile); \
	echo "include $(abs_srctree)/Makefile"; \
	} > Makefile

outputmakefile:
ifeq ($(KBUILD_EXTMOD),)
	@if [ -f $(srctree)/.config -o \
		 -d $(srctree)/include/config -o \
		 -d $(srctree)/arch/$(SRCARCH)/include/generated ]; then \
		echo >&2 "***"; \
		echo >&2 "*** The source tree is not clean, please run 'make$(if $(findstring command line, $(origin ARCH)), ARCH=$(ARCH)) mrproper'"; \
		echo >&2 "*** in $(abs_srctree)";\
		echo >&2 "***"; \
		false; \
	fi
else
	@if [ -f $(srcroot)/modules.order ]; then \
		echo >&2 "***"; \
		echo >&2 "*** The external module source tree is not clean."; \
		echo >&2 "*** Please run 'make -C $(abs_srctree) M=$(realpath $(srcroot)) clean'"; \
		echo >&2 "***"; \
		false; \
	fi
endif
	$(Q)ln -fsn $(srcroot) source
	$(call cmd,makefile)
	$(Q)test -e .gitignore || \
	{ echo "# this is build directory, ignore it"; echo "*"; } > .gitignore
endif

# The expansion should be delayed until arch/$(SRCARCH)/Makefile is included.
# Some architectures define CROSS_COMPILE in arch/$(SRCARCH)/Makefile.
# CC_VERSION_TEXT and RUSTC_VERSION_TEXT are referenced from Kconfig (so they
# need export), and from include/config/auto.conf.cmd to detect the compiler
# upgrade.
CC_VERSION_TEXT = $(subst $(pound),,$(shell LC_ALL=C $(CC) --version 2>/dev/null | head -n 1))
RUSTC_VERSION_TEXT = $(subst $(pound),,$(shell $(RUSTC) --version 2>/dev/null))

ifneq ($(findstring clang,$(CC_VERSION_TEXT)),)
include $(srctree)/scripts/Makefile.clang
endif

# Include this also for config targets because some architectures need
# cc-cross-prefix to determine CROSS_COMPILE.
ifdef need-compiler
include $(srctree)/scripts/Makefile.compiler
endif

ifdef config-build
# ===========================================================================
# *config targets only - make sure prerequisites are updated, and descend
# in scripts/kconfig to make the *config target

# Read arch-specific Makefile to set KBUILD_DEFCONFIG as needed.
# KBUILD_DEFCONFIG may point out an alternative default configuration
# used for 'make defconfig'
include $(srctree)/arch/$(SRCARCH)/Makefile
export KBUILD_DEFCONFIG KBUILD_KCONFIG CC_VERSION_TEXT RUSTC_VERSION_TEXT

config: outputmakefile scripts_basic FORCE
	$(Q)$(MAKE) $(build)=scripts/kconfig $@

%config: outputmakefile scripts_basic FORCE
	$(Q)$(MAKE) $(build)=scripts/kconfig $@

else #!config-build
# ===========================================================================
# Build targets only - this includes vmlinux, arch-specific targets, clean
# targets and others. In general all targets except *config targets.

# If building an external module we do not care about the all: rule
# but instead __all depend on modules
PHONY += all
ifeq ($(KBUILD_EXTMOD),)
__all: all
else
__all: modules
endif

targets :=

# Decide whether to build built-in, modular, or both.
# Normally, just do built-in.

KBUILD_MODULES :=
KBUILD_BUILTIN := y

# If we have only "make modules", don't compile built-in objects.
ifeq ($(MAKECMDGOALS),modules)
  KBUILD_BUILTIN :=
endif

# If we have "make <whatever> modules", compile modules
# in addition to whatever we do anyway.
# Just "make" or "make all" shall build modules as well

ifneq ($(filter all modules nsdeps compile_commands.json clang-%,$(MAKECMDGOALS)),)
  KBUILD_MODULES := y
endif

ifeq ($(MAKECMDGOALS),)
  KBUILD_MODULES := y
endif

export KBUILD_MODULES KBUILD_BUILTIN

ifdef need-config
include $(objtree)/include/config/auto.conf
endif

ifeq ($(KBUILD_EXTMOD),)
# Objects we will link into vmlinux / subdirs we need to visit
core-y		:=
drivers-y	:=
libs-y		:= lib/
endif # KBUILD_EXTMOD

# The all: target is the default when no target is given on the
# command line.
# This allow a user to issue only 'make' to build a kernel including modules
# Defaults to vmlinux, but the arch makefile usually adds further targets
all: vmlinux

CFLAGS_GCOV	:= -fprofile-arcs -ftest-coverage
ifdef CONFIG_CC_IS_GCC
CFLAGS_GCOV	+= -fno-tree-loop-im
endif
export CFLAGS_GCOV

# The arch Makefiles can override CC_FLAGS_FTRACE. We may also append it later.
ifdef CONFIG_FUNCTION_TRACER
  CC_FLAGS_FTRACE := -pg
endif

include $(srctree)/arch/$(SRCARCH)/Makefile

ifdef need-config
ifdef may-sync-config
# Read in dependencies to all Kconfig* files, make sure to run syncconfig if
# changes are detected. This should be included after arch/$(SRCARCH)/Makefile
# because some architectures define CROSS_COMPILE there.
include include/config/auto.conf.cmd

$(KCONFIG_CONFIG):
	@echo >&2 '***'
	@echo >&2 '*** Configuration file "$@" not found!'
	@echo >&2 '***'
	@echo >&2 '*** Please run some configurator (e.g. "make oldconfig" or'
	@echo >&2 '*** "make menuconfig" or "make xconfig").'
	@echo >&2 '***'
	@/bin/false

# The actual configuration files used during the build are stored in
# include/generated/ and include/config/. Update them if .config is newer than
# include/config/auto.conf (which mirrors .config).
#
# This exploits the 'multi-target pattern rule' trick.
# The syncconfig should be executed only once to make all the targets.
# (Note: use the grouped target '&:' when we bump to GNU Make 4.3)
#
# Do not use $(call cmd,...) here. That would suppress prompts from syncconfig,
# so you cannot notice that Kconfig is waiting for the user input.
%/config/auto.conf %/config/auto.conf.cmd %/generated/autoconf.h %/generated/rustc_cfg: $(KCONFIG_CONFIG)
	$(Q)$(kecho) "  SYNC    $@"
	$(Q)$(MAKE) -f $(srctree)/Makefile syncconfig
else # !may-sync-config
# External modules and some install targets need include/generated/autoconf.h
# and include/config/auto.conf but do not care if they are up-to-date.
# Use auto.conf to show the error message

checked-configs := $(addprefix $(objtree)/, include/generated/autoconf.h include/generated/rustc_cfg include/config/auto.conf)
missing-configs := $(filter-out $(wildcard $(checked-configs)), $(checked-configs))

ifdef missing-configs
PHONY += $(objtree)/include/config/auto.conf

$(objtree)/include/config/auto.conf:
	@echo   >&2 '***'
	@echo   >&2 '***  ERROR: Kernel configuration is invalid. The following files are missing:'
	@printf >&2 '***    - %s\n' $(missing-configs)
	@echo   >&2 '***  Run "make oldconfig && make prepare" on kernel source to fix it.'
	@echo   >&2 '***'
	@/bin/false
endif

endif # may-sync-config
endif # need-config

KBUILD_CFLAGS	+= -fno-delete-null-pointer-checks

ifdef CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE
KBUILD_CFLAGS += -O2
KBUILD_RUSTFLAGS += -Copt-level=2
else ifdef CONFIG_CC_OPTIMIZE_FOR_SIZE
KBUILD_CFLAGS += -Os
KBUILD_RUSTFLAGS += -Copt-level=s
endif

# Always set `debug-assertions` and `overflow-checks` because their default
# depends on `opt-level` and `debug-assertions`, respectively.
KBUILD_RUSTFLAGS += -Cdebug-assertions=$(if $(CONFIG_RUST_DEBUG_ASSERTIONS),y,n)
KBUILD_RUSTFLAGS += -Coverflow-checks=$(if $(CONFIG_RUST_OVERFLOW_CHECKS),y,n)

# Tell gcc to never replace conditional load with a non-conditional one
ifdef CONFIG_CC_IS_GCC
# gcc-10 renamed --param=allow-store-data-races=0 to
# -fno-allow-store-data-races.
KBUILD_CFLAGS	+= $(call cc-option,--param=allow-store-data-races=0)
KBUILD_CFLAGS	+= $(call cc-option,-fno-allow-store-data-races)
endif

ifdef CONFIG_READABLE_ASM
# Disable optimizations that make assembler listings hard to read.
# reorder blocks reorders the control in the function
# ipa clone creates specialized cloned functions
# partial inlining inlines only parts of functions
KBUILD_CFLAGS += -fno-reorder-blocks -fno-ipa-cp-clone -fno-partial-inlining
endif

stackp-flags-y                                    := -fno-stack-protector
stackp-flags-$(CONFIG_STACKPROTECTOR)             := -fstack-protector
stackp-flags-$(CONFIG_STACKPROTECTOR_STRONG)      := -fstack-protector-strong

KBUILD_CFLAGS += $(stackp-flags-y)

KBUILD_RUSTFLAGS-$(CONFIG_WERROR) += -Dwarnings
KBUILD_RUSTFLAGS += $(KBUILD_RUSTFLAGS-y)

ifdef CONFIG_FRAME_POINTER
KBUILD_CFLAGS	+= -fno-omit-frame-pointer -fno-optimize-sibling-calls
KBUILD_RUSTFLAGS += -Cforce-frame-pointers=y
else
# Some targets (ARM with Thumb2, for example), can't be built with frame
# pointers.  For those, we don't have FUNCTION_TRACER automatically
# select FRAME_POINTER.  However, FUNCTION_TRACER adds -pg, and this is
# incompatible with -fomit-frame-pointer with current GCC, so we don't use
# -fomit-frame-pointer with FUNCTION_TRACER.
# In the Rust target specification, "frame-pointer" is set explicitly
# to "may-omit".
ifndef CONFIG_FUNCTION_TRACER
KBUILD_CFLAGS	+= -fomit-frame-pointer
endif
endif

# Initialize all stack variables with a 0xAA pattern.
ifdef CONFIG_INIT_STACK_ALL_PATTERN
KBUILD_CFLAGS	+= -ftrivial-auto-var-init=pattern
endif

# Initialize all stack variables with a zero value.
ifdef CONFIG_INIT_STACK_ALL_ZERO
KBUILD_CFLAGS	+= -ftrivial-auto-var-init=zero
ifdef CONFIG_CC_HAS_AUTO_VAR_INIT_ZERO_ENABLER
# https://github.com/llvm/llvm-project/issues/44842
CC_AUTO_VAR_INIT_ZERO_ENABLER := -enable-trivial-auto-var-init-zero-knowing-it-will-be-removed-from-clang
export CC_AUTO_VAR_INIT_ZERO_ENABLER
KBUILD_CFLAGS	+= $(CC_AUTO_VAR_INIT_ZERO_ENABLER)
endif
endif

# Explicitly clear padding bits during variable initialization
KBUILD_CFLAGS += $(call cc-option,-fzero-init-padding-bits=all)

# While VLAs have been removed, GCC produces unreachable stack probes
# for the randomize_kstack_offset feature. Disable it for all compilers.
KBUILD_CFLAGS	+= $(call cc-option, -fno-stack-clash-protection)

# Clear used registers at func exit (to reduce data lifetime and ROP gadgets).
ifdef CONFIG_ZERO_CALL_USED_REGS
KBUILD_CFLAGS	+= -fzero-call-used-regs=used-gpr
endif

ifdef CONFIG_FUNCTION_TRACER
ifdef CONFIG_FTRACE_MCOUNT_USE_CC
  CC_FLAGS_FTRACE	+= -mrecord-mcount
  ifdef CONFIG_HAVE_NOP_MCOUNT
    ifeq ($(call cc-option-yn, -mnop-mcount),y)
      CC_FLAGS_FTRACE	+= -mnop-mcount
      CC_FLAGS_USING	+= -DCC_USING_NOP_MCOUNT
    endif
  endif
endif
ifdef CONFIG_FTRACE_MCOUNT_USE_OBJTOOL
  ifdef CONFIG_HAVE_OBJTOOL_NOP_MCOUNT
    CC_FLAGS_USING	+= -DCC_USING_NOP_MCOUNT
  endif
endif
ifdef CONFIG_FTRACE_MCOUNT_USE_RECORDMCOUNT
  ifdef CONFIG_HAVE_C_RECORDMCOUNT
    BUILD_C_RECORDMCOUNT := y
    export BUILD_C_RECORDMCOUNT
  endif
endif
ifdef CONFIG_HAVE_FENTRY
  # s390-linux-gnu-gcc did not support -mfentry until gcc-9.
  ifeq ($(call cc-option-yn, -mfentry),y)
    CC_FLAGS_FTRACE	+= -mfentry
    CC_FLAGS_USING	+= -DCC_USING_FENTRY
  endif
endif
export CC_FLAGS_FTRACE
KBUILD_CFLAGS	+= $(CC_FLAGS_FTRACE) $(CC_FLAGS_USING)
KBUILD_AFLAGS	+= $(CC_FLAGS_USING)
endif

# We trigger additional mismatches with less inlining
ifdef CONFIG_DEBUG_SECTION_MISMATCH
KBUILD_CFLAGS += -fno-inline-functions-called-once
endif

# `rustc`'s `-Zfunction-sections` applies to data too (as of 1.59.0).
ifdef CONFIG_LD_DEAD_CODE_DATA_ELIMINATION
KBUILD_CFLAGS_KERNEL += -ffunction-sections -fdata-sections
KBUILD_RUSTFLAGS_KERNEL += -Zfunction-sections=y
LDFLAGS_vmlinux += --gc-sections
endif

ifdef CONFIG_SHADOW_CALL_STACK
ifndef CONFIG_DYNAMIC_SCS
CC_FLAGS_SCS	:= -fsanitize=shadow-call-stack
KBUILD_CFLAGS	+= $(CC_FLAGS_SCS)
KBUILD_RUSTFLAGS += -Zsanitizer=shadow-call-stack
endif
export CC_FLAGS_SCS
endif

ifdef CONFIG_LTO_CLANG
ifdef CONFIG_LTO_CLANG_THIN
CC_FLAGS_LTO	:= -flto=thin -fsplit-lto-unit
else
CC_FLAGS_LTO	:= -flto
endif
CC_FLAGS_LTO	+= -fvisibility=hidden

# Limit inlining across translation units to reduce binary size
KBUILD_LDFLAGS += -mllvm -import-instr-limit=5
endif

ifdef CONFIG_LTO
KBUILD_CFLAGS	+= -fno-lto $(CC_FLAGS_LTO)
KBUILD_AFLAGS	+= -fno-lto
export CC_FLAGS_LTO
endif

ifdef CONFIG_CFI_CLANG
CC_FLAGS_CFI	:= -fsanitize=kcfi
ifdef CONFIG_CFI_ICALL_NORMALIZE_INTEGERS
	CC_FLAGS_CFI	+= -fsanitize-cfi-icall-experimental-normalize-integers
endif
ifdef CONFIG_FINEIBT_BHI
	CC_FLAGS_CFI	+= -fsanitize-kcfi-arity
endif
ifdef CONFIG_RUST
	# Always pass -Zsanitizer-cfi-normalize-integers as CONFIG_RUST selects
	# CONFIG_CFI_ICALL_NORMALIZE_INTEGERS.
	RUSTC_FLAGS_CFI   := -Zsanitizer=kcfi -Zsanitizer-cfi-normalize-integers
	KBUILD_RUSTFLAGS += $(RUSTC_FLAGS_CFI)
	export RUSTC_FLAGS_CFI
endif
KBUILD_CFLAGS	+= $(CC_FLAGS_CFI)
export CC_FLAGS_CFI
endif

# Architectures can define flags to add/remove for floating-point support
CC_FLAGS_FPU	+= -D_LINUX_FPU_COMPILATION_UNIT
export CC_FLAGS_FPU
export CC_FLAGS_NO_FPU

ifneq ($(CONFIG_FUNCTION_ALIGNMENT),0)
# Set the minimal function alignment. Use the newer GCC option
# -fmin-function-alignment if it is available, or fall back to -falign-funtions.
# See also CONFIG_CC_HAS_SANE_FUNCTION_ALIGNMENT.
ifdef CONFIG_CC_HAS_MIN_FUNCTION_ALIGNMENT
KBUILD_CFLAGS += -fmin-function-alignment=$(CONFIG_FUNCTION_ALIGNMENT)
else
KBUILD_CFLAGS += -falign-functions=$(CONFIG_FUNCTION_ALIGNMENT)
endif
endif

# arch Makefile may override CC so keep this after arch Makefile is included
NOSTDINC_FLAGS += -nostdinc

# To gain proper coverage for CONFIG_UBSAN_BOUNDS and CONFIG_FORTIFY_SOURCE,
# the kernel uses only C99 flexible arrays for dynamically sized trailing
# arrays. Enforce this for everything that may examine structure sizes and
# perform bounds checking.
KBUILD_CFLAGS += $(call cc-option, -fstrict-flex-arrays=3)

# disable invalid "can't wrap" optimizations for signed / pointers
KBUILD_CFLAGS	+= -fno-strict-overflow

# Make sure -fstack-check isn't enabled (like gentoo apparently did)
KBUILD_CFLAGS  += -fno-stack-check

# conserve stack if available
ifdef CONFIG_CC_IS_GCC
KBUILD_CFLAGS   += -fconserve-stack
endif

# Ensure compilers do not transform certain loops into calls to wcslen()
KBUILD_CFLAGS += -fno-builtin-wcslen

# change __FILE__ to the relative path to the source directory
ifdef building_out_of_srctree
KBUILD_CPPFLAGS += $(call cc-option,-fmacro-prefix-map=$(srcroot)/=)
endif

# include additional Makefiles when needed
include-y			:= scripts/Makefile.extrawarn
include-$(CONFIG_DEBUG_INFO)	+= scripts/Makefile.debug
include-$(CONFIG_DEBUG_INFO_BTF)+= scripts/Makefile.btf
include-$(CONFIG_KASAN)		+= scripts/Makefile.kasan
include-$(CONFIG_KCSAN)		+= scripts/Makefile.kcsan
include-$(CONFIG_KMSAN)		+= scripts/Makefile.kmsan
include-$(CONFIG_UBSAN)		+= scripts/Makefile.ubsan
include-$(CONFIG_KCOV)		+= scripts/Makefile.kcov
include-$(CONFIG_RANDSTRUCT)	+= scripts/Makefile.randstruct
include-$(CONFIG_KSTACK_ERASE)	+= scripts/Makefile.kstack_erase
include-$(CONFIG_AUTOFDO_CLANG)	+= scripts/Makefile.autofdo
include-$(CONFIG_PROPELLER_CLANG)	+= scripts/Makefile.propeller
include-$(CONFIG_GCC_PLUGINS)	+= scripts/Makefile.gcc-plugins

include $(addprefix $(srctree)/, $(include-y))

# scripts/Makefile.gcc-plugins is intentionally included last.
# Do not add $(call cc-option,...) below this line. When you build the kernel
# from the clean source tree, the GCC plugins do not exist at this point.

# Add user supplied CPPFLAGS, AFLAGS, CFLAGS and RUSTFLAGS as the last assignments
KBUILD_CPPFLAGS += $(KCPPFLAGS)
KBUILD_AFLAGS   += $(KAFLAGS)
KBUILD_CFLAGS   += $(KCFLAGS)
KBUILD_RUSTFLAGS += $(KRUSTFLAGS)

KBUILD_LDFLAGS_MODULE += --build-id=sha1
LDFLAGS_vmlinux += --build-id=sha1

KBUILD_LDFLAGS	+= -z noexecstack
ifeq ($(CONFIG_LD_IS_BFD),y)
KBUILD_LDFLAGS	+= $(call ld-option,--no-warn-rwx-segments)
endif

ifeq ($(CONFIG_STRIP_ASM_SYMS),y)
LDFLAGS_vmlinux	+= -X
endif

ifeq ($(CONFIG_RELR),y)
# ld.lld before 15 did not support -z pack-relative-relocs.
LDFLAGS_vmlinux	+= $(call ld-option,--pack-dyn-relocs=relr,-z pack-relative-relocs)
endif

# We never want expected sections to be placed heuristically by the
# linker. All sections should be explicitly named in the linker script.
ifdef CONFIG_LD_ORPHAN_WARN
LDFLAGS_vmlinux += --orphan-handling=$(CONFIG_LD_ORPHAN_WARN_LEVEL)
endif

ifneq ($(CONFIG_ARCH_VMLINUX_NEEDS_RELOCS),)
LDFLAGS_vmlinux	+= --emit-relocs --discard-none
endif

# Align the bit size of userspace programs with the kernel
KBUILD_USERCFLAGS  += $(filter -m32 -m64 --target=%, $(KBUILD_CPPFLAGS) $(KBUILD_CFLAGS))
KBUILD_USERLDFLAGS += $(filter -m32 -m64 --target=%, $(KBUILD_CPPFLAGS) $(KBUILD_CFLAGS))

# userspace programs are linked via the compiler, use the correct linker
ifdef CONFIG_CC_IS_CLANG
KBUILD_USERLDFLAGS += --ld-path=$(LD)
endif

# make the checker run with the right architecture
CHECKFLAGS += --arch=$(ARCH)

# insure the checker run with the right endianness
CHECKFLAGS += $(if $(CONFIG_CPU_BIG_ENDIAN),-mbig-endian,-mlittle-endian)

# the checker needs the correct machine size
CHECKFLAGS += $(if $(CONFIG_64BIT),-m64,-m32)

# Default kernel image to build when no specific target is given.
# KBUILD_IMAGE may be overruled on the command line or
# set in the environment
# Also any assignments in arch/$(ARCH)/Makefile take precedence over
# this default value
export KBUILD_IMAGE ?= vmlinux

#
# INSTALL_PATH specifies where to place the updated kernel and system map
# images. Default is /boot, but you can set it to other values
export	INSTALL_PATH ?= /boot

#
# INSTALL_DTBS_PATH specifies a prefix for relocations required by build roots.
# Like INSTALL_MOD_PATH, it isn't defined in the Makefile, but can be passed as
# an argument if needed. Otherwise it defaults to the kernel install path
#
export INSTALL_DTBS_PATH ?= $(INSTALL_PATH)/dtbs/$(KERNELRELEASE)

#
# INSTALL_MOD_PATH specifies a prefix to MODLIB for module directory
# relocations required by build roots.  This is not defined in the
# makefile but the argument can be passed to make if needed.
#

MODLIB	= $(INSTALL_MOD_PATH)/lib/modules/$(KERNELRELEASE)
export MODLIB

PHONY += prepare0

ifeq ($(KBUILD_EXTMOD),)

build-dir	:= .
clean-dirs	:= $(sort . Documentation \
		     $(patsubst %/,%,$(filter %/, $(core-) \
			$(drivers-) $(libs-))))

export ARCH_CORE	:= $(core-y)
export ARCH_LIB		:= $(filter %/, $(libs-y))
export ARCH_DRIVERS	:= $(drivers-y) $(drivers-m)
# Externally visible symbols (used by link-vmlinux.sh)

KBUILD_VMLINUX_OBJS := built-in.a $(patsubst %/, %/lib.a, $(filter %/, $(libs-y)))
KBUILD_VMLINUX_LIBS := $(filter-out %/, $(libs-y))

export KBUILD_VMLINUX_LIBS
export KBUILD_LDS          := arch/$(SRCARCH)/kernel/vmlinux.lds

ifdef CONFIG_TRIM_UNUSED_KSYMS
# For the kernel to actually contain only the needed exported symbols,
# we have to build modules as well to determine what those symbols are.
KBUILD_MODULES := y
endif

# '$(AR) mPi' needs 'T' to workaround the bug of llvm-ar <= 14
quiet_cmd_ar_vmlinux.a = AR      $@
      cmd_ar_vmlinux.a = \
	rm -f $@; \
	$(AR) cDPrST $@ $(KBUILD_VMLINUX_OBJS); \
	$(AR) mPiT $$($(AR) t $@ | sed -n 1p) $@ $$($(AR) t $@ | grep -F -f $(srctree)/scripts/head-object-list.txt)

targets += vmlinux.a
vmlinux.a: $(KBUILD_VMLINUX_OBJS) scripts/head-object-list.txt FORCE
	$(call if_changed,ar_vmlinux.a)

PHONY += vmlinux_o
vmlinux_o: vmlinux.a $(KBUILD_VMLINUX_LIBS)
	$(Q)$(MAKE) -f $(srctree)/scripts/Makefile.vmlinux_o

vmlinux.o modules.builtin.modinfo modules.builtin: vmlinux_o
	@:

PHONY += vmlinux
# LDFLAGS_vmlinux in the top Makefile defines linker flags for the top vmlinux,
# not for decompressors. LDFLAGS_vmlinux in arch/*/boot/compressed/Makefile is
# unrelated; the decompressors just happen to have the same base name,
# arch/*/boot/compressed/vmlinux.
# Export LDFLAGS_vmlinux only to scripts/Makefile.vmlinux.
#
# _LDFLAGS_vmlinux is a workaround for the 'private export' bug:
#   https://savannah.gnu.org/bugs/?61463
# For Make > 4.4, the following simple code will work:
#  vmlinux: private export LDFLAGS_vmlinux := $(LDFLAGS_vmlinux)
vmlinux: private _LDFLAGS_vmlinux := $(LDFLAGS_vmlinux)
vmlinux: export LDFLAGS_vmlinux = $(_LDFLAGS_vmlinux)
vmlinux: vmlinux.o $(KBUILD_LDS) modpost
	$(Q)$(MAKE) -f $(srctree)/scripts/Makefile.vmlinux

# The actual objects are generated when descending,
# make sure no implicit rule kicks in
$(sort $(KBUILD_LDS) $(KBUILD_VMLINUX_OBJS) $(KBUILD_VMLINUX_LIBS)): . ;

ifeq ($(origin KERNELRELEASE),file)
filechk_kernel.release = $(srctree)/scripts/setlocalversion $(srctree)
else
filechk_kernel.release = echo $(KERNELRELEASE)
endif

# Store (new) KERNELRELEASE string in include/config/kernel.release
include/config/kernel.release: FORCE
	$(call filechk,kernel.release)

# Additional helpers built in scripts/
# Carefully list dependencies so we do not try to build scripts twice
# in parallel
PHONY += scripts
scripts: scripts_basic scripts_dtc
	$(Q)$(MAKE) $(build)=$(@)

# Things we need to do before we recursively start building the kernel
# or the modules are listed in "prepare".
# A multi level approach is used. prepareN is processed before prepareN-1.
# archprepare is used in arch Makefiles and when processed asm symlink,
# version.h and scripts_basic is processed / created.

PHONY += prepare archprepare

archprepare: outputmakefile archheaders archscripts scripts include/config/kernel.release \
	asm-generic $(version_h) include/generated/utsrelease.h \
	include/generated/compile.h include/generated/autoconf.h \
	include/generated/rustc_cfg remove-stale-files

prepare0: archprepare
	$(Q)$(MAKE) $(build)=scripts/mod
	$(Q)$(MAKE) $(build)=. prepare

# All the preparing..
prepare: prepare0
ifdef CONFIG_RUST
	+$(Q)$(CONFIG_SHELL) $(srctree)/scripts/rust_is_available.sh
	$(Q)$(MAKE) $(build)=rust
endif

PHONY += remove-stale-files
remove-stale-files:
	$(Q)$(srctree)/scripts/remove-stale-files

# Support for using generic headers in asm-generic
asm-generic := -f $(srctree)/scripts/Makefile.asm-headers obj

PHONY += asm-generic uapi-asm-generic
asm-generic: uapi-asm-generic
	$(Q)$(MAKE) $(asm-generic)=arch/$(SRCARCH)/include/generated/asm \
	generic=include/asm-generic
uapi-asm-generic:
	$(Q)$(MAKE) $(asm-generic)=arch/$(SRCARCH)/include/generated/uapi/asm \
	generic=include/uapi/asm-generic

# Generate some files
# ---------------------------------------------------------------------------

# KERNELRELEASE can change from a few different places, meaning version.h
# needs to be updated, so this check is forced on all builds

uts_len := 64
define filechk_utsrelease.h
	if [ `echo -n "$(KERNELRELEASE)" | wc -c ` -gt $(uts_len) ]; then \
	  echo '"$(KERNELRELEASE)" exceeds $(uts_len) characters' >&2;    \
	  exit 1;                                                         \
	fi;                                                               \
	echo \#define UTS_RELEASE \"$(KERNELRELEASE)\"
endef

define filechk_version.h
	if [ $(SUBLEVEL) -gt 255 ]; then                                 \
		echo \#define LINUX_VERSION_CODE $(shell                 \
		expr $(VERSION) \* 65536 + $(PATCHLEVEL) \* 256 + 255); \
	else                                                             \
		echo \#define LINUX_VERSION_CODE $(shell                 \
		expr $(VERSION) \* 65536 + $(PATCHLEVEL) \* 256 + $(SUBLEVEL)); \
	fi;                                                              \
	echo '#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) +  \
	((c) > 255 ? 255 : (c)))';                                       \
	echo \#define LINUX_VERSION_MAJOR $(VERSION);                    \
	echo \#define LINUX_VERSION_PATCHLEVEL $(PATCHLEVEL);            \
	echo \#define LINUX_VERSION_SUBLEVEL $(SUBLEVEL)
endef

$(version_h): private PATCHLEVEL := $(or $(PATCHLEVEL), 0)
$(version_h): private SUBLEVEL := $(or $(SUBLEVEL), 0)
$(version_h): FORCE
	$(call filechk,version.h)

include/generated/utsrelease.h: include/config/kernel.release FORCE
	$(call filechk,utsrelease.h)

filechk_compile.h = $(srctree)/scripts/mkcompile_h \
	"$(UTS_MACHINE)" "$(CONFIG_CC_VERSION_TEXT)" "$(LD)"

include/generated/compile.h: FORCE
	$(call filechk,compile.h)

PHONY += headerdep
headerdep:
	$(Q)find $(srctree)/include/ -name '*.h' | xargs --max-args 1 \
	$(srctree)/scripts/headerdep.pl -I$(srctree)/include

# ---------------------------------------------------------------------------
# Kernel headers

#Default location for installed headers
export INSTALL_HDR_PATH = $(objtree)/usr

quiet_cmd_headers_install = INSTALL $(INSTALL_HDR_PATH)/include
      cmd_headers_install = \
	mkdir -p $(INSTALL_HDR_PATH); \
	rsync -mrl --include='*/' --include='*\.h' --exclude='*' \
	usr/include $(INSTALL_HDR_PATH)

PHONY += headers_install
headers_install: headers
	$(call cmd,headers_install)

PHONY += archheaders archscripts

hdr-inst := -f $(srctree)/scripts/Makefile.headersinst obj

PHONY += headers
headers: $(version_h) scripts_unifdef uapi-asm-generic archheaders
ifdef HEADER_ARCH
	$(Q)$(MAKE) -f $(srctree)/Makefile HEADER_ARCH= SRCARCH=$(HEADER_ARCH) headers
else
	$(Q)$(MAKE) $(hdr-inst)=include/uapi
	$(Q)$(MAKE) $(hdr-inst)=arch/$(SRCARCH)/include/uapi
endif

ifdef CONFIG_HEADERS_INSTALL
prepare: headers
endif

PHONY += scripts_unifdef
scripts_unifdef: scripts_basic
	$(Q)$(MAKE) $(build)=scripts scripts/unifdef

PHONY += scripts_gen_packed_field_checks
scripts_gen_packed_field_checks: scripts_basic
	$(Q)$(MAKE) $(build)=scripts scripts/gen_packed_field_checks

# ---------------------------------------------------------------------------
# Install

# Many distributions have the custom install script, /sbin/installkernel.
# If DKMS is installed, 'make install' will eventually recurse back
# to this Makefile to build and install external modules.
# Cancel sub_make_done so that options such as M=, V=, etc. are parsed.

quiet_cmd_install = INSTALL $(INSTALL_PATH)
      cmd_install = unset sub_make_done; $(srctree)/scripts/install.sh

# ---------------------------------------------------------------------------
# vDSO install

PHONY += vdso_install
vdso_install: export INSTALL_FILES = $(vdso-install-y)
vdso_install:
	$(Q)$(MAKE) -f $(srctree)/scripts/Makefile.vdsoinst

# ---------------------------------------------------------------------------
# Tools

ifdef CONFIG_OBJTOOL
prepare: tools/objtool
endif

ifdef CONFIG_BPF
ifdef CONFIG_DEBUG_INFO_BTF
prepare: tools/bpf/resolve_btfids
endif
endif

# The tools build system is not a part of Kbuild and tends to introduce
# its own unique issues. If you need to integrate a new tool into Kbuild,
# please consider locating that tool outside the tools/ tree and using the
# standard Kbuild "hostprogs" syntax instead of adding a new tools/* entry
# here. See Documentation/kbuild/makefiles.rst for details.

PHONY += resolve_btfids_clean

resolve_btfids_O = $(abspath $(objtree))/tools/bpf/resolve_btfids

# tools/bpf/resolve_btfids directory might not exist
# in output directory, skip its clean in that case
resolve_btfids_clean:
ifneq ($(wildcard $(resolve_btfids_O)),)
	$(Q)$(MAKE) -sC $(srctree)/tools/bpf/resolve_btfids O=$(resolve_btfids_O) clean
endif

tools/: FORCE
	$(Q)mkdir -p $(objtree)/tools
	$(Q)$(MAKE) LDFLAGS= O=$(abspath $(objtree)) subdir=tools -C $(srctree)/tools/

tools/%: FORCE
	$(Q)mkdir -p $(objtree)/tools
	$(Q)$(MAKE) LDFLAGS= O=$(abspath $(objtree)) subdir=tools -C $(srctree)/tools/ $*

# ---------------------------------------------------------------------------
# Kernel selftest

PHONY += kselftest
kselftest: headers
	$(Q)$(MAKE) -C $(srctree)/tools/testing/selftests run_tests

kselftest-%: headers FORCE
	$(Q)$(MAKE) -C $(srctree)/tools/testing/selftests $*

PHONY += kselftest-merge
kselftest-merge:
	$(if $(wildcard $(objtree)/.config),, $(error No .config exists, config your kernel first!))
	$(Q)find $(srctree)/tools/testing/selftests -name config -o -name config.$(UTS_MACHINE) | \
		xargs $(srctree)/scripts/kconfig/merge_config.sh -y -m $(objtree)/.config
	$(Q)$(MAKE) -f $(srctree)/Makefile olddefconfig

# ---------------------------------------------------------------------------
# Devicetree files

ifneq ($(wildcard $(srctree)/arch/$(SRCARCH)/boot/dts/),)
dtstree := arch/$(SRCARCH)/boot/dts
endif

ifneq ($(dtstree),)

%.dtb: dtbs_prepare
	$(Q)$(MAKE) $(build)=$(dtstree) $(dtstree)/$@

%.dtbo: dtbs_prepare
	$(Q)$(MAKE) $(build)=$(dtstree) $(dtstree)/$@

PHONY += dtbs dtbs_prepare dtbs_install dtbs_check
dtbs: dtbs_prepare
	$(Q)$(MAKE) $(build)=$(dtstree) need-dtbslist=1

# include/config/kernel.release is actually needed when installing DTBs because
# INSTALL_DTBS_PATH contains $(KERNELRELEASE). However, we do not want to make
# dtbs_install depend on it as dtbs_install may run as root.
dtbs_prepare: include/config/kernel.release scripts_dtc

ifneq ($(filter dtbs_check, $(MAKECMDGOALS)),)
export CHECK_DTBS=y
endif

ifneq ($(CHECK_DTBS),)
dtbs_prepare: dt_binding_schemas
endif

dtbs_check: dtbs

dtbs_install:
	$(Q)$(MAKE) -f $(srctree)/scripts/Makefile.dtbinst obj=$(dtstree)

ifdef CONFIG_OF_EARLY_FLATTREE
all: dtbs
endif

ifdef CONFIG_GENERIC_BUILTIN_DTB
vmlinux: dtbs
endif

endif

PHONY += scripts_dtc
scripts_dtc: scripts_basic
	$(Q)$(MAKE) $(build)=scripts/dtc

ifneq ($(filter dt_binding_check, $(MAKECMDGOALS)),)
export CHECK_DTBS=y
endif

PHONY += dt_binding_check dt_binding_schemas
dt_binding_check: dt_binding_schemas scripts_dtc
	$(Q)$(MAKE) $(build)=Documentation/devicetree/bindings $@

dt_binding_schemas:
	$(Q)$(MAKE) $(build)=Documentation/devicetree/bindings

PHONY += dt_compatible_check
dt_compatible_check: dt_binding_schemas
	$(Q)$(MAKE) $(build)=Documentation/devicetree/bindings $@

# ---------------------------------------------------------------------------
# Modules

ifdef CONFIG_MODULES

# By default, build modules as well

all: modules

# When we're building modules with modversions, we need to consider
# the built-in objects during the descend as well, in order to
# make sure the checksums are up to date before we record them.
ifdef CONFIG_MODVERSIONS
  KBUILD_BUILTIN := y
endif

# Build modules
#

# *.ko are usually independent of vmlinux, but CONFIG_DEBUG_INFO_BTF_MODULES
# is an exception.
ifdef CONFIG_DEBUG_INFO_BTF_MODULES
KBUILD_BUILTIN := y
modules: vmlinux
endif

modules: modules_prepare

# Target to prepare building external modules
modules_prepare: prepare
	$(Q)$(MAKE) $(build)=scripts scripts/module.lds

endif # CONFIG_MODULES

###
# Cleaning is done on three levels.
# make clean     Delete most generated files
#                Leave enough to build external modules
# make mrproper  Delete the current configuration, and all generated files
# make distclean Remove editor backup files, patch leftover files and the like

# Directories & files removed with 'make clean'
CLEAN_FILES += vmlinux.symvers modules-only.symvers \
	       modules.builtin modules.builtin.modinfo modules.nsdeps \
	       modules.builtin.ranges vmlinux.o.map vmlinux.unstripped \
	       compile_commands.json rust/test \
	       rust-project.json .vmlinux.objs .vmlinux.export.c \
               .builtin-dtbs-list .builtin-dtb.S

# Directories & files removed with 'make mrproper'
MRPROPER_FILES += include/config include/generated          \
		  arch/$(SRCARCH)/include/generated .objdiff \
		  debian snap tar-install PKGBUILD pacman \
		  .config .config.old .version \
		  Module.symvers \
		  certs/signing_key.pem \
		  certs/x509.genkey \
		  vmlinux-gdb.py \
		  rpmbuild \
		  rust/libmacros.so rust/libmacros.dylib

# clean - Delete most, but leave enough to build external modules
#
clean: private rm-files := $(CLEAN_FILES)

PHONY += archclean vmlinuxclean

vmlinuxclean:
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/link-vmlinux.sh clean
	$(Q)$(if $(ARCH_POSTLINK), $(MAKE) -f $(ARCH_POSTLINK) clean)

clean: archclean vmlinuxclean resolve_btfids_clean

# mrproper - Delete all generated files, including .config
#
mrproper: private rm-files := $(MRPROPER_FILES)
mrproper-dirs      := $(addprefix _mrproper_,scripts)

PHONY += $(mrproper-dirs) mrproper
$(mrproper-dirs):
	$(Q)$(MAKE) $(clean)=$(patsubst _mrproper_%,%,$@)

mrproper: clean $(mrproper-dirs)
	$(call cmd,rmfiles)
	@find . $(RCS_FIND_IGNORE) \
		\( -name '*.rmeta' \) \
		-type f -print | xargs rm -f

# distclean
#
PHONY += distclean

distclean: mrproper
	@find . $(RCS_FIND_IGNORE) \
		\( -name '*.orig' -o -name '*.rej' -o -name '*~' \
		-o -name '*.bak' -o -name '#*#' -o -name '*%' \
		-o -name 'core' -o -name tags -o -name TAGS -o -name 'cscope*' \
		-o -name GPATH -o -name GRTAGS -o -name GSYMS -o -name GTAGS \) \
		-type f -print | xargs rm -f


# Packaging of the kernel to various formats
# ---------------------------------------------------------------------------

%src-pkg: FORCE
	$(Q)$(MAKE) -f $(srctree)/scripts/Makefile.package $@
%pkg: include/config/kernel.release FORCE
	$(Q)$(MAKE) -f $(srctree)/scripts/Makefile.package $@

# Brief documentation of the typical targets used
# ---------------------------------------------------------------------------

boards := $(wildcard $(srctree)/arch/$(SRCARCH)/configs/*_defconfig)
boards := $(sort $(notdir $(boards)))
board-dirs := $(dir $(wildcard $(srctree)/arch/$(SRCARCH)/configs/*/*_defconfig))
board-dirs := $(sort $(notdir $(board-dirs:/=)))

PHONY += help
help:
	@echo  'Cleaning targets:'
	@echo  '  clean		  - Remove most generated files but keep the config and'
	@echo  '                    enough build support to build external modules'
	@echo  '  mrproper	  - Remove all generated files + config + various backup files'
	@echo  '  distclean	  - mrproper + remove editor backup and patch files'
	@echo  ''
	@$(MAKE) -f $(srctree)/scripts/kconfig/Makefile help
	@echo  ''
	@echo  'Other generic targets:'
	@echo  '  all		  - Build all targets marked with [*]'
	@echo  '* vmlinux	  - Build the bare kernel'
	@echo  '* modules	  - Build all modules'
	@echo  '  modules_install - Install all modules to INSTALL_MOD_PATH (default: /)'
	@echo  '  vdso_install    - Install unstripped vdso to INSTALL_MOD_PATH (default: /)'
	@echo  '  dir/            - Build all files in dir and below'
	@echo  '  dir/file.[ois]  - Build specified target only'
	@echo  '  dir/file.ll     - Build the LLVM assembly file'
	@echo  '                    (requires compiler support for LLVM assembly generation)'
	@echo  '  dir/file.lst    - Build specified mixed source/assembly target only'
	@echo  '                    (requires a recent binutils and recent build (System.map))'
	@echo  '  dir/file.ko     - Build module including final link'
	@echo  '  modules_prepare - Set up for building external modules'
	@echo  '  tags/TAGS	  - Generate tags file for editors'
	@echo  '  cscope	  - Generate cscope index'
	@echo  '  gtags           - Generate GNU GLOBAL index'
	@echo  '  kernelrelease	  - Output the release version string (use with make -s)'
	@echo  '  kernelversion	  - Output the version stored in Makefile (use with make -s)'
	@echo  '  image_name	  - Output the image name (use with make -s)'
	@echo  '  headers	  - Build ready-to-install UAPI headers in usr/include'
	@echo  '  headers_install - Install sanitised kernel UAPI headers to INSTALL_HDR_PATH'; \
	 echo  '                    (default: $(INSTALL_HDR_PATH))'; \
	 echo  ''
	@echo  'Static analysers:'
	@echo  '  checkstack      - Generate a list of stack hogs and consider all functions'
	@echo  '                    with a stack size larger than MINSTACKSIZE (default: 100)'
	@echo  '  versioncheck    - Sanity check on version.h usage'
	@echo  '  includecheck    - Check for duplicate included header files'
	@echo  '  headerdep       - Detect inclusion cycles in headers'
	@echo  '  coccicheck      - Check with Coccinelle'
	@echo  '  clang-analyzer  - Check with clang static analyzer'
	@echo  '  clang-tidy      - Check with clang-tidy'
	@echo  ''
	@echo  'Tools:'
	@echo  '  nsdeps          - Generate missing symbol namespace dependencies'
	@echo  ''
	@echo  'Kernel selftest:'
	@echo  '  kselftest         - Build and run kernel selftest'
	@echo  '                      Build, install, and boot kernel before'
	@echo  '                      running kselftest on it'
	@echo  '                      Run as root for full coverage'
	@echo  '  kselftest-all     - Build kernel selftest'
	@echo  '  kselftest-install - Build and install kernel selftest'
	@echo  '  kselftest-clean   - Remove all generated kselftest files'
	@echo  '  kselftest-merge   - Merge all the config dependencies of'
	@echo  '		      kselftest to existing .config.'
	@echo  ''
	@echo  'Rust targets:'
	@echo  '  rustavailable   - Checks whether the Rust toolchain is'
	@echo  '		    available and, if not, explains why.'
	@echo  '  rustfmt	  - Reformat all the Rust code in the kernel'
	@echo  '  rustfmtcheck	  - Checks if all the Rust code in the kernel'
	@echo  '		    is formatted, printing a diff otherwise.'
	@echo  '  rustdoc	  - Generate Rust documentation'
	@echo  '		    (requires kernel .config)'
	@echo  '  rusttest        - Runs the Rust tests'
	@echo  '                    (requires kernel .config; downloads external repos)'
	@echo  '  rust-analyzer	  - Generate rust-project.json rust-analyzer support file'
	@echo  '		    (requires kernel .config)'
	@echo  '  dir/file.[os]   - Build specified target only'
	@echo  '  dir/file.rsi    - Build macro expanded source, similar to C preprocessing.'
	@echo  '                    Run with RUSTFMT=n to skip reformatting if needed.'
	@echo  '                    The output is not intended to be compilable.'
	@echo  '  dir/file.ll     - Build the LLVM assembly file'
	@echo  ''
	@$(if $(dtstree), \
		echo 'Devicetree:'; \
		echo '* dtbs               - Build device tree blobs for enabled boards'; \
		echo '  dtbs_install       - Install dtbs to $(INSTALL_DTBS_PATH)'; \
		echo '  dt_binding_check   - Validate device tree binding documents and examples'; \
		echo '  dt_binding_schemas - Build processed device tree binding schemas'; \
		echo '  dtbs_check         - Validate device tree source files';\
		echo '')

	@echo 'Userspace tools targets:'
	@echo '  use "make tools/help"'
	@echo '  or  "cd tools; make help"'
	@echo  ''
	@echo  'Kernel packaging:'
	@$(MAKE) -f $(srctree)/scripts/Makefile.package help
	@echo  ''
	@echo  'Documentation targets:'
	@$(MAKE) -f $(srctree)/Documentation/Makefile dochelp
	@echo  ''
	@echo  'Architecture-specific targets ($(SRCARCH)):'
	@$(or $(archhelp),\
		echo '  No architecture-specific help defined for $(SRCARCH)')
	@echo  ''
	@$(if $(boards), \
		$(foreach b, $(boards), \
		printf "  %-27s - Build for %s\\n" $(b) $(subst _defconfig,,$(b));) \
		echo '')
	@$(if $(board-dirs), \
		$(foreach b, $(board-dirs), \
		printf "  %-16s - Show %s-specific targets\\n" help-$(b) $(b);) \
		printf "  %-16s - Show all of the above\\n" help-boards; \
		echo '')

	@echo  '  make V=n   [targets] 1: verbose build'
	@echo  '                       2: give reason for rebuild of target'
	@echo  '                       V=1 and V=2 can be combined with V=12'
	@echo  '  make O=dir [targets] Locate all output files in "dir", including .config'
	@echo  '  make C=1   [targets] Check re-compiled c source with $$CHECK'
	@echo  '                       (sparse by default)'
	@echo  '  make C=2   [targets] Force check of all c source with $$CHECK'
	@echo  '  make RECORDMCOUNT_WARN=1 [targets] Warn about ignored mcount sections'
	@echo  '  make W=n   [targets] Enable extra build checks, n=1,2,3,c,e where'
	@echo  '		1: warnings which may be relevant and do not occur too often'
	@echo  '		2: warnings which occur quite often but may still be relevant'
	@echo  '		3: more obscure warnings, can most likely be ignored'
	@echo  '		c: extra checks in the configuration stage (Kconfig)'
	@echo  '		e: warnings are being treated as errors'
	@echo  '		Multiple levels can be combined with W=12 or W=123'
	@$(if $(dtstree), \
		echo '  make CHECK_DTBS=1 [targets] Check all generated dtb files against schema'; \
		echo '         This can be applied both to "dtbs" and to individual "foo.dtb" targets' ; \
		)
	@echo  ''
	@echo  'Execute "make" or "make all" to build all targets marked with [*] '
	@echo  'For further info see the ./README file'


help-board-dirs := $(addprefix help-,$(board-dirs))

help-boards: $(help-board-dirs)

boards-per-dir = $(sort $(notdir $(wildcard $(srctree)/arch/$(SRCARCH)/configs/$*/*_defconfig)))

$(help-board-dirs): help-%:
	@echo  'Architecture-specific targets ($(SRCARCH) $*):'
	@$(if $(boards-per-dir), \
		$(foreach b, $(boards-per-dir), \
		printf "  %-24s - Build for %s\\n" $*/$(b) $(subst _defconfig,,$(b));) \
		echo '')


# Documentation targets
# ---------------------------------------------------------------------------
DOC_TARGETS := xmldocs latexdocs pdfdocs htmldocs epubdocs cleandocs \
	       linkcheckdocs dochelp refcheckdocs texinfodocs infodocs
PHONY += $(DOC_TARGETS)
$(DOC_TARGETS):
	$(Q)$(MAKE) $(build)=Documentation $@


# Rust targets
# ---------------------------------------------------------------------------

# "Is Rust available?" target
PHONY += rustavailable
rustavailable:
	+$(Q)$(CONFIG_SHELL) $(srctree)/scripts/rust_is_available.sh && echo "Rust is available!"

# Documentation target
#
# Using the singular to avoid running afoul of `no-dot-config-targets`.
PHONY += rustdoc
rustdoc: prepare
	$(Q)$(MAKE) $(build)=rust $@

# Testing target
PHONY += rusttest
rusttest: prepare
	$(Q)$(MAKE) $(build)=rust $@

# Formatting targets
PHONY += rustfmt rustfmtcheck

rustfmt:
	$(Q)find $(srctree) $(RCS_FIND_IGNORE) \
		-type f -a -name '*.rs' -a ! -name '*generated*' -print \
		| xargs $(RUSTFMT) $(rustfmt_flags)

rustfmtcheck: rustfmt_flags = --check
rustfmtcheck: rustfmt

# Misc
# ---------------------------------------------------------------------------

PHONY += misc-check
misc-check:
	$(Q)$(srctree)/scripts/misc-check

all: misc-check

PHONY += scripts_gdb
scripts_gdb: prepare0
	$(Q)$(MAKE) $(build)=scripts/gdb
	$(Q)ln -fsn $(abspath $(srctree)/scripts/gdb/vmlinux-gdb.py)

ifdef CONFIG_GDB_SCRIPTS
all: scripts_gdb
endif

else # KBUILD_EXTMOD

filechk_kernel.release = echo $(KERNELRELEASE)

###
# External module support.
# When building external modules the kernel used as basis is considered
# read-only, and no consistency checks are made and the make
# system is not used on the basis kernel. If updates are required
# in the basis kernel ordinary make commands (without M=...) must be used.

# We are always building only modules.
KBUILD_BUILTIN :=
KBUILD_MODULES := y

build-dir := .

clean-dirs := .
clean: private rm-files := Module.symvers modules.nsdeps compile_commands.json

PHONY += prepare
# now expand this into a simple variable to reduce the cost of shell evaluations
prepare: CC_VERSION_TEXT := $(CC_VERSION_TEXT)
prepare:
	@if [ "$(CC_VERSION_TEXT)" != "$(CONFIG_CC_VERSION_TEXT)" ]; then \
		echo >&2 "warning: the compiler differs from the one used to build the kernel"; \
		echo >&2 "  The kernel was built by: $(CONFIG_CC_VERSION_TEXT)"; \
		echo >&2 "  You are using:           $(CC_VERSION_TEXT)"; \
	fi

PHONY += help
help:
	@echo  '  Building external modules.'
	@echo  '  Syntax: make -C path/to/kernel/src M=$$PWD target'
	@echo  ''
	@echo  '  modules         - default target, build the module(s)'
	@echo  '  modules_install - install the module'
	@echo  '  clean           - remove generated files in module directory only'
	@echo  '  rust-analyzer	  - generate rust-project.json rust-analyzer support file'
	@echo  ''

ifndef CONFIG_MODULES
modules modules_install: __external_modules_error
__external_modules_error:
	@echo >&2 '***'
	@echo >&2 '*** The present kernel disabled CONFIG_MODULES.'
	@echo >&2 '*** You cannot build or install external modules.'
	@echo >&2 '***'
	@false
endif

endif # KBUILD_EXTMOD

# ---------------------------------------------------------------------------
# Modules

PHONY += modules modules_install modules_sign modules_prepare

modules_install:
	$(Q)$(MAKE) -f $(srctree)/scripts/Makefile.modinst \
	sign-only=$(if $(filter modules_install,$(MAKECMDGOALS)),,y)

ifeq ($(CONFIG_MODULE_SIG),y)
# modules_sign is a subset of modules_install.
# 'make modules_install modules_sign' is equivalent to 'make modules_install'.
modules_sign: modules_install
	@:
else
modules_sign:
	@echo >&2 '***'
	@echo >&2 '*** CONFIG_MODULE_SIG is disabled. You cannot sign modules.'
	@echo >&2 '***'
	@false
endif

ifdef CONFIG_MODULES

modules.order: $(build-dir)
	@:

# KBUILD_MODPOST_NOFINAL can be set to skip the final link of modules.
# This is solely useful to speed up test compiles.
modules: modpost
ifneq ($(KBUILD_MODPOST_NOFINAL),1)
	$(Q)$(MAKE) -f $(srctree)/scripts/Makefile.modfinal
endif

PHONY += modules_check
modules_check: modules.order
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/modules-check.sh $<

else # CONFIG_MODULES

modules:
	@:

KBUILD_MODULES :=

endif # CONFIG_MODULES

PHONY += modpost
modpost: $(if $(single-build),, $(if $(KBUILD_BUILTIN), vmlinux.o)) \
	 $(if $(KBUILD_MODULES), modules_check)
	$(Q)$(MAKE) -f $(srctree)/scripts/Makefile.modpost

# Single targets
# ---------------------------------------------------------------------------
# To build individual files in subdirectories, you can do like this:
#
#   make foo/bar/baz.s
#
# The supported suffixes for single-target are listed in 'single-targets'
#
# To build only under specific subdirectories, you can do like this:
#
#   make foo/bar/baz/

ifdef single-build

# .ko is special because modpost is needed
single-ko := $(sort $(filter %.ko, $(MAKECMDGOALS)))
single-no-ko := $(filter-out $(single-ko), $(MAKECMDGOALS)) \
		$(foreach x, o mod, $(patsubst %.ko, %.$x, $(single-ko)))

$(single-ko): single_modules
	@:
$(single-no-ko): $(build-dir)
	@:

# Remove modules.order when done because it is not the real one.
PHONY += single_modules
single_modules: $(single-no-ko) modules_prepare
	$(Q){ $(foreach m, $(single-ko), echo $(m:%.ko=%.o);) } > modules.order
	$(Q)$(MAKE) -f $(srctree)/scripts/Makefile.modpost
ifneq ($(KBUILD_MODPOST_NOFINAL),1)
	$(Q)$(MAKE) -f $(srctree)/scripts/Makefile.modfinal
endif
	$(Q)rm -f modules.order

single-goals := $(addprefix $(build-dir)/, $(single-no-ko))

KBUILD_MODULES := y

endif

prepare: outputmakefile

# Preset locale variables to speed up the build process. Limit locale
# tweaks to this spot to avoid wrong language settings when running
# make menuconfig etc.
# Error messages still appears in the original language
PHONY += $(build-dir)
$(build-dir): prepare
	$(Q)$(MAKE) $(build)=$@ need-builtin=1 need-modorder=1 $(single-goals)

clean-dirs := $(addprefix _clean_, $(clean-dirs))
PHONY += $(clean-dirs) clean
$(clean-dirs):
	$(Q)$(MAKE) $(clean)=$(patsubst _clean_%,%,$@)

clean: $(clean-dirs)
	$(call cmd,rmfiles)
	@find . $(RCS_FIND_IGNORE) \
		\( -name '*.[aios]' -o -name '*.rsi' -o -name '*.ko' -o -name '.*.cmd' \
		-o -name '*.ko.*' \
		-o -name '*.dtb' -o -name '*.dtbo' \
		-o -name '*.dtb.S' -o -name '*.dtbo.S' \
		-o -name '*.dt.yaml' -o -name 'dtbs-list' \
		-o -name '*.dwo' -o -name '*.lst' \
		-o -name '*.su' -o -name '*.mod' \
		-o -name '.*.d' -o -name '.*.tmp' -o -name '*.mod.c' \
		-o -name '*.lex.c' -o -name '*.tab.[ch]' \
		-o -name '*.asn1.[ch]' \
		-o -name '*.symtypes' -o -name 'modules.order' \
		-o -name '*.c.[012]*.*' \
		-o -name '*.ll' \
		-o -name '*.gcno' \
		\) -type f -print \
		-o -name '.tmp_*' -print \
		| xargs rm -rf

# Generate tags for editors
# ---------------------------------------------------------------------------
quiet_cmd_tags = GEN     $@
      cmd_tags = $(BASH) $(srctree)/scripts/tags.sh $@

tags TAGS cscope gtags: FORCE
	$(call cmd,tags)

# Generate rust-project.json (a file that describes the structure of non-Cargo
# Rust projects) for rust-analyzer (an implementation of the Language Server
# Protocol).
PHONY += rust-analyzer
rust-analyzer:
	+$(Q)$(CONFIG_SHELL) $(srctree)/scripts/rust_is_available.sh
ifdef KBUILD_EXTMOD
# FIXME: external modules must not descend into a sub-directory of the kernel
	$(Q)$(MAKE) $(build)=$(objtree)/rust src=$(srctree)/rust $@
else
	$(Q)$(MAKE) $(build)=rust $@
endif

# Script to generate missing namespace dependencies
# ---------------------------------------------------------------------------

PHONY += nsdeps
nsdeps: export KBUILD_NSDEPS=1
nsdeps: modules
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/nsdeps

# Clang Tooling
# ---------------------------------------------------------------------------

quiet_cmd_gen_compile_commands = GEN     $@
      cmd_gen_compile_commands = $(PYTHON3) $< -a $(AR) -o $@ $(filter-out $<, $(real-prereqs))

compile_commands.json: $(srctree)/scripts/clang-tools/gen_compile_commands.py \
	$(if $(KBUILD_EXTMOD),, vmlinux.a $(KBUILD_VMLINUX_LIBS)) \
	$(if $(CONFIG_MODULES), modules.order) FORCE
	$(call if_changed,gen_compile_commands)

targets += compile_commands.json

PHONY += clang-tidy clang-analyzer

ifdef CONFIG_CC_IS_CLANG
quiet_cmd_clang_tools = CHECK   $<
      cmd_clang_tools = $(PYTHON3) $(srctree)/scripts/clang-tools/run-clang-tools.py $@ $<

clang-tidy clang-analyzer: compile_commands.json
	$(call cmd,clang_tools)
else
clang-tidy clang-analyzer:
	@echo "$@ requires CC=clang" >&2
	@false
endif

# Scripts to check various things for consistency
# ---------------------------------------------------------------------------

PHONY += includecheck versioncheck coccicheck

includecheck:
	find $(srctree)/* $(RCS_FIND_IGNORE) \
		-name '*.[hcS]' -type f -print | sort \
		| xargs $(PERL) -w $(srctree)/scripts/checkincludes.pl

versioncheck:
	find $(srctree)/* $(RCS_FIND_IGNORE) \
		-name '*.[hcS]' -type f -print | sort \
		| xargs $(PERL) -w $(srctree)/scripts/checkversion.pl

coccicheck:
	$(Q)$(BASH) $(srctree)/scripts/$@

PHONY += checkstack kernelrelease kernelversion image_name

# UML needs a little special treatment here.  It wants to use the host
# toolchain, so needs $(SUBARCH) passed to checkstack.pl.  Everyone
# else wants $(ARCH), including people doing cross-builds, which means
# that $(SUBARCH) doesn't work here.
ifeq ($(ARCH), um)
CHECKSTACK_ARCH := $(SUBARCH)
else
CHECKSTACK_ARCH := $(ARCH)
endif
MINSTACKSIZE	?= 100
checkstack:
	$(OBJDUMP) -d vmlinux $$(find . -name '*.ko') | \
	$(PERL) $(srctree)/scripts/checkstack.pl $(CHECKSTACK_ARCH) $(MINSTACKSIZE)

kernelrelease:
	@$(filechk_kernel.release)

kernelversion:
	@echo $(KERNELVERSION)

image_name:
	@echo $(KBUILD_IMAGE)

PHONY += run-command
run-command:
	$(Q)$(KBUILD_RUN_COMMAND)

quiet_cmd_rmfiles = $(if $(wildcard $(rm-files)),CLEAN   $(wildcard $(rm-files)))
      cmd_rmfiles = rm -rf $(rm-files)

# read saved command lines for existing targets
existing-targets := $(wildcard $(sort $(targets)))

-include $(foreach f,$(existing-targets),$(dir $(f)).$(notdir $(f)).cmd)

endif # config-build
endif # mixed-build
endif # need-sub-make

PHONY += FORCE
FORCE:

# Declare the contents of the PHONY variable as phony.  We keep that
# information in a variable so we can use it in if_changed and friends.
.PHONY: $(PHONY)
