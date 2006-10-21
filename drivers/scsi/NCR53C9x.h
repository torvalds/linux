/* NCR53C9x.c:  Defines and structures for the NCR53C9x generic driver.
 *
 * Originaly esp.h:  Defines and structures for the Sparc ESP 
 *                   (Enhanced SCSI Processor) driver under Linux.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 * Generalization by Jesper Skov (jskov@cygnus.co.uk)
 *
 * More generalization (for i386 stuff) by Tymm Twillman (tymm@computer.org)
 */

#ifndef NCR53C9X_H
#define NCR53C9X_H

#include <linux/interrupt.h>

/* djweis for mac driver */
#if defined(CONFIG_MAC)
#define PAD_SIZE 15
#else
#define PAD_SIZE 3
#endif

/* Handle multiple hostadapters on Amiga
 * generally PAD_SIZE = 3
 * but there is one exception: Oktagon (PAD_SIZE = 1) */
#if defined(CONFIG_OKTAGON_SCSI) || defined(CONFIG_OKTAGON_SCSI_MODULE)
#undef PAD_SIZE
#if defined(CONFIG_BLZ1230_SCSI) || defined(CONFIG_BLZ1230_SCSI_MODULE) || \
    defined(CONFIG_BLZ2060_SCSI) || defined(CONFIG_BLZ2060_SCSI_MODULE) || \
    defined(CONFIG_CYBERSTORM_SCSI) || defined(CONFIG_CYBERSTORM_SCSI_MODULE) || \
    defined(CONFIG_CYBERSTORMII_SCSI) || defined(CONFIG_CYBERSTORMII_SCSI_MODULE) || \
    defined(CONFIG_FASTLANE_SCSI) || defined(CONFIG_FASTLANE_SCSI_MODULE)
#define MULTIPLE_PAD_SIZES
#else
#define PAD_SIZE 1
#endif
#endif

/* Macros for debugging messages */

#define DEBUG_ESP
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

/*
 * padding for register structure
 */
#ifdef CONFIG_JAZZ_ESP
#define EREGS_PAD(n)
#else
#ifndef MULTIPLE_PAD_SIZES
#define EREGS_PAD(n)    unchar n[PAD_SIZE];
#endif
#endif

/* The ESP SCSI controllers have their register sets in three
 * "classes":
 *
 * 1) Registers which are both read and write.
 * 2) Registers which are read only.
 * 3) Registers which are write only.
 *
 * Yet, they all live within the same IO space.
 */

#if !defined(__i386__) && !defined(__x86_64__)

#ifndef MULTIPLE_PAD_SIZES

#ifdef CONFIG_CPU_HAS_WB
#include <asm/wbflush.h>
#define esp_write(__reg, __val) do{(__reg) = (__val); wbflush();} while(0)
#else
#define esp_write(__reg, __val) ((__reg) = (__val))
#endif
#define esp_read(__reg) (__reg)

struct ESP_regs {
                                /* Access    Description              Offset */
    volatile unchar esp_tclow;  /* rw  Low bits of the transfer count 0x00   */
                                EREGS_PAD(tlpad1);
    volatile unchar esp_tcmed;  /* rw  Mid bits of the transfer count 0x04   */
                                EREGS_PAD(fdpad);
    volatile unchar esp_fdata;  /* rw  FIFO data bits                 0x08   */
                                EREGS_PAD(cbpad);
    volatile unchar esp_cmnd;   /* rw  SCSI command bits              0x0c   */
                                EREGS_PAD(stpad);
    volatile unchar esp_status; /* ro  ESP status register            0x10   */
#define esp_busid   esp_status  /* wo  Bus ID for select/reselect     0x10   */
                                EREGS_PAD(irqpd);
    volatile unchar esp_intrpt; /* ro  Kind of interrupt              0x14   */
#define esp_timeo   esp_intrpt  /* wo  Timeout value for select/resel 0x14   */
                                EREGS_PAD(sspad);
    volatile unchar esp_sstep;  /* ro  Sequence step register         0x18   */
#define esp_stp     esp_sstep   /* wo  Transfer period per sync       0x18   */
                                EREGS_PAD(ffpad);
    volatile unchar esp_fflags; /* ro  Bits of current FIFO info      0x1c   */
#define esp_soff    esp_fflags  /* wo  Sync offset                    0x1c   */
                                EREGS_PAD(cf1pd);
    volatile unchar esp_cfg1;   /* rw  First configuration register   0x20   */
                                EREGS_PAD(cfpad);
    volatile unchar esp_cfact;  /* wo  Clock conversion factor        0x24   */
                                EREGS_PAD(ctpad);
    volatile unchar esp_ctest;  /* wo  Chip test register             0x28   */
                                EREGS_PAD(cf2pd);
    volatile unchar esp_cfg2;   /* rw  Second configuration register  0x2c   */
                                EREGS_PAD(cf3pd);

