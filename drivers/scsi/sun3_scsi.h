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

#ifndef SUN3_SCSI_H
#define SUN3_SCSI_H

#define SUN3SCSI_PUBLIC_RELEASE 1

/*
 * Int: level 2 autovector
 * IO: type 1, base 0x00140000, 5 bits phys space: A<4..0>
 */
#define IRQ_SUN3_SCSI 2
#define IOBASE_SUN3_SCSI 0x00140000

#define IOBASE_SUN3_VMESCSI 0xff200000

static int sun3scsi_abort(struct scsi_cmnd *);
static int sun3scsi_detect (struct scsi_host_template *);
static const char *sun3scsi_info (struct Scsi_Host *);
static int sun3scsi_bus_reset(struct scsi_cmnd *);
static int sun3scsi_queue_command(struct Scsi_Host *, struct scsi_cmnd *);
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
#define NCR5380_show_info sun3scsi_show_info
#define NCR5380_dma_xfer_len(i, cmd, phase) \
        sun3scsi_dma_xfer_len(cmd->SCp.this_residual,cmd,((phase) & SR_IO) ? 0 : 1)

#define NCR5380_dma_write_setup(instance, data, count) sun3scsi_dma_setup(data, count, 1)
#define NCR5380_dma_read_setup(instance, data, count) sun3scsi_dma_setup(data, count, 0)
#define NCR5380_dma_residual sun3scsi_dma_residual

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

#endif /* SUN3_SCSI_H */

