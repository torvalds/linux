/* $Id: avmcard.h,v 1.1.4.1.2.1 2001/12/21 15:00:17 kai Exp $
 *
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef _AVMCARD_H_
#define _AVMCARD_H_

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/interrupt.h>

#define	AVMB1_PORTLEN		0x1f
#define AVM_MAXVERSION		8
#define AVM_NCCI_PER_CHANNEL	4

/*
 * Versions
 */

#define	VER_DRIVER	0
#define	VER_CARDTYPE	1
#define	VER_HWID	2
#define	VER_SERIAL	3
#define	VER_OPTION	4
#define	VER_PROTO	5
#define	VER_PROFILE	6
#define	VER_CAPI	7

enum avmcardtype {
	avm_b1isa,
	avm_b1pci,
	avm_b1pcmcia,
	avm_m1,
	avm_m2,
	avm_t1isa,
	avm_t1pci,
	avm_c4,
	avm_c2
};

typedef struct avmcard_dmabuf {
    long        size;
    u8       *dmabuf;
    dma_addr_t  dmaaddr;
} avmcard_dmabuf;

typedef struct avmcard_dmainfo {
	u32                recvlen;
        avmcard_dmabuf       recvbuf;

        avmcard_dmabuf       sendbuf;
	struct sk_buff_head  send_queue;

	struct pci_dev      *pcidev;
} avmcard_dmainfo;

typedef	struct avmctrl_info {
	char cardname[32];
	
	int versionlen;
	char versionbuf[1024];
	char *version[AVM_MAXVERSION];
	
	char infobuf[128];	/* for function procinfo */
	
	struct avmcard  *card;
	struct capi_ctr  capi_ctrl;
	
	struct list_head ncci_head;
} avmctrl_info;

typedef struct avmcard {
	char name[32];
  
	spinlock_t lock;
	unsigned int port;
	unsigned irq;
	unsigned long membase;
	enum avmcardtype cardtype;
	unsigned char revision;
	unsigned char class;
	int cardnr; /* for t1isa */

	char msgbuf[128];	/* capimsg msg part */
	char databuf[2048];	/* capimsg data part */

	void __iomem *mbase;
	volatile u32 csr;
	avmcard_dmainfo *dma;

	struct avmctrl_info *ctrlinfo;

	u_int nr_controllers;
	u_int nlogcontr;
	struct list_head list;
} avmcard;

extern int b1_irq_table[16];

/*
 * LLI Messages to the ISDN-ControllerISDN Controller 
 */

#define	SEND_POLL		0x72	/*
					   * after load <- RECEIVE_POLL 
					 */
#define SEND_INIT		0x11	/*
					   * first message <- RECEIVE_INIT
					   * int32 NumApplications  int32
					   * NumNCCIs int32 BoardNumber 
					 */
#define SEND_REGISTER		0x12	/*
					   * register an application int32
					   * ApplIDId int32 NumMessages
					   * int32 NumB3Connections int32
					   * NumB3Blocks int32 B3Size
					   * 
					   * AnzB3Connection != 0 &&
					   * AnzB3Blocks >= 1 && B3Size >= 1 
					 */
#define SEND_RELEASE		0x14	/*
					   * deregister an application int32 
					   * ApplID 
					 */
#define SEND_MESSAGE		0x15	/*
					   * send capi-message int32 length
					   * capi-data ... 
					 */
#define SEND_DATA_B3_REQ	0x13	/*
					   * send capi-data-message int32
					   * MsgLength capi-data ... int32
					   * B3Length data .... 
					 */

#define SEND_CONFIG		0x21    /*
                                         */

#define SEND_POLLACK		0x73    /* T1 Watchdog */

/*
 * LLI Messages from the ISDN-ControllerISDN Controller 
 */

#define RECEIVE_POLL		0x32	/*
					   * <- after SEND_POLL 
					 */
#define RECEIVE_INIT		0x27	/*
					   * <- after SEND_INIT int32 length
					   * byte total length b1struct board 
					   * driver revision b1struct card
					   * type b1struct reserved b1struct
					   * serial number b1struct driver
					   * capability b1struct d-channel
					   * protocol b1struct CAPI-2.0
					   * profile b1struct capi version 
					 */
#define RECEIVE_MESSAGE		0x21	/*
					   * <- after SEND_MESSAGE int32
					   * AppllID int32 Length capi-data
					   * .... 
					 */
#define RECEIVE_DATA_B3_IND	0x22	/*
					   * received data int32 AppllID
					   * int32 Length capi-data ...
					   * int32 B3Length data ... 
					 */
