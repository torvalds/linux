// SPDX-License-Identifier: GPL-2.0-or-later
#include "qlge.h"
#include "qlge_devlink.h"

static int qlge_fill_seg_(struct devlink_fmsg *fmsg,
			  struct mpi_coredump_segment_header *seg_header,
			  u32 *reg_data)
{
	int regs_num = (seg_header->seg_size
			- sizeof(struct mpi_coredump_segment_header)) / sizeof(u32);
	int err;
	int i;

	err = devlink_fmsg_pair_nest_start(fmsg, seg_header->description);
	if (err)
		return err;
	err = devlink_fmsg_obj_nest_start(fmsg);
	if (err)
		return err;
	err = devlink_fmsg_u32_pair_put(fmsg, "segment", seg_header->seg_num);
	if (err)
		return err;
	err = devlink_fmsg_arr_pair_nest_start(fmsg, "values");
	if (err)
		return err;
	for (i = 0; i < regs_num; i++) {
		err = devlink_fmsg_u32_put(fmsg, *reg_data);
		if (err)
			return err;
		reg_data++;
	}
	err = devlink_fmsg_obj_nest_end(fmsg);
	if (err)
		return err;
	err = devlink_fmsg_arr_pair_nest_end(fmsg);
	if (err)
		return err;
	err = devlink_fmsg_pair_nest_end(fmsg);
	return err;
}

#define FILL_SEG(seg_hdr, seg_regs)			                    \
	do {                                                                \
		err = qlge_fill_seg_(fmsg, &dump->seg_hdr, dump->seg_regs); \
		if (err) {					            \
			kvfree(dump);                                       \
			return err;				            \
		}                                                           \
	} while (0)

static int qlge_reporter_coredump(struct devlink_health_reporter *reporter,
				  struct devlink_fmsg *fmsg, void *priv_ctx,
				  struct netlink_ext_ack *extack)
{
	int err = 0;

	struct qlge_adapter *qdev = devlink_health_reporter_priv(reporter);
	struct qlge_mpi_coredump *dump;
	wait_queue_head_t wait;

	if (!netif_running(qdev->ndev))
		return 0;

	if (test_bit(QL_FRC_COREDUMP, &qdev->flags)) {
		if (qlge_own_firmware(qdev)) {
			qlge_queue_fw_error(qdev);
			init_waitqueue_head(&wait);
			wait_event_timeout(wait, 0, 5 * HZ);
		} else {
			netif_err(qdev, ifup, qdev->ndev,
				  "Force Coredump failed because this NIC function doesn't own the firmware\n");
			return -EPERM;
		}
	}

	dump = kvmalloc(sizeof(*dump), GFP_KERNEL);
	if (!dump)
		return -ENOMEM;

	err = qlge_core_dump(qdev, dump);
	if (err) {
		kvfree(dump);
		return err;
	}

	qlge_soft_reset_mpi_risc(qdev);

	FILL_SEG(core_regs_seg_hdr, mpi_core_regs);
	FILL_SEG(test_logic_regs_seg_hdr, test_logic_regs);
	FILL_SEG(rmii_regs_seg_hdr, rmii_regs);
	FILL_SEG(fcmac1_regs_seg_hdr, fcmac1_regs);
	FILL_SEG(fcmac2_regs_seg_hdr, fcmac2_regs);
	FILL_SEG(fc1_mbx_regs_seg_hdr, fc1_mbx_regs);
	FILL_SEG(ide_regs_seg_hdr, ide_regs);
	FILL_SEG(nic1_mbx_regs_seg_hdr, nic1_mbx_regs);
	FILL_SEG(smbus_regs_seg_hdr, smbus_regs);
	FILL_SEG(fc2_mbx_regs_seg_hdr, fc2_mbx_regs);
	FILL_SEG(nic2_mbx_regs_seg_hdr, nic2_mbx_regs);
	FILL_SEG(i2c_regs_seg_hdr, i2c_regs);
	FILL_SEG(memc_regs_seg_hdr, memc_regs);
	FILL_SEG(pbus_regs_seg_hdr, pbus_regs);
	FILL_SEG(mde_regs_seg_hdr, mde_regs);
	FILL_SEG(nic_regs_seg_hdr, nic_regs);
	FILL_SEG(nic2_regs_seg_hdr, nic2_regs);
	FILL_SEG(xgmac1_seg_hdr, xgmac1);
	FILL_SEG(xgmac2_seg_hdr, xgmac2);
	FILL_SEG(code_ram_seg_hdr, code_ram);
	FILL_SEG(memc_ram_seg_hdr, memc_ram);
	FILL_SEG(xaui_an_hdr, serdes_xaui_an);
	FILL_SEG(xaui_hss_pcs_hdr, serdes_xaui_hss_pcs);
	FILL_SEG(xfi_an_hdr, serdes_xfi_an);
	FILL_SEG(xfi_train_hdr, serdes_xfi_train);
	FILL_SEG(xfi_hss_pcs_hdr, serdes_xfi_hss_pcs);
	FILL_SEG(xfi_hss_tx_hdr, serdes_xfi_hss_tx);
	FILL_SEG(xfi_hss_rx_hdr, serdes_xfi_hss_rx);
	FILL_SEG(xfi_hss_pll_hdr, serdes_xfi_hss_pll);

	err = qlge_fill_seg_(fmsg, &dump->misc_nic_seg_hdr,
			     (u32 *)&dump->misc_nic_info);
	if (err) {
		kvfree(dump);
		return err;
	}

	FILL_SEG(intr_states_seg_hdr, intr_states);
	FILL_SEG(cam_entries_seg_hdr, cam_entries);
	FILL_SEG(nic_routing_words_seg_hdr, nic_routing_words);
	FILL_SEG(ets_seg_hdr, ets);
	FILL_SEG(probe_dump_seg_hdr, probe_dump);
	FILL_SEG(routing_reg_seg_hdr, routing_regs);
	FILL_SEG(mac_prot_reg_seg_hdr, mac_prot_regs);
	FILL_SEG(xaui2_an_hdr, serdes2_xaui_an);
	FILL_SEG(xaui2_hss_pcs_hdr, serdes2_xaui_hss_pcs);
	FILL_SEG(xfi2_an_hdr, serdes2_xfi_an);
	FILL_SEG(xfi2_train_hdr, serdes2_xfi_train);
	FILL_SEG(xfi2_hss_pcs_hdr, serdes2_xfi_hss_pcs);
	FILL_SEG(xfi2_hss_tx_hdr, serdes2_xfi_hss_tx);
	FILL_SEG(xfi2_hss_rx_hdr, serdes2_xfi_hss_rx);
	FILL_SEG(xfi2_hss_pll_hdr, serdes2_xfi_hss_pll);
	FILL_SEG(sem_regs_seg_hdr, sem_regs);

	kvfree(dump);
	return err;
}

static const struct devlink_health_reporter_ops qlge_reporter_ops = {
	.name = "coredump",
	.dump = qlge_reporter_coredump,
};

long qlge_health_create_reporters(struct qlge_adapter *priv)
{
	struct devlink *devlink;
	long err = 0;

	devlink = priv_to_devlink(priv);
	priv->reporter =
		devlink_health_reporter_create(devlink, &qlge_reporter_ops,
					       0, priv);
	if (IS_ERR(priv->reporter)) {
		err = PTR_ERR(priv->reporter);
		netdev_warn(priv->ndev,
			    "Failed to create reporter, err = %ld\n",
			    err);
	}
	return err;
}
