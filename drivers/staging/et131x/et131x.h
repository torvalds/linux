/*
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#define DRIVER_NAME "et131x"
#define DRIVER_VERSION "v2.0"

/* EEPROM registers */

/* LBCIF Register Groups (addressed via 32-bit offsets) */
#define LBCIF_DWORD0_GROUP       0xAC
#define LBCIF_DWORD1_GROUP       0xB0

/* LBCIF Registers (addressed via 8-bit offsets) */
#define LBCIF_ADDRESS_REGISTER   0xAC
#define LBCIF_DATA_REGISTER      0xB0
#define LBCIF_CONTROL_REGISTER   0xB1
#define LBCIF_STATUS_REGISTER    0xB2

/* LBCIF Control Register Bits */
#define LBCIF_CONTROL_SEQUENTIAL_READ   0x01
#define LBCIF_CONTROL_PAGE_WRITE        0x02
#define LBCIF_CONTROL_EEPROM_RELOAD     0x08
#define LBCIF_CONTROL_TWO_BYTE_ADDR     0x20
#define LBCIF_CONTROL_I2C_WRITE         0x40
#define LBCIF_CONTROL_LBCIF_ENABLE      0x80

/* LBCIF Status Register Bits */
#define LBCIF_STATUS_PHY_QUEUE_AVAIL    0x01
#define LBCIF_STATUS_I2C_IDLE           0x02
#define LBCIF_STATUS_ACK_ERROR          0x04
#define LBCIF_STATUS_GENERAL_ERROR      0x08
#define LBCIF_STATUS_CHECKSUM_ERROR     0x40
#define LBCIF_STATUS_EEPROM_PRESENT     0x80

/* START OF GLOBAL REGISTER ADDRESS MAP */

/*
 * 10bit registers
 *
 * Tx queue start address reg in global address map at address 0x0000
 * tx queue end address reg in global address map at address 0x0004
 * rx queue start address reg in global address map at address 0x0008
 * rx queue end address reg in global address map at address 0x000C
 */

/*
 * structure for power management control status reg in global address map
 * located at address 0x0010
 *	jagcore_rx_rdy	bit 9
 *	jagcore_tx_rdy	bit 8
 *	phy_lped_en	bit 7
 *	phy_sw_coma	bit 6
 *	rxclk_gate	bit 5
 *	txclk_gate	bit 4
 *	sysclk_gate	bit 3
 *	jagcore_rx_en	bit 2
 *	jagcore_tx_en	bit 1
 *	gigephy_en	bit 0
 */

#define ET_PM_PHY_SW_COMA		0x40
#define ET_PMCSR_INIT			0x38

/*
 * Interrupt status reg at address 0x0018
 */

#define	ET_INTR_TXDMA_ISR	0x00000008
#define ET_INTR_TXDMA_ERR	0x00000010
#define ET_INTR_RXDMA_XFR_DONE	0x00000020
#define ET_INTR_RXDMA_FB_R0_LOW	0x00000040
#define ET_INTR_RXDMA_FB_R1_LOW	0x00000080
#define ET_INTR_RXDMA_STAT_LOW	0x00000100
#define ET_INTR_RXDMA_ERR	0x00000200
#define ET_INTR_WATCHDOG	0x00004000
#define ET_INTR_WOL		0x00008000
#define ET_INTR_PHY		0x00010000
#define ET_INTR_TXMAC		0x00020000
#define ET_INTR_RXMAC		0x00040000
#define ET_INTR_MAC_STAT	0x00080000
#define ET_INTR_SLV_TIMEOUT	0x00100000

/*
 * Interrupt mask register at address 0x001C
 * Interrupt alias clear mask reg at address 0x0020
 * Interrupt status alias reg at address 0x0024
 *
 * Same masks as above
 */

/*
 * Software reset reg at address 0x0028
 * 0:	txdma_sw_reset
 * 1:	rxdma_sw_reset
 * 2:	txmac_sw_reset
 * 3:	rxmac_sw_reset
 * 4:	mac_sw_reset
 * 5:	mac_stat_sw_reset
 * 6:	mmc_sw_reset
 *31:	selfclr_disable
 */

#define ET_RESET_ALL	0x007F;

/*
 * SLV Timer reg at address 0x002C (low 24 bits)
 */

/*
 * MSI Configuration reg at address 0x0030
 */

#define ET_MSI_VECTOR	0x0000001F
#define ET_MSI_TC	0x00070000

/*
 * Loopback reg located at address 0x0034
 */

#define ET_LOOP_MAC	0x00000001
#define ET_LOOP_DMA	0x00000002

/*
 * GLOBAL Module of JAGCore Address Mapping
 * Located at address 0x0000
 */
struct global_regs {				/* Location: */
	u32 txq_start_addr;			/*  0x0000 */
	u32 txq_end_addr;			/*  0x0004 */
	u32 rxq_start_addr;			/*  0x0008 */
	u32 rxq_end_addr;			/*  0x000C */
	u32 pm_csr;				/*  0x0010 */
	u32 unused;				/*  0x0014 */
	u32 int_status;				/*  0x0018 */
	u32 int_mask;				/*  0x001C */
	u32 int_alias_clr_en;			/*  0x0020 */
	u32 int_status_alias;			/*  0x0024 */
	u32 sw_reset;				/*  0x0028 */
	u32 slv_timer;				/*  0x002C */
	u32 msi_config;				/*  0x0030 */
	u32 loopback;				/*  0x0034 */
	u32 watchdog_timer;			/*  0x0038 */
};


/* START OF TXDMA REGISTER ADDRESS MAP */

/*
 * txdma control status reg at address 0x1000
 */

#define ET_TXDMA_CSR_HALT	0x00000001
#define ET_TXDMA_DROP_TLP	0x00000002
#define ET_TXDMA_CACHE_THRS	0x000000F0
#define ET_TXDMA_CACHE_SHIFT	4
#define ET_TXDMA_SNGL_EPKT	0x00000100
#define ET_TXDMA_CLASS		0x00001E00

/*
 * structure for txdma packet ring base address hi reg in txdma address map
 * located at address 0x1004
 * Defined earlier (u32)
 */

/*
 * structure for txdma packet ring base address low reg in txdma address map
 * located at address 0x1008
 * Defined earlier (u32)
 */

/*
 * structure for txdma packet ring number of descriptor reg in txdma address
 * map.  Located at address 0x100C
 *
 * 31-10: unused
 * 9-0: pr ndes
 */

#define ET_DMA12_MASK		0x0FFF	/* 12 bit mask for DMA12W types */
#define ET_DMA12_WRAP		0x1000
#define ET_DMA10_MASK		0x03FF	/* 10 bit mask for DMA10W types */
#define ET_DMA10_WRAP		0x0400
#define ET_DMA4_MASK		0x000F	/* 4 bit mask for DMA4W types */
#define ET_DMA4_WRAP		0x0010

