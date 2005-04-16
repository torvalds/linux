/*
 *    in2000.h -  Linux device driver definitions for the
 *                Always IN2000 ISA SCSI card.
 *
 *    IMPORTANT: This file is for version 1.33 - 26/Aug/1998
 *
 * Copyright (c) 1996 John Shifflett, GeoLog Consulting
 *    john@geolog.com
 *    jshiffle@netcom.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef IN2000_H
#define IN2000_H

#include <asm/io.h>

#define PROC_INTERFACE     /* add code for /proc/scsi/in2000/xxx interface */
#ifdef  PROC_INTERFACE
#define PROC_STATISTICS    /* add code for keeping various real time stats */
#endif

#define SYNC_DEBUG         /* extra info on sync negotiation printed */
#define DEBUGGING_ON       /* enable command-line debugging bitmask */
#define DEBUG_DEFAULTS 0   /* default bitmask - change from command-line */

#ifdef __i386__
#define FAST_READ_IO       /* No problems with these on my machine */
#define FAST_WRITE_IO
#endif

#ifdef DEBUGGING_ON
#define DB(f,a) if (hostdata->args & (f)) a;
#define CHECK_NULL(p,s) /* if (!(p)) {printk("\n"); while (1) printk("NP:%s\r",(s));} */
#else
#define DB(f,a)
#define CHECK_NULL(p,s)
#endif

#define uchar unsigned char

#define read1_io(a)     (inb(hostdata->io_base+(a)))
#define read2_io(a)     (inw(hostdata->io_base+(a)))
#define write1_io(b,a)  (outb((b),hostdata->io_base+(a)))
#define write2_io(w,a)  (outw((w),hostdata->io_base+(a)))

#ifdef __i386__
/* These inline assembly defines are derived from a patch
 * sent to me by Bill Earnest. He's done a lot of very
 * valuable thinking, testing, and coding during his effort
 * to squeeze more speed out of this driver. I really think
 * that we are doing IO at close to the maximum now with
 * the fifo. (And yes, insw uses 'edi' while outsw uses
 * 'esi'. Thanks Bill!)
 */

#define FAST_READ2_IO()    \
({ \
int __dummy_1,__dummy_2; \
   __asm__ __volatile__ ("\n \
   cld                    \n \
   orl %%ecx, %%ecx       \n \
   jz 1f                  \n \
   rep                    \n \
   insw (%%dx),%%es:(%%edi) \n \
1: "                       \
   : "=D" (sp) ,"=c" (__dummy_1) ,"=d" (__dummy_2)  /* output */   \
   : "2" (f), "0" (sp), "1" (i)  /* input */    \
   );       /* trashed */ \
})

#define FAST_WRITE2_IO()   \
({ \
int __dummy_1,__dummy_2; \
   __asm__ __volatile__ ("\n \
   cld                    \n \
   orl %%ecx, %%ecx       \n \
   jz 1f                  \n \
   rep                    \n \
   outsw %%ds:(%%esi),(%%dx) \n \
1: "                       \
   : "=S" (sp) ,"=c" (__dummy_1) ,"=d" (__dummy_2)/* output */   \
   : "2" (f), "0" (sp), "1" (i)  /* input */    \
   );       /* trashed */ \
})
#endif

