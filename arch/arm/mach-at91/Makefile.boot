# Note: the following conditions must always be true:
#   ZRELADDR == virt_to_phys(TEXTADDR)
#   PARAMS_PHYS must be within 4MB of ZRELADDR
#   INITRD_PHYS must be in RAM

   zreladdr-y	+= 0x20008000
params_phys-y	:= 0x20000100
initrd_phys-y	:= 0x20410000
