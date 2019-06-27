/* SPDX-License-Identifier: GPL-2.0 */
/* esp_scsi.h: Defines and structures for the ESP driver.
 *
 * Copyright (C) 2007 David S. Miller (davem@davemloft.net)
 */

#ifndef _ESP_SCSI_H
#define _ESP_SCSI_H

					/* Access    Description      Offset */
#define ESP_TCLOW	0x00UL		/* rw  Low bits transfer count 0x00  */
#define ESP_TCMED	0x01UL		/* rw  Mid bits transfer count 0x04  */
#define ESP_FDATA	0x02UL		/* rw  FIFO data bits          0x08  */
#define ESP_CMD		0x03UL		/* rw  SCSI command bits       0x0c  */
#define ESP_STATUS	0x04UL		/* ro  ESP status register     0x10  */
#define ESP_BUSID	ESP_STATUS	/* wo  BusID for sel/resel     0x10  */
#define ESP_INTRPT	0x05UL		/* ro  Kind of interrupt       0x14  */
#define ESP_TIMEO	ESP_INTRPT	/* wo  Timeout for sel/resel   0x14  */
#define ESP_SSTEP	0x06UL		/* ro  Sequence step register  0x18  */
#define ESP_STP		ESP_SSTEP	/* wo  Transfer period/sync    0x18  */
#define ESP_FFLAGS	0x07UL		/* ro  Bits current FIFO info  0x1c  */
#define ESP_SOFF	ESP_FFLAGS	/* wo  Sync offset             0x1c  */
#define ESP_CFG1	0x08UL		/* rw  First cfg register      0x20  */
#define ESP_CFACT	0x09UL		/* wo  Clock conv factor       0x24  */
#define ESP_STATUS2	ESP_CFACT	/* ro  HME status2 register    0x24  */
#define ESP_CTEST	0x0aUL		/* wo  Chip test register      0x28  */
#define ESP_CFG2	0x0bUL		/* rw  Second cfg register     0x2c  */
#define ESP_CFG3	0x0cUL		/* rw  Third cfg register      0x30  */
#define ESP_CFG4	0x0dUL		/* rw  Fourth cfg register     0x34  */
#define ESP_TCHI	0x0eUL		/* rw  High bits transf count  0x38  */
#define ESP_UID		ESP_TCHI	/* ro  Unique ID code          0x38  */
#define FAS_RLO		ESP_TCHI	/* rw  HME extended counter    0x38  */
#define ESP_FGRND	0x0fUL		/* rw  Data base for fifo      0x3c  */
#define FAS_RHI		ESP_FGRND	/* rw  HME extended counter    0x3c  */

#define SBUS_ESP_REG_SIZE	0x40UL

/* Bitfield meanings for the above registers. */

/* ESP config reg 1, read-write, found on all ESP chips */
#define ESP_CONFIG1_ID        0x07      /* My BUS ID bits */
#define ESP_CONFIG1_CHTEST    0x08      /* Enable ESP chip tests */
#define ESP_CONFIG1_PENABLE   0x10      /* Enable parity checks */
#define ESP_CONFIG1_PARTEST   0x20      /* Parity test mode enabled? */
#define ESP_CONFIG1_SRRDISAB  0x40      /* Disable SCSI reset reports */
#define ESP_CONFIG1_SLCABLE   0x80      /* Enable slow cable mode */

