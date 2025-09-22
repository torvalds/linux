setenv fdtfile imx6q-sabrelite.dtb ;
load ${dtype} ${disk}:1 ${fdtaddr} ${fdtfile} ;
load ${dtype} ${disk}:1 ${loadaddr} efi/boot/bootarm.efi ;
bootefi ${loadaddr} ${fdtaddr} ;
