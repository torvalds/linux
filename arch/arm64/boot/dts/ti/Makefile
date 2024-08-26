# SPDX-License-Identifier: GPL-2.0-only
#
# Make file to build device tree binaries for boards based on
# Texas Instruments Inc processors
#
# Copyright (C) 2016-2021 Texas Instruments Incorporated - https://www.ti.com/
#
# Entries are grouped as per SoC present on the board. Groups are sorted
# alphabetically.

# Boards with AM62x SoC
dtb-$(CONFIG_ARCH_K3) += k3-am625-beagleplay.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am625-beagleplay-csi2-ov5640.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am625-beagleplay-csi2-tevi-ov5640.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am625-phyboard-lyra-rdk.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am625-sk.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am625-verdin-nonwifi-dahlia.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am625-verdin-nonwifi-dev.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am625-verdin-nonwifi-mallow.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am625-verdin-nonwifi-yavia.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am625-verdin-wifi-dahlia.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am625-verdin-wifi-dev.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am625-verdin-wifi-mallow.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am625-verdin-wifi-yavia.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am625-phyboard-lyra-1-4-ghz-opp.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am62x-phyboard-lyra-gpio-fan.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am62-lp-sk.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am62-lp-sk-nand.dtbo

# Boards with AM62Ax SoC
dtb-$(CONFIG_ARCH_K3) += k3-am62a7-sk.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am62a7-phyboard-lyra-rdk.dtb

# Boards with AM62Px SoC
dtb-$(CONFIG_ARCH_K3) += k3-am62p5-sk.dtb

# Common overlays for SK-AM62* family of boards
dtb-$(CONFIG_ARCH_K3) += k3-am62x-sk-csi2-ov5640.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am62x-sk-csi2-tevi-ov5640.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am62x-sk-csi2-imx219.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am62x-sk-hdmi-audio.dtbo

# Boards with AM64x SoC
k3-am642-hummingboard-t-pcie-dtbs := \
	k3-am642-hummingboard-t.dtb k3-am642-hummingboard-t-pcie.dtbo
k3-am642-hummingboard-t-usb3-dtbs := \
	k3-am642-hummingboard-t.dtb k3-am642-hummingboard-t-usb3.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am642-evm.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am642-evm-icssg1-dualemac.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am642-evm-icssg1-dualemac-mii.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am642-hummingboard-t.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am642-hummingboard-t-pcie.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am642-hummingboard-t-usb3.dtb
k3-am642-evm-nand-dtbs := k3-am642-evm.dtb k3-am642-evm-nand.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am642-evm-nand.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am642-phyboard-electra-rdk.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am642-phyboard-electra-gpio-fan.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am642-phyboard-electra-pcie-usb2.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am642-sk.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am642-tqma64xxl-mbax4xxl.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am64-tqma64xxl-mbax4xxl-sdcard.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am64-tqma64xxl-mbax4xxl-wlan.dtbo

# Common overlays for the phyCORE-AM6* family of boards
dtb-$(CONFIG_ARCH_K3) += k3-am6xx-phycore-disable-eth-phy.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am6xx-phycore-disable-rtc.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am6xx-phycore-disable-spi-nor.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am6xx-phycore-qspi-nor.dtbo

# Boards with AM65x SoC
k3-am654-gp-evm-dtbs := k3-am654-base-board.dtb \
	k3-am654-base-board-rocktech-rk101-panel.dtbo \
	k3-am654-pcie-usb3.dtbo
k3-am654-evm-dtbs := k3-am654-base-board.dtb k3-am654-icssg2.dtbo
k3-am654-idk-dtbs := k3-am654-evm.dtb k3-am654-idk.dtbo k3-am654-pcie-usb2.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am6528-iot2050-basic.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am6528-iot2050-basic-pg2.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am6548-iot2050-advanced.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am6548-iot2050-advanced-m2.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am6548-iot2050-advanced-pg2.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am6548-iot2050-advanced-sm.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am654-base-board.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am654-gp-evm.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am654-evm.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am654-idk.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am654-base-board-rocktech-rk101-panel.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am654-pcie-usb2.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-am654-pcie-usb3.dtbo

