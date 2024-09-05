// SPDX-License-Identifier: GPL-2.0-only
/*
 * PS/2 driver library
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 * Copyright (c) 2004 Dmitry Torokhov
 */


#include <linux/delay.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/kmsan-checks.h>
#include <linux/serio.h>
#include <linux/i8042.h>
#include <linux/libps2.h>

#define DRIVER_DESC	"PS/2 driver library"

#define PS2_CMD_SETSCALE11	0x00e6
#define PS2_CMD_SETRES		0x10e8
#define PS2_CMD_EX_SETLEDS	0x20eb
#define PS2_CMD_SETLEDS		0x10ed
#define PS2_CMD_GETID		0x02f2
#define PS2_CMD_SETREP		0x10f3 /* Set repeat rate/set report rate */
#define PS2_CMD_RESET_BAT	0x02ff

#define PS2_RET_BAT		0xaa
#define PS2_RET_ID		0x00
#define PS2_RET_ACK		0xfa
#define PS2_RET_NAK		0xfe
#define PS2_RET_ERR		0xfc

#define PS2_FLAG_ACK		BIT(0)	/* Waiting for ACK/NAK */
#define PS2_FLAG_CMD		BIT(1)	/* Waiting for a command to finish */
#define PS2_FLAG_CMD1		BIT(2)	/* Waiting for the first byte of command response */
#define PS2_FLAG_WAITID		BIT(3)	/* Command executing is GET ID */
#define PS2_FLAG_NAK		BIT(4)	/* Last transmission was NAKed */
#define PS2_FLAG_PASS_NOACK	BIT(5)	/* Pass non-ACK byte to receive handler */

static int ps2_do_sendbyte(struct ps2dev *ps2dev, u8 byte,
			   unsigned int timeout, unsigned int max_attempts)
	__releases(&ps2dev->serio->lock) __acquires(&ps2dev->serio->lock)
{
	int attempt = 0;
	int error;

	lockdep_assert_held(&ps2dev->serio->lock);

	do {
		ps2dev->nak = 1;
		ps2dev->flags |= PS2_FLAG_ACK;

		serio_continue_rx(ps2dev->serio);

		error = serio_write(ps2dev->serio, byte);
		if (error)
			dev_dbg(&ps2dev->serio->dev,
				"failed to write %#02x: %d\n", byte, error);
		else
			wait_event_timeout(ps2dev->wait,
					   !(ps2dev->flags & PS2_FLAG_ACK),
					   msecs_to_jiffies(timeout));

		serio_pause_rx(ps2dev->serio);
	} while (ps2dev->nak == PS2_RET_NAK && ++attempt < max_attempts);

	ps2dev->flags &= ~PS2_FLAG_ACK;

	if (!error) {
		switch (ps2dev->nak) {
		case 0:
			break;
		case PS2_RET_NAK:
			error = -EAGAIN;
			break;
		case PS2_RET_ERR:
			error = -EPROTO;
			break;
		default:
			error = -EIO;
			break;
		}
	}

	if (error || attempt > 1)
		dev_dbg(&ps2dev->serio->dev,
			"%02x - %d (%x), attempt %d\n",
			byte, error, ps2dev->nak, attempt);

	return error;
}

/**
 * ps2_sendbyte - sends a byte to the device and wait for acknowledgement
 * @ps2dev: a PS/2 device to send the data to
 * @byte: data to be sent to the device
 * @timeout: timeout for sending the data and receiving an acknowledge
 *
 * The function doesn't handle retransmission, the caller is expected to handle
 * it when needed.
 *
 * ps2_sendbyte() can only be called from a process context.
 */
int ps2_sendbyte(struct ps2dev *ps2dev, u8 byte, unsigned int timeout)
{
	int retval;

	guard(serio_pause_rx)(ps2dev->serio);

	retval = ps2_do_sendbyte(ps2dev, byte, timeout, 1);
	dev_dbg(&ps2dev->serio->dev, "%02x - %x\n", byte, ps2dev->nak);

	return retval;
}
EXPORT_SYMBOL(ps2_sendbyte);

/**
 * ps2_begin_command - mark beginning of execution of a complex command
 * @ps2dev: a PS/2 device executing the command
 *
 * Serializes a complex/compound command. Once command is finished
 * ps2_end_command() should be called.
 */