/* ESP config reg 2, read-write, found only on esp100a+esp200+esp236 chips */
#define ESP_CONFIG2_DMAPARITY 0x01      /* enable DMA Parity (200,236) */
#define ESP_CONFIG2_REGPARITY 0x02      /* enable reg Parity (200,236) */
#define ESP_CONFIG2_BADPARITY 0x04      /* Bad parity target abort  */
#define ESP_CONFIG2_SCSI2ENAB 0x08      /* Enable SCSI-2 features (tgtmode) */
#define ESP_CONFIG2_HI        0x10      /* High Impedance DREQ ???  */
#define ESP_CONFIG2_HMEFENAB  0x10      /* HME features enable */
#define ESP_CONFIG2_BCM       0x20      /* Enable byte-ctrl (236)   */
#define ESP_CONFIG2_DISPINT   0x20      /* Disable pause irq (hme) */
#define ESP_CONFIG2_FENAB     0x40      /* Enable features (fas100,216) */
#define ESP_CONFIG2_SPL       0x40      /* Enable status-phase latch (236) */
#define ESP_CONFIG2_MKDONE    0x40      /* HME magic feature */
#define ESP_CONFIG2_HME32     0x80      /* HME 32 extended */
#define ESP_CONFIG2_MAGIC     0xe0      /* Invalid bits... */

/* ESP config register 3 read-write, found only esp236+fas236+fas100a+hme chips */
#define ESP_CONFIG3_FCLOCK    0x01     /* FAST SCSI clock rate (esp100a/hme) */
#define ESP_CONFIG3_TEM       0x01     /* Enable thresh-8 mode (esp/fas236)  */
#define ESP_CONFIG3_FAST      0x02     /* Enable FAST SCSI     (esp100a/hme) */
#define ESP_CONFIG3_ADMA      0x02     /* Enable alternate-dma (esp/fas236)  */
#define ESP_CONFIG3_TENB      0x04     /* group2 SCSI2 support (esp100a/hme) */
#define ESP_CONFIG3_SRB       0x04     /* Save residual byte   (esp/fas236)  */
#define ESP_CONFIG3_TMS       0x08     /* Three-byte msg's ok  (esp100a/hme) */
#define ESP_CONFIG3_FCLK      0x08     /* Fast SCSI clock rate (esp/fas236)  */
#define ESP_CONFIG3_IDMSG     0x10     /* ID message checking  (esp100a/hme) */
#define ESP_CONFIG3_FSCSI     0x10     /* Enable FAST SCSI     (esp/fas236)  */
#define ESP_CONFIG3_GTM       0x20     /* group2 SCSI2 support (esp/fas236)  */
#define ESP_CONFIG3_IDBIT3    0x20     /* Bit 3 of HME SCSI-ID (hme)         */
#define ESP_CONFIG3_TBMS      0x40     /* Three-byte msg's ok  (esp/fas236)  */
#define ESP_CONFIG3_EWIDE     0x40     /* Enable Wide-SCSI     (hme)         */
#define ESP_CONFIG3_IMS       0x80     /* ID msg chk'ng        (esp/fas236)  */
#define ESP_CONFIG3_OBPUSH    0x80     /* Push odd-byte to dma (hme)         */

/* ESP config register 4 read-write, found only on am53c974 chips */
#define ESP_CONFIG4_RADE      0x04     /* Active negation */
#define ESP_CONFIG4_RAE       0x08     /* Active negation on REQ and ACK */
#define ESP_CONFIG4_PWD       0x20     /* Reduced power feature */
#define ESP_CONFIG4_GE0       0x40     /* Glitch eater bit 0 */
#define ESP_CONFIG4_GE1       0x80     /* Glitch eater bit 1 */

#define ESP_CONFIG_GE_12NS    (0)
#define ESP_CONFIG_GE_25NS    (ESP_CONFIG_GE1)
#define ESP_CONFIG_GE_35NS    (ESP_CONFIG_GE0)
#define ESP_CONFIG_GE_0NS     (ESP_CONFIG_GE0 | ESP_CONFIG_GE1)

/* ESP command register read-write */
/* Group 1 commands:  These may be sent at any point in time to the ESP
 *                    chip.  None of them can generate interrupts 'cept
 *                    the "SCSI bus reset" command if you have not disabled
 *                    SCSI reset interrupts in the config1 ESP register.
 */
#define ESP_CMD_NULL          0x00     /* Null command, ie. a nop */
#define ESP_CMD_FLUSH         0x01     /* FIFO Flush */
#define ESP_CMD_RC            0x02     /* Chip reset */
#define ESP_CMD_RS            0x03     /* SCSI bus reset */

