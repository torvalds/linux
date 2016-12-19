#define DEBUG

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include "aeolia.h"

/* There should normally be only one Aeolia device in a system. This allows
 * other kernel code in unrelated subsystems to issue icc requests without
 * having to get a reference to the device. */
static struct apcie_dev *icc_sc;
DEFINE_MUTEX(icc_mutex);

/* The ICC message passing interface seems to be potentially designed to
 * support multiple outstanding requests at once, but the original PS4 OS never
 * does this, so we don't either. */

#define BUF_FULL 0x7f0
#define BUF_EMPTY 0x7f4
#define HDR(x) (offsetof(struct icc_message_hdr, x))
#define REQUEST (sc->icc.spm + APCIE_SPM_ICC_REQUEST)
#define REPLY (sc->icc.spm + APCIE_SPM_ICC_REPLY)

/* Seconds. Yes, some ICC requests can be slow. */
int icc_timeout = 15;

static u16 checksum(const void *p, int length)
{
	const u8 *pp = p;
	u16 sum = 0;
	while (length--)
		sum += *pp++;
	return sum;
}

static void dump_message(struct apcie_dev *sc, int offset)
{
	int len;
	struct icc_message_hdr hdr;
	memcpy_fromio(&hdr, sc->icc.spm + offset, ICC_HDR_SIZE);

	sc_dbg("icc: hdr: [%02x] %02x:%04x unk %x #%d len %d cksum 0x%x\n",
	       hdr.magic, hdr.major, hdr.minor, hdr.unknown, hdr.cookie,
	       hdr.length, hdr.checksum);
	len = min(hdr.length - ICC_HDR_SIZE, ICC_MAX_PAYLOAD);
	if (len > 0) {
		sc_dbg("icc: data:");
		while (len--)
			printk(" %02x", ioread8(sc->icc.spm + (offset++) +
			                         ICC_HDR_SIZE));
		printk("\n");
	}
}

static void handle_message(struct apcie_dev *sc)
{
	u32 rep_empty, rep_full;
	int off, copy_size;
	struct icc_message_hdr msg;

	rep_empty = ioread32(REPLY + BUF_EMPTY);
	rep_full = ioread32(REPLY + BUF_FULL);

	if (rep_empty != 0 || rep_full != 1) {
		sc_err("icc: reply buffer in bad state (%d, %d)\n",
			rep_empty, rep_full);
		return;
	}

	memcpy_fromio(&msg, REPLY, ICC_HDR_SIZE);

	if (msg.minor & ICC_EVENT) {
		if (msg.magic != ICC_EVENT_MAGIC) {
			sc_err("icc: event has bad magic\n");
			dump_message(sc, APCIE_SPM_ICC_REPLY);
			return;
		}
		sc_err("icc: event arrived, not yet supported.\n");
		dump_message(sc, APCIE_SPM_ICC_REPLY);
	} else if (msg.minor & ICC_REPLY) {
		if (msg.magic != ICC_MAGIC) {
			sc_err("icc: reply has bad magic\n");
			dump_message(sc, APCIE_SPM_ICC_REPLY);
			return;
		}
		spin_lock(&sc->icc.reply_lock);
		if (!sc->icc.reply_pending) {
			spin_unlock(&sc->icc.reply_lock);
			sc_err("icc: unexpected reply\n");
			dump_message(sc, APCIE_SPM_ICC_REPLY);
			return;
		}
		if (msg.cookie != sc->icc.request.cookie) {
			spin_unlock(&sc->icc.reply_lock);
			sc_err("icc: reply has bad cookie %d\n", msg.cookie);
			dump_message(sc, APCIE_SPM_ICC_REPLY);
			return;
		}
		if (msg.length < ICC_HDR_SIZE || msg.length > ICC_MAX_SIZE) {
			spin_unlock(&sc->icc.reply_lock);
			sc_err("icc: reply has bad length %d\n", msg.length);
			dump_message(sc, APCIE_SPM_ICC_REPLY);
			return;
		}
		off = ICC_HDR_SIZE;
		copy_size = min(sc->icc.reply_length,
				(int)(msg.length - off));
		memcpy_fromio(sc->icc.reply_buffer, REPLY + off, copy_size);
		off += copy_size;
		while (off < msg.length)
			sc->icc.reply_extra_checksum += ioread8(REPLY + off++);
		sc->icc.reply_pending = false;
		sc->icc.reply_length = copy_size;
		sc->icc.reply = msg;
		spin_unlock(&sc->icc.reply_lock);
		wake_up(&sc->icc.wq);
	} else {
		sc_err("icc: unknown message arrived\n");
		dump_message(sc, APCIE_SPM_ICC_REPLY);
	}
}

