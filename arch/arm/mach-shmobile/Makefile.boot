# per-board load address for uImage
loadaddr-y	:=
loadaddr-$(CONFIG_MACH_AG5EVM) += 0x40008000
loadaddr-$(CONFIG_MACH_APE6EVM) += 0x40008000
loadaddr-$(CONFIG_MACH_APE6EVM_REFERENCE) += 0x40008000
loadaddr-$(CONFIG_MACH_ARMADILLO800EVA) += 0x40008000
loadaddr-$(CONFIG_MACH_ARMADILLO800EVA_REFERENCE) += 0x40008000
loadaddr-$(CONFIG_MACH_BOCKW) += 0x60008000
loadaddr-$(CONFIG_MACH_KOTA2) += 0x41008000
loadaddr-$(CONFIG_MACH_KZM9D) += 0x40008000
loadaddr-$(CONFIG_MACH_KZM9D_REFERENCE) += 0x40008000
loadaddr-$(CONFIG_MACH_KZM9G) += 0x41008000
loadaddr-$(CONFIG_MACH_KZM9G_REFERENCE) += 0x41008000
loadaddr-$(CONFIG_MACH_LAGER) += 0x40008000
loadaddr-$(CONFIG_MACH_LAGER_REFERENCE) += 0x40008000
loadaddr-$(CONFIG_MACH_MACKEREL) += 0x40008000
loadaddr-$(CONFIG_MACH_MARZEN) += 0x60008000
loadaddr-$(CONFIG_MACH_MARZEN_REFERENCE) += 0x60008000

__ZRELADDR	:= $(sort $(loadaddr-y))
   zreladdr-y   += $(__ZRELADDR)

# Unsupported legacy stuff
#
#params_phys-y (Instead: Pass atags pointer in r2)
#initrd_phys-y (Instead: Use compiled-in initramfs)
