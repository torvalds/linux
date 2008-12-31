#
# Makefile for the kernel I2O OSM.
#
# Note : at this point, these files are compiled on all systems.
# In the future, some of these should be built conditionally.
#

i2o_core-y		+= iop.o driver.o device.o debug.o pci.o exec-osm.o memory.o
i2o_bus-y		+= bus-osm.o
i2o_config-y		+= config-osm.o
obj-$(CONFIG_I2O)	+= i2o_core.o
obj-$(CONFIG_I2O_CONFIG)+= i2o_config.o
obj-$(CONFIG_I2O_BUS)	+= i2o_bus.o
obj-$(CONFIG_I2O_BLOCK)	+= i2o_block.o
obj-$(CONFIG_I2O_SCSI)	+= i2o_scsi.o
obj-$(CONFIG_I2O_PROC)	+= i2o_proc.o
