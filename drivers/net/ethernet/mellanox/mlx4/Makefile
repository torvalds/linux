obj-$(CONFIG_MLX4_CORE)		+= mlx4_core.o

mlx4_core-y :=	alloc.o catas.o cmd.o cq.o eq.o fw.o icm.o intf.o main.o mcg.o \
		mr.o pd.o port.o profile.o qp.o reset.o sense.o srq.o resource_tracker.o

obj-$(CONFIG_MLX4_EN)               += mlx4_en.o

mlx4_en-y := 	en_main.o en_tx.o en_rx.o en_ethtool.o en_port.o en_cq.o \
		en_resources.o en_netdev.o en_selftest.o en_clock.o
mlx4_en-$(CONFIG_MLX4_EN_DCB) += en_dcb_nl.o
