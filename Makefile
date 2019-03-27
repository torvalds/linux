#
# $FreeBSD$
#
# The user-driven targets are:
#
# universe            - *Really* build *everything* (buildworld and
#                       all kernels on all architectures).
# tinderbox           - Same as universe, but presents a list of failed build
#                       targets and exits with an error if there were any.
# buildworld          - Rebuild *everything*, including glue to help do
#                       upgrades.
# installworld        - Install everything built by "buildworld".
# world               - buildworld + installworld, no kernel.
# buildkernel         - Rebuild the kernel and the kernel-modules.
# installkernel       - Install the kernel and the kernel-modules.
# installkernel.debug
# reinstallkernel     - Reinstall the kernel and the kernel-modules.
# reinstallkernel.debug
# kernel              - buildkernel + installkernel.
# kernel-toolchain    - Builds the subset of world necessary to build a kernel
# kernel-toolchains   - Build kernel-toolchain for all universe targets.
# doxygen             - Build API documentation of the kernel, needs doxygen.
# update              - Convenient way to update your source tree(s).
# checkworld          - Run test suite on installed world.
# check-old           - List obsolete directories/files/libraries.
# check-old-dirs      - List obsolete directories.
# check-old-files     - List obsolete files.
# check-old-libs      - List obsolete libraries.
# delete-old          - Delete obsolete directories/files.
# delete-old-dirs     - Delete obsolete directories.
# delete-old-files    - Delete obsolete files.
# delete-old-libs     - Delete obsolete libraries.
# targets             - Print a list of supported TARGET/TARGET_ARCH pairs
#                       for world and kernel targets.
# toolchains          - Build a toolchain for all world and kernel targets.
# sysent              - (Re)build syscall entries from syscalls.master.
# xdev                - xdev-build + xdev-install for the architecture
#                       specified with TARGET and TARGET_ARCH.
# xdev-build          - Build cross-development tools.
# xdev-install        - Install cross-development tools.
# xdev-links          - Create traditional links in /usr/bin for cc, etc
# native-xtools       - Create host binaries that produce target objects
#                       for use in qemu user-mode jails.  TARGET and
#                       TARGET_ARCH should be defined.
# native-xtools-install
#                     - Install the files to the given DESTDIR/NXTP where
#                       NXTP defaults to /nxb-bin.
# 
# "quick" way to test all kernel builds:
# 	_jflag=`sysctl -n hw.ncpu`
# 	_jflag=$(($_jflag * 2))
# 	[ $_jflag -gt 12 ] && _jflag=12
# 	make universe -DMAKE_JUST_KERNELS JFLAG=-j${_jflag}
#
# This makefile is simple by design. The FreeBSD make automatically reads
# the /usr/share/mk/sys.mk unless the -m argument is specified on the
# command line. By keeping this makefile simple, it doesn't matter too
# much how different the installed mk files are from those in the source
# tree. This makefile executes a child make process, forcing it to use
# the mk files from the source tree which are supposed to DTRT.
#
# Most of the user-driven targets (as listed above) are implemented in
# Makefile.inc1.  The exceptions are universe, tinderbox and targets.
#
# If you want to build your system from source, be sure that /usr/obj has
# at least 6 GB of disk space available.  A complete 'universe' build of
# r340283 (2018-11) required 167 GB of space.  ZFS lz4 compression
# achieved a 2.18x ratio, reducing actual space to 81 GB.
#
# For individuals wanting to build from the sources currently on their
# system, the simple instructions are:
#
# 1.  `cd /usr/src'  (or to the directory containing your source tree).
# 2.  Define `HISTORICAL_MAKE_WORLD' variable (see README).
# 3.  `make world'
#
# For individuals wanting to upgrade their sources (even if only a
# delta of a few days):
#
#  1.  `cd /usr/src'       (or to the directory containing your source tree).
#  2.  `make buildworld'
#  3.  `make buildkernel KERNCONF=YOUR_KERNEL_HERE'     (default is GENERIC).
#  4.  `make installkernel KERNCONF=YOUR_KERNEL_HERE'   (default is GENERIC).
#       [steps 3. & 4. can be combined by using the "kernel" target]
#  5.  `reboot'        (in single user mode: boot -s from the loader prompt).
#  6.  `mergemaster -p'
#  7.  `make installworld'
#  8.  `mergemaster'		(you may wish to use -i, along with -U or -F).
#  9.  `make delete-old'
# 10.  `reboot'
# 11.  `make delete-old-libs' (in case no 3rd party program uses them anymore)
#
# See src/UPDATING `COMMON ITEMS' for more complete information.
#
# If TARGET=machine (e.g. powerpc, sparc64, ...) is specified you can
# cross build world for other machine types using the buildworld target,
# and once the world is built you can cross build a kernel using the
# buildkernel target.
#
# Define the user-driven targets. These are listed here in alphabetical
# order, but that's not important.
#
# Targets that begin with underscore are internal targets intended for
# developer convenience only.  They are intentionally not documented and
# completely subject to change without notice.
#
# For more information, see the build(7) manual page.
#