static irqreturn_t icc_interrupt(int irq, void *arg)
{
	struct apcie_dev *sc = arg;
	u32 status;
	u32 ret = IRQ_NONE;

	do {
		status = ioread32(sc->bar4 + APCIE_REG_ICC_STATUS);

		if (status & APCIE_ICC_ACK) {
			iowrite32(APCIE_ICC_ACK,
				  sc->bar4 + APCIE_REG_ICC_STATUS);
			ret = IRQ_HANDLED;
		}

		if (status & APCIE_ICC_SEND) {
			iowrite32(APCIE_ICC_SEND,
				  sc->bar4 + APCIE_REG_ICC_STATUS);
			handle_message(sc);
			iowrite32(0, REPLY + BUF_FULL);
			iowrite32(1, REPLY + BUF_EMPTY);
			iowrite32(APCIE_ICC_ACK,
				  sc->bar4 + APCIE_REG_ICC_DOORBELL);
			ret = IRQ_HANDLED;
		}
	} while (status);

	return ret;
}

static int _apcie_icc_cmd(struct apcie_dev *sc, u8 major, u16 minor, const void *data,
		    u16 length, void *reply, u16 reply_length, bool intr)
{
	int ret;
	u32 req_empty, req_full;
	u16 rep_checksum;

	if (length > ICC_MAX_PAYLOAD)
		return -E2BIG;

	sc->icc.request.magic = ICC_MAGIC;
	sc->icc.request.major = major;
	sc->icc.request.minor = minor;
	sc->icc.request.cookie++;
	sc->icc.request.length = ICC_HDR_SIZE + length;
	sc->icc.request.checksum = 0;
	if (sc->icc.request.length < ICC_MIN_SIZE)
		sc->icc.request.length = ICC_MIN_SIZE;

	sc->icc.request.checksum = checksum(&sc->icc.request, ICC_HDR_SIZE);
	sc->icc.request.checksum += checksum(data, length);
	sc->icc.reply_buffer = reply;
	sc->icc.reply_length = reply_length;

	req_empty = ioread32(REQUEST + BUF_EMPTY);
	req_full = ioread32(REQUEST + BUF_FULL);

	if (req_empty != 1 || req_full != 0) {
		sc_err("icc: request buffer is busy: empty=%d full=%d\n",
		       req_empty, req_full);
		return -EIO;
	}

	iowrite32(0, REQUEST + BUF_EMPTY);

	memcpy_toio(REQUEST, &sc->icc.request, ICC_HDR_SIZE);
	memcpy_toio(REQUEST + ICC_HDR_SIZE, data, length);
	if (length < ICC_MIN_PAYLOAD)
		memset_io(REQUEST + ICC_HDR_SIZE + length, 0,
			  ICC_MIN_PAYLOAD - length);

	iowrite32(1, REQUEST + BUF_FULL);

	spin_lock_irq(&sc->icc.reply_lock);
	sc->icc.reply_pending = true;
	spin_unlock_irq(&sc->icc.reply_lock);

	iowrite32(APCIE_ICC_SEND, sc->bar4 + APCIE_REG_ICC_DOORBELL);

	if (intr)
		ret = wait_event_interruptible_timeout(sc->icc.wq,
				!sc->icc.reply_pending, HZ * icc_timeout);
	else
		ret = wait_event_timeout(sc->icc.wq,
				!sc->icc.reply_pending, HZ * icc_timeout);

	spin_lock_irq(&sc->icc.reply_lock);
	sc->icc.reply_buffer = NULL;
	if (ret < 0 || sc->icc.reply_pending) { /* interrupted or timed out */
		sc->icc.reply_pending = false;
		spin_unlock_irq(&sc->icc.reply_lock);
		sc_err("icc: interrupted or timeout: ret = %d\n", rep_checksum);
		return ret < 0 ? -EINTR : -ETIMEDOUT;
	}
	spin_unlock_irq(&sc->icc.reply_lock);

	rep_checksum = sc->icc.reply.checksum;
	sc->icc.reply.checksum = 0;
	rep_checksum -= checksum(&sc->icc.reply, ICC_HDR_SIZE);
	rep_checksum -= checksum(reply, sc->icc.reply_length);
	rep_checksum -= sc->icc.reply_extra_checksum;

	if (rep_checksum) {
		sc_err("icc: checksum mismatch (diff: %x)\n", rep_checksum);
		return -EIO;
	}

	if (sc->icc.reply.major != major ) {
		sc_err("icc: major mismatch\n");
		return -EIO;
	}
	if (sc->icc.reply.minor != (minor | ICC_REPLY) ) {
		sc_err("icc: minor mismatch\n");
		return -EIO;
	}

	return sc->icc.reply.length - ICC_HDR_SIZE;
}

