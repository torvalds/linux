# The standard locations for stuff on CLPS711x type processors
   zreladdr-y				+= 0xc0028000
params_phys-y				:= 0xc0000100
# Should probably have some agreement on these...
initrd_phys-$(CONFIG_ARCH_P720T)	:= 0xc0400000
initrd_phys-$(CONFIG_ARCH_CDB89712)	:= 0x00700000
