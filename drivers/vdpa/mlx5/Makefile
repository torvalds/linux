subdir-ccflags-y += -I$(srctree)/drivers/vdpa/mlx5/core

obj-$(CONFIG_MLX5_VDPA_NET) += mlx5_vdpa.o
mlx5_vdpa-$(CONFIG_MLX5_VDPA_NET) += net/mlx5_vnet.o core/resources.o core/mr.o net/debug.o
