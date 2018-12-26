/* $Id: hfc_pci.c,v 1.48.2.4 2004/02/11 13:21:33 keil Exp $
 *
 * low level driver for CCD's hfc-pci based cards
 *
 * Author       Werner Cornelius
 *              based on existing driver for CCD hfc ISA cards
 * Copyright    by Werner Cornelius  <werner@isdn4linux.de>
 *              by Karsten Keil      <keil@isdn4linux.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For changes and modifications please read
 * Documentation/isdn/HiSax.cert
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "hfc_pci.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

static const char *hfcpci_revision = "$Revision: 1.48.2.4 $";

/* table entry in the PCI devices list */
typedef struct {
	int vendor_id;
	int device_id;
	char *vendor_name;
	char *card_name;
} PCI_ENTRY;

#define NT_T1_COUNT	20	/* number of 3.125ms interrupts for G2 timeout */
#define CLKDEL_TE	0x0e	/* CLKDEL in TE mode */
#define CLKDEL_NT	0x6c	/* CLKDEL in NT mode */

static const PCI_ENTRY id_list[] =
{
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_2BD0, "CCD/Billion/Asuscom", "2BD0"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B000, "Billion", "B000"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B006, "Billion", "B006"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B007, "Billion", "B007"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B008, "Billion", "B008"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B009, "Billion", "B009"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B00A, "Billion", "B00A"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B00B, "Billion", "B00B"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B00C, "Billion", "B00C"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B100, "Seyeon", "B100"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B700, "Primux II S0", "B700"},
	{PCI_VENDOR_ID_CCD, PCI_DEVICE_ID_CCD_B701, "Primux II S0 NT", "B701"},
	{PCI_VENDOR_ID_ABOCOM, PCI_DEVICE_ID_ABOCOM_2BD1, "Abocom/Magitek", "2BD1"},
	{PCI_VENDOR_ID_ASUSTEK, PCI_DEVICE_ID_ASUSTEK_0675, "Asuscom/Askey", "675"},
	{PCI_VENDOR_ID_BERKOM, PCI_DEVICE_ID_BERKOM_T_CONCEPT, "German telekom", "T-Concept"},
	{PCI_VENDOR_ID_BERKOM, PCI_DEVICE_ID_BERKOM_A1T, "German telekom", "A1T"},
	{PCI_VENDOR_ID_ANIGMA, PCI_DEVICE_ID_ANIGMA_MC145575, "Motorola MC145575", "MC145575"},
	{PCI_VENDOR_ID_ZOLTRIX, PCI_DEVICE_ID_ZOLTRIX_2BD0, "Zoltrix", "2BD0"},
	{PCI_VENDOR_ID_DIGI, PCI_DEVICE_ID_DIGI_DF_M_IOM2_E, "Digi International", "Digi DataFire Micro V IOM2 (Europe)"},
	{PCI_VENDOR_ID_DIGI, PCI_DEVICE_ID_DIGI_DF_M_E, "Digi International", "Digi DataFire Micro V (Europe)"},
	{PCI_VENDOR_ID_DIGI, PCI_DEVICE_ID_DIGI_DF_M_IOM2_A, "Digi International", "Digi DataFire Micro V IOM2 (North America)"},
	{PCI_VENDOR_ID_DIGI, PCI_DEVICE_ID_DIGI_DF_M_A, "Digi International", "Digi DataFire Micro V (North America)"},
	{PCI_VENDOR_ID_SITECOM, PCI_DEVICE_ID_SITECOM_DC105V2, "Sitecom Europe", "DC-105 ISDN PCI"},
	{0, 0, NULL, NULL},
};


/******************************************/
/* free hardware resources used by driver */
/******************************************/
static void
release_io_hfcpci(struct IsdnCardState *cs)
{
	printk(KERN_INFO "HiSax: release hfcpci at %p\n",
	       cs->hw.hfcpci.pci_io);
	cs->hw.hfcpci.int_m2 = 0;					/* interrupt output off ! */
	Write_hfc(cs, HFCPCI_INT_M2, cs->hw.hfcpci.int_m2);
	Write_hfc(cs, HFCPCI_CIRM, HFCPCI_RESET);			/* Reset On */
	mdelay(10);
	Write_hfc(cs, HFCPCI_CIRM, 0);					/* Reset Off */
	mdelay(10);
	Write_hfc(cs, HFCPCI_INT_M2, cs->hw.hfcpci.int_m2);
	pci_write_config_word(cs->hw.hfcpci.dev, PCI_COMMAND, 0);	/* disable memory mapped ports + busmaster */
	del_timer(&cs->hw.hfcpci.timer);
	pci_free_consistent(cs->hw.hfcpci.dev, 0x8000,
			    cs->hw.hfcpci.fifos, cs->hw.hfcpci.dma);
	cs->hw.hfcpci.fifos = NULL;
	iounmap((void *)cs->hw.hfcpci.pci_io);
}

/********************************************************************************/
/* function called to reset the HFC PCI chip. A complete software reset of chip */
/* and fifos is done.                                                           */
/********************************************************************************/
static void
reset_hfcpci(struct IsdnCardState *cs)
{
	pci_write_config_word(cs->hw.hfcpci.dev, PCI_COMMAND, PCI_ENA_MEMIO);	/* enable memory mapped ports, disable busmaster */
	cs->hw.hfcpci.int_m2 = 0;	/* interrupt output off ! */
	Write_hfc(cs, HFCPCI_INT_M2, cs->hw.hfcpci.int_m2);

	printk(KERN_INFO "HFC_PCI: resetting card\n");
	pci_write_config_word(cs->hw.hfcpci.dev, PCI_COMMAND, PCI_ENA_MEMIO + PCI_ENA_MASTER);	/* enable memory ports + busmaster */
	Write_hfc(cs, HFCPCI_CIRM, HFCPCI_RESET);	/* Reset On */
	mdelay(10);
	Write_hfc(cs, HFCPCI_CIRM, 0);	/* Reset Off */
	mdelay(10);
	if (Read_hfc(cs, HFCPCI_STATUS) & 2)
		printk(KERN_WARNING "HFC-PCI init bit busy\n");

	cs->hw.hfcpci.fifo_en = 0x30;	/* only D fifos enabled */
	Write_hfc(cs, HFCPCI_FIFO_EN, cs->hw.hfcpci.fifo_en);

	cs->hw.hfcpci.trm = 0 + HFCPCI_BTRANS_THRESMASK;	/* no echo connect , threshold */
	Write_hfc(cs, HFCPCI_TRM, cs->hw.hfcpci.trm);

	Write_hfc(cs, HFCPCI_CLKDEL, CLKDEL_TE); /* ST-Bit delay for TE-Mode */
	cs->hw.hfcpci.sctrl_e = HFCPCI_AUTO_AWAKE;
	Write_hfc(cs, HFCPCI_SCTRL_E, cs->hw.hfcpci.sctrl_e);	/* S/T Auto awake */
	cs->hw.hfcpci.bswapped = 0;	/* no exchange */
	cs->hw.hfcpci.nt_mode = 0;	/* we are in TE mode */
	cs->hw.hfcpci.ctmt = HFCPCI_TIM3_125 | HFCPCI_AUTO_TIMER;
	Write_hfc(cs, HFCPCI_CTMT, cs->hw.hfcpci.ctmt);

	cs->hw.hfcpci.int_m1 = HFCPCI_INTS_DTRANS | HFCPCI_INTS_DREC |
		HFCPCI_INTS_L1STATE | HFCPCI_INTS_TIMER;
	Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);

	/* Clear already pending ints */
	if (Read_hfc(cs, HFCPCI_INT_S1));

	Write_hfc(cs, HFCPCI_STATES, HFCPCI_LOAD_STATE | 2);	/* HFC ST 2 */
	udelay(10);
	Write_hfc(cs, HFCPCI_STATES, 2);	/* HFC ST 2 */
	cs->hw.hfcpci.mst_m = HFCPCI_MASTER;	/* HFC Master Mode */

	Write_hfc(cs, HFCPCI_MST_MODE, cs->hw.hfcpci.mst_m);
	cs->hw.hfcpci.sctrl = 0x40;	/* set tx_lo mode, error in datasheet ! */
	Write_hfc(cs, HFCPCI_SCTRL, cs->hw.hfcpci.sctrl);
	cs->hw.hfcpci.sctrl_r = 0;
	Write_hfc(cs, HFCPCI_SCTRL_R, cs->hw.hfcpci.sctrl_r);

	/* Init GCI/IOM2 in master mode */
	/* Slots 0 and 1 are set for B-chan 1 and 2 */
	/* D- and monitor/CI channel are not enabled */
	/* STIO1 is used as output for data, B1+B2 from ST->IOM+HFC */
	/* STIO2 is used as data input, B1+B2 from IOM->ST */
	/* ST B-channel send disabled -> continuous 1s */
	/* The IOM slots are always enabled */
	cs->hw.hfcpci.conn = 0x36;	/* set data flow directions */
	Write_hfc(cs, HFCPCI_CONNECT, cs->hw.hfcpci.conn);
	Write_hfc(cs, HFCPCI_B1_SSL, 0x80);	/* B1-Slot 0 STIO1 out enabled */
	Write_hfc(cs, HFCPCI_B2_SSL, 0x81);	/* B2-Slot 1 STIO1 out enabled */
	Write_hfc(cs, HFCPCI_B1_RSL, 0x80);	/* B1-Slot 0 STIO2 in enabled */
	Write_hfc(cs, HFCPCI_B2_RSL, 0x81);	/* B2-Slot 1 STIO2 in enabled */

	/* Finally enable IRQ output */
	cs->hw.hfcpci.int_m2 = HFCPCI_IRQ_ENABLE;
	Write_hfc(cs, HFCPCI_INT_M2, cs->hw.hfcpci.int_m2);
	if (Read_hfc(cs, HFCPCI_INT_S1));
}

