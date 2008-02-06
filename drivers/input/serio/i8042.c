/*
 *  i8042 keyboard and mouse controller driver for Linux
 *
 *  Copyright (c) 1999-2004 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/err.h>
#include <linux/rcupdate.h>
#include <linux/platform_device.h>
#include <linux/i8042.h>

#include <asm/io.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("i8042 keyboard and mouse controller driver");
MODULE_LICENSE("GPL");

static unsigned int i8042_nokbd;
module_param_named(nokbd, i8042_nokbd, bool, 0);
MODULE_PARM_DESC(nokbd, "Do not probe or use KBD port.");

static unsigned int i8042_noaux;
module_param_named(noaux, i8042_noaux, bool, 0);
MODULE_PARM_DESC(noaux, "Do not probe or use AUX (mouse) port.");

static unsigned int i8042_nomux;
module_param_named(nomux, i8042_nomux, bool, 0);
MODULE_PARM_DESC(nomux, "Do not check whether an active multiplexing conrtoller is present.");

static unsigned int i8042_unlock;
module_param_named(unlock, i8042_unlock, bool, 0);
MODULE_PARM_DESC(unlock, "Ignore keyboard lock.");

static unsigned int i8042_reset;
module_param_named(reset, i8042_reset, bool, 0);
MODULE_PARM_DESC(reset, "Reset controller during init and cleanup.");

static unsigned int i8042_direct;
module_param_named(direct, i8042_direct, bool, 0);
MODULE_PARM_DESC(direct, "Put keyboard port into non-translated mode.");

static unsigned int i8042_dumbkbd;
module_param_named(dumbkbd, i8042_dumbkbd, bool, 0);
MODULE_PARM_DESC(dumbkbd, "Pretend that controller can only read data from keyboard");

static unsigned int i8042_noloop;
module_param_named(noloop, i8042_noloop, bool, 0);
MODULE_PARM_DESC(noloop, "Disable the AUX Loopback command while probing for the AUX port");

static unsigned int i8042_blink_frequency = 500;
module_param_named(panicblink, i8042_blink_frequency, uint, 0600);
MODULE_PARM_DESC(panicblink, "Frequency with which keyboard LEDs should blink when kernel panics");

#ifdef CONFIG_X86
static unsigned int i8042_dritek;
module_param_named(dritek, i8042_dritek, bool, 0);
MODULE_PARM_DESC(dritek, "Force enable the Dritek keyboard extension");
#endif

#ifdef CONFIG_PNP
static int i8042_nopnp;
module_param_named(nopnp, i8042_nopnp, bool, 0);
MODULE_PARM_DESC(nopnp, "Do not use PNP to detect controller settings");
#endif

#define DEBUG
#ifdef DEBUG
static int i8042_debug;
module_param_named(debug, i8042_debug, bool, 0600);
MODULE_PARM_DESC(debug, "Turn i8042 debugging mode on and off");
#endif

#include "i8042.h"

static DEFINE_SPINLOCK(i8042_lock);

struct i8042_port {
	struct serio *serio;
	int irq;
	unsigned char exists;
	signed char mux;
};

#define I8042_KBD_PORT_NO	0
#define I8042_AUX_PORT_NO	1
#define I8042_MUX_PORT_NO	2
#define I8042_NUM_PORTS		(I8042_NUM_MUX_PORTS + 2)

static struct i8042_port i8042_ports[I8042_NUM_PORTS];

static unsigned char i8042_initial_ctr;
static unsigned char i8042_ctr;
static unsigned char i8042_mux_present;
static unsigned char i8042_kbd_irq_registered;
static unsigned char i8042_aux_irq_registered;
static unsigned char i8042_suppress_kbd_ack;
static struct platform_device *i8042_platform_device;

static irqreturn_t i8042_interrupt(int irq, void *dev_id);

/*
 * The i8042_wait_read() and i8042_wait_write functions wait for the i8042 to
 * be ready for reading values from it / writing values to it.
 * Called always with i8042_lock held.
 */

static int i8042_wait_read(void)
{
	int i = 0;

	while ((~i8042_read_status() & I8042_STR_OBF) && (i < I8042_CTL_TIMEOUT)) {
		udelay(50);
		i++;
	}
	return -(i == I8042_CTL_TIMEOUT);
}

static int i8042_wait_write(void)
{
	int i = 0;

	while ((i8042_read_status() & I8042_STR_IBF) && (i < I8042_CTL_TIMEOUT)) {
		udelay(50);
		i++;
	}
	return -(i == I8042_CTL_TIMEOUT);
}

