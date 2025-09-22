#	$OpenBSD: Makefile.inc,v 1.33 2024/04/09 11:13:51 kettenis Exp $

FS=		miniroot-${BOARD}-${OSrev}.img
FSSIZE=		71424	# ffs+msdos, actually
MOUNT_POINT=	/mnt

MKUBOOT?=	mkuboot

FSDISKTYPE=	miniroot
MSDOSSTART=	32768
MSDOSSIZE=	8192

PDTB=		/usr/local/share/dtb/arm
PUBOOT=		/usr/local/share/u-boot

all: ${FS}

${FS}: bsd.rd
	dd if=/dev/zero of=${FS} bs=512 count=${FSSIZE}
	vnconfig -v -t ${FSDISKTYPE} ${FS} > vnd
	fdisk -iy -b "${MSDOSSIZE}@${MSDOSSTART}:c" `cat vnd` >/dev/null
	echo '/ *' | disklabel -wAT- `cat vnd`
	newfs -O 1 -t msdos -L boot /dev/r`cat vnd`i
	mount -o-l /dev/`cat vnd`i ${MOUNT_POINT}

.for DTB in ${DTBS}
	cp ${PDTB}/${DTBDIR}${DTB} ${MOUNT_POINT}/
.endfor
.if ${PLATFORM} == "OMAP"
	cp ${PUBOOT}/${UBOOT}/{MLO,u-boot.img} ${MOUNT_POINT}/
.endif
.if ${BOARD} == "nitrogen"
	${MKUBOOT} -t script -a arm -o linux \
	    ${.CURDIR}/../boot.cmd ${MOUNT_POINT}/6x_bootscript
.endif
.if ${BOARD} == "cubox" || ${BOARD} == "wandboard"
	dd if=${PUBOOT}/${UBOOT}/SPL of=/dev/r`cat vnd`c bs=1024 seek=1
	dd if=${PUBOOT}/${UBOOT}/u-boot.img of=/dev/r`cat vnd`c \
	    bs=1024 seek=69 conv=sync
.endif

	mkdir -p ${MOUNT_POINT}/efi/boot
	cp /usr/mdec/BOOTARM.EFI ${MOUNT_POINT}/efi/boot/bootarm.efi
	df ${MOUNT_POINT}
	umount ${MOUNT_POINT}

	newfs -O 1 -m 0 -o space -i 524288 -c ${FSSIZE} /dev/r`cat vnd`a
	mount /dev/`cat vnd`a ${MOUNT_POINT}
	install -c -m 555 -o root -g wheel bsd.rd ${MOUNT_POINT}/bsd
	ln ${MOUNT_POINT}/bsd ${MOUNT_POINT}/bsd.rd
	df -i ${MOUNT_POINT}
	umount ${MOUNT_POINT}
	vnconfig -u `cat vnd`
	rm -f vnd

bsd.rd:
	cp ${.OBJDIR}/../../ramdisk/bsd.rd bsd.rd

unconfig:
	-umount -f ${MOUNT_POINT}
	-[ -f vnd ] && vnconfig -u `cat vnd` && rm -f vnd

.ifdef RELEASEDIR
install:
	cp ${FS} ${RELEASEDIR}
.endif

clean cleandir:
	rm -f ${FS}

.include <bsd.obj.mk>
