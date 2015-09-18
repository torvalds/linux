obj-$(CONFIG_MLX5_INFINIBAND)	+= mlx5_ib.o

mlx5_ib-y :=	main.o cq.o doorbell.o qp.o mem.o srq.o mr.o ah.o mad.o
mlx5_ib-$(CONFIG_INFINIBAND_ON_DEMAND_PAGING) += odp.o