#define RECEIVE_START		0x23	/*
					   * Handshake 
					 */
#define RECEIVE_STOP		0x24	/*
					   * Handshake 
					 */
#define RECEIVE_NEW_NCCI	0x25	/*
					   * int32 AppllID int32 NCCI int32
					   * WindowSize 
					 */
#define RECEIVE_FREE_NCCI	0x26	/*
					   * int32 AppllID int32 NCCI 
					 */
#define RECEIVE_RELEASE		0x26	/*
					   * int32 AppllID int32 0xffffffff 
					 */
#define RECEIVE_TASK_READY	0x31	/*
					   * int32 tasknr
					   * int32 Length Taskname ...
					 */
#define RECEIVE_DEBUGMSG	0x71	/*
					   * int32 Length message
					   * 
					 */
#define RECEIVE_POLLDWORD	0x75	/* t1pci in dword mode */

#define WRITE_REGISTER		0x00
#define READ_REGISTER		0x01

/*
 * port offsets
 */

#define B1_READ			0x00
#define B1_WRITE		0x01
#define B1_INSTAT		0x02
#define B1_OUTSTAT		0x03
#define B1_ANALYSE		0x04
#define B1_REVISION		0x05
#define B1_RESET		0x10


#define B1_STAT0(cardtype)  ((cardtype) == avm_m1 ? 0x81200000l : 0x80A00000l)
#define B1_STAT1(cardtype)  (0x80E00000l)

/* ---------------------------------------------------------------- */

static inline unsigned char b1outp(unsigned int base,
				   unsigned short offset,
				   unsigned char value)
{
	outb(value, base + offset);
	return inb(base + B1_ANALYSE);
}


static inline int b1_rx_full(unsigned int base)
{
	return inb(base + B1_INSTAT) & 0x1;
}

static inline unsigned char b1_get_byte(unsigned int base)
{
	unsigned long stop = jiffies + 1 * HZ;	/* maximum wait time 1 sec */
	while (!b1_rx_full(base) && time_before(jiffies, stop));
	if (b1_rx_full(base))
		return inb(base + B1_READ);
	printk(KERN_CRIT "b1lli(0x%x): rx not full after 1 second\n", base);
	return 0;
}

static inline unsigned int b1_get_word(unsigned int base)
{
	unsigned int val = 0;
	val |= b1_get_byte(base);
	val |= (b1_get_byte(base) << 8);
	val |= (b1_get_byte(base) << 16);
	val |= (b1_get_byte(base) << 24);
	return val;
}

static inline int b1_tx_empty(unsigned int base)
{
	return inb(base + B1_OUTSTAT) & 0x1;
}

static inline void b1_put_byte(unsigned int base, unsigned char val)
{
	while (!b1_tx_empty(base));
	b1outp(base, B1_WRITE, val);
}

static inline int b1_save_put_byte(unsigned int base, unsigned char val)
{
	unsigned long stop = jiffies + 2 * HZ;
	while (!b1_tx_empty(base) && time_before(jiffies,stop));
	if (!b1_tx_empty(base)) return -1;
	b1outp(base, B1_WRITE, val);
	return 0;
}

static inline void b1_put_word(unsigned int base, unsigned int val)
{
	b1_put_byte(base, val & 0xff);
	b1_put_byte(base, (val >> 8) & 0xff);
	b1_put_byte(base, (val >> 16) & 0xff);
	b1_put_byte(base, (val >> 24) & 0xff);
}

static inline unsigned int b1_get_slice(unsigned int base,
					unsigned char *dp)
{
	unsigned int len, i;

	len = i = b1_get_word(base);
	while (i-- > 0) *dp++ = b1_get_byte(base);
	return len;
}

static inline void b1_put_slice(unsigned int base,
				unsigned char *dp, unsigned int len)
{
	unsigned i = len;
	b1_put_word(base, i);
	while (i-- > 0)
		b1_put_byte(base, *dp++);
}

static void b1_wr_reg(unsigned int base,
                      unsigned int reg,
		      unsigned int value)
{
	b1_put_byte(base, WRITE_REGISTER);
        b1_put_word(base, reg);
        b1_put_word(base, value);
}

static inline unsigned int b1_rd_reg(unsigned int base,
                                     unsigned int reg)
{
	b1_put_byte(base, READ_REGISTER);
        b1_put_word(base, reg);
        return b1_get_word(base);
	
}

static inline void b1_reset(unsigned int base)
{
	b1outp(base, B1_RESET, 0);
	mdelay(55 * 2);	/* 2 TIC's */

	b1outp(base, B1_RESET, 1);
	mdelay(55 * 2);	/* 2 TIC's */

	b1outp(base, B1_RESET, 0);
	mdelay(55 * 2);	/* 2 TIC's */
}

