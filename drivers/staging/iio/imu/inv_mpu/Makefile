#
# Makefile for Invensense inv-mpu-iio device.
#

obj-$(CONFIG_INV_MPU_IIO) += inv-mpu-iio.o

inv-mpu-iio-objs := inv_mpu_spi.o
inv-mpu-iio-objs += inv_mpu_i2c.o
inv-mpu-iio-objs += inv_mpu_hid.o
inv-mpu-iio-objs += inv_mpu_core.o
inv-mpu-iio-objs += inv_mpu_ring.o
inv-mpu-iio-objs += inv_mpu_trigger.o
inv-mpu-iio-objs += inv_mpu_misc.o
inv-mpu-iio-objs += inv_mpu3050_iio.o
inv-mpu-iio-objs += dmpDefaultMPU6050.o
ifeq ($(CONFIG_INV_TESTING), y)
inv-mpu-iio-objs += inv_counters.o
endif
ifeq ($(VERSION),4)
ifeq ($(PATCHLEVEL),4)
CFLAGS_inv_mpu_core.o      	+= -Idrivers/iio -DINV_KERNEL_3_10
CFLAGS_inv_mpu_i2c.o            += -Idrivers/iio -DINV_KERNEL_3_10
CFLAGS_inv_mpu_spi.o       	+= -Idrivers/iio -DINV_KERNEL_3_10
CFLAGS_inv_mpu_hid.o		+= -Idrivers/iio -DINV_KERNEL_3_10
CFLAGS_inv_mpu_ring.o      	+= -Idrivers/iio -DINV_KERNEL_3_10
CFLAGS_inv_mpu_trigger.o   	+= -Idrivers/iio -DINV_KERNEL_3_10
CFLAGS_inv_mpu_common.o    	+= -Idrivers/iio -DINV_KERNEL_3_10
CFLAGS_inv_mpu_load_image.o	+= -Idrivers/iio -DINV_KERNEL_3_10
CFLAGS_inv_mpu_misc.o		+= -Idrivers/iio -DINV_KERNEL_3_10
CFLAGS_inv_mpu3050_iio.o	+= -Idrivers/iio -DINV_KERNEL_3_10
CFLAGS_dmpDefaultMPU6050.o	+= -Idrivers/iio -DINV_KERNEL_3_10
else
CFLAGS_inv_mpu_i2c.o       += -Idrivers/staging/iio
CFLAGS_inv_mpu_spi.o       += -Idrivers/staging/iio
CFLAGS_inv_mpu_hid.o       += -Idrivers/staging/iio
CFLAGS_inv_mpu_core.o      += -Idrivers/staging/iio
CFLAGS_inv_mpu_ring.o      += -Idrivers/staging/iio
CFLAGS_inv_mpu_trigger.o   += -Idrivers/staging/iio
CFLAGS_inv_mpu_misc.o      += -Idrivers/staging/iio
CFLAGS_inv_mpu3050_iio.o   += -Idrivers/staging/iio
CFLAGS_dmpDefaultMPU6050.o += -Idrivers/staging/iio
endif
endif

# the Bosch BMA250 driver is added to the inv-mpu device driver because it
# must be connected to an MPU3050 device on the secondary slave bus.
ifeq ($(CONFIG_INV_IIO_MPU3050_ACCEL_SLAVE_BMA250), y)
inv-mpu-iio-objs += inv_slave_bma250.o
ifeq ($(VERSION),4)
ifeq ($(PATCHLEVEL),4)
CFLAGS_inv_slave_bma250.o   += -Idrivers/iio
else
CFLAGS_inv_slave_bma250.o   += -Idrivers/staging/iio
endif
endif
endif

