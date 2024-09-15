human_arch      = System 390x
build_arch      = s390
defconfig       = defconfig
flavours        = generic
build_image	= bzImage
kernel_file	= arch/$(build_arch)/boot/bzImage
install_file	= vmlinuz

vdso		= vdso_install
no_dumpfile	= true

do_extras_package = true
sipl_signed       = true
do_tools_usbip    = true
do_tools_cpupower = true
do_tools_perf     = true
do_tools_perf_jvmti = true
do_tools_perf_python = true
do_tools_bpftool  = true
do_tools_rtla = false
