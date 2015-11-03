/*
 * TTY driver for MIPS EJTAG Fast Debug Channels.
 *
 * Copyright (C) 2007-2015 Imagination Technologies Ltd
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for more
 * details.
 */

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kgdb.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/uaccess.h>

#include <asm/cdmm.h>
#include <asm/irq.h>

/* Register offsets */
#define REG_FDACSR	0x00	/* FDC Access Control and Status Register */
#define REG_FDCFG	0x08	/* FDC Configuration Register */
#define REG_FDSTAT	0x10	/* FDC Status Register */
#define REG_FDRX	0x18	/* FDC Receive Register */
#define REG_FDTX(N)	(0x20+0x8*(N))	/* FDC Transmit Register n (0..15) */

/* Register fields */

#define REG_FDCFG_TXINTTHRES_SHIFT	18
#define REG_FDCFG_TXINTTHRES		(0x3 << REG_FDCFG_TXINTTHRES_SHIFT)
#define REG_FDCFG_TXINTTHRES_DISABLED	(0x0 << REG_FDCFG_TXINTTHRES_SHIFT)
#define REG_FDCFG_TXINTTHRES_EMPTY	(0x1 << REG_FDCFG_TXINTTHRES_SHIFT)
#define REG_FDCFG_TXINTTHRES_NOTFULL	(0x2 << REG_FDCFG_TXINTTHRES_SHIFT)
#define REG_FDCFG_TXINTTHRES_NEAREMPTY	(0x3 << REG_FDCFG_TXINTTHRES_SHIFT)
#define REG_FDCFG_RXINTTHRES_SHIFT	16
#define REG_FDCFG_RXINTTHRES		(0x3 << REG_FDCFG_RXINTTHRES_SHIFT)
#define REG_FDCFG_RXINTTHRES_DISABLED	(0x0 << REG_FDCFG_RXINTTHRES_SHIFT)
#define REG_FDCFG_RXINTTHRES_FULL	(0x1 << REG_FDCFG_RXINTTHRES_SHIFT)
#define REG_FDCFG_RXINTTHRES_NOTEMPTY	(0x2 << REG_FDCFG_RXINTTHRES_SHIFT)
#define REG_FDCFG_RXINTTHRES_NEARFULL	(0x3 << REG_FDCFG_RXINTTHRES_SHIFT)
#define REG_FDCFG_TXFIFOSIZE_SHIFT	8
#define REG_FDCFG_TXFIFOSIZE		(0xff << REG_FDCFG_TXFIFOSIZE_SHIFT)
#define REG_FDCFG_RXFIFOSIZE_SHIFT	0
#define REG_FDCFG_RXFIFOSIZE		(0xff << REG_FDCFG_RXFIFOSIZE_SHIFT)

#define REG_FDSTAT_TXCOUNT_SHIFT	24
#define REG_FDSTAT_TXCOUNT		(0xff << REG_FDSTAT_TXCOUNT_SHIFT)
#define REG_FDSTAT_RXCOUNT_SHIFT	16
#define REG_FDSTAT_RXCOUNT		(0xff << REG_FDSTAT_RXCOUNT_SHIFT)
#define REG_FDSTAT_RXCHAN_SHIFT		4
#define REG_FDSTAT_RXCHAN		(0xf << REG_FDSTAT_RXCHAN_SHIFT)
#define REG_FDSTAT_RXE			BIT(3)	/* Rx Empty */
#define REG_FDSTAT_RXF			BIT(2)	/* Rx Full */
#define REG_FDSTAT_TXE			BIT(1)	/* Tx Empty */
#define REG_FDSTAT_TXF			BIT(0)	/* Tx Full */

/* Default channel for the early console */
#define CONSOLE_CHANNEL      1

#define NUM_TTY_CHANNELS     16

#define RX_BUF_SIZE 1024

/*
 * When the IRQ is unavailable, the FDC state must be polled for incoming data
 * and space becoming available in TX FIFO.
 */
#define FDC_TTY_POLL (HZ / 50)

struct mips_ejtag_fdc_tty;

/**
 * struct mips_ejtag_fdc_tty_port - Wrapper struct for FDC tty_port.
 * @port:		TTY port data
 * @driver:		TTY driver.
 * @rx_lock:		Lock for rx_buf.
 *			This protects between the hard interrupt and user
 *			context. It's also held during read SWITCH operations.
 * @rx_buf:		Read buffer.
 * @xmit_lock:		Lock for xmit_*, and port.xmit_buf.
 *			This protects between user context and kernel thread.
 *			It is used from chars_in_buffer()/write_room() TTY
 *			callbacks which are used during wait operations, so a
 *			mutex is unsuitable.
 * @xmit_cnt:		Size of xmit buffer contents.
 * @xmit_head:		Head of xmit buffer where data is written.
 * @xmit_tail:		Tail of xmit buffer where data is read.
 * @xmit_empty:		Completion for xmit buffer being empty.
 */
struct mips_ejtag_fdc_tty_port {
	struct tty_port			 port;
	struct mips_ejtag_fdc_tty	*driver;
	raw_spinlock_t			 rx_lock;
	void				*rx_buf;
	spinlock_t			 xmit_lock;
	unsigned int			 xmit_cnt;
	unsigned int			 xmit_head;
	unsigned int			 xmit_tail;
	struct completion		 xmit_empty;
};

/**
 * struct mips_ejtag_fdc_tty - Driver data for FDC as a whole.
 * @dev:		FDC device (for dev_*() logging).
 * @driver:		TTY driver.
 * @cpu:		CPU number for this FDC.
 * @fdc_name:		FDC name (not for base of channel names).
 * @driver_name:	Base of driver name.
 * @ports:		Per-channel data.
 * @waitqueue:		Wait queue for waiting for TX data, or for space in TX
 *			FIFO.
 * @lock:		Lock to protect FDCFG (interrupt enable).
 * @thread:		KThread for writing out data to FDC.
 * @reg:		FDC registers.
 * @tx_fifo:		TX FIFO size.
 * @xmit_size:		Size of each port's xmit buffer.
 * @xmit_total:		Total number of bytes (from all ports) to transmit.
 * @xmit_next:		Next port number to transmit from (round robin).
 * @xmit_full:		Indicates TX FIFO is full, we're waiting for space.
 * @irq:		IRQ number (negative if no IRQ).
 * @removing:		Indicates the device is being removed and @poll_timer
 *			should not be restarted.
 * @poll_timer:		Timer for polling for interrupt events when @irq < 0.
 * @sysrq_pressed:	Whether the magic sysrq key combination has been
 *			detected. See mips_ejtag_fdc_handle().
 */
