#	$OpenBSD: Makefile,v 1.7 2024/06/09 17:24:19 deraadt Exp $

FS=		install${OSrev}.img
FSSIZE=		1032192
CDROM=		install${OSrev}.iso

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
MSDOSSIZE=	30000
TOTALSIZE!=	expr ${FSSIZE} + ${MSDOSSIZE}

NEWFS_ARGS_msdos=-L boot
MOUNT_ARGS_msdos=-o-l

all: ${FS} ${CDROM}

${FS}: ${BASE} ${XBASE}
	dd if=/dev/zero of=${FS} bs=512 count=${TOTALSIZE}
	vnconfig -v ${FS} > vnd
	fdisk -iy -b "${MSDOSSIZE}@${MSDOSSTART}:c" `cat vnd` >/dev/null
	echo '/ *' | disklabel -wAT- `cat vnd`
	newfs -t msdos ${NEWFS_ARGS_msdos} /dev/r`cat vnd`i
	mount ${MOUNT_ARGS_msdos} /dev/`cat vnd`i ${MOUNT_POINT}
	echo 'menuentry "OpenBSD install" {' > ${MOUNT_POINT}/grub.cfg
	echo '\tlinux /boot' >> ${MOUNT_POINT}/grub.cfg
	echo '\tinitrd /boot' >> ${MOUNT_POINT}/grub.cfg
	echo '}' >> ${MOUNT_POINT}/grub.cfg
	cp ${RELDIR}/bsd.rd ${MOUNT_POINT}/boot
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

${CDROM}: ${BASE} ${XBASE}
	rm -rf ${.OBJDIR}/cd-dir
	mkdir -p ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	echo 'menuentry "OpenBSD install" {' > ${.OBJDIR}/cd-dir/grub.cfg
	echo '\tlinux /boot' >> ${.OBJDIR}/cd-dir/grub.cfg
	echo '\tinitrd /boot' >> ${.OBJDIR}/cd-dir/grub.cfg
	echo '}' >> ${.OBJDIR}/cd-dir/grub.cfg
	cp ${RELDIR}/bsd.rd ${.OBJDIR}/cd-dir/boot

	cp -p ${BASE} ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	cp -p ${XBASE} ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	ln ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}/bsd.rd ${.OBJDIR}/cd-dir/bsd.rd
	ln ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}/bsd.rd ${.OBJDIR}/cd-dir/bsd

	cat ${RELDIR}/SHA256 ${RELXDIR}/SHA256 > \
	    ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}/SHA256
	# XXX no SHA256.sig

	mkhybrid -a -R -T -L -l -d -D -N -o ${.OBJDIR}/${CDROM} \
	    -A "OpenBSD ${OSREV} ${MACHINE} Install CD" \
	    -P "Copyright (c) `date +%Y` Theo de Raadt, The OpenBSD project" \
	    -p "Theo de Raadt <deraadt@openbsd.org>" \
	    -V "OpenBSD/${MACHINE} ${OSREV} Install CD" \
	    ${.OBJDIR}/cd-dir

install:
	cp ${CDROM} ${FS} ${RELDIR}/

clean cleandir:
	rm -f ${CDROM} ${FS}
	rm -rf cd-dir

.include <bsd.obj.mk>
