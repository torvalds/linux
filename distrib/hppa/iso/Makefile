#	$OpenBSD: Makefile,v 1.20 2024/06/09 17:24:19 deraadt Exp $

CDROM=		install${OSrev}.iso
RELXDIR?=	/home/relx-${MACHINE}
RELDIR?=	/home/rel-${MACHINE}

BASE=		${RELDIR}/base${OSrev}.tgz ${RELDIR}/comp${OSrev}.tgz \
		${RELDIR}/game${OSrev}.tgz ${RELDIR}/man${OSrev}.tgz \
		${RELDIR}/bsd ${RELDIR}/bsd.mp ${RELDIR}/bsd.rd \
		${RELDIR}/INSTALL.${MACHINE} ${RELDIR}/BUILDINFO
XBASE=		${RELXDIR}/xbase${OSrev}.tgz ${RELXDIR}/xfont${OSrev}.tgz \
		${RELXDIR}/xshare${OSrev}.tgz ${RELXDIR}/xserv${OSrev}.tgz

all: ${CDROM}

${CDROM}: ${BASE} ${XBASE}
	rm -rf ${.OBJDIR}/cd-dir
	mkdir -p ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}

	cp -p ${BASE} ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	cp -p ${XBASE} ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}

	cat ${RELDIR}/SHA256 ${RELXDIR}/SHA256 > \
	    ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}/SHA256
	# XXX no SHA256.sig

	ln -f ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}/bsd.rd \
	    ${.OBJDIR}/cd-dir/bsd.rd

	mkhybrid  -R -T -L -d -D -N \
	    -A "OpenBSD ${OSREV} ${MACHINE} Install CD" \
	    -P "Copyright (c) `date +%Y` Theo de Raadt, The OpenBSD project" \
	    -p "Theo de Raadt <deraadt@openbsd.org>" \
	    -V "OpenBSD/${MACHINE}    ${OSREV} Install CD" \
	    -o ${.OBJDIR}/${CDROM} ${.OBJDIR}/cd-dir
	dd if=${DESTDIR}/usr/mdec/cdboot of=${.OBJDIR}/${CDROM} \
	    bs=32k count=1 conv=notrunc
	dd if=/dev/zero bs=64k count=1 >> ${.OBJDIR}/${CDROM}

install:
	cp ${CDROM} ${RELDIR}/

clean cleandir:
	rm -rf cd-dir ${CDROM}

.include <bsd.obj.mk>