struct mips_ejtag_fdc_tty {
	struct device			*dev;
	struct tty_driver		*driver;
	unsigned int			 cpu;
	char				 fdc_name[16];
	char				 driver_name[16];
	struct mips_ejtag_fdc_tty_port	 ports[NUM_TTY_CHANNELS];
	wait_queue_head_t		 waitqueue;
	raw_spinlock_t			 lock;
	struct task_struct		*thread;

	void __iomem			*reg;
	u8				 tx_fifo;

	unsigned int			 xmit_size;
	atomic_t			 xmit_total;
	unsigned int			 xmit_next;
	bool				 xmit_full;

	int				 irq;
	bool				 removing;
	struct timer_list		 poll_timer;

#ifdef CONFIG_MAGIC_SYSRQ
	bool				 sysrq_pressed;
#endif
};

/* Hardware access */

static inline void mips_ejtag_fdc_write(struct mips_ejtag_fdc_tty *priv,
					unsigned int offs, unsigned int data)
{
	__raw_writel(data, priv->reg + offs);
}

static inline unsigned int mips_ejtag_fdc_read(struct mips_ejtag_fdc_tty *priv,
					       unsigned int offs)
{
	return __raw_readl(priv->reg + offs);
}

/* Encoding of byte stream in FDC words */

/**
 * struct fdc_word - FDC word encoding some number of bytes of data.
 * @word:		Raw FDC word.
 * @bytes:		Number of bytes encoded by @word.
 */
struct fdc_word {
	u32		word;
	unsigned int	bytes;
};

/*
 * This is a compact encoding which allows every 1 byte, 2 byte, and 3 byte
 * sequence to be encoded in a single word, while allowing the majority of 4
 * byte sequences (including all ASCII and common binary data) to be encoded in
 * a single word too.
 *    _______________________ _____________
 *   |       FDC Word        |             |
 *   |31-24|23-16|15-8 | 7-0 |    Bytes    |
 *   |_____|_____|_____|_____|_____________|
 *   |     |     |     |     |             |
 *   |0x80 |0x80 |0x80 |  WW | WW          |
 *   |0x81 |0x81 |  XX |  WW | WW XX       |
 *   |0x82 |  YY |  XX |  WW | WW XX YY    |
 *   |  ZZ |  YY |  XX |  WW | WW XX YY ZZ |
 *   |_____|_____|_____|_____|_____________|
 *
 * Note that the 4-byte encoding can only be used where none of the other 3
 * encodings match, otherwise it must fall back to the 3 byte encoding.
 */

/* ranges >= 1 && sizes[0] >= 1 */
static struct fdc_word mips_ejtag_fdc_encode(const char **ptrs,
					     unsigned int *sizes,
					     unsigned int ranges)
{
	struct fdc_word word = { 0, 0 };
	const char **ptrs_end = ptrs + ranges;

	for (; ptrs < ptrs_end; ++ptrs) {
		const char *ptr = *(ptrs++);
		const char *end = ptr + *(sizes++);

		for (; ptr < end; ++ptr) {
			word.word |= (u8)*ptr << (8*word.bytes);
			++word.bytes;
			if (word.bytes == 4)
				goto done;
		}
	}
done:
	/* Choose the appropriate encoding */
	switch (word.bytes) {
	case 4:
		/* 4 byte encoding, but don't match the 1-3 byte encodings */
		if ((word.word >> 8) != 0x808080 &&
		    (word.word >> 16) != 0x8181 &&
		    (word.word >> 24) != 0x82)
			break;
		/* Fall back to a 3 byte encoding */
		word.bytes = 3;
		word.word &= 0x00ffffff;
	case 3:
		/* 3 byte encoding */
		word.word |= 0x82000000;
		break;
	case 2:
		/* 2 byte encoding */
		word.word |= 0x81810000;
		break;
	case 1:
		/* 1 byte encoding */
		word.word |= 0x80808000;
		break;
	}
	return word;
}

static unsigned int mips_ejtag_fdc_decode(u32 word, char *buf)
{
	buf[0] = (u8)word;
	word >>= 8;
	if (word == 0x808080)
		return 1;
	buf[1] = (u8)word;
	word >>= 8;
	if (word == 0x8181)
		return 2;
	buf[2] = (u8)word;
	word >>= 8;
	if (word == 0x82)
		return 3;
	buf[3] = (u8)word;
	return 4;
}

/* Console operations */

/**
 * struct mips_ejtag_fdc_console - Wrapper struct for FDC consoles.
 * @cons:		Console object.
 * @tty_drv:		TTY driver associated with this console.
 * @lock:		Lock to protect concurrent access to other fields.
 *			This is raw because it may be used very early.
 * @initialised:	Whether the console is initialised.
 * @regs:		Registers base address for each CPU.
 */
struct mips_ejtag_fdc_console {
	struct console		 cons;
	struct tty_driver	*tty_drv;
	raw_spinlock_t		 lock;
	bool			 initialised;
	void __iomem		*regs[NR_CPUS];
};

