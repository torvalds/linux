  zreladdr-y		+= 0x10008000
params_phys-y		:= 0x10000100
initrd_phys-y		:= 0x10800000

dtb-$(CONFIG_ARCH_MSM8X60) += msm8660-surf.dtb
dtb-$(CONFIG_ARCH_MSM8960) += msm8960-cdp.dtb
