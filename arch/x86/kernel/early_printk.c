#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/screen_info.h>
#include <linux/usb/ch9.h>
#include <linux/pci_regs.h>
#include <linux/pci_ids.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/fcntl.h>
#include <asm/setup.h>
#include <xen/hvc-console.h>
#include <asm/pci-direct.h>
#include <asm/pgtable.h>
#include <asm/fixmap.h>
#include <linux/usb/ehci_def.h>

/* Simple VGA output */
#define VGABASE		(__ISA_IO_base + 0xb8000)

static int max_ypos = 25, max_xpos = 80;
static int current_ypos = 25, current_xpos;

static void early_vga_write(struct console *con, const char *str, unsigned n)
{
	char c;
	int  i, k, j;

	while ((c = *str++) != '\0' && n-- > 0) {
		if (current_ypos >= max_ypos) {
			/* scroll 1 line up */
			for (k = 1, j = 0; k < max_ypos; k++, j++) {
				for (i = 0; i < max_xpos; i++) {
					writew(readw(VGABASE+2*(max_xpos*k+i)),
					       VGABASE + 2*(max_xpos*j + i));
				}
			}
			for (i = 0; i < max_xpos; i++)
				writew(0x720, VGABASE + 2*(max_xpos*j + i));
			current_ypos = max_ypos-1;
		}
		if (c == '\n') {
			current_xpos = 0;
			current_ypos++;
		} else if (c != '\r')  {
			writew(((0x7 << 8) | (unsigned short) c),
			       VGABASE + 2*(max_xpos*current_ypos +
						current_xpos++));
			if (current_xpos >= max_xpos) {
				current_xpos = 0;
				current_ypos++;
			}
		}
	}
}

static struct console early_vga_console = {
	.name =		"earlyvga",
	.write =	early_vga_write,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};

/* Serial functions loosely based on a similar package from Klaus P. Gerlicher */

static int early_serial_base = 0x3f8;  /* ttyS0 */

#define XMTRDY          0x20

#define DLAB		0x80

#define TXR             0       /*  Transmit register (WRITE) */
#define RXR             0       /*  Receive register  (READ)  */
#define IER             1       /*  Interrupt Enable          */
#define IIR             2       /*  Interrupt ID              */
#define FCR             2       /*  FIFO control              */
#define LCR             3       /*  Line control              */
#define MCR             4       /*  Modem control             */
#define LSR             5       /*  Line Status               */
#define MSR             6       /*  Modem Status              */
#define DLL             0       /*  Divisor Latch Low         */
#define DLH             1       /*  Divisor latch High        */

static int early_serial_putc(unsigned char ch)
{
	unsigned timeout = 0xffff;

	while ((inb(early_serial_base + LSR) & XMTRDY) == 0 && --timeout)
		cpu_relax();
	outb(ch, early_serial_base + TXR);
	return timeout ? 0 : -1;
}

static void early_serial_write(struct console *con, const char *s, unsigned n)
{
	while (*s && n-- > 0) {
		if (*s == '\n')
			early_serial_putc('\r');
		early_serial_putc(*s);
		s++;
	}
}

#define DEFAULT_BAUD 9600

static __init void early_serial_init(char *s)
{
	unsigned char c;
	unsigned divisor;
	unsigned baud = DEFAULT_BAUD;
	char *e;

	if (*s == ',')
		++s;

	if (*s) {
		unsigned port;
		if (!strncmp(s, "0x", 2)) {
			early_serial_base = simple_strtoul(s, &e, 16);
		} else {
			static const int __initconst bases[] = { 0x3f8, 0x2f8 };

			if (!strncmp(s, "ttyS", 4))
				s += 4;
			port = simple_strtoul(s, &e, 10);
			if (port > 1 || s == e)
				port = 0;
			early_serial_base = bases[port];
		}
		s += strcspn(s, ",");
		if (*s == ',')
			s++;
	}

	outb(0x3, early_serial_base + LCR);	/* 8n1 */
	outb(0, early_serial_base + IER);	/* no interrupt */
	outb(0, early_serial_base + FCR);	/* no fifo */
	outb(0x3, early_serial_base + MCR);	/* DTR + RTS */

	if (*s) {
		baud = simple_strtoul(s, &e, 0);
		if (baud == 0 || s == e)
			baud = DEFAULT_BAUD;
	}

	divisor = 115200 / baud;
	c = inb(early_serial_base + LCR);
	outb(c | DLAB, early_serial_base + LCR);
	outb(divisor & 0xff, early_serial_base + DLL);
	outb((divisor >> 8) & 0xff, early_serial_base + DLH);
	outb(c & ~DLAB, early_serial_base + LCR);
}

