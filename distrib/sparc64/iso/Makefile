#	$OpenBSD: Makefile,v 1.26 2024/06/09 17:24:19 deraadt Exp $

FS=		install${OSrev}.img
FSSIZE=		1032192
FSDISKTYPE=	install
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

all: ${FS} ${CDROM} 

${FS}: ${BASE} ${XBASE}
	dd if=/dev/zero of=${FS} bs=512 count=${FSSIZE}
	vnconfig -v -t ${FSDISKTYPE} ${FS} > vnd
	echo '/ *' | disklabel -wAT- `cat vnd`
	newfs -O 1 -m 0 -o space -i 524288 -c ${FSSIZE} -b 8192 -f 4096 /dev/r`cat vnd`a
	mount /dev/`cat vnd`a ${MOUNT_POINT}
	install -c -m 555 -o root -g wheel \
	    ${DESTDIR}/usr/mdec/ofwboot ${MOUNT_POINT}/ofwboot
	installboot -v -r ${MOUNT_POINT} `cat vnd` \
	    ${DESTDIR}/usr/mdec/bootblk ${MOUNT_POINT}/ofwboot
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

${CDROM}: ${BASE} ${XBASE}
	rm -rf ${.OBJDIR}/cd-dir/
	mkdir -p ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}

	cp -p ${BASE} ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	cp -p ${XBASE} ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}

	cat ${RELDIR}/SHA256 ${RELXDIR}/SHA256 > \
	    ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}/SHA256
	# XXX no SHA256.sig

	cp -p ${RELDIR}/ofwboot ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}

	echo 'm 1	sun4' > ${.OBJDIR}/cd-dir/.slicemapfile
	echo 'm 2	sun4c' >> ${.OBJDIR}/cd-dir/.slicemapfile
	echo 'm 3	sun4m' >> ${.OBJDIR}/cd-dir/.slicemapfile
	echo 'm 4	sun4d' >> ${.OBJDIR}/cd-dir/.slicemapfile
	echo 'm 5	sun4u' >> ${.OBJDIR}/cd-dir/.slicemapfile
	echo 'm 5	sun4us' >> ${.OBJDIR}/cd-dir/.slicemapfile
	echo 'm 6	sun4v' >> ${.OBJDIR}/cd-dir/.slicemapfile
	mkhybrid -a -R -T -L -d -D -N \
	    -A "OpenBSD ${OSREV} ${MACHINE} Install CD" \
	    -P "Copyright (c) `date +%Y` Theo de Raadt, The OpenBSD project" \
	    -p "Theo de Raadt <deraadt@openbsd.org>" \
	    -V "OpenBSD/${MACHINE} ${OSREV} Install CD" \
	    -o ${.OBJDIR}/${CDROM} ${.OBJDIR}/cd-dir
	vnconfig -v ${.OBJDIR}/${CDROM} > vnd
	disklabel -w `cat vnd` fakecdrom "OpenBSD${OSrev}${MACHINE}"
	vnconfig -u `cat vnd`
	rm -f vnd
	mksuncd f ${.OBJDIR}/${CDROM} ${.OBJDIR}/../miniroot/miniroot${OSrev}.img

unconfig:
	-umount -f ${MOUNT_POINT}
	-[ -f vnd ] && vnconfig -u `cat vnd` && rm -f vnd

install:
	cp ${CDROM} ${FS} ${RELDIR}/

clean cleandir:
	rm -rf ${CDROM} ${FS}
	rm -rf cd-dir

.include <bsd.obj.mk>
