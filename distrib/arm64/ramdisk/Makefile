#	$OpenBSD: Makefile,v 1.34 2025/09/03 01:39:28 jsg Exp $
#
# Install media requires the following packages:
# - raspberrypi-firmware
# - u-boot-rpi
#

FS=		miniroot${OSrev}.img
FSSIZE=		88064
CDROM=		cd${OSrev}.iso
MOUNT_POINT=	/mnt
MTREE=		${UTILS}/mtree.conf
RAMDISK=	RAMDISK

# this offset is required by Rockchip
MSDOSSTART=	32768
MSDOSSIZE=	16384

NEWFS_ARGS_msdos=-L boot -c1 -F16
MOUNT_ARGS_msdos=-o-l

PDTB=		/usr/local/share/dtb/arm64
PUBOOT=		/usr/local/share/u-boot
PRPI=		/usr/local/share/raspberrypi-firmware/boot

LISTS=		${.CURDIR}/list
UTILS=		${.CURDIR}/../../miniroot

MRDISKTYPE=	rdroot
MRMAKEFSARGS=	-o disklabel=${MRDISKTYPE},minfree=0,density=4096

PIFILES=\
	bootcode.bin \
	start.elf \
	start4.elf \
	fixup.dat \
	fixup4.dat \
	bcm2710-rpi-2-b.dtb \
	bcm2710-rpi-3-b.dtb \
	bcm2710-rpi-3-b-plus.dtb \
	bcm2710-rpi-cm3.dtb \
	bcm2710-rpi-zero-2-w.dtb \
	bcm2711-rpi-4-b.dtb \
	bcm2711-rpi-400.dtb \
	bcm2711-rpi-cm4.dtb \
	bcm2712-d-rpi-5-b.dtb \
	bcm2712-rpi-5-b.dtb \
	bcm2712-rpi-500.dtb \
	bcm2712-rpi-cm5-cm4io.dtb \
	bcm2712-rpi-cm5-cm5io.dtb \
	bcm2712-rpi-cm5l-cm4io.dtb \
	bcm2712-rpi-cm5l-cm5io.dtb \
	bcm2712d0-rpi-5-b.dtb

PIDTBO=\
	bcm2712d0.dtbo \
	disable-bt.dtbo

all: ${FS} ${CDROM}

cdbr:	/usr/mdec/BOOTAA64.EFI
	dd if=/dev/zero of=cdbr bs=512 count=5760
	vnconfig -v cdbr > vnd
	fdisk -iy -b "5744@16:c" `cat vnd` >/dev/null
	newfs -t msdos /dev/r`cat vnd`i
	mount ${MOUNT_ARGS_msdos} /dev/`cat vnd`i ${MOUNT_POINT}
	mkdir -p ${MOUNT_POINT}/efi/boot
	cp /usr/mdec/BOOTAA64.EFI ${MOUNT_POINT}/efi/boot/bootaa64.efi
	echo bootaa64.efi > ${MOUNT_POINT}/efi/boot/startup.nsh
	umount ${MOUNT_POINT}
	vnconfig -u `cat vnd`
	rm -f vnd

${FS}: bsd.rd
	dd if=/dev/zero of=${FS} bs=512 count=${FSSIZE}
	vnconfig -v ${FS} > vnd
	fdisk -iy -b "${MSDOSSIZE}@${MSDOSSTART}:c" `cat vnd` >/dev/null
	echo '/ *' | disklabel -wAT- `cat vnd`
	newfs -t msdos ${NEWFS_ARGS_msdos} /dev/r`cat vnd`i
	mount ${MOUNT_ARGS_msdos} /dev/`cat vnd`i ${MOUNT_POINT}
.for FILE in ${PIFILES}
	cp ${PRPI}/${FILE} ${MOUNT_POINT}/
.endfor
	mkdir -p ${MOUNT_POINT}/overlays
.for FILE in ${PIDTBO}
	cp ${PRPI}/overlays/${FILE} ${MOUNT_POINT}/overlays/
.endfor
	cp ${PUBOOT}/rpi_arm64/u-boot.bin ${MOUNT_POINT}/u-boot.bin
	mkdir -p ${MOUNT_POINT}/efi/boot
	cp /usr/mdec/BOOTAA64.EFI ${MOUNT_POINT}/efi/boot/bootaa64.efi
	echo bootaa64.efi > ${MOUNT_POINT}/efi/boot/startup.nsh
	echo 'arm_64bit=1\nenable_uart=1\ndtoverlay=disable-bt\nkernel=u-boot.bin' > ${MOUNT_POINT}/config.txt
	umount ${MOUNT_POINT}
	newfs -O 1 -m 0 -o space -i 524288 -c ${FSSIZE} /dev/r`cat vnd`a
	mount /dev/`cat vnd`a ${MOUNT_POINT}
	install -c -m 555 -o root -g wheel bsd.rd ${MOUNT_POINT}/bsd
	ln ${MOUNT_POINT}/bsd ${MOUNT_POINT}/bsd.rd
	df -i ${MOUNT_POINT}
	umount ${MOUNT_POINT}
	vnconfig -u `cat vnd`
	rm -f vnd

${CDROM}: ${BASE} ${XBASE} cdbr bsd.rd
	rm -rf ${.OBJDIR}/cd-dir
	mkdir -p ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	mkdir -p ${.OBJDIR}/cd-dir/etc
	echo "set image /${OSREV}/${MACHINE}/bsd.rd" > ${.OBJDIR}/cd-dir/etc/boot.conf
	cp ${.OBJDIR}/bsd.rd ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	cp -p ${.OBJDIR}/cdbr ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	mkhybrid -a -R -T -L -l -d -D -N -o ${.OBJDIR}/${CDROM} \
	    -A "OpenBSD ${OSREV} ${MACHINE} Install CD" \
	    -P "Copyright (c) `date +%Y` Theo de Raadt, The OpenBSD project" \
	    -p "Theo de Raadt <deraadt@openbsd.org>" \
	    -V "OpenBSD/${MACHINE}   ${OSREV} Install CD" \
	    -b ${OSREV}/${MACHINE}/cdbr -c ${OSREV}/${MACHINE}/boot.catalog \
	    ${.OBJDIR}/cd-dir

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
	cp bsd.rd ${RELEASEDIR}/bsd.rd
	chmod a+r ${RELEASEDIR}/bsd.rd
	cp ${FS} ${CDROM} ${RELEASEDIR}
.endif

clean cleandir:
	rm -f *.core mr.fs instbin instbin.mk instbin.cache \
	    lib*.a lib*.olist instbin.map *.o *.lo *.c bsd.rd
	rm -f instbin.conf
	rm -rf cd-dir mr.fs.d cdbr

.include <bsd.obj.mk>