# This is included so CC is set to ccache for -V, and COMPILER_TYPE/VERSION
# can be cached for sub-makes. We can't do this while still running on the
# old fmake from FreeBSD 9.x or older, so avoid including it then to avoid
# heartburn upgrading from older systems. The need for CC is done with new
# make later in the build, and caching COMPILER_TYPE/VERSION is only an
# optimization. Also sinclude it to be friendlier to foreign OS hosted builds.
.if ${MAKE_VERSION} >= 20140620 && defined(.PARSEDIR)
.sinclude <bsd.compiler.mk>
.endif

# Note: we use this awkward construct to be compatible with FreeBSD's
# old make used in 10.0 and 9.2 and earlier.
.if defined(MK_DIRDEPS_BUILD) && ${MK_DIRDEPS_BUILD} == "yes" && \
    !make(showconfig) && !make(print-dir)
# targets/Makefile plays the role of top-level
.include "targets/Makefile"
.else

TGTS=	all all-man buildenv buildenvvars buildkernel buildworld \
	check check-old check-old-dirs check-old-files check-old-libs \
	checkdpadd checkworld clean cleandepend cleandir cleanworld \
	cleanuniverse \
	delete-old delete-old-dirs delete-old-files delete-old-libs \
	depend distribute distributekernel distributekernel.debug \
	distributeworld distrib-dirs distribution doxygen \
	everything hier hierarchy install installcheck installkernel \
	installkernel.debug packagekernel packageworld \
	reinstallkernel reinstallkernel.debug \
	installworld kernel-toolchain libraries maninstall \
	obj objlink showconfig tags toolchain update \
	sysent \
	_worldtmp _legacy _bootstrap-tools _cleanobj _obj \
	_build-tools _build-metadata _cross-tools _includes _libraries \
	build32 distribute32 install32 buildsoft distributesoft installsoft \
	builddtb xdev xdev-build xdev-install \
	xdev-links native-xtools native-xtools-install stageworld stagekernel \
	stage-packages \
	create-packages-world create-packages-kernel create-packages \
	packages installconfig real-packages sign-packages package-pkg \
	print-dir test-system-compiler test-system-linker

# These targets require a TARGET and TARGET_ARCH be defined.
XTGTS=	native-xtools native-xtools-install xdev xdev-build xdev-install \
	xdev-links

# XXX: r156740: This can't work since bsd.subdir.mk is not included ever.
# It will only work for SUBDIR_TARGETS in make.conf.
TGTS+=	${SUBDIR_TARGETS}

BITGTS=	files includes
BITGTS:=${BITGTS} ${BITGTS:S/^/build/} ${BITGTS:S/^/install/}
TGTS+=	${BITGTS}

# Only some targets are allowed to use meta mode.  Others get it
# disabled.  In some cases, such as 'install', meta mode can be dangerous
# as a cookie may be used to prevent redundant installations (such as
# for WORLDTMP staging).  For DESTDIR=/ we always want to install though.
# For other cases, such as delete-old-libs, meta mode may break
# the interactive tty prompt.  The safest route is to just whitelist
# the ones that benefit from it.
META_TGT_WHITELIST+= \
	_* build32 buildfiles buildincludes buildkernel buildsoft \
	buildworld everything kernel-toolchain kernel-toolchains kernel \
	kernels libraries native-xtools showconfig test-system-compiler \
	test-system-linker tinderbox toolchain \
	toolchains universe universe-toolchain world worlds xdev xdev-build

.ORDER: buildworld installworld
.ORDER: buildworld distrib-dirs
.ORDER: buildworld distribution
.ORDER: buildworld distribute
.ORDER: buildworld distributeworld
.ORDER: buildworld buildkernel
.ORDER: distrib-dirs distribute
.ORDER: distrib-dirs distributeworld
.ORDER: distrib-dirs installworld
.ORDER: distribution distribute
.ORDER: distributeworld distribute
.ORDER: distributeworld distribution
.ORDER: installworld distribute
.ORDER: installworld distribution
.ORDER: installworld installkernel
.ORDER: buildkernel installkernel
.ORDER: buildkernel installkernel.debug
.ORDER: buildkernel reinstallkernel
.ORDER: buildkernel reinstallkernel.debug

