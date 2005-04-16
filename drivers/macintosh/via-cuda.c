/*
 * Device driver for the via-cuda on Apple Powermacs.
 *
 * The VIA (versatile interface adapter) interfaces to the CUDA,
 * a 6805 microprocessor core which controls the ADB (Apple Desktop
 * Bus) which connects to the keyboard and mouse.  The CUDA also
 * controls system power and the RTC (real time clock) chip.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <stdarg.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#ifdef CONFIG_PPC
#include <asm/prom.h>
#include <asm/machdep.h>
#else
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/machw.h>
#include <asm/mac_via.h>
#endif
#include <asm/io.h>
#include <asm/system.h>
#include <linux/init.h>

static volatile unsigned char __iomem *via;
static DEFINE_SPINLOCK(cuda_lock);

#ifdef CONFIG_MAC
#define CUDA_IRQ IRQ_MAC_ADB
#define __openfirmware
#define eieio()
#else
#define CUDA_IRQ vias->intrs[0].line
#endif

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

/* Bits in B data register: all active low */
#define TREQ		0x08		/* Transfer request (input) */
#define TACK		0x10		/* Transfer acknowledge (output) */
#define TIP		0x20		/* Transfer in progress (output) */

/* Bits in ACR */
#define SR_CTRL		0x1c		/* Shift register control bits */
#define SR_EXT		0x0c		/* Shift on external clock */
#define SR_OUT		0x10		/* Shift out if 1 */

/* Bits in IFR and IER */
#define IER_SET		0x80		/* set bits in IER */
#define IER_CLR		0		/* clear bits in IER */
#define SR_INT		0x04		/* Shift register full/empty */

static enum cuda_state {
    idle,
    sent_first_byte,
    sending,
    reading,
    read_done,
    awaiting_reply
} cuda_state;

static struct adb_request *current_req;
static struct adb_request *last_req;
static unsigned char cuda_rbuf[16];
static unsigned char *reply_ptr;
static int reading_reply;
static int data_index;
#ifdef CONFIG_PPC
static struct device_node *vias;
#endif
static int cuda_fully_inited = 0;

#ifdef CONFIG_ADB
static int cuda_probe(void);
static int cuda_init(void);
static int cuda_send_request(struct adb_request *req, int sync);
static int cuda_adb_autopoll(int devs);
static int cuda_reset_adb_bus(void);
#endif /* CONFIG_ADB */

static int cuda_init_via(void);
static void cuda_start(void);
static irqreturn_t cuda_interrupt(int irq, void *arg, struct pt_regs *regs);
static void cuda_input(unsigned char *buf, int nb, struct pt_regs *regs);
void cuda_poll(void);
static int cuda_write(struct adb_request *req);

int cuda_request(struct adb_request *req,
		 void (*done)(struct adb_request *), int nbytes, ...);

#ifdef CONFIG_ADB
struct adb_driver via_cuda_driver = {
	"CUDA",
	cuda_probe,
	cuda_init,
	cuda_send_request,
	cuda_adb_autopoll,
	cuda_poll,
	cuda_reset_adb_bus
};
#endif /* CONFIG_ADB */

#ifdef CONFIG_PPC
int __init
find_via_cuda(void)
{
    int err;
    struct adb_request req;

    if (vias != 0)
	return 1;
    vias = find_devices("via-cuda");
    if (vias == 0)
	return 0;
    if (vias->next != 0)
	printk(KERN_WARNING "Warning: only using 1st via-cuda\n");

#if 0
    { int i;

    printk("find_via_cuda: node = %p, addrs =", vias->node);
    for (i = 0; i < vias->n_addrs; ++i)
	printk(" %x(%x)", vias->addrs[i].address, vias->addrs[i].size);
    printk(", intrs =");
    for (i = 0; i < vias->n_intrs; ++i)
	printk(" %x", vias->intrs[i].line);
    printk("\n"); }
#endif

    if (vias->n_addrs != 1 || vias->n_intrs != 1) {
	printk(KERN_ERR "via-cuda: expecting 1 address (%d) and 1 interrupt (%d)\n",
	       vias->n_addrs, vias->n_intrs);
	if (vias->n_addrs < 1 || vias->n_intrs < 1)
	    return 0;
    }
    via = ioremap(vias->addrs->address, 0x2000);

    cuda_state = idle;
    sys_ctrler = SYS_CTRLER_CUDA;

    err = cuda_init_via();
    if (err) {
	printk(KERN_ERR "cuda_init_via() failed\n");
	via = NULL;
	return 0;
    }

    /* Clear and enable interrupts, but only on PPC. On 68K it's done  */
    /* for us by the main VIA driver in arch/m68k/mac/via.c        */

#ifndef CONFIG_MAC
    out_8(&via[IFR], 0x7f);	/* clear interrupts by writing 1s */
    out_8(&via[IER], IER_SET|SR_INT); /* enable interrupt from SR */
#endif

    /* enable autopoll */
    cuda_request(&req, NULL, 3, CUDA_PACKET, CUDA_AUTOPOLL, 1);
    while (!req.complete)
	cuda_poll();

    return 1;
}
#endif /* CONFIG_PPC */

