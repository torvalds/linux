#
# Makefile for Amlogic MIPI.
#
EXTRA_AFLAGS=-mfloat-abi=softfp -mfpu=neon
aml_mipi-objs := am_mipi_csi2.o
aml_mipi_mem-objs := mem/mipi_mem.o mem/convert_mem.o mem/swap_mem.o
aml_mipi_vdin-objs := vdin/mipi_vdin.o vdin/convert_vdin.o vdin/swap_vdin_y.o vdin/swap_vdin_uv.o vdin/csi.o
aml_mipi_common-objs := common/bufq.o common/mipi_hw.o

obj-$(CONFIG_AMLOGIC_MIPI)  += aml_mipi.o
obj-$(CONFIG_AMLOGIC_MIPI)  += aml_mipi_common.o
obj-$(CONFIG_MEM_MIPI) += aml_mipi_mem.o
obj-$(CONFIG_VDIN_MIPI) += aml_mipi_vdin.o