static struct console early_serial_console = {
	.name =		"earlyser",
	.write =	early_serial_write,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};

#ifdef CONFIG_EARLY_PRINTK_DBGP

static struct ehci_caps __iomem *ehci_caps;
static struct ehci_regs __iomem *ehci_regs;
static struct ehci_dbg_port __iomem *ehci_debug;
static unsigned int dbgp_endpoint_out;

struct ehci_dev {
	u32 bus;
	u32 slot;
	u32 func;
};

static struct ehci_dev ehci_dev;

#define USB_DEBUG_DEVNUM 127

#define DBGP_DATA_TOGGLE	0x8800

static inline u32 dbgp_pid_update(u32 x, u32 tok)
{
	return ((x ^ DBGP_DATA_TOGGLE) & 0xffff00) | (tok & 0xff);
}

static inline u32 dbgp_len_update(u32 x, u32 len)
{
	return (x & ~0x0f) | (len & 0x0f);
}

/*
 * USB Packet IDs (PIDs)
 */

/* token */
#define USB_PID_OUT		0xe1
#define USB_PID_IN		0x69
#define USB_PID_SOF		0xa5
#define USB_PID_SETUP		0x2d
/* handshake */
#define USB_PID_ACK		0xd2
#define USB_PID_NAK		0x5a
#define USB_PID_STALL		0x1e
#define USB_PID_NYET		0x96
/* data */
#define USB_PID_DATA0		0xc3
#define USB_PID_DATA1		0x4b
#define USB_PID_DATA2		0x87
#define USB_PID_MDATA		0x0f
/* Special */
#define USB_PID_PREAMBLE	0x3c
#define USB_PID_ERR		0x3c
#define USB_PID_SPLIT		0x78
#define USB_PID_PING		0xb4
#define USB_PID_UNDEF_0		0xf0

#define USB_PID_DATA_TOGGLE	0x88
#define DBGP_CLAIM (DBGP_OWNER | DBGP_ENABLED | DBGP_INUSE)

#define PCI_CAP_ID_EHCI_DEBUG	0xa

#define HUB_ROOT_RESET_TIME	50	/* times are in msec */
#define HUB_SHORT_RESET_TIME	10
#define HUB_LONG_RESET_TIME	200
#define HUB_RESET_TIMEOUT	500

#define DBGP_MAX_PACKET		8

static int dbgp_wait_until_complete(void)
{
	u32 ctrl;
	int loop = 0x100000;

	do {
		ctrl = readl(&ehci_debug->control);
		/* Stop when the transaction is finished */
		if (ctrl & DBGP_DONE)
			break;
	} while (--loop > 0);

	if (!loop)
		return -1;

	/*
	 * Now that we have observed the completed transaction,
	 * clear the done bit.
	 */
	writel(ctrl | DBGP_DONE, &ehci_debug->control);
	return (ctrl & DBGP_ERROR) ? -DBGP_ERRCODE(ctrl) : DBGP_LEN(ctrl);
}

static void dbgp_mdelay(int ms)
{
	int i;

	while (ms--) {
		for (i = 0; i < 1000; i++)
			outb(0x1, 0x80);
	}
}

static void dbgp_breath(void)
{
	/* Sleep to give the debug port a chance to breathe */
}

static int dbgp_wait_until_done(unsigned ctrl)
{
	u32 pids, lpid;
	int ret;
	int loop = 3;

retry:
	writel(ctrl | DBGP_GO, &ehci_debug->control);
	ret = dbgp_wait_until_complete();
	pids = readl(&ehci_debug->pids);
	lpid = DBGP_PID_GET(pids);

	if (ret < 0)
		return ret;

	/*
	 * If the port is getting full or it has dropped data
	 * start pacing ourselves, not necessary but it's friendly.
	 */
	if ((lpid == USB_PID_NAK) || (lpid == USB_PID_NYET))
		dbgp_breath();

	/* If I get a NACK reissue the transmission */
	if (lpid == USB_PID_NAK) {
		if (--loop > 0)
			goto retry;
	}

	return ret;
}

