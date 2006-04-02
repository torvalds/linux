/* atari_pamsnet.c     PAMsNet device driver for linux68k.
 *
 * Version:	@(#)PAMsNet.c	0.2ß	03/31/96
 *
 * Author:  Torsten Lang <Torsten.Lang@ap.physik.uni-giessen.de>
 *                       <Torsten.Lang@jung.de>
 *
 * This driver is based on my driver PAMSDMA.c for MiNT-Net and
 * on the driver bionet.c written by
 *          Hartmut Laue <laue@ifk-mp.uni-kiel.de>
 * and      Torsten Narjes <narjes@ifk-mp.uni-kiel.de>
 *
 * Little adaptions for integration into pl7 by Roman Hodek
 *
	What is it ?
	------------
	This driver controls the PAMsNet LAN-Adapter which connects
	an ATARI ST/TT via the ACSI-port to an Ethernet-based network.

	This version can be compiled as a loadable module (See the
	compile command at the bottom of this file).
	At load time, you can optionally set the debugging level and the
	fastest response time on the command line of 'insmod'.

	'pamsnet_debug'
		controls the amount of diagnostic messages:
	  0  : no messages
	  >0 : see code for meaning of printed messages

	'pamsnet_min_poll_time' (always >=1)
		gives the time (in jiffies) between polls. Low values
		increase the system load (beware!)

	When loaded, a net device with the name 'eth?' becomes available,
	which can be controlled with the usual 'ifconfig' command.

	It is possible to compile this driver into the kernel like other
	(net) drivers. For this purpose, some source files (e.g. config-files
	makefiles, Space.c) must be changed accordingly. (You may refer to
	other drivers how to do it.) In this case, the device will be detected
	at boot time and (probably) appear as 'eth0'.

	Theory of Operation
	-------------------
	Because the ATARI DMA port is usually shared between several
	devices (eg. harddisk, floppy) we cannot block the ACSI bus
	while waiting for interrupts. Therefore we use a polling mechanism
	to fetch packets from the adapter. For the same reason, we send
	packets without checking that the previous packet has been sent to
	the LAN. We rely on the higher levels of the networking code to detect
	missing packets and resend them.

	Before we access the ATARI DMA controller, we check if another
	process is using the DMA. If not, we lock the DMA, perform one or
	more packet transfers and unlock the DMA before returning.
	We do not use 'stdma_lock' unconditionally because it is unclear
	if the networking code can be set to sleep, which will happen if
	another (possibly slow) device is using the DMA controller.

	The polling is done via timer interrupts which periodically
	'simulate' an interrupt from the Ethernet adapter. The time (in jiffies)
	between polls varies depending on an estimate of the net activity.
	The allowed range is given by the variable 'bionet_min_poll_time'
	for the lower (fastest) limit and the constant 'MAX_POLL_TIME'
	for the higher (slowest) limit.

	Whenever a packet arrives, we switch to fastest response by setting
	the polling time to its lowest limit. If the following poll fails,
	because no packets have arrived, we increase the time for the next
	poll. When the net activity is low, the polling time effectively
	stays at its maximum value, resulting in the lowest load for the
	machine.
 */

#define MAX_POLL_TIME	10

static char *version =
	"pamsnet.c:v0.2beta 30-mar-96 (c) Torsten Lang.\n";

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/errno.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_stdma.h>
#include <asm/atari_acsi.h>

#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/init.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#undef READ
#undef WRITE

/* use 0 for production, 1 for verification, >2 for debug
 */
#ifndef NET_DEBUG
#define NET_DEBUG 0
#endif
/*
 * Global variable 'pamsnet_debug'. Can be set at load time by 'insmod'
 */
unsigned int pamsnet_debug = NET_DEBUG;
module_param(pamsnet_debug, int, 0);
MODULE_PARM_DESC(pamsnet_debug, "pamsnet debug enable (0-1)");
MODULE_LICENSE("GPL");

static unsigned int pamsnet_min_poll_time = 2;


/* Information that need to be kept for each board.
 */