/* IN2000 io_port offsets */
#define IO_WD_ASR       0x00     /* R - 3393 auxstat reg */
#define     ASR_INT        0x80
#define     ASR_LCI        0x40
#define     ASR_BSY        0x20
#define     ASR_CIP        0x10
#define     ASR_PE         0x02
#define     ASR_DBR        0x01
#define IO_WD_ADDR      0x00     /* W - 3393 address reg */
#define IO_WD_DATA      0x01     /* R/W - rest of 3393 regs */
#define IO_FIFO         0x02     /* R/W - in2000 dual-port fifo (16 bits) */
#define IN2000_FIFO_SIZE   2048  /*    fifo capacity in bytes */
#define IO_CARD_RESET   0x03     /* W - in2000 start master reset */
#define IO_FIFO_COUNT   0x04     /* R - in2000 fifo counter */
#define IO_FIFO_WRITE   0x05     /* W - clear fifo counter, start write */
#define IO_FIFO_READ    0x07     /* W - start fifo read */
#define IO_LED_OFF      0x08     /* W - turn off in2000 activity LED */
#define IO_SWITCHES     0x08     /* R - read in2000 dip switch */
#define     SW_ADDR0       0x01  /*    bit 0 = bit 0 of index to io addr */
#define     SW_ADDR1       0x02  /*    bit 1 = bit 1 of index io addr */
#define     SW_DISINT      0x04  /*    bit 2 true if ints disabled */
#define     SW_INT0        0x08  /*    bit 3 = bit 0 of index to interrupt */
#define     SW_INT1        0x10  /*    bit 4 = bit 1 of index to interrupt */
#define     SW_INT_SHIFT   3     /*    shift right this amount to right justify int bits */
#define     SW_SYNC_DOS5   0x20  /*    bit 5 used by Always BIOS */
#define     SW_FLOPPY      0x40  /*    bit 6 true if floppy enabled */
#define     SW_BIT7        0x80  /*    bit 7 hardwired true (ground) */
#define IO_LED_ON       0x09     /* W - turn on in2000 activity LED */
#define IO_HARDWARE     0x0a     /* R - read in2000 hardware rev, stop reset */
#define IO_INTR_MASK    0x0c     /* W - in2000 interrupt mask reg */
#define     IMASK_WD       0x01  /*    WD33c93 interrupt mask */
#define     IMASK_FIFO     0x02  /*    FIFO interrupt mask */

/* wd register names */
#define WD_OWN_ID    0x00
#define WD_CONTROL   0x01
#define WD_TIMEOUT_PERIOD  0x02
#define WD_CDB_1     0x03
#define WD_CDB_2     0x04
#define WD_CDB_3     0x05
#define WD_CDB_4     0x06
#define WD_CDB_5     0x07
#define WD_CDB_6     0x08
#define WD_CDB_7     0x09
#define WD_CDB_8     0x0a
#define WD_CDB_9     0x0b
#define WD_CDB_10    0x0c
#define WD_CDB_11    0x0d
#define WD_CDB_12    0x0e
#define WD_TARGET_LUN      0x0f
#define WD_COMMAND_PHASE   0x10
#define WD_SYNCHRONOUS_TRANSFER  0x11
#define WD_TRANSFER_COUNT_MSB 0x12
#define WD_TRANSFER_COUNT  0x13
#define WD_TRANSFER_COUNT_LSB 0x14
#define WD_DESTINATION_ID  0x15
#define WD_SOURCE_ID    0x16
#define WD_SCSI_STATUS     0x17
#define WD_COMMAND      0x18
#define WD_DATA      0x19
#define WD_QUEUE_TAG    0x1a
#define WD_AUXILIARY_STATUS   0x1f

/* WD commands */
#define WD_CMD_RESET    0x00
#define WD_CMD_ABORT    0x01
#define WD_CMD_ASSERT_ATN  0x02
#define WD_CMD_NEGATE_ACK  0x03
#define WD_CMD_DISCONNECT  0x04
#define WD_CMD_RESELECT    0x05
#define WD_CMD_SEL_ATN     0x06
#define WD_CMD_SEL      0x07
#define WD_CMD_SEL_ATN_XFER   0x08
#define WD_CMD_SEL_XFER    0x09
#define WD_CMD_RESEL_RECEIVE  0x0a
#define WD_CMD_RESEL_SEND  0x0b
#define WD_CMD_WAIT_SEL_RECEIVE 0x0c
#define WD_CMD_TRANS_ADDR  0x18
#define WD_CMD_TRANS_INFO  0x20
#define WD_CMD_TRANSFER_PAD   0x21
#define WD_CMD_SBT_MODE    0x80

/* SCSI Bus Phases */
#define PHS_DATA_OUT    0x00
#define PHS_DATA_IN     0x01
#define PHS_COMMAND     0x02
#define PHS_STATUS      0x03
#define PHS_MESS_OUT    0x06
#define PHS_MESS_IN     0x07

/* Command Status Register definitions */

  /* reset state interrupts */
