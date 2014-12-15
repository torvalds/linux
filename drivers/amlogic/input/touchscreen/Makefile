#
# Makefile for the touchscreen drivers.
#

# Each configuration option enables a list of files.
obj-$(CONFIG_MESON_INPUT_TOUCHSCREEN) += common.o
obj-$(CONFIG_FOCALTECH_CAPACITIVE_TOUCHSCREEN)		+=focaltech_5x06.o
obj-$(CONFIG_GOODIX_GT81XX_CAPACITIVE_TOUCHSCREEN)	+= goodix_gt81xx.o
obj-$(CONFIG_GSLX680_CAPACITIVE_TOUCHSCREEN)	+= gslx680.o aml_gsl_common.o
obj-$(CONFIG_GSLX680B_CAPACITIVE_TOUCHSCREEN)	+= gslx680B.o
obj-$(CONFIG_GSLX680_COMPATIBLE_CAPACITIVE_TOUCHSCREEN)        += gslx680_compatible.o aml_gsl_common.o
obj-$(CONFIG_NOVATEK_CAPACITIVE_TOUCHSCREEN)  += novatek.o
#obj-m += gsl_point_id.o
#gsl_point_id-objs += gsl_point_id_z.o aml_gsl_module.o
obj-$(CONFIG_MESON_INPUT_TOUCHSCREEN) += gt9xx/
obj-$(CONFIG_MESON_INPUT_TOUCHSCREEN) += ct36x_ts/
obj-$(CONFIG_MESON_INPUT_TOUCHSCREEN) += ft5x02/
obj-$(CONFIG_MESON_INPUT_TOUCHSCREEN) += gslx680/
obj-y += gt9xx/
obj-y += ct36x_ts/
obj-y += ft5x02/