struct net_local {
	struct net_device_stats stats;
	long open_time;			/* for debugging */
	int  poll_time;			/* polling time varies with net load */
};

static struct nic_pkt_s {		/* packet format */
	unsigned char	buffer[2048];
} *nic_packet = 0;
unsigned char *phys_nic_packet;

typedef unsigned char HADDR[6]; /* 6-byte hardware address of lance */

/* Index to functions, as function prototypes.
 */
static void	start (int target);
static int	stop (int target);
static int	testpkt (int target);
static int	sendpkt (int target, unsigned char *buffer, int length);
static int	receivepkt (int target, unsigned char *buffer);
static int	inquiry (int target, unsigned char *buffer);
static HADDR	*read_hw_addr(int target, unsigned char *buffer);
static void	setup_dma (void *address, unsigned rw_flag, int num_blocks);
static int	send_first (int target, unsigned char byte);
static int	send_1_5 (int lun, unsigned char *command, int dma);
static int	get_status (void);
static int	calc_received (void *start_address);

static int pamsnet_open(struct net_device *dev);
static int pamsnet_send_packet(struct sk_buff *skb, struct net_device *dev);
static void pamsnet_poll_rx(struct net_device *);
static int pamsnet_close(struct net_device *dev);
static struct net_device_stats *net_get_stats(struct net_device *dev);
static void pamsnet_tick(unsigned long);

static irqreturn_t pamsnet_intr(int irq, void *data, struct pt_regs *fp);

static DEFINE_TIMER(pamsnet_timer, pamsnet_tick, 0, 0);

#define STRAM_ADDR(a)	(((a) & 0xff000000) == 0)

typedef struct
{
	unsigned char reserved1[0x38];
	HADDR  hwaddr;
	unsigned char reserved2[0x1c2];
} DMAHWADDR;

/*
 * Definitions of commands understood by the PAMs DMA adaptor.
 *
 * In general the DMA adaptor uses LUN 0, 5, 6 and 7 on one ID changeable
 * by the PAM's Net software.
 *
 * LUN 0 works as a harddisk. You can boot the PAM's Net driver there.
 * LUN 5 works as a harddisk and lets you access the RAM and some I/O HW
 *       area. In sector 0, bytes 0x38-0x3d you find the ethernet HW address
 *       of the adaptor.
 * LUN 6 works as a harddisk and lets you access the firmware ROM.
 * LUN 7 lets you send and receive packets.
 *
 * Some commands like the INQUIRY command work identical on all used LUNs.
 *
 * UNKNOWN1 seems to read some data.
 *          Command length is 6 bytes.
 * UNKNOWN2 seems to read some data (command byte 1 must be !=0). The
 *          following bytes seem to be something like an allocation length.
 *          Command length is 6 bytes.
 * READPKT  reads a packet received by the DMA adaptor.
 *          Command length is 6 bytes.
 * WRITEPKT sends a packet transferred by the following DMA phase. The length
 *          of the packet is transferred in command bytes 3 and 4.
 *          The adaptor automatically replaces the src hw address in an ethernet
 *          packet by its own hw address.
 *          Command length is 6 bytes.
 * INQUIRY  has the same function as the INQUIRY command supported by harddisks
 *          and other SCSI devices. It lets you detect which device you found
 *          at a given address.
 *          Command length is 6 bytes.
 * START    initializes the DMA adaptor. After this command it is able to send
 *          and receive packets. There is no status byte returned!
 *          Command length is 1 byte.
 * NUMPKTS  gives back the number of received packets waiting in the queue in
 *          the status byte.
 *          Command length is 1 byte.
 * UNKNOWN3
 * UNKNOWN4 Function of these three commands is unknown.
 * UNKNOWN5 The command length of these three commands is 1 byte.
 * DESELECT immediately deselects the DMA adaptor. May important with interrupt
 *          driven operation.
 *          Command length is 1 byte.
 * STOP     resets the DMA adaptor. After this command packets can no longer
 *          be received or transferred.
 *          Command length is 6 byte.
 */