/*
 * i8042_flush() flushes all data that may be in the keyboard and mouse buffers
 * of the i8042 down the toilet.
 */

static int i8042_flush(void)
{
	unsigned long flags;
	unsigned char data, str;
	int i = 0;

	spin_lock_irqsave(&i8042_lock, flags);

	while (((str = i8042_read_status()) & I8042_STR_OBF) && (i < I8042_BUFFER_SIZE)) {
		udelay(50);
		data = i8042_read_data();
		i++;
		dbg("%02x <- i8042 (flush, %s)", data,
			str & I8042_STR_AUXDATA ? "aux" : "kbd");
	}

	spin_unlock_irqrestore(&i8042_lock, flags);

	return i;
}

/*
 * i8042_command() executes a command on the i8042. It also sends the input
 * parameter(s) of the commands to it, and receives the output value(s). The
 * parameters are to be stored in the param array, and the output is placed
 * into the same array. The number of the parameters and output values is
 * encoded in bits 8-11 of the command number.
 */

static int __i8042_command(unsigned char *param, int command)
{
	int i, error;

	if (i8042_noloop && command == I8042_CMD_AUX_LOOP)
		return -1;

	error = i8042_wait_write();
	if (error)
		return error;

	dbg("%02x -> i8042 (command)", command & 0xff);
	i8042_write_command(command & 0xff);

	for (i = 0; i < ((command >> 12) & 0xf); i++) {
		error = i8042_wait_write();
		if (error)
			return error;
		dbg("%02x -> i8042 (parameter)", param[i]);
		i8042_write_data(param[i]);
	}

	for (i = 0; i < ((command >> 8) & 0xf); i++) {
		error = i8042_wait_read();
		if (error) {
			dbg("     -- i8042 (timeout)");
			return error;
		}

		if (command == I8042_CMD_AUX_LOOP &&
		    !(i8042_read_status() & I8042_STR_AUXDATA)) {
			dbg("     -- i8042 (auxerr)");
			return -1;
		}

		param[i] = i8042_read_data();
		dbg("%02x <- i8042 (return)", param[i]);
	}

	return 0;
}

int i8042_command(unsigned char *param, int command)
{
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&i8042_lock, flags);
	retval = __i8042_command(param, command);
	spin_unlock_irqrestore(&i8042_lock, flags);

	return retval;
}
EXPORT_SYMBOL(i8042_command);

/*
 * i8042_kbd_write() sends a byte out through the keyboard interface.
 */

static int i8042_kbd_write(struct serio *port, unsigned char c)
{
	unsigned long flags;
	int retval = 0;

	spin_lock_irqsave(&i8042_lock, flags);

	if (!(retval = i8042_wait_write())) {
		dbg("%02x -> i8042 (kbd-data)", c);
		i8042_write_data(c);
	}

	spin_unlock_irqrestore(&i8042_lock, flags);

	return retval;
}

/*
 * i8042_aux_write() sends a byte out through the aux interface.
 */

static int i8042_aux_write(struct serio *serio, unsigned char c)
{
	struct i8042_port *port = serio->port_data;

	return i8042_command(&c, port->mux == -1 ?
					I8042_CMD_AUX_SEND :
					I8042_CMD_MUX_SEND + port->mux);
}

/*
 * i8042_start() is called by serio core when port is about to finish
 * registering. It will mark port as existing so i8042_interrupt can
 * start sending data through it.
 */
static int i8042_start(struct serio *serio)
{
	struct i8042_port *port = serio->port_data;

	port->exists = 1;
	mb();
	return 0;
}

/*
 * i8042_stop() marks serio port as non-existing so i8042_interrupt
 * will not try to send data to the port that is about to go away.
 * The function is called by serio core as part of unregister procedure.
 */
static void i8042_stop(struct serio *serio)
{
	struct i8042_port *port = serio->port_data;

	port->exists = 0;

	/*
	 * We synchronize with both AUX and KBD IRQs because there is
	 * a (very unlikely) chance that AUX IRQ is raised for KBD port
	 * and vice versa.
	 */
	synchronize_irq(I8042_AUX_IRQ);
	synchronize_irq(I8042_KBD_IRQ);
	port->serio = NULL;
}

/*
 * i8042_interrupt() is the most important function in this driver -
 * it handles the interrupts from the i8042, and sends incoming bytes
 * to the upper layers.
 */

