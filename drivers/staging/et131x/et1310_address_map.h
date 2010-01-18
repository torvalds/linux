/*
 * Agere Systems Inc.
 * 10/100/1000 Base-T Ethernet Driver for the ET1301 and ET131x series MACs
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 *------------------------------------------------------------------------------
 *
 * et1310_address_map.h - Contains the register mapping for the ET1310
 *
 *------------------------------------------------------------------------------
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

#ifndef _ET1310_ADDRESS_MAP_H_
#define _ET1310_ADDRESS_MAP_H_


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
 * 0: 	txdma_sw_reset
 * 1:	rxdma_sw_reset
 * 2:	txmac_sw_reset
 * 3:	rxmac_sw_reset
 * 4:	mac_sw_reset
 * 5:	mac_stat_sw_reset
 * 6:	mmc_sw_reset
 *31:	selfclr_disable
 */

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
struct global_regs {			/* Location: */
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
	u32 loopback;			/*  0x0034 */
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

extern inline void add_10bit(u32 *v, int n)
{
	*v = INDEX10(*v + n) | (*v & ET_DMA10_WRAP);
}

extern inline void add_12bit(u32 *v, int n)
{
	*v = INDEX12(*v + n) | (*v & ET_DMA12_WRAP);
}

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
	u32 TxDmaError;			/*  0x1034 */
	u32 DescAbortCount;		/*  0x1038 */
	u32 PayloadAbortCnt;		/*  0x103c */
	u32 WriteBackAbortCnt;		/*  0x1040 */
	u32 DescTimeoutCnt;		/*  0x1044 */
	u32 PayloadTimeoutCnt;		/*  0x1048 */
	u32 WriteBackTimeoutCnt;	/*  0x104c */
	u32 DescErrorCount;		/*  0x1050 */
	u32 PayloadErrorCnt;		/*  0x1054 */
	u32 WriteBackErrorCnt;		/*  0x1058 */
	u32 DroppedTLPCount;		/*  0x105c */
	u32 NewServiceComplete;		/*  0x1060 */
	u32 EthernetPacketCount;	/*  0x1064 */
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
 * 16: reserved1
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
 */
typedef union _RXMAC_CTRL_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 reserved:25;		/* bits 7-31 */
		u32 rxmac_int_disable:1;	/* bit 6 */
		u32 async_disable:1;		/* bit 5 */
		u32 mif_disable:1;		/* bit 4 */
		u32 wol_disable:1;		/* bit 3 */
		u32 pkt_filter_disable:1;	/* bit 2 */
		u32 mcif_disable:1;		/* bit 1 */
		u32 rxmac_en:1;			/* bit 0 */
#else
		u32 rxmac_en:1;			/* bit 0 */
		u32 mcif_disable:1;		/* bit 1 */
		u32 pkt_filter_disable:1;	/* bit 2 */
		u32 wol_disable:1;		/* bit 3 */
		u32 mif_disable:1;		/* bit 4 */
		u32 async_disable:1;		/* bit 5 */
		u32 rxmac_int_disable:1;	/* bit 6 */
		u32 reserved:25;		/* bits 7-31 */
#endif
	} bits;
} RXMAC_CTRL_t, *PRXMAC_CTRL_t;

/*
 * structure for Wake On Lan Control and CRC 0 reg in rxmac address map
 * located at address 0x4004
 */
typedef union _RXMAC_WOL_CTL_CRC0_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 crc0:16;		/* bits 16-31 */
		u32 reserve:4;		/* bits 12-15 */
		u32 ignore_pp:1;	/* bit 11 */
		u32 ignore_mp:1;	/* bit 10 */
		u32 clr_intr:1;		/* bit 9 */
		u32 ignore_link_chg:1;	/* bit 8 */
		u32 ignore_uni:1;	/* bit 7 */
		u32 ignore_multi:1;	/* bit 6 */
		u32 ignore_broad:1;	/* bit 5 */
		u32 valid_crc4:1;	/* bit 4 */
		u32 valid_crc3:1;	/* bit 3 */
		u32 valid_crc2:1;	/* bit 2 */
		u32 valid_crc1:1;	/* bit 1 */
		u32 valid_crc0:1;	/* bit 0 */