# Boards with J7200 SoC
k3-j7200-evm-dtbs := k3-j7200-common-proc-board.dtb k3-j7200-evm-quad-port-eth-exp.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-j7200-evm.dtb

# Boards with J721e SoC
k3-j721e-evm-dtbs := k3-j721e-common-proc-board.dtb k3-j721e-evm-quad-port-eth-exp.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-j721e-beagleboneai64.dtb
dtb-$(CONFIG_ARCH_K3) += k3-j721e-common-proc-board-infotainment.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-j721e-evm.dtb
dtb-$(CONFIG_ARCH_K3) += k3-j721e-evm-gesi-exp-board.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-j721e-evm-pcie0-ep.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-j721e-sk.dtb
dtb-$(CONFIG_ARCH_K3) += k3-j721e-sk-csi2-dual-imx219.dtbo

# Boards with J721s2 SoC
dtb-$(CONFIG_ARCH_K3) += k3-am68-sk-base-board.dtb
dtb-$(CONFIG_ARCH_K3) += k3-j721s2-common-proc-board.dtb
dtb-$(CONFIG_ARCH_K3) += k3-j721s2-evm-gesi-exp-board.dtbo
k3-j721s2-evm-dtbs := k3-j721s2-common-proc-board.dtb k3-j721s2-evm-gesi-exp-board.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-j721s2-evm.dtb
dtb-$(CONFIG_ARCH_K3) += k3-j721s2-evm-pcie1-ep.dtbo

# Boards with J722s SoC
dtb-$(CONFIG_ARCH_K3) += k3-j722s-evm.dtb

# Boards with J784s4 SoC
dtb-$(CONFIG_ARCH_K3) += k3-am69-sk.dtb
dtb-$(CONFIG_ARCH_K3) += k3-j784s4-evm.dtb
dtb-$(CONFIG_ARCH_K3) += k3-j784s4-evm-pcie0-pcie1-ep.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-j784s4-evm-quad-port-eth-exp1.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-j784s4-evm-usxgmii-exp1-exp2.dtbo

# Build time test only, enabled by CONFIG_OF_ALL_DTBS
k3-am625-beagleplay-csi2-ov5640-dtbs := k3-am625-beagleplay.dtb \
	k3-am625-beagleplay-csi2-ov5640.dtbo
k3-am625-beagleplay-csi2-tevi-ov5640-dtbs := k3-am625-beagleplay.dtb \
	k3-am625-beagleplay-csi2-tevi-ov5640.dtbo
k3-am625-phyboard-lyra-1-4-ghz-opp.dtbs := k3-am625-phyboard-lyra-rdk.dtb \
	k3-am625-phyboard-lyra-1-4-ghz-opp.dtbo
k3-am625-phyboard-lyra-disable-eth-phy-dtbs := k3-am625-phyboard-lyra-rdk.dtb \
	k3-am6xx-phycore-disable-eth-phy.dtbo
k3-am625-phyboard-lyra-disable-rtc-dtbs := k3-am625-phyboard-lyra-rdk.dtb \
	k3-am6xx-phycore-disable-rtc.dtbo
k3-am625-phyboard-lyra-disable-spi-nor-dtbs := k3-am625-phyboard-lyra-rdk.dtb \
	k3-am6xx-phycore-disable-spi-nor.dtbo
k3-am625-phyboard-lyra-gpio-fan-dtbs := k3-am625-phyboard-lyra-rdk.dtb \
	k3-am62x-phyboard-lyra-gpio-fan.dtbo
k3-am625-phyboard-lyra-qspi-nor-dtbs := k3-am625-phyboard-lyra-rdk.dtb \
	k3-am6xx-phycore-qspi-nor.dtbo
k3-am625-sk-csi2-imx219-dtbs := k3-am625-sk.dtb \
	k3-am62x-sk-csi2-imx219.dtbo
k3-am625-sk-csi2-ov5640-dtbs := k3-am625-sk.dtb \
	k3-am62x-sk-csi2-ov5640.dtbo