enum {UNKNOWN1=3, READPKT=8, UNKNOWN2, WRITEPKT=10, INQUIRY=18, START,
      NUMPKTS=22, UNKNOWN3, UNKNOWN4, UNKNOWN5, DESELECT, STOP};

#define READSECTOR  READPKT
#define WRITESECTOR WRITEPKT

u_char *inquire8="MV      PAM's NET/GK";

#define DMALOW   dma_wd.dma_lo
#define DMAMID   dma_wd.dma_md
#define DMAHIGH  dma_wd.dma_hi
#define DACCESS  dma_wd.fdc_acces_seccount

#define MFP_GPIP mfp.par_dt_reg

/* Some useful functions */

#define INT      (!(MFP_GPIP & 0x20))
#define DELAY ({MFP_GPIP; MFP_GPIP; MFP_GPIP;})
#define WRITEMODE(value)					\
	({	u_short dummy = value;				\
		__asm__ volatile("movew %0, 0xFFFF8606" : : "d"(dummy));	\
		DELAY;						\
	})
#define WRITEBOTH(value1, value2)				\
	({	u_long dummy = (u_long)(value1)<<16 | (u_short)(value2);	\
		__asm__ volatile("movel %0, 0xFFFF8604" : : "d"(dummy));	\
		DELAY;						\
	})

/* Definitions for DMODE */

#define READ        0x000
#define WRITE       0x100

#define DMA_FDC     0x080
#define DMA_ACSI    0x000

#define DMA_DISABLE 0x040

#define SEC_COUNT   0x010
#define DMA_WINDOW  0x000

#define REG_ACSI    0x008
#define REG_FDC     0x000

#define A1          0x002

/* Timeout constants */

#define TIMEOUTCMD HZ/2   /* ca. 500ms */
#define TIMEOUTDMA HZ     /* ca. 1s */
#define COMMAND_DELAY 500 /* ca. 0.5ms */

unsigned rw;
int lance_target = -1;
int if_up = 0;

/* The following routines access the ethernet board connected to the
 * ACSI port via the st_dma chip.
 */

/* The following lowlevel routines work on physical addresses only and assume
 * that eventually needed buffers are
 * - completely located in ST RAM
 * - are contigous in the physical address space
 */

/* Setup the DMA counter */

static void
setup_dma (address, rw_flag, num_blocks)
	void *address;
	unsigned rw_flag;
	int num_blocks;
{
	WRITEMODE((unsigned) rw_flag          | DMA_FDC | SEC_COUNT | REG_ACSI |
		  A1);
	WRITEMODE((unsigned)(rw_flag ^ WRITE) | DMA_FDC | SEC_COUNT | REG_ACSI |
		  A1);
	WRITEMODE((unsigned) rw_flag          | DMA_FDC | SEC_COUNT | REG_ACSI |
		  A1);
	DMALOW  = (unsigned char)((unsigned long)address & 0xFF);
	DMAMID  = (unsigned char)(((unsigned long)address >>  8) & 0xFF);
	DMAHIGH = (unsigned char)(((unsigned long)address >> 16) & 0xFF);
	WRITEBOTH((unsigned)num_blocks & 0xFF,
		  rw_flag | DMA_FDC | DMA_WINDOW | REG_ACSI | A1);
	rw = rw_flag;
}

/* Send the first byte of an command block */

static int
send_first (target, byte)
	int target;
	unsigned char byte;
{
	rw = READ;
	acsi_delay_end(COMMAND_DELAY);
	/*
	 * wake up ACSI
	 */
	WRITEMODE(DMA_FDC | DMA_WINDOW | REG_ACSI);
	/*
	 * write command byte
	 */
	WRITEBOTH((target << 5) | (byte & 0x1F), DMA_FDC |
	          DMA_WINDOW | REG_ACSI | A1);
	return (!acsi_wait_for_IRQ(TIMEOUTCMD));
}

/* Send the rest of an command block */