static irqreturn_t i8042_interrupt(int irq, void *dev_id)
{
	struct i8042_port *port;
	unsigned long flags;
	unsigned char str, data;
	unsigned int dfl;
	unsigned int port_no;
	int ret = 1;

	spin_lock_irqsave(&i8042_lock, flags);
	str = i8042_read_status();
	if (unlikely(~str & I8042_STR_OBF)) {
		spin_unlock_irqrestore(&i8042_lock, flags);
		if (irq) dbg("Interrupt %d, without any data", irq);
		ret = 0;
		goto out;
	}
	data = i8042_read_data();
	spin_unlock_irqrestore(&i8042_lock, flags);

	if (i8042_mux_present && (str & I8042_STR_AUXDATA)) {
		static unsigned long last_transmit;
		static unsigned char last_str;

		dfl = 0;
		if (str & I8042_STR_MUXERR) {
			dbg("MUX error, status is %02x, data is %02x", str, data);
/*
 * When MUXERR condition is signalled the data register can only contain
 * 0xfd, 0xfe or 0xff if implementation follows the spec. Unfortunately
 * it is not always the case. Some KBCs also report 0xfc when there is
 * nothing connected to the port while others sometimes get confused which
 * port the data came from and signal error leaving the data intact. They
 * _do not_ revert to legacy mode (actually I've never seen KBC reverting
 * to legacy mode yet, when we see one we'll add proper handling).
 * Anyway, we process 0xfc, 0xfd, 0xfe and 0xff as timeouts, and for the
 * rest assume that the data came from the same serio last byte
 * was transmitted (if transmission happened not too long ago).
 */

			switch (data) {
				default:
					if (time_before(jiffies, last_transmit + HZ/10)) {
						str = last_str;
						break;
					}
					/* fall through - report timeout */
				case 0xfc:
				case 0xfd:
				case 0xfe: dfl = SERIO_TIMEOUT; data = 0xfe; break;
				case 0xff: dfl = SERIO_PARITY;  data = 0xfe; break;
			}
		}

		port_no = I8042_MUX_PORT_NO + ((str >> 6) & 3);
		last_str = str;
		last_transmit = jiffies;
	} else {

		dfl = ((str & I8042_STR_PARITY) ? SERIO_PARITY : 0) |
		      ((str & I8042_STR_TIMEOUT) ? SERIO_TIMEOUT : 0);

		port_no = (str & I8042_STR_AUXDATA) ?
				I8042_AUX_PORT_NO : I8042_KBD_PORT_NO;
	}

	port = &i8042_ports[port_no];

	dbg("%02x <- i8042 (interrupt, %d, %d%s%s)",
	    data, port_no, irq,
	    dfl & SERIO_PARITY ? ", bad parity" : "",
	    dfl & SERIO_TIMEOUT ? ", timeout" : "");

	if (unlikely(i8042_suppress_kbd_ack))
		if (port_no == I8042_KBD_PORT_NO &&
		    (data == 0xfa || data == 0xfe)) {
			i8042_suppress_kbd_ack--;
			goto out;
		}

	if (likely(port->exists))
		serio_interrupt(port->serio, data, dfl);

 out:
	return IRQ_RETVAL(ret);
}

/*
 * i8042_enable_kbd_port enables keybaord port on chip
 */

static int i8042_enable_kbd_port(void)
{
	i8042_ctr &= ~I8042_CTR_KBDDIS;
	i8042_ctr |= I8042_CTR_KBDINT;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		i8042_ctr &= ~I8042_CTR_KBDINT;
		i8042_ctr |= I8042_CTR_KBDDIS;
		printk(KERN_ERR "i8042.c: Failed to enable KBD port.\n");
		return -EIO;
	}

	return 0;
}

/*
 * i8042_enable_aux_port enables AUX (mouse) port on chip
 */

static int i8042_enable_aux_port(void)
{
	i8042_ctr &= ~I8042_CTR_AUXDIS;
	i8042_ctr |= I8042_CTR_AUXINT;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		i8042_ctr &= ~I8042_CTR_AUXINT;
		i8042_ctr |= I8042_CTR_AUXDIS;
		printk(KERN_ERR "i8042.c: Failed to enable AUX port.\n");
		return -EIO;
	}

	return 0;
}

/*
 * i8042_enable_mux_ports enables 4 individual AUX ports after
 * the controller has been switched into Multiplexed mode
 */

static int i8042_enable_mux_ports(void)
{
	unsigned char param;
	int i;

	for (i = 0; i < I8042_NUM_MUX_PORTS; i++) {
		i8042_command(&param, I8042_CMD_MUX_PFX + i);
		i8042_command(&param, I8042_CMD_AUX_ENABLE);
	}

	return i8042_enable_aux_port();
}

