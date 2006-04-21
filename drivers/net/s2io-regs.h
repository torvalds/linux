/************************************************************************
 * regs.h: A Linux PCI-X Ethernet driver for Neterion 10GbE Server NIC
 * Copyright(c) 2002-2005 Neterion Inc.

 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 ************************************************************************/
#ifndef _REGS_H
#define _REGS_H

#define TBD 0

typedef struct _XENA_dev_config {
/* Convention: mHAL_XXX is mask, vHAL_XXX is value */

/* General Control-Status Registers */
	u64 general_int_status;
#define GEN_INTR_TXPIC             BIT(0)
#define GEN_INTR_TXDMA             BIT(1)
#define GEN_INTR_TXMAC             BIT(2)
#define GEN_INTR_TXXGXS            BIT(3)
#define GEN_INTR_TXTRAFFIC         BIT(8)
#define GEN_INTR_RXPIC             BIT(32)
#define GEN_INTR_RXDMA             BIT(33)
#define GEN_INTR_RXMAC             BIT(34)
#define GEN_INTR_MC                BIT(35)
#define GEN_INTR_RXXGXS            BIT(36)
#define GEN_INTR_RXTRAFFIC         BIT(40)
#define GEN_ERROR_INTR             GEN_INTR_TXPIC | GEN_INTR_RXPIC | \
                                   GEN_INTR_TXDMA | GEN_INTR_RXDMA | \
                                   GEN_INTR_TXMAC | GEN_INTR_RXMAC | \
                                   GEN_INTR_TXXGXS| GEN_INTR_RXXGXS| \
                                   GEN_INTR_MC

	u64 general_int_mask;

	u8 unused0[0x100 - 0x10];

	u64 sw_reset;
/* XGXS must be removed from reset only once. */
#define SW_RESET_XENA              vBIT(0xA5,0,8)
#define SW_RESET_FLASH             vBIT(0xA5,8,8)
#define SW_RESET_EOI               vBIT(0xA5,16,8)
#define SW_RESET_ALL               (SW_RESET_XENA     |   \
                                    SW_RESET_FLASH    |   \
                                    SW_RESET_EOI)
/* The SW_RESET register must read this value after a successful reset. */
#define	SW_RESET_RAW_VAL			0xA5000000


	u64 adapter_status;
#define ADAPTER_STATUS_TDMA_READY          BIT(0)
#define ADAPTER_STATUS_RDMA_READY          BIT(1)
#define ADAPTER_STATUS_PFC_READY           BIT(2)
#define ADAPTER_STATUS_TMAC_BUF_EMPTY      BIT(3)
#define ADAPTER_STATUS_PIC_QUIESCENT       BIT(5)
#define ADAPTER_STATUS_RMAC_REMOTE_FAULT   BIT(6)
#define ADAPTER_STATUS_RMAC_LOCAL_FAULT    BIT(7)
#define ADAPTER_STATUS_RMAC_PCC_IDLE       vBIT(0xFF,8,8)
#define ADAPTER_STATUS_RMAC_PCC_FOUR_IDLE  vBIT(0x0F,8,8)
#define ADAPTER_STATUS_RC_PRC_QUIESCENT    vBIT(0xFF,16,8)
#define ADAPTER_STATUS_MC_DRAM_READY       BIT(24)
#define ADAPTER_STATUS_MC_QUEUES_READY     BIT(25)
#define ADAPTER_STATUS_M_PLL_LOCK          BIT(30)
#define ADAPTER_STATUS_P_PLL_LOCK          BIT(31)

	u64 adapter_control;
#define ADAPTER_CNTL_EN                    BIT(7)
#define ADAPTER_EOI_TX_ON                  BIT(15)
#define ADAPTER_LED_ON                     BIT(23)
#define ADAPTER_UDPI(val)                  vBIT(val,36,4)
#define ADAPTER_WAIT_INT                   BIT(48)
#define ADAPTER_ECC_EN                     BIT(55)

	u64 serr_source;
#define SERR_SOURCE_PIC			BIT(0)
#define SERR_SOURCE_TXDMA		BIT(1)
#define SERR_SOURCE_RXDMA		BIT(2)
#define SERR_SOURCE_MAC                 BIT(3)
#define SERR_SOURCE_MC                  BIT(4)
#define SERR_SOURCE_XGXS                BIT(5)
#define	SERR_SOURCE_ANY			(SERR_SOURCE_PIC	| \
					SERR_SOURCE_TXDMA	| \
					SERR_SOURCE_RXDMA	| \
					SERR_SOURCE_MAC		| \
					SERR_SOURCE_MC		| \
					SERR_SOURCE_XGXS)

	u64 pci_mode;
#define	GET_PCI_MODE(val)		((val & vBIT(0xF, 0, 4)) >> 60)
#define	PCI_MODE_PCI_33			0
#define	PCI_MODE_PCI_66			0x1
#define	PCI_MODE_PCIX_M1_66		0x2
#define	PCI_MODE_PCIX_M1_100		0x3
#define	PCI_MODE_PCIX_M1_133		0x4
#define	PCI_MODE_PCIX_M2_66		0x5
#define	PCI_MODE_PCIX_M2_100		0x6
#define	PCI_MODE_PCIX_M2_133		0x7
#define	PCI_MODE_UNSUPPORTED		BIT(0)
#define	PCI_MODE_32_BITS		BIT(8)
#define	PCI_MODE_UNKNOWN_MODE		BIT(9)

	u8 unused_0[0x800 - 0x128];

