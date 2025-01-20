# Used when you need to 'escape' a comma.
comma = ,
empty :=
space := $(empty) $(empty)

# We cannot include /usr/share/dpkg/pkg-info.mk because the variables defined
# here depend on the $(DEBIAN) directory, which can vary between kernels.
# Instead, this file will define the same variables but using the $(DEBIAN)
# variable to use the correct files.

# The source package name will be the first token from $(DEBIAN)/changelog
DEB_SOURCE := $(shell dpkg-parsechangelog -l$(DEBIAN)/changelog -S source)

# Get the series
DEB_DISTRIBUTION := $(shell dpkg-parsechangelog -l$(DEBIAN)/changelog -S distribution | sed -e 's/-\(security\|updates\|proposed\)$$//')

# Get some version info
DEB_VERSION := $(shell dpkg-parsechangelog -l$(DEBIAN)/changelog -S version)
DEB_REVISION ?= $(lastword $(subst -,$(space),$(DEB_VERSION)))
DEB_VERSION_UPSTREAM := $(patsubst %-$(DEB_REVISION),%,$(DEB_VERSION))

DEB_VERSION_PREV ?= $(shell dpkg-parsechangelog -l$(DEBIAN)/changelog -o1 -c1 -S version)
DEB_REVISION_PREV := $(lastword 0.0 $(subst -,$(space),$(DEB_VERSION_PREV)))

# Get upstream version info
upstream_version := $(shell sed -n 's/^VERSION = \(.*\)$$/\1/p' Makefile)
upstream_patchlevel := $(shell sed -n 's/^PATCHLEVEL = \(.*\)$$/\1/p' Makefile)
upstream_tag := "v$(upstream_version).$(upstream_patchlevel)"

# Get the kernels own extra version to be added to the release signature.
raw_kernelversion=$(shell make kernelversion)

packages_enabled := $(shell dh_listpackages 2>/dev/null)
define if_package
$(if $(filter $(1),$(packages_enabled)),$(2))
endef

stamp = [ -d $(dir $@) ] || mkdir $(dir $@); touch $@

#
# do_full_build -- are we doing a full buildd style build, i.e., are we
#                  building in a PPA
#
ifeq ($(wildcard /CurrentlyBuilding),)
	do_full_build ?= false
else
	do_full_build ?= true
endif

#
# The debug packages are ginormous, so you probably want to skip
# building them (as a developer).
#
do_dbgsym_package = true
ifeq ($(do_full_build),false)
	do_dbgsym_package = false
endif
ifeq ($(filter $(DEB_BUILD_OPTIONS),noautodbgsym),noautodbgsym)
	# Disable debug package builds if we're building in a PPA that has the
	# 'Build debug symbols' option disabled
	do_dbgsym_package = false
endif

abinum		:= $(firstword $(subst .,$(space),$(DEB_REVISION)))
prev_abinum	:= $(firstword $(subst .,$(space),$(prev_revision)))
abi_release	:= $(DEB_VERSION_UPSTREAM)-$(abinum)

uploadnum	:= $(patsubst $(abinum).%,%,$(DEB_REVISION))
ifneq ($(do_full_build),false)
  uploadnum	:= $(uploadnum)-Ubuntu
endif

DEB_HOST_MULTIARCH = $(shell dpkg-architecture -qDEB_HOST_MULTIARCH)
DEB_HOST_GNU_TYPE  = $(shell dpkg-architecture -qDEB_HOST_GNU_TYPE)
DEB_BUILD_GNU_TYPE = $(shell dpkg-architecture -qDEB_BUILD_GNU_TYPE)
DEB_HOST_ARCH = $(shell dpkg-architecture -qDEB_HOST_ARCH)
DEB_BUILD_ARCH = $(shell dpkg-architecture -qDEB_BUILD_ARCH)

#
# Detect invocations of the form 'fakeroot debian/rules binary arch=armhf'
# within an x86'en schroot. This only gets you part of the way since the
# packaging phase fails, but you can at least compile the kernel quickly.
#
arch := $(DEB_HOST_ARCH)
CROSS_COMPILE ?= $(DEB_HOST_GNU_TYPE)-

#
# Set consistent toolchain
# If a given kernel wants to change this, they can do so via their own
# $(DEBIAN)/rules.d/hooks.mk and $(DEBIAN)/rules.d/$(arch).mk files
#
export gcc?=gcc-14
export rustc?=rustc
export rustfmt?=rustfmt
export bindgen?=bindgen
GCC_BUILD_DEPENDS=\ $(gcc), $(gcc)-aarch64-linux-gnu [arm64] <cross>, $(gcc)-arm-linux-gnueabihf [armhf] <cross>, $(gcc)-powerpc64le-linux-gnu [ppc64el] <cross>, $(gcc)-riscv64-linux-gnu [riscv64] <cross>, $(gcc)-s390x-linux-gnu [s390x] <cross>, $(gcc)-x86-64-linux-gnu [amd64] <cross>,

