[+ AutoGen5 template -*- Mode: Makefile -*-
in
+]

# Makefile.in is generated from Makefile.tpl by 'autogen Makefile.def'.
#
# Makefile for directory with subdirs to build.
#   Copyright (C) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
#   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation
#
# This file is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#

# -------------------------------
# Standard Autoconf-set variables
# -------------------------------
VPATH=@srcdir@

build_alias=@build_noncanonical@
build_vendor=@build_vendor@
build_os=@build_os@
build=@build@
host_alias=@host_noncanonical@
host_vendor=@host_vendor@
host_os=@host_os@
host=@host@
target_alias=@target_noncanonical@
target_vendor=@target_vendor@
target_os=@target_os@
target=@target@

program_transform_name = @program_transform_name@

prefix = @prefix@
exec_prefix = @exec_prefix@

srcdir = @srcdir@

bindir = @bindir@
sbindir = @sbindir@
libexecdir = @libexecdir@
datadir = @datadir@
sysconfdir = @sysconfdir@
sharedstatedir = @sharedstatedir@
localstatedir = @localstatedir@
libdir = @libdir@
includedir = @includedir@
oldincludedir = @oldincludedir@
infodir = @infodir@
datarootdir = @datarootdir@
docdir = @docdir@
pdfdir = @pdfdir@
htmldir = @htmldir@
mandir = @mandir@
man1dir = $(mandir)/man1
man2dir = $(mandir)/man2
man3dir = $(mandir)/man3
man4dir = $(mandir)/man4
man5dir = $(mandir)/man5
man6dir = $(mandir)/man6
man7dir = $(mandir)/man7
man8dir = $(mandir)/man8
man9dir = $(mandir)/man9

INSTALL = @INSTALL@
INSTALL_PROGRAM = @INSTALL_PROGRAM@
INSTALL_SCRIPT = @INSTALL_SCRIPT@
INSTALL_DATA = @INSTALL_DATA@
LN = @LN@
LN_S = @LN_S@
MAINT = @MAINT@
MAINTAINER_MODE_FALSE = @MAINTAINER_MODE_FALSE@
MAINTAINER_MODE_TRUE = @MAINTAINER_MODE_TRUE@

# -------------------------------------------------
# Miscellaneous non-standard autoconf-set variables
# -------------------------------------------------

# The gcc driver likes to know the arguments it was configured with.
TOPLEVEL_CONFIGURE_ARGUMENTS=@TOPLEVEL_CONFIGURE_ARGUMENTS@

tooldir = @tooldir@
build_tooldir = @build_tooldir@

GDB_NLM_DEPS = 

# This is the name of the environment variable used for the path to
# the libraries.
RPATH_ENVVAR = @RPATH_ENVVAR@

# Build programs are put under this directory.
BUILD_SUBDIR = @build_subdir@
# This is set by the configure script to the arguments to use when configuring
# directories built for the build system.
BUILD_CONFIGARGS = @build_configargs@ --with-build-subdir="$(BUILD_SUBDIR)"

# This is the list of variables to export in the environment when
# configuring any subdirectory.  It must also be exported whenever
# recursing into a build directory in case that directory's Makefile
# re-runs configure.
BASE_EXPORTS = \
	FLEX="$(FLEX)"; export FLEX; \
	LEX="$(LEX)"; export LEX; \
	BISON="$(BISON)"; export BISON; \
	YACC="$(YACC)"; export YACC; \
	M4="$(M4)"; export M4; \
	MAKEINFO="$(MAKEINFO)"; export MAKEINFO;

# This is the list of variables to export in the environment when
# configuring subdirectories for the build system.
BUILD_EXPORTS = \
	$(BASE_EXPORTS) \
	AR="$(AR_FOR_BUILD)"; export AR; \
	AS="$(AS_FOR_BUILD)"; export AS; \
	CC="$(CC_FOR_BUILD)"; export CC; \
	CFLAGS="$(CFLAGS_FOR_BUILD)"; export CFLAGS; \
	CONFIG_SHELL="$(SHELL)"; export CONFIG_SHELL; \
	CXX="$(CXX_FOR_BUILD)"; export CXX; \
	CXXFLAGS="$(CXXFLAGS_FOR_BUILD)"; export CXXFLAGS; \
	GCJ="$(GCJ_FOR_BUILD)"; export GCJ; \
	GFORTRAN="$(GFORTRAN_FOR_BUILD)"; export GFORTRAN; \
	DLLTOOL="$(DLLTOOL_FOR_BUILD)"; export DLLTOOL; \
	LD="$(LD_FOR_BUILD)"; export LD; \
	LDFLAGS="$(LDFLAGS_FOR_BUILD)"; export LDFLAGS; \
	NM="$(NM_FOR_BUILD)"; export NM; \
	RANLIB="$(RANLIB_FOR_BUILD)"; export RANLIB; \
	WINDRES="$(WINDRES_FOR_BUILD)"; export WINDRES;

# This is the list of directories to built for the host system.
SUBDIRS = @configdirs@
# This is set by the configure script to the arguments to use when configuring
# directories built for the host system.
HOST_CONFIGARGS = @host_configargs@
# Host programs are put under this directory, which is . except if building
# with srcdir=..
HOST_SUBDIR = @host_subdir@
# This is the list of variables to export in the environment when
# configuring subdirectories for the host system.  We need to pass
# some to the GCC configure because of its hybrid host/target nature.
HOST_EXPORTS = \
	$(BASE_EXPORTS) \
	CC="$(CC)"; export CC; \
	CFLAGS="$(CFLAGS)"; export CFLAGS; \
	CONFIG_SHELL="$(SHELL)"; export CONFIG_SHELL; \
	CXX="$(CXX)"; export CXX; \
	CXXFLAGS="$(CXXFLAGS)"; export CXXFLAGS; \
	AR="$(AR)"; export AR; \
	AS="$(AS)"; export AS; \
	CC_FOR_BUILD="$(CC_FOR_BUILD)"; export CC_FOR_BUILD; \
	DLLTOOL="$(DLLTOOL)"; export DLLTOOL; \
	LD="$(LD)"; export LD; \
	LDFLAGS="$(LDFLAGS)"; export LDFLAGS; \
	NM="$(NM)"; export NM; \
	RANLIB="$(RANLIB)"; export RANLIB; \
	WINDRES="$(WINDRES)"; export WINDRES; \
	OBJCOPY="$(OBJCOPY)"; export OBJCOPY; \
	OBJDUMP="$(OBJDUMP)"; export OBJDUMP; \
	AR_FOR_TARGET="$(AR_FOR_TARGET)"; export AR_FOR_TARGET; \
	AS_FOR_TARGET="$(AS_FOR_TARGET)"; export AS_FOR_TARGET; \
	GCC_FOR_TARGET="$(GCC_FOR_TARGET)"; export GCC_FOR_TARGET; \
	LD_FOR_TARGET="$(LD_FOR_TARGET)"; export LD_FOR_TARGET; \
	NM_FOR_TARGET="$(NM_FOR_TARGET)"; export NM_FOR_TARGET; \
	OBJDUMP_FOR_TARGET="$(OBJDUMP_FOR_TARGET)"; export OBJDUMP_FOR_TARGET; \
	RANLIB_FOR_TARGET="$(RANLIB_FOR_TARGET)"; export RANLIB_FOR_TARGET; \
	TOPLEVEL_CONFIGURE_ARGUMENTS="$(TOPLEVEL_CONFIGURE_ARGUMENTS)"; export TOPLEVEL_CONFIGURE_ARGUMENTS; \
	GMPLIBS="$(HOST_GMPLIBS)"; export GMPLIBS; \
	GMPINC="$(HOST_GMPINC)"; export GMPINC; \
@if gcc-bootstrap
	$(RPATH_ENVVAR)=`echo "$(TARGET_LIB_PATH)$$$(RPATH_ENVVAR)" | sed 's,::*,:,g;s,^:*,,;s,:*$$,,'`; export $(RPATH_ENVVAR); \
@endif gcc-bootstrap
	$(RPATH_ENVVAR)=`echo "$(HOST_LIB_PATH)$$$(RPATH_ENVVAR)" | sed 's,::*,:,g;s,^:*,,;s,:*$$,,'`; export $(RPATH_ENVVAR);

# Similar, for later GCC stages.
POSTSTAGE1_HOST_EXPORTS = \
	$(HOST_EXPORTS) \
	CC="$(STAGE_CC_WRAPPER) $$r/$(HOST_SUBDIR)/prev-gcc/xgcc$(exeext) \
	  -B$$r/$(HOST_SUBDIR)/prev-gcc/ -B$(build_tooldir)/bin/"; export CC; \
	CC_FOR_BUILD="$(STAGE_CC_WRAPPER) \
	  $$r/$(HOST_SUBDIR)/prev-gcc/xgcc$(exeext) \
	  -B$$r/$(HOST_SUBDIR)/prev-gcc/ \
	  -B$(build_tooldir)/bin/"; export CC_FOR_BUILD; \
	LDFLAGS="$(BOOT_LDFLAGS)"; export LDFLAGS;

