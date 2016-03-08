#rkwifi packed Makefile
# (gwl)

obj-$(CONFIG_AP6XXX) += rk_wifi_config.o
obj-$(CONFIG_AP6XXX) += bcmdhd/

.PHONY: clean

clean:
	find . -name '*.o*' -exec rm -f {} \; 