#define CSR_RESET    0x00
#define CSR_RESET_AF    0x01

  /* successful completion interrupts */
#define CSR_RESELECT    0x10
#define CSR_SELECT      0x11
#define CSR_SEL_XFER_DONE  0x16
#define CSR_XFER_DONE      0x18

  /* paused or aborted interrupts */
#define CSR_MSGIN    0x20
#define CSR_SDP         0x21
#define CSR_SEL_ABORT      0x22
#define CSR_RESEL_ABORT    0x25
#define CSR_RESEL_ABORT_AM 0x27
#define CSR_ABORT    0x28

  /* terminated interrupts */
#define CSR_INVALID     0x40
#define CSR_UNEXP_DISC     0x41
#define CSR_TIMEOUT     0x42
#define CSR_PARITY      0x43
#define CSR_PARITY_ATN     0x44
#define CSR_BAD_STATUS     0x45
#define CSR_UNEXP    0x48

  /* service required interrupts */
#define CSR_RESEL    0x80
#define CSR_RESEL_AM    0x81
#define CSR_DISC     0x85
#define CSR_SRV_REQ     0x88

   /* Own ID/CDB Size register */
#define OWNID_EAF    0x08
#define OWNID_EHP    0x10
#define OWNID_RAF    0x20
#define OWNID_FS_8   0x00
#define OWNID_FS_12  0x40
#define OWNID_FS_16  0x80

   /* Control register */
#define CTRL_HSP     0x01
#define CTRL_HA      0x02
#define CTRL_IDI     0x04
#define CTRL_EDI     0x08
#define CTRL_HHP     0x10
#define CTRL_POLLED  0x00
#define CTRL_BURST   0x20
#define CTRL_BUS     0x40
#define CTRL_DMA     0x80

   /* Timeout Period register */
#define TIMEOUT_PERIOD_VALUE  20    /* results in 200 ms. */

   /* Synchronous Transfer Register */
#define STR_FSS      0x80

   /* Destination ID register */
#define DSTID_DPD    0x40
#define DATA_OUT_DIR 0
#define DATA_IN_DIR  1
#define DSTID_SCC    0x80

   /* Source ID register */
#define SRCID_MASK   0x07
#define SRCID_SIV    0x08
#define SRCID_DSP    0x20
#define SRCID_ES     0x40
#define SRCID_ER     0x80



#define ILLEGAL_STATUS_BYTE   0xff


#define DEFAULT_SX_PER     500   /* (ns) fairly safe */
#define DEFAULT_SX_OFF     0     /* aka async */

#define OPTIMUM_SX_PER     252   /* (ns) best we can do (mult-of-4) */
#define OPTIMUM_SX_OFF     12    /* size of in2000 fifo */

struct sx_period {
   unsigned int   period_ns;
   uchar          reg_value;
   };


struct IN2000_hostdata {
    struct Scsi_Host *next;
    uchar            chip;             /* what kind of wd33c93 chip? */
    uchar            microcode;        /* microcode rev if 'B' */
    unsigned short   io_base;          /* IO port base */
    unsigned int     dip_switch;       /* dip switch settings */
    unsigned int     hrev;             /* hardware revision of card */
    volatile uchar   busy[8];          /* index = target, bit = lun */
    volatile Scsi_Cmnd *input_Q;       /* commands waiting to be started */
    volatile Scsi_Cmnd *selecting;     /* trying to select this command */
    volatile Scsi_Cmnd *connected;     /* currently connected command */
    volatile Scsi_Cmnd *disconnected_Q;/* commands waiting for reconnect */
    uchar            state;            /* what we are currently doing */
    uchar            fifo;             /* what the FIFO is up to */
    uchar            level2;           /* extent to which Level-2 commands are used */
    uchar            disconnect;       /* disconnect/reselect policy */
    unsigned int     args;             /* set from command-line argument */
    uchar            incoming_msg[8];  /* filled during message_in phase */
    int              incoming_ptr;     /* mainly used with EXTENDED messages */
    uchar            outgoing_msg[8];  /* send this during next message_out */
    int              outgoing_len;     /* length of outgoing message */
    unsigned int     default_sx_per;   /* default transfer period for SCSI bus */
    uchar            sync_xfer[8];     /* sync_xfer reg settings per target */
    uchar            sync_stat[8];     /* status of sync negotiation per target */
    uchar            sync_off;         /* bit mask: don't use sync with these targets */
#ifdef PROC_INTERFACE
    uchar            proc;             /* bit mask: what's in proc output */
#ifdef PROC_STATISTICS
    unsigned long    cmd_cnt[8];       /* # of commands issued per target */
    unsigned long    int_cnt;          /* # of interrupts serviced */
    unsigned long    disc_allowed_cnt[8]; /* # of disconnects allowed per target */
    unsigned long    disc_done_cnt[8]; /* # of disconnects done per target*/
#endif
#endif
    };


