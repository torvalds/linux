/*
 * isp1301_omap - ISP 1301 USB transceiver, talking to OMAP OTG controller
 *
 * Copyright (C) 2004 Texas Instruments
 * Copyright (C) 2004 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#undef	DEBUG
#undef	VERBOSE

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/usb_ch9.h>
#include <linux/usb_gadget.h>
#include <linux/usb.h>
#include <linux/usb_otg.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>

#include <asm/irq.h>
#include <asm/arch/usb.h>


#ifndef	DEBUG
#undef	VERBOSE
#endif


#define	DRIVER_VERSION	"24 August 2004"
#define	DRIVER_NAME	(isp1301_driver.name)

MODULE_DESCRIPTION("ISP1301 USB OTG Transceiver Driver");
MODULE_LICENSE("GPL");

struct isp1301 {
	struct otg_transceiver	otg;
	struct i2c_client	client;
	void			(*i2c_release)(struct device *dev);

	int			irq;

	u32			last_otg_ctrl;
	unsigned		working:1;

	struct timer_list	timer;

	/* use keventd context to change the state for us */
	struct work_struct	work;
	
	unsigned long		todo;
#		define WORK_UPDATE_ISP	0	/* update ISP from OTG */
#		define WORK_UPDATE_OTG	1	/* update OTG from ISP */
#		define WORK_HOST_RESUME	4	/* resume host */
#		define WORK_TIMER	6	/* timer fired */
#		define WORK_STOP	7	/* don't resubmit */
};


/* bits in OTG_CTRL_REG */

#define	OTG_XCEIV_OUTPUTS \
	(OTG_ASESSVLD|OTG_BSESSEND|OTG_BSESSVLD|OTG_VBUSVLD|OTG_ID)
#define	OTG_XCEIV_INPUTS \
	(OTG_PULLDOWN|OTG_PULLUP|OTG_DRV_VBUS|OTG_PD_VBUS|OTG_PU_VBUS|OTG_PU_ID)
#define	OTG_CTRL_BITS \
	(OTG_A_BUSREQ|OTG_A_SETB_HNPEN|OTG_B_BUSREQ|OTG_B_HNPEN|OTG_BUSDROP)
	/* and OTG_PULLUP is sometimes written */

#define	OTG_CTRL_MASK	(OTG_DRIVER_SEL| \
	OTG_XCEIV_OUTPUTS|OTG_XCEIV_INPUTS| \
	OTG_CTRL_BITS)


/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_MACH_OMAP_H2

/* board-specific PM hooks */

#include <asm/arch/gpio.h>
#include <asm/arch/mux.h>
#include <asm/mach-types.h>


#if	defined(CONFIG_TPS65010) || defined(CONFIG_TPS65010_MODULE)

#include <asm/arch/tps65010.h>

#else

static inline int tps65010_set_vbus_draw(unsigned mA)
{
	pr_debug("tps65010: draw %d mA (STUB)\n", mA);
	return 0;
}

#endif

static void enable_vbus_draw(struct isp1301 *isp, unsigned mA)
{
	int status = tps65010_set_vbus_draw(mA);
	if (status < 0)
		pr_debug("  VBUS %d mA error %d\n", mA, status);
}

static void enable_vbus_source(struct isp1301 *isp)
{
	/* this board won't supply more than 8mA vbus power.
	 * some boards can switch a 100ma "unit load" (or more).
	 */
}


/* products will deliver OTG messages with LEDs, GUI, etc */
static inline void notresponding(struct isp1301 *isp)
{
	printk(KERN_NOTICE "OTG device not responding.\n");
}


#endif

/*-------------------------------------------------------------------------*/

/* only two addresses possible */
#define	ISP_BASE		0x2c
static unsigned short normal_i2c[] = {
	ISP_BASE, ISP_BASE + 1,
	I2C_CLIENT_END };

I2C_CLIENT_INSMOD;

static struct i2c_driver isp1301_driver;

/* smbus apis are used for portability */

static inline u8
isp1301_get_u8(struct isp1301 *isp, u8 reg)
{
	return i2c_smbus_read_byte_data(&isp->client, reg + 0);
}

static inline int
isp1301_get_u16(struct isp1301 *isp, u8 reg)
{
	return i2c_smbus_read_word_data(&isp->client, reg);
}

static inline int
isp1301_set_bits(struct isp1301 *isp, u8 reg, u8 bits)
{
	return i2c_smbus_write_byte_data(&isp->client, reg + 0, bits);
}

static inline int
isp1301_clear_bits(struct isp1301 *isp, u8 reg, u8 bits)
{
	return i2c_smbus_write_byte_data(&isp->client, reg + 1, bits);
}

/*-------------------------------------------------------------------------*/

/* identification */
#define	ISP1301_VENDOR_ID		0x00	/* u16 read */
#define	ISP1301_PRODUCT_ID		0x02	/* u16 read */
#define	ISP1301_BCD_DEVICE		0x14	/* u16 read */

#define	I2C_VENDOR_ID_PHILIPS		0x04cc
#define	I2C_PRODUCT_ID_PHILIPS_1301	0x1301

/* operational registers */
#define	ISP1301_MODE_CONTROL_1		0x04	/* u8 read, set, +1 clear */
#	define	MC1_SPEED_REG		(1 << 0)
#	define	MC1_SUSPEND_REG		(1 << 1)
#	define	MC1_DAT_SE0		(1 << 2)
#	define	MC1_TRANSPARENT		(1 << 3)
#	define	MC1_BDIS_ACON_EN	(1 << 4)
#	define	MC1_OE_INT_EN		(1 << 5)
#	define	MC1_UART_EN		(1 << 6)
#	define	MC1_MASK		0x7f
#define	ISP1301_MODE_CONTROL_2		0x12	/* u8 read, set, +1 clear */
#	define	MC2_GLOBAL_PWR_DN	(1 << 0)
#	define	MC2_SPD_SUSP_CTRL	(1 << 1)
#	define	MC2_BI_DI		(1 << 2)
#	define	MC2_TRANSP_BDIR0	(1 << 3)
#	define	MC2_TRANSP_BDIR1	(1 << 4)
#	define	MC2_AUDIO_EN		(1 << 5)
#	define	MC2_PSW_EN		(1 << 6)
#	define	MC2_EN2V7		(1 << 7)
#define	ISP1301_OTG_CONTROL_1		0x06	/* u8 read, set, +1 clear */
#	define	OTG1_DP_PULLUP		(1 << 0)
#	define	OTG1_DM_PULLUP		(1 << 1)
#	define	OTG1_DP_PULLDOWN	(1 << 2)
#	define	OTG1_DM_PULLDOWN	(1 << 3)
#	define	OTG1_ID_PULLDOWN	(1 << 4)
#	define	OTG1_VBUS_DRV		(1 << 5)
#	define	OTG1_VBUS_DISCHRG	(1 << 6)
#	define	OTG1_VBUS_CHRG		(1 << 7)
#define	ISP1301_OTG_STATUS		0x10	/* u8 readonly */
#	define	OTG_B_SESS_END		(1 << 6)
#	define	OTG_B_SESS_VLD		(1 << 7)

#define	ISP1301_INTERRUPT_SOURCE	0x08	/* u8 read */
#define	ISP1301_INTERRUPT_LATCH		0x0A	/* u8 read, set, +1 clear */

