   zreladdr-y	+= 0x00008000
params_phys-y	:= 0x00000100
initrd_phys-y	:= 0x00800000

dtb-$(CONFIG_MACH_DOVE_DT) += dove-dove-db.dtb
dtb-$(CONFIG_MACH_DOVE_DT) += dove-cm-a510.dtb
dtb-$(CONFIG_MACH_DOVE_DT) += dove-cubox.dtb
