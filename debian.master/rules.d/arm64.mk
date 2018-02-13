human_arch	= ARMv8
build_arch	= arm64
header_arch	= arm64
defconfig	= defconfig
flavours	= generic snapdragon
build_image	= Image.gz
kernel_file	= arch/$(build_arch)/boot/Image.gz
install_file	= vmlinuz
no_dumpfile = true

# The uboot used in ubuntu core can't handle Image.gz, so
# create this flavour to generate a Image just for them
build_image_snapdragon	= Image
kernel_file_snapdragon	= arch/$(build_arch)/boot/Image

loader		= grub
vdso		= vdso_install

do_extras_package = true
do_tools_usbip  = true
do_tools_cpupower = true
do_tools_perf   = true

do_dtbs		= true
do_zfs		= true
