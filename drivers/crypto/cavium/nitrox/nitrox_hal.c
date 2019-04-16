// SPDX-License-Identifier: GPL-2.0
#include <linux/delay.h>

#include "nitrox_dev.h"
#include "nitrox_csr.h"

#define PLL_REF_CLK 50
#define MAX_CSR_RETRIES 10

/**
 * emu_enable_cores - Enable EMU cluster cores.
 * @ndev: NITROX device
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
 * @ndev: NITROX device
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
	union nps_pkt_in_done_cnts pkt_in_cnts;
	int max_retries = MAX_CSR_RETRIES;
	u64 offset;

	/* step 1: disable the ring, clear enable bit */
	offset = NPS_PKT_IN_INSTR_CTLX(ring);
	pkt_in_ctl.value = nitrox_read_csr(ndev, offset);
	pkt_in_ctl.s.enb = 0;
	nitrox_write_csr(ndev, offset, pkt_in_ctl.value);

	/* step 2: wait to clear [ENB] */
	usleep_range(100, 150);
	do {
		pkt_in_ctl.value = nitrox_read_csr(ndev, offset);
		if (!pkt_in_ctl.s.enb)
			break;
		udelay(50);
	} while (max_retries--);

	/* step 3: clear done counts */
	offset = NPS_PKT_IN_DONE_CNTSX(ring);
	pkt_in_cnts.value = nitrox_read_csr(ndev, offset);
	nitrox_write_csr(ndev, offset, pkt_in_cnts.value);
	usleep_range(50, 100);
}

void enable_pkt_input_ring(struct nitrox_device *ndev, int ring)
{
	union nps_pkt_in_instr_ctl pkt_in_ctl;
	int max_retries = MAX_CSR_RETRIES;
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
		if (pkt_in_ctl.s.enb)
			break;
		udelay(50);
	} while (max_retries--);
}

/**
 * nitrox_config_pkt_input_rings - configure Packet Input Rings
 * @ndev: NITROX device
 */
void nitrox_config_pkt_input_rings(struct nitrox_device *ndev)
{
	int i;

	for (i = 0; i < ndev->nr_queues; i++) {
		struct nitrox_cmdq *cmdq = &ndev->pkt_inq[i];
		union nps_pkt_in_instr_rsize pkt_in_rsize;
		union nps_pkt_in_instr_baoff_dbell pkt_in_dbell;
		u64 offset;

		reset_pkt_input_ring(ndev, i);

		/**
		 * step 4:
		 * configure ring base address 16-byte aligned,
		 * size and interrupt threshold.
		 */
		offset = NPS_PKT_IN_INSTR_BADDRX(i);
		nitrox_write_csr(ndev, offset, cmdq->dma);

		/* configure ring size */
		offset = NPS_PKT_IN_INSTR_RSIZEX(i);
		pkt_in_rsize.value = 0;
		pkt_in_rsize.s.rsize = ndev->qlen;
		nitrox_write_csr(ndev, offset, pkt_in_rsize.value);

		/* set high threshold for pkt input ring interrupts */
		offset = NPS_PKT_IN_INT_LEVELSX(i);
		nitrox_write_csr(ndev, offset, 0xffffffff);

		/* step 5: clear off door bell counts */
		offset = NPS_PKT_IN_INSTR_BAOFF_DBELLX(i);
		pkt_in_dbell.value = 0;
		pkt_in_dbell.s.dbell = 0xffffffff;
		nitrox_write_csr(ndev, offset, pkt_in_dbell.value);

		/* enable the ring */
		enable_pkt_input_ring(ndev, i);
	}
}

static void reset_pkt_solicit_port(struct nitrox_device *ndev, int port)
{
	union nps_pkt_slc_ctl pkt_slc_ctl;
	union nps_pkt_slc_cnts pkt_slc_cnts;
	int max_retries = MAX_CSR_RETRIES;
	u64 offset;

	/* step 1: disable slc port */
	offset = NPS_PKT_SLC_CTLX(port);
	pkt_slc_ctl.value = nitrox_read_csr(ndev, offset);
	pkt_slc_ctl.s.enb = 0;
	nitrox_write_csr(ndev, offset, pkt_slc_ctl.value);

	/* step 2 */
	usleep_range(100, 150);
	/* wait to clear [ENB] */
	do {
		pkt_slc_ctl.value = nitrox_read_csr(ndev, offset);
		if (!pkt_slc_ctl.s.enb)
			break;
		udelay(50);
	} while (max_retries--);

	/* step 3: clear slc counters */
	offset = NPS_PKT_SLC_CNTSX(port);
	pkt_slc_cnts.value = nitrox_read_csr(ndev, offset);
	nitrox_write_csr(ndev, offset, pkt_slc_cnts.value);
	usleep_range(50, 100);
}

void enable_pkt_solicit_port(struct nitrox_device *ndev, int port)
{
	union nps_pkt_slc_ctl pkt_slc_ctl;
	int max_retries = MAX_CSR_RETRIES;
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
		if (pkt_slc_ctl.s.enb)
			break;
		udelay(50);
	} while (max_retries--);
}

