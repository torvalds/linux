/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * NCR 5380 defines
 *
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix consulting and custom programming)
 * 	drew@colorado.edu
 *      +1 (303) 666-5836
 *
 * For more information, please consult 
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * 1+ (719) 578-3400
 * 1+ (800) 334-5454
 */

#ifndef NCR5380_H
#define NCR5380_H

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_transport_spi.h>

#define NDEBUG_ARBITRATION	0x1
#define NDEBUG_AUTOSENSE	0x2
#define NDEBUG_DMA		0x4
#define NDEBUG_HANDSHAKE	0x8
#define NDEBUG_INFORMATION	0x10
#define NDEBUG_INIT		0x20
#define NDEBUG_INTR		0x40
#define NDEBUG_LINKED		0x80
#define NDEBUG_MAIN		0x100
#define NDEBUG_NO_DATAOUT	0x200
#define NDEBUG_NO_WRITE		0x400
#define NDEBUG_PIO		0x800
#define NDEBUG_PSEUDO_DMA	0x1000
#define NDEBUG_QUEUES		0x2000
#define NDEBUG_RESELECTION	0x4000
#define NDEBUG_SELECTION	0x8000
#define NDEBUG_USLEEP		0x10000
#define NDEBUG_LAST_BYTE_SENT	0x20000
#define NDEBUG_RESTART_SELECT	0x40000
#define NDEBUG_EXTENDED		0x80000
#define NDEBUG_C400_PREAD	0x100000
#define NDEBUG_C400_PWRITE	0x200000
#define NDEBUG_LISTS		0x400000
#define NDEBUG_ABORT		0x800000
#define NDEBUG_TAGS		0x1000000
#define NDEBUG_MERGING		0x2000000

#define NDEBUG_ANY		0xFFFFFFFFUL

/* 
 * The contents of the OUTPUT DATA register are asserted on the bus when
 * either arbitration is occurring or the phase-indicating signals (
 * IO, CD, MSG) in the TARGET COMMAND register and the ASSERT DATA
 * bit in the INITIATOR COMMAND register is set.
 */

#define OUTPUT_DATA_REG         0	/* wo DATA lines on SCSI bus */
#define CURRENT_SCSI_DATA_REG   0	/* ro same */

#define INITIATOR_COMMAND_REG	1	/* rw */
#define ICR_ASSERT_RST		0x80	/* rw Set to assert RST  */
#define ICR_ARBITRATION_PROGRESS 0x40	/* ro Indicates arbitration complete */
#define ICR_TRI_STATE		0x40	/* wo Set to tri-state drivers */
#define ICR_ARBITRATION_LOST	0x20	/* ro Indicates arbitration lost */
#define ICR_DIFF_ENABLE		0x20	/* wo Set to enable diff. drivers */
#define ICR_ASSERT_ACK		0x10	/* rw ini Set to assert ACK */
#define ICR_ASSERT_BSY		0x08	/* rw Set to assert BSY */
#define ICR_ASSERT_SEL 		0x04	/* rw Set to assert SEL */
#define ICR_ASSERT_ATN		0x02	/* rw Set to assert ATN */
#define ICR_ASSERT_DATA		0x01	/* rw SCSI_DATA_REG is asserted */

#define ICR_BASE		0

#define MODE_REG		2
/*
 * Note : BLOCK_DMA code will keep DRQ asserted for the duration of the 
 * transfer, causing the chip to hog the bus.  You probably don't want 
 * this.
 */
#define MR_BLOCK_DMA_MODE	0x80	/* rw block mode DMA */
#define MR_TARGET		0x40	/* rw target mode */
#define MR_ENABLE_PAR_CHECK	0x20	/* rw enable parity checking */
#define MR_ENABLE_PAR_INTR	0x10	/* rw enable bad parity interrupt */
#define MR_ENABLE_EOP_INTR	0x08	/* rw enable eop interrupt */
#define MR_MONITOR_BSY		0x04	/* rw enable int on unexpected bsy fail */
#define MR_DMA_MODE		0x02	/* rw DMA / pseudo DMA mode */
#define MR_ARBITRATE		0x01	/* rw start arbitration */

#define MR_BASE			0

#define TARGET_COMMAND_REG	3
#define TCR_LAST_BYTE_SENT	0x80	/* ro DMA done */
#define TCR_ASSERT_REQ		0x08	/* tgt rw assert REQ */
#define TCR_ASSERT_MSG		0x04	/* tgt rw assert MSG */
#define TCR_ASSERT_CD		0x02	/* tgt rw assert CD */
#define TCR_ASSERT_IO		0x01	/* tgt rw assert IO */

#define STATUS_REG		4	/* ro */
/*
 * Note : a set bit indicates an active signal, driven by us or another 
 * device.
 */
