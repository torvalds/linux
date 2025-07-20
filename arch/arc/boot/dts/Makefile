# SPDX-License-Identifier: GPL-2.0

dtb-y	:= $(addsuffix .dtb, $(CONFIG_BUILTIN_DTB_NAME))

# for CONFIG_OF_ALL_DTBS test
dtb-	:= $(patsubst $(src)/%.dts,%.dtb, $(wildcard $(src)/*.dts))

# board-specific dtc flags
DTC_FLAGS_hsdk += --pad 20
