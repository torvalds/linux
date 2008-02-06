/*
 * m8xx_pcmcia.c - Linux PCMCIA socket driver for the mpc8xx series.
 *
 * (C) 1999-2000 Magnus Damm <damm@bitsmart.com>
 * (C) 2001-2002 Montavista Software, Inc.
 *     <mlocke@mvista.com>
 *
 * Support for two slots by Cyclades Corporation
 *     <oliver.kurth@cyclades.de>
 * Further fixes, v2.6 kernel port
 *     <marcelo.tosatti@cyclades.com>
 * 
 * Some fixes, additions (C) 2005-2007 Montavista Software, Inc.
 *     <vbordug@ru.mvista.com>
 *
 * "The ExCA standard specifies that socket controllers should provide
 * two IO and five memory windows per socket, which can be independently
 * configured and positioned in the host address space and mapped to
 * arbitrary segments of card address space. " - David A Hinds. 1999
 *
 * This controller does _not_ meet the ExCA standard.
 *
 * m8xx pcmcia controller brief info:
 * + 8 windows (attrib, mem, i/o)
 * + up to two slots (SLOT_A and SLOT_B)
 * + inputpins, outputpins, event and mask registers.
 * - no offset register. sigh.
 *
 * Because of the lacking offset register we must map the whole card.
 * We assign each memory window PCMCIA_MEM_WIN_SIZE address space.
 * Make sure there is (PCMCIA_MEM_WIN_SIZE * PCMCIA_MEM_WIN_NO
 * * PCMCIA_SOCKETS_NO) bytes at PCMCIA_MEM_WIN_BASE.
 * The i/o windows are dynamically allocated at PCMCIA_IO_WIN_BASE.
 * They are maximum 64KByte each...
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/string.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fsl_devices.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/time.h>
#include <asm/mpc8xx.h>
#include <asm/8xx_immap.h>
#include <asm/irq.h>
#include <asm/fs_pd.h>
#include <asm/of_device.h>
#include <asm/of_platform.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/ss.h>

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0);
#define dprintk(args...) printk(KERN_DEBUG "m8xx_pcmcia: " args);
#else
#define dprintk(args...)
#endif

#define pcmcia_info(args...) printk(KERN_INFO "m8xx_pcmcia: "args)
#define pcmcia_error(args...) printk(KERN_ERR "m8xx_pcmcia: "args)

static const char *version = "Version 0.06, Aug 2005";
MODULE_LICENSE("Dual MPL/GPL");

#if !defined(CONFIG_PCMCIA_SLOT_A) && !defined(CONFIG_PCMCIA_SLOT_B)

/* The RPX series use SLOT_B */
#if defined(CONFIG_RPXCLASSIC) || defined(CONFIG_RPXLITE)
#define CONFIG_PCMCIA_SLOT_B
#define CONFIG_BD_IS_MHZ
#endif

/* The ADS board use SLOT_A */
#ifdef CONFIG_ADS
#define CONFIG_PCMCIA_SLOT_A
#define CONFIG_BD_IS_MHZ
#endif

/* The FADS series are a mess */
#ifdef CONFIG_FADS
#if defined(CONFIG_MPC860T) || defined(CONFIG_MPC860) || defined(CONFIG_MPC821)
#define CONFIG_PCMCIA_SLOT_A
#else
#define CONFIG_PCMCIA_SLOT_B
#endif
#endif

#if defined(CONFIG_MPC885ADS)
#define CONFIG_PCMCIA_SLOT_A
#define PCMCIA_GLITCHY_CD
#endif

/* Cyclades ACS uses both slots */
#ifdef CONFIG_PRxK
#define CONFIG_PCMCIA_SLOT_A
#define CONFIG_PCMCIA_SLOT_B
#endif

#endif				/* !defined(CONFIG_PCMCIA_SLOT_A) && !defined(CONFIG_PCMCIA_SLOT_B) */

#if defined(CONFIG_PCMCIA_SLOT_A) && defined(CONFIG_PCMCIA_SLOT_B)

#define PCMCIA_SOCKETS_NO 2
/* We have only 8 windows, dualsocket support will be limited. */
#define PCMCIA_MEM_WIN_NO 2
#define PCMCIA_IO_WIN_NO  2
#define PCMCIA_SLOT_MSG "SLOT_A and SLOT_B"

#elif defined(CONFIG_PCMCIA_SLOT_A) || defined(CONFIG_PCMCIA_SLOT_B)

#define PCMCIA_SOCKETS_NO 1
/* full support for one slot */
#define PCMCIA_MEM_WIN_NO 5
#define PCMCIA_IO_WIN_NO  2

/* define _slot_ to be able to optimize macros */

#ifdef CONFIG_PCMCIA_SLOT_A
#define _slot_ 0
#define PCMCIA_SLOT_MSG "SLOT_A"
#else
#define _slot_ 1
#define PCMCIA_SLOT_MSG "SLOT_B"
#endif

#else
#error m8xx_pcmcia: Bad configuration!
#endif

/* ------------------------------------------------------------------------- */

#define PCMCIA_MEM_WIN_BASE 0xe0000000	/* base address for memory window 0   */
#define PCMCIA_MEM_WIN_SIZE 0x04000000	/* each memory window is 64 MByte     */
#define PCMCIA_IO_WIN_BASE  _IO_BASE	/* base address for io window 0       */
/* ------------------------------------------------------------------------- */

static int pcmcia_schlvl;

static DEFINE_SPINLOCK(events_lock);

#define PCMCIA_SOCKET_KEY_5V 1
#define PCMCIA_SOCKET_KEY_LV 2

/* look up table for pgcrx registers */
static u32 *m8xx_pgcrx[2];