# Target libraries are put under this directory:
TARGET_SUBDIR = @target_subdir@
# This is set by the configure script to the arguments to use when configuring
# directories built for the target.
TARGET_CONFIGARGS = @target_configargs@ --with-target-subdir="$(TARGET_SUBDIR)"
# This is the list of variables to export in the environment when
# configuring subdirectories for the host system.
BASE_TARGET_EXPORTS = \
	$(BASE_EXPORTS) \
	AR="$(AR_FOR_TARGET)"; export AR; \
	AS="$(COMPILER_AS_FOR_TARGET)"; export AS; \
	CC="$(CC_FOR_TARGET)"; export CC; \
	CFLAGS="$(CFLAGS_FOR_TARGET)"; export CFLAGS; \
	CONFIG_SHELL="$(SHELL)"; export CONFIG_SHELL; \
	CPPFLAGS="$(CPPFLAGS_FOR_TARGET)"; export CPPFLAGS; \
	CXXFLAGS="$(CXXFLAGS_FOR_TARGET)"; export CXXFLAGS; \
	GCJ="$(GCJ_FOR_TARGET)"; export GCJ; \
	GFORTRAN="$(GFORTRAN_FOR_TARGET)"; export GFORTRAN; \
	DLLTOOL="$(DLLTOOL_FOR_TARGET)"; export DLLTOOL; \
	LD="$(COMPILER_LD_FOR_TARGET)"; export LD; \
	LDFLAGS="$(LDFLAGS_FOR_TARGET)"; export LDFLAGS; \
	LIPO="$(LIPO_FOR_TARGET)"; export LIPO; \
	NM="$(COMPILER_NM_FOR_TARGET)"; export NM; \
	OBJDUMP="$(OBJDUMP_FOR_TARGET)"; export OBJDUMP; \
	RANLIB="$(RANLIB_FOR_TARGET)"; export RANLIB; \
	STRIP="$(STRIP_FOR_TARGET)"; export STRIP; \
	WINDRES="$(WINDRES_FOR_TARGET)"; export WINDRES; \
	$(RPATH_ENVVAR)=`echo "$(HOST_LIB_PATH)$(TARGET_LIB_PATH)$$$(RPATH_ENVVAR)" | sed 's,::*,:,g;s,^:*,,;s,:*$$,,'`; export $(RPATH_ENVVAR);

RAW_CXX_TARGET_EXPORTS = \
	$(BASE_TARGET_EXPORTS) \
	CXX_FOR_TARGET="$(RAW_CXX_FOR_TARGET)"; export CXX_FOR_TARGET; \
	CXX="$(RAW_CXX_FOR_TARGET)"; export CXX;

NORMAL_TARGET_EXPORTS = \
	$(BASE_TARGET_EXPORTS) \
	CXX="$(CXX_FOR_TARGET)"; export CXX;

# Where to find GMP
HOST_GMPLIBS = @gmplibs@
HOST_GMPINC = @gmpinc@

# ----------------------------------------------
# Programs producing files for the BUILD machine
# ----------------------------------------------

SHELL = @config_shell@

# pwd command to use.  Allow user to override default by setting PWDCMD in
# the environment to account for automounters.  The make variable must not
# be called PWDCMD, otherwise the value set here is passed to make
# subprocesses and overrides the setting from the user's environment.
# Don't use PWD since it is a common shell environment variable and we
# don't want to corrupt it.
PWD_COMMAND = $${PWDCMD-pwd}

# compilers to use to create programs which must be run in the build
# environment.
CC_FOR_BUILD = @CC_FOR_BUILD@
CFLAGS_FOR_BUILD = @CFLAGS_FOR_BUILD@

CXX_FOR_BUILD = $(CXX)

# Special variables passed down in EXTRA_GCC_FLAGS.  They are defined
# here so that they can be overridden by Makefile fragments.
BUILD_PREFIX = @BUILD_PREFIX@
BUILD_PREFIX_1 = @BUILD_PREFIX_1@

# Flags to pass to stage2 and later makes.  They are defined
# here so that they can be overridden by Makefile fragments.
BOOT_CFLAGS= -g -O2
BOOT_LDFLAGS=

BISON = @BISON@
YACC = @YACC@
FLEX = @FLEX@
LEX = @LEX@
M4 = @M4@
MAKEINFO = @MAKEINFO@
EXPECT = @EXPECT@
RUNTEST = @RUNTEST@

# This just becomes part of the MAKEINFO definition passed down to
# sub-makes.  It lets flags be given on the command line while still
# using the makeinfo from the object tree.
# (Default to avoid splitting info files by setting the threshold high.)
MAKEINFOFLAGS = --split-size=5000000

# ---------------------------------------------
# Programs producing files for the HOST machine
# ---------------------------------------------

AS = @AS@
AR = @AR@
ARFLAGS = @ARFLAGS@
CC = @CC@
CXX = @CXX@
DLLTOOL = @DLLTOOL@
LD = @LD@
LIPO = @LIPO@
NM = @NM@
OBJDUMP = @OBJDUMP@
RANLIB = @RANLIB@
STRIP = @STRIP@
WINDRES = @WINDRES@

GNATBIND = @GNATBIND@
GNATMAKE = @GNATMAKE@

CFLAGS = @CFLAGS@
LDFLAGS = @LDFLAGS@
LIBCFLAGS = $(CFLAGS)
CXXFLAGS = @CXXFLAGS@
LIBCXXFLAGS = $(CXXFLAGS) -fno-implicit-templates
PICFLAG = 

# Only build the C compiler for stage1, because that is the only one that
# we can guarantee will build with the native compiler, and also it is the
# only thing useful for building stage2. STAGE1_CFLAGS (via CFLAGS),
# MAKEINFO and MAKEINFOFLAGS are explicitly passed here to make them
# overrideable (for a bootstrap build stage1 also builds gcc.info).

STAGE1_CFLAGS=@stage1_cflags@
STAGE1_CHECKING=@stage1_checking@
STAGE1_LANGUAGES=@stage1_languages@

# -----------------------------------------------
# Programs producing files for the TARGET machine
# -----------------------------------------------

FLAGS_FOR_TARGET = @FLAGS_FOR_TARGET@

AR_FOR_TARGET=@AR_FOR_TARGET@
AS_FOR_TARGET=@AS_FOR_TARGET@
CC_FOR_TARGET=$(STAGE_CC_WRAPPER) @CC_FOR_TARGET@ $(FLAGS_FOR_TARGET)

# If GCC_FOR_TARGET is not overriden on the command line, then this
# variable is passed down to the gcc Makefile, where it is used to
# build libgcc2.a.  We define it here so that it can itself be
# overridden on the command line.
GCC_FOR_TARGET=$(STAGE_CC_WRAPPER) @GCC_FOR_TARGET@ $(FLAGS_FOR_TARGET)
CXX_FOR_TARGET=$(STAGE_CC_WRAPPER) @CXX_FOR_TARGET@ $(FLAGS_FOR_TARGET)
RAW_CXX_FOR_TARGET=$(STAGE_CC_WRAPPER) @RAW_CXX_FOR_TARGET@ $(FLAGS_FOR_TARGET)
GCJ_FOR_TARGET=$(STAGE_CC_WRAPPER) @GCJ_FOR_TARGET@ $(FLAGS_FOR_TARGET)
GFORTRAN_FOR_TARGET=$(STAGE_CC_WRAPPER) @GFORTRAN_FOR_TARGET@ $(FLAGS_FOR_TARGET)
DLLTOOL_FOR_TARGET=@DLLTOOL_FOR_TARGET@
LD_FOR_TARGET=@LD_FOR_TARGET@

LIPO_FOR_TARGET=@LIPO_FOR_TARGET@
NM_FOR_TARGET=@NM_FOR_TARGET@
OBJDUMP_FOR_TARGET=@OBJDUMP_FOR_TARGET@
RANLIB_FOR_TARGET=@RANLIB_FOR_TARGET@
STRIP_FOR_TARGET=@STRIP_FOR_TARGET@
WINDRES_FOR_TARGET=@WINDRES_FOR_TARGET@

COMPILER_AS_FOR_TARGET=@COMPILER_AS_FOR_TARGET@
COMPILER_LD_FOR_TARGET=@COMPILER_LD_FOR_TARGET@
COMPILER_NM_FOR_TARGET=@COMPILER_NM_FOR_TARGET@

# During gcc bootstrap, if we use some random cc for stage1 then
# CFLAGS will be just -g.  We want to ensure that TARGET libraries
# (which we know are built with gcc) are built with optimizations so
# prepend -O2 when setting CFLAGS_FOR_TARGET.
CFLAGS_FOR_TARGET = -O2 $(CFLAGS) $(SYSROOT_CFLAGS_FOR_TARGET)
SYSROOT_CFLAGS_FOR_TARGET = @SYSROOT_CFLAGS_FOR_TARGET@
CXXFLAGS_FOR_TARGET = $(CXXFLAGS) $(SYSROOT_CFLAGS_FOR_TARGET)
LIBCFLAGS_FOR_TARGET = $(CFLAGS_FOR_TARGET)
LIBCXXFLAGS_FOR_TARGET = $(CXXFLAGS_FOR_TARGET) -fno-implicit-templates
LDFLAGS_FOR_TARGET = 
PICFLAG_FOR_TARGET = 

# ------------------------------------
# Miscellaneous targets and flag lists
# ------------------------------------

# The first rule in the file had better be this one.  Don't put any above it.
# This lives here to allow makefile fragments to contain dependencies.
all:

#### host and target specific makefile fragments come in here.
@target_makefile_frag@
@alphaieee_frag@
@ospace_frag@
@host_makefile_frag@
###

