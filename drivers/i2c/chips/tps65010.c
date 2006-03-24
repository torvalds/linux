/*
 * tps65010 - driver for tps6501x power management chips
 *
 * Copyright (C) 2004 Texas Instruments
 * Copyright (C) 2004-2005 David Brownell
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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/suspend.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>

#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/arch/gpio.h>
#include <asm/arch/mux.h>
#include <asm/arch/tps65010.h>

/*-------------------------------------------------------------------------*/

#define	DRIVER_VERSION	"2 May 2005"
#define	DRIVER_NAME	(tps65010_driver.name)

MODULE_DESCRIPTION("TPS6501x Power Management Driver");
MODULE_LICENSE("GPL");

static unsigned short normal_i2c[] = { 0x48, /* 0x49, */ I2C_CLIENT_END };
static unsigned short normal_i2c_range[] = { I2C_CLIENT_END };

I2C_CLIENT_INSMOD;

static struct i2c_driver tps65010_driver;

/*-------------------------------------------------------------------------*/

/* This driver handles a family of multipurpose chips, which incorporate
 * voltage regulators, lithium ion/polymer battery charging, GPIOs, LEDs,
 * and other features often needed in portable devices like cell phones
 * or digital cameras.
 *
 * The tps65011 and tps65013 have different voltage settings compared
 * to tps65010 and tps65012.  The tps65013 has a NO_CHG status/irq.
 * All except tps65010 have "wait" mode, possibly defaulted so that
 * battery-insert != device-on.
 *
 * We could distinguish between some models by checking VDCDC1.UVLO or
 * other registers, unless they've been changed already after powerup
 * as part of board setup by a bootloader.
 */
enum tps_model {
	TPS_UNKNOWN = 0,
	TPS65010,
	TPS65011,
	TPS65012,
	TPS65013,
};

struct tps65010 {
	struct i2c_client	client;
	struct mutex		lock;
	int			irq;
	struct work_struct	work;
	struct dentry		*file;
	unsigned		charging:1;
	unsigned		por:1;
	unsigned		model:8;
	u16			vbus;
	unsigned long		flags;
#define	FLAG_VBUS_CHANGED	0
#define	FLAG_IRQ_ENABLE		1

	/* copies of last register state */
	u8			chgstatus, regstatus, chgconf;
	u8			nmask1, nmask2;

	/* not currently tracking GPIO state */
};

#define	POWER_POLL_DELAY	msecs_to_jiffies(800)

/*-------------------------------------------------------------------------*/

#if	defined(DEBUG) || defined(CONFIG_DEBUG_FS)

static void dbg_chgstat(char *buf, size_t len, u8 chgstatus)
{
	snprintf(buf, len, "%02x%s%s%s%s%s%s%s%s\n",
		chgstatus,
		(chgstatus & TPS_CHG_USB) ? " USB" : "",
		(chgstatus & TPS_CHG_AC) ? " AC" : "",
		(chgstatus & TPS_CHG_THERM) ? " therm" : "",
		(chgstatus & TPS_CHG_TERM) ? " done" :
			((chgstatus & (TPS_CHG_USB|TPS_CHG_AC))
				? " (charging)" : ""),
		(chgstatus & TPS_CHG_TAPER_TMO) ? " taper_tmo" : "",
		(chgstatus & TPS_CHG_CHG_TMO) ? " charge_tmo" : "",
		(chgstatus & TPS_CHG_PRECHG_TMO) ? " prechg_tmo" : "",
		(chgstatus & TPS_CHG_TEMP_ERR) ? " temp_err" : "");
}

static void dbg_regstat(char *buf, size_t len, u8 regstatus)
{
	snprintf(buf, len, "%02x %s%s%s%s%s%s%s%s\n",
		regstatus,
		(regstatus & TPS_REG_ONOFF) ? "off" : "(on)",
		(regstatus & TPS_REG_COVER) ? " uncover" : "",
		(regstatus & TPS_REG_UVLO) ? " UVLO" : "",
		(regstatus & TPS_REG_NO_CHG) ? " NO_CHG" : "",
		(regstatus & TPS_REG_PG_LD02) ? " ld02_bad" : "",
		(regstatus & TPS_REG_PG_LD01) ? " ld01_bad" : "",
		(regstatus & TPS_REG_PG_MAIN) ? " main_bad" : "",
		(regstatus & TPS_REG_PG_CORE) ? " core_bad" : "");
}

