/* $Id: config.c,v 2.84.2.5 2004/02/11 13:21:33 keil Exp $
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 *              by Kai Germaschewski <kai.germaschewski@gmx.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For changes and modifications please read
 * Documentation/isdn/HiSax.cert
 *
 * based on the teles driver from Jan den Ouden
 *
 */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include <linux/init.h>
#include "hisax.h"
#include <linux/module.h>
#include <linux/kernel_stat.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#define HISAX_STATUS_BUFSIZE 4096

/*
 * This structure array contains one entry per card. An entry looks
 * like this:
 *
 * { type, protocol, p0, p1, p2, NULL }
 *
 * type
 *    1 Teles 16.0       p0=irq p1=membase p2=iobase
 *    2 Teles  8.0       p0=irq p1=membase
 *    3 Teles 16.3       p0=irq p1=iobase
 *    4 Creatix PNP      p0=irq p1=IO0 (ISAC)  p2=IO1 (HSCX)
 *    5 AVM A1 (Fritz)   p0=irq p1=iobase
 *    6 ELSA PC          [p0=iobase] or nothing (autodetect)
 *    7 ELSA Quickstep   p0=irq p1=iobase
 *    8 Teles PCMCIA     p0=irq p1=iobase
 *    9 ITK ix1-micro    p0=irq p1=iobase
 *   10 ELSA PCMCIA      p0=irq p1=iobase
 *   11 Eicon.Diehl Diva p0=irq p1=iobase
 *   12 Asuscom ISDNLink p0=irq p1=iobase
 *   13 Teleint          p0=irq p1=iobase
 *   14 Teles 16.3c      p0=irq p1=iobase
 *   15 Sedlbauer speed  p0=irq p1=iobase
 *   15 Sedlbauer PC/104	p0=irq p1=iobase
 *   15 Sedlbauer speed pci	no parameter
 *   16 USR Sportster internal  p0=irq  p1=iobase
 *   17 MIC card                p0=irq  p1=iobase
 *   18 ELSA Quickstep 1000PCI  no parameter
 *   19 Compaq ISDN S0 ISA card p0=irq  p1=IO0 (HSCX)  p2=IO1 (ISAC) p3=IO2
 *   20 Travers Technologies NETjet-S PCI card
 *   21 TELES PCI               no parameter
 *   22 Sedlbauer Speed Star    p0=irq p1=iobase
 *   23 reserved
 *   24 Dr Neuhaus Niccy PnP/PCI card p0=irq p1=IO0 p2=IO1 (PnP only)
 *   25 Teles S0Box             p0=irq p1=iobase (from isapnp setup)
 *   26 AVM A1 PCMCIA (Fritz)   p0=irq p1=iobase
 *   27 AVM PnP/PCI 		p0=irq p1=iobase (PCI no parameter)
 *   28 Sedlbauer Speed Fax+ 	p0=irq p1=iobase (from isapnp setup)
 *   29 Siemens I-Surf          p0=irq p1=iobase p2=memory (from isapnp setup)
 *   30 ACER P10                p0=irq p1=iobase (from isapnp setup)
 *   31 HST Saphir              p0=irq  p1=iobase
 *   32 Telekom A4T             none
 *   33 Scitel Quadro		p0=subcontroller (4*S0, subctrl 1...4)
 *   34	Gazel ISDN cards
 *   35 HFC 2BDS0 PCI           none
 *   36 Winbond 6692 PCI        none
 *   37 HFC 2BDS0 S+/SP         p0=irq p1=iobase
 *   38 Travers Technologies NETspider-U PCI card
 *   39 HFC 2BDS0-SP PCMCIA     p0=irq p1=iobase
 *   40 hotplug interface
 *   41 Formula-n enter:now ISDN PCI a/b   none
 *
 * protocol can be either ISDN_PTYPE_EURO or ISDN_PTYPE_1TR6 or ISDN_PTYPE_NI1
 *
 *
 */

const char *CardType[] = {
	"No Card", "Teles 16.0", "Teles 8.0", "Teles 16.3",
	"Creatix/Teles PnP", "AVM A1", "Elsa ML", "Elsa Quickstep",
	"Teles PCMCIA",	"ITK ix1-micro Rev.2", "Elsa PCMCIA",
	"Eicon.Diehl Diva", "ISDNLink",	"TeleInt", "Teles 16.3c",
	"Sedlbauer Speed Card", "USR Sportster", "ith mic Linux",
	"Elsa PCI", "Compaq ISA", "NETjet-S", "Teles PCI", 
	"Sedlbauer Speed Star (PCMCIA)", "AMD 7930", "NICCY", "S0Box",
	"AVM A1 (PCMCIA)", "AVM Fritz PnP/PCI", "Sedlbauer Speed Fax +",
	"Siemens I-Surf", "Acer P10", "HST Saphir", "Telekom A4T",
	"Scitel Quadro", "Gazel", "HFC 2BDS0 PCI", "Winbond 6692",
	"HFC 2BDS0 SX", "NETspider-U", "HFC-2BDS0-SP PCMCIA",
	"Hotplug", "Formula-n enter:now PCI a/b", 
};

#ifdef CONFIG_HISAX_ELSA
#define DEFAULT_CARD ISDN_CTYPE_ELSA
#define DEFAULT_CFG {0,0,0,0}
#endif

#ifdef CONFIG_HISAX_AVM_A1
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_A1
#define DEFAULT_CFG {10,0x340,0,0}
#endif

#ifdef CONFIG_HISAX_AVM_A1_PCMCIA
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_A1_PCMCIA
#define DEFAULT_CFG {11,0x170,0,0}
#endif

#ifdef CONFIG_HISAX_FRITZPCI
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_FRITZPCI
#define DEFAULT_CFG {0,0,0,0}
#endif

#ifdef CONFIG_HISAX_16_3
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_16_3
#define DEFAULT_CFG {15,0x180,0,0}
#endif

#ifdef CONFIG_HISAX_S0BOX
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_S0BOX
#define DEFAULT_CFG {7,0x378,0,0}
#endif

#ifdef CONFIG_HISAX_16_0
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_16_0
#define DEFAULT_CFG {15,0xd0000,0xd80,0}
#endif

#ifdef CONFIG_HISAX_TELESPCI
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_TELESPCI
#define DEFAULT_CFG {0,0,0,0}
#endif

#ifdef CONFIG_HISAX_IX1MICROR2
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_IX1MICROR2
#define DEFAULT_CFG {5,0x390,0,0}
#endif

#ifdef CONFIG_HISAX_DIEHLDIVA
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_DIEHLDIVA
#define DEFAULT_CFG {0,0x0,0,0}
#endif

#ifdef CONFIG_HISAX_ASUSCOM
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_ASUSCOM
#define DEFAULT_CFG {5,0x200,0,0}
#endif

#ifdef CONFIG_HISAX_TELEINT
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_TELEINT
#define DEFAULT_CFG {5,0x300,0,0}
#endif

#ifdef CONFIG_HISAX_SEDLBAUER
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_SEDLBAUER
#define DEFAULT_CFG {11,0x270,0,0}
#endif

#ifdef CONFIG_HISAX_SPORTSTER
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_SPORTSTER
#define DEFAULT_CFG {7,0x268,0,0}
#endif

#ifdef CONFIG_HISAX_MIC
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_MIC
#define DEFAULT_CFG {12,0x3e0,0,0}
#endif

#ifdef CONFIG_HISAX_NETJET
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_NETJET_S
#define DEFAULT_CFG {0,0,0,0}
#endif

#ifdef CONFIG_HISAX_HFCS
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_TELES3C
#define DEFAULT_CFG {5,0x500,0,0}
#endif

#ifdef CONFIG_HISAX_HFC_PCI
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_HFC_PCI
#define DEFAULT_CFG {0,0,0,0}
#endif

#ifdef CONFIG_HISAX_HFC_SX
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_HFC_SX
#define DEFAULT_CFG {5,0x2E0,0,0}
#endif

#ifdef CONFIG_HISAX_NICCY
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_NICCY
#define DEFAULT_CFG {0,0x0,0,0}
#endif

#ifdef CONFIG_HISAX_ISURF
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_ISURF
#define DEFAULT_CFG {5,0x100,0xc8000,0}
#endif