abidir		:= $(CURDIR)/$(DEBIAN)/__abi.current/$(arch)
commonconfdir	:= $(CURDIR)/$(DEBIAN)/config
archconfdir	:= $(CURDIR)/$(DEBIAN)/config/$(arch)
sharedconfdir	:= $(CURDIR)/debian.master/config
builddir	:= $(CURDIR)/debian/build
stampdir	:= $(CURDIR)/debian/stamps

#
# The binary package name always starts with linux-image-$KVER-$ABI.$UPLOAD_NUM. There
# are places that you'll find linux-image hard coded, but I guess thats OK since the
# assumption that the binary package always starts with linux-image will never change.
#
bin_pkg_name_signed=linux-image-$(abi_release)
bin_pkg_name_unsigned=linux-image-unsigned-$(abi_release)
mods_pkg_name=linux-modules-$(abi_release)
mods_extra_pkg_name=linux-modules-extra-$(abi_release)
bldinfo_pkg_name=linux-buildinfo-$(abi_release)
hdrs_pkg_name=linux-headers-$(abi_release)
rust_pkg_name=$(DEB_SOURCE)-lib-rust-$(abi_release)
indep_hdrs_pkg_name=$(DEB_SOURCE)-headers-$(abi_release)
indep_lib_rust_pkg_name=$(DEB_SOURCE)-lib-rust-$(abi_release)

#
# Similarly with the linux-source package, you need not build it as a developer. Its
# somewhat I/O intensive and utterly useless.
#
do_source_package=true
do_source_package_content=true
ifeq ($(do_full_build),false)
do_source_package_content=false
endif

# common headers normally is built as an indep package, but may be arch
do_common_headers_indep=true

# build tools
ifneq ($(wildcard $(CURDIR)/tools),)
	ifeq ($(do_tools),)
		ifneq ($(DEB_BUILD_GNU_TYPE),$(DEB_HOST_GNU_TYPE))
			do_tools=false
		endif
	endif
	do_tools?=true
else
	do_tools?=false
endif
tools_pkg_name=$(DEB_SOURCE)-tools-$(abi_release)
tools_common_pkg_name=linux-tools-common
tools_flavour_pkg_name=linux-tools-$(abi_release)
cloud_pkg_name=$(DEB_SOURCE)-cloud-tools-$(abi_release)
cloud_common_pkg_name=linux-cloud-tools-common
cloud_flavour_pkg_name=linux-cloud-tools-$(abi_release)
hosttools_pkg_name=linux-tools-host

# The general flavour specific image package.
do_flavour_image_package=true

# The general flavour specific header package.
do_flavour_header_package=true

# DTBs
do_dtbs=false

# ZSTD compressed kernel modules
do_zstd_ko=true
ifeq ($(DEB_DISTRIBUTION),jammy)
do_zstd_ko=
endif

# Support parallel=<n> in DEB_BUILD_OPTIONS (see #209008)
#
# These 2 environment variables set the -j value of the kernel build. For example,
# CONCURRENCY_LEVEL=16 fakeroot $(DEBIAN)/rules binary-debs
# or
# DEB_BUILD_OPTIONS=parallel=16 fakeroot $(DEBIAN)/rules binary-debs
#
# The default is to use the number of CPUs.
#
COMMA=,
DEB_BUILD_OPTIONS_PARA = $(subst parallel=,,$(filter parallel=%,$(subst $(COMMA), ,$(DEB_BUILD_OPTIONS))))
ifneq (,$(DEB_BUILD_OPTIONS_PARA))
  CONCURRENCY_LEVEL := $(DEB_BUILD_OPTIONS_PARA)
endif

ifeq ($(CONCURRENCY_LEVEL),)
  # Check the environment
  CONCURRENCY_LEVEL := $(shell echo $$CONCURRENCY_LEVEL)
  # No? Then build with the number of CPUs on the host.
  ifeq ($(CONCURRENCY_LEVEL),)
      CONCURRENCY_LEVEL := $(shell expr `getconf _NPROCESSORS_ONLN` \* 1)
  endif
  # Oh hell, give 'em one
  ifeq ($(CONCURRENCY_LEVEL),)
    CONCURRENCY_LEVEL := 1
  endif
endif

