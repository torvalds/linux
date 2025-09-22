#	$OpenBSD: Makefile,v 1.4 2024/06/09 17:24:19 deraadt Exp $

FS=		install${OSrev}.img
FSSIZE=		832192

MOUNT_POINT=	/mnt

RELXDIR?=	/home/relx-${MACHINE}
RELDIR?=	/home/rel-${MACHINE}

BASE=		${RELDIR}/base${OSrev}.tgz ${RELDIR}/comp${OSrev}.tgz \
		${RELDIR}/game${OSrev}.tgz ${RELDIR}/man${OSrev}.tgz \
		${RELDIR}/bsd ${RELDIR}/bsd.mp ${RELDIR}/bsd.rd \
		${RELDIR}/INSTALL.${MACHINE} ${RELDIR}/BUILDINFO
XBASE=		${RELXDIR}/xbase${OSrev}.tgz ${RELXDIR}/xfont${OSrev}.tgz \
		${RELXDIR}/xshare${OSrev}.tgz ${RELXDIR}/xserv${OSrev}.tgz

MSDOSSTART=	64
MSDOSSIZE=	80000
TOTALSIZE!=	expr ${FSSIZE} + ${MSDOSSIZE}

all: ${FS}

${FS}: ${BASE} ${XBASE}
	dd if=/dev/zero of=${FS} bs=512 count=${TOTALSIZE}
	vnconfig -v ${FS} > vnd
	fdisk -iy -b "${MSDOSSIZE}@${MSDOSSTART}:c" `cat vnd` >/dev/null
	echo '/ *' | disklabel -wAT- `cat vnd`
	newfs -t msdos /dev/r`cat vnd`i
	mount /dev/`cat vnd`i ${MOUNT_POINT}
	cp ${RELDIR}/bsd.rd ${MOUNT_POINT}/bsd.rd
	cp ${MOUNT_POINT}/bsd.rd ${MOUNT_POINT}/boot
	cp ${MOUNT_POINT}/bsd.rd ${MOUNT_POINT}/bsd
	umount ${MOUNT_POINT}

	newfs -O 1 -m 0 -o space -i 524288 -c ${FSSIZE} /dev/r`cat vnd`a
	mount /dev/`cat vnd`a ${MOUNT_POINT}
	mkdir -p ${MOUNT_POINT}/${OSREV}/${MACHINE}
	cp -p ${BASE} ${MOUNT_POINT}/${OSREV}/${MACHINE}
	cp -p ${XBASE} ${MOUNT_POINT}/${OSREV}/${MACHINE}
	ln ${MOUNT_POINT}/${OSREV}/${MACHINE}/bsd.rd ${MOUNT_POINT}/bsd.rd
	ln ${MOUNT_POINT}/${OSREV}/${MACHINE}/bsd.rd ${MOUNT_POINT}/bsd

	cat ${RELDIR}/SHA256 ${RELXDIR}/SHA256 > \
	    ${MOUNT_POINT}/${OSREV}/${MACHINE}/SHA256
	# XXX no SHA256.sig
	df -i ${MOUNT_POINT}
	umount ${MOUNT_POINT}
	vnconfig -u `cat vnd`
	rm -f vnd

install:
	cp ${FS} ${RELDIR}/

clean cleandir:
	rm -f ${CDROM} ${FS}
	rm -rf cd-dir

.include <bsd.obj.mk>