#ifdef CONFIG_HISAX_HSTSAPHIR
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_HSTSAPHIR
#define DEFAULT_CFG {5,0x250,0,0}
#endif

#ifdef CONFIG_HISAX_BKM_A4T
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_BKM_A4T
#define DEFAULT_CFG {0,0x0,0,0}
#endif

#ifdef CONFIG_HISAX_SCT_QUADRO
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_SCT_QUADRO
#define DEFAULT_CFG {1,0x0,0,0}
#endif

#ifdef CONFIG_HISAX_GAZEL
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_GAZEL
#define DEFAULT_CFG {15,0x180,0,0}
#endif

#ifdef CONFIG_HISAX_W6692
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_W6692
#define DEFAULT_CFG {0,0,0,0}
#endif

#ifdef CONFIG_HISAX_NETJET_U
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_NETJET_U
#define DEFAULT_CFG {0,0,0,0}
#endif

#ifdef CONFIG_HISAX_1TR6
#define DEFAULT_PROTO ISDN_PTYPE_1TR6
#define DEFAULT_PROTO_NAME "1TR6"
#endif
#ifdef CONFIG_HISAX_NI1
#undef DEFAULT_PROTO
#define DEFAULT_PROTO ISDN_PTYPE_NI1
#undef DEFAULT_PROTO_NAME
#define DEFAULT_PROTO_NAME "NI1"
#endif
#ifdef CONFIG_HISAX_EURO
#undef DEFAULT_PROTO
#define DEFAULT_PROTO ISDN_PTYPE_EURO
#undef DEFAULT_PROTO_NAME
#define DEFAULT_PROTO_NAME "EURO"
#endif
#ifndef DEFAULT_PROTO
#define DEFAULT_PROTO ISDN_PTYPE_UNKNOWN
#define DEFAULT_PROTO_NAME "UNKNOWN"
#endif
#ifndef DEFAULT_CARD
#define DEFAULT_CARD 0
#define DEFAULT_CFG {0,0,0,0}
#endif

#define FIRST_CARD { \
	DEFAULT_CARD, \
	DEFAULT_PROTO, \
	DEFAULT_CFG, \
	NULL, \
}

struct IsdnCard cards[HISAX_MAX_CARDS] = {
	FIRST_CARD,
};

#define HISAX_IDSIZE (HISAX_MAX_CARDS*8)
static char HiSaxID[HISAX_IDSIZE] = { 0, };

static char *HiSax_id = HiSaxID;
#ifdef MODULE
/* Variables for insmod */
static int type[HISAX_MAX_CARDS] = { 0, };
static int protocol[HISAX_MAX_CARDS] = { 0, };
static int io[HISAX_MAX_CARDS] = { 0, };
#undef IO0_IO1
#ifdef CONFIG_HISAX_16_3
#define IO0_IO1
#endif
#ifdef CONFIG_HISAX_NICCY
#undef IO0_IO1
#define IO0_IO1
#endif
#ifdef IO0_IO1
static int io0[HISAX_MAX_CARDS] __devinitdata = { 0, };
static int io1[HISAX_MAX_CARDS] __devinitdata = { 0, };
#endif
static int irq[HISAX_MAX_CARDS] __devinitdata = { 0, };
static int mem[HISAX_MAX_CARDS] __devinitdata = { 0, };
static char *id = HiSaxID;

MODULE_DESCRIPTION("ISDN4Linux: Driver for passive ISDN cards");
MODULE_AUTHOR("Karsten Keil");
MODULE_LICENSE("GPL");
module_param_array(type, int, NULL, 0);
module_param_array(protocol, int, NULL, 0);
module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param_array(mem, int, NULL, 0);
module_param(id, charp, 0);
#ifdef IO0_IO1
module_param_array(io0, int, NULL, 0);
module_param_array(io1, int, NULL, 0);
#endif
#endif /* MODULE */

int nrcards;

extern char *l1_revision;
extern char *l2_revision;
extern char *l3_revision;
extern char *lli_revision;
extern char *tei_revision;

char *HiSax_getrev(const char *revision)
{
	char *rev;
	char *p;

	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "???";
	return rev;
}

static void __init HiSaxVersion(void)
{
	char tmp[64];

	printk(KERN_INFO "HiSax: Linux Driver for passive ISDN cards\n");
#ifdef MODULE
	printk(KERN_INFO "HiSax: Version 3.5 (module)\n");
#else
	printk(KERN_INFO "HiSax: Version 3.5 (kernel)\n");
#endif
	strcpy(tmp, l1_revision);
	printk(KERN_INFO "HiSax: Layer1 Revision %s\n", HiSax_getrev(tmp));
	strcpy(tmp, l2_revision);
	printk(KERN_INFO "HiSax: Layer2 Revision %s\n", HiSax_getrev(tmp));
	strcpy(tmp, tei_revision);
	printk(KERN_INFO "HiSax: TeiMgr Revision %s\n", HiSax_getrev(tmp));
	strcpy(tmp, l3_revision);
	printk(KERN_INFO "HiSax: Layer3 Revision %s\n", HiSax_getrev(tmp));
	strcpy(tmp, lli_revision);
	printk(KERN_INFO "HiSax: LinkLayer Revision %s\n",
	       HiSax_getrev(tmp));
}

#ifndef MODULE
#define MAX_ARG	(HISAX_MAX_CARDS*5)
static int __init HiSax_setup(char *line)
{
	int i, j, argc;
	int ints[MAX_ARG + 1];
	char *str;

	str = get_options(line, MAX_ARG, ints);
	argc = ints[0];
	printk(KERN_DEBUG "HiSax_setup: argc(%d) str(%s)\n", argc, str);
	i = 0;
	j = 1;
	while (argc && (i < HISAX_MAX_CARDS)) {
		cards[i].protocol = DEFAULT_PROTO;
		if (argc) {
			cards[i].typ = ints[j];
			j++;
			argc--;
		}
		if (argc) {
			cards[i].protocol = ints[j];
			j++;
			argc--;
		}
		if (argc) {
			cards[i].para[0] = ints[j];
			j++;
			argc--;
		}
		if (argc) {
			cards[i].para[1] = ints[j];
			j++;
			argc--;
		}
		if (argc) {
			cards[i].para[2] = ints[j];
			j++;
			argc--;
		}
		i++;
	}
  	if (str && *str) {
		if (strlen(str) < HISAX_IDSIZE)
			strcpy(HiSaxID, str);
		else
			printk(KERN_WARNING "HiSax: ID too long!");
	} else
		strcpy(HiSaxID, "HiSax");

	HiSax_id = HiSaxID;
	return 1;
}

__setup("hisax=", HiSax_setup);
#endif /* MODULES */

#if CARD_TELES0
extern int setup_teles0(struct IsdnCard *card);
#endif

#if CARD_TELES3
extern int setup_teles3(struct IsdnCard *card);
#endif

#if CARD_S0BOX
extern int setup_s0box(struct IsdnCard *card);
#endif

#if CARD_TELESPCI
extern int setup_telespci(struct IsdnCard *card);
#endif

#if CARD_AVM_A1
extern int setup_avm_a1(struct IsdnCard *card);
#endif

#if CARD_AVM_A1_PCMCIA
extern int setup_avm_a1_pcmcia(struct IsdnCard *card);
#endif

#if CARD_FRITZPCI
extern int setup_avm_pcipnp(struct IsdnCard *card);
#endif

#if CARD_ELSA
extern int setup_elsa(struct IsdnCard *card);
#endif

#if CARD_IX1MICROR2
extern int setup_ix1micro(struct IsdnCard *card);
#endif

#if CARD_DIEHLDIVA
extern int setup_diva(struct IsdnCard *card);
#endif

#if CARD_ASUSCOM
extern int setup_asuscom(struct IsdnCard *card);
#endif

#if CARD_TELEINT
extern int setup_TeleInt(struct IsdnCard *card);
#endif

#if CARD_SEDLBAUER
extern int setup_sedlbauer(struct IsdnCard *card);
#endif

#if CARD_SPORTSTER
extern int setup_sportster(struct IsdnCard *card);
#endif

#if CARD_MIC
extern int setup_mic(struct IsdnCard *card);
#endif

#if CARD_NETJET_S
extern int setup_netjet_s(struct IsdnCard *card);
#endif