/*
 * This structure is used to address each window in the PCMCIA controller.
 *
 * Keep in mind that we assume that pcmcia_win[n+1] is mapped directly
 * after pcmcia_win[n]...
 */

struct pcmcia_win {
	u32 br;
	u32 or;
};

/*
 * For some reason the hardware guys decided to make both slots share
 * some registers.
 *
 * Could someone invent object oriented hardware ?
 *
 * The macros are used to get the right bit from the registers.
 * SLOT_A : slot = 0
 * SLOT_B : slot = 1
 */

#define M8XX_PCMCIA_VS1(slot)      (0x80000000 >> (slot << 4))
#define M8XX_PCMCIA_VS2(slot)      (0x40000000 >> (slot << 4))
#define M8XX_PCMCIA_VS_MASK(slot)  (0xc0000000 >> (slot << 4))
#define M8XX_PCMCIA_VS_SHIFT(slot) (30 - (slot << 4))

#define M8XX_PCMCIA_WP(slot)       (0x20000000 >> (slot << 4))
#define M8XX_PCMCIA_CD2(slot)      (0x10000000 >> (slot << 4))
#define M8XX_PCMCIA_CD1(slot)      (0x08000000 >> (slot << 4))
#define M8XX_PCMCIA_BVD2(slot)     (0x04000000 >> (slot << 4))
#define M8XX_PCMCIA_BVD1(slot)     (0x02000000 >> (slot << 4))
#define M8XX_PCMCIA_RDY(slot)      (0x01000000 >> (slot << 4))
#define M8XX_PCMCIA_RDY_L(slot)    (0x00800000 >> (slot << 4))
#define M8XX_PCMCIA_RDY_H(slot)    (0x00400000 >> (slot << 4))
#define M8XX_PCMCIA_RDY_R(slot)    (0x00200000 >> (slot << 4))
#define M8XX_PCMCIA_RDY_F(slot)    (0x00100000 >> (slot << 4))
#define M8XX_PCMCIA_MASK(slot)     (0xFFFF0000 >> (slot << 4))

#define M8XX_PCMCIA_POR_VALID    0x00000001
#define M8XX_PCMCIA_POR_WRPROT   0x00000002
#define M8XX_PCMCIA_POR_ATTRMEM  0x00000010
#define M8XX_PCMCIA_POR_IO       0x00000018
#define M8XX_PCMCIA_POR_16BIT    0x00000040

#define M8XX_PGCRX(slot)  m8xx_pgcrx[slot]

#define M8XX_PGCRX_CXOE    0x00000080
#define M8XX_PGCRX_CXRESET 0x00000040

/* we keep one lookup table per socket to check flags */

#define PCMCIA_EVENTS_MAX 5	/* 4 max at a time + termination */

struct event_table {
	u32 regbit;
	u32 eventbit;
};

static const char driver_name[] = "m8xx-pcmcia";

struct socket_info {
	void (*handler) (void *info, u32 events);
	void *info;

	u32 slot;
	pcmconf8xx_t *pcmcia;
	u32 bus_freq;
	int hwirq;

	socket_state_t state;
	struct pccard_mem_map mem_win[PCMCIA_MEM_WIN_NO];
	struct pccard_io_map io_win[PCMCIA_IO_WIN_NO];
	struct event_table events[PCMCIA_EVENTS_MAX];
	struct pcmcia_socket socket;
};

static struct socket_info socket[PCMCIA_SOCKETS_NO];

/*
 * Search this table to see if the windowsize is
 * supported...
 */

#define M8XX_SIZES_NO 32

static const u32 m8xx_size_to_gray[M8XX_SIZES_NO] = {
	0x00000001, 0x00000002, 0x00000008, 0x00000004,
	0x00000080, 0x00000040, 0x00000010, 0x00000020,
	0x00008000, 0x00004000, 0x00001000, 0x00002000,
	0x00000100, 0x00000200, 0x00000800, 0x00000400,

	0x0fffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0x01000000, 0x02000000, 0xffffffff, 0x04000000,
	0x00010000, 0x00020000, 0x00080000, 0x00040000,
	0x00800000, 0x00400000, 0x00100000, 0x00200000
};

/* ------------------------------------------------------------------------- */

static irqreturn_t m8xx_interrupt(int irq, void *dev);

#define PCMCIA_BMT_LIMIT (15*4)	/* Bus Monitor Timeout value */

/* ------------------------------------------------------------------------- */
/* board specific stuff:                                                     */
/* voltage_set(), hardware_enable() and hardware_disable()                   */
/* ------------------------------------------------------------------------- */
/* RPX Boards from Embedded Planet                                           */

#if defined(CONFIG_RPXCLASSIC) || defined(CONFIG_RPXLITE)

/* The RPX boards seems to have it's bus monitor timeout set to 6*8 clocks.
 * SYPCR is write once only, therefore must the slowest memory be faster
 * than the bus monitor or we will get a machine check due to the bus timeout.
 */

#define PCMCIA_BOARD_MSG "RPX CLASSIC or RPX LITE"

#undef PCMCIA_BMT_LIMIT
#define PCMCIA_BMT_LIMIT (6*8)

