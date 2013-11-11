/*
 * Device driver for the PMU on 68K-based Apple PowerBooks
 *
 * The VIA (versatile interface adapter) interfaces to the PMU,
 * a 6805 microprocessor core whose primary function is to control
 * battery charging and system power on the PowerBooks.
 * The PMU also controls the ADB (Apple Desktop Bus) which connects
 * to the keyboard and mouse, as well as the non-volatile RAM
 * and the RTC (real time clock) chip.
 *
 * Adapted for 68K PMU by Joshua M. Thompson
 *
 * Based largely on the PowerMac PMU code by Paul Mackerras and
 * Fabio Riccardi.
 *
 * Also based on the PMU driver from MkLinux by Apple Computer, Inc.
 * and the Open Software Foundation, Inc.
 */

#include <stdarg.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/cuda.h>

#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_via.h>

#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

/* Misc minor number allocated for /dev/pmu */
#define PMU_MINOR	154

/* VIA registers - spaced 0x200 bytes apart */
#define RS		0x200		/* skip between registers */
#define B		0		/* B-side data */
#define A		RS		/* A-side data */
#define DIRB		(2*RS)		/* B-side direction (1=output) */
#define DIRA		(3*RS)		/* A-side direction (1=output) */
#define T1CL		(4*RS)		/* Timer 1 ctr/latch (low 8 bits) */
#define T1CH		(5*RS)		/* Timer 1 counter (high 8 bits) */
#define T1LL		(6*RS)		/* Timer 1 latch (low 8 bits) */
#define T1LH		(7*RS)		/* Timer 1 latch (high 8 bits) */
#define T2CL		(8*RS)		/* Timer 2 ctr/latch (low 8 bits) */
#define T2CH		(9*RS)		/* Timer 2 counter (high 8 bits) */
#define SR		(10*RS)		/* Shift register */
#define ACR		(11*RS)		/* Auxiliary control register */
#define PCR		(12*RS)		/* Peripheral control register */
#define IFR		(13*RS)		/* Interrupt flag register */
#define IER		(14*RS)		/* Interrupt enable register */
#define ANH		(15*RS)		/* A-side data, no handshake */

/* Bits in B data register: both active low */
#define TACK		0x02		/* Transfer acknowledge (input) */
#define TREQ		0x04		/* Transfer request (output) */

/* Bits in ACR */
#define SR_CTRL		0x1c		/* Shift register control bits */
#define SR_EXT		0x0c		/* Shift on external clock */
#define SR_OUT		0x10		/* Shift out if 1 */

/* Bits in IFR and IER */
#define SR_INT		0x04		/* Shift register full/empty */
#define CB1_INT		0x10		/* transition on CB1 input */

static enum pmu_state {
	idle,
	sending,
	intack,
	reading,
	reading_intr,
} pmu_state;

static struct adb_request *current_req;
static struct adb_request *last_req;
static struct adb_request *req_awaiting_reply;
static unsigned char interrupt_data[32];
static unsigned char *reply_ptr;
static int data_index;
static int data_len;
static int adb_int_pending;
static int pmu_adb_flags;
static int adb_dev_map;
static struct adb_request bright_req_1, bright_req_2, bright_req_3;
static int pmu_kind = PMU_UNKNOWN;
static int pmu_fully_inited;

int asleep;

static int pmu_probe(void);
static int pmu_init(void);
static void pmu_start(void);
static irqreturn_t pmu_interrupt(int irq, void *arg);
static int pmu_send_request(struct adb_request *req, int sync);
static int pmu_autopoll(int devs);
void pmu_poll(void);
static int pmu_reset_bus(void);

static void pmu_start(void);
static void send_byte(int x);
static void recv_byte(void);
static void pmu_done(struct adb_request *req);
static void pmu_handle_data(unsigned char *data, int len);
static void set_volume(int level);
static void pmu_enable_backlight(int on);
static void pmu_set_brightness(int level);

struct adb_driver via_pmu_driver = {
	"68K PMU",
	pmu_probe,
	pmu_init,
	pmu_send_request,
	pmu_autopoll,
	pmu_poll,
	pmu_reset_bus
};

/*
 * This table indicates for each PMU opcode:
 * - the number of data bytes to be sent with the command, or -1
 *   if a length byte should be sent,
 * - the number of response bytes which the PMU will return, or
 *   -1 if it will send a length byte.
 */
