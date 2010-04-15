ir-common-objs  := ir-functions.o
ir-core-objs	:= ir-keytable.o ir-sysfs.o ir-raw-event.o rc-map.o

obj-y += keymaps/

obj-$(CONFIG_IR_CORE) += ir-core.o
obj-$(CONFIG_VIDEO_IR) += ir-common.o
obj-$(CONFIG_IR_NEC_DECODER) += ir-nec-decoder.o
obj-$(CONFIG_IR_RC5_DECODER) += ir-rc5-decoder.o
obj-$(CONFIG_IR_RC6_DECODER) += ir-rc6-decoder.o
obj-$(CONFIG_IR_JVC_DECODER) += ir-jvc-decoder.o
obj-$(CONFIG_IR_SONY_DECODER) += ir-sony-decoder.o

# stand-alone IR receivers/transmitters
obj-$(CONFIG_IR_IMON) += imon.o