static int voltage_set(int slot, int vcc, int vpp)
{
	u32 reg = 0;

	switch (vcc) {
	case 0:
		break;
	case 33:
		reg |= BCSR1_PCVCTL4;
		break;
	case 50:
		reg |= BCSR1_PCVCTL5;
		break;
	default:
		return 1;
	}

	switch (vpp) {
	case 0:
		break;
	case 33:
	case 50:
		if (vcc == vpp)
			reg |= BCSR1_PCVCTL6;
		else
			return 1;
		break;
	case 120:
		reg |= BCSR1_PCVCTL7;
	default:
		return 1;
	}

	if (!((vcc == 50) || (vcc == 0)))
		return 1;

	/* first, turn off all power */

	out_be32(((u32 *) RPX_CSR_ADDR),
		 in_be32(((u32 *) RPX_CSR_ADDR)) & ~(BCSR1_PCVCTL4 |
						     BCSR1_PCVCTL5 |
						     BCSR1_PCVCTL6 |
						     BCSR1_PCVCTL7));

	/* enable new powersettings */

	out_be32(((u32 *) RPX_CSR_ADDR), in_be32(((u32 *) RPX_CSR_ADDR)) | reg);

	return 0;
}

#define socket_get(_slot_) PCMCIA_SOCKET_KEY_5V
#define hardware_enable(_slot_)	/* No hardware to enable */
#define hardware_disable(_slot_)	/* No hardware to disable */

#endif				/* CONFIG_RPXCLASSIC */

/* FADS Boards from Motorola                                               */

#if defined(CONFIG_FADS)

#define PCMCIA_BOARD_MSG "FADS"

static int voltage_set(int slot, int vcc, int vpp)
{
	u32 reg = 0;

	switch (vcc) {
	case 0:
		break;
	case 33:
		reg |= BCSR1_PCCVCC0;
		break;
	case 50:
		reg |= BCSR1_PCCVCC1;
		break;
	default:
		return 1;
	}

	switch (vpp) {
	case 0:
		break;
	case 33:
	case 50:
		if (vcc == vpp)
			reg |= BCSR1_PCCVPP1;
		else
			return 1;
		break;
	case 120:
		if ((vcc == 33) || (vcc == 50))
			reg |= BCSR1_PCCVPP0;
		else
			return 1;
	default:
		return 1;
	}

	/* first, turn off all power */
	out_be32((u32 *) BCSR1,
		 in_be32((u32 *) BCSR1) & ~(BCSR1_PCCVCC_MASK |
					    BCSR1_PCCVPP_MASK));

	/* enable new powersettings */
	out_be32((u32 *) BCSR1, in_be32((u32 *) BCSR1) | reg);

	return 0;
}

#define socket_get(_slot_) PCMCIA_SOCKET_KEY_5V

static void hardware_enable(int slot)
{
	out_be32((u32 *) BCSR1, in_be32((u32 *) BCSR1) & ~BCSR1_PCCEN);
}

static void hardware_disable(int slot)
{
	out_be32((u32 *) BCSR1, in_be32((u32 *) BCSR1) | BCSR1_PCCEN);
}

#endif

/* MPC885ADS Boards */

#if defined(CONFIG_MPC885ADS)

#define PCMCIA_BOARD_MSG "MPC885ADS"
#define socket_get(_slot_) PCMCIA_SOCKET_KEY_5V

static inline void hardware_enable(int slot)
{
	m8xx_pcmcia_ops.hw_ctrl(slot, 1);
}

static inline void hardware_disable(int slot)
{
	m8xx_pcmcia_ops.hw_ctrl(slot, 0);
}

static inline int voltage_set(int slot, int vcc, int vpp)
{
	return m8xx_pcmcia_ops.voltage_set(slot, vcc, vpp);
}

#endif

/* ------------------------------------------------------------------------- */
/* Motorola MBX860                                                           */

#if defined(CONFIG_MBX)

#define PCMCIA_BOARD_MSG "MBX"

static int voltage_set(int slot, int vcc, int vpp)
{
	u8 reg = 0;

	switch (vcc) {
	case 0:
		break;
	case 33:
		reg |= CSR2_VCC_33;
		break;
	case 50:
		reg |= CSR2_VCC_50;
		break;
	default:
		return 1;
	}

	switch (vpp) {
	case 0:
		break;
	case 33:
	case 50:
		if (vcc == vpp)
			reg |= CSR2_VPP_VCC;
		else
			return 1;
		break;
	case 120:
		if ((vcc == 33) || (vcc == 50))
			reg |= CSR2_VPP_12;
		else
			return 1;
	default:
		return 1;
	}

	/* first, turn off all power */
	out_8((u8 *) MBX_CSR2_ADDR,
	      in_8((u8 *) MBX_CSR2_ADDR) & ~(CSR2_VCC_MASK | CSR2_VPP_MASK));

	/* enable new powersettings */
	out_8((u8 *) MBX_CSR2_ADDR, in_8((u8 *) MBX_CSR2_ADDR) | reg);

	return 0;
}

#define socket_get(_slot_) PCMCIA_SOCKET_KEY_5V
#define hardware_enable(_slot_)	/* No hardware to enable */
#define hardware_disable(_slot_)	/* No hardware to disable */

#endif				/* CONFIG_MBX */

#if defined(CONFIG_PRxK)
#include <asm/cpld.h>
extern volatile fpga_pc_regs *fpga_pc;

#define PCMCIA_BOARD_MSG "MPC855T"

static int voltage_set(int slot, int vcc, int vpp)
{
	u8 reg = 0;
	u8 regread;
	cpld_regs *ccpld = get_cpld();

	switch (vcc) {
	case 0:
		break;
	case 33:
		reg |= PCMCIA_VCC_33;
		break;
	case 50:
		reg |= PCMCIA_VCC_50;
		break;
	default:
		return 1;
	}

	switch (vpp) {
	case 0:
		break;
	case 33:
	case 50:
		if (vcc == vpp)
			reg |= PCMCIA_VPP_VCC;
		else
			return 1;
		break;
	case 120:
		if ((vcc == 33) || (vcc == 50))
			reg |= PCMCIA_VPP_12;
		else
			return 1;
	default:
		return 1;
	}

	reg = reg >> (slot << 2);
	regread = in_8(&ccpld->fpga_pc_ctl);
	if (reg !=
	    (regread & ((PCMCIA_VCC_MASK | PCMCIA_VPP_MASK) >> (slot << 2)))) {
		/* enable new powersettings */
		regread =
		    regread & ~((PCMCIA_VCC_MASK | PCMCIA_VPP_MASK) >>
				(slot << 2));
		out_8(&ccpld->fpga_pc_ctl, reg | regread);
		msleep(100);
	}

	return 0;
}