/***************************************************/
/* Timer function called when kernel timer expires */
/***************************************************/
static void
hfcpci_Timer(struct timer_list *t)
{
	struct IsdnCardState *cs = from_timer(cs, t, hw.hfcpci.timer);
	cs->hw.hfcpci.timer.expires = jiffies + 75;
	/* WD RESET */
/*      WriteReg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcpci.ctmt | 0x80);
	add_timer(&cs->hw.hfcpci.timer);
*/
}


/*********************************/
/* schedule a new D-channel task */
/*********************************/
static void
sched_event_D_pci(struct IsdnCardState *cs, int event)
{
	test_and_set_bit(event, &cs->event);
	schedule_work(&cs->tqueue);
}

/*********************************/
/* schedule a new b_channel task */
/*********************************/
static void
hfcpci_sched_event(struct BCState *bcs, int event)
{
	test_and_set_bit(event, &bcs->event);
	schedule_work(&bcs->tqueue);
}

/************************************************/
/* select a b-channel entry matching and active */
/************************************************/
static
struct BCState *
Sel_BCS(struct IsdnCardState *cs, int channel)
{
	if (cs->bcs[0].mode && (cs->bcs[0].channel == channel))
		return (&cs->bcs[0]);
	else if (cs->bcs[1].mode && (cs->bcs[1].channel == channel))
		return (&cs->bcs[1]);
	else
		return (NULL);
}

/***************************************/
/* clear the desired B-channel rx fifo */
/***************************************/
static void hfcpci_clear_fifo_rx(struct IsdnCardState *cs, int fifo)
{       u_char fifo_state;
	bzfifo_type *bzr;

	if (fifo) {
		bzr = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxbz_b2;
		fifo_state = cs->hw.hfcpci.fifo_en & HFCPCI_FIFOEN_B2RX;
	} else {
		bzr = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxbz_b1;
		fifo_state = cs->hw.hfcpci.fifo_en & HFCPCI_FIFOEN_B1RX;
	}
	if (fifo_state)
		cs->hw.hfcpci.fifo_en ^= fifo_state;
	Write_hfc(cs, HFCPCI_FIFO_EN, cs->hw.hfcpci.fifo_en);
	cs->hw.hfcpci.last_bfifo_cnt[fifo] = 0;
	bzr->za[MAX_B_FRAMES].z1 = B_FIFO_SIZE + B_SUB_VAL - 1;
	bzr->za[MAX_B_FRAMES].z2 = bzr->za[MAX_B_FRAMES].z1;
	bzr->f1 = MAX_B_FRAMES;
	bzr->f2 = bzr->f1;	/* init F pointers to remain constant */
	if (fifo_state)
		cs->hw.hfcpci.fifo_en |= fifo_state;
	Write_hfc(cs, HFCPCI_FIFO_EN, cs->hw.hfcpci.fifo_en);
}

/***************************************/
/* clear the desired B-channel tx fifo */
/***************************************/
static void hfcpci_clear_fifo_tx(struct IsdnCardState *cs, int fifo)
{       u_char fifo_state;
	bzfifo_type *bzt;

	if (fifo) {
		bzt = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.txbz_b2;
		fifo_state = cs->hw.hfcpci.fifo_en & HFCPCI_FIFOEN_B2TX;
	} else {
		bzt = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.txbz_b1;
		fifo_state = cs->hw.hfcpci.fifo_en & HFCPCI_FIFOEN_B1TX;
	}
	if (fifo_state)
		cs->hw.hfcpci.fifo_en ^= fifo_state;
	Write_hfc(cs, HFCPCI_FIFO_EN, cs->hw.hfcpci.fifo_en);
	bzt->za[MAX_B_FRAMES].z1 = B_FIFO_SIZE + B_SUB_VAL - 1;
	bzt->za[MAX_B_FRAMES].z2 = bzt->za[MAX_B_FRAMES].z1;
	bzt->f1 = MAX_B_FRAMES;
	bzt->f2 = bzt->f1;	/* init F pointers to remain constant */
	if (fifo_state)
		cs->hw.hfcpci.fifo_en |= fifo_state;
	Write_hfc(cs, HFCPCI_FIFO_EN, cs->hw.hfcpci.fifo_en);
}

/*********************************************/
/* read a complete B-frame out of the buffer */
/*********************************************/
static struct sk_buff
*
hfcpci_empty_fifo(struct BCState *bcs, bzfifo_type *bz, u_char *bdata, int count)
{
	u_char *ptr, *ptr1, new_f2;
	struct sk_buff *skb;
	struct IsdnCardState *cs = bcs->cs;
	int total, maxlen, new_z2;
	z_type *zp;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hfcpci_empty_fifo");
	zp = &bz->za[bz->f2];	/* point to Z-Regs */
	new_z2 = zp->z2 + count;	/* new position in fifo */
	if (new_z2 >= (B_FIFO_SIZE + B_SUB_VAL))
		new_z2 -= B_FIFO_SIZE;	/* buffer wrap */
	new_f2 = (bz->f2 + 1) & MAX_B_FRAMES;
	if ((count > HSCX_BUFMAX + 3) || (count < 4) ||
	    (*(bdata + (zp->z1 - B_SUB_VAL)))) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hfcpci_empty_fifo: incoming packet invalid length %d or crc", count);
#ifdef ERROR_STATISTIC
		bcs->err_inv++;
#endif
		bz->za[new_f2].z2 = new_z2;
		bz->f2 = new_f2;	/* next buffer */
		skb = NULL;
	} else if (!(skb = dev_alloc_skb(count - 3)))
		printk(KERN_WARNING "HFCPCI: receive out of memory\n");
	else {
		total = count;
		count -= 3;
		ptr = skb_put(skb, count);

		if (zp->z2 + count <= B_FIFO_SIZE + B_SUB_VAL)
			maxlen = count;		/* complete transfer */
		else
			maxlen = B_FIFO_SIZE + B_SUB_VAL - zp->z2;	/* maximum */

		ptr1 = bdata + (zp->z2 - B_SUB_VAL);	/* start of data */
		memcpy(ptr, ptr1, maxlen);	/* copy data */
		count -= maxlen;

		if (count) {	/* rest remaining */
			ptr += maxlen;
			ptr1 = bdata;	/* start of buffer */
			memcpy(ptr, ptr1, count);	/* rest */
		}
		bz->za[new_f2].z2 = new_z2;
		bz->f2 = new_f2;	/* next buffer */

	}
	return (skb);
}

/*******************************/
/* D-channel receive procedure */
/*******************************/
static
int
receive_dmsg(struct IsdnCardState *cs)
{
	struct sk_buff *skb;
	int maxlen;
	int rcnt, total;
	int count = 5;
	u_char *ptr, *ptr1;
	dfifo_type *df;
	z_type *zp;

	df = &((fifo_area *) (cs->hw.hfcpci.fifos))->d_chan.d_rx;
	if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		debugl1(cs, "rec_dmsg blocked");
		return (1);
	}
	while (((df->f1 & D_FREG_MASK) != (df->f2 & D_FREG_MASK)) && count--) {
		zp = &df->za[df->f2 & D_FREG_MASK];
		rcnt = zp->z1 - zp->z2;
		if (rcnt < 0)
			rcnt += D_FIFO_SIZE;
		rcnt++;
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfcpci recd f1(%d) f2(%d) z1(%x) z2(%x) cnt(%d)",
				df->f1, df->f2, zp->z1, zp->z2, rcnt);

		if ((rcnt > MAX_DFRAME_LEN + 3) || (rcnt < 4) ||
		    (df->data[zp->z1])) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "empty_fifo hfcpci packet inv. len %d or crc %d", rcnt, df->data[zp->z1]);
#ifdef ERROR_STATISTIC
			cs->err_rx++;
