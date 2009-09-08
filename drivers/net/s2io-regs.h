/************************************************************************
 * regs.h: A Linux PCI-X Ethernet driver for Neterion 10GbE Server NIC
 * Copyright(c) 2002-2007 Neterion Inc.

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

struct XENA_dev_config {
/* Convention: mHAL_XXX is mask, vHAL_XXX is value */

/* General Control-Status Registers */
	u64 general_int_status;
#define GEN_INTR_TXPIC             s2BIT(0)
#define GEN_INTR_TXDMA             s2BIT(1)
#define GEN_INTR_TXMAC             s2BIT(2)
#define GEN_INTR_TXXGXS            s2BIT(3)
#define GEN_INTR_TXTRAFFIC         s2BIT(8)
#define GEN_INTR_RXPIC             s2BIT(32)
#define GEN_INTR_RXDMA             s2BIT(33)
#define GEN_INTR_RXMAC             s2BIT(34)
#define GEN_INTR_MC                s2BIT(35)
#define GEN_INTR_RXXGXS            s2BIT(36)
#define GEN_INTR_RXTRAFFIC         s2BIT(40)
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
#define ADAPTER_STATUS_TDMA_READY          s2BIT(0)
#define ADAPTER_STATUS_RDMA_READY          s2BIT(1)
#define ADAPTER_STATUS_PFC_READY           s2BIT(2)
#define ADAPTER_STATUS_TMAC_BUF_EMPTY      s2BIT(3)
#define ADAPTER_STATUS_PIC_QUIESCENT       s2BIT(5)
#define ADAPTER_STATUS_RMAC_REMOTE_FAULT   s2BIT(6)
#define ADAPTER_STATUS_RMAC_LOCAL_FAULT    s2BIT(7)
#define ADAPTER_STATUS_RMAC_PCC_IDLE       vBIT(0xFF,8,8)
#define ADAPTER_STATUS_RMAC_PCC_FOUR_IDLE  vBIT(0x0F,8,8)
#define ADAPTER_STATUS_RC_PRC_QUIESCENT    vBIT(0xFF,16,8)
#define ADAPTER_STATUS_MC_DRAM_READY       s2BIT(24)
#define ADAPTER_STATUS_MC_QUEUES_READY     s2BIT(25)
#define ADAPTER_STATUS_RIC_RUNNING         s2BIT(26)
#define ADAPTER_STATUS_M_PLL_LOCK          s2BIT(30)
#define ADAPTER_STATUS_P_PLL_LOCK          s2BIT(31)

	u64 adapter_control;
#define ADAPTER_CNTL_EN                    s2BIT(7)
#define ADAPTER_EOI_TX_ON                  s2BIT(15)
#define ADAPTER_LED_ON                     s2BIT(23)
#define ADAPTER_UDPI(val)                  vBIT(val,36,4)
#define ADAPTER_WAIT_INT                   s2BIT(48)
#define ADAPTER_ECC_EN                     s2BIT(55)

	u64 serr_source;
#define SERR_SOURCE_PIC			s2BIT(0)
#define SERR_SOURCE_TXDMA		s2BIT(1)
#define SERR_SOURCE_RXDMA		s2BIT(2)
#define SERR_SOURCE_MAC                 s2BIT(3)
#define SERR_SOURCE_MC                  s2BIT(4)
#define SERR_SOURCE_XGXS                s2BIT(5)
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
#define	PCI_MODE_UNSUPPORTED		s2BIT(0)
#define	PCI_MODE_32_BITS		s2BIT(8)
#define	PCI_MODE_UNKNOWN_MODE		s2BIT(9)

	u8 unused_0[0x800 - 0x128];

/* PCI-X Controller registers */
	u64 pic_int_status;
	u64 pic_int_mask;
#define PIC_INT_TX                     s2BIT(0)
#define PIC_INT_FLSH                   s2BIT(1)
#define PIC_INT_MDIO                   s2BIT(2)
#define PIC_INT_IIC                    s2BIT(3)
#define PIC_INT_GPIO                   s2BIT(4)
#define PIC_INT_RX                     s2BIT(32)

	u64 txpic_int_reg;
	u64 txpic_int_mask;
