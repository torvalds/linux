# Those numbers are used only by the non-DT V2P-CA9 platform
# The DT-enabled ones require CONFIG_AUTO_ZRELADDR=y
   zreladdr-y	+= 0x60008000
params_phys-y	:= 0x60000100
initrd_phys-y	:= 0x60800000
