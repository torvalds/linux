RELEASE ?=
KERNEL_DEFCONFIG ?= rockchip_linux_defconfig

KERNEL_VERSION ?= $(shell $(KERNEL_MAKE) -s kernelversion)
KERNEL_RELEASE ?= $(shell $(KERNEL_MAKE) -s kernelrelease)
KDEB_PKGVERSION ?= $(KERNEL_VERSION)-$(RELEASE)-ayufan

KERNEL_MAKE ?= make \
	ARCH=arm64 \
	HOSTCC=aarch64-linux-gnu-gcc \
	CROSS_COMPILE="ccache aarch64-linux-gnu-"

.config: arch/arm64/configs/$(KERNEL_DEFCONFIG)
	$(KERNEL_MAKE) $(KERNEL_DEFCONFIG)

.PHONY: .scmversion
.scmversion:
ifneq (,$(RELEASE))
	@echo "-$(RELEASE)-ayufan-g$$(git rev-parse --short HEAD)" > .scmversion
else
	@echo "-dev" > .scmversion
endif

version:
	@echo "$(KDEB_PKGVERSION)"

.PHONY: info
info: .config .scmversion
	@echo $(KERNEL_RELEASE)

.PHONY: kernel-menuconfig
kernel-menuconfig:
	$(KERNEL_MAKE) $(KERNEL_DEFCONFIG)
	$(KERNEL_MAKE) HOSTCC=gcc menuconfig
	$(KERNEL_MAKE) savedefconfig
	mv defconfig arch/arm64/configs/$(KERNEL_DEFCONFIG)

.PHONY: kernel-image
kernel-image: .config .scmversion
	$(KERNEL_MAKE) Image dtbs -j$$(nproc)

.PHONY: kernel-modules
kernel-image-and-modules: .config .scmversion
	$(KERNEL_MAKE) Image modules dtbs -j$$(nproc)
	$(KERNEL_MAKE) modules_install INSTALL_MOD_PATH=$(CURDIR)/out/linux_modules

.PHONY: kernel-package
kernel-package: .config .scmversion
	KDEB_PKGVERSION=$(KDEB_PKGVERSION) $(KERNEL_MAKE) bindeb-pkg -j$$(nproc)

.PHONY: kernel-update-dts
kernel-update-dts: .config .scmversion
	$(KERNEL_MAKE) dtbs -j$$(nproc)
	rsync --partial --checksum --include="*.dtb" -rv arch/arm64/boot/dts/rockchip root@$(REMOTE_HOST):$(REMOTE_DIR)/boot/dtbs/$(KERNEL_RELEASE)

.PHONY: kernel-update
kernel-update-image: .scmversion
	rsync --partial --checksum -rv arch/arm64/boot/Image root@$(REMOTE_HOST):$(REMOTE_DIR)/boot/vmlinuz-$(KERNEL_RELEASE)
	rsync --partial --checksum --include="*.dtb" -rv arch/arm64/boot/dts/rockchip root@$(REMOTE_HOST):$(REMOTE_DIR)/boot/dtbs/$(KERNEL_RELEASE)
	rsync --partial --checksum -av out/linux_modules/lib/modules/$(KERNEL_RELEASE) root@$(REMOTE_HOST):$(REMOTE_DIR)/lib/modules/$(KERNEL_RELEASE)