/* Group 2 commands:  ESP must be an initiator and connected to a target
 *                    for these commands to work.
 */
#define ESP_CMD_TI            0x10     /* Transfer Information */
#define ESP_CMD_ICCSEQ        0x11     /* Initiator cmd complete sequence */
#define ESP_CMD_MOK           0x12     /* Message okie-dokie */
#define ESP_CMD_TPAD          0x18     /* Transfer Pad */
#define ESP_CMD_SATN          0x1a     /* Set ATN */
#define ESP_CMD_RATN          0x1b     /* De-assert ATN */

/* Group 3 commands:  ESP must be in the MSGOUT or MSGIN state and be connected
 *                    to a target as the initiator for these commands to work.
 */
#define ESP_CMD_SMSG          0x20     /* Send message */
#define ESP_CMD_SSTAT         0x21     /* Send status */
#define ESP_CMD_SDATA         0x22     /* Send data */
#define ESP_CMD_DSEQ          0x23     /* Discontinue Sequence */
#define ESP_CMD_TSEQ          0x24     /* Terminate Sequence */
#define ESP_CMD_TCCSEQ        0x25     /* Target cmd cmplt sequence */
#define ESP_CMD_DCNCT         0x27     /* Disconnect */
#define ESP_CMD_RMSG          0x28     /* Receive Message */
#define ESP_CMD_RCMD          0x29     /* Receive Command */
#define ESP_CMD_RDATA         0x2a     /* Receive Data */
#define ESP_CMD_RCSEQ         0x2b     /* Receive cmd sequence */

/* Group 4 commands:  The ESP must be in the disconnected state and must
 *                    not be connected to any targets as initiator for
 *                    these commands to work.
 */
#define ESP_CMD_RSEL          0x40     /* Reselect */
#define ESP_CMD_SEL           0x41     /* Select w/o ATN */
#define ESP_CMD_SELA          0x42     /* Select w/ATN */
#define ESP_CMD_SELAS         0x43     /* Select w/ATN & STOP */
#define ESP_CMD_ESEL          0x44     /* Enable selection */
#define ESP_CMD_DSEL          0x45     /* Disable selections */
#define ESP_CMD_SA3           0x46     /* Select w/ATN3 */
#define ESP_CMD_RSEL3         0x47     /* Reselect3 */

/* This bit enables the ESP's DMA on the SBus */
#define ESP_CMD_DMA           0x80     /* Do DMA? */

/* ESP status register read-only */
#define ESP_STAT_PIO          0x01     /* IO phase bit */
#define ESP_STAT_PCD          0x02     /* CD phase bit */
#define ESP_STAT_PMSG         0x04     /* MSG phase bit */
#define ESP_STAT_PMASK        0x07     /* Mask of phase bits */
#define ESP_STAT_TDONE        0x08     /* Transfer Completed */
#define ESP_STAT_TCNT         0x10     /* Transfer Counter Is Zero */
#define ESP_STAT_PERR         0x20     /* Parity error */
#define ESP_STAT_SPAM         0x40     /* Real bad error */
/* This indicates the 'interrupt pending' condition on esp236, it is a reserved
 * bit on other revs of the ESP.
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

/* HME only: status 2 register */
#define ESP_STAT2_SCHBIT      0x01 /* Upper bits 3-7 of sstep enabled */
#define ESP_STAT2_FFLAGS      0x02 /* The fifo flags are now latched */
#define ESP_STAT2_XCNT        0x04 /* The transfer counter is latched */
#define ESP_STAT2_CREGA       0x08 /* The command reg is active now */
#define ESP_STAT2_WIDE        0x10 /* Interface on this adapter is wide */
#define ESP_STAT2_F1BYTE      0x20 /* There is one byte at top of fifo */
#define ESP_STAT2_FMSB        0x40 /* Next byte in fifo is most significant */
#define ESP_STAT2_FEMPTY      0x80 /* FIFO is empty */

