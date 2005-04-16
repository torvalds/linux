#
# Makefile for the linux kernel.
#

# Object file lists.

obj-$(CONFIG_TC) += tc.o
obj-$(CONFIG_ZS) += zs.o
obj-$(CONFIG_VT) += lk201.o lk201-map.o lk201-remap.o

$(obj)/lk201-map.o: $(obj)/lk201-map.c

# Uncomment if you're changing the keymap and have an appropriate
# loadkeys version for the map. By default, we'll use the shipped
# versions.
# GENERATE_KEYMAP := 1

ifdef GENERATE_KEYMAP

$(obj)/lk201-map.c: $(obj)/%.c: $(src)/%.map
	loadkeys --mktable $< > $@

endif
