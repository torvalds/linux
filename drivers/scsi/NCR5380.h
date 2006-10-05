/* 
 * NCR 5380 defines
 *
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix consulting and custom programming)
 * 	drew@colorado.edu
 *      +1 (303) 666-5836
 *
 * DISTRIBUTION RELEASE 7
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

/*
 * $Log: NCR5380.h,v $
 */

#ifndef NCR5380_H
#define NCR5380_H

#include <linux/interrupt.h>

#define NCR5380_PUBLIC_RELEASE 7
#define NCR53C400_PUBLIC_RELEASE 2

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

#ifdef DIFFERENTIAL
#define ICR_BASE		ICR_DIFF_ENABLE
#else
#define ICR_BASE		0
#endif

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

#ifdef PARITY
#define MR_BASE			MR_ENABLE_PAR_CHECK
#else
#define MR_BASE			0
#endif

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

#define C400_CONTROL_STATUS_REG NCR53C400_register_offset-8	/* rw */

#define CSR_RESET              0x80	/* wo  Resets 53c400 */
#define CSR_53C80_REG          0x80	/* ro  5380 registers busy */
#define CSR_TRANS_DIR          0x40	/* rw  Data transfer direction */
#define CSR_SCSI_BUFF_INTR     0x20	/* rw  Enable int on transfer ready */
#define CSR_53C80_INTR         0x10	/* rw  Enable 53c80 interrupts */
#define CSR_SHARED_INTR        0x08	/* rw  Interrupt sharing */
#define CSR_HOST_BUF_NOT_RDY   0x04	/* ro  Is Host buffer ready */
#define CSR_SCSI_BUF_RDY       0x02	/* ro  SCSI buffer read */
#define CSR_GATED_53C80_IRQ    0x01	/* ro  Last block xferred */

#if 0
#define CSR_BASE CSR_SCSI_BUFF_INTR | CSR_53C80_INTR
#else
#define CSR_BASE CSR_53C80_INTR
#endif

/* Number of 128-byte blocks to be transferred */
#define C400_BLOCK_COUNTER_REG   NCR53C400_register_offset-7	/* rw */

/* Resume transfer after disconnect */
#define C400_RESUME_TRANSFER_REG NCR53C400_register_offset-6	/* wo */

/* Access to host buffer stack */
#define C400_HOST_BUFFER         NCR53C400_register_offset-4	/* rw */


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

/*
 * The internal should_disconnect() function returns these based on the 
 * expected length of a disconnect if a device supports disconnect/
 * reconnect.
 */

#define DISCONNECT_NONE		0
#define DISCONNECT_TIME_TO_DATA	1
#define DISCONNECT_LONG		2

/* 
 * These are "special" values for the tag parameter passed to NCR5380_select.
 */

#define TAG_NEXT	-1	/* Use next free tag */
#define TAG_NONE	-2	/* 
				 * Establish I_T_L nexus instead of I_T_L_Q
				 * even on SCSI-II devices.
				 */

/*
 * These are "special" values for the irq and dma_channel fields of the 
 * Scsi_Host structure
 */

#define SCSI_IRQ_NONE	255
#define DMA_NONE	255
#define IRQ_AUTO	254
#define DMA_AUTO	254
#define PORT_AUTO	0xffff	/* autoprobe io port for 53c400a */

#define FLAG_HAS_LAST_BYTE_SENT		1	/* NCR53c81 or better */
#define FLAG_CHECK_LAST_BYTE_SENT	2	/* Only test once */
#define FLAG_NCR53C400			4	/* NCR53c400 */
#define FLAG_NO_PSEUDO_DMA		8	/* Inhibit DMA */
#define FLAG_DTC3181E			16	/* DTC3181E */

#ifndef ASM
struct NCR5380_hostdata {
	NCR5380_implementation_fields;		/* implementation specific */
	struct Scsi_Host *host;			/* Host backpointer */
	unsigned char id_mask, id_higher_mask;	/* 1 << id, all bits greater */
	unsigned char targets_present;		/* targets we have connected
						   to, so we can call a select
						   failure a retryable condition */
	volatile unsigned char busy[8];		/* index = target, bit = lun */
#if defined(REAL_DMA) || defined(REAL_DMA_POLL)
	volatile int dma_len;			/* requested length of DMA */
#endif
	volatile unsigned char last_message;	/* last message OUT */
	volatile Scsi_Cmnd *connected;		/* currently connected command */
	volatile Scsi_Cmnd *issue_queue;	/* waiting to be issued */
	volatile Scsi_Cmnd *disconnected_queue;	/* waiting for reconnect */
	volatile int restart_select;		/* we have disconnected,
						   used to restart 
						   NCR5380_select() */
	volatile unsigned aborted:1;		/* flag, says aborted */
	int flags;
	unsigned long time_expires;		/* in jiffies, set prior to sleeping */
	int select_time;			/* timer in select for target response */
	volatile Scsi_Cmnd *selecting;
	struct work_struct coroutine;		/* our co-routine */
#ifdef NCR5380_STATS
	unsigned timebase;			/* Base for time calcs */
	long time_read[8];			/* time to do reads */
	long time_write[8];			/* time to do writes */
	unsigned long bytes_read[8];		/* bytes read */
	unsigned long bytes_write[8];		/* bytes written */
	unsigned pendingr;
	unsigned pendingw;
#endif
};

#ifdef __KERNEL__

#define dprintk(a,b)			do {} while(0)
#define NCR5380_dprint(a,b)		do {} while(0)
#define NCR5380_dprint_phase(a,b)	do {} while(0)

