#!/bin/bash

function makedev () {

	for dev in 0 1 2 3; do
		echo "/dev/$1$dev: char 81 $[ $2 + $dev ]"
		rm -f /dev/$1$dev
		mknod /dev/$1$dev c 81 $[ $2 + $dev ]
		chmod 666 /dev/$1$dev
	done

	# symlink for default device
	rm -f /dev/$1
	ln -s /dev/${1}0 /dev/$1
}

# see http://linux.bytesex.org/v4l2/API.html

echo "*** new device names ***"
makedev video 0
makedev radio 64
makedev vbi 224

#echo "*** old device names (for compatibility only) ***"
#makedev bttv 0
#makedev bttv-fm 64
#makedev bttv-vbi 224
