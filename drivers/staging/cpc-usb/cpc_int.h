/*
 * CPCLIB
 *
 * Copyright (C) 2000-2008 EMS Dr. Thomas Wuensche
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#ifndef CPC_INT_H
#define CPC_INT_H

#include <linux/wait.h>

#define CPC_MSG_BUF_CNT	1500

#ifdef CONFIG_PROC_FS
#   define CPC_PROC_DIR "driver/"
#endif

#undef dbg
#undef err
#undef info

/* Use our own dbg macro */
#define dbg(format, arg...) do { if (debug) printk( KERN_INFO format "\n" , ## arg); } while (0)
#define err(format, arg...) do { printk( KERN_INFO "ERROR " format "\n" , ## arg); } while (0)
#define info(format, arg...) do { printk( KERN_INFO format "\n" , ## arg); } while (0)

/* Macros help using of our buffers */
#define IsBufferFull(x)     (!(x)->WnR) && ((x)->iidx == (x)->oidx)
#define IsBufferEmpty(x)    ((x)->WnR) && ((x)->iidx == (x)->oidx)
#define IsBufferNotEmpty(x) (!(x)->WnR) || ((x)->iidx != (x)->oidx)
#define ResetBuffer(x)      do { (x)->oidx = (x)->iidx=0; (x)->WnR = 1; } while(0);

#define CPC_BufWriteAllowed ((chan->oidx != chan->iidx) || chan->WnR)

typedef void (*chan_write_byte_t) (void *chan, unsigned int reg,
				   unsigned char val);
typedef unsigned char (*chan_read_byte_t) (void *chan, unsigned int reg);

typedef struct CPC_CHAN {
	void __iomem * canBase;	// base address of SJA1000
	chan_read_byte_t read_byte;	// CAN controller read access routine
	chan_write_byte_t write_byte;	// CAN controller write access routine
	CPC_MSG_T *buf;		// buffer for CPC msg
	unsigned int iidx;
	unsigned int oidx;
	unsigned int WnR;
	unsigned int minor;
	unsigned int locked;
	unsigned int irqDisabled;

	unsigned char cpcCtrlCANMessage;
	unsigned char cpcCtrlCANState;
	unsigned char cpcCtrlBUSState;

	unsigned char controllerType;

	unsigned long ovrTimeSec;
	unsigned long ovrTimeNSec;
	unsigned long ovrLockedBuffer;
	CPC_OVERRUN_T ovr;

	/* for debugging only */
	unsigned int handledIrqs;
	unsigned int lostMessages;

	unsigned int sentStdCan;
	unsigned int sentExtCan;
	unsigned int sentStdRtr;
	unsigned int sentExtRtr;

	unsigned int recvStdCan;
	unsigned int recvExtCan;
	unsigned int recvStdRtr;
	unsigned int recvExtRtr;

	wait_queue_head_t *CPCWait_q;

	void *private;
} CPC_CHAN_T;

#endif
