/* $Id: esp.c,v 1.101 2002/01/15 06:48:55 davem Exp $
 * esp.c:  EnhancedScsiProcessor Sun SCSI driver code.
 *
 * Copyright (C) 1995, 1998 David S. Miller (davem@caip.rutgers.edu)
 */

/* TODO:
 *
 * 1) Maybe disable parity checking in config register one for SCSI1
 *    targets.  (Gilmore says parity error on the SBus can lock up
 *    old sun4c's)
 * 2) Add support for DMA2 pipelining.
 * 3) Add tagged queueing.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include "esp.h"

#include <asm/sbus.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/irq.h>
#ifndef __sparc_v9__
#include <asm/machines.h>
#include <asm/idprom.h>
#endif

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>

#define DRV_VERSION "1.101"

#define DEBUG_ESP
/* #define DEBUG_ESP_HME */
/* #define DEBUG_ESP_DATA */
/* #define DEBUG_ESP_QUEUE */
/* #define DEBUG_ESP_DISCONNECT */
/* #define DEBUG_ESP_STATUS */
/* #define DEBUG_ESP_PHASES */
/* #define DEBUG_ESP_WORKBUS */
/* #define DEBUG_STATE_MACHINE */
/* #define DEBUG_ESP_CMDS */
/* #define DEBUG_ESP_IRQS */
/* #define DEBUG_SDTR */
/* #define DEBUG_ESP_SG */

/* Use the following to sprinkle debugging messages in a way which
 * suits you if combinations of the above become too verbose when
 * trying to track down a specific problem.
 */
/* #define DEBUG_ESP_MISC */

#if defined(DEBUG_ESP)
#define ESPLOG(foo)  printk foo
#else
#define ESPLOG(foo)
#endif /* (DEBUG_ESP) */

#if defined(DEBUG_ESP_HME)
#define ESPHME(foo)  printk foo
#else
#define ESPHME(foo)
#endif

#if defined(DEBUG_ESP_DATA)
#define ESPDATA(foo)  printk foo
#else
#define ESPDATA(foo)
#endif

#if defined(DEBUG_ESP_QUEUE)
#define ESPQUEUE(foo)  printk foo
#else
#define ESPQUEUE(foo)
#endif

#if defined(DEBUG_ESP_DISCONNECT)
#define ESPDISC(foo)  printk foo
#else
#define ESPDISC(foo)
#endif

#if defined(DEBUG_ESP_STATUS)
#define ESPSTAT(foo)  printk foo
#else
#define ESPSTAT(foo)
#endif

#if defined(DEBUG_ESP_PHASES)
#define ESPPHASE(foo)  printk foo
#else
#define ESPPHASE(foo)
#endif

#if defined(DEBUG_ESP_WORKBUS)
#define ESPBUS(foo)  printk foo
#else
#define ESPBUS(foo)
#endif

#if defined(DEBUG_ESP_IRQS)
#define ESPIRQ(foo)  printk foo
#else
#define ESPIRQ(foo)
#endif

#if defined(DEBUG_SDTR)
#define ESPSDTR(foo)  printk foo
#else
#define ESPSDTR(foo)
#endif

#if defined(DEBUG_ESP_MISC)
#define ESPMISC(foo)  printk foo
#else
#define ESPMISC(foo)
#endif

/* Command phase enumeration. */
enum {
	not_issued    = 0x00,  /* Still in the issue_SC queue.          */

	/* Various forms of selecting a target. */
#define in_slct_mask    0x10
	in_slct_norm  = 0x10,  /* ESP is arbitrating, normal selection  */
	in_slct_stop  = 0x11,  /* ESP will select, then stop with IRQ   */
	in_slct_msg   = 0x12,  /* select, then send a message           */
	in_slct_tag   = 0x13,  /* select and send tagged queue msg      */
	in_slct_sneg  = 0x14,  /* select and acquire sync capabilities  */

	/* Any post selection activity. */
#define in_phases_mask  0x20
	in_datain     = 0x20,  /* Data is transferring from the bus     */
	in_dataout    = 0x21,  /* Data is transferring to the bus       */
	in_data_done  = 0x22,  /* Last DMA data operation done (maybe)  */
	in_msgin      = 0x23,  /* Eating message from target            */
	in_msgincont  = 0x24,  /* Eating more msg bytes from target     */
	in_msgindone  = 0x25,  /* Decide what to do with what we got    */
	in_msgout     = 0x26,  /* Sending message to target             */
	in_msgoutdone = 0x27,  /* Done sending msg out                  */
	in_cmdbegin   = 0x28,  /* Sending cmd after abnormal selection  */
	in_cmdend     = 0x29,  /* Done sending slow cmd                 */
	in_status     = 0x2a,  /* Was in status phase, finishing cmd    */
	in_freeing    = 0x2b,  /* freeing the bus for cmd cmplt or disc */
	in_the_dark   = 0x2c,  /* Don't know what bus phase we are in   */

	/* Special states, ie. not normal bus transitions... */
#define in_spec_mask    0x80
	in_abortone   = 0x80,  /* Aborting one command currently        */
	in_abortall   = 0x81,  /* Blowing away all commands we have     */
	in_resetdev   = 0x82,  /* SCSI target reset in progress         */
	in_resetbus   = 0x83,  /* SCSI bus reset in progress            */
	in_tgterror   = 0x84,  /* Target did something stupid           */
};

enum {
	/* Zero has special meaning, see skipahead[12]. */
/*0*/	do_never,

/*1*/	do_phase_determine,
/*2*/	do_reset_bus,
/*3*/	do_reset_complete,
/*4*/	do_work_bus,
/*5*/	do_intr_end
};

/* The master ring of all esp hosts we are managing in this driver. */
static struct esp *espchain;
static DEFINE_SPINLOCK(espchain_lock);
static int esps_running = 0;

/* Forward declarations. */
static irqreturn_t esp_intr(int irq, void *dev_id, struct pt_regs *pregs);

/* Debugging routines */
struct esp_cmdstrings {
	u8 cmdchar;
	char *text;
} esp_cmd_strings[] = {
	/* Miscellaneous */
	{ ESP_CMD_NULL, "ESP_NOP", },
	{ ESP_CMD_FLUSH, "FIFO_FLUSH", },
	{ ESP_CMD_RC, "RSTESP", },
	{ ESP_CMD_RS, "RSTSCSI", },
	/* Disconnected State Group */
	{ ESP_CMD_RSEL, "RESLCTSEQ", },
	{ ESP_CMD_SEL, "SLCTNATN", },
	{ ESP_CMD_SELA, "SLCTATN", },
	{ ESP_CMD_SELAS, "SLCTATNSTOP", },
	{ ESP_CMD_ESEL, "ENSLCTRESEL", },
	{ ESP_CMD_DSEL, "DISSELRESEL", },
	{ ESP_CMD_SA3, "SLCTATN3", },
	{ ESP_CMD_RSEL3, "RESLCTSEQ", },
	/* Target State Group */
	{ ESP_CMD_SMSG, "SNDMSG", },
	{ ESP_CMD_SSTAT, "SNDSTATUS", },
	{ ESP_CMD_SDATA, "SNDDATA", },
	{ ESP_CMD_DSEQ, "DISCSEQ", },
	{ ESP_CMD_TSEQ, "TERMSEQ", },
	{ ESP_CMD_TCCSEQ, "TRGTCMDCOMPSEQ", },
	{ ESP_CMD_DCNCT, "DISC", },
	{ ESP_CMD_RMSG, "RCVMSG", },
	{ ESP_CMD_RCMD, "RCVCMD", },
	{ ESP_CMD_RDATA, "RCVDATA", },
	{ ESP_CMD_RCSEQ, "RCVCMDSEQ", },
	/* Initiator State Group */
	{ ESP_CMD_TI, "TRANSINFO", },
	{ ESP_CMD_ICCSEQ, "INICMDSEQCOMP", },
	{ ESP_CMD_MOK, "MSGACCEPTED", },
	{ ESP_CMD_TPAD, "TPAD", },
	{ ESP_CMD_SATN, "SATN", },
	{ ESP_CMD_RATN, "RATN", },
};
#define NUM_ESP_COMMANDS  ((sizeof(esp_cmd_strings)) / (sizeof(struct esp_cmdstrings)))

/* Print textual representation of an ESP command */
static inline void esp_print_cmd(u8 espcmd)
{
	u8 dma_bit = espcmd & ESP_CMD_DMA;
	int i;

	espcmd &= ~dma_bit;
	for (i = 0; i < NUM_ESP_COMMANDS; i++)
		if (esp_cmd_strings[i].cmdchar == espcmd)
			break;
	if (i == NUM_ESP_COMMANDS)
		printk("ESP_Unknown");
	else
		printk("%s%s", esp_cmd_strings[i].text,
		       ((dma_bit) ? "+DMA" : ""));
}

/* Print the status register's value */
static inline void esp_print_statreg(u8 statreg)
{
	u8 phase;

	printk("STATUS<");
	phase = statreg & ESP_STAT_PMASK;
	printk("%s,", (phase == ESP_DOP ? "DATA-OUT" :
		       (phase == ESP_DIP ? "DATA-IN" :
			(phase == ESP_CMDP ? "COMMAND" :
			 (phase == ESP_STATP ? "STATUS" :
			  (phase == ESP_MOP ? "MSG-OUT" :
			   (phase == ESP_MIP ? "MSG_IN" :
			    "unknown")))))));
	if (statreg & ESP_STAT_TDONE)
		printk("TRANS_DONE,");
	if (statreg & ESP_STAT_TCNT)
		printk("TCOUNT_ZERO,");
	if (statreg & ESP_STAT_PERR)
		printk("P_ERROR,");
	if (statreg & ESP_STAT_SPAM)
		printk("SPAM,");
	if (statreg & ESP_STAT_INTR)
		printk("IRQ,");
	printk(">");
}

/* Print the interrupt register's value */
static inline void esp_print_ireg(u8 intreg)
{
	printk("INTREG< ");
	if (intreg & ESP_INTR_S)
		printk("SLCT_NATN ");
	if (intreg & ESP_INTR_SATN)
		printk("SLCT_ATN ");
	if (intreg & ESP_INTR_RSEL)
		printk("RSLCT ");
	if (intreg & ESP_INTR_FDONE)
		printk("FDONE ");
	if (intreg & ESP_INTR_BSERV)
		printk("BSERV ");
	if (intreg & ESP_INTR_DC)
		printk("DISCNCT ");
	if (intreg & ESP_INTR_IC)
		printk("ILL_CMD ");
	if (intreg & ESP_INTR_SR)
		printk("SCSI_BUS_RESET ");
	printk(">");
}

/* Print the sequence step registers contents */
static inline void esp_print_seqreg(u8 stepreg)
{
	stepreg &= ESP_STEP_VBITS;
	printk("STEP<%s>",
	       (stepreg == ESP_STEP_ASEL ? "SLCT_ARB_CMPLT" :
		(stepreg == ESP_STEP_SID ? "1BYTE_MSG_SENT" :
		 (stepreg == ESP_STEP_NCMD ? "NOT_IN_CMD_PHASE" :
		  (stepreg == ESP_STEP_PPC ? "CMD_BYTES_LOST" :
		   (stepreg == ESP_STEP_FINI4 ? "CMD_SENT_OK" :
		    "UNKNOWN"))))));
}

static char *phase_string(int phase)
{
	switch (phase) {
	case not_issued:
		return "UNISSUED";
	case in_slct_norm:
		return "SLCTNORM";
	case in_slct_stop:
		return "SLCTSTOP";
	case in_slct_msg:
		return "SLCTMSG";
	case in_slct_tag:
		return "SLCTTAG";
	case in_slct_sneg:
		return "SLCTSNEG";
	case in_datain:
		return "DATAIN";
	case in_dataout:
		return "DATAOUT";
	case in_data_done:
		return "DATADONE";
	case in_msgin:
		return "MSGIN";
	case in_msgincont:
		return "MSGINCONT";
	case in_msgindone:
		return "MSGINDONE";
	case in_msgout:
		return "MSGOUT";
	case in_msgoutdone:
		return "MSGOUTDONE";
	case in_cmdbegin:
		return "CMDBEGIN";
	case in_cmdend:
		return "CMDEND";
	case in_status:
		return "STATUS";
	case in_freeing:
		return "FREEING";
	case in_the_dark:
		return "CLUELESS";
	case in_abortone:
		return "ABORTONE";
	case in_abortall:
		return "ABORTALL";
	case in_resetdev:
		return "RESETDEV";
	case in_resetbus:
		return "RESETBUS";
	case in_tgterror:
		return "TGTERROR";
	default:
		return "UNKNOWN";
	};
}

#ifdef DEBUG_STATE_MACHINE
static inline void esp_advance_phase(struct scsi_cmnd *s, int newphase)
{
	ESPLOG(("<%s>", phase_string(newphase)));
	s->SCp.sent_command = s->SCp.phase;
	s->SCp.phase = newphase;
}
#else
#define esp_advance_phase(__s, __newphase) \
	(__s)->SCp.sent_command = (__s)->SCp.phase; \
	(__s)->SCp.phase = (__newphase);
#endif

#ifdef DEBUG_ESP_CMDS
static inline void esp_cmd(struct esp *esp, u8 cmd)
{
	esp->espcmdlog[esp->espcmdent] = cmd;
	esp->espcmdent = (esp->espcmdent + 1) & 31;
	sbus_writeb(cmd, esp->eregs + ESP_CMD);
}
#else
#define esp_cmd(__esp, __cmd)	\
	sbus_writeb((__cmd), ((__esp)->eregs) + ESP_CMD)
#endif

#define ESP_INTSOFF(__dregs)	\
	sbus_writel(sbus_readl((__dregs)+DMA_CSR)&~(DMA_INT_ENAB), (__dregs)+DMA_CSR)
#define ESP_INTSON(__dregs)	\
	sbus_writel(sbus_readl((__dregs)+DMA_CSR)|DMA_INT_ENAB, (__dregs)+DMA_CSR)
#define ESP_IRQ_P(__dregs)	\
	(sbus_readl((__dregs)+DMA_CSR) & (DMA_HNDL_INTR|DMA_HNDL_ERROR))

/* How we use the various Linux SCSI data structures for operation.
 *
 * struct scsi_cmnd:
 *
 *   We keep track of the synchronous capabilities of a target
 *   in the device member, using sync_min_period and
 *   sync_max_offset.  These are the values we directly write
 *   into the ESP registers while running a command.  If offset
 *   is zero the ESP will use asynchronous transfers.
 *   If the borken flag is set we assume we shouldn't even bother
 *   trying to negotiate for synchronous transfer as this target
 *   is really stupid.  If we notice the target is dropping the
 *   bus, and we have been allowing it to disconnect, we clear
 *   the disconnect flag.
 */


/* Manipulation of the ESP command queues.  Thanks to the aha152x driver
 * and its author, Juergen E. Fischer, for the methods used here.
 * Note that these are per-ESP queues, not global queues like
 * the aha152x driver uses.
 */
static inline void append_SC(struct scsi_cmnd **SC, struct scsi_cmnd *new_SC)
{
	struct scsi_cmnd *end;

	new_SC->host_scribble = (unsigned char *) NULL;
	if (!*SC)
		*SC = new_SC;
	else {
		for (end=*SC;end->host_scribble;end=(struct scsi_cmnd *)end->host_scribble)
			;
		end->host_scribble = (unsigned char *) new_SC;
	}
}

static inline void prepend_SC(struct scsi_cmnd **SC, struct scsi_cmnd *new_SC)
{
	new_SC->host_scribble = (unsigned char *) *SC;
	*SC = new_SC;
}

static inline struct scsi_cmnd *remove_first_SC(struct scsi_cmnd **SC)
{
	struct scsi_cmnd *ptr;
	ptr = *SC;
	if (ptr)
		*SC = (struct scsi_cmnd *) (*SC)->host_scribble;
	return ptr;
}

static inline struct scsi_cmnd *remove_SC(struct scsi_cmnd **SC, int target, int lun)
{
	struct scsi_cmnd *ptr, *prev;

	for (ptr = *SC, prev = NULL;
	     ptr && ((ptr->device->id != target) || (ptr->device->lun != lun));
	     prev = ptr, ptr = (struct scsi_cmnd *) ptr->host_scribble)
		;
	if (ptr) {
		if (prev)
			prev->host_scribble=ptr->host_scribble;
		else
			*SC=(struct scsi_cmnd *)ptr->host_scribble;
	}
	return ptr;
}

/* Resetting various pieces of the ESP scsi driver chipset/buses. */
static void esp_reset_dma(struct esp *esp)
{
	int can_do_burst16, can_do_burst32, can_do_burst64;
	int can_do_sbus64;
	u32 tmp;

	can_do_burst16 = (esp->bursts & DMA_BURST16) != 0;
	can_do_burst32 = (esp->bursts & DMA_BURST32) != 0;
	can_do_burst64 = 0;
	can_do_sbus64 = 0;
	if (sbus_can_dma_64bit(esp->sdev))
		can_do_sbus64 = 1;
	if (sbus_can_burst64(esp->sdev))
		can_do_burst64 = (esp->bursts & DMA_BURST64) != 0;

	/* Punt the DVMA into a known state. */
	if (esp->dma->revision != dvmahme) {
		tmp = sbus_readl(esp->dregs + DMA_CSR);
		sbus_writel(tmp | DMA_RST_SCSI, esp->dregs + DMA_CSR);
		sbus_writel(tmp & ~DMA_RST_SCSI, esp->dregs + DMA_CSR);
	}
	switch (esp->dma->revision) {
	case dvmahme:
		/* This is the HME DVMA gate array. */

		sbus_writel(DMA_RESET_FAS366, esp->dregs + DMA_CSR);
		sbus_writel(DMA_RST_SCSI, esp->dregs + DMA_CSR);

		esp->prev_hme_dmacsr = (DMA_PARITY_OFF|DMA_2CLKS|DMA_SCSI_DISAB|DMA_INT_ENAB);
		esp->prev_hme_dmacsr &= ~(DMA_ENABLE|DMA_ST_WRITE|DMA_BRST_SZ);

		if (can_do_burst64)
			esp->prev_hme_dmacsr |= DMA_BRST64;
		else if (can_do_burst32)
			esp->prev_hme_dmacsr |= DMA_BRST32;

		if (can_do_sbus64) {
			esp->prev_hme_dmacsr |= DMA_SCSI_SBUS64;
			sbus_set_sbus64(esp->sdev, esp->bursts);
		}

		/* This chip is horrible. */
		while (sbus_readl(esp->dregs + DMA_CSR) & DMA_PEND_READ)
			udelay(1);

		sbus_writel(0, esp->dregs + DMA_CSR);
		sbus_writel(esp->prev_hme_dmacsr, esp->dregs + DMA_CSR);

		/* This is necessary to avoid having the SCSI channel
		 * engine lock up on us.
		 */
		sbus_writel(0, esp->dregs + DMA_ADDR);

		break;
	case dvmarev2:
		/* This is the gate array found in the sun4m
		 * NCR SBUS I/O subsystem.
		 */
		if (esp->erev != esp100) {
			tmp = sbus_readl(esp->dregs + DMA_CSR);
			sbus_writel(tmp | DMA_3CLKS, esp->dregs + DMA_CSR);
		}
		break;
	case dvmarev3:
		tmp = sbus_readl(esp->dregs + DMA_CSR);
		tmp &= ~DMA_3CLKS;
		tmp |= DMA_2CLKS;
		if (can_do_burst32) {
			tmp &= ~DMA_BRST_SZ;
			tmp |= DMA_BRST32;
		}
		sbus_writel(tmp, esp->dregs + DMA_CSR);
		break;
	case dvmaesc1:
		/* This is the DMA unit found on SCSI/Ether cards. */
		tmp = sbus_readl(esp->dregs + DMA_CSR);
		tmp |= DMA_ADD_ENABLE;
		tmp &= ~DMA_BCNT_ENAB;
		if (!can_do_burst32 && can_do_burst16) {
			tmp |= DMA_ESC_BURST;
		} else {
			tmp &= ~(DMA_ESC_BURST);
		}
		sbus_writel(tmp, esp->dregs + DMA_CSR);
		break;
	default:
		break;
	};
	ESP_INTSON(esp->dregs);
}

