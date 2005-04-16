/*
 * PowerMac G5 SMU driver
 *
 * Copyright 2004 J. Mayer <l_indien@magic.fr>
 * Copyright 2005 Benjamin Herrenschmidt, IBM Corp.
 *
 * Released under the term of the GNU GPL v2.
 */

/*
 * For now, this driver includes:
 * - RTC get & set
 * - reboot & shutdown commands
 * all synchronous with IRQ disabled (ugh)
 *
 * TODO:
 *   rework in a way the PMU driver works, that is asynchronous
 *   with a queue of commands. I'll do that as soon as I have an
 *   SMU based machine at hand. Some more cleanup is needed too,
 *   like maybe fitting it into a platform device, etc...
 *   Also check what's up with cache coherency, and if we really
 *   can't do better than flushing the cache, maybe build a table
 *   of command len/reply len like the PMU driver to only flush
 *   what is actually necessary.
 *   --BenH.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/bootmem.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/smu.h>
#include <asm/sections.h>
#include <asm/abs_addr.h>

#define DEBUG_SMU 1

#ifdef DEBUG_SMU
#define DPRINTK(fmt, args...) do { printk(KERN_DEBUG fmt , ##args); } while (0)
#else
#define DPRINTK(fmt, args...) do { } while (0)
#endif

/*
 * This is the command buffer passed to the SMU hardware
 */
struct smu_cmd_buf {
	u8 cmd;
	u8 length;
	u8 data[0x0FFE];
};

struct smu_device {
	spinlock_t		lock;
	struct device_node	*of_node;
	int			db_ack;		/* doorbell ack GPIO */
	int			db_req;		/* doorbell req GPIO */
	u32 __iomem		*db_buf;	/* doorbell buffer */
	struct smu_cmd_buf	*cmd_buf;	/* command buffer virtual */
	u32			cmd_buf_abs;	/* command buffer absolute */
};

/*
 * I don't think there will ever be more than one SMU, so
 * for now, just hard code that
 */
static struct smu_device	*smu;

/*
 * SMU low level communication stuff
 */
static inline int smu_cmd_stat(struct smu_cmd_buf *cmd_buf, u8 cmd_ack)
{
	rmb();
	return cmd_buf->cmd == cmd_ack && cmd_buf->length != 0;
}

static inline u8 smu_save_ack_cmd(struct smu_cmd_buf *cmd_buf)
{
	return (~cmd_buf->cmd) & 0xff;
}

static void smu_send_cmd(struct smu_device *dev)
{
	/* SMU command buf is currently cacheable, we need a physical
	 * address. This isn't exactly a DMA mapping here, I suspect
	 * the SMU is actually communicating with us via i2c to the
	 * northbridge or the CPU to access RAM.
	 */
	writel(dev->cmd_buf_abs, dev->db_buf);

	/* Ring the SMU doorbell */
	pmac_do_feature_call(PMAC_FTR_WRITE_GPIO, NULL, dev->db_req, 4);
	pmac_do_feature_call(PMAC_FTR_READ_GPIO, NULL, dev->db_req, 4);
}

static int smu_cmd_done(struct smu_device *dev)
{
	unsigned long wait = 0;
	int gpio;

	/* Check the SMU doorbell */
	do  {
		gpio = pmac_do_feature_call(PMAC_FTR_READ_GPIO,
					    NULL, dev->db_ack);
		if ((gpio & 7) == 7)
			return 0;
		udelay(100);
	} while(++wait < 10000);

	printk(KERN_ERR "SMU timeout !\n");
	return -ENXIO;
}

static int smu_do_cmd(struct smu_device *dev)
{
	int rc;
	u8 cmd_ack;

	DPRINTK("SMU do_cmd %02x len=%d %02x\n",
		dev->cmd_buf->cmd, dev->cmd_buf->length,
		dev->cmd_buf->data[0]);

	cmd_ack = smu_save_ack_cmd(dev->cmd_buf);

	/* Clear cmd_buf cache lines */
	flush_inval_dcache_range((unsigned long)dev->cmd_buf,
				 ((unsigned long)dev->cmd_buf) +
				 sizeof(struct smu_cmd_buf));
	smu_send_cmd(dev);
	rc = smu_cmd_done(dev);
	if (rc == 0)
		rc = smu_cmd_stat(dev->cmd_buf, cmd_ack) ? 0 : -1;

	DPRINTK("SMU do_cmd %02x len=%d %02x => %d (%02x)\n",
		dev->cmd_buf->cmd, dev->cmd_buf->length,
		dev->cmd_buf->data[0], rc, cmd_ack);

	return rc;
}

/* RTC low level commands */
static inline int bcd2hex (int n)
{
	return (((n & 0xf0) >> 4) * 10) + (n & 0xf);
}

static inline int hex2bcd (int n)
{
	return ((n / 10) << 4) + (n % 10);
}

#if 0
static inline void smu_fill_set_pwrup_timer_cmd(struct smu_cmd_buf *cmd_buf)
{
	cmd_buf->cmd = 0x8e;
	cmd_buf->length = 8;
	cmd_buf->data[0] = 0x00;
	memset(cmd_buf->data + 1, 0, 7);
}

static inline void smu_fill_get_pwrup_timer_cmd(struct smu_cmd_buf *cmd_buf)
{
	cmd_buf->cmd = 0x8e;
	cmd_buf->length = 1;
	cmd_buf->data[0] = 0x01;
}

static inline void smu_fill_dis_pwrup_timer_cmd(struct smu_cmd_buf *cmd_buf)
{
	cmd_buf->cmd = 0x8e;
	cmd_buf->length = 1;
	cmd_buf->data[0] = 0x02;
}
#endif