static inline unsigned char b1_disable_irq(unsigned int base)
{
	return b1outp(base, B1_INSTAT, 0x00);
}

/* ---------------------------------------------------------------- */

static inline void b1_set_test_bit(unsigned int base,
				   enum avmcardtype cardtype,
				   int onoff)
{
    b1_wr_reg(base, B1_STAT0(cardtype), onoff ? 0x21 : 0x20);
}

static inline int b1_get_test_bit(unsigned int base,
                                  enum avmcardtype cardtype)
{
    return (b1_rd_reg(base, B1_STAT0(cardtype)) & 0x01) != 0;
}

/* ---------------------------------------------------------------- */

#define T1_FASTLINK		0x00
#define T1_SLOWLINK		0x08

#define T1_READ			B1_READ
#define T1_WRITE		B1_WRITE
#define T1_INSTAT		B1_INSTAT
#define T1_OUTSTAT		B1_OUTSTAT
#define T1_IRQENABLE		0x05
#define T1_FIFOSTAT		0x06
#define T1_RESETLINK		0x10
#define T1_ANALYSE		0x11
#define T1_IRQMASTER		0x12
#define T1_IDENT		0x17
#define T1_RESETBOARD		0x1f

#define	T1F_IREADY		0x01
#define	T1F_IHALF		0x02
#define	T1F_IFULL		0x04
#define	T1F_IEMPTY		0x08
#define	T1F_IFLAGS		0xF0

#define	T1F_OREADY		0x10
#define	T1F_OHALF		0x20
#define	T1F_OEMPTY		0x40
#define	T1F_OFULL		0x80
#define	T1F_OFLAGS		0xF0

/* there are HEMA cards with 1k and 4k FIFO out */
#define FIFO_OUTBSIZE		256
#define FIFO_INPBSIZE		512

#define HEMA_VERSION_ID		0
#define HEMA_PAL_ID		0

static inline void t1outp(unsigned int base,
			  unsigned short offset,
			  unsigned char value)
{
	outb(value, base + offset);
}

static inline unsigned char t1inp(unsigned int base,
			          unsigned short offset)
{
	return inb(base + offset);
}

static inline int t1_isfastlink(unsigned int base)
{
	return (inb(base + T1_IDENT) & ~0x82) == 1;
}

static inline unsigned char t1_fifostatus(unsigned int base)
{
	return inb(base + T1_FIFOSTAT);
}

static inline unsigned int t1_get_slice(unsigned int base,
					unsigned char *dp)
{
	unsigned int len, i;
#ifdef FASTLINK_DEBUG
	unsigned wcnt = 0, bcnt = 0;
#endif

	len = i = b1_get_word(base);
        if (t1_isfastlink(base)) {
		int status;
		while (i > 0) {
			status = t1_fifostatus(base) & (T1F_IREADY|T1F_IHALF);
			if (i >= FIFO_INPBSIZE) status |= T1F_IFULL;

			switch (status) {
				case T1F_IREADY|T1F_IHALF|T1F_IFULL:
					insb(base+B1_READ, dp, FIFO_INPBSIZE);
					dp += FIFO_INPBSIZE;
					i -= FIFO_INPBSIZE;
#ifdef FASTLINK_DEBUG
					wcnt += FIFO_INPBSIZE;
#endif
					break;
				case T1F_IREADY|T1F_IHALF: 
					insb(base+B1_READ,dp, i);
#ifdef FASTLINK_DEBUG
					wcnt += i;
#endif
					dp += i;
					i = 0;
					if (i == 0)
						break;
					/* fall through */
				default:
					*dp++ = b1_get_byte(base);
					i--;
#ifdef FASTLINK_DEBUG
					bcnt++;
#endif
					break;
			}
	    }
#ifdef FASTLINK_DEBUG
	    if (wcnt)
	    printk(KERN_DEBUG "b1lli(0x%x): get_slice l=%d w=%d b=%d\n",
				base, len, wcnt, bcnt);
#endif
	} else {
		while (i-- > 0)
			*dp++ = b1_get_byte(base);
	}
	return len;
}