/* PCI-X Controller registers */
	u64 pic_int_status;
	u64 pic_int_mask;
#define PIC_INT_TX                     BIT(0)
#define PIC_INT_FLSH                   BIT(1)
#define PIC_INT_MDIO                   BIT(2)
#define PIC_INT_IIC                    BIT(3)
#define PIC_INT_GPIO                   BIT(4)
#define PIC_INT_RX                     BIT(32)

	u64 txpic_int_reg;
	u64 txpic_int_mask;
#define PCIX_INT_REG_ECC_SG_ERR                BIT(0)
#define PCIX_INT_REG_ECC_DB_ERR                BIT(1)
#define PCIX_INT_REG_FLASHR_R_FSM_ERR          BIT(8)
#define PCIX_INT_REG_FLASHR_W_FSM_ERR          BIT(9)
#define PCIX_INT_REG_INI_TX_FSM_SERR           BIT(10)
#define PCIX_INT_REG_INI_TXO_FSM_ERR           BIT(11)
#define PCIX_INT_REG_TRT_FSM_SERR              BIT(13)
#define PCIX_INT_REG_SRT_FSM_SERR              BIT(14)
#define PCIX_INT_REG_PIFR_FSM_SERR             BIT(15)
#define PCIX_INT_REG_WRC_TX_SEND_FSM_SERR      BIT(21)
#define PCIX_INT_REG_RRC_TX_REQ_FSM_SERR       BIT(23)
#define PCIX_INT_REG_INI_RX_FSM_SERR           BIT(48)
#define PCIX_INT_REG_RA_RX_FSM_SERR            BIT(50)
/*
#define PCIX_INT_REG_WRC_RX_SEND_FSM_SERR      BIT(52)
#define PCIX_INT_REG_RRC_RX_REQ_FSM_SERR       BIT(54)
#define PCIX_INT_REG_RRC_RX_SPLIT_FSM_SERR     BIT(58)
*/
	u64 txpic_alarms;
	u64 rxpic_int_reg;
	u64 rxpic_int_mask;
	u64 rxpic_alarms;

	u64 flsh_int_reg;
	u64 flsh_int_mask;
#define PIC_FLSH_INT_REG_CYCLE_FSM_ERR         BIT(63)
#define PIC_FLSH_INT_REG_ERR                   BIT(62)
	u64 flash_alarms;

	u64 mdio_int_reg;
	u64 mdio_int_mask;
#define MDIO_INT_REG_MDIO_BUS_ERR              BIT(0)
#define MDIO_INT_REG_DTX_BUS_ERR               BIT(8)
#define MDIO_INT_REG_LASI                      BIT(39)
	u64 mdio_alarms;

	u64 iic_int_reg;
	u64 iic_int_mask;
#define IIC_INT_REG_BUS_FSM_ERR                BIT(4)
#define IIC_INT_REG_BIT_FSM_ERR                BIT(5)
#define IIC_INT_REG_CYCLE_FSM_ERR              BIT(6)
#define IIC_INT_REG_REQ_FSM_ERR                BIT(7)
#define IIC_INT_REG_ACK_ERR                    BIT(8)
	u64 iic_alarms;

	u8 unused4[0x08];

	u64 gpio_int_reg;
#define GPIO_INT_REG_DP_ERR_INT                BIT(0)
#define GPIO_INT_REG_LINK_DOWN                 BIT(1)
#define GPIO_INT_REG_LINK_UP                   BIT(2)
	u64 gpio_int_mask;
#define GPIO_INT_MASK_LINK_DOWN                BIT(1)
#define GPIO_INT_MASK_LINK_UP                  BIT(2)
	u64 gpio_alarms;

	u8 unused5[0x38];

	u64 tx_traffic_int;
#define TX_TRAFFIC_INT_n(n)                    BIT(n)
	u64 tx_traffic_mask;

	u64 rx_traffic_int;
#define RX_TRAFFIC_INT_n(n)                    BIT(n)
	u64 rx_traffic_mask;

/* PIC Control registers */
	u64 pic_control;
#define PIC_CNTL_RX_ALARM_MAP_1                BIT(0)
#define PIC_CNTL_SHARED_SPLITS(n)              vBIT(n,11,5)

	u64 swapper_ctrl;
#define SWAPPER_CTRL_PIF_R_FE                  BIT(0)
#define SWAPPER_CTRL_PIF_R_SE                  BIT(1)
#define SWAPPER_CTRL_PIF_W_FE                  BIT(8)
#define SWAPPER_CTRL_PIF_W_SE                  BIT(9)
#define SWAPPER_CTRL_TXP_FE                    BIT(16)
#define SWAPPER_CTRL_TXP_SE                    BIT(17)
#define SWAPPER_CTRL_TXD_R_FE                  BIT(18)
#define SWAPPER_CTRL_TXD_R_SE                  BIT(19)
#define SWAPPER_CTRL_TXD_W_FE                  BIT(20)
#define SWAPPER_CTRL_TXD_W_SE                  BIT(21)
#define SWAPPER_CTRL_TXF_R_FE                  BIT(22)
#define SWAPPER_CTRL_TXF_R_SE                  BIT(23)
#define SWAPPER_CTRL_RXD_R_FE                  BIT(32)
#define SWAPPER_CTRL_RXD_R_SE                  BIT(33)
#define SWAPPER_CTRL_RXD_W_FE                  BIT(34)
#define SWAPPER_CTRL_RXD_W_SE                  BIT(35)
#define SWAPPER_CTRL_RXF_W_FE                  BIT(36)
#define SWAPPER_CTRL_RXF_W_SE                  BIT(37)
#define SWAPPER_CTRL_XMSI_FE                   BIT(40)
#define SWAPPER_CTRL_XMSI_SE                   BIT(41)
#define SWAPPER_CTRL_STATS_FE                  BIT(48)
#define SWAPPER_CTRL_STATS_SE                  BIT(49)

	u64 pif_rd_swapper_fb;
