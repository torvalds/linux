__ZRELADDR	:= $(shell /bin/bash -c 'printf "0x%08x" \
		     $$[$(CONFIG_MEMORY_START) + 0x8000]')

   zreladdr-y   += $(__ZRELADDR)

# Unsupported legacy stuff
#
#params_phys-y (Instead: Pass atags pointer in r2)
#initrd_phys-y (Instead: Use compiled-in initramfs)

dtb-$(CONFIG_MACH_KZM9G) += sh73a0-kzm9g.dtb
dtb-$(CONFIG_MACH_KZM9D) += emev2-kzm9d.dtb
dtb-$(CONFIG_MACH_ARMADILLO800EVA) += r8a7740-armadillo800eva.dtb