#else
		u32 valid_crc0:1;	/* bit 0 */
		u32 valid_crc1:1;	/* bit 1 */
		u32 valid_crc2:1;	/* bit 2 */
		u32 valid_crc3:1;	/* bit 3 */
		u32 valid_crc4:1;	/* bit 4 */
		u32 ignore_broad:1;	/* bit 5 */
		u32 ignore_multi:1;	/* bit 6 */
		u32 ignore_uni:1;	/* bit 7 */
		u32 ignore_link_chg:1;	/* bit 8 */
		u32 clr_intr:1;		/* bit 9 */
		u32 ignore_mp:1;	/* bit 10 */
		u32 ignore_pp:1;	/* bit 11 */
		u32 reserve:4;		/* bits 12-15 */
		u32 crc0:16;		/* bits 16-31 */
#endif
	} bits;
} RXMAC_WOL_CTL_CRC0_t, *PRXMAC_WOL_CTL_CRC0_t;

/*
 * structure for CRC 1 and CRC 2 reg in rxmac address map
 * located at address 0x4008
 */
typedef union _RXMAC_WOL_CRC12_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 crc2:16;	/* bits 16-31 */
		u32 crc1:16;	/* bits 0-15 */
#else
		u32 crc1:16;	/* bits 0-15 */
		u32 crc2:16;	/* bits 16-31 */
#endif
	} bits;
} RXMAC_WOL_CRC12_t, *PRXMAC_WOL_CRC12_t;

/*
 * structure for CRC 3 and CRC 4 reg in rxmac address map
 * located at address 0x400C
 */
typedef union _RXMAC_WOL_CRC34_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 crc4:16;	/* bits 16-31 */
		u32 crc3:16;	/* bits 0-15 */
#else
		u32 crc3:16;	/* bits 0-15 */
		u32 crc4:16;	/* bits 16-31 */
#endif
	} bits;
} RXMAC_WOL_CRC34_t, *PRXMAC_WOL_CRC34_t;

/*
 * structure for Wake On Lan Source Address Lo reg in rxmac address map
 * located at address 0x4010
 */
typedef union _RXMAC_WOL_SA_LO_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 sa3:8;	/* bits 24-31 */
		u32 sa4:8;	/* bits 16-23 */
		u32 sa5:8;	/* bits 8-15 */
		u32 sa6:8;	/* bits 0-7 */
#else
		u32 sa6:8;	/* bits 0-7 */
		u32 sa5:8;	/* bits 8-15 */
		u32 sa4:8;	/* bits 16-23 */
		u32 sa3:8;	/* bits 24-31 */
#endif
	} bits;
} RXMAC_WOL_SA_LO_t, *PRXMAC_WOL_SA_LO_t;

/*
 * structure for Wake On Lan Source Address Hi reg in rxmac address map
 * located at address 0x4014
 */
typedef union _RXMAC_WOL_SA_HI_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 reserved:16;	/* bits 16-31 */
		u32 sa1:8;		/* bits 8-15 */
		u32 sa2:8;		/* bits 0-7 */
#else
		u32 sa2:8;		/* bits 0-7 */
		u32 sa1:8;		/* bits 8-15 */
		u32 reserved:16;	/* bits 16-31 */
#endif
	} bits;
} RXMAC_WOL_SA_HI_t, *PRXMAC_WOL_SA_HI_t;

/*
 * structure for Wake On Lan mask reg in rxmac address map
 * located at address 0x4018 - 0x4064
 * Defined earlier (u32)
 */

/*
 * structure for Unicast Paket Filter Address 1 reg in rxmac address map
 * located at address 0x4068
 */
