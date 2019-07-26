=============================
Linux 2.6.x on MPC52xx family
=============================

For the latest info, go to http://www.246tNt.com/mpc52xx/

To compile/use :

  - U-Boot::

     # <edit Makefile to set ARCH=ppc & CROSS_COMPILE=... ( also EXTRAVERSION
        if you wish to ).
     # make lite5200_defconfig
     # make uImage

     then, on U-boot:
     => tftpboot 200000 uImage
     => tftpboot 400000 pRamdisk
     => bootm 200000 400000

  - DBug::

     # <edit Makefile to set ARCH=ppc & CROSS_COMPILE=... ( also EXTRAVERSION
        if you wish to ).
     # make lite5200_defconfig
     # cp your_initrd.gz arch/ppc/boot/images/ramdisk.image.gz
     # make zImage.initrd
     # make

     then in DBug:
     DBug> dn -i zImage.initrd.lite5200


Some remarks:

 - The port is named mpc52xxx, and config options are PPC_MPC52xx. The MGT5100
   is not supported, and I'm not sure anyone is interesting in working on it
   so. I didn't took 5xxx because there's apparently a lot of 5xxx that have
   nothing to do with the MPC5200. I also included the 'MPC' for the same
   reason.
 - Of course, I inspired myself from the 2.4 port. If you think I forgot to
   mention you/your company in the copyright of some code, I'll correct it
   ASAP.
