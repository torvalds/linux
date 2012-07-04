zreladdr-y	+= 0x00008000
params_phys-y	:= 0x00000100
initrd_phys-y	:= 0x00800000

dtb-$(CONFIG_MACH_SPEAR300)	+= spear300-evb.dtb
dtb-$(CONFIG_MACH_SPEAR310)	+= spear310-evb.dtb
dtb-$(CONFIG_MACH_SPEAR320)	+= spear320-evb.dtb
