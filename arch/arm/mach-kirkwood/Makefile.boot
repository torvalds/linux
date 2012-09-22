   zreladdr-y	+= 0x00008000
params_phys-y	:= 0x00000100
initrd_phys-y	:= 0x00800000

dtb-$(CONFIG_MACH_IOMEGA_IX2_200_DT) += kirkwood-iomega_ix2_200.dtb
dtb-$(CONFIG_MACH_DOCKSTAR_DT) += kirkwood-dockstar.dtb
dtb-$(CONFIG_MACH_KM_KIRKWOOD_DT) += kirkwood-km_kirkwood.dtb
