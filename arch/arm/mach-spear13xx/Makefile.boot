zreladdr-y	+= 0x00008000
params_phys-y	:= 0x00000100
initrd_phys-y	:= 0x00800000

dtb-$(CONFIG_MACH_SPEAR1310)	+= spear1310-evb.dtb
dtb-$(CONFIG_MACH_SPEAR1340)	+= spear1340-evb.dtb