#define IF_RD_SWAPPER_FB                            0x0123456789ABCDEF

	u64 scheduled_int_ctrl;
#define SCHED_INT_CTRL_TIMER_EN                BIT(0)
#define SCHED_INT_CTRL_ONE_SHOT                BIT(1)
#define SCHED_INT_CTRL_INT2MSI                 TBD
#define SCHED_INT_PERIOD                       TBD

	u64 txreqtimeout;
#define TXREQTO_VAL(val)						vBIT(val,0,32)
#define TXREQTO_EN								BIT(63)

	u64 statsreqtimeout;
#define STATREQTO_VAL(n)                       TBD
#define STATREQTO_EN                           BIT(63)

	u64 read_retry_delay;
	u64 read_retry_acceleration;
	u64 write_retry_delay;
	u64 write_retry_acceleration;

	u64 xmsi_control;
	u64 xmsi_access;
	u64 xmsi_address;
	u64 xmsi_data;

	u64 rx_mat;
#define RX_MAT_SET(ring, msi)			vBIT(msi, (8 * ring), 8)

	u8 unused6[0x8];

	u64 tx_mat0_n[0x8];
#define TX_MAT_SET(fifo, msi)			vBIT(msi, (8 * fifo), 8)

	u8 unused_1[0x8];
	u64 stat_byte_cnt;
#define STAT_BC(n)                              vBIT(n,4,12)

	/* Automated statistics collection */
	u64 stat_cfg;
#define STAT_CFG_STAT_EN           BIT(0)
#define STAT_CFG_ONE_SHOT_EN       BIT(1)
#define STAT_CFG_STAT_NS_EN        BIT(8)
#define STAT_CFG_STAT_RO           BIT(9)
#define STAT_TRSF_PER(n)           TBD
#define	PER_SEC					   0x208d5
#define	SET_UPDT_PERIOD(n)		   vBIT((PER_SEC*n),32,32)
#define	SET_UPDT_CLICKS(val)		   vBIT(val, 32, 32)

	u64 stat_addr;

	/* General Configuration */
	u64 mdio_control;
#define MDIO_MMD_INDX_ADDR(val)		vBIT(val, 0, 16)
#define MDIO_MMD_DEV_ADDR(val)		vBIT(val, 19, 5)
#define MDIO_MMD_PMA_DEV_ADDR		0x1
#define MDIO_MMD_PMD_DEV_ADDR		0x1
#define MDIO_MMD_WIS_DEV_ADDR		0x2
#define MDIO_MMD_PCS_DEV_ADDR		0x3
#define MDIO_MMD_PHYXS_DEV_ADDR		0x4
#define MDIO_MMS_PRT_ADDR(val)		vBIT(val, 27, 5)
#define MDIO_CTRL_START_TRANS(val)	vBIT(val, 56, 4)
#define MDIO_OP(val)			vBIT(val, 60, 2)
#define MDIO_OP_ADDR_TRANS		0x0
#define MDIO_OP_WRITE_TRANS		0x1
#define MDIO_OP_READ_POST_INC_TRANS	0x2
#define MDIO_OP_READ_TRANS		0x3
#define MDIO_MDIO_DATA(val)		vBIT(val, 32, 16)

	u64 dtx_control;

	u64 i2c_control;
#define	I2C_CONTROL_DEV_ID(id)		vBIT(id,1,3)
#define	I2C_CONTROL_ADDR(addr)		vBIT(addr,5,11)
#define	I2C_CONTROL_BYTE_CNT(cnt)	vBIT(cnt,22,2)
#define	I2C_CONTROL_READ			BIT(24)
#define	I2C_CONTROL_NACK			BIT(25)
#define	I2C_CONTROL_CNTL_START		vBIT(0xE,28,4)
#define	I2C_CONTROL_CNTL_END(val)	(val & vBIT(0x1,28,4))
#define	I2C_CONTROL_GET_DATA(val)	(u32)(val & 0xFFFFFFFF)
#define	I2C_CONTROL_SET_DATA(val)	vBIT(val,32,32)

	u64 gpio_control;
#define GPIO_CTRL_GPIO_0		BIT(8)
	u64 misc_control;
#define EXT_REQ_EN			BIT(1)
#define MISC_LINK_STABILITY_PRD(val)   vBIT(val,29,3)

	u8 unused7_1[0x230 - 0x208];

	u64 pic_control2;
	u64 ini_dperr_ctrl;

	u64 wreq_split_mask;
#define	WREQ_SPLIT_MASK_SET_MASK(val)	vBIT(val, 52, 12)

	u8 unused7_2[0x800 - 0x248];

/* TxDMA registers */
	u64 txdma_int_status;
	u64 txdma_int_mask;
#define TXDMA_PFC_INT                  BIT(0)
#define TXDMA_TDA_INT                  BIT(1)
#define TXDMA_PCC_INT                  BIT(2)
#define TXDMA_TTI_INT                  BIT(3)
#define TXDMA_LSO_INT                  BIT(4)
#define TXDMA_TPA_INT                  BIT(5)
#define TXDMA_SM_INT                   BIT(6)
	u64 pfc_err_reg;
	u64 pfc_err_mask;
	u64 pfc_err_alarm;

	u64 tda_err_reg;
	u64 tda_err_mask;
	u64 tda_err_alarm;

	u64 pcc_err_reg;