void ps2_begin_command(struct ps2dev *ps2dev)
{
	struct mutex *m = ps2dev->serio->ps2_cmd_mutex ?: &ps2dev->cmd_mutex;

	mutex_lock(m);
}
EXPORT_SYMBOL(ps2_begin_command);

/**
 * ps2_end_command - mark end of execution of a complex command
 * @ps2dev: a PS/2 device executing the command
 */
void ps2_end_command(struct ps2dev *ps2dev)
{
	struct mutex *m = ps2dev->serio->ps2_cmd_mutex ?: &ps2dev->cmd_mutex;

	mutex_unlock(m);
}
EXPORT_SYMBOL(ps2_end_command);

/**
 * ps2_drain - waits for device to transmit requested number of bytes
 * and discards them
 * @ps2dev: the PS/2 device that should be drained
 * @maxbytes: maximum number of bytes to be drained
 * @timeout: time to drain the device
 */
void ps2_drain(struct ps2dev *ps2dev, size_t maxbytes, unsigned int timeout)
{
	if (maxbytes > sizeof(ps2dev->cmdbuf)) {
		WARN_ON(1);
		maxbytes = sizeof(ps2dev->cmdbuf);
	}

	ps2_begin_command(ps2dev);

	scoped_guard(serio_pause_rx, ps2dev->serio) {
		ps2dev->flags = PS2_FLAG_CMD;
		ps2dev->cmdcnt = maxbytes;
	}

	wait_event_timeout(ps2dev->wait,
			   !(ps2dev->flags & PS2_FLAG_CMD),
			   msecs_to_jiffies(timeout));

	ps2_end_command(ps2dev);
}
EXPORT_SYMBOL(ps2_drain);

/**
 * ps2_is_keyboard_id - checks received ID byte against the list of
 *   known keyboard IDs
 * @id_byte: data byte that should be checked
 */
bool ps2_is_keyboard_id(u8 id_byte)
{
	static const u8 keyboard_ids[] = {
		0xab,	/* Regular keyboards		*/
		0xac,	/* NCD Sun keyboard		*/
		0x2b,	/* Trust keyboard, translated	*/
		0x5d,	/* Trust keyboard		*/
		0x60,	/* NMB SGI keyboard, translated */
		0x47,	/* NMB SGI keyboard		*/
	};

	return memchr(keyboard_ids, id_byte, sizeof(keyboard_ids)) != NULL;
}
EXPORT_SYMBOL(ps2_is_keyboard_id);

/*
 * ps2_adjust_timeout() is called after receiving 1st byte of command
 * response and tries to reduce remaining timeout to speed up command
 * completion.
 */
static int ps2_adjust_timeout(struct ps2dev *ps2dev,
			      unsigned int command, unsigned int timeout)
{
	switch (command) {
	case PS2_CMD_RESET_BAT:
		/*
		 * Device has sent the first response byte after
		 * reset command, reset is thus done, so we can
		 * shorten the timeout.
		 * The next byte will come soon (keyboard) or not
		 * at all (mouse).
		 */
		if (timeout > msecs_to_jiffies(100))
			timeout = msecs_to_jiffies(100);
		break;

	case PS2_CMD_GETID:
		/*
		 * Microsoft Natural Elite keyboard responds to
		 * the GET ID command as it were a mouse, with
		 * a single byte. Fail the command so atkbd will
		 * use alternative probe to detect it.
		 */
		if (ps2dev->cmdbuf[1] == 0xaa) {
			scoped_guard(serio_pause_rx, ps2dev->serio)
				ps2dev->flags = 0;

			timeout = 0;
		}

		/*
		 * If device behind the port is not a keyboard there
		 * won't be 2nd byte of ID response.
		 */
		if (!ps2_is_keyboard_id(ps2dev->cmdbuf[1])) {
			scoped_guard(serio_pause_rx, ps2dev->serio)
				ps2dev->flags = ps2dev->cmdcnt = 0;

			timeout = 0;
		}
		break;

	default:
		break;
	}

	return timeout;
}