#define PCIX_INT_REG_ECC_SG_ERR                s2BIT(0)
#define PCIX_INT_REG_ECC_DB_ERR                s2BIT(1)
#define PCIX_INT_REG_FLASHR_R_FSM_ERR          s2BIT(8)
#define PCIX_INT_REG_FLASHR_W_FSM_ERR          s2BIT(9)
#define PCIX_INT_REG_INI_TX_FSM_SERR           s2BIT(10)
#define PCIX_INT_REG_INI_TXO_FSM_ERR           s2BIT(11)
#define PCIX_INT_REG_TRT_FSM_SERR              s2BIT(13)
#define PCIX_INT_REG_SRT_FSM_SERR              s2BIT(14)
#define PCIX_INT_REG_PIFR_FSM_SERR             s2BIT(15)
#define PCIX_INT_REG_WRC_TX_SEND_FSM_SERR      s2BIT(21)
#define PCIX_INT_REG_RRC_TX_REQ_FSM_SERR       s2BIT(23)
#define PCIX_INT_REG_INI_RX_FSM_SERR           s2BIT(48)
#define PCIX_INT_REG_RA_RX_FSM_SERR            s2BIT(50)
/*
#define PCIX_INT_REG_WRC_RX_SEND_FSM_SERR      s2BIT(52)
#define PCIX_INT_REG_RRC_RX_REQ_FSM_SERR       s2BIT(54)
#define PCIX_INT_REG_RRC_RX_SPLIT_FSM_SERR     s2BIT(58)
*/
	u64 txpic_alarms;
	u64 rxpic_int_reg;
	u64 rxpic_int_mask;
	u64 rxpic_alarms;

	u64 flsh_int_reg;
	u64 flsh_int_mask;
#define PIC_FLSH_INT_REG_CYCLE_FSM_ERR         s2BIT(63)
#define PIC_FLSH_INT_REG_ERR                   s2BIT(62)
	u64 flash_alarms;

	u64 mdio_int_reg;
	u64 mdio_int_mask;
#define MDIO_INT_REG_MDIO_BUS_ERR              s2BIT(0)
#define MDIO_INT_REG_DTX_BUS_ERR               s2BIT(8)
#define MDIO_INT_REG_LASI                      s2BIT(39)
	u64 mdio_alarms;

	u64 iic_int_reg;
	u64 iic_int_mask;
#define IIC_INT_REG_BUS_FSM_ERR                s2BIT(4)
#define IIC_INT_REG_BIT_FSM_ERR                s2BIT(5)
#define IIC_INT_REG_CYCLE_FSM_ERR              s2BIT(6)
#define IIC_INT_REG_REQ_FSM_ERR                s2BIT(7)
#define IIC_INT_REG_ACK_ERR                    s2BIT(8)
	u64 iic_alarms;

	u8 unused4[0x08];

	u64 gpio_int_reg;
#define GPIO_INT_REG_DP_ERR_INT                s2BIT(0)
#define GPIO_INT_REG_LINK_DOWN                 s2BIT(1)
#define GPIO_INT_REG_LINK_UP                   s2BIT(2)
	u64 gpio_int_mask;
#define GPIO_INT_MASK_LINK_DOWN                s2BIT(1)
#define GPIO_INT_MASK_LINK_UP                  s2BIT(2)
	u64 gpio_alarms;

	u8 unused5[0x38];

	u64 tx_traffic_int;
#define TX_TRAFFIC_INT_n(n)                    s2BIT(n)
	u64 tx_traffic_mask;

	u64 rx_traffic_int;
#define RX_TRAFFIC_INT_n(n)                    s2BIT(n)
	u64 rx_traffic_mask;

/* PIC Control registers */
	u64 pic_control;
#define PIC_CNTL_RX_ALARM_MAP_1                s2BIT(0)
#define PIC_CNTL_SHARED_SPLITS(n)              vBIT(n,11,5)

	u64 swapper_ctrl;
#define SWAPPER_CTRL_PIF_R_FE                  s2BIT(0)
#define SWAPPER_CTRL_PIF_R_SE                  s2BIT(1)
#define SWAPPER_CTRL_PIF_W_FE                  s2BIT(8)
#define SWAPPER_CTRL_PIF_W_SE                  s2BIT(9)
#define SWAPPER_CTRL_TXP_FE                    s2BIT(16)
#define SWAPPER_CTRL_TXP_SE                    s2BIT(17)
#define SWAPPER_CTRL_TXD_R_FE                  s2BIT(18)
#define SWAPPER_CTRL_TXD_R_SE                  s2BIT(19)
#define SWAPPER_CTRL_TXD_W_FE                  s2BIT(20)
#define SWAPPER_CTRL_TXD_W_SE                  s2BIT(21)
#define SWAPPER_CTRL_TXF_R_FE                  s2BIT(22)
#define SWAPPER_CTRL_TXF_R_SE                  s2BIT(23)
#define SWAPPER_CTRL_RXD_R_FE                  s2BIT(32)
#define SWAPPER_CTRL_RXD_R_SE                  s2BIT(33)
#define SWAPPER_CTRL_RXD_W_FE                  s2BIT(34)
#define SWAPPER_CTRL_RXD_W_SE                  s2BIT(35)
#define SWAPPER_CTRL_RXF_W_FE                  s2BIT(36)
#define SWAPPER_CTRL_RXF_W_SE                  s2BIT(37)
#define SWAPPER_CTRL_XMSI_FE                   s2BIT(40)
#define SWAPPER_CTRL_XMSI_SE                   s2BIT(41)
#define SWAPPER_CTRL_STATS_FE                  s2BIT(48)
#define SWAPPER_CTRL_STATS_SE                  s2BIT(49)

	u64 pif_rd_swapper_fb;