/* Low level console write shared by early console and normal console */
static void mips_ejtag_fdc_console_write(struct console *c, const char *s,
					 unsigned int count)
{
	struct mips_ejtag_fdc_console *cons =
		container_of(c, struct mips_ejtag_fdc_console, cons);
	void __iomem *regs;
	struct fdc_word word;
	unsigned long flags;
	unsigned int i, buf_len, cpu;
	bool done_cr = false;
	char buf[4];
	const char *buf_ptr = buf;
	/* Number of bytes of input data encoded up to each byte in buf */
	u8 inc[4];

	local_irq_save(flags);
	cpu = smp_processor_id();
	regs = cons->regs[cpu];
	/* First console output on this CPU? */
	if (!regs) {
		regs = mips_cdmm_early_probe(0xfd);
		cons->regs[cpu] = regs;
	}
	/* Already tried and failed to find FDC on this CPU? */
	if (IS_ERR(regs))
		goto out;
	while (count) {
		/*
		 * Copy the next few characters to a buffer so we can inject
		 * carriage returns before newlines.
		 */
		for (buf_len = 0, i = 0; buf_len < 4 && i < count; ++buf_len) {
			if (s[i] == '\n' && !done_cr) {
				buf[buf_len] = '\r';
				done_cr = true;
			} else {
				buf[buf_len] = s[i];
				done_cr = false;
				++i;
			}
			inc[buf_len] = i;
		}
		word = mips_ejtag_fdc_encode(&buf_ptr, &buf_len, 1);
		count -= inc[word.bytes - 1];
		s += inc[word.bytes - 1];

		/* Busy wait until there's space in fifo */
		while (__raw_readl(regs + REG_FDSTAT) & REG_FDSTAT_TXF)
			;
		__raw_writel(word.word, regs + REG_FDTX(c->index));
	}
out:
	local_irq_restore(flags);
}

static struct tty_driver *mips_ejtag_fdc_console_device(struct console *c,
							int *index)
{
	struct mips_ejtag_fdc_console *cons =
		container_of(c, struct mips_ejtag_fdc_console, cons);

	*index = c->index;
	return cons->tty_drv;
}

/* Initialise an FDC console (early or normal */
static int __init mips_ejtag_fdc_console_init(struct mips_ejtag_fdc_console *c)
{
	void __iomem *regs;
	unsigned long flags;
	int ret = 0;

	raw_spin_lock_irqsave(&c->lock, flags);
	/* Don't init twice */
	if (c->initialised)
		goto out;
	/* Look for the FDC device */
	regs = mips_cdmm_early_probe(0xfd);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		goto out;
	}

	c->initialised = true;
	c->regs[smp_processor_id()] = regs;
	register_console(&c->cons);
out:
	raw_spin_unlock_irqrestore(&c->lock, flags);
	return ret;
}

static struct mips_ejtag_fdc_console mips_ejtag_fdc_con = {
	.cons	= {
		.name	= "fdc",
		.write	= mips_ejtag_fdc_console_write,
		.device	= mips_ejtag_fdc_console_device,
		.flags	= CON_PRINTBUFFER,
		.index	= -1,
	},
	.lock	= __RAW_SPIN_LOCK_UNLOCKED(mips_ejtag_fdc_con.lock),
};

/* TTY RX/TX operations */

/**
 * mips_ejtag_fdc_put_chan() - Write out a block of channel data.
 * @priv:	Pointer to driver private data.
 * @chan:	Channel number.
 *
 * Write a single block of data out to the debug adapter. If the circular buffer
 * is wrapped then only the first block is written.
 *
 * Returns:	The number of bytes that were written.
 */
static unsigned int mips_ejtag_fdc_put_chan(struct mips_ejtag_fdc_tty *priv,
					    unsigned int chan)
{
	struct mips_ejtag_fdc_tty_port *dport;
	struct tty_struct *tty;
	const char *ptrs[2];
	unsigned int sizes[2] = { 0 };
	struct fdc_word word = { .bytes = 0 };
	unsigned long flags;

	dport = &priv->ports[chan];
	spin_lock(&dport->xmit_lock);
	if (dport->xmit_cnt) {
		ptrs[0] = dport->port.xmit_buf + dport->xmit_tail;
		sizes[0] = min_t(unsigned int,
				 priv->xmit_size - dport->xmit_tail,
				 dport->xmit_cnt);
		ptrs[1] = dport->port.xmit_buf;
		sizes[1] = dport->xmit_cnt - sizes[0];
		word = mips_ejtag_fdc_encode(ptrs, sizes, 1 + !!sizes[1]);

		dev_dbg(priv->dev, "%s%u: out %08x: \"%*pE%*pE\"\n",
			priv->driver_name, chan, word.word,
			min_t(int, word.bytes, sizes[0]), ptrs[0],
			max_t(int, 0, word.bytes - sizes[0]), ptrs[1]);

		local_irq_save(flags);
		/* Maybe we raced with the console and TX FIFO is full */
		if (mips_ejtag_fdc_read(priv, REG_FDSTAT) & REG_FDSTAT_TXF)
			word.bytes = 0;
		else
			mips_ejtag_fdc_write(priv, REG_FDTX(chan), word.word);
		local_irq_restore(flags);

		dport->xmit_cnt -= word.bytes;
		if (!dport->xmit_cnt) {
			/* Reset pointers to avoid wraps */
			dport->xmit_head = 0;
			dport->xmit_tail = 0;
			complete(&dport->xmit_empty);
		} else {
			dport->xmit_tail += word.bytes;
			if (dport->xmit_tail >= priv->xmit_size)
				dport->xmit_tail -= priv->xmit_size;
		}
		atomic_sub(word.bytes, &priv->xmit_total);
	}
	spin_unlock(&dport->xmit_lock);

	/* If we've made more data available, wake up tty */
	if (sizes[0] && word.bytes) {
		tty = tty_port_tty_get(&dport->port);
		if (tty) {
			tty_wakeup(tty);
			tty_kref_put(tty);
		}
	}

	return word.bytes;
}

/**
 * mips_ejtag_fdc_put() - Kernel thread to write out channel data to FDC.
 * @arg:	Driver pointer.
 *
 * This kernel thread runs while @priv->xmit_total != 0, and round robins the
 * channels writing out blocks of buffered data to the FDC TX FIFO.
 */
