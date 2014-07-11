/* usb1401.h
 Header file for the CED 1401 USB device driver for Linux
 Copyright (C) 2010 Cambridge Electronic Design Ltd
 Author Greg P Smith (greg@ced.co.uk)

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#ifndef __USB1401_H__
#define __USB1401_H__
#include "use1401.h"
#include "ced_ioctl.h"

#ifndef UINT
#define UINT unsigned int
#endif

/** Device type codes, but these don't need to be extended - a succession is assumed
** These are set for usb from the bcdDevice field (suitably mangled). Future devices
** will be added in order of device creation to the list, so the names here are just
** to help use remember which device is which. The U14ERR_... values follow the same
** pattern for modern devices.a
**/
#define TYPEUNKNOWN        -1             /*  dont know */
#define TYPE1401           0              /*  standard 1401 */
#define TYPEPLUS           1              /*  1401 plus */
#define TYPEU1401          2              /*  u1401 */
#define TYPEPOWER          3              /*  Power1401 */
#define TYPEU14012         4              /*  u1401 mkII */
#define TYPEPOWER2         5              /*  Power1401 mk II */
#define TYPEMICRO3         6              /*  Micro1401-3 */
#define TYPEPOWER3         7              /*  Power1401-3 */

/*  Some useful defines of constants. DONT FORGET to change the version in the */
/*  resources whenever you change it here!. */
#define DRIVERMAJREV      2             /*  driver revision level major (match windows) */
#define DRIVERMINREV      0             /*  driver revision level minor */

/*  Definitions of the various block transfer command codes */
#define TM_EXTTOHOST    8               /*  extended tohost */
#define TM_EXTTO1401    9               /*  extended to1401 */

/*  Definitions of values in usbReqtype. Used in sorting out setup actions */
#define H_TO_D 0x00
#define D_TO_H 0x80
#define VENDOR 0x40
#define DEVREQ 0x00
#define INTREQ 0x01
#define ENDREQ 0x02

/*  Definition of values in usbRequest, again used to sort out setup */
#define GET_STATUS      0x00
#define CLEAR_FEATURE   0x01
#define SET_FEATURE     0x03
#define SET_ADDRESS     0x05
#define GET_DESC        0x06
#define SET_DESC        0x07
#define GET_CONF        0x08
#define SET_CONF        0x09
#define GET_INTERFACE   0x0a
#define SET_INTERFACE   0x0b
#define SYNCH_FRAME     0x0c

/*  Definitions of the various debug command codes understood by the 1401. These */
/*  are used in various vendor-specific commands to achieve the desired effect */
#define DB_GRAB         0x50            /* Grab is a NOP for USB */
#define DB_FREE         0x51            /* Free is a NOP for the USB */
#define DB_SETADD       0x52            /* Set debug address (double) */
#define DB_SELFTEST     0x53            /* Start self test */
#define DB_SETMASK      0x54            /* Set enable mask (double) */
#define DB_SETDEF       0x55            /* Set default mask (double) */
#define DB_PEEK         0x56            /* Peek address, save result */
#define DB_POKE         0x57            /* Poke address with data (double) */
#define DB_RAMPD        0x58            /* Ramp data at debug address */
#define DB_RAMPA        0x59            /* Ramp address bus */
#define DB_REPEATS      0x5A            /* Set repeats for operations (double) */
#define DB_WIDTH        0x5B            /* Set width for operations (byte) */
#define DB_DATA         0x5C            /* Get 4-byte data read by PEEK */
#define DB_CHARS        0x5D            /* Send chars via EP0 control write */

#define CR_CHAR          0x0D           /* The carriage return character */
#define CR_CHAR_80       0x8d           /*  and with bit 7 set */

/* A structure holding information about a block */
/* of memory for use in circular transfers       */
struct circ_blk {
	volatile UINT offset;   /* Offset within area of block start */
	volatile UINT size;     /* Size of the block, in bytes (0 = unused) */
};

/* A structure holding all of the information about a transfer area - an area */
/* of memory set up for use either as a source or destination in DMA          */
/* transfers.                                                                 */
struct transarea {
	/* User address of xfer area saved for completeness */
	void __user *buff;

	/* offset to start of xfer area in first page */
	UINT        base_offset;