# This is the list of directories that may be needed in RPATH_ENVVAR
# so that prorgams built for the target machine work.
TARGET_LIB_PATH = [+ FOR target_modules +][+
  IF lib_path +]$(TARGET_LIB_PATH_[+module+])[+ ENDIF lib_path +][+
  ENDFOR target_modules +]$(HOST_LIB_PATH_gcc)
[+ FOR target_modules +][+ IF lib_path +]
@if target-[+module+]
TARGET_LIB_PATH_[+module+] = $$r/$(TARGET_SUBDIR)/[+module+]/[+lib_path+]:
@endif target-[+module+]
[+ ENDIF lib_path +][+ ENDFOR target_modules +]


# This is the list of directories that may be needed in RPATH_ENVVAR
# so that programs built for the host machine work.
HOST_LIB_PATH = [+ FOR host_modules +][+
  IF lib_path +]$(HOST_LIB_PATH_[+module+])[+ ENDIF lib_path +][+
  ENDFOR host_modules +]

# Define HOST_LIB_PATH_gcc here, for the sake of TARGET_LIB_PATH, ouch
@if gcc
HOST_LIB_PATH_gcc = $$r/$(HOST_SUBDIR)/gcc:$$r/$(HOST_SUBDIR)/prev-gcc:
@endif gcc

[+ FOR host_modules +][+ IF lib_path +]
@if [+module+]
HOST_LIB_PATH_[+module+] = \
  $$r/$(HOST_SUBDIR)/[+module+]/[+lib_path+]:[+ IF bootstrap
  +]$$r/$(HOST_SUBDIR)/prev-[+module+]/[+lib_path+]:[+ ENDIF bootstrap +]
@endif [+module+]
[+ ENDIF lib_path +][+ ENDFOR host_modules +]

# Flags to pass down to all sub-makes.
BASE_FLAGS_TO_PASS =[+ FOR flags_to_pass +][+ IF optional +] \
	"`echo '[+flag+]=$([+flag+])' | sed -e s'/[^=][^=]*=$$/XFOO=/'`"[+ ELSE optional +] \
	"[+flag+]=$([+flag+])"[+ ENDIF optional+][+ ENDFOR flags_to_pass +] \
	"CONFIG_SHELL=$(SHELL)" \
	"MAKEINFO=$(MAKEINFO) $(MAKEINFOFLAGS)" 

# We leave this in just in case, but it is not needed anymore.
RECURSE_FLAGS_TO_PASS = $(BASE_FLAGS_TO_PASS)

# Flags to pass down to most sub-makes, in which we're building with
# the host environment.
EXTRA_HOST_FLAGS = \
	'AR=$(AR)' \
	'AS=$(AS)' \
	'CC=$(CC)' \
	'CXX=$(CXX)' \
	'DLLTOOL=$(DLLTOOL)' \
	'LD=$(LD)' \
	'LIPO=$(LIPO)' \
	'NM=$(NM)' \
	'OBJDUMP=$(OBJDUMP)' \
	'RANLIB=$(RANLIB)' \
	'STRIP=$(STRIP)' \
	'WINDRES=$(WINDRES)'

FLAGS_TO_PASS = $(BASE_FLAGS_TO_PASS) $(EXTRA_HOST_FLAGS)

# Flags that are concerned with the location of the X11 include files
# and library files
#
# NOTE: until the top-level is getting the values via autoconf, it only
# causes problems to have this top-level Makefile overriding the autoconf-set
# values in child directories.  Only variables that don't conflict with
# autoconf'ed ones should be passed by X11_FLAGS_TO_PASS for now.
#
X11_FLAGS_TO_PASS = \
	'X11_EXTRA_CFLAGS=$(X11_EXTRA_CFLAGS)' \
	'X11_EXTRA_LIBS=$(X11_EXTRA_LIBS)'

# Flags to pass to stage2 and later makes.

POSTSTAGE1_FLAGS_TO_PASS = \
	CC="$${CC}" CC_FOR_BUILD="$${CC_FOR_BUILD}" \
	GNATBIND="$$r/$(HOST_SUBDIR)/prev-gcc/gnatbind" \
	LDFLAGS="$(BOOT_LDFLAGS)" \
	"`echo 'ADAFLAGS=$(BOOT_ADAFLAGS)' | sed -e s'/[^=][^=]*=$$/XFOO=/'`"

# Flags to pass down to makes which are built with the target environment.
# The double $ decreases the length of the command line; those variables
# are set in BASE_FLAGS_TO_PASS, and the sub-make will expand them.  The
# COMPILER_ prefixed variables are not passed down so we expand them here.
EXTRA_TARGET_FLAGS = \
	'AR=$$(AR_FOR_TARGET)' \
	'AS=$(COMPILER_AS_FOR_TARGET)' \
	'CC=$$(CC_FOR_TARGET)' \
	'CFLAGS=$$(CFLAGS_FOR_TARGET)' \
	'CXX=$$(CXX_FOR_TARGET)' \
	'CXXFLAGS=$$(CXXFLAGS_FOR_TARGET)' \
	'DLLTOOL=$$(DLLTOOL_FOR_TARGET)' \
	'LD=$(COMPILER_LD_FOR_TARGET)' \
	'LDFLAGS=$$(LDFLAGS_FOR_TARGET)' \
	'LIBCFLAGS=$$(LIBCFLAGS_FOR_TARGET)' \
	'LIBCXXFLAGS=$$(LIBCXXFLAGS_FOR_TARGET)' \
	'NM=$(COMPILER_NM_FOR_TARGET)' \
	'OBJDUMP=$$(OBJDUMP_FOR_TARGET)' \
	'RANLIB=$$(RANLIB_FOR_TARGET)' \
	'WINDRES=$$(WINDRES_FOR_TARGET)'

TARGET_FLAGS_TO_PASS = $(BASE_FLAGS_TO_PASS) $(EXTRA_TARGET_FLAGS)

# Flags to pass down to gcc.  gcc builds a library, libgcc.a, so it
# unfortunately needs the native compiler and the target ar and
# ranlib.
# If any variables are added here, they must be added to do-*, below.
# The BUILD_* variables are a special case, which are used for the gcc
# cross-building scheme.
EXTRA_GCC_FLAGS = \
	"GCC_FOR_TARGET=$(GCC_FOR_TARGET)" \
	"`echo 'STMP_FIXPROTO=$(STMP_FIXPROTO)' | sed -e s'/[^=][^=]*=$$/XFOO=/'`" \
	"`echo 'LIMITS_H_TEST=$(LIMITS_H_TEST)' | sed -e s'/[^=][^=]*=$$/XFOO=/'`" \
	"`echo 'LIBGCC2_CFLAGS=$(LIBGCC2_CFLAGS)' | sed -e s'/[^=][^=]*=$$/XFOO=/'`" \
	"`echo 'LIBGCC2_DEBUG_CFLAGS=$(LIBGCC2_DEBUG_CFLAGS)' | sed -e s'/[^=][^=]*=$$/XFOO=/'`" \
	"`echo 'LIBGCC2_INCLUDES=$(LIBGCC2_INCLUDES)' | sed -e s'/[^=][^=]*=$$/XFOO=/'`"

GCC_FLAGS_TO_PASS = $(BASE_FLAGS_TO_PASS) $(EXTRA_HOST_FLAGS) $(EXTRA_GCC_FLAGS)

.PHONY: configure-host
configure-host: [+
  FOR host_modules +] \
    maybe-configure-[+module+][+
  ENDFOR host_modules +]
.PHONY: configure-target
configure-target: [+
  FOR target_modules +] \
    maybe-configure-target-[+module+][+
  ENDFOR target_modules +]

# The target built for a native non-bootstrap build.
.PHONY: all
all:
@if gcc-bootstrap
	[ -f stage_final ] || echo stage3 > stage_final
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(MAKE) $(RECURSE_FLAGS_TO_PASS) `cat stage_final`-bubble
@endif gcc-bootstrap
	@: $(MAKE); $(unstage)
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	if [ -f stage_last ]; then \
	  $(MAKE) $(TARGET_FLAGS_TO_PASS) all-host all-target; \
	else \
	  $(MAKE) $(RECURSE_FLAGS_TO_PASS) all-host all-target; \
	fi

.PHONY: all-build
[+ FOR build_modules +]
all-build: maybe-all-build-[+module+][+ ENDFOR build_modules +]

.PHONY: all-host
[+ FOR host_modules +][+ IF bootstrap +]
@if [+module+]-no-bootstrap[+ ENDIF bootstrap +]
all-host: maybe-all-[+module+][+ IF bootstrap +]
@endif [+module+]-no-bootstrap[+ ENDIF bootstrap +][+ ENDFOR host_modules +]

.PHONY: all-target
[+ FOR target_modules +][+ IF bootstrap +]
@if target-[+module+]-no-bootstrap[+ ENDIF bootstrap +]
all-target: maybe-all-target-[+module+][+ IF bootstrap +]
@endif target-[+module+]-no-bootstrap[+
  ENDIF bootstrap +][+ ENDFOR target_modules +]

# Do a target for all the subdirectories.  A ``make do-X'' will do a
# ``make X'' in all subdirectories (because, in general, there is a
# dependency (below) of X upon do-X, a ``make X'' will also do this,
# but it may do additional work as well).
[+ FOR recursive_targets +]
.PHONY: do-[+make_target+]
do-[+make_target+]:
	@: $(MAKE); $(unstage)
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(MAKE) $(RECURSE_FLAGS_TO_PASS) [+make_target+]-host \
	  [+make_target+]-target