static void dbg_chgconf(int por, char *buf, size_t len, u8 chgconfig)
{
	const char *hibit;

	if (por)
		hibit = (chgconfig & TPS_CHARGE_POR)
				? "POR=69ms" : "POR=1sec";
	else
		hibit = (chgconfig & TPS65013_AUA) ? "AUA" : "";

	snprintf(buf, len, "%02x %s%s%s AC=%d%% USB=%dmA %sCharge\n",
		chgconfig, hibit,
		(chgconfig & TPS_CHARGE_RESET) ? " reset" : "",
		(chgconfig & TPS_CHARGE_FAST) ? " fast" : "",
		({int p; switch ((chgconfig >> 3) & 3) {
		case 3:		p = 100; break;
		case 2:		p = 75; break;
		case 1:		p = 50; break;
		default:	p = 25; break;
		}; p; }),
		(chgconfig & TPS_VBUS_CHARGING)
			? ((chgconfig & TPS_VBUS_500MA) ? 500 : 100)
			: 0,
		(chgconfig & TPS_CHARGE_ENABLE) ? "" : "No");
}

#endif

#ifdef	DEBUG

static void show_chgstatus(const char *label, u8 chgstatus)
{
	char buf [100];

	dbg_chgstat(buf, sizeof buf, chgstatus);
	pr_debug("%s: %s %s", DRIVER_NAME, label, buf);
}

static void show_regstatus(const char *label, u8 regstatus)
{
	char buf [100];

	dbg_regstat(buf, sizeof buf, regstatus);
	pr_debug("%s: %s %s", DRIVER_NAME, label, buf);
}

static void show_chgconfig(int por, const char *label, u8 chgconfig)
{
	char buf [100];

	dbg_chgconf(por, buf, sizeof buf, chgconfig);
	pr_debug("%s: %s %s", DRIVER_NAME, label, buf);
}

#else

static inline void show_chgstatus(const char *label, u8 chgstatus) { }
static inline void show_regstatus(const char *label, u8 chgstatus) { }
static inline void show_chgconfig(int por, const char *label, u8 chgconfig) { }

#endif

#ifdef	CONFIG_DEBUG_FS

static int dbg_show(struct seq_file *s, void *_)
{
	struct tps65010	*tps = s->private;
	u8		value, v2;
	unsigned	i;
	char		buf[100];
	const char	*chip;

	switch (tps->model) {
	case TPS65010:	chip = "tps65010"; break;
	case TPS65011:	chip = "tps65011"; break;
	case TPS65012:	chip = "tps65012"; break;
	case TPS65013:	chip = "tps65013"; break;
	default:	chip = NULL; break;
	}
	seq_printf(s, "driver  %s\nversion %s\nchip    %s\n\n",
			DRIVER_NAME, DRIVER_VERSION, chip);

	mutex_lock(&tps->lock);

	/* FIXME how can we tell whether a battery is present?
	 * likely involves a charge gauging chip (like BQ26501).
	 */

	seq_printf(s, "%scharging\n\n", tps->charging ? "" : "(not) ");


	/* registers for monitoring battery charging and status; note
	 * that reading chgstat and regstat may ack IRQs...
	 */
	value = i2c_smbus_read_byte_data(&tps->client, TPS_CHGCONFIG);
	dbg_chgconf(tps->por, buf, sizeof buf, value);
	seq_printf(s, "chgconfig %s", buf);

	value = i2c_smbus_read_byte_data(&tps->client, TPS_CHGSTATUS);
	dbg_chgstat(buf, sizeof buf, value);
	seq_printf(s, "chgstat   %s", buf);
	value = i2c_smbus_read_byte_data(&tps->client, TPS_MASK1);
	dbg_chgstat(buf, sizeof buf, value);
	seq_printf(s, "mask1     %s", buf);
	/* ignore ackint1 */

	value = i2c_smbus_read_byte_data(&tps->client, TPS_REGSTATUS);
	dbg_regstat(buf, sizeof buf, value);
	seq_printf(s, "regstat   %s", buf);
	value = i2c_smbus_read_byte_data(&tps->client, TPS_MASK2);
	dbg_regstat(buf, sizeof buf, value);
	seq_printf(s, "mask2     %s\n", buf);
	/* ignore ackint2 */

	(void) schedule_delayed_work(&tps->work, POWER_POLL_DELAY);


	/* VMAIN voltage, enable lowpower, etc */
	value = i2c_smbus_read_byte_data(&tps->client, TPS_VDCDC1);
	seq_printf(s, "vdcdc1    %02x\n", value);

	/* VCORE voltage, vibrator on/off */
	value = i2c_smbus_read_byte_data(&tps->client, TPS_VDCDC2);
	seq_printf(s, "vdcdc2    %02x\n", value);

	/* both LD0s, and their lowpower behavior */
	value = i2c_smbus_read_byte_data(&tps->client, TPS_VREGS1);
	seq_printf(s, "vregs1    %02x\n\n", value);


	/* LEDs and GPIOs */
	value = i2c_smbus_read_byte_data(&tps->client, TPS_LED1_ON);
	v2 = i2c_smbus_read_byte_data(&tps->client, TPS_LED1_PER);
	seq_printf(s, "led1 %s, on=%02x, per=%02x, %d/%d msec\n",
		(value & 0x80)
			? ((v2 & 0x80) ? "on" : "off")
			: ((v2 & 0x80) ? "blink" : "(nPG)"),
		value, v2,
		(value & 0x7f) * 10, (v2 & 0x7f) * 100);

	value = i2c_smbus_read_byte_data(&tps->client, TPS_LED2_ON);
	v2 = i2c_smbus_read_byte_data(&tps->client, TPS_LED2_PER);
	seq_printf(s, "led2 %s, on=%02x, per=%02x, %d/%d msec\n",
		(value & 0x80)
			? ((v2 & 0x80) ? "on" : "off")
			: ((v2 & 0x80) ? "blink" : "off"),
		value, v2,
		(value & 0x7f) * 10, (v2 & 0x7f) * 100);

	value = i2c_smbus_read_byte_data(&tps->client, TPS_DEFGPIO);
	v2 = i2c_smbus_read_byte_data(&tps->client, TPS_MASK3);
	seq_printf(s, "defgpio %02x mask3 %02x\n", value, v2);

	for (i = 0; i < 4; i++) {
		if (value & (1 << (4 + i)))
			seq_printf(s, "  gpio%d-out %s\n", i + 1,
				(value & (1 << i)) ? "low" : "hi ");
		else
			seq_printf(s, "  gpio%d-in  %s %s %s\n", i + 1,
				(value & (1 << i)) ? "hi " : "low",
				(v2 & (1 << i)) ? "no-irq" : "irq",
				(v2 & (1 << (4 + i))) ? "rising" : "falling");
	}

	mutex_unlock(&tps->lock);
	return 0;
}