static int
send_1_5 (lun, command, dma)
	int lun;
	unsigned char *command;
	int dma;
{
	int i, j;

	for (i=0; i<5; i++) {
		WRITEBOTH((!i ? (((lun & 0x7) << 5) | (command[i] & 0x1F))
			      : command[i]),
			  rw | REG_ACSI | DMA_WINDOW |
			   ((i < 4) ? DMA_FDC
				    : (dma ? DMA_ACSI
					   : DMA_FDC)) | A1);
		if (i < 4 && (j = !acsi_wait_for_IRQ(TIMEOUTCMD)))
			return (j);
	}
	return (0);
}

/* Read a status byte */

static int
get_status (void)
{
	WRITEMODE(DMA_FDC | DMA_WINDOW | REG_ACSI | A1);
	acsi_delay_start();
	return ((int)(DACCESS & 0xFF));
}

/* Calculate the number of received bytes */

static int
calc_received (start_address)
	void *start_address;
{
	return (int)(
		(((unsigned long)DMAHIGH << 16) | ((unsigned)DMAMID << 8) | DMALOW)
	      - (unsigned long)start_address);
}

/* The following midlevel routines still work on physical addresses ... */

/* start() starts the PAM's DMA adaptor */

static void
start (target)
	int target;
{
	send_first(target, START);
}

/* stop() stops the PAM's DMA adaptor and returns a value of zero in case of success */

static int
stop (target)
	int target;
{
	int ret = -1;
	unsigned char cmd_buffer[5];

	if (send_first(target, STOP))
		goto bad;
	cmd_buffer[0] = cmd_buffer[1] = cmd_buffer[2] =
	cmd_buffer[3] = cmd_buffer[4] = 0;
	if (send_1_5(7, cmd_buffer, 0) ||
	    !acsi_wait_for_IRQ(TIMEOUTDMA) ||
	    get_status())
		goto bad;
	ret = 0;
bad:
	return (ret);
}

/* testpkt() returns the number of received packets waiting in the queue */

static int
testpkt(target)
	int target;
{
	int ret = -1;

	if (send_first(target, NUMPKTS))
		goto bad;
	ret = get_status();
bad:
	return (ret);
}

/* inquiry() returns 0 when PAM's DMA found, -1 when timeout, -2 otherwise */
/* Please note: The buffer is for internal use only but must be defined!   */

static int
inquiry (target, buffer)
	int target;
	unsigned char *buffer;
{
	int ret = -1;
	unsigned char *vbuffer = phys_to_virt((unsigned long)buffer);
	unsigned char cmd_buffer[5];

	if (send_first(target, INQUIRY))
		goto bad;
	setup_dma(buffer, READ, 1);
	vbuffer[8] = vbuffer[27] = 0; /* Avoid confusion with previous read data */
	cmd_buffer[0] = cmd_buffer[1] = cmd_buffer[2] = cmd_buffer[4] = 0;
	cmd_buffer[3] = 48;
	if (send_1_5(5, cmd_buffer, 1) ||
	    !acsi_wait_for_IRQ(TIMEOUTDMA) ||
	    get_status() ||
	    (calc_received(buffer) < 32))
		goto bad;
	dma_cache_maintenance((unsigned long)(buffer+8), 20, 0);
	if (memcmp(inquire8, vbuffer+8, 20))
		goto bad;
	ret = 0;
bad:
	if (!!NET_DEBUG) {
		vbuffer[8+20]=0;
		printk("inquiry of target %d: %s\n", target, vbuffer+8);
	}
	return (ret);
}

/*
 * read_hw_addr() reads the sector containing the hwaddr and returns
 * a pointer to it (virtual address!) or 0 in case of an error
 */