PATH=	/sbin:/bin:/usr/sbin:/usr/bin
MAKEOBJDIRPREFIX?=	/usr/obj
_MAKEOBJDIRPREFIX!= /usr/bin/env -i PATH=${PATH} ${MAKE} MK_AUTO_OBJ=no \
    ${.MAKEFLAGS:MMAKEOBJDIRPREFIX=*} __MAKE_CONF=${__MAKE_CONF} \
    SRCCONF=${SRCCONF} SRC_ENV_CONF= \
    -f /dev/null -V MAKEOBJDIRPREFIX dummy
.if !empty(_MAKEOBJDIRPREFIX)
.error MAKEOBJDIRPREFIX can only be set in environment or src-env.conf(5),\
    not as a global (in make.conf(5) or src.conf(5)) or command-line variable.
.endif

# We often need to use the tree's version of make to build it.
# Choices add to complexity though.
# We cannot blindly use a make which may not be the one we want
# so be explicit - until all choice is removed.
WANT_MAKE=	bmake
.if !empty(.MAKE.MODE:Mmeta)
# 20160604 - support missing-meta,missing-filemon and performance improvements
WANT_MAKE_VERSION= 20160604
.else
# 20160220 - support .dinclude for FAST_DEPEND.
WANT_MAKE_VERSION= 20160220
.endif
MYMAKE=		${OBJROOT}make.${MACHINE}/${WANT_MAKE}
.if defined(.PARSEDIR)
HAVE_MAKE=	bmake
.else
HAVE_MAKE=	fmake
.endif
.if defined(ALWAYS_BOOTSTRAP_MAKE) || \
    ${HAVE_MAKE} != ${WANT_MAKE} || \
    (defined(WANT_MAKE_VERSION) && ${MAKE_VERSION} < ${WANT_MAKE_VERSION})
NEED_MAKE_UPGRADE= t
.endif
.if exists(${MYMAKE})
SUB_MAKE:= ${MYMAKE} -m ${.CURDIR}/share/mk
.elif defined(NEED_MAKE_UPGRADE)
# It may not exist yet but we may cause it to.
# In the case of fmake, upgrade_checks may cause a newer version to be built.
SUB_MAKE= `test -x ${MYMAKE} && echo ${MYMAKE} || echo ${MAKE}` \
	-m ${.CURDIR}/share/mk
.else
SUB_MAKE= ${MAKE} -m ${.CURDIR}/share/mk
.endif

_MAKE=	PATH=${PATH} MAKE_CMD="${MAKE}" ${SUB_MAKE} -f Makefile.inc1 \
	TARGET=${_TARGET} TARGET_ARCH=${_TARGET_ARCH} ${_MAKEARGS}

.if defined(MK_META_MODE) && ${MK_META_MODE} == "yes"
# Only allow meta mode for the whitelisted targets.  See META_TGT_WHITELIST
# above.  If overridden as a make argument then don't bother trying to
# disable it.
.if empty(.MAKEOVERRIDES:MMK_META_MODE)
.for _tgt in ${META_TGT_WHITELIST}
.if make(${_tgt})
_CAN_USE_META_MODE?= yes
.endif
.endfor
.if !defined(_CAN_USE_META_MODE)
_MAKE+=	MK_META_MODE=no
MK_META_MODE= no
.if defined(.PARSEDIR)
.unexport META_MODE
.endif
.endif	# !defined(_CAN_USE_META_MODE)
.endif	# empty(.MAKEOVERRIDES:MMK_META_MODE)

.if ${MK_META_MODE} == "yes"
.if !exists(/dev/filemon) && !defined(NO_FILEMON) && !make(showconfig)
# Require filemon be loaded to provide a working incremental build
.error ${.newline}ERROR: The filemon module (/dev/filemon) is not loaded. \
    ${.newline}ERROR: WITH_META_MODE is enabled but requires filemon for an incremental build. \
    ${.newline}ERROR: 'kldload filemon' or pass -DNO_FILEMON to suppress this error.
.endif	# !exists(/dev/filemon) && !defined(NO_FILEMON)
.endif	# ${MK_META_MODE} == yes
.endif	# defined(MK_META_MODE) && ${MK_META_MODE} == yes

