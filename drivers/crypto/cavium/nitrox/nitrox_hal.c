// SPDX-License-Identifier: GPL-2.0
#include <linux/delay.h>

#include "nitrox_dev.h"
#include "nitrox_csr.h"

/**
 * emu_enable_cores - Enable EMU cluster cores.
 * @ndev: N5 device
 */
static void emu_enable_cores(struct nitrox_device *ndev)
{
	union emu_se_enable emu_se;
	union emu_ae_enable emu_ae;
	int i;

	/* AE cores 20 per cluster */
	emu_ae.value = 0;
	emu_ae.s.enable = 0xfffff;

	/* SE cores 16 per cluster */
	emu_se.value = 0;
	emu_se.s.enable = 0xffff;

	/* enable per cluster cores */
	for (i = 0; i < NR_CLUSTERS; i++) {
		nitrox_write_csr(ndev, EMU_AE_ENABLEX(i), emu_ae.value);
		nitrox_write_csr(ndev, EMU_SE_ENABLEX(i), emu_se.value);
	}
}

/**
 * nitrox_config_emu_unit - configure EMU unit.
 * @ndev: N5 device
 */
void nitrox_config_emu_unit(struct nitrox_device *ndev)
{
	union emu_wd_int_ena_w1s emu_wd_int;
	union emu_ge_int_ena_w1s emu_ge_int;
	u64 offset;
	int i;

	/* enable cores */
	emu_enable_cores(ndev);

	/* enable general error and watch dog interrupts */
	emu_ge_int.value = 0;
	emu_ge_int.s.se_ge = 0xffff;
	emu_ge_int.s.ae_ge = 0xfffff;
	emu_wd_int.value = 0;
	emu_wd_int.s.se_wd = 1;

	for (i = 0; i < NR_CLUSTERS; i++) {
		offset = EMU_WD_INT_ENA_W1SX(i);
		nitrox_write_csr(ndev, offset, emu_wd_int.value);
		offset = EMU_GE_INT_ENA_W1SX(i);
		nitrox_write_csr(ndev, offset, emu_ge_int.value);
	}
}

static void reset_pkt_input_ring(struct nitrox_device *ndev, int ring)
{
	union nps_pkt_in_instr_ctl pkt_in_ctl;
	union nps_pkt_in_instr_baoff_dbell pkt_in_dbell;
	union nps_pkt_in_done_cnts pkt_in_cnts;
	u64 offset;

	offset = NPS_PKT_IN_INSTR_CTLX(ring);
	/* disable the ring */
	pkt_in_ctl.value = nitrox_read_csr(ndev, offset);
	pkt_in_ctl.s.enb = 0;
	nitrox_write_csr(ndev, offset, pkt_in_ctl.value);
	usleep_range(100, 150);

	/* wait to clear [ENB] */
	do {
		pkt_in_ctl.value = nitrox_read_csr(ndev, offset);
	} while (pkt_in_ctl.s.enb);

	/* clear off door bell counts */
	offset = NPS_PKT_IN_INSTR_BAOFF_DBELLX(ring);
	pkt_in_dbell.value = 0;
	pkt_in_dbell.s.dbell = 0xffffffff;
	nitrox_write_csr(ndev, offset, pkt_in_dbell.value);

	/* clear done counts */
	offset = NPS_PKT_IN_DONE_CNTSX(ring);
	pkt_in_cnts.value = nitrox_read_csr(ndev, offset);
	nitrox_write_csr(ndev, offset, pkt_in_cnts.value);
	usleep_range(50, 100);
}

void enable_pkt_input_ring(struct nitrox_device *ndev, int ring)
{
	union nps_pkt_in_instr_ctl pkt_in_ctl;
	u64 offset;

	/* 64-byte instruction size */
	offset = NPS_PKT_IN_INSTR_CTLX(ring);
	pkt_in_ctl.value = nitrox_read_csr(ndev, offset);
	pkt_in_ctl.s.is64b = 1;
	pkt_in_ctl.s.enb = 1;
	nitrox_write_csr(ndev, offset, pkt_in_ctl.value);

	/* wait for set [ENB] */
	do {
		pkt_in_ctl.value = nitrox_read_csr(ndev, offset);
	} while (!pkt_in_ctl.s.enb);
}

