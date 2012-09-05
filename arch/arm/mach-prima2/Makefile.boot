zreladdr-y		+= 0x00008000
params_phys-y		:= 0x00000100
initrd_phys-y		:= 0x00800000

dtb-$(CONFIG_ARCH_PRIMA2) += prima2-evb.dtb