#define PCC_FB_ECC_DB_ERR		vBIT(0xFF, 16, 8)
#define PCC_ENABLE_FOUR			vBIT(0x0F,0,8)

	u64 pcc_err_mask;
	u64 pcc_err_alarm;

	u64 tti_err_reg;
	u64 tti_err_mask;
	u64 tti_err_alarm;

	u64 lso_err_reg;
	u64 lso_err_mask;
	u64 lso_err_alarm;

	u64 tpa_err_reg;
	u64 tpa_err_mask;
	u64 tpa_err_alarm;

	u64 sm_err_reg;
	u64 sm_err_mask;
	u64 sm_err_alarm;

	u8 unused8[0x100 - 0xB8];

/* TxDMA arbiter */
	u64 tx_dma_wrap_stat;

/* Tx FIFO controller */
#define X_MAX_FIFOS                        8
#define X_FIFO_MAX_LEN                     0x1FFF	/*8191 */
	u64 tx_fifo_partition_0;
#define TX_FIFO_PARTITION_EN               BIT(0)
#define TX_FIFO_PARTITION_0_PRI(val)       vBIT(val,5,3)
#define TX_FIFO_PARTITION_0_LEN(val)       vBIT(val,19,13)
#define TX_FIFO_PARTITION_1_PRI(val)       vBIT(val,37,3)
#define TX_FIFO_PARTITION_1_LEN(val)       vBIT(val,51,13  )

	u64 tx_fifo_partition_1;
#define TX_FIFO_PARTITION_2_PRI(val)       vBIT(val,5,3)
#define TX_FIFO_PARTITION_2_LEN(val)       vBIT(val,19,13)
#define TX_FIFO_PARTITION_3_PRI(val)       vBIT(val,37,3)
#define TX_FIFO_PARTITION_3_LEN(val)       vBIT(val,51,13)

	u64 tx_fifo_partition_2;
#define TX_FIFO_PARTITION_4_PRI(val)       vBIT(val,5,3)
#define TX_FIFO_PARTITION_4_LEN(val)       vBIT(val,19,13)
#define TX_FIFO_PARTITION_5_PRI(val)       vBIT(val,37,3)
#define TX_FIFO_PARTITION_5_LEN(val)       vBIT(val,51,13)

	u64 tx_fifo_partition_3;
#define TX_FIFO_PARTITION_6_PRI(val)       vBIT(val,5,3)
#define TX_FIFO_PARTITION_6_LEN(val)       vBIT(val,19,13)
#define TX_FIFO_PARTITION_7_PRI(val)       vBIT(val,37,3)
#define TX_FIFO_PARTITION_7_LEN(val)       vBIT(val,51,13)

#define TX_FIFO_PARTITION_PRI_0                 0	/* highest */
#define TX_FIFO_PARTITION_PRI_1                 1
#define TX_FIFO_PARTITION_PRI_2                 2
#define TX_FIFO_PARTITION_PRI_3                 3
#define TX_FIFO_PARTITION_PRI_4                 4
#define TX_FIFO_PARTITION_PRI_5                 5
#define TX_FIFO_PARTITION_PRI_6                 6
#define TX_FIFO_PARTITION_PRI_7                 7	/* lowest */

	u64 tx_w_round_robin_0;
	u64 tx_w_round_robin_1;
	u64 tx_w_round_robin_2;
	u64 tx_w_round_robin_3;
	u64 tx_w_round_robin_4;

	u64 tti_command_mem;
#define TTI_CMD_MEM_WE                     BIT(7)
#define TTI_CMD_MEM_STROBE_NEW_CMD         BIT(15)
#define TTI_CMD_MEM_STROBE_BEING_EXECUTED  BIT(15)
#define TTI_CMD_MEM_OFFSET(n)              vBIT(n,26,6)

	u64 tti_data1_mem;
#define TTI_DATA1_MEM_TX_TIMER_VAL(n)      vBIT(n,6,26)
#define TTI_DATA1_MEM_TX_TIMER_AC_CI(n)    vBIT(n,38,2)
#define TTI_DATA1_MEM_TX_TIMER_AC_EN       BIT(38)
#define TTI_DATA1_MEM_TX_TIMER_CI_EN       BIT(39)
#define TTI_DATA1_MEM_TX_URNG_A(n)         vBIT(n,41,7)
#define TTI_DATA1_MEM_TX_URNG_B(n)         vBIT(n,49,7)
#define TTI_DATA1_MEM_TX_URNG_C(n)         vBIT(n,57,7)

	u64 tti_data2_mem;
#define TTI_DATA2_MEM_TX_UFC_A(n)          vBIT(n,0,16)
#define TTI_DATA2_MEM_TX_UFC_B(n)          vBIT(n,16,16)
#define TTI_DATA2_MEM_TX_UFC_C(n)          vBIT(n,32,16)
#define TTI_DATA2_MEM_TX_UFC_D(n)          vBIT(n,48,16)

/* Tx Protocol assist */
	u64 tx_pa_cfg;
#define TX_PA_CFG_IGNORE_FRM_ERR           BIT(1)
#define TX_PA_CFG_IGNORE_SNAP_OUI          BIT(2)
#define TX_PA_CFG_IGNORE_LLC_CTRL          BIT(3)
#define	TX_PA_CFG_IGNORE_L2_ERR			   BIT(6)