static inline void smu_fill_set_rtc_cmd(struct smu_cmd_buf *cmd_buf,
					struct rtc_time *time)
{
	cmd_buf->cmd = 0x8e;
	cmd_buf->length = 8;
	cmd_buf->data[0] = 0x80;
	cmd_buf->data[1] = hex2bcd(time->tm_sec);
	cmd_buf->data[2] = hex2bcd(time->tm_min);
	cmd_buf->data[3] = hex2bcd(time->tm_hour);
	cmd_buf->data[4] = time->tm_wday;
	cmd_buf->data[5] = hex2bcd(time->tm_mday);
	cmd_buf->data[6] = hex2bcd(time->tm_mon) + 1;
	cmd_buf->data[7] = hex2bcd(time->tm_year - 100);
}

static inline void smu_fill_get_rtc_cmd(struct smu_cmd_buf *cmd_buf)
{
	cmd_buf->cmd = 0x8e;
	cmd_buf->length = 1;
	cmd_buf->data[0] = 0x81;
}

static void smu_parse_get_rtc_reply(struct smu_cmd_buf *cmd_buf,
				    struct rtc_time *time)
{
	time->tm_sec = bcd2hex(cmd_buf->data[0]);
	time->tm_min = bcd2hex(cmd_buf->data[1]);
	time->tm_hour = bcd2hex(cmd_buf->data[2]);
	time->tm_wday = bcd2hex(cmd_buf->data[3]);
	time->tm_mday = bcd2hex(cmd_buf->data[4]);
	time->tm_mon = bcd2hex(cmd_buf->data[5]) - 1;
	time->tm_year = bcd2hex(cmd_buf->data[6]) + 100;
}

int smu_get_rtc_time(struct rtc_time *time)
{
	unsigned long flags;
	int rc;

	if (smu == NULL)
		return -ENODEV;

	memset(time, 0, sizeof(struct rtc_time));
	spin_lock_irqsave(&smu->lock, flags);
	smu_fill_get_rtc_cmd(smu->cmd_buf);
	rc = smu_do_cmd(smu);
	if (rc == 0)
		smu_parse_get_rtc_reply(smu->cmd_buf, time);
	spin_unlock_irqrestore(&smu->lock, flags);

	return rc;
}

int smu_set_rtc_time(struct rtc_time *time)
{
	unsigned long flags;
	int rc;

	if (smu == NULL)
		return -ENODEV;

	spin_lock_irqsave(&smu->lock, flags);
	smu_fill_set_rtc_cmd(smu->cmd_buf, time);
	rc = smu_do_cmd(smu);
	spin_unlock_irqrestore(&smu->lock, flags);

	return rc;
}

void smu_shutdown(void)
{
	const unsigned char *command = "SHUTDOWN";
	unsigned long flags;

	if (smu == NULL)
		return;

	spin_lock_irqsave(&smu->lock, flags);
	smu->cmd_buf->cmd = 0xaa;
	smu->cmd_buf->length = strlen(command);
	strcpy(smu->cmd_buf->data, command);
	smu_do_cmd(smu);
	for (;;)
		;
	spin_unlock_irqrestore(&smu->lock, flags);
}

void smu_restart(void)
{
	const unsigned char *command = "RESTART";
	unsigned long flags;

	if (smu == NULL)
		return;

	spin_lock_irqsave(&smu->lock, flags);
	smu->cmd_buf->cmd = 0xaa;
	smu->cmd_buf->length = strlen(command);
	strcpy(smu->cmd_buf->data, command);
	smu_do_cmd(smu);
	for (;;)
		;
	spin_unlock_irqrestore(&smu->lock, flags);
}

int smu_present(void)
{
	return smu != NULL;
}


int smu_init (void)
{
	struct device_node *np;
	u32 *data;

        np = of_find_node_by_type(NULL, "smu");
        if (np == NULL)
		return -ENODEV;

	if (smu_cmdbuf_abs == 0) {
		printk(KERN_ERR "SMU: Command buffer not allocated !\n");
		return -EINVAL;
	}

	smu = alloc_bootmem(sizeof(struct smu_device));
	if (smu == NULL)
		return -ENOMEM;
	memset(smu, 0, sizeof(*smu));

	spin_lock_init(&smu->lock);
	smu->of_node = np;
	/* smu_cmdbuf_abs is in the low 2G of RAM, can be converted to a
	 * 32 bits value safely
	 */
	smu->cmd_buf_abs = (u32)smu_cmdbuf_abs;
	smu->cmd_buf = (struct smu_cmd_buf *)abs_to_virt(smu_cmdbuf_abs);

	np = of_find_node_by_name(NULL, "smu-doorbell");
	if (np == NULL) {
		printk(KERN_ERR "SMU: Can't find doorbell GPIO !\n");
		goto fail;
	}
	data = (u32 *)get_property(np, "reg", NULL);
	of_node_put(np);
	if (data == NULL) {
		printk(KERN_ERR "SMU: Can't find doorbell GPIO address !\n");
		goto fail;
	}

	/* Current setup has one doorbell GPIO that does both doorbell
	 * and ack. GPIOs are at 0x50, best would be to find that out
	 * in the device-tree though.
	 */
	smu->db_req = 0x50 + *data;
	smu->db_ack = 0x50 + *data;

	/* Doorbell buffer is currently hard-coded, I didn't find a proper
	 * device-tree entry giving the address. Best would probably to use
	 * an offset for K2 base though, but let's do it that way for now.
	 */
	smu->db_buf = ioremap(0x8000860c, 0x1000);
	if (smu->db_buf == NULL) {
		printk(KERN_ERR "SMU: Can't map doorbell buffer pointer !\n");
		goto fail;
	}

	sys_ctrler = SYS_CTRLER_SMU;
	return 0;

 fail:
	smu = NULL;
	return -ENXIO;

}