.PHONY: [+make_target+]-host
[+ FOR host_modules +]
[+make_target+]-host: maybe-[+make_target+]-[+module+][+ ENDFOR host_modules +]

.PHONY: [+make_target+]-target
[+ FOR target_modules +]
[+make_target+]-target: maybe-[+make_target+]-target-[+module+][+ ENDFOR target_modules +]
[+ ENDFOR recursive_targets +]

# Here are the targets which correspond to the do-X targets.

.PHONY: info installcheck dvi pdf html
.PHONY: install-info install-pdf install-html
.PHONY: clean distclean mostlyclean maintainer-clean realclean
.PHONY: local-clean local-distclean local-maintainer-clean
info: do-info
installcheck: do-installcheck
dvi: do-dvi
pdf: do-pdf
html: do-html

# Make sure makeinfo is built before we do a `make info', if we're
# in fact building texinfo.
do-info: maybe-all-texinfo

install-info: do-install-info dir.info
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	if [ -f dir.info ] ; then \
	  $(INSTALL_DATA) dir.info $(DESTDIR)$(infodir)/dir.info ; \
	else true ; fi

install-pdf: do-install-pdf

install-html: do-install-html

local-clean:
	-rm -f *.a TEMP errs core *.o *~ \#* TAGS *.E *.log

local-distclean:
	-rm -f Makefile config.status config.cache mh-frag mt-frag
	-rm -f maybedep.tmp serdep.tmp
	-if [ "$(TARGET_SUBDIR)" != "." ]; then \
	  rm -rf $(TARGET_SUBDIR); \
	else true; fi
	-rm -rf $(BUILD_SUBDIR)
	-if [ "$(HOST_SUBDIR)" != "." ]; then \
	  rm -rf $(HOST_SUBDIR); \
	else true; fi
	-rm -f texinfo/po/Makefile texinfo/po/Makefile.in texinfo/info/Makefile
	-rm -f texinfo/doc/Makefile texinfo/po/POTFILES
	-rmdir texinfo/doc texinfo/info texinfo/intl texinfo/lib 2>/dev/null
	-rmdir texinfo/makeinfo texinfo/po texinfo/util 2>/dev/null
	-rmdir fastjar gcc libiberty texinfo zlib 2>/dev/null

local-maintainer-clean:
	@echo "This command is intended for maintainers to use;"
	@echo "it deletes files that may require special tools to rebuild."

clean: do-clean local-clean
mostlyclean: do-mostlyclean local-clean
distclean: do-distclean local-clean local-distclean
maintainer-clean: local-maintainer-clean do-maintainer-clean local-clean 
maintainer-clean: local-distclean
realclean: maintainer-clean

# Check target.

.PHONY: check do-check
check: do-check

# Only include modules actually being configured and built.
.PHONY: check-host
check-host: [+
  FOR host_modules +] \
    maybe-check-[+module+][+
  ENDFOR host_modules +]

.PHONY: check-target
check-target: [+
  FOR target_modules +] \
    maybe-check-target-[+module+][+
  ENDFOR target_modules +]

do-check:
	@: $(MAKE); $(unstage)
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(MAKE) $(RECURSE_FLAGS_TO_PASS) check-host check-target

# Automated reporting of test results.

warning.log: build.log
	$(srcdir)/contrib/warn_summary build.log > $@

mail-report.log:
	if test x'$(BOOT_CFLAGS)' != x''; then \
	    BOOT_CFLAGS='$(BOOT_CFLAGS)'; export BOOT_CFLAGS; \
	fi; \
	$(srcdir)/contrib/test_summary -t >$@
	chmod +x $@
	echo If you really want to send e-mail, run ./$@ now

mail-report-with-warnings.log: warning.log
	if test x'$(BOOT_CFLAGS)' != x''; then \
	    BOOT_CFLAGS='$(BOOT_CFLAGS)'; export BOOT_CFLAGS; \
	fi; \
	$(srcdir)/contrib/test_summary -t -i warning.log >$@
	chmod +x $@
	echo If you really want to send e-mail, run ./$@ now

# Installation targets.

.PHONY: install uninstall
install:
	@: $(MAKE); $(unstage)
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(MAKE) $(RECURSE_FLAGS_TO_PASS) installdirs install-host install-target

.PHONY: install-host-nogcc
install-host-nogcc: [+
  FOR host_modules +][+ IF (not (= (get "module") "gcc")) +] \
    maybe-install-[+module+][+ ENDIF +][+
  ENDFOR host_modules +]

.PHONY: install-host
install-host: [+
  FOR host_modules +] \
    maybe-install-[+module+][+
  ENDFOR host_modules +]

.PHONY: install-target
install-target: [+
  FOR target_modules +] \
    maybe-install-target-[+module+][+
  ENDFOR target_modules +]

uninstall:
	@echo "the uninstall target is not supported in this tree"

.PHONY: install.all
install.all: install-no-fixedincludes
	@if [ -f ./gcc/Makefile ] ; then \
		r=`${PWD_COMMAND}` ; export r ; \
		s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
		$(HOST_EXPORTS) \
		(cd ./gcc && \
		$(MAKE) $(FLAGS_TO_PASS) install-headers) ; \
	else \
		true ; \
	fi

# install-no-fixedincludes is used because Cygnus can not distribute
# the fixed header files.
.PHONY: install-no-fixedincludes
install-no-fixedincludes: installdirs install-host-nogcc \
	install-target gcc-no-fixedincludes

### other supporting targets

MAKEDIRS= \
	$(DESTDIR)$(prefix) \
	$(DESTDIR)$(exec_prefix)
.PHONY: installdirs
installdirs: mkinstalldirs
	$(SHELL) $(srcdir)/mkinstalldirs $(MAKEDIRS)

dir.info: do-install-info
	if [ -f $(srcdir)/texinfo/gen-info-dir ] ; then \
	  $(srcdir)/texinfo/gen-info-dir $(DESTDIR)$(infodir) $(srcdir)/texinfo/dir.info-template > dir.info.new ; \
	  mv -f dir.info.new dir.info ; \
	else true ; \
	fi

dist:
	@echo "Building a full distribution of this tree isn't done"
	@echo "via 'make dist'.  Check out the etc/ subdirectory" 

etags tags: TAGS

# Right now this just builds TAGS in each subdirectory.  emacs19 has the
# ability to use several tags files at once, so there is probably no need
# to combine them into one big TAGS file (like CVS 1.3 does).  We could
# (if we felt like it) have this Makefile write a piece of elisp which
# the user could load to tell emacs19 where all the TAGS files we just
# built are.
TAGS: do-TAGS

# ------------------------------------
# Macros for configure and all targets
# ------------------------------------

[+ DEFINE configure +]
.PHONY: configure-[+prefix+][+module+] maybe-configure-[+prefix+][+module+]
maybe-configure-[+prefix+][+module+]:
@if gcc-bootstrap
configure-[+prefix+][+module+]: stage_current
@endif gcc-bootstrap
@if [+prefix+][+module+]
maybe-configure-[+prefix+][+module+]: configure-[+prefix+][+module+]
configure-[+prefix+][+module+]: [+ IF bootstrap +][+ ELSE +]
	@: $(MAKE); $(unstage)[+ ENDIF bootstrap +]
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	[+ IF check_multilibs
	+]echo "Checking multilib configuration for [+module+]..."; \
	$(SHELL) $(srcdir)/mkinstalldirs [+subdir+]/[+module+] ; \
	$(CC_FOR_TARGET) --print-multi-lib > [+subdir+]/[+module+]/multilib.tmp 2> /dev/null ; \
	if test -r [+subdir+]/[+module+]/multilib.out; then \
	  if cmp -s [+subdir+]/[+module+]/multilib.tmp [+subdir+]/[+module+]/multilib.out; then \
	    rm -f [+subdir+]/[+module+]/multilib.tmp; \
	  else \
	    rm -f [+subdir+]/[+module+]/Makefile; \
	    mv [+subdir+]/[+module+]/multilib.tmp [+subdir+]/[+module+]/multilib.out; \
	  fi; \
	else \
	  mv [+subdir+]/[+module+]/multilib.tmp [+subdir+]/[+module+]/multilib.out; \
	fi; \
	[+ ENDIF check_multilibs +]test ! -f [+subdir+]/[+module+]/Makefile || exit 0; \
	$(SHELL) $(srcdir)/mkinstalldirs [+subdir+]/[+module+] ; \
	[+exports+] \
	echo Configuring in [+subdir+]/[+module+]; \
	cd "[+subdir+]/[+module+]" || exit 1; \
	case $(srcdir) in \
	  /* | [A-Za-z]:[\\/]*) topdir=$(srcdir) ;; \
	  *) topdir=`echo [+subdir+]/[+module+]/ | \
		sed -e 's,\./,,g' -e 's,[^/]*/,../,g' `$(srcdir) ;; \
	esac; \
	srcdiroption="--srcdir=$${topdir}/[+module+]"; \
	libsrcdir="$$s/[+module+]"; \
	[+ IF no-config-site +]rm -f no-such-file || : ; \
	CONFIG_SITE=no-such-file [+ ENDIF +]$(SHELL) $${libsrcdir}/configure \
	  [+args+] --build=${build_alias} --host=[+host_alias+] \
	  --target=[+target_alias+] $${srcdiroption} [+extra_configure_flags+] \
	  || exit 1
@endif [+prefix+][+module+]