static HADDR
*read_hw_addr(target, buffer)
	int target;
	unsigned char *buffer;
{
	HADDR *ret = 0;
	unsigned char cmd_buffer[5];

	if (send_first(target, READSECTOR))
		goto bad;
	setup_dma(buffer, READ, 1);
	cmd_buffer[0] = cmd_buffer[1] = cmd_buffer[2] = cmd_buffer[4] = 0;
	cmd_buffer[3] = 1;
	if (send_1_5(5, cmd_buffer, 1) ||
	    !acsi_wait_for_IRQ(TIMEOUTDMA) ||
	    get_status())
		goto bad;
	ret = phys_to_virt((unsigned long)&(((DMAHWADDR *)buffer)->hwaddr));
	dma_cache_maintenance((unsigned long)buffer, 512, 0);
bad:
	return (ret);
}

static irqreturn_t
pamsnet_intr(irq, data, fp)
	int irq;
	void *data;
	struct pt_regs *fp;
{
	return IRQ_HANDLED;
}

/* receivepkt() loads a packet to a given buffer and returns its length */

static int
receivepkt (target, buffer)
	int target;
	unsigned char *buffer;
{
	int ret = -1;
	unsigned char cmd_buffer[5];

	if (send_first(target, READPKT))
		goto bad;
	setup_dma(buffer, READ, 3);
	cmd_buffer[0] = cmd_buffer[1] = cmd_buffer[2] = cmd_buffer[4] = 0;
	cmd_buffer[3] = 3;
	if (send_1_5(7, cmd_buffer, 1) ||
	    !acsi_wait_for_IRQ(TIMEOUTDMA) ||
	    get_status())
		goto bad;
	ret = calc_received(buffer);
bad:
	return (ret);
}

/* sendpkt() sends a packet and returns a value of zero when the packet was sent
             successfully */

static int
sendpkt (target, buffer, length)
	int target;
	unsigned char *buffer;
	int length;
{
	int ret = -1;
	unsigned char cmd_buffer[5];

	if (send_first(target, WRITEPKT))
		goto bad;
	setup_dma(buffer, WRITE, 3);
	cmd_buffer[0] = cmd_buffer[1] = cmd_buffer[4] = 0;
	cmd_buffer[2] = length >> 8;
	cmd_buffer[3] = length & 0xFF;
	if (send_1_5(7, cmd_buffer, 1) ||
	    !acsi_wait_for_IRQ(TIMEOUTDMA) ||
	    get_status())
		goto bad;
	ret = 0;
bad:
	return (ret);
}

/* The following higher level routines work on virtual addresses and convert them to
 * physical addresses when passed to the lowlevel routines. It's up to the higher level
 * routines to copy data from Alternate RAM to ST RAM if neccesary!
 */

/* Check for a network adaptor of this type, and return '0' if one exists.
 */

struct net_device * __init pamsnet_probe (int unit)
{
	struct net_device *dev;
	int i;
	HADDR *hwaddr;
	int err;

	unsigned char station_addr[6];
	static unsigned version_printed;
	/* avoid "Probing for..." printed 4 times - the driver is supporting only one adapter now! */
	static int no_more_found;

	if (no_more_found)
		return ERR_PTR(-ENODEV);
	no_more_found = 1;