/* Reset the ESP chip, _not_ the SCSI bus. */
static void __init esp_reset_esp(struct esp *esp)
{
	u8 family_code, version;
	int i;

	/* Now reset the ESP chip */
	esp_cmd(esp, ESP_CMD_RC);
	esp_cmd(esp, ESP_CMD_NULL | ESP_CMD_DMA);
	esp_cmd(esp, ESP_CMD_NULL | ESP_CMD_DMA);

	/* Reload the configuration registers */
	sbus_writeb(esp->cfact, esp->eregs + ESP_CFACT);
	esp->prev_stp = 0;
	sbus_writeb(esp->prev_stp, esp->eregs + ESP_STP);
	esp->prev_soff = 0;
	sbus_writeb(esp->prev_soff, esp->eregs + ESP_SOFF);
	sbus_writeb(esp->neg_defp, esp->eregs + ESP_TIMEO);

	/* This is the only point at which it is reliable to read
	 * the ID-code for a fast ESP chip variants.
	 */
	esp->max_period = ((35 * esp->ccycle) / 1000);
	if (esp->erev == fast) {
		version = sbus_readb(esp->eregs + ESP_UID);
		family_code = (version & 0xf8) >> 3;
		if (family_code == 0x02)
			esp->erev = fas236;
		else if (family_code == 0x0a)
			esp->erev = fashme; /* Version is usually '5'. */
		else
			esp->erev = fas100a;
		ESPMISC(("esp%d: FAST chip is %s (family=%d, version=%d)\n",
			 esp->esp_id,
			 (esp->erev == fas236) ? "fas236" :
			 ((esp->erev == fas100a) ? "fas100a" :
			  "fasHME"), family_code, (version & 7)));

		esp->min_period = ((4 * esp->ccycle) / 1000);
	} else {
		esp->min_period = ((5 * esp->ccycle) / 1000);
	}
	esp->max_period = (esp->max_period + 3)>>2;
	esp->min_period = (esp->min_period + 3)>>2;

	sbus_writeb(esp->config1, esp->eregs + ESP_CFG1);
	switch (esp->erev) {
	case esp100:
		/* nothing to do */
		break;
	case esp100a:
		sbus_writeb(esp->config2, esp->eregs + ESP_CFG2);
		break;
	case esp236:
		/* Slow 236 */
		sbus_writeb(esp->config2, esp->eregs + ESP_CFG2);
		esp->prev_cfg3 = esp->config3[0];
		sbus_writeb(esp->prev_cfg3, esp->eregs + ESP_CFG3);
		break;
	case fashme:
		esp->config2 |= (ESP_CONFIG2_HME32 | ESP_CONFIG2_HMEFENAB);
		/* fallthrough... */
	case fas236:
		/* Fast 236 or HME */
		sbus_writeb(esp->config2, esp->eregs + ESP_CFG2);
		for (i = 0; i < 16; i++) {
			if (esp->erev == fashme) {
				u8 cfg3;

				cfg3 = ESP_CONFIG3_FCLOCK | ESP_CONFIG3_OBPUSH;
				if (esp->scsi_id >= 8)
					cfg3 |= ESP_CONFIG3_IDBIT3;
				esp->config3[i] |= cfg3;
			} else {
				esp->config3[i] |= ESP_CONFIG3_FCLK;
			}
		}
		esp->prev_cfg3 = esp->config3[0];
		sbus_writeb(esp->prev_cfg3, esp->eregs + ESP_CFG3);
		if (esp->erev == fashme) {
			esp->radelay = 80;
		} else {
			if (esp->diff)
				esp->radelay = 0;
			else
				esp->radelay = 96;
		}
		break;
	case fas100a:
		/* Fast 100a */
		sbus_writeb(esp->config2, esp->eregs + ESP_CFG2);
		for (i = 0; i < 16; i++)
			esp->config3[i] |= ESP_CONFIG3_FCLOCK;
		esp->prev_cfg3 = esp->config3[0];
		sbus_writeb(esp->prev_cfg3, esp->eregs + ESP_CFG3);
		esp->radelay = 32;
		break;
	default:
		panic("esp: what could it be... I wonder...");
		break;
	};

	/* Eat any bitrot in the chip */
	sbus_readb(esp->eregs + ESP_INTRPT);
	udelay(100);
}

/* This places the ESP into a known state at boot time. */
static void __init esp_bootup_reset(struct esp *esp)
{
	u8 tmp;

	/* Reset the DMA */
	esp_reset_dma(esp);

	/* Reset the ESP */
	esp_reset_esp(esp);

	/* Reset the SCSI bus, but tell ESP not to generate an irq */
	tmp = sbus_readb(esp->eregs + ESP_CFG1);
	tmp |= ESP_CONFIG1_SRRDISAB;
	sbus_writeb(tmp, esp->eregs + ESP_CFG1);

	esp_cmd(esp, ESP_CMD_RS);
	udelay(400);

	sbus_writeb(esp->config1, esp->eregs + ESP_CFG1);

	/* Eat any bitrot in the chip and we are done... */
	sbus_readb(esp->eregs + ESP_INTRPT);
}

static void esp_chain_add(struct esp *esp)
{
	spin_lock_irq(&espchain_lock);
	if (espchain) {
		struct esp *elink = espchain;
		while (elink->next)
			elink = elink->next;
		elink->next = esp;
	} else {
		espchain = esp;
	}
	esp->next = NULL;
	spin_unlock_irq(&espchain_lock);
}

static void esp_chain_del(struct esp *esp)
{
	spin_lock_irq(&espchain_lock);
	if (espchain == esp) {
		espchain = esp->next;
	} else {
		struct esp *elink = espchain;
		while (elink->next != esp)
			elink = elink->next;
		elink->next = esp->next;
	}
	esp->next = NULL;
	spin_unlock_irq(&espchain_lock);
}

static int __init esp_find_dvma(struct esp *esp, struct sbus_dev *dma_sdev)
{
	struct sbus_dev *sdev = esp->sdev;
	struct sbus_dma *dma;

	if (dma_sdev != NULL) {
		for_each_dvma(dma) {
			if (dma->sdev == dma_sdev)
				break;
		}
	} else {
		for_each_dvma(dma) {
			/* If allocated already, can't use it. */
			if (dma->allocated)
				continue;

			if (dma->sdev == NULL)
				break;

			/* If bus + slot are the same and it has the
			 * correct OBP name, it's ours.
			 */
			if (sdev->bus == dma->sdev->bus &&
			    sdev->slot == dma->sdev->slot &&
			    (!strcmp(dma->sdev->prom_name, "dma") ||
			     !strcmp(dma->sdev->prom_name, "espdma")))
				break;
		}
	}

	/* If we don't know how to handle the dvma,
	 * do not use this device.
	 */
	if (dma == NULL) {
		printk("Cannot find dvma for ESP%d's SCSI\n", esp->esp_id);
		return -1;
	}
	if (dma->allocated) {
		printk("esp%d: can't use my espdma\n", esp->esp_id);
		return -1;
	}
	dma->allocated = 1;
	esp->dma = dma;
	esp->dregs = dma->regs;

	return 0;
}

static int __init esp_map_regs(struct esp *esp, int hme)
{
	struct sbus_dev *sdev = esp->sdev;
	struct resource *res;

	/* On HME, two reg sets exist, first is DVMA,
	 * second is ESP registers.
	 */
	if (hme)
		res = &sdev->resource[1];
	else
		res = &sdev->resource[0];

	esp->eregs = sbus_ioremap(res, 0, ESP_REG_SIZE, "ESP Registers");

	if (esp->eregs == 0)
		return -1;
	return 0;
}

static int __init esp_map_cmdarea(struct esp *esp)
{
	struct sbus_dev *sdev = esp->sdev;

	esp->esp_command = sbus_alloc_consistent(sdev, 16,
						 &esp->esp_command_dvma);
	if (esp->esp_command == NULL ||
	    esp->esp_command_dvma == 0)
		return -1;
	return 0;
}

static int __init esp_register_irq(struct esp *esp)
{
	esp->ehost->irq = esp->irq = esp->sdev->irqs[0];

	/* We used to try various overly-clever things to
	 * reduce the interrupt processing overhead on
	 * sun4c/sun4m when multiple ESP's shared the
	 * same IRQ.  It was too complex and messy to
	 * sanely maintain.
	 */
	if (request_irq(esp->ehost->irq, esp_intr,
			SA_SHIRQ, "ESP SCSI", esp)) {
		printk("esp%d: Cannot acquire irq line\n",
		       esp->esp_id);
		return -1;
	}

	printk("esp%d: IRQ %s ", esp->esp_id,
	       __irq_itoa(esp->ehost->irq));

	return 0;
}

static void __init esp_get_scsi_id(struct esp *esp)
{
	struct sbus_dev *sdev = esp->sdev;

	esp->scsi_id = prom_getintdefault(esp->prom_node,
					  "initiator-id",
					  -1);
	if (esp->scsi_id == -1)
		esp->scsi_id = prom_getintdefault(esp->prom_node,
						  "scsi-initiator-id",
						  -1);
	if (esp->scsi_id == -1)
		esp->scsi_id = (sdev->bus == NULL) ? 7 :
			prom_getintdefault(sdev->bus->prom_node,
					   "scsi-initiator-id",
					   7);
	esp->ehost->this_id = esp->scsi_id;
	esp->scsi_id_mask = (1 << esp->scsi_id);

}

static void __init esp_get_clock_params(struct esp *esp)
{
	struct sbus_dev *sdev = esp->sdev;
	int prom_node = esp->prom_node;
	int sbus_prom_node;
	unsigned int fmhz;
	u8 ccf;

	if (sdev != NULL && sdev->bus != NULL)
		sbus_prom_node = sdev->bus->prom_node;
	else
		sbus_prom_node = 0;

	/* This is getting messy but it has to be done
	 * correctly or else you get weird behavior all
	 * over the place.  We are trying to basically
	 * figure out three pieces of information.
	 *
	 * a) Clock Conversion Factor
	 *
	 *    This is a representation of the input
	 *    crystal clock frequency going into the
	 *    ESP on this machine.  Any operation whose
	 *    timing is longer than 400ns depends on this
	 *    value being correct.  For example, you'll
	 *    get blips for arbitration/selection during
	 *    high load or with multiple targets if this
	 *    is not set correctly.
	 *
	 * b) Selection Time-Out
	 *
	 *    The ESP isn't very bright and will arbitrate
	 *    for the bus and try to select a target
	 *    forever if you let it.  This value tells
	 *    the ESP when it has taken too long to
	 *    negotiate and that it should interrupt
	 *    the CPU so we can see what happened.
	 *    The value is computed as follows (from
	 *    NCR/Symbios chip docs).
	 *
	 *          (Time Out Period) *  (Input Clock)
	 *    STO = ----------------------------------
	 *          (8192) * (Clock Conversion Factor)
	 *
	 *    You usually want the time out period to be
	 *    around 250ms, I think we'll set it a little
	 *    bit higher to account for fully loaded SCSI
	 *    bus's and slow devices that don't respond so
	 *    quickly to selection attempts. (yeah, I know
	 *    this is out of spec. but there is a lot of
	 *    buggy pieces of firmware out there so bite me)
	 *
	 * c) Imperical constants for synchronous offset
	 *    and transfer period register values
	 *
	 *    This entails the smallest and largest sync
	 *    period we could ever handle on this ESP.
	 */

	fmhz = prom_getintdefault(prom_node, "clock-frequency", -1);
	if (fmhz == -1)
		fmhz = (!sbus_prom_node) ? 0 :
			prom_getintdefault(sbus_prom_node, "clock-frequency", -1);

	if (fmhz <= (5000000))
		ccf = 0;
	else
		ccf = (((5000000 - 1) + (fmhz))/(5000000));

	if (!ccf || ccf > 8) {
		/* If we can't find anything reasonable,
		 * just assume 20MHZ.  This is the clock
		 * frequency of the older sun4c's where I've
		 * been unable to find the clock-frequency
		 * PROM property.  All other machines provide
		 * useful values it seems.
		 */
		ccf = ESP_CCF_F4;
		fmhz = (20000000);
	}

	if (ccf == (ESP_CCF_F7 + 1))
		esp->cfact = ESP_CCF_F0;
	else if (ccf == ESP_CCF_NEVER)
		esp->cfact = ESP_CCF_F2;
	else
		esp->cfact = ccf;
	esp->raw_cfact = ccf;

	esp->cfreq = fmhz;
	esp->ccycle = ESP_MHZ_TO_CYCLE(fmhz);
	esp->ctick = ESP_TICK(ccf, esp->ccycle);
	esp->neg_defp = ESP_NEG_DEFP(fmhz, ccf);
	esp->sync_defp = SYNC_DEFP_SLOW;

	printk("SCSI ID %d Clk %dMHz CCYC=%d CCF=%d TOut %d ",
	       esp->scsi_id, (fmhz / 1000000),
	       (int)esp->ccycle, (int)ccf, (int) esp->neg_defp);
}

static void __init esp_get_bursts(struct esp *esp, struct sbus_dev *dma)
{
	struct sbus_dev *sdev = esp->sdev;
	u8 bursts;

	bursts = prom_getintdefault(esp->prom_node, "burst-sizes", 0xff);

	if (dma) {
		u8 tmp = prom_getintdefault(dma->prom_node,
					    "burst-sizes", 0xff);
		if (tmp != 0xff)
			bursts &= tmp;
	}

	if (sdev->bus) {
		u8 tmp = prom_getintdefault(sdev->bus->prom_node,
					    "burst-sizes", 0xff);
		if (tmp != 0xff)
			bursts &= tmp;
	}

	if (bursts == 0xff ||
	    (bursts & DMA_BURST16) == 0 ||
	    (bursts & DMA_BURST32) == 0)
		bursts = (DMA_BURST32 - 1);

	esp->bursts = bursts;
}

static void __init esp_get_revision(struct esp *esp)
{
	u8 tmp;

	esp->config1 = (ESP_CONFIG1_PENABLE | (esp->scsi_id & 7));
	esp->config2 = (ESP_CONFIG2_SCSI2ENAB | ESP_CONFIG2_REGPARITY);
	sbus_writeb(esp->config2, esp->eregs + ESP_CFG2);

	tmp = sbus_readb(esp->eregs + ESP_CFG2);
	tmp &= ~ESP_CONFIG2_MAGIC;
	if (tmp != (ESP_CONFIG2_SCSI2ENAB | ESP_CONFIG2_REGPARITY)) {
		/* If what we write to cfg2 does not come back, cfg2
		 * is not implemented, therefore this must be a plain
		 * esp100.
		 */
		esp->erev = esp100;
		printk("NCR53C90(esp100)\n");
	} else {
		esp->config2 = 0;
		esp->prev_cfg3 = esp->config3[0] = 5;
		sbus_writeb(esp->config2, esp->eregs + ESP_CFG2);
		sbus_writeb(0, esp->eregs + ESP_CFG3);
		sbus_writeb(esp->prev_cfg3, esp->eregs + ESP_CFG3);

		tmp = sbus_readb(esp->eregs + ESP_CFG3);
		if (tmp != 5) {
			/* The cfg2 register is implemented, however
			 * cfg3 is not, must be esp100a.
			 */
			esp->erev = esp100a;
			printk("NCR53C90A(esp100a)\n");
		} else {
			int target;

			for (target = 0; target < 16; target++)
				esp->config3[target] = 0;
			esp->prev_cfg3 = 0;
			sbus_writeb(esp->prev_cfg3, esp->eregs + ESP_CFG3);

			/* All of cfg{1,2,3} implemented, must be one of
			 * the fas variants, figure out which one.
			 */
			if (esp->raw_cfact > ESP_CCF_F5) {
				esp->erev = fast;
				esp->sync_defp = SYNC_DEFP_FAST;
				printk("NCR53C9XF(espfast)\n");
			} else {
				esp->erev = esp236;
				printk("NCR53C9x(esp236)\n");
			}
			esp->config2 = 0;
			sbus_writeb(esp->config2, esp->eregs + ESP_CFG2);
		}
	}
}

static void __init esp_init_swstate(struct esp *esp)
{
	int i;

	/* Command queues... */
	esp->current_SC = NULL;
	esp->disconnected_SC = NULL;
	esp->issue_SC = NULL;

	/* Target and current command state... */
	esp->targets_present = 0;
	esp->resetting_bus = 0;
	esp->snip = 0;

	init_waitqueue_head(&esp->reset_queue);

	/* Debugging... */
	for(i = 0; i < 32; i++)
		esp->espcmdlog[i] = 0;
	esp->espcmdent = 0;

	/* MSG phase state... */
	for(i = 0; i < 16; i++) {
		esp->cur_msgout[i] = 0;
		esp->cur_msgin[i] = 0;
	}
	esp->prevmsgout = esp->prevmsgin = 0;
	esp->msgout_len = esp->msgin_len = 0;

	/* Clear the one behind caches to hold unmatchable values. */
	esp->prev_soff = esp->prev_stp = esp->prev_cfg3 = 0xff;
	esp->prev_hme_dmacsr = 0xffffffff;
}

static int __init detect_one_esp(struct scsi_host_template *tpnt, struct sbus_dev *esp_dev,
				 struct sbus_dev *espdma, struct sbus_bus *sbus,
				 int id, int hme)
{
	struct Scsi_Host *esp_host = scsi_register(tpnt, sizeof(struct esp));
	struct esp *esp;
	
	if (!esp_host) {
		printk("ESP: Cannot register SCSI host\n");
		return -1;
	}
	if (hme)
		esp_host->max_id = 16;
	esp = (struct esp *) esp_host->hostdata;
	esp->ehost = esp_host;
	esp->sdev = esp_dev;
	esp->esp_id = id;
	esp->prom_node = esp_dev->prom_node;
	prom_getstring(esp->prom_node, "name", esp->prom_name,
		       sizeof(esp->prom_name));

	esp_chain_add(esp);
	if (esp_find_dvma(esp, espdma) < 0)
		goto fail_unlink;
	if (esp_map_regs(esp, hme) < 0) {
		printk("ESP registers unmappable");
		goto fail_dvma_release;
	}
	if (esp_map_cmdarea(esp) < 0) {
		printk("ESP DVMA transport area unmappable");
		goto fail_unmap_regs;
	}
	if (esp_register_irq(esp) < 0)
		goto fail_unmap_cmdarea;

	esp_get_scsi_id(esp);

	esp->diff = prom_getbool(esp->prom_node, "differential");
	if (esp->diff)
		printk("Differential ");

	esp_get_clock_params(esp);
	esp_get_bursts(esp, espdma);
	esp_get_revision(esp);
	esp_init_swstate(esp);

	esp_bootup_reset(esp);

	return 0;

fail_unmap_cmdarea:
	sbus_free_consistent(esp->sdev, 16,
			     (void *) esp->esp_command,
			     esp->esp_command_dvma);

fail_unmap_regs:
	sbus_iounmap(esp->eregs, ESP_REG_SIZE);

fail_dvma_release:
	esp->dma->allocated = 0;

fail_unlink:
	esp_chain_del(esp);
	scsi_unregister(esp_host);
	return -1;
}

/* Detecting ESP chips on the machine.  This is the simple and easy
 * version.
 */

#ifdef CONFIG_SUN4

#include <asm/sun4paddr.h>

static int __init esp_detect(struct scsi_host_template *tpnt)
{
	static struct sbus_dev esp_dev;
	int esps_in_use = 0;

	espchain = NULL;

	if (sun4_esp_physaddr) {
		memset (&esp_dev, 0, sizeof(esp_dev));
		esp_dev.reg_addrs[0].phys_addr = sun4_esp_physaddr;
		esp_dev.irqs[0] = 4;
		esp_dev.resource[0].start = sun4_esp_physaddr;
		esp_dev.resource[0].end = sun4_esp_physaddr + ESP_REG_SIZE - 1;
		esp_dev.resource[0].flags = IORESOURCE_IO;

		if (!detect_one_esp(tpnt, &esp_dev, NULL, NULL, 0, 0))
			esps_in_use++;
		printk("ESP: Total of 1 ESP hosts found, %d actually in use.\n", esps_in_use);
		esps_running =  esps_in_use;
	}
	return esps_in_use;
}

