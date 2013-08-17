__ZRELADDR	:= $(shell /bin/bash -c 'printf "0x%08x" \
		     $$[$(CONFIG_EXYNOS_MEM_BASE) + 0x8000]')

__PARAMS_PHYS	:= $(shell /bin/bash -c 'printf "0x%08x" \
	                     $$[$(CONFIG_EXYNOS_MEM_BASE) + 0x100]')

zreladdr-y	+= $(__ZRELADDR)
params_phys-y	:= $(__PARAMS_PHYS)

dtb-$(CONFIG_MACH_EXYNOS5_DT) += exynos5410-smdk5410.dtb
