   zreladdr-y	+= 0x00008000
params_phys-y	:= 0x00000100
initrd_phys-y	:= 0x00800000

dtb-$(CONFIG_MACH_DREAMPLUG_DT) += kirkwood-dreamplug.dtb
dtb-$(CONFIG_MACH_DLINK_KIRKWOOD_DT) += kirkwood-dns320.dtb
dtb-$(CONFIG_MACH_DLINK_KIRKWOOD_DT) += kirkwood-dns325.dtb
dtb-$(CONFIG_MACH_ICONNECT_DT) += kirkwood-iconnect.dtb
dtb-$(CONFIG_MACH_IB62X0_DT) += kirkwood-ib62x0.dtb
dtb-$(CONFIG_MACH_TS219_DT)	+= kirkwood-qnap-ts219.dtb
dtb-$(CONFIG_MACH_GOFLEXNET_DT) += kirkwood-goflexnet.dtb
dtb-$(CONFIG_MACH_LSXL_DT) += kirkwood-lschlv2.dtb
dtb-$(CONFIG_MACH_LSXL_DT) += kirkwood-lsxhl.dtb
