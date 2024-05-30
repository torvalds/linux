human_arch     = RISC-V
build_arch     = riscv
defconfig      = defconfig
flavours       = generic
build_image    = Image
kernel_file    = arch/$(build_arch)/boot/Image
install_file   = vmlinuz

vdso           = vdso_install
no_dumpfile    = true

do_flavour_image_package = false
do_tools                 = false
do_tools_common          = false
do_source_package        = false
do_flavour_header_package = false
do_common_headers_indep = false
do_extras_package      = false
do_tools_usbip         = false
do_tools_cpupower      = false
do_tools_perf          = false
do_tools_perf_jvmti    = false
do_tools_bpftool       = false
do_dtbs                = false
