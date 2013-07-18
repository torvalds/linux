ifneq ($(CONFIG_OF),)
obj-y					+= clk.o autoidle.o clockdomain.o
clk-common				= dpll.o composite.o divider.o gate.o \
					  fixed-factor.o mux.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(clk-common) clk-44xx.o
obj-$(CONFIG_SOC_OMAP5)			+= $(clk-common) clk-54xx.o
endif
