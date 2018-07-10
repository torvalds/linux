#!/bin/sh -ex

mountpoint -q /
[ ! -e hdc.img.dir ]

cleanup()
{
	trap - EXIT
	if mountpoint -q hdc.img.dir; then
		umount -d hdc.img.dir
	fi
	mountpoint -q hdc.img.dir ||
		rm -rf hdc.img.dir
	exit $@
}

trap 'cleanup $?' EXIT
trap 'cleanup 1' HUP PIPE INT QUIT TERM

size=$(du -ks hdc.dir | sed -rn 's/^([0-9]+).*/\1/p')
[ "$size" -gt 0 ]

rm -f hdc.img
dd if=/dev/zero of=hdc.img count=1 bs=1024 seek=$(($size*2))
mkfs.ext3 -q -F -b 1024 -i 4096 hdc.img
tune2fs -c 0 -i 0 hdc.img
mkdir hdc.img.dir
mount -o loop hdc.img hdc.img.dir
cp -a hdc.dir/* hdc.img.dir/
umount -d hdc.img.dir
