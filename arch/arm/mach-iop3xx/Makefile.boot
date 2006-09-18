   zreladdr-y	:= 0xa0008000
params_phys-y	:= 0xa0000100
initrd_phys-y	:= 0xa0800000
ifeq ($(CONFIG_ARCH_IOP33X),y)
   zreladdr-y	:= 0x00008000
params_phys-y	:= 0x00000100
initrd_phys-y	:= 0x00800000
endif

