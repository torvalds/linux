#
# Makefile for the sensor drivers.
#

# Each configuration option enables a list of files.

#Gravity

obj-$(CONFIG_SENSOR_DEVICES) += common.o

obj-$(CONFIG_GRAVITY_MMA8452)   += mma8452.o
obj-$(CONFIG_GRAVITY_MMA7660)   += mma7660.o
obj-$(CONFIG_GRAVITY_MIR3DA)   += mir3da_core.o mir3da_cust.o
obj-$(CONFIG_GRAVITY_LIS3DH)   += lis3dh_acc.o
obj-$(CONFIG_GRAVITY_BMA250)		+=bma250.o
obj-$(CONFIG_GRAVITY_BMA222)      += bma222.o
obj-$(CONFIG_GRAVITY_MC32X0)   += mc32x0.o
obj-$(CONFIG_GRAVITY_DMARD06)	+= dmard06.o
obj-$(CONFIG_GRAVITY_MMA865X)	+= mma865x.o
obj-$(CONFIG_GRAVITY_LSM303D)	+= lsm303d.o
obj-$(CONFIG_GRAVITY_DMARD10)	+= dmt10.o
obj-$(CONFIG_GRAVITY_KXTJ9)	+= kionix_accel.o

obj-$(CONFIG_GRAVITY_STK8313)	+= stk8313.o
obj-$(CONFIG_GRAVITY_STK8312)	+= stk831x.o

obj-$(CONFIG_GRAVITY_MXC622X)     += mxc622x.o
obj-$(CONFIG_GRAVITY_MXC6255XC)     += mxc6255xc.o

#GYROSCOPE
obj-$(CONFIG_GYROSCOPE_L3GD20)    += l3gd20_gyr.o

#Light

obj-$(CONFIG_LIGHT_CM36283)	+= cm36283.o
obj-$(CONFIG_LIGHT_CM3232)	+= cm3232.o
obj-$(CONFIG_LIGHT_CM3217)	+= cm3217.o
obj-$(CONFIG_LIGHT_EPL6814)+= elan_epl6814.o
obj-$(CONFIG_LIGHT_LTR501)	+= ltr501_als.o
obj-$(CONFIG_LIGHT_STK220X) += stk220x_poll.o
obj-$(CONFIG_LIGHT_LTR558) += ltr558_pls.o
obj-$(CONFIG_LIGHT_ISL29023) += isl29023.o

