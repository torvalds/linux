#for GNU/Linux
make ARCH=arm smp2_defconfig
#for Android/Linux
#make ARCH=arm smp2_crane_defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- uImage modules
make ARCH=arm INSTALL_MOD_PATH=output modules_install
