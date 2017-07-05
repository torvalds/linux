obj-$(CONFIG_MLX5_CORE)		+= mlx5_core.o
subdir-ccflags-y += -I$(src)

mlx5_core-y :=	main.o cmd.o debugfs.o fw.o eq.o uar.o pagealloc.o \
		health.o mcg.o cq.o srq.o alloc.o qp.o port.o mr.o pd.o \
		mad.o transobj.o vport.o sriov.o fs_cmd.o fs_core.o \
		fs_counters.o rl.o lag.o dev.o lib/gid.o

mlx5_core-$(CONFIG_MLX5_ACCEL) += accel/ipsec.o

mlx5_core-$(CONFIG_MLX5_FPGA) += fpga/cmd.o fpga/core.o fpga/conn.o fpga/sdk.o \
		fpga/ipsec.o

mlx5_core-$(CONFIG_MLX5_CORE_EN) += wq.o eswitch.o eswitch_offloads.o \
		en_main.o en_common.o en_fs.o en_ethtool.o en_tx.o \
		en_rx.o en_rx_am.o en_txrx.o en_clock.o vxlan.o \
		en_tc.o en_arfs.o en_rep.o en_fs_ethtool.o en_selftest.o

mlx5_core-$(CONFIG_MLX5_CORE_EN_DCB) +=  en_dcbnl.o

mlx5_core-$(CONFIG_MLX5_CORE_IPOIB) += ipoib/ipoib.o ipoib/ethtool.o

mlx5_core-$(CONFIG_MLX5_EN_IPSEC) += en_accel/ipsec.o en_accel/ipsec_rxtx.o \
		en_accel/ipsec_stats.o