static int __init via_cuda_start(void)
{
    if (via == NULL)
	return -ENODEV;

#ifdef CONFIG_PPC
    request_OF_resource(vias, 0, NULL);
#endif

    if (request_irq(CUDA_IRQ, cuda_interrupt, 0, "ADB", cuda_interrupt)) {
	printk(KERN_ERR "cuda_init: can't get irq %d\n", CUDA_IRQ);
	return -EAGAIN;
    }

    printk("Macintosh CUDA driver v0.5 for Unified ADB.\n");

    cuda_fully_inited = 1;
    return 0;
}

device_initcall(via_cuda_start);

#ifdef CONFIG_ADB
static int
cuda_probe(void)
{
#ifdef CONFIG_PPC
    if (sys_ctrler != SYS_CTRLER_CUDA)
	return -ENODEV;
#else
    if (macintosh_config->adb_type != MAC_ADB_CUDA)
	return -ENODEV;
    via = via1;
#endif
    return 0;
}

static int __init
cuda_init(void)
{
#ifdef CONFIG_PPC
    if (via == NULL)
	return -ENODEV;
    return 0;
#else 
    int err = cuda_init_via();
    if (err) {
	printk(KERN_ERR "cuda_init_via() failed\n");
	return -ENODEV;
    }

    return via_cuda_start();
#endif
}
#endif /* CONFIG_ADB */

#define WAIT_FOR(cond, what)					\
    do {                                                        \
    	int x;							\
	for (x = 1000; !(cond); --x) {				\
	    if (x == 0) {					\
		printk("Timeout waiting for " what "\n");	\
		return -ENXIO;					\
	    }							\
	    udelay(100);					\
	}							\
    } while (0)

static int
cuda_init_via(void)
{
    out_8(&via[DIRB], (in_8(&via[DIRB]) | TACK | TIP) & ~TREQ);	/* TACK & TIP out */
    out_8(&via[B], in_8(&via[B]) | TACK | TIP);			/* negate them */
    out_8(&via[ACR] ,(in_8(&via[ACR]) & ~SR_CTRL) | SR_EXT);	/* SR data in */
    (void)in_8(&via[SR]);						/* clear any left-over data */
#ifndef CONFIG_MAC
    out_8(&via[IER], 0x7f);					/* disable interrupts from VIA */
    (void)in_8(&via[IER]);
#endif

    /* delay 4ms and then clear any pending interrupt */
    mdelay(4);
    (void)in_8(&via[SR]);
    out_8(&via[IFR], in_8(&via[IFR]) & 0x7f);

    /* sync with the CUDA - assert TACK without TIP */
    out_8(&via[B], in_8(&via[B]) & ~TACK);

    /* wait for the CUDA to assert TREQ in response */
    WAIT_FOR((in_8(&via[B]) & TREQ) == 0, "CUDA response to sync");

    /* wait for the interrupt and then clear it */
    WAIT_FOR(in_8(&via[IFR]) & SR_INT, "CUDA response to sync (2)");
    (void)in_8(&via[SR]);
    out_8(&via[IFR], in_8(&via[IFR]) & 0x7f);

    /* finish the sync by negating TACK */
    out_8(&via[B], in_8(&via[B]) | TACK);

    /* wait for the CUDA to negate TREQ and the corresponding interrupt */
    WAIT_FOR(in_8(&via[B]) & TREQ, "CUDA response to sync (3)");
    WAIT_FOR(in_8(&via[IFR]) & SR_INT, "CUDA response to sync (4)");
    (void)in_8(&via[SR]);
    out_8(&via[IFR], in_8(&via[IFR]) & 0x7f);
    out_8(&via[B], in_8(&via[B]) | TIP);	/* should be unnecessary */

    return 0;
}

#ifdef CONFIG_ADB
/* Send an ADB command */
static int
cuda_send_request(struct adb_request *req, int sync)
{
    int i;

    if ((via == NULL) || !cuda_fully_inited) {
	req->complete = 1;
	return -ENXIO;
    }
  
    req->reply_expected = 1;

    i = cuda_write(req);
    if (i)
	return i;

    if (sync) {
	while (!req->complete)
	    cuda_poll();
    }
    return 0;
}