# Guess target architecture from target type, and vice versa, based on
# historic FreeBSD practice of tending to have TARGET == TARGET_ARCH
# expanding to TARGET == TARGET_CPUARCH in recent times, with known
# exceptions.
.if !defined(TARGET_ARCH) && defined(TARGET)
# T->TA mapping is usually TARGET with arm64 the odd man out
_TARGET_ARCH=	${TARGET:S/arm64/aarch64/:S/riscv/riscv64/}
.elif !defined(TARGET) && defined(TARGET_ARCH) && \
    ${TARGET_ARCH} != ${MACHINE_ARCH}
# TA->T mapping is accidentally CPUARCH with aarch64 the odd man out
_TARGET=	${TARGET_ARCH:${__TO_CPUARCH}:C/aarch64/arm64/}
.endif
.if defined(TARGET) && !defined(_TARGET)
_TARGET=${TARGET}
.endif
.if defined(TARGET_ARCH) && !defined(_TARGET_ARCH)
_TARGET_ARCH=${TARGET_ARCH}
.endif
# for historical compatibility for xdev targets
.if defined(XDEV)
_TARGET=	${XDEV}
.endif
.if defined(XDEV_ARCH)
_TARGET_ARCH=	${XDEV_ARCH}
.endif
# Some targets require a set TARGET/TARGET_ARCH, check before the default
# MACHINE and after the compatibility handling.
.if !defined(_TARGET) || !defined(_TARGET_ARCH)
${XTGTS}: _assert_target
.endif
# Otherwise, default to current machine type and architecture.
_TARGET?=	${MACHINE}
_TARGET_ARCH?=	${MACHINE_ARCH}

.if make(native-xtools*)
NXB_TARGET:=		${_TARGET}
NXB_TARGET_ARCH:=	${_TARGET_ARCH}
_TARGET=		${MACHINE}
_TARGET_ARCH=		${MACHINE_ARCH}
_MAKE+=			NXB_TARGET=${NXB_TARGET} \
			NXB_TARGET_ARCH=${NXB_TARGET_ARCH}
.endif

.if make(print-dir)
.SILENT:
.endif

_assert_target: .PHONY .MAKE
.for _tgt in ${XTGTS}
.if make(${_tgt})
	@echo "*** Error: Both TARGET and TARGET_ARCH must be defined for \"${_tgt}\" target"
	@false
.endif
.endfor

#
# Make sure we have an up-to-date make(1). Only world and buildworld
# should do this as those are the initial targets used for upgrades.
# The user can define ALWAYS_CHECK_MAKE to have this check performed
# for all targets.
#
.if defined(ALWAYS_CHECK_MAKE) || !defined(.PARSEDIR)
${TGTS}: upgrade_checks
.else
buildworld: upgrade_checks
.endif

#
# Handle the user-driven targets, using the source relative mk files.
#

tinderbox toolchains kernel-toolchains: .MAKE
${TGTS}: .PHONY .MAKE
	${_+_}@cd ${.CURDIR}; ${_MAKE} ${.TARGET}

# The historic default "all" target creates files which may cause stale
# or (in the cross build case) unlinkable results. Fail with an error
# when no target is given. The users can explicitly specify "all"
# if they want the historic behavior.
.MAIN:	_guard

_guard: .PHONY
	@echo
	@echo "Explicit target required.  Likely \"${SUBDIR_OVERRIDE:Dall:Ubuildworld}\" is wanted.  See build(7)."
	@echo
	@false

STARTTIME!= LC_ALL=C date
CHECK_TIME!= cmp=`mktemp`; find ${.CURDIR}/sys/sys/param.h -newer "$$cmp" && rm "$$cmp"; echo
.if !empty(CHECK_TIME)
.error check your date/time: ${STARTTIME}
.endif

.if defined(HISTORICAL_MAKE_WORLD) || defined(DESTDIR)
#
# world
#
# Attempt to rebuild and reinstall everything. This target is not to be
# used for upgrading an existing FreeBSD system, because the kernel is
# not included. One can argue that this target doesn't build everything
# then.
#
world: upgrade_checks .PHONY
	@echo "--------------------------------------------------------------"
	@echo ">>> make world started on ${STARTTIME}"
	@echo "--------------------------------------------------------------"
.if target(pre-world)
	@echo
	@echo "--------------------------------------------------------------"
	@echo ">>> Making 'pre-world' target"
	@echo "--------------------------------------------------------------"
	${_+_}@cd ${.CURDIR}; ${_MAKE} pre-world
