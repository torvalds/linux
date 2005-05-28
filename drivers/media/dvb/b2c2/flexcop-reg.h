/*
 * This file is part of linux driver the digital TV devices equipped with B2C2 FlexcopII(b)/III
 *
 * flexcop-reg.h - register abstraction for FlexCopII, FlexCopIIb and FlexCopIII
 *
 * see flexcop.c for copyright information.
 */
#ifndef __FLEXCOP_REG_H__
#define __FLEXCOP_REG_H__


typedef enum {
	FLEXCOP_UNK = 0,
	FLEXCOP_II,
	FLEXCOP_IIB,
	FLEXCOP_III,
} flexcop_revision_t;

extern const char *flexcop_revision_names[];

typedef enum {
	FC_UNK = 0,
	FC_AIR_DVB,
	FC_AIR_ATSC,
	FC_SKY,
	FC_SKY_OLD,
	FC_CABLE,
} flexcop_device_type_t;

typedef enum {
	FC_USB = 0,
	FC_PCI,
} flexcop_bus_t;

extern const char *flexcop_device_names[];

/* FlexCop IBI Registers */

/* flexcop_ibi_reg - a huge union representing the register structure */
typedef union {
	u32 raw;

/* DMA 0x000 to 0x01c
 * DMA1 0x000 to 0x00c
 * DMA2 0x010 to 0x01c
 */
	struct {
		u32 dma_0start        : 1;   /* set: data will be delivered to dma1_address0 */
        u32 dma_0No_update    : 1;   /* set: dma1_cur_address will be updated, unset: no update */
        u32 dma_address0      :30;   /* physical/virtual host memory address0 DMA */
	} dma_0x0;

	struct {
		u32 DMA_maxpackets    : 8;   /* (remapped) PCI DMA1 Packet Count Interrupt. This variable
										is able to be read and written while bit(1) of register
										0x00c (remap_enable) is set. This variable represents
										the number of packets that will be transmitted to the PCI
										host using PCI DMA1 before an interrupt to the PCI is
										asserted. This functionality may be enabled using bit(20)
										of register 0x208. N=0 disables the IRQ. */
		u32 dma_addr_size     :24;   /* size of memory buffer in DWORDs (bytesize / 4) for DMA */
	} dma_0x4_remap;

	struct {
		u32 dma1timer         : 7;   /* reading PCI DMA1 timer ... when remap_enable is 0 */
		u32 unused            : 1;
		u32 dma_addr_size     :24;
	} dma_0x4_read;

	struct {
		u32 unused            : 1;
		u32 dmatimer          : 7;   /* writing PCI DMA1 timer ... when remap_enable is 0 */
		u32 dma_addr_size     :24;
	} dma_0x4_write;

	struct {
		u32 unused            : 2;
		u32 dma_cur_addr      :30;   /* current physical host memory address pointer for DMA */
	} dma_0x8;

	struct {
		u32 dma_1start        : 1;   /* set: data will be delivered to dma_address1, when dma_address0 is full */
		u32 remap_enable      : 1;   /* remap enable for 0x0x4(7:0) */
		u32 dma_address1      :30;   /* Physical/virtual address 1 on DMA */
	} dma_0xc;

/* Two-wire Serial Master and Clock 0x100-0x110 */
	struct {
//		u32 slave_transmitter : 1;   /* ???*/
		u32 chipaddr          : 7;   /* two-line serial address of the target slave */
		u32 reserved1         : 1;
		u32 baseaddr          : 8;   /* address of the location of the read/write operation */
		u32 data1_reg         : 8;   /* first byte in two-line serial read/write operation */
		u32 working_start     : 1;  /* when doing a write operation this indicator is 0 when ready
									  * set to 1 when doing a write operation */
		u32 twoWS_rw          : 1;   /* read/write indicator (1 = read, 0 write) */
		u32 total_bytes       : 2;   /* number of data bytes in each two-line serial transaction (0 = 1 byte, 11 = 4byte)*/
		u32 twoWS_port_reg    : 2;   /* port selection: 01 - Front End/Demod, 10 - EEPROM, 11 - Tuner */
		u32 no_base_addr_ack_error : 1;   /* writing: write-req: frame is produced w/o baseaddr, read-req: read-cycles w/o
									  * preceding address assignment write frame
									  * ACK_ERROR = 1 when no ACK from slave in the last transaction */
		u32 st_done           : 1;   /* indicator for transaction is done */
	} tw_sm_c_100;

	struct {
		u32 data2_reg         : 8;   /* 2nd data byte */
		u32 data3_reg         : 8;   /* 3rd data byte */
		u32 data4_reg         : 8;   /* 4th data byte */
		u32 exlicit_stops     : 1;   /* when set, transactions are produced w/o trailing STOP flag, then send isolated STOP flags */
		u32 force_stop        : 1;   /* isolated stop flag */
		u32 unused            : 6;
	} tw_sm_c_104;

/* Clock. The register allows the FCIII to convert an incoming Master clock
 * (MCLK) signal into a lower frequency clock through the use of a LowCounter
 * (TLO) and a High- Counter (THI). The time counts for THI and TLO are
 * measured in MCLK; each count represents 4 MCLK input clock cycles.
 *
 * The default output for port #1 is set for Front End Demod communication. (0x108)
 * The default output for port #2 is set for EEPROM communication. (0x10c)
 * The default output for port #3 is set for Tuner communication. (0x110)
 */
	struct {
		u32 thi1              : 6;   /* Thi for port #1 (def: 100110b; 38) */
		u32 reserved1         : 2;
		u32 tlo1              : 5;   /* Tlo for port #1 (def: 11100b; 28) */
		u32 reserved2         :19;
	} tw_sm_c_108;

	struct {
		u32 thi1              : 6;   /* Thi for port #2 (def: 111001b; 57) */
		u32 reserved1         : 2;
		u32 tlo1              : 5;   /* Tlo for port #2 (def: 11100b; 28) */
		u32 reserved2         :19;
	} tw_sm_c_10c;

	struct {
		u32 thi1              : 6;   /* Thi for port #3 (def: 111001b; 57) */
		u32 reserved1         : 2;
		u32 tlo1              : 5;   /* Tlo for port #3 (def: 11100b; 28) */
		u32 reserved2         :19;
	} tw_sm_c_110;

/* LNB Switch Frequency 0x200
 * Clock that creates the LNB switch tone. The default is set to have a fixed
 * low output (not oscillating) to the LNB_CTL line.
 */
	struct {
		u32 LNB_CTLHighCount_sig :15; /* It is the number of pre-scaled clock cycles that will be low. */
		u32 LNB_CTLLowCount_sig  :15; /* For example, to obtain a 22KHz output given a 45 Mhz Master
										Clock signal (MCLK), set PreScalar=01 and LowCounter value to 0x1ff. */
		u32 LNB_CTLPrescaler_sig : 2; /* pre-scaler divides MCLK: 00 (no division), 01 by 2, 10 by 4, 11 by 12 */
	} lnb_switch_freq_200;

/* ACPI, Peripheral Reset, LNB Polarity
 * ACPI power conservation mode, LNB polarity selection (low or high voltage),
 * and peripheral reset.
 */
	struct {
		u32 ACPI1_sig         : 1;   /* turn of the power of tuner and LNB, not implemented in FCIII */
		u32 ACPI3_sig         : 1;   /* turn of power of the complete satelite receiver board (except FCIII) */
		u32 LNB_L_H_sig       : 1;   /* low or high voltage for LNB. (0 = low, 1 = high) */
		u32 Per_reset_sig     : 1;   /* misc. init reset (default: 1), to reset set to low and back to high */
		u32 reserved          :20;
		u32 Rev_N_sig_revision_hi : 4;/* 0xc in case of FCIII */
		u32 Rev_N_sig_reserved1 : 2;
		u32 Rev_N_sig_caps    : 1;   /* if 1, FCIII has 32 PID- and MAC-filters and is capable of IP multicast */
		u32 Rev_N_sig_reserved2 : 1;
	} misc_204;

/* Control and Status 0x208 to 0x21c */
/* Gross enable and disable control */
	struct {
		u32 Stream1_filter_sig : 1;  /* Stream1 PID filtering */
		u32 Stream2_filter_sig : 1;  /* Stream2 PID filtering */
		u32 PCR_filter_sig    : 1;   /* PCR PID filter */
		u32 PMT_filter_sig    : 1;   /* PMT PID filter */

		u32 EMM_filter_sig    : 1;   /* EMM PID filter */
		u32 ECM_filter_sig    : 1;   /* ECM PID filter */
		u32 Null_filter_sig   : 1;   /* Filters null packets, PID=0x1fff. */
		u32 Mask_filter_sig   : 1;   /* mask PID filter */

		u32 WAN_Enable_sig    : 1;   /* WAN output line through V8 memory space is activated. */
		u32 WAN_CA_Enable_sig : 1;   /* not in FCIII */
		u32 CA_Enable_sig     : 1;   /* not in FCIII */
		u32 SMC_Enable_sig    : 1;   /* CI stream data (CAI) goes directly to the smart card intf (opposed IBI 0x600 or SC-cmd buf). */

		u32 Per_CA_Enable_sig : 1;   /* not in FCIII */
		u32 Multi2_Enable_sig : 1;   /* ? */
		u32 MAC_filter_Mode_sig : 1; /* (MAC_filter_enable) Globally enables MAC filters for Net PID filteres. */
		u32 Rcv_Data_sig      : 1;   /* PID filtering module enable. When this bit is a one, the PID filter will
										examine and process packets according to all other (individual) PID
										filtering controls. If it a zero, no packet processing of any kind will
										take place. All data from the tuner will be thrown away. */

		u32 DMA1_IRQ_Enable_sig : 1; /* When set, a DWORD counter is enabled on PCI DMA1 that asserts the PCI
									  * interrupt after the specified count for filling the buffer. */
		u32 DMA1_Timer_Enable_sig : 1; /* When set, a timer is enabled on PCI DMA1 that asserts the PCI interrupt
											after a specified amount of time. */
		u32 DMA2_IRQ_Enable_sig : 1;   /* same as DMA1_IRQ_Enable_sig but for DMA2 */
		u32 DMA2_Timer_Enable_sig : 1;   /* same as DMA1_Timer_Enable_sig but for DMA2 */

		u32 DMA1_Size_IRQ_Enable_sig : 1; /* When set, a packet count detector is enabled on PCI DMA1 that asserts the PCI interrupt. */
		u32 DMA2_Size_IRQ_Enable_sig : 1; /* When set, a packet	count detector is enabled on PCI DMA2 that asserts the PCI interrupt. */
		u32 Mailbox_from_V8_Enable_sig: 1; /* When set, writes to the mailbox register produce an interrupt to the
											PCI host to indicate that mailbox data is available. */

		u32 unused            : 9;
	} ctrl_208;

/* General status. When a PCI interrupt occurs, this register is read to
 * discover the reason for the interrupt.
 */
	struct {
		u32 DMA1_IRQ_Status   : 1;   /* When set(1) the DMA1 counter had generated an IRQ. Read Only. */
		u32 DMA1_Timer_Status : 1;   /* When set(1) the DMA1 timer had generated an IRQ. Read Only. */
		u32 DMA2_IRQ_Status   : 1;   /* When set(1) the DMA2 counter had generated an IRQ. Read Only. */
		u32 DMA2_Timer_Status : 1;   /* When set(1) the DMA2 timer had generated an IRQ. Read Only. */
		u32 DMA1_Size_IRQ_Status : 1; /* (Read only). This register is read after an interrupt to */
		u32 DMA2_Size_IRQ_Status : 1; /* find out why we had an IRQ. Reading this register will clear this bit. Packet count*/
		u32 Mailbox_from_V8_Status_sig: 1; /* Same as above. Reading this register will clear this bit. */
		u32 Data_receiver_error : 1; /* 1 indicate an error in the receiver Front End (Tuner module) */
		u32 Continuity_error_flag : 1;   /* 1 indicates a continuity error in the TS stream. */
		u32 LLC_SNAP_FLAG_set : 1;   /* 1 indicates that the LCC_SNAP_FLAG was set. */
		u32 Transport_Error   : 1;   /*  When set indicates that an unexpected packet was received. */
		u32 reserved          :21;
	} irq_20c;


/* Software reset register */
	struct {
		u32 reset_blocks      : 8;   /* Enabled when Block_reset_enable = 0xB2 and 0x208 bits 15:8 = 0x00.
										Each bit location represents a 0x100 block of registers. Writing
										a one in a bit location resets that block of registers and the logic
										that it controls. */
		u32 Block_reset_enable : 8;  /* This variable is set to 0xB2 when the register is written. */
		u32 Special_controls  :16;   /* Asserts Reset_V8 => 0xC258; Turns on pci encryption => 0xC25A;
										Turns off pci encryption => 0xC259 Note: pci_encryption default
										at power-up is ON. */
	} sw_reset_210;

	struct {
		u32 vuart_oe_sig      : 1;   /* When clear, the V8 processor has sole control of the serial UART
										(RS-232 Smart Card interface). When set, the IBI interface
										defined by register 0x600 controls the serial UART. */
		u32 v2WS_oe_sig       : 1;   /* When clear, the V8 processor has direct control of the Two-line
										Serial Master EEPROM target. When set, the Two-line Serial Master
										EEPROM target interface is controlled by IBI register 0x100. */
		u32 halt_V8_sig       : 1;   /* When set, contiguous wait states are applied to the V8-space
										bus masters. Once this signal is cleared, normal V8-space
										operations resume. */
		u32 section_pkg_enable_sig: 1; /* When set, this signal enables the front end translation circuitry
										  to process section packed transport streams. */
		u32 s2p_sel_sig       : 1;   /* Serial to parallel conversion. When set, polarized transport data
										within the FlexCop3 front end circuitry is converted from a serial
										stream into parallel data before downstream processing otherwise
										interprets the data. */
		u32 unused1           : 3;
		u32 polarity_PS_CLK_sig: 1;  /* This signal is used to invert the input polarity of the tranport
										stream CLOCK signal before any processing occurs on the transport
										stream within FlexCop3. */
		u32 polarity_PS_VALID_sig: 1; /* This signal is used to invert the input polarity of the tranport
										stream VALID signal before any processing occurs on the transport
										stream within FlexCop3. */
		u32 polarity_PS_SYNC_sig: 1; /* This signal is used to invert the input polarity of the tranport
										stream SYNC signal before any processing occurs on the transport
										stream within FlexCop3. */
		u32 polarity_PS_ERR_sig: 1;  /* This signal is used to invert the input polarity of the tranport
										stream ERROR signal before any processing occurs on the transport
										stream within FlexCop3. */
		u32 unused2           :20;
	} misc_214;

/* Mailbox from V8 to host */
	struct {
		u32 Mailbox_from_V8   :32;   /* When this register is written by either the V8 processor or by an
										end host, an interrupt is generated to the PCI host to indicate
										that mailbox data is available. Reading register 20c will clear
										the IRQ. */
	} mbox_v8_to_host_218;

/* Mailbox from host to v8 Mailbox_to_V8
 * Mailbox_to_V8 mailbox storage register
 * used to send messages from PCI to V8. Writing to this register will send an
 * IRQ to the V8. Then it can read the data from here. Reading this register
 * will clear the IRQ. If the V8 is halted and bit 31 of this register is set,
 * then this register is used instead as a direct interface to access the
 * V8space memory.
 */
	struct {
		u32 sysramaccess_data : 8;   /* Data byte written or read from the specified address in V8 SysRAM. */
		u32 sysramaccess_addr :15;   /* 15 bit address used to access V8 Sys-RAM. */
		u32 unused            : 7;
		u32 sysramaccess_write: 1;   /* Write flag used to latch data into the V8 SysRAM. */
		u32 sysramaccess_busmuster: 1; /* Setting this bit when the V8 is halted at 0x214 Bit(2) allows
										  this IBI register interface to directly drive the V8-space memory. */
	} mbox_host_to_v8_21c;


/* PIDs, Translation Bit, SMC Filter Select 0x300 to 0x31c */
	struct {
		u32 Stream1_PID       :13;   /* Primary use is receiving Net data, so these 13 bits normally
										hold the PID value for the desired network stream. */
		u32 Stream1_trans     : 1;   /* When set, Net translation will take place for Net data ferried in TS packets. */
		u32 MAC_Multicast_filter : 1;   /* When clear, multicast MAC filtering is not allowed for Stream1 and PID_n filters. */
		u32 debug_flag_pid_saved : 1;
		u32 Stream2_PID       :13;   /* 13 bits for Stream 2 PID filter value. General use. */
		u32 Stream2_trans     : 1;   /* When set Tables/CAI translation will take place for the data ferried in
										Stream2_PID TS packets. */
		u32 debug_flag_write_status00 : 1;
		u32 debug_fifo_problem : 1;
	} pid_filter_300;

	struct {
		u32 PCR_PID           :13;   /* PCR stream PID filter value. Primary use is Program Clock Reference stream filtering. */
		u32 PCR_trans         : 1;   /* When set, Tables/CAI translation will take place for these packets. */
		u32 debug_overrun3    : 1;
		u32 debug_overrun2    : 1;
		u32 PMT_PID           :13;   /* stream PID filter value. Primary use is Program Management Table segment filtering. */
		u32 PMT_trans         : 1;   /* When set, Tables/CAI translation will take place for these packets. */
		u32 reserved          : 2;
	} pid_filter_304;

	struct {
		u32 EMM_PID           :13;   /* EMM PID filter value. Primary use is Entitlement Management Messaging for
										conditional access-related data. */
		u32 EMM_trans         : 1;   /* When set, Tables/CAI translation will take place for these packets. */
		u32 EMM_filter_4      : 1;   /* When set will pass only EMM data possessing the same ID code as the
										first four bytes (32 bits) of the end-user s 6-byte Smart Card ID number Select */
		u32 EMM_filter_6      : 1;   /* When set will pass only EMM data possessing the same 6-byte code as the end-users
										complete 6-byte Smart Card ID number. */
		u32 ECM_PID           :13;   /* ECM PID filter value. Primary use is Entitlement Control Messaging for conditional
										access-related data. */
		u32 ECM_trans         : 1;   /* When set, Tables/CAI translation will take place for these packets. */
		u32 reserved          : 2;
	} pid_filter_308;

	struct {
		u32 Group_PID     :13;   /* PID value for group filtering. */
		u32 Group_trans   : 1;   /* When set, Tables/CAI translation will take place for these packets. */
		u32 unused1       : 2;
		u32 Group_mask    :13;   /* Mask value used in logical "and" equation that defines group filtering */
		u32 unused2       : 3;
	} pid_filter_30c_ext_ind_0_7;

	struct {
		u32 net_master_read :17;
		u32 unused        :15;
	} pid_filter_30c_ext_ind_1;

	struct {
		u32 net_master_write :17;
		u32 unused        :15;
	} pid_filter_30c_ext_ind_2;

	struct {
		u32 next_net_master_write :17;
		u32 unused        :15;
	} pid_filter_30c_ext_ind_3;

	struct {
		u32 unused1       : 1;
		u32 state_write   :10;
		u32 reserved1     : 6;   /* default: 000100 */
		u32 stack_read    :10;
		u32 reserved2     : 5;   /* default: 00100 */
	} pid_filter_30c_ext_ind_4;

	struct {
		u32 stack_cnt     :10;
		u32 unused        :22;
	} pid_filter_30c_ext_ind_5;

	struct {
		u32 pid_fsm_save_reg0 : 2;
		u32 pid_fsm_save_reg1 : 2;
		u32 pid_fsm_save_reg2 : 2;
		u32 pid_fsm_save_reg3 : 2;
		u32 pid_fsm_save_reg4 : 2;
		u32 pid_fsm_save_reg300 : 2;
		u32 write_status1 : 2;
		u32 write_status4 : 2;
		u32 data_size_reg :12;
		u32 unused        : 4;
	} pid_filter_30c_ext_ind_6;

	struct {
		u32 index_reg         : 5;   /* (Index pointer) Points at an internal PIDn register. A binary code
										representing one of 32 internal PIDn registers as well as its
										corresponding internal MAC_lown register. */
		u32 extra_index_reg   : 3;   /* This vector is used to select between sets of debug signals routed to register 0x30c. */
		u32 AB_select         : 1;   /* Used in conjunction with 0x31c. read/write to the MAC_highA or MAC_highB register
										0=MAC_highB register, 1=MAC_highA */
		u32 pass_alltables    : 1;   /* 1=Net packets are not filtered against the Network Table ID found in register 0x400.
										All types of networks (DVB, ATSC, ISDB) are passed. */
		u32 unused            :22;
	} index_reg_310;

	struct {
		u32 PID               :13;   /* PID value */
		u32 PID_trans         : 1;   /* translation will take place for packets filtered */
		u32 PID_enable_bit    : 1;   /* When set this PID filter is enabled */
		u32 reserved          :17;
	} pid_n_reg_314;

	struct {
		u32 A4_byte           : 8;
		u32 A5_byte           : 8;
		u32 A6_byte           : 8;
		u32 Enable_bit        : 1;   /* enabled (1) or disabled (1) */
		u32 HighAB_bit        : 1;   /* use MAC_highA (1) or MAC_highB (0) as MSB */
		u32 reserved          : 6;
	} mac_low_reg_318;

	struct {
		u32 A1_byte           : 8;
		u32 A2_byte           : 8;
		u32 A3_byte           : 8;
		u32 reserved          : 8;
	} mac_high_reg_31c;

/* Table, SMCID,MACDestination Filters 0x400 to 0x41c */
	struct {
		u32 reserved          :16;
#define fc_data_Tag_ID_DVB  0x3e
#define fc_data_Tag_ID_ATSC 0x3f
#define fc_data_Tag_ID_IDSB 0x8b
		u32 data_Tag_ID       :16;
	} data_tag_400;

	struct {
		u32 Card_IDbyte6      : 8;
		u32 Card_IDbyte5      : 8;
		u32 Card_IDbyte4      : 8;
		u32 Card_IDbyte3      : 8;
	} card_id_408;

	struct {
		u32 Card_IDbyte2      : 8;
		u32 Card_IDbyte1      : 8;
	} card_id_40c;

	/* holding the unique mac address of the receiver which houses the FlexCopIII */
	struct {
		u32 MAC1              : 8;
		u32 MAC2              : 8;
		u32 MAC3              : 8;
		u32 MAC6              : 8;
	} mac_address_418;

	struct {
		u32 MAC7              : 8;
		u32 MAC8              : 8;
		u32 reserved          : 16;
	} mac_address_41c;

	struct {
		u32 transmitter_data_byte : 8;
		u32 ReceiveDataReady  : 1;
		u32 ReceiveByteFrameError: 1;
		u32 txbuffempty       : 1;
		u32 reserved          :21;
	} ci_600;

	struct {
		u32 pi_d              : 8;
		u32 pi_ha             :20;
		u32 pi_rw             : 1;
		u32 pi_component_reg  : 3;
	} pi_604;

	struct {
		u32 serialReset       : 1;
		u32 oncecycle_read    : 1;
		u32 Timer_Read_req    : 1;
		u32 Timer_Load_req    : 1;
		u32 timer_data        : 7;
		u32 unused            : 1; /* ??? not mentioned in data book */
		u32 Timer_addr        : 5;
		u32 reserved          : 3;
		u32 pcmcia_a_mod_pwr_n : 1;
		u32 pcmcia_b_mod_pwr_n : 1;
		u32 config_Done_stat  : 1;
		u32 config_Init_stat  : 1;
		u32 config_Prog_n     : 1;
		u32 config_wr_n       : 1;
		u32 config_cs_n       : 1;
		u32 config_cclk       : 1;
		u32 pi_CiMax_IRQ_n    : 1;
		u32 pi_timeout_status : 1;
		u32 pi_wait_n         : 1;
		u32 pi_busy_n         : 1;
	} pi_608;

	struct {
		u32 PID               :13;
		u32 key_enable        : 1;
#define fc_key_code_default 0x1
#define fc_key_code_even    0x2
#define fc_key_code_odd     0x3
		u32 key_code          : 2;
		u32 key_array_col     : 3;
		u32 key_array_row     : 5;
		u32 dvb_en            : 1; /* 0=TS bypasses the Descrambler */
		u32 rw_flag           : 1;
		u32 reserved          : 6;
	} dvb_reg_60c;

/* SRAM and Output Destination 0x700 to 0x714 */
	struct {
		u32 sram_addr         :15;
		u32 sram_rw           : 1;   /* 0=write, 1=read */
		u32 sram_data         : 8;
		u32 sc_xfer_bit       : 1;
		u32 reserved1         : 3;
		u32 oe_pin_reg        : 1;
		u32 ce_pin_reg        : 1;
		u32 reserved2         : 1;
		u32 start_sram_ibi    : 1;
	} sram_ctrl_reg_700;

	struct {
		u32 net_addr_read     :16;
		u32 net_addr_write    :16;
	} net_buf_reg_704;

	struct {
		u32 cai_read          :11;
		u32 reserved1         : 5;
		u32 cai_write         :11;
		u32 reserved2         : 6;
		u32 cai_cnt           : 4;
	} cai_buf_reg_708;

	struct {
		u32 cao_read          :11;
		u32 reserved1         : 5;
		u32 cap_write         :11;
		u32 reserved2         : 6;
		u32 cao_cnt           : 4;
	} cao_buf_reg_70c;

	struct {
		u32 media_read        :11;
		u32 reserved1         : 5;
		u32 media_write       :11;
		u32 reserved2         : 6;
		u32 media_cnt         : 4;
	} media_buf_reg_710;

	struct {
		u32 NET_Dest          : 2;
		u32 CAI_Dest          : 2;
		u32 CAO_Dest          : 2;
		u32 MEDIA_Dest        : 2;
		u32 net_ovflow_error  : 1;
		u32 media_ovflow_error : 1;
		u32 cai_ovflow_error  : 1;
		u32 cao_ovflow_error  : 1;
		u32 ctrl_usb_wan      : 1;
		u32 ctrl_sramdma      : 1;
		u32 ctrl_maximumfill  : 1;
		u32 reserved          :17;
	} sram_dest_reg_714;

	struct {
		u32 net_cnt           :12;
		u32 reserved1         : 4;
		u32 net_addr_read     : 1;
		u32 reserved2         : 3;
		u32 net_addr_write    : 1;
		u32 reserved3         :11;
	} net_buf_reg_718;

	struct {
		u32 wan_speed_sig     : 2;
		u32 reserved1         : 6;
		u32 wan_wait_state    : 8;
		u32 sram_chip         : 2;
		u32 sram_memmap       : 2;
		u32 reserved2         : 4;
		u32 wan_pkt_frame     : 4;
		u32 reserved3         : 4;
	} wan_ctrl_reg_71c;
} flexcop_ibi_value;