#define socket_get(_slot_) PCMCIA_SOCKET_KEY_LV
#define hardware_enable(_slot_)	/* No hardware to enable */
#define hardware_disable(_slot_)	/* No hardware to disable */

#endif				/* CONFIG_PRxK */

static u32 pending_events[PCMCIA_SOCKETS_NO];
static DEFINE_SPINLOCK(pending_event_lock);

static irqreturn_t m8xx_interrupt(int irq, void *dev)
{
	struct socket_info *s;
	struct event_table *e;
	unsigned int i, events, pscr, pipr, per;
	pcmconf8xx_t *pcmcia = socket[0].pcmcia;

	dprintk("Interrupt!\n");
	/* get interrupt sources */

	pscr = in_be32(&pcmcia->pcmc_pscr);
	pipr = in_be32(&pcmcia->pcmc_pipr);
	per = in_be32(&pcmcia->pcmc_per);

	for (i = 0; i < PCMCIA_SOCKETS_NO; i++) {
		s = &socket[i];
		e = &s->events[0];
		events = 0;

		while (e->regbit) {
			if (pscr & e->regbit)
				events |= e->eventbit;

			e++;
		}

		/*
		 * report only if both card detect signals are the same
		 * not too nice done,
		 * we depend on that CD2 is the bit to the left of CD1...
		 */
		if (events & SS_DETECT)
			if (((pipr & M8XX_PCMCIA_CD2(i)) >> 1) ^
			    (pipr & M8XX_PCMCIA_CD1(i))) {
				events &= ~SS_DETECT;
			}
#ifdef PCMCIA_GLITCHY_CD
		/*
		 * I've experienced CD problems with my ADS board.
		 * We make an extra check to see if there was a
		 * real change of Card detection.
		 */

		if ((events & SS_DETECT) &&
		    ((pipr &
		      (M8XX_PCMCIA_CD2(i) | M8XX_PCMCIA_CD1(i))) == 0) &&
		    (s->state.Vcc | s->state.Vpp)) {
			events &= ~SS_DETECT;
			/*printk( "CD glitch workaround - CD = 0x%08x!\n",
			   (pipr & (M8XX_PCMCIA_CD2(i)
			   | M8XX_PCMCIA_CD1(i)))); */
		}
#endif

		/* call the handler */

		dprintk("slot %u: events = 0x%02x, pscr = 0x%08x, "
			"pipr = 0x%08x\n", i, events, pscr, pipr);

		if (events) {
			spin_lock(&pending_event_lock);
			pending_events[i] |= events;
			spin_unlock(&pending_event_lock);
			/*
			 * Turn off RDY_L bits in the PER mask on
			 * CD interrupt receival.
			 *
			 * They can generate bad interrupts on the
			 * ACS4,8,16,32.   - marcelo
			 */
			per &= ~M8XX_PCMCIA_RDY_L(0);
			per &= ~M8XX_PCMCIA_RDY_L(1);

			out_be32(&pcmcia->pcmc_per, per);

			if (events)
				pcmcia_parse_events(&socket[i].socket, events);
		}
	}

	/* clear the interrupt sources */
	out_be32(&pcmcia->pcmc_pscr, pscr);

	dprintk("Interrupt done.\n");

	return IRQ_HANDLED;
}

static u32 m8xx_get_graycode(u32 size)
{
	u32 k;

	for (k = 0; k < M8XX_SIZES_NO; k++)
		if (m8xx_size_to_gray[k] == size)
			break;

	if ((k == M8XX_SIZES_NO) || (m8xx_size_to_gray[k] == -1))
		k = -1;

	return k;
}

static u32 m8xx_get_speed(u32 ns, u32 is_io, u32 bus_freq)
{
	u32 reg, clocks, psst, psl, psht;

	if (!ns) {

		/*
		 * We get called with IO maps setup to 0ns
		 * if not specified by the user.
		 * They should be 255ns.
		 */

		if (is_io)
			ns = 255;
		else
			ns = 100;	/* fast memory if 0 */
	}

	/*
	 * In PSST, PSL, PSHT fields we tell the controller
	 * timing parameters in CLKOUT clock cycles.
	 * CLKOUT is the same as GCLK2_50.
	 */

/* how we want to adjust the timing - in percent */

#define ADJ 180			/* 80 % longer accesstime - to be sure */

	clocks = ((bus_freq / 1000) * ns) / 1000;
	clocks = (clocks * ADJ) / (100 * 1000);
	if (clocks >= PCMCIA_BMT_LIMIT) {
		printk("Max access time limit reached\n");
		clocks = PCMCIA_BMT_LIMIT - 1;
	}

	psst = clocks / 7;	/* setup time */
	psht = clocks / 7;	/* hold time */
	psl = (clocks * 5) / 7;	/* strobe length */

	psst += clocks - (psst + psht + psl);

	reg = psst << 12;
	reg |= psl << 7;
	reg |= psht << 16;

	return reg;
}

