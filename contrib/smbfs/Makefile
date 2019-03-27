#	$Id: Makefile,v 1.12 2001/04/16 04:34:26 bp Exp $
#
#	Valid targets:
#	install		installs binaries and man pages
#	deinstall	undo install (except man pages)
#	install-src	copy .h files in /usr/local/include
#	install-lib	copy lib files in /usr/lib
#	deinstall-lib,
#	deinstall-src	undo corresponding install-* target
#	clean		cleanup source tree

.if ${MACHINE_ARCH} != "i386"
.	error "only IA32 machines supported"
.endif

CONFIG_INT?=	config.int
CONFIG_MK?=	config.mk

CFGDEPEND=

.ifmake !configure && !clean
.  if !exists(${CONFIG_INT})
.	error "Run 'make configure' before build"
.  else
.	include "${CONFIG_INT}"
.  endif
.else
.  if exists(${CONFIG_MK})
CFGDEPEND=	${CONFIG_MK}

${CONFIG_MK}:	config.mk.in
	@echo Your ${CONFIG_MK} file is older than the config.mk.in file
	@echo Please check for possible changes
	@false

.	include "${CONFIG_MK}"
.  endif
.endif

BUILDKLD?=yes
PREFIX?=/usr/local
SYSDIR?=/usr/src/sys

configure::
.if ${CFGDEPEND} == ${CONFIG_MK}
	@echo Using ${CFGDEPEND} file...
.endif
	echo PREFIX= ${PREFIX} > ${CONFIG_INT}
	echo SYSDIR=${SYSDIR} >> ${CONFIG_INT}
	echo KMODDIR=${KMODDIR} >> ${CONFIG_INT}
	echo SINGLEKLD=yes >> ${CONFIG_INT}
.if !defined(WITHOUT_CRYPT)
	echo ENCRYPTED_PASSWD=yes >> ${CONFIG_INT}
.endif
.if defined(SMP_SUPPORT) || defined(SMP)
	echo SMP=yes >> ${CONFIG_INT}
.endif

SUBDIR=

.if defined(COMPLETEBUILD) || (${BUILDKLD} == yes && !exists(${SYSDIR}/netsmb/smb.h))
SUBDIR+= kernel/modules
.endif

SUBDIR+= lib

SUBDIR+= mount_smbfs smbutil

install-src install-lib deinstall-lib deinstall-src:
	cd kernel && make ${.TARGET}
	cd lib && make ${.TARGET}


makedev:
	@-( if [ `mount -t devfs | wc -l` = 0 ]; then \
		cd /dev && && rm -f nsmb* && mknod nsmb0 c 144 0; \
	    else \
		echo This operation is not required with devfs; \
	    fi; \
	)

cleandepend deinstall: _SUBDIRUSE

clean: _SUBDIRUSE
	rm -f ${CONFIG_INT}

.include <bsd.subdir.mk>