static int mips_ejtag_fdc_put(void *arg)
{
	struct mips_ejtag_fdc_tty *priv = arg;
	struct mips_ejtag_fdc_tty_port *dport;
	unsigned int ret;
	u32 cfg;

	__set_current_state(TASK_RUNNING);
	while (!kthread_should_stop()) {
		/* Wait for data to actually write */
		wait_event_interruptible(priv->waitqueue,
					 atomic_read(&priv->xmit_total) ||
					 kthread_should_stop());
		if (kthread_should_stop())
			break;

		/* Wait for TX FIFO space to write data */
		raw_spin_lock_irq(&priv->lock);
		if (mips_ejtag_fdc_read(priv, REG_FDSTAT) & REG_FDSTAT_TXF) {
			priv->xmit_full = true;
			if (priv->irq >= 0) {
				/* Enable TX interrupt */
				cfg = mips_ejtag_fdc_read(priv, REG_FDCFG);
				cfg &= ~REG_FDCFG_TXINTTHRES;
				cfg |= REG_FDCFG_TXINTTHRES_NOTFULL;
				mips_ejtag_fdc_write(priv, REG_FDCFG, cfg);
			}
		}
		raw_spin_unlock_irq(&priv->lock);
		wait_event_interruptible(priv->waitqueue,
					 !(mips_ejtag_fdc_read(priv, REG_FDSTAT)
					   & REG_FDSTAT_TXF) ||
					 kthread_should_stop());
		if (kthread_should_stop())
			break;

		/* Find next channel with data to output */
		for (;;) {
			dport = &priv->ports[priv->xmit_next];
			spin_lock(&dport->xmit_lock);
			ret = dport->xmit_cnt;
			spin_unlock(&dport->xmit_lock);
			if (ret)
				break;
			/* Round robin */
			++priv->xmit_next;
			if (priv->xmit_next >= NUM_TTY_CHANNELS)
				priv->xmit_next = 0;
		}

		/* Try writing data to the chosen channel */
		ret = mips_ejtag_fdc_put_chan(priv, priv->xmit_next);

		/*
		 * If anything was output, move on to the next channel so as not
		 * to starve other channels.
		 */
		if (ret) {
			++priv->xmit_next;
			if (priv->xmit_next >= NUM_TTY_CHANNELS)
				priv->xmit_next = 0;
		}
	}

	return 0;
}

/**
 * mips_ejtag_fdc_handle() - Handle FDC events.
 * @priv:	Pointer to driver private data.
 *
 * Handle FDC events, such as new incoming data which needs draining out of the
 * RX FIFO and feeding into the appropriate TTY ports, and space becoming
 * available in the TX FIFO which would allow more data to be written out.
 */
static void mips_ejtag_fdc_handle(struct mips_ejtag_fdc_tty *priv)
{
	struct mips_ejtag_fdc_tty_port *dport;
	unsigned int stat, channel, data, cfg, i, flipped;
	int len;
	char buf[4];

	for (;;) {
		/* Find which channel the next FDC word is destined for */
		stat = mips_ejtag_fdc_read(priv, REG_FDSTAT);
		if (stat & REG_FDSTAT_RXE)
			break;
		channel = (stat & REG_FDSTAT_RXCHAN) >> REG_FDSTAT_RXCHAN_SHIFT;
		dport = &priv->ports[channel];

		/* Read out the FDC word, decode it, and pass to tty layer */
		raw_spin_lock(&dport->rx_lock);
		data = mips_ejtag_fdc_read(priv, REG_FDRX);

		len = mips_ejtag_fdc_decode(data, buf);
		dev_dbg(priv->dev, "%s%u: in  %08x: \"%*pE\"\n",
			priv->driver_name, channel, data, len, buf);

		flipped = 0;
		for (i = 0; i < len; ++i) {
#ifdef CONFIG_MAGIC_SYSRQ
#ifdef CONFIG_MIPS_EJTAG_FDC_KGDB
			/* Support just Ctrl+C with KGDB channel */
			if (channel == CONFIG_MIPS_EJTAG_FDC_KGDB_CHAN) {
				if (buf[i] == '\x03') { /* ^C */
					handle_sysrq('g');
					continue;
				}
			}
#endif
			/* Support Ctrl+O for console channel */
			if (channel == mips_ejtag_fdc_con.cons.index) {
				if (buf[i] == '\x0f') {	/* ^O */
					priv->sysrq_pressed =
						!priv->sysrq_pressed;
					if (priv->sysrq_pressed)
						continue;
				} else if (priv->sysrq_pressed) {
					handle_sysrq(buf[i]);
					priv->sysrq_pressed = false;
					continue;
				}
			}
#endif /* CONFIG_MAGIC_SYSRQ */

			/* Check the port isn't being shut down */
			if (!dport->rx_buf)
				continue;

			flipped += tty_insert_flip_char(&dport->port, buf[i],
							TTY_NORMAL);
		}
		if (flipped)
			tty_flip_buffer_push(&dport->port);

		raw_spin_unlock(&dport->rx_lock);
	}

	/* If TX FIFO no longer full we may be able to write more data */
	raw_spin_lock(&priv->lock);
	if (priv->xmit_full && !(stat & REG_FDSTAT_TXF)) {
		priv->xmit_full = false;

		/* Disable TX interrupt */
		cfg = mips_ejtag_fdc_read(priv, REG_FDCFG);
		cfg &= ~REG_FDCFG_TXINTTHRES;
		cfg |= REG_FDCFG_TXINTTHRES_DISABLED;
		mips_ejtag_fdc_write(priv, REG_FDCFG, cfg);

		/* Wait the kthread so it can try writing more data */
		wake_up_interruptible(&priv->waitqueue);
	}
	raw_spin_unlock(&priv->lock);
}

/**
 * mips_ejtag_fdc_isr() - Interrupt handler.
 * @irq:	IRQ number.
 * @dev_id:	Pointer to driver private data.
 *
 * This is the interrupt handler, used when interrupts are enabled.
 *
 * It simply triggers the common FDC handler code.
 *
 * Returns:	IRQ_HANDLED if an FDC interrupt was pending.
 *		IRQ_NONE otherwise.
 */
static irqreturn_t mips_ejtag_fdc_isr(int irq, void *dev_id)
{
	struct mips_ejtag_fdc_tty *priv = dev_id;

	/*
	 * We're not using proper per-cpu IRQs, so we must be careful not to
	 * handle IRQs on CPUs we're not interested in.
	 *
	 * Ideally proper per-cpu IRQ handlers could be used, but that doesn't
	 * fit well with the whole sharing of the main CPU IRQ lines. When we
	 * have something with a GIC that routes the FDC IRQs (i.e. no sharing
	 * between handlers) then support could be added more easily.
	 */
	if (smp_processor_id() != priv->cpu)
		return IRQ_NONE;

	/* If no FDC interrupt pending, it wasn't for us */
	if (!(read_c0_cause() & CAUSEF_FDCI))
		return IRQ_NONE;

	mips_ejtag_fdc_handle(priv);
	return IRQ_HANDLED;
}

