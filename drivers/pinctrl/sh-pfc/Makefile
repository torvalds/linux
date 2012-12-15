sh-pfc-objs			= core.o pinctrl.o
ifeq ($(CONFIG_GPIO_SH_PFC),y)
sh-pfc-objs			+= gpio.o
endif
obj-$(CONFIG_PINCTRL_SH_PFC)	+= sh-pfc.o
obj-$(CONFIG_PINCTRL_PFC_R8A7740)	+= pfc-r8a7740.o
obj-$(CONFIG_PINCTRL_PFC_R8A7779)	+= pfc-r8a7779.o
