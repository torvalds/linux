obj-$(CONFIG_AMD_XGBE) += amd-xgbe.o

amd-xgbe-objs := xgbe-main.o xgbe-drv.o xgbe-dev.o \
		 xgbe-desc.o xgbe-ethtool.o xgbe-mdio.o \
		 xgbe-ptp.o \
		 xgbe-phy-v1.o \
		 xgbe-platform.o

amd-xgbe-$(CONFIG_AMD_XGBE_DCB) += xgbe-dcb.o
amd-xgbe-$(CONFIG_DEBUG_FS) += xgbe-debugfs.o