	dev = alloc_etherdev(sizeof(struct net_local));
	if (!dev)
		return ERR_PTR(-ENOMEM);
	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
	}
	SET_MODULE_OWNER(dev);

	printk("Probing for PAM's Net/GK Adapter...\n");

	/* Allocate the DMA buffer here since we need it for probing! */

	nic_packet = (struct nic_pkt_s *)acsi_buffer;
	phys_nic_packet = (unsigned char *)phys_acsi_buffer;
	if (pamsnet_debug > 0) {
		printk("nic_packet at 0x%p, phys at 0x%p\n",
			   nic_packet, phys_nic_packet );
	}

	stdma_lock(pamsnet_intr, NULL);
	DISABLE_IRQ();

	for (i=0; i<8; i++) {
		/* Do two inquiries to cover cases with strange equipment on previous ID */
		/* blocking the ACSI bus (like the SLMC804 laser printer controller...   */
		inquiry(i, phys_nic_packet);
		if (!inquiry(i, phys_nic_packet)) {
			lance_target = i;
			break;
		}
	}

	if (!!NET_DEBUG)
		printk("ID: %d\n",i);

	if (lance_target >= 0) {
		if (!(hwaddr = read_hw_addr(lance_target, phys_nic_packet)))
			lance_target = -1;
		else
			memcpy (station_addr, hwaddr, ETH_ALEN);
	}

	ENABLE_IRQ();
	stdma_release();

	if (lance_target < 0) {
		printk("No PAM's Net/GK found.\n");
		free_netdev(dev);
		return ERR_PTR(-ENODEV);
	}

	if (pamsnet_debug > 0 && version_printed++ == 0)
		printk(version);

	printk("%s: %s found on target %01d, eth-addr: %02x:%02x:%02x:%02x:%02x:%02x.\n",
		dev->name, "PAM's Net/GK", lance_target,
		station_addr[0], station_addr[1], station_addr[2],
		station_addr[3], station_addr[4], station_addr[5]);

	/* Initialize the device structure. */
	dev->open		= pamsnet_open;
	dev->stop		= pamsnet_close;
	dev->hard_start_xmit	= pamsnet_send_packet;
	dev->get_stats		= net_get_stats;

	/* Fill in the fields of the device structure with ethernet-generic
	 * values. This should be in a common file instead of per-driver.
	 */

	for (i = 0; i < ETH_ALEN; i++) {
#if 0
		dev->broadcast[i] = 0xff;
#endif
		dev->dev_addr[i]  = station_addr[i];
	}
	err = register_netdev(dev);
	if (!err)
		return dev;

	free_netdev(dev);
	return ERR_PTR(err);
}

/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'ifconfig' program is run.

   This routine should set everything up anew at each open, even
   registers that "should" only need to be set once at boot, so that
   there is non-reboot way to recover if something goes wrong.
 */
static int
pamsnet_open(struct net_device *dev) {
	struct net_local *lp = netdev_priv(dev);

	if (pamsnet_debug > 0)
		printk("pamsnet_open\n");
	stdma_lock(pamsnet_intr, NULL);
	DISABLE_IRQ();

	/* Reset the hardware here.
	 */
	if (!if_up)
		start(lance_target);
	if_up = 1;
	lp->open_time = 0;	/*jiffies*/
	lp->poll_time = MAX_POLL_TIME;

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	ENABLE_IRQ();
	stdma_release();
	pamsnet_timer.data = (long)dev;
	pamsnet_timer.expires = jiffies + lp->poll_time;
	add_timer(&pamsnet_timer);
	return 0;
}

static int
pamsnet_send_packet(struct sk_buff *skb, struct net_device *dev) {
	struct net_local *lp = netdev_priv(dev);
	unsigned long flags;

	/* Block a timer-based transmit from overlapping.  This could better be
	 * done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
	 */
	local_irq_save(flags);

	if (stdma_islocked()) {
		local_irq_restore(flags);
		lp->stats.tx_errors++;
	}
	else {
		int length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
		unsigned long buf = virt_to_phys(skb->data);
		int stat;

		stdma_lock(pamsnet_intr, NULL);
		DISABLE_IRQ();

		local_irq_restore(flags);
		if( !STRAM_ADDR(buf+length-1) ) {
			memcpy(nic_packet->buffer, skb->data, length);
			buf = (unsigned long)phys_nic_packet;
		}

		dma_cache_maintenance(buf, length, 1);

		stat = sendpkt(lance_target, (unsigned char *)buf, length);
		ENABLE_IRQ();
		stdma_release();

		dev->trans_start = jiffies;
		dev->tbusy	 = 0;
		lp->stats.tx_packets++;
		lp->stats.tx_bytes+=length;
	}
	dev_kfree_skb(skb);

	return 0;
}

/* We have a good packet(s), get it/them out of the buffers.
 */
