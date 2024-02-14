/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 */

#ifndef EFX_NIC_H
#define EFX_NIC_H

#include "nic_common.h"
#include "efx.h"

u32 efx_farch_fpga_ver(struct efx_nic *efx);

enum {
	PHY_TYPE_NONE = 0,
	PHY_TYPE_TXC43128 = 1,
	PHY_TYPE_88E1111 = 2,
	PHY_TYPE_SFX7101 = 3,
	PHY_TYPE_QT2022C2 = 4,
	PHY_TYPE_PM8358 = 6,
	PHY_TYPE_SFT9001A = 8,
	PHY_TYPE_QT2025C = 9,
	PHY_TYPE_SFT9001B = 10,
};

enum {
	SIENA_STAT_tx_bytes = GENERIC_STAT_COUNT,
	SIENA_STAT_tx_good_bytes,
	SIENA_STAT_tx_bad_bytes,
	SIENA_STAT_tx_packets,
	SIENA_STAT_tx_bad,
	SIENA_STAT_tx_pause,
	SIENA_STAT_tx_control,
	SIENA_STAT_tx_unicast,
	SIENA_STAT_tx_multicast,
	SIENA_STAT_tx_broadcast,
	SIENA_STAT_tx_lt64,
	SIENA_STAT_tx_64,
	SIENA_STAT_tx_65_to_127,
	SIENA_STAT_tx_128_to_255,
	SIENA_STAT_tx_256_to_511,
	SIENA_STAT_tx_512_to_1023,
	SIENA_STAT_tx_1024_to_15xx,
	SIENA_STAT_tx_15xx_to_jumbo,
	SIENA_STAT_tx_gtjumbo,
	SIENA_STAT_tx_collision,
	SIENA_STAT_tx_single_collision,
	SIENA_STAT_tx_multiple_collision,
	SIENA_STAT_tx_excessive_collision,
	SIENA_STAT_tx_deferred,
	SIENA_STAT_tx_late_collision,
	SIENA_STAT_tx_excessive_deferred,
	SIENA_STAT_tx_non_tcpudp,
	SIENA_STAT_tx_mac_src_error,
	SIENA_STAT_tx_ip_src_error,
	SIENA_STAT_rx_bytes,
	SIENA_STAT_rx_good_bytes,
	SIENA_STAT_rx_bad_bytes,
	SIENA_STAT_rx_packets,
	SIENA_STAT_rx_good,
	SIENA_STAT_rx_bad,
	SIENA_STAT_rx_pause,
	SIENA_STAT_rx_control,
	SIENA_STAT_rx_unicast,
	SIENA_STAT_rx_multicast,
	SIENA_STAT_rx_broadcast,
	SIENA_STAT_rx_lt64,
	SIENA_STAT_rx_64,
	SIENA_STAT_rx_65_to_127,
	SIENA_STAT_rx_128_to_255,
	SIENA_STAT_rx_256_to_511,
	SIENA_STAT_rx_512_to_1023,
	SIENA_STAT_rx_1024_to_15xx,
	SIENA_STAT_rx_15xx_to_jumbo,
	SIENA_STAT_rx_gtjumbo,
	SIENA_STAT_rx_bad_gtjumbo,
	SIENA_STAT_rx_overflow,
	SIENA_STAT_rx_false_carrier,
	SIENA_STAT_rx_symbol_error,
	SIENA_STAT_rx_align_error,
	SIENA_STAT_rx_length_error,
	SIENA_STAT_rx_internal_error,
	SIENA_STAT_rx_nodesc_drop_cnt,
	SIENA_STAT_COUNT
};

/**
 * struct siena_nic_data - Siena NIC state
 * @efx: Pointer back to main interface structure
 * @wol_filter_id: Wake-on-LAN packet filter id
 * @stats: Hardware statistics
 * @vf: Array of &struct siena_vf objects
 * @vf_buftbl_base: The zeroth buffer table index used to back VF queues.
 * @vfdi_status: Common VFDI status page to be dmad to VF address space.
 * @local_addr_list: List of local addresses. Protected by %local_lock.
 * @local_page_list: List of DMA addressable pages used to broadcast
 *	%local_addr_list. Protected by %local_lock.
 * @local_lock: Mutex protecting %local_addr_list and %local_page_list.
 * @peer_work: Work item to broadcast peer addresses to VMs.
 */
struct siena_nic_data {
	struct efx_nic *efx;
	int wol_filter_id;
	u64 stats[SIENA_STAT_COUNT];
#ifdef CONFIG_SFC_SRIOV
	struct siena_vf *vf;
	struct efx_channel *vfdi_channel;
	unsigned vf_buftbl_base;
	struct efx_buffer vfdi_status;
	struct list_head local_addr_list;
	struct list_head local_page_list;
	struct mutex local_lock;
	struct work_struct peer_work;
#endif
};