    /* The following is only found on the 53C9X series SCSI chips */
    volatile unchar esp_cfg3;   /* rw  Third configuration register   0x30  */
                                EREGS_PAD(cf4pd);
    volatile unchar esp_cfg4;   /* rw  Fourth configuration register  0x34  */
                                EREGS_PAD(thpd);
    /* The following is found on all chips except the NCR53C90 (ESP100) */
    volatile unchar esp_tchi;   /* rw  High bits of transfer count    0x38  */
#define esp_uid     esp_tchi    /* ro  Unique ID code                 0x38  */
                                EREGS_PAD(fgpad);    
    volatile unchar esp_fgrnd;  /* rw  Data base for fifo             0x3c  */
};

#else /* MULTIPLE_PAD_SIZES */

#define esp_write(__reg, __val) (*(__reg) = (__val))
#define esp_read(__reg) (*(__reg))

struct ESP_regs {
    unsigned char io_addr[64]; /* dummy */
                                                 /* Access    Description              Offset */
#define esp_tclow   io_addr                      /* rw  Low bits of the transfer count 0x00   */
#define esp_tcmed   io_addr + (1<<(esp->shift))  /* rw  Mid bits of the transfer count 0x04   */
#define esp_fdata   io_addr + (2<<(esp->shift))  /* rw  FIFO data bits                 0x08   */
#define esp_cmnd    io_addr + (3<<(esp->shift))  /* rw  SCSI command bits              0x0c   */
#define esp_status  io_addr + (4<<(esp->shift))  /* ro  ESP status register            0x10   */
#define esp_busid   esp_status                   /* wo  Bus ID for select/reselect     0x10   */
#define esp_intrpt  io_addr + (5<<(esp->shift))  /* ro  Kind of interrupt              0x14   */
#define esp_timeo   esp_intrpt                   /* wo  Timeout value for select/resel 0x14   */
#define esp_sstep   io_addr + (6<<(esp->shift))  /* ro  Sequence step register         0x18   */
#define esp_stp     esp_sstep                    /* wo  Transfer period per sync       0x18   */
#define esp_fflags  io_addr + (7<<(esp->shift))  /* ro  Bits of current FIFO info      0x1c   */
#define esp_soff    esp_fflags                   /* wo  Sync offset                    0x1c   */
#define esp_cfg1    io_addr + (8<<(esp->shift))  /* rw  First configuration register   0x20   */
#define esp_cfact   io_addr + (9<<(esp->shift))  /* wo  Clock conversion factor        0x24   */
#define esp_ctest   io_addr + (10<<(esp->shift)) /* wo  Chip test register             0x28   */
#define esp_cfg2    io_addr + (11<<(esp->shift)) /* rw  Second configuration register  0x2c   */

    /* The following is only found on the 53C9X series SCSI chips */
#define esp_cfg3    io_addr + (12<<(esp->shift)) /* rw  Third configuration register   0x30  */
#define esp_cfg4    io_addr + (13<<(esp->shift)) /* rw  Fourth configuration register  0x34  */

    /* The following is found on all chips except the NCR53C90 (ESP100) */
#define esp_tchi    io_addr + (14<<(esp->shift)) /* rw  High bits of transfer count    0x38  */
#define esp_uid     esp_tchi                     /* ro  Unique ID code                 0x38  */
#define esp_fgrnd   io_addr + (15<<(esp->shift)) /* rw  Data base for fifo             0x3c  */
};

#endif