/**
 * mips_ejtag_fdc_tty_timer() - Poll FDC for incoming data.
 * @opaque:	Pointer to driver private data.
 *
 * This is the timer handler for when interrupts are disabled and polling the
 * FDC state is required.
 *
 * It simply triggers the common FDC handler code and arranges for further
 * polling.
 */
static void mips_ejtag_fdc_tty_timer(unsigned long opaque)
{
	struct mips_ejtag_fdc_tty *priv = (void *)opaque;

	mips_ejtag_fdc_handle(priv);
	if (!priv->removing)
		mod_timer_pinned(&priv->poll_timer, jiffies + FDC_TTY_POLL);
}

/* TTY Port operations */

static int mips_ejtag_fdc_tty_port_activate(struct tty_port *port,
					    struct tty_struct *tty)
{
	struct mips_ejtag_fdc_tty_port *dport =
		container_of(port, struct mips_ejtag_fdc_tty_port, port);
	void *rx_buf;

	/* Allocate the buffer we use for writing data */
	if (tty_port_alloc_xmit_buf(port) < 0)
		goto err;

	/* Allocate the buffer we use for reading data */
	rx_buf = kzalloc(RX_BUF_SIZE, GFP_KERNEL);
	if (!rx_buf)
		goto err_free_xmit;

	raw_spin_lock_irq(&dport->rx_lock);
	dport->rx_buf = rx_buf;
	raw_spin_unlock_irq(&dport->rx_lock);

	return 0;
err_free_xmit:
	tty_port_free_xmit_buf(port);
err:
	return -ENOMEM;
}

static void mips_ejtag_fdc_tty_port_shutdown(struct tty_port *port)
{
	struct mips_ejtag_fdc_tty_port *dport =
		container_of(port, struct mips_ejtag_fdc_tty_port, port);
	struct mips_ejtag_fdc_tty *priv = dport->driver;
	void *rx_buf;
	unsigned int count;

	spin_lock(&dport->xmit_lock);
	count = dport->xmit_cnt;
	spin_unlock(&dport->xmit_lock);
	if (count) {
		/*
		 * There's still data to write out, so wake and wait for the
		 * writer thread to drain the buffer.
		 */
		wake_up_interruptible(&priv->waitqueue);
		wait_for_completion(&dport->xmit_empty);
	}

	/* Null the read buffer (timer could still be running!) */
	raw_spin_lock_irq(&dport->rx_lock);
	rx_buf = dport->rx_buf;
	dport->rx_buf = NULL;
	raw_spin_unlock_irq(&dport->rx_lock);
	/* Free the read buffer */
	kfree(rx_buf);

	/* Free the write buffer */
	tty_port_free_xmit_buf(port);
}

static const struct tty_port_operations mips_ejtag_fdc_tty_port_ops = {
	.activate	= mips_ejtag_fdc_tty_port_activate,
	.shutdown	= mips_ejtag_fdc_tty_port_shutdown,
};

/* TTY operations */

static int mips_ejtag_fdc_tty_install(struct tty_driver *driver,
				      struct tty_struct *tty)
{
	struct mips_ejtag_fdc_tty *priv = driver->driver_state;

	tty->driver_data = &priv->ports[tty->index];
	return tty_port_install(&priv->ports[tty->index].port, driver, tty);
}

static int mips_ejtag_fdc_tty_open(struct tty_struct *tty, struct file *filp)
{
	return tty_port_open(tty->port, tty, filp);
}

static void mips_ejtag_fdc_tty_close(struct tty_struct *tty, struct file *filp)
{
	return tty_port_close(tty->port, tty, filp);
}

static void mips_ejtag_fdc_tty_hangup(struct tty_struct *tty)
{
	struct mips_ejtag_fdc_tty_port *dport = tty->driver_data;
	struct mips_ejtag_fdc_tty *priv = dport->driver;

	/* Drop any data in the xmit buffer */
	spin_lock(&dport->xmit_lock);
	if (dport->xmit_cnt) {
		atomic_sub(dport->xmit_cnt, &priv->xmit_total);
		dport->xmit_cnt = 0;
		dport->xmit_head = 0;
		dport->xmit_tail = 0;
		complete(&dport->xmit_empty);
	}
	spin_unlock(&dport->xmit_lock);

	tty_port_hangup(tty->port);
}

static int mips_ejtag_fdc_tty_write(struct tty_struct *tty,
				    const unsigned char *buf, int total)
{
	int count, block;
	struct mips_ejtag_fdc_tty_port *dport = tty->driver_data;
	struct mips_ejtag_fdc_tty *priv = dport->driver;

	/*
	 * Write to output buffer.
	 *
	 * The reason that we asynchronously write the buffer is because if we
	 * were to write the buffer synchronously then because the channels are
	 * per-CPU the buffer would be written to the channel of whatever CPU
	 * we're running on.
	 *
	 * What we actually want to happen is have all input and output done on
	 * one CPU.
	 */
	spin_lock(&dport->xmit_lock);
	/* Work out how many bytes we can write to the xmit buffer */
	total = min(total, (int)(priv->xmit_size - dport->xmit_cnt));
	atomic_add(total, &priv->xmit_total);
	dport->xmit_cnt += total;
	/* Write the actual bytes (may need splitting if it wraps) */
	for (count = total; count; count -= block) {
		block = min(count, (int)(priv->xmit_size - dport->xmit_head));
		memcpy(dport->port.xmit_buf + dport->xmit_head, buf, block);
		dport->xmit_head += block;
		if (dport->xmit_head >= priv->xmit_size)
			dport->xmit_head -= priv->xmit_size;
		buf += block;
	}
	count = dport->xmit_cnt;
	/* Xmit buffer no longer empty? */
	if (count)
		reinit_completion(&dport->xmit_empty);
	spin_unlock(&dport->xmit_lock);

	/* Wake up the kthread */
	if (total)
		wake_up_interruptible(&priv->waitqueue);
	return total;
}