#define	ISP1301_INTERRUPT_FALLING	0x0C	/* u8 read, set, +1 clear */
#define	ISP1301_INTERRUPT_RISING	0x0E	/* u8 read, set, +1 clear */

/* same bitfields in all interrupt registers */
#	define	INTR_VBUS_VLD		(1 << 0)
#	define	INTR_SESS_VLD		(1 << 1)
#	define	INTR_DP_HI		(1 << 2)
#	define	INTR_ID_GND		(1 << 3)
#	define	INTR_DM_HI		(1 << 4)
#	define	INTR_ID_FLOAT		(1 << 5)
#	define	INTR_BDIS_ACON		(1 << 6)
#	define	INTR_CR_INT		(1 << 7)

/*-------------------------------------------------------------------------*/

static const char *state_string(enum usb_otg_state state)
{
	switch (state) {
	case OTG_STATE_A_IDLE:		return "a_idle";
	case OTG_STATE_A_WAIT_VRISE:	return "a_wait_vrise";
	case OTG_STATE_A_WAIT_BCON:	return "a_wait_bcon";
	case OTG_STATE_A_HOST:		return "a_host";
	case OTG_STATE_A_SUSPEND:	return "a_suspend";
	case OTG_STATE_A_PERIPHERAL:	return "a_peripheral";
	case OTG_STATE_A_WAIT_VFALL:	return "a_wait_vfall";
	case OTG_STATE_A_VBUS_ERR:	return "a_vbus_err";
	case OTG_STATE_B_IDLE:		return "b_idle";
	case OTG_STATE_B_SRP_INIT:	return "b_srp_init";
	case OTG_STATE_B_PERIPHERAL:	return "b_peripheral";
	case OTG_STATE_B_WAIT_ACON:	return "b_wait_acon";
	case OTG_STATE_B_HOST:		return "b_host";
	default:			return "UNDEFINED";
	}
}

static inline const char *state_name(struct isp1301 *isp)
{
	return state_string(isp->otg.state);
}

#ifdef	VERBOSE
#define	dev_vdbg			dev_dbg
#else
#define	dev_vdbg(dev, fmt, arg...)	do{}while(0)
#endif

/*-------------------------------------------------------------------------*/

/* NOTE:  some of this ISP1301 setup is specific to H2 boards;
 * not everything is guarded by board-specific checks, or even using
 * omap_usb_config data to deduce MC1_DAT_SE0 and MC2_BI_DI.
 *
 * ALSO:  this currently doesn't use ISP1301 low-power modes
 * while OTG is running.
 */

static void power_down(struct isp1301 *isp)
{
	isp->otg.state = OTG_STATE_UNDEFINED;

	// isp1301_set_bits(isp, ISP1301_MODE_CONTROL_2, MC2_GLOBAL_PWR_DN);
	isp1301_set_bits(isp, ISP1301_MODE_CONTROL_1, MC1_SUSPEND_REG);

	isp1301_clear_bits(isp, ISP1301_OTG_CONTROL_1, OTG1_ID_PULLDOWN);
	isp1301_clear_bits(isp, ISP1301_MODE_CONTROL_1, MC1_DAT_SE0);
}

static void power_up(struct isp1301 *isp)
{
	// isp1301_clear_bits(isp, ISP1301_MODE_CONTROL_2, MC2_GLOBAL_PWR_DN);
	isp1301_clear_bits(isp, ISP1301_MODE_CONTROL_1, MC1_SUSPEND_REG);
	
	/* do this only when cpu is driving transceiver,
	 * so host won't see a low speed device...
	 */
	isp1301_set_bits(isp, ISP1301_MODE_CONTROL_1, MC1_DAT_SE0);
}

#define	NO_HOST_SUSPEND

static int host_suspend(struct isp1301 *isp)
{
#ifdef	NO_HOST_SUSPEND
	return 0;
#else
	struct device	*dev;

	if (!isp->otg.host)
		return -ENODEV;

	/* Currently ASSUMES only the OTG port matters;
	 * other ports could be active...
	 */
	dev = isp->otg.host->controller;
	return dev->driver->suspend(dev, 3, 0);
#endif
}

static int host_resume(struct isp1301 *isp)
{
#ifdef	NO_HOST_SUSPEND
	return 0;
#else
	struct device	*dev;

	if (!isp->otg.host)
		return -ENODEV;

	dev = isp->otg.host->controller;
	return dev->driver->resume(dev, 0);
#endif
}

static int gadget_suspend(struct isp1301 *isp)
{
	isp->otg.gadget->b_hnp_enable = 0;
	isp->otg.gadget->a_hnp_support = 0;
	isp->otg.gadget->a_alt_hnp_support = 0;
	return usb_gadget_vbus_disconnect(isp->otg.gadget);
}

/*-------------------------------------------------------------------------*/

#define	TIMER_MINUTES	10
#define	TIMER_JIFFIES	(TIMER_MINUTES * 60 * HZ)

/* Almost all our I2C messaging comes from a work queue's task context.
 * NOTE: guaranteeing certain response times might mean we shouldn't
 * share keventd's work queue; a realtime task might be safest.
 */
void
isp1301_defer_work(struct isp1301 *isp, int work)
{
	int status;

	if (isp && !test_and_set_bit(work, &isp->todo)) {
		(void) get_device(&isp->client.dev);
		status = schedule_work(&isp->work);
		if (!status && !isp->working)
			dev_vdbg(&isp->client.dev,
				"work item %d may be lost\n", work);
	}
}

/* called from irq handlers */
static void a_idle(struct isp1301 *isp, const char *tag)
{
	if (isp->otg.state == OTG_STATE_A_IDLE)
		return;

	isp->otg.default_a = 1;
	if (isp->otg.host) {
		isp->otg.host->is_b_host = 0;
		host_suspend(isp);
	}
	if (isp->otg.gadget) {
		isp->otg.gadget->is_a_peripheral = 1;
		gadget_suspend(isp);
	}
	isp->otg.state = OTG_STATE_A_IDLE;
	isp->last_otg_ctrl = OTG_CTRL_REG = OTG_CTRL_REG & OTG_XCEIV_OUTPUTS;
	pr_debug("  --> %s/%s\n", state_name(isp), tag);
}

/* called from irq handlers */
static void b_idle(struct isp1301 *isp, const char *tag)
{
	if (isp->otg.state == OTG_STATE_B_IDLE)
		return;

	isp->otg.default_a = 0;
	if (isp->otg.host) {
		isp->otg.host->is_b_host = 1;
		host_suspend(isp);
	}
	if (isp->otg.gadget) {
		isp->otg.gadget->is_a_peripheral = 0;
		gadget_suspend(isp);
	}
	isp->otg.state = OTG_STATE_B_IDLE;
	isp->last_otg_ctrl = OTG_CTRL_REG = OTG_CTRL_REG & OTG_XCEIV_OUTPUTS;
	pr_debug("  --> %s/%s\n", state_name(isp), tag);
}