/*
 * i8042_set_mux_mode checks whether the controller has an active
 * multiplexor and puts the chip into Multiplexed (1) or Legacy (0) mode.
 */

static int i8042_set_mux_mode(unsigned int mode, unsigned char *mux_version)
{

	unsigned char param;
/*
 * Get rid of bytes in the queue.
 */

	i8042_flush();

/*
 * Internal loopback test - send three bytes, they should come back from the
 * mouse interface, the last should be version.
 */

	param = 0xf0;
	if (i8042_command(&param, I8042_CMD_AUX_LOOP) || param != 0xf0)
		return -1;
	param = mode ? 0x56 : 0xf6;
	if (i8042_command(&param, I8042_CMD_AUX_LOOP) || param != (mode ? 0x56 : 0xf6))
		return -1;
	param = mode ? 0xa4 : 0xa5;
	if (i8042_command(&param, I8042_CMD_AUX_LOOP) || param == (mode ? 0xa4 : 0xa5))
		return -1;

	if (mux_version)
		*mux_version = param;

	return 0;
}

/*
 * i8042_check_mux() checks whether the controller supports the PS/2 Active
 * Multiplexing specification by Synaptics, Phoenix, Insyde and
 * LCS/Telegraphics.
 */

static int __devinit i8042_check_mux(void)
{
	unsigned char mux_version;

	if (i8042_set_mux_mode(1, &mux_version))
		return -1;

/*
 * Workaround for interference with USB Legacy emulation
 * that causes a v10.12 MUX to be found.
 */
	if (mux_version == 0xAC)
		return -1;

	printk(KERN_INFO "i8042.c: Detected active multiplexing controller, rev %d.%d.\n",
		(mux_version >> 4) & 0xf, mux_version & 0xf);

/*
 * Disable all muxed ports by disabling AUX.
 */
	i8042_ctr |= I8042_CTR_AUXDIS;
	i8042_ctr &= ~I8042_CTR_AUXINT;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		printk(KERN_ERR "i8042.c: Failed to disable AUX port, can't use MUX.\n");
		return -EIO;
	}

	i8042_mux_present = 1;

	return 0;
}

/*
 * The following is used to test AUX IRQ delivery.
 */
static struct completion i8042_aux_irq_delivered __devinitdata;
static int i8042_irq_being_tested __devinitdata;

static irqreturn_t __devinit i8042_aux_test_irq(int irq, void *dev_id)
{
	unsigned long flags;
	unsigned char str, data;
	int ret = 0;

	spin_lock_irqsave(&i8042_lock, flags);
	str = i8042_read_status();
	if (str & I8042_STR_OBF) {
		data = i8042_read_data();
		if (i8042_irq_being_tested &&
		    data == 0xa5 && (str & I8042_STR_AUXDATA))
			complete(&i8042_aux_irq_delivered);
		ret = 1;
	}
	spin_unlock_irqrestore(&i8042_lock, flags);

	return IRQ_RETVAL(ret);
}

/*
 * i8042_toggle_aux - enables or disables AUX port on i8042 via command and
 * verifies success by readinng CTR. Used when testing for presence of AUX
 * port.
 */
static int __devinit i8042_toggle_aux(int on)
{
	unsigned char param;
	int i;

	if (i8042_command(&param,
			on ? I8042_CMD_AUX_ENABLE : I8042_CMD_AUX_DISABLE))
		return -1;

	/* some chips need some time to set the I8042_CTR_AUXDIS bit */
	for (i = 0; i < 100; i++) {
		udelay(50);

		if (i8042_command(&param, I8042_CMD_CTL_RCTR))
			return -1;

		if (!(param & I8042_CTR_AUXDIS) == on)
			return 0;
	}

	return -1;
}

/*
 * i8042_check_aux() applies as much paranoia as it can at detecting
 * the presence of an AUX interface.
 */