/* Recent add, used only debug purposes. */
	u64 pcc_enable;

	u8 unused9[0x700 - 0x178];

	u64 txdma_debug_ctrl;

	u8 unused10[0x1800 - 0x1708];

/* RxDMA Registers */
	u64 rxdma_int_status;
	u64 rxdma_int_mask;
#define RXDMA_INT_RC_INT_M             BIT(0)
#define RXDMA_INT_RPA_INT_M            BIT(1)
#define RXDMA_INT_RDA_INT_M            BIT(2)
#define RXDMA_INT_RTI_INT_M            BIT(3)

	u64 rda_err_reg;
	u64 rda_err_mask;
	u64 rda_err_alarm;

	u64 rc_err_reg;
	u64 rc_err_mask;
	u64 rc_err_alarm;

	u64 prc_pcix_err_reg;
	u64 prc_pcix_err_mask;
	u64 prc_pcix_err_alarm;

	u64 rpa_err_reg;
	u64 rpa_err_mask;
	u64 rpa_err_alarm;

	u64 rti_err_reg;
	u64 rti_err_mask;
	u64 rti_err_alarm;

	u8 unused11[0x100 - 0x88];

/* DMA arbiter */
	u64 rx_queue_priority;
#define RX_QUEUE_0_PRIORITY(val)       vBIT(val,5,3)
#define RX_QUEUE_1_PRIORITY(val)       vBIT(val,13,3)
#define RX_QUEUE_2_PRIORITY(val)       vBIT(val,21,3)
#define RX_QUEUE_3_PRIORITY(val)       vBIT(val,29,3)
#define RX_QUEUE_4_PRIORITY(val)       vBIT(val,37,3)
#define RX_QUEUE_5_PRIORITY(val)       vBIT(val,45,3)
#define RX_QUEUE_6_PRIORITY(val)       vBIT(val,53,3)
#define RX_QUEUE_7_PRIORITY(val)       vBIT(val,61,3)

#define RX_QUEUE_PRI_0                 0	/* highest */
#define RX_QUEUE_PRI_1                 1
#define RX_QUEUE_PRI_2                 2
#define RX_QUEUE_PRI_3                 3
#define RX_QUEUE_PRI_4                 4
#define RX_QUEUE_PRI_5                 5
#define RX_QUEUE_PRI_6                 6
#define RX_QUEUE_PRI_7                 7	/* lowest */

	u64 rx_w_round_robin_0;
	u64 rx_w_round_robin_1;
	u64 rx_w_round_robin_2;
	u64 rx_w_round_robin_3;
	u64 rx_w_round_robin_4;

	/* Per-ring controller regs */
#define RX_MAX_RINGS                8
#if 0
#define RX_MAX_RINGS_SZ             0xFFFF	/* 65536 */
#define RX_MIN_RINGS_SZ             0x3F	/* 63 */
#endif
	u64 prc_rxd0_n[RX_MAX_RINGS];
	u64 prc_ctrl_n[RX_MAX_RINGS];
#define PRC_CTRL_RC_ENABLED                    BIT(7)
#define PRC_CTRL_RING_MODE                     (BIT(14)|BIT(15))
#define PRC_CTRL_RING_MODE_1                   vBIT(0,14,2)
#define PRC_CTRL_RING_MODE_3                   vBIT(1,14,2)
#define PRC_CTRL_RING_MODE_5                   vBIT(2,14,2)
#define PRC_CTRL_RING_MODE_x                   vBIT(3,14,2)
#define PRC_CTRL_NO_SNOOP                      (BIT(22)|BIT(23))
#define PRC_CTRL_NO_SNOOP_DESC                 BIT(22)
#define PRC_CTRL_NO_SNOOP_BUFF                 BIT(23)
#define PRC_CTRL_BIMODAL_INTERRUPT             BIT(37)
#define PRC_CTRL_GROUP_READS                   BIT(38)
#define PRC_CTRL_RXD_BACKOFF_INTERVAL(val)     vBIT(val,40,24)

	u64 prc_alarm_action;
#define PRC_ALARM_ACTION_RR_R0_STOP            BIT(3)
#define PRC_ALARM_ACTION_RW_R0_STOP            BIT(7)
#define PRC_ALARM_ACTION_RR_R1_STOP            BIT(11)
#define PRC_ALARM_ACTION_RW_R1_STOP            BIT(15)
#define PRC_ALARM_ACTION_RR_R2_STOP            BIT(19)
#define PRC_ALARM_ACTION_RW_R2_STOP            BIT(23)
#define PRC_ALARM_ACTION_RR_R3_STOP            BIT(27)
#define PRC_ALARM_ACTION_RW_R3_STOP            BIT(31)
#define PRC_ALARM_ACTION_RR_R4_STOP            BIT(35)
#define PRC_ALARM_ACTION_RW_R4_STOP            BIT(39)
#define PRC_ALARM_ACTION_RR_R5_STOP            BIT(43)
#define PRC_ALARM_ACTION_RW_R5_STOP            BIT(47)
#define PRC_ALARM_ACTION_RR_R6_STOP            BIT(51)
#define PRC_ALARM_ACTION_RW_R6_STOP            BIT(55)
#define PRC_ALARM_ACTION_RR_R7_STOP            BIT(59)
#define PRC_ALARM_ACTION_RW_R7_STOP            BIT(63)

/* Receive traffic interrupts */
	u64 rti_command_mem;