/**
 * nitrox_config_pkt_input_rings - configure Packet Input Rings
 * @ndev: N5 device
 */
void nitrox_config_pkt_input_rings(struct nitrox_device *ndev)
{
	int i;

	for (i = 0; i < ndev->nr_queues; i++) {
		struct nitrox_cmdq *cmdq = &ndev->pkt_cmdqs[i];
		union nps_pkt_in_instr_rsize pkt_in_rsize;
		u64 offset;

		reset_pkt_input_ring(ndev, i);

		/* configure ring base address 16-byte aligned,
		 * size and interrupt threshold.
		 */
		offset = NPS_PKT_IN_INSTR_BADDRX(i);
		nitrox_write_csr(ndev, NPS_PKT_IN_INSTR_BADDRX(i), cmdq->dma);

		/* configure ring size */
		offset = NPS_PKT_IN_INSTR_RSIZEX(i);
		pkt_in_rsize.value = 0;
		pkt_in_rsize.s.rsize = ndev->qlen;
		nitrox_write_csr(ndev, offset, pkt_in_rsize.value);

		/* set high threshold for pkt input ring interrupts */
		offset = NPS_PKT_IN_INT_LEVELSX(i);
		nitrox_write_csr(ndev, offset, 0xffffffff);

		enable_pkt_input_ring(ndev, i);
	}
}

static void reset_pkt_solicit_port(struct nitrox_device *ndev, int port)
{
	union nps_pkt_slc_ctl pkt_slc_ctl;
	union nps_pkt_slc_cnts pkt_slc_cnts;
	u64 offset;

	/* disable slc port */
	offset = NPS_PKT_SLC_CTLX(port);
	pkt_slc_ctl.value = nitrox_read_csr(ndev, offset);
	pkt_slc_ctl.s.enb = 0;
	nitrox_write_csr(ndev, offset, pkt_slc_ctl.value);
	usleep_range(100, 150);

	/* wait to clear [ENB] */
	do {
		pkt_slc_ctl.value = nitrox_read_csr(ndev, offset);
	} while (pkt_slc_ctl.s.enb);

	/* clear slc counters */
	offset = NPS_PKT_SLC_CNTSX(port);
	pkt_slc_cnts.value = nitrox_read_csr(ndev, offset);
	nitrox_write_csr(ndev, offset, pkt_slc_cnts.value);
	usleep_range(50, 100);
}

void enable_pkt_solicit_port(struct nitrox_device *ndev, int port)
{
	union nps_pkt_slc_ctl pkt_slc_ctl;
	u64 offset;

	offset = NPS_PKT_SLC_CTLX(port);
	pkt_slc_ctl.value = 0;
	pkt_slc_ctl.s.enb = 1;

	/*
	 * 8 trailing 0x00 bytes will be added
	 * to the end of the outgoing packet.
	 */
	pkt_slc_ctl.s.z = 1;
	/* enable response header */
	pkt_slc_ctl.s.rh = 1;
	nitrox_write_csr(ndev, offset, pkt_slc_ctl.value);

	/* wait to set [ENB] */
	do {
		pkt_slc_ctl.value = nitrox_read_csr(ndev, offset);
	} while (!pkt_slc_ctl.s.enb);
}

static void config_single_pkt_solicit_port(struct nitrox_device *ndev,
					   int port)
{
	union nps_pkt_slc_int_levels pkt_slc_int;
	u64 offset;

	reset_pkt_solicit_port(ndev, port);

	offset = NPS_PKT_SLC_INT_LEVELSX(port);
	pkt_slc_int.value = 0;
	/* time interrupt threshold */
	pkt_slc_int.s.timet = 0x3fffff;
	nitrox_write_csr(ndev, offset, pkt_slc_int.value);

	enable_pkt_solicit_port(ndev, port);
}