static int dbg_tps_open(struct inode *inode, struct file *file)
{
	return single_open(file, dbg_show, inode->u.generic_ip);
}

static struct file_operations debug_fops = {
	.open		= dbg_tps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#define	DEBUG_FOPS	&debug_fops

#else
#define	DEBUG_FOPS	NULL
#endif

/*-------------------------------------------------------------------------*/

/* handle IRQS in a task context, so we can use I2C calls */
static void tps65010_interrupt(struct tps65010 *tps)
{
	u8 tmp = 0, mask, poll;

	/* IRQs won't trigger irqs for certain events, but we can get
	 * others by polling (normally, with external power applied).
	 */
	poll = 0;

	/* regstatus irqs */
	if (tps->nmask2) {
		tmp = i2c_smbus_read_byte_data(&tps->client, TPS_REGSTATUS);
		mask = tmp ^ tps->regstatus;
		tps->regstatus = tmp;
		mask &= tps->nmask2;
	} else
		mask = 0;
	if (mask) {
		tps->regstatus =  tmp;
		/* may need to shut something down ... */

		/* "off" usually means deep sleep */
		if (tmp & TPS_REG_ONOFF) {
			pr_info("%s: power off button\n", DRIVER_NAME);
#if 0
			/* REVISIT:  this might need its own workqueue
			 * plus tweaks including deadlock avoidance ...
			 */
			software_suspend();
#endif
			poll = 1;
		}
	}

	/* chgstatus irqs */
	if (tps->nmask1) {
		tmp = i2c_smbus_read_byte_data(&tps->client, TPS_CHGSTATUS);
		mask = tmp ^ tps->chgstatus;
		tps->chgstatus = tmp;
		mask &= tps->nmask1;
	} else
		mask = 0;
	if (mask) {
		unsigned	charging = 0;

		show_chgstatus("chg/irq", tmp);
		if (tmp & (TPS_CHG_USB|TPS_CHG_AC))
			show_chgconfig(tps->por, "conf", tps->chgconf);

		/* Unless it was turned off or disabled, we charge any
		 * battery whenever there's power available for it
		 * and the charger hasn't been disabled.
		 */
		if (!(tps->chgstatus & ~(TPS_CHG_USB|TPS_CHG_AC))
				&& (tps->chgstatus & (TPS_CHG_USB|TPS_CHG_AC))
				&& (tps->chgconf & TPS_CHARGE_ENABLE)
				) {
			if (tps->chgstatus & TPS_CHG_USB) {
				/* VBUS options are readonly until reconnect */
				if (mask & TPS_CHG_USB)
					set_bit(FLAG_VBUS_CHANGED, &tps->flags);
				charging = 1;
			} else if (tps->chgstatus & TPS_CHG_AC)
				charging = 1;
		}
		if (charging != tps->charging) {
			tps->charging = charging;
			pr_info("%s: battery %scharging\n",
				DRIVER_NAME, charging ? "" :
				((tps->chgstatus & (TPS_CHG_USB|TPS_CHG_AC))
					? "NOT " : "dis"));
		}
	}

	/* always poll to detect (a) power removal, without tps65013
	 * NO_CHG IRQ; or (b) restart of charging after stop.
	 */
	if ((tps->model != TPS65013 || !tps->charging)
			&& (tps->chgstatus & (TPS_CHG_USB|TPS_CHG_AC)))
		poll = 1;
	if (poll)
		(void) schedule_delayed_work(&tps->work, POWER_POLL_DELAY);

	/* also potentially gpio-in rise or fall */
}

/* handle IRQs and polling using keventd for now */
static void tps65010_work(void *_tps)
{
	struct tps65010		*tps = _tps;

	mutex_lock(&tps->lock);

	tps65010_interrupt(tps);

	if (test_and_clear_bit(FLAG_VBUS_CHANGED, &tps->flags)) {
		int	status;
		u8	chgconfig, tmp;

		chgconfig = i2c_smbus_read_byte_data(&tps->client,
					TPS_CHGCONFIG);
		chgconfig &= ~(TPS_VBUS_500MA | TPS_VBUS_CHARGING);
		if (tps->vbus == 500)
			chgconfig |= TPS_VBUS_500MA | TPS_VBUS_CHARGING;
		else if (tps->vbus >= 100)
			chgconfig |= TPS_VBUS_CHARGING;

		status = i2c_smbus_write_byte_data(&tps->client,
				TPS_CHGCONFIG, chgconfig);

		/* vbus update fails unless VBUS is connected! */
		tmp = i2c_smbus_read_byte_data(&tps->client, TPS_CHGCONFIG);
		tps->chgconf = tmp;
		show_chgconfig(tps->por, "update vbus", tmp);
	}

	if (test_and_clear_bit(FLAG_IRQ_ENABLE, &tps->flags))
		enable_irq(tps->irq);

	mutex_unlock(&tps->lock);
}

static irqreturn_t tps65010_irq(int irq, void *_tps, struct pt_regs *regs)
{
	struct tps65010		*tps = _tps;

	disable_irq_nosync(irq);
	set_bit(FLAG_IRQ_ENABLE, &tps->flags);
	(void) schedule_work(&tps->work);
	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

static struct tps65010 *the_tps;

static int __exit tps65010_detach_client(struct i2c_client *client)
{
	struct tps65010		*tps;

	tps = container_of(client, struct tps65010, client);
#ifdef	CONFIG_ARM
	if (machine_is_omap_h2())
		omap_free_gpio(58);
	if (machine_is_omap_osk())
		omap_free_gpio(OMAP_MPUIO(1));
#endif
	free_irq(tps->irq, tps);
	debugfs_remove(tps->file);
	if (i2c_detach_client(client) == 0)
		kfree(tps);
	the_tps = NULL;
	return 0;
}

static int tps65010_noscan(struct i2c_adapter *bus)
{
	/* pure paranoia, in case someone adds another i2c bus
	 * after our init section's gone...
	 */
	return -ENODEV;
}

/* no error returns, they'd just make bus scanning stop */
static int __init
tps65010_probe(struct i2c_adapter *bus, int address, int kind)
{
	struct tps65010		*tps;
	int			status;
	unsigned long		irqflags;

	if (the_tps) {
		dev_dbg(&bus->dev, "only one %s for now\n", DRIVER_NAME);
		return 0;
	}

	tps = kzalloc(sizeof *tps, GFP_KERNEL);
	if (!tps)
		return 0;

	mutex_init(&tps->lock);
	INIT_WORK(&tps->work, tps65010_work, tps);
	tps->irq = -1;
	tps->client.addr = address;
	tps->client.adapter = bus;
	tps->client.driver = &tps65010_driver;
	strlcpy(tps->client.name, DRIVER_NAME, I2C_NAME_SIZE);

	status = i2c_attach_client(&tps->client);
	if (status < 0) {
		dev_dbg(&bus->dev, "can't attach %s to device %d, err %d\n",
				DRIVER_NAME, address, status);
		goto fail1;
	}

#ifdef	CONFIG_ARM
	irqflags = SA_SAMPLE_RANDOM | SA_TRIGGER_LOW;
	if (machine_is_omap_h2()) {
		tps->model = TPS65010;
		omap_cfg_reg(W4_GPIO58);
		tps->irq = OMAP_GPIO_IRQ(58);
		omap_request_gpio(58);
		omap_set_gpio_direction(58, 1);
		irqflags |= SA_TRIGGER_FALLING;
	}
	if (machine_is_omap_osk()) {
		tps->model = TPS65010;
		// omap_cfg_reg(U19_1610_MPUIO1);
		tps->irq = OMAP_GPIO_IRQ(OMAP_MPUIO(1));
		omap_request_gpio(OMAP_MPUIO(1));
		omap_set_gpio_direction(OMAP_MPUIO(1), 1);
		irqflags |= SA_TRIGGER_FALLING;
	}
	if (machine_is_omap_h3()) {
		tps->model = TPS65013;

		// FIXME set up this board's IRQ ...
	}
#else
	irqflags = SA_SAMPLE_RANDOM;
#endif

	if (tps->irq > 0) {
		status = request_irq(tps->irq, tps65010_irq,
			irqflags, DRIVER_NAME, tps);
		if (status < 0) {
			dev_dbg(&tps->client.dev, "can't get IRQ %d, err %d\n",
					tps->irq, status);
			i2c_detach_client(&tps->client);
			goto fail1;
		}
#ifdef	CONFIG_ARM
		/* annoying race here, ideally we'd have an option
		 * to claim the irq now and enable it later.
		 */
		disable_irq(tps->irq);
		set_bit(FLAG_IRQ_ENABLE, &tps->flags);
#endif
	} else
		printk(KERN_WARNING "%s: IRQ not configured!\n",
				DRIVER_NAME);


	switch (tps->model) {
	case TPS65010:
	case TPS65012:
		tps->por = 1;
		break;
	case TPS_UNKNOWN:
		printk(KERN_WARNING "%s: unknown TPS chip\n", DRIVER_NAME);
		break;
	/* else CHGCONFIG.POR is replaced by AUA, enabling a WAIT mode */
	}
	tps->chgconf = i2c_smbus_read_byte_data(&tps->client, TPS_CHGCONFIG);
	show_chgconfig(tps->por, "conf/init", tps->chgconf);

	show_chgstatus("chg/init",
		i2c_smbus_read_byte_data(&tps->client, TPS_CHGSTATUS));
	show_regstatus("reg/init",
		i2c_smbus_read_byte_data(&tps->client, TPS_REGSTATUS));

	pr_debug("%s: vdcdc1 0x%02x, vdcdc2 %02x, vregs1 %02x\n", DRIVER_NAME,
		i2c_smbus_read_byte_data(&tps->client, TPS_VDCDC1),
		i2c_smbus_read_byte_data(&tps->client, TPS_VDCDC2),
		i2c_smbus_read_byte_data(&tps->client, TPS_VREGS1));
	pr_debug("%s: defgpio 0x%02x, mask3 0x%02x\n", DRIVER_NAME,
		i2c_smbus_read_byte_data(&tps->client, TPS_DEFGPIO),
		i2c_smbus_read_byte_data(&tps->client, TPS_MASK3));

	tps65010_driver.attach_adapter = tps65010_noscan;
	the_tps = tps;

#if	defined(CONFIG_USB_GADGET) && !defined(CONFIG_USB_OTG)
	/* USB hosts can't draw VBUS.  OTG devices could, later
	 * when OTG infrastructure enables it.  USB peripherals
	 * could be relying on VBUS while booting, though.
	 */
	tps->vbus = 100;
#endif

	/* unmask the "interesting" irqs, then poll once to
	 * kickstart monitoring, initialize shadowed status
	 * registers, and maybe disable VBUS draw.
	 */
	tps->nmask1 = ~0;
	(void) i2c_smbus_write_byte_data(&tps->client, TPS_MASK1, ~tps->nmask1);

	tps->nmask2 = TPS_REG_ONOFF;
	if (tps->model == TPS65013)
		tps->nmask2 |= TPS_REG_NO_CHG;
	(void) i2c_smbus_write_byte_data(&tps->client, TPS_MASK2, ~tps->nmask2);

	(void) i2c_smbus_write_byte_data(&tps->client, TPS_MASK3, 0x0f
		| i2c_smbus_read_byte_data(&tps->client, TPS_MASK3));

	tps65010_work(tps);

	tps->file = debugfs_create_file(DRIVER_NAME, S_IRUGO, NULL,
				tps, DEBUG_FOPS);
	return 0;
fail1:
	kfree(tps);
	return 0;
}

static int __init tps65010_scan_bus(struct i2c_adapter *bus)
{
	if (!i2c_check_functionality(bus, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EINVAL;
	return i2c_probe(bus, &addr_data, tps65010_probe);
}

static struct i2c_driver tps65010_driver = {
	.driver = {
		.name	= "tps65010",
	},
	.attach_adapter	= tps65010_scan_bus,
	.detach_client	= __exit_p(tps65010_detach_client),
};

/*-------------------------------------------------------------------------*/

/* Draw from VBUS:
 *   0 mA -- DON'T DRAW (might supply power instead)
 * 100 mA -- usb unit load (slowest charge rate)
 * 500 mA -- usb high power (fast battery charge)
 */
int tps65010_set_vbus_draw(unsigned mA)
{
	unsigned long	flags;

	if (!the_tps)
		return -ENODEV;

	/* assumes non-SMP */
	local_irq_save(flags);
	if (mA >= 500)
		mA = 500;
	else if (mA >= 100)
		mA = 100;
	else
		mA = 0;
	the_tps->vbus = mA;
	if ((the_tps->chgstatus & TPS_CHG_USB)
			&& test_and_set_bit(
				FLAG_VBUS_CHANGED, &the_tps->flags)) {
		/* gadget drivers call this in_irq() */
		(void) schedule_work(&the_tps->work);
	}
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(tps65010_set_vbus_draw);

/*-------------------------------------------------------------------------*/
/* tps65010_set_gpio_out_value parameter:
 * gpio:  GPIO1, GPIO2, GPIO3 or GPIO4
 * value: LOW or HIGH
 */
int tps65010_set_gpio_out_value(unsigned gpio, unsigned value)
{
	int	 status;
	unsigned defgpio;

	if (!the_tps)
		return -ENODEV;
	if ((gpio < GPIO1) || (gpio > GPIO4))
		return -EINVAL;

	mutex_lock(&the_tps->lock);

	defgpio = i2c_smbus_read_byte_data(&the_tps->client, TPS_DEFGPIO);

	/* Configure GPIO for output */
	defgpio |= 1 << (gpio + 3);

	/* Writing 1 forces a logic 0 on that GPIO and vice versa */
	switch (value) {
	case LOW:
		defgpio |= 1 << (gpio - 1);    /* set GPIO low by writing 1 */
		break;
	/* case HIGH: */
	default:
		defgpio &= ~(1 << (gpio - 1)); /* set GPIO high by writing 0 */
		break;
	}

	status = i2c_smbus_write_byte_data(&the_tps->client,
		TPS_DEFGPIO, defgpio);

	pr_debug("%s: gpio%dout = %s, defgpio 0x%02x\n", DRIVER_NAME,
		gpio, value ? "high" : "low",
		i2c_smbus_read_byte_data(&the_tps->client, TPS_DEFGPIO));

	mutex_unlock(&the_tps->lock);
	return status;
}
EXPORT_SYMBOL(tps65010_set_gpio_out_value);

/*-------------------------------------------------------------------------*/
/* tps65010_set_led parameter:
 * led:  LED1 or LED2
 * mode: ON, OFF or BLINK
 */
int tps65010_set_led(unsigned led, unsigned mode)
{
	int	 status;
	unsigned led_on, led_per, offs;

	if (!the_tps)
		return -ENODEV;

	if (led == LED1)
		offs = 0;
	else {
		offs = 2;
		led = LED2;
	}

	mutex_lock(&the_tps->lock);

	pr_debug("%s: led%i_on   0x%02x\n", DRIVER_NAME, led,
		i2c_smbus_read_byte_data(&the_tps->client,
				TPS_LED1_ON + offs));

	pr_debug("%s: led%i_per  0x%02x\n", DRIVER_NAME, led,
		i2c_smbus_read_byte_data(&the_tps->client,
				TPS_LED1_PER + offs));

	switch (mode) {
	case OFF:
		led_on  = 1 << 7;
		led_per = 0 << 7;
		break;
	case ON:
		led_on  = 1 << 7;
		led_per = 1 << 7;
		break;
	case BLINK:
		led_on  = 0x30 | (0 << 7);
		led_per = 0x08 | (1 << 7);
		break;
	default:
		printk(KERN_ERR "%s: Wrong mode parameter for set_led()\n",
		       DRIVER_NAME);
		mutex_unlock(&the_tps->lock);
		return -EINVAL;
	}

	status = i2c_smbus_write_byte_data(&the_tps->client,
			TPS_LED1_ON + offs, led_on);

	if (status != 0) {
		printk(KERN_ERR "%s: Failed to write led%i_on register\n",
		       DRIVER_NAME, led);
		mutex_unlock(&the_tps->lock);
		return status;
	}

	pr_debug("%s: led%i_on   0x%02x\n", DRIVER_NAME, led,
		i2c_smbus_read_byte_data(&the_tps->client, TPS_LED1_ON + offs));

	status = i2c_smbus_write_byte_data(&the_tps->client,
			TPS_LED1_PER + offs, led_per);

	if (status != 0) {
		printk(KERN_ERR "%s: Failed to write led%i_per register\n",
		       DRIVER_NAME, led);
		mutex_unlock(&the_tps->lock);
		return status;
	}

	pr_debug("%s: led%i_per  0x%02x\n", DRIVER_NAME, led,
		i2c_smbus_read_byte_data(&the_tps->client,
				TPS_LED1_PER + offs));

	mutex_unlock(&the_tps->lock);

	return status;
}
EXPORT_SYMBOL(tps65010_set_led);

/*-------------------------------------------------------------------------*/
/* tps65010_set_vib parameter:
 * value: ON or OFF
 */
int tps65010_set_vib(unsigned value)
{
	int	 status;
	unsigned vdcdc2;

	if (!the_tps)
		return -ENODEV;

	mutex_lock(&the_tps->lock);

	vdcdc2 = i2c_smbus_read_byte_data(&the_tps->client, TPS_VDCDC2);
	vdcdc2 &= ~(1 << 1);
	if (value)
		vdcdc2 |= (1 << 1);
	status = i2c_smbus_write_byte_data(&the_tps->client,
		TPS_VDCDC2, vdcdc2);

	pr_debug("%s: vibrator %s\n", DRIVER_NAME, value ? "on" : "off");

	mutex_unlock(&the_tps->lock);
	return status;
}
EXPORT_SYMBOL(tps65010_set_vib);

/*-------------------------------------------------------------------------*/
/* tps65010_set_low_pwr parameter:
 * mode: ON or OFF
 */
int tps65010_set_low_pwr(unsigned mode)
{
	int	 status;
	unsigned vdcdc1;

	if (!the_tps)
		return -ENODEV;

	mutex_lock(&the_tps->lock);

	pr_debug("%s: %s low_pwr, vdcdc1 0x%02x\n", DRIVER_NAME,
		mode ? "enable" : "disable",
		i2c_smbus_read_byte_data(&the_tps->client, TPS_VDCDC1));

	vdcdc1 = i2c_smbus_read_byte_data(&the_tps->client, TPS_VDCDC1);

	switch (mode) {
	case OFF:
		vdcdc1 &= ~TPS_ENABLE_LP; /* disable ENABLE_LP bit */
		break;
	/* case ON: */
	default:
		vdcdc1 |= TPS_ENABLE_LP;  /* enable ENABLE_LP bit */
		break;
	}

	status = i2c_smbus_write_byte_data(&the_tps->client,
			TPS_VDCDC1, vdcdc1);

	if (status != 0)
		printk(KERN_ERR "%s: Failed to write vdcdc1 register\n",
			DRIVER_NAME);
	else
		pr_debug("%s: vdcdc1 0x%02x\n", DRIVER_NAME,
			i2c_smbus_read_byte_data(&the_tps->client, TPS_VDCDC1));

	mutex_unlock(&the_tps->lock);

	return status;
}
EXPORT_SYMBOL(tps65010_set_low_pwr);

/*-------------------------------------------------------------------------*/
/* tps65010_config_vregs1 parameter:
 * value to be written to VREGS1 register
 * Note: The complete register is written, set all bits you need
 */
int tps65010_config_vregs1(unsigned value)
{
	int	 status;

	if (!the_tps)
		return -ENODEV;

	mutex_lock(&the_tps->lock);

	pr_debug("%s: vregs1 0x%02x\n", DRIVER_NAME,
			i2c_smbus_read_byte_data(&the_tps->client, TPS_VREGS1));

	status = i2c_smbus_write_byte_data(&the_tps->client,
			TPS_VREGS1, value);

	if (status != 0)
		printk(KERN_ERR "%s: Failed to write vregs1 register\n",
			DRIVER_NAME);
	else
		pr_debug("%s: vregs1 0x%02x\n", DRIVER_NAME,
			i2c_smbus_read_byte_data(&the_tps->client, TPS_VREGS1));

	mutex_unlock(&the_tps->lock);

	return status;
}
EXPORT_SYMBOL(tps65010_config_vregs1);

/*-------------------------------------------------------------------------*/
/* tps65013_set_low_pwr parameter:
 * mode: ON or OFF
 */

/* FIXME: Assumes AC or USB power is present. Setting AUA bit is not
	required if power supply is through a battery */

int tps65013_set_low_pwr(unsigned mode)
{
	int	 status;
	unsigned vdcdc1, chgconfig;

	if (!the_tps || the_tps->por)
		return -ENODEV;

	mutex_lock(&the_tps->lock);

	pr_debug("%s: %s low_pwr, chgconfig 0x%02x vdcdc1 0x%02x\n",
		DRIVER_NAME,
		mode ? "enable" : "disable",
		i2c_smbus_read_byte_data(&the_tps->client, TPS_CHGCONFIG),
		i2c_smbus_read_byte_data(&the_tps->client, TPS_VDCDC1));

	chgconfig = i2c_smbus_read_byte_data(&the_tps->client, TPS_CHGCONFIG);
	vdcdc1 = i2c_smbus_read_byte_data(&the_tps->client, TPS_VDCDC1);

	switch (mode) {
	case OFF:
		chgconfig &= ~TPS65013_AUA; /* disable AUA bit */
		vdcdc1 &= ~TPS_ENABLE_LP; /* disable ENABLE_LP bit */
		break;
	/* case ON: */
	default:
		chgconfig |= TPS65013_AUA;  /* enable AUA bit */
		vdcdc1 |= TPS_ENABLE_LP;  /* enable ENABLE_LP bit */
		break;
	}

	status = i2c_smbus_write_byte_data(&the_tps->client,
			TPS_CHGCONFIG, chgconfig);
	if (status != 0) {
		printk(KERN_ERR "%s: Failed to write chconfig register\n",
	 DRIVER_NAME);
		mutex_unlock(&the_tps->lock);
		return status;
	}

	chgconfig = i2c_smbus_read_byte_data(&the_tps->client, TPS_CHGCONFIG);
	the_tps->chgconf = chgconfig;
	show_chgconfig(0, "chgconf", chgconfig);

	status = i2c_smbus_write_byte_data(&the_tps->client,
			TPS_VDCDC1, vdcdc1);

	if (status != 0)
		printk(KERN_ERR "%s: Failed to write vdcdc1 register\n",
	 DRIVER_NAME);
	else
		pr_debug("%s: vdcdc1 0x%02x\n", DRIVER_NAME,
			i2c_smbus_read_byte_data(&the_tps->client, TPS_VDCDC1));

	mutex_unlock(&the_tps->lock);

	return status;
}
EXPORT_SYMBOL(tps65013_set_low_pwr);

/*-------------------------------------------------------------------------*/

static int __init tps_init(void)
{
	u32	tries = 3;
	int	status = -ENODEV;

	printk(KERN_INFO "%s: version %s\n", DRIVER_NAME, DRIVER_VERSION);

	/* some boards have startup glitches */
	while (tries--) {
		status = i2c_add_driver(&tps65010_driver);
		if (the_tps)
			break;
		i2c_del_driver(&tps65010_driver);
		if (!tries) {
			printk(KERN_ERR "%s: no chip?\n", DRIVER_NAME);
			return -ENODEV;
		}
		pr_debug("%s: re-probe ...\n", DRIVER_NAME);
		msleep(10);
	}

#ifdef	CONFIG_ARM
	if (machine_is_omap_osk()) {

		// FIXME: More should be placed in the initialization code
		//	  of the submodules (DSP, ethernet, power management,
		//	  board-osk.c). Careful: I2C is initialized "late".

		/* Let LED1 (D9) blink */
		tps65010_set_led(LED1, BLINK);

		/* Disable LED 2 (D2) */
		tps65010_set_led(LED2, OFF);

		/* Set GPIO 1 HIGH to disable VBUS power supply;
		 * OHCI driver powers it up/down as needed.
		 */
		tps65010_set_gpio_out_value(GPIO1, HIGH);

		/* Set GPIO 2 low to turn on LED D3 */
		tps65010_set_gpio_out_value(GPIO2, HIGH);

		/* Set GPIO 3 low to take ethernet out of reset */
		tps65010_set_gpio_out_value(GPIO3, LOW);

		/* gpio4 for VDD_DSP */

		/* Enable LOW_PWR */
		tps65010_set_low_pwr(ON);

		/* Switch VLDO2 to 3.0V for AIC23 */
		tps65010_config_vregs1(TPS_LDO2_ENABLE | TPS_VLDO2_3_0V | TPS_LDO1_ENABLE);

	} else if (machine_is_omap_h2()) {
		/* gpio3 for SD, gpio4 for VDD_DSP */

		/* Enable LOW_PWR */
		tps65010_set_low_pwr(ON);
	} else if (machine_is_omap_h3()) {
		/* gpio4 for SD, gpio3 for VDD_DSP */
#ifdef CONFIG_PM
		/* Enable LOW_PWR */
		tps65013_set_low_pwr(ON);
#endif
	}
#endif

	return status;
}
/* NOTE:  this MUST be initialized before the other parts of the system
 * that rely on it ... but after the i2c bus on which this relies.
 * That is, much earlier than on PC-type systems, which don't often use
 * I2C as a core system bus.
 */
subsys_initcall(tps_init);

static void __exit tps_exit(void)
{
	i2c_del_driver(&tps65010_driver);
}
module_exit(tps_exit);

