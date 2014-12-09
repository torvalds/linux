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
 */

#ifndef SUN3_SCSI_H
#define SUN3_SCSI_H

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