static int mips_ejtag_fdc_tty_write_room(struct tty_struct *tty)
{
	struct mips_ejtag_fdc_tty_port *dport = tty->driver_data;
	struct mips_ejtag_fdc_tty *priv = dport->driver;
	int room;

	/* Report the space in the xmit buffer */
	spin_lock(&dport->xmit_lock);
	room = priv->xmit_size - dport->xmit_cnt;
	spin_unlock(&dport->xmit_lock);

	return room;
}

static int mips_ejtag_fdc_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct mips_ejtag_fdc_tty_port *dport = tty->driver_data;
	int chars;

	/* Report the number of bytes in the xmit buffer */
	spin_lock(&dport->xmit_lock);
	chars = dport->xmit_cnt;
	spin_unlock(&dport->xmit_lock);

	return chars;
}

static const struct tty_operations mips_ejtag_fdc_tty_ops = {
	.install		= mips_ejtag_fdc_tty_install,
	.open			= mips_ejtag_fdc_tty_open,
	.close			= mips_ejtag_fdc_tty_close,
	.hangup			= mips_ejtag_fdc_tty_hangup,
	.write			= mips_ejtag_fdc_tty_write,
	.write_room		= mips_ejtag_fdc_tty_write_room,
	.chars_in_buffer	= mips_ejtag_fdc_tty_chars_in_buffer,
};

int __weak get_c0_fdc_int(void)
{
	return -1;
}