#define RTI_CMD_MEM_WE                          BIT(7)
#define RTI_CMD_MEM_STROBE                      BIT(15)
#define RTI_CMD_MEM_STROBE_NEW_CMD              BIT(15)
#define RTI_CMD_MEM_STROBE_CMD_BEING_EXECUTED   BIT(15)
#define RTI_CMD_MEM_OFFSET(n)                   vBIT(n,29,3)

	u64 rti_data1_mem;
#define RTI_DATA1_MEM_RX_TIMER_VAL(n)      vBIT(n,3,29)
#define RTI_DATA1_MEM_RX_TIMER_AC_EN       BIT(38)
#define RTI_DATA1_MEM_RX_TIMER_CI_EN       BIT(39)
#define RTI_DATA1_MEM_RX_URNG_A(n)         vBIT(n,41,7)
#define RTI_DATA1_MEM_RX_URNG_B(n)         vBIT(n,49,7)
#define RTI_DATA1_MEM_RX_URNG_C(n)         vBIT(n,57,7)

	u64 rti_data2_mem;
#define RTI_DATA2_MEM_RX_UFC_A(n)          vBIT(n,0,16)
#define RTI_DATA2_MEM_RX_UFC_B(n)          vBIT(n,16,16)
#define RTI_DATA2_MEM_RX_UFC_C(n)          vBIT(n,32,16)
#define RTI_DATA2_MEM_RX_UFC_D(n)          vBIT(n,48,16)

	u64 rx_pa_cfg;
#define RX_PA_CFG_IGNORE_FRM_ERR           BIT(1)
#define RX_PA_CFG_IGNORE_SNAP_OUI          BIT(2)
#define RX_PA_CFG_IGNORE_LLC_CTRL          BIT(3)
#define RX_PA_CFG_IGNORE_L2_ERR            BIT(6)

	u64 unused_11_1;

	u64 ring_bump_counter1;
	u64 ring_bump_counter2;

	u8 unused12[0x700 - 0x1F0];

	u64 rxdma_debug_ctrl;

	u8 unused13[0x2000 - 0x1f08];

/* Media Access Controller Register */
	u64 mac_int_status;
	u64 mac_int_mask;
#define MAC_INT_STATUS_TMAC_INT            BIT(0)
#define MAC_INT_STATUS_RMAC_INT            BIT(1)

	u64 mac_tmac_err_reg;
#define TMAC_ERR_REG_TMAC_ECC_DB_ERR       BIT(15)
#define TMAC_ERR_REG_TMAC_TX_BUF_OVRN      BIT(23)
#define TMAC_ERR_REG_TMAC_TX_CRI_ERR       BIT(31)
	u64 mac_tmac_err_mask;
	u64 mac_tmac_err_alarm;

	u64 mac_rmac_err_reg;
#define RMAC_ERR_REG_RX_BUFF_OVRN          BIT(0)
#define RMAC_ERR_REG_RTS_ECC_DB_ERR        BIT(14)
#define RMAC_ERR_REG_ECC_DB_ERR            BIT(15)
#define RMAC_LINK_STATE_CHANGE_INT         BIT(31)
	u64 mac_rmac_err_mask;
	u64 mac_rmac_err_alarm;

	u8 unused14[0x100 - 0x40];

	u64 mac_cfg;
#define MAC_CFG_TMAC_ENABLE             BIT(0)
#define MAC_CFG_RMAC_ENABLE             BIT(1)
#define MAC_CFG_LAN_NOT_WAN             BIT(2)
#define MAC_CFG_TMAC_LOOPBACK           BIT(3)
#define MAC_CFG_TMAC_APPEND_PAD         BIT(4)
#define MAC_CFG_RMAC_STRIP_FCS          BIT(5)
#define MAC_CFG_RMAC_STRIP_PAD          BIT(6)
#define MAC_CFG_RMAC_PROM_ENABLE        BIT(7)
#define MAC_RMAC_DISCARD_PFRM           BIT(8)
#define MAC_RMAC_BCAST_ENABLE           BIT(9)
#define MAC_RMAC_ALL_ADDR_ENABLE        BIT(10)
#define MAC_RMAC_INVLD_IPG_THR(val)     vBIT(val,16,8)

	u64 tmac_avg_ipg;
#define TMAC_AVG_IPG(val)           vBIT(val,0,8)

	u64 rmac_max_pyld_len;
#define RMAC_MAX_PYLD_LEN(val)      vBIT(val,2,14)
#define RMAC_MAX_PYLD_LEN_DEF       vBIT(1500,2,14)
#define RMAC_MAX_PYLD_LEN_JUMBO_DEF vBIT(9600,2,14)

	u64 rmac_err_cfg;
#define RMAC_ERR_FCS                    BIT(0)
#define RMAC_ERR_FCS_ACCEPT             BIT(1)
#define RMAC_ERR_TOO_LONG               BIT(1)
#define RMAC_ERR_TOO_LONG_ACCEPT        BIT(1)
#define RMAC_ERR_RUNT                   BIT(2)
#define RMAC_ERR_RUNT_ACCEPT            BIT(2)
#define RMAC_ERR_LEN_MISMATCH           BIT(3)
#define RMAC_ERR_LEN_MISMATCH_ACCEPT    BIT(3)

	u64 rmac_cfg_key;
#define RMAC_CFG_KEY(val)               vBIT(val,0,16)

#define MAX_MAC_ADDRESSES           16
#define MAX_MC_ADDRESSES            32	/* Multicast addresses */
#define MAC_MAC_ADDR_START_OFFSET   0
#define MAC_MC_ADDR_START_OFFSET    16
#define MAC_MC_ALL_MC_ADDR_OFFSET   63	/* enables all multicast pkts */
	u64 rmac_addr_cmd_mem;
