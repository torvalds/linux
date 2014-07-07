/*
 *  arch/m68k/mvme16x/config.c
 *
 *  Copyright (C) 1995 Richard Hirst [richard@sleepie.demon.co.uk]
 *
 * Based on:
 *
 *  linux/amiga/config.c
 *
 *  Copyright (C) 1993 Hamish Macdonald
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/genhd.h>
#include <linux/rtc.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/bootinfo.h>
#include <asm/bootinfo-vme.h>
#include <asm/byteorder.h>
#include <asm/pgtable.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/rtc.h>
#include <asm/machdep.h>
#include <asm/mvme16xhw.h>

extern t_bdid mvme_bdid;

static MK48T08ptr_t volatile rtc = (MK48T08ptr_t)MVME_RTC_BASE;

static void mvme16x_get_model(char *model);
extern void mvme16x_sched_init(irq_handler_t handler);
extern u32 mvme16x_gettimeoffset(void);
extern int mvme16x_hwclk (int, struct rtc_time *);
extern int mvme16x_set_clock_mmss (unsigned long);
extern void mvme16x_reset (void);

int bcd2int (unsigned char b);

/* Save tick handler routine pointer, will point to xtime_update() in
 * kernel/time/timekeeping.c, called via mvme16x_process_int() */

static irq_handler_t tick_handler;


unsigned short mvme16x_config;
EXPORT_SYMBOL(mvme16x_config);


int __init mvme16x_parse_bootinfo(const struct bi_record *bi)
{
	uint16_t tag = be16_to_cpu(bi->tag);
	if (tag == BI_VME_TYPE || tag == BI_VME_BRDINFO)
		return 0;
	else
		return 1;
}

void mvme16x_reset(void)
{
	printk ("\r\n\nCalled mvme16x_reset\r\n"
			"\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r\r");
	/* The string of returns is to delay the reset until the whole
	 * message is output.  Assert reset bit in GCSR */
	*(volatile char *)0xfff40107 = 0x80;
}

static void mvme16x_get_model(char *model)
{
    p_bdid p = &mvme_bdid;
    char suf[4];

    suf[1] = p->brdsuffix[0];
    suf[2] = p->brdsuffix[1];
    suf[3] = '\0';
    suf[0] = suf[1] ? '-' : '\0';

    sprintf(model, "Motorola MVME%x%s", be16_to_cpu(p->brdno), suf);
}


static void mvme16x_get_hardware_list(struct seq_file *m)
{
    uint16_t brdno = be16_to_cpu(mvme_bdid.brdno);

    if (brdno == 0x0162 || brdno == 0x0172)
    {
	unsigned char rev = *(unsigned char *)MVME162_VERSION_REG;

	seq_printf (m, "VMEchip2        %spresent\n",
			rev & MVME16x_CONFIG_NO_VMECHIP2 ? "NOT " : "");
	seq_printf (m, "SCSI interface  %spresent\n",
			rev & MVME16x_CONFIG_NO_SCSICHIP ? "NOT " : "");
	seq_printf (m, "Ethernet i/f    %spresent\n",
			rev & MVME16x_CONFIG_NO_ETHERNET ? "NOT " : "");
    }
}

/*
 * This function is called during kernel startup to initialize
 * the mvme16x IRQ handling routines.  Should probably ensure
 * that the base vectors for the VMEChip2 and PCCChip2 are valid.
 */

static void __init mvme16x_init_IRQ (void)
{
	m68k_setup_user_interrupt(VEC_USER, 192);
}

#define pcc2chip	((volatile u_char *)0xfff42000)
#define PccSCCMICR	0x1d
#define PccSCCTICR	0x1e
#define PccSCCRICR	0x1f
#define PccTPIACKR	0x25

#ifdef CONFIG_EARLY_PRINTK

/**** cd2401 registers ****/
#define CD2401_ADDR	(0xfff45000)

