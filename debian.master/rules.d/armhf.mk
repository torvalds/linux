human_arch	= ARM (hard float)
build_arch	= arm
defconfig	= defconfig
flavours	= generic
build_image	= zImage
kernel_file	= arch/$(build_arch)/boot/zImage
install_file	= vmlinuz
no_dumpfile	= true

do_tools_usbip  = true
do_tools_cpupower = true
do_tools_perf	= true
do_tools_perf_jvmti = true
do_tools_bpftool = true

do_dtbs		= true