#else /* !defined(__i386__) && !defined(__x86_64__) */

#define esp_write(__reg, __val) outb((__val), (__reg))
#define esp_read(__reg) inb((__reg))

struct ESP_regs {
    unsigned int io_addr;
                                 /* Access    Description              Offset */
#define esp_tclow   io_addr      /* rw  Low bits of the transfer count 0x00   */
#define esp_tcmed   io_addr + 1  /* rw  Mid bits of the transfer count 0x04   */
#define esp_fdata   io_addr + 2  /* rw  FIFO data bits                 0x08   */
#define esp_cmnd    io_addr + 3  /* rw  SCSI command bits              0x0c   */
#define esp_status  io_addr + 4  /* ro  ESP status register            0x10   */
#define esp_busid   esp_status   /* wo  Bus ID for select/reselect     0x10   */
#define esp_intrpt  io_addr + 5  /* ro  Kind of interrupt              0x14   */
#define esp_timeo   esp_intrpt   /* wo  Timeout value for select/resel 0x14   */
#define esp_sstep   io_addr + 6  /* ro  Sequence step register         0x18   */
#define esp_stp     esp_sstep    /* wo  Transfer period per sync       0x18   */
#define esp_fflags  io_addr + 7  /* ro  Bits of current FIFO info      0x1c   */
#define esp_soff    esp_fflags   /* wo  Sync offset                    0x1c   */
#define esp_cfg1    io_addr + 8  /* rw  First configuration register   0x20   */
#define esp_cfact   io_addr + 9  /* wo  Clock conversion factor        0x24   */
#define esp_ctest   io_addr + 10 /* wo  Chip test register             0x28   */
#define esp_cfg2    io_addr + 11 /* rw  Second configuration register  0x2c   */

    /* The following is only found on the 53C9X series SCSI chips */
#define esp_cfg3    io_addr + 12 /* rw  Third configuration register   0x30  */
#define esp_cfg4    io_addr + 13 /* rw  Fourth configuration register  0x34  */

    /* The following is found on all chips except the NCR53C90 (ESP100) */
#define esp_tchi    io_addr + 14 /* rw  High bits of transfer count    0x38  */
#define esp_uid     esp_tchi     /* ro  Unique ID code                 0x38  */
#define esp_fgrnd   io_addr + 15 /* rw  Data base for fifo             0x3c  */
};

#endif /* !defined(__i386__) && !defined(__x86_64__) */

/* Various revisions of the ESP board. */
enum esp_rev {
  esp100     = 0x00,  /* NCR53C90 - very broken */
  esp100a    = 0x01,  /* NCR53C90A */
  esp236     = 0x02,
  fas236     = 0x03,
  fas100a    = 0x04,
  fast       = 0x05,
  fas366     = 0x06,
  fas216     = 0x07,
  fsc        = 0x08,  /* SYM53C94-2 */
  espunknown = 0x09
};

/* We allocate one of these for each scsi device and attach it to
 * SDptr->hostdata for use in the driver
 */
struct esp_device {
  unsigned char sync_min_period;
  unsigned char sync_max_offset;
  unsigned sync:1;
  unsigned wide:1;
  unsigned disconnect:1;
};

/* We get one of these for each ESP probed. */
struct NCR_ESP {
  struct NCR_ESP *next;                   /* Next ESP on probed or NULL */
  struct ESP_regs *eregs;	          /* All esp registers */
  int dma;                                /* Who I do transfers with. */
  void *dregs;		  		  /* And his registers. */
  struct Scsi_Host *ehost;                /* Backpointer to SCSI Host */

  void *edev;        		          /* Pointer to controller base/SBus */
  int esp_id;                             /* Unique per-ESP ID number */

  /* ESP Configuration Registers */
  unsigned char config1;                  /* Copy of the 1st config register */
  unsigned char config2;                  /* Copy of the 2nd config register */
  unsigned char config3[16];              /* Copy of the 3rd config register */

