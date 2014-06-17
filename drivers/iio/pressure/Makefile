#
# Makefile for industrial I/O pressure drivers
#

# When adding new entries keep the list in alphabetical order
obj-$(CONFIG_HID_SENSOR_PRESS)   += hid-sensor-press.o
obj-$(CONFIG_MPL3115) += mpl3115.o
obj-$(CONFIG_IIO_ST_PRESS) += st_pressure.o
st_pressure-y := st_pressure_core.o
st_pressure-$(CONFIG_IIO_BUFFER) += st_pressure_buffer.o

obj-$(CONFIG_IIO_ST_PRESS_I2C) += st_pressure_i2c.o
obj-$(CONFIG_IIO_ST_PRESS_SPI) += st_pressure_spi.o