static int __devinit i8042_check_aux(void)
{
	int retval = -1;
	int irq_registered = 0;
	int aux_loop_broken = 0;
	unsigned long flags;
	unsigned char param;

/*
 * Get rid of bytes in the queue.
 */

	i8042_flush();

/*
 * Internal loopback test - filters out AT-type i8042's. Unfortunately
 * SiS screwed up and their 5597 doesn't support the LOOP command even
 * though it has an AUX port.
 */

	param = 0x5a;
	retval = i8042_command(&param, I8042_CMD_AUX_LOOP);
	if (retval || param != 0x5a) {

/*
 * External connection test - filters out AT-soldered PS/2 i8042's
 * 0x00 - no error, 0x01-0x03 - clock/data stuck, 0xff - general error
 * 0xfa - no error on some notebooks which ignore the spec
 * Because it's common for chipsets to return error on perfectly functioning
 * AUX ports, we test for this only when the LOOP command failed.
 */

		if (i8042_command(&param, I8042_CMD_AUX_TEST) ||
		    (param && param != 0xfa && param != 0xff))
			return -1;

/*
 * If AUX_LOOP completed without error but returned unexpected data
 * mark it as broken
 */
		if (!retval)
			aux_loop_broken = 1;
	}

/*
 * Bit assignment test - filters out PS/2 i8042's in AT mode
 */

	if (i8042_toggle_aux(0)) {
		printk(KERN_WARNING "Failed to disable AUX port, but continuing anyway... Is this a SiS?\n");
		printk(KERN_WARNING "If AUX port is really absent please use the 'i8042.noaux' option.\n");
	}

	if (i8042_toggle_aux(1))
		return -1;

/*
 * Test AUX IRQ delivery to make sure BIOS did not grab the IRQ and
 * used it for a PCI card or somethig else.
 */

	if (i8042_noloop || aux_loop_broken) {
/*
 * Without LOOP command we can't test AUX IRQ delivery. Assume the port
 * is working and hope we are right.
 */
		retval = 0;
		goto out;
	}

	if (request_irq(I8042_AUX_IRQ, i8042_aux_test_irq, IRQF_SHARED,
			"i8042", i8042_platform_device))
		goto out;

	irq_registered = 1;

	if (i8042_enable_aux_port())
		goto out;

	spin_lock_irqsave(&i8042_lock, flags);

	init_completion(&i8042_aux_irq_delivered);
	i8042_irq_being_tested = 1;

	param = 0xa5;
	retval = __i8042_command(&param, I8042_CMD_AUX_LOOP & 0xf0ff);

	spin_unlock_irqrestore(&i8042_lock, flags);

	if (retval)
		goto out;

	if (wait_for_completion_timeout(&i8042_aux_irq_delivered,
					msecs_to_jiffies(250)) == 0) {
/*
 * AUX IRQ was never delivered so we need to flush the controller to
 * get rid of the byte we put there; otherwise keyboard may not work.
 */
		i8042_flush();
		retval = -1;
	}

 out:

/*
 * Disable the interface.
 */

	i8042_ctr |= I8042_CTR_AUXDIS;
	i8042_ctr &= ~I8042_CTR_AUXINT;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR))
		retval = -1;

	if (irq_registered)
		free_irq(I8042_AUX_IRQ, i8042_platform_device);

	return retval;
}

static int i8042_controller_check(void)
{
	if (i8042_flush() == I8042_BUFFER_SIZE) {
		printk(KERN_ERR "i8042.c: No controller found.\n");
		return -ENODEV;
	}

	return 0;
}

static int i8042_controller_selftest(void)
{
	unsigned char param;

	if (!i8042_reset)
		return 0;

	if (i8042_command(&param, I8042_CMD_CTL_TEST)) {
		printk(KERN_ERR "i8042.c: i8042 controller self test timeout.\n");
		return -ENODEV;
	}

	if (param != I8042_RET_CTL_TEST) {
		printk(KERN_ERR "i8042.c: i8042 controller selftest failed. (%#x != %#x)\n",
			 param, I8042_RET_CTL_TEST);
		return -EIO;
	}

	return 0;
}

/*
 * i8042_controller init initializes the i8042 controller, and,
 * most importantly, sets it into non-xlated mode if that's
 * desired.
 */

static int i8042_controller_init(void)
{
	unsigned long flags;

/*
 * Save the CTR for restoral on unload / reboot.
 */

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_RCTR)) {
		printk(KERN_ERR "i8042.c: Can't read CTR while initializing i8042.\n");
		return -EIO;
	}

	i8042_initial_ctr = i8042_ctr;

/*
 * Disable the keyboard interface and interrupt.
 */

	i8042_ctr |= I8042_CTR_KBDDIS;
	i8042_ctr &= ~I8042_CTR_KBDINT;

/*
 * Handle keylock.
 */

	spin_lock_irqsave(&i8042_lock, flags);
	if (~i8042_read_status() & I8042_STR_KEYLOCK) {
		if (i8042_unlock)
			i8042_ctr |= I8042_CTR_IGNKEYLOCK;
		else
			printk(KERN_WARNING "i8042.c: Warning: Keylock active.\n");
	}
	spin_unlock_irqrestore(&i8042_lock, flags);