#define INDEX12(x)	((x) & ET_DMA12_MASK)
#define INDEX10(x)	((x) & ET_DMA10_MASK)
#define INDEX4(x)	((x) & ET_DMA4_MASK)

/*
 * 10bit DMA with wrap
 * txdma tx queue write address reg in txdma address map at 0x1010
 * txdma tx queue write address external reg in txdma address map at 0x1014
 * txdma tx queue read address reg in txdma address map at 0x1018
 *
 * u32
 * txdma status writeback address hi reg in txdma address map at0x101C
 * txdma status writeback address lo reg in txdma address map at 0x1020
 *
 * 10bit DMA with wrap
 * txdma service request reg in txdma address map at 0x1024
 * structure for txdma service complete reg in txdma address map at 0x1028
 *
 * 4bit DMA with wrap
 * txdma tx descriptor cache read index reg in txdma address map at 0x102C
 * txdma tx descriptor cache write index reg in txdma address map at 0x1030
 *
 * txdma error reg in txdma address map at address 0x1034
 * 0: PyldResend
 * 1: PyldRewind
 * 4: DescrResend
 * 5: DescrRewind
 * 8: WrbkResend
 * 9: WrbkRewind
 */

/*
 * Tx DMA Module of JAGCore Address Mapping
 * Located at address 0x1000
 */
struct txdma_regs {			/* Location: */
	u32 csr;			/*  0x1000 */
	u32 pr_base_hi;			/*  0x1004 */
	u32 pr_base_lo;			/*  0x1008 */
	u32 pr_num_des;			/*  0x100C */
	u32 txq_wr_addr;		/*  0x1010 */
	u32 txq_wr_addr_ext;		/*  0x1014 */
	u32 txq_rd_addr;		/*  0x1018 */
	u32 dma_wb_base_hi;		/*  0x101C */
	u32 dma_wb_base_lo;		/*  0x1020 */
	u32 service_request;		/*  0x1024 */
	u32 service_complete;		/*  0x1028 */
	u32 cache_rd_index;		/*  0x102C */
	u32 cache_wr_index;		/*  0x1030 */
	u32 tx_dma_error;		/*  0x1034 */
	u32 desc_abort_cnt;		/*  0x1038 */
	u32 payload_abort_cnt;		/*  0x103c */
	u32 writeback_abort_cnt;	/*  0x1040 */
	u32 desc_timeout_cnt;		/*  0x1044 */
	u32 payload_timeout_cnt;	/*  0x1048 */
	u32 writeback_timeout_cnt;	/*  0x104c */
	u32 desc_error_cnt;		/*  0x1050 */
	u32 payload_error_cnt;		/*  0x1054 */
	u32 writeback_error_cnt;	/*  0x1058 */
	u32 dropped_tlp_cnt;		/*  0x105c */
	u32 new_service_complete;	/*  0x1060 */
	u32 ethernet_packet_cnt;	/*  0x1064 */
};

/* END OF TXDMA REGISTER ADDRESS MAP */


/* START OF RXDMA REGISTER ADDRESS MAP */

/*
 * structure for control status reg in rxdma address map
 * Located at address 0x2000
 *
 * CSR
 * 0: halt
 * 1-3: tc
 * 4: fbr_big_endian
 * 5: psr_big_endian
 * 6: pkt_big_endian
 * 7: dma_big_endian
 * 8-9: fbr0_size
 * 10: fbr0_enable
 * 11-12: fbr1_size
 * 13: fbr1_enable
 * 14: unused
 * 15: pkt_drop_disable
 * 16: pkt_done_flush
 * 17: halt_status
 * 18-31: unused
 */

#define ET_RXDMA_CSR_HALT		0x0001
#define ET_RXDMA_CSR_FBR0_SIZE_LO	0x0100
#define ET_RXDMA_CSR_FBR0_SIZE_HI	0x0200
#define ET_RXDMA_CSR_FBR0_ENABLE	0x0400
#define ET_RXDMA_CSR_FBR1_SIZE_LO	0x0800
#define ET_RXDMA_CSR_FBR1_SIZE_HI	0x1000
#define ET_RXDMA_CSR_FBR1_ENABLE	0x2000
#define ET_RXDMA_CSR_HALT_STATUS	0x00020000

/*
 * structure for dma writeback lo reg in rxdma address map
 * located at address 0x2004
 * Defined earlier (u32)
 */

/*
 * structure for dma writeback hi reg in rxdma address map
 * located at address 0x2008
 * Defined earlier (u32)
 */

/*
 * structure for number of packets done reg in rxdma address map
 * located at address 0x200C
 *
 * 31-8: unused
 * 7-0: num done
 */

/*
 * structure for max packet time reg in rxdma address map
 * located at address 0x2010
 *
 * 31-18: unused
 * 17-0: time done
 */

/*
 * structure for rx queue read address reg in rxdma address map
 * located at address 0x2014
 * Defined earlier (u32)
 */

/*
 * structure for rx queue read address external reg in rxdma address map
 * located at address 0x2018
 * Defined earlier (u32)
 */

/*
 * structure for rx queue write address reg in rxdma address map
 * located at address 0x201C
 * Defined earlier (u32)
 */

/*
 * structure for packet status ring base address lo reg in rxdma address map
 * located at address 0x2020
 * Defined earlier (u32)
 */

/*
 * structure for packet status ring base address hi reg in rxdma address map
 * located at address 0x2024
 * Defined earlier (u32)
 */

/*
 * structure for packet status ring number of descriptors reg in rxdma address
 * map.  Located at address 0x2028
 *
 * 31-12: unused
 * 11-0: psr ndes
 */

#define ET_RXDMA_PSR_NUM_DES_MASK	0xFFF;

/*
 * structure for packet status ring available offset reg in rxdma address map
 * located at address 0x202C
 *
 * 31-13: unused
 * 12: psr avail wrap
 * 11-0: psr avail
 */

/*
 * structure for packet status ring full offset reg in rxdma address map
 * located at address 0x2030
 *
 * 31-13: unused
 * 12: psr full wrap
 * 11-0: psr full
 */

/*
 * structure for packet status ring access index reg in rxdma address map
 * located at address 0x2034
 *
 * 31-5: unused
 * 4-0: psr_ai
 */

/*
 * structure for packet status ring minimum descriptors reg in rxdma address
 * map.  Located at address 0x2038
 *
 * 31-12: unused
 * 11-0: psr_min
 */

/*
 * structure for free buffer ring base lo address reg in rxdma address map
 * located at address 0x203C
 * Defined earlier (u32)
 */

/*
 * structure for free buffer ring base hi address reg in rxdma address map
 * located at address 0x2040
 * Defined earlier (u32)
 */

/*
 * structure for free buffer ring number of descriptors reg in rxdma address
 * map.  Located at address 0x2044
 *
 * 31-10: unused
 * 9-0: fbr ndesc
 */

