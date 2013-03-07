#
# Makefile for the touchscreen drivers.
#

# Each configuration option enables a list of files.

# Core
ct36x_ts-y      := tscore.o
obj-$(CONFIG_TOUCHSCREEN_CT36XX)	+= ct36x_ts.o

# platform
# Generic
ct36x_ts-$(CONFIG_TOUCHSCREEN_CT36X_PLATFORM_GENERIC)	+= generic.o
# RockChip
ct36x_ts-$(CONFIG_TOUCHSCREEN_CT36X_PLATFORM_ROCKCHIP)	+= rockchip.o
# AllWinner
ct36x_ts-$(CONFIG_TOUCHSCREEN_CT36X_PLATFORM_ALLWINNER)	+= allwinner.o

# Chip
# ct360
ct36x_ts-$(CONFIG_TOUCHSCREEN_CT36X_CHIP_CT360)	+= ct360.o
# ct362/ct363/365
ct36x_ts-$(CONFIG_TOUCHSCREEN_CT36X_CHIP_CT365)	+= ct365.o

# Misc

