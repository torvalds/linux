# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_MLXSW_CORE)	+= mlxsw_core.o
mlxsw_core-objs			:= core.o core_acl_flex_keys.o \
				   core_acl_flex_actions.o core_env.o \
				   core_linecards.o core_linecard_dev.o
mlxsw_core-$(CONFIG_MLXSW_CORE_HWMON) += core_hwmon.o
mlxsw_core-$(CONFIG_MLXSW_CORE_THERMAL) += core_thermal.o
obj-$(CONFIG_MLXSW_PCI)		+= mlxsw_pci.o
mlxsw_pci-objs			:= pci.o
obj-$(CONFIG_MLXSW_I2C)		+= mlxsw_i2c.o
mlxsw_i2c-objs			:= i2c.o
obj-$(CONFIG_MLXSW_SPECTRUM)	+= mlxsw_spectrum.o
mlxsw_spectrum-objs		:= spectrum.o spectrum_buffers.o \
				   spectrum_switchdev.o spectrum_router.o \
				   spectrum1_kvdl.o spectrum2_kvdl.o \
				   spectrum_kvdl.o \
				   spectrum_acl_tcam.o spectrum_acl_ctcam.o \
				   spectrum_acl_atcam.o spectrum_acl_erp.o \
				   spectrum1_acl_tcam.o spectrum2_acl_tcam.o \
				   spectrum_acl_bloom_filter.o spectrum_acl.o \
				   spectrum_flow.o spectrum_matchall.o \
				   spectrum_flower.o spectrum_cnt.o \
				   spectrum_fid.o spectrum_ipip.o \
				   spectrum_acl_flex_actions.o \
				   spectrum_acl_flex_keys.o \
				   spectrum1_mr_tcam.o spectrum2_mr_tcam.o \
				   spectrum_mr_tcam.o spectrum_mr.o \
				   spectrum_qdisc.o spectrum_span.o \
				   spectrum_nve.o spectrum_nve_vxlan.o \
				   spectrum_dpipe.o spectrum_trap.o \
				   spectrum_ethtool.o spectrum_policer.o \
				   spectrum_pgt.o
mlxsw_spectrum-$(CONFIG_MLXSW_SPECTRUM_DCB)	+= spectrum_dcb.o
mlxsw_spectrum-$(CONFIG_PTP_1588_CLOCK)		+= spectrum_ptp.o
obj-$(CONFIG_MLXSW_MINIMAL)	+= mlxsw_minimal.o
mlxsw_minimal-objs		:= minimal.o