/*
 * structure for free buffer ring 0 available offset reg in rxdma address map
 * located at address 0x2048
 * Defined earlier (u32)
 */

/*
 * structure for free buffer ring 0 full offset reg in rxdma address map
 * located at address 0x204C
 * Defined earlier (u32)
 */

/*
 * structure for free buffer cache 0 full offset reg in rxdma address map
 * located at address 0x2050
 *
 * 31-5: unused
 * 4-0: fbc rdi
 */

/*
 * structure for free buffer ring 0 minimum descriptor reg in rxdma address map
 * located at address 0x2054
 *
 * 31-10: unused
 * 9-0: fbr min
 */

/*
 * structure for free buffer ring 1 base address lo reg in rxdma address map
 * located at address 0x2058 - 0x205C
 * Defined earlier (RXDMA_FBR_BASE_LO_t and RXDMA_FBR_BASE_HI_t)
 */

/*
 * structure for free buffer ring 1 number of descriptors reg in rxdma address
 * map.  Located at address 0x2060
 * Defined earlier (RXDMA_FBR_NUM_DES_t)
 */

/*
 * structure for free buffer ring 1 available offset reg in rxdma address map
 * located at address 0x2064
 * Defined Earlier (RXDMA_FBR_AVAIL_OFFSET_t)
 */

/*
 * structure for free buffer ring 1 full offset reg in rxdma address map
 * located at address 0x2068
 * Defined Earlier (RXDMA_FBR_FULL_OFFSET_t)
 */

/*
 * structure for free buffer cache 1 read index reg in rxdma address map
 * located at address 0x206C
 * Defined Earlier (RXDMA_FBC_RD_INDEX_t)
 */

/*
 * structure for free buffer ring 1 minimum descriptor reg in rxdma address map
 * located at address 0x2070
 * Defined Earlier (RXDMA_FBR_MIN_DES_t)
 */

/*
 * Rx DMA Module of JAGCore Address Mapping
 * Located at address 0x2000
 */
struct rxdma_regs {					/* Location: */
	u32 csr;					/*  0x2000 */
	u32 dma_wb_base_lo;				/*  0x2004 */
	u32 dma_wb_base_hi;				/*  0x2008 */
	u32 num_pkt_done;				/*  0x200C */
	u32 max_pkt_time;				/*  0x2010 */
	u32 rxq_rd_addr;				/*  0x2014 */
	u32 rxq_rd_addr_ext;				/*  0x2018 */
	u32 rxq_wr_addr;				/*  0x201C */
	u32 psr_base_lo;				/*  0x2020 */
	u32 psr_base_hi;				/*  0x2024 */
	u32 psr_num_des;				/*  0x2028 */
	u32 psr_avail_offset;				/*  0x202C */
	u32 psr_full_offset;				/*  0x2030 */
	u32 psr_access_index;				/*  0x2034 */
	u32 psr_min_des;				/*  0x2038 */
	u32 fbr0_base_lo;				/*  0x203C */
	u32 fbr0_base_hi;				/*  0x2040 */
	u32 fbr0_num_des;				/*  0x2044 */
	u32 fbr0_avail_offset;				/*  0x2048 */
	u32 fbr0_full_offset;				/*  0x204C */
	u32 fbr0_rd_index;				/*  0x2050 */
	u32 fbr0_min_des;				/*  0x2054 */
	u32 fbr1_base_lo;				/*  0x2058 */
	u32 fbr1_base_hi;				/*  0x205C */
	u32 fbr1_num_des;				/*  0x2060 */
	u32 fbr1_avail_offset;				/*  0x2064 */
	u32 fbr1_full_offset;				/*  0x2068 */
	u32 fbr1_rd_index;				/*  0x206C */
	u32 fbr1_min_des;				/*  0x2070 */
};

/* END OF RXDMA REGISTER ADDRESS MAP */


/* START OF TXMAC REGISTER ADDRESS MAP */

/*
 * structure for control reg in txmac address map
 * located at address 0x3000
 *
 * bits
 * 31-8: unused
 * 7: cklseg_disable
 * 6: ckbcnt_disable
 * 5: cksegnum
 * 4: async_disable
 * 3: fc_disable
 * 2: mcif_disable
 * 1: mif_disable
 * 0: txmac_en
 */

#define ET_TX_CTRL_FC_DISABLE	0x0008
#define ET_TX_CTRL_TXMAC_ENABLE	0x0001

/*
 * structure for shadow pointer reg in txmac address map
 * located at address 0x3004
 * 31-27: reserved
 * 26-16: txq rd ptr
 * 15-11: reserved
 * 10-0: txq wr ptr
 */

/*
 * structure for error count reg in txmac address map
 * located at address 0x3008
 *
 * 31-12: unused
 * 11-8: reserved
 * 7-4: txq_underrun
 * 3-0: fifo_underrun
 */

/*
 * structure for max fill reg in txmac address map
 * located at address 0x300C
 * 31-12: unused
 * 11-0: max fill
 */

/*
 * structure for cf parameter reg in txmac address map
 * located at address 0x3010
 * 31-16: cfep
 * 15-0: cfpt
 */

/*
 * structure for tx test reg in txmac address map
 * located at address 0x3014
 * 31-17: unused
 * 16: reserved
 * 15: txtest_en
 * 14-11: unused
 * 10-0: txq test pointer
 */

/*
 * structure for error reg in txmac address map
 * located at address 0x3018
 *
 * 31-9: unused
 * 8: fifo_underrun
 * 7-6: unused
 * 5: ctrl2_err
 * 4: txq_underrun
 * 3: bcnt_err
 * 2: lseg_err
 * 1: segnum_err
 * 0: seg0_err
 */

/*
 * structure for error interrupt reg in txmac address map
 * located at address 0x301C
 *
 * 31-9: unused
 * 8: fifo_underrun
 * 7-6: unused
 * 5: ctrl2_err
 * 4: txq_underrun
 * 3: bcnt_err
 * 2: lseg_err
 * 1: segnum_err
 * 0: seg0_err
 */

/*
 * structure for error interrupt reg in txmac address map
 * located at address 0x3020
 *
 * 31-2: unused
 * 1: bp_req
 * 0: bp_xonxoff
 */

/*
 * Tx MAC Module of JAGCore Address Mapping
 */
struct txmac_regs {			/* Location: */
	u32 ctl;			/*  0x3000 */
	u32 shadow_ptr;			/*  0x3004 */
	u32 err_cnt;			/*  0x3008 */
	u32 max_fill;			/*  0x300C */
	u32 cf_param;			/*  0x3010 */
	u32 tx_test;			/*  0x3014 */
	u32 err;			/*  0x3018 */
	u32 err_int;			/*  0x301C */
	u32 bp_ctrl;			/*  0x3020 */
};

/* END OF TXMAC REGISTER ADDRESS MAP */

