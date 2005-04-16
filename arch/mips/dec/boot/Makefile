#
# Makefile for the DECstation family specific parts of the kernel
#

netboot: all
	$(LD) -N -G 0 -T ld.ecoff ../../boot/zImage \
		dec_boot.o ramdisk.img -o nbImage

obj-y	 := decstation.o

clean:
	rm -f nbImage