static void config_pkt_solicit_port(struct nitrox_device *ndev, int port)
{
	union nps_pkt_slc_int_levels pkt_slc_int;
	u64 offset;

	reset_pkt_solicit_port(ndev, port);

	/* step 4: configure interrupt levels */
	offset = NPS_PKT_SLC_INT_LEVELSX(port);
	pkt_slc_int.value = 0;
	/* time interrupt threshold */
	pkt_slc_int.s.timet = 0x3fffff;
	nitrox_write_csr(ndev, offset, pkt_slc_int.value);

	/* enable the solicit port */
	enable_pkt_solicit_port(ndev, port);
}

void nitrox_config_pkt_solicit_ports(struct nitrox_device *ndev)
{
	int i;

	for (i = 0; i < ndev->nr_queues; i++)
		config_pkt_solicit_port(ndev, i);
}

/**
 * enable_nps_interrupts - enable NPS interrutps
 * @ndev: NITROX device.
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
	core_gbl_vfcfg.s.cfg = __NDEV_MODE_PF;
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
 * nitrox_config_rand_unit - enable NITROX random number unit
 * @ndev: NITROX device
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
	int max_retries = MAX_CSR_RETRIES;
	u64 offset;

	/* invalidate LBC */
	offset = LBC_INVAL_CTL;
	lbc_ctl.value = nitrox_read_csr(ndev, offset);
	lbc_ctl.s.cam_inval_start = 1;
	nitrox_write_csr(ndev, offset, lbc_ctl.value);

	offset = LBC_INVAL_STATUS;
	do {
		lbc_stat.value = nitrox_read_csr(ndev, offset);
		if (lbc_stat.s.done)
			break;
		udelay(50);
	} while (max_retries--);
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

void config_nps_core_vfcfg_mode(struct nitrox_device *ndev, enum vf_mode mode)
{
	union nps_core_gbl_vfcfg vfcfg;

	vfcfg.value = nitrox_read_csr(ndev, NPS_CORE_GBL_VFCFG);
	vfcfg.s.cfg = mode & 0x7;

	nitrox_write_csr(ndev, NPS_CORE_GBL_VFCFG, vfcfg.value);
}

void nitrox_get_hwinfo(struct nitrox_device *ndev)
{
	union emu_fuse_map emu_fuse;
	union rst_boot rst_boot;
	union fus_dat1 fus_dat1;
	unsigned char name[IFNAMSIZ * 2] = {};
	int i, dead_cores;
	u64 offset;

	/* get core frequency */
	offset = RST_BOOT;
	rst_boot.value = nitrox_read_csr(ndev, offset);
	ndev->hw.freq = (rst_boot.pnr_mul + 3) * PLL_REF_CLK;

	for (i = 0; i < NR_CLUSTERS; i++) {
		offset = EMU_FUSE_MAPX(i);
		emu_fuse.value = nitrox_read_csr(ndev, offset);
		if (emu_fuse.s.valid) {
			dead_cores = hweight32(emu_fuse.s.ae_fuse);
			ndev->hw.ae_cores += AE_CORES_PER_CLUSTER - dead_cores;
			dead_cores = hweight16(emu_fuse.s.se_fuse);
			ndev->hw.se_cores += SE_CORES_PER_CLUSTER - dead_cores;
		}
	}
	/* find zip hardware availability */
	offset = FUS_DAT1;
	fus_dat1.value = nitrox_read_csr(ndev, offset);
	if (!fus_dat1.nozip) {
		dead_cores = hweight8(fus_dat1.zip_info);
		ndev->hw.zip_cores = ZIP_MAX_CORES - dead_cores;
	}

	/* determine the partname CNN55<cores>-<freq><pincount>-<rev>*/
	if (ndev->hw.ae_cores == AE_MAX_CORES) {
		switch (ndev->hw.se_cores) {
		case SE_MAX_CORES:
			i = snprintf(name, sizeof(name), "CNN5560");
			break;
		case 40:
			i = snprintf(name, sizeof(name), "CNN5560s");
			break;
		}
	} else if (ndev->hw.ae_cores == (AE_MAX_CORES / 2)) {
		i = snprintf(name, sizeof(name), "CNN5530");
	} else {
		i = snprintf(name, sizeof(name), "CNN5560i");
	}

	snprintf(name + i, sizeof(name) - i, "-%3dBG676-1.%u",
		 ndev->hw.freq, ndev->hw.revision_id);

	/* copy partname */
	strncpy(ndev->hw.partname, name, sizeof(ndev->hw.partname));
}

void enable_pf2vf_mbox_interrupts(struct nitrox_device *ndev)
{
	u64 value = ~0ULL;
	u64 reg_addr;

	/* Mailbox interrupt low enable set register */
	reg_addr = NPS_PKT_MBOX_INT_LO_ENA_W1S;
	nitrox_write_csr(ndev, reg_addr, value);

	/* Mailbox interrupt high enable set register */
	reg_addr = NPS_PKT_MBOX_INT_HI_ENA_W1S;
	nitrox_write_csr(ndev, reg_addr, value);
}

void disable_pf2vf_mbox_interrupts(struct nitrox_device *ndev)
{
	u64 value = ~0ULL;
	u64 reg_addr;

	/* Mailbox interrupt low enable clear register */
	reg_addr = NPS_PKT_MBOX_INT_LO_ENA_W1C;
	nitrox_write_csr(ndev, reg_addr, value);

	/* Mailbox interrupt high enable clear register */
	reg_addr = NPS_PKT_MBOX_INT_HI_ENA_W1C;
	nitrox_write_csr(ndev, reg_addr, value);
}