typedef union _RXMAC_UNI_PF_ADDR1_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 addr1_3:8;	/* bits 24-31 */
		u32 addr1_4:8;	/* bits 16-23 */
		u32 addr1_5:8;	/* bits 8-15 */
		u32 addr1_6:8;	/* bits 0-7 */
#else
		u32 addr1_6:8;	/* bits 0-7 */
		u32 addr1_5:8;	/* bits 8-15 */
		u32 addr1_4:8;	/* bits 16-23 */
		u32 addr1_3:8;	/* bits 24-31 */
#endif
	} bits;
} RXMAC_UNI_PF_ADDR1_t, *PRXMAC_UNI_PF_ADDR1_t;

/*
 * structure for Unicast Paket Filter Address 2 reg in rxmac address map
 * located at address 0x406C
 */
typedef union _RXMAC_UNI_PF_ADDR2_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 addr2_3:8;	/* bits 24-31 */
		u32 addr2_4:8;	/* bits 16-23 */
		u32 addr2_5:8;	/* bits 8-15 */
		u32 addr2_6:8;	/* bits 0-7 */
#else
		u32 addr2_6:8;	/* bits 0-7 */
		u32 addr2_5:8;	/* bits 8-15 */
		u32 addr2_4:8;	/* bits 16-23 */
		u32 addr2_3:8;	/* bits 24-31 */
#endif
	} bits;
} RXMAC_UNI_PF_ADDR2_t, *PRXMAC_UNI_PF_ADDR2_t;

/*
 * structure for Unicast Paket Filter Address 1 & 2 reg in rxmac address map
 * located at address 0x4070
 */
typedef union _RXMAC_UNI_PF_ADDR3_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 addr2_1:8;	/* bits 24-31 */
		u32 addr2_2:8;	/* bits 16-23 */
		u32 addr1_1:8;	/* bits 8-15 */
		u32 addr1_2:8;	/* bits 0-7 */
#else
		u32 addr1_2:8;	/* bits 0-7 */
		u32 addr1_1:8;	/* bits 8-15 */
		u32 addr2_2:8;	/* bits 16-23 */
		u32 addr2_1:8;	/* bits 24-31 */
#endif
	} bits;
} RXMAC_UNI_PF_ADDR3_t, *PRXMAC_UNI_PF_ADDR3_t;

/*
 * structure for Multicast Hash reg in rxmac address map
 * located at address 0x4074 - 0x4080
 * Defined earlier (u32)
 */

/*
 * structure for Packet Filter Control reg in rxmac address map
 * located at address 0x4084
 */
typedef union _RXMAC_PF_CTRL_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 unused2:9;		/* bits 23-31 */
		u32 min_pkt_size:7;	/* bits 16-22 */
		u32 unused1:12;		/* bits 4-15 */
		u32 filter_frag_en:1;	/* bit 3 */
		u32 filter_uni_en:1;	/* bit 2 */
		u32 filter_multi_en:1;	/* bit 1 */
		u32 filter_broad_en:1;	/* bit 0 */
#else
		u32 filter_broad_en:1;	/* bit 0 */
		u32 filter_multi_en:1;	/* bit 1 */
		u32 filter_uni_en:1;	/* bit 2 */
		u32 filter_frag_en:1;	/* bit 3 */
		u32 unused1:12;		/* bits 4-15 */
		u32 min_pkt_size:7;	/* bits 16-22 */
		u32 unused2:9;		/* bits 23-31 */
#endif
	} bits;
} RXMAC_PF_CTRL_t, *PRXMAC_PF_CTRL_t;

/*
 * structure for Memory Controller Interface Control Max Segment reg in rxmac
 * address map.  Located at address 0x4088
 */
typedef union _RXMAC_MCIF_CTRL_MAX_SEG_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 reserved:22;	/* bits 10-31 */
		u32 max_size:8;	/* bits 2-9 */
		u32 fc_en:1;	/* bit 1 */
		u32 seg_en:1;	/* bit 0 */