static void dbgp_set_data(const void *buf, int size)
{
	const unsigned char *bytes = buf;
	u32 lo, hi;
	int i;

	lo = hi = 0;
	for (i = 0; i < 4 && i < size; i++)
		lo |= bytes[i] << (8*i);
	for (; i < 8 && i < size; i++)
		hi |= bytes[i] << (8*(i - 4));
	writel(lo, &ehci_debug->data03);
	writel(hi, &ehci_debug->data47);
}

static void dbgp_get_data(void *buf, int size)
{
	unsigned char *bytes = buf;
	u32 lo, hi;
	int i;

	lo = readl(&ehci_debug->data03);
	hi = readl(&ehci_debug->data47);
	for (i = 0; i < 4 && i < size; i++)
		bytes[i] = (lo >> (8*i)) & 0xff;
	for (; i < 8 && i < size; i++)
		bytes[i] = (hi >> (8*(i - 4))) & 0xff;
}

static int dbgp_bulk_write(unsigned devnum, unsigned endpoint,
			 const char *bytes, int size)
{
	u32 pids, addr, ctrl;
	int ret;

	if (size > DBGP_MAX_PACKET)
		return -1;

	addr = DBGP_EPADDR(devnum, endpoint);

	pids = readl(&ehci_debug->pids);
	pids = dbgp_pid_update(pids, USB_PID_OUT);

	ctrl = readl(&ehci_debug->control);
	ctrl = dbgp_len_update(ctrl, size);
	ctrl |= DBGP_OUT;
	ctrl |= DBGP_GO;

	dbgp_set_data(bytes, size);
	writel(addr, &ehci_debug->address);
	writel(pids, &ehci_debug->pids);

	ret = dbgp_wait_until_done(ctrl);
	if (ret < 0)
		return ret;

	return ret;
}

static int dbgp_bulk_read(unsigned devnum, unsigned endpoint, void *data,
				 int size)
{
	u32 pids, addr, ctrl;
	int ret;

	if (size > DBGP_MAX_PACKET)
		return -1;

	addr = DBGP_EPADDR(devnum, endpoint);

	pids = readl(&ehci_debug->pids);
	pids = dbgp_pid_update(pids, USB_PID_IN);

	ctrl = readl(&ehci_debug->control);
	ctrl = dbgp_len_update(ctrl, size);
	ctrl &= ~DBGP_OUT;
	ctrl |= DBGP_GO;

	writel(addr, &ehci_debug->address);
	writel(pids, &ehci_debug->pids);
	ret = dbgp_wait_until_done(ctrl);
	if (ret < 0)
		return ret;

	if (size > ret)
		size = ret;
	dbgp_get_data(data, size);
	return ret;
}

static int dbgp_control_msg(unsigned devnum, int requesttype, int request,
	int value, int index, void *data, int size)
{
	u32 pids, addr, ctrl;
	struct usb_ctrlrequest req;
	int read;
	int ret;

	read = (requesttype & USB_DIR_IN) != 0;
	if (size > (read ? DBGP_MAX_PACKET:0))
		return -1;

	/* Compute the control message */
	req.bRequestType = requesttype;
	req.bRequest = request;
	req.wValue = cpu_to_le16(value);
	req.wIndex = cpu_to_le16(index);
	req.wLength = cpu_to_le16(size);

	pids = DBGP_PID_SET(USB_PID_DATA0, USB_PID_SETUP);
	addr = DBGP_EPADDR(devnum, 0);

	ctrl = readl(&ehci_debug->control);
	ctrl = dbgp_len_update(ctrl, sizeof(req));
	ctrl |= DBGP_OUT;
	ctrl |= DBGP_GO;

	/* Send the setup message */
	dbgp_set_data(&req, sizeof(req));
	writel(addr, &ehci_debug->address);
	writel(pids, &ehci_debug->pids);
	ret = dbgp_wait_until_done(ctrl);
	if (ret < 0)
		return ret;

	/* Read the result */
	return dbgp_bulk_read(devnum, 0, data, size);
}


/* Find a PCI capability */
static u32 __init find_cap(u32 num, u32 slot, u32 func, int cap)
{
	u8 pos;
	int bytes;

	if (!(read_pci_config_16(num, slot, func, PCI_STATUS) &
		PCI_STATUS_CAP_LIST))
		return 0;

	pos = read_pci_config_byte(num, slot, func, PCI_CAPABILITY_LIST);
	for (bytes = 0; bytes < 48 && pos >= 0x40; bytes++) {
		u8 id;

		pos &= ~3;
		id = read_pci_config_byte(num, slot, func, pos+PCI_CAP_LIST_ID);
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;

		pos = read_pci_config_byte(num, slot, func,
						 pos+PCI_CAP_LIST_NEXT);
	}
	return 0;
}