/*
 * If the chip is configured into nontranslated mode by the BIOS, don't
 * bother enabling translating and be happy.
 */

	if (~i8042_ctr & I8042_CTR_XLATE)
		i8042_direct = 1;

/*
 * Set nontranslated mode for the kbd interface if requested by an option.
 * After this the kbd interface becomes a simple serial in/out, like the aux
 * interface is. We don't do this by default, since it can confuse notebook
 * BIOSes.
 */

	if (i8042_direct)
		i8042_ctr &= ~I8042_CTR_XLATE;

/*
 * Write CTR back.
 */

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		printk(KERN_ERR "i8042.c: Can't write CTR while initializing i8042.\n");
		return -EIO;
	}

	return 0;
}


/*
 * Reset the controller and reset CRT to the original value set by BIOS.
 */

static void i8042_controller_reset(void)
{
	i8042_flush();

/*
 * Disable both KBD and AUX interfaces so they don't get in the way
 */

	i8042_ctr |= I8042_CTR_KBDDIS | I8042_CTR_AUXDIS;
	i8042_ctr &= ~(I8042_CTR_KBDINT | I8042_CTR_AUXINT);

/*
 * Disable MUX mode if present.
 */

	if (i8042_mux_present)
		i8042_set_mux_mode(0, NULL);

/*
 * Reset the controller if requested.
 */

	i8042_controller_selftest();

/*
 * Restore the original control register setting.
 */

	if (i8042_command(&i8042_initial_ctr, I8042_CMD_CTL_WCTR))
		printk(KERN_WARNING "i8042.c: Can't restore CTR.\n");
}


/*
 * i8042_panic_blink() will flash the keyboard LEDs and is called when
 * kernel panics. Flashing LEDs is useful for users running X who may
 * not see the console and will help distingushing panics from "real"
 * lockups.
 *
 * Note that DELAY has a limit of 10ms so we will not get stuck here
 * waiting for KBC to free up even if KBD interrupt is off
 */

#define DELAY do { mdelay(1); if (++delay > 10) return delay; } while(0)

static long i8042_panic_blink(long count)
{
	long delay = 0;
	static long last_blink;
	static char led;

	/*
	 * We expect frequency to be about 1/2s. KDB uses about 1s.
	 * Make sure they are different.
	 */
	if (!i8042_blink_frequency)
		return 0;
	if (count - last_blink < i8042_blink_frequency)
		return 0;

	led ^= 0x01 | 0x04;
	while (i8042_read_status() & I8042_STR_IBF)
		DELAY;
	dbg("%02x -> i8042 (panic blink)", 0xed);
	i8042_suppress_kbd_ack = 2;
	i8042_write_data(0xed); /* set leds */
	DELAY;
	while (i8042_read_status() & I8042_STR_IBF)
		DELAY;
	DELAY;
	dbg("%02x -> i8042 (panic blink)", led);
	i8042_write_data(led);
	DELAY;
	last_blink = count;
	return delay;
}

#undef DELAY

#ifdef CONFIG_PM
/*
 * Here we try to restore the original BIOS settings. We only want to
 * do that once, when we really suspend, not when we taking memory
 * snapshot for swsusp (in this case we'll perform required cleanup
 * as part of shutdown process).
 */

static int i8042_suspend(struct platform_device *dev, pm_message_t state)
{
	if (dev->dev.power.power_state.event != state.event) {
		if (state.event == PM_EVENT_SUSPEND)
			i8042_controller_reset();

		dev->dev.power.power_state = state;
	}

	return 0;
}


/*
 * Here we try to reset everything back to a state in which suspended
 */

static int i8042_resume(struct platform_device *dev)
{
	int error;

/*
 * Do not bother with restoring state if we haven't suspened yet
 */
	if (dev->dev.power.power_state.event == PM_EVENT_ON)
		return 0;

	error = i8042_controller_check();
	if (error)
		return error;

	error = i8042_controller_selftest();
	if (error)
		return error;

/*
 * Restore original CTR value and disable all ports
 */

	i8042_ctr = i8042_initial_ctr;
	if (i8042_direct)
		i8042_ctr &= ~I8042_CTR_XLATE;
	i8042_ctr |= I8042_CTR_AUXDIS | I8042_CTR_KBDDIS;
	i8042_ctr &= ~(I8042_CTR_AUXINT | I8042_CTR_KBDINT);
	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		printk(KERN_ERR "i8042: Can't write CTR to resume\n");
		return -EIO;
	}

	if (i8042_mux_present) {
		if (i8042_set_mux_mode(1, NULL) || i8042_enable_mux_ports())
			printk(KERN_WARNING
				"i8042: failed to resume active multiplexor, "
				"mouse won't work.\n");
	} else if (i8042_ports[I8042_AUX_PORT_NO].serio)
		i8042_enable_aux_port();

	if (i8042_ports[I8042_KBD_PORT_NO].serio)
		i8042_enable_kbd_port();

	i8042_interrupt(0, NULL);

	dev->dev.power.power_state = PMSG_ON;

	return 0;
}
#endif /* CONFIG_PM */