static int m8xx_get_status(struct pcmcia_socket *sock, unsigned int *value)
{
	int lsock = container_of(sock, struct socket_info, socket)->slot;
	struct socket_info *s = &socket[lsock];
	unsigned int pipr, reg;
	pcmconf8xx_t *pcmcia = s->pcmcia;

	pipr = in_be32(&pcmcia->pcmc_pipr);

	*value = ((pipr & (M8XX_PCMCIA_CD1(lsock)
			   | M8XX_PCMCIA_CD2(lsock))) == 0) ? SS_DETECT : 0;
	*value |= (pipr & M8XX_PCMCIA_WP(lsock)) ? SS_WRPROT : 0;

	if (s->state.flags & SS_IOCARD)
		*value |= (pipr & M8XX_PCMCIA_BVD1(lsock)) ? SS_STSCHG : 0;
	else {
		*value |= (pipr & M8XX_PCMCIA_RDY(lsock)) ? SS_READY : 0;
		*value |= (pipr & M8XX_PCMCIA_BVD1(lsock)) ? SS_BATDEAD : 0;
		*value |= (pipr & M8XX_PCMCIA_BVD2(lsock)) ? SS_BATWARN : 0;
	}

	if (s->state.Vcc | s->state.Vpp)
		*value |= SS_POWERON;

	/*
	 * Voltage detection:
	 * This driver only supports 16-Bit pc-cards.
	 * Cardbus is not handled here.
	 *
	 * To determine what voltage to use we must read the VS1 and VS2 pin.
	 * Depending on what socket type is present,
	 * different combinations mean different things.
	 *
	 * Card Key  Socket Key   VS1   VS2   Card         Vcc for CIS parse
	 *
	 * 5V        5V, LV*      NC    NC    5V only       5V (if available)
	 *
	 * 5V        5V, LV*      GND   NC    5 or 3.3V     as low as possible
	 *
	 * 5V        5V, LV*      GND   GND   5, 3.3, x.xV  as low as possible
	 *
	 * LV*       5V            -     -    shall not fit into socket
	 *
	 * LV*       LV*          GND   NC    3.3V only     3.3V
	 *
	 * LV*       LV*          NC    GND   x.xV          x.xV (if avail.)
	 *
	 * LV*       LV*          GND   GND   3.3 or x.xV   as low as possible
	 *
	 * *LV means Low Voltage
	 *
	 *
	 * That gives us the following table:
	 *
	 * Socket    VS1  VS2   Voltage
	 *
	 * 5V        NC   NC    5V
	 * 5V        NC   GND   none (should not be possible)
	 * 5V        GND  NC    >= 3.3V
	 * 5V        GND  GND   >= x.xV
	 *
	 * LV        NC   NC    5V   (if available)
	 * LV        NC   GND   x.xV (if available)
	 * LV        GND  NC    3.3V
	 * LV        GND  GND   >= x.xV
	 *
	 * So, how do I determine if I have a 5V or a LV
	 * socket on my board?  Look at the socket!
	 *
	 *
	 * Socket with 5V key:
	 * ++--------------------------------------------+
	 * ||                                            |
	 * ||                                           ||
	 * ||                                           ||
	 * |                                             |
	 * +---------------------------------------------+
	 *
	 * Socket with LV key:
	 * ++--------------------------------------------+
	 * ||                                            |
	 * |                                            ||
	 * |                                            ||
	 * |                                             |
	 * +---------------------------------------------+
	 *
	 *
	 * With other words - LV only cards does not fit
	 * into the 5V socket!
	 */

	/* read out VS1 and VS2 */

	reg = (pipr & M8XX_PCMCIA_VS_MASK(lsock))
	    >> M8XX_PCMCIA_VS_SHIFT(lsock);

	if (socket_get(lsock) == PCMCIA_SOCKET_KEY_LV) {
		switch (reg) {
		case 1:
			*value |= SS_3VCARD;
			break;	/* GND, NC - 3.3V only */
		case 2:
			*value |= SS_XVCARD;
			break;	/* NC. GND - x.xV only */
		};
	}

	dprintk("GetStatus(%d) = %#2.2x\n", lsock, *value);
	return 0;
}

