# Common objects
lib-$(CONFIG_SUNXI_CCU)		+= ccu_common.o
lib-$(CONFIG_SUNXI_CCU)		+= ccu_reset.o

# Base clock types
lib-$(CONFIG_SUNXI_CCU)		+= ccu_div.o
lib-$(CONFIG_SUNXI_CCU)		+= ccu_frac.o
lib-$(CONFIG_SUNXI_CCU)		+= ccu_gate.o
lib-$(CONFIG_SUNXI_CCU)		+= ccu_mux.o
lib-$(CONFIG_SUNXI_CCU)		+= ccu_mult.o
lib-$(CONFIG_SUNXI_CCU)		+= ccu_phase.o

# Multi-factor clocks
lib-$(CONFIG_SUNXI_CCU)		+= ccu_nk.o
lib-$(CONFIG_SUNXI_CCU)		+= ccu_nkm.o
lib-$(CONFIG_SUNXI_CCU)		+= ccu_nkmp.o
lib-$(CONFIG_SUNXI_CCU)		+= ccu_nm.o
lib-$(CONFIG_SUNXI_CCU)		+= ccu_mp.o

# SoC support
obj-$(CONFIG_SUN50I_A64_CCU)	+= ccu-sun50i-a64.o
obj-$(CONFIG_SUN5I_CCU)		+= ccu-sun5i.o
obj-$(CONFIG_SUN6I_A31_CCU)	+= ccu-sun6i-a31.o
obj-$(CONFIG_SUN8I_A23_CCU)	+= ccu-sun8i-a23.o
obj-$(CONFIG_SUN8I_A33_CCU)	+= ccu-sun8i-a33.o
obj-$(CONFIG_SUN8I_A83T_CCU)	+= ccu-sun8i-a83t.o
obj-$(CONFIG_SUN8I_H3_CCU)	+= ccu-sun8i-h3.o
obj-$(CONFIG_SUN8I_V3S_CCU)	+= ccu-sun8i-v3s.o
obj-$(CONFIG_SUN8I_DE2_CCU)	+= ccu-sun8i-de2.o
obj-$(CONFIG_SUN8I_R_CCU)	+= ccu-sun8i-r.o
obj-$(CONFIG_SUN9I_A80_CCU)	+= ccu-sun9i-a80.o
obj-$(CONFIG_SUN9I_A80_CCU)	+= ccu-sun9i-a80-de.o
obj-$(CONFIG_SUN9I_A80_CCU)	+= ccu-sun9i-a80-usb.o

# The lib-y file goals is supposed to work only in arch/*/lib or lib/. In our
# case, we want to use that goal, but even though lib.a will be properly
# generated, it will not be linked in, eventually resulting in a linker error
# for missing symbols.
#
# We can work around that by explicitly adding lib.a to the obj-y goal. This is
# an undocumented behaviour, but works well for now.
obj-$(CONFIG_SUNXI_CCU)		+= lib.a