#else
		u32 seg_en:1;	/* bit 0 */
		u32 fc_en:1;	/* bit 1 */
		u32 max_size:8;	/* bits 2-9 */
		u32 reserved:22;	/* bits 10-31 */
#endif
	} bits;
} RXMAC_MCIF_CTRL_MAX_SEG_t, *PRXMAC_MCIF_CTRL_MAX_SEG_t;

/*
 * structure for Memory Controller Interface Water Mark reg in rxmac address
 * map.  Located at address 0x408C
 */
typedef union _RXMAC_MCIF_WATER_MARK_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 reserved2:6;	/* bits 26-31 */
		u32 mark_hi:10;	/* bits 16-25 */
		u32 reserved1:6;	/* bits 10-15 */
		u32 mark_lo:10;	/* bits 0-9 */
#else
		u32 mark_lo:10;	/* bits 0-9 */
		u32 reserved1:6;	/* bits 10-15 */
		u32 mark_hi:10;	/* bits 16-25 */
		u32 reserved2:6;	/* bits 26-31 */
#endif
	} bits;
} RXMAC_MCIF_WATER_MARK_t, *PRXMAC_MCIF_WATER_MARK_t;

/*
 * structure for Rx Queue Dialog reg in rxmac address map.
 * located at address 0x4090
 */
typedef union _RXMAC_RXQ_DIAG_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 reserved2:6;	/* bits 26-31 */
		u32 rd_ptr:10;	/* bits 16-25 */
		u32 reserved1:6;	/* bits 10-15 */
		u32 wr_ptr:10;	/* bits 0-9 */
#else
		u32 wr_ptr:10;	/* bits 0-9 */
		u32 reserved1:6;	/* bits 10-15 */
		u32 rd_ptr:10;	/* bits 16-25 */
		u32 reserved2:6;	/* bits 26-31 */
#endif
	} bits;
} RXMAC_RXQ_DIAG_t, *PRXMAC_RXQ_DIAG_t;

/*
 * structure for space availiable reg in rxmac address map.
 * located at address 0x4094
 */
typedef union _RXMAC_SPACE_AVAIL_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 reserved2:15;		/* bits 17-31 */
		u32 space_avail_en:1;	/* bit 16 */
		u32 reserved1:6;		/* bits 10-15 */
		u32 space_avail:10;	/* bits 0-9 */
#else
		u32 space_avail:10;	/* bits 0-9 */
		u32 reserved1:6;		/* bits 10-15 */
		u32 space_avail_en:1;	/* bit 16 */
		u32 reserved2:15;		/* bits 17-31 */
#endif
	} bits;
} RXMAC_SPACE_AVAIL_t, *PRXMAC_SPACE_AVAIL_t;

/*
 * structure for management interface reg in rxmac address map.
 * located at address 0x4098
 */
typedef union _RXMAC_MIF_CTL_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 reserve:14;		/* bits 18-31 */
		u32 drop_pkt_en:1;		/* bit 17 */
		u32 drop_pkt_mask:17;	/* bits 0-16 */
#else
		u32 drop_pkt_mask:17;	/* bits 0-16 */
		u32 drop_pkt_en:1;		/* bit 17 */
		u32 reserve:14;		/* bits 18-31 */
#endif
	} bits;
} RXMAC_MIF_CTL_t, *PRXMAC_MIF_CTL_t;

/*
 * structure for Error reg in rxmac address map.
 * located at address 0x409C
 */
typedef union _RXMAC_ERROR_REG_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 reserve:28;	/* bits 4-31 */
		u32 mif:1;		/* bit 3 */
		u32 async:1;	/* bit 2 */
		u32 pkt_filter:1;	/* bit 1 */
		u32 mcif:1;	/* bit 0 */
