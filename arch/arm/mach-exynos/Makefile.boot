   zreladdr-y	+= 0x40008000
params_phys-y	:= 0x40000100

dtb-$(CONFIG_MACH_EXYNOS4_DT) += exynos4210-origen.dtb exynos4210-smdkv310.dtb
dtb-$(CONFIG_MACH_EXYNOS5_DT) += exynos5250-smdk5250.dtb