#define IF_RD_SWAPPER_FB                            0x0123456789ABCDEF

	u64 scheduled_int_ctrl;
#define SCHED_INT_CTRL_TIMER_EN                s2BIT(0)
#define SCHED_INT_CTRL_ONE_SHOT                s2BIT(1)
#define SCHED_INT_CTRL_INT2MSI(val)		vBIT(val,10,6)
#define SCHED_INT_PERIOD                       TBD

	u64 txreqtimeout;
#define TXREQTO_VAL(val)						vBIT(val,0,32)
#define TXREQTO_EN								s2BIT(63)

	u64 statsreqtimeout;
#define STATREQTO_VAL(n)                       TBD
#define STATREQTO_EN                           s2BIT(63)

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

	u64 xmsi_mask_reg;
	u64 stat_byte_cnt;
#define STAT_BC(n)                              vBIT(n,4,12)

	/* Automated statistics collection */
	u64 stat_cfg;
#define STAT_CFG_STAT_EN           s2BIT(0)
#define STAT_CFG_ONE_SHOT_EN       s2BIT(1)
#define STAT_CFG_STAT_NS_EN        s2BIT(8)
#define STAT_CFG_STAT_RO           s2BIT(9)
#define STAT_TRSF_PER(n)           TBD
#define	PER_SEC					   0x208d5
#define	SET_UPDT_PERIOD(n)		   vBIT((PER_SEC*n),32,32)
#define	SET_UPDT_CLICKS(val)		   vBIT(val, 32, 32)

	u64 stat_addr;

	/* General Configuration */
	u64 mdio_control;
#define MDIO_MMD_INDX_ADDR(val)		vBIT(val, 0, 16)
#define MDIO_MMD_DEV_ADDR(val)		vBIT(val, 19, 5)
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
#define	I2C_CONTROL_READ			s2BIT(24)
#define	I2C_CONTROL_NACK			s2BIT(25)
#define	I2C_CONTROL_CNTL_START		vBIT(0xE,28,4)
#define	I2C_CONTROL_CNTL_END(val)	(val & vBIT(0x1,28,4))
#define	I2C_CONTROL_GET_DATA(val)	(u32)(val & 0xFFFFFFFF)
#define	I2C_CONTROL_SET_DATA(val)	vBIT(val,32,32)

	u64 gpio_control;
#define GPIO_CTRL_GPIO_0		s2BIT(8)
	u64 misc_control;
#define FAULT_BEHAVIOUR			s2BIT(0)
#define EXT_REQ_EN			s2BIT(1)
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
#define TXDMA_PFC_INT                  s2BIT(0)
#define TXDMA_TDA_INT                  s2BIT(1)
#define TXDMA_PCC_INT                  s2BIT(2)
#define TXDMA_TTI_INT                  s2BIT(3)
#define TXDMA_LSO_INT                  s2BIT(4)
#define TXDMA_TPA_INT                  s2BIT(5)
#define TXDMA_SM_INT                   s2BIT(6)
	u64 pfc_err_reg;
#define PFC_ECC_SG_ERR			s2BIT(7)
#define PFC_ECC_DB_ERR			s2BIT(15)
#define PFC_SM_ERR_ALARM		s2BIT(23)
#define PFC_MISC_0_ERR			s2BIT(31)
#define PFC_MISC_1_ERR			s2BIT(32)
#define PFC_PCIX_ERR			s2BIT(39)
	u64 pfc_err_mask;
	u64 pfc_err_alarm;

	u64 tda_err_reg;
#define TDA_Fn_ECC_SG_ERR		vBIT(0xff,0,8)
#define TDA_Fn_ECC_DB_ERR		vBIT(0xff,8,8)
#define TDA_SM0_ERR_ALARM		s2BIT(22)
#define TDA_SM1_ERR_ALARM		s2BIT(23)
#define TDA_PCIX_ERR			s2BIT(39)
	u64 tda_err_mask;
	u64 tda_err_alarm;

	u64 pcc_err_reg;