/* START OF RXMAC REGISTER ADDRESS MAP */

/*
 * structure for rxmac control reg in rxmac address map
 * located at address 0x4000
 *
 * 31-7: reserved
 * 6: rxmac_int_disable
 * 5: async_disable
 * 4: mif_disable
 * 3: wol_disable
 * 2: pkt_filter_disable
 * 1: mcif_disable
 * 0: rxmac_en
 */

#define ET_RX_CTRL_WOL_DISABLE	0x0008
#define ET_RX_CTRL_RXMAC_ENABLE	0x0001

/*
 * structure for Wake On Lan Control and CRC 0 reg in rxmac address map
 * located at address 0x4004
 * 31-16: crc
 * 15-12: reserved
 * 11: ignore_pp
 * 10: ignore_mp
 * 9: clr_intr
 * 8: ignore_link_chg
 * 7: ignore_uni
 * 6: ignore_multi
 * 5: ignore_broad
 * 4-0: valid_crc 4-0
 */

/*
 * structure for CRC 1 and CRC 2 reg in rxmac address map
 * located at address 0x4008
 *
 * 31-16: crc2
 * 15-0: crc1
 */

/*
 * structure for CRC 3 and CRC 4 reg in rxmac address map
 * located at address 0x400C
 *
 * 31-16: crc4
 * 15-0: crc3
 */

/*
 * structure for Wake On Lan Source Address Lo reg in rxmac address map
 * located at address 0x4010
 *
 * 31-24: sa3
 * 23-16: sa4
 * 15-8: sa5
 * 7-0: sa6
 */

#define ET_RX_WOL_LO_SA3_SHIFT 24
#define ET_RX_WOL_LO_SA4_SHIFT 16
#define ET_RX_WOL_LO_SA5_SHIFT 8

/*
 * structure for Wake On Lan Source Address Hi reg in rxmac address map
 * located at address 0x4014
 *
 * 31-16: reserved
 * 15-8: sa1
 * 7-0: sa2
 */

#define ET_RX_WOL_HI_SA1_SHIFT 8

/*
 * structure for Wake On Lan mask reg in rxmac address map
 * located at address 0x4018 - 0x4064
 * Defined earlier (u32)
 */

/*
 * structure for Unicast Packet Filter Address 1 reg in rxmac address map
 * located at address 0x4068
 *
 * 31-24: addr1_3
 * 23-16: addr1_4
 * 15-8: addr1_5
 * 7-0: addr1_6
 */

#define ET_RX_UNI_PF_ADDR1_3_SHIFT 24
#define ET_RX_UNI_PF_ADDR1_4_SHIFT 16
#define ET_RX_UNI_PF_ADDR1_5_SHIFT 8

/*
 * structure for Unicast Packet Filter Address 2 reg in rxmac address map
 * located at address 0x406C
 *
 * 31-24: addr2_3
 * 23-16: addr2_4
 * 15-8: addr2_5
 * 7-0: addr2_6
 */

#define ET_RX_UNI_PF_ADDR2_3_SHIFT 24
#define ET_RX_UNI_PF_ADDR2_4_SHIFT 16
#define ET_RX_UNI_PF_ADDR2_5_SHIFT 8

/*
 * structure for Unicast Packet Filter Address 1 & 2 reg in rxmac address map
 * located at address 0x4070
 *
 * 31-24: addr2_1
 * 23-16: addr2_2
 * 15-8: addr1_1
 * 7-0: addr1_2
 */

#define ET_RX_UNI_PF_ADDR2_1_SHIFT 24
#define ET_RX_UNI_PF_ADDR2_2_SHIFT 16
#define ET_RX_UNI_PF_ADDR1_1_SHIFT 8

/*
 * structure for Multicast Hash reg in rxmac address map
 * located at address 0x4074 - 0x4080
 * Defined earlier (u32)
 */

/*
 * structure for Packet Filter Control reg in rxmac address map
 * located at address 0x4084
 *
 * 31-23: unused
 * 22-16: min_pkt_size
 * 15-4: unused
 * 3: filter_frag_en
 * 2: filter_uni_en
 * 1: filter_multi_en
 * 0: filter_broad_en
 */

#define ET_RX_PFCTRL_MIN_PKT_SZ_SHIFT		16;
#define ET_RX_PFCTRL_FRAG_FILTER_ENABLE		0x0008;
#define ET_RX_PFCTRL_UNICST_FILTER_ENABLE	0x0004;
#define ET_RX_PFCTRL_MLTCST_FILTER_ENABLE	0x0002;
#define ET_RX_PFCTRL_BRDCST_FILTER_ENABLE	0x0001;

/*
 * structure for Memory Controller Interface Control Max Segment reg in rxmac
 * address map.  Located at address 0x4088
 *
 * 31-10: reserved
 * 9-2: max_size
 * 1: fc_en
 * 0: seg_en
 */

#define ET_RX_MCIF_CTRL_MAX_SEG_SIZE_SHIFT	2;
#define ET_RX_MCIF_CTRL_MAX_SEG_FC_ENABLE	0x0002;
#define ET_RX_MCIF_CTRL_MAX_SEG_ENABLE		0x0001;

/*
 * structure for Memory Controller Interface Water Mark reg in rxmac address
 * map.  Located at address 0x408C
 *
 * 31-26: unused
 * 25-16: mark_hi
 * 15-10: unused
 * 9-0: mark_lo
 */

/*
 * structure for Rx Queue Dialog reg in rxmac address map.
 * located at address 0x4090
 *
 * 31-26: reserved
 * 25-16: rd_ptr
 * 15-10: reserved
 * 9-0: wr_ptr
 */

/*
 * structure for space available reg in rxmac address map.
 * located at address 0x4094
 *
 * 31-17: reserved
 * 16: space_avail_en
 * 15-10: reserved
 * 9-0: space_avail
 */

/*
 * structure for management interface reg in rxmac address map.
 * located at address 0x4098
 *
 * 31-18: reserved
 * 17: drop_pkt_en
 * 16-0: drop_pkt_mask
 */

/*
 * structure for Error reg in rxmac address map.
 * located at address 0x409C
 *
 * 31-4: unused
 * 3: mif
 * 2: async
 * 1: pkt_filter
 * 0: mcif
 */

/*
 * Rx MAC Module of JAGCore Address Mapping
 */
