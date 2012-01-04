# Address where decompressor will be written and eventually executed.
#
# default to SDRAM
zreladdr-y      += $(CONFIG_BCM_ZRELADDR)
params_phys-y   := 0x00000800

