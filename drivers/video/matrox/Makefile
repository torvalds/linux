# Makefile for the Linux video drivers.
# 5 Aug 1999, James Simmons, <mailto:jsimmons@edgeglobal.com>
# Rewritten to use lists instead of if-statements.

# Each configuration option enables a list of files.

my-obj-$(CONFIG_FB_MATROX_G)      += g450_pll.o matroxfb_g450.o matroxfb_crtc2.o

obj-$(CONFIG_FB_MATROX)           += matroxfb_base.o matroxfb_accel.o matroxfb_DAC1064.o matroxfb_Ti3026.o matroxfb_misc.o $(my-obj-y)
obj-$(CONFIG_FB_MATROX_I2C)       += i2c-matroxfb.o
obj-$(CONFIG_FB_MATROX_MAVEN)     += matroxfb_maven.o matroxfb_crtc2.o