extern flexcop_ibi_value ibi_zero;

typedef enum {
	FC_I2C_PORT_DEMOD  = 1,
	FC_I2C_PORT_EEPROM = 2,
	FC_I2C_PORT_TUNER  = 3,
} flexcop_i2c_port_t;

typedef enum {
	FC_WRITE = 0,
	FC_READ  = 1,
} flexcop_access_op_t;

typedef enum {
	FC_SRAM_DEST_NET   = 1,
	FC_SRAM_DEST_CAI   = 2,
	FC_SRAM_DEST_CAO   = 4,
	FC_SRAM_DEST_MEDIA = 8
} flexcop_sram_dest_t;

typedef enum {
	FC_SRAM_DEST_TARGET_WAN_USB = 0,
	FC_SRAM_DEST_TARGET_DMA1    = 1,
	FC_SRAM_DEST_TARGET_DMA2    = 2,
	FC_SRAM_DEST_TARGET_FC3_CA  = 3
} flexcop_sram_dest_target_t;

typedef enum {
	FC_SRAM_2_32KB  = 0, /*  64KB */
	FC_SRAM_1_32KB  = 1, /*  32KB - default fow FCII */
	FC_SRAM_1_128KB = 2, /* 128KB */
	FC_SRAM_1_48KB  = 3, /*  48KB - default for FCIII */
} flexcop_sram_type_t;

