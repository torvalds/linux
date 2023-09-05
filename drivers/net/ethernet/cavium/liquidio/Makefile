# SPDX-License-Identifier: GPL-2.0
#
# Cavium Liquidio ethernet device driver
#

obj-$(CONFIG_LIQUIDIO_CORE) += liquidio-core.o
liquidio-core-y := \
		lio_ethtool.o		\
		lio_core.o		\
		request_manager.o	\
		response_manager.o	\
		octeon_device.o		\
		cn66xx_device.o		\
		cn68xx_device.o		\
		cn23xx_pf_device.o	\
		cn23xx_vf_device.o	\
		octeon_mailbox.o	\
		octeon_mem_ops.o	\
		octeon_droq.o		\
		octeon_nic.o

obj-$(CONFIG_LIQUIDIO) += liquidio.o
liquidio-y := lio_main.o octeon_console.o lio_vf_rep.o

obj-$(CONFIG_LIQUIDIO_VF) += liquidio_vf.o
liquidio_vf-y := lio_vf_main.o