[+ IF bootstrap +]
[+ FOR bootstrap_stage +]
.PHONY: configure-stage[+id+]-[+prefix+][+module+] maybe-configure-stage[+id+]-[+prefix+][+module+]
maybe-configure-stage[+id+]-[+prefix+][+module+]:
@if [+prefix+][+module+]-bootstrap
maybe-configure-stage[+id+]-[+prefix+][+module+]: configure-stage[+id+]-[+prefix+][+module+]
configure-stage[+id+]-[+prefix+][+module+]:
	@[ $(current_stage) = stage[+id+] ] || $(MAKE) stage[+id+]-start
	@$(SHELL) $(srcdir)/mkinstalldirs [+subdir+]/[+module+]
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	[+ IF check_multilibs
	+]echo "Checking multilib configuration for [+module+]..."; \
	$(CC_FOR_TARGET) --print-multi-lib > [+subdir+]/[+module+]/multilib.tmp 2> /dev/null ; \
	if test -r [+subdir+]/[+module+]/multilib.out; then \
	  if cmp -s [+subdir+]/[+module+]/multilib.tmp [+subdir+]/[+module+]/multilib.out; then \
	    rm -f [+subdir+]/[+module+]/multilib.tmp; \
	  else \
	    rm -f [+subdir+]/[+module+]/Makefile; \
	    mv [+subdir+]/[+module+]/multilib.tmp [+subdir+]/[+module+]/multilib.out; \
	  fi; \
	else \
	  mv [+subdir+]/[+module+]/multilib.tmp [+subdir+]/[+module+]/multilib.out; \
	fi; \
	[+ ENDIF check_multilibs +]test ! -f [+subdir+]/[+module+]/Makefile || exit 0; \
	[+exports+][+ IF prev +] \
	[+poststage1_exports+][+ ENDIF prev +] \
	CFLAGS="[+stage_cflags+]"; export CFLAGS; \
	LIBCFLAGS="[+stage_cflags+]"; export LIBCFLAGS; \
	echo Configuring stage [+id+] in [+subdir+]/[+module+] ; \
	$(SHELL) $(srcdir)/mkinstalldirs [+subdir+]/[+module+] ; \
	cd [+subdir+]/[+module+] || exit 1; \
	case $(srcdir) in \
	  /* | [A-Za-z]:[\\/]*) topdir=$(srcdir) ;; \
	  *) topdir=`echo [+subdir+]/[+module+]/ | \
		sed -e 's,\./,,g' -e 's,[^/]*/,../,g' `$(srcdir) ;; \
	esac; \
	srcdiroption="--srcdir=$${topdir}/[+module+]"; \
	libsrcdir="$$s/[+module+]"; \
	$(SHELL) $${libsrcdir}/configure \
	  [+args+] --build=${build_alias} --host=[+host_alias+] \
	  --target=[+target_alias+] $${srcdiroption} \
	  [+ IF prev +]--with-build-libsubdir=$(HOST_SUBDIR)[+ ENDIF prev +] \
	  [+stage_configure_flags+] [+extra_configure_flags+]
@endif [+prefix+][+module+]-bootstrap
[+ ENDFOR bootstrap_stage +]
[+ ENDIF bootstrap +]
[+ ENDDEF +]

[+ DEFINE all +]
.PHONY: all-[+prefix+][+module+] maybe-all-[+prefix+][+module+]
maybe-all-[+prefix+][+module+]:
@if gcc-bootstrap
all-[+prefix+][+module+]: stage_current
@endif gcc-bootstrap
@if [+prefix+][+module+]
TARGET-[+prefix+][+module+]=[+
  IF all_target +][+all_target+][+ ELSE +]all[+ ENDIF all_target +]
maybe-all-[+prefix+][+module+]: all-[+prefix+][+module+]
all-[+prefix+][+module+]: configure-[+prefix+][+module+][+ IF bootstrap +][+ ELSE +]
	@: $(MAKE); $(unstage)[+ ENDIF bootstrap +]
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	[+exports+] \
	(cd [+subdir+]/[+module+] && \
	  $(MAKE) [+args+] [+extra_make_flags+] $(TARGET-[+prefix+][+module+]))
@endif [+prefix+][+module+]

[+ IF bootstrap +]
[+ FOR bootstrap_stage +]
.PHONY: all-stage[+id+]-[+prefix+][+module+] maybe-all-stage[+id+]-[+prefix+][+module+]
.PHONY: clean-stage[+id+]-[+prefix+][+module+] maybe-clean-stage[+id+]-[+prefix+][+module+]
maybe-all-stage[+id+]-[+prefix+][+module+]:
maybe-clean-stage[+id+]-[+prefix+][+module+]:
@if [+prefix+][+module+]-bootstrap
maybe-all-stage[+id+]-[+prefix+][+module+]: all-stage[+id+]-[+prefix+][+module+]
all-stage[+id+]: all-stage[+id+]-[+prefix+][+module+]
TARGET-stage[+id+]-[+prefix+][+module+] = $(TARGET-[+prefix+][+module+])
all-stage[+id+]-[+prefix+][+module+]: configure-stage[+id+]-[+prefix+][+module+]
	@[ $(current_stage) = stage[+id+] ] || $(MAKE) stage[+id+]-start
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	[+exports+][+ IF prev +] \
	[+poststage1_exports+][+ ENDIF prev +] \
	cd [+subdir+]/[+module+] && \
	$(MAKE) [+args+] \
		CFLAGS="[+stage_cflags+]" LIBCFLAGS="[+stage_cflags+]" [+
		IF prev +][+poststage1_args+][+ ENDIF prev
		+] [+extra_make_flags+] \
		$(TARGET-stage[+id+]-[+prefix+][+module+])

maybe-clean-stage[+id+]-[+prefix+][+module+]: clean-stage[+id+]-[+prefix+][+module+]
clean-stage[+id+]: clean-stage[+id+]-[+prefix+][+module+]
clean-stage[+id+]-[+prefix+][+module+]:
	@if [ $(current_stage) = stage[+id+] ]; then \
	  [ -f [+subdir+]/[+module+]/Makefile ] || exit 0; \
	else \
	  [ -f [+subdir+]/stage[+id+]-[+module+]/Makefile ] || exit 0; \
	  $(MAKE) stage[+id+]-start; \
	fi; \
	cd [+subdir+]/[+module+] && \
	$(MAKE) [+args+] [+ IF prev +] \
		[+poststage1_args+] [+ ENDIF prev +] \
		[+extra_make_flags+] clean
@endif [+prefix+][+module+]-bootstrap

[+ ENDFOR bootstrap_stage +]
[+ ENDIF bootstrap +]
[+ ENDDEF +]

# --------------------------------------
# Modules which run on the build machine
# --------------------------------------
[+ FOR build_modules +]
[+ configure prefix="build-" subdir="$(BUILD_SUBDIR)" exports="$(BUILD_EXPORTS)"
	     host_alias=(get "host" "${build_alias}")
	     target_alias=(get "target" "${target_alias}")
	     args="$(BUILD_CONFIGARGS)" no-config-site=true +]

[+ all prefix="build-" subdir="$(BUILD_SUBDIR)" exports="$(BUILD_EXPORTS)" +]
[+ ENDFOR build_module +]

# --------------------------------------
# Modules which run on the host machine
# --------------------------------------
[+ FOR host_modules +]
[+ configure prefix="" subdir="$(HOST_SUBDIR)"
	     exports="$(HOST_EXPORTS)"
	     poststage1_exports="$(POSTSTAGE1_HOST_EXPORTS)"
	     host_alias=(get "host" "${host_alias}")
	     target_alias=(get "target" "${target_alias}")
	     args="$(HOST_CONFIGARGS)" +]

[+ all prefix="" subdir="$(HOST_SUBDIR)"
       exports="$(HOST_EXPORTS)"
       poststage1_exports="$(POSTSTAGE1_HOST_EXPORTS)"
       args="$(FLAGS_TO_PASS)"
       poststage1_args="$(POSTSTAGE1_FLAGS_TO_PASS)" +]

.PHONY: check-[+module+] maybe-check-[+module+]
maybe-check-[+module+]:
@if [+module+]
maybe-check-[+module+]: check-[+module+]
[+ IF no_check +]
check-[+module+]:
[+ ELIF no_check_cross +]
# This module is only tested in a native toolchain.
check-[+module+]:
	@: $(MAKE); $(unstage)
	@if [ '$(host)' = '$(target)' ] ; then \
	  r=`${PWD_COMMAND}`; export r; \
	  s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	  $(HOST_EXPORTS) \
	  (cd $(HOST_SUBDIR)/[+module+] && \
	    $(MAKE) $(FLAGS_TO_PASS) [+extra_make_flags+] check); \
	fi
[+ ELSE check +]
check-[+module+]:
	@: $(MAKE); $(unstage)
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(HOST_EXPORTS) \
	(cd $(HOST_SUBDIR)/[+module+] && \
	  $(MAKE) $(FLAGS_TO_PASS) [+extra_make_flags+] check)
[+ ENDIF no_check +]
@endif [+module+]

.PHONY: install-[+module+] maybe-install-[+module+]
maybe-install-[+module+]:
@if [+module+]
maybe-install-[+module+]: install-[+module+]
[+ IF no_install +]
install-[+module+]:
[+ ELSE install +]
install-[+module+]: installdirs
	@: $(MAKE); $(unstage)
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(HOST_EXPORTS) \
	(cd $(HOST_SUBDIR)/[+module+] && \
	  $(MAKE) $(FLAGS_TO_PASS) [+extra_make_flags+] install)