static void
dump_regs(struct isp1301 *isp, const char *label)
{
#ifdef	DEBUG
	u8	ctrl = isp1301_get_u8(isp, ISP1301_OTG_CONTROL_1);
	u8	status = isp1301_get_u8(isp, ISP1301_OTG_STATUS);
	u8	src = isp1301_get_u8(isp, ISP1301_INTERRUPT_SOURCE);

	pr_debug("otg: %06x, %s %s, otg/%02x stat/%02x.%02x\n",
		OTG_CTRL_REG, label, state_name(isp),
		ctrl, status, src);
	/* mode control and irq enables don't change much */
#endif
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_USB_OTG

/*
 * The OMAP OTG controller handles most of the OTG state transitions.
 *
 * We translate isp1301 outputs (mostly voltage comparator status) into
 * OTG inputs; OTG outputs (mostly pullup/pulldown controls) and HNP state
 * flags into isp1301 inputs ... and infer state transitions.
 */

#ifdef	VERBOSE

static void check_state(struct isp1301 *isp, const char *tag)
{
	enum usb_otg_state	state = OTG_STATE_UNDEFINED;
	u8			fsm = OTG_TEST_REG & 0x0ff;
	unsigned		extra = 0;

	switch (fsm) {

	/* default-b */
	case 0x0:
		state = OTG_STATE_B_IDLE;
		break;
	case 0x3:
	case 0x7:
		extra = 1;
	case 0x1:
		state = OTG_STATE_B_PERIPHERAL;
		break;
	case 0x11:
		state = OTG_STATE_B_SRP_INIT;
		break;

	/* extra dual-role default-b states */
	case 0x12:
	case 0x13:
	case 0x16:
		extra = 1;
	case 0x17:
		state = OTG_STATE_B_WAIT_ACON;
		break;
	case 0x34:
		state = OTG_STATE_B_HOST;
		break;

	/* default-a */
	case 0x36:
		state = OTG_STATE_A_IDLE;
		break;
	case 0x3c:
		state = OTG_STATE_A_WAIT_VFALL;
		break;
	case 0x7d:
		state = OTG_STATE_A_VBUS_ERR;
		break;
	case 0x9e:
	case 0x9f:
		extra = 1;
	case 0x89:
		state = OTG_STATE_A_PERIPHERAL;
		break;
	case 0xb7:
		state = OTG_STATE_A_WAIT_VRISE;
		break;
	case 0xb8:
		state = OTG_STATE_A_WAIT_BCON;
		break;
	case 0xb9:
		state = OTG_STATE_A_HOST;
		break;
	case 0xba:
		state = OTG_STATE_A_SUSPEND;
		break;
	default:
		break;
	}
	if (isp->otg.state == state && !extra)
		return;
	pr_debug("otg: %s FSM %s/%02x, %s, %06x\n", tag,
		state_string(state), fsm, state_name(isp), OTG_CTRL_REG);
}

#else

static inline void check_state(struct isp1301 *isp, const char *tag) { }

#endif

/* outputs from ISP1301_INTERRUPT_SOURCE */
static void update_otg1(struct isp1301 *isp, u8 int_src)
{
	u32	otg_ctrl;

	otg_ctrl = OTG_CTRL_REG
			& OTG_CTRL_MASK
			& ~OTG_XCEIV_INPUTS
			& ~(OTG_ID|OTG_ASESSVLD|OTG_VBUSVLD);
	if (int_src & INTR_SESS_VLD)
		otg_ctrl |= OTG_ASESSVLD;
	else if (isp->otg.state == OTG_STATE_A_WAIT_VFALL) {
		a_idle(isp, "vfall");
		otg_ctrl &= ~OTG_CTRL_BITS;
	}
	if (int_src & INTR_VBUS_VLD)
		otg_ctrl |= OTG_VBUSVLD;
	if (int_src & INTR_ID_GND) {		/* default-A */
		if (isp->otg.state == OTG_STATE_B_IDLE
				|| isp->otg.state == OTG_STATE_UNDEFINED) {
			a_idle(isp, "init");
			return;
		}
	} else {				/* default-B */
		otg_ctrl |= OTG_ID;
		if (isp->otg.state == OTG_STATE_A_IDLE
				|| isp->otg.state == OTG_STATE_UNDEFINED) {
			b_idle(isp, "init");
			return;
		}
	}
	OTG_CTRL_REG = otg_ctrl;
}

/* outputs from ISP1301_OTG_STATUS */
static void update_otg2(struct isp1301 *isp, u8 otg_status)
{
	u32	otg_ctrl;

	otg_ctrl = OTG_CTRL_REG
			& OTG_CTRL_MASK
			& ~OTG_XCEIV_INPUTS
			& ~(OTG_BSESSVLD|OTG_BSESSEND);
	if (otg_status & OTG_B_SESS_VLD)
		otg_ctrl |= OTG_BSESSVLD;
	else if (otg_status & OTG_B_SESS_END)
		otg_ctrl |= OTG_BSESSEND;
	OTG_CTRL_REG = otg_ctrl;
}

/* inputs going to ISP1301 */
static void otg_update_isp(struct isp1301 *isp)
{
	u32	otg_ctrl, otg_change;
	u8	set = OTG1_DM_PULLDOWN, clr = OTG1_DM_PULLUP;

	otg_ctrl = OTG_CTRL_REG;
	otg_change = otg_ctrl ^ isp->last_otg_ctrl;
	isp->last_otg_ctrl = otg_ctrl;
	otg_ctrl = otg_ctrl & OTG_XCEIV_INPUTS;

	switch (isp->otg.state) {
	case OTG_STATE_B_IDLE:
	case OTG_STATE_B_PERIPHERAL:
	case OTG_STATE_B_SRP_INIT:
		if (!(otg_ctrl & OTG_PULLUP)) {
			// if (otg_ctrl & OTG_B_HNPEN) {
			if (isp->otg.gadget->b_hnp_enable) {
				isp->otg.state = OTG_STATE_B_WAIT_ACON;
				pr_debug("  --> b_wait_acon\n");
			}
			goto pulldown;
		}
pullup:
		set |= OTG1_DP_PULLUP;
		clr |= OTG1_DP_PULLDOWN;
		break;
	case OTG_STATE_A_SUSPEND:
	case OTG_STATE_A_PERIPHERAL:
		if (otg_ctrl & OTG_PULLUP)
			goto pullup;
		/* FALLTHROUGH */
	// case OTG_STATE_B_WAIT_ACON:
	default:
pulldown:
		set |= OTG1_DP_PULLDOWN;
		clr |= OTG1_DP_PULLUP;
		break;
	}

#	define toggle(OTG,ISP) do { \
		if (otg_ctrl & OTG) set |= ISP; \
		else clr |= ISP; \
		} while (0)

	if (!(isp->otg.host))
		otg_ctrl &= ~OTG_DRV_VBUS;

	switch (isp->otg.state) {
	case OTG_STATE_A_SUSPEND:
		if (otg_ctrl & OTG_DRV_VBUS) {
			set |= OTG1_VBUS_DRV;
			break;
		}
		/* HNP failed for some reason (A_AIDL_BDIS timeout) */
		notresponding(isp);

		/* FALLTHROUGH */
	case OTG_STATE_A_VBUS_ERR:
		isp->otg.state = OTG_STATE_A_WAIT_VFALL;
		pr_debug("  --> a_wait_vfall\n");
		/* FALLTHROUGH */
	case OTG_STATE_A_WAIT_VFALL:
		/* FIXME usbcore thinks port power is still on ... */
		clr |= OTG1_VBUS_DRV;
		break;
	case OTG_STATE_A_IDLE:
		if (otg_ctrl & OTG_DRV_VBUS) {
			isp->otg.state = OTG_STATE_A_WAIT_VRISE;
			pr_debug("  --> a_wait_vrise\n");
		}
		/* FALLTHROUGH */
	default:
		toggle(OTG_DRV_VBUS, OTG1_VBUS_DRV);
	}

	toggle(OTG_PU_VBUS, OTG1_VBUS_CHRG);
	toggle(OTG_PD_VBUS, OTG1_VBUS_DISCHRG);

#	undef toggle

	isp1301_set_bits(isp, ISP1301_OTG_CONTROL_1, set);
	isp1301_clear_bits(isp, ISP1301_OTG_CONTROL_1, clr);

	/* HNP switch to host or peripheral; and SRP */
	if (otg_change & OTG_PULLUP) {
		switch (isp->otg.state) {
		case OTG_STATE_B_IDLE:
			if (clr & OTG1_DP_PULLUP)
				break;
			isp->otg.state = OTG_STATE_B_PERIPHERAL;
			pr_debug("  --> b_peripheral\n");
			break;
		case OTG_STATE_A_SUSPEND:
			if (clr & OTG1_DP_PULLUP)
				break;
			isp->otg.state = OTG_STATE_A_PERIPHERAL;
			pr_debug("  --> a_peripheral\n");
			break;
		default:
			break;
		}
		OTG_CTRL_REG |= OTG_PULLUP;
	}

	check_state(isp, __FUNCTION__);
	dump_regs(isp, "otg->isp1301");
}