int apcie_icc_cmd(u8 major, u16 minor, const void *data, u16 length,
		   void *reply, u16 reply_length)
{
	int ret;

	mutex_lock(&icc_mutex);
	if (!icc_sc) {
		pr_err("icc: not ready\n");
		return -EAGAIN;
	}
	ret = _apcie_icc_cmd(icc_sc, major, minor, data, length, reply, reply_length,
		       false);
	mutex_unlock(&icc_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(apcie_icc_cmd);

void do_icc_init(void) {
	u8 svc = 0x10;
	u8 reply[0x30];
	static const u8 led_config[] = {
		3, 1, 0, 0,
			0x10, 1, /* Blue: on */
				2, 0xff, 2, 1, 0x00,
			0x11, 1, /* White: off */
				2, 0x00, 2, 1, 0x00,
			0x02, 3, /* Orange: delay and pulse, loop forever */
				1, 0x00, 4, 1, 0xbf,
				2, 0xff, 5, 1, 0xff,
				2, 0x00, 5, 1, 0xff,
	};
	int ret;
	// test: get FW version
	ret = apcie_icc_cmd(2, 6, NULL, 0, reply, 0x30);
	printk("ret=%d, reply %02x %02x %02x %02x %02x %02x %02x %02x\n", ret,
		reply[0], reply[1], reply[2], reply[3],
		reply[4], reply[5], reply[6], reply[7]);
	ret = apcie_icc_cmd(1, 0, &svc, 1, reply, 0x30);
	printk("ret=%d, reply %02x %02x %02x %02x %02x %02x %02x %02x\n", ret,
		reply[0], reply[1], reply[2], reply[3],
		reply[4], reply[5], reply[6], reply[7]);

	/* Set the LED to something nice */
	ret = apcie_icc_cmd(9, 0x20, led_config, ARRAY_SIZE(led_config), reply, 0x30);
	printk("ret=%d, reply %02x %02x %02x %02x %02x %02x %02x %02x\n", ret,
		reply[0], reply[1], reply[2], reply[3],
		reply[4], reply[5], reply[6], reply[7]);
}
int icc_i2c_init(struct apcie_dev *sc);
void icc_i2c_remove(struct apcie_dev *sc);

int apcie_icc_init(struct apcie_dev *sc)
{
	int ret;
	unsigned int mem_devfn = PCI_DEVFN(PCI_SLOT(sc->pdev->devfn), AEOLIA_FUNC_ID_MEM);
	struct pci_dev *mem_dev;
	u32 req_empty, req_full;

	/* ICC makes use of a segment of SPM memory, available via a different
	 * PCI function in Aeolia, so we need to get a handle to it. */
	mem_dev = pci_get_slot(sc->pdev->bus, mem_devfn);
	if (!mem_dev) {
		sc_err("icc: could not get handle to mem device\n");
		return -ENODEV;
	}

	if (!request_mem_region(pci_resource_start(sc->pdev, 4) +
				APCIE_RGN_ICC_BASE, APCIE_RGN_ICC_SIZE,
				"apcie.icc")) {
		sc_err("icc: failed to request ICC register region\n");
		return -EBUSY;
	}

	sc->icc.spm_base = pci_resource_start(mem_dev, 5) + APCIE_SPM_ICC_BASE;
	if (!request_mem_region(sc->icc.spm_base, APCIE_SPM_ICC_SIZE,
				"spm.icc")) {
		sc_err("icc: failed to request ICC SPM region\n");
		ret = -EBUSY;
		goto release_icc;
	}

	sc->icc.spm = ioremap(sc->icc.spm_base, APCIE_SPM_ICC_SIZE);
	if (!sc->icc.spm) {
		sc_err("icc: failed to map ICC portion of SPM\n");
		ret = -EIO;
		goto release_spm;
	}

	spin_lock_init(&sc->icc.reply_lock);
	init_waitqueue_head(&sc->icc.wq);

	/* Clear flags */
	iowrite32(APCIE_ICC_SEND | APCIE_ICC_ACK,
		  sc->bar4 + APCIE_REG_ICC_STATUS);

	ret = request_irq(apcie_irqnum(sc, APCIE_SUBFUNC_ICC),
			  icc_interrupt, IRQF_SHARED, "icc", sc);
	if (ret) {
		sc_err("icc: could not request IRQ: %d\n", ret);
		goto iounmap;
	}

	req_empty = ioread32(REQUEST + BUF_EMPTY);
	req_full = ioread32(REQUEST + BUF_FULL);

	if (req_empty != 1 || req_full != 0) {
		sc_err("icc: request buffer is busy: empty=%d full=%d\n",
		       req_empty, req_full);
		ret = -EIO;
		goto free_irq;
	}

	/* Enable IRQs */
	iowrite32(APCIE_ICC_SEND | APCIE_ICC_ACK,
		  sc->bar4 + APCIE_REG_ICC_IRQ_MASK);

	mutex_lock(&icc_mutex);
	icc_sc = sc;
	mutex_unlock(&icc_mutex);

	ret = icc_i2c_init(sc);
	if (ret) {
		sc_err("icc: i2c init failed: %d\n", ret);
		goto unassign_global;
	}

	do_icc_init();

	return 0;

unassign_global:
	mutex_lock(&icc_mutex);
	icc_sc = NULL;
	mutex_unlock(&icc_mutex);
	iowrite32(0, sc->bar4 + APCIE_REG_ICC_IRQ_MASK);
free_irq:
	free_irq(apcie_irqnum(sc, APCIE_SUBFUNC_ICC), sc);
iounmap:
	iounmap(sc->icc.spm);
release_spm:
	release_mem_region(sc->icc.spm_base, APCIE_SPM_ICC_SIZE);
release_icc:
	release_mem_region(pci_resource_start(sc->pdev, 4) +
			   APCIE_RGN_ICC_BASE, APCIE_RGN_ICC_SIZE);
	return ret;
}

void apcie_icc_remove(struct apcie_dev *sc)
{
	icc_i2c_remove(sc);
	mutex_lock(&icc_mutex);
	icc_sc = NULL;
	mutex_unlock(&icc_mutex);
	free_irq(apcie_irqnum(sc, APCIE_SUBFUNC_ICC), sc);
	iounmap(sc->icc.spm);
	release_mem_region(sc->icc.spm_base, APCIE_SPM_ICC_SIZE);
	release_mem_region(pci_resource_start(sc->pdev, 4) +
			   APCIE_RGN_ICC_BASE, APCIE_RGN_ICC_SIZE);
}

#ifdef CONFIG_PM
void apcie_icc_suspend(struct apcie_dev *sc, pm_message_t state)
{
}

void apcie_icc_resume(struct apcie_dev *sc)
{
}
#endif
