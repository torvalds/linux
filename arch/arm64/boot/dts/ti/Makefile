# SPDX-License-Identifier: GPL-2.0
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
dtb-$(CONFIG_ARCH_K3) += k3-am625-sk.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am62-lp-sk.dtb

# Boards with AM62Ax SoC
dtb-$(CONFIG_ARCH_K3) += k3-am62a7-sk.dtb

# Boards with AM64x SoC
dtb-$(CONFIG_ARCH_K3) += k3-am642-evm.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am642-phyboard-electra-rdk.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am642-sk.dtb

# Boards with AM65x SoC
dtb-$(CONFIG_ARCH_K3) += k3-am6528-iot2050-basic.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am6528-iot2050-basic-pg2.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am6548-iot2050-advanced.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am6548-iot2050-advanced-m2.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am6548-iot2050-advanced-pg2.dtb
dtb-$(CONFIG_ARCH_K3) += k3-am654-base-board.dtb

# Boards with J7200 SoC
k3-j7200-evm-dtbs := k3-j7200-common-proc-board.dtb k3-j7200-evm-quad-port-eth-exp.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-j7200-evm.dtb

# Boards with J721e SoC
k3-j721e-evm-dtbs := k3-j721e-common-proc-board.dtb k3-j721e-evm-quad-port-eth-exp.dtbo
dtb-$(CONFIG_ARCH_K3) += k3-j721e-beagleboneai64.dtb
dtb-$(CONFIG_ARCH_K3) += k3-j721e-evm.dtb
dtb-$(CONFIG_ARCH_K3) += k3-j721e-sk.dtb

# Boards with J721s2 SoC
dtb-$(CONFIG_ARCH_K3) += k3-am68-sk-base-board.dtb
dtb-$(CONFIG_ARCH_K3) += k3-j721s2-common-proc-board.dtb

# Boards with J784s4 SoC
dtb-$(CONFIG_ARCH_K3) += k3-am69-sk.dtb
dtb-$(CONFIG_ARCH_K3) += k3-j784s4-evm.dtb

# Enable support for device-tree overlays
DTC_FLAGS_k3-am6548-iot2050-advanced-m2 += -@