/* ESP interrupt register read-only */
#define ESP_INTR_S            0x01     /* Select w/o ATN */
#define ESP_INTR_SATN         0x02     /* Select w/ATN */
#define ESP_INTR_RSEL         0x04     /* Reselected */
#define ESP_INTR_FDONE        0x08     /* Function done */
#define ESP_INTR_BSERV        0x10     /* Bus service */
#define ESP_INTR_DC           0x20     /* Disconnect */
#define ESP_INTR_IC           0x40     /* Illegal command given */
#define ESP_INTR_SR           0x80     /* SCSI bus reset detected */

/* ESP sequence step register read-only */
#define ESP_STEP_VBITS        0x07     /* Valid bits */
#define ESP_STEP_ASEL         0x00     /* Selection&Arbitrate cmplt */
#define ESP_STEP_SID          0x01     /* One msg byte sent */
#define ESP_STEP_NCMD         0x02     /* Was not in command phase */
#define ESP_STEP_PPC          0x03     /* Early phase chg caused cmnd
                                        * bytes to be lost
                                        */
#define ESP_STEP_FINI4        0x04     /* Command was sent ok */

/* Ho hum, some ESP's set the step register to this as well... */
#define ESP_STEP_FINI5        0x05
#define ESP_STEP_FINI6        0x06
#define ESP_STEP_FINI7        0x07

/* ESP chip-test register read-write */
#define ESP_TEST_TARG         0x01     /* Target test mode */
#define ESP_TEST_INI          0x02     /* Initiator test mode */
#define ESP_TEST_TS           0x04     /* Tristate test mode */

/* ESP unique ID register read-only, found on fas236+fas100a only */
#define ESP_UID_F100A         0x00     /* ESP FAS100A  */
#define ESP_UID_F236          0x02     /* ESP FAS236   */
#define ESP_UID_REV           0x07     /* ESP revision */
#define ESP_UID_FAM           0xf8     /* ESP family   */

/* ESP fifo flags register read-only */
/* Note that the following implies a 16 byte FIFO on the ESP. */
#define ESP_FF_FBYTES         0x1f     /* Num bytes in FIFO */
#define ESP_FF_ONOTZERO       0x20     /* offset ctr not zero (esp100) */
#define ESP_FF_SSTEP          0xe0     /* Sequence step */

/* ESP clock conversion factor register write-only */
#define ESP_CCF_F0            0x00     /* 35.01MHz - 40MHz */
#define ESP_CCF_NEVER         0x01     /* Set it to this and die */
#define ESP_CCF_F2            0x02     /* 10MHz */
#define ESP_CCF_F3            0x03     /* 10.01MHz - 15MHz */
#define ESP_CCF_F4            0x04     /* 15.01MHz - 20MHz */
#define ESP_CCF_F5            0x05     /* 20.01MHz - 25MHz */
#define ESP_CCF_F6            0x06     /* 25.01MHz - 30MHz */
#define ESP_CCF_F7            0x07     /* 30.01MHz - 35MHz */

/* HME only... */
#define ESP_BUSID_RESELID     0x10
#define ESP_BUSID_CTR32BIT    0x40

#define ESP_BUS_TIMEOUT        250     /* In milli-seconds */
#define ESP_TIMEO_CONST       8192
#define ESP_NEG_DEFP(mhz, cfact) \
        ((ESP_BUS_TIMEOUT * ((mhz) / 1000)) / (8192 * (cfact)))
#define ESP_HZ_TO_CYCLE(hertz)  ((1000000000) / ((hertz) / 1000))
#define ESP_TICK(ccf, cycle)  ((7682 * (ccf) * (cycle) / 1000))

/* For slow to medium speed input clock rates we shoot for 5mb/s, but for high
 * input clock rates we try to do 10mb/s although I don't think a transfer can
 * even run that fast with an ESP even with DMA2 scatter gather pipelining.
 */