static irqreturn_t omap_otg_irq(int irq, void *_isp, struct pt_regs *regs)
{
	u16		otg_irq = OTG_IRQ_SRC_REG;
	u32		otg_ctrl;
	int		ret = IRQ_NONE;
	struct isp1301	*isp = _isp;

	/* update ISP1301 transciever from OTG controller */
	if (otg_irq & OPRT_CHG) {
		OTG_IRQ_SRC_REG = OPRT_CHG;
		isp1301_defer_work(isp, WORK_UPDATE_ISP);
		ret = IRQ_HANDLED;

	/* SRP to become b_peripheral failed */
	} else if (otg_irq & B_SRP_TMROUT) {
		pr_debug("otg: B_SRP_TIMEOUT, %06x\n", OTG_CTRL_REG);
		notresponding(isp);

		/* gadget drivers that care should monitor all kinds of
		 * remote wakeup (SRP, normal) using their own timer
		 * to give "check cable and A-device" messages.
		 */
		if (isp->otg.state == OTG_STATE_B_SRP_INIT)
			b_idle(isp, "srp_timeout");

		OTG_IRQ_SRC_REG = B_SRP_TMROUT;
		ret = IRQ_HANDLED;

	/* HNP to become b_host failed */
	} else if (otg_irq & B_HNP_FAIL) {
		pr_debug("otg: %s B_HNP_FAIL, %06x\n",
				state_name(isp), OTG_CTRL_REG);
		notresponding(isp);

		otg_ctrl = OTG_CTRL_REG;
		otg_ctrl |= OTG_BUSDROP;
		otg_ctrl &= OTG_CTRL_MASK & ~OTG_XCEIV_INPUTS;
		OTG_CTRL_REG = otg_ctrl;

		/* subset of b_peripheral()... */
		isp->otg.state = OTG_STATE_B_PERIPHERAL;
		pr_debug("  --> b_peripheral\n");

		OTG_IRQ_SRC_REG = B_HNP_FAIL;
		ret = IRQ_HANDLED;

	/* detect SRP from B-device ... */
	} else if (otg_irq & A_SRP_DETECT) {
		pr_debug("otg: %s SRP_DETECT, %06x\n",
				state_name(isp), OTG_CTRL_REG);

		isp1301_defer_work(isp, WORK_UPDATE_OTG);
		switch (isp->otg.state) {
		case OTG_STATE_A_IDLE:
			if (!isp->otg.host)
				break;
			isp1301_defer_work(isp, WORK_HOST_RESUME);
			otg_ctrl = OTG_CTRL_REG;
			otg_ctrl |= OTG_A_BUSREQ;
			otg_ctrl &= ~(OTG_BUSDROP|OTG_B_BUSREQ)
					& ~OTG_XCEIV_INPUTS
					& OTG_CTRL_MASK;
			OTG_CTRL_REG = otg_ctrl;
			break;
		default:
			break;
		}

		OTG_IRQ_SRC_REG = A_SRP_DETECT;
		ret = IRQ_HANDLED;

	/* timer expired:  T(a_wait_bcon) and maybe T(a_wait_vrise)
	 * we don't track them separately
	 */
	} else if (otg_irq & A_REQ_TMROUT) {
		otg_ctrl = OTG_CTRL_REG;
		pr_info("otg: BCON_TMOUT from %s, %06x\n",
				state_name(isp), otg_ctrl);
		notresponding(isp);

		otg_ctrl |= OTG_BUSDROP;
		otg_ctrl &= ~OTG_A_BUSREQ & OTG_CTRL_MASK & ~OTG_XCEIV_INPUTS;
		OTG_CTRL_REG = otg_ctrl;
		isp->otg.state = OTG_STATE_A_WAIT_VFALL;

		OTG_IRQ_SRC_REG = A_REQ_TMROUT;
		ret = IRQ_HANDLED;

	/* A-supplied voltage fell too low; overcurrent */
	} else if (otg_irq & A_VBUS_ERR) {
		otg_ctrl = OTG_CTRL_REG;
		printk(KERN_ERR "otg: %s, VBUS_ERR %04x ctrl %06x\n",
			state_name(isp), otg_irq, otg_ctrl);

		otg_ctrl |= OTG_BUSDROP;
		otg_ctrl &= ~OTG_A_BUSREQ & OTG_CTRL_MASK & ~OTG_XCEIV_INPUTS;
		OTG_CTRL_REG = otg_ctrl;
		isp->otg.state = OTG_STATE_A_VBUS_ERR;

		OTG_IRQ_SRC_REG = A_VBUS_ERR;
		ret = IRQ_HANDLED;

	/* switch driver; the transciever code activates it,
	 * ungating the udc clock or resuming OHCI.
	 */
	} else if (otg_irq & DRIVER_SWITCH) {
		int	kick = 0;

		otg_ctrl = OTG_CTRL_REG;
		printk(KERN_NOTICE "otg: %s, SWITCH to %s, ctrl %06x\n",
				state_name(isp),
				(otg_ctrl & OTG_DRIVER_SEL)
					? "gadget" : "host",
				otg_ctrl);
		isp1301_defer_work(isp, WORK_UPDATE_ISP);

		/* role is peripheral */
		if (otg_ctrl & OTG_DRIVER_SEL) {
			switch (isp->otg.state) {
			case OTG_STATE_A_IDLE:
				b_idle(isp, __FUNCTION__);
				break;
			default:
				break;
			}
			isp1301_defer_work(isp, WORK_UPDATE_ISP);

		/* role is host */
		} else {
			if (!(otg_ctrl & OTG_ID)) {
		 		otg_ctrl &= OTG_CTRL_MASK & ~OTG_XCEIV_INPUTS;
				OTG_CTRL_REG = otg_ctrl | OTG_A_BUSREQ;
			}

			if (isp->otg.host) {
				switch (isp->otg.state) {
				case OTG_STATE_B_WAIT_ACON:
					isp->otg.state = OTG_STATE_B_HOST;
					pr_debug("  --> b_host\n");
					kick = 1;
					break;
				case OTG_STATE_A_WAIT_BCON:
					isp->otg.state = OTG_STATE_A_HOST;
					pr_debug("  --> a_host\n");
					break;
				case OTG_STATE_A_PERIPHERAL:
					isp->otg.state = OTG_STATE_A_WAIT_BCON;
					pr_debug("  --> a_wait_bcon\n");
					break;
				default:
					break;
				}
				isp1301_defer_work(isp, WORK_HOST_RESUME);
			}
		}

		OTG_IRQ_SRC_REG = DRIVER_SWITCH;
		ret = IRQ_HANDLED;

		if (kick)
			usb_bus_start_enum(isp->otg.host,
						isp->otg.host->otg_port);
	}

	check_state(isp, __FUNCTION__);
	return ret;
}