/* Enable/disable autopolling */
static int
cuda_adb_autopoll(int devs)
{
    struct adb_request req;

    if ((via == NULL) || !cuda_fully_inited)
	return -ENXIO;

    cuda_request(&req, NULL, 3, CUDA_PACKET, CUDA_AUTOPOLL, (devs? 1: 0));
    while (!req.complete)
	cuda_poll();
    return 0;
}

/* Reset adb bus - how do we do this?? */
static int
cuda_reset_adb_bus(void)
{
    struct adb_request req;

    if ((via == NULL) || !cuda_fully_inited)
	return -ENXIO;

    cuda_request(&req, NULL, 2, ADB_PACKET, 0);		/* maybe? */
    while (!req.complete)
	cuda_poll();
    return 0;
}
#endif /* CONFIG_ADB */
/* Construct and send a cuda request */
int
cuda_request(struct adb_request *req, void (*done)(struct adb_request *),
	     int nbytes, ...)
{
    va_list list;
    int i;

    if (via == NULL) {
	req->complete = 1;
	return -ENXIO;
    }

    req->nbytes = nbytes;
    req->done = done;
    va_start(list, nbytes);
    for (i = 0; i < nbytes; ++i)
	req->data[i] = va_arg(list, int);
    va_end(list);
    req->reply_expected = 1;
    return cuda_write(req);
}

static int
cuda_write(struct adb_request *req)
{
    unsigned long flags;

    if (req->nbytes < 2 || req->data[0] > CUDA_PACKET) {
	req->complete = 1;
	return -EINVAL;
    }
    req->next = NULL;
    req->sent = 0;
    req->complete = 0;
    req->reply_len = 0;

    spin_lock_irqsave(&cuda_lock, flags);
    if (current_req != 0) {
	last_req->next = req;
	last_req = req;
    } else {
	current_req = req;
	last_req = req;
	if (cuda_state == idle)
	    cuda_start();
    }
    spin_unlock_irqrestore(&cuda_lock, flags);

    return 0;
}

static void
cuda_start(void)
{
    struct adb_request *req;

    /* assert cuda_state == idle */
    /* get the packet to send */
    req = current_req;
    if (req == 0)
	return;
    if ((in_8(&via[B]) & TREQ) == 0)
	return;			/* a byte is coming in from the CUDA */

    /* set the shift register to shift out and send a byte */
    out_8(&via[ACR], in_8(&via[ACR]) | SR_OUT);
    out_8(&via[SR], req->data[0]);
    out_8(&via[B], in_8(&via[B]) & ~TIP);
    cuda_state = sent_first_byte;
}

void
cuda_poll(void)
{
    unsigned long flags;

    /* cuda_interrupt only takes a normal lock, we disable
     * interrupts here to avoid re-entering and thus deadlocking.
     * An option would be to disable only the IRQ source with
     * disable_irq(), would that work on m68k ? --BenH
     */
    local_irq_save(flags);
    cuda_interrupt(0, NULL, NULL);
    local_irq_restore(flags);
}