/* defines for hostdata->chip */

#define C_WD33C93       0
#define C_WD33C93A      1
#define C_WD33C93B      2
#define C_UNKNOWN_CHIP  100

/* defines for hostdata->state */

#define S_UNCONNECTED         0
#define S_SELECTING           1
#define S_RUNNING_LEVEL2      2
#define S_CONNECTED           3
#define S_PRE_TMP_DISC        4
#define S_PRE_CMP_DISC        5

/* defines for hostdata->fifo */

#define FI_FIFO_UNUSED        0
#define FI_FIFO_READING       1
#define FI_FIFO_WRITING       2

/* defines for hostdata->level2 */
/* NOTE: only the first 3 are trustworthy at this point -
 * having trouble when more than 1 device is reading/writing
 * at the same time...
 */

#define L2_NONE      0  /* no combination commands - we get lots of ints */
#define L2_SELECT    1  /* start with SEL_ATN_XFER, but never resume it */
#define L2_BASIC     2  /* resume after STATUS ints & RDP messages */
#define L2_DATA      3  /* resume after DATA_IN/OUT ints */
#define L2_MOST      4  /* resume after anything except a RESELECT int */
#define L2_RESELECT  5  /* resume after everything, including RESELECT ints */
#define L2_ALL       6  /* always resume */

/* defines for hostdata->disconnect */

#define DIS_NEVER    0
#define DIS_ADAPTIVE 1
#define DIS_ALWAYS   2

/* defines for hostdata->args */

#define DB_TEST               1<<0
#define DB_FIFO               1<<1
#define DB_QUEUE_COMMAND      1<<2
#define DB_EXECUTE            1<<3
#define DB_INTR               1<<4
#define DB_TRANSFER           1<<5
#define DB_MASK               0x3f

#define A_NO_SCSI_RESET       1<<15


/* defines for hostdata->sync_xfer[] */

#define SS_UNSET     0
#define SS_FIRST     1
#define SS_WAITING   2
#define SS_SET       3

/* defines for hostdata->proc */

#define PR_VERSION   1<<0
#define PR_INFO      1<<1
#define PR_STATISTICS 1<<2
#define PR_CONNECTED 1<<3
#define PR_INPUTQ    1<<4
#define PR_DISCQ     1<<5
#define PR_TEST      1<<6
#define PR_STOP      1<<7


# include <linux/init.h>
# include <linux/spinlock.h>
# define in2000__INITFUNC(function) __initfunc(function)
# define in2000__INIT __init
# define in2000__INITDATA __initdata
# define CLISPIN_LOCK(host,flags)   spin_lock_irqsave(host->host_lock, flags)
# define CLISPIN_UNLOCK(host,flags) spin_unlock_irqrestore(host->host_lock, \
							   flags)

static int in2000_detect(Scsi_Host_Template *) in2000__INIT;
static int in2000_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
static int in2000_abort(Scsi_Cmnd *);
static void in2000_setup(char *, int *) in2000__INIT;
static int in2000_biosparam(struct scsi_device *, struct block_device *,
		sector_t, int *);
static int in2000_host_reset(Scsi_Cmnd *);
static int in2000_bus_reset(Scsi_Cmnd *);
static int in2000_device_reset(Scsi_Cmnd *);


#define IN2000_CAN_Q    16
#define IN2000_SG       SG_ALL
#define IN2000_CPL      2
#define IN2000_HOST_ID  7

#endif /* IN2000_H */
