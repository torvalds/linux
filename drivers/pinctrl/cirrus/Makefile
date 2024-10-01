# SPDX-License-Identifier: GPL-2.0-only
# Cirrus Logic pinctrl drivers
obj-$(CONFIG_PINCTRL_CS42L43)	+= pinctrl-cs42l43.o

obj-$(CONFIG_PINCTRL_LOCHNAGAR)	+= pinctrl-lochnagar.o

pinctrl-madera-objs		:= pinctrl-madera-core.o
ifeq ($(CONFIG_PINCTRL_CS47L15),y)
pinctrl-madera-objs		+= pinctrl-cs47l15.o
endif
ifeq ($(CONFIG_PINCTRL_CS47L35),y)
pinctrl-madera-objs		+= pinctrl-cs47l35.o
endif
ifeq ($(CONFIG_PINCTRL_CS47L85),y)
pinctrl-madera-objs		+= pinctrl-cs47l85.o
endif
ifeq ($(CONFIG_PINCTRL_CS47L90),y)
pinctrl-madera-objs		+= pinctrl-cs47l90.o
endif
ifeq ($(CONFIG_PINCTRL_CS47L92),y)
pinctrl-madera-objs		+= pinctrl-cs47l92.o
endif

obj-$(CONFIG_PINCTRL_MADERA)	+= pinctrl-madera.o