#endif
			df->f2 = ((df->f2 + 1) & MAX_D_FRAMES) | (MAX_D_FRAMES + 1);	/* next buffer */
			df->za[df->f2 & D_FREG_MASK].z2 = (zp->z2 + rcnt) & (D_FIFO_SIZE - 1);
		} else if ((skb = dev_alloc_skb(rcnt - 3))) {
			total = rcnt;
			rcnt -= 3;
			ptr = skb_put(skb, rcnt);

			if (zp->z2 + rcnt <= D_FIFO_SIZE)
				maxlen = rcnt;	/* complete transfer */
			else
				maxlen = D_FIFO_SIZE - zp->z2;	/* maximum */

			ptr1 = df->data + zp->z2;	/* start of data */
			memcpy(ptr, ptr1, maxlen);	/* copy data */
			rcnt -= maxlen;

			if (rcnt) {	/* rest remaining */
				ptr += maxlen;
				ptr1 = df->data;	/* start of buffer */
				memcpy(ptr, ptr1, rcnt);	/* rest */
			}
			df->f2 = ((df->f2 + 1) & MAX_D_FRAMES) | (MAX_D_FRAMES + 1);	/* next buffer */
			df->za[df->f2 & D_FREG_MASK].z2 = (zp->z2 + total) & (D_FIFO_SIZE - 1);

			skb_queue_tail(&cs->rq, skb);
			sched_event_D_pci(cs, D_RCVBUFREADY);
		} else
			printk(KERN_WARNING "HFC-PCI: D receive out of memory\n");
	}
	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	return (1);
}

/*******************************************************************************/
/* check for transparent receive data and read max one threshold size if avail */
/*******************************************************************************/
static int
hfcpci_empty_fifo_trans(struct BCState *bcs, bzfifo_type *bz, u_char *bdata)
{
	unsigned short *z1r, *z2r;
	int new_z2, fcnt, maxlen;
	struct sk_buff *skb;
	u_char *ptr, *ptr1;

	z1r = &bz->za[MAX_B_FRAMES].z1;		/* pointer to z reg */
	z2r = z1r + 1;

	if (!(fcnt = *z1r - *z2r))
		return (0);	/* no data avail */

	if (fcnt <= 0)
		fcnt += B_FIFO_SIZE;	/* bytes actually buffered */
	if (fcnt > HFCPCI_BTRANS_THRESHOLD)
		fcnt = HFCPCI_BTRANS_THRESHOLD;		/* limit size */

	new_z2 = *z2r + fcnt;	/* new position in fifo */
	if (new_z2 >= (B_FIFO_SIZE + B_SUB_VAL))
		new_z2 -= B_FIFO_SIZE;	/* buffer wrap */

	if (!(skb = dev_alloc_skb(fcnt)))
		printk(KERN_WARNING "HFCPCI: receive out of memory\n");
	else {
		ptr = skb_put(skb, fcnt);
		if (*z2r + fcnt <= B_FIFO_SIZE + B_SUB_VAL)
			maxlen = fcnt;	/* complete transfer */
		else
			maxlen = B_FIFO_SIZE + B_SUB_VAL - *z2r;	/* maximum */

		ptr1 = bdata + (*z2r - B_SUB_VAL);	/* start of data */
		memcpy(ptr, ptr1, maxlen);	/* copy data */
		fcnt -= maxlen;

		if (fcnt) {	/* rest remaining */
			ptr += maxlen;
			ptr1 = bdata;	/* start of buffer */
			memcpy(ptr, ptr1, fcnt);	/* rest */
		}
		skb_queue_tail(&bcs->rqueue, skb);
		hfcpci_sched_event(bcs, B_RCVBUFREADY);
	}

	*z2r = new_z2;		/* new position */
	return (1);
}				/* hfcpci_empty_fifo_trans */

/**********************************/
/* B-channel main receive routine */
/**********************************/
static void
main_rec_hfcpci(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int rcnt, real_fifo;
	int receive, count = 5;
	struct sk_buff *skb;
	bzfifo_type *bz;
	u_char *bdata;
	z_type *zp;


	if ((bcs->channel) && (!cs->hw.hfcpci.bswapped)) {
		bz = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxbz_b2;
		bdata = ((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxdat_b2;
		real_fifo = 1;
	} else {
		bz = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxbz_b1;
		bdata = ((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxdat_b1;
		real_fifo = 0;
	}
Begin:
	count--;
	if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		debugl1(cs, "rec_data %d blocked", bcs->channel);
		return;
	}
	if (bz->f1 != bz->f2) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfcpci rec %d f1(%d) f2(%d)",
				bcs->channel, bz->f1, bz->f2);
		zp = &bz->za[bz->f2];

		rcnt = zp->z1 - zp->z2;
		if (rcnt < 0)
			rcnt += B_FIFO_SIZE;
		rcnt++;
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfcpci rec %d z1(%x) z2(%x) cnt(%d)",
				bcs->channel, zp->z1, zp->z2, rcnt);
		if ((skb = hfcpci_empty_fifo(bcs, bz, bdata, rcnt))) {
			skb_queue_tail(&bcs->rqueue, skb);
			hfcpci_sched_event(bcs, B_RCVBUFREADY);
		}
		rcnt = bz->f1 - bz->f2;
		if (rcnt < 0)
			rcnt += MAX_B_FRAMES + 1;
		if (cs->hw.hfcpci.last_bfifo_cnt[real_fifo] > rcnt + 1) {
			rcnt = 0;
			hfcpci_clear_fifo_rx(cs, real_fifo);
		}
		cs->hw.hfcpci.last_bfifo_cnt[real_fifo] = rcnt;
		if (rcnt > 1)
			receive = 1;
		else
			receive = 0;
	} else if (bcs->mode == L1_MODE_TRANS)
		receive = hfcpci_empty_fifo_trans(bcs, bz, bdata);
	else
		receive = 0;
	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	if (count && receive)
		goto Begin;
}

/**************************/
/* D-channel send routine */
/**************************/
static void
hfcpci_fill_dfifo(struct IsdnCardState *cs)
{
	int fcnt;
	int count, new_z1, maxlen;
	dfifo_type *df;
	u_char *src, *dst, new_f1;

	if (!cs->tx_skb)
		return;
	if (cs->tx_skb->len <= 0)
		return;

	df = &((fifo_area *) (cs->hw.hfcpci.fifos))->d_chan.d_tx;

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "hfcpci_fill_Dfifo f1(%d) f2(%d) z1(f1)(%x)",
			df->f1, df->f2,
			df->za[df->f1 & D_FREG_MASK].z1);
	fcnt = df->f1 - df->f2;	/* frame count actually buffered */
	if (fcnt < 0)
		fcnt += (MAX_D_FRAMES + 1);	/* if wrap around */
	if (fcnt > (MAX_D_FRAMES - 1)) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfcpci_fill_Dfifo more as 14 frames");
#ifdef ERROR_STATISTIC
		cs->err_tx++;
#endif
		return;
	}
	/* now determine free bytes in FIFO buffer */
	count = df->za[df->f2 & D_FREG_MASK].z2 - df->za[df->f1 & D_FREG_MASK].z1 - 1;
	if (count <= 0)
		count += D_FIFO_SIZE;	/* count now contains available bytes */

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "hfcpci_fill_Dfifo count(%u/%d)",
			cs->tx_skb->len, count);
	if (count < cs->tx_skb->len) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfcpci_fill_Dfifo no fifo mem");
		return;
	}
	count = cs->tx_skb->len;	/* get frame len */
	new_z1 = (df->za[df->f1 & D_FREG_MASK].z1 + count) & (D_FIFO_SIZE - 1);
	new_f1 = ((df->f1 + 1) & D_FREG_MASK) | (D_FREG_MASK + 1);
	src = cs->tx_skb->data;	/* source pointer */
	dst = df->data + df->za[df->f1 & D_FREG_MASK].z1;
	maxlen = D_FIFO_SIZE - df->za[df->f1 & D_FREG_MASK].z1;		/* end fifo */
	if (maxlen > count)
		maxlen = count;	/* limit size */
	memcpy(dst, src, maxlen);	/* first copy */

	count -= maxlen;	/* remaining bytes */
	if (count) {
		dst = df->data;	/* start of buffer */
		src += maxlen;	/* new position */
		memcpy(dst, src, count);
	}
	df->za[new_f1 & D_FREG_MASK].z1 = new_z1;	/* for next buffer */
	df->za[df->f1 & D_FREG_MASK].z1 = new_z1;	/* new pos actual buffer */
	df->f1 = new_f1;	/* next frame */

	dev_kfree_skb_any(cs->tx_skb);
	cs->tx_skb = NULL;
}

