# Makefile for the dss1_divert ISDN module

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_DIVERSION)	+= dss1_divert.o

# Multipart objects.

dss1_divert-y			:= isdn_divert.o divert_procfs.o divert_init.o
