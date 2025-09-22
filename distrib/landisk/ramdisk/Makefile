#	$OpenBSD: Makefile,v 1.37 2023/04/28 12:26:43 krw Exp $

FS=		miniroot${OSrev}.img
FSSIZE=		5120
FSDISKTYPE=	miniroot2.5M
MOUNT_POINT=	/mnt
MTREE=		${UTILS}/mtree.conf
RAMDISK=	RAMDISK

LISTS=		${.CURDIR}/list
UTILS=		${.CURDIR}/../../miniroot

MRDISKTYPE=	rdroot2.5M
MRMAKEFSARGS=	-o disklabel=${MRDISKTYPE},minfree=0,density=4096

all: ${FS}

${FS}: bsd.gz
	dd if=/dev/zero of=${FS} bs=512 count=${FSSIZE}
	vnconfig -v -t ${FSDISKTYPE} ${FS} > vnd
	fdisk -i -y `cat vnd`
	fdisk -u -y -f ${DESTDIR}/usr/mdec/mbr `cat vnd`
	echo '/ *' | disklabel -wAT- `cat vnd`
	newfs -O 1 -m 0 -o space -i 524288 -c ${FSSIZE} -b 4096 -f 512 /dev/r`cat vnd`a
	mount /dev/`cat vnd`a ${MOUNT_POINT}
	installboot -vr ${MOUNT_POINT} `cat vnd` \
	    ${DESTDIR}/usr/mdec/xxboot ${DESTDIR}/usr/mdec/boot
	install -c -m 555 -o root -g wheel bsd.gz ${MOUNT_POINT}/bsd
	ln ${MOUNT_POINT}/bsd ${MOUNT_POINT}/bsd.rd
	df -i ${MOUNT_POINT}
	umount ${MOUNT_POINT}
	vnconfig -u `cat vnd`
	rm -f vnd

bsd.gz: bsd.rd
	gzip -9cn bsd.rd > bsd.gz

bsd.rd: mr.fs
	cp bsd bsd.rd
	rdsetroot bsd.rd mr.fs

bsd:
	cd ${.CURDIR}/../../../sys/arch/${MACHINE}/compile/${RAMDISK} && \
	    su ${BUILDUSER} -c '${MAKE} config && ${MAKE} clean && exec ${MAKE} ${MFLAGS}'
	cp -p ${.CURDIR}/../../../sys/arch/${MACHINE}/compile/${RAMDISK}/obj/bsd bsd

mr.fs: instbin bsd
	rm -rf $@.d
	install -d -o root -g wheel $@.d
	mtree -def ${MTREE} -p $@.d -u
	CURDIR=${.CURDIR} OBJDIR=${.OBJDIR} OSrev=${OSrev} \
	    TARGDIR=$@.d UTILS=${UTILS} RELEASEDIR=${RELEASEDIR} \
	    sh ${UTILS}/runlist.sh ${LISTS}
	rm $@.d/instbin
	makefs ${MRMAKEFSARGS} $@ $@.d

instbin.mk instbin.cache instbin.c: instbin.conf
	crunchgen -E -D ${.CURDIR}/../../.. -L ${DESTDIR}/usr/lib \
	    -c instbin.c -e instbin -m instbin.mk instbin.conf

instbin: instbin.mk instbin.cache instbin.c
	${MAKE} ${MFLAGS} -f instbin.mk SRCLIBDIR=${.CURDIR}/../../../lib all

instbin.conf: ${LISTS}
	awk -f ${UTILS}/makeconf.awk  ${LISTS} > instbin.conf

unconfig:
	-umount -f ${MOUNT_POINT}
	-[ -f vnd ] && vnconfig -u `cat vnd` && rm -f vnd

.ifdef RELEASEDIR
install:
	cp bsd.gz ${RELEASEDIR}/bsd.rd
	chmod a+r ${RELEASEDIR}/bsd.rd
	cp ${FS} ${RELEASEDIR}
.endif

clean cleandir:
	rm -f *.core mr.fs instbin instbin.mk instbin.cache \
	    lib*.a lib*.olist instbin.map *.o *.lo *.c bsd bsd.rd
	rm -f instbin.conf
	rm -rf cd-dir mr.fs.d

.include <bsd.obj.mk>