struct rxmac_regs {					/* Location: */
	u32 ctrl;					/*  0x4000 */
	u32 crc0;					/*  0x4004 */
	u32 crc12;					/*  0x4008 */
	u32 crc34;					/*  0x400C */
	u32 sa_lo;					/*  0x4010 */
	u32 sa_hi;					/*  0x4014 */
	u32 mask0_word0;				/*  0x4018 */
	u32 mask0_word1;				/*  0x401C */
	u32 mask0_word2;				/*  0x4020 */
	u32 mask0_word3;				/*  0x4024 */
	u32 mask1_word0;				/*  0x4028 */
	u32 mask1_word1;				/*  0x402C */
	u32 mask1_word2;				/*  0x4030 */
	u32 mask1_word3;				/*  0x4034 */
	u32 mask2_word0;				/*  0x4038 */
	u32 mask2_word1;				/*  0x403C */
	u32 mask2_word2;				/*  0x4040 */
	u32 mask2_word3;				/*  0x4044 */
	u32 mask3_word0;				/*  0x4048 */
	u32 mask3_word1;				/*  0x404C */
	u32 mask3_word2;				/*  0x4050 */
	u32 mask3_word3;				/*  0x4054 */
	u32 mask4_word0;				/*  0x4058 */
	u32 mask4_word1;				/*  0x405C */
	u32 mask4_word2;				/*  0x4060 */
	u32 mask4_word3;				/*  0x4064 */
	u32 uni_pf_addr1;				/*  0x4068 */
	u32 uni_pf_addr2;				/*  0x406C */
	u32 uni_pf_addr3;				/*  0x4070 */
	u32 multi_hash1;				/*  0x4074 */
	u32 multi_hash2;				/*  0x4078 */
	u32 multi_hash3;				/*  0x407C */
	u32 multi_hash4;				/*  0x4080 */
	u32 pf_ctrl;					/*  0x4084 */
	u32 mcif_ctrl_max_seg;				/*  0x4088 */
	u32 mcif_water_mark;				/*  0x408C */
	u32 rxq_diag;					/*  0x4090 */
	u32 space_avail;				/*  0x4094 */

	u32 mif_ctrl;					/*  0x4098 */
	u32 err_reg;					/*  0x409C */
};

/* END OF RXMAC REGISTER ADDRESS MAP */

/* START OF MAC REGISTER ADDRESS MAP */

/*
 * structure for configuration #1 reg in mac address map.
 * located at address 0x5000
 *
 * 31: soft reset
 * 30: sim reset
 * 29-20: reserved
 * 19: reset rx mc
 * 18: reset tx mc
 * 17: reset rx func
 * 16: reset tx fnc
 * 15-9: reserved
 * 8: loopback
 * 7-6: reserved
 * 5: rx flow
 * 4: tx flow
 * 3: syncd rx en
 * 2: rx enable
 * 1: syncd tx en
 * 0: tx enable
 */

#define ET_MAC_CFG1_SOFT_RESET		0x80000000
#define ET_MAC_CFG1_SIM_RESET		0x40000000
#define ET_MAC_CFG1_RESET_RXMC		0x00080000
#define ET_MAC_CFG1_RESET_TXMC		0x00040000
#define ET_MAC_CFG1_RESET_RXFUNC	0x00020000
#define ET_MAC_CFG1_RESET_TXFUNC	0x00010000
#define ET_MAC_CFG1_LOOPBACK		0x00000100
#define ET_MAC_CFG1_RX_FLOW		0x00000020
#define ET_MAC_CFG1_TX_FLOW		0x00000010
#define ET_MAC_CFG1_RX_ENABLE		0x00000004
#define ET_MAC_CFG1_TX_ENABLE		0x00000001
#define ET_MAC_CFG1_WAIT		0x0000000A	/* RX & TX syncd */

/*
 * structure for configuration #2 reg in mac address map.
 * located at address 0x5004
 * 31-16: reserved
 * 15-12: preamble
 * 11-10: reserved
 * 9-8: if mode
 * 7-6: reserved
 * 5: huge frame
 * 4: length check
 * 3: undefined
 * 2: pad crc
 * 1: crc enable
 * 0: full duplex
 */

#define ET_MAC_CFG2_PREAMBLE_SHIFT	12;
#define ET_MAC_CFG2_IFMODE_MASK		0x0300;
#define ET_MAC_CFG2_IFMODE_1000		0x0200;
#define ET_MAC_CFG2_IFMODE_100		0x0100;
#define ET_MAC_CFG2_IFMODE_HUGE_FRAME	0x0020;
#define ET_MAC_CFG2_IFMODE_LEN_CHECK	0x0010;
#define ET_MAC_CFG2_IFMODE_PAD_CRC	0x0004;
#define ET_MAC_CFG2_IFMODE_CRC_ENABLE	0x0002;
#define ET_MAC_CFG2_IFMODE_FULL_DPLX	0x0001;

/*
 * structure for Interpacket gap reg in mac address map.
 * located at address 0x5008
 *
 * 31: reserved
 * 30-24: non B2B ipg 1
 * 23: undefined
 * 22-16: non B2B ipg 2
 * 15-8: Min ifg enforce
 * 7-0: B2B ipg
 *
 * structure for half duplex reg in mac address map.
 * located at address 0x500C
 * 31-24: reserved
 * 23-20: Alt BEB trunc
 * 19: Alt BEB enable
 * 18: BP no backoff
 * 17: no backoff
 * 16: excess defer
 * 15-12: re-xmit max
 * 11-10: reserved
 * 9-0: collision window
 */

/*
 * structure for Maximum Frame Length reg in mac address map.
 * located at address 0x5010: bits 0-15 hold the length.
 */

/*
 * structure for Reserve 1 reg in mac address map.
 * located at address 0x5014 - 0x5018
 * Defined earlier (u32)
 */

/*
 * structure for Test reg in mac address map.
 * located at address 0x501C
 * test: bits 0-2, rest unused
 */

/*
 * structure for MII Management Configuration reg in mac address map.
 * located at address 0x5020
 *
 * 31: reset MII mgmt
 * 30-6: unused
 * 5: scan auto increment
 * 4: preamble suppress
 * 3: undefined
 * 2-0: mgmt clock reset
 */

#define ET_MAC_MIIMGMT_CLK_RST	0x0007

/*
 * structure for MII Management Command reg in mac address map.
 * located at address 0x5024
 * bit 1: scan cycle
 * bit 0: read cycle
 */

/*
 * structure for MII Management Address reg in mac address map.
 * located at address 0x5028
 * 31-13: reserved
 * 12-8: phy addr
 * 7-5: reserved
 * 4-0: register
 */

#define ET_MAC_MII_ADDR(phy, reg)	((phy) << 8 | (reg))

/*
 * structure for MII Management Control reg in mac address map.
 * located at address 0x502C
 * 31-16: reserved
 * 15-0: phy control
 */

/*
 * structure for MII Management Status reg in mac address map.
 * located at address 0x5030
 * 31-16: reserved
 * 15-0: phy control
 */

#define ET_MAC_MIIMGMT_STAT_PHYCRTL_MASK 0xFFFF;

/*
 * structure for MII Management Indicators reg in mac address map.
 * located at address 0x5034
 * 31-3: reserved
 * 2: not valid
 * 1: scanning
 * 0: busy
 */

#define ET_MAC_MGMT_BUSY	0x00000001	/* busy */
#define ET_MAC_MGMT_WAIT	0x00000005	/* busy | not valid */

