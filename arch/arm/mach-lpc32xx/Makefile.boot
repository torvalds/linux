   zreladdr-y	+= 0x80008000
params_phys-y	:= 0x80000100
initrd_phys-y	:= 0x82000000

dtb-$(CONFIG_ARCH_LPC32XX) += ea3250.dtb phy3250.dtb