/**
 * __ps2_command - send a command to PS/2 device
 * @ps2dev: the PS/2 device that should execute the command
 * @param: a buffer containing parameters to be sent along with the command,
 *   or place where the results of the command execution will be deposited,
 *   or both
 * @command: command word that encodes the command itself, as well as number of
 *   additional parameter bytes that should be sent to the device and expected
 *   length of the command response
 *
 * Not serialized. Callers should use ps2_begin_command() and ps2_end_command()
 * to ensure proper serialization for complex commands.
 */
int __ps2_command(struct ps2dev *ps2dev, u8 *param, unsigned int command)
{
	unsigned int timeout;
	unsigned int send = (command >> 12) & 0xf;
	unsigned int receive = (command >> 8) & 0xf;
	int rc;
	int i;
	u8 send_param[16];

	if (receive > sizeof(ps2dev->cmdbuf)) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (send && !param) {
		WARN_ON(1);
		return -EINVAL;
	}

	memcpy(send_param, param, send);

	/*
	 * Not using guard notation because we need to break critical
	 * section below while waiting for the response.
	 */
	serio_pause_rx(ps2dev->serio);

	ps2dev->cmdcnt = receive;

	switch (command) {
	case PS2_CMD_GETID:
		/*
		 * Some mice do not ACK the "get ID" command, prepare to
		 * handle this.
		 */
		ps2dev->flags = PS2_FLAG_WAITID;
		break;

	case PS2_CMD_SETLEDS:
	case PS2_CMD_EX_SETLEDS:
	case PS2_CMD_SETREP:
		ps2dev->flags = PS2_FLAG_PASS_NOACK;
		break;

	default:
		ps2dev->flags = 0;
		break;
	}

	if (receive) {
		/* Indicate that we expect response to the command. */
		ps2dev->flags |= PS2_FLAG_CMD | PS2_FLAG_CMD1;
		if (param)
			for (i = 0; i < receive; i++)
				ps2dev->cmdbuf[(receive - 1) - i] = param[i];
	}

	/*
	 * Some devices (Synaptics) perform the reset before
	 * ACKing the reset command, and so it can take a long
	 * time before the ACK arrives.
	 */
	timeout = command == PS2_CMD_RESET_BAT ? 1000 : 200;

	rc = ps2_do_sendbyte(ps2dev, command & 0xff, timeout, 2);
	if (rc)
		goto out_reset_flags;

	/* Send command parameters, if any. */
	for (i = 0; i < send; i++) {
		rc = ps2_do_sendbyte(ps2dev, param[i], 200, 2);
		if (rc)
			goto out_reset_flags;
	}

	serio_continue_rx(ps2dev->serio);

	/*
	 * The reset command takes a long time to execute.
	 */
	timeout = msecs_to_jiffies(command == PS2_CMD_RESET_BAT ? 4000 : 500);

	timeout = wait_event_timeout(ps2dev->wait,
				     !(ps2dev->flags & PS2_FLAG_CMD1), timeout);

	if (ps2dev->cmdcnt && !(ps2dev->flags & PS2_FLAG_CMD1)) {

		timeout = ps2_adjust_timeout(ps2dev, command, timeout);
		wait_event_timeout(ps2dev->wait,
				   !(ps2dev->flags & PS2_FLAG_CMD), timeout);
	}

	serio_pause_rx(ps2dev->serio);

	if (param) {
		for (i = 0; i < receive; i++)
			param[i] = ps2dev->cmdbuf[(receive - 1) - i];
		kmsan_unpoison_memory(param, receive);
	}

	if (ps2dev->cmdcnt &&
	    (command != PS2_CMD_RESET_BAT || ps2dev->cmdcnt != 1)) {
		rc = -EPROTO;
		goto out_reset_flags;
	}

	rc = 0;

 out_reset_flags:
	ps2dev->flags = 0;
	serio_continue_rx(ps2dev->serio);

	dev_dbg(&ps2dev->serio->dev,
		"%02x [%*ph] - %x/%08lx [%*ph]\n",
		command & 0xff, send, send_param,
		ps2dev->nak, ps2dev->flags,
		receive, param ?: send_param);

	/*
	 * ps_command() handles resends itself, so do not leak -EAGAIN
	 * to the callers.
	 */
	return rc != -EAGAIN ? rc : -EPROTO;
}
EXPORT_SYMBOL(__ps2_command);