#else /* !CONFIG_SUN4 */

static int __init esp_detect(struct scsi_host_template *tpnt)
{
	struct sbus_bus *sbus;
	struct sbus_dev *esp_dev, *sbdev_iter;
	int nesps = 0, esps_in_use = 0;

	espchain = 0;
	if (!sbus_root) {
#ifdef CONFIG_PCI
		return 0;
#else
		panic("No SBUS in esp_detect()");
#endif
	}
	for_each_sbus(sbus) {
		for_each_sbusdev(sbdev_iter, sbus) {
			struct sbus_dev *espdma = NULL;
			int hme = 0;

			/* Is it an esp sbus device? */
			esp_dev = sbdev_iter;
			if (strcmp(esp_dev->prom_name, "esp") &&
			    strcmp(esp_dev->prom_name, "SUNW,esp")) {
				if (!strcmp(esp_dev->prom_name, "SUNW,fas")) {
					hme = 1;
					espdma = esp_dev;
				} else {
					if (!esp_dev->child ||
					    (strcmp(esp_dev->prom_name, "espdma") &&
					     strcmp(esp_dev->prom_name, "dma")))
						continue; /* nope... */
					espdma = esp_dev;
					esp_dev = esp_dev->child;
					if (strcmp(esp_dev->prom_name, "esp") &&
					    strcmp(esp_dev->prom_name, "SUNW,esp"))
						continue; /* how can this happen? */
				}
			}
			
			if (detect_one_esp(tpnt, esp_dev, espdma, sbus, nesps++, hme) < 0)
				continue;
				
			esps_in_use++;
		} /* for each sbusdev */
	} /* for each sbus */
	printk("ESP: Total of %d ESP hosts found, %d actually in use.\n", nesps,
	       esps_in_use);
	esps_running = esps_in_use;
	return esps_in_use;
}

#endif /* !CONFIG_SUN4 */

/*
 */
static int esp_release(struct Scsi_Host *host)
{
	struct esp *esp = (struct esp *) host->hostdata;

	ESP_INTSOFF(esp->dregs);
#if 0
	esp_reset_dma(esp);
	esp_reset_esp(esp);
#endif

	free_irq(esp->ehost->irq, esp);
	sbus_free_consistent(esp->sdev, 16,
			     (void *) esp->esp_command, esp->esp_command_dvma);
	sbus_iounmap(esp->eregs, ESP_REG_SIZE);
	esp->dma->allocated = 0;
	esp_chain_del(esp);

        return 0;
}

/* The info function will return whatever useful
 * information the developer sees fit.  If not provided, then
 * the name field will be used instead.
 */
static const char *esp_info(struct Scsi_Host *host)
{
	struct esp *esp;

	esp = (struct esp *) host->hostdata;
	switch (esp->erev) {
	case esp100:
		return "Sparc ESP100 (NCR53C90)";
	case esp100a:
		return "Sparc ESP100A (NCR53C90A)";
	case esp236:
		return "Sparc ESP236";
	case fas236:
		return "Sparc ESP236-FAST";
	case fashme:
		return "Sparc ESP366-HME";
	case fas100a:
		return "Sparc ESP100A-FAST";
	default:
		return "Bogon ESP revision";
	};
}

/* From Wolfgang Stanglmeier's NCR scsi driver. */
struct info_str
{
	char *buffer;
	int length;
	int offset;
	int pos;
};

static void copy_mem_info(struct info_str *info, char *data, int len)
{
	if (info->pos + len > info->length)
		len = info->length - info->pos;

	if (info->pos + len < info->offset) {
		info->pos += len;
		return;
	}
	if (info->pos < info->offset) {
		data += (info->offset - info->pos);
		len  -= (info->offset - info->pos);
	}

	if (len > 0) {
		memcpy(info->buffer + info->pos, data, len);
		info->pos += len;
	}
}

static int copy_info(struct info_str *info, char *fmt, ...)
{
	va_list args;
	char buf[81];
	int len;

	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);

	copy_mem_info(info, buf, len);
	return len;
}

static int esp_host_info(struct esp *esp, char *ptr, off_t offset, int len)
{
	struct scsi_device *sdev;
	struct info_str info;
	int i;

	info.buffer	= ptr;
	info.length	= len;
	info.offset	= offset;
	info.pos	= 0;

	copy_info(&info, "Sparc ESP Host Adapter:\n");
	copy_info(&info, "\tPROM node\t\t%08x\n", (unsigned int) esp->prom_node);
	copy_info(&info, "\tPROM name\t\t%s\n", esp->prom_name);
	copy_info(&info, "\tESP Model\t\t");
	switch (esp->erev) {
	case esp100:
		copy_info(&info, "ESP100\n");
		break;
	case esp100a:
		copy_info(&info, "ESP100A\n");
		break;
	case esp236:
		copy_info(&info, "ESP236\n");
		break;
	case fas236:
		copy_info(&info, "FAS236\n");
		break;
	case fas100a:
		copy_info(&info, "FAS100A\n");
		break;
	case fast:
		copy_info(&info, "FAST\n");
		break;
	case fashme:
		copy_info(&info, "Happy Meal FAS\n");
		break;
	case espunknown:
	default:
		copy_info(&info, "Unknown!\n");
		break;
	};
	copy_info(&info, "\tDMA Revision\t\t");
	switch (esp->dma->revision) {
	case dvmarev0:
		copy_info(&info, "Rev 0\n");
		break;
	case dvmaesc1:
		copy_info(&info, "ESC Rev 1\n");
		break;
	case dvmarev1:
		copy_info(&info, "Rev 1\n");
		break;
	case dvmarev2:
		copy_info(&info, "Rev 2\n");
		break;
	case dvmarev3:
		copy_info(&info, "Rev 3\n");
		break;
	case dvmarevplus:
		copy_info(&info, "Rev 1+\n");
		break;
	case dvmahme:
		copy_info(&info, "Rev HME/FAS\n");
		break;
	default:
		copy_info(&info, "Unknown!\n");
		break;
	};
	copy_info(&info, "\tLive Targets\t\t[ ");
	for (i = 0; i < 15; i++) {
		if (esp->targets_present & (1 << i))
			copy_info(&info, "%d ", i);
	}
	copy_info(&info, "]\n\n");
	
	/* Now describe the state of each existing target. */
	copy_info(&info, "Target #\tconfig3\t\tSync Capabilities\tDisconnect\tWide\n");

	shost_for_each_device(sdev, esp->ehost) {
		struct esp_device *esp_dev = sdev->hostdata;
		uint id = sdev->id;

		if (!(esp->targets_present & (1 << id)))
			continue;

		copy_info(&info, "%d\t\t", id);
		copy_info(&info, "%08lx\t", esp->config3[id]);
		copy_info(&info, "[%02lx,%02lx]\t\t\t",
			esp_dev->sync_max_offset,
			esp_dev->sync_min_period);
		copy_info(&info, "%s\t\t",
			esp_dev->disconnect ? "yes" : "no");
		copy_info(&info, "%s\n",
			(esp->config3[id] & ESP_CONFIG3_EWIDE) ? "yes" : "no");
	}
	return info.pos > info.offset? info.pos - info.offset : 0;
}

/* ESP proc filesystem code. */
static int esp_proc_info(struct Scsi_Host *host, char *buffer, char **start, off_t offset,
			 int length, int inout)
{
	struct esp *esp;

	if (inout)
		return -EINVAL; /* not yet */

	for_each_esp(esp) {
		if (esp->ehost == host)
			break;
	}
	if (!esp)
		return -EINVAL;

	if (start)
		*start = buffer;

	return esp_host_info(esp, buffer, offset, length);
}

static void esp_get_dmabufs(struct esp *esp, struct scsi_cmnd *sp)
{
	if (sp->use_sg == 0) {
		sp->SCp.this_residual = sp->request_bufflen;
		sp->SCp.buffer = (struct scatterlist *) sp->request_buffer;
		sp->SCp.buffers_residual = 0;
		if (sp->request_bufflen) {
			sp->SCp.have_data_in = sbus_map_single(esp->sdev, sp->SCp.buffer,
							       sp->SCp.this_residual,
							       sp->sc_data_direction);
			sp->SCp.ptr = (char *) ((unsigned long)sp->SCp.have_data_in);
		} else {
			sp->SCp.ptr = NULL;
		}
	} else {
		sp->SCp.buffer = (struct scatterlist *) sp->buffer;
		sp->SCp.buffers_residual = sbus_map_sg(esp->sdev,
						       sp->SCp.buffer,
						       sp->use_sg,
						       sp->sc_data_direction);
		sp->SCp.this_residual = sg_dma_len(sp->SCp.buffer);
		sp->SCp.ptr = (char *) ((unsigned long)sg_dma_address(sp->SCp.buffer));
	}
}

static void esp_release_dmabufs(struct esp *esp, struct scsi_cmnd *sp)
{
	if (sp->use_sg) {
		sbus_unmap_sg(esp->sdev, sp->buffer, sp->use_sg,
			      sp->sc_data_direction);
	} else if (sp->request_bufflen) {
		sbus_unmap_single(esp->sdev,
				  sp->SCp.have_data_in,
				  sp->request_bufflen,
				  sp->sc_data_direction);
	}
}

static void esp_restore_pointers(struct esp *esp, struct scsi_cmnd *sp)
{
	struct esp_pointers *ep = &esp->data_pointers[sp->device->id];

	sp->SCp.ptr = ep->saved_ptr;
	sp->SCp.buffer = ep->saved_buffer;
	sp->SCp.this_residual = ep->saved_this_residual;
	sp->SCp.buffers_residual = ep->saved_buffers_residual;
}

static void esp_save_pointers(struct esp *esp, struct scsi_cmnd *sp)
{
	struct esp_pointers *ep = &esp->data_pointers[sp->device->id];

	ep->saved_ptr = sp->SCp.ptr;
	ep->saved_buffer = sp->SCp.buffer;
	ep->saved_this_residual = sp->SCp.this_residual;
	ep->saved_buffers_residual = sp->SCp.buffers_residual;
}

/* Some rules:
 *
 *   1) Never ever panic while something is live on the bus.
 *      If there is to be any chance of syncing the disks this
 *      rule is to be obeyed.
 *
 *   2) Any target that causes a foul condition will no longer
 *      have synchronous transfers done to it, no questions
 *      asked.
 *
 *   3) Keep register accesses to a minimum.  Think about some
 *      day when we have Xbus machines this is running on and
 *      the ESP chip is on the other end of the machine on a
 *      different board from the cpu where this is running.
 */

/* Fire off a command.  We assume the bus is free and that the only
 * case where we could see an interrupt is where we have disconnected
 * commands active and they are trying to reselect us.
 */
static inline void esp_check_cmd(struct esp *esp, struct scsi_cmnd *sp)
{
	switch (sp->cmd_len) {
	case 6:
	case 10:
	case 12:
		esp->esp_slowcmd = 0;
		break;

	default:
		esp->esp_slowcmd = 1;
		esp->esp_scmdleft = sp->cmd_len;
		esp->esp_scmdp = &sp->cmnd[0];
		break;
	};
}

static inline void build_sync_nego_msg(struct esp *esp, int period, int offset)
{
	esp->cur_msgout[0] = EXTENDED_MESSAGE;
	esp->cur_msgout[1] = 3;
	esp->cur_msgout[2] = EXTENDED_SDTR;
	esp->cur_msgout[3] = period;
	esp->cur_msgout[4] = offset;
	esp->msgout_len = 5;
}

/* SIZE is in bits, currently HME only supports 16 bit wide transfers. */
static inline void build_wide_nego_msg(struct esp *esp, int size)
{
	esp->cur_msgout[0] = EXTENDED_MESSAGE;
	esp->cur_msgout[1] = 2;
	esp->cur_msgout[2] = EXTENDED_WDTR;
	switch (size) {
	case 32:
		esp->cur_msgout[3] = 2;
		break;
	case 16:
		esp->cur_msgout[3] = 1;
		break;
	case 8:
	default:
		esp->cur_msgout[3] = 0;
		break;
	};

	esp->msgout_len = 4;
}

static void esp_exec_cmd(struct esp *esp)
{
	struct scsi_cmnd *SCptr;
	struct scsi_device *SDptr;
	struct esp_device *esp_dev;
	volatile u8 *cmdp = esp->esp_command;
	u8 the_esp_command;
	int lun, target;
	int i;

	/* Hold off if we have disconnected commands and
	 * an IRQ is showing...
	 */
	if (esp->disconnected_SC && ESP_IRQ_P(esp->dregs))
		return;

	/* Grab first member of the issue queue. */
	SCptr = esp->current_SC = remove_first_SC(&esp->issue_SC);

	/* Safe to panic here because current_SC is null. */
	if (!SCptr)
		panic("esp: esp_exec_cmd and issue queue is NULL");

	SDptr = SCptr->device;
	esp_dev = SDptr->hostdata;
	lun = SCptr->device->lun;
	target = SCptr->device->id;

	esp->snip = 0;
	esp->msgout_len = 0;

	/* Send it out whole, or piece by piece?   The ESP
	 * only knows how to automatically send out 6, 10,
	 * and 12 byte commands.  I used to think that the
	 * Linux SCSI code would never throw anything other
	 * than that to us, but then again there is the
	 * SCSI generic driver which can send us anything.
	 */
	esp_check_cmd(esp, SCptr);

	/* If arbitration/selection is successful, the ESP will leave
	 * ATN asserted, causing the target to go into message out
	 * phase.  The ESP will feed the target the identify and then
	 * the target can only legally go to one of command,
	 * datain/out, status, or message in phase, or stay in message
	 * out phase (should we be trying to send a sync negotiation
	 * message after the identify).  It is not allowed to drop
	 * BSY, but some buggy targets do and we check for this
	 * condition in the selection complete code.  Most of the time
	 * we'll make the command bytes available to the ESP and it
	 * will not interrupt us until it finishes command phase, we
	 * cannot do this for command sizes the ESP does not
	 * understand and in this case we'll get interrupted right
	 * when the target goes into command phase.
	 *
	 * It is absolutely _illegal_ in the presence of SCSI-2 devices
	 * to use the ESP select w/o ATN command.  When SCSI-2 devices are
	 * present on the bus we _must_ always go straight to message out
	 * phase with an identify message for the target.  Being that
	 * selection attempts in SCSI-1 w/o ATN was an option, doing SCSI-2
	 * selections should not confuse SCSI-1 we hope.
	 */

	if (esp_dev->sync) {
		/* this targets sync is known */
#ifndef __sparc_v9__
do_sync_known:
#endif
		if (esp_dev->disconnect)
			*cmdp++ = IDENTIFY(1, lun);
		else
			*cmdp++ = IDENTIFY(0, lun);

		if (esp->esp_slowcmd) {
			the_esp_command = (ESP_CMD_SELAS | ESP_CMD_DMA);
			esp_advance_phase(SCptr, in_slct_stop);
		} else {
			the_esp_command = (ESP_CMD_SELA | ESP_CMD_DMA);
			esp_advance_phase(SCptr, in_slct_norm);
		}
	} else if (!(esp->targets_present & (1<<target)) || !(esp_dev->disconnect)) {
		/* After the bootup SCSI code sends both the
		 * TEST_UNIT_READY and INQUIRY commands we want
		 * to at least attempt allowing the device to
		 * disconnect.
		 */
		ESPMISC(("esp: Selecting device for first time. target=%d "
			 "lun=%d\n", target, SCptr->device->lun));
		if (!SDptr->borken && !esp_dev->disconnect)
			esp_dev->disconnect = 1;

		*cmdp++ = IDENTIFY(0, lun);
		esp->prevmsgout = NOP;
		esp_advance_phase(SCptr, in_slct_norm);
		the_esp_command = (ESP_CMD_SELA | ESP_CMD_DMA);

		/* Take no chances... */
		esp_dev->sync_max_offset = 0;
		esp_dev->sync_min_period = 0;
	} else {
		/* Sorry, I have had way too many problems with
		 * various CDROM devices on ESP. -DaveM
		 */
		int cdrom_hwbug_wkaround = 0;

#ifndef __sparc_v9__
		/* Never allow disconnects or synchronous transfers on
		 * SparcStation1 and SparcStation1+.  Allowing those
		 * to be enabled seems to lockup the machine completely.
		 */
		if ((idprom->id_machtype == (SM_SUN4C | SM_4C_SS1)) ||
		    (idprom->id_machtype == (SM_SUN4C | SM_4C_SS1PLUS))) {
			/* But we are nice and allow tapes and removable
			 * disks (but not CDROMs) to disconnect.
			 */
			if(SDptr->type == TYPE_TAPE ||
			   (SDptr->type != TYPE_ROM && SDptr->removable))
				esp_dev->disconnect = 1;
			else
				esp_dev->disconnect = 0;
			esp_dev->sync_max_offset = 0;
			esp_dev->sync_min_period = 0;
			esp_dev->sync = 1;
			esp->snip = 0;
			goto do_sync_known;
		}
#endif /* !(__sparc_v9__) */

		/* We've talked to this guy before,
		 * but never negotiated.  Let's try,
		 * need to attempt WIDE first, before
		 * sync nego, as per SCSI 2 standard.
		 */
		if (esp->erev == fashme && !esp_dev->wide) {
			if (!SDptr->borken &&
			   SDptr->type != TYPE_ROM &&
			   SDptr->removable == 0) {
				build_wide_nego_msg(esp, 16);
				esp_dev->wide = 1;
				esp->wnip = 1;
				goto after_nego_msg_built;
			} else {
				esp_dev->wide = 1;
				/* Fall through and try sync. */
			}
		}

		if (!SDptr->borken) {
			if ((SDptr->type == TYPE_ROM)) {
				/* Nice try sucker... */
				ESPMISC(("esp%d: Disabling sync for buggy "
					 "CDROM.\n", esp->esp_id));
				cdrom_hwbug_wkaround = 1;
				build_sync_nego_msg(esp, 0, 0);
			} else if (SDptr->removable != 0) {
				ESPMISC(("esp%d: Not negotiating sync/wide but "
					 "allowing disconnect for removable media.\n",
					 esp->esp_id));
				build_sync_nego_msg(esp, 0, 0);
			} else {
				build_sync_nego_msg(esp, esp->sync_defp, 15);
			}
		} else {
			build_sync_nego_msg(esp, 0, 0);
		}
		esp_dev->sync = 1;
		esp->snip = 1;

after_nego_msg_built:
		/* A fix for broken SCSI1 targets, when they disconnect
		 * they lock up the bus and confuse ESP.  So disallow
		 * disconnects for SCSI1 targets for now until we
		 * find a better fix.
		 *
		 * Addendum: This is funny, I figured out what was going
		 *           on.  The blotzed SCSI1 target would disconnect,
		 *           one of the other SCSI2 targets or both would be
		 *           disconnected as well.  The SCSI1 target would
		 *           stay disconnected long enough that we start
		 *           up a command on one of the SCSI2 targets.  As
		 *           the ESP is arbitrating for the bus the SCSI1
		 *           target begins to arbitrate as well to reselect
		 *           the ESP.  The SCSI1 target refuses to drop it's
		 *           ID bit on the data bus even though the ESP is
		 *           at ID 7 and is the obvious winner for any
		 *           arbitration.  The ESP is a poor sport and refuses
		 *           to lose arbitration, it will continue indefinitely
		 *           trying to arbitrate for the bus and can only be
		 *           stopped via a chip reset or SCSI bus reset.
		 *           Therefore _no_ disconnects for SCSI1 targets
		 *           thank you very much. ;-)
		 */
		if(((SDptr->scsi_level < 3) &&
		    (SDptr->type != TYPE_TAPE) &&
		    SDptr->removable == 0) ||
		    cdrom_hwbug_wkaround || SDptr->borken) {
			ESPMISC((KERN_INFO "esp%d: Disabling DISCONNECT for target %d "
				 "lun %d\n", esp->esp_id, SCptr->device->id, SCptr->device->lun));
			esp_dev->disconnect = 0;
			*cmdp++ = IDENTIFY(0, lun);
		} else {
			*cmdp++ = IDENTIFY(1, lun);
		}

		/* ESP fifo is only so big...
		 * Make this look like a slow command.
		 */
		esp->esp_slowcmd = 1;
		esp->esp_scmdleft = SCptr->cmd_len;
		esp->esp_scmdp = &SCptr->cmnd[0];

		the_esp_command = (ESP_CMD_SELAS | ESP_CMD_DMA);
		esp_advance_phase(SCptr, in_slct_msg);
	}

	if (!esp->esp_slowcmd)
		for (i = 0; i < SCptr->cmd_len; i++)
			*cmdp++ = SCptr->cmnd[i];

	/* HME sucks... */
	if (esp->erev == fashme)
		sbus_writeb((target & 0xf) | (ESP_BUSID_RESELID | ESP_BUSID_CTR32BIT),
			    esp->eregs + ESP_BUSID);
	else
		sbus_writeb(target & 7, esp->eregs + ESP_BUSID);
	if (esp->prev_soff != esp_dev->sync_max_offset ||
	    esp->prev_stp  != esp_dev->sync_min_period ||
	    (esp->erev > esp100a &&
	     esp->prev_cfg3 != esp->config3[target])) {
		esp->prev_soff = esp_dev->sync_max_offset;
		esp->prev_stp = esp_dev->sync_min_period;
		sbus_writeb(esp->prev_soff, esp->eregs + ESP_SOFF);
		sbus_writeb(esp->prev_stp, esp->eregs + ESP_STP);
		if (esp->erev > esp100a) {
			esp->prev_cfg3 = esp->config3[target];
			sbus_writeb(esp->prev_cfg3, esp->eregs + ESP_CFG3);
		}
	}
	i = (cmdp - esp->esp_command);

	if (esp->erev == fashme) {
		esp_cmd(esp, ESP_CMD_FLUSH); /* Grrr! */

		/* Set up the DMA and HME counters */
		sbus_writeb(i, esp->eregs + ESP_TCLOW);
		sbus_writeb(0, esp->eregs + ESP_TCMED);
		sbus_writeb(0, esp->eregs + FAS_RLO);
		sbus_writeb(0, esp->eregs + FAS_RHI);
		esp_cmd(esp, the_esp_command);

		/* Talk about touchy hardware... */
		esp->prev_hme_dmacsr = ((esp->prev_hme_dmacsr |
					 (DMA_SCSI_DISAB | DMA_ENABLE)) &
					~(DMA_ST_WRITE));
		sbus_writel(16, esp->dregs + DMA_COUNT);
		sbus_writel(esp->esp_command_dvma, esp->dregs + DMA_ADDR);
		sbus_writel(esp->prev_hme_dmacsr, esp->dregs + DMA_CSR);
	} else {
		u32 tmp;

		/* Set up the DMA and ESP counters */
		sbus_writeb(i, esp->eregs + ESP_TCLOW);
		sbus_writeb(0, esp->eregs + ESP_TCMED);
		tmp = sbus_readl(esp->dregs + DMA_CSR);
		tmp &= ~DMA_ST_WRITE;
		tmp |= DMA_ENABLE;
		sbus_writel(tmp, esp->dregs + DMA_CSR);
		if (esp->dma->revision == dvmaesc1) {
			if (i) /* Workaround ESC gate array SBUS rerun bug. */
				sbus_writel(PAGE_SIZE, esp->dregs + DMA_COUNT);
		}
		sbus_writel(esp->esp_command_dvma, esp->dregs + DMA_ADDR);

		/* Tell ESP to "go". */
		esp_cmd(esp, the_esp_command);
	}
}

