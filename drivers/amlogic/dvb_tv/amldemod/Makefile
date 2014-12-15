
obj-$(CONFIG_AM_M6_DEMOD) += aml_fe.o

aml_fe-objs := demod_func.o  dvbc_func.o i2c_func.o tuner_func.o #dvbt_func.o

#aml_fe-objs += mxl/MxL5007.o
#aml_fe-objs += mxl/MxL5007_API.o
#aml_fe-objs += mxl/MxL_User_Define.o
#aml_fe-objs += nxp/tuner_fj2207.o
#aml_fe-objs += nxp/tmbslNT220x.o
#aml_fe-objs += nxp/tmbslNT220xInstance.o
#aml_fe-objs += nxp/tmddNT220x.o
#aml_fe-objs += nxp/tmddNT220xInstance.o
#aml_fe-objs += nxp/tmddNT220x_Advanced.o
#aml_fe-objs += si2176/si2176_func.o

##ifeq ($(CONFIG_AM_DEMOD_DVBAPI), y)
aml_fe-objs += amlfrontend.o
##endif

##ifeq ($(CONFIG_AM_DEMOD_DEBUG), y)
aml_fe-objs	+= aml_demod.o
##endif

ifeq ($(CONFIG_AM_SI2177), y)
##EXTRA_CFLAGS += -I../../../../hardware/dvb/silabs/drivers/si2177/
endif


EXTRA_CFLAGS += -I.
EXTRA_CFLAGS += -Idrivers/media/dvb-core