#if CARD_HFCS
extern int setup_hfcs(struct IsdnCard *card);
#endif

#if CARD_HFC_PCI
extern int setup_hfcpci(struct IsdnCard *card);
#endif

#if CARD_HFC_SX
extern int setup_hfcsx(struct IsdnCard *card);
#endif

#if CARD_NICCY
extern int setup_niccy(struct IsdnCard *card);
#endif

#if CARD_ISURF
extern int setup_isurf(struct IsdnCard *card);
#endif

#if CARD_HSTSAPHIR
extern int setup_saphir(struct IsdnCard *card);
#endif

#if CARD_TESTEMU
extern int setup_testemu(struct IsdnCard *card);
#endif

#if CARD_BKM_A4T
extern int setup_bkm_a4t(struct IsdnCard *card);
#endif

#if CARD_SCT_QUADRO
extern int setup_sct_quadro(struct IsdnCard *card);
#endif

#if CARD_GAZEL
extern int setup_gazel(struct IsdnCard *card);
#endif

#if CARD_W6692
extern int setup_w6692(struct IsdnCard *card);
#endif

#if CARD_NETJET_U
extern int setup_netjet_u(struct IsdnCard *card);
#endif

#if CARD_FN_ENTERNOW_PCI
extern int setup_enternow_pci(struct IsdnCard *card);
#endif

/*
 * Find card with given driverId
 */
static inline struct IsdnCardState *hisax_findcard(int driverid)
{
	int i;

	for (i = 0; i < nrcards; i++)
		if (cards[i].cs)
			if (cards[i].cs->myid == driverid)
				return cards[i].cs;
	return NULL;
}

/*
 * Find card with given card number
 */
#if 0
struct IsdnCardState *hisax_get_card(int cardnr)
{
	if ((cardnr <= nrcards) && (cardnr > 0))
		if (cards[cardnr - 1].cs)
			return cards[cardnr - 1].cs;
	return NULL;
}
#endif  /*  0  */

static int HiSax_readstatus(u_char __user *buf, int len, int id, int channel)
{
	int count, cnt;
	u_char __user *p = buf;
	struct IsdnCardState *cs = hisax_findcard(id);

	if (cs) {
		if (len > HISAX_STATUS_BUFSIZE) {
			printk(KERN_WARNING
			       "HiSax: status overflow readstat %d/%d\n",
			       len, HISAX_STATUS_BUFSIZE);
		}
		count = cs->status_end - cs->status_read + 1;
		if (count >= len)
			count = len;
		if (copy_to_user(p, cs->status_read, count))
			return -EFAULT;
		cs->status_read += count;
		if (cs->status_read > cs->status_end)
			cs->status_read = cs->status_buf;
		p += count;
		count = len - count;
		while (count) {
			if (count > HISAX_STATUS_BUFSIZE)
				cnt = HISAX_STATUS_BUFSIZE;
			else
				cnt = count;
			if (copy_to_user(p, cs->status_read, cnt))
				return -EFAULT;
			p += cnt;
			cs->status_read += cnt % HISAX_STATUS_BUFSIZE;
			count -= cnt;
		}
		return len;
	} else {
		printk(KERN_ERR
		       "HiSax: if_readstatus called with invalid driverId!\n");
		return -ENODEV;
	}
}

int jiftime(char *s, long mark)
{
	s += 8;

	*s-- = '\0';
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = '.';
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = mark % 6 + '0';
	mark /= 6;
	*s-- = ':';
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = mark % 10 + '0';
	return 8;
}

static u_char tmpbuf[HISAX_STATUS_BUFSIZE];

void VHiSax_putstatus(struct IsdnCardState *cs, char *head, char *fmt,
		      va_list args)
{
	/* if head == NULL the fmt contains the full info */

	u_long		flags;
	int		count, i;
	u_char		*p;
	isdn_ctrl	ic;
	int		len;

	if (!cs) {
		printk(KERN_WARNING "HiSax: No CardStatus for message");
		return;
	}
	spin_lock_irqsave(&cs->statlock, flags);
	p = tmpbuf;
	if (head) {
		p += jiftime(p, jiffies);
		p += sprintf(p, " %s", head);
		p += vsprintf(p, fmt, args);
		*p++ = '\n';
		*p = 0;
		len = p - tmpbuf;
		p = tmpbuf;
	} else {
		p = fmt;
		len = strlen(fmt);
	}
	if (len > HISAX_STATUS_BUFSIZE) {
		spin_unlock_irqrestore(&cs->statlock, flags);
		printk(KERN_WARNING "HiSax: status overflow %d/%d\n",
		       len, HISAX_STATUS_BUFSIZE);
		return;
	}
	count = len;
	i = cs->status_end - cs->status_write + 1;
	if (i >= len)
		i = len;
	len -= i;
	memcpy(cs->status_write, p, i);
	cs->status_write += i;
	if (cs->status_write > cs->status_end)
		cs->status_write = cs->status_buf;
	p += i;
	if (len) {
		memcpy(cs->status_write, p, len);
		cs->status_write += len;
	}
#ifdef KERNELSTACK_DEBUG
	i = (ulong) & len - current->kernel_stack_page;
	sprintf(tmpbuf, "kstack %s %lx use %ld\n", current->comm,
		current->kernel_stack_page, i);
	len = strlen(tmpbuf);
	for (p = tmpbuf, i = len; i > 0; i--, p++) {
		*cs->status_write++ = *p;
		if (cs->status_write > cs->status_end)
			cs->status_write = cs->status_buf;
		count++;
	}
#endif
	spin_unlock_irqrestore(&cs->statlock, flags);
	if (count) {
		ic.command = ISDN_STAT_STAVAIL;
		ic.driver = cs->myid;
		ic.arg = count;
		cs->iif.statcallb(&ic);
	}
}

void HiSax_putstatus(struct IsdnCardState *cs, char *head, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	VHiSax_putstatus(cs, head, fmt, args);
	va_end(args);
}

int ll_run(struct IsdnCardState *cs, int addfeatures)
{
	isdn_ctrl ic;

	ic.driver = cs->myid;
	ic.command = ISDN_STAT_RUN;
	cs->iif.features |= addfeatures;
	cs->iif.statcallb(&ic);
	return 0;
}

static void ll_stop(struct IsdnCardState *cs)
{
	isdn_ctrl ic;

	ic.command = ISDN_STAT_STOP;
	ic.driver = cs->myid;
	cs->iif.statcallb(&ic);
	//      CallcFreeChan(cs);
}

static void ll_unload(struct IsdnCardState *cs)
{
	isdn_ctrl ic;

	ic.command = ISDN_STAT_UNLOAD;
	ic.driver = cs->myid;
	cs->iif.statcallb(&ic);
	kfree(cs->status_buf);
	cs->status_read = NULL;
	cs->status_write = NULL;
	cs->status_end = NULL;
	kfree(cs->dlog);
	cs->dlog = NULL;
}

static void closecard(int cardnr)
{
	struct IsdnCardState *csta = cards[cardnr].cs;

	if (csta->bcs->BC_Close != NULL) {
		csta->bcs->BC_Close(csta->bcs + 1);
		csta->bcs->BC_Close(csta->bcs);
	}

	skb_queue_purge(&csta->rq);
	skb_queue_purge(&csta->sq);
	kfree(csta->rcvbuf);
	csta->rcvbuf = NULL;
	if (csta->tx_skb) {
		dev_kfree_skb(csta->tx_skb);
		csta->tx_skb = NULL;
	}
	if (csta->DC_Close != NULL) {
		csta->DC_Close(csta);
	}
	if (csta->cardmsg)
		csta->cardmsg(csta, CARD_RELEASE, NULL);
	if (csta->dbusytimer.function != NULL) // FIXME?
		del_timer(&csta->dbusytimer);
	ll_unload(csta);
}