/* Queue a SCSI command delivered from the mid-level Linux SCSI code. */
static int esp_queue(struct scsi_cmnd *SCpnt, void (*done)(struct scsi_cmnd *))
{
	struct esp *esp;

	/* Set up func ptr and initial driver cmd-phase. */
	SCpnt->scsi_done = done;
	SCpnt->SCp.phase = not_issued;

	/* We use the scratch area. */
	ESPQUEUE(("esp_queue: target=%d lun=%d ", SCpnt->device->id, SCpnt->device->lun));
	ESPDISC(("N<%02x,%02x>", SCpnt->device->id, SCpnt->device->lun));

	esp = (struct esp *) SCpnt->device->host->hostdata;
	esp_get_dmabufs(esp, SCpnt);
	esp_save_pointers(esp, SCpnt); /* FIXME for tag queueing */

	SCpnt->SCp.Status           = CHECK_CONDITION;
	SCpnt->SCp.Message          = 0xff;
	SCpnt->SCp.sent_command     = 0;

	/* Place into our queue. */
	if (SCpnt->cmnd[0] == REQUEST_SENSE) {
		ESPQUEUE(("RQSENSE\n"));
		prepend_SC(&esp->issue_SC, SCpnt);
	} else {
		ESPQUEUE(("\n"));
		append_SC(&esp->issue_SC, SCpnt);
	}

	/* Run it now if we can. */
	if (!esp->current_SC && !esp->resetting_bus)
		esp_exec_cmd(esp);

	return 0;
}

/* Dump driver state. */
static void esp_dump_cmd(struct scsi_cmnd *SCptr)
{
	ESPLOG(("[tgt<%02x> lun<%02x> "
		"pphase<%s> cphase<%s>]",
		SCptr->device->id, SCptr->device->lun,
		phase_string(SCptr->SCp.sent_command),
		phase_string(SCptr->SCp.phase)));
}

static void esp_dump_state(struct esp *esp)
{
	struct scsi_cmnd *SCptr = esp->current_SC;
#ifdef DEBUG_ESP_CMDS
	int i;
#endif

	ESPLOG(("esp%d: dumping state\n", esp->esp_id));
	ESPLOG(("esp%d: dma -- cond_reg<%08x> addr<%08x>\n",
		esp->esp_id,
		sbus_readl(esp->dregs + DMA_CSR),
		sbus_readl(esp->dregs + DMA_ADDR)));
	ESPLOG(("esp%d: SW [sreg<%02x> sstep<%02x> ireg<%02x>]\n",
		esp->esp_id, esp->sreg, esp->seqreg, esp->ireg));
	ESPLOG(("esp%d: HW reread [sreg<%02x> sstep<%02x> ireg<%02x>]\n",
		esp->esp_id,
		sbus_readb(esp->eregs + ESP_STATUS),
		sbus_readb(esp->eregs + ESP_SSTEP),
		sbus_readb(esp->eregs + ESP_INTRPT)));
#ifdef DEBUG_ESP_CMDS
	printk("esp%d: last ESP cmds [", esp->esp_id);
	i = (esp->espcmdent - 1) & 31;
	printk("<"); esp_print_cmd(esp->espcmdlog[i]); printk(">");
	i = (i - 1) & 31;
	printk("<"); esp_print_cmd(esp->espcmdlog[i]); printk(">");
	i = (i - 1) & 31;
	printk("<"); esp_print_cmd(esp->espcmdlog[i]); printk(">");
	i = (i - 1) & 31;
	printk("<"); esp_print_cmd(esp->espcmdlog[i]); printk(">");
	printk("]\n");
#endif /* (DEBUG_ESP_CMDS) */

	if (SCptr) {
		ESPLOG(("esp%d: current command ", esp->esp_id));
		esp_dump_cmd(SCptr);
	}
	ESPLOG(("\n"));
	SCptr = esp->disconnected_SC;
	ESPLOG(("esp%d: disconnected ", esp->esp_id));
	while (SCptr) {
		esp_dump_cmd(SCptr);
		SCptr = (struct scsi_cmnd *) SCptr->host_scribble;
	}
	ESPLOG(("\n"));
}

/* Abort a command.  The host_lock is acquired by caller. */
static int esp_abort(struct scsi_cmnd *SCptr)
{
	struct esp *esp = (struct esp *) SCptr->device->host->hostdata;
	int don;

	ESPLOG(("esp%d: Aborting command\n", esp->esp_id));
	esp_dump_state(esp);

	/* Wheee, if this is the current command on the bus, the
	 * best we can do is assert ATN and wait for msgout phase.
	 * This should even fix a hung SCSI bus when we lose state
	 * in the driver and timeout because the eventual phase change
	 * will cause the ESP to (eventually) give an interrupt.
	 */
	if (esp->current_SC == SCptr) {
		esp->cur_msgout[0] = ABORT;
		esp->msgout_len = 1;
		esp->msgout_ctr = 0;
		esp_cmd(esp, ESP_CMD_SATN);
		return SUCCESS;
	}

	/* If it is still in the issue queue then we can safely
	 * call the completion routine and report abort success.
	 */
	don = (sbus_readl(esp->dregs + DMA_CSR) & DMA_INT_ENAB);
	if (don) {
		ESP_INTSOFF(esp->dregs);
	}
	if (esp->issue_SC) {
		struct scsi_cmnd **prev, *this;
		for (prev = (&esp->issue_SC), this = esp->issue_SC;
		     this != NULL;
		     prev = (struct scsi_cmnd **) &(this->host_scribble),
			     this = (struct scsi_cmnd *) this->host_scribble) {

			if (this == SCptr) {
				*prev = (struct scsi_cmnd *) this->host_scribble;
				this->host_scribble = NULL;

				esp_release_dmabufs(esp, this);
				this->result = DID_ABORT << 16;
				this->scsi_done(this);

				if (don)
					ESP_INTSON(esp->dregs);

				return SUCCESS;
			}
		}
	}

	/* Yuck, the command to abort is disconnected, it is not
	 * worth trying to abort it now if something else is live
	 * on the bus at this time.  So, we let the SCSI code wait
	 * a little bit and try again later.
	 */
	if (esp->current_SC) {
		if (don)
			ESP_INTSON(esp->dregs);
		return FAILED;
	}

	/* It's disconnected, we have to reconnect to re-establish
	 * the nexus and tell the device to abort.  However, we really
	 * cannot 'reconnect' per se.  Don't try to be fancy, just
	 * indicate failure, which causes our caller to reset the whole
	 * bus.
	 */

	if (don)
		ESP_INTSON(esp->dregs);

	return FAILED;
}

/* We've sent ESP_CMD_RS to the ESP, the interrupt had just
 * arrived indicating the end of the SCSI bus reset.  Our job
 * is to clean out the command queues and begin re-execution
 * of SCSI commands once more.
 */
static int esp_finish_reset(struct esp *esp)
{
	struct scsi_cmnd *sp = esp->current_SC;

	/* Clean up currently executing command, if any. */
	if (sp != NULL) {
		esp->current_SC = NULL;

		esp_release_dmabufs(esp, sp);
		sp->result = (DID_RESET << 16);

		sp->scsi_done(sp);
	}

	/* Clean up disconnected queue, they have been invalidated
	 * by the bus reset.
	 */
	if (esp->disconnected_SC) {
		while ((sp = remove_first_SC(&esp->disconnected_SC)) != NULL) {
			esp_release_dmabufs(esp, sp);
			sp->result = (DID_RESET << 16);

			sp->scsi_done(sp);
		}
	}

	/* SCSI bus reset is complete. */
	esp->resetting_bus = 0;
	wake_up(&esp->reset_queue);

	/* Ok, now it is safe to get commands going once more. */
	if (esp->issue_SC)
		esp_exec_cmd(esp);

	return do_intr_end;
}

static int esp_do_resetbus(struct esp *esp)
{
	ESPLOG(("esp%d: Resetting scsi bus\n", esp->esp_id));
	esp->resetting_bus = 1;
	esp_cmd(esp, ESP_CMD_RS);

	return do_intr_end;
}

/* Reset ESP chip, reset hanging bus, then kill active and
 * disconnected commands for targets without soft reset.
 *
 * The host_lock is acquired by caller.
 */
static int esp_reset(struct scsi_cmnd *SCptr)
{
	struct esp *esp = (struct esp *) SCptr->device->host->hostdata;

	spin_lock_irq(esp->ehost->host_lock);
	(void) esp_do_resetbus(esp);
	spin_unlock_irq(esp->ehost->host_lock);

	wait_event(esp->reset_queue, (esp->resetting_bus == 0));

	return SUCCESS;
}

/* Internal ESP done function. */
static void esp_done(struct esp *esp, int error)
{
	struct scsi_cmnd *done_SC = esp->current_SC;

	esp->current_SC = NULL;

	esp_release_dmabufs(esp, done_SC);
	done_SC->result = error;

	done_SC->scsi_done(done_SC);

	/* Bus is free, issue any commands in the queue. */
	if (esp->issue_SC && !esp->current_SC)
		esp_exec_cmd(esp);

}

/* Wheee, ESP interrupt engine. */  

/* Forward declarations. */
static int esp_do_phase_determine(struct esp *esp);
static int esp_do_data_finale(struct esp *esp);
static int esp_select_complete(struct esp *esp);
static int esp_do_status(struct esp *esp);
static int esp_do_msgin(struct esp *esp);
static int esp_do_msgindone(struct esp *esp);
static int esp_do_msgout(struct esp *esp);
static int esp_do_cmdbegin(struct esp *esp);

#define sreg_datainp(__sreg)  (((__sreg) & ESP_STAT_PMASK) == ESP_DIP)
#define sreg_dataoutp(__sreg) (((__sreg) & ESP_STAT_PMASK) == ESP_DOP)

/* Read any bytes found in the FAS366 fifo, storing them into
 * the ESP driver software state structure.
 */
static void hme_fifo_read(struct esp *esp)
{
	u8 count = 0;
	u8 status = esp->sreg;

	/* Cannot safely frob the fifo for these following cases, but
	 * we must always read the fifo when the reselect interrupt
	 * is pending.
	 */
	if (((esp->ireg & ESP_INTR_RSEL) == 0)	&&
	    (sreg_datainp(status)		||
	     sreg_dataoutp(status)		||
	     (esp->current_SC &&
	      esp->current_SC->SCp.phase == in_data_done))) {
		ESPHME(("<wkaround_skipped>"));
	} else {
		unsigned long fcnt = sbus_readb(esp->eregs + ESP_FFLAGS) & ESP_FF_FBYTES;

		/* The HME stores bytes in multiples of 2 in the fifo. */
		ESPHME(("hme_fifo[fcnt=%d", (int)fcnt));
		while (fcnt) {
			esp->hme_fifo_workaround_buffer[count++] =
				sbus_readb(esp->eregs + ESP_FDATA);
			esp->hme_fifo_workaround_buffer[count++] =
				sbus_readb(esp->eregs + ESP_FDATA);
			ESPHME(("<%02x,%02x>", esp->hme_fifo_workaround_buffer[count-2], esp->hme_fifo_workaround_buffer[count-1]));
			fcnt--;
		}
		if (sbus_readb(esp->eregs + ESP_STATUS2) & ESP_STAT2_F1BYTE) {
			ESPHME(("<poke_byte>"));
			sbus_writeb(0, esp->eregs + ESP_FDATA);
			esp->hme_fifo_workaround_buffer[count++] =
				sbus_readb(esp->eregs + ESP_FDATA);
			ESPHME(("<%02x,0x00>", esp->hme_fifo_workaround_buffer[count-1]));
			ESPHME(("CMD_FLUSH"));
			esp_cmd(esp, ESP_CMD_FLUSH);
		} else {
			ESPHME(("no_xtra_byte"));
		}
	}
	ESPHME(("wkarnd_cnt=%d]", (int)count));
	esp->hme_fifo_workaround_count = count;
}

static inline void hme_fifo_push(struct esp *esp, u8 *bytes, u8 count)
{
	esp_cmd(esp, ESP_CMD_FLUSH);
	while (count) {
		u8 tmp = *bytes++;
		sbus_writeb(tmp, esp->eregs + ESP_FDATA);
		sbus_writeb(0, esp->eregs + ESP_FDATA);
		count--;
	}
}

/* We try to avoid some interrupts by jumping ahead and see if the ESP
 * has gotten far enough yet.  Hence the following.
 */
static inline int skipahead1(struct esp *esp, struct scsi_cmnd *scp,
			     int prev_phase, int new_phase)
{
	if (scp->SCp.sent_command != prev_phase)
		return 0;
	if (ESP_IRQ_P(esp->dregs)) {
		/* Yes, we are able to save an interrupt. */
		if (esp->erev == fashme)
			esp->sreg2 = sbus_readb(esp->eregs + ESP_STATUS2);
		esp->sreg = (sbus_readb(esp->eregs + ESP_STATUS) & ~(ESP_STAT_INTR));
		esp->ireg = sbus_readb(esp->eregs + ESP_INTRPT);
		if (esp->erev == fashme) {
			/* This chip is really losing. */
			ESPHME(("HME["));
			/* Must latch fifo before reading the interrupt
			 * register else garbage ends up in the FIFO
			 * which confuses the driver utterly.
			 * Happy Meal indeed....
			 */
			ESPHME(("fifo_workaround]"));
			if (!(esp->sreg2 & ESP_STAT2_FEMPTY) ||
			    (esp->sreg2 & ESP_STAT2_F1BYTE))
				hme_fifo_read(esp);
		}
		if (!(esp->ireg & ESP_INTR_SR))
			return 0;
		else
			return do_reset_complete;
	}
	/* Ho hum, target is taking forever... */
	scp->SCp.sent_command = new_phase; /* so we don't recurse... */
	return do_intr_end;
}

static inline int skipahead2(struct esp *esp, struct scsi_cmnd *scp,
			     int prev_phase1, int prev_phase2, int new_phase)
{
	if (scp->SCp.sent_command != prev_phase1 &&
	    scp->SCp.sent_command != prev_phase2)
		return 0;
	if (ESP_IRQ_P(esp->dregs)) {
		/* Yes, we are able to save an interrupt. */
		if (esp->erev == fashme)
			esp->sreg2 = sbus_readb(esp->eregs + ESP_STATUS2);
		esp->sreg = (sbus_readb(esp->eregs + ESP_STATUS) & ~(ESP_STAT_INTR));
		esp->ireg = sbus_readb(esp->eregs + ESP_INTRPT);
		if (esp->erev == fashme) {
			/* This chip is really losing. */
			ESPHME(("HME["));

			/* Must latch fifo before reading the interrupt
			 * register else garbage ends up in the FIFO
			 * which confuses the driver utterly.
			 * Happy Meal indeed....
			 */
			ESPHME(("fifo_workaround]"));
			if (!(esp->sreg2 & ESP_STAT2_FEMPTY) ||
			    (esp->sreg2 & ESP_STAT2_F1BYTE))
				hme_fifo_read(esp);
		}
		if (!(esp->ireg & ESP_INTR_SR))
			return 0;
		else
			return do_reset_complete;
	}
	/* Ho hum, target is taking forever... */
	scp->SCp.sent_command = new_phase; /* so we don't recurse... */
	return do_intr_end;
}

/* Now some dma helpers. */
static void dma_setup(struct esp *esp, __u32 addr, int count, int write)
{
	u32 nreg = sbus_readl(esp->dregs + DMA_CSR);

	if (write)
		nreg |= DMA_ST_WRITE;
	else
		nreg &= ~(DMA_ST_WRITE);
	nreg |= DMA_ENABLE;
	sbus_writel(nreg, esp->dregs + DMA_CSR);
	if (esp->dma->revision == dvmaesc1) {
		/* This ESC gate array sucks! */
		__u32 src = addr;
		__u32 dest = src + count;

		if (dest & (PAGE_SIZE - 1))
			count = PAGE_ALIGN(count);
		sbus_writel(count, esp->dregs + DMA_COUNT);
	}
	sbus_writel(addr, esp->dregs + DMA_ADDR);
}