#define PCC_FB_ECC_SG_ERR		vBIT(0xFF,0,8)
#define PCC_TXB_ECC_SG_ERR		vBIT(0xFF,8,8)
#define PCC_FB_ECC_DB_ERR		vBIT(0xFF,16, 8)
#define PCC_TXB_ECC_DB_ERR		vBIT(0xff,24,8)
#define PCC_SM_ERR_ALARM		vBIT(0xff,32,8)
#define PCC_WR_ERR_ALARM		vBIT(0xff,40,8)
#define PCC_N_SERR			vBIT(0xff,48,8)
#define PCC_6_COF_OV_ERR		s2BIT(56)
#define PCC_7_COF_OV_ERR		s2BIT(57)
#define PCC_6_LSO_OV_ERR		s2BIT(58)
#define PCC_7_LSO_OV_ERR		s2BIT(59)
#define PCC_ENABLE_FOUR			vBIT(0x0F,0,8)
	u64 pcc_err_mask;
	u64 pcc_err_alarm;

	u64 tti_err_reg;
#define TTI_ECC_SG_ERR			s2BIT(7)
#define TTI_ECC_DB_ERR			s2BIT(15)
#define TTI_SM_ERR_ALARM		s2BIT(23)
	u64 tti_err_mask;
	u64 tti_err_alarm;

	u64 lso_err_reg;
#define LSO6_SEND_OFLOW			s2BIT(12)
#define LSO7_SEND_OFLOW			s2BIT(13)
#define LSO6_ABORT			s2BIT(14)
#define LSO7_ABORT			s2BIT(15)
#define LSO6_SM_ERR_ALARM		s2BIT(22)
#define LSO7_SM_ERR_ALARM		s2BIT(23)
	u64 lso_err_mask;
	u64 lso_err_alarm;

	u64 tpa_err_reg;
#define TPA_TX_FRM_DROP			s2BIT(7)
#define TPA_SM_ERR_ALARM		s2BIT(23)

	u64 tpa_err_mask;
	u64 tpa_err_alarm;

	u64 sm_err_reg;
#define SM_SM_ERR_ALARM			s2BIT(15)
	u64 sm_err_mask;
	u64 sm_err_alarm;

	u8 unused8[0x100 - 0xB8];

/* TxDMA arbiter */
	u64 tx_dma_wrap_stat;

/* Tx FIFO controller */
#define X_MAX_FIFOS                        8
#define X_FIFO_MAX_LEN                     0x1FFF	/*8191 */
	u64 tx_fifo_partition_0;
#define TX_FIFO_PARTITION_EN               s2BIT(0)
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
#define TTI_CMD_MEM_WE                     s2BIT(7)
#define TTI_CMD_MEM_STROBE_NEW_CMD         s2BIT(15)
#define TTI_CMD_MEM_STROBE_BEING_EXECUTED  s2BIT(15)
#define TTI_CMD_MEM_OFFSET(n)              vBIT(n,26,6)

	u64 tti_data1_mem;
#define TTI_DATA1_MEM_TX_TIMER_VAL(n)      vBIT(n,6,26)
#define TTI_DATA1_MEM_TX_TIMER_AC_CI(n)    vBIT(n,38,2)
#define TTI_DATA1_MEM_TX_TIMER_AC_EN       s2BIT(38)
#define TTI_DATA1_MEM_TX_TIMER_CI_EN       s2BIT(39)
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
#define TX_PA_CFG_IGNORE_FRM_ERR           s2BIT(1)
#define TX_PA_CFG_IGNORE_SNAP_OUI          s2BIT(2)
#define TX_PA_CFG_IGNORE_LLC_CTRL          s2BIT(3)
#define	TX_PA_CFG_IGNORE_L2_ERR			   s2BIT(6)
#define RX_PA_CFG_STRIP_VLAN_TAG		s2BIT(15)

/* Recent add, used only debug purposes. */
	u64 pcc_enable;

	u8 unused9[0x700 - 0x178];

	u64 txdma_debug_ctrl;

	u8 unused10[0x1800 - 0x1708];

/* RxDMA Registers */
	u64 rxdma_int_status;
	u64 rxdma_int_mask;
#define RXDMA_INT_RC_INT_M             s2BIT(0)
#define RXDMA_INT_RPA_INT_M            s2BIT(1)
#define RXDMA_INT_RDA_INT_M            s2BIT(2)
#define RXDMA_INT_RTI_INT_M            s2BIT(3)

	u64 rda_err_reg;
#define RDA_RXDn_ECC_SG_ERR		vBIT(0xFF,0,8)
#define RDA_RXDn_ECC_DB_ERR		vBIT(0xFF,8,8)
#define RDA_FRM_ECC_SG_ERR		s2BIT(23)
#define RDA_FRM_ECC_DB_N_AERR		s2BIT(31)
#define RDA_SM1_ERR_ALARM		s2BIT(38)
#define RDA_SM0_ERR_ALARM		s2BIT(39)
#define RDA_MISC_ERR			s2BIT(47)
#define RDA_PCIX_ERR			s2BIT(55)
#define RDA_RXD_ECC_DB_SERR		s2BIT(63)
	u64 rda_err_mask;
	u64 rda_err_alarm;

	u64 rc_err_reg;
