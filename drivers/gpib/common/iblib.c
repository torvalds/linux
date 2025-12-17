// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 *    copyright            : (C) 2001, 2002 by Frank Mori Hess
 ***************************************************************************/

#define dev_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ibsys.h"
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>

/*
 * IBCAC
 * Return to the controller active state from the
 * controller standby state, i.e., turn ATN on.  Note
 * that in order to enter the controller active state
 * from the controller idle state, ibsic must be called.
 * If sync is non-zero, attempt to take control synchronously.
 * If fallback_to_async is non-zero, try to take control asynchronously
 * if synchronous attempt fails.
 */
int ibcac(struct gpib_board *board, int sync, int fallback_to_async)
{
	int status = ibstatus(board);
	int retval;

	if ((status & CIC) == 0)
		return -EINVAL;

	if (status & ATN)
		return 0;

	if (sync && (status & LACS) == 0)
		/*
		 * tcs (take control synchronously) can only possibly work when
		 * controller is listener.  Error code also needs to be -ETIMEDOUT
		 * or it will giveout without doing fallback.
		 */
		retval = -ETIMEDOUT;
	else
		retval = board->interface->take_control(board, sync);

	if (retval < 0 && fallback_to_async) {
		if (sync && retval == -ETIMEDOUT)
			retval = board->interface->take_control(board, 0);
	}
	board->interface->update_status(board, 0);

	return retval;
}

/*
 * After ATN is asserted, it should cause any connected devices
 * to start listening for command bytes and leave acceptor idle state.
 * So if ATN is asserted and neither NDAC or NRFD are asserted,
 * then there are no devices and ibcmd should error out immediately.
 * Some gpib hardware sees itself asserting NDAC/NRFD when it
 * is controller in charge, in which case this check will
 * do nothing useful (but shouldn't cause any harm either).
 * Drivers that don't need this check (ni_usb for example) may
 * set the skip_check_for_command_acceptors flag in their
 * gpib_interface_struct to avoid useless overhead.
 */
static int check_for_command_acceptors(struct gpib_board *board)
{
	int lines;

	if (board->interface->skip_check_for_command_acceptors)
		return 0;
	if (!board->interface->line_status)
		return 0;

	udelay(2); // allow time for devices to respond to ATN if it was just asserted

	lines = board->interface->line_status(board);
	if (lines < 0)
		return lines;

	if ((lines & VALID_NRFD) && (lines & VALID_NDAC))	{
		if ((lines & BUS_NRFD) == 0 && (lines & BUS_NDAC) == 0)
			return -ENOTCONN;
	}

	return 0;
}

/*
 * IBCMD
 * Write cnt command bytes from buf to the GPIB.  The
 * command operation terminates only on I/O complete.
 *
 * NOTE:
 *      1.  Prior to beginning the command, the interface is
 *          placed in the controller active state.
 *      2.  Before calling ibcmd for the first time, ibsic
 *          must be called to initialize the GPIB and enable
 *          the interface to leave the controller idle state.
 */
int ibcmd(struct gpib_board *board, u8 *buf, size_t length, size_t *bytes_written)
{
	ssize_t ret = 0;
	int status;

	*bytes_written = 0;

	status = ibstatus(board);

	if ((status & CIC) == 0)
		return -EINVAL;

	os_start_timer(board, board->usec_timeout);

	ret = ibcac(board, 1, 1);
	if (ret == 0) {
		ret = check_for_command_acceptors(board);
		if (ret == 0)
			ret = board->interface->command(board, buf, length, bytes_written);
	}

	os_remove_timer(board);

	if (io_timed_out(board))
		ret = -ETIMEDOUT;

	return ret;
}

/*
 * IBGTS
 * Go to the controller standby state from the controller
 * active state, i.e., turn ATN off.
 */

int ibgts(struct gpib_board *board)
{
	int status = ibstatus(board);
	int retval;

	if ((status & CIC) == 0)
		return -EINVAL;

	retval = board->interface->go_to_standby(board);    /* go to standby */

	board->interface->update_status(board, 0);

	return retval;
}

