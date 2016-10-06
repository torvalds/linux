ifeq ($(CONFIG_ARCH_OMAP2PLUS), y)

obj-y					+= clk.o autoidle.o clockdomain.o
clk-common				= dpll.o composite.o divider.o gate.o \
					  fixed-factor.o mux.o apll.o \
					  clkt_dpll.o clkt_iclk.o clkt_dflt.o
obj-$(CONFIG_SOC_AM33XX)		+= $(clk-common) clk-33xx.o dpll3xxx.o
obj-$(CONFIG_SOC_TI81XX)		+= $(clk-common) fapll.o clk-814x.o clk-816x.o
obj-$(CONFIG_ARCH_OMAP2)		+= $(clk-common) interface.o clk-2xxx.o
obj-$(CONFIG_ARCH_OMAP3)		+= $(clk-common) interface.o \
					   clk-3xxx.o dpll3xxx.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(clk-common) clk-44xx.o \
					   dpll3xxx.o dpll44xx.o
obj-$(CONFIG_SOC_OMAP5)			+= $(clk-common) clk-54xx.o \
					   dpll3xxx.o dpll44xx.o
obj-$(CONFIG_SOC_DRA7XX)		+= $(clk-common) clk-7xx.o \
					   clk-dra7-atl.o dpll3xxx.o dpll44xx.o
obj-$(CONFIG_SOC_AM43XX)		+= $(clk-common) dpll3xxx.o clk-43xx.o

ifdef CONFIG_ATAGS
obj-$(CONFIG_ARCH_OMAP3)                += clk-3xxx-legacy.o
endif

endif	# CONFIG_ARCH_OMAP2PLUS

obj-$(CONFIG_COMMON_CLK_TI_ADPLL)	+= adpll.o