static void
pamsnet_poll_rx(struct net_device *dev) {
	struct net_local *lp = netdev_priv(dev);
	int boguscount;
	int pkt_len;
	struct sk_buff *skb;
	unsigned long flags;

	local_irq_save(flags);
	/* ++roman: Take care at locking the ST-DMA... This must be done with ints
	 * off, since otherwise an int could slip in between the question and the
	 * locking itself, and then we'd go to sleep... And locking itself is
	 * necessary to keep the floppy_change timer from working with ST-DMA
	 * registers. */
	if (stdma_islocked()) {
		local_irq_restore(flags);
		return;
	}
	stdma_lock(pamsnet_intr, NULL);
	DISABLE_IRQ();
	local_irq_restore(flags);

	boguscount = testpkt(lance_target);
	if( lp->poll_time < MAX_POLL_TIME ) lp->poll_time++;

	while(boguscount--) {
		pkt_len = receivepkt(lance_target, phys_nic_packet);

		if( pkt_len < 60 ) break;

		/* Good packet... */

		dma_cache_maintenance((unsigned long)phys_nic_packet, pkt_len, 0);

		lp->poll_time = pamsnet_min_poll_time;    /* fast poll */
		if( pkt_len >= 60 && pkt_len <= 2048 ) {
			if (pkt_len > 1514)
				pkt_len = 1514;

			/* Malloc up new buffer.
			 */
			skb = alloc_skb(pkt_len, GFP_ATOMIC);
			if (skb == NULL) {
				printk("%s: Memory squeeze, dropping packet.\n",
					dev->name);
				lp->stats.rx_dropped++;
				break;
			}
			skb->len = pkt_len;
			skb->dev = dev;

			/* 'skb->data' points to the start of sk_buff data area.
			 */
			memcpy(skb->data, nic_packet->buffer, pkt_len);
			netif_rx(skb);
			dev->last_rx = jiffies;
			lp->stats.rx_packets++;
			lp->stats.rx_bytes+=pkt_len;
		}
	}

	/* If any worth-while packets have been received, dev_rint()
	   has done a mark_bh(INET_BH) for us and will work on them
	   when we get to the bottom-half routine.
	 */

	ENABLE_IRQ();
	stdma_release();
	return;
}

/* pamsnet_tick: called by pamsnet_timer. Reads packets from the adapter,
 * passes them to the higher layers and restarts the timer.
 */
static void
pamsnet_tick(unsigned long data) {
	struct net_device	 *dev = (struct net_device *)data;
	struct net_local *lp = netdev_priv(dev);

	if( pamsnet_debug > 0 && (lp->open_time++ & 7) == 8 )
		printk("pamsnet_tick: %ld\n", lp->open_time);

	pamsnet_poll_rx(dev);

	pamsnet_timer.expires = jiffies + lp->poll_time;
	add_timer(&pamsnet_timer);
}

/* The inverse routine to pamsnet_open().
 */
static int
pamsnet_close(struct net_device *dev) {
	struct net_local *lp = netdev_priv(dev);

	if (pamsnet_debug > 0)
		printk("pamsnet_close, open_time=%ld\n", lp->open_time);
	del_timer(&pamsnet_timer);
	stdma_lock(pamsnet_intr, NULL);
	DISABLE_IRQ();

	if (if_up)
		stop(lance_target);
	if_up = 0;

	lp->open_time = 0;

	dev->tbusy = 1;
	dev->start = 0;

	ENABLE_IRQ();
	stdma_release();
	return 0;
}

/* Get the current statistics.
   This may be called with the card open or closed.
 */
static struct net_device_stats *net_get_stats(struct net_device *dev) 
{
	struct net_local *lp = netdev_priv(dev);
	return &lp->stats;
}


#ifdef MODULE

static struct net_device *pam_dev;

int init_module(void)
{
	pam_dev = pamsnet_probe(-1);
	if (IS_ERR(pam_dev))
		return PTR_ERR(pam_dev);
	return 0;
}

void cleanup_module(void)
{
	unregister_netdev(pam_dev);
	free_netdev(pam_dev);
}

#endif /* MODULE */

/* Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include
	-b m68k-linuxaout -Wall -Wstrict-prototypes -O2
	-fomit-frame-pointer -pipe -DMODULE -I../../net/inet -c atari_pamsnet.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 8
 * End:
 */