static int autospoll_wait_should_wake_up(struct gpib_board *board)
{
	int retval;

	mutex_lock(&board->big_gpib_mutex);

	retval = board->master && board->autospollers > 0 &&
		!atomic_read(&board->stuck_srq) &&
		test_and_clear_bit(SRQI_NUM, &board->status);

	mutex_unlock(&board->big_gpib_mutex);
	return retval;
}

static int autospoll_thread(void *board_void)
{
	struct gpib_board *board = board_void;
	int retval = 0;

	dev_dbg(board->gpib_dev, "entering autospoll thread\n");

	while (1) {
		wait_event_interruptible(board->wait,
					 kthread_should_stop() ||
					 autospoll_wait_should_wake_up(board));
		dev_dbg(board->gpib_dev, "autospoll wait satisfied\n");
		if (kthread_should_stop())
			break;

		mutex_lock(&board->big_gpib_mutex);
		/* make sure we are still good after we have lock */
		if (board->autospollers <= 0 || board->master == 0) {
			mutex_unlock(&board->big_gpib_mutex);
			continue;
		}
		mutex_unlock(&board->big_gpib_mutex);

		if (try_module_get(board->provider_module)) {
			retval = autopoll_all_devices(board);
			module_put(board->provider_module);
		} else {
			dev_err(board->gpib_dev, "try_module_get() failed!\n");
		}
		if (retval <= 0) {
			dev_err(board->gpib_dev, "stuck SRQ\n");

			atomic_set(&board->stuck_srq, 1);	// XXX could be better
			set_bit(SRQI_NUM, &board->status);
		}
	}
	return retval;
}

int ibonline(struct gpib_board *board)
{
	int retval;

	if (board->online)
		return -EBUSY;
	if (!board->interface)
		return -ENODEV;
	retval = gpib_allocate_board(board);
	if (retval < 0)
		return retval;

	board->dev = NULL;
	board->local_ppoll_mode = 0;
	retval = board->interface->attach(board, &board->config);
	if (retval < 0) {
		board->interface->detach(board);
		return retval;
	}
	/*
	 * nios2nommu on 2.6.11 uclinux kernel has weird problems
	 * with autospoll thread causing huge slowdowns
	 */
#ifndef CONFIG_NIOS2
	board->autospoll_task = kthread_run(&autospoll_thread, board,
					    "gpib%d_autospoll_kthread", board->minor);
	retval = IS_ERR(board->autospoll_task);
	if (retval) {
		dev_err(board->gpib_dev, "failed to create autospoll thread\n");
		board->interface->detach(board);
		return retval;
	}
#endif
	board->online = 1;
	dev_dbg(board->gpib_dev, "board online\n");

	return 0;
}

/* XXX need to make sure board is generally not in use (grab board lock?) */
int iboffline(struct gpib_board *board)
{
	int retval;

	if (board->online == 0)
		return 0;
	if (!board->interface)
		return -ENODEV;

	if (board->autospoll_task && !IS_ERR(board->autospoll_task)) {
		retval = kthread_stop(board->autospoll_task);
		if (retval)
			dev_err(board->gpib_dev, "kthread_stop returned %i\n", retval);
		board->autospoll_task = NULL;
	}

	board->interface->detach(board);
	gpib_deallocate_board(board);
	board->online = 0;
	dev_dbg(board->gpib_dev, "board offline\n");

	return 0;
}

/*
 * IBLINES
 * Poll the GPIB control lines and return their status in buf.
 *
 *      LSB (bits 0-7)  -  VALID lines mask (lines that can be monitored).
 * Next LSB (bits 8-15) - STATUS lines mask (lines that are currently set).
 *
 */
int iblines(const struct gpib_board *board, short *lines)
{
	int retval;

	*lines = 0;
	if (!board->interface->line_status)
		return 0;
	retval = board->interface->line_status(board);
	if (retval < 0)
		return retval;
	*lines = retval;
	return 0;
}

