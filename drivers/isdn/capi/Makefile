# Makefile for the CAPI subsystem.

# Ordering constraints: kernelcapi.o first

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_CAPI)			+= kernelcapi.o
obj-$(CONFIG_ISDN_CAPI_CAPI20)		+= capi.o 
obj-$(CONFIG_ISDN_CAPI_CAPIDRV)		+= capidrv.o

# Multipart objects.

kernelcapi-y				:= kcapi.o capiutil.o capilib.o
kernelcapi-$(CONFIG_PROC_FS)		+= kcapi_proc.o
