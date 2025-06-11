/* SPDX-License-Identifier: GPL-2.0 */
/*
 * I/O Processor (IOP) defines and structures, mostly snagged from A/UX
 * header files.
 *
 * The original header from which this was taken is copyrighted. I've done some
 * rewriting (in fact my changes make this a bit more readable, IMHO) but some
 * more should be done.
 */

/*
 * This is the base address of the IOPs. Use this as the address of
 * a "struct iop" (see below) to see where the actual registers fall.
 */

#define SCC_IOP_BASE_IIFX	(0x50F04000)
#define ISM_IOP_BASE_IIFX	(0x50F12000)

#define SCC_IOP_BASE_QUADRA	(0x50F0C000)
#define ISM_IOP_BASE_QUADRA	(0x50F1E000)

/* IOP status/control register bits: */

#define	IOP_BYPASS	0x01	/* bypass-mode hardware access */
#define	IOP_AUTOINC	0x02	/* allow autoincrement of ramhi/lo */
#define	IOP_RUN		0x04	/* set to 0 to reset IOP chip */
#define	IOP_IRQ		0x08	/* generate IRQ to IOP if 1 */
#define	IOP_INT0	0x10	/* intr priority from IOP to host */
#define	IOP_INT1	0x20	/* intr priority from IOP to host */
#define	IOP_HWINT	0x40	/* IRQ from hardware; bypass mode only */
#define	IOP_DMAINACTIVE	0x80	/* no DMA request active; bypass mode only */

#define NUM_IOPS	2
#define NUM_IOP_CHAN	7
#define NUM_IOP_MSGS	NUM_IOP_CHAN*8
#define IOP_MSG_LEN	32

/* IOP reference numbers, used by the globally-visible iop_xxx functions */

#define IOP_NUM_SCC	0
#define IOP_NUM_ISM	1

/* IOP channel states */

#define IOP_MSG_IDLE		0       /* idle                         */
#define IOP_MSG_NEW		1       /* new message sent             */
#define IOP_MSG_RCVD		2       /* message received; processing */
#define IOP_MSG_COMPLETE	3       /* message processing complete  */

/* IOP message status codes */

#define IOP_MSGSTATUS_UNUSED	0	/* Unused message structure        */
#define IOP_MSGSTATUS_WAITING	1	/* waiting for channel             */
#define IOP_MSGSTATUS_SENT	2	/* message sent, awaiting reply    */
#define IOP_MSGSTATUS_COMPLETE	3	/* message complete and reply rcvd */
#define IOP_MSGSTATUS_UNSOL	6	/* message is unsolicited          */

/* IOP memory addresses of the members of the mac_iop_kernel structure. */

#define IOP_ADDR_MAX_SEND_CHAN	0x0200
#define IOP_ADDR_SEND_STATE	0x0201
#define IOP_ADDR_PATCH_CTRL	0x021F
#define IOP_ADDR_SEND_MSG	0x0220
#define IOP_ADDR_MAX_RECV_CHAN	0x0300
#define IOP_ADDR_RECV_STATE	0x0301
#define IOP_ADDR_ALIVE		0x031F
#define IOP_ADDR_RECV_MSG	0x0320

#ifndef __ASSEMBLER__

/*
 * IOP Control registers, staggered because in usual Apple style they were
 * too lazy to decode the A0 bit. This structure is assumed to begin at
 * one of the xxx_IOP_BASE addresses given above.
 */

struct mac_iop {
    __u8	ram_addr_hi;	/* shared RAM address hi byte */
    __u8	pad0;
    __u8	ram_addr_lo;	/* shared RAM address lo byte */
    __u8	pad1;
    __u8	status_ctrl;	/* status/control register */
    __u8	pad2[3];
    __u8	ram_data;	/* RAM data byte at ramhi/lo */

    __u8	pad3[23];

    /* Bypass-mode hardware access registers */

    union {
	struct {		/* SCC registers */
	    __u8 sccb_cmd;	/* SCC B command reg */
	    __u8 pad4;
	    __u8 scca_cmd;	/* SCC A command reg */
	    __u8 pad5;
	    __u8 sccb_data;	/* SCC B data */
	    __u8 pad6;
	    __u8 scca_data;	/* SCC A data */
	} scc_regs;

	struct {		/* ISM registers */
	    __u8 wdata;		/* write a data byte */
	    __u8 pad7;
	    __u8 wmark;		/* write a mark byte */
	    __u8 pad8;
	    __u8 wcrc;		/* write 2-byte crc to disk */
	    __u8 pad9;
	    __u8 wparams;	/* write the param regs */
	    __u8 pad10;
	    __u8 wphase;	/* write the phase states & dirs */
	    __u8 pad11;
	    __u8 wsetup;	/* write the setup register */
	    __u8 pad12;
	    __u8 wzeroes;	/* mode reg: 1's clr bits, 0's are x */
	    __u8 pad13;
	    __u8 wones;		/* mode reg: 1's set bits, 0's are x */
	    __u8 pad14;
	    __u8 rdata;		/* read a data byte */
	    __u8 pad15;
	    __u8 rmark;		/* read a mark byte */
	    __u8 pad16;
	    __u8 rerror;	/* read the error register */
	    __u8 pad17;
	    __u8 rparams;	/* read the param regs */
	    __u8 pad18;
	    __u8 rphase;	/* read the phase states & dirs */
	    __u8 pad19;
	    __u8 rsetup;	/* read the setup register */
	    __u8 pad20;
	    __u8 rmode;		/* read the mode register */
	    __u8 pad21;
	    __u8 rhandshake;	/* read the handshake register */
	} ism_regs;
    } b;
};

/* This structure is used to track IOP messages in the Linux kernel */

struct iop_msg {
	struct iop_msg	*next;		/* next message in queue or NULL     */
	uint	iop_num;		/* IOP number                        */
	uint	channel;		/* channel number                    */
	void	*caller_priv;		/* caller private data               */
	int	status;			/* status of this message            */
	__u8	message[IOP_MSG_LEN];	/* the message being sent/received   */
	__u8	reply[IOP_MSG_LEN];	/* the reply to the message          */
	void	(*handler)(struct iop_msg *);
					/* function to call when reply recvd */
};

extern int iop_scc_present,iop_ism_present;

extern int iop_listen(uint, uint,
			void (*handler)(struct iop_msg *),
			const char *);
extern int iop_send_message(uint, uint, void *, uint, __u8 *,
			    void (*)(struct iop_msg *));
extern void iop_complete_message(struct iop_msg *);
extern void iop_upload_code(uint, __u8 *, uint, __u16);
extern void iop_download_code(uint, __u8 *, uint, __u16);
extern __u8 *iop_compare_code(uint, __u8 *, uint, __u16);
extern void iop_ism_irq_poll(uint);

extern void iop_register_interrupts(void);

#endif /* __ASSEMBLER__ */