typedef enum {
	FC_WAN_SPEED_4MBITS  = 0,
	FC_WAN_SPEED_8MBITS  = 1,
	FC_WAN_SPEED_12MBITS = 2,
	FC_WAN_SPEED_16MBITS = 3,
} flexcop_wan_speed_t;

typedef enum {
	FC_DMA_1 = 1,
	FC_DMA_2 = 2,
} flexcop_dma_index_t;

typedef enum {
	FC_DMA_SUBADDR_0 = 1,
	FC_DMA_SUBADDR_1 = 2,
} flexcop_dma_addr_index_t;

/* names of the particular registers */
typedef enum {
	dma1_000            = 0x000,
	dma1_004            = 0x004,
	dma1_008            = 0x008,
	dma1_00c            = 0x00c,
	dma2_010            = 0x010,
	dma2_014            = 0x014,
	dma2_018            = 0x018,
	dma2_01c            = 0x01c,

	tw_sm_c_100         = 0x100,
	tw_sm_c_104         = 0x104,
	tw_sm_c_108         = 0x108,
	tw_sm_c_10c         = 0x10c,
	tw_sm_c_110         = 0x110,

	lnb_switch_freq_200 = 0x200,
	misc_204            = 0x204,
	ctrl_208            = 0x208,
	irq_20c             = 0x20c,
	sw_reset_210        = 0x210,
	misc_214            = 0x214,
	mbox_v8_to_host_218 = 0x218,
	mbox_host_to_v8_21c = 0x21c,

	pid_filter_300      = 0x300,
	pid_filter_304      = 0x304,
	pid_filter_308      = 0x308,
	pid_filter_30c      = 0x30c,
	index_reg_310       = 0x310,
	pid_n_reg_314       = 0x314,
	mac_low_reg_318     = 0x318,
	mac_high_reg_31c    = 0x31c,

	data_tag_400        = 0x400,
	card_id_408         = 0x408,
	card_id_40c         = 0x40c,
	mac_address_418     = 0x418,
	mac_address_41c     = 0x41c,

	ci_600              = 0x600,
	pi_604              = 0x604,
	pi_608              = 0x608,
	dvb_reg_60c         = 0x60c,

	sram_ctrl_reg_700   = 0x700,
	net_buf_reg_704     = 0x704,
	cai_buf_reg_708     = 0x708,
	cao_buf_reg_70c     = 0x70c,
	media_buf_reg_710   = 0x710,
	sram_dest_reg_714   = 0x714,
	net_buf_reg_718     = 0x718,
	wan_ctrl_reg_71c    = 0x71c,
} flexcop_ibi_register;

#define flexcop_set_ibi_value(reg,attr,val) { \
	flexcop_ibi_value v = fc->read_ibi_reg(fc,reg); \
	v.reg.attr = val; \
	fc->write_ibi_reg(fc,reg,v); \
}

#endif