/*
 * structure for Interface Control reg in mac address map.
 * located at address 0x5038
 *
 * 31: reset if module
 * 30-28: reserved
 * 27: tbi mode
 * 26: ghd mode
 * 25: lhd mode
 * 24: phy mode
 * 23: reset per mii
 * 22-17: reserved
 * 16: speed
 * 15: reset pe100x
 * 14-11: reserved
 * 10: force quiet
 * 9: no cipher
 * 8: disable link fail
 * 7: reset gpsi
 * 6-1: reserved
 * 0: enable jabber protection
 */

#define ET_MAC_IFCTRL_GHDMODE	(1 << 26)
#define ET_MAC_IFCTRL_PHYMODE	(1 << 24)

/*
 * structure for Interface Status reg in mac address map.
 * located at address 0x503C
 *
 * 31-10: reserved
 * 9: excess_defer
 * 8: clash
 * 7: phy_jabber
 * 6: phy_link_ok
 * 5: phy_full_duplex
 * 4: phy_speed
 * 3: pe100x_link_fail
 * 2: pe10t_loss_carrier
 * 1: pe10t_sqe_error
 * 0: pe10t_jabber
 */

/*
 * structure for Mac Station Address, Part 1 reg in mac address map.
 * located at address 0x5040
 *
 * 31-24: Octet6
 * 23-16: Octet5
 * 15-8: Octet4
 * 7-0: Octet3
 */

#define ET_MAC_STATION_ADDR1_OC6_SHIFT 24
#define ET_MAC_STATION_ADDR1_OC5_SHIFT 16
#define ET_MAC_STATION_ADDR1_OC4_SHIFT 8

/*
 * structure for Mac Station Address, Part 2 reg in mac address map.
 * located at address 0x5044
 *
 * 31-24: Octet2
 * 23-16: Octet1
 * 15-0: reserved
 */

#define ET_MAC_STATION_ADDR2_OC2_SHIFT 24
#define ET_MAC_STATION_ADDR2_OC1_SHIFT 16

/*
 * MAC Module of JAGCore Address Mapping
 */
struct mac_regs {					/* Location: */
	u32 cfg1;					/*  0x5000 */
	u32 cfg2;					/*  0x5004 */
	u32 ipg;					/*  0x5008 */
	u32 hfdp;					/*  0x500C */
	u32 max_fm_len;					/*  0x5010 */
	u32 rsv1;					/*  0x5014 */
	u32 rsv2;					/*  0x5018 */
	u32 mac_test;					/*  0x501C */
	u32 mii_mgmt_cfg;				/*  0x5020 */
	u32 mii_mgmt_cmd;				/*  0x5024 */
	u32 mii_mgmt_addr;				/*  0x5028 */
	u32 mii_mgmt_ctrl;				/*  0x502C */
	u32 mii_mgmt_stat;				/*  0x5030 */
	u32 mii_mgmt_indicator;				/*  0x5034 */
	u32 if_ctrl;					/*  0x5038 */
	u32 if_stat;					/*  0x503C */
	u32 station_addr_1;				/*  0x5040 */
	u32 station_addr_2;				/*  0x5044 */
};

/* END OF MAC REGISTER ADDRESS MAP */

/* START OF MAC STAT REGISTER ADDRESS MAP */

/*
 * structure for Carry Register One and it's Mask Register reg located in mac
 * stat address map address 0x6130 and 0x6138.
 *
 * 31: tr64
 * 30: tr127
 * 29: tr255
 * 28: tr511
 * 27: tr1k
 * 26: trmax
 * 25: trmgv
 * 24-17: unused
 * 16: rbyt
 * 15: rpkt
 * 14: rfcs
 * 13: rmca
 * 12: rbca
 * 11: rxcf
 * 10: rxpf
 * 9: rxuo
 * 8: raln
 * 7: rflr
 * 6: rcde
 * 5: rcse
 * 4: rund
 * 3: rovr
 * 2: rfrg
 * 1: rjbr
 * 0: rdrp
 */

/*
 * structure for Carry Register Two Mask Register reg in mac stat address map.
 * located at address 0x613C
 *
 * 31-20: unused
 * 19: tjbr
 * 18: tfcs
 * 17: txcf
 * 16: tovr
 * 15: tund
 * 14: trfg
 * 13: tbyt
 * 12: tpkt
 * 11: tmca
 * 10: tbca
 * 9: txpf
 * 8: tdfr
 * 7: tedf
 * 6: tscl
 * 5: tmcl
 * 4: tlcl
 * 3: txcl
 * 2: tncl
 * 1: tpfh
 * 0: tdrp
 */

/*
 * MAC STATS Module of JAGCore Address Mapping
 */
struct macstat_regs {			/* Location: */
	u32 pad[32];			/*  0x6000 - 607C */

	/* Tx/Rx 0-64 Byte Frame Counter */
	u32 txrx_0_64_byte_frames;	/*  0x6080 */

	/* Tx/Rx 65-127 Byte Frame Counter */
	u32 txrx_65_127_byte_frames;	/*  0x6084 */

	/* Tx/Rx 128-255 Byte Frame Counter */
	u32 txrx_128_255_byte_frames;	/*  0x6088 */

	/* Tx/Rx 256-511 Byte Frame Counter */
	u32 txrx_256_511_byte_frames;	/*  0x608C */

	/* Tx/Rx 512-1023 Byte Frame Counter */
	u32 txrx_512_1023_byte_frames;	/*  0x6090 */

	/* Tx/Rx 1024-1518 Byte Frame Counter */
	u32 txrx_1024_1518_byte_frames;	/*  0x6094 */

	/* Tx/Rx 1519-1522 Byte Good VLAN Frame Count */
	u32 txrx_1519_1522_gvln_frames;	/*  0x6098 */

	/* Rx Byte Counter */
	u32 rx_bytes;			/*  0x609C */

	/* Rx Packet Counter */
	u32 rx_packets;			/*  0x60A0 */

	/* Rx FCS Error Counter */
	u32 rx_fcs_errs;		/*  0x60A4 */

	/* Rx Multicast Packet Counter */
	u32 rx_multicast_packets;	/*  0x60A8 */

	/* Rx Broadcast Packet Counter */
	u32 rx_broadcast_packets;	/*  0x60AC */

	/* Rx Control Frame Packet Counter */
	u32 rx_control_frames;		/*  0x60B0 */

	/* Rx Pause Frame Packet Counter */
	u32 rx_pause_frames;		/*  0x60B4 */

	/* Rx Unknown OP Code Counter */
	u32 rx_unknown_opcodes;		/*  0x60B8 */

	/* Rx Alignment Error Counter */
	u32 rx_align_errs;		/*  0x60BC */

	/* Rx Frame Length Error Counter */
	u32 rx_frame_len_errs;		/*  0x60C0 */

