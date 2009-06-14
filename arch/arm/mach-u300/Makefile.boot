# Note: the following conditions must always be true:
#   ZRELADDR == virt_to_phys(TEXTADDR)
#   PARAMS_PHYS must be within 4MB of ZRELADDR
#   INITRD_PHYS must be in RAM

ifdef CONFIG_MACH_U300_SINGLE_RAM
     zreladdr-y	:= 0x28E08000
  params_phys-y	:= 0x28E00100
else
     zreladdr-y	:= 0x48008000
  params_phys-y	:= 0x48000100
endif

# This isn't used.
#initrd_phys-y	:= 0x29800000