.endif
	${_+_}@cd ${.CURDIR}; ${_MAKE} buildworld
	${_+_}@cd ${.CURDIR}; ${_MAKE} installworld MK_META_MODE=no
.if target(post-world)
	@echo
	@echo "--------------------------------------------------------------"
	@echo ">>> Making 'post-world' target"
	@echo "--------------------------------------------------------------"
	${_+_}@cd ${.CURDIR}; ${_MAKE} post-world
.endif
	@echo
	@echo "--------------------------------------------------------------"
	@echo ">>> make world completed on `LC_ALL=C date`"
	@echo "                   (started ${STARTTIME})"
	@echo "--------------------------------------------------------------"
.else
world: .PHONY
	@echo "WARNING: make world will overwrite your existing FreeBSD"
	@echo "installation without also building and installing a new"
	@echo "kernel.  This can be dangerous.  Please read the handbook,"
	@echo "'Rebuilding world', for how to upgrade your system."
	@echo "Define DESTDIR to where you want to install FreeBSD,"
	@echo "including /, to override this warning and proceed as usual."
	@echo ""
	@echo "Bailing out now..."
	@false
.endif

#
# kernel
#
# Short hand for `make buildkernel installkernel'
#
kernel: buildkernel installkernel .PHONY

#
# Perform a few tests to determine if the installed tools are adequate
# for building the world.
#
upgrade_checks: .PHONY
.if defined(NEED_MAKE_UPGRADE)
	@${_+_}(cd ${.CURDIR} && ${MAKE} ${WANT_MAKE:S,^f,,})
.endif

#
# Upgrade make(1) to the current version using the installed
# headers, libraries and tools.  Also, allow the location of
# the system bsdmake-like utility to be overridden.
#
MMAKEENV=	\
		DESTDIR= \
		INSTALL="sh ${.CURDIR}/tools/install.sh"
MMAKE=		${MMAKEENV} ${MAKE} \
		OBJTOP=${MYMAKE:H}/obj \
		OBJROOT='$${OBJTOP}/' \
		MAKEOBJDIRPREFIX= \
		MAN= -DNO_SHARED \
		-DNO_CPU_CFLAGS -DNO_WERROR \
		-DNO_SUBDIR \
		DESTDIR= PROGNAME=${MYMAKE:T}

bmake: .PHONY
	@echo
	@echo "--------------------------------------------------------------"
	@echo ">>> Building an up-to-date ${.TARGET}(1)"
	@echo "--------------------------------------------------------------"
	${_+_}@cd ${.CURDIR}/usr.bin/${.TARGET}; \
		${MMAKE} obj; \
		${MMAKE} depend; \
		${MMAKE} all; \
		${MMAKE} install DESTDIR=${MYMAKE:H} BINDIR=

regress: .PHONY
	@echo "'make regress' has been renamed 'make check'" | /usr/bin/fmt
	@false

tinderbox toolchains kernel-toolchains kernels worlds: upgrade_checks

tinderbox: .PHONY
	@cd ${.CURDIR}; ${SUB_MAKE} DOING_TINDERBOX=YES universe

toolchains: .PHONY
	@cd ${.CURDIR}; ${SUB_MAKE} UNIVERSE_TARGET=toolchain universe

kernel-toolchains: .PHONY
	@cd ${.CURDIR}; ${SUB_MAKE} UNIVERSE_TARGET=kernel-toolchain universe

kernels: .PHONY
	@cd ${.CURDIR}; ${SUB_MAKE} UNIVERSE_TARGET=buildkernel universe

worlds: .PHONY
	@cd ${.CURDIR}; ${SUB_MAKE} UNIVERSE_TARGET=buildworld universe

#
# universe
#
# Attempt to rebuild *everything* for all supported architectures,
# with a reasonable chance of success, regardless of how old your
# existing system is.
#
.if make(universe) || make(universe_kernels) || make(tinderbox) || \
    make(targets) || make(universe-toolchain)
TARGETS?=amd64 arm arm64 i386 mips powerpc riscv sparc64
_UNIVERSE_TARGETS=	${TARGETS}
TARGET_ARCHES_arm?=	arm armv6 armv7
TARGET_ARCHES_arm64?=	aarch64
TARGET_ARCHES_mips?=	mipsel mips mips64el mips64 mipsn32 mipselhf mipshf mips64elhf mips64hf
TARGET_ARCHES_powerpc?=	powerpc powerpc64 powerpcspe
# riscv64sf excluded due to PR 232085
TARGET_ARCHES_riscv?=	riscv64
.for target in ${TARGETS}
TARGET_ARCHES_${target}?= ${target}
.endfor

