obj-$(CONFIG_RMI4_CORE) += rmi_core.o
rmi_core-y := rmi_bus.o rmi_driver.o rmi_f01.o

rmi_core-$(CONFIG_RMI4_2D_SENSOR) += rmi_2d_sensor.o

# Function drivers
rmi_core-$(CONFIG_RMI4_F11) += rmi_f11.o
rmi_core-$(CONFIG_RMI4_F12) += rmi_f12.o
rmi_core-$(CONFIG_RMI4_F30) += rmi_f30.o

# Transports
obj-$(CONFIG_RMI4_I2C) += rmi_i2c.o
obj-$(CONFIG_RMI4_SPI) += rmi_spi.o