static void dma_drain(struct esp *esp)
{
	u32 tmp;

	if (esp->dma->revision == dvmahme)
		return;
	if ((tmp = sbus_readl(esp->dregs + DMA_CSR)) & DMA_FIFO_ISDRAIN) {
		switch (esp->dma->revision) {
		default:
			tmp |= DMA_FIFO_STDRAIN;
			sbus_writel(tmp, esp->dregs + DMA_CSR);

		case dvmarev3:
		case dvmaesc1:
			while (sbus_readl(esp->dregs + DMA_CSR) & DMA_FIFO_ISDRAIN)
				udelay(1);
		};
	}
}

static void dma_invalidate(struct esp *esp)
{
	u32 tmp;

	if (esp->dma->revision == dvmahme) {
		sbus_writel(DMA_RST_SCSI, esp->dregs + DMA_CSR);

		esp->prev_hme_dmacsr = ((esp->prev_hme_dmacsr |
					 (DMA_PARITY_OFF | DMA_2CLKS |
					  DMA_SCSI_DISAB | DMA_INT_ENAB)) &
					~(DMA_ST_WRITE | DMA_ENABLE));

		sbus_writel(0, esp->dregs + DMA_CSR);
		sbus_writel(esp->prev_hme_dmacsr, esp->dregs + DMA_CSR);

		/* This is necessary to avoid having the SCSI channel
		 * engine lock up on us.
		 */
		sbus_writel(0, esp->dregs + DMA_ADDR);
	} else {
		while ((tmp = sbus_readl(esp->dregs + DMA_CSR)) & DMA_PEND_READ)
			udelay(1);

		tmp &= ~(DMA_ENABLE | DMA_ST_WRITE | DMA_BCNT_ENAB);
		tmp |= DMA_FIFO_INV;
		sbus_writel(tmp, esp->dregs + DMA_CSR);
		tmp &= ~DMA_FIFO_INV;
		sbus_writel(tmp, esp->dregs + DMA_CSR);
	}
}

static inline void dma_flashclear(struct esp *esp)
{
	dma_drain(esp);
	dma_invalidate(esp);
}

static int dma_can_transfer(struct esp *esp, struct scsi_cmnd *sp)
{
	__u32 base, end, sz;

	if (esp->dma->revision == dvmarev3) {
		sz = sp->SCp.this_residual;
		if (sz > 0x1000000)
			sz = 0x1000000;
	} else {
		base = ((__u32)((unsigned long)sp->SCp.ptr));
		base &= (0x1000000 - 1);
		end = (base + sp->SCp.this_residual);
		if (end > 0x1000000)
			end = 0x1000000;
		sz = (end - base);
	}
	return sz;
}

/* Misc. esp helper macros. */
#define esp_setcount(__eregs, __cnt, __hme) \
	sbus_writeb(((__cnt)&0xff), (__eregs) + ESP_TCLOW); \
	sbus_writeb((((__cnt)>>8)&0xff), (__eregs) + ESP_TCMED); \
	if (__hme) { \
		sbus_writeb((((__cnt)>>16)&0xff), (__eregs) + FAS_RLO); \
		sbus_writeb(0, (__eregs) + FAS_RHI); \
	}

#define esp_getcount(__eregs, __hme) \
	((sbus_readb((__eregs) + ESP_TCLOW)&0xff) | \
	 ((sbus_readb((__eregs) + ESP_TCMED)&0xff) << 8) | \
         ((__hme) ? sbus_readb((__eregs) + FAS_RLO) << 16 : 0))

#define fcount(__esp) \
	(((__esp)->erev == fashme) ? \
	  (__esp)->hme_fifo_workaround_count : \
	  sbus_readb(((__esp)->eregs) + ESP_FFLAGS) & ESP_FF_FBYTES)

#define fnzero(__esp) \
	(((__esp)->erev == fashme) ? 0 : \
	 sbus_readb(((__esp)->eregs) + ESP_FFLAGS) & ESP_FF_ONOTZERO)

/* XXX speculative nops unnecessary when continuing amidst a data phase
 * XXX even on esp100!!!  another case of flooding the bus with I/O reg
 * XXX writes...
 */
#define esp_maybe_nop(__esp) \
	if ((__esp)->erev == esp100) \
		esp_cmd((__esp), ESP_CMD_NULL)

#define sreg_to_dataphase(__sreg) \
	((((__sreg) & ESP_STAT_PMASK) == ESP_DOP) ? in_dataout : in_datain)

/* The ESP100 when in synchronous data phase, can mistake a long final
 * REQ pulse from the target as an extra byte, it places whatever is on
 * the data lines into the fifo.  For now, we will assume when this
 * happens that the target is a bit quirky and we don't want to
 * be talking synchronously to it anyways.  Regardless, we need to
 * tell the ESP to eat the extraneous byte so that we can proceed
 * to the next phase.
 */
static int esp100_sync_hwbug(struct esp *esp, struct scsi_cmnd *sp, int fifocnt)
{
	/* Do not touch this piece of code. */
	if ((!(esp->erev == esp100)) ||
	    (!(sreg_datainp((esp->sreg = sbus_readb(esp->eregs + ESP_STATUS))) &&
	       !fifocnt) &&
	     !(sreg_dataoutp(esp->sreg) && !fnzero(esp)))) {
		if (sp->SCp.phase == in_dataout)
			esp_cmd(esp, ESP_CMD_FLUSH);
		return 0;
	} else {
		/* Async mode for this guy. */
		build_sync_nego_msg(esp, 0, 0);

		/* Ack the bogus byte, but set ATN first. */
		esp_cmd(esp, ESP_CMD_SATN);
		esp_cmd(esp, ESP_CMD_MOK);
		return 1;
	}
}

/* This closes the window during a selection with a reselect pending, because
 * we use DMA for the selection process the FIFO should hold the correct
 * contents if we get reselected during this process.  So we just need to
 * ack the possible illegal cmd interrupt pending on the esp100.
 */
static inline int esp100_reconnect_hwbug(struct esp *esp)
{
	u8 tmp;

	if (esp->erev != esp100)
		return 0;
	tmp = sbus_readb(esp->eregs + ESP_INTRPT);
	if (tmp & ESP_INTR_SR)
		return 1;
	return 0;
}

/* This verifies the BUSID bits during a reselection so that we know which
 * target is talking to us.
 */
static inline int reconnect_target(struct esp *esp)
{
	int it, me = esp->scsi_id_mask, targ = 0;

	if (2 != fcount(esp))
		return -1;
	if (esp->erev == fashme) {
		/* HME does not latch it's own BUS ID bits during
		 * a reselection.  Also the target number is given
		 * as an unsigned char, not as a sole bit number
		 * like the other ESP's do.
		 * Happy Meal indeed....
		 */
		targ = esp->hme_fifo_workaround_buffer[0];
	} else {
		it = sbus_readb(esp->eregs + ESP_FDATA);
		if (!(it & me))
			return -1;
		it &= ~me;
		if (it & (it - 1))
			return -1;
		while (!(it & 1))
			targ++, it >>= 1;
	}
	return targ;
}

/* This verifies the identify from the target so that we know which lun is
 * being reconnected.
 */
static inline int reconnect_lun(struct esp *esp)
{
	int lun;

	if ((esp->sreg & ESP_STAT_PMASK) != ESP_MIP)
		return -1;
	if (esp->erev == fashme)
		lun = esp->hme_fifo_workaround_buffer[1];
	else
		lun = sbus_readb(esp->eregs + ESP_FDATA);

	/* Yes, you read this correctly.  We report lun of zero
	 * if we see parity error.  ESP reports parity error for
	 * the lun byte, and this is the only way to hope to recover
	 * because the target is connected.
	 */
	if (esp->sreg & ESP_STAT_PERR)
		return 0;

	/* Check for illegal bits being set in the lun. */
	if ((lun & 0x40) || !(lun & 0x80))
		return -1;

	return lun & 7;
}

/* This puts the driver in a state where it can revitalize a command that
 * is being continued due to reselection.
 */
static inline void esp_connect(struct esp *esp, struct scsi_cmnd *sp)
{
	struct esp_device *esp_dev = sp->device->hostdata;

	if (esp->prev_soff  != esp_dev->sync_max_offset ||
	    esp->prev_stp   != esp_dev->sync_min_period ||
	    (esp->erev > esp100a &&
	     esp->prev_cfg3 != esp->config3[sp->device->id])) {
		esp->prev_soff = esp_dev->sync_max_offset;
		esp->prev_stp = esp_dev->sync_min_period;
		sbus_writeb(esp->prev_soff, esp->eregs + ESP_SOFF);
		sbus_writeb(esp->prev_stp, esp->eregs + ESP_STP);
		if (esp->erev > esp100a) {
			esp->prev_cfg3 = esp->config3[sp->device->id];
			sbus_writeb(esp->prev_cfg3, esp->eregs + ESP_CFG3);
		}
	}
	esp->current_SC = sp;
}

/* This will place the current working command back into the issue queue
 * if we are to receive a reselection amidst a selection attempt.
 */
static inline void esp_reconnect(struct esp *esp, struct scsi_cmnd *sp)
{
	if (!esp->disconnected_SC)
		ESPLOG(("esp%d: Weird, being reselected but disconnected "
			"command queue is empty.\n", esp->esp_id));
	esp->snip = 0;
	esp->current_SC = NULL;
	sp->SCp.phase = not_issued;
	append_SC(&esp->issue_SC, sp);
}

/* Begin message in phase. */
static int esp_do_msgin(struct esp *esp)
{
	/* Must be very careful with the fifo on the HME */
	if ((esp->erev != fashme) ||
	    !(sbus_readb(esp->eregs + ESP_STATUS2) & ESP_STAT2_FEMPTY))
		esp_cmd(esp, ESP_CMD_FLUSH);
	esp_maybe_nop(esp);
	esp_cmd(esp, ESP_CMD_TI);
	esp->msgin_len = 1;
	esp->msgin_ctr = 0;
	esp_advance_phase(esp->current_SC, in_msgindone);
	return do_work_bus;
}

/* This uses various DMA csr fields and the fifo flags count value to
 * determine how many bytes were successfully sent/received by the ESP.
 */
static inline int esp_bytes_sent(struct esp *esp, int fifo_count)
{
	int rval = sbus_readl(esp->dregs + DMA_ADDR) - esp->esp_command_dvma;

	if (esp->dma->revision == dvmarev1)
		rval -= (4 - ((sbus_readl(esp->dregs + DMA_CSR) & DMA_READ_AHEAD)>>11));
	return rval - fifo_count;
}

static inline void advance_sg(struct scsi_cmnd *sp)
{
	++sp->SCp.buffer;
	--sp->SCp.buffers_residual;
	sp->SCp.this_residual = sg_dma_len(sp->SCp.buffer);
	sp->SCp.ptr = (char *)((unsigned long)sg_dma_address(sp->SCp.buffer));
}

/* Please note that the way I've coded these routines is that I _always_
 * check for a disconnect during any and all information transfer
 * phases.  The SCSI standard states that the target _can_ cause a BUS
 * FREE condition by dropping all MSG/CD/IO/BSY signals.  Also note
 * that during information transfer phases the target controls every
 * change in phase, the only thing the initiator can do is "ask" for
 * a message out phase by driving ATN true.  The target can, and sometimes
 * will, completely ignore this request so we cannot assume anything when
 * we try to force a message out phase to abort/reset a target.  Most of
 * the time the target will eventually be nice and go to message out, so
 * we may have to hold on to our state about what we want to tell the target
 * for some period of time.
 */

/* I think I have things working here correctly.  Even partial transfers
 * within a buffer or sub-buffer should not upset us at all no matter
 * how bad the target and/or ESP fucks things up.
 */
static int esp_do_data(struct esp *esp)
{
	struct scsi_cmnd *SCptr = esp->current_SC;
	int thisphase, hmuch;

	ESPDATA(("esp_do_data: "));
	esp_maybe_nop(esp);
	thisphase = sreg_to_dataphase(esp->sreg);
	esp_advance_phase(SCptr, thisphase);
	ESPDATA(("newphase<%s> ", (thisphase == in_datain) ? "DATAIN" : "DATAOUT"));
	hmuch = dma_can_transfer(esp, SCptr);
	if (hmuch > (64 * 1024) && (esp->erev != fashme))
		hmuch = (64 * 1024);
	ESPDATA(("hmuch<%d> ", hmuch));
	esp->current_transfer_size = hmuch;

	if (esp->erev == fashme) {
		u32 tmp = esp->prev_hme_dmacsr;

		/* Always set the ESP count registers first. */
		esp_setcount(esp->eregs, hmuch, 1);

		/* Get the DMA csr computed. */
		tmp |= (DMA_SCSI_DISAB | DMA_ENABLE);
		if (thisphase == in_datain)
			tmp |= DMA_ST_WRITE;
		else
			tmp &= ~(DMA_ST_WRITE);
		esp->prev_hme_dmacsr = tmp;

		ESPDATA(("DMA|TI --> do_intr_end\n"));
		if (thisphase == in_datain) {
			sbus_writel(hmuch, esp->dregs + DMA_COUNT);
			esp_cmd(esp, ESP_CMD_DMA | ESP_CMD_TI);
		} else {
			esp_cmd(esp, ESP_CMD_DMA | ESP_CMD_TI);
			sbus_writel(hmuch, esp->dregs + DMA_COUNT);
		}
		sbus_writel((__u32)((unsigned long)SCptr->SCp.ptr), esp->dregs+DMA_ADDR);
		sbus_writel(esp->prev_hme_dmacsr, esp->dregs + DMA_CSR);
	} else {
		esp_setcount(esp->eregs, hmuch, 0);
		dma_setup(esp, ((__u32)((unsigned long)SCptr->SCp.ptr)),
			  hmuch, (thisphase == in_datain));
		ESPDATA(("DMA|TI --> do_intr_end\n"));
		esp_cmd(esp, ESP_CMD_DMA | ESP_CMD_TI);
	}
	return do_intr_end;
}

/* See how successful the data transfer was. */
static int esp_do_data_finale(struct esp *esp)
{
	struct scsi_cmnd *SCptr = esp->current_SC;
	struct esp_device *esp_dev = SCptr->device->hostdata;
	int bogus_data = 0, bytes_sent = 0, fifocnt, ecount = 0;

	ESPDATA(("esp_do_data_finale: "));

	if (SCptr->SCp.phase == in_datain) {
		if (esp->sreg & ESP_STAT_PERR) {
			/* Yuck, parity error.  The ESP asserts ATN
			 * so that we can go to message out phase
			 * immediately and inform the target that
			 * something bad happened.
			 */
			ESPLOG(("esp%d: data bad parity detected.\n",
				esp->esp_id));
			esp->cur_msgout[0] = INITIATOR_ERROR;
			esp->msgout_len = 1;
		}
		dma_drain(esp);
	}
	dma_invalidate(esp);

	/* This could happen for the above parity error case. */
	if (esp->ireg != ESP_INTR_BSERV) {
		/* Please go to msgout phase, please please please... */
		ESPLOG(("esp%d: !BSERV after data, probably to msgout\n",
			esp->esp_id));
		return esp_do_phase_determine(esp);
	}	

	/* Check for partial transfers and other horrible events.
	 * Note, here we read the real fifo flags register even
	 * on HME broken adapters because we skip the HME fifo
	 * workaround code in esp_handle() if we are doing data
	 * phase things.  We don't want to fuck directly with
	 * the fifo like that, especially if doing synchronous
	 * transfers!  Also, will need to double the count on
	 * HME if we are doing wide transfers, as the HME fifo
	 * will move and count 16-bit quantities during wide data.
	 * SMCC _and_ Qlogic can both bite me.
	 */
	fifocnt = (sbus_readb(esp->eregs + ESP_FFLAGS) & ESP_FF_FBYTES);
	if (esp->erev != fashme)
		ecount = esp_getcount(esp->eregs, 0);
	bytes_sent = esp->current_transfer_size;

	ESPDATA(("trans_sz(%d), ", bytes_sent));
	if (esp->erev == fashme) {
		if (!(esp->sreg & ESP_STAT_TCNT)) {
			ecount = esp_getcount(esp->eregs, 1);
			bytes_sent -= ecount;
		}

		/* Always subtract any cruft remaining in the FIFO. */
		if (esp->prev_cfg3 & ESP_CONFIG3_EWIDE)
			fifocnt <<= 1;
		if (SCptr->SCp.phase == in_dataout)
			bytes_sent -= fifocnt;

		/* I have an IBM disk which exhibits the following
		 * behavior during writes to it.  It disconnects in
		 * the middle of a partial transfer, the current sglist
		 * buffer is 1024 bytes, the disk stops data transfer
		 * at 512 bytes.
		 *
		 * However the FAS366 reports that 32 more bytes were
		 * transferred than really were.  This is precisely
		 * the size of a fully loaded FIFO in wide scsi mode.
		 * The FIFO state recorded indicates that it is empty.
		 *
		 * I have no idea if this is a bug in the FAS366 chip
		 * or a bug in the firmware on this IBM disk.  In any
		 * event the following seems to be a good workaround.  -DaveM
		 */
		if (bytes_sent != esp->current_transfer_size &&
		    SCptr->SCp.phase == in_dataout) {
			int mask = (64 - 1);

			if ((esp->prev_cfg3 & ESP_CONFIG3_EWIDE) == 0)
				mask >>= 1;

			if (bytes_sent & mask)
				bytes_sent -= (bytes_sent & mask);
		}
	} else {
		if (!(esp->sreg & ESP_STAT_TCNT))
			bytes_sent -= ecount;
		if (SCptr->SCp.phase == in_dataout)
			bytes_sent -= fifocnt;
	}

	ESPDATA(("bytes_sent(%d), ", bytes_sent));

	/* If we were in synchronous mode, check for peculiarities. */
	if (esp->erev == fashme) {
		if (esp_dev->sync_max_offset) {
			if (SCptr->SCp.phase == in_dataout)
				esp_cmd(esp, ESP_CMD_FLUSH);
		} else {
			esp_cmd(esp, ESP_CMD_FLUSH);
		}
	} else {
		if (esp_dev->sync_max_offset)
			bogus_data = esp100_sync_hwbug(esp, SCptr, fifocnt);
		else
			esp_cmd(esp, ESP_CMD_FLUSH);
	}

	/* Until we are sure of what has happened, we are certainly
	 * in the dark.
	 */
	esp_advance_phase(SCptr, in_the_dark);

	if (bytes_sent < 0) {
		/* I've seen this happen due to lost state in this
		 * driver.  No idea why it happened, but allowing
		 * this value to be negative caused things to
		 * lock up.  This allows greater chance of recovery.
		 * In fact every time I've seen this, it has been
		 * a driver bug without question.
		 */
		ESPLOG(("esp%d: yieee, bytes_sent < 0!\n", esp->esp_id));
		ESPLOG(("esp%d: csz=%d fifocount=%d ecount=%d\n",
			esp->esp_id,
			esp->current_transfer_size, fifocnt, ecount));
		ESPLOG(("esp%d: use_sg=%d ptr=%p this_residual=%d\n",
			esp->esp_id,
			SCptr->use_sg, SCptr->SCp.ptr, SCptr->SCp.this_residual));
		ESPLOG(("esp%d: Forcing async for target %d\n", esp->esp_id, 
			SCptr->device->id));
		SCptr->device->borken = 1;
		esp_dev->sync = 0;
		bytes_sent = 0;
	}

	/* Update the state of our transfer. */
	SCptr->SCp.ptr += bytes_sent;
	SCptr->SCp.this_residual -= bytes_sent;
	if (SCptr->SCp.this_residual < 0) {
		/* shit */
		ESPLOG(("esp%d: Data transfer overrun.\n", esp->esp_id));
		SCptr->SCp.this_residual = 0;
	}

	/* Maybe continue. */
	if (!bogus_data) {
		ESPDATA(("!bogus_data, "));

		/* NO MATTER WHAT, we advance the scatterlist,
		 * if the target should decide to disconnect
		 * in between scatter chunks (which is common)
		 * we could die horribly!  I used to have the sg
		 * advance occur only if we are going back into
		 * (or are staying in) a data phase, you can
		 * imagine the hell I went through trying to
		 * figure this out.
		 */
		if (SCptr->use_sg && !SCptr->SCp.this_residual)
			advance_sg(SCptr);
		if (sreg_datainp(esp->sreg) || sreg_dataoutp(esp->sreg)) {
			ESPDATA(("to more data\n"));
			return esp_do_data(esp);
		}
		ESPDATA(("to new phase\n"));
		return esp_do_phase_determine(esp);
	}
	/* Bogus data, just wait for next interrupt. */
	ESPLOG(("esp%d: bogus_data during end of data phase\n",
		esp->esp_id));
	return do_intr_end;
}