  /* The current command we are sending to the ESP chip.  This esp_command
   * ptr needs to be mapped in DVMA area so we can send commands and read
   * from the ESP fifo without burning precious CPU cycles.  Programmed I/O
   * sucks when we have the DVMA to do it for us.  The ESP is stupid and will
   * only send out 6, 10, and 12 byte SCSI commands, others we need to send
   * one byte at a time.  esp_slowcmd being set says that we are doing one
   * of the command types ESP doesn't understand, esp_scmdp keeps track of
   * which byte we are sending, esp_scmdleft says how many bytes to go.
   */
  volatile unchar *esp_command;           /* Location of command (CPU view)  */
  __u32            esp_command_dvma;      /* Location of command (DVMA view) */
  unsigned char esp_clen;                 /* Length of this command */
  unsigned char esp_slowcmd;
  unsigned char *esp_scmdp;
  unsigned char esp_scmdleft;

  /* The following are used to determine the cause of an IRQ. Upon every
   * IRQ entry we synchronize these with the hardware registers.
   */
  unchar ireg;                            /* Copy of ESP interrupt register */
  unchar sreg;                            /* Same for ESP status register */
  unchar seqreg;                          /* The ESP sequence register */

  /* The following is set when a premature interrupt condition is detected
   * in some FAS revisions.
   */
  unchar fas_premature_intr_workaround;

  /* To save register writes to the ESP, which can be expensive, we
   * keep track of the previous value that various registers had for
   * the last target we connected to.  If they are the same for the
   * current target, we skip the register writes as they are not needed.
   */
  unchar prev_soff, prev_stp, prev_cfg3;

  /* For each target we keep track of save/restore data
   * pointer information.  This needs to be updated majorly
   * when we add support for tagged queueing.  -DaveM
   */
  struct esp_pointers {
	  char *saved_ptr;
	  struct scatterlist *saved_buffer;
	  int saved_this_residual;
	  int saved_buffers_residual;
  } data_pointers[16] /*XXX [MAX_TAGS_PER_TARGET]*/;

  /* Clock periods, frequencies, synchronization, etc. */
  unsigned int cfreq;                    /* Clock frequency in HZ */
  unsigned int cfact;                    /* Clock conversion factor */
  unsigned int ccycle;                   /* One ESP clock cycle */
  unsigned int ctick;                    /* One ESP clock time */
  unsigned int radelay;                  /* FAST chip req/ack delay */
  unsigned int neg_defp;                 /* Default negotiation period */
  unsigned int sync_defp;                /* Default sync transfer period */
  unsigned int max_period;               /* longest our period can be */
  unsigned int min_period;               /* shortest period we can withstand */
  /* For slow to medium speed input clock rates we shoot for 5mb/s,
   * but for high input clock rates we try to do 10mb/s although I
   * don't think a transfer can even run that fast with an ESP even
   * with DMA2 scatter gather pipelining.
   */
#define SYNC_DEFP_SLOW            0x32   /* 5mb/s  */
#define SYNC_DEFP_FAST            0x19   /* 10mb/s */

  unsigned int snip;                      /* Sync. negotiation in progress */
  unsigned int wnip;                      /* WIDE negotiation in progress */
  unsigned int targets_present;           /* targets spoken to before */

  int current_transfer_size;              /* Set at beginning of data dma */

  unchar espcmdlog[32];                   /* Log of current esp cmds sent. */
  unchar espcmdent;                       /* Current entry in esp cmd log. */

  /* Misc. info about this ESP */
  enum esp_rev erev;                      /* ESP revision */
  int irq;                                /* IRQ for this ESP */
  int scsi_id;                            /* Who am I as initiator? */
  int scsi_id_mask;                       /* Bitmask of 'me'. */
  int diff;                               /* Differential SCSI bus? */
  int slot;                               /* Slot the adapter occupies */

  /* Our command queues, only one cmd lives in the current_SC queue. */
  Scsi_Cmnd *issue_SC;           /* Commands to be issued */
  Scsi_Cmnd *current_SC;         /* Who is currently working the bus */
  Scsi_Cmnd *disconnected_SC;    /* Commands disconnected from the bus */

  /* Message goo */
  unchar cur_msgout[16];
  unchar cur_msgin[16];
  unchar prevmsgout, prevmsgin;
  unchar msgout_len, msgin_len;
  unchar msgout_ctr, msgin_ctr;

