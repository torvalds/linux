#
# From Makefile.inc
#

#
# Common definitions go here.
#

#
# TRUE_VERSION is the version string used in the driver build,
# it is intended to be in the form:
#
#   2.0-0
#
# A string noting a particular special modification could be
# used as well.  This string will be reported when the driver
# is loaded, and will be exposed by its /proc/dgnc/info
# interface.
#
TRUE_VERSION="1.3-16"

#
# DGNC_PART_NUM is the part number string for the driver package.
# It should be in the form:
#
#   40002369_A
#
DGNC_PART_NUM=40002369_F

#
# DGNC_REL_NOTE is the part number string for the driver release
# notes.  It should be in the form:
#
#   93000517_A
#
DGNC_REL_NOTE=93000517_F

#
# DGNC_PKG_VER is the "version" number string included in the
# various documentation and packaging files.  It should be
# in the form:
#
#   1.0
#
DGNC_PKG_VER=1.3

#
# DGNC_PKG_REV is the "revision" of this version.  Together,
# a linux module revision is built with:
#
#   ${DGNC_PKG_VER}-${DGNC_PKG_REV}
#
DGNC_PKG_REV=16

#
# DRP_PKG_DATE is the "date" string included in (for now) the
# release notes.  It should be in the form:
#
#   11/04/2003
#
DGNC_PKG_DATE=10/17/2008

INIT_DIR= $(shell \
	if [ -d /etc/rc.d/init.d ]; \
	then echo "$(RPM_BUILD_ROOT)/etc/rc.d/init.d"; \
	else echo "$(RPM_BUILD_ROOT)/etc/init.d"; fi)

#
#	Miscelaneous path macro's
#

PACKAGE=	dgnc
DEVDIR=		/dev/dg/$(PACKAGE)
SRCDIR=		/usr/src/dg/$(PACKAGE)
BINDIR=		/usr/bin
DRVLIBDIR=	/etc/$(PACKAGE)
MANDIR=		/usr/man
USRLIBDIR=	/usr/lib
DGNCDIR=	/etc/dgnc


INIT_DIR= $(shell \
	if [ -d /etc/rc.d/init.d ]; \
	then echo "/etc/rc.d/init.d"; \
	else echo "/etc/init.d"; fi)


#
# From Makefile
#
ifndef MYPWD
MYPWD = $(shell pwd)
endif

ifeq ($(KERNDIR),)
  KERNVERS       := $(shell uname -r)
  KERNDIR         :=/lib/modules/${KERNVERS}/
endif

# Grab version and other important stuff

RPMNAME := $(PACKAGE)-$(TRUE_VERSION)

PARTNUM := $(DGNC_PART_NUM)

RELNOTES := $(DGNC_REL_NOTE)

MODDIR = $(shell echo $(BUILDROOT)/lib/modules/3.4.36-lcrs/misc)
LSMOD = /sbin/lsmod
RMMOD = /sbin/rmmod
INSMOD = /sbin/insmod
NEW_TTY_LOCKING = No
NEW_TTY_BUFFERING = No
REGISTER_TTYS_WITH_SYSFS = No

# Send in some extra things...
EXTRA_CFLAGS += -I${MYPWD} -I${MYPWD}/include -I${MYPWD}/../../commoninc\
	-I${MYPWD}/../../dpa -DLINUX -DDG_NAME=\"$(RPMNAME)\"\
	-DDG_PART=\"$(PARTNUM)\" -DDGNC_TRACER

ifeq ($(NEW_TTY_LOCKING),Yes)
	EXTRA_CFLAGS += -DNEW_TTY_LOCKING
endif

ifeq ($(NEW_TTY_BUFFERING),Yes)
	EXTRA_CFLAGS += -DNEW_TTY_BUFFERING
endif

ifeq ($(REGISTER_TTYS_WITH_SYSFS),Yes)
	EXTRA_CFLAGS += -DREGISTER_TTYS_WITH_SYSFS
endif

# Conform to correct kbuild conventions...