/**
 * ps2_command - send a command to PS/2 device
 * @ps2dev: the PS/2 device that should execute the command
 * @param: a buffer containing parameters to be sent along with the command,
 *   or place where the results of the command execution will be deposited,
 *   or both
 * @command: command word that encodes the command itself, as well as number of
 *   additional parameter bytes that should be sent to the device and expected
 *   length of the command response
 *
 * Note: ps2_command() serializes the command execution so that only one
 * command can be executed at a time for either individual port or the entire
 * 8042 controller.
 */
int ps2_command(struct ps2dev *ps2dev, u8 *param, unsigned int command)
{
	int rc;

	ps2_begin_command(ps2dev);
	rc = __ps2_command(ps2dev, param, command);
	ps2_end_command(ps2dev);

	return rc;
}
EXPORT_SYMBOL(ps2_command);

/**
 * ps2_sliced_command - sends an extended PS/2 command to a mouse
 * @ps2dev: the PS/2 device that should execute the command
 * @command: command byte
 *
 * The command is sent using "sliced" syntax understood by advanced devices,
 * such as Logitech or Synaptics touchpads. The command is encoded as:
 * 0xE6 0xE8 rr 0xE8 ss 0xE8 tt 0xE8 uu where (rr*64)+(ss*16)+(tt*4)+uu
 * is the command.
 */
int ps2_sliced_command(struct ps2dev *ps2dev, u8 command)
{
	int i;
	int retval;

	ps2_begin_command(ps2dev);

	retval = __ps2_command(ps2dev, NULL, PS2_CMD_SETSCALE11);
	if (retval)
		goto out;

	for (i = 6; i >= 0; i -= 2) {
		u8 d = (command >> i) & 3;
		retval = __ps2_command(ps2dev, &d, PS2_CMD_SETRES);
		if (retval)
			break;
	}

out:
	dev_dbg(&ps2dev->serio->dev, "%02x - %d\n", command, retval);
	ps2_end_command(ps2dev);
	return retval;
}
EXPORT_SYMBOL(ps2_sliced_command);

/**
 * ps2_init - initializes ps2dev structure
 * @ps2dev: structure to be initialized
 * @serio: serio port associated with the PS/2 device
 * @pre_receive_handler: validation handler to check basic communication state
 * @receive_handler: main protocol handler
 *
 * Prepares ps2dev structure for use in drivers for PS/2 devices.
 */
void ps2_init(struct ps2dev *ps2dev, struct serio *serio,
	      ps2_pre_receive_handler_t pre_receive_handler,
	      ps2_receive_handler_t receive_handler)
{
	ps2dev->pre_receive_handler = pre_receive_handler;
	ps2dev->receive_handler = receive_handler;

	mutex_init(&ps2dev->cmd_mutex);
	lockdep_set_subclass(&ps2dev->cmd_mutex, serio->depth);
	init_waitqueue_head(&ps2dev->wait);
	ps2dev->serio = serio;
	serio_set_drvdata(serio, ps2dev);
}
EXPORT_SYMBOL(ps2_init);

/*
 * ps2_handle_response() stores device's response to a command and notifies
 * the process waiting for completion of the command. Note that there is a
 * distinction between waiting for the first byte of the response, and
 * waiting for subsequent bytes. It is done so that callers could shorten
 * timeouts once first byte of response is received.
 */
static void ps2_handle_response(struct ps2dev *ps2dev, u8 data)
{
	if (ps2dev->cmdcnt)
		ps2dev->cmdbuf[--ps2dev->cmdcnt] = data;

	if (ps2dev->flags & PS2_FLAG_CMD1) {
		ps2dev->flags &= ~PS2_FLAG_CMD1;
		if (ps2dev->cmdcnt)
			wake_up(&ps2dev->wait);
	}

	if (!ps2dev->cmdcnt) {
		ps2dev->flags &= ~PS2_FLAG_CMD;
		wake_up(&ps2dev->wait);
	}
}

/*
 * ps2_handle_ack() processes ACK/NAK of a command from a PS/2 device,
 * possibly applying workarounds for mice not acknowledging the "get ID"
 * command.
 */