k3-am625-sk-csi2-tevi-ov5640-dtbs := k3-am625-sk.dtb \
	k3-am62x-sk-csi2-tevi-ov5640.dtbo
k3-am625-sk-hdmi-audio-dtbs := k3-am625-sk.dtb k3-am62x-sk-hdmi-audio.dtbo
k3-am62-lp-sk-hdmi-audio-dtbs := k3-am62-lp-sk.dtb k3-am62x-sk-hdmi-audio.dtbo
k3-am62a7-sk-csi2-imx219-dtbs := k3-am62a7-sk.dtb \
	k3-am62x-sk-csi2-imx219.dtbo
k3-am62a7-sk-csi2-ov5640-dtbs := k3-am62a7-sk.dtb \
	k3-am62x-sk-csi2-ov5640.dtbo
k3-am62a7-sk-csi2-tevi-ov5640-dtbs := k3-am62a7-sk.dtb \
	k3-am62x-sk-csi2-tevi-ov5640.dtbo
k3-am62a7-sk-hdmi-audio-dtbs := k3-am62a7-sk.dtb k3-am62x-sk-hdmi-audio.dtbo
k3-am62p5-sk-csi2-imx219-dtbs := k3-am62p5-sk.dtb \
	k3-am62x-sk-csi2-imx219.dtbo
k3-am62p5-sk-csi2-ov5640-dtbs := k3-am62p5-sk.dtb \
	k3-am62x-sk-csi2-ov5640.dtbo
k3-am62p5-sk-csi2-tevi-ov5640-dtbs := k3-am62p5-sk.dtb \
	k3-am62x-sk-csi2-tevi-ov5640.dtbo
k3-am642-evm-icssg1-dualemac-dtbs := \
	k3-am642-evm.dtb k3-am642-evm-icssg1-dualemac.dtbo
k3-am642-evm-icssg1-dualemac-mii-dtbs := \
	k3-am642-evm.dtb k3-am642-evm-icssg1-dualemac-mii.dtbo
k3-am642-phyboard-electra-disable-eth-phy-dtbs := \
	k3-am642-phyboard-electra-rdk.dtb k3-am6xx-phycore-disable-eth-phy.dtbo
k3-am642-phyboard-electra-disable-rtc-dtbs := \
	k3-am642-phyboard-electra-rdk.dtb k3-am6xx-phycore-disable-rtc.dtbo
k3-am642-phyboard-electra-disable-spi-nor-dtbs := \
	k3-am642-phyboard-electra-rdk.dtb k3-am6xx-phycore-disable-spi-nor.dtbo
k3-am642-phyboard-electra-qspi-nor-dtbs := \
	k3-am642-phyboard-electra-rdk.dtb k3-am6xx-phycore-qspi-nor.dtbo
k3-am642-phyboard-electra-gpio-fan-dtbs := \
	k3-am642-phyboard-electra-rdk.dtb k3-am642-phyboard-electra-gpio-fan.dtbo
k3-am642-phyboard-electra-pcie-usb2-dtbs := \
	k3-am642-phyboard-electra-rdk.dtb k3-am642-phyboard-electra-pcie-usb2.dtbo
k3-am642-tqma64xxl-mbax4xxl-sdcard-dtbs := \
	k3-am642-tqma64xxl-mbax4xxl.dtb k3-am64-tqma64xxl-mbax4xxl-sdcard.dtbo
k3-am642-tqma64xxl-mbax4xxl-wlan-dtbs := \
	k3-am642-tqma64xxl-mbax4xxl.dtb k3-am64-tqma64xxl-mbax4xxl-wlan.dtbo
k3-am68-sk-base-board-csi2-dual-imx219-dtbs := k3-am68-sk-base-board.dtb \
	k3-j721e-sk-csi2-dual-imx219.dtbo
k3-am69-sk-csi2-dual-imx219-dtbs := k3-am69-sk.dtb \
	k3-j721e-sk-csi2-dual-imx219.dtbo
k3-j721e-common-proc-board-infotainment-dtbs := k3-j721e-common-proc-board.dtb \
	k3-j721e-common-proc-board-infotainment.dtbo