/**************************/
/* B-channel send routine */
/**************************/
static void
hfcpci_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int maxlen, fcnt;
	int count, new_z1;
	bzfifo_type *bz;
	u_char *bdata;
	u_char new_f1, *src, *dst;
	unsigned short *z1t, *z2t;

	if (!bcs->tx_skb)
		return;
	if (bcs->tx_skb->len <= 0)
		return;

	if ((bcs->channel) && (!cs->hw.hfcpci.bswapped)) {
		bz = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.txbz_b2;
		bdata = ((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.txdat_b2;
	} else {
		bz = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.txbz_b1;
		bdata = ((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.txdat_b1;
	}

	if (bcs->mode == L1_MODE_TRANS) {
		z1t = &bz->za[MAX_B_FRAMES].z1;
		z2t = z1t + 1;
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfcpci_fill_fifo_trans %d z1(%x) z2(%x)",
				bcs->channel, *z1t, *z2t);
		fcnt = *z2t - *z1t;
		if (fcnt <= 0)
			fcnt += B_FIFO_SIZE;	/* fcnt contains available bytes in fifo */
		fcnt = B_FIFO_SIZE - fcnt;	/* remaining bytes to send */

		while ((fcnt < 2 * HFCPCI_BTRANS_THRESHOLD) && (bcs->tx_skb)) {
			if (bcs->tx_skb->len < B_FIFO_SIZE - fcnt) {
				/* data is suitable for fifo */
				count = bcs->tx_skb->len;

				new_z1 = *z1t + count;	/* new buffer Position */
				if (new_z1 >= (B_FIFO_SIZE + B_SUB_VAL))
					new_z1 -= B_FIFO_SIZE;	/* buffer wrap */
				src = bcs->tx_skb->data;	/* source pointer */
				dst = bdata + (*z1t - B_SUB_VAL);
				maxlen = (B_FIFO_SIZE + B_SUB_VAL) - *z1t;	/* end of fifo */
				if (maxlen > count)
					maxlen = count;		/* limit size */
				memcpy(dst, src, maxlen);	/* first copy */

				count -= maxlen;	/* remaining bytes */
				if (count) {
					dst = bdata;	/* start of buffer */
					src += maxlen;	/* new position */
					memcpy(dst, src, count);
				}
				bcs->tx_cnt -= bcs->tx_skb->len;
				fcnt += bcs->tx_skb->len;
				*z1t = new_z1;	/* now send data */
			} else if (cs->debug & L1_DEB_HSCX)
				debugl1(cs, "hfcpci_fill_fifo_trans %d frame length %d discarded",
					bcs->channel, bcs->tx_skb->len);

			if (test_bit(FLG_LLI_L1WAKEUP, &bcs->st->lli.flag) &&
			    (PACKET_NOACK != bcs->tx_skb->pkt_type)) {
				u_long	flags;
				spin_lock_irqsave(&bcs->aclock, flags);
				bcs->ackcnt += bcs->tx_skb->len;
				spin_unlock_irqrestore(&bcs->aclock, flags);
				schedule_event(bcs, B_ACKPENDING);
			}

			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = skb_dequeue(&bcs->squeue);	/* fetch next data */
		}
		test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		return;
	}
	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "hfcpci_fill_fifo_hdlc %d f1(%d) f2(%d) z1(f1)(%x)",
			bcs->channel, bz->f1, bz->f2,
			bz->za[bz->f1].z1);

	fcnt = bz->f1 - bz->f2;	/* frame count actually buffered */
	if (fcnt < 0)
		fcnt += (MAX_B_FRAMES + 1);	/* if wrap around */
	if (fcnt > (MAX_B_FRAMES - 1)) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfcpci_fill_Bfifo more as 14 frames");
		return;
	}
	/* now determine free bytes in FIFO buffer */
	count = bz->za[bz->f2].z2 - bz->za[bz->f1].z1 - 1;
	if (count <= 0)
		count += B_FIFO_SIZE;	/* count now contains available bytes */

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "hfcpci_fill_fifo %d count(%u/%d),%lx",
			bcs->channel, bcs->tx_skb->len,
			count, current->state);

	if (count < bcs->tx_skb->len) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfcpci_fill_fifo no fifo mem");
		return;
	}
	count = bcs->tx_skb->len;	/* get frame len */
	new_z1 = bz->za[bz->f1].z1 + count;	/* new buffer Position */
	if (new_z1 >= (B_FIFO_SIZE + B_SUB_VAL))
		new_z1 -= B_FIFO_SIZE;	/* buffer wrap */

	new_f1 = ((bz->f1 + 1) & MAX_B_FRAMES);
	src = bcs->tx_skb->data;	/* source pointer */
	dst = bdata + (bz->za[bz->f1].z1 - B_SUB_VAL);
	maxlen = (B_FIFO_SIZE + B_SUB_VAL) - bz->za[bz->f1].z1;		/* end fifo */
	if (maxlen > count)
		maxlen = count;	/* limit size */
	memcpy(dst, src, maxlen);	/* first copy */

	count -= maxlen;	/* remaining bytes */
	if (count) {
		dst = bdata;	/* start of buffer */
		src += maxlen;	/* new position */
		memcpy(dst, src, count);
	}
	bcs->tx_cnt -= bcs->tx_skb->len;
	if (test_bit(FLG_LLI_L1WAKEUP, &bcs->st->lli.flag) &&
	    (PACKET_NOACK != bcs->tx_skb->pkt_type)) {
		u_long	flags;
		spin_lock_irqsave(&bcs->aclock, flags);
		bcs->ackcnt += bcs->tx_skb->len;
		spin_unlock_irqrestore(&bcs->aclock, flags);
		schedule_event(bcs, B_ACKPENDING);
	}

	bz->za[new_f1].z1 = new_z1;	/* for next buffer */
	bz->f1 = new_f1;	/* next frame */

	dev_kfree_skb_any(bcs->tx_skb);
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
}

/**********************************************/
/* D-channel l1 state call for leased NT-mode */
/**********************************************/
static void
dch_nt_l2l1(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;

	switch (pr) {
	case (PH_DATA | REQUEST):
	case (PH_PULL | REQUEST):
	case (PH_PULL | INDICATION):
		st->l1.l1hw(st, pr, arg);
		break;
	case (PH_ACTIVATE | REQUEST):
		st->l1.l1l2(st, PH_ACTIVATE | CONFIRM, NULL);
		break;
	case (PH_TESTLOOP | REQUEST):
		if (1 & (long) arg)
			debugl1(cs, "PH_TEST_LOOP B1");
		if (2 & (long) arg)
			debugl1(cs, "PH_TEST_LOOP B2");
		if (!(3 & (long) arg))
			debugl1(cs, "PH_TEST_LOOP DISABLED");
		st->l1.l1hw(st, HW_TESTLOOP | REQUEST, arg);
		break;
	default:
		if (cs->debug)
			debugl1(cs, "dch_nt_l2l1 msg %04X unhandled", pr);
		break;
	}
}



/***********************/
/* set/reset echo mode */
/***********************/
static int
hfcpci_auxcmd(struct IsdnCardState *cs, isdn_ctrl *ic)
{
	u_long	flags;
	int	i = *(unsigned int *) ic->parm.num;

	if ((ic->arg == 98) &&
	    (!(cs->hw.hfcpci.int_m1 & (HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC + HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC)))) {
		spin_lock_irqsave(&cs->lock, flags);
		Write_hfc(cs, HFCPCI_CLKDEL, CLKDEL_NT); /* ST-Bit delay for NT-Mode */
		Write_hfc(cs, HFCPCI_STATES, HFCPCI_LOAD_STATE | 0);	/* HFC ST G0 */
		udelay(10);
		cs->hw.hfcpci.sctrl |= SCTRL_MODE_NT;
		Write_hfc(cs, HFCPCI_SCTRL, cs->hw.hfcpci.sctrl);	/* set NT-mode */
		udelay(10);
		Write_hfc(cs, HFCPCI_STATES, HFCPCI_LOAD_STATE | 1);	/* HFC ST G1 */
		udelay(10);
		Write_hfc(cs, HFCPCI_STATES, 1 | HFCPCI_ACTIVATE | HFCPCI_DO_ACTION);
		cs->dc.hfcpci.ph_state = 1;
		cs->hw.hfcpci.nt_mode = 1;
		cs->hw.hfcpci.nt_timer = 0;
		cs->stlist->l2.l2l1 = dch_nt_l2l1;
		spin_unlock_irqrestore(&cs->lock, flags);
		debugl1(cs, "NT mode activated");
		return (0);
	}
	if ((cs->chanlimit > 1) || (cs->hw.hfcpci.bswapped) ||
	    (cs->hw.hfcpci.nt_mode) || (ic->arg != 12))
		return (-EINVAL);

	spin_lock_irqsave(&cs->lock, flags);
	if (i) {
		cs->logecho = 1;
		cs->hw.hfcpci.trm |= 0x20;	/* enable echo chan */
		cs->hw.hfcpci.int_m1 |= HFCPCI_INTS_B2REC;
		cs->hw.hfcpci.fifo_en |= HFCPCI_FIFOEN_B2RX;
	} else {
		cs->logecho = 0;
		cs->hw.hfcpci.trm &= ~0x20;	/* disable echo chan */
		cs->hw.hfcpci.int_m1 &= ~HFCPCI_INTS_B2REC;
		cs->hw.hfcpci.fifo_en &= ~HFCPCI_FIFOEN_B2RX;
	}
	cs->hw.hfcpci.sctrl_r &= ~SCTRL_B2_ENA;
	cs->hw.hfcpci.sctrl &= ~SCTRL_B2_ENA;
	cs->hw.hfcpci.conn |= 0x10;	/* B2-IOM -> B2-ST */
	cs->hw.hfcpci.ctmt &= ~2;
	Write_hfc(cs, HFCPCI_CTMT, cs->hw.hfcpci.ctmt);
	Write_hfc(cs, HFCPCI_SCTRL_R, cs->hw.hfcpci.sctrl_r);
	Write_hfc(cs, HFCPCI_SCTRL, cs->hw.hfcpci.sctrl);
	Write_hfc(cs, HFCPCI_CONNECT, cs->hw.hfcpci.conn);
	Write_hfc(cs, HFCPCI_TRM, cs->hw.hfcpci.trm);
	Write_hfc(cs, HFCPCI_FIFO_EN, cs->hw.hfcpci.fifo_en);
	Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);
	spin_unlock_irqrestore(&cs->lock, flags);
	return (0);
}				/* hfcpci_auxcmd */