static struct platform_device *otg_dev;

static int otg_init(struct isp1301 *isp)
{
	if (!otg_dev)
		return -ENODEV;

	dump_regs(isp, __FUNCTION__);
	/* some of these values are board-specific... */
	OTG_SYSCON_2_REG |= OTG_EN
		/* for B-device: */
		| SRP_GPDATA		/* 9msec Bdev D+ pulse */
		| SRP_GPDVBUS		/* discharge after VBUS pulse */
		// | (3 << 24)		/* 2msec VBUS pulse */
		/* for A-device: */
		| (0 << 20)		/* 200ms nominal A_WAIT_VRISE timer */
		| SRP_DPW		/* detect 167+ns SRP pulses */
		| SRP_DATA | SRP_VBUS	/* accept both kinds of SRP pulse */
		;

	update_otg1(isp, isp1301_get_u8(isp, ISP1301_INTERRUPT_SOURCE));
	update_otg2(isp, isp1301_get_u8(isp, ISP1301_OTG_STATUS));

	check_state(isp, __FUNCTION__);
	pr_debug("otg: %s, %s %06x\n",
			state_name(isp), __FUNCTION__, OTG_CTRL_REG);

	OTG_IRQ_EN_REG = DRIVER_SWITCH | OPRT_CHG
			| B_SRP_TMROUT | B_HNP_FAIL
			| A_VBUS_ERR | A_SRP_DETECT | A_REQ_TMROUT;
	OTG_SYSCON_2_REG |= OTG_EN;

	return 0;
}

static int otg_probe(struct device *dev)
{
	// struct omap_usb_config *config = dev->platform_data;

	otg_dev = to_platform_device(dev);
	return 0;
}

static int otg_remove(struct device *dev)
{
	otg_dev = 0;
	return 0;
}

struct device_driver omap_otg_driver = {
	.owner		= THIS_MODULE,
	.name		= "omap_otg",
	.bus		= &platform_bus_type,
	.probe		= otg_probe,
	.remove		= otg_remove,	
};

static int otg_bind(struct isp1301 *isp)
{
	int	status;

	if (otg_dev)
		return -EBUSY;

	status = driver_register(&omap_otg_driver);
	if (status < 0)
		return status;

	if (otg_dev)
		status = request_irq(otg_dev->resource[1].start, omap_otg_irq,
				SA_INTERRUPT, DRIVER_NAME, isp);
	else
		status = -ENODEV;

	if (status < 0)
		driver_unregister(&omap_otg_driver);
	return status;
}

static void otg_unbind(struct isp1301 *isp)
{
	if (!otg_dev)
		return;
	free_irq(otg_dev->resource[1].start, isp);
}

#else

/* OTG controller isn't clocked */

#endif	/* CONFIG_USB_OTG */

/*-------------------------------------------------------------------------*/

static void b_peripheral(struct isp1301 *isp)
{
	OTG_CTRL_REG = OTG_CTRL_REG & OTG_XCEIV_OUTPUTS;
	usb_gadget_vbus_connect(isp->otg.gadget);

#ifdef	CONFIG_USB_OTG
	enable_vbus_draw(isp, 8);
	otg_update_isp(isp);
#else
	enable_vbus_draw(isp, 100);
	/* UDC driver just set OTG_BSESSVLD */
	isp1301_set_bits(isp, ISP1301_OTG_CONTROL_1, OTG1_DP_PULLUP);
	isp1301_clear_bits(isp, ISP1301_OTG_CONTROL_1, OTG1_DP_PULLDOWN);
	isp->otg.state = OTG_STATE_B_PERIPHERAL;
	pr_debug("  --> b_peripheral\n");
	dump_regs(isp, "2periph");
#endif
}

static void isp_update_otg(struct isp1301 *isp, u8 stat)
{
	u8			isp_stat, isp_bstat;
	enum usb_otg_state	state = isp->otg.state;

	if (stat & INTR_BDIS_ACON)
		pr_debug("OTG:  BDIS_ACON, %s\n", state_name(isp));

	/* start certain state transitions right away */
	isp_stat = isp1301_get_u8(isp, ISP1301_INTERRUPT_SOURCE);
	if (isp_stat & INTR_ID_GND) {
		if (isp->otg.default_a) {
			switch (state) {
			case OTG_STATE_B_IDLE:
				a_idle(isp, "idle");
				/* FALLTHROUGH */
			case OTG_STATE_A_IDLE:
				enable_vbus_source(isp);
				/* FALLTHROUGH */
			case OTG_STATE_A_WAIT_VRISE:
				/* we skip over OTG_STATE_A_WAIT_BCON, since
				 * the HC will transition to A_HOST (or
				 * A_SUSPEND!) without our noticing except
				 * when HNP is used.
				 */
				if (isp_stat & INTR_VBUS_VLD)
					isp->otg.state = OTG_STATE_A_HOST;
				break;
			case OTG_STATE_A_WAIT_VFALL:
				if (!(isp_stat & INTR_SESS_VLD))
					a_idle(isp, "vfell");
				break;
			default:
				if (!(isp_stat & INTR_VBUS_VLD))
					isp->otg.state = OTG_STATE_A_VBUS_ERR;
				break;
			}
			isp_bstat = isp1301_get_u8(isp, ISP1301_OTG_STATUS);
		} else {
			switch (state) {
			case OTG_STATE_B_PERIPHERAL:
			case OTG_STATE_B_HOST:
			case OTG_STATE_B_WAIT_ACON:
				usb_gadget_vbus_disconnect(isp->otg.gadget);
				break;
			default:
				break;
			}
			if (state != OTG_STATE_A_IDLE)
				a_idle(isp, "id");
			if (isp->otg.host && state == OTG_STATE_A_IDLE)
				isp1301_defer_work(isp, WORK_HOST_RESUME);
			isp_bstat = 0;
		}
	} else {
		/* if user unplugged mini-A end of cable,
		 * don't bypass A_WAIT_VFALL.
		 */
		if (isp->otg.default_a) {
			switch (state) {
			default:
				isp->otg.state = OTG_STATE_A_WAIT_VFALL;
				break;
			case OTG_STATE_A_WAIT_VFALL:
				state = OTG_STATE_A_IDLE;
				/* khubd may take a while to notice and
				 * handle this disconnect, so don't go
				 * to B_IDLE quite yet.
				 */
				break;
			case OTG_STATE_A_IDLE:
				host_suspend(isp);
				isp1301_clear_bits(isp, ISP1301_MODE_CONTROL_1,
						MC1_BDIS_ACON_EN);
				isp->otg.state = OTG_STATE_B_IDLE;
				OTG_CTRL_REG &= OTG_CTRL_REG & OTG_CTRL_MASK
						& ~OTG_CTRL_BITS;
				break;
			case OTG_STATE_B_IDLE:
				break;
			}
		}
		isp_bstat = isp1301_get_u8(isp, ISP1301_OTG_STATUS);

		switch (isp->otg.state) {
		case OTG_STATE_B_PERIPHERAL:
		case OTG_STATE_B_WAIT_ACON:
		case OTG_STATE_B_HOST:
			if (likely(isp_bstat & OTG_B_SESS_VLD))
				break;
			enable_vbus_draw(isp, 0);
#ifndef	CONFIG_USB_OTG
			/* UDC driver will clear OTG_BSESSVLD */
			isp1301_set_bits(isp, ISP1301_OTG_CONTROL_1,
						OTG1_DP_PULLDOWN);
			isp1301_clear_bits(isp, ISP1301_OTG_CONTROL_1,
						OTG1_DP_PULLUP);
			dump_regs(isp, __FUNCTION__);
#endif
			/* FALLTHROUGH */
		case OTG_STATE_B_SRP_INIT:
			b_idle(isp, __FUNCTION__);
			OTG_CTRL_REG &= OTG_CTRL_REG & OTG_XCEIV_OUTPUTS;
			/* FALLTHROUGH */
		case OTG_STATE_B_IDLE:
			if (isp->otg.gadget && (isp_bstat & OTG_B_SESS_VLD)) {
#ifdef	CONFIG_USB_OTG
				update_otg1(isp, isp_stat);
				update_otg2(isp, isp_bstat);
#endif
				b_peripheral(isp);
			} else if (!(isp_stat & (INTR_VBUS_VLD|INTR_SESS_VLD)))
				isp_bstat |= OTG_B_SESS_END;
			break;
		case OTG_STATE_A_WAIT_VFALL:
			break;
		default:
			pr_debug("otg: unsupported b-device %s\n",
				state_name(isp));
			break;
		}
	}

	if (state != isp->otg.state)
		pr_debug("  isp, %s -> %s\n",
				state_string(state), state_name(isp));

#ifdef	CONFIG_USB_OTG
	/* update the OTG controller state to match the isp1301; may
	 * trigger OPRT_CHG irqs for changes going to the isp1301.
	 */
	update_otg1(isp, isp_stat);
	update_otg2(isp, isp_bstat);
	check_state(isp, __FUNCTION__);
#endif

	dump_regs(isp, "isp1301->otg");
}