/*
 * IBRD
 * Read up to 'length' bytes of data from the GPIB into buf.  End
 * on detection of END (EOI and or EOS) and set 'end_flag'.
 *
 * NOTE:
 *      1.  The interface is placed in the controller standby
 *          state prior to beginning the read.
 *      2.  Prior to calling ibrd, the intended devices as well
 *          as the interface board itself must be addressed by
 *          calling ibcmd.
 */

int ibrd(struct gpib_board *board, u8 *buf, size_t length, int *end_flag, size_t *nbytes)
{
	ssize_t ret = 0;
	int retval;
	size_t bytes_read;

	*nbytes = 0;
	*end_flag = 0;
	if (length == 0)
		return 0;

	if (board->master) {
		retval = ibgts(board);
		if (retval < 0)
			return retval;
	}
	/*
	 * XXX resetting timer here could cause timeouts take longer than they should,
	 * since read_ioctl calls this
	 * function in a loop, there is probably a similar problem with writes/commands
	 */
	os_start_timer(board, board->usec_timeout);

	do {
		ret = board->interface->read(board, buf, length - *nbytes, end_flag, &bytes_read);
		if (ret < 0)
			goto ibrd_out;

		buf += bytes_read;
		*nbytes += bytes_read;
		if (need_resched())
			schedule();
	} while (ret == 0 && *nbytes > 0 && *nbytes < length && *end_flag == 0);
ibrd_out:
	os_remove_timer(board);

	return ret;
}

/*
 * IBRPP
 * Conduct a parallel poll and return the byte in buf.
 *
 * NOTE:
 *	1.  Prior to conducting the poll the interface is placed
 *	    in the controller active state.
 */
int ibrpp(struct gpib_board *board, u8 *result)
{
	int retval = 0;

	os_start_timer(board, board->usec_timeout);
	retval = ibcac(board, 1, 1);
	if (retval)
		return -1;

	retval =  board->interface->parallel_poll(board, result);

	os_remove_timer(board);
	return retval;
}

int ibppc(struct gpib_board *board, u8 configuration)
{
	configuration &= 0x1f;
	board->interface->parallel_poll_configure(board, configuration);
	board->parallel_poll_configuration = configuration;

	return 0;
}