/*****************************/
/* E-channel receive routine */
/*****************************/
static void
receive_emsg(struct IsdnCardState *cs)
{
	int rcnt;
	int receive, count = 5;
	bzfifo_type *bz;
	u_char *bdata;
	z_type *zp;
	u_char *ptr, *ptr1, new_f2;
	int total, maxlen, new_z2;
	u_char e_buffer[256];

	bz = &((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxbz_b2;
	bdata = ((fifo_area *) (cs->hw.hfcpci.fifos))->b_chans.rxdat_b2;
Begin:
	count--;
	if (test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		debugl1(cs, "echo_rec_data blocked");
		return;
	}
	if (bz->f1 != bz->f2) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfcpci e_rec f1(%d) f2(%d)",
				bz->f1, bz->f2);
		zp = &bz->za[bz->f2];

		rcnt = zp->z1 - zp->z2;
		if (rcnt < 0)
			rcnt += B_FIFO_SIZE;
		rcnt++;
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfcpci e_rec z1(%x) z2(%x) cnt(%d)",
				zp->z1, zp->z2, rcnt);
		new_z2 = zp->z2 + rcnt;		/* new position in fifo */
		if (new_z2 >= (B_FIFO_SIZE + B_SUB_VAL))
			new_z2 -= B_FIFO_SIZE;	/* buffer wrap */
		new_f2 = (bz->f2 + 1) & MAX_B_FRAMES;
		if ((rcnt > 256 + 3) || (count < 4) ||
		    (*(bdata + (zp->z1 - B_SUB_VAL)))) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "hfcpci_empty_echan: incoming packet invalid length %d or crc", rcnt);
			bz->za[new_f2].z2 = new_z2;
			bz->f2 = new_f2;	/* next buffer */
		} else {
			total = rcnt;
			rcnt -= 3;
			ptr = e_buffer;

			if (zp->z2 <= B_FIFO_SIZE + B_SUB_VAL)
				maxlen = rcnt;	/* complete transfer */
			else
				maxlen = B_FIFO_SIZE + B_SUB_VAL - zp->z2;	/* maximum */

			ptr1 = bdata + (zp->z2 - B_SUB_VAL);	/* start of data */
			memcpy(ptr, ptr1, maxlen);	/* copy data */
			rcnt -= maxlen;

			if (rcnt) {	/* rest remaining */
				ptr += maxlen;
				ptr1 = bdata;	/* start of buffer */
				memcpy(ptr, ptr1, rcnt);	/* rest */
			}
			bz->za[new_f2].z2 = new_z2;
			bz->f2 = new_f2;	/* next buffer */
			if (cs->debug & DEB_DLOG_HEX) {
				ptr = cs->dlog;
				if ((total - 3) < MAX_DLOG_SPACE / 3 - 10) {
					*ptr++ = 'E';
					*ptr++ = 'C';
					*ptr++ = 'H';
					*ptr++ = 'O';
					*ptr++ = ':';
					ptr += QuickHex(ptr, e_buffer, total - 3);
					ptr--;
					*ptr++ = '\n';
					*ptr = 0;
					HiSax_putstatus(cs, NULL, cs->dlog);
				} else
					HiSax_putstatus(cs, "LogEcho: ", "warning Frame too big (%d)", total - 3);
			}
		}

		rcnt = bz->f1 - bz->f2;
		if (rcnt < 0)
			rcnt += MAX_B_FRAMES + 1;
		if (rcnt > 1)
			receive = 1;
		else
			receive = 0;
	} else
		receive = 0;
	test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	if (count && receive)
		goto Begin;
}				/* receive_emsg */

/*********************/
/* Interrupt handler */
/*********************/
static irqreturn_t
hfcpci_interrupt(int intno, void *dev_id)
{
	u_long flags;
	struct IsdnCardState *cs = dev_id;
	u_char exval;
	struct BCState *bcs;
	int count = 15;
	u_char val, stat;

	if (!(cs->hw.hfcpci.int_m2 & 0x08)) {
		debugl1(cs, "HFC-PCI: int_m2 %x not initialised", cs->hw.hfcpci.int_m2);
		return IRQ_NONE;	/* not initialised */
	}
	spin_lock_irqsave(&cs->lock, flags);
	if (HFCPCI_ANYINT & (stat = Read_hfc(cs, HFCPCI_STATUS))) {
		val = Read_hfc(cs, HFCPCI_INT_S1);
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "HFC-PCI: stat(%02x) s1(%02x)", stat, val);
	} else {
		spin_unlock_irqrestore(&cs->lock, flags);
		return IRQ_NONE;
	}
	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "HFC-PCI irq %x %s", val,
			test_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags) ?
			"locked" : "unlocked");
	val &= cs->hw.hfcpci.int_m1;
	if (val & 0x40) {	/* state machine irq */
		exval = Read_hfc(cs, HFCPCI_STATES) & 0xf;
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ph_state chg %d->%d", cs->dc.hfcpci.ph_state,
				exval);
		cs->dc.hfcpci.ph_state = exval;
		sched_event_D_pci(cs, D_L1STATECHANGE);
		val &= ~0x40;
	}
	if (val & 0x80) {	/* timer irq */
		if (cs->hw.hfcpci.nt_mode) {
			if ((--cs->hw.hfcpci.nt_timer) < 0)
				sched_event_D_pci(cs, D_L1STATECHANGE);
		}
		val &= ~0x80;
		Write_hfc(cs, HFCPCI_CTMT, cs->hw.hfcpci.ctmt | HFCPCI_CLTIMER);
	}
	while (val) {
		if (test_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
			cs->hw.hfcpci.int_s1 |= val;
			spin_unlock_irqrestore(&cs->lock, flags);
			return IRQ_HANDLED;
		}
		if (cs->hw.hfcpci.int_s1 & 0x18) {
			exval = val;
			val = cs->hw.hfcpci.int_s1;
			cs->hw.hfcpci.int_s1 = exval;
		}
		if (val & 0x08) {
			if (!(bcs = Sel_BCS(cs, cs->hw.hfcpci.bswapped ? 1 : 0))) {
				if (cs->debug)
					debugl1(cs, "hfcpci spurious 0x08 IRQ");
			} else
				main_rec_hfcpci(bcs);
		}
		if (val & 0x10) {
			if (cs->logecho)
				receive_emsg(cs);
			else if (!(bcs = Sel_BCS(cs, 1))) {
				if (cs->debug)
					debugl1(cs, "hfcpci spurious 0x10 IRQ");
			} else
				main_rec_hfcpci(bcs);
		}
		if (val & 0x01) {
			if (!(bcs = Sel_BCS(cs, cs->hw.hfcpci.bswapped ? 1 : 0))) {
				if (cs->debug)
					debugl1(cs, "hfcpci spurious 0x01 IRQ");
			} else {
				if (bcs->tx_skb) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
						hfcpci_fill_fifo(bcs);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
					} else
						debugl1(cs, "fill_data %d blocked", bcs->channel);
				} else {
					if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
						if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
							hfcpci_fill_fifo(bcs);
							test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
						} else
							debugl1(cs, "fill_data %d blocked", bcs->channel);
					} else {
						hfcpci_sched_event(bcs, B_XMTBUFREADY);
					}
				}
			}
		}
		if (val & 0x02) {
			if (!(bcs = Sel_BCS(cs, 1))) {
				if (cs->debug)
					debugl1(cs, "hfcpci spurious 0x02 IRQ");
			} else {
				if (bcs->tx_skb) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
						hfcpci_fill_fifo(bcs);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
					} else
						debugl1(cs, "fill_data %d blocked", bcs->channel);
				} else {
					if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
						if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
							hfcpci_fill_fifo(bcs);
							test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
						} else
							debugl1(cs, "fill_data %d blocked", bcs->channel);
					} else {
						hfcpci_sched_event(bcs, B_XMTBUFREADY);
					}
				}
			}
		}
		if (val & 0x20) {	/* receive dframe */
			receive_dmsg(cs);
		}
		if (val & 0x04) {	/* dframe transmitted */
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
				del_timer(&cs->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
				sched_event_D_pci(cs, D_CLEARBUSY);
			if (cs->tx_skb) {
				if (cs->tx_skb->len) {
					if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
						hfcpci_fill_dfifo(cs);
						test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
					} else {
						debugl1(cs, "hfcpci_fill_dfifo irq blocked");
					}
					goto afterXPR;
				} else {
					dev_kfree_skb_irq(cs->tx_skb);
					cs->tx_cnt = 0;
					cs->tx_skb = NULL;
				}
			}
			if ((cs->tx_skb = skb_dequeue(&cs->sq))) {
				cs->tx_cnt = 0;
				if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
					hfcpci_fill_dfifo(cs);
					test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
				} else {
					debugl1(cs, "hfcpci_fill_dfifo irq blocked");
				}
			} else
				sched_event_D_pci(cs, D_XMTBUFREADY);
		}
	afterXPR:
		if (cs->hw.hfcpci.int_s1 && count--) {
			val = cs->hw.hfcpci.int_s1;
			cs->hw.hfcpci.int_s1 = 0;
			if (cs->debug & L1_DEB_ISAC)
				debugl1(cs, "HFC-PCI irq %x loop %d", val, 15 - count);
		} else
			val = 0;
	}
	spin_unlock_irqrestore(&cs->lock, flags);
	return IRQ_HANDLED;
}

