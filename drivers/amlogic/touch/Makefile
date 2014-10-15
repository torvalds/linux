#############################################################
#
# NOTE:
#
#
# ALL new added gsl driver should be depend on 
# this config
#
############################################################

obj-$(CONFIG_AML_TOUCH_ALGORITHM_SUPPORT) += gsl_point_id.o

gsl_point_id-objs =
gsl_point_id-objs += aml_gsl_module.o

ifeq ($(MAKE_FOR_LIB), y)
gsl_point_id-objs += gsl_point_id_z.o 
else 
gsl_point_id-objs += gsl_point_id_z.z 
endif
