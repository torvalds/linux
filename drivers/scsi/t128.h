/*
 * Trantor T128/T128F/T228 defines
 *	Note : architecturally, the T100 and T128 are different and won't work
 *
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 *
 * DISTRIBUTION RELEASE 3.
 *
 * For more information, please consult
 *
 * Trantor Systems, Ltd.
 * T128/T128F/T228 SCSI Host Adapter
 * Hardware Specifications
 *
 * Trantor Systems, Ltd.
 * 5415 Randall Place
 * Fremont, CA 94538
 * 1+ (415) 770-1400, FAX 1+ (415) 770-9910
 *
 * and
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
 * $Log: t128.h,v $
 */

#ifndef T128_H
#define T128_H

#define T128_PUBLIC_RELEASE 3

#define TDEBUG		0
#define TDEBUG_INIT	0x1
#define TDEBUG_TRANSFER 0x2

/*
 * The trantor boards are memory mapped. They use an NCR5380 or
 * equivalent (my sample board had part second sourced from ZILOG).
 * NCR's recommended "Pseudo-DMA" architecture is used, where
 * a PAL drives the DMA signals on the 5380 allowing fast, blind
 * transfers with proper handshaking.
 */

/*
 * Note : a boot switch is provided for the purpose of informing the
 * firmware to boot or not boot from attached SCSI devices.  So, I imagine
 * there are fewer people who've yanked the ROM like they do on the Seagate
 * to make bootup faster, and I'll probably use this for autodetection.
 */
#define T_ROM_OFFSET		0

/*
 * Note : my sample board *WAS NOT* populated with the SRAM, so this
 * can't be used for autodetection without a ROM present.
 */
#define T_RAM_OFFSET		0x1800

/*
 * All of the registers are allocated 32 bytes of address space, except
 * for the data register (read/write to/from the 5380 in pseudo-DMA mode)
 */ 
#define T_CONTROL_REG_OFFSET	0x1c00	/* rw */
#define T_CR_INT		0x10	/* Enable interrupts */
#define T_CR_CT			0x02	/* Reset watchdog timer */

#define T_STATUS_REG_OFFSET	0x1c20	/* ro */
#define T_ST_BOOT		0x80	/* Boot switch */
#define T_ST_S3			0x40	/* User settable switches, */
#define T_ST_S2			0x20	/* read 0 when switch is on, 1 off */
#define T_ST_S1			0x10
#define T_ST_PS2		0x08	/* Set for Microchannel 228 */
#define T_ST_RDY		0x04	/* 5380 DRQ */
#define T_ST_TIM		0x02	/* indicates 40us watchdog timer fired */
#define T_ST_ZERO		0x01	/* Always zero */

#define T_5380_OFFSET		0x1d00	/* 8 registers here, see NCR5380.h */

#define T_DATA_REG_OFFSET	0x1e00	/* rw 512 bytes long */

#ifndef ASM
static int t128_abort(struct scsi_cmnd *);
static int t128_biosparam(struct scsi_device *, struct block_device *,
			  sector_t, int*);
static int t128_detect(struct scsi_host_template *);
static int t128_queue_command(struct Scsi_Host *, struct scsi_cmnd *);
static int t128_bus_reset(struct scsi_cmnd *);

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 32
#endif

#ifndef HOSTS_C

#define NCR5380_implementation_fields \
    void __iomem *base

#define NCR5380_local_declare() \
    void __iomem *base

#define NCR5380_setup(instance) \
    base = ((struct NCR5380_hostdata *)(instance->hostdata))->base

#define T128_address(reg) (base + T_5380_OFFSET + ((reg) * 0x20))

#if !(TDEBUG & TDEBUG_TRANSFER)
#define NCR5380_read(reg) readb(T128_address(reg))
#define NCR5380_write(reg, value) writeb((value),(T128_address(reg)))
#else
#define NCR5380_read(reg)						\
    (((unsigned char) printk("scsi%d : read register %d at address %08x\n"\
    , instance->hostno, (reg), T128_address(reg))), readb(T128_address(reg)))

#define NCR5380_write(reg, value) {					\
    printk("scsi%d : write %02x to register %d at address %08x\n",	\
	    instance->hostno, (value), (reg), T128_address(reg));	\
    writeb((value), (T128_address(reg)));				\
}
#endif

#define NCR5380_intr t128_intr
#define do_NCR5380_intr do_t128_intr
#define NCR5380_queue_command t128_queue_command
#define NCR5380_abort t128_abort
#define NCR5380_bus_reset t128_bus_reset
#define NCR5380_show_info t128_show_info
#define NCR5380_write_info t128_write_info

/* 15 14 12 10 7 5 3
   1101 0100 1010 1000 */

#define T128_IRQS 0xc4a8

#endif /* else def HOSTS_C */
#endif /* ndef ASM */
#endif /* T128_H */
