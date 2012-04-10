#rkwifi packed Makefile
# (gwl)

obj-$(CONFIG_RKWIFI) += rkwifi.o rk_wifi_config.o

#$(obj)/rkwifi.o: $(obj)/rkwifi.uu
#	@echo "UUDE    rkwifi.uu"
#	@uudecode $(obj)/rkwifi.uu -o $(obj)/rkwifi.o

.PHONY: clean

clean:
	find . -name '*.o*' -exec rm -f {} \; 