  /* States that we cannot keep in the per cmd structure because they
   * cannot be assosciated with any specific command.
   */
  unchar resetting_bus;
  wait_queue_head_t reset_queue;

  unchar do_pio_cmds;		/* Do command transfer with pio */

  /* How much bits do we have to shift the registers */
  unsigned char shift;

  /* Functions handling DMA
   */ 
  /* Required functions */
  int  (*dma_bytes_sent)(struct NCR_ESP *, int);
  int  (*dma_can_transfer)(struct NCR_ESP *, Scsi_Cmnd *);
  void (*dma_dump_state)(struct NCR_ESP *);
  void (*dma_init_read)(struct NCR_ESP *, __u32, int);
  void (*dma_init_write)(struct NCR_ESP *, __u32, int);
  void (*dma_ints_off)(struct NCR_ESP *);
  void (*dma_ints_on)(struct NCR_ESP *);
  int  (*dma_irq_p)(struct NCR_ESP *);
  int  (*dma_ports_p)(struct NCR_ESP *);
  void (*dma_setup)(struct NCR_ESP *, __u32, int, int);

  /* Optional functions (i.e. may be initialized to 0) */
  void (*dma_barrier)(struct NCR_ESP *);
  void (*dma_drain)(struct NCR_ESP *);
  void (*dma_invalidate)(struct NCR_ESP *);
  void (*dma_irq_entry)(struct NCR_ESP *);
  void (*dma_irq_exit)(struct NCR_ESP *);
  void (*dma_led_off)(struct NCR_ESP *);
  void (*dma_led_on)(struct NCR_ESP *);
  void (*dma_poll)(struct NCR_ESP *, unsigned char *);
  void (*dma_reset)(struct NCR_ESP *);
    
  /* Optional virtual DMA functions */
  void (*dma_mmu_get_scsi_one)(struct NCR_ESP *, Scsi_Cmnd *);
  void (*dma_mmu_get_scsi_sgl)(struct NCR_ESP *, Scsi_Cmnd *);
  void (*dma_mmu_release_scsi_one)(struct NCR_ESP *, Scsi_Cmnd *);
  void (*dma_mmu_release_scsi_sgl)(struct NCR_ESP *, Scsi_Cmnd *);
  void (*dma_advance_sg)(Scsi_Cmnd *);
};

/* Bitfield meanings for the above registers. */

/* ESP config reg 1, read-write, found on all ESP chips */
#define ESP_CONFIG1_ID        0x07             /* My BUS ID bits */
#define ESP_CONFIG1_CHTEST    0x08             /* Enable ESP chip tests */
#define ESP_CONFIG1_PENABLE   0x10             /* Enable parity checks */
#define ESP_CONFIG1_PARTEST   0x20             /* Parity test mode enabled? */
#define ESP_CONFIG1_SRRDISAB  0x40             /* Disable SCSI reset reports */
#define ESP_CONFIG1_SLCABLE   0x80             /* Enable slow cable mode */

/* ESP config reg 2, read-write, found only on esp100a+esp200+esp236+fsc chips */
#define ESP_CONFIG2_DMAPARITY 0x01             /* enable DMA Parity (200,236,fsc) */
#define ESP_CONFIG2_REGPARITY 0x02             /* enable reg Parity (200,236,fsc) */
#define ESP_CONFIG2_BADPARITY 0x04             /* Bad parity target abort  */
#define ESP_CONFIG2_SCSI2ENAB 0x08             /* Enable SCSI-2 features (tmode only) */
#define ESP_CONFIG2_HI        0x10             /* High Impedance DREQ ???  */
#define ESP_CONFIG2_HMEFENAB  0x10             /* HME features enable */
#define ESP_CONFIG2_BCM       0x20             /* Enable byte-ctrl (236,fsc)   */
#define ESP_CONFIG2_FENAB     0x40             /* Enable features (fas100,esp216,fsc)      */
#define ESP_CONFIG2_SPL       0x40             /* Enable status-phase latch (esp236)   */
#define ESP_CONFIG2_RFB       0x80             /* Reserve FIFO byte (fsc) */
#define ESP_CONFIG2_MAGIC     0xe0             /* Invalid bits... */