/********************************************************************/
/* timer callback for D-chan busy resolution. Currently no function */
/********************************************************************/
static void
hfcpci_dbusy_timer(struct timer_list *t)
{
}

/*************************************/
/* Layer 1 D-channel hardware access */
/*************************************/
static void
HFCPCI_l1hw(struct PStack *st, int pr, void *arg)
{
	u_long flags;
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;

	switch (pr) {
	case (PH_DATA | REQUEST):
		if (cs->debug & DEB_DLOG_HEX)
			LogFrame(cs, skb->data, skb->len);
		if (cs->debug & DEB_DLOG_VERBOSE)
			dlogframe(cs, skb, 0);
		spin_lock_irqsave(&cs->lock, flags);
		if (cs->tx_skb) {
			skb_queue_tail(&cs->sq, skb);
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				Logl2Frame(cs, skb, "PH_DATA Queued", 0);
#endif
		} else {
			cs->tx_skb = skb;
			cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				Logl2Frame(cs, skb, "PH_DATA", 0);
#endif
			if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
				hfcpci_fill_dfifo(cs);
				test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
			} else
				debugl1(cs, "hfcpci_fill_dfifo blocked");

		}
		spin_unlock_irqrestore(&cs->lock, flags);
		break;
	case (PH_PULL | INDICATION):
		spin_lock_irqsave(&cs->lock, flags);
		if (cs->tx_skb) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, " l2l1 tx_skb exist this shouldn't happen");
			skb_queue_tail(&cs->sq, skb);
			spin_unlock_irqrestore(&cs->lock, flags);
			break;
		}
		if (cs->debug & DEB_DLOG_HEX)
			LogFrame(cs, skb->data, skb->len);
		if (cs->debug & DEB_DLOG_VERBOSE)
			dlogframe(cs, skb, 0);
		cs->tx_skb = skb;
		cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
		if (cs->debug & L1_DEB_LAPD)
			Logl2Frame(cs, skb, "PH_DATA_PULLED", 0);
#endif
		if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
			hfcpci_fill_dfifo(cs);
			test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
		} else
			debugl1(cs, "hfcpci_fill_dfifo blocked");
		spin_unlock_irqrestore(&cs->lock, flags);
		break;
	case (PH_PULL | REQUEST):
#ifdef L2FRAME_DEBUG		/* psa */
		if (cs->debug & L1_DEB_LAPD)
			debugl1(cs, "-> PH_REQUEST_PULL");
#endif
		spin_lock_irqsave(&cs->lock, flags);
		if (!cs->tx_skb) {
			test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
		} else
			test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
		spin_unlock_irqrestore(&cs->lock, flags);
		break;
	case (HW_RESET | REQUEST):
		spin_lock_irqsave(&cs->lock, flags);
		Write_hfc(cs, HFCPCI_STATES, HFCPCI_LOAD_STATE | 3);	/* HFC ST 3 */
		udelay(6);
		Write_hfc(cs, HFCPCI_STATES, 3);	/* HFC ST 2 */
		cs->hw.hfcpci.mst_m |= HFCPCI_MASTER;
		Write_hfc(cs, HFCPCI_MST_MODE, cs->hw.hfcpci.mst_m);
		Write_hfc(cs, HFCPCI_STATES, HFCPCI_ACTIVATE | HFCPCI_DO_ACTION);
		spin_unlock_irqrestore(&cs->lock, flags);
		l1_msg(cs, HW_POWERUP | CONFIRM, NULL);
		break;
	case (HW_ENABLE | REQUEST):
		spin_lock_irqsave(&cs->lock, flags);
		Write_hfc(cs, HFCPCI_STATES, HFCPCI_DO_ACTION);
		spin_unlock_irqrestore(&cs->lock, flags);
		break;
	case (HW_DEACTIVATE | REQUEST):
		spin_lock_irqsave(&cs->lock, flags);
		cs->hw.hfcpci.mst_m &= ~HFCPCI_MASTER;
		Write_hfc(cs, HFCPCI_MST_MODE, cs->hw.hfcpci.mst_m);
		spin_unlock_irqrestore(&cs->lock, flags);
		break;
	case (HW_INFO3 | REQUEST):
		spin_lock_irqsave(&cs->lock, flags);
		cs->hw.hfcpci.mst_m |= HFCPCI_MASTER;
		Write_hfc(cs, HFCPCI_MST_MODE, cs->hw.hfcpci.mst_m);
		spin_unlock_irqrestore(&cs->lock, flags);
		break;
	case (HW_TESTLOOP | REQUEST):
		spin_lock_irqsave(&cs->lock, flags);
		switch ((long) arg) {
		case (1):
			Write_hfc(cs, HFCPCI_B1_SSL, 0x80);	/* tx slot */
			Write_hfc(cs, HFCPCI_B1_RSL, 0x80);	/* rx slot */
			cs->hw.hfcpci.conn = (cs->hw.hfcpci.conn & ~7) | 1;
			Write_hfc(cs, HFCPCI_CONNECT, cs->hw.hfcpci.conn);
			break;

		case (2):
			Write_hfc(cs, HFCPCI_B2_SSL, 0x81);	/* tx slot */
			Write_hfc(cs, HFCPCI_B2_RSL, 0x81);	/* rx slot */
			cs->hw.hfcpci.conn = (cs->hw.hfcpci.conn & ~0x38) | 0x08;
			Write_hfc(cs, HFCPCI_CONNECT, cs->hw.hfcpci.conn);
			break;

		default:
			spin_unlock_irqrestore(&cs->lock, flags);
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "hfcpci_l1hw loop invalid %4lx", (long) arg);
			return;
		}
		cs->hw.hfcpci.trm |= 0x80;	/* enable IOM-loop */
		Write_hfc(cs, HFCPCI_TRM, cs->hw.hfcpci.trm);
		spin_unlock_irqrestore(&cs->lock, flags);
		break;
	default:
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hfcpci_l1hw unknown pr %4x", pr);
		break;
	}
}

/***********************************************/
/* called during init setting l1 stack pointer */
/***********************************************/
static void
setstack_hfcpci(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.l1hw = HFCPCI_l1hw;
}

/**************************************/
/* send B-channel data if not blocked */
/**************************************/
static void
hfcpci_send_data(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;

	if (!test_and_set_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags)) {
		hfcpci_fill_fifo(bcs);
		test_and_clear_bit(FLG_LOCK_ATOMIC, &cs->HW_Flags);
	} else
		debugl1(cs, "send_data %d blocked", bcs->channel);
}