static u32 __init __find_dbgp(u32 bus, u32 slot, u32 func)
{
	u32 class;

	class = read_pci_config(bus, slot, func, PCI_CLASS_REVISION);
	if ((class >> 8) != PCI_CLASS_SERIAL_USB_EHCI)
		return 0;

	return find_cap(bus, slot, func, PCI_CAP_ID_EHCI_DEBUG);
}

static u32 __init find_dbgp(int ehci_num, u32 *rbus, u32 *rslot, u32 *rfunc)
{
	u32 bus, slot, func;

	for (bus = 0; bus < 256; bus++) {
		for (slot = 0; slot < 32; slot++) {
			for (func = 0; func < 8; func++) {
				unsigned cap;

				cap = __find_dbgp(bus, slot, func);

				if (!cap)
					continue;
				if (ehci_num-- != 0)
					continue;
				*rbus = bus;
				*rslot = slot;
				*rfunc = func;
				return cap;
			}
		}
	}
	return 0;
}

static int ehci_reset_port(int port)
{
	u32 portsc;
	u32 delay_time, delay;
	int loop;

	/* Reset the usb debug port */
	portsc = readl(&ehci_regs->port_status[port - 1]);
	portsc &= ~PORT_PE;
	portsc |= PORT_RESET;
	writel(portsc, &ehci_regs->port_status[port - 1]);

	delay = HUB_ROOT_RESET_TIME;
	for (delay_time = 0; delay_time < HUB_RESET_TIMEOUT;
	     delay_time += delay) {
		dbgp_mdelay(delay);

		portsc = readl(&ehci_regs->port_status[port - 1]);
		if (portsc & PORT_RESET) {
			/* force reset to complete */
			loop = 2;
			writel(portsc & ~(PORT_RWC_BITS | PORT_RESET),
				&ehci_regs->port_status[port - 1]);
			do {
				portsc = readl(&ehci_regs->port_status[port-1]);
			} while ((portsc & PORT_RESET) && (--loop > 0));
		}

		/* Device went away? */
		if (!(portsc & PORT_CONNECT))
			return -ENOTCONN;

		/* bomb out completely if something weird happend */
		if ((portsc & PORT_CSC))
			return -EINVAL;

		/* If we've finished resetting, then break out of the loop */
		if (!(portsc & PORT_RESET) && (portsc & PORT_PE))
			return 0;
	}
	return -EBUSY;
}

static int ehci_wait_for_port(int port)
{
	u32 status;
	int ret, reps;

	for (reps = 0; reps < 3; reps++) {
		dbgp_mdelay(100);
		status = readl(&ehci_regs->status);
		if (status & STS_PCD) {
			ret = ehci_reset_port(port);
			if (ret == 0)
				return 0;
		}
	}
	return -ENOTCONN;
}

#ifdef DBGP_DEBUG
# define dbgp_printk early_printk
#else
static inline void dbgp_printk(const char *fmt, ...) { }
#endif

typedef void (*set_debug_port_t)(int port);

static void default_set_debug_port(int port)
{
}

static set_debug_port_t set_debug_port = default_set_debug_port;

static void nvidia_set_debug_port(int port)
{
	u32 dword;
	dword = read_pci_config(ehci_dev.bus, ehci_dev.slot, ehci_dev.func,
				 0x74);
	dword &= ~(0x0f<<12);
	dword |= ((port & 0x0f)<<12);
	write_pci_config(ehci_dev.bus, ehci_dev.slot, ehci_dev.func, 0x74,
				 dword);
	dbgp_printk("set debug port to %d\n", port);
}

static void __init detect_set_debug_port(void)
{
	u32 vendorid;

	vendorid = read_pci_config(ehci_dev.bus, ehci_dev.slot, ehci_dev.func,
		 0x00);

	if ((vendorid & 0xffff) == 0x10de) {
		dbgp_printk("using nvidia set_debug_port\n");
		set_debug_port = nvidia_set_debug_port;
	}
}

