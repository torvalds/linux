obj-$(CONFIG_MLXSW_CORE)	+= mlxsw_core.o
mlxsw_core-objs			:= core.o
mlxsw_core-$(CONFIG_MLXSW_CORE_HWMON) += core_hwmon.o
obj-$(CONFIG_MLXSW_PCI)		+= mlxsw_pci.o
mlxsw_pci-objs			:= pci.o
obj-$(CONFIG_MLXSW_SWITCHX2)	+= mlxsw_switchx2.o
mlxsw_switchx2-objs		:= switchx2.o
obj-$(CONFIG_MLXSW_SPECTRUM)	+= mlxsw_spectrum.o
mlxsw_spectrum-objs		:= spectrum.o spectrum_buffers.o \
				   spectrum_switchdev.o spectrum_router.o \
				   spectrum_kvdl.o
mlxsw_spectrum-$(CONFIG_MLXSW_SPECTRUM_DCB)	+= spectrum_dcb.o