#define SR_RST			0x80
#define SR_BSY			0x40
#define SR_REQ			0x20
#define SR_MSG			0x10
#define SR_CD			0x08
#define SR_IO			0x04
#define SR_SEL			0x02
#define SR_DBP			0x01

/*
 * Setting a bit in this register will cause an interrupt to be generated when 
 * BSY is false and SEL true and this bit is asserted  on the bus.
 */
#define SELECT_ENABLE_REG	4	/* wo */

#define BUS_AND_STATUS_REG	5	/* ro */
#define BASR_END_DMA_TRANSFER	0x80	/* ro set on end of transfer */
#define BASR_DRQ		0x40	/* ro mirror of DRQ pin */
#define BASR_PARITY_ERROR	0x20	/* ro parity error detected */
#define BASR_IRQ		0x10	/* ro mirror of IRQ pin */
#define BASR_PHASE_MATCH	0x08	/* ro Set when MSG CD IO match TCR */
#define BASR_BUSY_ERROR		0x04	/* ro Unexpected change to inactive state */
#define BASR_ATN 		0x02	/* ro BUS status */
#define BASR_ACK		0x01	/* ro BUS status */

/* Write any value to this register to start a DMA send */
#define START_DMA_SEND_REG	5	/* wo */

/* 
 * Used in DMA transfer mode, data is latched from the SCSI bus on
 * the falling edge of REQ (ini) or ACK (tgt)
 */
#define INPUT_DATA_REG			6	/* ro */

/* Write any value to this register to start a DMA receive */
#define START_DMA_TARGET_RECEIVE_REG	6	/* wo */

/* Read this register to clear interrupt conditions */
#define RESET_PARITY_INTERRUPT_REG	7	/* ro */

/* Write any value to this register to start an ini mode DMA receive */
#define START_DMA_INITIATOR_RECEIVE_REG 7	/* wo */

/* NCR 53C400(A) Control Status Register bits: */
#define CSR_RESET              0x80	/* wo  Resets 53c400 */
#define CSR_53C80_REG          0x80	/* ro  5380 registers busy */
#define CSR_TRANS_DIR          0x40	/* rw  Data transfer direction */
#define CSR_SCSI_BUFF_INTR     0x20	/* rw  Enable int on transfer ready */
#define CSR_53C80_INTR         0x10	/* rw  Enable 53c80 interrupts */
#define CSR_SHARED_INTR        0x08	/* rw  Interrupt sharing */
#define CSR_HOST_BUF_NOT_RDY   0x04	/* ro  Is Host buffer ready */
#define CSR_SCSI_BUF_RDY       0x02	/* ro  SCSI buffer read */
#define CSR_GATED_53C80_IRQ    0x01	/* ro  Last block xferred */

#define CSR_BASE CSR_53C80_INTR

/* Note : PHASE_* macros are based on the values of the STATUS register */
#define PHASE_MASK 	(SR_MSG | SR_CD | SR_IO)

#define PHASE_DATAOUT		0
#define PHASE_DATAIN		SR_IO
#define PHASE_CMDOUT		SR_CD
#define PHASE_STATIN		(SR_CD | SR_IO)
#define PHASE_MSGOUT		(SR_MSG | SR_CD)
#define PHASE_MSGIN		(SR_MSG | SR_CD | SR_IO)
#define PHASE_UNKNOWN		0xff

/* 
 * Convert status register phase to something we can use to set phase in 
 * the target register so we can get phase mismatch interrupts on DMA 
 * transfers.
 */

#define PHASE_SR_TO_TCR(phase) ((phase) >> 2)

#ifndef NO_IRQ
#define NO_IRQ		0
#endif

#define FLAG_DMA_FIXUP			1	/* Use DMA errata workarounds */
#define FLAG_NO_PSEUDO_DMA		8	/* Inhibit DMA */
#define FLAG_LATE_DMA_SETUP		32	/* Setup NCR before DMA H/W */
#define FLAG_TOSHIBA_DELAY		128	/* Allow for borken CD-ROMs */

struct NCR5380_hostdata {
	NCR5380_implementation_fields;		/* Board-specific data */
	u8 __iomem *io;				/* Remapped 5380 address */
	u8 __iomem *pdma_io;			/* Remapped PDMA address */
	unsigned long poll_loops;		/* Register polling limit */
	spinlock_t lock;			/* Protects this struct */
	struct scsi_cmnd *connected;		/* Currently connected cmnd */
	struct list_head disconnected;		/* Waiting for reconnect */
	struct Scsi_Host *host;			/* SCSI host backpointer */
	struct workqueue_struct *work_q;	/* SCSI host work queue */
	struct work_struct main_task;		/* Work item for main loop */
	int flags;				/* Board-specific quirks */
	int dma_len;				/* Requested length of DMA */
	int read_overruns;	/* Transfer size reduction for DMA erratum */
	unsigned long io_port;			/* Device IO port */
	unsigned long base;			/* Device base address */
	struct list_head unissued;		/* Waiting to be issued */
	struct scsi_cmnd *selecting;		/* Cmnd to be connected */
	struct list_head autosense;		/* Priority cmnd queue */
	struct scsi_cmnd *sensing;		/* Cmnd needing autosense */
	struct scsi_eh_save ses;		/* Cmnd state saved for EH */
	unsigned char busy[8];			/* Index = target, bit = lun */
	unsigned char id_mask;			/* 1 << Host ID */
	unsigned char id_higher_mask;		/* All bits above id_mask */
	unsigned char last_message;		/* Last Message Out */
	unsigned long region_size;		/* Size of address/port range */
	char info[168];				/* Host banner message */
};