#define RMAC_ADDR_CMD_MEM_WE                    BIT(7)
#define RMAC_ADDR_CMD_MEM_RD                    0
#define RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD        BIT(15)
#define RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING  BIT(15)
#define RMAC_ADDR_CMD_MEM_OFFSET(n)             vBIT(n,26,6)

	u64 rmac_addr_data0_mem;
#define RMAC_ADDR_DATA0_MEM_ADDR(n)    vBIT(n,0,48)
#define RMAC_ADDR_DATA0_MEM_USER       BIT(48)

	u64 rmac_addr_data1_mem;
#define RMAC_ADDR_DATA1_MEM_MASK(n)    vBIT(n,0,48)

	u8 unused15[0x8];

/*
        u64 rmac_addr_cfg;
#define RMAC_ADDR_UCASTn_EN(n)     mBIT(0)_n(n)
#define RMAC_ADDR_MCASTn_EN(n)     mBIT(0)_n(n)
#define RMAC_ADDR_BCAST_EN         vBIT(0)_48 
#define RMAC_ADDR_ALL_ADDR_EN      vBIT(0)_49 
*/
	u64 tmac_ipg_cfg;

	u64 rmac_pause_cfg;
#define RMAC_PAUSE_GEN             BIT(0)
#define RMAC_PAUSE_GEN_ENABLE      BIT(0)
#define RMAC_PAUSE_RX              BIT(1)
#define RMAC_PAUSE_RX_ENABLE       BIT(1)
#define RMAC_PAUSE_HG_PTIME_DEF    vBIT(0xFFFF,16,16)
#define RMAC_PAUSE_HG_PTIME(val)    vBIT(val,16,16)

	u64 rmac_red_cfg;

	u64 rmac_red_rate_q0q3;
	u64 rmac_red_rate_q4q7;

	u64 mac_link_util;
#define MAC_TX_LINK_UTIL           vBIT(0xFE,1,7)
#define MAC_TX_LINK_UTIL_DISABLE   vBIT(0xF, 8,4)
#define MAC_TX_LINK_UTIL_VAL( n )  vBIT(n,8,4)
#define MAC_RX_LINK_UTIL           vBIT(0xFE,33,7)
#define MAC_RX_LINK_UTIL_DISABLE   vBIT(0xF,40,4)
#define MAC_RX_LINK_UTIL_VAL( n )  vBIT(n,40,4)

#define MAC_LINK_UTIL_DISABLE      MAC_TX_LINK_UTIL_DISABLE | \
                                   MAC_RX_LINK_UTIL_DISABLE

	u64 rmac_invalid_ipg;

/* rx traffic steering */
#define	MAC_RTS_FRM_LEN_SET(len)	vBIT(len,2,14)
	u64 rts_frm_len_n[8];

	u64 rts_qos_steering;

#define MAX_DIX_MAP                         4
	u64 rts_dix_map_n[MAX_DIX_MAP];
#define RTS_DIX_MAP_ETYPE(val)             vBIT(val,0,16)
#define RTS_DIX_MAP_SCW(val)               BIT(val,21)

	u64 rts_q_alternates;
	u64 rts_default_q;

	u64 rts_ctrl;
#define RTS_CTRL_IGNORE_SNAP_OUI           BIT(2)
#define RTS_CTRL_IGNORE_LLC_CTRL           BIT(3)

	u64 rts_pn_cam_ctrl;
#define RTS_PN_CAM_CTRL_WE                 BIT(7)
#define RTS_PN_CAM_CTRL_STROBE_NEW_CMD     BIT(15)
#define RTS_PN_CAM_CTRL_STROBE_BEING_EXECUTED   BIT(15)
#define RTS_PN_CAM_CTRL_OFFSET(n)          vBIT(n,24,8)
	u64 rts_pn_cam_data;
#define RTS_PN_CAM_DATA_TCP_SELECT         BIT(7)
#define RTS_PN_CAM_DATA_PORT(val)          vBIT(val,8,16)
#define RTS_PN_CAM_DATA_SCW(val)           vBIT(val,24,8)

	u64 rts_ds_mem_ctrl;
#define RTS_DS_MEM_CTRL_WE                 BIT(7)
#define RTS_DS_MEM_CTRL_STROBE_NEW_CMD     BIT(15)
#define RTS_DS_MEM_CTRL_STROBE_CMD_BEING_EXECUTED   BIT(15)
#define RTS_DS_MEM_CTRL_OFFSET(n)          vBIT(n,26,6)
	u64 rts_ds_mem_data;
#define RTS_DS_MEM_DATA(n)                 vBIT(n,0,8)

	u8 unused16[0x700 - 0x220];

	u64 mac_debug_ctrl;
#define MAC_DBG_ACTIVITY_VALUE		   0x411040400000000ULL

	u8 unused17[0x2800 - 0x2708];

/* memory controller registers */
	u64 mc_int_status;
#define MC_INT_STATUS_MC_INT               BIT(0)
	u64 mc_int_mask;
#define MC_INT_MASK_MC_INT                 BIT(0)

	u64 mc_err_reg;
