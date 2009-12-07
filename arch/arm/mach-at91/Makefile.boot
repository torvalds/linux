# Note: the following conditions must always be true:
#   ZRELADDR == virt_to_phys(TEXTADDR)
#   PARAMS_PHYS must be within 4MB of ZRELADDR
#   INITRD_PHYS must be in RAM

ifeq ($(CONFIG_ARCH_AT91CAP9),y)
   zreladdr-y	:= 0x70008000
params_phys-y	:= 0x70000100
initrd_phys-y	:= 0x70410000
else ifeq ($(CONFIG_ARCH_AT91SAM9G45),y)
   zreladdr-y	:= 0x70008000
params_phys-y	:= 0x70000100
initrd_phys-y	:= 0x70410000
else
   zreladdr-y	:= 0x20008000
params_phys-y	:= 0x20000100
initrd_phys-y	:= 0x20410000
endif