[+ ENDIF no_install +]
@endif [+module+]

# Other targets (info, dvi, pdf, etc.)
[+ FOR recursive_targets +]
.PHONY: maybe-[+make_target+]-[+module+] [+make_target+]-[+module+]
maybe-[+make_target+]-[+module+]:
@if [+module+]
maybe-[+make_target+]-[+module+]: [+make_target+]-[+module+]
[+ IF (match-value? = "missing" (get "make_target") ) +]
# [+module+] doesn't support [+make_target+].
[+make_target+]-[+module+]:
[+ ELSE +]
[+make_target+]-[+module+]: [+
  FOR depend +]\
    [+depend+]-[+module+] [+
  ENDFOR depend +]
	@[+ IF bootstrap +][+ ELSE +]: $(MAKE); $(unstage)
	@[+ ENDIF bootstrap +][ -f ./[+module+]/Makefile ] || exit 0; \
	r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(HOST_EXPORTS) \
	for flag in $(EXTRA_HOST_FLAGS) [+extra_make_flags+]; do \
	  eval `echo "$$flag" | sed -e "s|^\([^=]*\)=\(.*\)|\1='\2'; export \1|"`; \
	done; \
	echo "Doing [+make_target+] in [+module+]" ; \
	(cd $(HOST_SUBDIR)/[+module+] && \
	  $(MAKE) $(BASE_FLAGS_TO_PASS) "AR=$${AR}" "AS=$${AS}" \
	          "CC=$${CC}" "CXX=$${CXX}" "LD=$${LD}" "NM=$${NM}" \
	          "RANLIB=$${RANLIB}" \
	          "DLLTOOL=$${DLLTOOL}" "WINDRES=$${WINDRES}" \
	          [+make_target+]) \
	  || exit 1
[+ ENDIF +]
@endif [+module+]
[+ ENDFOR recursive_targets +]
[+ ENDFOR host_modules +]

# ---------------------------------------
# Modules which run on the target machine
# ---------------------------------------
[+ FOR target_modules +]

[+ IF raw_cxx +]
[+ configure prefix="target-" subdir="$(TARGET_SUBDIR)"
	     check_multilibs=true
	     exports="$(RAW_CXX_TARGET_EXPORTS)"
	     host_alias=(get "host" "${target_alias}")
	     target_alias=(get "target" "${target_alias}")
	     args="$(TARGET_CONFIGARGS)" no-config-site=true +]

[+ all prefix="target-" subdir="$(TARGET_SUBDIR)"
       exports="$(RAW_CXX_TARGET_EXPORTS)"
       args="$(TARGET_FLAGS_TO_PASS) 'CXX=$$(RAW_CXX_FOR_TARGET)' 'CXX_FOR_TARGET=$$(RAW_CXX_FOR_TARGET)'" +]
[+ ELSE +]
[+ configure prefix="target-" subdir="$(TARGET_SUBDIR)"
	     check_multilibs=true
	     exports="$(NORMAL_TARGET_EXPORTS)"
	     host_alias=(get "host" "${target_alias}")
	     target_alias=(get "target" "${target_alias}")
	     args="$(TARGET_CONFIGARGS)" no-config-site=true +]

[+ all prefix="target-" subdir="$(TARGET_SUBDIR)"
       exports="$(NORMAL_TARGET_EXPORTS)"
       args="$(TARGET_FLAGS_TO_PASS)" +]
[+ ENDIF +]

.PHONY: check-target-[+module+] maybe-check-target-[+module+]
maybe-check-target-[+module+]:
@if target-[+module+]
maybe-check-target-[+module+]: check-target-[+module+]
[+ IF no_check +]
# Dummy target for uncheckable module.
check-target-[+module+]:
[+ ELSE check +]
check-target-[+module+]:
	@: $(MAKE); $(unstage)
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \[+
IF raw_cxx +]
	$(RAW_CXX_TARGET_EXPORTS) \[+
ELSE normal_cxx +]
	$(NORMAL_TARGET_EXPORTS) \[+
ENDIF raw_cxx +]
	(cd $(TARGET_SUBDIR)/[+module+] && \
	  $(MAKE) $(TARGET_FLAGS_TO_PASS) [+
	    IF raw_cxx 
	      +] 'CXX=$$(RAW_CXX_FOR_TARGET)' 'CXX_FOR_TARGET=$$(RAW_CXX_FOR_TARGET)' [+ 
	    ENDIF raw_cxx 
	  +] [+extra_make_flags+] check)
[+ ENDIF no_check +]
@endif target-[+module+]

.PHONY: install-target-[+module+] maybe-install-target-[+module+]
maybe-install-target-[+module+]:
@if target-[+module+]
maybe-install-target-[+module+]: install-target-[+module+]
[+ IF no_install +]
# Dummy target for uninstallable.
install-target-[+module+]:
[+ ELSE install +]
install-target-[+module+]: installdirs
	@: $(MAKE); $(unstage)
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \[+
IF raw_cxx +]
	$(RAW_CXX_TARGET_EXPORTS) \[+
ELSE normal_cxx +]
	$(NORMAL_TARGET_EXPORTS) \[+
ENDIF raw_cxx +]
	(cd $(TARGET_SUBDIR)/[+module+] && \
	  $(MAKE) $(TARGET_FLAGS_TO_PASS) [+extra_make_flags+] install)
[+ ENDIF no_install +]
@endif target-[+module+]

# Other targets (info, dvi, pdf, etc.)
[+ FOR recursive_targets +]
.PHONY: maybe-[+make_target+]-target-[+module+] [+make_target+]-target-[+module+]
maybe-[+make_target+]-target-[+module+]:
@if target-[+module+]
maybe-[+make_target+]-target-[+module+]: [+make_target+]-target-[+module+]
[+ IF (match-value? = "missing" (get "make_target") ) +]
# [+module+] doesn't support [+make_target+].
[+make_target+]-target-[+module+]:
[+ ELSE +]
[+make_target+]-target-[+module+]: [+
  FOR depend +]\
    [+depend+]-target-[+module+] [+
  ENDFOR depend +]
	@: $(MAKE); $(unstage)
	@[ -f $(TARGET_SUBDIR)/[+module+]/Makefile ] || exit 0 ; \
	r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \[+
IF raw_cxx +]
	$(RAW_CXX_TARGET_EXPORTS) \[+
ELSE normal_cxx +]
	$(NORMAL_TARGET_EXPORTS) \[+
ENDIF raw_cxx +]
	echo "Doing [+make_target+] in $(TARGET_SUBDIR)/[+module+]" ; \
	for flag in $(EXTRA_TARGET_FLAGS); do \
	  eval `echo "$$flag" | sed -e "s|^\([^=]*\)=\(.*\)|\1='\2'; export \1|"`; \
	done; \
	(cd $(TARGET_SUBDIR)/[+module+] && \
	  $(MAKE) $(BASE_FLAGS_TO_PASS) "AR=$${AR}" "AS=$${AS}" \
	          "CC=$${CC}" "CXX=$${CXX}" "LD=$${LD}" "NM=$${NM}" \
	          "RANLIB=$${RANLIB}" \
	          "DLLTOOL=$${DLLTOOL}" "WINDRES=$${WINDRES}" \
	          [+extra_make_flags+] [+make_target+]) \
	  || exit 1
[+ ENDIF +]
@endif target-[+module+]
[+ ENDFOR recursive_targets +]
[+ ENDFOR target_modules +]

# ----------
# GCC module
# ----------

