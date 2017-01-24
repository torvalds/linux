# Berlin SoC Family
dtb-$(CONFIG_ARCH_BERLIN) += berlin4ct-dmp.dtb
dtb-$(CONFIG_ARCH_BERLIN) += berlin4ct-stb.dtb

# Mvebu SoC Family
dtb-$(CONFIG_ARCH_MVEBU) += armada-3720-db.dtb
dtb-$(CONFIG_ARCH_MVEBU) += armada-3720-espressobin.dtb
dtb-$(CONFIG_ARCH_MVEBU) += armada-7040-db.dtb
dtb-$(CONFIG_ARCH_MVEBU) += armada-8040-db.dtb

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb
