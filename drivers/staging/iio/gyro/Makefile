#
# Makefile for digital gyroscope sensor drivers
#

adis16060-y             := adis16060_core.o
obj-$(CONFIG_ADIS16060) += adis16060.o

adis16080-y             := adis16080_core.o
obj-$(CONFIG_ADIS16080) += adis16080.o

adis16130-y             := adis16130_core.o
obj-$(CONFIG_ADIS16130) += adis16130.o

adis16260-y             := adis16260_core.o
adis16260-$(CONFIG_IIO_RING_BUFFER) += adis16260_ring.o adis16260_trigger.o
obj-$(CONFIG_ADIS16260) += adis16260.o

adis16251-y             := adis16251_core.o
obj-$(CONFIG_ADIS16251) += adis16251.o