@if gcc-no-bootstrap
.PHONY: cross
cross: all-build all-gas all-ld
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(HOST_EXPORTS) \
	echo "Building the C and C++ compiler"; \
	cd gcc && $(MAKE) $(GCC_FLAGS_TO_PASS) LANGUAGES="c c++"
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}` ; export s; \
	echo "Building runtime libraries"; \
	$(MAKE) $(RECURSE_FLAGS_TO_PASS) LANGUAGES="c c++" all
@endif gcc-no-bootstrap

@if gcc
.PHONY: check-gcc-c++
check-gcc-c++:
	@if [ -f ./gcc/Makefile ] ; then \
	  r=`${PWD_COMMAND}`; export r; \
	  s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	  $(HOST_EXPORTS) \
	  (cd gcc && $(MAKE) $(GCC_FLAGS_TO_PASS) check-c++); \
	else \
	  true; \
	fi

.PHONY: check-c++
check-c++: check-target-libstdc++-v3 check-gcc-c++

# Install the gcc headers files, but not the fixed include files,
# which Cygnus is not allowed to distribute.  This rule is very
# dependent on the workings of the gcc Makefile.in.
.PHONY: gcc-no-fixedincludes
gcc-no-fixedincludes:
	@if [ -f ./gcc/Makefile ]; then \
	  rm -rf gcc/tmp-include; \
	  mv gcc/include gcc/tmp-include 2>/dev/null; \
	  mkdir gcc/include; \
	  cp $(srcdir)/gcc/gsyslimits.h gcc/include/syslimits.h; \
	  touch gcc/stmp-fixinc gcc/include/fixed; \
	  rm -f gcc/stmp-headers gcc/stmp-int-hdrs; \
	  r=`${PWD_COMMAND}`; export r; \
	  s=`cd $(srcdir); ${PWD_COMMAND}` ; export s; \
	  $(HOST_EXPORTS) \
	  (cd ./gcc && \
	   $(MAKE) $(GCC_FLAGS_TO_PASS) install); \
	  rm -rf gcc/include; \
	  mv gcc/tmp-include gcc/include 2>/dev/null; \
	else true; fi
@endif gcc

# ---------------------
# GCC bootstrap support
# ---------------------

# We track the current stage (the one in 'gcc') in the stage_current file.
# stage_last instead tracks the stage that was built last.  These targets
# are dummy when toplevel bootstrap is not active.

# While making host and target tools, symlinks to the final stage must be
# there, so $(unstage) should be run at various points.  To avoid excessive
# recursive invocations of make, we "inline" them using a variable.  These
# must be referenced as ": $(MAKE) ; $(unstage)" rather than "$(unstage)"
# to avoid warnings from the GNU Make job server.

unstage = :
stage = :
current_stage = ""

@if gcc-bootstrap
unstage = if [ -f stage_last ]; then [ -f stage_current ] || $(MAKE) `cat stage_last`-start || exit 1; else :; fi
stage = if [ -f stage_current ]; then $(MAKE) `cat stage_current`-end || exit 1; else :; fi
current_stage = "`cat stage_current 2> /dev/null`"
@endif gcc-bootstrap

.PHONY: unstage stage
unstage:
	@: $(MAKE); $(unstage)
stage:
	@: $(MAKE); $(stage)

# Disable commands for lean bootstrap.
LEAN = false

# We name the build directories for the various stages "stage1-gcc",
# "stage2-gcc","stage3-gcc", etc.

# Since the 'compare' process will fail (on debugging information) if any
# directory names are different, we need to link the gcc directory for
# the previous stage to a constant name ('prev-gcc'), and to make the name of
# the build directories constant as well. For the latter, we use naked names
# like 'gcc', because the scripts in that directory assume it.  We use
# mv on platforms where symlinks to directories do not work or are not
# reliable.

# 'touch' doesn't work right on some platforms.
STAMP = echo timestamp > 

# We only want to compare .o files, so set this!
objext = .o

[+ FOR bootstrap-stage +]
.PHONY: stage[+id+]-start stage[+id+]-end

stage[+id+]-start::
	@: $(MAKE); $(stage); \
	echo stage[+id+] > stage_current ; \
	echo stage[+id+] > stage_last; \
	$(SHELL) $(srcdir)/mkinstalldirs $(HOST_SUBDIR)[+
   FOR host_modules +][+ IF bootstrap +]
@if [+ module +]
	@cd $(HOST_SUBDIR); [ -d stage[+id+]-[+module+] ] || \
	  mkdir stage[+id+]-[+module+]; \
	mv stage[+id+]-[+module+] [+module+] [+ IF prev +] ; \
	mv stage[+prev+]-[+module+] prev-[+module+] || test -f stage[+prev+]-lean [+ ENDIF prev +]
@endif [+ module +][+ ENDIF bootstrap +][+ ENDFOR host_modules +]
	@[ -d stage[+id+]-$(TARGET_SUBDIR) ] || \
	  mkdir stage[+id+]-$(TARGET_SUBDIR); \
	mv stage[+id+]-$(TARGET_SUBDIR) $(TARGET_SUBDIR) [+ IF prev +] ; \
	mv stage[+prev+]-$(TARGET_SUBDIR) prev-$(TARGET_SUBDIR) || test -f stage[+prev+]-lean [+ ENDIF prev +]

stage[+id+]-end:: [+ FOR host_modules +][+ IF bootstrap +]
@if [+ module +]
	@if test -d $(HOST_SUBDIR)/[+module+] ; then \
	  cd $(HOST_SUBDIR); mv [+module+] stage[+id+]-[+module+] [+ IF prev +]; \
	  mv prev-[+module+] stage[+prev+]-[+module+] ; : [+ ENDIF prev +] ; \
	fi
@endif [+ module +][+ ENDIF bootstrap +][+ ENDFOR host_modules +]
	@if test -d $(TARGET_SUBDIR) ; then \
	  mv $(TARGET_SUBDIR) stage[+id+]-$(TARGET_SUBDIR) [+ IF prev +] ; \
	  mv prev-$(TARGET_SUBDIR) stage[+prev+]-$(TARGET_SUBDIR) ; : [+ ENDIF prev +] ; \
	fi
	rm -f stage_current

# Bubble a bug fix through all the stages up to stage [+id+].  They are
# remade, but not reconfigured.  The next stage (if any) will not be
# reconfigured as well.
.PHONY: stage[+id+]-bubble
stage[+id+]-bubble:: [+ IF prev +]stage[+prev+]-bubble[+ ENDIF +]
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	if test -f stage[+id+]-lean [+
	  IF prev +]|| test -f stage[+prev+]-lean [+ ENDIF prev +] ; then \
	  echo Skipping rebuild of stage[+id+] ; \
	else \
	  $(MAKE) stage[+id+]-start; \[+IF lean +]
	  if $(LEAN); then \
	    rm -rf stage[+lean+]-* ; \
	    $(STAMP) stage[+lean+]-lean ; \
	  fi; \[+ ENDIF lean +]
	  $(MAKE) $(RECURSE_FLAGS_TO_PASS) all-stage[+id+]; \
	fi[+ IF compare-target +]
	$(MAKE) $(RECURSE_FLAGS_TO_PASS) [+compare-target+][+ ENDIF compare-target +]

.PHONY: all-stage[+id+] clean-stage[+id+]
do-clean: clean-stage[+id+]

# FIXME: Will not need to be conditional when toplevel bootstrap is the
# only possibility, but now it conflicts with no-bootstrap rules
@if gcc-bootstrap
[+ IF compare-target +]
[+compare-target+]:
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	if test -f stage[+prev+]-lean; then \
	  echo Cannot compare object files as stage [+prev+] was deleted. ; \
	  exit 0 ; \
	fi; \
	: $(MAKE); $(stage); \
	rm -f .bad_compare ; \
	echo Comparing stages [+prev+] and [+id+] ; \
	cd stage[+id+]-gcc; \
	files=`find . -name "*$(objext)" -print` ; \
	cd .. ; \
	for file in $${files} ; do \
	  f1=$$r/stage[+prev+]-gcc/$$file; f2=$$r/stage[+id+]-gcc/$$file; \
	  @do_compare@ > /dev/null 2>&1; \
	  if test $$? -eq 1; then \
	    case $$file in \
	      ./cc*-checksum$(objext) | ./libgcc/* ) \
	        echo warning: $$file differs ;; \
	      *) \
	        echo $$file differs >> .bad_compare ;; \
	    esac ; \
	  fi ; \
	done ; \
	if [ -f .bad_compare ]; then \
	  echo "Bootstrap comparison failure!"; \
	  cat .bad_compare; \
	  exit 1; \
	else \
	  echo Comparison successful.; \
	fi ; \
	$(STAMP) [+compare-target+][+ IF prev +]
	if $(LEAN); then \
	  rm -rf stage[+prev+]-*; \
	  $(STAMP) stage[+prev+]-lean; \
	fi[+ ENDIF prev +]
[+ ENDIF compare-target +]

[+ IF bootstrap-target +]
.PHONY: [+bootstrap-target+] [+bootstrap-target+]-lean
[+bootstrap-target+]:
	echo stage[+id+] > stage_final
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(MAKE) $(RECURSE_FLAGS_TO_PASS) stage[+id+]-bubble
	@: $(MAKE); $(unstage)
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(MAKE) $(TARGET_FLAGS_TO_PASS) all-host all-target

[+bootstrap-target+]-lean:
	echo stage[+id+] > stage_final
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(MAKE) $(RECURSE_FLAGS_TO_PASS) LEAN=: stage[+id+]-bubble
	@: $(MAKE); $(unstage)
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(MAKE) $(TARGET_FLAGS_TO_PASS) all-host all-target
[+ ENDIF bootstrap-target +]

# Rules to wipe a stage and all the following ones, also used for cleanstrap
[+ IF prev +]distclean-stage[+prev+]:: distclean-stage[+id+] [+ ENDIF prev +]
.PHONY: distclean-stage[+id+]
distclean-stage[+id+]::
	@: $(MAKE); $(stage)
	@test "`cat stage_last`" != stage[+id+] || rm -f stage_last
	rm -rf stage[+id+]-* [+
	  IF compare-target +][+compare-target+] [+ ENDIF compare-target +]

[+ IF cleanstrap-target +]
.PHONY: [+cleanstrap-target+]
[+cleanstrap-target+]: do-distclean local-clean
	echo stage[+id+] > stage_final
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(MAKE) $(RECURSE_FLAGS_TO_PASS) stage[+id+]-bubble
	@: $(MAKE); $(unstage)
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	$(MAKE) $(TARGET_FLAGS_TO_PASS) all-host all-target
[+ ENDIF cleanstrap-target +]
@endif gcc-bootstrap

[+ ENDFOR bootstrap-stage +]

stageprofile-end::
	$(MAKE) distclean-stagefeedback

stagefeedback-start::
	@r=`${PWD_COMMAND}`; export r; \
	s=`cd $(srcdir); ${PWD_COMMAND}`; export s; \
	for i in prev-*; do \
	  j=`echo $$i | sed s/^prev-//` ; \
	  cd $$r/$$i && \
	  { find . -type d | sort | sed 's,.*,$(SHELL) '"$$s"'/mkinstalldirs "../'$$j'/&",' | $(SHELL) ; } && \
	  { find . -name '*.*da' | sed 's,.*,$(LN) -f "&" "../'$$j'/&",' | $(SHELL) ; } ; \
	done