static int __init ehci_setup(void)
{
	struct usb_debug_descriptor dbgp_desc;
	u32 cmd, ctrl, status, portsc, hcs_params;
	u32 debug_port, new_debug_port = 0, n_ports;
	u32  devnum;
	int ret, i;
	int loop;
	int port_map_tried;
	int playtimes = 3;

try_next_time:
	port_map_tried = 0;

try_next_port:

	hcs_params = readl(&ehci_caps->hcs_params);
	debug_port = HCS_DEBUG_PORT(hcs_params);
	n_ports    = HCS_N_PORTS(hcs_params);

	dbgp_printk("debug_port: %d\n", debug_port);
	dbgp_printk("n_ports:    %d\n", n_ports);

	for (i = 1; i <= n_ports; i++) {
		portsc = readl(&ehci_regs->port_status[i-1]);
		dbgp_printk("portstatus%d: %08x\n", i, portsc);
	}

	if (port_map_tried && (new_debug_port != debug_port)) {
		if (--playtimes) {
			set_debug_port(new_debug_port);
			goto try_next_time;
		}
		return -1;
	}

	loop = 10;
	/* Reset the EHCI controller */
	cmd = readl(&ehci_regs->command);
	cmd |= CMD_RESET;
	writel(cmd, &ehci_regs->command);
	do {
		cmd = readl(&ehci_regs->command);
	} while ((cmd & CMD_RESET) && (--loop > 0));

	if (!loop) {
		dbgp_printk("can not reset ehci\n");
		return -1;
	}
	dbgp_printk("ehci reset done\n");

	/* Claim ownership, but do not enable yet */
	ctrl = readl(&ehci_debug->control);
	ctrl |= DBGP_OWNER;
	ctrl &= ~(DBGP_ENABLED | DBGP_INUSE);
	writel(ctrl, &ehci_debug->control);

	/* Start the ehci running */
	cmd = readl(&ehci_regs->command);
	cmd &= ~(CMD_LRESET | CMD_IAAD | CMD_PSE | CMD_ASE | CMD_RESET);
	cmd |= CMD_RUN;
	writel(cmd, &ehci_regs->command);

	/* Ensure everything is routed to the EHCI */
	writel(FLAG_CF, &ehci_regs->configured_flag);

	/* Wait until the controller is no longer halted */
	loop = 10;
	do {
		status = readl(&ehci_regs->status);
	} while ((status & STS_HALT) && (--loop > 0));

	if (!loop) {
		dbgp_printk("ehci can be started\n");
		return -1;
	}
	dbgp_printk("ehci started\n");

	/* Wait for a device to show up in the debug port */
	ret = ehci_wait_for_port(debug_port);
	if (ret < 0) {
		dbgp_printk("No device found in debug port\n");
		goto next_debug_port;
	}
	dbgp_printk("ehci wait for port done\n");

	/* Enable the debug port */
	ctrl = readl(&ehci_debug->control);
	ctrl |= DBGP_CLAIM;
	writel(ctrl, &ehci_debug->control);
	ctrl = readl(&ehci_debug->control);
	if ((ctrl & DBGP_CLAIM) != DBGP_CLAIM) {
		dbgp_printk("No device in debug port\n");
		writel(ctrl & ~DBGP_CLAIM, &ehci_debug->control);
		goto err;
	}
	dbgp_printk("debug ported enabled\n");

	/* Completely transfer the debug device to the debug controller */
	portsc = readl(&ehci_regs->port_status[debug_port - 1]);
	portsc &= ~PORT_PE;
	writel(portsc, &ehci_regs->port_status[debug_port - 1]);

	dbgp_mdelay(100);

	/* Find the debug device and make it device number 127 */
	for (devnum = 0; devnum <= 127; devnum++) {
		ret = dbgp_control_msg(devnum,
			USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
			USB_REQ_GET_DESCRIPTOR, (USB_DT_DEBUG << 8), 0,
			&dbgp_desc, sizeof(dbgp_desc));
		if (ret > 0)
			break;
	}
	if (devnum > 127) {
		dbgp_printk("Could not find attached debug device\n");
		goto err;
	}
	if (ret < 0) {
		dbgp_printk("Attached device is not a debug device\n");
		goto err;
	}
	dbgp_endpoint_out = dbgp_desc.bDebugOutEndpoint;

	/* Move the device to 127 if it isn't already there */
	if (devnum != USB_DEBUG_DEVNUM) {
		ret = dbgp_control_msg(devnum,
			USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
			USB_REQ_SET_ADDRESS, USB_DEBUG_DEVNUM, 0, NULL, 0);
		if (ret < 0) {
			dbgp_printk("Could not move attached device to %d\n",
				USB_DEBUG_DEVNUM);
			goto err;
		}
		devnum = USB_DEBUG_DEVNUM;
		dbgp_printk("debug device renamed to 127\n");
	}

	/* Enable the debug interface */
	ret = dbgp_control_msg(USB_DEBUG_DEVNUM,
		USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
		USB_REQ_SET_FEATURE, USB_DEVICE_DEBUG_MODE, 0, NULL, 0);
	if (ret < 0) {
		dbgp_printk(" Could not enable the debug device\n");
		goto err;
	}
	dbgp_printk("debug interface enabled\n");

	/* Perform a small write to get the even/odd data state in sync
	 */
	ret = dbgp_bulk_write(USB_DEBUG_DEVNUM, dbgp_endpoint_out, " ", 1);
	if (ret < 0) {
		dbgp_printk("dbgp_bulk_write failed: %d\n", ret);
		goto err;
	}
	dbgp_printk("small write doned\n");

	return 0;
err:
	/* Things didn't work so remove my claim */
	ctrl = readl(&ehci_debug->control);
	ctrl &= ~(DBGP_CLAIM | DBGP_OUT);
	writel(ctrl, &ehci_debug->control);
	return -1;

next_debug_port:
	port_map_tried |= (1<<(debug_port - 1));
	new_debug_port = ((debug_port-1+1)%n_ports) + 1;
	if (port_map_tried != ((1<<n_ports) - 1)) {
		set_debug_port(new_debug_port);
		goto try_next_port;
	}
	if (--playtimes) {
		set_debug_port(new_debug_port);
		goto try_next_time;
	}

	return -1;
}