/***************************************************************/
/* activate/deactivate hardware for selected channels and mode */
/***************************************************************/
static void
mode_hfcpci(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
	int fifo2;

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "HFCPCI bchannel mode %d bchan %d/%d",
			mode, bc, bcs->channel);
	bcs->mode = mode;
	bcs->channel = bc;
	fifo2 = bc;
	if (cs->chanlimit > 1) {
		cs->hw.hfcpci.bswapped = 0;	/* B1 and B2 normal mode */
		cs->hw.hfcpci.sctrl_e &= ~0x80;
	} else {
		if (bc) {
			if (mode != L1_MODE_NULL) {
				cs->hw.hfcpci.bswapped = 1;	/* B1 and B2 exchanged */
				cs->hw.hfcpci.sctrl_e |= 0x80;
			} else {
				cs->hw.hfcpci.bswapped = 0;	/* B1 and B2 normal mode */
				cs->hw.hfcpci.sctrl_e &= ~0x80;
			}
			fifo2 = 0;
		} else {
			cs->hw.hfcpci.bswapped = 0;	/* B1 and B2 normal mode */
			cs->hw.hfcpci.sctrl_e &= ~0x80;
		}
	}
	switch (mode) {
	case (L1_MODE_NULL):
		if (bc) {
			cs->hw.hfcpci.sctrl &= ~SCTRL_B2_ENA;
			cs->hw.hfcpci.sctrl_r &= ~SCTRL_B2_ENA;
		} else {
			cs->hw.hfcpci.sctrl &= ~SCTRL_B1_ENA;
			cs->hw.hfcpci.sctrl_r &= ~SCTRL_B1_ENA;
		}
		if (fifo2) {
			cs->hw.hfcpci.fifo_en &= ~HFCPCI_FIFOEN_B2;
			cs->hw.hfcpci.int_m1 &= ~(HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC);
		} else {
			cs->hw.hfcpci.fifo_en &= ~HFCPCI_FIFOEN_B1;
			cs->hw.hfcpci.int_m1 &= ~(HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC);
		}
		break;
	case (L1_MODE_TRANS):
		hfcpci_clear_fifo_rx(cs, fifo2);
		hfcpci_clear_fifo_tx(cs, fifo2);
		if (bc) {
			cs->hw.hfcpci.sctrl |= SCTRL_B2_ENA;
			cs->hw.hfcpci.sctrl_r |= SCTRL_B2_ENA;
		} else {
			cs->hw.hfcpci.sctrl |= SCTRL_B1_ENA;
			cs->hw.hfcpci.sctrl_r |= SCTRL_B1_ENA;
		}
		if (fifo2) {
			cs->hw.hfcpci.fifo_en |= HFCPCI_FIFOEN_B2;
			cs->hw.hfcpci.int_m1 |= (HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC);
			cs->hw.hfcpci.ctmt |= 2;
			cs->hw.hfcpci.conn &= ~0x18;
		} else {
			cs->hw.hfcpci.fifo_en |= HFCPCI_FIFOEN_B1;
			cs->hw.hfcpci.int_m1 |= (HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC);
			cs->hw.hfcpci.ctmt |= 1;
			cs->hw.hfcpci.conn &= ~0x03;
		}
		break;
	case (L1_MODE_HDLC):
		hfcpci_clear_fifo_rx(cs, fifo2);
		hfcpci_clear_fifo_tx(cs, fifo2);
		if (bc) {
			cs->hw.hfcpci.sctrl |= SCTRL_B2_ENA;
			cs->hw.hfcpci.sctrl_r |= SCTRL_B2_ENA;
		} else {
			cs->hw.hfcpci.sctrl |= SCTRL_B1_ENA;
			cs->hw.hfcpci.sctrl_r |= SCTRL_B1_ENA;
		}
		if (fifo2) {
			cs->hw.hfcpci.last_bfifo_cnt[1] = 0;
			cs->hw.hfcpci.fifo_en |= HFCPCI_FIFOEN_B2;
			cs->hw.hfcpci.int_m1 |= (HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC);
			cs->hw.hfcpci.ctmt &= ~2;
			cs->hw.hfcpci.conn &= ~0x18;
		} else {
			cs->hw.hfcpci.last_bfifo_cnt[0] = 0;
			cs->hw.hfcpci.fifo_en |= HFCPCI_FIFOEN_B1;
			cs->hw.hfcpci.int_m1 |= (HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC);
			cs->hw.hfcpci.ctmt &= ~1;
			cs->hw.hfcpci.conn &= ~0x03;
		}
		break;
	case (L1_MODE_EXTRN):
		if (bc) {
			cs->hw.hfcpci.conn |= 0x10;
			cs->hw.hfcpci.sctrl |= SCTRL_B2_ENA;
			cs->hw.hfcpci.sctrl_r |= SCTRL_B2_ENA;
			cs->hw.hfcpci.fifo_en &= ~HFCPCI_FIFOEN_B2;
			cs->hw.hfcpci.int_m1 &= ~(HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC);
		} else {
			cs->hw.hfcpci.conn |= 0x02;
			cs->hw.hfcpci.sctrl |= SCTRL_B1_ENA;
			cs->hw.hfcpci.sctrl_r |= SCTRL_B1_ENA;
			cs->hw.hfcpci.fifo_en &= ~HFCPCI_FIFOEN_B1;
			cs->hw.hfcpci.int_m1 &= ~(HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC);
		}
		break;
	}
	Write_hfc(cs, HFCPCI_SCTRL_E, cs->hw.hfcpci.sctrl_e);
	Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);
	Write_hfc(cs, HFCPCI_FIFO_EN, cs->hw.hfcpci.fifo_en);
	Write_hfc(cs, HFCPCI_SCTRL, cs->hw.hfcpci.sctrl);
	Write_hfc(cs, HFCPCI_SCTRL_R, cs->hw.hfcpci.sctrl_r);
	Write_hfc(cs, HFCPCI_CTMT, cs->hw.hfcpci.ctmt);
	Write_hfc(cs, HFCPCI_CONNECT, cs->hw.hfcpci.conn);
}

/******************************/
/* Layer2 -> Layer 1 Transfer */
/******************************/
static void
hfcpci_l2l1(struct PStack *st, int pr, void *arg)
{
	struct BCState	*bcs = st->l1.bcs;
	u_long		flags;
	struct sk_buff	*skb = arg;

	switch (pr) {
	case (PH_DATA | REQUEST):
		spin_lock_irqsave(&bcs->cs->lock, flags);
		if (bcs->tx_skb) {
			skb_queue_tail(&bcs->squeue, skb);
		} else {
			bcs->tx_skb = skb;
//				test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
			bcs->cs->BC_Send_Data(bcs);
		}
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		break;
	case (PH_PULL | INDICATION):
		spin_lock_irqsave(&bcs->cs->lock, flags);
		if (bcs->tx_skb) {
			spin_unlock_irqrestore(&bcs->cs->lock, flags);
			printk(KERN_WARNING "hfc_l2l1: this shouldn't happen\n");
			break;
		}
//			test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
		bcs->tx_skb = skb;
		bcs->cs->BC_Send_Data(bcs);
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		break;
	case (PH_PULL | REQUEST):
		if (!bcs->tx_skb) {
			test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
		} else
			test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
		break;
	case (PH_ACTIVATE | REQUEST):
		spin_lock_irqsave(&bcs->cs->lock, flags);
		test_and_set_bit(BC_FLG_ACTIV, &bcs->Flag);
		mode_hfcpci(bcs, st->l1.mode, st->l1.bc);
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		l1_msg_b(st, pr, arg);
		break;
	case (PH_DEACTIVATE | REQUEST):
		l1_msg_b(st, pr, arg);
		break;
	case (PH_DEACTIVATE | CONFIRM):
		spin_lock_irqsave(&bcs->cs->lock, flags);
		test_and_clear_bit(BC_FLG_ACTIV, &bcs->Flag);
		test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		mode_hfcpci(bcs, 0, st->l1.bc);
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
		break;
	}
}

/******************************************/
/* deactivate B-channel access and queues */
/******************************************/
static void
close_hfcpci(struct BCState *bcs)
{
	mode_hfcpci(bcs, 0, bcs->channel);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		skb_queue_purge(&bcs->rqueue);
		skb_queue_purge(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

/*************************************/
/* init B-channel queues and control */
/*************************************/
static int
open_hfcpcistate(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->tx_cnt = 0;
	return (0);
}

/*********************************/
/* inits the stack for B-channel */
/*********************************/
static int
setstack_2b(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_hfcpcistate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = hfcpci_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

/***************************/
/* handle L1 state changes */
/***************************/
static void
hfcpci_bh(struct work_struct *work)
{
	struct IsdnCardState *cs =
		container_of(work, struct IsdnCardState, tqueue);
	u_long	flags;
//      struct PStack *stptr;

	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event)) {
		if (!cs->hw.hfcpci.nt_mode)
			switch (cs->dc.hfcpci.ph_state) {
			case (0):
				l1_msg(cs, HW_RESET | INDICATION, NULL);
				break;
			case (3):
				l1_msg(cs, HW_DEACTIVATE | INDICATION, NULL);
				break;
			case (8):
				l1_msg(cs, HW_RSYNC | INDICATION, NULL);
				break;
			case (6):
				l1_msg(cs, HW_INFO2 | INDICATION, NULL);
				break;
			case (7):
				l1_msg(cs, HW_INFO4_P8 | INDICATION, NULL);
				break;
			default:
				break;
			} else {
			spin_lock_irqsave(&cs->lock, flags);
			switch (cs->dc.hfcpci.ph_state) {
			case (2):
				if (cs->hw.hfcpci.nt_timer < 0) {
					cs->hw.hfcpci.nt_timer = 0;
					cs->hw.hfcpci.int_m1 &= ~HFCPCI_INTS_TIMER;
					Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);
					/* Clear already pending ints */
					if (Read_hfc(cs, HFCPCI_INT_S1));
					Write_hfc(cs, HFCPCI_STATES, 4 | HFCPCI_LOAD_STATE);
					udelay(10);
					Write_hfc(cs, HFCPCI_STATES, 4);
					cs->dc.hfcpci.ph_state = 4;
				} else {
					cs->hw.hfcpci.int_m1 |= HFCPCI_INTS_TIMER;
					Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);
					cs->hw.hfcpci.ctmt &= ~HFCPCI_AUTO_TIMER;
					cs->hw.hfcpci.ctmt |= HFCPCI_TIM3_125;
					Write_hfc(cs, HFCPCI_CTMT, cs->hw.hfcpci.ctmt | HFCPCI_CLTIMER);
					Write_hfc(cs, HFCPCI_CTMT, cs->hw.hfcpci.ctmt | HFCPCI_CLTIMER);
					cs->hw.hfcpci.nt_timer = NT_T1_COUNT;
					Write_hfc(cs, HFCPCI_STATES, 2 | HFCPCI_NT_G2_G3);	/* allow G2 -> G3 transition */
				}
				break;
			case (1):
			case (3):
			case (4):
				cs->hw.hfcpci.nt_timer = 0;
				cs->hw.hfcpci.int_m1 &= ~HFCPCI_INTS_TIMER;
				Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);
				break;
			default:
				break;
			}
			spin_unlock_irqrestore(&cs->lock, flags);
		}
	}
	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event))
		DChannel_proc_rcv(cs);
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event))
		DChannel_proc_xmt(cs);
}