int ibrsv2(struct gpib_board *board, u8 status_byte, int new_reason_for_service)
{
	int board_status = ibstatus(board);
	const unsigned int MSS = status_byte & request_service_bit;

	if ((board_status & CIC))
		return -EINVAL;

	if (MSS == 0 && new_reason_for_service)
		return -EINVAL;

	if (board->interface->serial_poll_response2)	{
		board->interface->serial_poll_response2(board, status_byte, new_reason_for_service);
		// fall back on simpler serial_poll_response if the behavior would be the same
	} else if (board->interface->serial_poll_response &&
		   (MSS == 0 || (MSS && new_reason_for_service))) {
		board->interface->serial_poll_response(board, status_byte);
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

/*
 * IBSIC
 * Send IFC for at least 100 microseconds.
 *
 * NOTE:
 *	1.  Ibsic must be called prior to the first call to
 *	    ibcmd in order to initialize the bus and enable the
 *	    interface to leave the controller idle state.
 */
int ibsic(struct gpib_board *board, unsigned int usec_duration)
{
	if (board->master == 0)
		return -EINVAL;

	if (usec_duration < 100)
		usec_duration = 100;
	if (usec_duration > 1000)
		usec_duration = 1000;

	dev_dbg(board->gpib_dev, "sending interface clear, delay = %ius\n", usec_duration);
	board->interface->interface_clear(board, 1);
	udelay(usec_duration);
	board->interface->interface_clear(board, 0);

	return 0;
}

int ibrsc(struct gpib_board *board, int request_control)
{
	int retval;

	if (!board->interface->request_system_control)
		return -EPERM;

	retval = board->interface->request_system_control(board, request_control);

	if (retval)
		return retval;

	board->master = request_control != 0;

	return  0;
}

/*
 * IBSRE
 * Send REN true if v is non-zero or false if v is zero.
 */
int ibsre(struct gpib_board *board, int enable)
{
	if (board->master == 0)
		return -EINVAL;

	board->interface->remote_enable(board, enable);	/* set or clear REN */
	if (!enable)
		usleep_range(100, 150);

	return 0;
}

/*
 * IBPAD
 * change the GPIB address of the interface board.  The address
 * must be 0 through 30.  ibonl resets the address to PAD.
 */
int ibpad(struct gpib_board *board, unsigned int addr)
{
	if (addr > MAX_GPIB_PRIMARY_ADDRESS)
		return -EINVAL;

	board->pad = addr;
	if (board->online)
		board->interface->primary_address(board, board->pad);
	dev_dbg(board->gpib_dev, "set primary addr to %i\n", board->pad);
	return 0;
}

/*
 * IBSAD
 * change the secondary GPIB address of the interface board.
 * The address must be 0 through 30, or negative disables.  ibonl resets the
 * address to SAD.
 */
int ibsad(struct gpib_board *board, int addr)
{
	if (addr > MAX_GPIB_SECONDARY_ADDRESS)
		return -EINVAL;
	board->sad = addr;
	if (board->online) {
		if (board->sad >= 0)
			board->interface->secondary_address(board, board->sad, 1);
		else
			board->interface->secondary_address(board, 0, 0);
	}
	dev_dbg(board->gpib_dev, "set secondary addr to %i\n", board->sad);

	return 0;
}

/*
 * IBEOS
 * Set the end-of-string modes for I/O operations to v.
 *
 */
int ibeos(struct gpib_board *board, int eos, int eosflags)
{
	int retval;

	if (eosflags & ~EOS_MASK)
		return -EINVAL;
	if (eosflags & REOS) {
		retval = board->interface->enable_eos(board, eos, eosflags & BIN);
	} else {
		board->interface->disable_eos(board);
		retval = 0;
	}
	return retval;
}

int ibstatus(struct gpib_board *board)
{
	return general_ibstatus(board, NULL, 0, 0, NULL);
}

int general_ibstatus(struct gpib_board *board, const struct gpib_status_queue *device,
		     int clear_mask, int set_mask, struct gpib_descriptor *desc)
{
	int status = 0;
	short line_status;

	if (board->private_data) {
		status = board->interface->update_status(board, clear_mask);
		/*
		 * XXX should probably stop having drivers use TIMO bit in
		 * board->status to avoid confusion
		 */
		status &= ~TIMO;
		/* get real SRQI status if we can */
		if (iblines(board, &line_status) == 0) {
			if ((line_status & VALID_SRQ)) {
				if ((line_status & BUS_SRQ))
					status |= SRQI;
				else
					status &= ~SRQI;
			}
		}
	}
	if (device)
		if (num_status_bytes(device))
			status |= RQS;

	if (desc) {
		if (set_mask & CMPL)
			atomic_set(&desc->io_in_progress, 0);
		else if (clear_mask & CMPL)
			atomic_set(&desc->io_in_progress, 1);

		if (atomic_read(&desc->io_in_progress))
			status &= ~CMPL;
		else
			status |= CMPL;
	}
	if (num_gpib_events(&board->event_queue))
		status |= EVENT;
	else
		status &= ~EVENT;

	return status;
}

struct wait_info {
	struct gpib_board *board;
	struct timer_list timer;
	int timed_out;
	unsigned long usec_timeout;
};

static void wait_timeout(struct timer_list *t)
{
	struct wait_info *winfo = timer_container_of(winfo, t, timer);

	winfo->timed_out = 1;
	wake_up_interruptible(&winfo->board->wait);
}

static void init_wait_info(struct wait_info *winfo)
{
	winfo->board = NULL;
	winfo->timed_out = 0;
	timer_setup_on_stack(&winfo->timer, wait_timeout, 0);
}

static int wait_satisfied(struct wait_info *winfo, struct gpib_status_queue *status_queue,
			  int wait_mask, int *status, struct gpib_descriptor *desc)
{
	struct gpib_board *board = winfo->board;
	int temp_status;

	if (mutex_lock_interruptible(&board->big_gpib_mutex))
		return -ERESTARTSYS;

	temp_status = general_ibstatus(board, status_queue, 0, 0, desc);

	mutex_unlock(&board->big_gpib_mutex);

	if (winfo->timed_out)
		temp_status |= TIMO;
	else
		temp_status &= ~TIMO;
	if (wait_mask & temp_status) {
		*status = temp_status;
		return 1;
	}
// XXX does wait for END work?
	return 0;
}

/* install timer interrupt handler */
static void start_wait_timer(struct wait_info *winfo)
/* Starts the timeout task  */
{
	winfo->timed_out = 0;

	if (winfo->usec_timeout > 0)
		mod_timer(&winfo->timer, jiffies + usec_to_jiffies(winfo->usec_timeout));
}

static void remove_wait_timer(struct wait_info *winfo)
{
	timer_delete_sync(&winfo->timer);
	timer_destroy_on_stack(&winfo->timer);
}

/*
 * IBWAIT
 * Check or wait for a GPIB event to occur.  The mask argument
 * is a bit vector corresponding to the status bit vector.  It
 * has a bit set for each condition which can terminate the wait
 * If the mask is 0 then
 * no condition is waited for.
 */
int ibwait(struct gpib_board *board, int wait_mask, int clear_mask, int set_mask,
	   int *status, unsigned long usec_timeout, struct gpib_descriptor *desc)
{
	int retval = 0;
	struct gpib_status_queue *status_queue;
	struct wait_info winfo;

	if (desc->is_board)
		status_queue = NULL;
	else
		status_queue = get_gpib_status_queue(board, desc->pad, desc->sad);

	if (wait_mask == 0) {
		*status = general_ibstatus(board, status_queue, clear_mask, set_mask, desc);
		return 0;
	}

	mutex_unlock(&board->big_gpib_mutex);

	init_wait_info(&winfo);
	winfo.board = board;
	winfo.usec_timeout = usec_timeout;
	start_wait_timer(&winfo);

	if (wait_event_interruptible(board->wait, wait_satisfied(&winfo, status_queue,
								 wait_mask, status, desc))) {
		dev_dbg(board->gpib_dev, "wait interrupted\n");
		retval = -ERESTARTSYS;
	}
	remove_wait_timer(&winfo);

	if (retval)
		return retval;
	if (mutex_lock_interruptible(&board->big_gpib_mutex))
		return -ERESTARTSYS;

	/* make sure we only clear status bits that we are reporting */
	if (*status & clear_mask || set_mask)
		general_ibstatus(board, status_queue, *status & clear_mask, set_mask, NULL);

	return 0;
}

/*
 * IBWRT
 * Write cnt bytes of data from buf to the GPIB.  The write
 * operation terminates only on I/O complete.
 *
 * NOTE:
 *      1.  Prior to beginning the write, the interface is
 *          placed in the controller standby state.
 *      2.  Prior to calling ibwrt, the intended devices as
 *          well as the interface board itself must be
 *          addressed by calling ibcmd.
 */
int ibwrt(struct gpib_board *board, u8 *buf, size_t cnt, int send_eoi, size_t *bytes_written)
{
	int ret = 0;
	int retval;

	if (cnt == 0)
		return 0;

	if (board->master) {
		retval = ibgts(board);
		if (retval < 0)
			return retval;
	}
	os_start_timer(board, board->usec_timeout);
	ret = board->interface->write(board, buf, cnt, send_eoi, bytes_written);

	if (io_timed_out(board))
		ret = -ETIMEDOUT;

	os_remove_timer(board);

	return ret;
}