@if gcc-bootstrap
do-distclean: distclean-stage1

# Provide a GCC build when we're building target libraries.  This does
# not work as a dependency, just as the minimum necessary to avoid errors.
stage_last:
	$(MAKE) $(RECURSE_FLAGS_TO_PASS) stage1-bubble

# Same as unstage, but not phony and defaulting to stage1-start.  We place
# it in the dependency so that for example `make -j3 all-gcc' works.
stage_current:
	@if test -f stage_last; then $(unstage); else $(MAKE) stage1-start; fi

.PHONY: restrap
restrap:
	@: $(MAKE); $(stage)
	rm -rf stage1-$(TARGET_SUBDIR) [+ FOR bootstrap-stage +][+ IF prev
	  +]stage[+id+]-* [+ ENDIF prev +][+ ENDFOR bootstrap-stage +]
	$(MAKE) $(RECURSE_FLAGS_TO_PASS) all
@endif gcc-bootstrap

# --------------------------------------
# Dependencies between different modules
# --------------------------------------

# Generic dependencies for target modules on host stuff, especially gcc
@if gcc-bootstrap[+ FOR target_modules +][+ IF bootstrap
  +][+ FOR bootstrap_stage +]
configure-stage[+id+]-target-[+module+]: maybe-all-stage[+id+]-gcc[+
  ENDFOR +][+ ELSE bootstrap +]
configure-target-[+module+]: stage_last[+
  ENDIF bootstrap +][+ ENDFOR target_modules +]
@endif gcc-bootstrap

@if gcc-no-bootstrap[+ FOR target_modules +]
configure-target-[+module+]: maybe-all-gcc[+
  ENDFOR target_modules +]
@endif gcc-no-bootstrap


# There are two types of dependencies here: 'hard' dependencies, where one
# module simply won't build without the other; and 'soft' dependencies, where
# if the depended-on module is missing, the depending module will do without
# or find a substitute somewhere (perhaps installed).  Soft dependencies
# are made here to depend on a 'maybe-' target.  If you're not sure,
# it's safer to use a soft dependency.

[+ ;; These Scheme functions build the bulk of the dependencies.
   ;; dep-target builds a string like "maybe-all-MODULE_KIND-gcc",
   ;; where "maybe-" is only included if HARD is true, and all-gcc
   ;; is taken from VAR-NAME.
   (define dep-target (lambda (module-kind var-name hard)
      (string-append
         (if hard "" "maybe-")
         (dep-subtarget var-name)
         module-kind
         (dep-module var-name)
      )))

   ;; make-dep builds a dependency from the MODULE and ON AutoGen vars.
   (define make-dep (lambda (module-kind on-kind)
      (string-append
         (dep-target module-kind "module" #t) ": "
         (dep-target on-kind "on" (exist? "hard")))))

   ;; dep-subtarget extracts everything up to the first dash in the given
   ;; AutoGen variable, for example it extracts "all-" out of "all-gcc".
   (define dep-subtarget (lambda (var-name)
      (substring (get var-name) 0 (+ 1 (string-index (get var-name) #\-)))))

   ;; dep-module extracts everything up to the first dash in the given
   ;; AutoGen variable, for example it extracts "gcc" out of "all-gcc".
   (define dep-module (lambda (var-name)
      (substring (get var-name) (+ 1 (string-index (get var-name) #\-)))))

   ;; dep-stage builds a string for the prefix of a bootstrap stage.
   (define dep-stage (lambda ()
      (string-append
	 "stage"
	 (get "id")
	 "-")))

   ;; dep-maybe is the same as the AutoGen expression "- hard 'maybe-'"
   ;; but is written in Scheme.
   (define dep-maybe (lambda ()
      (if (exist? "hard") "" "maybe-")))

   ;; dep-kind returns "normal" if the dependency is on an "install" target,
   ;; or if either module is not bootstrapped.  It returns "bootstrap" for
   ;; configure or build dependencies between bootstrapped modules; it returns
   ;; "prebootstrap" for configure or build dependencies of bootstrapped
   ;; modules on a build module (e.g. all-gcc on all-build-bison).  All this
   ;; is only necessary for host modules.
   (define dep-kind (lambda ()
      (if (and (hash-ref boot-modules (dep-module "module"))
	       (=* (dep-module "on") "build-"))
	  "prebootstrap"

	  (if (or (= (dep-subtarget "on") "install-")
		  (not (hash-ref boot-modules (dep-module "module")))
		  (not (hash-ref boot-modules (dep-module "on"))))
              "normal"
	      "bootstrap"))))

   ;; We now build the hash table that is used by dep-kind.
   (define boot-modules (make-hash-table 113))
+]

[+ FOR host_modules +][+
   (if (exist? "bootstrap")
       (hash-create-handle! boot-modules (get "module") #t))
   "" +][+ ENDFOR host_modules +]
[+ FOR target_modules +][+
   (if (exist? "bootstrap")
       (hash-create-handle! boot-modules (string-append "target-" (get "module")) #t))
   "" +][+ ENDFOR target_modules +]

# With all the machinery above in place, it is pretty easy to generate
# dependencies.  Host dependencies are a bit more complex because we have
# to check for bootstrap/prebootstrap dependencies.  To resolve
# prebootstrap dependencies, prebootstrap modules are gathered in
# a hash table.
[+ FOR dependencies +][+ (make-dep "" "") +]
[+ CASE (dep-kind) +]
[+ == "prebootstrap"
     +][+ FOR bootstrap_stage +]
[+ (make-dep (dep-stage) "") +][+
       ENDFOR bootstrap_stage +]
[+ == "bootstrap"
     +][+ FOR bootstrap_stage +]
[+ (make-dep (dep-stage) (dep-stage)) +][+
       ENDFOR bootstrap_stage +]
[+ ESAC +][+
ENDFOR dependencies +]

# Dependencies for target modules on other target modules are
# described by lang_env_dependencies; the defaults apply to anything
# not mentioned there.
[+
   ;; Predicate for whether LANG was specified in lang_env_dependencies.
   (define lang-dep (lambda (lang)
      (hash-ref lang-env-deps (string-append (get "module") "-" lang))))

   ;; Build the hash table we will need.
   (define lang-env-deps (make-hash-table 7))
+][+ FOR lang_env_dependencies +][+
   (if (exist? "cxx")
       (hash-create-handle! lang-env-deps
	  (string-append (get "module") "-" "cxx") #t))

   (if (exist? "no_c")
       (hash-create-handle! lang-env-deps
	  (string-append (get "module") "-" "no_c") #t))

   (if (exist? "no_gcc")
       (hash-create-handle! lang-env-deps
	  (string-append (get "module") "-" "no_gcc") #t))
   "" +][+ ENDFOR lang_env_dependencies +]

@if gcc-bootstrap[+ FOR target_modules +][+ IF (not (lang-dep "no_gcc"))
  +][+ IF bootstrap +][+ FOR bootstrap_stage +]
configure-stage[+id+]-target-[+module+]: maybe-all-stage[+id+]-target-libgcc[+
  ENDFOR +][+ ENDIF bootstrap +][+ ENDIF +][+ ENDFOR target_modules +]
@endif gcc-bootstrap

@if gcc-no-bootstrap[+ FOR target_modules +][+ IF (not (lang-dep "no_gcc")) +]
configure-target-[+module+]: maybe-all-target-libgcc[+
  ENDIF +][+ ENDFOR target_modules +]
@endif gcc-no-bootstrap

[+ FOR target_modules +][+ IF (not (lang-dep "no_c")) +]
configure-target-[+module+]: maybe-all-target-newlib maybe-all-target-libgloss[+
  ENDIF +][+ IF (lang-dep "cxx") +]
configure-target-[+module+]: maybe-all-target-libstdc++-v3[+
  ENDIF +]
[+ ENDFOR target_modules +]

CONFIGURE_GDB_TK = @CONFIGURE_GDB_TK@
GDB_TK = @GDB_TK@
INSTALL_GDB_TK = @INSTALL_GDB_TK@
configure-gdb: $(CONFIGURE_GDB_TK)
all-gdb: $(gdbnlmrequirements) $(GDB_TK)
install-gdb: $(INSTALL_GDB_TK)

# Serialization dependencies.  Host configures don't work well in parallel to
# each other, due to contention over config.cache.  Target configures and 
# build configures are similar.
@serialization_dependencies@

# --------------------------------
# Regenerating top level configury
# --------------------------------

# Rebuilding Makefile.in, using autogen.
AUTOGEN = autogen
$(srcdir)/Makefile.in: @MAINT@ $(srcdir)/Makefile.tpl $(srcdir)/Makefile.def
	cd $(srcdir) && $(AUTOGEN) Makefile.def

# Rebuilding Makefile.
Makefile: $(srcdir)/Makefile.in config.status
	CONFIG_FILES=$@ CONFIG_HEADERS= $(SHELL) ./config.status

config.status: configure
	CONFIG_SHELL="$(SHELL)" $(SHELL) ./config.status --recheck

# Rebuilding configure.
AUTOCONF = autoconf
$(srcdir)/configure: @MAINT@ $(srcdir)/configure.ac $(srcdir)/config/acx.m4
	cd $(srcdir) && $(AUTOCONF)

# ------------------------------
# Special directives to GNU Make
# ------------------------------

# Don't pass command-line variables to submakes.
.NOEXPORT:
MAKEOVERRIDES=

# end of Makefile.in