static int init_card(struct IsdnCardState *cs)
{
	int 	irq_cnt, cnt = 3, ret;

	if (!cs->irq) {
		ret = cs->cardmsg(cs, CARD_INIT, NULL);
		return(ret);
	}
	irq_cnt = kstat_irqs(cs->irq);
	printk(KERN_INFO "%s: IRQ %d count %d\n", CardType[cs->typ],
	       cs->irq, irq_cnt);
	if (request_irq(cs->irq, cs->irq_func, cs->irq_flags, "HiSax", cs)) {
		printk(KERN_WARNING "HiSax: couldn't get interrupt %d\n",
		       cs->irq);
		return 1;
	}
	while (cnt) {
		cs->cardmsg(cs, CARD_INIT, NULL);
		/* Timeout 10ms */
		msleep(10);
		printk(KERN_INFO "%s: IRQ %d count %d\n",
		       CardType[cs->typ], cs->irq, kstat_irqs(cs->irq));
		if (kstat_irqs(cs->irq) == irq_cnt) {
			printk(KERN_WARNING
			       "%s: IRQ(%d) getting no interrupts during init %d\n",
			       CardType[cs->typ], cs->irq, 4 - cnt);
			if (cnt == 1) {
				free_irq(cs->irq, cs);
				return 2;
			} else {
				cs->cardmsg(cs, CARD_RESET, NULL);
				cnt--;
			}
		} else {
			cs->cardmsg(cs, CARD_TEST, NULL);
			return 0;
		}
	}
	return 3;
}

