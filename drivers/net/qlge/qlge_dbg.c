#include "qlge.h"

#ifdef QL_REG_DUMP
static void ql_dump_intr_states(struct ql_adapter *qdev)
{
	int i;
	u32 value;
	for (i = 0; i < qdev->intr_count; i++) {
		ql_write32(qdev, INTR_EN, qdev->intr_context[i].intr_read_mask);
		value = ql_read32(qdev, INTR_EN);
		printk(KERN_ERR PFX
		       "%s: Interrupt %d is %s.\n",
		       qdev->ndev->name, i,
		       (value & INTR_EN_EN ? "enabled" : "disabled"));
	}
}

void ql_dump_xgmac_control_regs(struct ql_adapter *qdev)
{
	u32 data;
	if (ql_sem_spinlock(qdev, qdev->xg_sem_mask)) {
		printk(KERN_ERR "%s: Couldn't get xgmac sem.\n", __func__);
		return;
	}
	ql_read_xgmac_reg(qdev, PAUSE_SRC_LO, &data);
	printk(KERN_ERR PFX "%s: PAUSE_SRC_LO = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, PAUSE_SRC_HI, &data);
	printk(KERN_ERR PFX "%s: PAUSE_SRC_HI = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, GLOBAL_CFG, &data);
	printk(KERN_ERR PFX "%s: GLOBAL_CFG = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, TX_CFG, &data);
	printk(KERN_ERR PFX "%s: TX_CFG = 0x%.08x.\n", qdev->ndev->name, data);
	ql_read_xgmac_reg(qdev, RX_CFG, &data);
	printk(KERN_ERR PFX "%s: RX_CFG = 0x%.08x.\n", qdev->ndev->name, data);
	ql_read_xgmac_reg(qdev, FLOW_CTL, &data);
	printk(KERN_ERR PFX "%s: FLOW_CTL = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, PAUSE_OPCODE, &data);
	printk(KERN_ERR PFX "%s: PAUSE_OPCODE = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, PAUSE_TIMER, &data);
	printk(KERN_ERR PFX "%s: PAUSE_TIMER = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, PAUSE_FRM_DEST_LO, &data);
	printk(KERN_ERR PFX "%s: PAUSE_FRM_DEST_LO = 0x%.08x.\n",
	       qdev->ndev->name, data);
	ql_read_xgmac_reg(qdev, PAUSE_FRM_DEST_HI, &data);
	printk(KERN_ERR PFX "%s: PAUSE_FRM_DEST_HI = 0x%.08x.\n",
	       qdev->ndev->name, data);
	ql_read_xgmac_reg(qdev, MAC_TX_PARAMS, &data);
	printk(KERN_ERR PFX "%s: MAC_TX_PARAMS = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, MAC_RX_PARAMS, &data);
	printk(KERN_ERR PFX "%s: MAC_RX_PARAMS = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, MAC_SYS_INT, &data);
	printk(KERN_ERR PFX "%s: MAC_SYS_INT = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, MAC_SYS_INT_MASK, &data);
	printk(KERN_ERR PFX "%s: MAC_SYS_INT_MASK = 0x%.08x.\n",
	       qdev->ndev->name, data);
	ql_read_xgmac_reg(qdev, MAC_MGMT_INT, &data);
	printk(KERN_ERR PFX "%s: MAC_MGMT_INT = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, MAC_MGMT_IN_MASK, &data);
	printk(KERN_ERR PFX "%s: MAC_MGMT_IN_MASK = 0x%.08x.\n",
	       qdev->ndev->name, data);
	ql_read_xgmac_reg(qdev, EXT_ARB_MODE, &data);
	printk(KERN_ERR PFX "%s: EXT_ARB_MODE = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_sem_unlock(qdev, qdev->xg_sem_mask);

}

static void ql_dump_ets_regs(struct ql_adapter *qdev)
{
}

static void ql_dump_cam_entries(struct ql_adapter *qdev)
{
	int i;
	u32 value[3];
	for (i = 0; i < 4; i++) {
		if (ql_get_mac_addr_reg(qdev, MAC_ADDR_TYPE_CAM_MAC, i, value)) {
			printk(KERN_ERR PFX
			       "%s: Failed read of mac index register.\n",
			       __func__);
			return;
		} else {
			if (value[0])
				printk(KERN_ERR PFX
				       "%s: CAM index %d CAM Lookup Lower = 0x%.08x:%.08x, Output = 0x%.08x.\n",
				       qdev->ndev->name, i, value[1], value[0],
				       value[2]);
		}
	}
	for (i = 0; i < 32; i++) {
		if (ql_get_mac_addr_reg
		    (qdev, MAC_ADDR_TYPE_MULTI_MAC, i, value)) {
			printk(KERN_ERR PFX
			       "%s: Failed read of mac index register.\n",
			       __func__);
			return;
		} else {
			if (value[0])
				printk(KERN_ERR PFX
				       "%s: MCAST index %d CAM Lookup Lower = 0x%.08x:%.08x.\n",
				       qdev->ndev->name, i, value[1], value[0]);
		}
	}
}

void ql_dump_routing_entries(struct ql_adapter *qdev)
{
	int i;
	u32 value;
	for (i = 0; i < 16; i++) {
		value = 0;
		if (ql_get_routing_reg(qdev, i, &value)) {
			printk(KERN_ERR PFX
			       "%s: Failed read of routing index register.\n",
			       __func__);
			return;
		} else {
			if (value)
				printk(KERN_ERR PFX
				       "%s: Routing Mask %d = 0x%.08x.\n",
				       qdev->ndev->name, i, value);
		}
	}
}

void ql_dump_regs(struct ql_adapter *qdev)
{
	printk(KERN_ERR PFX "reg dump for function #%d.\n", qdev->func);
	printk(KERN_ERR PFX "SYS	 			= 0x%x.\n",
	       ql_read32(qdev, SYS));
	printk(KERN_ERR PFX "RST_FO 			= 0x%x.\n",
	       ql_read32(qdev, RST_FO));
	printk(KERN_ERR PFX "FSC 				= 0x%x.\n",
	       ql_read32(qdev, FSC));
	printk(KERN_ERR PFX "CSR 				= 0x%x.\n",
	       ql_read32(qdev, CSR));
	printk(KERN_ERR PFX "ICB_RID 			= 0x%x.\n",
	       ql_read32(qdev, ICB_RID));
	printk(KERN_ERR PFX "ICB_L 				= 0x%x.\n",
	       ql_read32(qdev, ICB_L));
	printk(KERN_ERR PFX "ICB_H 				= 0x%x.\n",
	       ql_read32(qdev, ICB_H));
	printk(KERN_ERR PFX "CFG 				= 0x%x.\n",
	       ql_read32(qdev, CFG));
	printk(KERN_ERR PFX "BIOS_ADDR 			= 0x%x.\n",
	       ql_read32(qdev, BIOS_ADDR));
	printk(KERN_ERR PFX "STS 				= 0x%x.\n",
	       ql_read32(qdev, STS));
	printk(KERN_ERR PFX "INTR_EN			= 0x%x.\n",
	       ql_read32(qdev, INTR_EN));
	printk(KERN_ERR PFX "INTR_MASK 			= 0x%x.\n",
	       ql_read32(qdev, INTR_MASK));
	printk(KERN_ERR PFX "ISR1 				= 0x%x.\n",
	       ql_read32(qdev, ISR1));
	printk(KERN_ERR PFX "ISR2 				= 0x%x.\n",
	       ql_read32(qdev, ISR2));
	printk(KERN_ERR PFX "ISR3 				= 0x%x.\n",
	       ql_read32(qdev, ISR3));
	printk(KERN_ERR PFX "ISR4 				= 0x%x.\n",
	       ql_read32(qdev, ISR4));
	printk(KERN_ERR PFX "REV_ID 			= 0x%x.\n",
	       ql_read32(qdev, REV_ID));
	printk(KERN_ERR PFX "FRC_ECC_ERR 			= 0x%x.\n",
	       ql_read32(qdev, FRC_ECC_ERR));
	printk(KERN_ERR PFX "ERR_STS 			= 0x%x.\n",
	       ql_read32(qdev, ERR_STS));
	printk(KERN_ERR PFX "RAM_DBG_ADDR 			= 0x%x.\n",
	       ql_read32(qdev, RAM_DBG_ADDR));
	printk(KERN_ERR PFX "RAM_DBG_DATA 			= 0x%x.\n",
	       ql_read32(qdev, RAM_DBG_DATA));
	printk(KERN_ERR PFX "ECC_ERR_CNT 			= 0x%x.\n",
	       ql_read32(qdev, ECC_ERR_CNT));
	printk(KERN_ERR PFX "SEM 				= 0x%x.\n",
	       ql_read32(qdev, SEM));
	printk(KERN_ERR PFX "GPIO_1 			= 0x%x.\n",
	       ql_read32(qdev, GPIO_1));
	printk(KERN_ERR PFX "GPIO_2 			= 0x%x.\n",
	       ql_read32(qdev, GPIO_2));
	printk(KERN_ERR PFX "GPIO_3 			= 0x%x.\n",
	       ql_read32(qdev, GPIO_3));
	printk(KERN_ERR PFX "XGMAC_ADDR 			= 0x%x.\n",
	       ql_read32(qdev, XGMAC_ADDR));
	printk(KERN_ERR PFX "XGMAC_DATA 			= 0x%x.\n",
	       ql_read32(qdev, XGMAC_DATA));
	printk(KERN_ERR PFX "NIC_ETS 			= 0x%x.\n",
	       ql_read32(qdev, NIC_ETS));
	printk(KERN_ERR PFX "CNA_ETS 			= 0x%x.\n",
	       ql_read32(qdev, CNA_ETS));
	printk(KERN_ERR PFX "FLASH_ADDR 			= 0x%x.\n",
	       ql_read32(qdev, FLASH_ADDR));
	printk(KERN_ERR PFX "FLASH_DATA 			= 0x%x.\n",
	       ql_read32(qdev, FLASH_DATA));
	printk(KERN_ERR PFX "CQ_STOP 			= 0x%x.\n",
	       ql_read32(qdev, CQ_STOP));
	printk(KERN_ERR PFX "PAGE_TBL_RID 			= 0x%x.\n",
	       ql_read32(qdev, PAGE_TBL_RID));
	printk(KERN_ERR PFX "WQ_PAGE_TBL_LO 		= 0x%x.\n",
	       ql_read32(qdev, WQ_PAGE_TBL_LO));
	printk(KERN_ERR PFX "WQ_PAGE_TBL_HI 		= 0x%x.\n",
	       ql_read32(qdev, WQ_PAGE_TBL_HI));
	printk(KERN_ERR PFX "CQ_PAGE_TBL_LO 		= 0x%x.\n",
	       ql_read32(qdev, CQ_PAGE_TBL_LO));
	printk(KERN_ERR PFX "CQ_PAGE_TBL_HI 		= 0x%x.\n",
	       ql_read32(qdev, CQ_PAGE_TBL_HI));
	printk(KERN_ERR PFX "COS_DFLT_CQ1 			= 0x%x.\n",
	       ql_read32(qdev, COS_DFLT_CQ1));
	printk(KERN_ERR PFX "COS_DFLT_CQ2 			= 0x%x.\n",
	       ql_read32(qdev, COS_DFLT_CQ2));
	printk(KERN_ERR PFX "SPLT_HDR 			= 0x%x.\n",
	       ql_read32(qdev, SPLT_HDR));
	printk(KERN_ERR PFX "FC_PAUSE_THRES 		= 0x%x.\n",
	       ql_read32(qdev, FC_PAUSE_THRES));
	printk(KERN_ERR PFX "NIC_PAUSE_THRES 		= 0x%x.\n",
	       ql_read32(qdev, NIC_PAUSE_THRES));
	printk(KERN_ERR PFX "FC_ETHERTYPE 			= 0x%x.\n",
	       ql_read32(qdev, FC_ETHERTYPE));
	printk(KERN_ERR PFX "FC_RCV_CFG 			= 0x%x.\n",
	       ql_read32(qdev, FC_RCV_CFG));
	printk(KERN_ERR PFX "NIC_RCV_CFG 			= 0x%x.\n",
	       ql_read32(qdev, NIC_RCV_CFG));
	printk(KERN_ERR PFX "FC_COS_TAGS 			= 0x%x.\n",
	       ql_read32(qdev, FC_COS_TAGS));
	printk(KERN_ERR PFX "NIC_COS_TAGS 			= 0x%x.\n",
	       ql_read32(qdev, NIC_COS_TAGS));
	printk(KERN_ERR PFX "MGMT_RCV_CFG 			= 0x%x.\n",
	       ql_read32(qdev, MGMT_RCV_CFG));
	printk(KERN_ERR PFX "XG_SERDES_ADDR 		= 0x%x.\n",
	       ql_read32(qdev, XG_SERDES_ADDR));
	printk(KERN_ERR PFX "XG_SERDES_DATA 		= 0x%x.\n",
	       ql_read32(qdev, XG_SERDES_DATA));
	printk(KERN_ERR PFX "PRB_MX_ADDR 			= 0x%x.\n",
	       ql_read32(qdev, PRB_MX_ADDR));
	printk(KERN_ERR PFX "PRB_MX_DATA 			= 0x%x.\n",
	       ql_read32(qdev, PRB_MX_DATA));
	ql_dump_intr_states(qdev);
	ql_dump_xgmac_control_regs(qdev);
	ql_dump_ets_regs(qdev);
	ql_dump_cam_entries(qdev);
	ql_dump_routing_entries(qdev);
}
#endif

#ifdef QL_STAT_DUMP
void ql_dump_stat(struct ql_adapter *qdev)
{
	printk(KERN_ERR "%s: Enter.\n", __func__);
	printk(KERN_ERR "tx_pkts = %ld\n",
	       (unsigned long)qdev->nic_stats.tx_pkts);
	printk(KERN_ERR "tx_bytes = %ld\n",
	       (unsigned long)qdev->nic_stats.tx_bytes);
	printk(KERN_ERR "tx_mcast_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_mcast_pkts);
	printk(KERN_ERR "tx_bcast_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_bcast_pkts);
	printk(KERN_ERR "tx_ucast_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_ucast_pkts);
	printk(KERN_ERR "tx_ctl_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_ctl_pkts);
	printk(KERN_ERR "tx_pause_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_pause_pkts);
	printk(KERN_ERR "tx_64_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_64_pkt);
	printk(KERN_ERR "tx_65_to_127_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_65_to_127_pkt);
	printk(KERN_ERR "tx_128_to_255_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_128_to_255_pkt);
	printk(KERN_ERR "tx_256_511_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_256_511_pkt);
	printk(KERN_ERR "tx_512_to_1023_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_512_to_1023_pkt);
	printk(KERN_ERR "tx_1024_to_1518_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_1024_to_1518_pkt);
	printk(KERN_ERR "tx_1519_to_max_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_1519_to_max_pkt);
	printk(KERN_ERR "tx_undersize_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_undersize_pkt);
	printk(KERN_ERR "tx_oversize_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_oversize_pkt);
	printk(KERN_ERR "rx_bytes = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_bytes);
	printk(KERN_ERR "rx_bytes_ok = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_bytes_ok);
	printk(KERN_ERR "rx_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_pkts);
	printk(KERN_ERR "rx_pkts_ok = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_pkts_ok);
	printk(KERN_ERR "rx_bcast_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_bcast_pkts);
	printk(KERN_ERR "rx_mcast_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_mcast_pkts);
	printk(KERN_ERR "rx_ucast_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_ucast_pkts);
	printk(KERN_ERR "rx_undersize_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_undersize_pkts);
	printk(KERN_ERR "rx_oversize_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_oversize_pkts);
	printk(KERN_ERR "rx_jabber_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_jabber_pkts);
	printk(KERN_ERR "rx_undersize_fcerr_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_undersize_fcerr_pkts);
	printk(KERN_ERR "rx_drop_events = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_drop_events);
	printk(KERN_ERR "rx_fcerr_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_fcerr_pkts);
	printk(KERN_ERR "rx_align_err = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_align_err);
	printk(KERN_ERR "rx_symbol_err = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_symbol_err);
	printk(KERN_ERR "rx_mac_err = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_mac_err);
	printk(KERN_ERR "rx_ctl_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_ctl_pkts);
	printk(KERN_ERR "rx_pause_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_pause_pkts);
	printk(KERN_ERR "rx_64_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_64_pkts);
	printk(KERN_ERR "rx_65_to_127_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_65_to_127_pkts);
	printk(KERN_ERR "rx_128_255_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_128_255_pkts);
	printk(KERN_ERR "rx_256_511_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_256_511_pkts);
	printk(KERN_ERR "rx_512_to_1023_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_512_to_1023_pkts);
	printk(KERN_ERR "rx_1024_to_1518_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_1024_to_1518_pkts);
	printk(KERN_ERR "rx_1519_to_max_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_1519_to_max_pkts);
	printk(KERN_ERR "rx_len_err_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_len_err_pkts);
};
#endif

#ifdef QL_DEV_DUMP
void ql_dump_qdev(struct ql_adapter *qdev)
{
	int i;
	printk(KERN_ERR PFX "qdev->flags 			= %lx.\n",
	       qdev->flags);
	printk(KERN_ERR PFX "qdev->vlgrp 			= %p.\n",
	       qdev->vlgrp);
	printk(KERN_ERR PFX "qdev->pdev 			= %p.\n",
	       qdev->pdev);
	printk(KERN_ERR PFX "qdev->ndev 			= %p.\n",
	       qdev->ndev);
	printk(KERN_ERR PFX "qdev->chip_rev_id 		= %d.\n",
	       qdev->chip_rev_id);
	printk(KERN_ERR PFX "qdev->reg_base 		= %p.\n",
	       qdev->reg_base);
	printk(KERN_ERR PFX "qdev->doorbell_area 	= %p.\n",
	       qdev->doorbell_area);
	printk(KERN_ERR PFX "qdev->doorbell_area_size 	= %d.\n",
	       qdev->doorbell_area_size);
	printk(KERN_ERR PFX "msg_enable 		= %x.\n",
	       qdev->msg_enable);
	printk(KERN_ERR PFX "qdev->rx_ring_shadow_reg_area	= %p.\n",
	       qdev->rx_ring_shadow_reg_area);
	printk(KERN_ERR PFX "qdev->rx_ring_shadow_reg_dma 	= %p.\n",
	       (void *)qdev->rx_ring_shadow_reg_dma);
	printk(KERN_ERR PFX "qdev->tx_ring_shadow_reg_area	= %p.\n",
	       qdev->tx_ring_shadow_reg_area);
	printk(KERN_ERR PFX "qdev->tx_ring_shadow_reg_dma	= %p.\n",
	       (void *)qdev->tx_ring_shadow_reg_dma);
	printk(KERN_ERR PFX "qdev->intr_count 		= %d.\n",
	       qdev->intr_count);
	if (qdev->msi_x_entry)
		for (i = 0; i < qdev->intr_count; i++) {
			printk(KERN_ERR PFX
			       "msi_x_entry.[%d]vector	= %d.\n", i,
			       qdev->msi_x_entry[i].vector);
			printk(KERN_ERR PFX
			       "msi_x_entry.[%d]entry	= %d.\n", i,
			       qdev->msi_x_entry[i].entry);
		}
	for (i = 0; i < qdev->intr_count; i++) {
		printk(KERN_ERR PFX
		       "intr_context[%d].qdev		= %p.\n", i,
		       qdev->intr_context[i].qdev);
		printk(KERN_ERR PFX
		       "intr_context[%d].intr		= %d.\n", i,
		       qdev->intr_context[i].intr);
		printk(KERN_ERR PFX
		       "intr_context[%d].hooked		= %d.\n", i,
		       qdev->intr_context[i].hooked);
		printk(KERN_ERR PFX
		       "intr_context[%d].intr_en_mask	= 0x%08x.\n", i,
		       qdev->intr_context[i].intr_en_mask);
		printk(KERN_ERR PFX
		       "intr_context[%d].intr_dis_mask	= 0x%08x.\n", i,
		       qdev->intr_context[i].intr_dis_mask);
		printk(KERN_ERR PFX
		       "intr_context[%d].intr_read_mask	= 0x%08x.\n", i,
		       qdev->intr_context[i].intr_read_mask);
	}
	printk(KERN_ERR PFX "qdev->tx_ring_count = %d.\n", qdev->tx_ring_count);
	printk(KERN_ERR PFX "qdev->rx_ring_count = %d.\n", qdev->rx_ring_count);
	printk(KERN_ERR PFX "qdev->ring_mem_size = %d.\n", qdev->ring_mem_size);
	printk(KERN_ERR PFX "qdev->ring_mem 	= %p.\n", qdev->ring_mem);
	printk(KERN_ERR PFX "qdev->intr_count 	= %d.\n", qdev->intr_count);
	printk(KERN_ERR PFX "qdev->tx_ring		= %p.\n",
	       qdev->tx_ring);
	printk(KERN_ERR PFX "qdev->rss_ring_first_cq_id 	= %d.\n",
	       qdev->rss_ring_first_cq_id);
	printk(KERN_ERR PFX "qdev->rss_ring_count 	= %d.\n",
	       qdev->rss_ring_count);
	printk(KERN_ERR PFX "qdev->rx_ring	= %p.\n", qdev->rx_ring);
	printk(KERN_ERR PFX "qdev->default_rx_queue	= %d.\n",
	       qdev->default_rx_queue);
	printk(KERN_ERR PFX "qdev->xg_sem_mask		= 0x%08x.\n",
	       qdev->xg_sem_mask);
	printk(KERN_ERR PFX "qdev->port_link_up		= 0x%08x.\n",
	       qdev->port_link_up);
	printk(KERN_ERR PFX "qdev->port_init		= 0x%08x.\n",
	       qdev->port_init);

}
#endif

#ifdef QL_CB_DUMP
void ql_dump_wqicb(struct wqicb *wqicb)
{
	printk(KERN_ERR PFX "Dumping wqicb stuff...\n");
	printk(KERN_ERR PFX "wqicb->len = 0x%x.\n", le16_to_cpu(wqicb->len));
	printk(KERN_ERR PFX "wqicb->flags = %x.\n", le16_to_cpu(wqicb->flags));
	printk(KERN_ERR PFX "wqicb->cq_id_rss = %d.\n",
	       le16_to_cpu(wqicb->cq_id_rss));
	printk(KERN_ERR PFX "wqicb->rid = 0x%x.\n", le16_to_cpu(wqicb->rid));
	printk(KERN_ERR PFX "wqicb->wq_addr_lo = 0x%.08x.\n",
	       le32_to_cpu(wqicb->addr_lo));
	printk(KERN_ERR PFX "wqicb->wq_addr_hi = 0x%.08x.\n",
	       le32_to_cpu(wqicb->addr_hi));
	printk(KERN_ERR PFX "wqicb->wq_cnsmr_idx_addr_lo = 0x%.08x.\n",
	       le32_to_cpu(wqicb->cnsmr_idx_addr_lo));
	printk(KERN_ERR PFX "wqicb->wq_cnsmr_idx_addr_hi = 0x%.08x.\n",
	       le32_to_cpu(wqicb->cnsmr_idx_addr_hi));
}

void ql_dump_tx_ring(struct tx_ring *tx_ring)
{
	if (tx_ring == NULL)
		return;
	printk(KERN_ERR PFX
	       "===================== Dumping tx_ring %d ===============.\n",
	       tx_ring->wq_id);
	printk(KERN_ERR PFX "tx_ring->base = %p.\n", tx_ring->wq_base);
	printk(KERN_ERR PFX "tx_ring->base_dma = 0x%llx.\n",
	       (u64) tx_ring->wq_base_dma);
	printk(KERN_ERR PFX "tx_ring->cnsmr_idx_sh_reg = %p.\n",
	       tx_ring->cnsmr_idx_sh_reg);
	printk(KERN_ERR PFX "tx_ring->cnsmr_idx_sh_reg_dma = 0x%llx.\n",
	       (u64) tx_ring->cnsmr_idx_sh_reg_dma);
	printk(KERN_ERR PFX "tx_ring->size = %d.\n", tx_ring->wq_size);
	printk(KERN_ERR PFX "tx_ring->len = %d.\n", tx_ring->wq_len);
	printk(KERN_ERR PFX "tx_ring->prod_idx_db_reg = %p.\n",
	       tx_ring->prod_idx_db_reg);
	printk(KERN_ERR PFX "tx_ring->valid_db_reg = %p.\n",
	       tx_ring->valid_db_reg);
	printk(KERN_ERR PFX "tx_ring->prod_idx = %d.\n", tx_ring->prod_idx);
	printk(KERN_ERR PFX "tx_ring->cq_id = %d.\n", tx_ring->cq_id);
	printk(KERN_ERR PFX "tx_ring->wq_id = %d.\n", tx_ring->wq_id);
	printk(KERN_ERR PFX "tx_ring->q = %p.\n", tx_ring->q);
	printk(KERN_ERR PFX "tx_ring->tx_count = %d.\n",
	       atomic_read(&tx_ring->tx_count));
}

void ql_dump_ricb(struct ricb *ricb)
{
	int i;
	printk(KERN_ERR PFX
	       "===================== Dumping ricb ===============.\n");
	printk(KERN_ERR PFX "Dumping ricb stuff...\n");

	printk(KERN_ERR PFX "ricb->base_cq = %d.\n", ricb->base_cq & 0x1f);
	printk(KERN_ERR PFX "ricb->flags = %s%s%s%s%s%s%s%s%s.\n",
	       ricb->base_cq & RSS_L4K ? "RSS_L4K " : "",
	       ricb->flags & RSS_L6K ? "RSS_L6K " : "",
	       ricb->flags & RSS_LI ? "RSS_LI " : "",
	       ricb->flags & RSS_LB ? "RSS_LB " : "",
	       ricb->flags & RSS_LM ? "RSS_LM " : "",
	       ricb->flags & RSS_RI4 ? "RSS_RI4 " : "",
	       ricb->flags & RSS_RT4 ? "RSS_RT4 " : "",
	       ricb->flags & RSS_RI6 ? "RSS_RI6 " : "",
	       ricb->flags & RSS_RT6 ? "RSS_RT6 " : "");
	printk(KERN_ERR PFX "ricb->mask = 0x%.04x.\n", le16_to_cpu(ricb->mask));
	for (i = 0; i < 16; i++)
		printk(KERN_ERR PFX "ricb->hash_cq_id[%d] = 0x%.08x.\n", i,
		       le32_to_cpu(ricb->hash_cq_id[i]));
	for (i = 0; i < 10; i++)
		printk(KERN_ERR PFX "ricb->ipv6_hash_key[%d] = 0x%.08x.\n", i,
		       le32_to_cpu(ricb->ipv6_hash_key[i]));
	for (i = 0; i < 4; i++)
		printk(KERN_ERR PFX "ricb->ipv4_hash_key[%d] = 0x%.08x.\n", i,
		       le32_to_cpu(ricb->ipv4_hash_key[i]));
}

void ql_dump_cqicb(struct cqicb *cqicb)
{
	printk(KERN_ERR PFX "Dumping cqicb stuff...\n");

	printk(KERN_ERR PFX "cqicb->msix_vect = %d.\n", cqicb->msix_vect);
	printk(KERN_ERR PFX "cqicb->flags = %x.\n", cqicb->flags);
	printk(KERN_ERR PFX "cqicb->len = %d.\n", le16_to_cpu(cqicb->len));
	printk(KERN_ERR PFX "cqicb->addr_lo = %x.\n",
	       le32_to_cpu(cqicb->addr_lo));
	printk(KERN_ERR PFX "cqicb->addr_hi = %x.\n",
	       le32_to_cpu(cqicb->addr_hi));
	printk(KERN_ERR PFX "cqicb->prod_idx_addr_lo = %x.\n",
	       le32_to_cpu(cqicb->prod_idx_addr_lo));
	printk(KERN_ERR PFX "cqicb->prod_idx_addr_hi = %x.\n",
	       le32_to_cpu(cqicb->prod_idx_addr_hi));
	printk(KERN_ERR PFX "cqicb->pkt_delay = 0x%.04x.\n",
	       le16_to_cpu(cqicb->pkt_delay));
	printk(KERN_ERR PFX "cqicb->irq_delay = 0x%.04x.\n",
	       le16_to_cpu(cqicb->irq_delay));
	printk(KERN_ERR PFX "cqicb->lbq_addr_lo = %x.\n",
	       le32_to_cpu(cqicb->lbq_addr_lo));
	printk(KERN_ERR PFX "cqicb->lbq_addr_hi = %x.\n",
	       le32_to_cpu(cqicb->lbq_addr_hi));
	printk(KERN_ERR PFX "cqicb->lbq_buf_size = 0x%.04x.\n",
	       le16_to_cpu(cqicb->lbq_buf_size));
	printk(KERN_ERR PFX "cqicb->lbq_len = 0x%.04x.\n",
	       le16_to_cpu(cqicb->lbq_len));
	printk(KERN_ERR PFX "cqicb->sbq_addr_lo = %x.\n",
	       le32_to_cpu(cqicb->sbq_addr_lo));
	printk(KERN_ERR PFX "cqicb->sbq_addr_hi = %x.\n",
	       le32_to_cpu(cqicb->sbq_addr_hi));
	printk(KERN_ERR PFX "cqicb->sbq_buf_size = 0x%.04x.\n",
	       le16_to_cpu(cqicb->sbq_buf_size));
	printk(KERN_ERR PFX "cqicb->sbq_len = 0x%.04x.\n",
	       le16_to_cpu(cqicb->sbq_len));
}

void ql_dump_rx_ring(struct rx_ring *rx_ring)
{
	if (rx_ring == NULL)
		return;
	printk(KERN_ERR PFX
	       "===================== Dumping rx_ring %d ===============.\n",
	       rx_ring->cq_id);
	printk(KERN_ERR PFX "Dumping rx_ring %d, type = %s%s%s.\n",
	       rx_ring->cq_id, rx_ring->type == DEFAULT_Q ? "DEFAULT" : "",
	       rx_ring->type == TX_Q ? "OUTBOUND COMPLETIONS" : "",
	       rx_ring->type == RX_Q ? "INBOUND_COMPLETIONS" : "");
	printk(KERN_ERR PFX "rx_ring->cqicb = %p.\n", &rx_ring->cqicb);
	printk(KERN_ERR PFX "rx_ring->cq_base = %p.\n", rx_ring->cq_base);
	printk(KERN_ERR PFX "rx_ring->cq_base_dma = %llx.\n",
	       (u64) rx_ring->cq_base_dma);
	printk(KERN_ERR PFX "rx_ring->cq_size = %d.\n", rx_ring->cq_size);
	printk(KERN_ERR PFX "rx_ring->cq_len = %d.\n", rx_ring->cq_len);
	printk(KERN_ERR PFX
	       "rx_ring->prod_idx_sh_reg, addr = %p, value = %d.\n",
	       rx_ring->prod_idx_sh_reg,
	       rx_ring->prod_idx_sh_reg ? *(rx_ring->prod_idx_sh_reg) : 0);
	printk(KERN_ERR PFX "rx_ring->prod_idx_sh_reg_dma = %llx.\n",
	       (u64) rx_ring->prod_idx_sh_reg_dma);
	printk(KERN_ERR PFX "rx_ring->cnsmr_idx_db_reg = %p.\n",
	       rx_ring->cnsmr_idx_db_reg);
	printk(KERN_ERR PFX "rx_ring->cnsmr_idx = %d.\n", rx_ring->cnsmr_idx);
	printk(KERN_ERR PFX "rx_ring->curr_entry = %p.\n", rx_ring->curr_entry);
	printk(KERN_ERR PFX "rx_ring->valid_db_reg = %p.\n",
	       rx_ring->valid_db_reg);

	printk(KERN_ERR PFX "rx_ring->lbq_base = %p.\n", rx_ring->lbq_base);
	printk(KERN_ERR PFX "rx_ring->lbq_base_dma = %llx.\n",
	       (u64) rx_ring->lbq_base_dma);
	printk(KERN_ERR PFX "rx_ring->lbq_base_indirect = %p.\n",
	       rx_ring->lbq_base_indirect);
	printk(KERN_ERR PFX "rx_ring->lbq_base_indirect_dma = %llx.\n",
	       (u64) rx_ring->lbq_base_indirect_dma);
	printk(KERN_ERR PFX "rx_ring->lbq = %p.\n", rx_ring->lbq);
	printk(KERN_ERR PFX "rx_ring->lbq_len = %d.\n", rx_ring->lbq_len);
	printk(KERN_ERR PFX "rx_ring->lbq_size = %d.\n", rx_ring->lbq_size);
	printk(KERN_ERR PFX "rx_ring->lbq_prod_idx_db_reg = %p.\n",
	       rx_ring->lbq_prod_idx_db_reg);
	printk(KERN_ERR PFX "rx_ring->lbq_prod_idx = %d.\n",
	       rx_ring->lbq_prod_idx);
	printk(KERN_ERR PFX "rx_ring->lbq_curr_idx = %d.\n",
	       rx_ring->lbq_curr_idx);
	printk(KERN_ERR PFX "rx_ring->lbq_clean_idx = %d.\n",
	       rx_ring->lbq_clean_idx);
	printk(KERN_ERR PFX "rx_ring->lbq_free_cnt = %d.\n",
	       rx_ring->lbq_free_cnt);
	printk(KERN_ERR PFX "rx_ring->lbq_buf_size = %d.\n",
	       rx_ring->lbq_buf_size);

	printk(KERN_ERR PFX "rx_ring->sbq_base = %p.\n", rx_ring->sbq_base);
	printk(KERN_ERR PFX "rx_ring->sbq_base_dma = %llx.\n",
	       (u64) rx_ring->sbq_base_dma);
	printk(KERN_ERR PFX "rx_ring->sbq_base_indirect = %p.\n",
	       rx_ring->sbq_base_indirect);
	printk(KERN_ERR PFX "rx_ring->sbq_base_indirect_dma = %llx.\n",
	       (u64) rx_ring->sbq_base_indirect_dma);
	printk(KERN_ERR PFX "rx_ring->sbq = %p.\n", rx_ring->sbq);
	printk(KERN_ERR PFX "rx_ring->sbq_len = %d.\n", rx_ring->sbq_len);
	printk(KERN_ERR PFX "rx_ring->sbq_size = %d.\n", rx_ring->sbq_size);
	printk(KERN_ERR PFX "rx_ring->sbq_prod_idx_db_reg addr = %p.\n",
	       rx_ring->sbq_prod_idx_db_reg);
	printk(KERN_ERR PFX "rx_ring->sbq_prod_idx = %d.\n",
	       rx_ring->sbq_prod_idx);
	printk(KERN_ERR PFX "rx_ring->sbq_curr_idx = %d.\n",
	       rx_ring->sbq_curr_idx);
	printk(KERN_ERR PFX "rx_ring->sbq_clean_idx = %d.\n",
	       rx_ring->sbq_clean_idx);
	printk(KERN_ERR PFX "rx_ring->sbq_free_cnt = %d.\n",
	       rx_ring->sbq_free_cnt);
	printk(KERN_ERR PFX "rx_ring->sbq_buf_size = %d.\n",
	       rx_ring->sbq_buf_size);
	printk(KERN_ERR PFX "rx_ring->cq_id = %d.\n", rx_ring->cq_id);
	printk(KERN_ERR PFX "rx_ring->irq = %d.\n", rx_ring->irq);
	printk(KERN_ERR PFX "rx_ring->cpu = %d.\n", rx_ring->cpu);
	printk(KERN_ERR PFX "rx_ring->qdev = %p.\n", rx_ring->qdev);
}

void ql_dump_hw_cb(struct ql_adapter *qdev, int size, u32 bit, u16 q_id)
{
	void *ptr;

	printk(KERN_ERR PFX "%s: Enter.\n", __func__);

	ptr = kmalloc(size, GFP_ATOMIC);
	if (ptr == NULL) {
		printk(KERN_ERR PFX "%s: Couldn't allocate a buffer.\n",
		       __func__);
		return;
	}

	if (ql_write_cfg(qdev, ptr, size, bit, q_id)) {
		printk(KERN_ERR "%s: Failed to upload control block!\n",
		       __func__);
		goto fail_it;
	}
	switch (bit) {
	case CFG_DRQ:
		ql_dump_wqicb((struct wqicb *)ptr);
		break;
	case CFG_DCQ:
		ql_dump_cqicb((struct cqicb *)ptr);
		break;
	case CFG_DR:
		ql_dump_ricb((struct ricb *)ptr);
		break;
	default:
		printk(KERN_ERR PFX "%s: Invalid bit value = %x.\n",
		       __func__, bit);
		break;
	}
fail_it:
	kfree(ptr);
}
#endif

#ifdef QL_OB_DUMP
void ql_dump_tx_desc(struct tx_buf_desc *tbd)
{
	printk(KERN_ERR PFX "tbd->addr  = 0x%llx\n",
	       le64_to_cpu((u64) tbd->addr));
	printk(KERN_ERR PFX "tbd->len   = %d\n",
	       le32_to_cpu(tbd->len & TX_DESC_LEN_MASK));
	printk(KERN_ERR PFX "tbd->flags = %s %s\n",
	       tbd->len & TX_DESC_C ? "C" : ".",
	       tbd->len & TX_DESC_E ? "E" : ".");
	tbd++;
	printk(KERN_ERR PFX "tbd->addr  = 0x%llx\n",
	       le64_to_cpu((u64) tbd->addr));
	printk(KERN_ERR PFX "tbd->len   = %d\n",
	       le32_to_cpu(tbd->len & TX_DESC_LEN_MASK));
	printk(KERN_ERR PFX "tbd->flags = %s %s\n",
	       tbd->len & TX_DESC_C ? "C" : ".",
	       tbd->len & TX_DESC_E ? "E" : ".");
	tbd++;
	printk(KERN_ERR PFX "tbd->addr  = 0x%llx\n",
	       le64_to_cpu((u64) tbd->addr));
	printk(KERN_ERR PFX "tbd->len   = %d\n",
	       le32_to_cpu(tbd->len & TX_DESC_LEN_MASK));
	printk(KERN_ERR PFX "tbd->flags = %s %s\n",
	       tbd->len & TX_DESC_C ? "C" : ".",
	       tbd->len & TX_DESC_E ? "E" : ".");

}

void ql_dump_ob_mac_iocb(struct ob_mac_iocb_req *ob_mac_iocb)
{
	struct ob_mac_tso_iocb_req *ob_mac_tso_iocb =
	    (struct ob_mac_tso_iocb_req *)ob_mac_iocb;
	struct tx_buf_desc *tbd;
	u16 frame_len;

	printk(KERN_ERR PFX "%s\n", __func__);
	printk(KERN_ERR PFX "opcode         = %s\n",
	       (ob_mac_iocb->opcode == OPCODE_OB_MAC_IOCB) ? "MAC" : "TSO");
	printk(KERN_ERR PFX "flags1          = %s %s %s %s %s\n",
	       ob_mac_tso_iocb->flags1 & OB_MAC_TSO_IOCB_OI ? "OI" : "",
	       ob_mac_tso_iocb->flags1 & OB_MAC_TSO_IOCB_I ? "I" : "",
	       ob_mac_tso_iocb->flags1 & OB_MAC_TSO_IOCB_D ? "D" : "",
	       ob_mac_tso_iocb->flags1 & OB_MAC_TSO_IOCB_IP4 ? "IP4" : "",
	       ob_mac_tso_iocb->flags1 & OB_MAC_TSO_IOCB_IP6 ? "IP6" : "");
	printk(KERN_ERR PFX "flags2          = %s %s %s\n",
	       ob_mac_tso_iocb->flags2 & OB_MAC_TSO_IOCB_LSO ? "LSO" : "",
	       ob_mac_tso_iocb->flags2 & OB_MAC_TSO_IOCB_UC ? "UC" : "",
	       ob_mac_tso_iocb->flags2 & OB_MAC_TSO_IOCB_TC ? "TC" : "");
	printk(KERN_ERR PFX "flags3          = %s %s %s \n",
	       ob_mac_tso_iocb->flags3 & OB_MAC_TSO_IOCB_IC ? "IC" : "",
	       ob_mac_tso_iocb->flags3 & OB_MAC_TSO_IOCB_DFP ? "DFP" : "",
	       ob_mac_tso_iocb->flags3 & OB_MAC_TSO_IOCB_V ? "V" : "");
	printk(KERN_ERR PFX "tid = %x\n", ob_mac_iocb->tid);
	printk(KERN_ERR PFX "txq_idx = %d\n", ob_mac_iocb->txq_idx);
	printk(KERN_ERR PFX "vlan_tci      = %x\n", ob_mac_tso_iocb->vlan_tci);
	if (ob_mac_iocb->opcode == OPCODE_OB_MAC_TSO_IOCB) {
		printk(KERN_ERR PFX "frame_len      = %d\n",
		       le32_to_cpu(ob_mac_tso_iocb->frame_len));
		printk(KERN_ERR PFX "mss      = %d\n",
		       le16_to_cpu(ob_mac_tso_iocb->mss));
		printk(KERN_ERR PFX "prot_hdr_len   = %d\n",
		       le16_to_cpu(ob_mac_tso_iocb->total_hdrs_len));
		printk(KERN_ERR PFX "hdr_offset     = 0x%.04x\n",
		       le16_to_cpu(ob_mac_tso_iocb->net_trans_offset));
		frame_len = le32_to_cpu(ob_mac_tso_iocb->frame_len);
	} else {
		printk(KERN_ERR PFX "frame_len      = %d\n",
		       le16_to_cpu(ob_mac_iocb->frame_len));
		frame_len = le16_to_cpu(ob_mac_iocb->frame_len);
	}
	tbd = &ob_mac_iocb->tbd[0];
	ql_dump_tx_desc(tbd);
}

void ql_dump_ob_mac_rsp(struct ob_mac_iocb_rsp *ob_mac_rsp)
{
	printk(KERN_ERR PFX "%s\n", __func__);
	printk(KERN_ERR PFX "opcode         = %d\n", ob_mac_rsp->opcode);
	printk(KERN_ERR PFX "flags          = %s %s %s %s %s %s %s\n",
	       ob_mac_rsp->flags1 & OB_MAC_IOCB_RSP_OI ? "OI" : ".",
	       ob_mac_rsp->flags1 & OB_MAC_IOCB_RSP_I ? "I" : ".",
	       ob_mac_rsp->flags1 & OB_MAC_IOCB_RSP_E ? "E" : ".",
	       ob_mac_rsp->flags1 & OB_MAC_IOCB_RSP_S ? "S" : ".",
	       ob_mac_rsp->flags1 & OB_MAC_IOCB_RSP_L ? "L" : ".",
	       ob_mac_rsp->flags1 & OB_MAC_IOCB_RSP_P ? "P" : ".",
	       ob_mac_rsp->flags2 & OB_MAC_IOCB_RSP_B ? "B" : ".");
	printk(KERN_ERR PFX "tid = %x\n", ob_mac_rsp->tid);
}
#endif

#ifdef QL_IB_DUMP
void ql_dump_ib_mac_rsp(struct ib_mac_iocb_rsp *ib_mac_rsp)
{
	printk(KERN_ERR PFX "%s\n", __func__);
	printk(KERN_ERR PFX "opcode         = 0x%x\n", ib_mac_rsp->opcode);
	printk(KERN_ERR PFX "flags1 = %s%s%s%s%s%s\n",
	       ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_OI ? "OI " : "",
	       ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_I ? "I " : "",
	       ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_TE ? "TE " : "",
	       ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_NU ? "NU " : "",
	       ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_IE ? "IE " : "",
	       ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_B ? "B " : "");

	if (ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_M_MASK)
		printk(KERN_ERR PFX "%s%s%s Multicast.\n",
		       (ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_M_MASK) ==
		       IB_MAC_IOCB_RSP_M_HASH ? "Hash" : "",
		       (ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_M_MASK) ==
		       IB_MAC_IOCB_RSP_M_REG ? "Registered" : "",
		       (ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_M_MASK) ==
		       IB_MAC_IOCB_RSP_M_PROM ? "Promiscuous" : "");

	printk(KERN_ERR PFX "flags2 = %s%s%s%s%s\n",
	       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_P) ? "P " : "",
	       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_V) ? "V " : "",
	       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_U) ? "U " : "",
	       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_T) ? "T " : "",
	       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_FO) ? "FO " : "");

	if (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_ERR_MASK)
		printk(KERN_ERR PFX "%s%s%s%s%s error.\n",
		       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_ERR_MASK) ==
		       IB_MAC_IOCB_RSP_ERR_OVERSIZE ? "oversize" : "",
		       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_ERR_MASK) ==
		       IB_MAC_IOCB_RSP_ERR_UNDERSIZE ? "undersize" : "",
		       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_ERR_MASK) ==
		       IB_MAC_IOCB_RSP_ERR_PREAMBLE ? "preamble" : "",
		       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_ERR_MASK) ==
		       IB_MAC_IOCB_RSP_ERR_FRAME_LEN ? "frame length" : "",
		       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_ERR_MASK) ==
		       IB_MAC_IOCB_RSP_ERR_CRC ? "CRC" : "");

	printk(KERN_ERR PFX "flags3 = %s%s.\n",
	       ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_DS ? "DS " : "",
	       ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_DL ? "DL " : "");

	if (ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_RSS_MASK)
		printk(KERN_ERR PFX "RSS flags = %s%s%s%s.\n",
		       ((ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_RSS_MASK) ==
			IB_MAC_IOCB_RSP_M_IPV4) ? "IPv4 RSS" : "",
		       ((ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_RSS_MASK) ==
			IB_MAC_IOCB_RSP_M_IPV6) ? "IPv6 RSS " : "",
		       ((ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_RSS_MASK) ==
			IB_MAC_IOCB_RSP_M_TCP_V4) ? "TCP/IPv4 RSS" : "",
		       ((ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_RSS_MASK) ==
			IB_MAC_IOCB_RSP_M_TCP_V6) ? "TCP/IPv6 RSS" : "");

	printk(KERN_ERR PFX "data_len	= %d\n",
	       le32_to_cpu(ib_mac_rsp->data_len));
	printk(KERN_ERR PFX "data_addr_hi    = 0x%x\n",
	       le32_to_cpu(ib_mac_rsp->data_addr_hi));
	printk(KERN_ERR PFX "data_addr_lo    = 0x%x\n",
	       le32_to_cpu(ib_mac_rsp->data_addr_lo));
	if (ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_RSS_MASK)
		printk(KERN_ERR PFX "rss    = %x\n",
		       le32_to_cpu(ib_mac_rsp->rss));
	if (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_V)
		printk(KERN_ERR PFX "vlan_id    = %x\n",
		       le16_to_cpu(ib_mac_rsp->vlan_id));

	printk(KERN_ERR PFX "flags4 = %s%s%s.\n",
	       le32_to_cpu(ib_mac_rsp->
			   flags4) & IB_MAC_IOCB_RSP_HV ? "HV " : "",
	       le32_to_cpu(ib_mac_rsp->
			   flags4) & IB_MAC_IOCB_RSP_HS ? "HS " : "",
	       le32_to_cpu(ib_mac_rsp->
			   flags4) & IB_MAC_IOCB_RSP_HL ? "HL " : "");

	if (le32_to_cpu(ib_mac_rsp->flags4) & IB_MAC_IOCB_RSP_HV) {
		printk(KERN_ERR PFX "hdr length	= %d.\n",
		       le32_to_cpu(ib_mac_rsp->hdr_len));
		printk(KERN_ERR PFX "hdr addr_hi    = 0x%x.\n",
		       le32_to_cpu(ib_mac_rsp->hdr_addr_hi));
		printk(KERN_ERR PFX "hdr addr_lo    = 0x%x.\n",
		       le32_to_cpu(ib_mac_rsp->hdr_addr_lo));
	}
}
#endif

#ifdef QL_ALL_DUMP
void ql_dump_all(struct ql_adapter *qdev)
{
	int i;

	QL_DUMP_REGS(qdev);
	QL_DUMP_QDEV(qdev);
	for (i = 0; i < qdev->tx_ring_count; i++) {
		QL_DUMP_TX_RING(&qdev->tx_ring[i]);
		QL_DUMP_WQICB((struct wqicb *)&qdev->tx_ring[i]);
	}
	for (i = 0; i < qdev->rx_ring_count; i++) {
		QL_DUMP_RX_RING(&qdev->rx_ring[i]);
		QL_DUMP_CQICB((struct cqicb *)&qdev->rx_ring[i]);
	}
}
#endif
