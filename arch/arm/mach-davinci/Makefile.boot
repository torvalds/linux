ifeq ($(CONFIG_ARCH_DAVINCI_DA8XX),y)
ifeq ($(CONFIG_ARCH_DAVINCI_DMx),y)
$(error Cannot enable DaVinci and DA8XX platforms concurrently)
else
   zreladdr-y	+= 0xc0008000
params_phys-y	:= 0xc0000100
initrd_phys-y	:= 0xc0800000
endif
else
   zreladdr-y	+= 0x80008000
params_phys-y	:= 0x80000100
initrd_phys-y	:= 0x80800000
endif

dtb-$(CONFIG_MACH_DA8XX_DT)	+= da850-enbw-cmc.dtb da850-evm.dtb