#define RC_PRCn_ECC_SG_ERR		vBIT(0xFF,0,8)
#define RC_PRCn_ECC_DB_ERR		vBIT(0xFF,8,8)
#define RC_FTC_ECC_SG_ERR		s2BIT(23)
#define RC_FTC_ECC_DB_ERR		s2BIT(31)
#define RC_PRCn_SM_ERR_ALARM		vBIT(0xFF,32,8)
#define RC_FTC_SM_ERR_ALARM		s2BIT(47)
#define RC_RDA_FAIL_WR_Rn		vBIT(0xFF,48,8)
	u64 rc_err_mask;
	u64 rc_err_alarm;

	u64 prc_pcix_err_reg;
#define PRC_PCI_AB_RD_Rn		vBIT(0xFF,0,8)
#define PRC_PCI_DP_RD_Rn		vBIT(0xFF,8,8)
#define PRC_PCI_AB_WR_Rn		vBIT(0xFF,16,8)
#define PRC_PCI_DP_WR_Rn		vBIT(0xFF,24,8)
#define PRC_PCI_AB_F_WR_Rn		vBIT(0xFF,32,8)
#define PRC_PCI_DP_F_WR_Rn		vBIT(0xFF,40,8)
	u64 prc_pcix_err_mask;
	u64 prc_pcix_err_alarm;

	u64 rpa_err_reg;
#define RPA_ECC_SG_ERR			s2BIT(7)
#define RPA_ECC_DB_ERR			s2BIT(15)
#define RPA_FLUSH_REQUEST		s2BIT(22)
#define RPA_SM_ERR_ALARM		s2BIT(23)
#define RPA_CREDIT_ERR			s2BIT(31)
	u64 rpa_err_mask;
	u64 rpa_err_alarm;

	u64 rti_err_reg;
#define RTI_ECC_SG_ERR			s2BIT(7)
#define RTI_ECC_DB_ERR			s2BIT(15)
#define RTI_SM_ERR_ALARM		s2BIT(23)
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
#define PRC_CTRL_RC_ENABLED                    s2BIT(7)
#define PRC_CTRL_RING_MODE                     (s2BIT(14)|s2BIT(15))
#define PRC_CTRL_RING_MODE_1                   vBIT(0,14,2)
#define PRC_CTRL_RING_MODE_3                   vBIT(1,14,2)
#define PRC_CTRL_RING_MODE_5                   vBIT(2,14,2)
#define PRC_CTRL_RING_MODE_x                   vBIT(3,14,2)
#define PRC_CTRL_NO_SNOOP                      (s2BIT(22)|s2BIT(23))
#define PRC_CTRL_NO_SNOOP_DESC                 s2BIT(22)
#define PRC_CTRL_NO_SNOOP_BUFF                 s2BIT(23)
#define PRC_CTRL_BIMODAL_INTERRUPT             s2BIT(37)
#define PRC_CTRL_GROUP_READS                   s2BIT(38)
#define PRC_CTRL_RXD_BACKOFF_INTERVAL(val)     vBIT(val,40,24)

	u64 prc_alarm_action;
#define PRC_ALARM_ACTION_RR_R0_STOP            s2BIT(3)
#define PRC_ALARM_ACTION_RW_R0_STOP            s2BIT(7)
#define PRC_ALARM_ACTION_RR_R1_STOP            s2BIT(11)
#define PRC_ALARM_ACTION_RW_R1_STOP            s2BIT(15)
#define PRC_ALARM_ACTION_RR_R2_STOP            s2BIT(19)
#define PRC_ALARM_ACTION_RW_R2_STOP            s2BIT(23)
#define PRC_ALARM_ACTION_RR_R3_STOP            s2BIT(27)
#define PRC_ALARM_ACTION_RW_R3_STOP            s2BIT(31)
#define PRC_ALARM_ACTION_RR_R4_STOP            s2BIT(35)
#define PRC_ALARM_ACTION_RW_R4_STOP            s2BIT(39)
#define PRC_ALARM_ACTION_RR_R5_STOP            s2BIT(43)
#define PRC_ALARM_ACTION_RW_R5_STOP            s2BIT(47)
#define PRC_ALARM_ACTION_RR_R6_STOP            s2BIT(51)
#define PRC_ALARM_ACTION_RW_R6_STOP            s2BIT(55)
#define PRC_ALARM_ACTION_RR_R7_STOP            s2BIT(59)
#define PRC_ALARM_ACTION_RW_R7_STOP            s2BIT(63)

/* Receive traffic interrupts */
	u64 rti_command_mem;
#define RTI_CMD_MEM_WE                          s2BIT(7)
#define RTI_CMD_MEM_STROBE                      s2BIT(15)
#define RTI_CMD_MEM_STROBE_NEW_CMD              s2BIT(15)
#define RTI_CMD_MEM_STROBE_CMD_BEING_EXECUTED   s2BIT(15)
#define RTI_CMD_MEM_OFFSET(n)                   vBIT(n,29,3)

	u64 rti_data1_mem;
