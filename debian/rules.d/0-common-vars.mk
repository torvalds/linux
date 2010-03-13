# Used when you need to 'escape' a comma.
comma = ,

#
# The source package name will be the first token from $(DEBIAN)/changelog
#
src_pkg_name=$(shell sed -n '1s/^\(.*\) (.*).*$$/\1/p' $(DEBIAN)/changelog)

# Get some version info
release := $(shell sed -n '1s/^$(src_pkg_name).*(\(.*\)-.*).*$$/\1/p' $(DEBIAN)/changelog)
revisions := $(shell sed -n 's/^$(src_pkg_name)\ .*($(release)-\(.*\)).*$$/\1/p' $(DEBIAN)/changelog | tac)
revision ?= $(word $(words $(revisions)),$(revisions))
prev_revisions := $(filter-out $(revision),0.0 $(revisions))
prev_revision := $(word $(words $(prev_revisions)),$(prev_revisions))

prev_fullver ?= $(shell dpkg-parsechangelog -l$(DEBIAN)/changelog -o1 -c1 | sed -ne 's/^Version: *//p')

family=ubuntu

# This is an internally used mechanism for the daily kernel builds. It
# creates packages whose ABI is suffixed with a minimal representation of
# the current git HEAD sha. If .git/HEAD is not present, then it uses the
# uuidgen program,
#
# AUTOBUILD can also be used by anyone wanting to build a custom kernel
# image, or rebuild the entire set of Ubuntu packages using custom patches
# or configs.
AUTOBUILD=

ifneq ($(AUTOBUILD),)
skipabi		= true
skipmodule	= true
skipdbg		= true
gitver=$(shell if test -f .git/HEAD; then cat .git/HEAD; else uuidgen; fi)
gitverpre=$(shell echo $(gitver) | cut -b -3)
gitverpost=$(shell echo $(gitver) | cut -b 38-40)
abi_suffix = -$(gitverpre)$(gitverpost)
endif

ifneq ($(NOKERNLOG),)
ubuntu_log_opts += --no-kern-log
endif
ifneq ($(PRINTSHAS),)
ubuntu_log_opts += --print-shas
endif

# Get the kernels own extra version to be added to the release signature.
raw_kernelversion=$(shell make kernelversion)

#
# full_build -- are we doing a full buildd style build
#
ifeq ($(wildcard /CurrentlyBuilding),)
full_build?=false
else
full_build?=true
endif

#
# The debug packages are ginormous, so you probably want to skip
# building them (as a developer).
#
ifeq ($(full_build),false)
skipdbg=true
endif

abinum		:= $(shell echo $(revision) | sed -r -e 's/([^\+~]*)\.[^\.]+(~.*)?(\+.*)?$$/\1/')$(abi_suffix)
prev_abinum	:= $(shell echo $(prev_revision) | sed -r -e 's/([^\+~]*)\.[^\.]+(~.*)?(\+.*)?$$/\1/')$(abi_suffix)
abi_release	:= $(release)-$(abinum)

uploadnum	:= $(shell echo $(revision) | sed -r -e 's/[^\+~]*\.([^\.~]+(~.*)?(\+.*)?$$)/\1/')
ifneq ($(full_build),false)
  uploadnum	:= $(uploadnum)-Ubuntu
endif

# XXX: linux-libc-dev got bumped to -803.N inadvertantly by a ti-omap4 upload
#      shift our version higher for this package only.  Ensure this only
#      occurs for the v2.6.35 kernel so that we do not propogate this into
#      any other series.
raw_uploadnum	:= $(shell echo $(revision) | sed -e 's/.*\.//')
libc_dev_version :=
ifeq ($(DEBIAN),debian.master)
ifeq ($(release),2.6.35)
libc_dev_version := -v$(release)-$(shell expr "$(abinum)" + 1000).$(raw_uploadnum)
endif
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
ifneq ($(arch),$(DEB_HOST_ARCH))
	CROSS_COMPILE ?= $(shell dpkg-architecture -a$(arch) -qDEB_HOST_GNU_TYPE -f 2>/dev/null)-