/* ESP config register 3 read-write, found only esp236+fas236+fas100a+fsc chips */
#define ESP_CONFIG3_FCLOCK    0x01             /* FAST SCSI clock rate (esp100a/fas366) */
#define ESP_CONFIG3_TEM       0x01             /* Enable thresh-8 mode (esp/fas236/fsc)  */
#define ESP_CONFIG3_FAST      0x02             /* Enable FAST SCSI     (esp100a) */
#define ESP_CONFIG3_ADMA      0x02             /* Enable alternate-dma (esp/fas236/fsc)  */
#define ESP_CONFIG3_TENB      0x04             /* group2 SCSI2 support (esp100a) */
#define ESP_CONFIG3_SRB       0x04             /* Save residual byte   (esp/fas236/fsc)  */
#define ESP_CONFIG3_TMS       0x08             /* Three-byte msg's ok  (esp100a) */
#define ESP_CONFIG3_FCLK      0x08             /* Fast SCSI clock rate (esp/fas236/fsc)  */
#define ESP_CONFIG3_IDMSG     0x10             /* ID message checking  (esp100a) */
#define ESP_CONFIG3_FSCSI     0x10             /* Enable FAST SCSI     (esp/fas236/fsc)  */
#define ESP_CONFIG3_GTM       0x20             /* group2 SCSI2 support (esp/fas236/fsc)  */
#define ESP_CONFIG3_TBMS      0x40             /* Three-byte msg's ok  (esp/fas236/fsc)  */
#define ESP_CONFIG3_IMS       0x80             /* ID msg chk'ng        (esp/fas236/fsc)  */

/* ESP config register 4 read-write, found only on fsc chips */
#define ESP_CONFIG4_BBTE      0x01             /* Back-to-Back transfer enable */
#define ESP_CONFIG4_TEST      0x02             /* Transfer counter test mode */
#define ESP_CONFIG4_EAN       0x04             /* Enable Active Negotiation */

/* ESP command register read-write */
/* Group 1 commands:  These may be sent at any point in time to the ESP
 *                    chip.  None of them can generate interrupts 'cept
 *                    the "SCSI bus reset" command if you have not disabled
 *                    SCSI reset interrupts in the config1 ESP register.
 */
#define ESP_CMD_NULL          0x00             /* Null command, ie. a nop */
#define ESP_CMD_FLUSH         0x01             /* FIFO Flush */
#define ESP_CMD_RC            0x02             /* Chip reset */
#define ESP_CMD_RS            0x03             /* SCSI bus reset */

/* Group 2 commands:  ESP must be an initiator and connected to a target
 *                    for these commands to work.
 */
#define ESP_CMD_TI            0x10             /* Transfer Information */
#define ESP_CMD_ICCSEQ        0x11             /* Initiator cmd complete sequence */
#define ESP_CMD_MOK           0x12             /* Message okie-dokie */
#define ESP_CMD_TPAD          0x18             /* Transfer Pad */
#define ESP_CMD_SATN          0x1a             /* Set ATN */
#define ESP_CMD_RATN          0x1b             /* De-assert ATN */

/* Group 3 commands:  ESP must be in the MSGOUT or MSGIN state and be connected
 *                    to a target as the initiator for these commands to work.
 */
#define ESP_CMD_SMSG          0x20             /* Send message */
#define ESP_CMD_SSTAT         0x21             /* Send status */
#define ESP_CMD_SDATA         0x22             /* Send data */
#define ESP_CMD_DSEQ          0x23             /* Discontinue Sequence */
#define ESP_CMD_TSEQ          0x24             /* Terminate Sequence */
#define ESP_CMD_TCCSEQ        0x25             /* Target cmd cmplt sequence */
#define ESP_CMD_DCNCT         0x27             /* Disconnect */
#define ESP_CMD_RMSG          0x28             /* Receive Message */
#define ESP_CMD_RCMD          0x29             /* Receive Command */
#define ESP_CMD_RDATA         0x2a             /* Receive Data */
#define ESP_CMD_RCSEQ         0x2b             /* Receive cmd sequence */

/* Group 4 commands:  The ESP must be in the disconnected state and must
 *                    not be connected to any targets as initiator for
 *                    these commands to work.
 */