MAKE_PARAMS_riscv?=	CROSS_TOOLCHAIN=riscv64-gcc

# XXX Remove architectures only supported by external toolchain from universe
# if required toolchain packages are missing.
TOOLCHAINS_riscv=	riscv64
.for target in riscv
.if ${_UNIVERSE_TARGETS:M${target}}
.for toolchain in ${TOOLCHAINS_${target}}
.if !exists(/usr/local/share/toolchains/${toolchain}-gcc.mk)
_UNIVERSE_TARGETS:= ${_UNIVERSE_TARGETS:N${target}}
universe: universe_${toolchain}_skip .PHONY
universe_epilogue: universe_${toolchain}_skip .PHONY
universe_${toolchain}_skip: universe_prologue .PHONY
	@echo ">> ${target} skipped - install ${toolchain}-xtoolchain-gcc port or package to build"
.endif
.endfor
.endif
.endfor

.if defined(UNIVERSE_TARGET)
MAKE_JUST_WORLDS=	YES
.else
UNIVERSE_TARGET?=	buildworld
.endif
KERNSRCDIR?=		${.CURDIR}/sys

targets:	.PHONY
	@echo "Supported TARGET/TARGET_ARCH pairs for world and kernel targets"
.for target in ${TARGETS}
.for target_arch in ${TARGET_ARCHES_${target}}
	@echo "    ${target}/${target_arch}"
.endfor
.endfor

.if defined(DOING_TINDERBOX)
FAILFILE=${.CURDIR}/_.tinderbox.failed
MAKEFAIL=tee -a ${FAILFILE}
.else
MAKEFAIL=cat
.endif

universe_prologue:  upgrade_checks
universe: universe_prologue
universe_prologue: .PHONY
	@echo "--------------------------------------------------------------"
	@echo ">>> make universe started on ${STARTTIME}"
	@echo "--------------------------------------------------------------"
.if defined(DOING_TINDERBOX)
	@rm -f ${FAILFILE}
.endif

universe-toolchain: .PHONY universe_prologue
	@echo "--------------------------------------------------------------"
	@echo "> Toolchain bootstrap started on `LC_ALL=C date`"
	@echo "--------------------------------------------------------------"
	${_+_}@cd ${.CURDIR}; \
	    env PATH=${PATH} ${SUB_MAKE} ${JFLAG} kernel-toolchain \
	    TARGET=${MACHINE} TARGET_ARCH=${MACHINE_ARCH} \
	    OBJTOP="${HOST_OBJTOP}" \
	    WITHOUT_SYSTEM_COMPILER=yes \
	    WITHOUT_SYSTEM_LINKER=yes \
	    TOOLS_PREFIX_UNDEF= \
	    kernel-toolchain \
	    MK_LLVM_TARGET_ALL=yes \
	    > _.${.TARGET} 2>&1 || \
	    (echo "${.TARGET} failed," \
	    "check _.${.TARGET} for details" | \
	    ${MAKEFAIL}; false)
	@if [ ! -e "${HOST_OBJTOP}/tmp/usr/bin/cc" ]; then \
	    echo "Missing host compiler at ${HOST_OBJTOP}/tmp/usr/bin/cc?" >&2; \
	    false; \
	fi
	@if [ ! -e "${HOST_OBJTOP}/tmp/usr/bin/ld" ]; then \
	    echo "Missing host linker at ${HOST_OBJTOP}/tmp/usr/bin/cc?" >&2; \
	    false; \
	fi
	@echo "--------------------------------------------------------------"
	@echo "> Toolchain bootstrap completed on `LC_ALL=C date`"
	@echo "--------------------------------------------------------------"

.for target in ${_UNIVERSE_TARGETS}
universe: universe_${target}
universe_epilogue: universe_${target}
universe_${target}: universe_${target}_prologue .PHONY
universe_${target}_prologue: universe_prologue .PHONY
	@echo ">> ${target} started on `LC_ALL=C date`"
universe_${target}_worlds: .PHONY

.if !make(targets) && !make(universe-toolchain)
.for target_arch in ${TARGET_ARCHES_${target}}
.if !defined(_need_clang_${target}_${target_arch})
_need_clang_${target}_${target_arch} != \
	env TARGET=${target} TARGET_ARCH=${target_arch} \
	${SUB_MAKE} -C ${.CURDIR} -f Makefile.inc1 test-system-compiler \
	    ${MAKE_PARAMS_${target}} -V MK_CLANG_BOOTSTRAP 2>/dev/null || \
	    echo unknown