struct NCR5380_cmd {
	char *ptr;
	int this_residual;
	struct scatterlist *buffer;
	int status;
	int message;
	int phase;
	struct list_head list;
};

#define NCR5380_PIO_CHUNK_SIZE		256

/* Time limit (ms) to poll registers when IRQs are disabled, e.g. during PDMA */
#define NCR5380_REG_POLL_TIME		10

static inline struct scsi_cmnd *NCR5380_to_scmd(struct NCR5380_cmd *ncmd_ptr)
{
	return ((struct scsi_cmnd *)ncmd_ptr) - 1;
}

static inline struct NCR5380_cmd *NCR5380_to_ncmd(struct scsi_cmnd *cmd)
{
	return scsi_cmd_priv(cmd);
}

#ifndef NDEBUG
#define NDEBUG (0)
#endif

#define dprintk(flg, fmt, ...) \
	do { if ((NDEBUG) & (flg)) \
		printk(KERN_DEBUG fmt, ## __VA_ARGS__); } while (0)

#define dsprintk(flg, host, fmt, ...) \
	do { if ((NDEBUG) & (flg)) \
		shost_printk(KERN_DEBUG, host, fmt, ## __VA_ARGS__); \
	} while (0)

#if NDEBUG
#define NCR5380_dprint(flg, arg) \
	do { if ((NDEBUG) & (flg)) NCR5380_print(arg); } while (0)
#define NCR5380_dprint_phase(flg, arg) \
	do { if ((NDEBUG) & (flg)) NCR5380_print_phase(arg); } while (0)
static void NCR5380_print_phase(struct Scsi_Host *instance);
static void NCR5380_print(struct Scsi_Host *instance);
#else
#define NCR5380_dprint(flg, arg)       do {} while (0)
#define NCR5380_dprint_phase(flg, arg) do {} while (0)
#endif

static int NCR5380_init(struct Scsi_Host *instance, int flags);
static int NCR5380_maybe_reset_bus(struct Scsi_Host *);
static void NCR5380_exit(struct Scsi_Host *instance);
static void NCR5380_information_transfer(struct Scsi_Host *instance);
static irqreturn_t NCR5380_intr(int irq, void *dev_id);
static void NCR5380_main(struct work_struct *work);
static const char *NCR5380_info(struct Scsi_Host *instance);
static void NCR5380_reselect(struct Scsi_Host *instance);
static bool NCR5380_select(struct Scsi_Host *, struct scsi_cmnd *);
static int NCR5380_transfer_dma(struct Scsi_Host *instance, unsigned char *phase, int *count, unsigned char **data);
static int NCR5380_transfer_pio(struct Scsi_Host *instance, unsigned char *phase, int *count, unsigned char **data,
				unsigned int can_sleep);
static int NCR5380_poll_politely2(struct NCR5380_hostdata *,
                                  unsigned int, u8, u8,
                                  unsigned int, u8, u8, unsigned long);

static inline int NCR5380_poll_politely(struct NCR5380_hostdata *hostdata,
                                        unsigned int reg, u8 bit, u8 val,
                                        unsigned long wait)
{
	if ((NCR5380_read(reg) & bit) == val)
		return 0;

	return NCR5380_poll_politely2(hostdata, reg, bit, val,
						reg, bit, val, wait);
}

static int NCR5380_dma_xfer_len(struct NCR5380_hostdata *,
                                struct scsi_cmnd *);
static int NCR5380_dma_send_setup(struct NCR5380_hostdata *,
                                  unsigned char *, int);
static int NCR5380_dma_recv_setup(struct NCR5380_hostdata *,
                                  unsigned char *, int);
static int NCR5380_dma_residual(struct NCR5380_hostdata *);

static inline int NCR5380_dma_xfer_none(struct NCR5380_hostdata *hostdata,
                                        struct scsi_cmnd *cmd)
{
	return 0;
}

static inline int NCR5380_dma_setup_none(struct NCR5380_hostdata *hostdata,
                                         unsigned char *data, int count)
{
	return 0;
}

static inline int NCR5380_dma_residual_none(struct NCR5380_hostdata *hostdata)
{
	return 0;
}

#endif				/* NCR5380_H */