#else
		u32 mcif:1;	/* bit 0 */
		u32 pkt_filter:1;	/* bit 1 */
		u32 async:1;	/* bit 2 */
		u32 mif:1;		/* bit 3 */
		u32 reserve:28;	/* bits 4-31 */
#endif
	} bits;
} RXMAC_ERROR_REG_t, *PRXMAC_ERROR_REG_t;

/*
 * Rx MAC Module of JAGCore Address Mapping
 */
typedef struct _RXMAC_t {				/* Location: */
	RXMAC_CTRL_t ctrl;				/*  0x4000 */
	RXMAC_WOL_CTL_CRC0_t crc0;			/*  0x4004 */
	RXMAC_WOL_CRC12_t crc12;			/*  0x4008 */
	RXMAC_WOL_CRC34_t crc34;			/*  0x400C */
	RXMAC_WOL_SA_LO_t sa_lo;			/*  0x4010 */
	RXMAC_WOL_SA_HI_t sa_hi;			/*  0x4014 */
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
	RXMAC_UNI_PF_ADDR1_t uni_pf_addr1;		/*  0x4068 */
	RXMAC_UNI_PF_ADDR2_t uni_pf_addr2;		/*  0x406C */
	RXMAC_UNI_PF_ADDR3_t uni_pf_addr3;		/*  0x4070 */
	u32 multi_hash1;				/*  0x4074 */
	u32 multi_hash2;				/*  0x4078 */
	u32 multi_hash3;				/*  0x407C */
	u32 multi_hash4;				/*  0x4080 */
	RXMAC_PF_CTRL_t pf_ctrl;			/*  0x4084 */
	RXMAC_MCIF_CTRL_MAX_SEG_t mcif_ctrl_max_seg;	/*  0x4088 */
	RXMAC_MCIF_WATER_MARK_t mcif_water_mark;	/*  0x408C */
	RXMAC_RXQ_DIAG_t rxq_diag;			/*  0x4090 */
	RXMAC_SPACE_AVAIL_t space_avail;		/*  0x4094 */

	RXMAC_MIF_CTL_t mif_ctrl;			/*  0x4098 */
	RXMAC_ERROR_REG_t err_reg;			/*  0x409C */
} RXMAC_t, *PRXMAC_t;

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

#define CFG1_LOOPBACK	0x00000100
#define CFG1_RX_FLOW	0x00000020
#define CFG1_TX_FLOW	0x00000010
#define CFG1_RX_ENABLE	0x00000004
#define CFG1_TX_ENABLE	0x00000001
#define CFG1_WAIT	0x0000000A	/* RX & TX syncd */

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
 * 4: preamble supress
 * 3: undefined
 * 2-0: mgmt clock reset
 */

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

#define MII_ADDR(phy,reg)	((phy) << 8 | (reg))

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

/*
 * structure for MII Management Indicators reg in mac address map.
 * located at address 0x5034
 * 31-3: reserved
 * 2: not valid
 * 1: scanning
 * 0: busy
 */

#define MGMT_BUSY	0x00000001	/* busy */
#define MGMT_WAIT	0x00000005	/* busy | not valid */

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

/*
 * structure for Interface Status reg in mac address map.
 * located at address 0x503C
 */
typedef union _MAC_IF_STAT_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 reserved:22;		/* bits 10-31 */
		u32 excess_defer:1;	/* bit 9 */
		u32 clash:1;		/* bit 8 */
		u32 phy_jabber:1;		/* bit 7 */
		u32 phy_link_ok:1;		/* bit 6 */
		u32 phy_full_duplex:1;	/* bit 5 */
		u32 phy_speed:1;		/* bit 4 */
		u32 pe100x_link_fail:1;	/* bit 3 */
		u32 pe10t_loss_carrie:1;	/* bit 2 */
		u32 pe10t_sqe_error:1;	/* bit 1 */
		u32 pe10t_jabber:1;	/* bit 0 */