#define SYNC_DEFP_SLOW            0x32   /* 5mb/s  */
#define SYNC_DEFP_FAST            0x19   /* 10mb/s */

struct esp_cmd_priv {
	int			num_sg;
	int			cur_residue;
	struct scatterlist	*prv_sg;
	struct scatterlist	*cur_sg;
	int			tot_residue;
};
#define ESP_CMD_PRIV(CMD)	((struct esp_cmd_priv *)(&(CMD)->SCp))

enum esp_rev {
	ESP100     = 0x00,  /* NCR53C90 - very broken */
	ESP100A    = 0x01,  /* NCR53C90A */
	ESP236     = 0x02,
	FAS236     = 0x03,
	FAS100A    = 0x04,
	FAST       = 0x05,
	FASHME     = 0x06,
	PCSCSI     = 0x07,  /* AM53c974 */
};

struct esp_cmd_entry {
	struct list_head	list;

	struct scsi_cmnd	*cmd;

	unsigned int		saved_cur_residue;
	struct scatterlist	*saved_prv_sg;
	struct scatterlist	*saved_cur_sg;
	unsigned int		saved_tot_residue;

	u8			flags;
#define ESP_CMD_FLAG_WRITE	0x01 /* DMA is a write */
#define ESP_CMD_FLAG_AUTOSENSE	0x04 /* Doing automatic REQUEST_SENSE */
#define ESP_CMD_FLAG_RESIDUAL	0x08 /* AM53c974 BLAST residual */

	u8			tag[2];
	u8			orig_tag[2];

	u8			status;
	u8			message;

	unsigned char		*sense_ptr;
	unsigned char		*saved_sense_ptr;
	dma_addr_t		sense_dma;

	struct completion	*eh_done;
};

#define ESP_DEFAULT_TAGS	16

#define ESP_MAX_TARGET		16
#define ESP_MAX_LUN		8
#define ESP_MAX_TAG		256

struct esp_lun_data {
	struct esp_cmd_entry	*non_tagged_cmd;
	int			num_tagged;
	int			hold;
	struct esp_cmd_entry	*tagged_cmds[ESP_MAX_TAG];
};

struct esp_target_data {
	/* These are the ESP_STP, ESP_SOFF, and ESP_CFG3 register values which
	 * match the currently negotiated settings for this target.  The SCSI
	 * protocol values are maintained in spi_{offset,period,wide}(starget).
	 */
	u8			esp_period;
	u8			esp_offset;
	u8			esp_config3;

	u8			flags;
#define ESP_TGT_WIDE		0x01
#define ESP_TGT_DISCONNECT	0x02
#define ESP_TGT_NEGO_WIDE	0x04
#define ESP_TGT_NEGO_SYNC	0x08
#define ESP_TGT_CHECK_NEGO	0x40
#define ESP_TGT_BROKEN		0x80

	/* When ESP_TGT_CHECK_NEGO is set, on the next scsi command to this
	 * device we will try to negotiate the following parameters.
	 */
	u8			nego_goal_period;
	u8			nego_goal_offset;
	u8			nego_goal_width;
	u8			nego_goal_tags;

	struct scsi_target	*starget;
};

struct esp_event_ent {
	u8			type;
#define ESP_EVENT_TYPE_EVENT	0x01
#define ESP_EVENT_TYPE_CMD	0x02
	u8			val;

	u8			sreg;
	u8			seqreg;
	u8			sreg2;
	u8			ireg;
	u8			select_state;
	u8			event;
	u8			__pad;
};

struct esp;
struct esp_driver_ops {
	/* Read and write the ESP 8-bit registers.  On some
	 * applications of the ESP chip the registers are at 4-byte
	 * instead of 1-byte intervals.
	 */
	void (*esp_write8)(struct esp *esp, u8 val, unsigned long reg);
	u8 (*esp_read8)(struct esp *esp, unsigned long reg);