static int checkcard(int cardnr, char *id, int *busy_flag, struct module *lockowner)
{
	int ret = 0;
	struct IsdnCard *card = cards + cardnr;
	struct IsdnCardState *cs;

	cs = kzalloc(sizeof(struct IsdnCardState), GFP_ATOMIC);
	if (!cs) {
		printk(KERN_WARNING
		       "HiSax: No memory for IsdnCardState(card %d)\n",
		       cardnr + 1);
		goto out;
	}
	card->cs = cs;
	spin_lock_init(&cs->statlock);
	spin_lock_init(&cs->lock);
	cs->chanlimit = 2;	/* maximum B-channel number */
	cs->logecho = 0;	/* No echo logging */
	cs->cardnr = cardnr;
	cs->debug = L1_DEB_WARN;
	cs->HW_Flags = 0;
	cs->busy_flag = busy_flag;
	cs->irq_flags = I4L_IRQ_FLAG;
#if TEI_PER_CARD
	if (card->protocol == ISDN_PTYPE_NI1)
		test_and_set_bit(FLG_TWO_DCHAN, &cs->HW_Flags);
#else
	test_and_set_bit(FLG_TWO_DCHAN, &cs->HW_Flags);
#endif
	cs->protocol = card->protocol;

	if (card->typ <= 0 || card->typ > ISDN_CTYPE_COUNT) {
		printk(KERN_WARNING
		       "HiSax: Card Type %d out of range\n", card->typ);
		goto outf_cs;
	}
	if (!(cs->dlog = kmalloc(MAX_DLOG_SPACE, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for dlog(card %d)\n", cardnr + 1);
		goto outf_cs;
	}
	if (!(cs->status_buf = kmalloc(HISAX_STATUS_BUFSIZE, GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for status_buf(card %d)\n",
		       cardnr + 1);
		goto outf_dlog;
	}
	cs->stlist = NULL;
	cs->status_read = cs->status_buf;
	cs->status_write = cs->status_buf;
	cs->status_end = cs->status_buf + HISAX_STATUS_BUFSIZE - 1;
	cs->typ = card->typ;
#ifdef MODULE
	cs->iif.owner = lockowner;
#endif
	strcpy(cs->iif.id, id);
	cs->iif.channels = 2;
	cs->iif.maxbufsize = MAX_DATA_SIZE;
	cs->iif.hl_hdrlen = MAX_HEADER_LEN;
	cs->iif.features =
		ISDN_FEATURE_L2_X75I |
		ISDN_FEATURE_L2_HDLC |
		ISDN_FEATURE_L2_HDLC_56K |
		ISDN_FEATURE_L2_TRANS |
		ISDN_FEATURE_L3_TRANS |
#ifdef	CONFIG_HISAX_1TR6
		ISDN_FEATURE_P_1TR6 |
#endif
#ifdef	CONFIG_HISAX_EURO
		ISDN_FEATURE_P_EURO |
#endif
#ifdef	CONFIG_HISAX_NI1
		ISDN_FEATURE_P_NI1 |
#endif
		0;

	cs->iif.command = HiSax_command;
	cs->iif.writecmd = NULL;
	cs->iif.writebuf_skb = HiSax_writebuf_skb;
	cs->iif.readstat = HiSax_readstatus;
	register_isdn(&cs->iif);
	cs->myid = cs->iif.channels;
	printk(KERN_INFO
	       "HiSax: Card %d Protocol %s Id=%s (%d)\n", cardnr + 1,
	       (card->protocol == ISDN_PTYPE_1TR6) ? "1TR6" :
	       (card->protocol == ISDN_PTYPE_EURO) ? "EDSS1" :
	       (card->protocol == ISDN_PTYPE_LEASED) ? "LEASED" :
	       (card->protocol == ISDN_PTYPE_NI1) ? "NI1" :
	       "NONE", cs->iif.id, cs->myid);
	switch (card->typ) {
#if CARD_TELES0
	case ISDN_CTYPE_16_0:
	case ISDN_CTYPE_8_0:
		ret = setup_teles0(card);
		break;
#endif
#if CARD_TELES3
	case ISDN_CTYPE_16_3:
	case ISDN_CTYPE_PNP:
	case ISDN_CTYPE_TELESPCMCIA:
	case ISDN_CTYPE_COMPAQ_ISA:
		ret = setup_teles3(card);
		break;
#endif
#if CARD_S0BOX
	case ISDN_CTYPE_S0BOX:
		ret = setup_s0box(card);
		break;
#endif
#if CARD_TELESPCI
	case ISDN_CTYPE_TELESPCI:
		ret = setup_telespci(card);
		break;
#endif
#if CARD_AVM_A1
	case ISDN_CTYPE_A1:
		ret = setup_avm_a1(card);
		break;
#endif
#if CARD_AVM_A1_PCMCIA
	case ISDN_CTYPE_A1_PCMCIA:
		ret = setup_avm_a1_pcmcia(card);
		break;
#endif
#if CARD_FRITZPCI
	case ISDN_CTYPE_FRITZPCI:
		ret = setup_avm_pcipnp(card);
		break;
#endif
#if CARD_ELSA
	case ISDN_CTYPE_ELSA:
	case ISDN_CTYPE_ELSA_PNP:
	case ISDN_CTYPE_ELSA_PCMCIA:
	case ISDN_CTYPE_ELSA_PCI:
		ret = setup_elsa(card);
		break;
#endif
#if CARD_IX1MICROR2
	case ISDN_CTYPE_IX1MICROR2:
		ret = setup_ix1micro(card);
		break;
#endif
#if CARD_DIEHLDIVA
	case ISDN_CTYPE_DIEHLDIVA:
		ret = setup_diva(card);
		break;
#endif
#if CARD_ASUSCOM
	case ISDN_CTYPE_ASUSCOM:
		ret = setup_asuscom(card);
		break;
#endif
#if CARD_TELEINT
	case ISDN_CTYPE_TELEINT:
		ret = setup_TeleInt(card);
		break;
#endif
#if CARD_SEDLBAUER
	case ISDN_CTYPE_SEDLBAUER:
	case ISDN_CTYPE_SEDLBAUER_PCMCIA:
	case ISDN_CTYPE_SEDLBAUER_FAX:
		ret = setup_sedlbauer(card);
		break;
#endif
#if CARD_SPORTSTER
	case ISDN_CTYPE_SPORTSTER:
		ret = setup_sportster(card);
		break;
#endif
#if CARD_MIC
	case ISDN_CTYPE_MIC:
		ret = setup_mic(card);
		break;
#endif
#if CARD_NETJET_S
	case ISDN_CTYPE_NETJET_S:
		ret = setup_netjet_s(card);
		break;
#endif
#if CARD_HFCS
	case ISDN_CTYPE_TELES3C:
	case ISDN_CTYPE_ACERP10:
		ret = setup_hfcs(card);
		break;
#endif
#if CARD_HFC_PCI
	case ISDN_CTYPE_HFC_PCI:
		ret = setup_hfcpci(card);
		break;
#endif
#if CARD_HFC_SX
	case ISDN_CTYPE_HFC_SX:
		ret = setup_hfcsx(card);
		break;
#endif
#if CARD_NICCY
	case ISDN_CTYPE_NICCY:
		ret = setup_niccy(card);
		break;
#endif
#if CARD_ISURF
	case ISDN_CTYPE_ISURF:
		ret = setup_isurf(card);
		break;
#endif
#if CARD_HSTSAPHIR
	case ISDN_CTYPE_HSTSAPHIR:
		ret = setup_saphir(card);
		break;
#endif
#if CARD_TESTEMU
	case ISDN_CTYPE_TESTEMU:
		ret = setup_testemu(card);
		break;
#endif
#if	CARD_BKM_A4T
	case ISDN_CTYPE_BKM_A4T:
		ret = setup_bkm_a4t(card);
		break;
#endif
#if	CARD_SCT_QUADRO
	case ISDN_CTYPE_SCT_QUADRO:
		ret = setup_sct_quadro(card);
		break;
#endif
#if CARD_GAZEL
	case ISDN_CTYPE_GAZEL:
		ret = setup_gazel(card);
		break;
#endif
#if CARD_W6692
	case ISDN_CTYPE_W6692:
		ret = setup_w6692(card);
		break;
#endif
#if CARD_NETJET_U
	case ISDN_CTYPE_NETJET_U:
		ret = setup_netjet_u(card);
		break;
#endif
#if CARD_FN_ENTERNOW_PCI
	case ISDN_CTYPE_ENTERNOW:
		ret = setup_enternow_pci(card);
		break;
#endif
	case ISDN_CTYPE_DYNAMIC:
		ret = 2;
		break;
	default:
		printk(KERN_WARNING
		       "HiSax: Support for %s Card not selected\n",
		       CardType[card->typ]);
		ll_unload(cs);
		goto outf_cs;
	}
	if (!ret) {
		ll_unload(cs);
		goto outf_cs;
	}
	if (!(cs->rcvbuf = kmalloc(MAX_DFRAME_LEN_L1, GFP_ATOMIC))) {
		printk(KERN_WARNING "HiSax: No memory for isac rcvbuf\n");
		ll_unload(cs);
		goto outf_cs;
	}
	cs->rcvidx = 0;
	cs->tx_skb = NULL;
	cs->tx_cnt = 0;
	cs->event = 0;

	skb_queue_head_init(&cs->rq);
	skb_queue_head_init(&cs->sq);

	init_bcstate(cs, 0);
	init_bcstate(cs, 1);

	/* init_card only handles interrupts which are not */
	/* used here for the loadable driver */
	switch (card->typ) {
		case ISDN_CTYPE_DYNAMIC:
			ret = 0;
			break;
		default:
			ret = init_card(cs);
			break;
	}
	if (ret) {
		closecard(cardnr);
		ret = 0;
		goto outf_cs;
	}
	init_tei(cs, cs->protocol);
	ret = CallcNewChan(cs);
	if (ret) {
		closecard(cardnr);
		ret = 0;
		goto outf_cs;
	}
	/* ISAR needs firmware download first */
	if (!test_bit(HW_ISAR, &cs->HW_Flags))
		ll_run(cs, 0);

	ret = 1;
	goto out;

 outf_dlog:
	kfree(cs->dlog);
 outf_cs:
	kfree(cs);
	card->cs = NULL;
 out:
	return ret;
}

static void HiSax_shiftcards(int idx)
{
	int i;

	for (i = idx; i < (HISAX_MAX_CARDS - 1); i++)
		memcpy(&cards[i], &cards[i + 1], sizeof(cards[i]));
}

static int HiSax_inithardware(int *busy_flag)
{
	int foundcards = 0;
	int i = 0;
	int t = ',';
	int flg = 0;
	char *id;
	char *next_id = HiSax_id;
	char ids[20];

	if (strchr(HiSax_id, ','))
		t = ',';
	else if (strchr(HiSax_id, '%'))
		t = '%';

	while (i < nrcards) {
		if (cards[i].typ < 1)
			break;
		id = next_id;
		if ((next_id = strchr(id, t))) {
			*next_id++ = 0;
			strcpy(ids, id);
			flg = i + 1;
		} else {
			next_id = id;
			if (flg >= i)
				strcpy(ids, id);
			else
				sprintf(ids, "%s%d", id, i);
		}
		if (checkcard(i, ids, busy_flag, THIS_MODULE)) {
			foundcards++;
			i++;
		} else {
			/* make sure we don't oops the module */
			if (cards[i].typ > 0 && cards[i].typ <= ISDN_CTYPE_COUNT) {
				printk(KERN_WARNING
			       		"HiSax: Card %s not installed !\n",
			       		CardType[cards[i].typ]);
			}
			HiSax_shiftcards(i);
			nrcards--;
		}
	}
	return foundcards;
}

void HiSax_closecard(int cardnr)
{
	int i, last = nrcards - 1;

	if (cardnr > last || cardnr < 0)
		return;
	if (cards[cardnr].cs) {
		ll_stop(cards[cardnr].cs);
		release_tei(cards[cardnr].cs);
		CallcFreeChan(cards[cardnr].cs);

		closecard(cardnr);
		if (cards[cardnr].cs->irq)
			free_irq(cards[cardnr].cs->irq, cards[cardnr].cs);
		kfree((void *) cards[cardnr].cs);
		cards[cardnr].cs = NULL;
	}
	i = cardnr;
	while (i <= last) {
		cards[i] = cards[i + 1];
		i++;
	}
	nrcards--;
}

void HiSax_reportcard(int cardnr, int sel)
{
	struct IsdnCardState *cs = cards[cardnr].cs;

	printk(KERN_DEBUG "HiSax: reportcard No %d\n", cardnr + 1);
	printk(KERN_DEBUG "HiSax: Type %s\n", CardType[cs->typ]);
	printk(KERN_DEBUG "HiSax: debuglevel %x\n", cs->debug);
	printk(KERN_DEBUG "HiSax: HiSax_reportcard address 0x%lX\n",
	       (ulong) & HiSax_reportcard);
	printk(KERN_DEBUG "HiSax: cs 0x%lX\n", (ulong) cs);
	printk(KERN_DEBUG "HiSax: HW_Flags %lx bc0 flg %lx bc1 flg %lx\n",
	       cs->HW_Flags, cs->bcs[0].Flag, cs->bcs[1].Flag);
	printk(KERN_DEBUG "HiSax: bcs 0 mode %d ch%d\n",
	       cs->bcs[0].mode, cs->bcs[0].channel);
	printk(KERN_DEBUG "HiSax: bcs 1 mode %d ch%d\n",
	       cs->bcs[1].mode, cs->bcs[1].channel);
#ifdef ERROR_STATISTIC
	printk(KERN_DEBUG "HiSax: dc errors(rx,crc,tx) %d,%d,%d\n",
	       cs->err_rx, cs->err_crc, cs->err_tx);
	printk(KERN_DEBUG
	       "HiSax: bc0 errors(inv,rdo,crc,tx) %d,%d,%d,%d\n",
	       cs->bcs[0].err_inv, cs->bcs[0].err_rdo, cs->bcs[0].err_crc,
	       cs->bcs[0].err_tx);
	printk(KERN_DEBUG
	       "HiSax: bc1 errors(inv,rdo,crc,tx) %d,%d,%d,%d\n",
	       cs->bcs[1].err_inv, cs->bcs[1].err_rdo, cs->bcs[1].err_crc,
	       cs->bcs[1].err_tx);
	if (sel == 99) {
		cs->err_rx  = 0;
		cs->err_crc = 0;
		cs->err_tx  = 0;
		cs->bcs[0].err_inv = 0;
		cs->bcs[0].err_rdo = 0;
		cs->bcs[0].err_crc = 0;
		cs->bcs[0].err_tx  = 0;
		cs->bcs[1].err_inv = 0;
		cs->bcs[1].err_rdo = 0;
		cs->bcs[1].err_crc = 0;
		cs->bcs[1].err_tx  = 0;
	}
#endif
}

static int __init HiSax_init(void)
{
	int i, retval;
#ifdef MODULE
	int j;
	int nzproto = 0;
#endif

	HiSaxVersion();
	retval = CallcNew();
	if (retval)
		goto out;
	retval = Isdnl3New();
	if (retval)
		goto out_callc;
	retval = Isdnl2New();
	if (retval)
		goto out_isdnl3;
	retval = TeiNew();
	if (retval)
		goto out_isdnl2;
	retval = Isdnl1New();
	if (retval)
		goto out_tei;

#ifdef MODULE
	if (!type[0]) {
		/* We 'll register drivers later, but init basic functions */
		for (i = 0; i < HISAX_MAX_CARDS; i++)
			cards[i].typ = 0;
		return 0;
	}
#ifdef CONFIG_HISAX_ELSA
	if (type[0] == ISDN_CTYPE_ELSA_PCMCIA) {
		/* we have exported  and return in this case */
		return 0;
	}
#endif
#ifdef CONFIG_HISAX_SEDLBAUER
	if (type[0] == ISDN_CTYPE_SEDLBAUER_PCMCIA) {
		/* we have to export  and return in this case */
		return 0;
	}
#endif
#ifdef CONFIG_HISAX_AVM_A1_PCMCIA
	if (type[0] == ISDN_CTYPE_A1_PCMCIA) {
		/* we have to export  and return in this case */
		return 0;
	}
#endif
#ifdef CONFIG_HISAX_HFC_SX
	if (type[0] == ISDN_CTYPE_HFC_SP_PCMCIA) {
		/* we have to export  and return in this case */
		return 0;
	}
#endif
#endif
	nrcards = 0;
#ifdef MODULE
	if (id)			/* If id= string used */
		HiSax_id = id;
	for (i = j = 0; j < HISAX_MAX_CARDS; i++) {
		cards[j].typ = type[i];
		if (protocol[i]) {
			cards[j].protocol = protocol[i];
			nzproto++;
		} else {
			cards[j].protocol = DEFAULT_PROTO;
		}
		switch (type[i]) {
		case ISDN_CTYPE_16_0:
			cards[j].para[0] = irq[i];
			cards[j].para[1] = mem[i];
			cards[j].para[2] = io[i];
			break;

		case ISDN_CTYPE_8_0:
			cards[j].para[0] = irq[i];
			cards[j].para[1] = mem[i];
			break;

#ifdef IO0_IO1
		case ISDN_CTYPE_PNP:
		case ISDN_CTYPE_NICCY:
			cards[j].para[0] = irq[i];
			cards[j].para[1] = io0[i];
			cards[j].para[2] = io1[i];
			break;
		case ISDN_CTYPE_COMPAQ_ISA:
			cards[j].para[0] = irq[i];
			cards[j].para[1] = io0[i];
			cards[j].para[2] = io1[i];
			cards[j].para[3] = io[i];
			break;
#endif
		case ISDN_CTYPE_ELSA:
		case ISDN_CTYPE_HFC_PCI:
			cards[j].para[0] = io[i];
			break;
		case ISDN_CTYPE_16_3:
		case ISDN_CTYPE_TELESPCMCIA:
		case ISDN_CTYPE_A1:
		case ISDN_CTYPE_A1_PCMCIA:
		case ISDN_CTYPE_ELSA_PNP:
		case ISDN_CTYPE_ELSA_PCMCIA:
		case ISDN_CTYPE_IX1MICROR2:
		case ISDN_CTYPE_DIEHLDIVA:
		case ISDN_CTYPE_ASUSCOM:
		case ISDN_CTYPE_TELEINT:
		case ISDN_CTYPE_SEDLBAUER:
		case ISDN_CTYPE_SEDLBAUER_PCMCIA:
		case ISDN_CTYPE_SEDLBAUER_FAX:
		case ISDN_CTYPE_SPORTSTER:
		case ISDN_CTYPE_MIC:
		case ISDN_CTYPE_TELES3C:
		case ISDN_CTYPE_ACERP10:
		case ISDN_CTYPE_S0BOX:
		case ISDN_CTYPE_FRITZPCI:
		case ISDN_CTYPE_HSTSAPHIR:
		case ISDN_CTYPE_GAZEL:
		case ISDN_CTYPE_HFC_SX:
		case ISDN_CTYPE_HFC_SP_PCMCIA:
			cards[j].para[0] = irq[i];
			cards[j].para[1] = io[i];
			break;
		case ISDN_CTYPE_ISURF:
			cards[j].para[0] = irq[i];
			cards[j].para[1] = io[i];
			cards[j].para[2] = mem[i];
			break;
		case ISDN_CTYPE_ELSA_PCI:
		case ISDN_CTYPE_NETJET_S:
		case ISDN_CTYPE_TELESPCI:
		case ISDN_CTYPE_W6692:
		case ISDN_CTYPE_NETJET_U:
			break;
		case ISDN_CTYPE_BKM_A4T:
			break;
		case ISDN_CTYPE_SCT_QUADRO:
			if (irq[i]) {
				cards[j].para[0] = irq[i];
			} else {
				/* QUADRO is a 4 BRI card */
				cards[j++].para[0] = 1;
				/* we need to check if further cards can be added */
				if (j < HISAX_MAX_CARDS) {
					cards[j].typ = ISDN_CTYPE_SCT_QUADRO;
					cards[j].protocol = protocol[i];
					cards[j++].para[0] = 2;
				}
				if (j < HISAX_MAX_CARDS) {
					cards[j].typ = ISDN_CTYPE_SCT_QUADRO;
					cards[j].protocol = protocol[i];
					cards[j++].para[0] = 3;
				}
				if (j < HISAX_MAX_CARDS) {
					cards[j].typ = ISDN_CTYPE_SCT_QUADRO;
					cards[j].protocol = protocol[i];
					cards[j].para[0] = 4;
				}
			}
			break;
		}
		j++;
	}
	if (!nzproto) {
		printk(KERN_WARNING
		       "HiSax: Warning - no protocol specified\n");
		printk(KERN_WARNING "HiSax: using protocol %s\n",
		       DEFAULT_PROTO_NAME);
	}
#endif
	if (!HiSax_id)
		HiSax_id = HiSaxID;
	if (!HiSaxID[0])
		strcpy(HiSaxID, "HiSax");
	for (i = 0; i < HISAX_MAX_CARDS; i++)
		if (cards[i].typ > 0)
			nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
	       nrcards, (nrcards > 1) ? "s" : "");

	/* Install only, if at least one card found */
	if (!HiSax_inithardware(NULL))
		return -ENODEV;
	return 0;

 out_tei:
	TeiFree();
 out_isdnl2:
	Isdnl2Free();
 out_isdnl3:
	Isdnl3Free();
 out_callc:
	CallcFree();
 out:
	return retval;
}

static void __exit HiSax_exit(void)
{
	int cardnr = nrcards - 1;

	while (cardnr >= 0)
		HiSax_closecard(cardnr--);
	Isdnl1Free();
	TeiFree();
	Isdnl2Free();
	Isdnl3Free();
	CallcFree();
	printk(KERN_INFO "HiSax module removed\n");
}

int hisax_init_pcmcia(void *pcm_iob, int *busy_flag, struct IsdnCard *card)
{
	u_char ids[16];
	int ret = -1;

	cards[nrcards] = *card;
	if (nrcards)
		sprintf(ids, "HiSax%d", nrcards);
	else
		sprintf(ids, "HiSax");
	if (!checkcard(nrcards, ids, busy_flag, THIS_MODULE))
		goto error;

	ret = nrcards;
	nrcards++;
error:
	return ret;
}

EXPORT_SYMBOL(hisax_init_pcmcia);
EXPORT_SYMBOL(HiSax_closecard);

#include "hisax_if.h"

EXPORT_SYMBOL(hisax_register);
EXPORT_SYMBOL(hisax_unregister);

static void hisax_d_l1l2(struct hisax_if *ifc, int pr, void *arg);
static void hisax_b_l1l2(struct hisax_if *ifc, int pr, void *arg);
static void hisax_d_l2l1(struct PStack *st, int pr, void *arg);
static void hisax_b_l2l1(struct PStack *st, int pr, void *arg);
static int hisax_cardmsg(struct IsdnCardState *cs, int mt, void *arg);
static int hisax_bc_setstack(struct PStack *st, struct BCState *bcs);
static void hisax_bc_close(struct BCState *bcs);
static void hisax_bh(struct work_struct *work);
static void EChannel_proc_rcv(struct hisax_d_if *d_if);

int hisax_register(struct hisax_d_if *hisax_d_if, struct hisax_b_if *b_if[],
		   char *name, int protocol)
{
	int i, retval;
	char id[20];
	struct IsdnCardState *cs;

	for (i = 0; i < HISAX_MAX_CARDS; i++) {
		if (!cards[i].typ)
			break;
	}

	if (i >= HISAX_MAX_CARDS)
		return -EBUSY;

	cards[i].typ = ISDN_CTYPE_DYNAMIC;
	cards[i].protocol = protocol;
	sprintf(id, "%s%d", name, i);
	nrcards++;
	retval = checkcard(i, id, NULL, hisax_d_if->owner);
	if (retval == 0) { // yuck
		cards[i].typ = 0;
		nrcards--;
		return retval;
	}
	cs = cards[i].cs;
	hisax_d_if->cs = cs;
	cs->hw.hisax_d_if = hisax_d_if;
	cs->cardmsg = hisax_cardmsg;
	INIT_WORK(&cs->tqueue, hisax_bh);
	cs->channel[0].d_st->l2.l2l1 = hisax_d_l2l1;
	for (i = 0; i < 2; i++) {
		cs->bcs[i].BC_SetStack = hisax_bc_setstack;
		cs->bcs[i].BC_Close = hisax_bc_close;

		b_if[i]->ifc.l1l2 = hisax_b_l1l2;

		hisax_d_if->b_if[i] = b_if[i];
	}
	hisax_d_if->ifc.l1l2 = hisax_d_l1l2;
	skb_queue_head_init(&hisax_d_if->erq);
	clear_bit(0, &hisax_d_if->ph_state);
	
	return 0;
}

void hisax_unregister(struct hisax_d_if *hisax_d_if)
{
	cards[hisax_d_if->cs->cardnr].typ = 0;
	HiSax_closecard(hisax_d_if->cs->cardnr);
	skb_queue_purge(&hisax_d_if->erq);
}

#include "isdnl1.h"

static void hisax_sched_event(struct IsdnCardState *cs, int event)
{
	test_and_set_bit(event, &cs->event);
	schedule_work(&cs->tqueue);
}

static void hisax_bh(struct work_struct *work)
{
	struct IsdnCardState *cs =
		container_of(work, struct IsdnCardState, tqueue);
	struct PStack *st;
	int pr;

	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event))
		DChannel_proc_rcv(cs);
	if (test_and_clear_bit(E_RCVBUFREADY, &cs->event))
		EChannel_proc_rcv(cs->hw.hisax_d_if);
	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event)) {
		if (test_bit(0, &cs->hw.hisax_d_if->ph_state))
			pr = PH_ACTIVATE | INDICATION;
		else
			pr = PH_DEACTIVATE | INDICATION;
		for (st = cs->stlist; st; st = st->next)
			st->l1.l1l2(st, pr, NULL);
		
	}
}