static int __init early_dbgp_init(char *s)
{
	u32 debug_port, bar, offset;
	u32 bus, slot, func, cap;
	void __iomem *ehci_bar;
	u32 dbgp_num;
	u32 bar_val;
	char *e;
	int ret;
	u8 byte;

	if (!early_pci_allowed())
		return -1;

	dbgp_num = 0;
	if (*s)
		dbgp_num = simple_strtoul(s, &e, 10);
	dbgp_printk("dbgp_num: %d\n", dbgp_num);

	cap = find_dbgp(dbgp_num, &bus, &slot, &func);
	if (!cap)
		return -1;

	dbgp_printk("Found EHCI debug port on %02x:%02x.%1x\n", bus, slot,
			 func);

	debug_port = read_pci_config(bus, slot, func, cap);
	bar = (debug_port >> 29) & 0x7;
	bar = (bar * 4) + 0xc;
	offset = (debug_port >> 16) & 0xfff;
	dbgp_printk("bar: %02x offset: %03x\n", bar, offset);
	if (bar != PCI_BASE_ADDRESS_0) {
		dbgp_printk("only debug ports on bar 1 handled.\n");

		return -1;
	}

	bar_val = read_pci_config(bus, slot, func, PCI_BASE_ADDRESS_0);
	dbgp_printk("bar_val: %02x offset: %03x\n", bar_val, offset);
	if (bar_val & ~PCI_BASE_ADDRESS_MEM_MASK) {
		dbgp_printk("only simple 32bit mmio bars supported\n");

		return -1;
	}

	/* double check if the mem space is enabled */
	byte = read_pci_config_byte(bus, slot, func, 0x04);
	if (!(byte & 0x2)) {
		byte  |= 0x02;
		write_pci_config_byte(bus, slot, func, 0x04, byte);
		dbgp_printk("mmio for ehci enabled\n");
	}

	/*
	 * FIXME I don't have the bar size so just guess PAGE_SIZE is more
	 * than enough.  1K is the biggest I have seen.
	 */
	set_fixmap_nocache(FIX_DBGP_BASE, bar_val & PAGE_MASK);
	ehci_bar = (void __iomem *)__fix_to_virt(FIX_DBGP_BASE);
	ehci_bar += bar_val & ~PAGE_MASK;
	dbgp_printk("ehci_bar: %p\n", ehci_bar);

	ehci_caps  = ehci_bar;
	ehci_regs  = ehci_bar + HC_LENGTH(readl(&ehci_caps->hc_capbase));
	ehci_debug = ehci_bar + offset;
	ehci_dev.bus = bus;
	ehci_dev.slot = slot;
	ehci_dev.func = func;

	detect_set_debug_port();

	ret = ehci_setup();
	if (ret < 0) {
		dbgp_printk("ehci_setup failed\n");
		ehci_debug = NULL;

		return -1;
	}

	return 0;
}