/* We received a non-good status return at the end of
 * running a SCSI command.  This is used to decide if
 * we should clear our synchronous transfer state for
 * such a device when that happens.
 *
 * The idea is that when spinning up a disk or rewinding
 * a tape, we don't want to go into a loop re-negotiating
 * synchronous capabilities over and over.
 */
static int esp_should_clear_sync(struct scsi_cmnd *sp)
{
	u8 cmd1 = sp->cmnd[0];
	u8 cmd2 = sp->data_cmnd[0];

	/* These cases are for spinning up a disk and
	 * waiting for that spinup to complete.
	 */
	if (cmd1 == START_STOP ||
	    cmd2 == START_STOP)
		return 0;

	if (cmd1 == TEST_UNIT_READY ||
	    cmd2 == TEST_UNIT_READY)
		return 0;

	/* One more special case for SCSI tape drives,
	 * this is what is used to probe the device for
	 * completion of a rewind or tape load operation.
	 */
	if (sp->device->type == TYPE_TAPE) {
		if (cmd1 == MODE_SENSE ||
		    cmd2 == MODE_SENSE)
			return 0;
	}

	return 1;
}

/* Either a command is completing or a target is dropping off the bus
 * to continue the command in the background so we can do other work.
 */
static int esp_do_freebus(struct esp *esp)
{
	struct scsi_cmnd *SCptr = esp->current_SC;
	struct esp_device *esp_dev = SCptr->device->hostdata;
	int rval;

	rval = skipahead2(esp, SCptr, in_status, in_msgindone, in_freeing);
	if (rval)
		return rval;
	if (esp->ireg != ESP_INTR_DC) {
		ESPLOG(("esp%d: Target will not disconnect\n", esp->esp_id));
		return do_reset_bus; /* target will not drop BSY... */
	}
	esp->msgout_len = 0;
	esp->prevmsgout = NOP;
	if (esp->prevmsgin == COMMAND_COMPLETE) {
		/* Normal end of nexus. */
		if (esp->disconnected_SC || (esp->erev == fashme))
			esp_cmd(esp, ESP_CMD_ESEL);

		if (SCptr->SCp.Status != GOOD &&
		    SCptr->SCp.Status != CONDITION_GOOD &&
		    ((1<<SCptr->device->id) & esp->targets_present) &&
		    esp_dev->sync &&
		    esp_dev->sync_max_offset) {
			/* SCSI standard says that the synchronous capabilities
			 * should be renegotiated at this point.  Most likely
			 * we are about to request sense from this target
			 * in which case we want to avoid using sync
			 * transfers until we are sure of the current target
			 * state.
			 */
			ESPMISC(("esp: Status <%d> for target %d lun %d\n",
				 SCptr->SCp.Status, SCptr->device->id, SCptr->device->lun));

			/* But don't do this when spinning up a disk at
			 * boot time while we poll for completion as it
			 * fills up the console with messages.  Also, tapes
			 * can report not ready many times right after
			 * loading up a tape.
			 */
			if (esp_should_clear_sync(SCptr) != 0)
				esp_dev->sync = 0;
		}
		ESPDISC(("F<%02x,%02x>", SCptr->device->id, SCptr->device->lun));
		esp_done(esp, ((SCptr->SCp.Status & 0xff) |
			       ((SCptr->SCp.Message & 0xff)<<8) |
			       (DID_OK << 16)));
	} else if (esp->prevmsgin == DISCONNECT) {
		/* Normal disconnect. */
		esp_cmd(esp, ESP_CMD_ESEL);
		ESPDISC(("D<%02x,%02x>", SCptr->device->id, SCptr->device->lun));
		append_SC(&esp->disconnected_SC, SCptr);
		esp->current_SC = NULL;
		if (esp->issue_SC)
			esp_exec_cmd(esp);
	} else {
		/* Driver bug, we do not expect a disconnect here
		 * and should not have advanced the state engine
		 * to in_freeing.
		 */
		ESPLOG(("esp%d: last msg not disc and not cmd cmplt.\n",
			esp->esp_id));
		return do_reset_bus;
	}
	return do_intr_end;
}

/* When a reselect occurs, and we cannot find the command to
 * reconnect to in our queues, we do this.
 */
static int esp_bad_reconnect(struct esp *esp)
{
	struct scsi_cmnd *sp;

	ESPLOG(("esp%d: Eieeee, reconnecting unknown command!\n",
		esp->esp_id));
	ESPLOG(("QUEUE DUMP\n"));
	sp = esp->issue_SC;
	ESPLOG(("esp%d: issue_SC[", esp->esp_id));
	while (sp) {
		ESPLOG(("<%02x,%02x>", sp->device->id, sp->device->lun));
		sp = (struct scsi_cmnd *) sp->host_scribble;
	}
	ESPLOG(("]\n"));
	sp = esp->current_SC;
	ESPLOG(("esp%d: current_SC[", esp->esp_id));
	if (sp)
		ESPLOG(("<%02x,%02x>", sp->device->id, sp->device->lun));
	else
		ESPLOG(("<NULL>"));
	ESPLOG(("]\n"));
	sp = esp->disconnected_SC;
	ESPLOG(("esp%d: disconnected_SC[", esp->esp_id));
	while (sp) {
		ESPLOG(("<%02x,%02x>", sp->device->id, sp->device->lun));
		sp = (struct scsi_cmnd *) sp->host_scribble;
	}
	ESPLOG(("]\n"));
	return do_reset_bus;
}

/* Do the needy when a target tries to reconnect to us. */
static int esp_do_reconnect(struct esp *esp)
{
	int lun, target;
	struct scsi_cmnd *SCptr;

	/* Check for all bogus conditions first. */
	target = reconnect_target(esp);
	if (target < 0) {
		ESPDISC(("bad bus bits\n"));
		return do_reset_bus;
	}
	lun = reconnect_lun(esp);
	if (lun < 0) {
		ESPDISC(("target=%2x, bad identify msg\n", target));
		return do_reset_bus;
	}

	/* Things look ok... */
	ESPDISC(("R<%02x,%02x>", target, lun));

	/* Must not flush FIFO or DVMA on HME. */
	if (esp->erev != fashme) {
		esp_cmd(esp, ESP_CMD_FLUSH);
		if (esp100_reconnect_hwbug(esp))
			return do_reset_bus;
		esp_cmd(esp, ESP_CMD_NULL);
	}

	SCptr = remove_SC(&esp->disconnected_SC, (u8) target, (u8) lun);
	if (!SCptr)
		return esp_bad_reconnect(esp);

	esp_connect(esp, SCptr);
	esp_cmd(esp, ESP_CMD_MOK);

	if (esp->erev == fashme)
		sbus_writeb(((SCptr->device->id & 0xf) |
			     (ESP_BUSID_RESELID | ESP_BUSID_CTR32BIT)),
			    esp->eregs + ESP_BUSID);

	/* Reconnect implies a restore pointers operation. */
	esp_restore_pointers(esp, SCptr);

	esp->snip = 0;
	esp_advance_phase(SCptr, in_the_dark);
	return do_intr_end;
}

/* End of NEXUS (hopefully), pick up status + message byte then leave if
 * all goes well.
 */
static int esp_do_status(struct esp *esp)
{
	struct scsi_cmnd *SCptr = esp->current_SC;
	int intr, rval;

	rval = skipahead1(esp, SCptr, in_the_dark, in_status);
	if (rval)
		return rval;
	intr = esp->ireg;
	ESPSTAT(("esp_do_status: "));
	if (intr != ESP_INTR_DC) {
		int message_out = 0; /* for parity problems */

		/* Ack the message. */
		ESPSTAT(("ack msg, "));
		esp_cmd(esp, ESP_CMD_MOK);

		if (esp->erev != fashme) {
			dma_flashclear(esp);

			/* Wait till the first bits settle. */
			while (esp->esp_command[0] == 0xff)
				udelay(1);
		} else {
			esp->esp_command[0] = esp->hme_fifo_workaround_buffer[0];
			esp->esp_command[1] = esp->hme_fifo_workaround_buffer[1];
		}

		ESPSTAT(("got something, "));
		/* ESP chimes in with one of
		 *
		 * 1) function done interrupt:
		 *	both status and message in bytes
		 *	are available
		 *
		 * 2) bus service interrupt:
		 *	only status byte was acquired
		 *
		 * 3) Anything else:
		 *	can't happen, but we test for it
		 *	anyways
		 *
		 * ALSO: If bad parity was detected on either
		 *       the status _or_ the message byte then
		 *       the ESP has asserted ATN on the bus
		 *       and we must therefore wait for the
		 *       next phase change.
		 */
		if (intr & ESP_INTR_FDONE) {
			/* We got it all, hallejulia. */
			ESPSTAT(("got both, "));
			SCptr->SCp.Status = esp->esp_command[0];
			SCptr->SCp.Message = esp->esp_command[1];
			esp->prevmsgin = SCptr->SCp.Message;
			esp->cur_msgin[0] = SCptr->SCp.Message;
			if (esp->sreg & ESP_STAT_PERR) {
				/* There was bad parity for the
				 * message byte, the status byte
				 * was ok.
				 */
				message_out = MSG_PARITY_ERROR;
			}
		} else if (intr == ESP_INTR_BSERV) {
			/* Only got status byte. */
			ESPLOG(("esp%d: got status only, ", esp->esp_id));
			if (!(esp->sreg & ESP_STAT_PERR)) {
				SCptr->SCp.Status = esp->esp_command[0];
				SCptr->SCp.Message = 0xff;
			} else {
				/* The status byte had bad parity.
				 * we leave the scsi_pointer Status
				 * field alone as we set it to a default
				 * of CHECK_CONDITION in esp_queue.
				 */
				message_out = INITIATOR_ERROR;
			}
		} else {
			/* This shouldn't happen ever. */
			ESPSTAT(("got bolixed\n"));
			esp_advance_phase(SCptr, in_the_dark);
			return esp_do_phase_determine(esp);
		}

		if (!message_out) {
			ESPSTAT(("status=%2x msg=%2x, ", SCptr->SCp.Status,
				SCptr->SCp.Message));
			if (SCptr->SCp.Message == COMMAND_COMPLETE) {
				ESPSTAT(("and was COMMAND_COMPLETE\n"));
				esp_advance_phase(SCptr, in_freeing);
				return esp_do_freebus(esp);
			} else {
				ESPLOG(("esp%d: and _not_ COMMAND_COMPLETE\n",
					esp->esp_id));
				esp->msgin_len = esp->msgin_ctr = 1;
				esp_advance_phase(SCptr, in_msgindone);
				return esp_do_msgindone(esp);
			}
		} else {
			/* With luck we'll be able to let the target
			 * know that bad parity happened, it will know
			 * which byte caused the problems and send it
			 * again.  For the case where the status byte
			 * receives bad parity, I do not believe most
			 * targets recover very well.  We'll see.
			 */
			ESPLOG(("esp%d: bad parity somewhere mout=%2x\n",
				esp->esp_id, message_out));
			esp->cur_msgout[0] = message_out;
			esp->msgout_len = esp->msgout_ctr = 1;
			esp_advance_phase(SCptr, in_the_dark);
			return esp_do_phase_determine(esp);
		}
	} else {
		/* If we disconnect now, all hell breaks loose. */
		ESPLOG(("esp%d: whoops, disconnect\n", esp->esp_id));
		esp_advance_phase(SCptr, in_the_dark);
		return esp_do_phase_determine(esp);
	}
}

static int esp_enter_status(struct esp *esp)
{
	u8 thecmd = ESP_CMD_ICCSEQ;

	esp_cmd(esp, ESP_CMD_FLUSH);
	if (esp->erev != fashme) {
		u32 tmp;

		esp->esp_command[0] = esp->esp_command[1] = 0xff;
		sbus_writeb(2, esp->eregs + ESP_TCLOW);
		sbus_writeb(0, esp->eregs + ESP_TCMED);
		tmp = sbus_readl(esp->dregs + DMA_CSR);
		tmp |= (DMA_ST_WRITE | DMA_ENABLE);
		sbus_writel(tmp, esp->dregs + DMA_CSR);
		if (esp->dma->revision == dvmaesc1)
			sbus_writel(0x100, esp->dregs + DMA_COUNT);
		sbus_writel(esp->esp_command_dvma, esp->dregs + DMA_ADDR);
		thecmd |= ESP_CMD_DMA;
	}
	esp_cmd(esp, thecmd);
	esp_advance_phase(esp->current_SC, in_status);

	return esp_do_status(esp);
}

static int esp_disconnect_amidst_phases(struct esp *esp)
{
	struct scsi_cmnd *sp = esp->current_SC;
	struct esp_device *esp_dev = sp->device->hostdata;

	/* This means real problems if we see this
	 * here.  Unless we were actually trying
	 * to force the device to abort/reset.
	 */
	ESPLOG(("esp%d Disconnect amidst phases, ", esp->esp_id));
	ESPLOG(("pphase<%s> cphase<%s>, ",
		phase_string(sp->SCp.phase),
		phase_string(sp->SCp.sent_command)));

	if (esp->disconnected_SC != NULL || (esp->erev == fashme))
		esp_cmd(esp, ESP_CMD_ESEL);

	switch (esp->cur_msgout[0]) {
	default:
		/* We didn't expect this to happen at all. */
		ESPLOG(("device is bolixed\n"));
		esp_advance_phase(sp, in_tgterror);
		esp_done(esp, (DID_ERROR << 16));
		break;

	case BUS_DEVICE_RESET:
		ESPLOG(("device reset successful\n"));
		esp_dev->sync_max_offset = 0;
		esp_dev->sync_min_period = 0;
		esp_dev->sync = 0;
		esp_advance_phase(sp, in_resetdev);
		esp_done(esp, (DID_RESET << 16));
		break;

	case ABORT:
		ESPLOG(("device abort successful\n"));
		esp_advance_phase(sp, in_abortone);
		esp_done(esp, (DID_ABORT << 16));
		break;

	};
	return do_intr_end;
}

static int esp_enter_msgout(struct esp *esp)
{
	esp_advance_phase(esp->current_SC, in_msgout);
	return esp_do_msgout(esp);
}

static int esp_enter_msgin(struct esp *esp)
{
	esp_advance_phase(esp->current_SC, in_msgin);
	return esp_do_msgin(esp);
}

static int esp_enter_cmd(struct esp *esp)
{
	esp_advance_phase(esp->current_SC, in_cmdbegin);
	return esp_do_cmdbegin(esp);
}

static int esp_enter_badphase(struct esp *esp)
{
	ESPLOG(("esp%d: Bizarre bus phase %2x.\n", esp->esp_id,
		esp->sreg & ESP_STAT_PMASK));
	return do_reset_bus;
}

typedef int (*espfunc_t)(struct esp *);

static espfunc_t phase_vector[] = {
	esp_do_data,		/* ESP_DOP */
	esp_do_data,		/* ESP_DIP */
	esp_enter_cmd,		/* ESP_CMDP */
	esp_enter_status,	/* ESP_STATP */
	esp_enter_badphase,	/* ESP_STAT_PMSG */
	esp_enter_badphase,	/* ESP_STAT_PMSG | ESP_STAT_PIO */
	esp_enter_msgout,	/* ESP_MOP */
	esp_enter_msgin,	/* ESP_MIP */
};

/* The target has control of the bus and we have to see where it has
 * taken us.
 */
static int esp_do_phase_determine(struct esp *esp)
{
	if ((esp->ireg & ESP_INTR_DC) != 0)
		return esp_disconnect_amidst_phases(esp);
	return phase_vector[esp->sreg & ESP_STAT_PMASK](esp);
}