enum {
	EF10_STAT_port_tx_bytes = GENERIC_STAT_COUNT,
	EF10_STAT_port_tx_packets,
	EF10_STAT_port_tx_pause,
	EF10_STAT_port_tx_control,
	EF10_STAT_port_tx_unicast,
	EF10_STAT_port_tx_multicast,
	EF10_STAT_port_tx_broadcast,
	EF10_STAT_port_tx_lt64,
	EF10_STAT_port_tx_64,
	EF10_STAT_port_tx_65_to_127,
	EF10_STAT_port_tx_128_to_255,
	EF10_STAT_port_tx_256_to_511,
	EF10_STAT_port_tx_512_to_1023,
	EF10_STAT_port_tx_1024_to_15xx,
	EF10_STAT_port_tx_15xx_to_jumbo,
	EF10_STAT_port_rx_bytes,
	EF10_STAT_port_rx_bytes_minus_good_bytes,
	EF10_STAT_port_rx_good_bytes,
	EF10_STAT_port_rx_bad_bytes,
	EF10_STAT_port_rx_packets,
	EF10_STAT_port_rx_good,
	EF10_STAT_port_rx_bad,
	EF10_STAT_port_rx_pause,
	EF10_STAT_port_rx_control,
	EF10_STAT_port_rx_unicast,
	EF10_STAT_port_rx_multicast,
	EF10_STAT_port_rx_broadcast,
	EF10_STAT_port_rx_lt64,
	EF10_STAT_port_rx_64,
	EF10_STAT_port_rx_65_to_127,
	EF10_STAT_port_rx_128_to_255,
	EF10_STAT_port_rx_256_to_511,
	EF10_STAT_port_rx_512_to_1023,
	EF10_STAT_port_rx_1024_to_15xx,
	EF10_STAT_port_rx_15xx_to_jumbo,
	EF10_STAT_port_rx_gtjumbo,
	EF10_STAT_port_rx_bad_gtjumbo,
	EF10_STAT_port_rx_overflow,
	EF10_STAT_port_rx_align_error,
	EF10_STAT_port_rx_length_error,
	EF10_STAT_port_rx_nodesc_drops,
	EF10_STAT_port_rx_pm_trunc_bb_overflow,
	EF10_STAT_port_rx_pm_discard_bb_overflow,
	EF10_STAT_port_rx_pm_trunc_vfifo_full,
	EF10_STAT_port_rx_pm_discard_vfifo_full,
	EF10_STAT_port_rx_pm_trunc_qbb,
	EF10_STAT_port_rx_pm_discard_qbb,
	EF10_STAT_port_rx_pm_discard_mapping,
	EF10_STAT_port_rx_dp_q_disabled_packets,
	EF10_STAT_port_rx_dp_di_dropped_packets,
	EF10_STAT_port_rx_dp_streaming_packets,
	EF10_STAT_port_rx_dp_hlb_fetch,
	EF10_STAT_port_rx_dp_hlb_wait,
	EF10_STAT_rx_unicast,
	EF10_STAT_rx_unicast_bytes,
	EF10_STAT_rx_multicast,
	EF10_STAT_rx_multicast_bytes,
	EF10_STAT_rx_broadcast,
	EF10_STAT_rx_broadcast_bytes,
	EF10_STAT_rx_bad,
	EF10_STAT_rx_bad_bytes,
	EF10_STAT_rx_overflow,
	EF10_STAT_tx_unicast,
	EF10_STAT_tx_unicast_bytes,
	EF10_STAT_tx_multicast,
	EF10_STAT_tx_multicast_bytes,
	EF10_STAT_tx_broadcast,
	EF10_STAT_tx_broadcast_bytes,
	EF10_STAT_tx_bad,
	EF10_STAT_tx_bad_bytes,
	EF10_STAT_tx_overflow,
	EF10_STAT_V1_COUNT,
	EF10_STAT_fec_uncorrected_errors = EF10_STAT_V1_COUNT,
	EF10_STAT_fec_corrected_errors,
	EF10_STAT_fec_corrected_symbols_lane0,
	EF10_STAT_fec_corrected_symbols_lane1,
	EF10_STAT_fec_corrected_symbols_lane2,
	EF10_STAT_fec_corrected_symbols_lane3,
	EF10_STAT_ctpio_vi_busy_fallback,
	EF10_STAT_ctpio_long_write_success,
	EF10_STAT_ctpio_missing_dbell_fail,
	EF10_STAT_ctpio_overflow_fail,
	EF10_STAT_ctpio_underflow_fail,
	EF10_STAT_ctpio_timeout_fail,
	EF10_STAT_ctpio_noncontig_wr_fail,
	EF10_STAT_ctpio_frm_clobber_fail,
	EF10_STAT_ctpio_invalid_wr_fail,
	EF10_STAT_ctpio_vi_clobber_fallback,
	EF10_STAT_ctpio_unqualified_fallback,
	EF10_STAT_ctpio_runt_fallback,
	EF10_STAT_ctpio_success,
	EF10_STAT_ctpio_fallback,
	EF10_STAT_ctpio_poison,
	EF10_STAT_ctpio_erase,
	EF10_STAT_COUNT
};