/*
 * We need to reset the 8042 back to original mode on system shutdown,
 * because otherwise BIOSes will be confused.
 */

static void i8042_shutdown(struct platform_device *dev)
{
	i8042_controller_reset();
}

static int __devinit i8042_create_kbd_port(void)
{
	struct serio *serio;
	struct i8042_port *port = &i8042_ports[I8042_KBD_PORT_NO];

	serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!serio)
		return -ENOMEM;

	serio->id.type		= i8042_direct ? SERIO_8042 : SERIO_8042_XL;
	serio->write		= i8042_dumbkbd ? NULL : i8042_kbd_write;
	serio->start		= i8042_start;
	serio->stop		= i8042_stop;
	serio->port_data	= port;
	serio->dev.parent	= &i8042_platform_device->dev;
	strlcpy(serio->name, "i8042 KBD port", sizeof(serio->name));
	strlcpy(serio->phys, I8042_KBD_PHYS_DESC, sizeof(serio->phys));

	port->serio = serio;
	port->irq = I8042_KBD_IRQ;

	return 0;
}

static int __devinit i8042_create_aux_port(int idx)
{
	struct serio *serio;
	int port_no = idx < 0 ? I8042_AUX_PORT_NO : I8042_MUX_PORT_NO + idx;
	struct i8042_port *port = &i8042_ports[port_no];

	serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!serio)
		return -ENOMEM;

	serio->id.type		= SERIO_8042;
	serio->write		= i8042_aux_write;
	serio->start		= i8042_start;
	serio->stop		= i8042_stop;
	serio->port_data	= port;
	serio->dev.parent	= &i8042_platform_device->dev;
	if (idx < 0) {
		strlcpy(serio->name, "i8042 AUX port", sizeof(serio->name));
		strlcpy(serio->phys, I8042_AUX_PHYS_DESC, sizeof(serio->phys));
	} else {
		snprintf(serio->name, sizeof(serio->name), "i8042 AUX%d port", idx);
		snprintf(serio->phys, sizeof(serio->phys), I8042_MUX_PHYS_DESC, idx + 1);
	}

	port->serio = serio;
	port->mux = idx;
	port->irq = I8042_AUX_IRQ;

	return 0;
}

static void __devinit i8042_free_kbd_port(void)
{
	kfree(i8042_ports[I8042_KBD_PORT_NO].serio);
	i8042_ports[I8042_KBD_PORT_NO].serio = NULL;
}

static void __devinit i8042_free_aux_ports(void)
{
	int i;

	for (i = I8042_AUX_PORT_NO; i < I8042_NUM_PORTS; i++) {
		kfree(i8042_ports[i].serio);
		i8042_ports[i].serio = NULL;
	}
}

static void __devinit i8042_register_ports(void)
{
	int i;

	for (i = 0; i < I8042_NUM_PORTS; i++) {
		if (i8042_ports[i].serio) {
			printk(KERN_INFO "serio: %s at %#lx,%#lx irq %d\n",
				i8042_ports[i].serio->name,
				(unsigned long) I8042_DATA_REG,
				(unsigned long) I8042_COMMAND_REG,
				i8042_ports[i].irq);
			serio_register_port(i8042_ports[i].serio);
		}
	}
}

static void __devexit i8042_unregister_ports(void)
{
	int i;

	for (i = 0; i < I8042_NUM_PORTS; i++) {
		if (i8042_ports[i].serio) {
			serio_unregister_port(i8042_ports[i].serio);
			i8042_ports[i].serio = NULL;
		}
	}
}

static void i8042_free_irqs(void)
{
	if (i8042_aux_irq_registered)
		free_irq(I8042_AUX_IRQ, i8042_platform_device);
	if (i8042_kbd_irq_registered)
		free_irq(I8042_KBD_IRQ, i8042_platform_device);

	i8042_aux_irq_registered = i8042_kbd_irq_registered = 0;
}