void nitrox_config_pkt_solicit_ports(struct nitrox_device *ndev)
{
	int i;

	for (i = 0; i < ndev->nr_queues; i++)
		config_single_pkt_solicit_port(ndev, i);
}

/**
 * enable_nps_interrupts - enable NPS interrutps
 * @ndev: N5 device.
 *
 * This includes NPS core, packet in and slc interrupts.
 */
static void enable_nps_interrupts(struct nitrox_device *ndev)
{
	union nps_core_int_ena_w1s core_int;

	/* NPS core interrutps */
	core_int.value = 0;
	core_int.s.host_wr_err = 1;
	core_int.s.host_wr_timeout = 1;
	core_int.s.exec_wr_timeout = 1;
	core_int.s.npco_dma_malform = 1;
	core_int.s.host_nps_wr_err = 1;
	nitrox_write_csr(ndev, NPS_CORE_INT_ENA_W1S, core_int.value);

	/* NPS packet in ring interrupts */
	nitrox_write_csr(ndev, NPS_PKT_IN_RERR_LO_ENA_W1S, (~0ULL));
	nitrox_write_csr(ndev, NPS_PKT_IN_RERR_HI_ENA_W1S, (~0ULL));
	nitrox_write_csr(ndev, NPS_PKT_IN_ERR_TYPE_ENA_W1S, (~0ULL));
	/* NPS packet slc port interrupts */
	nitrox_write_csr(ndev, NPS_PKT_SLC_RERR_HI_ENA_W1S, (~0ULL));
	nitrox_write_csr(ndev, NPS_PKT_SLC_RERR_LO_ENA_W1S, (~0ULL));
	nitrox_write_csr(ndev, NPS_PKT_SLC_ERR_TYPE_ENA_W1S, (~0uLL));
}

void nitrox_config_nps_unit(struct nitrox_device *ndev)
{
	union nps_core_gbl_vfcfg core_gbl_vfcfg;

	/* endian control information */
	nitrox_write_csr(ndev, NPS_CORE_CONTROL, 1ULL);

	/* disable ILK interface */
	core_gbl_vfcfg.value = 0;
	core_gbl_vfcfg.s.ilk_disable = 1;
	core_gbl_vfcfg.s.cfg = PF_MODE;
	nitrox_write_csr(ndev, NPS_CORE_GBL_VFCFG, core_gbl_vfcfg.value);
	/* config input and solicit ports */
	nitrox_config_pkt_input_rings(ndev);
	nitrox_config_pkt_solicit_ports(ndev);

	/* enable interrupts */
	enable_nps_interrupts(ndev);
}

void nitrox_config_pom_unit(struct nitrox_device *ndev)
{
	union pom_int_ena_w1s pom_int;
	int i;

	/* enable pom interrupts */
	pom_int.value = 0;
	pom_int.s.illegal_dport = 1;
	nitrox_write_csr(ndev, POM_INT_ENA_W1S, pom_int.value);

	/* enable perf counters */
	for (i = 0; i < ndev->hw.se_cores; i++)
		nitrox_write_csr(ndev, POM_PERF_CTL, BIT_ULL(i));
}

/**
 * nitrox_config_rand_unit - enable N5 random number unit
 * @ndev: N5 device
 */
void nitrox_config_rand_unit(struct nitrox_device *ndev)
{
	union efl_rnm_ctl_status efl_rnm_ctl;
	u64 offset;

	offset = EFL_RNM_CTL_STATUS;
	efl_rnm_ctl.value = nitrox_read_csr(ndev, offset);
	efl_rnm_ctl.s.ent_en = 1;
	efl_rnm_ctl.s.rng_en = 1;
	nitrox_write_csr(ndev, offset, efl_rnm_ctl.value);
}

