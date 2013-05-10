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

#ifndef MAC_NCR5380_H
#define MAC_NCR5380_H

#define MACSCSI_PUBLIC_RELEASE 2

#ifndef ASM

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 16
#endif

#ifndef SG_TABLESIZE
#define SG_TABLESIZE SG_NONE
#endif

#ifndef USE_TAGGED_QUEUING
#define	USE_TAGGED_QUEUING 0
#endif

#include <scsi/scsicam.h>

#ifndef HOSTS_C

#define NCR5380_implementation_fields \
    int port, ctrl

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
#define NCR5380_proc_info macscsi_proc_info

#define BOARD_NORMAL	0
#define BOARD_NCR53C400	1

#endif /* ndef HOSTS_C */
#endif /* ndef ASM */
#endif /* MAC_NCR5380_H */