#define RTI_DATA1_MEM_RX_TIMER_VAL(n)      vBIT(n,3,29)
#define RTI_DATA1_MEM_RX_TIMER_AC_EN       s2BIT(38)
#define RTI_DATA1_MEM_RX_TIMER_CI_EN       s2BIT(39)
#define RTI_DATA1_MEM_RX_URNG_A(n)         vBIT(n,41,7)
#define RTI_DATA1_MEM_RX_URNG_B(n)         vBIT(n,49,7)
#define RTI_DATA1_MEM_RX_URNG_C(n)         vBIT(n,57,7)

	u64 rti_data2_mem;
#define RTI_DATA2_MEM_RX_UFC_A(n)          vBIT(n,0,16)
#define RTI_DATA2_MEM_RX_UFC_B(n)          vBIT(n,16,16)
#define RTI_DATA2_MEM_RX_UFC_C(n)          vBIT(n,32,16)
#define RTI_DATA2_MEM_RX_UFC_D(n)          vBIT(n,48,16)

	u64 rx_pa_cfg;
#define RX_PA_CFG_IGNORE_FRM_ERR           s2BIT(1)
#define RX_PA_CFG_IGNORE_SNAP_OUI          s2BIT(2)
#define RX_PA_CFG_IGNORE_LLC_CTRL          s2BIT(3)
#define RX_PA_CFG_IGNORE_L2_ERR            s2BIT(6)

	u64 unused_11_1;

	u64 ring_bump_counter1;
	u64 ring_bump_counter2;

	u8 unused12[0x700 - 0x1F0];

	u64 rxdma_debug_ctrl;

	u8 unused13[0x2000 - 0x1f08];

/* Media Access Controller Register */
	u64 mac_int_status;
	u64 mac_int_mask;
#define MAC_INT_STATUS_TMAC_INT            s2BIT(0)
#define MAC_INT_STATUS_RMAC_INT            s2BIT(1)

	u64 mac_tmac_err_reg;
#define TMAC_ECC_SG_ERR				s2BIT(7)
#define TMAC_ECC_DB_ERR				s2BIT(15)
#define TMAC_TX_BUF_OVRN			s2BIT(23)
#define TMAC_TX_CRI_ERR				s2BIT(31)
#define TMAC_TX_SM_ERR				s2BIT(39)
#define TMAC_DESC_ECC_SG_ERR			s2BIT(47)
#define TMAC_DESC_ECC_DB_ERR			s2BIT(55)

	u64 mac_tmac_err_mask;
	u64 mac_tmac_err_alarm;

	u64 mac_rmac_err_reg;
#define RMAC_RX_BUFF_OVRN			s2BIT(0)
#define RMAC_FRM_RCVD_INT			s2BIT(1)
#define RMAC_UNUSED_INT				s2BIT(2)
#define RMAC_RTS_PNUM_ECC_SG_ERR		s2BIT(5)
#define RMAC_RTS_DS_ECC_SG_ERR			s2BIT(6)
#define RMAC_RD_BUF_ECC_SG_ERR			s2BIT(7)
#define RMAC_RTH_MAP_ECC_SG_ERR			s2BIT(8)
#define RMAC_RTH_SPDM_ECC_SG_ERR		s2BIT(9)
#define RMAC_RTS_VID_ECC_SG_ERR			s2BIT(10)
#define RMAC_DA_SHADOW_ECC_SG_ERR		s2BIT(11)
#define RMAC_RTS_PNUM_ECC_DB_ERR		s2BIT(13)
#define RMAC_RTS_DS_ECC_DB_ERR			s2BIT(14)
#define RMAC_RD_BUF_ECC_DB_ERR			s2BIT(15)
#define RMAC_RTH_MAP_ECC_DB_ERR			s2BIT(16)
#define RMAC_RTH_SPDM_ECC_DB_ERR		s2BIT(17)
#define RMAC_RTS_VID_ECC_DB_ERR			s2BIT(18)
#define RMAC_DA_SHADOW_ECC_DB_ERR		s2BIT(19)
#define RMAC_LINK_STATE_CHANGE_INT		s2BIT(31)
#define RMAC_RX_SM_ERR				s2BIT(39)
#define RMAC_SINGLE_ECC_ERR			(s2BIT(5) | s2BIT(6) | s2BIT(7) |\
						s2BIT(8)  | s2BIT(9) | s2BIT(10)|\
						s2BIT(11))
#define RMAC_DOUBLE_ECC_ERR			(s2BIT(13) | s2BIT(14) | s2BIT(15) |\
						s2BIT(16)  | s2BIT(17) | s2BIT(18)|\
						s2BIT(19))
	u64 mac_rmac_err_mask;
	u64 mac_rmac_err_alarm;

	u8 unused14[0x100 - 0x40];

	u64 mac_cfg;