	/* Rx Code Error Counter */
	u32 rx_code_errs;		/*  0x60C4 */

	/* Rx Carrier Sense Error Counter */
	u32 rx_carrier_sense_errs;	/*  0x60C8 */

	/* Rx Undersize Packet Counter */
	u32 rx_undersize_packets;	/*  0x60CC */

	/* Rx Oversize Packet Counter */
	u32 rx_oversize_packets;	/*  0x60D0 */

	/* Rx Fragment Counter */
	u32 rx_fragment_packets;	/*  0x60D4 */

	/* Rx Jabber Counter */
	u32 rx_jabbers;			/*  0x60D8 */

	/* Rx Drop */
	u32 rx_drops;			/*  0x60DC */

	/* Tx Byte Counter */
	u32 tx_bytes;			/*  0x60E0 */

	/* Tx Packet Counter */
	u32 tx_packets;			/*  0x60E4 */

	/* Tx Multicast Packet Counter */
	u32 tx_multicast_packets;	/*  0x60E8 */

	/* Tx Broadcast Packet Counter */
	u32 tx_broadcast_packets;	/*  0x60EC */

	/* Tx Pause Control Frame Counter */
	u32 tx_pause_frames;		/*  0x60F0 */

	/* Tx Deferral Packet Counter */
	u32 tx_deferred;		/*  0x60F4 */

	/* Tx Excessive Deferral Packet Counter */
	u32 tx_excessive_deferred;	/*  0x60F8 */

	/* Tx Single Collision Packet Counter */
	u32 tx_single_collisions;	/*  0x60FC */

	/* Tx Multiple Collision Packet Counter */
	u32 tx_multiple_collisions;	/*  0x6100 */

	/* Tx Late Collision Packet Counter */
	u32 tx_late_collisions;		/*  0x6104 */

	/* Tx Excessive Collision Packet Counter */
	u32 tx_excessive_collisions;	/*  0x6108 */

	/* Tx Total Collision Packet Counter */
	u32 tx_total_collisions;	/*  0x610C */

	/* Tx Pause Frame Honored Counter */
	u32 tx_pause_honored_frames;	/*  0x6110 */

	/* Tx Drop Frame Counter */
	u32 tx_drops;			/*  0x6114 */

	/* Tx Jabber Frame Counter */
	u32 tx_jabbers;			/*  0x6118 */

	/* Tx FCS Error Counter */
	u32 tx_fcs_errs;		/*  0x611C */

	/* Tx Control Frame Counter */
	u32 tx_control_frames;		/*  0x6120 */

	/* Tx Oversize Frame Counter */
	u32 tx_oversize_frames;		/*  0x6124 */

	/* Tx Undersize Frame Counter */
	u32 tx_undersize_frames;	/*  0x6128 */

	/* Tx Fragments Frame Counter */
	u32 tx_fragments;		/*  0x612C */

	/* Carry Register One Register */
	u32 carry_reg1;			/*  0x6130 */

	/* Carry Register Two Register */
	u32 carry_reg2;			/*  0x6134 */

	/* Carry Register One Mask Register */
	u32 carry_reg1_mask;		/*  0x6138 */

	/* Carry Register Two Mask Register */
	u32 carry_reg2_mask;		/*  0x613C */
};

/* END OF MAC STAT REGISTER ADDRESS MAP */

/* START OF MMC REGISTER ADDRESS MAP */

/*
 * Main Memory Controller Control reg in mmc address map.
 * located at address 0x7000
 */

#define ET_MMC_ENABLE		1
#define ET_MMC_ARB_DISABLE	2
#define ET_MMC_RXMAC_DISABLE	4
#define ET_MMC_TXMAC_DISABLE	8
#define ET_MMC_TXDMA_DISABLE	16
#define ET_MMC_RXDMA_DISABLE	32
#define ET_MMC_FORCE_CE		64

/*
 * Main Memory Controller Host Memory Access Address reg in mmc
 * address map.  Located at address 0x7004. Top 16 bits hold the address bits
 */

#define ET_SRAM_REQ_ACCESS	1
#define ET_SRAM_WR_ACCESS	2
#define ET_SRAM_IS_CTRL		4

/*
 * structure for Main Memory Controller Host Memory Access Data reg in mmc
 * address map.  Located at address 0x7008 - 0x7014
 * Defined earlier (u32)
 */

/*
 * Memory Control Module of JAGCore Address Mapping
 */
struct mmc_regs {		/* Location: */
	u32 mmc_ctrl;		/*  0x7000 */
	u32 sram_access;	/*  0x7004 */
	u32 sram_word1;		/*  0x7008 */
	u32 sram_word2;		/*  0x700C */
	u32 sram_word3;		/*  0x7010 */
	u32 sram_word4;		/*  0x7014 */
};

/* END OF MMC REGISTER ADDRESS MAP */


/*
 * JAGCore Address Mapping
 */
struct address_map {
	struct global_regs global;
	/* unused section of global address map */
	u8 unused_global[4096 - sizeof(struct global_regs)];
	struct txdma_regs txdma;
	/* unused section of txdma address map */
	u8 unused_txdma[4096 - sizeof(struct txdma_regs)];
	struct rxdma_regs rxdma;
	/* unused section of rxdma address map */
	u8 unused_rxdma[4096 - sizeof(struct rxdma_regs)];
	struct txmac_regs txmac;
	/* unused section of txmac address map */
	u8 unused_txmac[4096 - sizeof(struct txmac_regs)];
	struct rxmac_regs rxmac;
	/* unused section of rxmac address map */
	u8 unused_rxmac[4096 - sizeof(struct rxmac_regs)];
	struct mac_regs mac;
	/* unused section of mac address map */
	u8 unused_mac[4096 - sizeof(struct mac_regs)];
	struct macstat_regs macstat;
	/* unused section of mac stat address map */
	u8 unused_mac_stat[4096 - sizeof(struct macstat_regs)];
	struct mmc_regs mmc;
	/* unused section of mmc address map */
	u8 unused_mmc[4096 - sizeof(struct mmc_regs)];
	/* unused section of address map */
	u8 unused_[1015808];

	u8 unused_exp_rom[4096];	/* MGS-size TBD */
	u8 unused__[524288];	/* unused section of address map */
};

/*
 * Defines for generic MII registers 0x00 -> 0x0F can be found in
 * include/linux/mii.h
 */

/* some defines for modem registers that seem to be 'reserved' */
#define PHY_INDEX_REG              0x10
#define PHY_DATA_REG               0x11
#define PHY_MPHY_CONTROL_REG       0x12

/* defines for specified registers */
#define PHY_LOOPBACK_CONTROL       0x13	/* TRU_VMI_LOOPBACK_CONTROL_1_REG 19 */
					/* TRU_VMI_LOOPBACK_CONTROL_2_REG 20 */
