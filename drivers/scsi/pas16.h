/*
 * This driver adapted from Drew Eckhardt's Trantor T128 driver
 *
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 666-5836
 *
 *  ( Based on T128 - DISTRIBUTION RELEASE 3. ) 
 *
 * Modified to work with the Pro Audio Spectrum/Studio 16
 * by John Weidman.
 *
 *
 * For more information, please consult 
 *
 * Media Vision
 * (510) 770-8600
 * (800) 348-7116
 */


#ifndef PAS16_H
#define PAS16_H

#define PAS16_DEFAULT_BASE_1  0x388
#define PAS16_DEFAULT_BASE_2  0x384
#define PAS16_DEFAULT_BASE_3  0x38c
#define PAS16_DEFAULT_BASE_4  0x288

#define PAS16_DEFAULT_BOARD_1_IRQ 10
#define PAS16_DEFAULT_BOARD_2_IRQ 12
#define PAS16_DEFAULT_BOARD_3_IRQ 14
#define PAS16_DEFAULT_BOARD_4_IRQ 15


/*
 * The Pro Audio Spectrum boards are I/O mapped. They use a Zilog 5380
 * SCSI controller, which is the equivalent of NCR's 5380.  "Pseudo-DMA"
 * architecture is used, where a PAL drives the DMA signals on the 5380
 * allowing fast, blind transfers with proper handshaking. 
 */


/* The Time-out Counter register is used to safe-guard against a stuck
 * bus (in the case of RDY driven handshake) or a stuck byte (if 16-Bit
 * DMA conversion is used).  The counter uses a 28.224MHz clock
 * divided by 14 as its clock source.  In the case of a stuck byte in
 * the holding register, an interrupt is generated (and mixed with the
 * one with the drive) using the CD-ROM interrupt pointer.
 */
 
#define P_TIMEOUT_COUNTER_REG	0x4000
#define P_TC_DISABLE	0x80	/* Set to 0 to enable timeout int. */
				/* Bits D6-D0 contain timeout count */


#define P_TIMEOUT_STATUS_REG_OFFSET	0x4001
#define P_TS_TIM		0x80	/* check timeout status */
					/* Bits D6-D4 N/U */
#define P_TS_ARM_DRQ_INT	0x08	/* Arm DRQ Int.  When set high,
					 * the next rising edge will
					 * cause a CD-ROM interrupt.
					 * When set low, the interrupt
					 * will be cleared.  There is
					 * no status available for
					 * this interrupt.
					 */
#define P_TS_ENABLE_TO_ERR_INTERRUPT	/* Enable timeout error int. */
#define P_TS_ENABLE_WAIT		/* Enable Wait */

#define P_TS_CT			0x01	/* clear timeout. Note: writing
					 * to this register clears the
					 * timeout error int. or status
					 */


/*
 * The data register reads/writes to/from the 5380 in pseudo-DMA mode
 */ 

#define P_DATA_REG_OFFSET	0x5c00	/* rw */

#define P_STATUS_REG_OFFSET	0x5c01	/* ro */
#define P_ST_RDY		0x80	/* 5380 DDRQ Status */

#define P_IRQ_STATUS		0x5c03
#define P_IS_IRQ		0x80	/* DIRQ status */

#define PCB_CONFIG 0x803
#define MASTER_ADDRESS_PTR 0x9a01  /* Fixed position - no relo */
#define SYS_CONFIG_4 0x8003
#define WAIT_STATE 0xbc00
#define OPERATION_MODE_1 0xec03
#define IO_CONFIG_3 0xf002

#define NCR5380_implementation_fields /* none */

#define PAS16_io_port(reg) (instance->io_port + pas16_offset[(reg)])

#define NCR5380_read(reg) ( inb(PAS16_io_port(reg)) )
#define NCR5380_write(reg, value) ( outb((value),PAS16_io_port(reg)) )

#define NCR5380_dma_xfer_len(instance, cmd, phase)	(cmd->transfersize)
#define NCR5380_dma_recv_setup		pas16_pread
#define NCR5380_dma_send_setup		pas16_pwrite
#define NCR5380_dma_residual(instance)	(0)

#define NCR5380_intr pas16_intr
#define NCR5380_queue_command pas16_queue_command
#define NCR5380_abort pas16_abort
#define NCR5380_bus_reset pas16_bus_reset
#define NCR5380_info pas16_info

/* 15 14 12 10 7 5 3 
   1101 0100 1010 1000 */
   
#define PAS16_IRQS 0xd4a8 

#endif /* PAS16_H */