k3-j721e-evm-pcie0-ep-dtbs := k3-j721e-common-proc-board.dtb \
	k3-j721e-evm-pcie0-ep.dtbo
k3-j721e-sk-csi2-dual-imx219-dtbs := k3-j721e-sk.dtb \
	k3-j721e-sk-csi2-dual-imx219.dtbo
k3-j721s2-evm-pcie1-ep-dtbs := k3-j721s2-common-proc-board.dtb \
	k3-j721s2-evm-pcie1-ep.dtbo
k3-j784s4-evm-pcie0-pcie1-ep-dtbs := k3-j784s4-evm.dtb \
	k3-j784s4-evm-pcie0-pcie1-ep.dtbo
k3-j784s4-evm-quad-port-eth-exp1-dtbs := k3-j784s4-evm.dtb \
	k3-j784s4-evm-quad-port-eth-exp1.dtbo
k3-j784s4-evm-usxgmii-exp1-exp2-dtbs := k3-j784s4-evm.dtb \
	k3-j784s4-evm-usxgmii-exp1-exp2.dtbo
dtb- += k3-am625-beagleplay-csi2-ov5640.dtb \
	k3-am625-beagleplay-csi2-tevi-ov5640.dtb \
	k3-am625-sk-csi2-imx219.dtb \
	k3-am625-sk-csi2-ov5640.dtb \
	k3-am625-sk-csi2-tevi-ov5640.dtb \
	k3-am625-sk-hdmi-audio.dtb \
	k3-am62-lp-sk-hdmi-audio.dtb \
	k3-am62a7-sk-csi2-imx219.dtb \
	k3-am62a7-sk-csi2-ov5640.dtb \
	k3-am62a7-sk-hdmi-audio.dtb \
	k3-am62p5-sk-csi2-imx219.dtb \
	k3-am62p5-sk-csi2-ov5640.dtb \
	k3-am62p5-sk-csi2-tevi-ov5640.dtb \
	k3-am642-evm-icssg1-dualemac.dtb \
	k3-am642-evm-icssg1-dualemac-mii.dtb \
	k3-am642-tqma64xxl-mbax4xxl-sdcard.dtb \
	k3-am642-tqma64xxl-mbax4xxl-wlan.dtb \
	k3-am68-sk-base-board-csi2-dual-imx219.dtb \
	k3-am69-sk-csi2-dual-imx219.dtb \
	k3-j721e-common-proc-board-infotainment.dtb \
	k3-j721e-evm-pcie0-ep.dtb \
	k3-j721e-sk-csi2-dual-imx219.dtb \
	k3-j721s2-evm-pcie1-ep.dtb \
	k3-j784s4-evm-pcie0-pcie1-ep.dtb \
	k3-j784s4-evm-quad-port-eth-exp1.dtb \
	k3-j784s4-evm-usxgmii-exp1-exp2.dtb

# Enable support for device-tree overlays
DTC_FLAGS_k3-am625-beagleplay += -@
DTC_FLAGS_k3-am625-phyboard-lyra-rdk += -@
DTC_FLAGS_k3-am62a7-phyboard-lyra-rdk += -@
DTC_FLAGS_k3-am625-sk += -@
DTC_FLAGS_k3-am62-lp-sk += -@
DTC_FLAGS_k3-am62a7-sk += -@
DTC_FLAGS_k3-am62p5-sk += -@
DTC_FLAGS_k3-am642-evm += -@
DTC_FLAGS_k3-am642-phyboard-electra-rdk += -@
DTC_FLAGS_k3-am642-tqma64xxl-mbax4xxl += -@
DTC_FLAGS_k3-am6548-iot2050-advanced-m2 += -@
DTC_FLAGS_k3-am68-sk-base-board += -@
DTC_FLAGS_k3-am69-sk += -@
DTC_FLAGS_k3-j721e-common-proc-board += -@
DTC_FLAGS_k3-j721e-sk += -@
DTC_FLAGS_k3-j721s2-common-proc-board += -@
DTC_FLAGS_k3-j784s4-evm += -@