static void ps2_handle_ack(struct ps2dev *ps2dev, u8 data)
{
	switch (data) {
	case PS2_RET_ACK:
		ps2dev->nak = 0;
		break;

	case PS2_RET_NAK:
		ps2dev->flags |= PS2_FLAG_NAK;
		ps2dev->nak = PS2_RET_NAK;
		break;

	case PS2_RET_ERR:
		if (ps2dev->flags & PS2_FLAG_NAK) {
			ps2dev->flags &= ~PS2_FLAG_NAK;
			ps2dev->nak = PS2_RET_ERR;
			break;
		}
		fallthrough;

	/*
	 * Workaround for mice which don't ACK the Get ID command.
	 * These are valid mouse IDs that we recognize.
	 */
	case 0x00:
	case 0x03:
	case 0x04:
		if (ps2dev->flags & PS2_FLAG_WAITID) {
			ps2dev->nak = 0;
			break;
		}
		fallthrough;
	default:
		/*
		 * Do not signal errors if we get unexpected reply while
		 * waiting for an ACK to the initial (first) command byte:
		 * the device might not be quiesced yet and continue
		 * delivering data. For certain commands (such as set leds and
		 * set repeat rate) that can be used during normal device
		 * operation, we even pass this data byte to the normal receive
		 * handler.
		 * Note that we reset PS2_FLAG_WAITID flag, so the workaround
		 * for mice not acknowledging the Get ID command only triggers
		 * on the 1st byte; if device spews data we really want to see
		 * a real ACK from it.
		 */
		dev_dbg(&ps2dev->serio->dev, "unexpected %#02x\n", data);
		if (ps2dev->flags & PS2_FLAG_PASS_NOACK)
			ps2dev->receive_handler(ps2dev, data);
		ps2dev->flags &= ~(PS2_FLAG_WAITID | PS2_FLAG_PASS_NOACK);
		return;
	}

	if (!ps2dev->nak)
		ps2dev->flags &= ~PS2_FLAG_NAK;

	ps2dev->flags &= ~PS2_FLAG_ACK;

	if (!ps2dev->nak && data != PS2_RET_ACK)
		ps2_handle_response(ps2dev, data);
	else
		wake_up(&ps2dev->wait);
}

/*
 * Clears state of PS/2 device after communication error by resetting majority
 * of flags and waking up waiters, if any.
 */
static void ps2_cleanup(struct ps2dev *ps2dev)
{
	unsigned long old_flags = ps2dev->flags;

	/* reset all flags except last nak */
	ps2dev->flags &= PS2_FLAG_NAK;

	if (old_flags & PS2_FLAG_ACK)
		ps2dev->nak = 1;

	if (old_flags & (PS2_FLAG_ACK | PS2_FLAG_CMD))
		wake_up(&ps2dev->wait);
}

/**
 * ps2_interrupt - common interrupt handler for PS/2 devices
 * @serio: serio port for the device
 * @data: a data byte received from the device
 * @flags: flags such as %SERIO_PARITY or %SERIO_TIMEOUT indicating state of
 *   the data transfer
 *
 * ps2_interrupt() invokes pre-receive handler, optionally handles command
 * acknowledgement and response from the device, and finally passes the data
 * to the main protocol handler for future processing.
 */
irqreturn_t ps2_interrupt(struct serio *serio, u8 data, unsigned int flags) {
	struct ps2dev *ps2dev = serio_get_drvdata(serio);
	enum ps2_disposition rc;

	rc = ps2dev->pre_receive_handler(ps2dev, data, flags);
	switch (rc) {
	case PS2_ERROR:
		ps2_cleanup(ps2dev);
		break;

	case PS2_IGNORE:
		break;

	case PS2_PROCESS:
		if (ps2dev->flags & PS2_FLAG_ACK)
			ps2_handle_ack(ps2dev, data);
		else if (ps2dev->flags & PS2_FLAG_CMD)
			ps2_handle_response(ps2dev, data);
		else
			ps2dev->receive_handler(ps2dev, data);
		break;
	}

	return IRQ_HANDLED;
}
EXPORT_SYMBOL(ps2_interrupt);

MODULE_AUTHOR("Dmitry Torokhov <dtor@mail.ru>");
MODULE_DESCRIPTION("PS/2 driver library");
MODULE_LICENSE("GPL");