static s8 pmu_data_len[256][2] = {
/*	   0	   1	   2	   3	   4	   5	   6	   7  */
/*00*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*08*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*10*/	{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*18*/	{ 0, 1},{ 0, 1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{ 0, 0},
/*20*/	{-1, 0},{ 0, 0},{ 2, 0},{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},
/*28*/	{ 0,-1},{ 0,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{ 0,-1},
/*30*/	{ 4, 0},{20, 0},{-1, 0},{ 3, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*38*/	{ 0, 4},{ 0,20},{ 2,-1},{ 2, 1},{ 3,-1},{-1,-1},{-1,-1},{ 4, 0},
/*40*/	{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*48*/	{ 0, 1},{ 0, 1},{-1,-1},{ 1, 0},{ 1, 0},{-1,-1},{-1,-1},{-1,-1},
/*50*/	{ 1, 0},{ 0, 0},{ 2, 0},{ 2, 0},{-1, 0},{ 1, 0},{ 3, 0},{ 1, 0},
/*58*/	{ 0, 1},{ 1, 0},{ 0, 2},{ 0, 2},{ 0,-1},{-1,-1},{-1,-1},{-1,-1},
/*60*/	{ 2, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*68*/	{ 0, 3},{ 0, 3},{ 0, 2},{ 0, 8},{ 0,-1},{ 0,-1},{-1,-1},{-1,-1},
/*70*/	{ 1, 0},{ 1, 0},{ 1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*78*/	{ 0,-1},{ 0,-1},{-1,-1},{-1,-1},{-1,-1},{ 5, 1},{ 4, 1},{ 4, 1},
/*80*/	{ 4, 0},{-1, 0},{ 0, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*88*/	{ 0, 5},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*90*/	{ 1, 0},{ 2, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*98*/	{ 0, 1},{ 0, 1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*a0*/	{ 2, 0},{ 2, 0},{ 2, 0},{ 4, 0},{-1, 0},{ 0, 0},{-1, 0},{-1, 0},
/*a8*/	{ 1, 1},{ 1, 0},{ 3, 0},{ 2, 0},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*b0*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*b8*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*c0*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*c8*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
/*d0*/	{ 0, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*d8*/	{ 1, 1},{ 1, 1},{-1,-1},{-1,-1},{ 0, 1},{ 0,-1},{-1,-1},{-1,-1},
/*e0*/	{-1, 0},{ 4, 0},{ 0, 1},{-1, 0},{-1, 0},{ 4, 0},{-1, 0},{-1, 0},
/*e8*/	{ 3,-1},{-1,-1},{ 0, 1},{-1,-1},{ 0,-1},{-1,-1},{-1,-1},{ 0, 0},
/*f0*/	{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},{-1, 0},
/*f8*/	{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},{-1,-1},
};

int pmu_probe(void)
{
	if (macintosh_config->adb_type == MAC_ADB_PB1) {
		pmu_kind = PMU_68K_V1;
	} else if (macintosh_config->adb_type == MAC_ADB_PB2) {
		pmu_kind = PMU_68K_V2;
	} else {
		return -ENODEV;
	}

	pmu_state = idle;

	return 0;
}

static int 
pmu_init(void)
{
	int timeout;
	volatile struct adb_request req;

	via2[B] |= TREQ;				/* negate TREQ */
	via2[DIRB] = (via2[DIRB] | TREQ) & ~TACK;	/* TACK in, TREQ out */

	pmu_request((struct adb_request *) &req, NULL, 2, PMU_SET_INTR_MASK, PMU_INT_ADB);
	timeout =  100000;
	while (!req.complete) {
		if (--timeout < 0) {
			printk(KERN_ERR "pmu_init: no response from PMU\n");
			return -EAGAIN;
		}
		udelay(10);
		pmu_poll();
	}

	/* ack all pending interrupts */
	timeout = 100000;
	interrupt_data[0] = 1;
	while (interrupt_data[0] || pmu_state != idle) {
		if (--timeout < 0) {
			printk(KERN_ERR "pmu_init: timed out acking intrs\n");
			return -EAGAIN;
		}
		if (pmu_state == idle) {
			adb_int_pending = 1;
			pmu_interrupt(0, NULL);
		}
		pmu_poll();
		udelay(10);
	}

	pmu_request((struct adb_request *) &req, NULL, 2, PMU_SET_INTR_MASK,
			PMU_INT_ADB_AUTO|PMU_INT_SNDBRT|PMU_INT_ADB);
	timeout =  100000;
	while (!req.complete) {
		if (--timeout < 0) {
			printk(KERN_ERR "pmu_init: no response from PMU\n");
			return -EAGAIN;
		}
		udelay(10);
		pmu_poll();
	}

	bright_req_1.complete = 1;
	bright_req_2.complete = 1;
	bright_req_3.complete = 1;

	if (request_irq(IRQ_MAC_ADB_SR, pmu_interrupt, 0, "pmu-shift",
			pmu_interrupt)) {
		printk(KERN_ERR "pmu_init: can't get irq %d\n",
			IRQ_MAC_ADB_SR);
		return -EAGAIN;
	}
	if (request_irq(IRQ_MAC_ADB_CL, pmu_interrupt, 0, "pmu-clock",
			pmu_interrupt)) {
		printk(KERN_ERR "pmu_init: can't get irq %d\n",
			IRQ_MAC_ADB_CL);
		free_irq(IRQ_MAC_ADB_SR, pmu_interrupt);
		return -EAGAIN;
	}

	pmu_fully_inited = 1;
	
	/* Enable backlight */
	pmu_enable_backlight(1);

	printk("adb: PMU 68K driver v0.5 for Unified ADB.\n");

	return 0;
}

int
pmu_get_model(void)
{
	return pmu_kind;
}

/* Send an ADB command */
static int 
pmu_send_request(struct adb_request *req, int sync)
{
    int i, ret;

    if (!pmu_fully_inited)
    {
 	req->complete = 1;
   	return -ENXIO;
   }

    ret = -EINVAL;
	
    switch (req->data[0]) {
    case PMU_PACKET:
		for (i = 0; i < req->nbytes - 1; ++i)
			req->data[i] = req->data[i+1];
		--req->nbytes;
		if (pmu_data_len[req->data[0]][1] != 0) {
			req->reply[0] = ADB_RET_OK;
			req->reply_len = 1;
		} else
			req->reply_len = 0;
		ret = pmu_queue_request(req);
		break;
    case CUDA_PACKET:
		switch (req->data[1]) {
		case CUDA_GET_TIME:
			if (req->nbytes != 2)
				break;
			req->data[0] = PMU_READ_RTC;
			req->nbytes = 1;
			req->reply_len = 3;
			req->reply[0] = CUDA_PACKET;
			req->reply[1] = 0;
			req->reply[2] = CUDA_GET_TIME;
			ret = pmu_queue_request(req);
			break;
		case CUDA_SET_TIME:
			if (req->nbytes != 6)
				break;
			req->data[0] = PMU_SET_RTC;
			req->nbytes = 5;
			for (i = 1; i <= 4; ++i)
				req->data[i] = req->data[i+1];
			req->reply_len = 3;
			req->reply[0] = CUDA_PACKET;
			req->reply[1] = 0;
			req->reply[2] = CUDA_SET_TIME;
			ret = pmu_queue_request(req);
			break;
		case CUDA_GET_PRAM:
			if (req->nbytes != 4)
				break;
			req->data[0] = PMU_READ_NVRAM;
			req->data[1] = req->data[2];
			req->data[2] = req->data[3];
			req->nbytes = 3;
			req->reply_len = 3;
			req->reply[0] = CUDA_PACKET;
			req->reply[1] = 0;
			req->reply[2] = CUDA_GET_PRAM;
			ret = pmu_queue_request(req);
			break;
		case CUDA_SET_PRAM:
			if (req->nbytes != 5)
				break;
			req->data[0] = PMU_WRITE_NVRAM;
			req->data[1] = req->data[2];
			req->data[2] = req->data[3];
			req->data[3] = req->data[4];
			req->nbytes = 4;
			req->reply_len = 3;
			req->reply[0] = CUDA_PACKET;
			req->reply[1] = 0;
			req->reply[2] = CUDA_SET_PRAM;
			ret = pmu_queue_request(req);
			break;
		}
		break;
    case ADB_PACKET:
		for (i = req->nbytes - 1; i > 1; --i)
			req->data[i+2] = req->data[i];
		req->data[3] = req->nbytes - 2;
		req->data[2] = pmu_adb_flags;
		/*req->data[1] = req->data[1];*/
		req->data[0] = PMU_ADB_CMD;
		req->nbytes += 2;
		req->reply_expected = 1;
		req->reply_len = 0;
		ret = pmu_queue_request(req);
		break;
    }
    if (ret)
    {
    	req->complete = 1;
    	return ret;
    }
    	
    if (sync) {
	while (!req->complete)
		pmu_poll();
    }

    return 0;
}

/* Enable/disable autopolling */
static int 
pmu_autopoll(int devs)
{
	struct adb_request req;

	if (!pmu_fully_inited) return -ENXIO;

	if (devs) {
		adb_dev_map = devs;
		pmu_request(&req, NULL, 5, PMU_ADB_CMD, 0, 0x86,
			    adb_dev_map >> 8, adb_dev_map);
		pmu_adb_flags = 2;
	} else {
		pmu_request(&req, NULL, 1, PMU_ADB_POLL_OFF);
		pmu_adb_flags = 0;
	}
	while (!req.complete)
		pmu_poll();
	return 0;
}

/* Reset the ADB bus */
static int 
pmu_reset_bus(void)
{
	struct adb_request req;
	long timeout;
	int save_autopoll = adb_dev_map;

	if (!pmu_fully_inited) return -ENXIO;

	/* anyone got a better idea?? */
	pmu_autopoll(0);

	req.nbytes = 5;
	req.done = NULL;
	req.data[0] = PMU_ADB_CMD;
	req.data[1] = 0;
	req.data[2] = 3; /* ADB_BUSRESET ??? */
	req.data[3] = 0;
	req.data[4] = 0;
	req.reply_len = 0;
	req.reply_expected = 1;
	if (pmu_queue_request(&req) != 0)
	{
		printk(KERN_ERR "pmu_adb_reset_bus: pmu_queue_request failed\n");
		return -EIO;
	}
	while (!req.complete)
		pmu_poll();
	timeout = 100000;
	while (!req.complete) {
		if (--timeout < 0) {
			printk(KERN_ERR "pmu_adb_reset_bus (reset): no response from PMU\n");
			return -EIO;
		}
		udelay(10);
		pmu_poll();
	}

	if (save_autopoll != 0)
		pmu_autopoll(save_autopoll);
		
	return 0;
}

/* Construct and send a pmu request */
int 
pmu_request(struct adb_request *req, void (*done)(struct adb_request *),
	    int nbytes, ...)
{
	va_list list;
	int i;

	if (nbytes < 0 || nbytes > 32) {
		printk(KERN_ERR "pmu_request: bad nbytes (%d)\n", nbytes);
		req->complete = 1;
		return -EINVAL;
	}
	req->nbytes = nbytes;
	req->done = done;
	va_start(list, nbytes);
	for (i = 0; i < nbytes; ++i)
		req->data[i] = va_arg(list, int);
	va_end(list);
	if (pmu_data_len[req->data[0]][1] != 0) {
		req->reply[0] = ADB_RET_OK;
		req->reply_len = 1;
	} else
		req->reply_len = 0;
	req->reply_expected = 0;
	return pmu_queue_request(req);
}

int
pmu_queue_request(struct adb_request *req)
{
	unsigned long flags;
	int nsend;

	if (req->nbytes <= 0) {
		req->complete = 1;
		return 0;
	}
	nsend = pmu_data_len[req->data[0]][0];
	if (nsend >= 0 && req->nbytes != nsend + 1) {
		req->complete = 1;
		return -EINVAL;
	}

	req->next = NULL;
	req->sent = 0;
	req->complete = 0;
	local_irq_save(flags);

	if (current_req != 0) {
		last_req->next = req;
		last_req = req;
	} else {
		current_req = req;
		last_req = req;
		if (pmu_state == idle)
			pmu_start();
	}

	local_irq_restore(flags);
	return 0;
}

static void 
send_byte(int x)
{
	via1[ACR] |= SR_CTRL;
	via1[SR] = x;
	via2[B] &= ~TREQ;		/* assert TREQ */
}

static void 
recv_byte(void)
{
	char c;

	via1[ACR] = (via1[ACR] | SR_EXT) & ~SR_OUT;
	c = via1[SR];		/* resets SR */
	via2[B] &= ~TREQ;
}

static void 
pmu_start(void)
{
	unsigned long flags;
	struct adb_request *req;

	/* assert pmu_state == idle */
	/* get the packet to send */
	local_irq_save(flags);
	req = current_req;
	if (req == 0 || pmu_state != idle
	    || (req->reply_expected && req_awaiting_reply))
		goto out;

	pmu_state = sending;
	data_index = 1;
	data_len = pmu_data_len[req->data[0]][0];

	/* set the shift register to shift out and send a byte */
	send_byte(req->data[0]);

out:
	local_irq_restore(flags);
}

void 
pmu_poll(void)
{
	unsigned long flags;

	local_irq_save(flags);
	if (via1[IFR] & SR_INT) {
		via1[IFR] = SR_INT;
		pmu_interrupt(IRQ_MAC_ADB_SR, NULL);
	}
	if (via1[IFR] & CB1_INT) {
		via1[IFR] = CB1_INT;
		pmu_interrupt(IRQ_MAC_ADB_CL, NULL);
	}
	local_irq_restore(flags);
}

static irqreturn_t
pmu_interrupt(int irq, void *dev_id)
{
	struct adb_request *req;
	int timeout, bite = 0;	/* to prevent compiler warning */

#if 0
	printk("pmu_interrupt: irq %d state %d acr %02X, b %02X data_index %d/%d adb_int_pending %d\n",
		irq, pmu_state, (uint) via1[ACR], (uint) via2[B], data_index, data_len, adb_int_pending);
#endif

	if (irq == IRQ_MAC_ADB_CL) {		/* CB1 interrupt */
		adb_int_pending = 1;
	} else if (irq == IRQ_MAC_ADB_SR) {	/* SR interrupt  */
		if (via2[B] & TACK) {
			printk(KERN_DEBUG "PMU: SR_INT but ack still high! (%x)\n", via2[B]);
		}

		/* if reading grab the byte */
		if ((via1[ACR] & SR_OUT) == 0) bite = via1[SR];

		/* reset TREQ and wait for TACK to go high */
		via2[B] |= TREQ;
		timeout = 3200;
		while (!(via2[B] & TACK)) {
			if (--timeout < 0) {
				printk(KERN_ERR "PMU not responding (!ack)\n");
				goto finish;
			}
			udelay(10);
		}

		switch (pmu_state) {
		case sending:
			req = current_req;
			if (data_len < 0) {
				data_len = req->nbytes - 1;
				send_byte(data_len);
				break;
			}
			if (data_index <= data_len) {
				send_byte(req->data[data_index++]);
				break;
			}
			req->sent = 1;
			data_len = pmu_data_len[req->data[0]][1];
			if (data_len == 0) {
				pmu_state = idle;
				current_req = req->next;
				if (req->reply_expected)
					req_awaiting_reply = req;
				else
					pmu_done(req);
			} else {
				pmu_state = reading;
				data_index = 0;
				reply_ptr = req->reply + req->reply_len;
				recv_byte();
			}
			break;

		case intack:
			data_index = 0;
			data_len = -1;
			pmu_state = reading_intr;
			reply_ptr = interrupt_data;
			recv_byte();
			break;

		case reading:
		case reading_intr:
			if (data_len == -1) {
				data_len = bite;
				if (bite > 32)
					printk(KERN_ERR "PMU: bad reply len %d\n",
					       bite);
			} else {
				reply_ptr[data_index++] = bite;
			}
			if (data_index < data_len) {
				recv_byte();
				break;
			}

			if (pmu_state == reading_intr) {
				pmu_handle_data(interrupt_data, data_index);
			} else {
				req = current_req;
				current_req = req->next;
				req->reply_len += data_index;
				pmu_done(req);
			}
			pmu_state = idle;

			break;

		default:
			printk(KERN_ERR "pmu_interrupt: unknown state %d?\n",
			       pmu_state);
		}
	}
finish:
	if (pmu_state == idle) {
		if (adb_int_pending) {
			pmu_state = intack;
			send_byte(PMU_INT_ACK);
			adb_int_pending = 0;
		} else if (current_req) {
			pmu_start();
		}
	}

#if 0
	printk("pmu_interrupt: exit state %d acr %02X, b %02X data_index %d/%d adb_int_pending %d\n",
		pmu_state, (uint) via1[ACR], (uint) via2[B], data_index, data_len, adb_int_pending);
#endif
	return IRQ_HANDLED;
}

static void 
pmu_done(struct adb_request *req)
{
	req->complete = 1;
	if (req->done)
		(*req->done)(req);
}

/* Interrupt data could be the result data from an ADB cmd */
static void 
pmu_handle_data(unsigned char *data, int len)
{
	static int show_pmu_ints = 1;

	asleep = 0;
	if (len < 1) {
		adb_int_pending = 0;
		return;
	}
	if (data[0] & PMU_INT_ADB) {
		if ((data[0] & PMU_INT_ADB_AUTO) == 0) {
			struct adb_request *req = req_awaiting_reply;
			if (req == 0) {
				printk(KERN_ERR "PMU: extra ADB reply\n");
				return;
			}
			req_awaiting_reply = NULL;
			if (len <= 2)
				req->reply_len = 0;
			else {
				memcpy(req->reply, data + 1, len - 1);
				req->reply_len = len - 1;
			}
			pmu_done(req);
		} else {
			adb_input(data+1, len-1, 1);
		}
	} else {
		if (data[0] == 0x08 && len == 3) {
			/* sound/brightness buttons pressed */
			pmu_set_brightness(data[1] >> 3);
			set_volume(data[2]);
		} else if (show_pmu_ints
			   && !(data[0] == PMU_INT_TICK && len == 1)) {
			int i;
			printk(KERN_DEBUG "pmu intr");
			for (i = 0; i < len; ++i)
				printk(" %.2x", data[i]);
			printk("\n");
		}
	}
}

static int backlight_level = -1;
static int backlight_enabled = 0;

#define LEVEL_TO_BRIGHT(lev)	((lev) < 1? 0x7f: 0x4a - ((lev) << 1))

static void 
pmu_enable_backlight(int on)
{
	struct adb_request req;

	if (on) {
	    /* first call: get current backlight value */
	    if (backlight_level < 0) {
		switch(pmu_kind) {
		    case PMU_68K_V1:
		    case PMU_68K_V2:
			pmu_request(&req, NULL, 3, PMU_READ_NVRAM, 0x14, 0xe);
			while (!req.complete)
				pmu_poll();
			printk(KERN_DEBUG "pmu: nvram returned bright: %d\n", (int)req.reply[1]);
			backlight_level = req.reply[1];
			break;
		    default:
		        backlight_enabled = 0;
		        return;
		}
	    }
	    pmu_request(&req, NULL, 2, PMU_BACKLIGHT_BRIGHT,
	    	LEVEL_TO_BRIGHT(backlight_level));
	    while (!req.complete)
		pmu_poll();
	}
	pmu_request(&req, NULL, 2, PMU_POWER_CTRL,
	    PMU_POW_BACKLIGHT | (on ? PMU_POW_ON : PMU_POW_OFF));
	while (!req.complete)
		pmu_poll();
	backlight_enabled = on;
}

static void 
pmu_set_brightness(int level)
{
	int bright;

	backlight_level = level;
	bright = LEVEL_TO_BRIGHT(level);
	if (!backlight_enabled)
		return;
	if (bright_req_1.complete)
		pmu_request(&bright_req_1, NULL, 2, PMU_BACKLIGHT_BRIGHT,
		    bright);
	if (bright_req_2.complete)
		pmu_request(&bright_req_2, NULL, 2, PMU_POWER_CTRL,
		    PMU_POW_BACKLIGHT | (bright < 0x7f ? PMU_POW_ON : PMU_POW_OFF));
}

void 
pmu_enable_irled(int on)
{
	struct adb_request req;

	pmu_request(&req, NULL, 2, PMU_POWER_CTRL, PMU_POW_IRLED |
	    (on ? PMU_POW_ON : PMU_POW_OFF));
	while (!req.complete)
		pmu_poll();
}

static void 
set_volume(int level)
{
}

int
pmu_present(void)
{
	return (pmu_kind != PMU_UNKNOWN);
}
