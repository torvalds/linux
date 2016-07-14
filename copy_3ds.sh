#!/bin/sh

make ARCH=arm CROSS_COMPILE=arm-unknown-linux-gnueabihf- -j3
make ARCH=arm CROSS_COMPILE=arm-unknown-linux-gnueabihf- nintendo3ds_ctr.dtb
cp ./arch/arm/boot/zImage "$SD3DS/linux"
cp ./arch/arm/boot/dts/nintendo3ds_ctr.dtb "$SD3DS/linux"
sync
echo "Copied!"