#define ESP_CMD_RSEL          0x40             /* Reselect */
#define ESP_CMD_SEL           0x41             /* Select w/o ATN */
#define ESP_CMD_SELA          0x42             /* Select w/ATN */
#define ESP_CMD_SELAS         0x43             /* Select w/ATN & STOP */
#define ESP_CMD_ESEL          0x44             /* Enable selection */
#define ESP_CMD_DSEL          0x45             /* Disable selections */
#define ESP_CMD_SA3           0x46             /* Select w/ATN3 */
#define ESP_CMD_RSEL3         0x47             /* Reselect3 */

/* This bit enables the ESP's DMA */
#define ESP_CMD_DMA           0x80             /* Do DMA? */

/* ESP status register read-only */
#define ESP_STAT_PIO          0x01             /* IO phase bit */
#define ESP_STAT_PCD          0x02             /* CD phase bit */
#define ESP_STAT_PMSG         0x04             /* MSG phase bit */
#define ESP_STAT_PMASK        0x07             /* Mask of phase bits */
#define ESP_STAT_TDONE        0x08             /* Transfer Completed */
#define ESP_STAT_TCNT         0x10             /* Transfer Counter Is Zero */
#define ESP_STAT_PERR         0x20             /* Parity error */
#define ESP_STAT_SPAM         0x40             /* Real bad error */
/* This indicates the 'interrupt pending' condition, it is a reserved
 * bit on old revs of the ESP (ESP100, ESP100A, FAS100A).
 */
#define ESP_STAT_INTR         0x80             /* Interrupt */

/* The status register can be masked with ESP_STAT_PMASK and compared
 * with the following values to determine the current phase the ESP
 * (at least thinks it) is in.  For our purposes we also add our own
 * software 'done' bit for our phase management engine.
 */
#define ESP_DOP   (0)                                       /* Data Out  */
#define ESP_DIP   (ESP_STAT_PIO)                            /* Data In   */
#define ESP_CMDP  (ESP_STAT_PCD)                            /* Command   */
#define ESP_STATP (ESP_STAT_PCD|ESP_STAT_PIO)               /* Status    */
#define ESP_MOP   (ESP_STAT_PMSG|ESP_STAT_PCD)              /* Message Out */
#define ESP_MIP   (ESP_STAT_PMSG|ESP_STAT_PCD|ESP_STAT_PIO) /* Message In */

/* ESP interrupt register read-only */
#define ESP_INTR_S            0x01             /* Select w/o ATN */
#define ESP_INTR_SATN         0x02             /* Select w/ATN */
#define ESP_INTR_RSEL         0x04             /* Reselected */
#define ESP_INTR_FDONE        0x08             /* Function done */
#define ESP_INTR_BSERV        0x10             /* Bus service */
#define ESP_INTR_DC           0x20             /* Disconnect */
#define ESP_INTR_IC           0x40             /* Illegal command given */
#define ESP_INTR_SR           0x80             /* SCSI bus reset detected */

/* Interrupt status macros */
#define ESP_SRESET_IRQ(esp)  ((esp)->intreg & (ESP_INTR_SR))
#define ESP_ILLCMD_IRQ(esp)  ((esp)->intreg & (ESP_INTR_IC))
#define ESP_SELECT_WITH_ATN_IRQ(esp)     ((esp)->intreg & (ESP_INTR_SATN))
#define ESP_SELECT_WITHOUT_ATN_IRQ(esp)  ((esp)->intreg & (ESP_INTR_S))
#define ESP_SELECTION_IRQ(esp)  ((ESP_SELECT_WITH_ATN_IRQ(esp)) ||         \
				 (ESP_SELECT_WITHOUT_ATN_IRQ(esp)))
#define ESP_RESELECTION_IRQ(esp)         ((esp)->intreg & (ESP_INTR_RSEL))

/* ESP sequence step register read-only */
#define ESP_STEP_VBITS        0x07             /* Valid bits */
#define ESP_STEP_ASEL         0x00             /* Selection&Arbitrate cmplt */
#define ESP_STEP_SID          0x01             /* One msg byte sent */
#define ESP_STEP_NCMD         0x02             /* Was not in command phase */
#define ESP_STEP_PPC          0x03             /* Early phase chg caused cmnd
                                                * bytes to be lost
                                                */