.export _need_clang_${target}_${target_arch}
.endif
.if !defined(_need_lld_${target}_${target_arch})
_need_lld_${target}_${target_arch} != \
	env TARGET=${target} TARGET_ARCH=${target_arch} \
	${SUB_MAKE} -C ${.CURDIR} -f Makefile.inc1 test-system-linker \
	    ${MAKE_PARAMS_${target}} -V MK_LLD_BOOTSTRAP 2>/dev/null || \
	    echo unknown
.export _need_lld_${target}_${target_arch}
.endif
# Setup env for each arch to use the one clang.
.if defined(_need_clang_${target}_${target_arch}) && \
    ${_need_clang_${target}_${target_arch}} == "yes"
# No check on existing XCC or CROSS_BINUTILS_PREFIX, etc, is needed since
# we use the test-system-compiler logic to determine if clang needs to be
# built.  It will be no from that logic if already using an external
# toolchain or /usr/bin/cc.
# XXX: Passing HOST_OBJTOP into the PATH would allow skipping legacy,
#      bootstrap-tools, and cross-tools.  Need to ensure each tool actually
#      supports all TARGETS though.
# For now we only pass UNIVERSE_TOOLCHAIN_PATH which will be added at the end
# of STRICTTMPPATH to ensure that the target-specific binaries come first.
MAKE_PARAMS_${target}+= \
	XCC="${HOST_OBJTOP}/tmp/usr/bin/cc" \
	XCXX="${HOST_OBJTOP}/tmp/usr/bin/c++" \
	XCPP="${HOST_OBJTOP}/tmp/usr/bin/cpp" \
	UNIVERSE_TOOLCHAIN_PATH=${HOST_OBJTOP}/tmp/usr/bin
.endif
.if defined(_need_lld_${target}_${target_arch}) && \
    ${_need_lld_${target}_${target_arch}} == "yes"
MAKE_PARAMS_${target}+= \
	XLD="${HOST_OBJTOP}/tmp/usr/bin/ld"
.endif
.endfor
.endif	# !make(targets)

.if !defined(MAKE_JUST_KERNELS)
universe_${target}_done: universe_${target}_worlds .PHONY
.for target_arch in ${TARGET_ARCHES_${target}}
universe_${target}_worlds: universe_${target}_${target_arch} .PHONY
.if (defined(_need_clang_${target}_${target_arch}) && \
    ${_need_clang_${target}_${target_arch}} == "yes") || \
    (defined(_need_lld_${target}_${target_arch}) && \
    ${_need_lld_${target}_${target_arch}} == "yes")
universe_${target}_${target_arch}: universe-toolchain
universe_${target}_prologue: universe-toolchain
.endif
universe_${target}_${target_arch}: universe_${target}_prologue .MAKE .PHONY
	@echo ">> ${target}.${target_arch} ${UNIVERSE_TARGET} started on `LC_ALL=C date`"
	@(cd ${.CURDIR} && env __MAKE_CONF=/dev/null \
	    ${SUB_MAKE} ${JFLAG} ${UNIVERSE_TARGET} \
	    TARGET=${target} \
	    TARGET_ARCH=${target_arch} \
	    ${MAKE_PARAMS_${target}} \
	    > _.${target}.${target_arch}.${UNIVERSE_TARGET} 2>&1 || \
	    (echo "${target}.${target_arch} ${UNIVERSE_TARGET} failed," \
	    "check _.${target}.${target_arch}.${UNIVERSE_TARGET} for details" | \
	    ${MAKEFAIL}))
	@echo ">> ${target}.${target_arch} ${UNIVERSE_TARGET} completed on `LC_ALL=C date`"
.endfor
.endif # !MAKE_JUST_KERNELS

.if !defined(MAKE_JUST_WORLDS)
universe_${target}_done: universe_${target}_kernels .PHONY
universe_${target}_kernels: universe_${target}_worlds .PHONY
universe_${target}_kernels: universe_${target}_prologue .MAKE .PHONY
	@if [ -e "${KERNSRCDIR}/${target}/conf/NOTES" ]; then \
	  (cd ${KERNSRCDIR}/${target}/conf && env __MAKE_CONF=/dev/null \
	    ${SUB_MAKE} LINT \
	    > ${.CURDIR}/_.${target}.makeLINT 2>&1 || \
	    (echo "${target} 'make LINT' failed," \
	    "check _.${target}.makeLINT for details"| ${MAKEFAIL})); \
	fi
	@cd ${.CURDIR}; ${SUB_MAKE} ${.MAKEFLAGS} TARGET=${target} \
	    universe_kernels
