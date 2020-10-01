# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Mellanox 5th generation network adapters
# (ConnectX series) core & netdev driver
#

subdir-ccflags-y += -I$(src)

obj-$(CONFIG_MLX5_CORE) += mlx5_core.o

#
# mlx5 core basic
#
mlx5_core-y :=	main.o cmd.o debugfs.o fw.o eq.o uar.o pagealloc.o \
		health.o mcg.o cq.o alloc.o port.o mr.o pd.o \
		transobj.o vport.o sriov.o fs_cmd.o fs_core.o pci_irq.o \
		fs_counters.o rl.o lag.o dev.o events.o wq.o lib/gid.o \
		lib/devcom.o lib/pci_vsc.o lib/dm.o diag/fs_tracepoint.o \
		diag/fw_tracer.o diag/crdump.o devlink.o diag/rsc_dump.o

#
# Netdev basic
#
mlx5_core-$(CONFIG_MLX5_CORE_EN) += en_main.o en_common.o en_fs.o en_ethtool.o \
		en_tx.o en_rx.o en_dim.o en_txrx.o en/xdp.o en_stats.o \
		en_selftest.o en/port.o en/monitor_stats.o en/health.o \
		en/reporter_tx.o en/reporter_rx.o en/params.o en/xsk/pool.o \
		en/xsk/setup.o en/xsk/rx.o en/xsk/tx.o en/devlink.o

#
# Netdev extra
#
mlx5_core-$(CONFIG_MLX5_EN_ARFS)     += en_arfs.o
mlx5_core-$(CONFIG_MLX5_EN_RXNFC)    += en_fs_ethtool.o
mlx5_core-$(CONFIG_MLX5_CORE_EN_DCB) += en_dcbnl.o en/port_buffer.o
mlx5_core-$(CONFIG_PCI_HYPERV_INTERFACE) += en/hv_vhca_stats.o
mlx5_core-$(CONFIG_MLX5_ESWITCH)     += lag_mp.o lib/geneve.o lib/port_tun.o \
					en_rep.o en/rep/bond.o en/mod_hdr.o
mlx5_core-$(CONFIG_MLX5_CLS_ACT)     += en_tc.o en/rep/tc.o en/rep/neigh.o \
					en/mapping.o lib/fs_chains.o en/tc_tun.o \
					en/tc_tun_vxlan.o en/tc_tun_gre.o en/tc_tun_geneve.o \
					en/tc_tun_mplsoudp.o diag/en_tc_tracepoint.o
mlx5_core-$(CONFIG_MLX5_TC_CT)	     += en/tc_ct.o

#
# Core extra
#
mlx5_core-$(CONFIG_MLX5_ESWITCH)   += eswitch.o eswitch_offloads.o eswitch_offloads_termtbl.o \
				      ecpf.o rdma.o
mlx5_core-$(CONFIG_MLX5_ESWITCH)   += esw/acl/helper.o \
				      esw/acl/egress_lgcy.o esw/acl/egress_ofld.o \
				      esw/acl/ingress_lgcy.o esw/acl/ingress_ofld.o \
				      esw/devlink_port.o

mlx5_core-$(CONFIG_MLX5_MPFS)      += lib/mpfs.o
mlx5_core-$(CONFIG_VXLAN)          += lib/vxlan.o
mlx5_core-$(CONFIG_PTP_1588_CLOCK) += lib/clock.o
mlx5_core-$(CONFIG_PCI_HYPERV_INTERFACE) += lib/hv.o lib/hv_vhca.o

#
# Ipoib netdev
#
mlx5_core-$(CONFIG_MLX5_CORE_IPOIB) += ipoib/ipoib.o ipoib/ethtool.o ipoib/ipoib_vlan.o

#
# Accelerations & FPGA
#
mlx5_core-$(CONFIG_MLX5_IPSEC) += accel/ipsec_offload.o
mlx5_core-$(CONFIG_MLX5_FPGA_IPSEC) += fpga/ipsec.o
mlx5_core-$(CONFIG_MLX5_FPGA_TLS)   += fpga/tls.o
mlx5_core-$(CONFIG_MLX5_ACCEL)      += lib/crypto.o accel/tls.o accel/ipsec.o

mlx5_core-$(CONFIG_MLX5_FPGA) += fpga/cmd.o fpga/core.o fpga/conn.o fpga/sdk.o

mlx5_core-$(CONFIG_MLX5_EN_IPSEC) += en_accel/ipsec.o en_accel/ipsec_rxtx.o \
				     en_accel/ipsec_stats.o en_accel/ipsec_fs.o

mlx5_core-$(CONFIG_MLX5_EN_TLS) += en_accel/tls.o en_accel/tls_rxtx.o en_accel/tls_stats.o \
				   en_accel/fs_tcp.o en_accel/ktls.o en_accel/ktls_txrx.o \
				   en_accel/ktls_tx.o en_accel/ktls_rx.o

mlx5_core-$(CONFIG_MLX5_SW_STEERING) += steering/dr_domain.o steering/dr_table.o \
					steering/dr_matcher.o steering/dr_rule.o \
					steering/dr_icm_pool.o \
					steering/dr_ste.o steering/dr_send.o \
					steering/dr_cmd.o steering/dr_fw.o \
					steering/dr_action.o steering/fs_dr.o
