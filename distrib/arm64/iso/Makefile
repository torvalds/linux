#	$OpenBSD: Makefile,v 1.14 2025/09/03 01:39:28 jsg Exp $

FS=		install${OSrev}.img
FSSIZE=		1290240
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

# this offset is required by Rockchip
MSDOSSTART=	32768
MSDOSSIZE=	16384

NEWFS_ARGS_msdos=-L boot -c1 -F16
MOUNT_ARGS_msdos=-o-l

PDTB=		/usr/local/share/dtb/arm64
PUBOOT=		/usr/local/share/u-boot
PRPI=		/usr/local/share/raspberrypi-firmware/boot

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

cdbr:	${RELDIR}/BOOTAA64.EFI
	dd if=/dev/zero of=cdbr bs=512 count=5760
	vnconfig -v cdbr > vnd
	fdisk -iy -b "5744@16:c" `cat vnd` >/dev/null
	newfs -t msdos /dev/r`cat vnd`i
	mount ${MOUNT_ARGS_msdos} /dev/`cat vnd`i ${MOUNT_POINT}
	mkdir -p ${MOUNT_POINT}/efi/boot
	cp ${RELDIR}/BOOTAA64.EFI ${MOUNT_POINT}/efi/boot/bootaa64.efi
	echo bootaa64.efi > ${MOUNT_POINT}/efi/boot/startup.nsh
	umount ${MOUNT_POINT}
	vnconfig -u `cat vnd`
	rm -f vnd

${FS}: ${BASE} ${XBASE}
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

${CDROM}: ${BASE} ${XBASE} cdbr
	rm -rf ${.OBJDIR}/cd-dir
	mkdir -p ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	mkdir -p ${.OBJDIR}/cd-dir/etc
	echo "set image /${OSREV}/${MACHINE}/bsd.rd" > ${.OBJDIR}/cd-dir/etc/boot.conf

	cp -p ${BASE} ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	cp -p ${XBASE} ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}

	cat ${RELDIR}/SHA256 ${RELXDIR}/SHA256 > \
	    ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}/SHA256
	# XXX no SHA256.sig

	cp -p ${.OBJDIR}/cdbr ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}

	mkhybrid -a -R -T -L -l -d -D -N -o ${.OBJDIR}/${CDROM} \
	    -A "OpenBSD ${OSREV} ${MACHINE} Install CD" \
	    -P "Copyright (c) `date +%Y` Theo de Raadt, The OpenBSD project" \
	    -p "Theo de Raadt <deraadt@openbsd.org>" \
	    -V "OpenBSD/${MACHINE}   ${OSREV} Install CD" \
	    -b ${OSREV}/${MACHINE}/cdbr -c ${OSREV}/${MACHINE}/boot.catalog \
	    ${.OBJDIR}/cd-dir

install:
	cp ${CDROM} ${FS} ${RELDIR}/

clean cleandir:
	rm -f ${CDROM} ${FS} cdbr
	rm -rf cd-dir

.include <bsd.obj.mk>