static void early_dbgp_write(struct console *con, const char *str, u32 n)
{
	int chunk, ret;

	if (!ehci_debug)
		return;
	while (n > 0) {
		chunk = n;
		if (chunk > DBGP_MAX_PACKET)
			chunk = DBGP_MAX_PACKET;
		ret = dbgp_bulk_write(USB_DEBUG_DEVNUM,
			dbgp_endpoint_out, str, chunk);
		str += chunk;
		n -= chunk;
	}
}

static struct console early_dbgp_console = {
	.name =		"earlydbg",
	.write =	early_dbgp_write,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};
#endif

/* Console interface to a host file on AMD's SimNow! */

static int simnow_fd;

enum {
	MAGIC1 = 0xBACCD00A,
	MAGIC2 = 0xCA110000,
	XOPEN = 5,
	XWRITE = 4,
};

static noinline long simnow(long cmd, long a, long b, long c)
{
	long ret;

	asm volatile("cpuid" :
		     "=a" (ret) :
		     "b" (a), "c" (b), "d" (c), "0" (MAGIC1), "D" (cmd + MAGIC2));
	return ret;
}

static void __init simnow_init(char *str)
{
	char *fn = "klog";

	if (*str == '=')
		fn = ++str;
	/* error ignored */
	simnow_fd = simnow(XOPEN, (unsigned long)fn, O_WRONLY|O_APPEND|O_CREAT, 0644);
}

static void simnow_write(struct console *con, const char *s, unsigned n)
{
	simnow(XWRITE, simnow_fd, (unsigned long)s, n);
}

static struct console simnow_console = {
	.name =		"simnow",
	.write =	simnow_write,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};

/* Direct interface for emergencies */
static struct console *early_console = &early_vga_console;
static int __initdata early_console_initialized;

asmlinkage void early_printk(const char *fmt, ...)
{
	char buf[512];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = vscnprintf(buf, 512, fmt, ap);
	early_console->write(early_console, buf, n);
	va_end(ap);
}


static int __init setup_early_printk(char *buf)
{
	int keep_early;

	if (!buf)
		return 0;

	if (early_console_initialized)
		return 0;
	early_console_initialized = 1;

	keep_early = (strstr(buf, "keep") != NULL);

	if (!strncmp(buf, "serial", 6)) {
		early_serial_init(buf + 6);
		early_console = &early_serial_console;
	} else if (!strncmp(buf, "ttyS", 4)) {
		early_serial_init(buf);
		early_console = &early_serial_console;
	} else if (!strncmp(buf, "vga", 3)
		&& boot_params.screen_info.orig_video_isVGA == 1) {
		max_xpos = boot_params.screen_info.orig_video_cols;
		max_ypos = boot_params.screen_info.orig_video_lines;
		current_ypos = boot_params.screen_info.orig_y;
		early_console = &early_vga_console;
	} else if (!strncmp(buf, "simnow", 6)) {
		simnow_init(buf + 6);
		early_console = &simnow_console;
		keep_early = 1;
#ifdef CONFIG_EARLY_PRINTK_DBGP
	} else if (!strncmp(buf, "dbgp", 4)) {
		if (early_dbgp_init(buf+4) < 0)
			return 0;
		early_console = &early_dbgp_console;
		/*
		 * usb subsys will reset ehci controller, so don't keep
		 * that early console
		 */
		keep_early = 0;
#endif
#ifdef CONFIG_HVC_XEN
	} else if (!strncmp(buf, "xen", 3)) {
		early_console = &xenboot_console;
#endif
	}

	if (keep_early)
		early_console->flags &= ~CON_BOOT;
	else
		early_console->flags |= CON_BOOT;
	register_console(early_console);
	return 0;
}

static void __init enable_debug_console(char *buf)
{
#ifdef DBGP_DEBUG
	struct console *old_early_console = NULL;

	if (early_console_initialized && early_console) {
		old_early_console = early_console;
		unregister_console(early_console);
		early_console_initialized = 0;
	}

	setup_early_printk(buf);

	if (early_console == old_early_console && old_early_console)
		register_console(old_early_console);
#endif
}

early_param("earlyprintk", setup_early_printk);