/*-------------------------------------------------------------------------*/

static u8 isp1301_clear_latch(struct isp1301 *isp)
{
	u8 latch = isp1301_get_u8(isp, ISP1301_INTERRUPT_LATCH);
	isp1301_clear_bits(isp, ISP1301_INTERRUPT_LATCH, latch);
	return latch;
}

static void
isp1301_work(void *data)
{
	struct isp1301	*isp = data;
	int		stop;

	/* implicit lock:  we're the only task using this device */
	isp->working = 1;
	do {
		stop = test_bit(WORK_STOP, &isp->todo);

#ifdef	CONFIG_USB_OTG
		/* transfer state from otg engine to isp1301 */
		if (test_and_clear_bit(WORK_UPDATE_ISP, &isp->todo)) {
			otg_update_isp(isp);
			put_device(&isp->client.dev);
		}
#endif
		/* transfer state from isp1301 to otg engine */
		if (test_and_clear_bit(WORK_UPDATE_OTG, &isp->todo)) {
			u8		stat = isp1301_clear_latch(isp);

			isp_update_otg(isp, stat);
			put_device(&isp->client.dev);
		}

		if (test_and_clear_bit(WORK_HOST_RESUME, &isp->todo)) {
			u32	otg_ctrl;

			/*
			 * skip A_WAIT_VRISE; hc transitions invisibly
			 * skip A_WAIT_BCON; same.
			 */
			switch (isp->otg.state) {
			case OTG_STATE_A_WAIT_BCON:
			case OTG_STATE_A_WAIT_VRISE:
				isp->otg.state = OTG_STATE_A_HOST;
				pr_debug("  --> a_host\n");
				otg_ctrl = OTG_CTRL_REG;
				otg_ctrl |= OTG_A_BUSREQ;
				otg_ctrl &= ~(OTG_BUSDROP|OTG_B_BUSREQ)
						& OTG_CTRL_MASK;
				OTG_CTRL_REG = otg_ctrl;
				break;
			case OTG_STATE_B_WAIT_ACON:
				isp->otg.state = OTG_STATE_B_HOST;
				pr_debug("  --> b_host (acon)\n");
				break;
			case OTG_STATE_B_HOST:
			case OTG_STATE_B_IDLE:
			case OTG_STATE_A_IDLE:
				break;
			default:
				pr_debug("  host resume in %s\n",
						state_name(isp));
			}
			host_resume(isp);
			// mdelay(10);
			put_device(&isp->client.dev);
		}

		if (test_and_clear_bit(WORK_TIMER, &isp->todo)) {
#ifdef	VERBOSE
			dump_regs(isp, "timer");
			if (!stop)
				mod_timer(&isp->timer, jiffies + TIMER_JIFFIES);
#endif
			put_device(&isp->client.dev);
		}

		if (isp->todo)
			dev_vdbg(&isp->client.dev,
				"work done, todo = 0x%lx\n",
				isp->todo);
		if (stop) {
			dev_dbg(&isp->client.dev, "stop\n");
			break;
		}
	} while (isp->todo);
	isp->working = 0;
}

static irqreturn_t isp1301_irq(int irq, void *isp, struct pt_regs *regs)
{
	isp1301_defer_work(isp, WORK_UPDATE_OTG);
	return IRQ_HANDLED;
}

static void isp1301_timer(unsigned long _isp)
{
	isp1301_defer_work((void *)_isp, WORK_TIMER);
}

/*-------------------------------------------------------------------------*/

static void isp1301_release(struct device *dev)
{
	struct isp1301	*isp;

	isp = container_of(dev, struct isp1301, client.dev);

	/* ugly -- i2c hijacks our memory hook to wait_for_completion() */
	if (isp->i2c_release)
		isp->i2c_release(dev);
	kfree (isp);
}

static struct isp1301 *the_transceiver;

static int isp1301_detach_client(struct i2c_client *i2c)
{
	struct isp1301	*isp;

	isp = container_of(i2c, struct isp1301, client);

	isp1301_clear_bits(isp, ISP1301_INTERRUPT_FALLING, ~0);
	isp1301_clear_bits(isp, ISP1301_INTERRUPT_RISING, ~0);
	free_irq(isp->irq, isp);
#ifdef	CONFIG_USB_OTG
	otg_unbind(isp);
#endif
	if (machine_is_omap_h2())
		omap_free_gpio(2);

	isp->timer.data = 0;
	set_bit(WORK_STOP, &isp->todo);
	del_timer_sync(&isp->timer);
	flush_scheduled_work();

	put_device(&i2c->dev);
	the_transceiver = 0;

	return i2c_detach_client(i2c);
}

/*-------------------------------------------------------------------------*/

/* NOTE:  three modes are possible here, only one of which
 * will be standards-conformant on any given system:
 *
 *  - OTG mode (dual-role), required if there's a Mini-AB connector
 *  - HOST mode, for when there's one or more A (host) connectors
 *  - DEVICE mode, for when there's a B/Mini-B (device) connector
 *
 * As a rule, you won't have an isp1301 chip unless it's there to
 * support the OTG mode.  Other modes help testing USB controllers 
 * in isolation from (full) OTG support, or maybe so later board
 * revisions can help to support those feature.
 */