/* First interrupt after exec'ing a cmd comes here. */
static int esp_select_complete(struct esp *esp)
{
	struct scsi_cmnd *SCptr = esp->current_SC;
	struct esp_device *esp_dev = SCptr->device->hostdata;
	int cmd_bytes_sent, fcnt;

	if (esp->erev != fashme)
		esp->seqreg = (sbus_readb(esp->eregs + ESP_SSTEP) & ESP_STEP_VBITS);

	if (esp->erev == fashme)
		fcnt = esp->hme_fifo_workaround_count;
	else
		fcnt = (sbus_readb(esp->eregs + ESP_FFLAGS) & ESP_FF_FBYTES);

	cmd_bytes_sent = esp_bytes_sent(esp, fcnt);
	dma_invalidate(esp);

	/* Let's check to see if a reselect happened
	 * while we we're trying to select.  This must
	 * be checked first.
	 */
	if (esp->ireg == (ESP_INTR_RSEL | ESP_INTR_FDONE)) {
		esp_reconnect(esp, SCptr);
		return esp_do_reconnect(esp);
	}

	/* Looks like things worked, we should see a bus service &
	 * a function complete interrupt at this point.  Note we
	 * are doing a direct comparison because we don't want to
	 * be fooled into thinking selection was successful if
	 * ESP_INTR_DC is set, see below.
	 */
	if (esp->ireg == (ESP_INTR_FDONE | ESP_INTR_BSERV)) {
		/* target speaks... */
		esp->targets_present |= (1<<SCptr->device->id);

		/* What if the target ignores the sdtr? */
		if (esp->snip)
			esp_dev->sync = 1;

		/* See how far, if at all, we got in getting
		 * the information out to the target.
		 */
		switch (esp->seqreg) {
		default:

		case ESP_STEP_ASEL:
			/* Arbitration won, target selected, but
			 * we are in some phase which is not command
			 * phase nor is it message out phase.
			 *
			 * XXX We've confused the target, obviously.
			 * XXX So clear it's state, but we also end
			 * XXX up clearing everyone elses.  That isn't
			 * XXX so nice.  I'd like to just reset this
			 * XXX target, but if I cannot even get it's
			 * XXX attention and finish selection to talk
			 * XXX to it, there is not much more I can do.
			 * XXX If we have a loaded bus we're going to
			 * XXX spend the next second or so renegotiating
			 * XXX for synchronous transfers.
			 */
			ESPLOG(("esp%d: STEP_ASEL for tgt %d\n",
				esp->esp_id, SCptr->device->id));

		case ESP_STEP_SID:
			/* Arbitration won, target selected, went
			 * to message out phase, sent one message
			 * byte, then we stopped.  ATN is asserted
			 * on the SCSI bus and the target is still
			 * there hanging on.  This is a legal
			 * sequence step if we gave the ESP a select
			 * and stop command.
			 *
			 * XXX See above, I could set the borken flag
			 * XXX in the device struct and retry the
			 * XXX command.  But would that help for
			 * XXX tagged capable targets?
			 */

		case ESP_STEP_NCMD:
			/* Arbitration won, target selected, maybe
			 * sent the one message byte in message out
			 * phase, but we did not go to command phase
			 * in the end.  Actually, we could have sent
			 * only some of the message bytes if we tried
			 * to send out the entire identify and tag
			 * message using ESP_CMD_SA3.
			 */
			cmd_bytes_sent = 0;
			break;

		case ESP_STEP_PPC:
			/* No, not the powerPC pinhead.  Arbitration
			 * won, all message bytes sent if we went to
			 * message out phase, went to command phase
			 * but only part of the command was sent.
			 *
			 * XXX I've seen this, but usually in conjunction
			 * XXX with a gross error which appears to have
			 * XXX occurred between the time I told the
			 * XXX ESP to arbitrate and when I got the
			 * XXX interrupt.  Could I have misloaded the
			 * XXX command bytes into the fifo?  Actually,
			 * XXX I most likely missed a phase, and therefore
			 * XXX went into never never land and didn't even
			 * XXX know it.  That was the old driver though.
			 * XXX What is even more peculiar is that the ESP
			 * XXX showed the proper function complete and
			 * XXX bus service bits in the interrupt register.
			 */

		case ESP_STEP_FINI4:
		case ESP_STEP_FINI5:
		case ESP_STEP_FINI6:
		case ESP_STEP_FINI7:
			/* Account for the identify message */
			if (SCptr->SCp.phase == in_slct_norm)
				cmd_bytes_sent -= 1;
		};

		if (esp->erev != fashme)
			esp_cmd(esp, ESP_CMD_NULL);

		/* Be careful, we could really get fucked during synchronous
		 * data transfers if we try to flush the fifo now.
		 */
		if ((esp->erev != fashme) && /* not a Happy Meal and... */
		    !fcnt && /* Fifo is empty and... */
		    /* either we are not doing synchronous transfers or... */
		    (!esp_dev->sync_max_offset ||
		     /* We are not going into data in phase. */
		     ((esp->sreg & ESP_STAT_PMASK) != ESP_DIP)))
			esp_cmd(esp, ESP_CMD_FLUSH); /* flush is safe */

		/* See how far we got if this is not a slow command. */
		if (!esp->esp_slowcmd) {
			if (cmd_bytes_sent < 0)
				cmd_bytes_sent = 0;
			if (cmd_bytes_sent != SCptr->cmd_len) {
				/* Crapola, mark it as a slowcmd
				 * so that we have some chance of
				 * keeping the command alive with
				 * good luck.
				 *
				 * XXX Actually, if we didn't send it all
				 * XXX this means either we didn't set things
				 * XXX up properly (driver bug) or the target
				 * XXX or the ESP detected parity on one of
				 * XXX the command bytes.  This makes much
				 * XXX more sense, and therefore this code
				 * XXX should be changed to send out a
				 * XXX parity error message or if the status
				 * XXX register shows no parity error then
				 * XXX just expect the target to bring the
				 * XXX bus into message in phase so that it
				 * XXX can send us the parity error message.
				 * XXX SCSI sucks...
				 */
				esp->esp_slowcmd = 1;
				esp->esp_scmdp = &(SCptr->cmnd[cmd_bytes_sent]);
				esp->esp_scmdleft = (SCptr->cmd_len - cmd_bytes_sent);
			}
		}

		/* Now figure out where we went. */
		esp_advance_phase(SCptr, in_the_dark);
		return esp_do_phase_determine(esp);
	}

	/* Did the target even make it? */
	if (esp->ireg == ESP_INTR_DC) {
		/* wheee... nobody there or they didn't like
		 * what we told it to do, clean up.
		 */

		/* If anyone is off the bus, but working on
		 * a command in the background for us, tell
		 * the ESP to listen for them.
		 */
		if (esp->disconnected_SC)
			esp_cmd(esp, ESP_CMD_ESEL);

		if (((1<<SCptr->device->id) & esp->targets_present) &&
		    esp->seqreg != 0 &&
		    (esp->cur_msgout[0] == EXTENDED_MESSAGE) &&
		    (SCptr->SCp.phase == in_slct_msg ||
		     SCptr->SCp.phase == in_slct_stop)) {
			/* shit */
			esp->snip = 0;
			ESPLOG(("esp%d: Failed synchronous negotiation for target %d "
				"lun %d\n", esp->esp_id, SCptr->device->id, SCptr->device->lun));
			esp_dev->sync_max_offset = 0;
			esp_dev->sync_min_period = 0;
			esp_dev->sync = 1; /* so we don't negotiate again */

			/* Run the command again, this time though we
			 * won't try to negotiate for synchronous transfers.
			 *
			 * XXX I'd like to do something like send an
			 * XXX INITIATOR_ERROR or ABORT message to the
			 * XXX target to tell it, "Sorry I confused you,
			 * XXX please come back and I will be nicer next
			 * XXX time".  But that requires having the target
			 * XXX on the bus, and it has dropped BSY on us.
			 */
			esp->current_SC = NULL;
			esp_advance_phase(SCptr, not_issued);
			prepend_SC(&esp->issue_SC, SCptr);
			esp_exec_cmd(esp);
			return do_intr_end;
		}

		/* Ok, this is normal, this is what we see during boot
		 * or whenever when we are scanning the bus for targets.
		 * But first make sure that is really what is happening.
		 */
		if (((1<<SCptr->device->id) & esp->targets_present)) {
			ESPLOG(("esp%d: Warning, live target %d not responding to "
				"selection.\n", esp->esp_id, SCptr->device->id));

			/* This _CAN_ happen.  The SCSI standard states that
			 * the target is to _not_ respond to selection if
			 * _it_ detects bad parity on the bus for any reason.
			 * Therefore, we assume that if we've talked successfully
			 * to this target before, bad parity is the problem.
			 */
			esp_done(esp, (DID_PARITY << 16));
		} else {
			/* Else, there really isn't anyone there. */
			ESPMISC(("esp: selection failure, maybe nobody there?\n"));
			ESPMISC(("esp: target %d lun %d\n",
				 SCptr->device->id, SCptr->device->lun));
			esp_done(esp, (DID_BAD_TARGET << 16));
		}
		return do_intr_end;
	}

	ESPLOG(("esp%d: Selection failure.\n", esp->esp_id));
	printk("esp%d: Currently -- ", esp->esp_id);
	esp_print_ireg(esp->ireg); printk(" ");
	esp_print_statreg(esp->sreg); printk(" ");
	esp_print_seqreg(esp->seqreg); printk("\n");
	printk("esp%d: New -- ", esp->esp_id);
	esp->sreg = sbus_readb(esp->eregs + ESP_STATUS);
	esp->seqreg = sbus_readb(esp->eregs + ESP_SSTEP);
	esp->ireg = sbus_readb(esp->eregs + ESP_INTRPT);
	esp_print_ireg(esp->ireg); printk(" ");
	esp_print_statreg(esp->sreg); printk(" ");
	esp_print_seqreg(esp->seqreg); printk("\n");
	ESPLOG(("esp%d: resetting bus\n", esp->esp_id));
	return do_reset_bus; /* ugh... */
}

/* Continue reading bytes for msgin phase. */
static int esp_do_msgincont(struct esp *esp)
{
	if (esp->ireg & ESP_INTR_BSERV) {
		/* in the right phase too? */
		if ((esp->sreg & ESP_STAT_PMASK) == ESP_MIP) {
			/* phew... */
			esp_cmd(esp, ESP_CMD_TI);
			esp_advance_phase(esp->current_SC, in_msgindone);
			return do_intr_end;
		}

		/* We changed phase but ESP shows bus service,
		 * in this case it is most likely that we, the
		 * hacker who has been up for 20hrs straight
		 * staring at the screen, drowned in coffee
		 * smelling like retched cigarette ashes
		 * have miscoded something..... so, try to
		 * recover as best we can.
		 */
		ESPLOG(("esp%d: message in mis-carriage.\n", esp->esp_id));
	}
	esp_advance_phase(esp->current_SC, in_the_dark);
	return do_phase_determine;
}

static int check_singlebyte_msg(struct esp *esp)
{
	esp->prevmsgin = esp->cur_msgin[0];
	if (esp->cur_msgin[0] & 0x80) {
		/* wheee... */
		ESPLOG(("esp%d: target sends identify amidst phases\n",
			esp->esp_id));
		esp_advance_phase(esp->current_SC, in_the_dark);
		return 0;
	} else if (((esp->cur_msgin[0] & 0xf0) == 0x20) ||
		   (esp->cur_msgin[0] == EXTENDED_MESSAGE)) {
		esp->msgin_len = 2;
		esp_advance_phase(esp->current_SC, in_msgincont);
		return 0;
	}
	esp_advance_phase(esp->current_SC, in_the_dark);
	switch (esp->cur_msgin[0]) {
	default:
		/* We don't want to hear about it. */
		ESPLOG(("esp%d: msg %02x which we don't know about\n", esp->esp_id,
			esp->cur_msgin[0]));
		return MESSAGE_REJECT;

	case NOP:
		ESPLOG(("esp%d: target %d sends a nop\n", esp->esp_id,
			esp->current_SC->device->id));
		return 0;

	case RESTORE_POINTERS:
		/* In this case we might also have to backup the
		 * "slow command" pointer.  It is rare to get such
		 * a save/restore pointer sequence so early in the
		 * bus transition sequences, but cover it.
		 */
		if (esp->esp_slowcmd) {
			esp->esp_scmdleft = esp->current_SC->cmd_len;
			esp->esp_scmdp = &esp->current_SC->cmnd[0];
		}
		esp_restore_pointers(esp, esp->current_SC);
		return 0;

	case SAVE_POINTERS:
		esp_save_pointers(esp, esp->current_SC);
		return 0;

	case COMMAND_COMPLETE:
	case DISCONNECT:
		/* Freeing the bus, let it go. */
		esp->current_SC->SCp.phase = in_freeing;
		return 0;

	case MESSAGE_REJECT:
		ESPMISC(("msg reject, "));
		if (esp->prevmsgout == EXTENDED_MESSAGE) {
			struct esp_device *esp_dev = esp->current_SC->device->hostdata;

			/* Doesn't look like this target can
			 * do synchronous or WIDE transfers.
			 */
			ESPSDTR(("got reject, was trying nego, clearing sync/WIDE\n"));
			esp_dev->sync = 1;
			esp_dev->wide = 1;
			esp_dev->sync_min_period = 0;
			esp_dev->sync_max_offset = 0;
			return 0;
		} else {
			ESPMISC(("not sync nego, sending ABORT\n"));
			return ABORT;
		}
	};
}

/* Target negotiates for synchronous transfers before we do, this
 * is legal although very strange.  What is even funnier is that
 * the SCSI2 standard specifically recommends against targets doing
 * this because so many initiators cannot cope with this occurring.
 */
static int target_with_ants_in_pants(struct esp *esp,
				     struct scsi_cmnd *SCptr,
				     struct esp_device *esp_dev)
{
	if (esp_dev->sync || SCptr->device->borken) {
		/* sorry, no can do */
		ESPSDTR(("forcing to async, "));
		build_sync_nego_msg(esp, 0, 0);
		esp_dev->sync = 1;
		esp->snip = 1;
		ESPLOG(("esp%d: hoping for msgout\n", esp->esp_id));
		esp_advance_phase(SCptr, in_the_dark);
		return EXTENDED_MESSAGE;
	}

	/* Ok, we'll check them out... */
	return 0;
}

static void sync_report(struct esp *esp)
{
	int msg3, msg4;
	char *type;

	msg3 = esp->cur_msgin[3];
	msg4 = esp->cur_msgin[4];
	if (msg4) {
		int hz = 1000000000 / (msg3 * 4);
		int integer = hz / 1000000;
		int fraction = (hz - (integer * 1000000)) / 10000;
		if ((esp->erev == fashme) &&
		    (esp->config3[esp->current_SC->device->id] & ESP_CONFIG3_EWIDE)) {
			type = "FAST-WIDE";
			integer <<= 1;
			fraction <<= 1;
		} else if ((msg3 * 4) < 200) {
			type = "FAST";
		} else {
			type = "synchronous";
		}

		/* Do not transform this back into one big printk
		 * again, it triggers a bug in our sparc64-gcc272
		 * sibling call optimization.  -DaveM
		 */
		ESPLOG((KERN_INFO "esp%d: target %d ",
			esp->esp_id, esp->current_SC->device->id));
		ESPLOG(("[period %dns offset %d %d.%02dMHz ",
			(int) msg3 * 4, (int) msg4,
			integer, fraction));
		ESPLOG(("%s SCSI%s]\n", type,
			(((msg3 * 4) < 200) ? "-II" : "")));
	} else {
		ESPLOG((KERN_INFO "esp%d: target %d asynchronous\n",
			esp->esp_id, esp->current_SC->device->id));
	}
}

static int check_multibyte_msg(struct esp *esp)
{
	struct scsi_cmnd *SCptr = esp->current_SC;
	struct esp_device *esp_dev = SCptr->device->hostdata;
	u8 regval = 0;
	int message_out = 0;

	ESPSDTR(("chk multibyte msg: "));
	if (esp->cur_msgin[2] == EXTENDED_SDTR) {
		int period = esp->cur_msgin[3];
		int offset = esp->cur_msgin[4];

		ESPSDTR(("is sync nego response, "));
		if (!esp->snip) {
			int rval;

			/* Target negotiates first! */
			ESPSDTR(("target jumps the gun, "));
			message_out = EXTENDED_MESSAGE; /* we must respond */
			rval = target_with_ants_in_pants(esp, SCptr, esp_dev);
			if (rval)
				return rval;
		}

		ESPSDTR(("examining sdtr, "));

		/* Offset cannot be larger than ESP fifo size. */
		if (offset > 15) {
			ESPSDTR(("offset too big %2x, ", offset));
			offset = 15;
			ESPSDTR(("sending back new offset\n"));
			build_sync_nego_msg(esp, period, offset);
			return EXTENDED_MESSAGE;
		}

		if (offset && period > esp->max_period) {
			/* Yeee, async for this slow device. */
			ESPSDTR(("period too long %2x, ", period));
			build_sync_nego_msg(esp, 0, 0);
			ESPSDTR(("hoping for msgout\n"));
			esp_advance_phase(esp->current_SC, in_the_dark);
			return EXTENDED_MESSAGE;
		} else if (offset && period < esp->min_period) {
			ESPSDTR(("period too short %2x, ", period));
			period = esp->min_period;
			if (esp->erev > esp236)
				regval = 4;
			else
				regval = 5;
		} else if (offset) {
			int tmp;

			ESPSDTR(("period is ok, "));
			tmp = esp->ccycle / 1000;
			regval = (((period << 2) + tmp - 1) / tmp);
			if (regval && ((esp->erev == fas100a ||
					esp->erev == fas236  ||
					esp->erev == fashme))) {
				if (period >= 50)
					regval--;
			}
		}

		if (offset) {
			u8 bit;

			esp_dev->sync_min_period = (regval & 0x1f);
			esp_dev->sync_max_offset = (offset | esp->radelay);
			if (esp->erev == fas100a || esp->erev == fas236 || esp->erev == fashme) {
				if ((esp->erev == fas100a) || (esp->erev == fashme))
					bit = ESP_CONFIG3_FAST;
				else
					bit = ESP_CONFIG3_FSCSI;
				if (period < 50) {
					/* On FAS366, if using fast-20 synchronous transfers
					 * we need to make sure the REQ/ACK assert/deassert
					 * control bits are clear.
					 */
					if (esp->erev == fashme)
						esp_dev->sync_max_offset &= ~esp->radelay;
					esp->config3[SCptr->device->id] |= bit;
				} else {
					esp->config3[SCptr->device->id] &= ~bit;
				}
				esp->prev_cfg3 = esp->config3[SCptr->device->id];
				sbus_writeb(esp->prev_cfg3, esp->eregs + ESP_CFG3);
			}
			esp->prev_soff = esp_dev->sync_max_offset;
			esp->prev_stp = esp_dev->sync_min_period;
			sbus_writeb(esp->prev_soff, esp->eregs + ESP_SOFF);
			sbus_writeb(esp->prev_stp, esp->eregs + ESP_STP);
			ESPSDTR(("soff=%2x stp=%2x cfg3=%2x\n",
				 esp_dev->sync_max_offset,
				 esp_dev->sync_min_period,
				 esp->config3[SCptr->device->id]));

			esp->snip = 0;
		} else if (esp_dev->sync_max_offset) {
			u8 bit;

			/* back to async mode */
			ESPSDTR(("unaccaptable sync nego, forcing async\n"));
			esp_dev->sync_max_offset = 0;
			esp_dev->sync_min_period = 0;
			esp->prev_soff = 0;
			esp->prev_stp = 0;
			sbus_writeb(esp->prev_soff, esp->eregs + ESP_SOFF);
			sbus_writeb(esp->prev_stp, esp->eregs + ESP_STP);
			if (esp->erev == fas100a || esp->erev == fas236 || esp->erev == fashme) {
				if ((esp->erev == fas100a) || (esp->erev == fashme))
					bit = ESP_CONFIG3_FAST;
				else
					bit = ESP_CONFIG3_FSCSI;
				esp->config3[SCptr->device->id] &= ~bit;
				esp->prev_cfg3 = esp->config3[SCptr->device->id];
				sbus_writeb(esp->prev_cfg3, esp->eregs + ESP_CFG3);
			}
		}

		sync_report(esp);

		ESPSDTR(("chk multibyte msg: sync is known, "));
		esp_dev->sync = 1;

		if (message_out) {
			ESPLOG(("esp%d: sending sdtr back, hoping for msgout\n",
				esp->esp_id));
			build_sync_nego_msg(esp, period, offset);
			esp_advance_phase(SCptr, in_the_dark);
			return EXTENDED_MESSAGE;
		}

		ESPSDTR(("returning zero\n"));
		esp_advance_phase(SCptr, in_the_dark); /* ...or else! */
		return 0;
	} else if (esp->cur_msgin[2] == EXTENDED_WDTR) {
		int size = 8 << esp->cur_msgin[3];

		esp->wnip = 0;
		if (esp->erev != fashme) {
			ESPLOG(("esp%d: AIEEE wide msg received and not HME.\n",
				esp->esp_id));
			message_out = MESSAGE_REJECT;
		} else if (size > 16) {
			ESPLOG(("esp%d: AIEEE wide transfer for %d size "
				"not supported.\n", esp->esp_id, size));
			message_out = MESSAGE_REJECT;
		} else {
			/* Things look good; let's see what we got. */
			if (size == 16) {
				/* Set config 3 register for this target. */
				esp->config3[SCptr->device->id] |= ESP_CONFIG3_EWIDE;
			} else {
				/* Just make sure it was one byte sized. */
				if (size != 8) {
					ESPLOG(("esp%d: Aieee, wide nego of %d size.\n",
						esp->esp_id, size));
					message_out = MESSAGE_REJECT;
					goto finish;
				}
				/* Pure paranoia. */
				esp->config3[SCptr->device->id] &= ~(ESP_CONFIG3_EWIDE);
			}
			esp->prev_cfg3 = esp->config3[SCptr->device->id];
			sbus_writeb(esp->prev_cfg3, esp->eregs + ESP_CFG3);

			/* Regardless, next try for sync transfers. */
			build_sync_nego_msg(esp, esp->sync_defp, 15);
			esp_dev->sync = 1;
			esp->snip = 1;
			message_out = EXTENDED_MESSAGE;
		}
	} else if (esp->cur_msgin[2] == EXTENDED_MODIFY_DATA_POINTER) {
		ESPLOG(("esp%d: rejecting modify data ptr msg\n", esp->esp_id));
		message_out = MESSAGE_REJECT;
	}
finish:
	esp_advance_phase(SCptr, in_the_dark);
	return message_out;
}

