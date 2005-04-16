/*
 *	3COM "EtherLink MC/32" Descriptions
 */

/*
 *	Registers
 */
  
#define HOST_CMD		0
#define         HOST_CMD_START_RX   (1<<3)
#define         HOST_CMD_SUSPND_RX  (3<<3)
#define         HOST_CMD_RESTRT_RX  (5<<3)

#define         HOST_CMD_SUSPND_TX  3
#define         HOST_CMD_RESTRT_TX  5


#define HOST_STATUS		2
#define		HOST_STATUS_CRR	(1<<6)
#define		HOST_STATUS_CWR	(1<<5)


#define HOST_CTRL		6
#define		HOST_CTRL_ATTN	(1<<7)
#define 	HOST_CTRL_RESET	(1<<6)
#define 	HOST_CTRL_INTE	(1<<2)

#define HOST_RAMPAGE		8

#define HALTED 0
#define RUNNING 1

struct mc32_mailbox
{
	u16	mbox __attribute((packed));
	u16	data[1] __attribute((packed));
};

struct skb_header
{
	u8	status __attribute((packed));
	u8	control __attribute((packed));
	u16	next __attribute((packed));	/* Do not change! */
	u16	length __attribute((packed));
	u32	data __attribute((packed));
};

struct mc32_stats
{
	/* RX Errors */
	u32     rx_crc_errors       __attribute((packed)); 	
	u32     rx_alignment_errors  __attribute((packed)); 	
	u32     rx_overrun_errors    __attribute((packed));
	u32     rx_tooshort_errors  __attribute((packed));
	u32     rx_toolong_errors   __attribute((packed));
	u32     rx_outofresource_errors  __attribute((packed)); 

	u32     rx_discarded   __attribute((packed));  /* via card pattern match filter */ 

	/* TX Errors */
	u32     tx_max_collisions __attribute((packed)); 
	u32     tx_carrier_errors __attribute((packed)); 
	u32     tx_underrun_errors __attribute((packed)); 
	u32     tx_cts_errors     __attribute((packed)); 
	u32     tx_timeout_errors __attribute((packed)) ;
	
	/* various cruft */
	u32     dataA[6] __attribute((packed));   
        u16	dataB[5] __attribute((packed));   
  	u32     dataC[14] __attribute((packed)); 	
};

#define STATUS_MASK	0x0F
#define COMPLETED	(1<<7)
#define COMPLETED_OK	(1<<6)
#define BUFFER_BUSY	(1<<5)

#define CONTROL_EOP	(1<<7)	/* End Of Packet */
#define CONTROL_EOL	(1<<6)	/* End of List */

#define MCA_MC32_ID	0x0041	/* Our MCA ident */