#ifdef	CONFIG_USB_OTG

static int isp1301_otg_enable(struct isp1301 *isp)
{
	power_up(isp);
	otg_init(isp);

	/* NOTE:  since we don't change this, this provides
	 * a few more interrupts than are strictly needed.
	 */
	isp1301_set_bits(isp, ISP1301_INTERRUPT_RISING,
	 	INTR_VBUS_VLD | INTR_SESS_VLD | INTR_ID_GND);
	isp1301_set_bits(isp, ISP1301_INTERRUPT_FALLING,
	 	INTR_VBUS_VLD | INTR_SESS_VLD | INTR_ID_GND);

	dev_info(&isp->client.dev, "ready for dual-role USB ...\n");

	return 0;
}

#endif

/* add or disable the host device+driver */
static int
isp1301_set_host(struct otg_transceiver *otg, struct usb_bus *host)
{
	struct isp1301	*isp = container_of(otg, struct isp1301, otg);

	if (!otg || isp != the_transceiver)
		return -ENODEV;

	if (!host) {
		OTG_IRQ_EN_REG = 0;
		power_down(isp);
		isp->otg.host = 0;
		return 0;
	}

#ifdef	CONFIG_USB_OTG
	isp->otg.host = host;
	dev_dbg(&isp->client.dev, "registered host\n");
	host_suspend(isp);
	if (isp->otg.gadget)
		return isp1301_otg_enable(isp);
	return 0;

#elif	!defined(CONFIG_USB_GADGET_OMAP)
	// FIXME update its refcount
	isp->otg.host = host;

	power_up(isp);

	if (machine_is_omap_h2())
		isp1301_set_bits(isp, ISP1301_MODE_CONTROL_1, MC1_DAT_SE0);

	dev_info(&isp->client.dev, "A-Host sessions ok\n");
	isp1301_set_bits(isp, ISP1301_INTERRUPT_RISING,
	 	INTR_ID_GND);
	isp1301_set_bits(isp, ISP1301_INTERRUPT_FALLING,
	 	INTR_ID_GND);

	/* If this has a Mini-AB connector, this mode is highly
	 * nonstandard ... but can be handy for testing, especially with
	 * the Mini-A end of an OTG cable.  (Or something nonstandard
	 * like MiniB-to-StandardB, maybe built with a gender mender.)
	 */
	isp1301_set_bits(isp, ISP1301_OTG_CONTROL_1, OTG1_VBUS_DRV);

	dump_regs(isp, __FUNCTION__);

	return 0;

#else
	dev_dbg(&isp->client.dev, "host sessions not allowed\n");
	return -EINVAL;
#endif

}

static int
isp1301_set_peripheral(struct otg_transceiver *otg, struct usb_gadget *gadget)
{
	struct isp1301	*isp = container_of(otg, struct isp1301, otg);

	if (!otg || isp != the_transceiver)
		return -ENODEV;

	if (!gadget) {
		OTG_IRQ_EN_REG = 0;
		if (!isp->otg.default_a)
			enable_vbus_draw(isp, 0);
		usb_gadget_vbus_disconnect(isp->otg.gadget);
		isp->otg.gadget = 0;
		power_down(isp);
		return 0;
	}

#ifdef	CONFIG_USB_OTG
	isp->otg.gadget = gadget;
	dev_dbg(&isp->client.dev, "registered gadget\n");
	/* gadget driver may be suspended until vbus_connect () */
	if (isp->otg.host)
		return isp1301_otg_enable(isp);
	return 0;

#elif	!defined(CONFIG_USB_OHCI_HCD) && !defined(CONFIG_USB_OHCI_HCD_MODULE)
	isp->otg.gadget = gadget;
	// FIXME update its refcount

	OTG_CTRL_REG = (OTG_CTRL_REG & OTG_CTRL_MASK
				& ~(OTG_XCEIV_OUTPUTS|OTG_CTRL_BITS))
			| OTG_ID;
	power_up(isp);
	isp->otg.state = OTG_STATE_B_IDLE;

	if (machine_is_omap_h2())
		isp1301_set_bits(isp, ISP1301_MODE_CONTROL_1, MC1_DAT_SE0);

	isp1301_set_bits(isp, ISP1301_INTERRUPT_RISING,
	 	INTR_SESS_VLD);
	isp1301_set_bits(isp, ISP1301_INTERRUPT_FALLING,
	 	INTR_VBUS_VLD);
	dev_info(&isp->client.dev, "B-Peripheral sessions ok\n");
	dump_regs(isp, __FUNCTION__);

	/* If this has a Mini-AB connector, this mode is highly
	 * nonstandard ... but can be handy for testing, so long
	 * as you don't plug a Mini-A cable into the jack.
	 */
	if (isp1301_get_u8(isp, ISP1301_INTERRUPT_SOURCE) & INTR_VBUS_VLD)
		b_peripheral(isp);

	return 0;

#else
	dev_dbg(&isp->client.dev, "peripheral sessions not allowed\n");
	return -EINVAL;
#endif
}


/*-------------------------------------------------------------------------*/

static int
isp1301_set_power(struct otg_transceiver *dev, unsigned mA)
{
	if (!the_transceiver)
		return -ENODEV;
	if (dev->state == OTG_STATE_B_PERIPHERAL)
		enable_vbus_draw(the_transceiver, mA);
	return 0;
}

static int
isp1301_start_srp(struct otg_transceiver *dev)
{
	struct isp1301	*isp = container_of(dev, struct isp1301, otg);
	u32		otg_ctrl;

	if (!dev || isp != the_transceiver
			|| isp->otg.state != OTG_STATE_B_IDLE)
		return -ENODEV;

	otg_ctrl = OTG_CTRL_REG;
	if (!(otg_ctrl & OTG_BSESSEND))
		return -EINVAL;

	otg_ctrl |= OTG_B_BUSREQ;
	otg_ctrl &= ~OTG_A_BUSREQ & OTG_CTRL_MASK;
	OTG_CTRL_REG = otg_ctrl;
	isp->otg.state = OTG_STATE_B_SRP_INIT;

	pr_debug("otg: SRP, %s ... %06x\n", state_name(isp), OTG_CTRL_REG);
#ifdef	CONFIG_USB_OTG
	check_state(isp, __FUNCTION__);
#endif
	return 0;
}

static int
isp1301_start_hnp(struct otg_transceiver *dev)
{
#ifdef	CONFIG_USB_OTG
	struct isp1301	*isp = container_of(dev, struct isp1301, otg);

	if (!dev || isp != the_transceiver)
		return -ENODEV;
	if (isp->otg.default_a && (isp->otg.host == NULL
			|| !isp->otg.host->b_hnp_enable))
		return -ENOTCONN;
	if (!isp->otg.default_a && (isp->otg.gadget == NULL
			|| !isp->otg.gadget->b_hnp_enable))
		return -ENOTCONN;

	/* We want hardware to manage most HNP protocol timings.
	 * So do this part as early as possible...
	 */
	switch (isp->otg.state) {
	case OTG_STATE_B_HOST:
		isp->otg.state = OTG_STATE_B_PERIPHERAL;
		/* caller will suspend next */
		break;
	case OTG_STATE_A_HOST:
#if 0
		/* autoconnect mode avoids irq latency bugs */
		isp1301_set_bits(isp, ISP1301_MODE_CONTROL_1,
				MC1_BDIS_ACON_EN);
#endif
		/* caller must suspend then clear A_BUSREQ */
		usb_gadget_vbus_connect(isp->otg.gadget);
		OTG_CTRL_REG |= OTG_A_SETB_HNPEN;

		break;
	case OTG_STATE_A_PERIPHERAL:
		/* initiated by B-Host suspend */
		break;
	default:
		return -EILSEQ;
	}
	pr_debug("otg: HNP %s, %06x ...\n",
		state_name(isp), OTG_CTRL_REG);
	check_state(isp, __FUNCTION__);
	return 0;
#else
	/* srp-only */
	return -EINVAL;
#endif
}