static int esp_do_msgindone(struct esp *esp)
{
	struct scsi_cmnd *SCptr = esp->current_SC;
	int message_out = 0, it = 0, rval;

	rval = skipahead1(esp, SCptr, in_msgin, in_msgindone);
	if (rval)
		return rval;
	if (SCptr->SCp.sent_command != in_status) {
		if (!(esp->ireg & ESP_INTR_DC)) {
			if (esp->msgin_len && (esp->sreg & ESP_STAT_PERR)) {
				message_out = MSG_PARITY_ERROR;
				esp_cmd(esp, ESP_CMD_FLUSH);
			} else if (esp->erev != fashme &&
			  (it = (sbus_readb(esp->eregs + ESP_FFLAGS) & ESP_FF_FBYTES)) != 1) {
				/* We certainly dropped the ball somewhere. */
				message_out = INITIATOR_ERROR;
				esp_cmd(esp, ESP_CMD_FLUSH);
			} else if (!esp->msgin_len) {
				if (esp->erev == fashme)
					it = esp->hme_fifo_workaround_buffer[0];
				else
					it = sbus_readb(esp->eregs + ESP_FDATA);
				esp_advance_phase(SCptr, in_msgincont);
			} else {
				/* it is ok and we want it */
				if (esp->erev == fashme)
					it = esp->cur_msgin[esp->msgin_ctr] =
						esp->hme_fifo_workaround_buffer[0];
				else
					it = esp->cur_msgin[esp->msgin_ctr] =
						sbus_readb(esp->eregs + ESP_FDATA);
				esp->msgin_ctr++;
			}
		} else {
			esp_advance_phase(SCptr, in_the_dark);
			return do_work_bus;
		}
	} else {
		it = esp->cur_msgin[0];
	}
	if (!message_out && esp->msgin_len) {
		if (esp->msgin_ctr < esp->msgin_len) {
			esp_advance_phase(SCptr, in_msgincont);
		} else if (esp->msgin_len == 1) {
			message_out = check_singlebyte_msg(esp);
		} else if (esp->msgin_len == 2) {
			if (esp->cur_msgin[0] == EXTENDED_MESSAGE) {
				if ((it + 2) >= 15) {
					message_out = MESSAGE_REJECT;
				} else {
					esp->msgin_len = (it + 2);
					esp_advance_phase(SCptr, in_msgincont);
				}
			} else {
				message_out = MESSAGE_REJECT; /* foo on you */
			}
		} else {
			message_out = check_multibyte_msg(esp);
		}
	}
	if (message_out < 0) {
		return -message_out;
	} else if (message_out) {
		if (((message_out != 1) &&
		     ((message_out < 0x20) || (message_out & 0x80))))
			esp->msgout_len = 1;
		esp->cur_msgout[0] = message_out;
		esp_cmd(esp, ESP_CMD_SATN);
		esp_advance_phase(SCptr, in_the_dark);
		esp->msgin_len = 0;
	}
	esp->sreg = sbus_readb(esp->eregs + ESP_STATUS);
	esp->sreg &= ~(ESP_STAT_INTR);
	if ((esp->sreg & (ESP_STAT_PMSG|ESP_STAT_PCD)) == (ESP_STAT_PMSG|ESP_STAT_PCD))
		esp_cmd(esp, ESP_CMD_MOK);
	if ((SCptr->SCp.sent_command == in_msgindone) &&
	    (SCptr->SCp.phase == in_freeing))
		return esp_do_freebus(esp);
	return do_intr_end;
}

static int esp_do_cmdbegin(struct esp *esp)
{
	struct scsi_cmnd *SCptr = esp->current_SC;

	esp_advance_phase(SCptr, in_cmdend);
	if (esp->erev == fashme) {
		u32 tmp = sbus_readl(esp->dregs + DMA_CSR);
		int i;

		for (i = 0; i < esp->esp_scmdleft; i++)
			esp->esp_command[i] = *esp->esp_scmdp++;
		esp->esp_scmdleft = 0;
		esp_cmd(esp, ESP_CMD_FLUSH);
		esp_setcount(esp->eregs, i, 1);
		esp_cmd(esp, (ESP_CMD_DMA | ESP_CMD_TI));
		tmp |= (DMA_SCSI_DISAB | DMA_ENABLE);
		tmp &= ~(DMA_ST_WRITE);
		sbus_writel(i, esp->dregs + DMA_COUNT);
		sbus_writel(esp->esp_command_dvma, esp->dregs + DMA_ADDR);
		sbus_writel(tmp, esp->dregs + DMA_CSR);
	} else {
		u8 tmp;

		esp_cmd(esp, ESP_CMD_FLUSH);
		tmp = *esp->esp_scmdp++;
		esp->esp_scmdleft--;
		sbus_writeb(tmp, esp->eregs + ESP_FDATA);
		esp_cmd(esp, ESP_CMD_TI);
	}
	return do_intr_end;
}

static int esp_do_cmddone(struct esp *esp)
{
	if (esp->erev == fashme)
		dma_invalidate(esp);
	else
		esp_cmd(esp, ESP_CMD_NULL);

	if (esp->ireg & ESP_INTR_BSERV) {
		esp_advance_phase(esp->current_SC, in_the_dark);
		return esp_do_phase_determine(esp);
	}

	ESPLOG(("esp%d: in do_cmddone() but didn't get BSERV interrupt.\n",
		esp->esp_id));
	return do_reset_bus;
}

static int esp_do_msgout(struct esp *esp)
{
	esp_cmd(esp, ESP_CMD_FLUSH);
	switch (esp->msgout_len) {
	case 1:
		if (esp->erev == fashme)
			hme_fifo_push(esp, &esp->cur_msgout[0], 1);
		else
			sbus_writeb(esp->cur_msgout[0], esp->eregs + ESP_FDATA);

		esp_cmd(esp, ESP_CMD_TI);
		break;

	case 2:
		esp->esp_command[0] = esp->cur_msgout[0];
		esp->esp_command[1] = esp->cur_msgout[1];

		if (esp->erev == fashme) {
			hme_fifo_push(esp, &esp->cur_msgout[0], 2);
			esp_cmd(esp, ESP_CMD_TI);
		} else {
			dma_setup(esp, esp->esp_command_dvma, 2, 0);
			esp_setcount(esp->eregs, 2, 0);
			esp_cmd(esp, ESP_CMD_DMA | ESP_CMD_TI);
		}
		break;

	case 4:
		esp->esp_command[0] = esp->cur_msgout[0];
		esp->esp_command[1] = esp->cur_msgout[1];
		esp->esp_command[2] = esp->cur_msgout[2];
		esp->esp_command[3] = esp->cur_msgout[3];
		esp->snip = 1;

		if (esp->erev == fashme) {
			hme_fifo_push(esp, &esp->cur_msgout[0], 4);
			esp_cmd(esp, ESP_CMD_TI);
		} else {
			dma_setup(esp, esp->esp_command_dvma, 4, 0);
			esp_setcount(esp->eregs, 4, 0);
			esp_cmd(esp, ESP_CMD_DMA | ESP_CMD_TI);
		}
		break;

	case 5:
		esp->esp_command[0] = esp->cur_msgout[0];
		esp->esp_command[1] = esp->cur_msgout[1];
		esp->esp_command[2] = esp->cur_msgout[2];
		esp->esp_command[3] = esp->cur_msgout[3];
		esp->esp_command[4] = esp->cur_msgout[4];
		esp->snip = 1;

		if (esp->erev == fashme) {
			hme_fifo_push(esp, &esp->cur_msgout[0], 5);
			esp_cmd(esp, ESP_CMD_TI);
		} else {
			dma_setup(esp, esp->esp_command_dvma, 5, 0);
			esp_setcount(esp->eregs, 5, 0);
			esp_cmd(esp, ESP_CMD_DMA | ESP_CMD_TI);
		}
		break;

	default:
		/* whoops */
		ESPMISC(("bogus msgout sending NOP\n"));
		esp->cur_msgout[0] = NOP;

		if (esp->erev == fashme) {
			hme_fifo_push(esp, &esp->cur_msgout[0], 1);
		} else {
			sbus_writeb(esp->cur_msgout[0], esp->eregs + ESP_FDATA);
		}

		esp->msgout_len = 1;
		esp_cmd(esp, ESP_CMD_TI);
		break;
	};

	esp_advance_phase(esp->current_SC, in_msgoutdone);
	return do_intr_end;
}

static int esp_do_msgoutdone(struct esp *esp)
{
	if (esp->msgout_len > 1) {
		/* XXX HME/FAS ATN deassert workaround required,
		 * XXX no DMA flushing, only possible ESP_CMD_FLUSH
		 * XXX to kill the fifo.
		 */
		if (esp->erev != fashme) {
			u32 tmp;

			while ((tmp = sbus_readl(esp->dregs + DMA_CSR)) & DMA_PEND_READ)
				udelay(1);
			tmp &= ~DMA_ENABLE;
			sbus_writel(tmp, esp->dregs + DMA_CSR);
			dma_invalidate(esp);
		} else {
			esp_cmd(esp, ESP_CMD_FLUSH);
		}
	}
	if (!(esp->ireg & ESP_INTR_DC)) {
		if (esp->erev != fashme)
			esp_cmd(esp, ESP_CMD_NULL);
		switch (esp->sreg & ESP_STAT_PMASK) {
		case ESP_MOP:
			/* whoops, parity error */
			ESPLOG(("esp%d: still in msgout, parity error assumed\n",
				esp->esp_id));
			if (esp->msgout_len > 1)
				esp_cmd(esp, ESP_CMD_SATN);
			esp_advance_phase(esp->current_SC, in_msgout);
			return do_work_bus;

		case ESP_DIP:
			break;

		default:
			/* Happy Meal fifo is touchy... */
			if ((esp->erev != fashme) &&
			    !fcount(esp) &&
			    !(((struct esp_device *)esp->current_SC->device->hostdata)->sync_max_offset))
				esp_cmd(esp, ESP_CMD_FLUSH);
			break;

		};
	} else {
		ESPLOG(("esp%d: disconnect, resetting bus\n", esp->esp_id));
		return do_reset_bus;
	}

	/* If we sent out a synchronous negotiation message, update
	 * our state.
	 */
	if (esp->cur_msgout[2] == EXTENDED_MESSAGE &&
	    esp->cur_msgout[4] == EXTENDED_SDTR) {
		esp->snip = 1; /* anal retentiveness... */
	}

	esp->prevmsgout = esp->cur_msgout[0];
	esp->msgout_len = 0;
	esp_advance_phase(esp->current_SC, in_the_dark);
	return esp_do_phase_determine(esp);
}

static int esp_bus_unexpected(struct esp *esp)
{
	ESPLOG(("esp%d: command in weird state %2x\n",
		esp->esp_id, esp->current_SC->SCp.phase));
	return do_reset_bus;
}

static espfunc_t bus_vector[] = {
	esp_do_data_finale,
	esp_do_data_finale,
	esp_bus_unexpected,
	esp_do_msgin,
	esp_do_msgincont,
	esp_do_msgindone,
	esp_do_msgout,
	esp_do_msgoutdone,
	esp_do_cmdbegin,
	esp_do_cmddone,
	esp_do_status,
	esp_do_freebus,
	esp_do_phase_determine,
	esp_bus_unexpected,
	esp_bus_unexpected,
	esp_bus_unexpected,
};

/* This is the second tier in our dual-level SCSI state machine. */
static int esp_work_bus(struct esp *esp)
{
	struct scsi_cmnd *SCptr = esp->current_SC;
	unsigned int phase;

	ESPBUS(("esp_work_bus: "));
	if (!SCptr) {
		ESPBUS(("reconnect\n"));
		return esp_do_reconnect(esp);
	}
	phase = SCptr->SCp.phase;
	if ((phase & 0xf0) == in_phases_mask)
		return bus_vector[(phase & 0x0f)](esp);
	else if ((phase & 0xf0) == in_slct_mask)
		return esp_select_complete(esp);
	else
		return esp_bus_unexpected(esp);
}

static espfunc_t isvc_vector[] = {
	NULL,
	esp_do_phase_determine,
	esp_do_resetbus,
	esp_finish_reset,
	esp_work_bus
};

/* Main interrupt handler for an esp adapter. */
static void esp_handle(struct esp *esp)
{
	struct scsi_cmnd *SCptr;
	int what_next = do_intr_end;

	SCptr = esp->current_SC;

	/* Check for errors. */
	esp->sreg = sbus_readb(esp->eregs + ESP_STATUS);
	esp->sreg &= (~ESP_STAT_INTR);
	if (esp->erev == fashme) {
		esp->sreg2 = sbus_readb(esp->eregs + ESP_STATUS2);
		esp->seqreg = (sbus_readb(esp->eregs + ESP_SSTEP) & ESP_STEP_VBITS);
	}

	if (esp->sreg & (ESP_STAT_SPAM)) {
		/* Gross error, could be due to one of:
		 *
		 * - top of fifo overwritten, could be because
		 *   we tried to do a synchronous transfer with
		 *   an offset greater than ESP fifo size
		 *
		 * - top of command register overwritten
		 *
		 * - DMA setup to go in one direction, SCSI
		 *   bus points in the other, whoops
		 *
		 * - weird phase change during asynchronous
		 *   data phase while we are initiator
		 */
		ESPLOG(("esp%d: Gross error sreg=%2x\n", esp->esp_id, esp->sreg));

		/* If a command is live on the bus we cannot safely
		 * reset the bus, so we'll just let the pieces fall
		 * where they may.  Here we are hoping that the
		 * target will be able to cleanly go away soon
		 * so we can safely reset things.
		 */
		if (!SCptr) {
			ESPLOG(("esp%d: No current cmd during gross error, "
				"resetting bus\n", esp->esp_id));
			what_next = do_reset_bus;
			goto state_machine;
		}
	}

	if (sbus_readl(esp->dregs + DMA_CSR) & DMA_HNDL_ERROR) {
		/* A DMA gate array error.  Here we must
		 * be seeing one of two things.  Either the
		 * virtual to physical address translation
		 * on the SBUS could not occur, else the
		 * translation it did get pointed to a bogus
		 * page.  Ho hum...
		 */
		ESPLOG(("esp%d: DMA error %08x\n", esp->esp_id,
			sbus_readl(esp->dregs + DMA_CSR)));

		/* DMA gate array itself must be reset to clear the
		 * error condition.
		 */
		esp_reset_dma(esp);

		what_next = do_reset_bus;
		goto state_machine;
	}

	esp->ireg = sbus_readb(esp->eregs + ESP_INTRPT);   /* Unlatch intr reg */

	if (esp->erev == fashme) {
		/* This chip is really losing. */
		ESPHME(("HME["));

		ESPHME(("sreg2=%02x,", esp->sreg2));
		/* Must latch fifo before reading the interrupt
		 * register else garbage ends up in the FIFO
		 * which confuses the driver utterly.
		 */
		if (!(esp->sreg2 & ESP_STAT2_FEMPTY) ||
		    (esp->sreg2 & ESP_STAT2_F1BYTE)) {
			ESPHME(("fifo_workaround]"));
			hme_fifo_read(esp);
		} else {
			ESPHME(("no_fifo_workaround]"));
		}
	}

	/* No current cmd is only valid at this point when there are
	 * commands off the bus or we are trying a reset.
	 */
	if (!SCptr && !esp->disconnected_SC && !(esp->ireg & ESP_INTR_SR)) {
		/* Panic is safe, since current_SC is null. */
		ESPLOG(("esp%d: no command in esp_handle()\n", esp->esp_id));
		panic("esp_handle: current_SC == penguin within interrupt!");
	}

	if (esp->ireg & (ESP_INTR_IC)) {
		/* Illegal command fed to ESP.  Outside of obvious
		 * software bugs that could cause this, there is
		 * a condition with esp100 where we can confuse the
		 * ESP into an erroneous illegal command interrupt
		 * because it does not scrape the FIFO properly
		 * for reselection.  See esp100_reconnect_hwbug()
		 * to see how we try very hard to avoid this.
		 */
		ESPLOG(("esp%d: invalid command\n", esp->esp_id));

		esp_dump_state(esp);

		if (SCptr != NULL) {
			/* Devices with very buggy firmware can drop BSY
			 * during a scatter list interrupt when using sync
			 * mode transfers.  We continue the transfer as
			 * expected, the target drops the bus, the ESP
			 * gets confused, and we get a illegal command
			 * interrupt because the bus is in the disconnected
			 * state now and ESP_CMD_TI is only allowed when
			 * a nexus is alive on the bus.
			 */
			ESPLOG(("esp%d: Forcing async and disabling disconnect for "
				"target %d\n", esp->esp_id, SCptr->device->id));
			SCptr->device->borken = 1; /* foo on you */
		}

		what_next = do_reset_bus;
	} else if (!(esp->ireg & ~(ESP_INTR_FDONE | ESP_INTR_BSERV | ESP_INTR_DC))) {
		if (SCptr) {
			unsigned int phase = SCptr->SCp.phase;

			if (phase & in_phases_mask) {
				what_next = esp_work_bus(esp);
			} else if (phase & in_slct_mask) {
				what_next = esp_select_complete(esp);
			} else {
				ESPLOG(("esp%d: interrupt for no good reason...\n",
					esp->esp_id));
				what_next = do_intr_end;
			}
		} else {
			ESPLOG(("esp%d: BSERV or FDONE or DC while SCptr==NULL\n",
				esp->esp_id));
			what_next = do_reset_bus;
		}
	} else if (esp->ireg & ESP_INTR_SR) {
		ESPLOG(("esp%d: SCSI bus reset interrupt\n", esp->esp_id));
		what_next = do_reset_complete;
	} else if (esp->ireg & (ESP_INTR_S | ESP_INTR_SATN)) {
		ESPLOG(("esp%d: AIEEE we have been selected by another initiator!\n",
			esp->esp_id));
		what_next = do_reset_bus;
	} else if (esp->ireg & ESP_INTR_RSEL) {
		if (SCptr == NULL) {
			/* This is ok. */
			what_next = esp_do_reconnect(esp);
		} else if (SCptr->SCp.phase & in_slct_mask) {
			/* Only selection code knows how to clean
			 * up properly.
			 */
			ESPDISC(("Reselected during selection attempt\n"));
			what_next = esp_select_complete(esp);
		} else {
			ESPLOG(("esp%d: Reselected while bus is busy\n",
				esp->esp_id));
			what_next = do_reset_bus;
		}
	}

	/* This is tier-one in our dual level SCSI state machine. */
state_machine:
	while (what_next != do_intr_end) {
		if (what_next >= do_phase_determine &&
		    what_next < do_intr_end) {
			what_next = isvc_vector[what_next](esp);
		} else {
			/* state is completely lost ;-( */
			ESPLOG(("esp%d: interrupt engine loses state, resetting bus\n",
				esp->esp_id));
			what_next = do_reset_bus;
		}
	}
}

/* Service only the ESP described by dev_id. */
static irqreturn_t esp_intr(int irq, void *dev_id, struct pt_regs *pregs)
{
	struct esp *esp = dev_id;
	unsigned long flags;

	spin_lock_irqsave(esp->ehost->host_lock, flags);
	if (ESP_IRQ_P(esp->dregs)) {
		ESP_INTSOFF(esp->dregs);

		ESPIRQ(("I[%d:%d](", smp_processor_id(), esp->esp_id));
		esp_handle(esp);
		ESPIRQ((")"));

		ESP_INTSON(esp->dregs);
	}
	spin_unlock_irqrestore(esp->ehost->host_lock, flags);

	return IRQ_HANDLED;
}

static int esp_slave_alloc(struct scsi_device *SDptr)
{
	struct esp_device *esp_dev =
		kmalloc(sizeof(struct esp_device), GFP_ATOMIC);

	if (!esp_dev)
		return -ENOMEM;
	memset(esp_dev, 0, sizeof(struct esp_device));
	SDptr->hostdata = esp_dev;
	return 0;
}

static void esp_slave_destroy(struct scsi_device *SDptr)
{
	struct esp *esp = (struct esp *) SDptr->host->hostdata;

	esp->targets_present &= ~(1 << SDptr->id);
	kfree(SDptr->hostdata);
	SDptr->hostdata = NULL;
}

static struct scsi_host_template driver_template = {
	.proc_name		= "esp",
	.proc_info		= esp_proc_info,
	.name			= "Sun ESP 100/100a/200",
	.detect			= esp_detect,
	.slave_alloc		= esp_slave_alloc,
	.slave_destroy		= esp_slave_destroy,
	.release		= esp_release,
	.info			= esp_info,
	.queuecommand		= esp_queue,
	.eh_abort_handler	= esp_abort,
	.eh_bus_reset_handler	= esp_reset,
	.can_queue		= 7,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 1,
	.use_clustering		= ENABLE_CLUSTERING,
};

#include "scsi_module.c"

MODULE_DESCRIPTION("EnhancedScsiProcessor Sun SCSI driver");
MODULE_AUTHOR("David S. Miller (davem@redhat.com)");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

