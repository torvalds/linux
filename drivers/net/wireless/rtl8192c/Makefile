
#EXTRA_CFLAGS += -I$(src)/rkcfg/include

rk_cfg := \
	wifi_power.o \
	wifi_power_usb.o \
	wifi_power_ops.o 

rkcfg-y += $(rk_cfg)

obj-$(CONFIG_RTL8192CU) += rtl8192c.o rkcfg.o

$(obj)/rtl8192c.o: $(obj)/rtl8192c.uu
	@echo "UUDE    rtl8192c.uu"
	@uudecode $(obj)/rtl8192c.uu -o $(obj)/rtl8192c.o


.PHONY: clean

clean:
	find . -name '*.o*' -exec rm -f {} \; 
