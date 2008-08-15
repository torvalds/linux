#
# Makefile for multifunction miscellaneous devices
#

obj-$(CONFIG_MFD_SM501)		+= sm501.o
obj-$(CONFIG_MFD_ASIC3)		+= asic3.o

obj-$(CONFIG_HTC_EGPIO)		+= htc-egpio.o
obj-$(CONFIG_HTC_PASIC3)	+= htc-pasic3.o

obj-$(CONFIG_MFD_T7L66XB)	+= t7l66xb.o
obj-$(CONFIG_MFD_TC6387XB)	+= tc6387xb.o
obj-$(CONFIG_MFD_TC6393XB)	+= tc6393xb.o

obj-$(CONFIG_MFD_CORE)		+= mfd-core.o

obj-$(CONFIG_MCP)		+= mcp-core.o
obj-$(CONFIG_MCP_SA11X0)	+= mcp-sa11x0.o
obj-$(CONFIG_MCP_UCB1200)	+= ucb1x00-core.o
obj-$(CONFIG_MCP_UCB1200_TS)	+= ucb1x00-ts.o

ifeq ($(CONFIG_SA1100_ASSABET),y)
obj-$(CONFIG_MCP_UCB1200)	+= ucb1x00-assabet.o
endif
