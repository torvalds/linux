# per-board load address for uImage
loadaddr-y	:=
loadaddr-$(CONFIG_MACH_BOCKW) += 0x60008000
loadaddr-$(CONFIG_MACH_BOCKW_REFERENCE) += 0x60008000
loadaddr-$(CONFIG_MACH_MARZEN) += 0x60008000

__ZRELADDR	:= $(sort $(loadaddr-y))
   zreladdr-y   += $(__ZRELADDR)

# Unsupported legacy stuff
#
#params_phys-y (Instead: Pass atags pointer in r2)
#initrd_phys-y (Instead: Use compiled-in initramfs)