#else
		u32 pe10t_jabber:1;	/* bit 0 */
		u32 pe10t_sqe_error:1;	/* bit 1 */
		u32 pe10t_loss_carrie:1;	/* bit 2 */
		u32 pe100x_link_fail:1;	/* bit 3 */
		u32 phy_speed:1;		/* bit 4 */
		u32 phy_full_duplex:1;	/* bit 5 */
		u32 phy_link_ok:1;		/* bit 6 */
		u32 phy_jabber:1;		/* bit 7 */
		u32 clash:1;		/* bit 8 */
		u32 excess_defer:1;	/* bit 9 */
		u32 reserved:22;		/* bits 10-31 */
#endif
	} bits;
} MAC_IF_STAT_t, *PMAC_IF_STAT_t;

/*
 * structure for Mac Station Address, Part 1 reg in mac address map.
 * located at address 0x5040
 */
typedef union _MAC_STATION_ADDR1_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 Octet6:8;	/* bits 24-31 */
		u32 Octet5:8;	/* bits 16-23 */
		u32 Octet4:8;	/* bits 8-15 */
		u32 Octet3:8;	/* bits 0-7 */
#else
		u32 Octet3:8;	/* bits 0-7 */
		u32 Octet4:8;	/* bits 8-15 */
		u32 Octet5:8;	/* bits 16-23 */
		u32 Octet6:8;	/* bits 24-31 */
#endif
	} bits;
} MAC_STATION_ADDR1_t, *PMAC_STATION_ADDR1_t;

/*
 * structure for Mac Station Address, Part 2 reg in mac address map.
 * located at address 0x5044
 */
typedef union _MAC_STATION_ADDR2_t {
	u32 value;
	struct {
#ifdef _BIT_FIELDS_HTOL
		u32 Octet2:8;	/* bits 24-31 */
		u32 Octet1:8;	/* bits 16-23 */
		u32 reserved:16;	/* bits 0-15 */
#else
		u32 reserved:16;	/* bit 0-15 */
		u32 Octet1:8;	/* bits 16-23 */
		u32 Octet2:8;	/* bits 24-31 */
#endif
	} bits;
} MAC_STATION_ADDR2_t, *PMAC_STATION_ADDR2_t;

/*
 * MAC Module of JAGCore Address Mapping
 */
typedef struct _MAC_t {					/* Location: */
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
	MAC_IF_STAT_t if_stat;				/*  0x503C */
	MAC_STATION_ADDR1_t station_addr_1;		/*  0x5040 */
	MAC_STATION_ADDR2_t station_addr_2;		/*  0x5044 */
} MAC_t, *PMAC_t;

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
struct macstat_regs
{					/* Location: */
	u32 pad[32];			/*  0x6000 - 607C */

	/* Tx/Rx 0-64 Byte Frame Counter */
	u32 TR64;			/*  0x6080 */

	/* Tx/Rx 65-127 Byte Frame Counter */
	u32 TR127;			/*  0x6084 */

	/* Tx/Rx 128-255 Byte Frame Counter */
	u32 TR255;			/*  0x6088 */

	/* Tx/Rx 256-511 Byte Frame Counter */
	u32 TR511;			/*  0x608C */

	/* Tx/Rx 512-1023 Byte Frame Counter */
	u32 TR1K;			/*  0x6090 */

	/* Tx/Rx 1024-1518 Byte Frame Counter */
	u32 TRMax;			/*  0x6094 */

	/* Tx/Rx 1519-1522 Byte Good VLAN Frame Count */
	u32 TRMgv;			/*  0x6098 */

	/* Rx Byte Counter */
	u32 RByt;			/*  0x609C */

	/* Rx Packet Counter */
	u32 RPkt;			/*  0x60A0 */

	/* Rx FCS Error Counter */
	u32 RFcs;			/*  0x60A4 */

	/* Rx Multicast Packet Counter */
	u32 RMca;			/*  0x60A8 */

	/* Rx Broadcast Packet Counter */
	u32 RBca;			/*  0x60AC */

