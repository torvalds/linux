# SPDX-License-Identifier: GPL-2.0
#
# Compass Slaves MPUxxxx
#
obj-$(CONFIG_MPU_SENSORS_AMI30X) += inv_mpu_ami30x.o
inv_mpu_ami30x-objs +=	ami30x.o

obj-$(CONFIG_MPU_SENSORS_AMI306) += inv_mpu_ami306.o
inv_mpu_ami306-objs +=	ami306.o

obj-$(CONFIG_MPU_SENSORS_HMC5883) += inv_mpu_hmc5883.o
inv_mpu_hmc5883-objs +=	hmc5883.o

obj-$(CONFIG_MPU_SENSORS_LSM303DLX_M) += inv_mpu_lsm303dlx_m.o
inv_mpu_lsm303dlx_m-objs +=	lsm303dlx_m.o

obj-$(CONFIG_MPU_SENSORS_MMC314X) += inv_mpu_mmc314x.o
inv_mpu_mmc314x-objs +=	mmc314x.o

obj-$(CONFIG_MPU_SENSORS_YAS529) += inv_mpu_yas529.o
inv_mpu_yas529-objs +=	yas529-kernel.o

obj-$(CONFIG_MPU_SENSORS_YAS530) += inv_mpu_yas530.o
inv_mpu_yas530-objs +=	yas530.o

obj-$(CONFIG_MPU_SENSORS_HSCDTD002B) += inv_mpu_hscdtd002b.o
inv_mpu_hscdtd002b-objs +=	hscdtd002b.o

obj-$(CONFIG_MPU_SENSORS_HSCDTD004A) += inv_mpu_hscdtd004a.o
inv_mpu_hscdtd004a-objs +=	hscdtd004a.o

obj-$(CONFIG_MPU_SENSORS_AK8975) += inv_mpu_ak8975.o
inv_mpu_ak8975-objs +=	ak8975.o

obj-$(CONFIG_MPU_SENSORS_AK8963) += inv_mpu_ak8963.o
inv_mpu_ak8963-objs +=	ak8963.o
obj-$(CONFIG_MPU_SENSORS_AK8972) += inv_mpu_ak8972.o
inv_mpu_ak8972-objs +=	ak8972.o

EXTRA_CFLAGS += -Idrivers/misc/inv_mpu
EXTRA_CFLAGS += -D__C99_DESIGNATED_INITIALIZER