/* Maximum number of TX PIO buffers we may allocate to a function.
 * This matches the total number of buffers on each SFC9100-family
 * controller.
 */
#define EF10_TX_PIOBUF_COUNT 16

/**
 * struct efx_ef10_nic_data - EF10 architecture NIC state
 * @mcdi_buf: DMA buffer for MCDI
 * @warm_boot_count: Last seen MC warm boot count
 * @vi_base: Absolute index of first VI in this function
 * @n_allocated_vis: Number of VIs allocated to this function
 * @n_piobufs: Number of PIO buffers allocated to this function
 * @wc_membase: Base address of write-combining mapping of the memory BAR
 * @pio_write_base: Base address for writing PIO buffers
 * @pio_write_vi_base: Relative VI number for @pio_write_base
 * @piobuf_handle: Handle of each PIO buffer allocated
 * @piobuf_size: size of a single PIO buffer
 * @must_restore_piobufs: Flag: PIO buffers have yet to be restored after MC
 *	reboot
 * @mc_stats: Scratch buffer for converting statistics to the kernel's format
 * @stats: Hardware statistics
 * @workaround_35388: Flag: firmware supports workaround for bug 35388
 * @workaround_26807: Flag: firmware supports workaround for bug 26807
 * @workaround_61265: Flag: firmware supports workaround for bug 61265
 * @must_check_datapath_caps: Flag: @datapath_caps needs to be revalidated
 *	after MC reboot
 * @datapath_caps: Capabilities of datapath firmware (FLAGS1 field of
 *	%MC_CMD_GET_CAPABILITIES response)
 * @datapath_caps2: Further Capabilities of datapath firmware (FLAGS2 field of
 * %MC_CMD_GET_CAPABILITIES response)
 * @rx_dpcpu_fw_id: Firmware ID of the RxDPCPU
 * @tx_dpcpu_fw_id: Firmware ID of the TxDPCPU
 * @must_probe_vswitching: Flag: vswitching has yet to be setup after MC reboot
 * @pf_index: The number for this PF, or the parent PF if this is a VF
#ifdef CONFIG_SFC_SRIOV
 * @vf: Pointer to VF data structure
#endif
 * @vport_mac: The MAC address on the vport, only for PFs; VFs will be zero
 * @vlan_list: List of VLANs added over the interface. Serialised by vlan_lock.
 * @vlan_lock: Lock to serialize access to vlan_list.
 * @udp_tunnels: UDP tunnel port numbers and types.
 * @udp_tunnels_dirty: flag indicating a reboot occurred while pushing
 *	@udp_tunnels to hardware and thus the push must be re-done.
 * @udp_tunnels_lock: Serialises writes to @udp_tunnels and @udp_tunnels_dirty.
 */
struct efx_ef10_nic_data {
	struct efx_buffer mcdi_buf;
	u16 warm_boot_count;
	unsigned int vi_base;
	unsigned int n_allocated_vis;
	unsigned int n_piobufs;
	void __iomem *wc_membase, *pio_write_base;
	unsigned int pio_write_vi_base;
	unsigned int piobuf_handle[EF10_TX_PIOBUF_COUNT];
	u16 piobuf_size;
	bool must_restore_piobufs;
	__le64 *mc_stats;
	u64 stats[EF10_STAT_COUNT];
	bool workaround_35388;
	bool workaround_26807;
	bool workaround_61265;
	bool must_check_datapath_caps;
	u32 datapath_caps;
	u32 datapath_caps2;
	unsigned int rx_dpcpu_fw_id;
	unsigned int tx_dpcpu_fw_id;
	bool must_probe_vswitching;
	unsigned int pf_index;
	u8 port_id[ETH_ALEN];
#ifdef CONFIG_SFC_SRIOV
	unsigned int vf_index;
	struct ef10_vf *vf;
#endif
	u8 vport_mac[ETH_ALEN];
	struct list_head vlan_list;
	struct mutex vlan_lock;
	struct efx_udp_tunnel udp_tunnels[16];
	bool udp_tunnels_dirty;
	struct mutex udp_tunnels_lock;
	u64 licensed_features;
};

/* TSOv2 */
int efx_ef10_tx_tso_desc(struct efx_tx_queue *tx_queue, struct sk_buff *skb,
			 bool *data_mapped);