static int mips_ejtag_fdc_tty_probe(struct mips_cdmm_device *dev)
{
	int ret, nport;
	struct mips_ejtag_fdc_tty_port *dport;
	struct mips_ejtag_fdc_tty *priv;
	struct tty_driver *driver;
	unsigned int cfg, tx_fifo;

	priv = devm_kzalloc(&dev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->cpu = dev->cpu;
	priv->dev = &dev->dev;
	mips_cdmm_set_drvdata(dev, priv);
	atomic_set(&priv->xmit_total, 0);
	raw_spin_lock_init(&priv->lock);

	priv->reg = devm_ioremap_nocache(priv->dev, dev->res.start,
					 resource_size(&dev->res));
	if (!priv->reg) {
		dev_err(priv->dev, "ioremap failed for resource %pR\n",
			&dev->res);
		return -ENOMEM;
	}

	cfg = mips_ejtag_fdc_read(priv, REG_FDCFG);
	tx_fifo = (cfg & REG_FDCFG_TXFIFOSIZE) >> REG_FDCFG_TXFIFOSIZE_SHIFT;
	/* Disable interrupts */
	cfg &= ~(REG_FDCFG_TXINTTHRES | REG_FDCFG_RXINTTHRES);
	cfg |= REG_FDCFG_TXINTTHRES_DISABLED;
	cfg |= REG_FDCFG_RXINTTHRES_DISABLED;
	mips_ejtag_fdc_write(priv, REG_FDCFG, cfg);

	/* Make each port's xmit FIFO big enough to fill FDC TX FIFO */
	priv->xmit_size = min(tx_fifo * 4, (unsigned int)SERIAL_XMIT_SIZE);

	driver = tty_alloc_driver(NUM_TTY_CHANNELS, TTY_DRIVER_REAL_RAW);
	if (IS_ERR(driver))
		return PTR_ERR(driver);
	priv->driver = driver;

	driver->driver_name = "ejtag_fdc";
	snprintf(priv->fdc_name, sizeof(priv->fdc_name), "ttyFDC%u", dev->cpu);
	snprintf(priv->driver_name, sizeof(priv->driver_name), "%sc",
		 priv->fdc_name);
	driver->name = priv->driver_name;
	driver->major = 0; /* Auto-allocate */
	driver->minor_start = 0;
	driver->type = TTY_DRIVER_TYPE_SERIAL;
	driver->subtype = SERIAL_TYPE_NORMAL;
	driver->init_termios = tty_std_termios;
	driver->init_termios.c_cflag |= CLOCAL;
	driver->driver_state = priv;

	tty_set_operations(driver, &mips_ejtag_fdc_tty_ops);
	for (nport = 0; nport < NUM_TTY_CHANNELS; nport++) {
		dport = &priv->ports[nport];
		dport->driver = priv;
		tty_port_init(&dport->port);
		dport->port.ops = &mips_ejtag_fdc_tty_port_ops;
		raw_spin_lock_init(&dport->rx_lock);
		spin_lock_init(&dport->xmit_lock);
		/* The xmit buffer starts empty, i.e. completely written */
		init_completion(&dport->xmit_empty);
		complete(&dport->xmit_empty);
	}

	/* Set up the console */
	mips_ejtag_fdc_con.regs[dev->cpu] = priv->reg;
	if (dev->cpu == 0)
		mips_ejtag_fdc_con.tty_drv = driver;

	init_waitqueue_head(&priv->waitqueue);
	priv->thread = kthread_create(mips_ejtag_fdc_put, priv, priv->fdc_name);
	if (IS_ERR(priv->thread)) {
		ret = PTR_ERR(priv->thread);
		dev_err(priv->dev, "Couldn't create kthread (%d)\n", ret);
		goto err_destroy_ports;
	}
	/*
	 * Bind the writer thread to the right CPU so it can't migrate.
	 * The channels are per-CPU and we want all channel I/O to be on a
	 * single predictable CPU.
	 */
	kthread_bind(priv->thread, dev->cpu);
	wake_up_process(priv->thread);

	/* Look for an FDC IRQ */
	priv->irq = get_c0_fdc_int();

	/* Try requesting the IRQ */
	if (priv->irq >= 0) {
		/*
		 * IRQF_SHARED, IRQF_NO_SUSPEND: The FDC IRQ may be shared with
		 * other local interrupts such as the timer which sets
		 * IRQF_TIMER (including IRQF_NO_SUSPEND).
		 *
		 * IRQF_NO_THREAD: The FDC IRQ isn't individually maskable so it
		 * cannot be deferred and handled by a thread on RT kernels. For
		 * this reason any spinlocks used from the ISR are raw.
		 */
		ret = devm_request_irq(priv->dev, priv->irq, mips_ejtag_fdc_isr,
				       IRQF_PERCPU | IRQF_SHARED |
				       IRQF_NO_THREAD | IRQF_NO_SUSPEND,
				       priv->fdc_name, priv);
		if (ret)
			priv->irq = -1;
	}
	if (priv->irq >= 0) {
		/* IRQ is usable, enable RX interrupt */
		raw_spin_lock_irq(&priv->lock);
		cfg = mips_ejtag_fdc_read(priv, REG_FDCFG);
		cfg &= ~REG_FDCFG_RXINTTHRES;
		cfg |= REG_FDCFG_RXINTTHRES_NOTEMPTY;
		mips_ejtag_fdc_write(priv, REG_FDCFG, cfg);
		raw_spin_unlock_irq(&priv->lock);
	} else {
		/* If we didn't get an usable IRQ, poll instead */
		setup_timer(&priv->poll_timer, mips_ejtag_fdc_tty_timer,
			    (unsigned long)priv);
		priv->poll_timer.expires = jiffies + FDC_TTY_POLL;
		/*
		 * Always attach the timer to the right CPU. The channels are
		 * per-CPU so all polling should be from a single CPU.
		 */
		add_timer_on(&priv->poll_timer, dev->cpu);

		dev_info(priv->dev, "No usable IRQ, polling enabled\n");
	}

	ret = tty_register_driver(driver);
	if (ret < 0) {
		dev_err(priv->dev, "Couldn't install tty driver (%d)\n", ret);
		goto err_stop_irq;
	}

	return 0;

err_stop_irq:
	if (priv->irq >= 0) {
		raw_spin_lock_irq(&priv->lock);
		cfg = mips_ejtag_fdc_read(priv, REG_FDCFG);
		/* Disable interrupts */
		cfg &= ~(REG_FDCFG_TXINTTHRES | REG_FDCFG_RXINTTHRES);
		cfg |= REG_FDCFG_TXINTTHRES_DISABLED;
		cfg |= REG_FDCFG_RXINTTHRES_DISABLED;
		mips_ejtag_fdc_write(priv, REG_FDCFG, cfg);
		raw_spin_unlock_irq(&priv->lock);
	} else {
		priv->removing = true;
		del_timer_sync(&priv->poll_timer);
	}
	kthread_stop(priv->thread);
err_destroy_ports:
	if (dev->cpu == 0)
		mips_ejtag_fdc_con.tty_drv = NULL;
	for (nport = 0; nport < NUM_TTY_CHANNELS; nport++) {
		dport = &priv->ports[nport];
		tty_port_destroy(&dport->port);
	}
	put_tty_driver(priv->driver);
	return ret;
}

static int mips_ejtag_fdc_tty_remove(struct mips_cdmm_device *dev)
{
	struct mips_ejtag_fdc_tty *priv = mips_cdmm_get_drvdata(dev);
	struct mips_ejtag_fdc_tty_port *dport;
	int nport;
	unsigned int cfg;

	if (priv->irq >= 0) {
		raw_spin_lock_irq(&priv->lock);
		cfg = mips_ejtag_fdc_read(priv, REG_FDCFG);
		/* Disable interrupts */
		cfg &= ~(REG_FDCFG_TXINTTHRES | REG_FDCFG_RXINTTHRES);
		cfg |= REG_FDCFG_TXINTTHRES_DISABLED;
		cfg |= REG_FDCFG_RXINTTHRES_DISABLED;
		mips_ejtag_fdc_write(priv, REG_FDCFG, cfg);
		raw_spin_unlock_irq(&priv->lock);
	} else {
		priv->removing = true;
		del_timer_sync(&priv->poll_timer);
	}
	kthread_stop(priv->thread);
	if (dev->cpu == 0)
		mips_ejtag_fdc_con.tty_drv = NULL;
	tty_unregister_driver(priv->driver);
	for (nport = 0; nport < NUM_TTY_CHANNELS; nport++) {
		dport = &priv->ports[nport];
		tty_port_destroy(&dport->port);
	}
	put_tty_driver(priv->driver);
	return 0;
}

static int mips_ejtag_fdc_tty_cpu_down(struct mips_cdmm_device *dev)
{
	struct mips_ejtag_fdc_tty *priv = mips_cdmm_get_drvdata(dev);
	unsigned int cfg;

	if (priv->irq >= 0) {
		raw_spin_lock_irq(&priv->lock);
		cfg = mips_ejtag_fdc_read(priv, REG_FDCFG);
		/* Disable interrupts */
		cfg &= ~(REG_FDCFG_TXINTTHRES | REG_FDCFG_RXINTTHRES);
		cfg |= REG_FDCFG_TXINTTHRES_DISABLED;
		cfg |= REG_FDCFG_RXINTTHRES_DISABLED;
		mips_ejtag_fdc_write(priv, REG_FDCFG, cfg);
		raw_spin_unlock_irq(&priv->lock);
	} else {
		priv->removing = true;
		del_timer_sync(&priv->poll_timer);
	}
	kthread_stop(priv->thread);

	return 0;
}

static int mips_ejtag_fdc_tty_cpu_up(struct mips_cdmm_device *dev)
{
	struct mips_ejtag_fdc_tty *priv = mips_cdmm_get_drvdata(dev);
	unsigned int cfg;
	int ret = 0;

	if (priv->irq >= 0) {
		/*
		 * IRQ is usable, enable RX interrupt
		 * This must be before kthread is restarted, as kthread may
		 * enable TX interrupt.
		 */
		raw_spin_lock_irq(&priv->lock);
		cfg = mips_ejtag_fdc_read(priv, REG_FDCFG);
		cfg &= ~(REG_FDCFG_TXINTTHRES | REG_FDCFG_RXINTTHRES);
		cfg |= REG_FDCFG_TXINTTHRES_DISABLED;
		cfg |= REG_FDCFG_RXINTTHRES_NOTEMPTY;
		mips_ejtag_fdc_write(priv, REG_FDCFG, cfg);
		raw_spin_unlock_irq(&priv->lock);
	} else {
		/* Restart poll timer */
		priv->removing = false;
		add_timer_on(&priv->poll_timer, dev->cpu);
	}

	/* Restart the kthread */
	priv->thread = kthread_create(mips_ejtag_fdc_put, priv, priv->fdc_name);
	if (IS_ERR(priv->thread)) {
		ret = PTR_ERR(priv->thread);
		dev_err(priv->dev, "Couldn't re-create kthread (%d)\n", ret);
		goto out;
	}
	/* Bind it back to the right CPU and set it off */
	kthread_bind(priv->thread, dev->cpu);
	wake_up_process(priv->thread);
out:
	return ret;
}

static struct mips_cdmm_device_id mips_ejtag_fdc_tty_ids[] = {
	{ .type = 0xfd },
	{ }
};

static struct mips_cdmm_driver mips_ejtag_fdc_tty_driver = {
	.drv		= {
		.name	= "mips_ejtag_fdc",
	},
	.probe		= mips_ejtag_fdc_tty_probe,
	.remove		= mips_ejtag_fdc_tty_remove,
	.cpu_down	= mips_ejtag_fdc_tty_cpu_down,
	.cpu_up		= mips_ejtag_fdc_tty_cpu_up,
	.id_table	= mips_ejtag_fdc_tty_ids,
};
module_mips_cdmm_driver(mips_ejtag_fdc_tty_driver);

static int __init mips_ejtag_fdc_init_console(void)
{
	return mips_ejtag_fdc_console_init(&mips_ejtag_fdc_con);
}
console_initcall(mips_ejtag_fdc_init_console);

#ifdef CONFIG_MIPS_EJTAG_FDC_EARLYCON
static struct mips_ejtag_fdc_console mips_ejtag_fdc_earlycon = {
	.cons	= {
		.name	= "early_fdc",
		.write	= mips_ejtag_fdc_console_write,
		.flags	= CON_PRINTBUFFER | CON_BOOT,
		.index	= CONSOLE_CHANNEL,
	},
	.lock	= __RAW_SPIN_LOCK_UNLOCKED(mips_ejtag_fdc_earlycon.lock),
};

int __init setup_early_fdc_console(void)
{
	return mips_ejtag_fdc_console_init(&mips_ejtag_fdc_earlycon);
}
#endif

#ifdef CONFIG_MIPS_EJTAG_FDC_KGDB

/* read buffer to allow decompaction */
static unsigned int kgdbfdc_rbuflen;
static unsigned int kgdbfdc_rpos;
static char kgdbfdc_rbuf[4];

/* write buffer to allow compaction */
static unsigned int kgdbfdc_wbuflen;
static char kgdbfdc_wbuf[4];

static void __iomem *kgdbfdc_setup(void)
{
	void __iomem *regs;
	unsigned int cpu;

	/* Find address, piggy backing off console percpu regs */
	cpu = smp_processor_id();
	regs = mips_ejtag_fdc_con.regs[cpu];
	/* First console output on this CPU? */
	if (!regs) {
		regs = mips_cdmm_early_probe(0xfd);
		mips_ejtag_fdc_con.regs[cpu] = regs;
	}
	/* Already tried and failed to find FDC on this CPU? */
	if (IS_ERR(regs))
		return regs;

	return regs;
}

/* read a character from the read buffer, filling from FDC RX FIFO */
static int kgdbfdc_read_char(void)
{
	unsigned int stat, channel, data;
	void __iomem *regs;

	/* No more data, try and read another FDC word from RX FIFO */
	if (kgdbfdc_rpos >= kgdbfdc_rbuflen) {
		kgdbfdc_rpos = 0;
		kgdbfdc_rbuflen = 0;

		regs = kgdbfdc_setup();
		if (IS_ERR(regs))
			return NO_POLL_CHAR;

		/* Read next word from KGDB channel */
		do {
			stat = __raw_readl(regs + REG_FDSTAT);

			/* No data waiting? */
			if (stat & REG_FDSTAT_RXE)
				return NO_POLL_CHAR;

			/* Read next word */
			channel = (stat & REG_FDSTAT_RXCHAN) >>
					REG_FDSTAT_RXCHAN_SHIFT;
			data = __raw_readl(regs + REG_FDRX);
		} while (channel != CONFIG_MIPS_EJTAG_FDC_KGDB_CHAN);

		/* Decode into rbuf */
		kgdbfdc_rbuflen = mips_ejtag_fdc_decode(data, kgdbfdc_rbuf);
	}
	pr_devel("kgdbfdc r %c\n", kgdbfdc_rbuf[kgdbfdc_rpos]);
	return kgdbfdc_rbuf[kgdbfdc_rpos++];
}

/* push an FDC word from write buffer to TX FIFO */
static void kgdbfdc_push_one(void)
{
	const char *bufs[1] = { kgdbfdc_wbuf };
	struct fdc_word word;
	void __iomem *regs;
	unsigned int i;

	/* Construct a word from any data in buffer */
	word = mips_ejtag_fdc_encode(bufs, &kgdbfdc_wbuflen, 1);
	/* Relocate any remaining data to beginnning of buffer */
	kgdbfdc_wbuflen -= word.bytes;
	for (i = 0; i < kgdbfdc_wbuflen; ++i)
		kgdbfdc_wbuf[i] = kgdbfdc_wbuf[i + word.bytes];

	regs = kgdbfdc_setup();
	if (IS_ERR(regs))
		return;

	/* Busy wait until there's space in fifo */
	while (__raw_readl(regs + REG_FDSTAT) & REG_FDSTAT_TXF)
		;
	__raw_writel(word.word,
		     regs + REG_FDTX(CONFIG_MIPS_EJTAG_FDC_KGDB_CHAN));
}

/* flush the whole write buffer to the TX FIFO */
static void kgdbfdc_flush(void)
{
	while (kgdbfdc_wbuflen)
		kgdbfdc_push_one();
}

/* write a character into the write buffer, writing out if full */
static void kgdbfdc_write_char(u8 chr)
{
	pr_devel("kgdbfdc w %c\n", chr);
	kgdbfdc_wbuf[kgdbfdc_wbuflen++] = chr;
	if (kgdbfdc_wbuflen >= sizeof(kgdbfdc_wbuf))
		kgdbfdc_push_one();
}

static struct kgdb_io kgdbfdc_io_ops = {
	.name		= "kgdbfdc",
	.read_char	= kgdbfdc_read_char,
	.write_char	= kgdbfdc_write_char,
	.flush		= kgdbfdc_flush,
};

static int __init kgdbfdc_init(void)
{
	kgdb_register_io_module(&kgdbfdc_io_ops);
	return 0;
}
early_initcall(kgdbfdc_init);
#endif