#define MC_ERR_REG_ECC_DB_ERR_L            BIT(14)
#define MC_ERR_REG_ECC_DB_ERR_U            BIT(15)
#define MC_ERR_REG_MIRI_ECC_DB_ERR_0       BIT(18)
#define MC_ERR_REG_MIRI_ECC_DB_ERR_1       BIT(20)
#define MC_ERR_REG_MIRI_CRI_ERR_0          BIT(22)
#define MC_ERR_REG_MIRI_CRI_ERR_1          BIT(23)
#define MC_ERR_REG_SM_ERR                  BIT(31)
#define MC_ERR_REG_ECC_ALL_SNG		   (BIT(2) | BIT(3) | BIT(4) | BIT(5) |\
					    BIT(6) | BIT(7) | BIT(17) | BIT(19))
#define MC_ERR_REG_ECC_ALL_DBL		   (BIT(10) | BIT(11) | BIT(12) |\
					    BIT(13) | BIT(14) | BIT(15) |\
					    BIT(18) | BIT(20))
	u64 mc_err_mask;
	u64 mc_err_alarm;

	u8 unused18[0x100 - 0x28];

/* MC configuration */
	u64 rx_queue_cfg;
#define RX_QUEUE_CFG_Q0_SZ(n)              vBIT(n,0,8)
#define RX_QUEUE_CFG_Q1_SZ(n)              vBIT(n,8,8)
#define RX_QUEUE_CFG_Q2_SZ(n)              vBIT(n,16,8)
#define RX_QUEUE_CFG_Q3_SZ(n)              vBIT(n,24,8)
#define RX_QUEUE_CFG_Q4_SZ(n)              vBIT(n,32,8)
#define RX_QUEUE_CFG_Q5_SZ(n)              vBIT(n,40,8)
#define RX_QUEUE_CFG_Q6_SZ(n)              vBIT(n,48,8)
#define RX_QUEUE_CFG_Q7_SZ(n)              vBIT(n,56,8)

	u64 mc_rldram_mrs;
#define	MC_RLDRAM_QUEUE_SIZE_ENABLE			BIT(39)
#define	MC_RLDRAM_MRS_ENABLE				BIT(47)

	u64 mc_rldram_interleave;

	u64 mc_pause_thresh_q0q3;
	u64 mc_pause_thresh_q4q7;

	u64 mc_red_thresh_q[8];

	u8 unused19[0x200 - 0x168];
	u64 mc_rldram_ref_per;
	u8 unused20[0x220 - 0x208];
	u64 mc_rldram_test_ctrl;
#define MC_RLDRAM_TEST_MODE		BIT(47)
#define MC_RLDRAM_TEST_WRITE	BIT(7)
#define MC_RLDRAM_TEST_GO		BIT(15)
#define MC_RLDRAM_TEST_DONE		BIT(23)
#define MC_RLDRAM_TEST_PASS		BIT(31)

	u8 unused21[0x240 - 0x228];
	u64 mc_rldram_test_add;
	u8 unused22[0x260 - 0x248];
	u64 mc_rldram_test_d0;
	u8 unused23[0x280 - 0x268];
	u64 mc_rldram_test_d1;
	u8 unused24[0x300 - 0x288];
	u64 mc_rldram_test_d2;

	u8 unused24_1[0x360 - 0x308];
	u64 mc_rldram_ctrl;
#define	MC_RLDRAM_ENABLE_ODT		BIT(7)

	u8 unused24_2[0x640 - 0x368];
	u64 mc_rldram_ref_per_herc;
#define	MC_RLDRAM_SET_REF_PERIOD(val)	vBIT(val, 0, 16)

	u8 unused24_3[0x660 - 0x648];
	u64 mc_rldram_mrs_herc;

	u8 unused25[0x700 - 0x668];
	u64 mc_debug_ctrl;

	u8 unused26[0x3000 - 0x2f08];

/* XGXG */
	/* XGXS control registers */

	u64 xgxs_int_status;
#define XGXS_INT_STATUS_TXGXS              BIT(0)
#define XGXS_INT_STATUS_RXGXS              BIT(1)
	u64 xgxs_int_mask;
#define XGXS_INT_MASK_TXGXS                BIT(0)
#define XGXS_INT_MASK_RXGXS                BIT(1)

	u64 xgxs_txgxs_err_reg;
#define TXGXS_ECC_DB_ERR                   BIT(15)
	u64 xgxs_txgxs_err_mask;
	u64 xgxs_txgxs_err_alarm;

	u64 xgxs_rxgxs_err_reg;
	u64 xgxs_rxgxs_err_mask;
	u64 xgxs_rxgxs_err_alarm;

	u8 unused27[0x100 - 0x40];

	u64 xgxs_cfg;
	u64 xgxs_status;

	u64 xgxs_cfg_key;
	u64 xgxs_efifo_cfg;	/* CHANGED */
	u64 rxgxs_ber_0;	/* CHANGED */
	u64 rxgxs_ber_1;	/* CHANGED */

	u64 spi_control;
#define SPI_CONTROL_KEY(key)		vBIT(key,0,4)
#define SPI_CONTROL_BYTECNT(cnt)	vBIT(cnt,29,3)
#define SPI_CONTROL_CMD(cmd)		vBIT(cmd,32,8)
#define SPI_CONTROL_ADDR(addr)		vBIT(addr,40,24)
#define SPI_CONTROL_SEL1		BIT(4)
#define SPI_CONTROL_REQ			BIT(7)
#define SPI_CONTROL_NACK		BIT(5)
#define SPI_CONTROL_DONE		BIT(6)
	u64 spi_data;
#define SPI_DATA_WRITE(data,len)	vBIT(data,0,len)
} XENA_dev_config_t;

#define XENA_REG_SPACE	sizeof(XENA_dev_config_t)
#define	XENA_EEPROM_SPACE (0x01 << 11)

#endif				/* _REGS_H */