#define ESP_STEP_FINI4        0x04             /* Command was sent ok */

/* Ho hum, some ESP's set the step register to this as well... */
#define ESP_STEP_FINI5        0x05
#define ESP_STEP_FINI6        0x06
#define ESP_STEP_FINI7        0x07
#define ESP_STEP_SOM          0x08             /* Synchronous Offset Max */

/* ESP chip-test register read-write */
#define ESP_TEST_TARG         0x01             /* Target test mode */
#define ESP_TEST_INI          0x02             /* Initiator test mode */
#define ESP_TEST_TS           0x04             /* Tristate test mode */

/* ESP unique ID register read-only, found on fas236+fas100a+fsc only */
#define ESP_UID_F100A         0x00             /* FAS100A  */
#define ESP_UID_F236          0x02             /* FAS236   */
#define ESP_UID_FSC           0xa2             /* NCR53CF9x-2  */
#define ESP_UID_REV           0x07             /* ESP revision */
#define ESP_UID_FAM           0xf8             /* ESP family   */

/* ESP fifo flags register read-only */
/* Note that the following implies a 16 byte FIFO on the ESP. */
#define ESP_FF_FBYTES         0x1f             /* Num bytes in FIFO */
#define ESP_FF_ONOTZERO       0x20             /* offset ctr not zero (esp100,fsc) */
#define ESP_FF_SSTEP          0xe0             /* Sequence step */

/* ESP clock conversion factor register write-only */
#define ESP_CCF_F0            0x00             /* 35.01MHz - 40MHz */
#define ESP_CCF_NEVER         0x01             /* Set it to this and die */
#define ESP_CCF_F2            0x02             /* 10MHz */
#define ESP_CCF_F3            0x03             /* 10.01MHz - 15MHz */
#define ESP_CCF_F4            0x04             /* 15.01MHz - 20MHz */
#define ESP_CCF_F5            0x05             /* 20.01MHz - 25MHz */
#define ESP_CCF_F6            0x06             /* 25.01MHz - 30MHz */
#define ESP_CCF_F7            0x07             /* 30.01MHz - 35MHz */

#define ESP_BUS_TIMEOUT        275             /* In milli-seconds */
#define ESP_TIMEO_CONST       8192
#define FSC_TIMEO_CONST       7668
#define ESP_NEG_DEFP(mhz, cfact) \
        ((ESP_BUS_TIMEOUT * ((mhz) / 1000)) / (8192 * (cfact)))
#define FSC_NEG_DEFP(mhz, cfact) \
        ((ESP_BUS_TIMEOUT * ((mhz) / 1000)) / (7668 * (cfact)))
#define ESP_MHZ_TO_CYCLE(mhertz)  ((1000000000) / ((mhertz) / 1000))
#define ESP_TICK(ccf, cycle)  ((7682 * (ccf) * (cycle) / 1000))


/* UGLY, UGLY, UGLY! */
extern int nesps, esps_in_use, esps_running;

/* For our interrupt engine. */
#define for_each_esp(esp) \
        for((esp) = espchain; (esp); (esp) = (esp)->next)


/* External functions */
extern void esp_bootup_reset(struct NCR_ESP *esp, struct ESP_regs *eregs);
extern struct NCR_ESP *esp_allocate(struct scsi_host_template *, void *);
extern void esp_deallocate(struct NCR_ESP *);
extern void esp_release(void);
extern void esp_initialize(struct NCR_ESP *);
extern irqreturn_t esp_intr(int, void *);
extern const char *esp_info(struct Scsi_Host *);
extern int esp_queue(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
extern int esp_abort(Scsi_Cmnd *);
extern int esp_reset(Scsi_Cmnd *);
extern int esp_proc_info(struct Scsi_Host *shost, char *buffer, char **start, off_t offset, int length,
			 int inout);
extern int esp_slave_alloc(struct scsi_device *);
extern void esp_slave_destroy(struct scsi_device *);
#endif /* !(NCR53C9X_H) */