.endif # !MAKE_JUST_WORLDS

# Tell the user the worlds and kernels have completed
universe_${target}: universe_${target}_done
universe_${target}_done:
	@echo ">> ${target} completed on `LC_ALL=C date`"
.endfor
.if make(universe_kernconfs) || make(universe_kernels)
.if !defined(TARGET)
TARGET!=	uname -m
.endif
universe_kernels_prologue: .PHONY
	@echo ">> ${TARGET} kernels started on `LC_ALL=C date`"
universe_kernels: universe_kernconfs .PHONY
	@echo ">> ${TARGET} kernels completed on `LC_ALL=C date`"
.if defined(MAKE_ALL_KERNELS)
_THINNER=cat
.elif defined(MAKE_LINT_KERNELS)
_THINNER=grep 'LINT' || true
.else
_THINNER=xargs grep -L "^.NO_UNIVERSE" || true
.endif
KERNCONFS!=	cd ${KERNSRCDIR}/${TARGET}/conf && \
		find [[:upper:][:digit:]]*[[:upper:][:digit:]] \
		-type f -maxdepth 0 \
		! -name DEFAULTS ! -name NOTES | \
		${_THINNER}
universe_kernconfs: universe_kernels_prologue .PHONY
.for kernel in ${KERNCONFS}
TARGET_ARCH_${kernel}!=	cd ${KERNSRCDIR}/${TARGET}/conf && \
	config -m ${KERNSRCDIR}/${TARGET}/conf/${kernel} 2> /dev/null | \
	grep -v WARNING: | cut -f 2
.if empty(TARGET_ARCH_${kernel})
.error "Target architecture for ${TARGET}/conf/${kernel} unknown.  config(8) likely too old."
.endif
universe_kernconfs: universe_kernconf_${TARGET}_${kernel}
universe_kernconf_${TARGET}_${kernel}: .MAKE
	@echo ">> ${TARGET}.${TARGET_ARCH_${kernel}} ${kernel} kernel started on `LC_ALL=C date`"
	@(cd ${.CURDIR} && env __MAKE_CONF=/dev/null \
	    ${SUB_MAKE} ${JFLAG} buildkernel \
	    TARGET=${TARGET} \
	    TARGET_ARCH=${TARGET_ARCH_${kernel}} \
	    ${MAKE_PARAMS_${TARGET}} \
	    KERNCONF=${kernel} \
	    > _.${TARGET}.${kernel} 2>&1 || \
	    (echo "${TARGET} ${kernel} kernel failed," \
	    "check _.${TARGET}.${kernel} for details"| ${MAKEFAIL}))
	@echo ">> ${TARGET}.${TARGET_ARCH_${kernel}} ${kernel} kernel completed on `LC_ALL=C date`"
.endfor
.endif	# make(universe_kernels)
universe: universe_epilogue
universe_epilogue: .PHONY
	@echo "--------------------------------------------------------------"
	@echo ">>> make universe completed on `LC_ALL=C date`"
	@echo "                      (started ${STARTTIME})"
	@echo "--------------------------------------------------------------"
.if defined(DOING_TINDERBOX)
	@if [ -e ${FAILFILE} ] ; then \
		echo "Tinderbox failed:" ;\
		cat ${FAILFILE} ;\
		exit 1 ;\
	fi
.endif
.endif

buildLINT: .PHONY
	${MAKE} -C ${.CURDIR}/sys/${_TARGET}/conf LINT

.if defined(.PARSEDIR)
# This makefile does not run in meta mode
.MAKE.MODE= normal
# Normally the things we run from here don't either.
# Using -DWITH_META_MODE
# we can buildworld with meta files created which are useful 
# for debugging, but without any of the rest of a meta mode build.
MK_DIRDEPS_BUILD= no
MK_STAGING= no
# tell meta.autodep.mk to not even think about updating anything.
UPDATE_DEPENDFILE= NO
.if !make(showconfig)
.export MK_DIRDEPS_BUILD MK_STAGING UPDATE_DEPENDFILE
.endif

.if make(universe)
# we do not want a failure of one branch abort all.
MAKE_JOB_ERROR_TOKEN= no
.export MAKE_JOB_ERROR_TOKEN
.endif
.endif # bmake

.endif				# DIRDEPS_BUILD