extern const struct efx_nic_type efx_hunt_a0_nic_type;
extern const struct efx_nic_type efx_hunt_a0_vf_nic_type;

int falcon_probe_board(struct efx_nic *efx, u16 revision_info);

/* Falcon/Siena queue operations */
int efx_farch_tx_probe(struct efx_tx_queue *tx_queue);
void efx_farch_tx_init(struct efx_tx_queue *tx_queue);
void efx_farch_tx_fini(struct efx_tx_queue *tx_queue);
void efx_farch_tx_remove(struct efx_tx_queue *tx_queue);
void efx_farch_tx_write(struct efx_tx_queue *tx_queue);
unsigned int efx_farch_tx_limit_len(struct efx_tx_queue *tx_queue,
				    dma_addr_t dma_addr, unsigned int len);
int efx_farch_rx_probe(struct efx_rx_queue *rx_queue);
void efx_farch_rx_init(struct efx_rx_queue *rx_queue);
void efx_farch_rx_fini(struct efx_rx_queue *rx_queue);
void efx_farch_rx_remove(struct efx_rx_queue *rx_queue);
void efx_farch_rx_write(struct efx_rx_queue *rx_queue);
void efx_farch_rx_defer_refill(struct efx_rx_queue *rx_queue);
int efx_farch_ev_probe(struct efx_channel *channel);
int efx_farch_ev_init(struct efx_channel *channel);
void efx_farch_ev_fini(struct efx_channel *channel);
void efx_farch_ev_remove(struct efx_channel *channel);
int efx_farch_ev_process(struct efx_channel *channel, int quota);
void efx_farch_ev_read_ack(struct efx_channel *channel);
void efx_farch_ev_test_generate(struct efx_channel *channel);

/* Falcon/Siena filter operations */
int efx_farch_filter_table_probe(struct efx_nic *efx);
void efx_farch_filter_table_restore(struct efx_nic *efx);
void efx_farch_filter_table_remove(struct efx_nic *efx);
void efx_farch_filter_update_rx_scatter(struct efx_nic *efx);
s32 efx_farch_filter_insert(struct efx_nic *efx, struct efx_filter_spec *spec,
			    bool replace);
int efx_farch_filter_remove_safe(struct efx_nic *efx,
				 enum efx_filter_priority priority,
				 u32 filter_id);
int efx_farch_filter_get_safe(struct efx_nic *efx,
			      enum efx_filter_priority priority, u32 filter_id,
			      struct efx_filter_spec *);
int efx_farch_filter_clear_rx(struct efx_nic *efx,
			      enum efx_filter_priority priority);
u32 efx_farch_filter_count_rx_used(struct efx_nic *efx,
				   enum efx_filter_priority priority);
u32 efx_farch_filter_get_rx_id_limit(struct efx_nic *efx);
s32 efx_farch_filter_get_rx_ids(struct efx_nic *efx,
				enum efx_filter_priority priority, u32 *buf,
				u32 size);
#ifdef CONFIG_RFS_ACCEL
bool efx_farch_filter_rfs_expire_one(struct efx_nic *efx, u32 flow_id,
				     unsigned int index);
#endif
void efx_farch_filter_sync_rx_mode(struct efx_nic *efx);

/* Falcon/Siena interrupts */
void efx_farch_irq_enable_master(struct efx_nic *efx);
int efx_farch_irq_test_generate(struct efx_nic *efx);
void efx_farch_irq_disable_master(struct efx_nic *efx);
irqreturn_t efx_farch_msi_interrupt(int irq, void *dev_id);
irqreturn_t efx_farch_legacy_interrupt(int irq, void *dev_id);
irqreturn_t efx_farch_fatal_interrupt(struct efx_nic *efx);

/* Global Resources */
void siena_prepare_flush(struct efx_nic *efx);
int efx_farch_fini_dmaq(struct efx_nic *efx);
void efx_farch_finish_flr(struct efx_nic *efx);
void siena_finish_flush(struct efx_nic *efx);
void falcon_start_nic_stats(struct efx_nic *efx);
void falcon_stop_nic_stats(struct efx_nic *efx);
int falcon_reset_xaui(struct efx_nic *efx);
void efx_farch_dimension_resources(struct efx_nic *efx, unsigned sram_lim_qw);
void efx_farch_init_common(struct efx_nic *efx);
void efx_farch_rx_push_indir_table(struct efx_nic *efx);
void efx_farch_rx_pull_indir_table(struct efx_nic *efx);

/* Tests */
struct efx_farch_register_test {
	unsigned address;
	efx_oword_t mask;
};

int efx_farch_test_registers(struct efx_nic *efx,
			     const struct efx_farch_register_test *regs,
			     size_t n_regs);

void efx_farch_generate_event(struct efx_nic *efx, unsigned int evq,
			      efx_qword_t *event);

#endif /* EFX_NIC_H */
