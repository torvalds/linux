   zreladdr-y	:= 0xc0008000
ifeq ($(CONFIG_ARCH_SA1100),y)
   zreladdr-$(CONFIG_SA1111)		:= 0xc0208000
endif
params_phys-y	:= 0xc0000100
initrd_phys-y	:= 0xc0800000