	/* Rx Control Frame Packet Counter */
	u32 RxCf;			/*  0x60B0 */

	/* Rx Pause Frame Packet Counter */
	u32 RxPf;			/*  0x60B4 */

	/* Rx Unknown OP Code Counter */
	u32 RxUo;			/*  0x60B8 */

	/* Rx Alignment Error Counter */
	u32 RAln;			/*  0x60BC */

	/* Rx Frame Length Error Counter */
	u32 RFlr;			/*  0x60C0 */

	/* Rx Code Error Counter */
	u32 RCde;			/*  0x60C4 */

	/* Rx Carrier Sense Error Counter */
	u32 RCse;			/*  0x60C8 */

	/* Rx Undersize Packet Counter */
	u32 RUnd;			/*  0x60CC */

	/* Rx Oversize Packet Counter */
	u32 ROvr;			/*  0x60D0 */

	/* Rx Fragment Counter */
	u32 RFrg;			/*  0x60D4 */

	/* Rx Jabber Counter */
	u32 RJbr;			/*  0x60D8 */

	/* Rx Drop */
	u32 RDrp;			/*  0x60DC */

	/* Tx Byte Counter */
	u32 TByt;			/*  0x60E0 */

	/* Tx Packet Counter */
	u32 TPkt;			/*  0x60E4 */

	/* Tx Multicast Packet Counter */
	u32 TMca;			/*  0x60E8 */

	/* Tx Broadcast Packet Counter */
	u32 TBca;			/*  0x60EC */

	/* Tx Pause Control Frame Counter */
	u32 TxPf;			/*  0x60F0 */

	/* Tx Deferral Packet Counter */
	u32 TDfr;			/*  0x60F4 */

	/* Tx Excessive Deferral Packet Counter */
	u32 TEdf;			/*  0x60F8 */

	/* Tx Single Collision Packet Counter */
	u32 TScl;			/*  0x60FC */

	/* Tx Multiple Collision Packet Counter */
	u32 TMcl;			/*  0x6100 */

	/* Tx Late Collision Packet Counter */
	u32 TLcl;			/*  0x6104 */

	/* Tx Excessive Collision Packet Counter */
	u32 TXcl;			/*  0x6108 */

	/* Tx Total Collision Packet Counter */
	u32 TNcl;			/*  0x610C */

	/* Tx Pause Frame Honored Counter */
	u32 TPfh;			/*  0x6110 */

	/* Tx Drop Frame Counter */
	u32 TDrp;			/*  0x6114 */

	/* Tx Jabber Frame Counter */
	u32 TJbr;			/*  0x6118 */

	/* Tx FCS Error Counter */
	u32 TFcs;			/*  0x611C */

	/* Tx Control Frame Counter */
	u32 TxCf;			/*  0x6120 */

	/* Tx Oversize Frame Counter */
	u32 TOvr;			/*  0x6124 */

	/* Tx Undersize Frame Counter */
	u32 TUnd;			/*  0x6128 */

	/* Tx Fragments Frame Counter */
	u32 TFrg;			/*  0x612C */

	/* Carry Register One Register */
	u32 Carry1;			/*  0x6130 */

	/* Carry Register Two Register */
	u32 Carry2;			/*  0x6134 */

	/* Carry Register One Mask Register */
	u32 Carry1M;			/*  0x6138 */

	/* Carry Register Two Mask Register */
	u32 Carry2M;			/*  0x613C */
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
typedef struct _ADDRESS_MAP_t {
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
	RXMAC_t rxmac;
	/* unused section of rxmac address map */
	u8 unused_rxmac[4096 - sizeof(RXMAC_t)];
	MAC_t mac;
	/* unused section of mac address map */
	u8 unused_mac[4096 - sizeof(MAC_t)];
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
} ADDRESS_MAP_t, *PADDRESS_MAP_t;

#endif /* _ET1310_ADDRESS_MAP_H_ */
