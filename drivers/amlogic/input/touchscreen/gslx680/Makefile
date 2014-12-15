#
# Makefile for the touchscreen drivers.
#

# Each configuration option enables a list of files.
obj-y	+= amlogic_gslX680.o
obj-y += gsl_point_id.o
$(obj)/gsl_point_id.o: $(srctree)/$(obj)/gsl_point_id
	cp $(srctree)/$(obj)/gsl_point_id $(obj)/gsl_point_id.o