#define CyGFRCR         (0x81)
#define CyCCR		(0x13)
#define      CyCLR_CHAN		(0x40)
#define      CyINIT_CHAN	(0x20)
#define      CyCHIP_RESET	(0x10)
#define      CyENB_XMTR		(0x08)
#define      CyDIS_XMTR		(0x04)
#define      CyENB_RCVR		(0x02)
#define      CyDIS_RCVR		(0x01)
#define CyCAR		(0xee)
#define CyIER		(0x11)
#define      CyMdmCh		(0x80)
#define      CyRxExc		(0x20)
#define      CyRxData		(0x08)
#define      CyTxMpty		(0x02)
#define      CyTxRdy		(0x01)
#define CyLICR		(0x26)
#define CyRISR		(0x89)
#define      CyTIMEOUT		(0x80)
#define      CySPECHAR		(0x70)
#define      CyOVERRUN		(0x08)
#define      CyPARITY		(0x04)
#define      CyFRAME		(0x02)
#define      CyBREAK		(0x01)
#define CyREOIR		(0x84)
#define CyTEOIR		(0x85)
#define CyMEOIR		(0x86)
#define      CyNOTRANS		(0x08)
#define CyRFOC		(0x30)
#define CyRDR		(0xf8)
#define CyTDR		(0xf8)
#define CyMISR		(0x8b)
#define CyRISR		(0x89)
#define CyTISR		(0x8a)
#define CyMSVR1		(0xde)
#define CyMSVR2		(0xdf)
#define      CyDSR		(0x80)
#define      CyDCD		(0x40)
#define      CyCTS		(0x20)
#define      CyDTR		(0x02)
#define      CyRTS		(0x01)
#define CyRTPRL		(0x25)
#define CyRTPRH		(0x24)
#define CyCOR1		(0x10)
#define      CyPARITY_NONE	(0x00)
#define      CyPARITY_E		(0x40)
#define      CyPARITY_O		(0xC0)
#define      Cy_5_BITS		(0x04)
#define      Cy_6_BITS		(0x05)
#define      Cy_7_BITS		(0x06)
#define      Cy_8_BITS		(0x07)
#define CyCOR2		(0x17)
#define      CyETC		(0x20)
#define      CyCtsAE		(0x02)
#define CyCOR3		(0x16)
#define      Cy_1_STOP		(0x02)
#define      Cy_2_STOP		(0x04)
#define CyCOR4		(0x15)
#define      CyREC_FIFO		(0x0F)  /* Receive FIFO threshold */
#define CyCOR5		(0x14)
#define CyCOR6		(0x18)
#define CyCOR7		(0x07)
#define CyRBPR		(0xcb)
#define CyRCOR		(0xc8)
#define CyTBPR		(0xc3)
#define CyTCOR		(0xc0)
#define CySCHR1		(0x1f)
#define CySCHR2 	(0x1e)
#define CyTPR		(0xda)
#define CyPILR1		(0xe3)
#define CyPILR2		(0xe0)
#define CyPILR3		(0xe1)
#define CyCMR		(0x1b)
#define      CyASYNC		(0x02)
#define CyLICR          (0x26)
#define CyLIVR          (0x09)
#define CySCRL		(0x23)
#define CySCRH		(0x22)
#define CyTFTC		(0x80)

void mvme16x_cons_write(struct console *co, const char *str, unsigned count)
{
	volatile unsigned char *base_addr = (u_char *)CD2401_ADDR;
	volatile u_char sink;
	u_char ier;
	int port;
	u_char do_lf = 0;
	int i = 0;

	/* Ensure transmitter is enabled! */

	port = 0;
	base_addr[CyCAR] = (u_char)port;
	while (base_addr[CyCCR])
		;
	base_addr[CyCCR] = CyENB_XMTR;

	ier = base_addr[CyIER];
	base_addr[CyIER] = CyTxMpty;

	while (1) {
		if (pcc2chip[PccSCCTICR] & 0x20)
		{
			/* We have a Tx int. Acknowledge it */
			sink = pcc2chip[PccTPIACKR];
			if ((base_addr[CyLICR] >> 2) == port) {
				if (i == count) {
					/* Last char of string is now output */
					base_addr[CyTEOIR] = CyNOTRANS;
					break;
				}
				if (do_lf) {
					base_addr[CyTDR] = '\n';
					str++;
					i++;
					do_lf = 0;
				}
				else if (*str == '\n') {
					base_addr[CyTDR] = '\r';
					do_lf = 1;
				}
				else {
					base_addr[CyTDR] = *str++;
					i++;
				}
				base_addr[CyTEOIR] = 0;
			}
			else
				base_addr[CyTEOIR] = CyNOTRANS;
		}
	}

	base_addr[CyIER] = ier;
}

#endif

