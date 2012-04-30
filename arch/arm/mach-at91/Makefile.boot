# Note: the following conditions must always be true:
#   ZRELADDR == virt_to_phys(TEXTADDR)
#   PARAMS_PHYS must be within 4MB of ZRELADDR
#   INITRD_PHYS must be in RAM

ifeq ($(CONFIG_ARCH_AT91SAM9G45),y)
   zreladdr-y	+= 0x70008000
params_phys-y	:= 0x70000100
initrd_phys-y	:= 0x70410000
else
   zreladdr-y	+= 0x20008000
params_phys-y	:= 0x20000100
initrd_phys-y	:= 0x20410000
endif

# Keep dtb files sorted alphabetically for each SoC
# sam9260
dtb-$(CONFIG_MACH_AT91SAM_DT) += ethernut5.dtb
dtb-$(CONFIG_MACH_AT91SAM_DT) += tny_a9260.dtb
dtb-$(CONFIG_MACH_AT91SAM_DT) += usb_a9260.dtb
# sam9263
dtb-$(CONFIG_MACH_AT91SAM_DT) += at91sam9263ek.dtb
dtb-$(CONFIG_MACH_AT91SAM_DT) += tny_a9263.dtb
dtb-$(CONFIG_MACH_AT91SAM_DT) += usb_a9263.dtb
# sam9g20
dtb-$(CONFIG_MACH_AT91SAM_DT) += at91sam9g20ek.dtb
dtb-$(CONFIG_MACH_AT91SAM_DT) += at91sam9g20ek_2mmc.dtb
dtb-$(CONFIG_MACH_AT91SAM_DT) += kizbox.dtb
dtb-$(CONFIG_MACH_AT91SAM_DT) += tny_a9g20.dtb
dtb-$(CONFIG_MACH_AT91SAM_DT) += usb_a9g20.dtb
# sam9g45
dtb-$(CONFIG_MACH_AT91SAM_DT) += at91sam9m10g45ek.dtb
# sam9x5
dtb-$(CONFIG_MACH_AT91SAM_DT) += at91sam9g25ek.dtb