static void hisax_b_sched_event(struct BCState *bcs, int event)
{
	test_and_set_bit(event, &bcs->event);
	schedule_work(&bcs->tqueue);
}

static inline void D_L2L1(struct hisax_d_if *d_if, int pr, void *arg)
{
	struct hisax_if *ifc = (struct hisax_if *) d_if;
	ifc->l2l1(ifc, pr, arg);
}

static inline void B_L2L1(struct hisax_b_if *b_if, int pr, void *arg)
{
	struct hisax_if *ifc = (struct hisax_if *) b_if;
	ifc->l2l1(ifc, pr, arg);
}

static void hisax_d_l1l2(struct hisax_if *ifc, int pr, void *arg)
{
	struct hisax_d_if *d_if = (struct hisax_d_if *) ifc;
	struct IsdnCardState *cs = d_if->cs;
	struct PStack *st;
	struct sk_buff *skb;

	switch (pr) {
	case PH_ACTIVATE | INDICATION:
		set_bit(0, &d_if->ph_state);
		hisax_sched_event(cs, D_L1STATECHANGE);
		break;
	case PH_DEACTIVATE | INDICATION:
		clear_bit(0, &d_if->ph_state);
		hisax_sched_event(cs, D_L1STATECHANGE);
		break;
	case PH_DATA | INDICATION:
		skb_queue_tail(&cs->rq, arg);
		hisax_sched_event(cs, D_RCVBUFREADY);
		break;
	case PH_DATA | CONFIRM:
		skb = skb_dequeue(&cs->sq);
		if (skb) {
			D_L2L1(d_if, PH_DATA | REQUEST, skb);
			break;
		}
		clear_bit(FLG_L1_DBUSY, &cs->HW_Flags);
		for (st = cs->stlist; st; st = st->next) {
			if (test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags)) {
				st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
				break;
			}
		}
		break;
	case PH_DATA_E | INDICATION:
		skb_queue_tail(&d_if->erq, arg);
		hisax_sched_event(cs, E_RCVBUFREADY);
		break;
	default:
		printk("pr %#x\n", pr);
		break;
	}
}

