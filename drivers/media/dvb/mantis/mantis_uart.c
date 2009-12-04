#include <linux/kernel.h>
#include <linux/spinlock.h>

#include <asm/irq.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"

#include "mantis_common.h"
#include "mantis_reg.h"
#include "mantis_uart.h"

struct mantis_uart_params {
	enum mantis_baud	baud_rate;
	enum mantis_parity	parity;
};

#define UART_MAX_BUF			16

int mantis_uart_read(struct mantis_pci *mantis, u8 *data)
{
	struct mantis_hwconfig *config = mantis->hwconfig;
	u32 stat, i;
	unsigned long flags;

	/* get data */
	for (i = 0; i < (config->bytes + 1); i++) {

		if (stat & MANTIS_UART_RXFIFO_FULL) {
			dprintk(MANTIS_ERROR, 1, "RX Fifo FULL");
		}
		data[i] = mmread(MANTIS_UART_RXD) & 0x3f;

		stat = mmread(MANTIS_UART_STAT);

		dprintk(MANTIS_DEBUG, 1, "Reading ... <%02x>", data[i] & 0x3f);

		if (data[i] & (1 << 7)) {
			dprintk(MANTIS_ERROR, 1, "UART framing error");
			return -EINVAL;
		}
		if (data[i] & (1 << 6)) {
			dprintk(MANTIS_ERROR, 1, "UART parity error");
			return -EINVAL;
		}
	}

	return 0;
}

static void mantis_uart_work(struct work_struct *work)
{
	struct mantis_pci *mantis = container_of(work, struct mantis_pci, uart_work);
	struct mantis_hwconfig *config = mantis->hwconfig;
	u8 buf[16];
	int i;

	dprintk(MANTIS_DEBUG, 1, "UART read");
	mantis_uart_read(mantis, buf);

	dprintk(MANTIS_DEBUG, 1, "UART: ");
	for (i = 0; i < (config->bytes + 1); i++)
		dprintk(MANTIS_DEBUG, 0, "<%02x> ", buf[i]);

	dprintk(MANTIS_DEBUG, 0, "\n");
}

static int mantis_uart_setup(struct mantis_pci *mantis,
			     struct mantis_uart_params *params)
{
	char* rates[] = { "B_9600", "B_19200", "B_38400", "B_57600", "B_115200" };
	char* parity[] = { "NONE", "ODD", "EVEN" };

	u32 reg;

	dprintk(MANTIS_DEBUG, 1, "Set Parity <%s> Baud Rate <%s>",
		parity[params->parity],
		rates[params->baud_rate]);

	mmwrite((mmread(MANTIS_UART_CTL) | (params->parity & 0x3)), MANTIS_UART_CTL);

	reg = mmread(MANTIS_UART_BAUD);

	switch (params->baud_rate) {
	case MANTIS_BAUD_9600:
		reg |= 0xd8;
		break;
	case MANTIS_BAUD_19200:
		reg |= 0x6c;
		break;
	case MANTIS_BAUD_38400:
		reg |= 0x36;
		break;
	case MANTIS_BAUD_57600:
		reg |= 0x23;
		break;
	case MANTIS_BAUD_115200:
		reg |= 0x11;
		break;
	default:
		return -EINVAL;
	}

	mmwrite(reg, MANTIS_UART_BAUD);

	return 0;
}

int mantis_uart_init(struct mantis_pci *mantis)
{
	struct mantis_hwconfig *config = mantis->hwconfig;
	struct mantis_uart_params params;

	dprintk(MANTIS_DEBUG, 1, "Initializing UART ..");
	/* default parity: */
	params.baud_rate = config->baud_rate;
	params.parity = config->parity;

	init_waitqueue_head(&mantis->uart_wq);
	spin_lock_init(&mantis->uart_lock);

	INIT_WORK(&mantis->uart_work, mantis_uart_work);

	/* disable interrupt */
	mmwrite(mmread(MANTIS_UART_CTL) & 0xffef, MANTIS_UART_CTL);

	mantis_uart_setup(mantis, &params);

	/* default 1 byte */
	mmwrite((mmread(MANTIS_UART_BAUD) | (config->bytes << 8)), MANTIS_UART_BAUD);

	/* flush buffer */
	mmwrite((mmread(MANTIS_UART_CTL) | MANTIS_UART_RXFLUSH), MANTIS_UART_CTL);

	/* enable interrupt */
	mmwrite(mmread(MANTIS_INT_MASK) | 0x800, MANTIS_INT_MASK);
	mmwrite(mmread(MANTIS_UART_CTL) | MANTIS_UART_RXINT, MANTIS_UART_CTL);

	schedule_work(&mantis->uart_work);

	return 0;
}
EXPORT_SYMBOL_GPL(mantis_uart_init);

void mantis_uart_exit(struct mantis_pci *mantis)
{
	/* disable interrupt */
	mmwrite(mmread(MANTIS_UART_CTL) & 0xffef, MANTIS_UART_CTL);
}
EXPORT_SYMBOL_GPL(mantis_uart_exit);
