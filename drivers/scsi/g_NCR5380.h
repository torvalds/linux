/*
 * Generic Generic NCR5380 driver defines
 *
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 *
 * NCR53C400 extensions (c) 1994,1995,1996, Kevin Lentin
 *    K.Lentin@cs.monash.edu.au
 */

#ifndef GENERIC_NCR5380_H
#define GENERIC_NCR5380_H

#define DRV_MODULE_NAME "g_NCR5380"

#define NCR5380_read(reg) \
	ioread8(hostdata->io + hostdata->offset + (reg))
#define NCR5380_write(reg, value) \
	iowrite8(value, hostdata->io + hostdata->offset + (reg))

#define NCR5380_implementation_fields \
	int offset; \
	int c400_ctl_status; \
	int c400_blk_cnt; \
	int c400_host_buf; \
	int io_width;

#define NCR53C400_mem_base 0x3880
#define NCR53C400_host_buffer 0x3900
#define NCR53C400_region_size 0x3a00

#define NCR5380_dma_xfer_len		generic_NCR5380_dma_xfer_len
#define NCR5380_dma_recv_setup		generic_NCR5380_pread
#define NCR5380_dma_send_setup		generic_NCR5380_pwrite
#define NCR5380_dma_residual		NCR5380_dma_residual_none

#define NCR5380_intr generic_NCR5380_intr
#define NCR5380_queue_command generic_NCR5380_queue_command
#define NCR5380_abort generic_NCR5380_abort
#define NCR5380_bus_reset generic_NCR5380_bus_reset
#define NCR5380_info generic_NCR5380_info

#define NCR5380_io_delay(x)		udelay(x)

#define BOARD_NCR5380	0
#define BOARD_NCR53C400	1
#define BOARD_NCR53C400A 2
#define BOARD_DTC3181E	3
#define BOARD_HP_C2502	4

#endif /* GENERIC_NCR5380_H */