static void hisax_b_l1l2(struct hisax_if *ifc, int pr, void *arg)
{
	struct hisax_b_if *b_if = (struct hisax_b_if *) ifc;
	struct BCState *bcs = b_if->bcs;
	struct PStack *st = bcs->st;
	struct sk_buff *skb;

	// FIXME use isdnl1?
	switch (pr) {
	case PH_ACTIVATE | INDICATION:
		st->l1.l1l2(st, pr, NULL);
		break;
	case PH_DEACTIVATE | INDICATION:
		st->l1.l1l2(st, pr, NULL);
		clear_bit(BC_FLG_BUSY, &bcs->Flag);
		skb_queue_purge(&bcs->squeue);
		bcs->hw.b_if = NULL;
		break;
	case PH_DATA | INDICATION:
		skb_queue_tail(&bcs->rqueue, arg);
		hisax_b_sched_event(bcs, B_RCVBUFREADY);
		break;
	case PH_DATA | CONFIRM:
		bcs->tx_cnt -= (long)arg;
		if (test_bit(FLG_LLI_L1WAKEUP,&bcs->st->lli.flag)) {
			u_long	flags;
			spin_lock_irqsave(&bcs->aclock, flags);
			bcs->ackcnt += (long)arg;
			spin_unlock_irqrestore(&bcs->aclock, flags);
			schedule_event(bcs, B_ACKPENDING);
		}
		skb = skb_dequeue(&bcs->squeue);
		if (skb) {
			B_L2L1(b_if, PH_DATA | REQUEST, skb);
			break;
		}
		clear_bit(BC_FLG_BUSY, &bcs->Flag);
		if (test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags)) {
			st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
		}
		break;
	default:
		printk("hisax_b_l1l2 pr %#x\n", pr);
		break;
	}
}

static void hisax_d_l2l1(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = st->l1.hardware;
	struct hisax_d_if *hisax_d_if = cs->hw.hisax_d_if;
	struct sk_buff *skb = arg;

	switch (pr) {
	case PH_DATA | REQUEST:
	case PH_PULL | INDICATION:
		if (cs->debug & DEB_DLOG_HEX)
			LogFrame(cs, skb->data, skb->len);
		if (cs->debug & DEB_DLOG_VERBOSE)
			dlogframe(cs, skb, 0);
		Logl2Frame(cs, skb, "PH_DATA_REQ", 0);
		// FIXME lock?
		if (!test_and_set_bit(FLG_L1_DBUSY, &cs->HW_Flags))
			D_L2L1(hisax_d_if, PH_DATA | REQUEST, skb);
		else
			skb_queue_tail(&cs->sq, skb);
		break;
	case PH_PULL | REQUEST:
		if (!test_bit(FLG_L1_DBUSY, &cs->HW_Flags))
			st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
		else
			set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
		break;
	default:
		D_L2L1(hisax_d_if, pr, arg);
		break;
	}
}

static int hisax_cardmsg(struct IsdnCardState *cs, int mt, void *arg)
{
	return 0;
}

static void hisax_b_l2l1(struct PStack *st, int pr, void *arg)
{
	struct BCState *bcs = st->l1.bcs;
	struct hisax_b_if *b_if = bcs->hw.b_if;

	switch (pr) {
	case PH_ACTIVATE | REQUEST:
		B_L2L1(b_if, pr, (void *)(unsigned long)st->l1.mode);
		break;
	case PH_DATA | REQUEST:
	case PH_PULL | INDICATION:
		// FIXME lock?
		if (!test_and_set_bit(BC_FLG_BUSY, &bcs->Flag)) {
			B_L2L1(b_if, PH_DATA | REQUEST, arg);
		} else {
			skb_queue_tail(&bcs->squeue, arg);
		}
		break;
	case PH_PULL | REQUEST:
		if (!test_bit(BC_FLG_BUSY, &bcs->Flag))
			st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
		else
			set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
		break;
	case PH_DEACTIVATE | REQUEST:
		test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		skb_queue_purge(&bcs->squeue);
	default:
		B_L2L1(b_if, pr, arg);
		break;
	}
}