endif

#
# Detect invocations of the form 'dpkg-buildpackage -B -aarmhf' within
# an x86'en schroot. This is the only way to build all of the packages
# (except for tools).
#
ifneq ($(DEB_BUILD_GNU_TYPE),$(DEB_HOST_GNU_TYPE))
	CROSS_COMPILE ?= $(DEB_HOST_GNU_TYPE)-
endif

abidir		:= $(CURDIR)/$(DEBIAN)/abi/$(release)-$(revision)/$(arch)
prev_abidir	:= $(CURDIR)/$(DEBIAN)/abi/$(release)-$(prev_revision)/$(arch)
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
bin_pkg_name=linux-image-$(abi_release)
extra_pkg_name=linux-image-extra-$(abi_release)
hdrs_pkg_name=linux-headers-$(abi_release)
indep_hdrs_pkg_name=$(src_pkg_name)-headers-$(abi_release)

#
# The generation of content in the doc package depends on both 'AUTOBUILD=' and
# 'do_doc_package_content=true'. There are usually build errors during the development
# cycle, so its OK to leave 'do_doc_package_content=false' until those build
# failures get sorted out. Finally, the doc package doesn't really need to be built
# for developer testing (its kind of slow), so only do it if on a buildd.
do_doc_package=true
do_doc_package_content=true
ifeq ($(full_build),false)
do_doc_package_content=false
endif
doc_pkg_name=$(src_pkg_name)-doc

#
# Similarly with the linux-source package, you need not build it as a developer. Its
# somewhat I/O intensive and utterly useless.
#
do_source_package=true
do_source_package_content=true
ifeq ($(full_build),false)
do_source_package_content=false
endif

# linux-libc-dev may not be needed, default to building it.
do_libc_dev_package=true

# common headers normally is built as an indep package, but may be arch
do_common_headers_indep=true

# add a 'full source' mode
do_full_source=false

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
tools_pkg_name=$(src_pkg_name)-tools-$(abi_release)
tools_common_pkg_name=$(src_pkg_name)-tools-common
tools_flavour_pkg_name=linux-tools-$(abi_release)
cloud_pkg_name=$(src_pkg_name)-cloud-tools-$(abi_release)
cloud_common_pkg_name=$(src_pkg_name)-cloud-tools-common
cloud_flavour_pkg_name=linux-cloud-tools-$(abi_release)

# The general flavour specific image package.
do_flavour_image_package=true

# The general flavour specific header package.
do_flavour_header_package=true

# DTBs
do_dtbs=false

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

# target_flavour is filled in for each step
kmake = make ARCH=$(build_arch) \
	CROSS_COMPILE=$(CROSS_COMPILE) \
	KERNELVERSION=$(abi_release)-$(target_flavour) \
	CONFIG_DEBUG_SECTION_MISMATCH=y \
	KBUILD_BUILD_VERSION="$(uploadnum)" \
	LOCALVERSION= localver-extra= \
	CFLAGS_MODULE="-DPKG_ABI=$(abinum)"
ifneq ($(LOCAL_ENV_CC),)
kmake += CC=$(LOCAL_ENV_CC) DISTCC_HOSTS=$(LOCAL_ENV_DISTCC_HOSTS)
endif

# Locking is required in parallel builds to prevent loss of contents
# of the debian/files.
lockme_file = $(CURDIR)/debian/.LOCK
lockme_cmd = flock -w 60
lockme = $(lockme_cmd) $(lockme_file)

# Don't fail if a link already exists.
LN = ln -sf

# Checks if a var is overriden by the custom rules. Called with var and
# flavour as arguments.
custom_override = \
 $(shell if [ -n "$($(1)_$(2))" ]; then echo "$($(1)_$(2))"; else echo "$($(1))"; fi)