static int m8xx_set_socket(struct pcmcia_socket *sock, socket_state_t * state)
{
	int lsock = container_of(sock, struct socket_info, socket)->slot;
	struct socket_info *s = &socket[lsock];
	struct event_table *e;
	unsigned int reg;
	unsigned long flags;
	pcmconf8xx_t *pcmcia = socket[0].pcmcia;

	dprintk("SetSocket(%d, flags %#3.3x, Vcc %d, Vpp %d, "
		"io_irq %d, csc_mask %#2.2x)\n", lsock, state->flags,
		state->Vcc, state->Vpp, state->io_irq, state->csc_mask);

	/* First, set voltage - bail out if invalid */
	if (voltage_set(lsock, state->Vcc, state->Vpp))
		return -EINVAL;

	/* Take care of reset... */
	if (state->flags & SS_RESET)
		out_be32(M8XX_PGCRX(lsock), in_be32(M8XX_PGCRX(lsock)) | M8XX_PGCRX_CXRESET);	/* active high */
	else
		out_be32(M8XX_PGCRX(lsock),
			 in_be32(M8XX_PGCRX(lsock)) & ~M8XX_PGCRX_CXRESET);

	/* ... and output enable. */

	/* The CxOE signal is connected to a 74541 on the ADS.
	   I guess most other boards used the ADS as a reference.
	   I tried to control the CxOE signal with SS_OUTPUT_ENA,
	   but the reset signal seems connected via the 541.
	   If the CxOE is left high are some signals tristated and
	   no pullups are present -> the cards act weird.
	   So right now the buffers are enabled if the power is on. */

	if (state->Vcc || state->Vpp)
		out_be32(M8XX_PGCRX(lsock), in_be32(M8XX_PGCRX(lsock)) & ~M8XX_PGCRX_CXOE);	/* active low */
	else
		out_be32(M8XX_PGCRX(lsock),
			 in_be32(M8XX_PGCRX(lsock)) | M8XX_PGCRX_CXOE);

	/*
	 * We'd better turn off interrupts before
	 * we mess with the events-table..
	 */

	spin_lock_irqsave(&events_lock, flags);

	/*
	 * Play around with the interrupt mask to be able to
	 * give the events the generic pcmcia driver wants us to.
	 */

	e = &s->events[0];
	reg = 0;

	if (state->csc_mask & SS_DETECT) {
		e->eventbit = SS_DETECT;
		reg |= e->regbit = (M8XX_PCMCIA_CD2(lsock)
				    | M8XX_PCMCIA_CD1(lsock));
		e++;
	}
	if (state->flags & SS_IOCARD) {
		/*
		 * I/O card
		 */
		if (state->csc_mask & SS_STSCHG) {
			e->eventbit = SS_STSCHG;
			reg |= e->regbit = M8XX_PCMCIA_BVD1(lsock);
			e++;
		}
		/*
		 * If io_irq is non-zero we should enable irq.
		 */
		if (state->io_irq) {
			out_be32(M8XX_PGCRX(lsock),
				 in_be32(M8XX_PGCRX(lsock)) |
				 mk_int_int_mask(s->hwirq) << 24);
			/*
			 * Strange thing here:
			 * The manual does not tell us which interrupt
			 * the sources generate.
			 * Anyhow, I found out that RDY_L generates IREQLVL.
			 *
			 * We use level triggerd interrupts, and they don't
			 * have to be cleared in PSCR in the interrupt handler.
			 */
			reg |= M8XX_PCMCIA_RDY_L(lsock);
		} else
			out_be32(M8XX_PGCRX(lsock),
				 in_be32(M8XX_PGCRX(lsock)) & 0x00ffffff);
	} else {
		/*
		 * Memory card
		 */
		if (state->csc_mask & SS_BATDEAD) {
			e->eventbit = SS_BATDEAD;
			reg |= e->regbit = M8XX_PCMCIA_BVD1(lsock);
			e++;
		}
		if (state->csc_mask & SS_BATWARN) {
			e->eventbit = SS_BATWARN;
			reg |= e->regbit = M8XX_PCMCIA_BVD2(lsock);
			e++;
		}
		/* What should I trigger on - low/high,raise,fall? */
		if (state->csc_mask & SS_READY) {
			e->eventbit = SS_READY;
			reg |= e->regbit = 0;	//??
			e++;
		}
	}

	e->regbit = 0;		/* terminate list */

	/*
	 * Clear the status changed .
	 * Port A and Port B share the same port.
	 * Writing ones will clear the bits.
	 */

	out_be32(&pcmcia->pcmc_pscr, reg);

	/*
	 * Write the mask.
	 * Port A and Port B share the same port.
	 * Need for read-modify-write.
	 * Ones will enable the interrupt.
	 */

	reg |=
	    in_be32(&pcmcia->
		    pcmc_per) & (M8XX_PCMCIA_MASK(0) | M8XX_PCMCIA_MASK(1));
	out_be32(&pcmcia->pcmc_per, reg);

	spin_unlock_irqrestore(&events_lock, flags);

	/* copy the struct and modify the copy */

	s->state = *state;

	return 0;
}

static int m8xx_set_io_map(struct pcmcia_socket *sock, struct pccard_io_map *io)
{
	int lsock = container_of(sock, struct socket_info, socket)->slot;

	struct socket_info *s = &socket[lsock];
	struct pcmcia_win *w;
	unsigned int reg, winnr;
	pcmconf8xx_t *pcmcia = s->pcmcia;

#define M8XX_SIZE (io->stop - io->start + 1)
#define M8XX_BASE (PCMCIA_IO_WIN_BASE + io->start)

	dprintk("SetIOMap(%d, %d, %#2.2x, %d ns, "
		"%#4.4x-%#4.4x)\n", lsock, io->map, io->flags,
		io->speed, io->start, io->stop);

	if ((io->map >= PCMCIA_IO_WIN_NO) || (io->start > 0xffff)
	    || (io->stop > 0xffff) || (io->stop < io->start))
		return -EINVAL;

	if ((reg = m8xx_get_graycode(M8XX_SIZE)) == -1)
		return -EINVAL;

	if (io->flags & MAP_ACTIVE) {

		dprintk("io->flags & MAP_ACTIVE\n");

		winnr = (PCMCIA_MEM_WIN_NO * PCMCIA_SOCKETS_NO)
		    + (lsock * PCMCIA_IO_WIN_NO) + io->map;

		/* setup registers */

		w = (void *)&pcmcia->pcmc_pbr0;
		w += winnr;

		out_be32(&w->or, 0);	/* turn off window first */
		out_be32(&w->br, M8XX_BASE);

		reg <<= 27;
		reg |= M8XX_PCMCIA_POR_IO | (lsock << 2);

		reg |= m8xx_get_speed(io->speed, 1, s->bus_freq);

		if (io->flags & MAP_WRPROT)
			reg |= M8XX_PCMCIA_POR_WRPROT;

		/*if(io->flags & (MAP_16BIT | MAP_AUTOSZ)) */
		if (io->flags & MAP_16BIT)
			reg |= M8XX_PCMCIA_POR_16BIT;

		if (io->flags & MAP_ACTIVE)
			reg |= M8XX_PCMCIA_POR_VALID;

		out_be32(&w->or, reg);

		dprintk("Socket %u: Mapped io window %u at %#8.8x, "
			"OR = %#8.8x.\n", lsock, io->map, w->br, w->or);
	} else {
		/* shutdown IO window */
		winnr = (PCMCIA_MEM_WIN_NO * PCMCIA_SOCKETS_NO)
		    + (lsock * PCMCIA_IO_WIN_NO) + io->map;

		/* setup registers */

		w = (void *)&pcmcia->pcmc_pbr0;
		w += winnr;

		out_be32(&w->or, 0);	/* turn off window */
		out_be32(&w->br, 0);	/* turn off base address */

		dprintk("Socket %u: Unmapped io window %u at %#8.8x, "
			"OR = %#8.8x.\n", lsock, io->map, w->br, w->or);
	}

	/* copy the struct and modify the copy */
	s->io_win[io->map] = *io;
	s->io_win[io->map].flags &= (MAP_WRPROT | MAP_16BIT | MAP_ACTIVE);
	dprintk("SetIOMap exit\n");

	return 0;
}

