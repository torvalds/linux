# for GNU/Linux General (boot from SD-Card)
make ARCH=arm smp2_defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- uImage modules
make ARCH=arm INSTALL_MOD_PATH=output modules_install

# for GNU/Linux General (boot from NAND)
make ARCH=arm smp2_nand_defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- uImage modules
make ARCH=arm INSTALL_MOD_PATH=output modules_install

# for GNU/Linux Server (boot from NAND)
make ARCH=arm smp2_server_defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- uImage modules
make ARCH=arm INSTALL_MOD_PATH=output modules_install

# for Android/Linux (boot from NAND)
make ARCH=arm smp2_crane_defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- uImage modules
make ARCH=arm INSTALL_MOD_PATH=output modules_install
