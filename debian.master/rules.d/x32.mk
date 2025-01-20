human_arch	= 64 bit x86 (32 bit userspace)
build_arch	= x86
defconfig	= defconfig
flavours	= 
build_image	= bzImage
kernel_file	= arch/$(build_arch)/boot/bzImage
install_file	= vmlinuz
vdso		= vdso_install
no_dumpfile	= true
uefi_signed     = true

do_flavour_image_package = false