static int m8xx_set_mem_map(struct pcmcia_socket *sock,
			    struct pccard_mem_map *mem)
{
	int lsock = container_of(sock, struct socket_info, socket)->slot;
	struct socket_info *s = &socket[lsock];
	struct pcmcia_win *w;
	struct pccard_mem_map *old;
	unsigned int reg, winnr;
	pcmconf8xx_t *pcmcia = s->pcmcia;

	dprintk("SetMemMap(%d, %d, %#2.2x, %d ns, "
		"%#5.5lx, %#5.5x)\n", lsock, mem->map, mem->flags,
		mem->speed, mem->static_start, mem->card_start);

	if ((mem->map >= PCMCIA_MEM_WIN_NO)
//          || ((mem->s) >= PCMCIA_MEM_WIN_SIZE)
	    || (mem->card_start >= 0x04000000)
	    || (mem->static_start & 0xfff)	/* 4KByte resolution */
	    ||(mem->card_start & 0xfff))
		return -EINVAL;

	if ((reg = m8xx_get_graycode(PCMCIA_MEM_WIN_SIZE)) == -1) {
		printk("Cannot set size to 0x%08x.\n", PCMCIA_MEM_WIN_SIZE);
		return -EINVAL;
	}
	reg <<= 27;

	winnr = (lsock * PCMCIA_MEM_WIN_NO) + mem->map;

	/* Setup the window in the pcmcia controller */

	w = (void *)&pcmcia->pcmc_pbr0;
	w += winnr;

	reg |= lsock << 2;

	reg |= m8xx_get_speed(mem->speed, 0, s->bus_freq);

	if (mem->flags & MAP_ATTRIB)
		reg |= M8XX_PCMCIA_POR_ATTRMEM;

	if (mem->flags & MAP_WRPROT)
		reg |= M8XX_PCMCIA_POR_WRPROT;

	if (mem->flags & MAP_16BIT)
		reg |= M8XX_PCMCIA_POR_16BIT;

	if (mem->flags & MAP_ACTIVE)
		reg |= M8XX_PCMCIA_POR_VALID;

	out_be32(&w->or, reg);

	dprintk("Socket %u: Mapped memory window %u at %#8.8x, "
		"OR = %#8.8x.\n", lsock, mem->map, w->br, w->or);

	if (mem->flags & MAP_ACTIVE) {
		/* get the new base address */
		mem->static_start = PCMCIA_MEM_WIN_BASE +
		    (PCMCIA_MEM_WIN_SIZE * winnr)
		    + mem->card_start;
	}

	dprintk("SetMemMap(%d, %d, %#2.2x, %d ns, "
		"%#5.5lx, %#5.5x)\n", lsock, mem->map, mem->flags,
		mem->speed, mem->static_start, mem->card_start);

	/* copy the struct and modify the copy */

	old = &s->mem_win[mem->map];

	*old = *mem;
	old->flags &= (MAP_ATTRIB | MAP_WRPROT | MAP_16BIT | MAP_ACTIVE);

	return 0;
}

static int m8xx_sock_init(struct pcmcia_socket *sock)
{
	int i;
	pccard_io_map io = { 0, 0, 0, 0, 1 };
	pccard_mem_map mem = { 0, 0, 0, 0, 0, 0 };

	dprintk("sock_init(%d)\n", s);

	m8xx_set_socket(sock, &dead_socket);
	for (i = 0; i < PCMCIA_IO_WIN_NO; i++) {
		io.map = i;
		m8xx_set_io_map(sock, &io);
	}
	for (i = 0; i < PCMCIA_MEM_WIN_NO; i++) {
		mem.map = i;
		m8xx_set_mem_map(sock, &mem);
	}

	return 0;

}

static int m8xx_sock_suspend(struct pcmcia_socket *sock)
{
	return m8xx_set_socket(sock, &dead_socket);
}

static struct pccard_operations m8xx_services = {
	.init = m8xx_sock_init,
	.suspend = m8xx_sock_suspend,
	.get_status = m8xx_get_status,
	.set_socket = m8xx_set_socket,
	.set_io_map = m8xx_set_io_map,
	.set_mem_map = m8xx_set_mem_map,
};

