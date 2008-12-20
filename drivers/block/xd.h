#ifndef _LINUX_XD_H
#define _LINUX_XD_H

/*
 * This file contains the definitions for the IO ports and errors etc. for XT hard disk controllers (at least the DTC 5150X).
 *
 * Author: Pat Mackinlay, pat@it.com.au
 * Date: 29/09/92
 *
 * Revised: 01/01/93, ...
 *
 * Ref: DTC 5150X Controller Specification (thanks to Kevin Fowler, kevinf@agora.rain.com)
 * Also thanks to: Salvador Abreu, Dave Thaler, Risto Kankkunen and Wim Van Dorst.
 */

#include <linux/interrupt.h>

/* XT hard disk controller registers */
#define XD_DATA		(xd_iobase + 0x00)	/* data RW register */
#define XD_RESET	(xd_iobase + 0x01)	/* reset WO register */
#define XD_STATUS	(xd_iobase + 0x01)	/* status RO register */
#define XD_SELECT	(xd_iobase + 0x02)	/* select WO register */
#define XD_JUMPER	(xd_iobase + 0x02)	/* jumper RO register */
#define XD_CONTROL	(xd_iobase + 0x03)	/* DMAE/INTE WO register */
#define XD_RESERVED	(xd_iobase + 0x03)	/* reserved */

/* XT hard disk controller commands (incomplete list) */
#define CMD_TESTREADY	0x00	/* test drive ready */
#define CMD_RECALIBRATE	0x01	/* recalibrate drive */
#define CMD_SENSE	0x03	/* request sense */
#define CMD_FORMATDRV	0x04	/* format drive */
#define CMD_VERIFY	0x05	/* read verify */
#define CMD_FORMATTRK	0x06	/* format track */
#define CMD_FORMATBAD	0x07	/* format bad track */
#define CMD_READ	0x08	/* read */
#define CMD_WRITE	0x0A	/* write */
#define CMD_SEEK	0x0B	/* seek */

/* Controller specific commands */
#define CMD_DTCSETPARAM	0x0C	/* set drive parameters (DTC 5150X & CX only?) */
#define CMD_DTCGETECC	0x0D	/* get ecc error length (DTC 5150X only?) */
#define CMD_DTCREADBUF	0x0E	/* read sector buffer (DTC 5150X only?) */
#define CMD_DTCWRITEBUF 0x0F	/* write sector buffer (DTC 5150X only?) */
#define CMD_DTCREMAPTRK	0x11	/* assign alternate track (DTC 5150X only?) */
#define CMD_DTCGETPARAM	0xFB	/* get drive parameters (DTC 5150X only?) */
#define CMD_DTCSETSTEP	0xFC	/* set step rate (DTC 5150X only?) */
#define CMD_DTCSETGEOM	0xFE	/* set geometry data (DTC 5150X only?) */
#define CMD_DTCGETGEOM	0xFF	/* get geometry data (DTC 5150X only?) */
#define CMD_ST11GETGEOM 0xF8	/* get geometry data (Seagate ST11R/M only?) */
#define CMD_WDSETPARAM	0x0C	/* set drive parameters (WD 1004A27X only?) */
#define CMD_XBSETPARAM	0x0C	/* set drive parameters (XEBEC only?) */

/* Bits for command status byte */
#define CSB_ERROR	0x02	/* error */
#define CSB_LUN		0x20	/* logical Unit Number */

/* XT hard disk controller status bits */
#define STAT_READY	0x01	/* controller is ready */
#define STAT_INPUT	0x02	/* data flowing from controller to host */
#define STAT_COMMAND	0x04	/* controller in command phase */
#define STAT_SELECT	0x08	/* controller is selected */
#define STAT_REQUEST	0x10	/* controller requesting data */
#define STAT_INTERRUPT	0x20	/* controller requesting interrupt */

/* XT hard disk controller control bits */
#define PIO_MODE	0x00	/* control bits to set for PIO */
#define DMA_MODE	0x03	/* control bits to set for DMA & interrupt */

#define XD_MAXDRIVES	2	/* maximum 2 drives */
#define XD_TIMEOUT	HZ	/* 1 second timeout */
#define XD_RETRIES	4	/* maximum 4 retries */

#undef DEBUG			/* define for debugging output */

#ifdef DEBUG
	#define DEBUG_STARTUP	/* debug driver initialisation */
	#define DEBUG_OVERRIDE	/* debug override geometry detection */
	#define DEBUG_READWRITE	/* debug each read/write command */
	#define DEBUG_OTHER	/* debug misc. interrupt/DMA stuff */
	#define DEBUG_COMMAND	/* debug each controller command */
#endif /* DEBUG */

/* this structure defines the XT drives and their types */
typedef struct {
	u_char heads;
	u_short cylinders;
	u_char sectors;
	u_char control;
	int unit;
} XD_INFO;

/* this structure defines a ROM BIOS signature */
typedef struct {
	unsigned int offset;
	const char *string;
	void (*init_controller)(unsigned int address);
	void (*init_drive)(u_char drive);
	const char *name;
} XD_SIGNATURE;

#ifndef MODULE
static int xd_manual_geo_init (char *command);
#endif /* MODULE */
static u_char xd_detect (u_char *controller, unsigned int *address);
static u_char xd_initdrives (void (*init_drive)(u_char drive));

static void do_xd_request (struct request_queue * q);
static int xd_ioctl (struct block_device *bdev,fmode_t mode,unsigned int cmd,unsigned long arg);
static int xd_readwrite (u_char operation,XD_INFO *disk,char *buffer,u_int block,u_int count);
static void xd_recalibrate (u_char drive);

static irqreturn_t xd_interrupt_handler(int irq, void *dev_id);
static u_char xd_setup_dma (u_char opcode,u_char *buffer,u_int count);
static u_char *xd_build (u_char *cmdblk,u_char command,u_char drive,u_char head,u_short cylinder,u_char sector,u_char count,u_char control);
static void xd_watchdog (unsigned long unused);
static inline u_char xd_waitport (u_short port,u_char flags,u_char mask,u_long timeout);
static u_int xd_command (u_char *command,u_char mode,u_char *indata,u_char *outdata,u_char *sense,u_long timeout);

/* card specific setup and geometry gathering code */
static void xd_dtc_init_controller (unsigned int address);
static void xd_dtc5150cx_init_drive (u_char drive);
static void xd_dtc_init_drive (u_char drive);
static void xd_wd_init_controller (unsigned int address);
static void xd_wd_init_drive (u_char drive);
static void xd_seagate_init_controller (unsigned int address);
static void xd_seagate_init_drive (u_char drive);
static void xd_omti_init_controller (unsigned int address);
static void xd_omti_init_drive (u_char drive);
static void xd_xebec_init_controller (unsigned int address);
static void xd_xebec_init_drive (u_char drive);
static void xd_setparam (u_char command,u_char drive,u_char heads,u_short cylinders,u_short rwrite,u_short wprecomp,u_char ecc);
static void xd_override_init_drive (u_char drive);

#endif /* _LINUX_XD_H */
