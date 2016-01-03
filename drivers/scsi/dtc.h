/*
 * DTC controller, taken from T128 driver by...
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 */

#ifndef DTC3280_H
#define DTC3280_H

#define NCR5380_implementation_fields \
    void __iomem *base

#define DTC_address(reg) \
	(((struct NCR5380_hostdata *)shost_priv(instance))->base + DTC_5380_OFFSET + reg)

#define NCR5380_read(reg) (readb(DTC_address(reg)))
#define NCR5380_write(reg, value) (writeb(value, DTC_address(reg)))

#define NCR5380_dma_xfer_len(instance, cmd, phase) \
        dtc_dma_xfer_len(cmd)

#define NCR5380_intr			dtc_intr
#define NCR5380_queue_command		dtc_queue_command
#define NCR5380_abort			dtc_abort
#define NCR5380_bus_reset		dtc_bus_reset
#define NCR5380_info			dtc_info
#define NCR5380_show_info		dtc_show_info 
#define NCR5380_write_info		dtc_write_info 

/* 15 12 11 10
   1001 1100 0000 0000 */

#define DTC_IRQS 0x9c00


#endif /* DTC3280_H */