	/* Return non-zero if there is an IRQ pending.  Usually this
	 * status bit lives in the DMA controller sitting in front of
	 * the ESP.  This has to be accurate or else the ESP interrupt
	 * handler will not run.
	 */
	int (*irq_pending)(struct esp *esp);

	/* Return the maximum allowable size of a DMA transfer for a
	 * given buffer.
	 */
	u32 (*dma_length_limit)(struct esp *esp, u32 dma_addr,
				u32 dma_len);

	/* Reset the DMA engine entirely.  On return, ESP interrupts
	 * should be enabled.  Often the interrupt enabling is
	 * controlled in the DMA engine.
	 */
	void (*reset_dma)(struct esp *esp);

	/* Drain any pending DMA in the DMA engine after a transfer.
	 * This is for writes to memory.
	 */
	void (*dma_drain)(struct esp *esp);

	/* Invalidate the DMA engine after a DMA transfer.  */
	void (*dma_invalidate)(struct esp *esp);

	/* Setup an ESP command that will use a DMA transfer.
	 * The 'esp_count' specifies what transfer length should be
	 * programmed into the ESP transfer counter registers, whereas
	 * the 'dma_count' is the length that should be programmed into
	 * the DMA controller.  Usually they are the same.  If 'write'
	 * is non-zero, this transfer is a write into memory.  'cmd'
	 * holds the ESP command that should be issued by calling
	 * scsi_esp_cmd() at the appropriate time while programming
	 * the DMA hardware.
	 */
	void (*send_dma_cmd)(struct esp *esp, u32 dma_addr, u32 esp_count,
			     u32 dma_count, int write, u8 cmd);

	/* Return non-zero if the DMA engine is reporting an error
	 * currently.
	 */
	int (*dma_error)(struct esp *esp);
};

#define ESP_MAX_MSG_SZ		8
#define ESP_EVENT_LOG_SZ	32

#define ESP_QUICKIRQ_LIMIT	100
#define ESP_RESELECT_TAG_LIMIT	2500

struct esp {
	void __iomem		*regs;
	void __iomem		*dma_regs;

	const struct esp_driver_ops *ops;

	struct Scsi_Host	*host;
	struct device		*dev;

	struct esp_cmd_entry	*active_cmd;

	struct list_head	queued_cmds;
	struct list_head	active_cmds;

	u8			*command_block;
	dma_addr_t		command_block_dma;

	unsigned int		data_dma_len;

	/* The following are used to determine the cause of an IRQ. Upon every
	 * IRQ entry we synchronize these with the hardware registers.
	 */
	u8			sreg;
	u8			seqreg;
	u8			sreg2;
	u8			ireg;

	u32			prev_hme_dmacsr;
	u8			prev_soff;
	u8			prev_stp;
	u8			prev_cfg3;
	u8			num_tags;

	struct list_head	esp_cmd_pool;

	struct esp_target_data	target[ESP_MAX_TARGET];

	int			fifo_cnt;
	u8			fifo[16];

	struct esp_event_ent	esp_event_log[ESP_EVENT_LOG_SZ];
	int			esp_event_cur;

	u8			msg_out[ESP_MAX_MSG_SZ];
	int			msg_out_len;

	u8			msg_in[ESP_MAX_MSG_SZ];
	int			msg_in_len;

	u8			bursts;
	u8			config1;
	u8			config2;
	u8			config4;

	u8			scsi_id;
	u32			scsi_id_mask;

	enum esp_rev		rev;

	u32			flags;
#define ESP_FLAG_DIFFERENTIAL	0x00000001
#define ESP_FLAG_RESETTING	0x00000002
#define ESP_FLAG_WIDE_CAPABLE	0x00000008
#define ESP_FLAG_QUICKIRQ_CHECK	0x00000010
#define ESP_FLAG_DISABLE_SYNC	0x00000020
#define ESP_FLAG_USE_FIFO	0x00000040
#define ESP_FLAG_NO_DMA_MAP	0x00000080

