#!/bin/bash
#
# mkrootfs.sh - creates a root file system
#

# TODO: need to add checks here to verify that busybox, uClibc and bzImage
# exist


# command-line settable variables
BUSYBOX_DIR=..
UCLIBC_DIR=../../uClibc
TARGET_DIR=./loop
FSSIZE=4000
CLEANUP=1
MKFS='mkfs.ext2 -F'

# don't-touch variables
BASE_DIR=`pwd`


while getopts 'b:u:s:t:Cm' opt
do
	case $opt in
		b) BUSYBOX_DIR=$OPTARG ;;
		u) UCLIBC_DIR=$OPTARG ;;
		t) TARGET_DIR=$OPTARG ;;
		s) FSSIZE=$OPTARG ;;
		C) CLEANUP=0 ;;
		m) MKFS='mkfs.minix' ;;
		*)
			echo "usage: `basename $0` [-bu]"
			echo "  -b DIR  path to busybox direcory (default ..)"
			echo "  -u DIR  path to uClibc direcory (default ../../uClibc)"
			echo "  -t DIR  path to target direcory (default ./loop)"
			echo "  -s SIZE size of root filesystem in Kbytes (default 4000)"
			echo "  -C      don't perform cleanup (umount target dir, gzip rootfs, etc.)"
			echo "          (this allows you to 'chroot loop/ /bin/sh' to test it)"
			echo "  -m      use minix filesystem (default is ext2)"
			exit 1
			;;
	esac
done




# clean up from any previous work
mount | grep -q loop
[ $? -eq 0 ] && umount $TARGET_DIR
[ -d $TARGET_DIR ] && rm -rf $TARGET_DIR/
[ -f rootfs ] && rm -f rootfs
[ -f rootfs.gz ] && rm -f rootfs.gz


# prepare root file system and mount as loopback
dd if=/dev/zero of=rootfs bs=1k count=$FSSIZE
$MKFS -i 2000 rootfs
mkdir $TARGET_DIR
mount -o loop,exec rootfs $TARGET_DIR # must be root


# install uClibc
mkdir -p $TARGET_DIR/lib
cd $UCLIBC_DIR
make INSTALL_DIR=
cp -a libc.so* $BASE_DIR/$TARGET_DIR/lib
cp -a uClibc*.so $BASE_DIR/$TARGET_DIR/lib
cp -a ld.so-1/d-link/ld-linux-uclibc.so* $BASE_DIR/$TARGET_DIR/lib
cp -a ld.so-1/libdl/libdl.so* $BASE_DIR/$TARGET_DIR/lib
cp -a crypt/libcrypt.so* $BASE_DIR/$TARGET_DIR/lib
cd $BASE_DIR


# install busybox and components
cd $BUSYBOX_DIR
make distclean
make CC=$BASE_DIR/$UCLIBC_DIR/extra/gcc-uClibc/i386-uclibc-gcc
make CONFIG_PREFIX=$BASE_DIR/$TARGET_DIR install
cd $BASE_DIR


# make files in /dev
mkdir $TARGET_DIR/dev
./mkdevs.sh $TARGET_DIR/dev


# make files in /etc
cp -a etc $TARGET_DIR
ln -s /proc/mounts $TARGET_DIR/etc/mtab


# other miscellaneous setup
mkdir $TARGET_DIR/initrd
mkdir $TARGET_DIR/proc


# Done. Maybe do cleanup.
if [ $CLEANUP -eq 1 ]
then
	umount $TARGET_DIR
	rmdir $TARGET_DIR
	gzip -9 rootfs
fi
