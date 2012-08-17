#rtl8192c packed Makefile
# (gwl)

rk_cfg := wifi_power.o wifi_power_usb.o wifi_power_ops.o

rkcfg-y += $(rk_cfg)

obj-$(CONFIG_RTL8188EUS) += 8188eu.o rkcfg.o

.PHONY: clean

clean:
	find . -name '*.o*' -exec rm -f {} \; 