/********************************/
/* called for card init message */
/********************************/
static void
inithfcpci(struct IsdnCardState *cs)
{
	cs->bcs[0].BC_SetStack = setstack_2b;
	cs->bcs[1].BC_SetStack = setstack_2b;
	cs->bcs[0].BC_Close = close_hfcpci;
	cs->bcs[1].BC_Close = close_hfcpci;
	timer_setup(&cs->dbusytimer, hfcpci_dbusy_timer, 0);
	mode_hfcpci(cs->bcs, 0, 0);
	mode_hfcpci(cs->bcs + 1, 0, 1);
}



/*******************************************/
/* handle card messages from control layer */
/*******************************************/
static int
hfcpci_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	u_long flags;

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "HFCPCI: card_msg %x", mt);
	switch (mt) {
	case CARD_RESET:
		spin_lock_irqsave(&cs->lock, flags);
		reset_hfcpci(cs);
		spin_unlock_irqrestore(&cs->lock, flags);
		return (0);
	case CARD_RELEASE:
		release_io_hfcpci(cs);
		return (0);
	case CARD_INIT:
		spin_lock_irqsave(&cs->lock, flags);
		inithfcpci(cs);
		reset_hfcpci(cs);
		spin_unlock_irqrestore(&cs->lock, flags);
		msleep(80);				/* Timeout 80ms */
		/* now switch timer interrupt off */
		spin_lock_irqsave(&cs->lock, flags);
		cs->hw.hfcpci.int_m1 &= ~HFCPCI_INTS_TIMER;
		Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);
		/* reinit mode reg */
		Write_hfc(cs, HFCPCI_MST_MODE, cs->hw.hfcpci.mst_m);
		spin_unlock_irqrestore(&cs->lock, flags);
		return (0);
	case CARD_TEST:
		return (0);
	}
	return (0);
}


/* this variable is used as card index when more than one cards are present */
static struct pci_dev *dev_hfcpci = NULL;

int
setup_hfcpci(struct IsdnCard *card)
{
	u_long flags;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
	int i;
	struct pci_dev *tmp_hfcpci = NULL;

	strcpy(tmp, hfcpci_revision);
	printk(KERN_INFO "HiSax: HFC-PCI driver Rev. %s\n", HiSax_getrev(tmp));

	cs->hw.hfcpci.int_s1 = 0;
	cs->dc.hfcpci.ph_state = 0;
	cs->hw.hfcpci.fifo = 255;
	if (cs->typ != ISDN_CTYPE_HFC_PCI)
		return (0);

	i = 0;
	while (id_list[i].vendor_id) {
		tmp_hfcpci = hisax_find_pci_device(id_list[i].vendor_id,
						   id_list[i].device_id,
						   dev_hfcpci);
		i++;
		if (tmp_hfcpci) {
			dma_addr_t	dma_mask = DMA_BIT_MASK(32) & ~0x7fffUL;
			if (pci_enable_device(tmp_hfcpci))
				continue;
			if (pci_set_dma_mask(tmp_hfcpci, dma_mask)) {
				printk(KERN_WARNING
				       "HiSax hfc_pci: No suitable DMA available.\n");
				continue;
			}
			if (pci_set_consistent_dma_mask(tmp_hfcpci, dma_mask)) {
				printk(KERN_WARNING
				       "HiSax hfc_pci: No suitable consistent DMA available.\n");
				continue;
			}
			pci_set_master(tmp_hfcpci);
			if ((card->para[0]) && (card->para[0] != (tmp_hfcpci->resource[0].start & PCI_BASE_ADDRESS_IO_MASK)))
				continue;
			else
				break;
		}
	}

	if (!tmp_hfcpci) {
		printk(KERN_WARNING "HFC-PCI: No PCI card found\n");
		return (0);
	}

	i--;
	dev_hfcpci = tmp_hfcpci;	/* old device */
	cs->hw.hfcpci.dev = dev_hfcpci;
	cs->irq = dev_hfcpci->irq;
	if (!cs->irq) {
		printk(KERN_WARNING "HFC-PCI: No IRQ for PCI card found\n");
		return (0);
	}
	cs->hw.hfcpci.pci_io = (char *)(unsigned long)dev_hfcpci->resource[1].start;
	printk(KERN_INFO "HiSax: HFC-PCI card manufacturer: %s card name: %s\n", id_list[i].vendor_name, id_list[i].card_name);

	if (!cs->hw.hfcpci.pci_io) {
		printk(KERN_WARNING "HFC-PCI: No IO-Mem for PCI card found\n");
		return (0);
	}

	/* Allocate memory for FIFOS */
	cs->hw.hfcpci.fifos = pci_alloc_consistent(cs->hw.hfcpci.dev,
						   0x8000, &cs->hw.hfcpci.dma);
	if (!cs->hw.hfcpci.fifos) {
		printk(KERN_WARNING "HFC-PCI: Error allocating FIFO memory!\n");
		return 0;
	}
	if (cs->hw.hfcpci.dma & 0x7fff) {
		printk(KERN_WARNING
		       "HFC-PCI: Error DMA memory not on 32K boundary (%lx)\n",
		       (u_long)cs->hw.hfcpci.dma);
		pci_free_consistent(cs->hw.hfcpci.dev, 0x8000,
				    cs->hw.hfcpci.fifos, cs->hw.hfcpci.dma);
		return 0;
	}
	pci_write_config_dword(cs->hw.hfcpci.dev, 0x80, (u32)cs->hw.hfcpci.dma);
	cs->hw.hfcpci.pci_io = ioremap((ulong) cs->hw.hfcpci.pci_io, 256);
	printk(KERN_INFO
	       "HFC-PCI: defined at mem %p fifo %p(%lx) IRQ %d HZ %d\n",
	       cs->hw.hfcpci.pci_io,
	       cs->hw.hfcpci.fifos,
	       (u_long)cs->hw.hfcpci.dma,
	       cs->irq, HZ);

	spin_lock_irqsave(&cs->lock, flags);

	pci_write_config_word(cs->hw.hfcpci.dev, PCI_COMMAND, PCI_ENA_MEMIO);	/* enable memory mapped ports, disable busmaster */
	cs->hw.hfcpci.int_m2 = 0;	/* disable alle interrupts */
	cs->hw.hfcpci.int_m1 = 0;
	Write_hfc(cs, HFCPCI_INT_M1, cs->hw.hfcpci.int_m1);
	Write_hfc(cs, HFCPCI_INT_M2, cs->hw.hfcpci.int_m2);
	/* At this point the needed PCI config is done */
	/* fifos are still not enabled */

	INIT_WORK(&cs->tqueue,  hfcpci_bh);
	cs->setstack_d = setstack_hfcpci;
	cs->BC_Send_Data = &hfcpci_send_data;
	cs->readisac = NULL;
	cs->writeisac = NULL;
	cs->readisacfifo = NULL;
	cs->writeisacfifo = NULL;
	cs->BC_Read_Reg = NULL;
	cs->BC_Write_Reg = NULL;
	cs->irq_func = &hfcpci_interrupt;
	cs->irq_flags |= IRQF_SHARED;
	timer_setup(&cs->hw.hfcpci.timer, hfcpci_Timer, 0);
	cs->cardmsg = &hfcpci_card_msg;
	cs->auxcmd = &hfcpci_auxcmd;

	spin_unlock_irqrestore(&cs->lock, flags);

	return (1);
}
