obj-y					+= clk.o autoidle.o clockdomain.o
clk-common				= dpll.o composite.o divider.o gate.o \
					  fixed-factor.o mux.o apll.o \
					  clkt_dpll.o
obj-$(CONFIG_SOC_AM33XX)		+= $(clk-common) clk-33xx.o
obj-$(CONFIG_SOC_TI81XX)		+= $(clk-common) fapll.o clk-816x.o
obj-$(CONFIG_ARCH_OMAP2)		+= $(clk-common) interface.o clk-2xxx.o
obj-$(CONFIG_ARCH_OMAP3)		+= $(clk-common) interface.o \
					   clk-3xxx.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(clk-common) clk-44xx.o
obj-$(CONFIG_SOC_OMAP5)			+= $(clk-common) clk-54xx.o
obj-$(CONFIG_SOC_DRA7XX)		+= $(clk-common) clk-7xx.o \
					   clk-dra7-atl.o
obj-$(CONFIG_SOC_AM43XX)		+= $(clk-common) clk-43xx.o

ifdef CONFIG_ATAGS
obj-$(CONFIG_ARCH_OMAP3)                += clk-3xxx-legacy.o
endif