	UINT        length;        /* Length of xfer area, in bytes */
	struct page **pages;       /* Points at array of locked down pages */
	int         n_pages;       /* number of pages that are locked down */
	bool        used;          /* Is this structure in use? */
	bool        circular;      /* Is this area for circular transfers? */
	bool        circ_to_host;  /* Flag for direction of circular transfer */
	bool        event_to_host; /*  Set event on transfer to host? */
	int         wake_up;       /* Set 1 on event, cleared by TestEvent() */
	UINT        event_st;      /* Defines section within xfer area for... */
	UINT        event_sz;   /* notification by the event SZ is 0 if unset */
	struct circ_blk blocks[2]; /* Info on a pair of circular blocks */

	wait_queue_head_t event; /* The wait queue for events in this */
				 /* area MUST BE LAST */
};

/* The dmadesc structure is used to hold information on the transfer in       */
/* progress. It is set up by ReadDMAInfo, using information sent by the 1401  */
/* in an escape sequence.                                                     */
struct dmadesc {
	unsigned short trans_type;    /* transfer type as TM_xxx above        */
	unsigned short ident;         /* identifier word                      */
	unsigned int   size;          /* bytes to transfer                    */
	unsigned int   offset;        /* offset into transfer area for trans  */
	bool           outward;       /* true when data is going TO 1401      */
};

#define INBUF_SZ  256         /* input buffer size */
#define OUTBUF_SZ 256         /* output buffer size */
#define STAGED_SZ 0x10000     /* size of coherent buffer for staged transfers */

/* Structure to hold all of our device specific stuff. We are making this as  */
/* similar as we can to the Windows driver to help in our understanding of    */
/* what is going on.                                                          */
struct ced_data {
	char input_buffer[INBUF_SZ];        /* The two buffers */
	char output_buffer[OUTBUF_SZ];      /* accessed by the host functions */
	volatile unsigned int num_input;    /* num of chars in input buffer   */
	volatile unsigned int in_buff_get;  /* where to get from input buffer */
	volatile unsigned int in_buff_put;  /* where to put into input buffer */
	volatile unsigned int num_output;   /* num of chars in output buffer  */
	volatile unsigned int out_buff_get; /* where to get from output buffer*/
	volatile unsigned int out_buff_put; /* where to put into output buffer*/

	volatile bool send_chars_pending; /* Flag to indicate sendchar active */
	volatile bool read_chars_pending; /* Flag to indicate a read is primed*/
	char *coher_char_out;     /* special aligned buffer for chars to 1401 */
	struct urb *urb_char_out;           /* urb used for chars to 1401 */
	char *coher_char_in;      /* special aligned buffer for chars to host */
	struct urb *urb_char_in;            /* urb used for chars to host */

	spinlock_t char_out_lock; /* protect the output_buffer and outputting */
	spinlock_t char_in_lock;  /* protect the input_buffer and char reads  */
	__u8 interval;                     /* Interrupt end point interval */

	volatile unsigned int dma_flag;     /* state of DMA */
	struct transarea trans_def[MAX_TRANSAREAS];  /* transfer area info */
	volatile struct dmadesc dma_info;   /*  info on current DMA transfer */
	volatile bool xfer_waiting;      /*  Flag set if DMA transfer stalled */
	volatile bool in_draw_down;   /*  Flag that we want to halt transfers */

	/* Parameters relating to a block read\write that is in progress. Some of these values */
	/* are equivalent to values in dma_info. The values here are those in use, while those */
	/* in dma_info are those received from the 1401 via an escape sequence. If another */
	/* escape sequence arrives before the previous xfer ends, dma_info values are updated while these */
	/* are used to finish off the current transfer. */
	volatile short staged_id;  /*  The transfer area id for this transfer */
	volatile bool staged_read; /*  Flag TRUE for read from 1401, FALSE for write */
	volatile unsigned int staged_length; /* Total length of this transfer */
	volatile unsigned int staged_offset; /*  Offset within memory area for transfer start */
	volatile unsigned int staged_done;   /*  Bytes transferred so far */
	volatile bool staged_urb_pending;    /*  Flag to indicate active */
	char *coher_staged_io;            /*  buffer used for block transfers */
	struct urb *staged_urb;             /*  The URB to use */
	spinlock_t staged_lock;             /* protects ReadWriteMem() and    */
					    /* circular buffer stuff          */