#define MAC_CFG_TMAC_ENABLE             s2BIT(0)
#define MAC_CFG_RMAC_ENABLE             s2BIT(1)
#define MAC_CFG_LAN_NOT_WAN             s2BIT(2)
#define MAC_CFG_TMAC_LOOPBACK           s2BIT(3)
#define MAC_CFG_TMAC_APPEND_PAD         s2BIT(4)
#define MAC_CFG_RMAC_STRIP_FCS          s2BIT(5)
#define MAC_CFG_RMAC_STRIP_PAD          s2BIT(6)
#define MAC_CFG_RMAC_PROM_ENABLE        s2BIT(7)
#define MAC_RMAC_DISCARD_PFRM           s2BIT(8)
#define MAC_RMAC_BCAST_ENABLE           s2BIT(9)
#define MAC_RMAC_ALL_ADDR_ENABLE        s2BIT(10)
#define MAC_RMAC_INVLD_IPG_THR(val)     vBIT(val,16,8)

	u64 tmac_avg_ipg;
#define TMAC_AVG_IPG(val)           vBIT(val,0,8)

	u64 rmac_max_pyld_len;
#define RMAC_MAX_PYLD_LEN(val)      vBIT(val,2,14)
#define RMAC_MAX_PYLD_LEN_DEF       vBIT(1500,2,14)
#define RMAC_MAX_PYLD_LEN_JUMBO_DEF vBIT(9600,2,14)

	u64 rmac_err_cfg;
#define RMAC_ERR_FCS                    s2BIT(0)
#define RMAC_ERR_FCS_ACCEPT             s2BIT(1)
#define RMAC_ERR_TOO_LONG               s2BIT(1)
#define RMAC_ERR_TOO_LONG_ACCEPT        s2BIT(1)
#define RMAC_ERR_RUNT                   s2BIT(2)
#define RMAC_ERR_RUNT_ACCEPT            s2BIT(2)
#define RMAC_ERR_LEN_MISMATCH           s2BIT(3)
#define RMAC_ERR_LEN_MISMATCH_ACCEPT    s2BIT(3)

	u64 rmac_cfg_key;
#define RMAC_CFG_KEY(val)               vBIT(val,0,16)

#define S2IO_MAC_ADDR_START_OFFSET	0

#define S2IO_XENA_MAX_MC_ADDRESSES	64	/* multicast addresses */
#define S2IO_HERC_MAX_MC_ADDRESSES	256

#define S2IO_XENA_MAX_MAC_ADDRESSES	16
#define S2IO_HERC_MAX_MAC_ADDRESSES	64

#define S2IO_XENA_MC_ADDR_START_OFFSET	16
#define S2IO_HERC_MC_ADDR_START_OFFSET	64

	u64 rmac_addr_cmd_mem;
#define RMAC_ADDR_CMD_MEM_WE                    s2BIT(7)
#define RMAC_ADDR_CMD_MEM_RD                    0
#define RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD        s2BIT(15)
#define RMAC_ADDR_CMD_MEM_STROBE_CMD_EXECUTING  s2BIT(15)
#define RMAC_ADDR_CMD_MEM_OFFSET(n)             vBIT(n,26,6)

	u64 rmac_addr_data0_mem;
#define RMAC_ADDR_DATA0_MEM_ADDR(n)    vBIT(n,0,48)
#define RMAC_ADDR_DATA0_MEM_USER       s2BIT(48)

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
#define RMAC_PAUSE_GEN             s2BIT(0)
#define RMAC_PAUSE_GEN_ENABLE      s2BIT(0)
#define RMAC_PAUSE_RX              s2BIT(1)
#define RMAC_PAUSE_RX_ENABLE       s2BIT(1)
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
#define RTS_DIX_MAP_SCW(val)               s2BIT(val,21)

	u64 rts_q_alternates;
	u64 rts_default_q;

	u64 rts_ctrl;
#define RTS_CTRL_IGNORE_SNAP_OUI           s2BIT(2)
#define RTS_CTRL_IGNORE_LLC_CTRL           s2BIT(3)

	u64 rts_pn_cam_ctrl;
#define RTS_PN_CAM_CTRL_WE                 s2BIT(7)
#define RTS_PN_CAM_CTRL_STROBE_NEW_CMD     s2BIT(15)
#define RTS_PN_CAM_CTRL_STROBE_BEING_EXECUTED   s2BIT(15)
#define RTS_PN_CAM_CTRL_OFFSET(n)          vBIT(n,24,8)
	u64 rts_pn_cam_data;