static int hisax_bc_setstack(struct PStack *st, struct BCState *bcs)
{
	struct IsdnCardState *cs = st->l1.hardware;
	struct hisax_d_if *hisax_d_if = cs->hw.hisax_d_if;

	bcs->channel = st->l1.bc;

	bcs->hw.b_if = hisax_d_if->b_if[st->l1.bc];
	hisax_d_if->b_if[st->l1.bc]->bcs = bcs;

	st->l1.bcs = bcs;
	st->l2.l2l1 = hisax_b_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	skb_queue_head_init(&bcs->rqueue);
	skb_queue_head_init(&bcs->squeue);
	return 0;
}

static void hisax_bc_close(struct BCState *bcs)
{
	struct hisax_b_if *b_if = bcs->hw.b_if;

	if (b_if)
		B_L2L1(b_if, PH_DEACTIVATE | REQUEST, NULL);
}

static void EChannel_proc_rcv(struct hisax_d_if *d_if)
{
	struct IsdnCardState *cs = d_if->cs;
	u_char *ptr;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&d_if->erq)) != NULL) {
		if (cs->debug & DEB_DLOG_HEX) {
			ptr = cs->dlog;
			if ((skb->len) < MAX_DLOG_SPACE / 3 - 10) {
				*ptr++ = 'E';
				*ptr++ = 'C';
				*ptr++ = 'H';
				*ptr++ = 'O';
				*ptr++ = ':';
				ptr += QuickHex(ptr, skb->data, skb->len);
				ptr--;
				*ptr++ = '\n';
				*ptr = 0;
				HiSax_putstatus(cs, NULL, cs->dlog);
			} else
				HiSax_putstatus(cs, "LogEcho: ",
						"warning Frame too big (%d)",
						skb->len);
		}
		dev_kfree_skb_any(skb);
	}
}

#ifdef CONFIG_PCI
#include <linux/pci.h>

static struct pci_device_id hisax_pci_tbl[] __devinitdata = {
#ifdef CONFIG_HISAX_FRITZPCI
	{PCI_VENDOR_ID_AVM,      PCI_DEVICE_ID_AVM_A1,           PCI_ANY_ID, PCI_ANY_ID},
#endif
#ifdef CONFIG_HISAX_DIEHLDIVA
	{PCI_VENDOR_ID_EICON,    PCI_DEVICE_ID_EICON_DIVA20,     PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_EICON,    PCI_DEVICE_ID_EICON_DIVA20_U,   PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_EICON,    PCI_DEVICE_ID_EICON_DIVA201,    PCI_ANY_ID, PCI_ANY_ID},
//#########################################################################################	
	{PCI_VENDOR_ID_EICON,    PCI_DEVICE_ID_EICON_DIVA202,    PCI_ANY_ID, PCI_ANY_ID},
//#########################################################################################	
#endif
#ifdef CONFIG_HISAX_ELSA
	{PCI_VENDOR_ID_ELSA,     PCI_DEVICE_ID_ELSA_MICROLINK,   PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_ELSA,     PCI_DEVICE_ID_ELSA_QS3000,      PCI_ANY_ID, PCI_ANY_ID},
#endif
#ifdef CONFIG_HISAX_GAZEL
	{PCI_VENDOR_ID_PLX,      PCI_DEVICE_ID_PLX_R685,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_PLX,      PCI_DEVICE_ID_PLX_R753,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_PLX,      PCI_DEVICE_ID_PLX_DJINN_ITOO,   PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_PLX,      PCI_DEVICE_ID_PLX_OLITEC,       PCI_ANY_ID, PCI_ANY_ID},
#endif
#ifdef CONFIG_HISAX_QUADRO
	{PCI_VENDOR_ID_PLX,      PCI_DEVICE_ID_PLX_9050,         PCI_ANY_ID, PCI_ANY_ID},
#endif
#ifdef CONFIG_HISAX_NICCY
	{PCI_VENDOR_ID_SATSAGEM, PCI_DEVICE_ID_SATSAGEM_NICCY,   PCI_ANY_ID,PCI_ANY_ID},
#endif
#ifdef CONFIG_HISAX_SEDLBAUER
	{PCI_VENDOR_ID_TIGERJET, PCI_DEVICE_ID_TIGERJET_100,     PCI_ANY_ID,PCI_ANY_ID},
#endif
#if defined(CONFIG_HISAX_NETJET) || defined(CONFIG_HISAX_NETJET_U)
	{PCI_VENDOR_ID_TIGERJET, PCI_DEVICE_ID_TIGERJET_300,     PCI_ANY_ID,PCI_ANY_ID},
#endif
#if defined(CONFIG_HISAX_TELESPCI) || defined(CONFIG_HISAX_SCT_QUADRO)
	{PCI_VENDOR_ID_ZORAN,    PCI_DEVICE_ID_ZORAN_36120,      PCI_ANY_ID,PCI_ANY_ID},
#endif
#ifdef CONFIG_HISAX_W6692
	{PCI_VENDOR_ID_DYNALINK, PCI_DEVICE_ID_DYNALINK_IS64PH,  PCI_ANY_ID,PCI_ANY_ID},
	{PCI_VENDOR_ID_WINBOND2, PCI_DEVICE_ID_WINBOND2_6692,    PCI_ANY_ID,PCI_ANY_ID},
#endif
#ifdef CONFIG_HISAX_HFC_PCI
	{PCI_VENDOR_ID_CCD,      PCI_DEVICE_ID_CCD_2BD0,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_CCD,      PCI_DEVICE_ID_CCD_B000,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_CCD,      PCI_DEVICE_ID_CCD_B006,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_CCD,      PCI_DEVICE_ID_CCD_B007,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_CCD,      PCI_DEVICE_ID_CCD_B008,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_CCD,      PCI_DEVICE_ID_CCD_B009,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_CCD,      PCI_DEVICE_ID_CCD_B00A,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_CCD,      PCI_DEVICE_ID_CCD_B00B,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_CCD,      PCI_DEVICE_ID_CCD_B00C,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_CCD,      PCI_DEVICE_ID_CCD_B100,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_CCD,      PCI_DEVICE_ID_CCD_B700,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_CCD,      PCI_DEVICE_ID_CCD_B701,         PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_ABOCOM,   PCI_DEVICE_ID_ABOCOM_2BD1,      PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_ASUSTEK,  PCI_DEVICE_ID_ASUSTEK_0675,     PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_BERKOM,   PCI_DEVICE_ID_BERKOM_T_CONCEPT, PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_BERKOM,   PCI_DEVICE_ID_BERKOM_A1T,       PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_ANIGMA,   PCI_DEVICE_ID_ANIGMA_MC145575,  PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_ZOLTRIX,  PCI_DEVICE_ID_ZOLTRIX_2BD0,     PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_DIGI,     PCI_DEVICE_ID_DIGI_DF_M_IOM2_E, PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_DIGI,     PCI_DEVICE_ID_DIGI_DF_M_E,      PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_DIGI,     PCI_DEVICE_ID_DIGI_DF_M_IOM2_A, PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_DIGI,     PCI_DEVICE_ID_DIGI_DF_M_A,      PCI_ANY_ID, PCI_ANY_ID},
#endif
	{ }				/* Terminating entry */
};

MODULE_DEVICE_TABLE(pci, hisax_pci_tbl);
#endif /* CONFIG_PCI */

module_init(HiSax_init);
module_exit(HiSax_exit);

EXPORT_SYMBOL(FsmNew);
EXPORT_SYMBOL(FsmFree);
EXPORT_SYMBOL(FsmEvent);
EXPORT_SYMBOL(FsmChangeState);
EXPORT_SYMBOL(FsmInitTimer);
EXPORT_SYMBOL(FsmDelTimer);
EXPORT_SYMBOL(FsmRestartTimer);