/*-------------------------------------------------------------------------*/

/* no error returns, they'd just make bus scanning stop */
static int isp1301_probe(struct i2c_adapter *bus, int address, int kind)
{
	int			status;
	struct isp1301		*isp;
	struct i2c_client	*i2c;

	if (the_transceiver)
		return 0;

	isp = kzalloc(sizeof *isp, GFP_KERNEL);
	if (!isp)
		return 0;

	INIT_WORK(&isp->work, isp1301_work, isp);
	init_timer(&isp->timer);
	isp->timer.function = isp1301_timer;
	isp->timer.data = (unsigned long) isp;

	isp->irq = -1;
	isp->client.addr = address;
	i2c_set_clientdata(&isp->client, isp);
	isp->client.adapter = bus;
	isp->client.driver = &isp1301_driver;
	strlcpy(isp->client.name, DRIVER_NAME, I2C_NAME_SIZE);
	i2c = &isp->client;

	/* if this is a true probe, verify the chip ... */
	if (kind < 0) {
		status = isp1301_get_u16(isp, ISP1301_VENDOR_ID);
		if (status != I2C_VENDOR_ID_PHILIPS) {
			dev_dbg(&bus->dev, "addr %d not philips id: %d\n",
				address, status);
			goto fail1;
		}
		status = isp1301_get_u16(isp, ISP1301_PRODUCT_ID);
		if (status != I2C_PRODUCT_ID_PHILIPS_1301) {
			dev_dbg(&bus->dev, "%d not isp1301, %d\n",
				address, status);
			goto fail1;
		}
	}

	status = i2c_attach_client(i2c);
	if (status < 0) {
		dev_dbg(&bus->dev, "can't attach %s to device %d, err %d\n",
				DRIVER_NAME, address, status);
fail1:
		kfree(isp);
		return 0;
	}
	isp->i2c_release = i2c->dev.release;
	i2c->dev.release = isp1301_release;

	/* initial development used chiprev 2.00 */
	status = i2c_smbus_read_word_data(i2c, ISP1301_BCD_DEVICE);
	dev_info(&i2c->dev, "chiprev %x.%02x, driver " DRIVER_VERSION "\n",
		status >> 8, status & 0xff);

	/* make like power-on reset */
	isp1301_clear_bits(isp, ISP1301_MODE_CONTROL_1, MC1_MASK);

	isp1301_set_bits(isp, ISP1301_MODE_CONTROL_2, MC2_BI_DI);
	isp1301_clear_bits(isp, ISP1301_MODE_CONTROL_2, ~MC2_BI_DI);

	isp1301_set_bits(isp, ISP1301_OTG_CONTROL_1,
				OTG1_DM_PULLDOWN | OTG1_DP_PULLDOWN);
	isp1301_clear_bits(isp, ISP1301_OTG_CONTROL_1,
				~(OTG1_DM_PULLDOWN | OTG1_DP_PULLDOWN));

	isp1301_clear_bits(isp, ISP1301_INTERRUPT_LATCH, ~0);
	isp1301_clear_bits(isp, ISP1301_INTERRUPT_FALLING, ~0);
	isp1301_clear_bits(isp, ISP1301_INTERRUPT_RISING, ~0);

#ifdef	CONFIG_USB_OTG
	status = otg_bind(isp);
	if (status < 0) {
		dev_dbg(&i2c->dev, "can't bind OTG\n");
		goto fail2;
	}
#endif

	if (machine_is_omap_h2()) {
		/* full speed signaling by default */
		isp1301_set_bits(isp, ISP1301_MODE_CONTROL_1,
			MC1_SPEED_REG);
		isp1301_set_bits(isp, ISP1301_MODE_CONTROL_2,
			MC2_SPD_SUSP_CTRL);

		/* IRQ wired at M14 */
		omap_cfg_reg(M14_1510_GPIO2);
		isp->irq = OMAP_GPIO_IRQ(2);
		omap_request_gpio(2);
		omap_set_gpio_direction(2, 1);
		omap_set_gpio_edge_ctrl(2, OMAP_GPIO_FALLING_EDGE);
	}

	status = request_irq(isp->irq, isp1301_irq,
			SA_SAMPLE_RANDOM, DRIVER_NAME, isp);
	if (status < 0) {
		dev_dbg(&i2c->dev, "can't get IRQ %d, err %d\n",
				isp->irq, status);
#ifdef	CONFIG_USB_OTG
fail2:
#endif
		i2c_detach_client(i2c);
		goto fail1;
	}

	isp->otg.dev = &isp->client.dev;
	isp->otg.label = DRIVER_NAME;

	isp->otg.set_host = isp1301_set_host,
	isp->otg.set_peripheral = isp1301_set_peripheral,
	isp->otg.set_power = isp1301_set_power,
	isp->otg.start_srp = isp1301_start_srp,
	isp->otg.start_hnp = isp1301_start_hnp,

	enable_vbus_draw(isp, 0);
	power_down(isp);
	the_transceiver = isp;

#ifdef	CONFIG_USB_OTG
	update_otg1(isp, isp1301_get_u8(isp, ISP1301_INTERRUPT_SOURCE));
	update_otg2(isp, isp1301_get_u8(isp, ISP1301_OTG_STATUS));
#endif

	dump_regs(isp, __FUNCTION__);

#ifdef	VERBOSE
	mod_timer(&isp->timer, jiffies + TIMER_JIFFIES);
	dev_dbg(&i2c->dev, "scheduled timer, %d min\n", TIMER_MINUTES);
#endif

	status = otg_set_transceiver(&isp->otg);
	if (status < 0)
		dev_err(&i2c->dev, "can't register transceiver, %d\n",
			status);

	return 0;
}

static int isp1301_scan_bus(struct i2c_adapter *bus)
{
	if (!i2c_check_functionality(bus, I2C_FUNC_SMBUS_BYTE_DATA
			| I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EINVAL;
	return i2c_probe(bus, &addr_data, isp1301_probe);
}

static struct i2c_driver isp1301_driver = {
	.owner		= THIS_MODULE,
	.name		= "isp1301_omap",
	.id		= 1301,		/* FIXME "official", i2c-ids.h */
	.class		= I2C_CLASS_HWMON,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= isp1301_scan_bus,
	.detach_client	= isp1301_detach_client,
};

/*-------------------------------------------------------------------------*/

static int __init isp_init(void)
{
	return i2c_add_driver(&isp1301_driver);
}
module_init(isp_init);

static void __exit isp_exit(void)
{
	if (the_transceiver)
		otg_set_transceiver(0);
	i2c_del_driver(&isp1301_driver);
}
module_exit(isp_exit);

