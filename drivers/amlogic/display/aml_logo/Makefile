#
# linux/arch/arc/drivers/Makefile
#
#


#O_TARGET	:= peripherals.o
ifeq ($(CONFIG_AM_LOGO),y)
obj-y += amlogo.o
amlogo-objs := ./loader/logo_setup_uboot.o  \
			./logo_parse/logo_parse.o	\
			./logo_parse/bmp_parser.o	\
			./logo_display/logo_output.o	\
			./logo_display/output_dev_osd.o \
			./logo_display/dev_ge2d.o \
			./logo_display/output_dev_vid.o \
			./logo_parse/jpeg_parser.o

EXTRA_CFLAGS += -Iinclude/linux/amlogic/logo/			  
EXTRA_CFLAGS += -Idrivers/amlogic/display/ge2d
EXTRA_CFLAGS += -Idrivers/amlogic/amports/
else
obj-y=./loader/logo_empty_setup.o
EXTRA_CFLAGS += -Iinclude/linux/amlogic/logo/
endif
