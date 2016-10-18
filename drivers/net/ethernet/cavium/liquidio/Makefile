#
# Cavium Liquidio ethernet device driver
#
obj-$(CONFIG_LIQUIDIO) += liquidio.o

liquidio-$(CONFIG_LIQUIDIO) += lio_ethtool.o \
			lio_core.o         \
			request_manager.o  \
			response_manager.o \
			octeon_device.o    \
			cn66xx_device.o    \
			cn68xx_device.o    \
			cn23xx_pf_device.o \
			octeon_mem_ops.o   \
			octeon_droq.o      \
			octeon_nic.o

liquidio-objs := lio_main.o octeon_console.o $(liquidio-y)
