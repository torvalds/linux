# SPDX-License-Identifier: GPL-2.0
#rkwifi packed Makefile
# (gwl)

obj-$(CONFIG_AP6XXX) += bcmdhd/
obj-$(CONFIG_AP6XXX_WIFI6) += bcmdhd_wifi6/
obj-$(CONFIG_AP6XXX_INDEP_POWER) += bcmdhd_indep_power/

.PHONY: clean

clean:
	find . -name '*.o*' -exec rm -f {} \; 