	short type;                         /*  type of 1401 attached */
	short current_state;                /*  current error state */
	bool is_usb2;                 /*  type of the interface we connect to */
	bool force_reset;           /*  Flag to make sure we get a real reset */
	__u32 stat_buf[2];                  /*  buffer for 1401 state info */

	unsigned long self_test_time;       /*  used to timeout self test */

	int n_pipes;           /* Should be 3 or 4 depending on 1401 usb chip */
	int pipe_error[4];     /* set non-zero if an error on one of the pipe */
	__u8 ep_addr[4];                   /* addresses of the 3/4 end points */

	struct usb_device *udev;            /*  the usb device for this device */
	struct usb_interface *interface;    /*  the interface for this device, NULL if removed */
	struct usb_anchor submitted;        /*  in case we need to retract our submissions */
	struct mutex io_mutex;              /*  synchronize I/O with disconnect, one user-mode caller at a time */

	int    errors;                      /*  the last request tanked */
	int    open_count;                  /*  count the number of openers */
	spinlock_t err_lock;                /*  lock for errors */
	struct kref kref;
};

#define to_ced_data(d) container_of(d, struct ced_data, kref)

/*  Definitions of routimes used between compilation object files */
/*  in usb1401.c */
extern int ced_allowi(struct ced_data *ced);
extern int ced_send_chars(struct ced_data *ced);
extern void ced_draw_down(struct ced_data *ced);
extern int ced_read_write_mem(struct ced_data *ced, bool read,
			      unsigned short ident, unsigned int offs,
			      unsigned int len);

/*  in ced_ioc.c */
extern int ced_clear_area(struct ced_data *ced, int area);
extern int ced_send_string(struct ced_data *ced, const char __user *data, unsigned int n);
extern int ced_send_char(struct ced_data *ced, char c);
extern int ced_get_state(struct ced_data *ced, __u32 *state, __u32 *error);
extern int ced_read_write_cancel(struct ced_data *ced);
extern int ced_reset(struct ced_data *ced);
extern int ced_get_char(struct ced_data *ced);
extern int ced_get_string(struct ced_data *ced, char __user *user, int n);
extern int ced_set_transfer(struct ced_data *ced,
			    struct transfer_area_desc __user *utd);
extern int ced_unset_transfer(struct ced_data *ced, int area);
extern int ced_set_event(struct ced_data *ced,
			 struct transfer_event __user *ute);
extern int ced_stat_1401(struct ced_data *ced);
extern int ced_line_count(struct ced_data *ced);
extern int ced_get_out_buf_space(struct ced_data *ced);
extern int ced_get_transfer(struct ced_data *ced, TGET_TX_BLOCK __user *utx);
extern int ced_kill_io(struct ced_data *ced);
extern int ced_state_of_1401(struct ced_data *ced);
extern int ced_start_self_test(struct ced_data *ced);
extern int ced_check_self_test(struct ced_data *ced,
			       TGET_SELFTEST __user *ugst);
extern int ced_type_of_1401(struct ced_data *ced);
extern int ced_transfer_flags(struct ced_data *ced);
extern int ced_dbg_peek(struct ced_data *ced, TDBGBLOCK __user *udb);
extern int ced_dbg_poke(struct ced_data *ced, TDBGBLOCK __user *udb);
extern int ced_dbg_ramp_data(struct ced_data *ced, TDBGBLOCK __user *udb);
extern int ced_dbg_ramp_addr(struct ced_data *ced, TDBGBLOCK __user *udb);
extern int ced_dbg_get_data(struct ced_data *ced, TDBGBLOCK __user *udb);
extern int ced_dbg_stop_loop(struct ced_data *ced);
extern int ced_set_circular(struct ced_data *ced,
			    struct transfer_area_desc __user *utd);
extern int ced_get_circ_block(struct ced_data *ced, TCIRCBLOCK __user *ucb);
extern int ced_free_circ_block(struct ced_data *ced, TCIRCBLOCK __user *ucb);
extern int ced_wait_event(struct ced_data *ced, int area, int time_out);
extern int ced_test_event(struct ced_data *ced, int area);
#endif