conc_level		= -j$(CONCURRENCY_LEVEL)

PYTHON ?= $(firstword $(wildcard /usr/bin/python3) $(wildcard /usr/bin/python2) $(wildcard /usr/bin/python))

HOSTCC ?= $(DEB_BUILD_GNU_TYPE)-$(gcc)

# target_flavour is filled in for each step
kmake = make ARCH=$(build_arch) \
	CROSS_COMPILE=$(CROSS_COMPILE) \
	HOSTCC=$(HOSTCC) \
	CC=$(CROSS_COMPILE)$(gcc) \
	RUSTC=$(rustc) \
	HOSTRUSTC=$(rustc) \
	RUSTFMT=$(rustfmt) \
	BINDGEN=$(bindgen) \
	KERNELRELEASE=$(abi_release)-$(target_flavour) \
	CONFIG_DEBUG_SECTION_MISMATCH=y \
	KBUILD_BUILD_VERSION="$(uploadnum)" \
	CFLAGS_MODULE="-DPKG_ABI=$(abinum)" \
	PYTHON=$(PYTHON)
ifneq ($(LOCAL_ENV_CC),)
kmake += CC="$(LOCAL_ENV_CC)" DISTCC_HOSTS="$(LOCAL_ENV_DISTCC_HOSTS)"
endif

# Locking is required in parallel builds to prevent loss of contents
# of the debian/files.
lockme = flock -w 60 $(CURDIR)/debian/.LOCK

# Don't fail if a link already exists.
LN = ln -sf

# Checks if a var is overriden by the custom rules. Called with var and
# flavour as arguments.
custom_override = $(or $($(1)_$(2)),$($(1)))

# selftests that Ubuntu cares about
ubuntu_selftests = breakpoints cpu-hotplug efivarfs memfd memory-hotplug mount net ptrace seccomp timers powerpc user ftrace

# DKMS
all_dkms_modules =

subst_paired = $(subst $(firstword $(subst =, ,$(1))),$(lastword $(subst =, ,$(1))),$(2))
recursive_call = $(if $(2),$(call recursive_call,$(1),$(wordlist 2,$(words $(2)),$(2)),$(call $(1),$(firstword $(2)),$(3))),$(3))

$(foreach _line,$(shell gawk '{ OFS = "!"; $$1 = $$1; print }' $(DEBIAN)/dkms-versions), \
  $(eval _params = $(subst !, ,$(_line))) \
  $(eval _deb_pkgname = $(firstword $(_params))) \
  $(eval _deb_version = $(word 2,$(_params))) \
  $(if $(filter modulename=%,$(_params)), \
    $(eval _m = $(word 2,$(subst =, ,$(filter modulename=%,$(_params))))) \
    , \
    $(info modulename for $(_deb_pkgname) not specified in dkms-versions. Assume $(_deb_pkgname).) \
    $(eval _m = $(_deb_pkgname)) \
  ) \
  $(eval all_dkms_modules += $(_m)) \
  $(eval dkms_$(_m)_version = $(_deb_version)) \
  $(foreach _p,$(patsubst debpath=%,%,$(filter debpath=%,$(_params))), \
    $(eval dkms_$(_m)_debpath += $(strip \
      $(call recursive_call,subst_paired, \
        %module%=$(_m) \
        %package%=$(_deb_pkgname) \
        %version%=$(lastword $(subst :, ,$(_deb_version))) \
        , \
        $(_p) \
      ) \
    )) \
  ) \
  $(if $(dkms_$(_m)_debpath),,$(error debpath for $(_deb_pkgname) not specified.)) \
  $(if $(filter arch=%,$(_params)), \
    $(eval dkms_$(_m)_archs = $(patsubst arch=%,%,$(filter arch=%,$(_params)))) \
    , \
    $(eval dkms_$(_m)_archs = any) \
  ) \
  $(eval dkms_$(_m)_rprovides = $(patsubst rprovides=%,%,$(filter rprovides=%,$(_params)))) \
  $(eval dkms_$(_m)_type = $(word 1,$(patsubst type=%,%,$(filter type=%,$(_params))) built-in)) \
  $(eval all_$(dkms_$(_m)_type)_dkms_modules += $(_m)) \
  $(if $(filter standalone,$(dkms_$(_m)_type)), \
    $(eval dkms_$(_m)_pkg_name = linux-modules-$(_m)-$(abi_release)) \
    $(eval dkms_$(_m)_subdir = ubuntu) \
    , \
    $(eval dkms_$(_m)_pkg_name = $(mods_pkg_name)) \
    $(eval dkms_$(_m)_subdir = kernel) \
  ) \
)
