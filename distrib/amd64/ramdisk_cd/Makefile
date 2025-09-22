#	$OpenBSD: Makefile,v 1.39 2023/12/15 06:03:00 jmatthew Exp $

FS=		miniroot${OSrev}.img
FSSIZE=		11392
EFICDBOOT=	eficdboot
EFICDBOOTSIZE=	350K
CDROM=		cd${OSrev}.iso
MOUNT_POINT=	/mnt
MTREE=		${UTILS}/mtree.conf
RAMDISK=	RAMDISK_CD

MOUNT_ARGS_msdos=-o-s

LISTS=		${.CURDIR}/list
UTILS=		${.CURDIR}/../../miniroot

EFIBOOT=	${DESTDIR}/usr/mdec/BOOTX64.EFI ${DESTDIR}/usr/mdec/BOOTIA32.EFI

all: ${FS} ${CDROM}

${FS}: bsd.gz
	dd if=/dev/zero of=${FS} bs=512 count=${FSSIZE}
	vnconfig -v ${FS} > vnd
	fdisk -yi -l ${FSSIZE} -b 960 -f ${DESTDIR}/usr/mdec/mbr `cat vnd`
	echo '/ *' | disklabel -wAT- `cat vnd`
	newfs -t msdos /dev/r`cat vnd`i
	mount ${MOUNT_ARGS_msdos} /dev/`cat vnd`i ${MOUNT_POINT}
	mkdir -p ${MOUNT_POINT}/efi/boot
	cp ${EFIBOOT} ${MOUNT_POINT}/efi/boot
	umount ${MOUNT_POINT}
	newfs -O 1 -m 0 -o space -i 524288 -c ${FSSIZE} /dev/r`cat vnd`a
	mount /dev/`cat vnd`a ${MOUNT_POINT}
	objcopy -S -R .comment ${DESTDIR}/usr/mdec/boot ${.OBJDIR}/boot
	installboot -v -r ${MOUNT_POINT} `cat vnd` \
	    ${DESTDIR}/usr/mdec/biosboot ${.OBJDIR}/boot
	install -c -m 555 -o root -g wheel bsd.gz ${MOUNT_POINT}/bsd
	df -i ${MOUNT_POINT}
	umount ${MOUNT_POINT}
	vnconfig -u `cat vnd`
	rm -f vnd

${EFICDBOOT}: ${EFIBOOT}
	rm -rf ${.OBJDIR}/eficdboot-dir
	mkdir -p ${.OBJDIR}/eficdboot-dir/efi/boot
	cp ${EFIBOOT} ${.OBJDIR}/eficdboot-dir/efi/boot
	makefs -t msdos -o create_size=${EFICDBOOTSIZE} ${EFICDBOOT} \
	    ${.OBJDIR}/eficdboot-dir

${CDROM}: bsd.rd ${EFICDBOOT}
	rm -rf ${.OBJDIR}/cd-dir
	mkdir -p ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	mkdir -p ${.OBJDIR}/cd-dir/etc
	echo "set image /${OSREV}/${MACHINE}/bsd.rd" > ${.OBJDIR}/cd-dir/etc/boot.conf
	cp ${.OBJDIR}/bsd.rd ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	cp ${DESTDIR}/usr/mdec/cdbr ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	cp ${DESTDIR}/usr/mdec/cdboot ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}/cdboot
	cp ${.OBJDIR}/${EFICDBOOT} ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	mkhybrid -a -R -T -L -l -d -D -N -o ${.OBJDIR}/${CDROM} \
	    -A "OpenBSD ${OSREV} ${MACHINE} bootonly CD" \
	    -P "Copyright (c) `date +%Y` Theo de Raadt, The OpenBSD project" \
	    -p "Theo de Raadt <deraadt@openbsd.org>" \
	    -V "OpenBSD/${MACHINE}   ${OSREV} boot-only CD" \
	    -b ${OSREV}/${MACHINE}/cdbr -c ${OSREV}/${MACHINE}/boot.catalog \
	    -e ${OSREV}/${MACHINE}/${EFICDBOOT} \
	    ${.OBJDIR}/cd-dir

MRDISKTYPE=	rdrootb
MRMAKEFSARGS=	-o disklabel=${MRDISKTYPE},minfree=0,density=4096

bsd.gz: bsd.rd
	objcopy -g -x -R .comment -R .SUNW_ctf \
	    -K rd_root_size -K rd_root_image \
	    bsd.rd bsd.strip
	gzip -9cn bsd.strip > bsd.gz

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
	cp ${CDROM} ${RELEASEDIR}
.endif

clean cleandir:
	rm -f *.core mr.fs instbin instbin.mk instbin*.cache \
	    *.o *.lo *.c bsd bsd.rd bsd.gz bsd.strip floppy*.img \
	    lib*.a lib*.olist instbin.map boot instbin.conf ${FS}
	rm -rf mr.fs.d
	rm -f ${CDROM} ${EFICDBOOT}
	rm -rf cd-dir eficdboot-dir

.include <bsd.obj.mk>
