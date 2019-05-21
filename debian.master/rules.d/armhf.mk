human_arch	= ARM (hard float)
build_arch	= arm
header_arch	= arm
defconfig	= defconfig
flavours	= generic generic-lpae
build_image	= zImage
kernel_file	= arch/$(build_arch)/boot/zImage
install_file	= vmlinuz
no_dumpfile	= true

loader		= grub

do_tools_usbip  = true
do_tools_cpupower = true
do_tools_perf	= true
do_tools_perf_jvmti = true

do_dtbs		= true
