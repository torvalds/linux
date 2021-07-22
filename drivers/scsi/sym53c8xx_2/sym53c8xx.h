/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
 */

#ifndef SYM53C8XX_H
#define SYM53C8XX_H


/*
 *  DMA addressing mode.
 *
 *  0 : 32 bit addressing for all chips.
 *  1 : 40 bit addressing when supported by chip.
 *  2 : 64 bit addressing when supported by chip,
 *      limited to 16 segments of 4 GB -> 64 GB max.
 */
#define	SYM_CONF_DMA_ADDRESSING_MODE CONFIG_SCSI_SYM53C8XX_DMA_ADDRESSING_MODE

/*
 *  NVRAM support.
 */
#if 1
#define SYM_CONF_NVRAM_SUPPORT		(1)
#endif

/*
 *  These options are not tunable from 'make config'
 */
#if 1
#define	SYM_LINUX_PROC_INFO_SUPPORT
#define SYM_LINUX_USER_COMMAND_SUPPORT
#define SYM_LINUX_USER_INFO_SUPPORT
#define SYM_LINUX_DEBUG_CONTROL_SUPPORT
#endif

/*
 *  Also handle old NCR chips if not (0).
 */
#define SYM_CONF_GENERIC_SUPPORT	(1)

/*
 *  Allow tags from 2 to 256, default 8
 */
#ifndef CONFIG_SCSI_SYM53C8XX_MAX_TAGS
#define CONFIG_SCSI_SYM53C8XX_MAX_TAGS	(8)
#endif

#if	CONFIG_SCSI_SYM53C8XX_MAX_TAGS < 2
#define SYM_CONF_MAX_TAG	(2)
#elif	CONFIG_SCSI_SYM53C8XX_MAX_TAGS > 256
#define SYM_CONF_MAX_TAG	(256)
#else
#define	SYM_CONF_MAX_TAG	CONFIG_SCSI_SYM53C8XX_MAX_TAGS
#endif

#ifndef	CONFIG_SCSI_SYM53C8XX_DEFAULT_TAGS
#define	CONFIG_SCSI_SYM53C8XX_DEFAULT_TAGS	SYM_CONF_MAX_TAG
#endif

/*
 *  Anyway, we configure the driver for at least 64 tags per LUN. :)
 */
#if	SYM_CONF_MAX_TAG <= 64
#define SYM_CONF_MAX_TAG_ORDER	(6)
#elif	SYM_CONF_MAX_TAG <= 128
#define SYM_CONF_MAX_TAG_ORDER	(7)
#else
#define SYM_CONF_MAX_TAG_ORDER	(8)
#endif

/*
 *  Max number of SG entries.
 */
#define SYM_CONF_MAX_SG		(96)

/*
 *  Driver setup structure.
 *
 *  This structure is initialized from linux config options.
 *  It can be overridden at boot-up by the boot command line.
 */
struct sym_driver_setup {
	u_short	max_tag;
	u_char	burst_order;
	u_char	scsi_led;
	u_char	scsi_diff;
	u_char	irq_mode;
	u_char	scsi_bus_check;
	u_char	host_id;

	u_char	verbose;
	u_char	settle_delay;
	u_char	use_nvram;
	u_long	excludes[8];
};

#define SYM_SETUP_MAX_TAG		sym_driver_setup.max_tag
#define SYM_SETUP_BURST_ORDER		sym_driver_setup.burst_order
#define SYM_SETUP_SCSI_LED		sym_driver_setup.scsi_led
#define SYM_SETUP_SCSI_DIFF		sym_driver_setup.scsi_diff
#define SYM_SETUP_IRQ_MODE		sym_driver_setup.irq_mode
#define SYM_SETUP_SCSI_BUS_CHECK	sym_driver_setup.scsi_bus_check
#define SYM_SETUP_HOST_ID		sym_driver_setup.host_id
#define boot_verbose			sym_driver_setup.verbose

/*
 *  Initial setup.
 *
 *  Can be overriden at startup by a command line.
 */
#define SYM_LINUX_DRIVER_SETUP	{				\
	.max_tag	= CONFIG_SCSI_SYM53C8XX_DEFAULT_TAGS,	\
	.burst_order	= 7,					\
	.scsi_led	= 1,					\
	.scsi_diff	= 1,					\
	.irq_mode	= 0,					\
	.scsi_bus_check	= 1,					\
	.host_id	= 7,					\
	.verbose	= 0,					\
	.settle_delay	= 3,					\
	.use_nvram	= 1,					\
}

extern struct sym_driver_setup sym_driver_setup;
extern unsigned int sym_debug_flags;
#define DEBUG_FLAGS	sym_debug_flags

/*
 *  Max number of targets.
 *  Maximum is 16 and you are advised not to change this value.
 */
#ifndef SYM_CONF_MAX_TARGET
#define SYM_CONF_MAX_TARGET	(16)
#endif

/*
 *  Max number of logical units.
 *  SPI-2 allows up to 64 logical units, but in real life, target
 *  that implements more that 7 logical units are pretty rare.
 *  Anyway, the cost of accepting up to 64 logical unit is low in 
 *  this driver, thus going with the maximum is acceptable.
 */
#ifndef SYM_CONF_MAX_LUN
#define SYM_CONF_MAX_LUN	(64)
#endif

/*
 *  Max number of IO control blocks queued to the controller.
 *  Each entry needs 8 bytes and the queues are allocated contiguously.
 *  Since we donnot want to allocate more than a page, the theorical 
 *  maximum is PAGE_SIZE/8. For safety, we announce a bit less to the 
 *  access method. :)
 *  When not supplied, as it is suggested, the driver compute some 
 *  good value for this parameter.
 */
/* #define SYM_CONF_MAX_START	(PAGE_SIZE/8 - 16) */

/*
 *  Support for Immediate Arbitration.
 *  Not advised.
 */
/* #define SYM_CONF_IARB_SUPPORT */

/*
 *  Only relevant if IARB support configured.
 *  - Max number of successive settings of IARB hints.
 *  - Set IARB on arbitration lost.
 */
#define SYM_CONF_IARB_MAX 3
#define SYM_CONF_SET_IARB_ON_ARB_LOST 1

/*
 *  Returning wrong residuals may make problems.
 *  When zero, this define tells the driver to 
 *  always return 0 as transfer residual.
 *  Btw, all my testings of residuals have succeeded.
 */
#define SYM_SETUP_RESIDUAL_SUPPORT 1

#endif /* SYM53C8XX_H */