#define PHY_REGISTER_MGMT_CONTROL  0x15	/* TRU_VMI_MI_SEQ_CONTROL_REG     21 */
#define PHY_CONFIG                 0x16	/* TRU_VMI_CONFIGURATION_REG      22 */
#define PHY_PHY_CONTROL            0x17	/* TRU_VMI_PHY_CONTROL_REG        23 */
#define PHY_INTERRUPT_MASK         0x18	/* TRU_VMI_INTERRUPT_MASK_REG     24 */
#define PHY_INTERRUPT_STATUS       0x19	/* TRU_VMI_INTERRUPT_STATUS_REG   25 */
#define PHY_PHY_STATUS             0x1A	/* TRU_VMI_PHY_STATUS_REG         26 */
#define PHY_LED_1                  0x1B	/* TRU_VMI_LED_CONTROL_1_REG      27 */
#define PHY_LED_2                  0x1C	/* TRU_VMI_LED_CONTROL_2_REG      28 */
					/* TRU_VMI_LINK_CONTROL_REG       29 */
					/* TRU_VMI_TIMING_CONTROL_REG        */

/* MI Register 10: Gigabit basic mode status reg(Reg 0x0A) */
#define ET_1000BT_MSTR_SLV 0x4000

/* MI Register 16 - 18: Reserved Reg(0x10-0x12) */

/* MI Register 19: Loopback Control Reg(0x13)
 *	15:	mii_en
 *	14:	pcs_en
 *	13:	pmd_en
 *	12:	all_digital_en
 *	11:	replica_en
 *	10:	line_driver_en
 *	9-0:	reserved
 */

/* MI Register 20: Reserved Reg(0x14) */

/* MI Register 21: Management Interface Control Reg(0x15)
 *	15-11:	reserved
 *	10-4:	mi_error_count
 *	3:	reserved
 *	2:	ignore_10g_fr
 *	1:	reserved
 *	0:	preamble_suppress_en
 */

/* MI Register 22: PHY Configuration Reg(0x16)
 *	15:	crs_tx_en
 *	14:	reserved
 *	13-12:	tx_fifo_depth
 *	11-10:	speed_downshift
 *	9:	pbi_detect
 *	8:	tbi_rate
 *	7:	alternate_np
 *	6:	group_mdio_en
 *	5:	tx_clock_en
 *	4:	sys_clock_en
 *	3:	reserved
 *	2-0:	mac_if_mode
 */

#define ET_PHY_CONFIG_TX_FIFO_DEPTH	0x3000

#define ET_PHY_CONFIG_FIFO_DEPTH_8	0x0000
#define ET_PHY_CONFIG_FIFO_DEPTH_16	0x1000
#define ET_PHY_CONFIG_FIFO_DEPTH_32	0x2000
#define ET_PHY_CONFIG_FIFO_DEPTH_64	0x3000

/* MI Register 23: PHY CONTROL Reg(0x17)
 *	15:	reserved
 *	14:	tdr_en
 *	13:	reserved
 *	12-11:	downshift_attempts
 *	10-6:	reserved
 *	5:	jabber_10baseT
 *	4:	sqe_10baseT
 *	3:	tp_loopback_10baseT
 *	2:	preamble_gen_en
 *	1:	reserved
 *	0:	force_int
 */

/* MI Register 24: Interrupt Mask Reg(0x18)
 *	15-10:	reserved
 *	9:	mdio_sync_lost
 *	8:	autoneg_status
 *	7:	hi_bit_err
 *	6:	np_rx
 *	5:	err_counter_full
 *	4:	fifo_over_underflow
 *	3:	rx_status
 *	2:	link_status
 *	1:	automatic_speed
 *	0:	int_en
 */

/* MI Register 25: Interrupt Status Reg(0x19)
 *	15-10:	reserved
 *	9:	mdio_sync_lost
 *	8:	autoneg_status
 *	7:	hi_bit_err
 *	6:	np_rx
 *	5:	err_counter_full
 *	4:	fifo_over_underflow
 *	3:	rx_status
 *	2:	link_status
 *	1:	automatic_speed
 *	0:	int_en
 */

/* MI Register 26: PHY Status Reg(0x1A)
 *	15:	reserved
 *	14-13:	autoneg_fault
 *	12:	autoneg_status
 *	11:	mdi_x_status
 *	10:	polarity_status
 *	9-8:	speed_status
 *	7:	duplex_status
 *	6:	link_status
 *	5:	tx_status
 *	4:	rx_status
 *	3:	collision_status
 *	2:	autoneg_en
 *	1:	pause_en
 *	0:	asymmetric_dir
 */
#define ET_PHY_AUTONEG_STATUS	0x1000
#define ET_PHY_POLARITY_STATUS	0x0400
#define ET_PHY_SPEED_STATUS	0x0300
#define ET_PHY_DUPLEX_STATUS	0x0080
#define ET_PHY_LSTATUS		0x0040
#define ET_PHY_AUTONEG_ENABLE	0x0020

/* MI Register 27: LED Control Reg 1(0x1B)
 *	15-14:	reserved
 *	13-12:	led_dup_indicate
 *	11-10:	led_10baseT
 *	9-8:	led_collision
 *	7-4:	reserved
 *	3-2:	pulse_dur
 *	1:	pulse_stretch1
 *	0:	pulse_stretch0
 */

/* MI Register 28: LED Control Reg 2(0x1C)
 *	15-12:	led_link
 *	11-8:	led_tx_rx
 *	7-4:	led_100BaseTX
 *	3-0:	led_1000BaseT
 */
#define ET_LED2_LED_LINK	0xF000
#define ET_LED2_LED_TXRX	0x0F00
#define ET_LED2_LED_100TX	0x00F0
#define ET_LED2_LED_1000T	0x000F

/* defines for LED control reg 2 values */
#define LED_VAL_1000BT			0x0
#define LED_VAL_100BTX			0x1
#define LED_VAL_10BT			0x2
#define LED_VAL_1000BT_100BTX		0x3 /* 1000BT on, 100BTX blink */
#define LED_VAL_LINKON			0x4
#define LED_VAL_TX			0x5
#define LED_VAL_RX			0x6
#define LED_VAL_TXRX			0x7 /* TX or RX */
#define LED_VAL_DUPLEXFULL		0x8
#define LED_VAL_COLLISION		0x9
#define LED_VAL_LINKON_ACTIVE		0xA /* Link on, activity blink */
#define LED_VAL_LINKON_RECV		0xB /* Link on, receive blink */
#define LED_VAL_DUPLEXFULL_COLLISION	0xC /* Duplex on, collision blink */
#define LED_VAL_BLINK			0xD
#define LED_VAL_ON			0xE
#define LED_VAL_OFF			0xF

#define LED_LINK_SHIFT			12
#define LED_TXRX_SHIFT			8
#define LED_100TX_SHIFT			4

/* MI Register 29 - 31: Reserved Reg(0x1D - 0x1E) */
