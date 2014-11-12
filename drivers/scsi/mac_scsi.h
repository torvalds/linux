/*
 * Cumana Generic NCR5380 driver defines
 *
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 */

#ifndef MAC_NCR5380_H
#define MAC_NCR5380_H

#ifndef ASM

#include <scsi/scsicam.h>

#define NCR5380_implementation_fields /* none */

#define NCR5380_local_declare() \
        struct Scsi_Host *_instance

#define NCR5380_setup(instance) \
        _instance = instance

#define NCR5380_read(reg) macscsi_read(_instance, reg)
#define NCR5380_write(reg, value) macscsi_write(_instance, reg, value)

#define NCR5380_pread 	macscsi_pread
#define NCR5380_pwrite 	macscsi_pwrite
	
#define NCR5380_intr macscsi_intr
#define NCR5380_queue_command macscsi_queue_command
#define NCR5380_abort macscsi_abort
#define NCR5380_bus_reset macscsi_bus_reset
#define NCR5380_info macscsi_info
#define NCR5380_show_info macscsi_show_info
#define NCR5380_write_info macscsi_write_info

#endif /* ndef ASM */
#endif /* MAC_NCR5380_H */