static irqreturn_t
cuda_interrupt(int irq, void *arg, struct pt_regs *regs)
{
    int status;
    struct adb_request *req = NULL;
    unsigned char ibuf[16];
    int ibuf_len = 0;
    int complete = 0;
    unsigned char virq;
    
    spin_lock(&cuda_lock);

    virq = in_8(&via[IFR]) & 0x7f;
    out_8(&via[IFR], virq);   
    if ((virq & SR_INT) == 0) {
        spin_unlock(&cuda_lock);
	return IRQ_NONE;
    }
    
    status = (~in_8(&via[B]) & (TIP|TREQ)) | (in_8(&via[ACR]) & SR_OUT);
    /* printk("cuda_interrupt: state=%d status=%x\n", cuda_state, status); */
    switch (cuda_state) {
    case idle:
	/* CUDA has sent us the first byte of data - unsolicited */
	if (status != TREQ)
	    printk("cuda: state=idle, status=%x\n", status);
	(void)in_8(&via[SR]);
	out_8(&via[B], in_8(&via[B]) & ~TIP);
	cuda_state = reading;
	reply_ptr = cuda_rbuf;
	reading_reply = 0;
	break;

    case awaiting_reply:
	/* CUDA has sent us the first byte of data of a reply */
	if (status != TREQ)
	    printk("cuda: state=awaiting_reply, status=%x\n", status);
	(void)in_8(&via[SR]);
	out_8(&via[B], in_8(&via[B]) & ~TIP);
	cuda_state = reading;
	reply_ptr = current_req->reply;
	reading_reply = 1;
	break;

    case sent_first_byte:
	if (status == TREQ + TIP + SR_OUT) {
	    /* collision */
	    out_8(&via[ACR], in_8(&via[ACR]) & ~SR_OUT);
	    (void)in_8(&via[SR]);
	    out_8(&via[B], in_8(&via[B]) | TIP | TACK);
	    cuda_state = idle;
	} else {
	    /* assert status == TIP + SR_OUT */
	    if (status != TIP + SR_OUT)
		printk("cuda: state=sent_first_byte status=%x\n", status);
	    out_8(&via[SR], current_req->data[1]);
	    out_8(&via[B], in_8(&via[B]) ^ TACK);
	    data_index = 2;
	    cuda_state = sending;
	}
	break;

    case sending:
	req = current_req;
	if (data_index >= req->nbytes) {
	    out_8(&via[ACR], in_8(&via[ACR]) & ~SR_OUT);
	    (void)in_8(&via[SR]);
	    out_8(&via[B], in_8(&via[B]) | TACK | TIP);
	    req->sent = 1;
	    if (req->reply_expected) {
		cuda_state = awaiting_reply;
	    } else {
		current_req = req->next;
		complete = 1;
		/* not sure about this */
		cuda_state = idle;
		cuda_start();
	    }
	} else {
	    out_8(&via[SR], req->data[data_index++]);
	    out_8(&via[B], in_8(&via[B]) ^ TACK);
	}
	break;

    case reading:
	*reply_ptr++ = in_8(&via[SR]);
	if (status == TIP) {
	    /* that's all folks */
	    out_8(&via[B], in_8(&via[B]) | TACK | TIP);
	    cuda_state = read_done;
	} else {
	    /* assert status == TIP | TREQ */
	    if (status != TIP + TREQ)
		printk("cuda: state=reading status=%x\n", status);
	    out_8(&via[B], in_8(&via[B]) ^ TACK);
	}
	break;

    case read_done:
	(void)in_8(&via[SR]);
	if (reading_reply) {
	    req = current_req;
	    req->reply_len = reply_ptr - req->reply;
	    if (req->data[0] == ADB_PACKET) {
		/* Have to adjust the reply from ADB commands */
		if (req->reply_len <= 2 || (req->reply[1] & 2) != 0) {
		    /* the 0x2 bit indicates no response */
		    req->reply_len = 0;
		} else {
		    /* leave just the command and result bytes in the reply */
		    req->reply_len -= 2;
		    memmove(req->reply, req->reply + 2, req->reply_len);
		}
	    }
	    current_req = req->next;
	    complete = 1;
	} else {
	    /* This is tricky. We must break the spinlock to call
	     * cuda_input. However, doing so means we might get
	     * re-entered from another CPU getting an interrupt
	     * or calling cuda_poll(). I ended up using the stack
	     * (it's only for 16 bytes) and moving the actual
	     * call to cuda_input to outside of the lock.
	     */
	    ibuf_len = reply_ptr - cuda_rbuf;
	    memcpy(ibuf, cuda_rbuf, ibuf_len);
	}
	if (status == TREQ) {
	    out_8(&via[B], in_8(&via[B]) & ~TIP);
	    cuda_state = reading;
	    reply_ptr = cuda_rbuf;
	    reading_reply = 0;
	} else {
	    cuda_state = idle;
	    cuda_start();
	}
	break;

    default:
	printk("cuda_interrupt: unknown cuda_state %d?\n", cuda_state);
    }
    spin_unlock(&cuda_lock);
    if (complete && req) {
    	void (*done)(struct adb_request *) = req->done;
    	mb();
    	req->complete = 1;
    	/* Here, we assume that if the request has a done member, the
    	 * struct request will survive to setting req->complete to 1
    	 */
    	if (done)
		(*done)(req);
    }
    if (ibuf_len)
	cuda_input(ibuf, ibuf_len, regs);
    return IRQ_HANDLED;
}

static void
cuda_input(unsigned char *buf, int nb, struct pt_regs *regs)
{
    int i;

    switch (buf[0]) {
    case ADB_PACKET:
#ifdef CONFIG_XMON
	if (nb == 5 && buf[2] == 0x2c) {
	    extern int xmon_wants_key, xmon_adb_keycode;
	    if (xmon_wants_key) {
		xmon_adb_keycode = buf[3];
		return;
	    }
	}
#endif /* CONFIG_XMON */
#ifdef CONFIG_ADB
	adb_input(buf+2, nb-2, regs, buf[1] & 0x40);
#endif /* CONFIG_ADB */
	break;

    default:
	printk("data from cuda (%d bytes):", nb);
	for (i = 0; i < nb; ++i)
	    printk(" %.2x", buf[i]);
	printk("\n");
    }
}
