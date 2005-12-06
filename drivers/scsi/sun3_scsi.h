/*
 * Sun3 SCSI stuff by Erik Verbruggen (erik@bigmama.xtdnet.nl)
 *
 * Sun3 DMA additions by Sam Creasey (sammy@sammy.net)
 *
 * Adapted from mac_scsinew.h:
 */
/*
 * Cumana Generic NCR5380 driver defines
 *
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 *
 * ALPHA RELEASE 1.
 *
 * For more information, please consult
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * 1+ (719) 578-3400
 * 1+ (800) 334-5454
 */

/*
 * $Log: cumana_NCR5380.h,v $
 */

#ifndef SUN3_NCR5380_H
#define SUN3_NCR5380_H

#define SUN3SCSI_PUBLIC_RELEASE 1

/*
 * Int: level 2 autovector
 * IO: type 1, base 0x00140000, 5 bits phys space: A<4..0>
 */
#define IRQ_SUN3_SCSI 2
#define IOBASE_SUN3_SCSI 0x00140000

#define IOBASE_SUN3_VMESCSI 0xff200000

static int sun3scsi_abort (Scsi_Cmnd *);
static int sun3scsi_detect (struct scsi_host_template *);
static const char *sun3scsi_info (struct Scsi_Host *);
static int sun3scsi_bus_reset(Scsi_Cmnd *);
static int sun3scsi_queue_command (Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
static int sun3scsi_release (struct Scsi_Host *);

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 16
#endif

#ifndef SG_TABLESIZE
#define SG_TABLESIZE SG_NONE
#endif

#ifndef MAX_TAGS
#define MAX_TAGS 32
#endif

#ifndef USE_TAGGED_QUEUING
#define	USE_TAGGED_QUEUING 1
#endif

#include <scsi/scsicam.h>

#ifdef SUN3_SCSI_VME
#define SUN3_SCSI_NAME "Sun3 NCR5380 VME SCSI"
#else
#define SUN3_SCSI_NAME "Sun3 NCR5380 SCSI"
#endif

#ifndef HOSTS_C

#define NCR5380_implementation_fields \
    int port, ctrl

#define NCR5380_local_declare() \
        struct Scsi_Host *_instance

#define NCR5380_setup(instance) \
        _instance = instance

#define NCR5380_read(reg) sun3scsi_read(reg)
#define NCR5380_write(reg, value) sun3scsi_write(reg, value)

#define NCR5380_intr sun3scsi_intr
#define NCR5380_queue_command sun3scsi_queue_command
#define NCR5380_bus_reset sun3scsi_bus_reset
#define NCR5380_abort sun3scsi_abort
#define NCR5380_proc_info sun3scsi_proc_info
#define NCR5380_dma_xfer_len(i, cmd, phase) \
        sun3scsi_dma_xfer_len(cmd->SCp.this_residual,cmd,((phase) & SR_IO) ? 0 : 1)

#define NCR5380_dma_write_setup(instance, data, count) sun3scsi_dma_setup(data, count, 1)
#define NCR5380_dma_read_setup(instance, data, count) sun3scsi_dma_setup(data, count, 0)
#define NCR5380_dma_residual sun3scsi_dma_residual

#define BOARD_NORMAL	0
#define BOARD_NCR53C400	1

/* additional registers - mainly DMA control regs */
/* these start at regbase + 8 -- directly after the NCR regs */
struct sun3_dma_regs {
	unsigned short dma_addr_hi; /* vme only */
	unsigned short dma_addr_lo; /* vme only */
	unsigned short dma_count_hi; /* vme only */
	unsigned short dma_count_lo; /* vme only */
	unsigned short udc_data; /* udc dma data reg (obio only) */
	unsigned short udc_addr; /* uda dma addr reg (obio only) */
	unsigned short fifo_data; /* fifo data reg, holds extra byte on
				     odd dma reads */
	unsigned short fifo_count; 
	unsigned short csr; /* control/status reg */
	unsigned short bpack_hi; /* vme only */
	unsigned short bpack_lo; /* vme only */
	unsigned short ivect; /* vme only */
	unsigned short fifo_count_hi; /* vme only */
};

/* ucd chip specific regs - live in dvma space */
struct sun3_udc_regs {
     unsigned short rsel; /* select regs to load */
     unsigned short addr_hi; /* high word of addr */
     unsigned short addr_lo; /* low word */
     unsigned short count; /* words to be xfer'd */
     unsigned short mode_hi; /* high word of channel mode */
     unsigned short mode_lo; /* low word of channel mode */
};

/* addresses of the udc registers */
#define UDC_MODE 0x38 
#define UDC_CSR 0x2e /* command/status */
#define UDC_CHN_HI 0x26 /* chain high word */
#define UDC_CHN_LO 0x22 /* chain lo word */
#define UDC_CURA_HI 0x1a /* cur reg A high */
#define UDC_CURA_LO 0x0a /* cur reg A low */
#define UDC_CURB_HI 0x12 /* cur reg B high */
#define UDC_CURB_LO 0x02 /* cur reg B low */
#define UDC_MODE_HI 0x56 /* mode reg high */
#define UDC_MODE_LO 0x52 /* mode reg low */
#define UDC_COUNT 0x32 /* words to xfer */

/* some udc commands */
#define UDC_RESET 0
#define UDC_CHN_START 0xa0 /* start chain */
#define UDC_INT_ENABLE 0x32 /* channel 1 int on */

/* udc mode words */
#define UDC_MODE_HIWORD 0x40
#define UDC_MODE_LSEND 0xc2
#define UDC_MODE_LRECV 0xd2

/* udc reg selections */
#define UDC_RSEL_SEND 0x282
#define UDC_RSEL_RECV 0x182

/* bits in csr reg */
#define CSR_DMA_ACTIVE 0x8000
#define CSR_DMA_CONFLICT 0x4000
#define CSR_DMA_BUSERR 0x2000

#define CSR_FIFO_EMPTY 0x400 /* fifo flushed? */
#define CSR_SDB_INT 0x200 /* sbc interrupt pending */
#define CSR_DMA_INT 0x100 /* dma interrupt pending */

#define CSR_LEFT 0xc0
#define CSR_LEFT_3 0xc0
#define CSR_LEFT_2 0x80
#define CSR_LEFT_1 0x40
#define CSR_PACK_ENABLE 0x20

#define CSR_DMA_ENABLE 0x10

#define CSR_SEND 0x8 /* 1 = send  0 = recv */
#define CSR_FIFO 0x2 /* reset fifo */
#define CSR_INTR 0x4 /* interrupt enable */
#define CSR_SCSI 0x1 

#define VME_DATA24 0x3d00

// debugging printk's, taken from atari_scsi.h 
/* Debugging printk definitions:
 *
 *  ARB  -> arbitration
 *  ASEN -> auto-sense
 *  DMA  -> DMA
 *  HSH  -> PIO handshake
 *  INF  -> information transfer
 *  INI  -> initialization
 *  INT  -> interrupt
 *  LNK  -> linked commands
 *  MAIN -> NCR5380_main() control flow
 *  NDAT -> no data-out phase
 *  NWR  -> no write commands
 *  PIO  -> PIO transfers
 *  PDMA -> pseudo DMA (unused on Atari)
 *  QU   -> queues
 *  RSL  -> reselections
 *  SEL  -> selections
 *  USL  -> usleep cpde (unused on Atari)
 *  LBS  -> last byte sent (unused on Atari)
 *  RSS  -> restarting of selections
 *  EXT  -> extended messages
 *  ABRT -> aborting and resetting
 *  TAG  -> queue tag handling
 *  MER  -> merging of consec. buffers
 *
 */



#if NDEBUG & NDEBUG_ARBITRATION
#define ARB_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define ARB_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_AUTOSENSE
#define ASEN_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define ASEN_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_DMA
#define DMA_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define DMA_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_HANDSHAKE
#define HSH_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define HSH_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_INFORMATION
#define INF_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define INF_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_INIT
#define INI_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define INI_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_INTR
#define INT_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define INT_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_LINKED
#define LNK_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define LNK_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_MAIN
#define MAIN_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define MAIN_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_NO_DATAOUT
#define NDAT_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define NDAT_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_NO_WRITE
#define NWR_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define NWR_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_PIO
#define PIO_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define PIO_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_PSEUDO_DMA
#define PDMA_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define PDMA_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_QUEUES
#define QU_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define QU_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_RESELECTION
#define RSL_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define RSL_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_SELECTION
#define SEL_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define SEL_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_USLEEP
#define USL_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define USL_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_LAST_BYTE_SENT
#define LBS_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define LBS_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_RESTART_SELECT
#define RSS_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define RSS_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_EXTENDED
#define EXT_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define EXT_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_ABORT
#define ABRT_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define ABRT_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_TAGS
#define TAG_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define TAG_PRINTK(format, args...)
#endif
#if NDEBUG & NDEBUG_MERGING
#define MER_PRINTK(format, args...) \
	printk(KERN_DEBUG format , ## args)
#else
#define MER_PRINTK(format, args...)
#endif

/* conditional macros for NCR5380_print_{,phase,status} */

#define NCR_PRINT(mask)	\
	((NDEBUG & (mask)) ? NCR5380_print(instance) : (void)0)

#define NCR_PRINT_PHASE(mask) \
	((NDEBUG & (mask)) ? NCR5380_print_phase(instance) : (void)0)

#define NCR_PRINT_STATUS(mask) \
	((NDEBUG & (mask)) ? NCR5380_print_status(instance) : (void)0)



#endif /* ndef HOSTS_C */
#endif /* SUN3_NCR5380_H */