void __init config_mvme16x(void)
{
    p_bdid p = &mvme_bdid;
    char id[40];
    uint16_t brdno = be16_to_cpu(p->brdno);

    mach_max_dma_address = 0xffffffff;
    mach_sched_init      = mvme16x_sched_init;
    mach_init_IRQ        = mvme16x_init_IRQ;
    arch_gettimeoffset   = mvme16x_gettimeoffset;
    mach_hwclk           = mvme16x_hwclk;
    mach_set_clock_mmss	 = mvme16x_set_clock_mmss;
    mach_reset		 = mvme16x_reset;
    mach_get_model       = mvme16x_get_model;
    mach_get_hardware_list = mvme16x_get_hardware_list;

    /* Report board revision */

    if (strncmp("BDID", p->bdid, 4))
    {
	printk ("\n\nBug call .BRD_ID returned garbage - giving up\n\n");
	while (1)
		;
    }
    /* Board type is only set by newer versions of vmelilo/tftplilo */
    if (vme_brdtype == 0)
	vme_brdtype = brdno;

    mvme16x_get_model(id);
    printk ("\nBRD_ID: %s   BUG %x.%x %02x/%02x/%02x\n", id, p->rev>>4,
					p->rev&0xf, p->yr, p->mth, p->day);
    if (brdno == 0x0162 || brdno == 0x172)
    {
	unsigned char rev = *(unsigned char *)MVME162_VERSION_REG;

	mvme16x_config = rev | MVME16x_CONFIG_GOT_SCCA;

	printk ("MVME%x Hardware status:\n", brdno);
	printk ("    CPU Type           68%s040\n",
			rev & MVME16x_CONFIG_GOT_FPU ? "" : "LC");
	printk ("    CPU clock          %dMHz\n",
			rev & MVME16x_CONFIG_SPEED_32 ? 32 : 25);
	printk ("    VMEchip2           %spresent\n",
			rev & MVME16x_CONFIG_NO_VMECHIP2 ? "NOT " : "");
	printk ("    SCSI interface     %spresent\n",
			rev & MVME16x_CONFIG_NO_SCSICHIP ? "NOT " : "");
	printk ("    Ethernet interface %spresent\n",
			rev & MVME16x_CONFIG_NO_ETHERNET ? "NOT " : "");
    }
    else
    {
	mvme16x_config = MVME16x_CONFIG_GOT_LP | MVME16x_CONFIG_GOT_CD2401;
    }
}

static irqreturn_t mvme16x_abort_int (int irq, void *dev_id)
{
	unsigned long *new = (unsigned long *)vectors;
	unsigned long *old = (unsigned long *)0xffe00000;
	volatile unsigned char uc, *ucp;
	uint16_t brdno = be16_to_cpu(mvme_bdid.brdno);

	if (brdno == 0x0162 || brdno == 0x172)
	{
		ucp = (volatile unsigned char *)0xfff42043;
		uc = *ucp | 8;
		*ucp = uc;
	}
	else
	{
		*(volatile unsigned long *)0xfff40074 = 0x40000000;
	}
	*(new+4) = *(old+4);		/* Illegal instruction */
	*(new+9) = *(old+9);		/* Trace */
	*(new+47) = *(old+47);		/* Trap #15 */

	if (brdno == 0x0162 || brdno == 0x172)
		*(new+0x5e) = *(old+0x5e);	/* ABORT switch */
	else
		*(new+0x6e) = *(old+0x6e);	/* ABORT switch */
	return IRQ_HANDLED;
}

static irqreturn_t mvme16x_timer_int (int irq, void *dev_id)
{
    *(volatile unsigned char *)0xfff4201b |= 8;
    return tick_handler(irq, dev_id);
}

void mvme16x_sched_init (irq_handler_t timer_routine)
{
    uint16_t brdno = be16_to_cpu(mvme_bdid.brdno);
    int irq;

    tick_handler = timer_routine;
    /* Using PCCchip2 or MC2 chip tick timer 1 */
    *(volatile unsigned long *)0xfff42008 = 0;
    *(volatile unsigned long *)0xfff42004 = 10000;	/* 10ms */
    *(volatile unsigned char *)0xfff42017 |= 3;
    *(volatile unsigned char *)0xfff4201b = 0x16;
    if (request_irq(MVME16x_IRQ_TIMER, mvme16x_timer_int, 0,
				"timer", mvme16x_timer_int))
	panic ("Couldn't register timer int");

    if (brdno == 0x0162 || brdno == 0x172)
	irq = MVME162_IRQ_ABORT;
    else
        irq = MVME167_IRQ_ABORT;
    if (request_irq(irq, mvme16x_abort_int, 0,
				"abort", mvme16x_abort_int))
	panic ("Couldn't register abort int");
}


/* This is always executed with interrupts disabled.  */
u32 mvme16x_gettimeoffset(void)
{
    return (*(volatile u32 *)0xfff42008) * 1000;
}

int bcd2int (unsigned char b)
{
	return ((b>>4)*10 + (b&15));
}

int mvme16x_hwclk(int op, struct rtc_time *t)
{
#warning check me!
	if (!op) {
		rtc->ctrl = RTC_READ;
		t->tm_year = bcd2int (rtc->bcd_year);
		t->tm_mon  = bcd2int (rtc->bcd_mth);
		t->tm_mday = bcd2int (rtc->bcd_dom);
		t->tm_hour = bcd2int (rtc->bcd_hr);
		t->tm_min  = bcd2int (rtc->bcd_min);
		t->tm_sec  = bcd2int (rtc->bcd_sec);
		rtc->ctrl = 0;
	}
	return 0;
}

int mvme16x_set_clock_mmss (unsigned long nowtime)
{
	return 0;
}