static int __init m8xx_probe(struct of_device *ofdev,
			     const struct of_device_id *match)
{
	struct pcmcia_win *w;
	unsigned int i, m, hwirq;
	pcmconf8xx_t *pcmcia;
	int status;
	struct device_node *np = ofdev->node;

	pcmcia_info("%s\n", version);

	pcmcia = of_iomap(np, 0);
	if (pcmcia == NULL)
		return -EINVAL;

	pcmcia_schlvl = irq_of_parse_and_map(np, 0);
	hwirq = irq_map[pcmcia_schlvl].hwirq;
	if (pcmcia_schlvl < 0) {
		iounmap(pcmcia);
		return -EINVAL;
	}

	m8xx_pgcrx[0] = &pcmcia->pcmc_pgcra;
	m8xx_pgcrx[1] = &pcmcia->pcmc_pgcrb;

	pcmcia_info(PCMCIA_BOARD_MSG " using " PCMCIA_SLOT_MSG
		    " with IRQ %u  (%d). \n", pcmcia_schlvl, hwirq);

	/* Configure Status change interrupt */

	if (request_irq(pcmcia_schlvl, m8xx_interrupt, IRQF_SHARED,
			driver_name, socket)) {
		pcmcia_error("Cannot allocate IRQ %u for SCHLVL!\n",
			     pcmcia_schlvl);
		iounmap(pcmcia);
		return -1;
	}

	w = (void *)&pcmcia->pcmc_pbr0;

	out_be32(&pcmcia->pcmc_pscr, M8XX_PCMCIA_MASK(0) | M8XX_PCMCIA_MASK(1));
	clrbits32(&pcmcia->pcmc_per, M8XX_PCMCIA_MASK(0) | M8XX_PCMCIA_MASK(1));

	/* connect interrupt and disable CxOE */

	out_be32(M8XX_PGCRX(0),
		 M8XX_PGCRX_CXOE | (mk_int_int_mask(hwirq) << 16));
	out_be32(M8XX_PGCRX(1),
		 M8XX_PGCRX_CXOE | (mk_int_int_mask(hwirq) << 16));

	/* intialize the fixed memory windows */

	for (i = 0; i < PCMCIA_SOCKETS_NO; i++) {
		for (m = 0; m < PCMCIA_MEM_WIN_NO; m++) {
			out_be32(&w->br, PCMCIA_MEM_WIN_BASE +
				 (PCMCIA_MEM_WIN_SIZE
				  * (m + i * PCMCIA_MEM_WIN_NO)));

			out_be32(&w->or, 0);	/* set to not valid */

			w++;
		}
	}

	/* turn off voltage */
	voltage_set(0, 0, 0);
	voltage_set(1, 0, 0);

	/* Enable external hardware */
	hardware_enable(0);
	hardware_enable(1);

	for (i = 0; i < PCMCIA_SOCKETS_NO; i++) {
		socket[i].slot = i;
		socket[i].socket.owner = THIS_MODULE;
		socket[i].socket.features =
		    SS_CAP_PCCARD | SS_CAP_MEM_ALIGN | SS_CAP_STATIC_MAP;
		socket[i].socket.irq_mask = 0x000;
		socket[i].socket.map_size = 0x1000;
		socket[i].socket.io_offset = 0;
		socket[i].socket.pci_irq = pcmcia_schlvl;
		socket[i].socket.ops = &m8xx_services;
		socket[i].socket.resource_ops = &pccard_nonstatic_ops;
		socket[i].socket.cb_dev = NULL;
		socket[i].socket.dev.parent = &ofdev->dev;
		socket[i].pcmcia = pcmcia;
		socket[i].bus_freq = ppc_proc_freq;
		socket[i].hwirq = hwirq;

	}

	for (i = 0; i < PCMCIA_SOCKETS_NO; i++) {
		status = pcmcia_register_socket(&socket[i].socket);
		if (status < 0)
			pcmcia_error("Socket register failed\n");
	}

	return 0;
}

static int m8xx_remove(struct of_device *ofdev)
{
	u32 m, i;
	struct pcmcia_win *w;
	pcmconf8xx_t *pcmcia = socket[0].pcmcia;

	for (i = 0; i < PCMCIA_SOCKETS_NO; i++) {
		w = (void *)&pcmcia->pcmc_pbr0;

		out_be32(&pcmcia->pcmc_pscr, M8XX_PCMCIA_MASK(i));
		out_be32(&pcmcia->pcmc_per,
			 in_be32(&pcmcia->pcmc_per) & ~M8XX_PCMCIA_MASK(i));

		/* turn off interrupt and disable CxOE */
		out_be32(M8XX_PGCRX(i), M8XX_PGCRX_CXOE);

		/* turn off memory windows */
		for (m = 0; m < PCMCIA_MEM_WIN_NO; m++) {
			out_be32(&w->or, 0);	/* set to not valid */
			w++;
		}

		/* turn off voltage */
		voltage_set(i, 0, 0);

		/* disable external hardware */
		hardware_disable(i);
	}
	for (i = 0; i < PCMCIA_SOCKETS_NO; i++)
		pcmcia_unregister_socket(&socket[i].socket);
	iounmap(pcmcia);

	free_irq(pcmcia_schlvl, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int m8xx_suspend(struct platform_device *pdev, pm_message_t state)
{
	return pcmcia_socket_dev_suspend(&pdev->dev, state);
}

static int m8xx_resume(struct platform_device *pdev)
{
	return pcmcia_socket_dev_resume(&pdev->dev);
}
#else
#define m8xx_suspend NULL
#define m8xx_resume NULL
#endif

static struct of_device_id m8xx_pcmcia_match[] = {
	{
	 .type = "pcmcia",
	 .compatible = "fsl,pq-pcmcia",
	 },
	{},
};

MODULE_DEVICE_TABLE(of, m8xx_pcmcia_match);

static struct of_platform_driver m8xx_pcmcia_driver = {
	.name = driver_name,
	.match_table = m8xx_pcmcia_match,
	.probe = m8xx_probe,
	.remove = m8xx_remove,
	.suspend = m8xx_suspend,
	.resume = m8xx_resume,
};

static int __init m8xx_init(void)
{
	return of_register_platform_driver(&m8xx_pcmcia_driver);
}

static void __exit m8xx_exit(void)
{
	of_unregister_platform_driver(&m8xx_pcmcia_driver);
}

module_init(m8xx_init);
module_exit(m8xx_exit);