	u8			select_state;
#define ESP_SELECT_NONE		0x00 /* Not selecting */
#define ESP_SELECT_BASIC	0x01 /* Select w/o MSGOUT phase */
#define ESP_SELECT_MSGOUT	0x02 /* Select with MSGOUT */

	/* When we are not selecting, we are expecting an event.  */
	u8			event;
#define ESP_EVENT_NONE		0x00
#define ESP_EVENT_CMD_START	0x01
#define ESP_EVENT_CMD_DONE	0x02
#define ESP_EVENT_DATA_IN	0x03
#define ESP_EVENT_DATA_OUT	0x04
#define ESP_EVENT_DATA_DONE	0x05
#define ESP_EVENT_MSGIN		0x06
#define ESP_EVENT_MSGIN_MORE	0x07
#define ESP_EVENT_MSGIN_DONE	0x08
#define ESP_EVENT_MSGOUT	0x09
#define ESP_EVENT_MSGOUT_DONE	0x0a
#define ESP_EVENT_STATUS	0x0b
#define ESP_EVENT_FREE_BUS	0x0c
#define ESP_EVENT_CHECK_PHASE	0x0d
#define ESP_EVENT_RESET		0x10

	/* Probed in esp_get_clock_params() */
	u32			cfact;
	u32			cfreq;
	u32			ccycle;
	u32			ctick;
	u32			neg_defp;
	u32			sync_defp;

	/* Computed in esp_reset_esp() */
	u32			max_period;
	u32			min_period;
	u32			radelay;

	/* ESP_CMD_SELAS command state */
	u8			*cmd_bytes_ptr;
	int			cmd_bytes_left;

	struct completion	*eh_reset;

	void			*dma;
	int			dmarev;

	/* These are used by esp_send_pio_cmd() */
	u8 __iomem		*fifo_reg;
	int			send_cmd_error;
	u32			send_cmd_residual;
};

/* A front-end driver for the ESP chip should do the following in
 * it's device probe routine:
 * 1) Allocate the host and private area using scsi_host_alloc()
 *    with size 'sizeof(struct esp)'.  The first argument to
 *    scsi_host_alloc() should be &scsi_esp_template.
 * 2) Set host->max_id as appropriate.
 * 3) Set esp->host to the scsi_host itself, and esp->dev
 *    to the device object pointer.
 * 4) Hook up esp->ops to the front-end implementation.
 * 5) If the ESP chip supports wide transfers, set ESP_FLAG_WIDE_CAPABLE
 *    in esp->flags.
 * 6) Map the DMA and ESP chip registers.
 * 7) DMA map the ESP command block, store the DMA address
 *    in esp->command_block_dma.
 * 8) Register the scsi_esp_intr() interrupt handler.
 * 9) Probe for and provide the following chip properties:
 *    esp->scsi_id (assign to esp->host->this_id too)
 *    esp->scsi_id_mask
 *    If ESP bus is differential, set ESP_FLAG_DIFFERENTIAL
 *    esp->cfreq
 *    DMA burst bit mask in esp->bursts, if necessary
 * 10) Perform any actions necessary before the ESP device can
 *     be programmed for the first time.  On some configs, for
 *     example, the DMA engine has to be reset before ESP can
 *     be programmed.
 * 11) If necessary, call dev_set_drvdata() as needed.
 * 12) Call scsi_esp_register() with prepared 'esp' structure.
 * 13) Check scsi_esp_register() return value, release all resources
 *     if an error was returned.
 */
extern struct scsi_host_template scsi_esp_template;
extern int scsi_esp_register(struct esp *);

extern void scsi_esp_unregister(struct esp *);
extern irqreturn_t scsi_esp_intr(int, void *);
extern void scsi_esp_cmd(struct esp *, u8);

extern void esp_send_pio_cmd(struct esp *esp, u32 dma_addr, u32 esp_count,
			     u32 dma_count, int write, u8 cmd);

#endif /* !(_ESP_SCSI_H) */
