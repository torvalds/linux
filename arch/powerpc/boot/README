
To extract the kernel vmlinux, System.map, .config or initrd from the zImage binary:

objcopy -j .kernel:vmlinux -O binary zImage vmlinux.gz
objcopy -j .kernel:System.map -O binary zImage System.map.gz
objcopy -j .kernel:.config -O binary zImage config.gz
objcopy -j .kernel:initrd -O binary zImage.initrd initrd.gz


	Peter