static inline void t1_put_slice(unsigned int base,
				unsigned char *dp, unsigned int len)
{
	unsigned i = len;
	b1_put_word(base, i);
        if (t1_isfastlink(base)) {
		int status;
		while (i > 0) {
			status = t1_fifostatus(base) & (T1F_OREADY|T1F_OHALF);
			if (i >= FIFO_OUTBSIZE) status |= T1F_OEMPTY;
			switch (status) {
				case T1F_OREADY|T1F_OHALF|T1F_OEMPTY: 
					outsb(base+B1_WRITE, dp, FIFO_OUTBSIZE);
					dp += FIFO_OUTBSIZE;
					i -= FIFO_OUTBSIZE;
					break;
				case T1F_OREADY|T1F_OHALF: 
					outsb(base+B1_WRITE, dp, i);
					dp += i;
					i = 0;
				        break;
				default:
					b1_put_byte(base, *dp++);
					i--;
					break;
			}
		}
	} else {
		while (i-- > 0)
			b1_put_byte(base, *dp++);
	}
}

static inline void t1_disable_irq(unsigned int base)
{
      t1outp(base, T1_IRQMASTER, 0x00);
}

static inline void t1_reset(unsigned int base)
{
        /* reset T1 Controller */
        b1_reset(base);
        /* disable irq on HEMA */
        t1outp(base, B1_INSTAT, 0x00);
        t1outp(base, B1_OUTSTAT, 0x00);
        t1outp(base, T1_IRQMASTER, 0x00);
        /* reset HEMA board configuration */
	t1outp(base, T1_RESETBOARD, 0xf);
}

static inline void b1_setinterrupt(unsigned int base, unsigned irq,
				   enum avmcardtype cardtype)
{
	switch (cardtype) {
	   case avm_t1isa:
              t1outp(base, B1_INSTAT, 0x00);
              t1outp(base, B1_INSTAT, 0x02);
	      t1outp(base, T1_IRQMASTER, 0x08);
	      break;
	   case avm_b1isa:
	      b1outp(base, B1_INSTAT, 0x00);
	      b1outp(base, B1_RESET, b1_irq_table[irq]);
	      b1outp(base, B1_INSTAT, 0x02);
	      break;
	   default:
	   case avm_m1:
	   case avm_m2:
	   case avm_b1pci:
	      b1outp(base, B1_INSTAT, 0x00);
	      b1outp(base, B1_RESET, 0xf0);
	      b1outp(base, B1_INSTAT, 0x02);
	      break;
	   case avm_c4:
	   case avm_t1pci:
	      b1outp(base, B1_RESET, 0xf0);
	      break;
	 }
}

/* b1.c */
avmcard *b1_alloc_card(int nr_controllers);
void b1_free_card(avmcard *card);
int b1_detect(unsigned int base, enum avmcardtype cardtype);
void b1_getrevision(avmcard *card);
int b1_load_t4file(avmcard *card, capiloaddatapart * t4file);
int b1_load_config(avmcard *card, capiloaddatapart * config);
int b1_loaded(avmcard *card);

int b1_load_firmware(struct capi_ctr *ctrl, capiloaddata *data);
void b1_reset_ctr(struct capi_ctr *ctrl);
void b1_register_appl(struct capi_ctr *ctrl, u16 appl,
				capi_register_params *rp);
void b1_release_appl(struct capi_ctr *ctrl, u16 appl);
u16  b1_send_message(struct capi_ctr *ctrl, struct sk_buff *skb);
void b1_parse_version(avmctrl_info *card);
irqreturn_t b1_interrupt(int interrupt, void *devptr, struct pt_regs *regs);

int b1ctl_read_proc(char *page, char **start, off_t off,
        		int count, int *eof, struct capi_ctr *ctrl);

avmcard_dmainfo *avmcard_dma_alloc(char *name, struct pci_dev *,
				   long rsize, long ssize);
void avmcard_dma_free(avmcard_dmainfo *);

/* b1dma.c */
int b1pciv4_detect(avmcard *card);
int t1pci_detect(avmcard *card);
void b1dma_reset(avmcard *card);
irqreturn_t b1dma_interrupt(int interrupt, void *devptr, struct pt_regs *regs);

int b1dma_load_firmware(struct capi_ctr *ctrl, capiloaddata *data);
void b1dma_reset_ctr(struct capi_ctr *ctrl);
void b1dma_remove_ctr(struct capi_ctr *ctrl);
void b1dma_register_appl(struct capi_ctr *ctrl,
				u16 appl,
				capi_register_params *rp);
void b1dma_release_appl(struct capi_ctr *ctrl, u16 appl);
u16  b1dma_send_message(struct capi_ctr *ctrl, struct sk_buff *skb);
int b1dmactl_read_proc(char *page, char **start, off_t off,
        		int count, int *eof, struct capi_ctr *ctrl);

#endif /* _AVMCARD_H_ */