#define RTS_PN_CAM_DATA_TCP_SELECT         s2BIT(7)
#define RTS_PN_CAM_DATA_PORT(val)          vBIT(val,8,16)
#define RTS_PN_CAM_DATA_SCW(val)           vBIT(val,24,8)

	u64 rts_ds_mem_ctrl;
#define RTS_DS_MEM_CTRL_WE                 s2BIT(7)
#define RTS_DS_MEM_CTRL_STROBE_NEW_CMD     s2BIT(15)
#define RTS_DS_MEM_CTRL_STROBE_CMD_BEING_EXECUTED   s2BIT(15)
#define RTS_DS_MEM_CTRL_OFFSET(n)          vBIT(n,26,6)
	u64 rts_ds_mem_data;
#define RTS_DS_MEM_DATA(n)                 vBIT(n,0,8)

	u8 unused16[0x700 - 0x220];

	u64 mac_debug_ctrl;
#define MAC_DBG_ACTIVITY_VALUE		   0x411040400000000ULL

	u8 unused17[0x2800 - 0x2708];

/* memory controller registers */
	u64 mc_int_status;
#define MC_INT_STATUS_MC_INT               s2BIT(0)
	u64 mc_int_mask;
#define MC_INT_MASK_MC_INT                 s2BIT(0)

	u64 mc_err_reg;
#define MC_ERR_REG_ECC_DB_ERR_L            s2BIT(14)
#define MC_ERR_REG_ECC_DB_ERR_U            s2BIT(15)
#define MC_ERR_REG_MIRI_ECC_DB_ERR_0       s2BIT(18)
#define MC_ERR_REG_MIRI_ECC_DB_ERR_1       s2BIT(20)
#define MC_ERR_REG_MIRI_CRI_ERR_0          s2BIT(22)
#define MC_ERR_REG_MIRI_CRI_ERR_1          s2BIT(23)
#define MC_ERR_REG_SM_ERR                  s2BIT(31)
#define MC_ERR_REG_ECC_ALL_SNG		   (s2BIT(2) | s2BIT(3) | s2BIT(4) | s2BIT(5) |\
					s2BIT(17) | s2BIT(19))
#define MC_ERR_REG_ECC_ALL_DBL		   (s2BIT(10) | s2BIT(11) | s2BIT(12) |\
					s2BIT(13) | s2BIT(18) | s2BIT(20))
#define PLL_LOCK_N			s2BIT(39)
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
#define	MC_RLDRAM_QUEUE_SIZE_ENABLE			s2BIT(39)
#define	MC_RLDRAM_MRS_ENABLE				s2BIT(47)

	u64 mc_rldram_interleave;

	u64 mc_pause_thresh_q0q3;
	u64 mc_pause_thresh_q4q7;

	u64 mc_red_thresh_q[8];

	u8 unused19[0x200 - 0x168];
	u64 mc_rldram_ref_per;
	u8 unused20[0x220 - 0x208];
	u64 mc_rldram_test_ctrl;
#define MC_RLDRAM_TEST_MODE		s2BIT(47)
#define MC_RLDRAM_TEST_WRITE	s2BIT(7)
#define MC_RLDRAM_TEST_GO		s2BIT(15)
#define MC_RLDRAM_TEST_DONE		s2BIT(23)
#define MC_RLDRAM_TEST_PASS		s2BIT(31)

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
#define	MC_RLDRAM_ENABLE_ODT		s2BIT(7)

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
#define XGXS_INT_STATUS_TXGXS              s2BIT(0)
#define XGXS_INT_STATUS_RXGXS              s2BIT(1)
	u64 xgxs_int_mask;
#define XGXS_INT_MASK_TXGXS                s2BIT(0)
#define XGXS_INT_MASK_RXGXS                s2BIT(1)

	u64 xgxs_txgxs_err_reg;
#define TXGXS_ECC_SG_ERR		s2BIT(7)
#define TXGXS_ECC_DB_ERR		s2BIT(15)
#define TXGXS_ESTORE_UFLOW		s2BIT(31)
#define TXGXS_TX_SM_ERR			s2BIT(39)

	u64 xgxs_txgxs_err_mask;
	u64 xgxs_txgxs_err_alarm;

	u64 xgxs_rxgxs_err_reg;
#define RXGXS_ESTORE_OFLOW		s2BIT(7)
#define RXGXS_RX_SM_ERR			s2BIT(39)
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
#define SPI_CONTROL_SEL1		s2BIT(4)
#define SPI_CONTROL_REQ			s2BIT(7)
#define SPI_CONTROL_NACK		s2BIT(5)
#define SPI_CONTROL_DONE		s2BIT(6)
	u64 spi_data;
#define SPI_DATA_WRITE(data,len)	vBIT(data,0,len)
};

#define XENA_REG_SPACE	sizeof(struct XENA_dev_config)
#define	XENA_EEPROM_SPACE (0x01 << 11)

#endif				/* _REGS_H */