#if defined(AUTOPROBE_IRQ)
static int NCR5380_probe_irq(struct Scsi_Host *instance, int possible);
#endif
static int NCR5380_init(struct Scsi_Host *instance, int flags);
static void NCR5380_exit(struct Scsi_Host *instance);
static void NCR5380_information_transfer(struct Scsi_Host *instance);
#ifndef DONT_USE_INTR
static irqreturn_t NCR5380_intr(int irq, void *dev_id);
#endif
static void NCR5380_main(void *ptr);
static void NCR5380_print_options(struct Scsi_Host *instance);
#ifdef NDEBUG
static void NCR5380_print_phase(struct Scsi_Host *instance);
static void NCR5380_print(struct Scsi_Host *instance);
#endif
static int NCR5380_abort(Scsi_Cmnd * cmd);
static int NCR5380_bus_reset(Scsi_Cmnd * cmd);
static int NCR5380_queue_command(Scsi_Cmnd * cmd, void (*done) (Scsi_Cmnd *));
static int NCR5380_proc_info(struct Scsi_Host *instance, char *buffer, char **start,
off_t offset, int length, int inout);

static void NCR5380_reselect(struct Scsi_Host *instance);
static int NCR5380_select(struct Scsi_Host *instance, Scsi_Cmnd * cmd, int tag);
#if defined(PSEUDO_DMA) || defined(REAL_DMA) || defined(REAL_DMA_POLL)
static int NCR5380_transfer_dma(struct Scsi_Host *instance, unsigned char *phase, int *count, unsigned char **data);
#endif
static int NCR5380_transfer_pio(struct Scsi_Host *instance, unsigned char *phase, int *count, unsigned char **data);

#if (defined(REAL_DMA) || defined(REAL_DMA_POLL))

#if defined(i386) || defined(__alpha__)

/**
 *	NCR5380_pc_dma_setup		-	setup ISA DMA
 *	@instance: adapter to set up
 *	@ptr: block to transfer (virtual address)
 *	@count: number of bytes to transfer
 *	@mode: DMA controller mode to use
 *
 *	Program the DMA controller ready to perform an ISA DMA transfer
 *	on this chip.
 *
 *	Locks: takes and releases the ISA DMA lock.
 */
 
static __inline__ int NCR5380_pc_dma_setup(struct Scsi_Host *instance, unsigned char *ptr, unsigned int count, unsigned char mode)
{
	unsigned limit;
	unsigned long bus_addr = virt_to_bus(ptr);
	unsigned long flags;

	if (instance->dma_channel <= 3) {
		if (count > 65536)
			count = 65536;
		limit = 65536 - (bus_addr & 0xFFFF);
	} else {
		if (count > 65536 * 2)
			count = 65536 * 2;
		limit = 65536 * 2 - (bus_addr & 0x1FFFF);
	}

	if (count > limit)
		count = limit;

	if ((count & 1) || (bus_addr & 1))
		panic("scsi%d : attempted unaligned DMA transfer\n", instance->host_no);
	
	flags=claim_dma_lock();
	disable_dma(instance->dma_channel);
	clear_dma_ff(instance->dma_channel);
	set_dma_addr(instance->dma_channel, bus_addr);
	set_dma_count(instance->dma_channel, count);
	set_dma_mode(instance->dma_channel, mode);
	enable_dma(instance->dma_channel);
	release_dma_lock(flags);
	
	return count;
}

/**
 *	NCR5380_pc_dma_write_setup		-	setup ISA DMA write
 *	@instance: adapter to set up
 *	@ptr: block to transfer (virtual address)
 *	@count: number of bytes to transfer
 *
 *	Program the DMA controller ready to perform an ISA DMA write to the
 *	SCSI controller.
 *
 *	Locks: called routines take and release the ISA DMA lock.
 */

static __inline__ int NCR5380_pc_dma_write_setup(struct Scsi_Host *instance, unsigned char *src, unsigned int count)
{
	return NCR5380_pc_dma_setup(instance, src, count, DMA_MODE_WRITE);
}

/**
 *	NCR5380_pc_dma_read_setup		-	setup ISA DMA read
 *	@instance: adapter to set up
 *	@ptr: block to transfer (virtual address)
 *	@count: number of bytes to transfer
 *
 *	Program the DMA controller ready to perform an ISA DMA read from the
 *	SCSI controller.
 *
 *	Locks: called routines take and release the ISA DMA lock.
 */

static __inline__ int NCR5380_pc_dma_read_setup(struct Scsi_Host *instance, unsigned char *src, unsigned int count)
{
	return NCR5380_pc_dma_setup(instance, src, count, DMA_MODE_READ);
}

/**
 *	NCR5380_pc_dma_residual		-	return bytes left 
 *	@instance: adapter
 *
 *	Reports the number of bytes left over after the DMA was terminated.
 *
 *	Locks: takes and releases the ISA DMA lock.
 */

static __inline__ int NCR5380_pc_dma_residual(struct Scsi_Host *instance)
{
	unsigned long flags;
	int tmp;

	flags = claim_dma_lock();
	clear_dma_ff(instance->dma_channel);
	tmp = get_dma_residue(instance->dma_channel);
	release_dma_lock(flags);
	
	return tmp;
}
#endif				/* defined(i386) || defined(__alpha__) */
#endif				/* defined(REAL_DMA)  */
#endif				/* __KERNEL__ */
#endif				/* ndef ASM */
#endif				/* NCR5380_H */