static int __devinit i8042_setup_aux(void)
{
	int (*aux_enable)(void);
	int error;
	int i;

	if (i8042_check_aux())
		return -ENODEV;

	if (i8042_nomux || i8042_check_mux()) {
		error = i8042_create_aux_port(-1);
		if (error)
			goto err_free_ports;
		aux_enable = i8042_enable_aux_port;
	} else {
		for (i = 0; i < I8042_NUM_MUX_PORTS; i++) {
			error = i8042_create_aux_port(i);
			if (error)
				goto err_free_ports;
		}
		aux_enable = i8042_enable_mux_ports;
	}

	error = request_irq(I8042_AUX_IRQ, i8042_interrupt, IRQF_SHARED,
			    "i8042", i8042_platform_device);
	if (error)
		goto err_free_ports;

	if (aux_enable())
		goto err_free_irq;

	i8042_aux_irq_registered = 1;
	return 0;

 err_free_irq:
	free_irq(I8042_AUX_IRQ, i8042_platform_device);
 err_free_ports:
	i8042_free_aux_ports();
	return error;
}

static int __devinit i8042_setup_kbd(void)
{
	int error;

	error = i8042_create_kbd_port();
	if (error)
		return error;

	error = request_irq(I8042_KBD_IRQ, i8042_interrupt, IRQF_SHARED,
			    "i8042", i8042_platform_device);
	if (error)
		goto err_free_port;

	error = i8042_enable_kbd_port();
	if (error)
		goto err_free_irq;

	i8042_kbd_irq_registered = 1;
	return 0;

 err_free_irq:
	free_irq(I8042_KBD_IRQ, i8042_platform_device);
 err_free_port:
	i8042_free_kbd_port();
	return error;
}

static int __devinit i8042_probe(struct platform_device *dev)
{
	int error;
	char param;

	error = i8042_controller_selftest();
	if (error)
		return error;

	error = i8042_controller_init();
	if (error)
		return error;

	if (!i8042_noaux) {
		error = i8042_setup_aux();
		if (error && error != -ENODEV && error != -EBUSY)
			goto out_fail;
	}

	if (!i8042_nokbd) {
		error = i8042_setup_kbd();
		if (error)
			goto out_fail;
	}
#ifdef CONFIG_X86
	if (i8042_dritek) {
		param = 0x90;
		error = i8042_command(&param, 0x1059);
		if (error)
			goto out_fail;
	}
#endif
/*
 * Ok, everything is ready, let's register all serio ports
 */
	i8042_register_ports();

	return 0;

 out_fail:
	i8042_free_aux_ports();	/* in case KBD failed but AUX not */
	i8042_free_irqs();
	i8042_controller_reset();

	return error;
}

static int __devexit i8042_remove(struct platform_device *dev)
{
	i8042_unregister_ports();
	i8042_free_irqs();
	i8042_controller_reset();

	return 0;
}

static struct platform_driver i8042_driver = {
	.driver		= {
		.name	= "i8042",
		.owner	= THIS_MODULE,
	},
	.probe		= i8042_probe,
	.remove		= __devexit_p(i8042_remove),
	.shutdown	= i8042_shutdown,
#ifdef CONFIG_PM
	.suspend	= i8042_suspend,
	.resume		= i8042_resume,
#endif
};

static int __init i8042_init(void)
{
	int err;

	dbg_init();

	err = i8042_platform_init();
	if (err)
		return err;

	err = i8042_controller_check();
	if (err)
		goto err_platform_exit;

	err = platform_driver_register(&i8042_driver);
	if (err)
		goto err_platform_exit;

	i8042_platform_device = platform_device_alloc("i8042", -1);
	if (!i8042_platform_device) {
		err = -ENOMEM;
		goto err_unregister_driver;
	}

	err = platform_device_add(i8042_platform_device);
	if (err)
		goto err_free_device;

	panic_blink = i8042_panic_blink;

	return 0;

 err_free_device:
	platform_device_put(i8042_platform_device);
 err_unregister_driver:
	platform_driver_unregister(&i8042_driver);
 err_platform_exit:
	i8042_platform_exit();

	return err;
}

static void __exit i8042_exit(void)
{
	platform_device_unregister(i8042_platform_device);
	platform_driver_unregister(&i8042_driver);
	i8042_platform_exit();

	panic_blink = NULL;
}

module_init(i8042_init);
module_exit(i8042_exit);