void nitrox_config_efl_unit(struct nitrox_device *ndev)
{
	int i;

	for (i = 0; i < NR_CLUSTERS; i++) {
		union efl_core_int_ena_w1s efl_core_int;
		u64 offset;

		/* EFL core interrupts */
		offset = EFL_CORE_INT_ENA_W1SX(i);
		efl_core_int.value = 0;
		efl_core_int.s.len_ovr = 1;
		efl_core_int.s.d_left = 1;
		efl_core_int.s.epci_decode_err = 1;
		nitrox_write_csr(ndev, offset, efl_core_int.value);

		offset = EFL_CORE_VF_ERR_INT0_ENA_W1SX(i);
		nitrox_write_csr(ndev, offset, (~0ULL));
		offset = EFL_CORE_VF_ERR_INT1_ENA_W1SX(i);
		nitrox_write_csr(ndev, offset, (~0ULL));
	}
}

void nitrox_config_bmi_unit(struct nitrox_device *ndev)
{
	union bmi_ctl bmi_ctl;
	union bmi_int_ena_w1s bmi_int_ena;
	u64 offset;

	/* no threshold limits for PCIe */
	offset = BMI_CTL;
	bmi_ctl.value = nitrox_read_csr(ndev, offset);
	bmi_ctl.s.max_pkt_len = 0xff;
	bmi_ctl.s.nps_free_thrsh = 0xff;
	bmi_ctl.s.nps_hdrq_thrsh = 0x7a;
	nitrox_write_csr(ndev, offset, bmi_ctl.value);

	/* enable interrupts */
	offset = BMI_INT_ENA_W1S;
	bmi_int_ena.value = 0;
	bmi_int_ena.s.max_len_err_nps = 1;
	bmi_int_ena.s.pkt_rcv_err_nps = 1;
	bmi_int_ena.s.fpf_undrrn = 1;
	nitrox_write_csr(ndev, offset, bmi_int_ena.value);
}

void nitrox_config_bmo_unit(struct nitrox_device *ndev)
{
	union bmo_ctl2 bmo_ctl2;
	u64 offset;

	/* no threshold limits for PCIe */
	offset = BMO_CTL2;
	bmo_ctl2.value = nitrox_read_csr(ndev, offset);
	bmo_ctl2.s.nps_slc_buf_thrsh = 0xff;
	nitrox_write_csr(ndev, offset, bmo_ctl2.value);
}

void invalidate_lbc(struct nitrox_device *ndev)
{
	union lbc_inval_ctl lbc_ctl;
	union lbc_inval_status lbc_stat;
	u64 offset;

	/* invalidate LBC */
	offset = LBC_INVAL_CTL;
	lbc_ctl.value = nitrox_read_csr(ndev, offset);
	lbc_ctl.s.cam_inval_start = 1;
	nitrox_write_csr(ndev, offset, lbc_ctl.value);

	offset = LBC_INVAL_STATUS;

	do {
		lbc_stat.value = nitrox_read_csr(ndev, offset);
	} while (!lbc_stat.s.done);
}

void nitrox_config_lbc_unit(struct nitrox_device *ndev)
{
	union lbc_int_ena_w1s lbc_int_ena;
	u64 offset;

	invalidate_lbc(ndev);

	/* enable interrupts */
	offset = LBC_INT_ENA_W1S;
	lbc_int_ena.value = 0;
	lbc_int_ena.s.dma_rd_err = 1;
	lbc_int_ena.s.over_fetch_err = 1;
	lbc_int_ena.s.cam_inval_abort = 1;
	lbc_int_ena.s.cam_hard_err = 1;
	nitrox_write_csr(ndev, offset, lbc_int_ena.value);

	offset = LBC_PLM_VF1_64_INT_ENA_W1S;
	nitrox_write_csr(ndev, offset, (~0ULL));
	offset = LBC_PLM_VF65_128_INT_ENA_W1S;
	nitrox_write_csr(ndev, offset, (~0ULL));

	offset = LBC_ELM_VF1_64_INT_ENA_W1S;
	nitrox_write_csr(ndev, offset, (~0ULL));
	offset = LBC_ELM_VF65_128_INT_ENA_W1S;
	nitrox_write_csr(ndev, offset, (~0ULL));
}
