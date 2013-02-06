/* linux/arch/arm/mach-xxxx/board-c1via-modems.c
 * Copyright (C) 2010 Samsung Electronics. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk.h>

/* inlcude platform specific file */
#include <linux/platform_data/modem.h>
#include <mach/sec_modem.h>
#include <mach/gpio.h>
#include <mach/gpio-exynos5.h>
#include <plat/gpio-cfg.h>
#include <mach/regs-mem.h>
#include <plat/regs-srom.h>

#ifdef CONFIG_USBHUB_USB3503
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/platform_data/usb3503.h>
#include <mach/cpufreq.h>
#include <plat/usb-phy.h>
#endif
#include <plat/devs.h>
#include <plat/ehci.h>

#define SROM_CS0_BASE		0x04000000
#define SROM_WIDTH		0x01000000
#define SROM_NUM_ADDR_BITS	14

/* For "bus width and wait control (BW)" register */
enum sromc_attr {
	SROMC_DATA_16   = 0x1,	/* 16-bit data bus	*/
	SROMC_BYTE_ADDR = 0x2,	/* Byte base address	*/
	SROMC_WAIT_EN   = 0x4,	/* Wait enabled		*/
	SROMC_BYTE_EN   = 0x8,	/* Byte access enabled	*/
	SROMC_MASK      = 0xF
};

/* DPRAM configuration */
struct sromc_cfg {
	enum sromc_attr attr;
	unsigned size;
	unsigned csn;		/* CSn #			*/
	unsigned addr;		/* Start address (physical)	*/
	unsigned end;		/* End address (physical)	*/
};

/* DPRAM access timing configuration */
struct sromc_access_cfg {
	u32 tacs;		/* Address set-up before CSn		*/
	u32 tcos;		/* Chip selection set-up before OEn	*/
	u32 tacc;		/* Access cycle				*/
	u32 tcoh;		/* Chip selection hold on OEn		*/
	u32 tcah;		/* Address holding time after CSn	*/
	u32 tacp;		/* Page mode access cycle at Page mode	*/
	u32 pmc;		/* Page Mode config			*/
};

/* For CMC221 IDPRAM (Internal DPRAM) */
#define CMC_IDPRAM_SIZE		0x4000	/* 16 KB */

/* FOR CMC221 SFR for IDPRAM */
#define CMC_INT2CP_REG		0x10	/* Interrupt to CP            */
#define CMC_INT2AP_REG		0x50
#define CMC_CLR_INT_REG		0x28	/* Clear Interrupt to AP      */
#define CMC_RESET_REG		0x3C
#define CMC_PUT_REG		0x40	/* AP->CP reg for hostbooting */
#define CMC_GET_REG		0x50	/* CP->AP reg for hostbooting */

#define INT_MASK_REQ_ACK_F	0x0020
#define INT_MASK_REQ_ACK_R	0x0010
#define INT_MASK_RES_ACK_F	0x0008
#define INT_MASK_RES_ACK_R	0x0004
#define INT_MASK_SEND_F		0x0002
#define INT_MASK_SEND_R		0x0001

#define INT_MASK_REQ_ACK_RFS	0x0400 /* Request RES_ACK_RFS		*/
#define INT_MASK_RES_ACK_RFS	0x0200 /* Response of REQ_ACK_RFS	*/
#define INT_MASK_SEND_RFS	0x0100 /* Indicate sending RFS data	*/


/* Function prototypes */
static void config_dpram_port_gpio(void);
static void init_sromc(void);
static void setup_sromc(unsigned csn, struct sromc_cfg *cfg,
		struct sromc_access_cfg *acc_cfg);
static void setup_dpram_speed(unsigned csn, struct sromc_access_cfg *acc_cfg);
static int __init init_modem(void);


#ifdef CONFIG_USBHUB_USB3503
static int host_port_enable(int port, int enable);
#else
/*
static int host_port_enable(int port, int enable)
{
	return s5p_ehci_port_control(&s5p_device_ehci, port, enable);
}
*/
#endif

static struct sromc_cfg cmc_idpram_cfg = {
	.attr = SROMC_DATA_16,
	.size = CMC_IDPRAM_SIZE,
};

static struct sromc_access_cfg cmc_idpram_access_cfg[] = {
	[DPRAM_SPEED_LOW] = {
		.tacs = 0x0F << 28,
		.tcos = 0x0F << 24,
		.tacc = 0x1F << 16,
		.tcoh = 0x0F << 12,
		.tcah = 0x0F << 8,
		.tacp = 0x00 << 4,
		.pmc  = 0x00 << 0,
	},
/*	[DPRAM_SPEED_LOW] = {
		.tacs = 0x01 << 28,
		.tcos = 0x01 << 24,
		.tacc = 0x1B << 16,
		.tcoh = 0x01 << 12,
		.tcah = 0x01 << 8,
		.tacp = 0x00 << 4,
		.pmc  = 0x00 << 0,
	},*/
	[DPRAM_SPEED_HIGH] = {
		.tacs = 0x01 << 28,
		.tcos = 0x01 << 24,
		.tacc = 0x0B << 16,
		.tcoh = 0x01 << 12,
		.tcah = 0x01 << 8,
		.tacp = 0x00 << 4,
		.pmc  = 0x00 << 0,
	},
};

/*
	magic_code +
	access_enable +
	fmt_tx_head + fmt_tx_tail + fmt_tx_buff +
	raw_tx_head + raw_tx_tail + raw_tx_buff +
	rfs_tx_head + rfs_tx_tail + rfs_tx_buff +
	fmt_rx_head + fmt_rx_tail + fmt_rx_buff +
	raw_rx_head + raw_rx_tail + raw_rx_buff +
	rfs_rx_head + rfs_rx_tail + rfs_rx_buff +
 =	2 +
	2 +
	4 + 4 + 2040 +
	4 + 4 + 4088 +
	4 + 4 + 1016 +
	4 + 4 + 2040 +
	4 + 4 + 5112 +
	4 + 4 + 2036 +
 =	16384
*/
#define DP_FMT_TX_BUFF_SZ	2040
#define DP_RAW_TX_BUFF_SZ	4088
#define DP_RFS_TX_BUFF_SZ	1016
#define DP_FMT_RX_BUFF_SZ	2040
#define DP_RAW_RX_BUFF_SZ	5112
#define DP_RFS_RX_BUFF_SZ	2036

#define MAX_CMC_IDPRAM_IPC_DEV	(IPC_RFS + 1)	/* FMT, RAW, RFS */

struct dpram_ipc_cfg {
	u16 magic;
	u16 access;

	u32 fmt_tx_head;
	u32 fmt_tx_tail;
	u8  fmt_tx_buff[DP_FMT_TX_BUFF_SZ];

	u32 raw_tx_head;
	u32 raw_tx_tail;
	u8  raw_tx_buff[DP_RAW_TX_BUFF_SZ];

	u32 rfs_tx_head;
	u32 rfs_tx_tail;
	u8  rfs_tx_buff[DP_RFS_TX_BUFF_SZ];

	u32 fmt_rx_head;
	u32 fmt_rx_tail;
	u8  fmt_rx_buff[DP_FMT_RX_BUFF_SZ];

	u32 raw_rx_head;
	u32 raw_rx_tail;
	u8  raw_rx_buff[DP_RAW_RX_BUFF_SZ];

	u32 rfs_rx_head;
	u32 rfs_rx_tail;
	u8  rfs_rx_buff[DP_RFS_RX_BUFF_SZ];
};

struct cmc_dpram_circ {
	u32 __iomem *head;
	u32 __iomem *tail;
	u8  __iomem *buff;
	u32          size;
};

struct cmc_dpram_ipc_device {
	char name[16];
	int  id;

	struct cmc_dpram_circ txq;
	struct cmc_dpram_circ rxq;

	u16 mask_req_ack;
	u16 mask_res_ack;
	u16 mask_send;
};

struct cmc_dpram_ipc_map {
	u16 __iomem *magic;
	u16 __iomem *access;

	struct cmc_dpram_ipc_device dev[MAX_CMC_IDPRAM_IPC_DEV];
};

struct cmc221_idpram_sfr {
	u16 __iomem *int2cp;
	u16 __iomem *int2ap;
	u16 __iomem *clr_int2ap;
	u16 __iomem *reset;
	u16 __iomem *msg2cp;
	u16 __iomem *msg2ap;
};

static struct cmc_dpram_ipc_map cmc_ipc_map;
static u8 *cmc_sfr_base;
static struct cmc221_idpram_sfr cmc_sfr;

/* Function prototypes */
static void cmc_idpram_reset(void);
static void cmc_idpram_setup_speed(enum dpram_speed);
static int  cmc_idpram_wakeup(void);
static void cmc_idpram_sleep(void);
static void cmc_idpram_clr_intr(void);
static u16  cmc_idpram_recv_intr(void);
static void cmc_idpram_send_intr(u16 irq_mask);
static u16  cmc_idpram_recv_msg(void);
static void cmc_idpram_send_msg(u16 msg);

static u16  cmc_idpram_get_magic(void);
static void cmc_idpram_set_magic(u16 value);
static u16  cmc_idpram_get_access(void);
static void cmc_idpram_set_access(u16 value);

static u32  cmc_idpram_get_tx_head(int dev_id);
static u32  cmc_idpram_get_tx_tail(int dev_id);
static void cmc_idpram_set_tx_head(int dev_id, u32 head);
static void cmc_idpram_set_tx_tail(int dev_id, u32 tail);
static u8 __iomem *cmc_idpram_get_tx_buff(int dev_id);
static u32  cmc_idpram_get_tx_buff_size(int dev_id);

static u32  cmc_idpram_get_rx_head(int dev_id);
static u32  cmc_idpram_get_rx_tail(int dev_id);
static void cmc_idpram_set_rx_head(int dev_id, u32 head);
static void cmc_idpram_set_rx_tail(int dev_id, u32 tail);
static u8 __iomem *cmc_idpram_get_rx_buff(int dev_id);
static u32  cmc_idpram_get_rx_buff_size(int dev_id);

static u16  cmc_idpram_get_mask_req_ack(int dev_id);
static u16  cmc_idpram_get_mask_res_ack(int dev_id);
static u16  cmc_idpram_get_mask_send(int dev_id);

static struct modemlink_dpram_control cmc_idpram_ctrl = {
	.reset = cmc_idpram_reset,

	.setup_speed = cmc_idpram_setup_speed,

	.wakeup = cmc_idpram_wakeup,
	.sleep  = cmc_idpram_sleep,

	.clear_intr = cmc_idpram_clr_intr,
	.recv_intr  = cmc_idpram_recv_intr,
	.send_intr  = cmc_idpram_send_intr,
	.recv_msg   = cmc_idpram_recv_msg,
	.send_msg   = cmc_idpram_send_msg,

	.get_magic  = cmc_idpram_get_magic,
	.set_magic  = cmc_idpram_set_magic,
	.get_access = cmc_idpram_get_access,
	.set_access = cmc_idpram_set_access,

	.get_tx_head = cmc_idpram_get_tx_head,
	.get_tx_tail = cmc_idpram_get_tx_tail,
	.set_tx_head = cmc_idpram_set_tx_head,
	.set_tx_tail = cmc_idpram_set_tx_tail,
	.get_tx_buff = cmc_idpram_get_tx_buff,
	.get_tx_buff_size = cmc_idpram_get_tx_buff_size,

	.get_rx_head = cmc_idpram_get_rx_head,
	.get_rx_tail = cmc_idpram_get_rx_tail,
	.set_rx_head = cmc_idpram_set_rx_head,
	.set_rx_tail = cmc_idpram_set_rx_tail,
	.get_rx_buff = cmc_idpram_get_rx_buff,
	.get_rx_buff_size = cmc_idpram_get_rx_buff_size,

	.get_mask_req_ack = cmc_idpram_get_mask_req_ack,
	.get_mask_res_ack = cmc_idpram_get_mask_res_ack,
	.get_mask_send    = cmc_idpram_get_mask_send,

	.dp_base = NULL,
	.dp_size = 0,
	.dp_type = CP_IDPRAM,
	.aligned = 1,

	.dpram_irq        = CMC_IDPRAM_INT_IRQ_00,
	.dpram_irq_flags  = (IRQF_NO_SUSPEND | IRQF_TRIGGER_RISING),
	.dpram_irq_name   = "CMC221_IDPRAM_IRQ",
	.dpram_wlock_name = "CMC221_IDPRAM_WLOCK",

	.max_ipc_dev = MAX_CMC_IDPRAM_IPC_DEV,
};

/*
** UMTS target platform data
*/
static struct modem_io_t umts_io_devices[] = {
	[0] = {
		.name = "umts_boot0",
		.id = 0x0,
		.format = IPC_BOOT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[1] = {
		.name = "umts_ipc0",
		.id = 0x0,
		.format = IPC_FMT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[2] = {
		.name = "umts_rfs0",
		.id = 0x0,
		.format = IPC_RFS,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[3] = {
		.name = "umts_multipdp",
		.id = 0x1,
		.format = IPC_MULTI_RAW,
		.io_type = IODEV_DUMMY,
		/* .links = LINKTYPE(LINKDEV_DPRAM) | LINKTYPE(LINKDEV_USB), */
		.links = LINKTYPE(LINKDEV_DPRAM),
		.tx_link = LINKDEV_DPRAM,
	},
	[4] = {
		.name = "rmnet0",
		.id = 0x2A,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[5] = {
		.name = "rmnet1",
		.id = 0x2B,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[6] = {
		.name = "rmnet2",
		.id = 0x2C,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[7] = {
		.name = "rmnet3",
		.id = 0x2D,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[8] = {
		.name = "umts_rmnet4",
		.id = 0x27,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[9] = {
		.name = "umts_rmnet5",
		.id = 0x3A,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[10] = {
		.name = "umts_router",	/* AT Iface & Dial-up */
		.id = 0x3E,		/* Channel 30 (0x3E & 0x1F) */
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[11] = {
		.name = "umts_csd",
		.id = 0x21,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[12] = {
		.name = "umts_ramdump0",
		.id = 0x0,
		.format = IPC_RAMDUMP,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[13] = {
		.name = "umts_loopback0",
		.id = 0x0,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
	[14] = {
		.name = "umts_dm0",	/* DM Port */
		.id = 0x3F,		/* Channel 31 (0x3F & 0x1F) */
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_DPRAM),
	},
/*
	[15] = {
		.name = "lte_ipc0",
		.id = 0x1,
		.format = IPC_FMT,
		.io_type = IODEV_MISC,
		.links = LINKTYPE(LINKDEV_USB),
	},
*/
};

/*
static int exynos_cpu_frequency_lock(void);
static int exynos_cpu_frequency_unlock(void);
*/

static struct modemlink_pm_data umts_link_pm_data = {
	.name = "umts_link_pm",

	.gpio_link_enable    = 0,
	/*
	.gpio_link_active    = GPIO_ACTIVE_STATE,
	.gpio_link_hostwake  = GPIO_IPC_HOST_WAKEUP,
	.gpio_link_slavewake = GPIO_IPC_SLAVE_WAKEUP,

	.port_enable = host_port_enable,

	.link_reconnect = umts_link_reconnect,
	*/
	.freqlock = ATOMIC_INIT(0),
	/*
	.cpufreq_lock = exynos_cpu_frequency_lock,
	.cpufreq_unlock = exynos_cpu_frequency_unlock,
	*/
};

static struct modem_data umts_modem_data = {
	.name = "cmc221",

	.gpio_cp_on        = CP_CMC221_PMIC_PWRON,
	.gpio_cp_reset     = CP_CMC221_CPU_RST,
	.gpio_phone_active = GPIO_LTE_ACTIVE,

	.gpio_dpram_int    = GPIO_CMC_IDPRAM_INT_00,
	.gpio_dpram_status = GPIO_CMC_IDPRAM_STATUS,
	.gpio_dpram_wakeup = GPIO_CMC_IDPRAM_WAKEUP,
	/*
	.gpio_slave_wakeup = GPIO_IPC_SLAVE_WAKEUP,
	.gpio_host_active  = GPIO_ACTIVE_STATE,
	.gpio_host_wakeup  = GPIO_IPC_HOST_WAKEUP,
	*/
	.modem_net  = UMTS_NETWORK,
	.modem_type = SEC_CMC221,
	/* .link_types = LINKTYPE(LINKDEV_DPRAM) | LINKTYPE(LINKDEV_USB), */
	.link_types = LINKTYPE(LINKDEV_DPRAM),
	.link_name  = "cmc221_idpram",
	.dpram_ctl  = &cmc_idpram_ctrl,

	.num_iodevs = ARRAY_SIZE(umts_io_devices),
	.iodevs     = umts_io_devices,

	.link_pm_data = &umts_link_pm_data,

	.ipc_version = SIPC_VER_41,
};

static struct resource umts_modem_res[] = {
	[0] = {
		.name  = "cp_active_irq",
		.start = LTE_ACTIVE_IRQ,
		.end   = LTE_ACTIVE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device umts_modem = {
	.name = "modem_if",
	.id = 1,
	.num_resources = ARRAY_SIZE(umts_modem_res),
	.resource = umts_modem_res,
	.dev = {
		.platform_data = &umts_modem_data,
	},
};

#define HUB_STATE_OFF 0
#if defined(CONFIG_LINK_DEVICE_HSIC) || defined(CONFIG_LINK_DEVICE_USB)
void set_hsic_lpa_states(int states)
{
	int val = gpio_get_value(umts_modem_data.gpio_cp_reset);

	mif_trace("\n");

	if (val && states == STATE_HSIC_LPA_ENTER) {
		pr_info("mif: usb3503: %s: hub off - lpa\n", __func__);
		host_port_enable(2, 0);
		*(umts_link_pm_data.p_hub_status) = HUB_STATE_OFF;
	}
}
#endif

int get_cp_active_state(void)
{
	return gpio_get_value(umts_modem_data.gpio_phone_active);
}

static void cmc_idpram_reset(void)
{
	iowrite16(1, cmc_sfr.reset);
}

static void cmc_idpram_setup_speed(enum dpram_speed speed)
{
	setup_dpram_speed(cmc_idpram_cfg.csn, &cmc_idpram_access_cfg[speed]);
}

static int cmc_idpram_wakeup(void)
{
	int cnt = 0;
	u16 magic = 0;
	u16 access = 0;

	gpio_set_value(umts_modem_data.gpio_dpram_wakeup, 1);

	cnt = 0;
	while (!gpio_get_value(umts_modem_data.gpio_dpram_status)) {
		if (cnt++ > 10) {
			pr_err("[MDM/E] <%s> gpio_dpram_status == 0\n",
				__func__);
			break;	/* return -EAGAIN; */
		}

		if (in_interrupt())
			mdelay(1);
		else
			msleep_interruptible(1);
	}

	return 0;
}

static void cmc_idpram_sleep(void)
{
	gpio_set_value(umts_modem_data.gpio_dpram_wakeup, 0);
}

static void cmc_idpram_clr_intr(void)
{
	iowrite16(0xFFFF, cmc_sfr.clr_int2ap);
	iowrite16(0, cmc_sfr.int2ap);
}

static u16 cmc_idpram_recv_intr(void)
{
	return ioread16(cmc_sfr.int2ap);
}

static void cmc_idpram_send_intr(u16 irq_mask)
{
	iowrite16(irq_mask, cmc_sfr.int2cp);
}

static u16 cmc_idpram_recv_msg(void)
{
	return ioread16(cmc_sfr.msg2ap);
}

static void cmc_idpram_send_msg(u16 msg)
{
	iowrite16(msg, cmc_sfr.msg2cp);
}

static u16 cmc_idpram_get_magic(void)
{
	return ioread16(cmc_ipc_map.magic);
}

static void cmc_idpram_set_magic(u16 value)
{
	iowrite16(value, cmc_ipc_map.magic);
}

static u16 cmc_idpram_get_access(void)
{
	return ioread16(cmc_ipc_map.access);
}

static void cmc_idpram_set_access(u16 value)
{
	iowrite16(value, cmc_ipc_map.access);
}

static u32 cmc_idpram_get_tx_head(int dev_id)
{
	return ioread32(cmc_ipc_map.dev[dev_id].txq.head);
}

static u32 cmc_idpram_get_tx_tail(int dev_id)
{
	return ioread32(cmc_ipc_map.dev[dev_id].txq.tail);
}

static void cmc_idpram_set_tx_head(int dev_id, u32 head)
{
	int cnt = 100;
	u32 val = 0;

	iowrite32(head, cmc_ipc_map.dev[dev_id].txq.head);

	do {
		/* Check head value written */
		val = ioread32(cmc_ipc_map.dev[dev_id].txq.head);
		if (val == head)
			break;

		pr_err("[MDM/E] <%s> txq.head(%d) != head(%d)\n",
			__func__, val, head);

		/* Write head value again */
		iowrite32(head, cmc_ipc_map.dev[dev_id].txq.head);
	} while (cnt--);
}

static void cmc_idpram_set_tx_tail(int dev_id, u32 tail)
{
	int cnt = 100;
	u32 val = 0;

	iowrite32(tail, cmc_ipc_map.dev[dev_id].txq.tail);

	do {
		/* Check tail value written */
		val = ioread32(cmc_ipc_map.dev[dev_id].txq.tail);
		if (val == tail)
			break;

		pr_err("[MDM/E] <%s> txq.tail(%d) != tail(%d)\n",
			__func__, val, tail);

		/* Write tail value again */
		iowrite32(tail, cmc_ipc_map.dev[dev_id].txq.tail);
	} while (cnt--);
}

static u8 __iomem *cmc_idpram_get_tx_buff(int dev_id)
{
	return cmc_ipc_map.dev[dev_id].txq.buff;
}

static u32 cmc_idpram_get_tx_buff_size(int dev_id)
{
	return cmc_ipc_map.dev[dev_id].txq.size;
}

static u32 cmc_idpram_get_rx_head(int dev_id)
{
	return ioread32(cmc_ipc_map.dev[dev_id].rxq.head);
}

static u32 cmc_idpram_get_rx_tail(int dev_id)
{
	return ioread32(cmc_ipc_map.dev[dev_id].rxq.tail);
}

static void cmc_idpram_set_rx_head(int dev_id, u32 head)
{
	int cnt = 100;
	u32 val = 0;

	iowrite32(head, cmc_ipc_map.dev[dev_id].rxq.head);

	do {
		/* Check head value written */
		val = ioread32(cmc_ipc_map.dev[dev_id].rxq.head);
		if (val == head)
			break;

		pr_err("[MDM/E] <%s> rxq.head(%d) != head(%d)\n",
			__func__, val, head);

		/* Write head value again */
		iowrite32(head, cmc_ipc_map.dev[dev_id].rxq.head);
	} while (cnt--);
}

static void cmc_idpram_set_rx_tail(int dev_id, u32 tail)
{
	int cnt = 100;
	u32 val = 0;

	iowrite32(tail, cmc_ipc_map.dev[dev_id].rxq.tail);

	do {
		/* Check tail value written */
		val = ioread32(cmc_ipc_map.dev[dev_id].rxq.tail);
		if (val == tail)
			break;

		pr_err("[MDM/E] <%s> rxq.tail(%d) != tail(%d)\n",
			__func__, val, tail);

		/* Write tail value again */
		iowrite32(tail, cmc_ipc_map.dev[dev_id].rxq.tail);
	} while (cnt--);
}

static u8 __iomem *cmc_idpram_get_rx_buff(int dev_id)
{
	return cmc_ipc_map.dev[dev_id].rxq.buff;
}

static u32 cmc_idpram_get_rx_buff_size(int dev_id)
{
	return cmc_ipc_map.dev[dev_id].rxq.size;
}

static u16 cmc_idpram_get_mask_req_ack(int dev_id)
{
	return cmc_ipc_map.dev[dev_id].mask_req_ack;
}

static u16 cmc_idpram_get_mask_res_ack(int dev_id)
{
	return cmc_ipc_map.dev[dev_id].mask_res_ack;
}

static u16 cmc_idpram_get_mask_send(int dev_id)
{
	return cmc_ipc_map.dev[dev_id].mask_send;
}

/* Set dynamic environment for a modem */
static void setup_umts_modem_env(void)
{
	/* Config DPRAM control structure */
	cmc_idpram_cfg.csn  = 0;
	cmc_idpram_cfg.addr = SROM_CS0_BASE + (SROM_WIDTH * cmc_idpram_cfg.csn);
	cmc_idpram_cfg.end  = cmc_idpram_cfg.addr + cmc_idpram_cfg.size - 1;

	umts_modem_data.gpio_dpram_int = GPIO_CMC_IDPRAM_INT_00;
}

static void config_umts_modem_gpio(void)
{
	int err = 0;
	unsigned gpio_cp_on        = umts_modem_data.gpio_cp_on;
	unsigned gpio_cp_rst       = umts_modem_data.gpio_cp_reset;
	unsigned gpio_pda_active   = umts_modem_data.gpio_pda_active;
	unsigned gpio_phone_active = umts_modem_data.gpio_phone_active;
	unsigned gpio_active_state = umts_modem_data.gpio_host_active;
	unsigned gpio_host_wakeup  = umts_modem_data.gpio_host_wakeup;
	unsigned gpio_slave_wakeup = umts_modem_data.gpio_slave_wakeup;
	unsigned gpio_dpram_int    = umts_modem_data.gpio_dpram_int;
	unsigned gpio_dpram_status = umts_modem_data.gpio_dpram_status;
	unsigned gpio_dpram_wakeup = umts_modem_data.gpio_dpram_wakeup;

	pr_info("[MDM] <%s>\n", __func__);

	if (gpio_cp_on) {
		err = gpio_request(gpio_cp_on, "CMC_ON");
		if (err) {
			pr_err("fail to request gpio %s\n", "CMC_ON");
		} else {
			gpio_direction_output(gpio_cp_on, 0);
			s3c_gpio_setpull(gpio_cp_on, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_cp_rst) {
		err = gpio_request(gpio_cp_rst, "CMC_RST");
		if (err) {
			pr_err("fail to request gpio %s\n", "CMC_RST");
		} else {
			gpio_direction_output(gpio_cp_rst, 0);
			s3c_gpio_setpull(gpio_cp_rst, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_pda_active) {
		err = gpio_request(gpio_pda_active, "PDA_ACTIVE");
		if (err) {
			pr_err("fail to request gpio %s\n", "PDA_ACTIVE");
		} else {
			gpio_direction_output(gpio_pda_active, 0);
			s3c_gpio_setpull(gpio_pda_active, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_phone_active) {
		err = gpio_request(gpio_phone_active, "CMC_ACTIVE");
		if (err) {
			pr_err("fail to request gpio %s\n", "CMC_ACTIVE");
		} else {
			s3c_gpio_cfgpin(gpio_phone_active, S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(gpio_phone_active, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_active_state) {
		err = gpio_request(gpio_active_state, "CMC_ACTIVE_STATE");
		if (err) {
			pr_err("fail to request gpio %s\n", "CMC_ACTIVE_STATE");
		} else {
			gpio_direction_output(gpio_active_state, 0);
			s3c_gpio_setpull(gpio_active_state, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_slave_wakeup) {
		err = gpio_request(gpio_slave_wakeup, "CMC_SLAVE_WAKEUP");
		if (err) {
			pr_err("fail to request gpio %s\n", "CMC_SLAVE_WAKEUP");
		} else {
			gpio_direction_output(gpio_slave_wakeup, 0);
			s3c_gpio_setpull(gpio_slave_wakeup, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_host_wakeup) {
		err = gpio_request(gpio_host_wakeup, "CMC_HOST_WAKEUP");
		if (err) {
			pr_err("fail to request gpio %s\n", "CMC_HOST_WAKEUP");
		} else {
			s3c_gpio_cfgpin(gpio_host_wakeup, S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(gpio_host_wakeup, S3C_GPIO_PULL_DOWN);
		}
	}

	if (gpio_dpram_int) {
		err = gpio_request(gpio_dpram_int, "CMC_DPRAM_INT");
		if (err) {
			pr_err("fail to request gpio %s\n", "CMC_DPRAM_INT");
		} else {
			/* Configure as a wake-up source */
			s3c_gpio_cfgpin(gpio_dpram_int, S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(gpio_dpram_int, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_dpram_status) {
		err = gpio_request(gpio_dpram_status, "CMC_DPRAM_STATUS");
		if (err) {
			pr_err("fail to request gpio %s\n", "CMC_DPRAM_STATUS");
		} else {
			/* Configure as a wake-up source */
			s3c_gpio_cfgpin(gpio_dpram_status, S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(gpio_dpram_status, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_dpram_wakeup) {
		err = gpio_request(gpio_dpram_wakeup, "CMC_DPRAM_WAKEUP");
		if (err) {
			pr_err("fail to request gpio %s\n", "CMC_DPRAM_WAKEUP");
		} else {
			gpio_direction_output(gpio_dpram_wakeup, 1);
			s3c_gpio_setpull(gpio_dpram_wakeup, S3C_GPIO_PULL_NONE);
		}
	}
}

static u8 *cmc_idpram_remap_mem_region(struct sromc_cfg *cfg)
{
	int			 dp_addr = 0;
	int			 dp_size = 0;
	u8 __iomem              *dp_base = NULL;
	struct dpram_ipc_cfg    *ipc_map = NULL;
	struct cmc_dpram_ipc_device *dev = NULL;

	dp_addr = cfg->addr;
	dp_size = cfg->size;

	dp_base = (u8 *)ioremap_nocache(dp_addr, dp_size);
	if (!dp_base) {
		pr_err("[MDM] <%s> dpram base ioremap fail\n", __func__);
		return NULL;
	}
	pr_info("[MDM] <%s> DPRAM VA=0x%08X\n", __func__, (int)dp_base);

	cmc_sfr_base = (u8 *)ioremap_nocache((dp_addr + dp_size), dp_size);
	if (cmc_sfr_base == NULL) {
		pr_err("[MDM] <%s> Failed in ioremap_nocache()\n", __func__);
		return NULL;
	}

	cmc_sfr.int2cp     = (u16 __iomem *)(cmc_sfr_base + CMC_INT2CP_REG);
	cmc_sfr.int2ap     = (u16 __iomem *)(cmc_sfr_base + CMC_INT2AP_REG);
	cmc_sfr.clr_int2ap = (u16 __iomem *)(cmc_sfr_base + CMC_CLR_INT_REG);
	cmc_sfr.reset      = (u16 __iomem *)(cmc_sfr_base + CMC_RESET_REG);
	cmc_sfr.msg2cp     = (u16 __iomem *)(cmc_sfr_base + CMC_PUT_REG);
	cmc_sfr.msg2ap     = (u16 __iomem *)(cmc_sfr_base + CMC_GET_REG);


	cmc_idpram_ctrl.dp_base = (u8 __iomem *)dp_base;
	cmc_idpram_ctrl.dp_size = dp_size;

	/* Map for IPC */
	ipc_map = (struct dpram_ipc_cfg *)dp_base;

	/* Magic code and access enable fields */
	cmc_ipc_map.magic  = (u16 __iomem *)&ipc_map->magic;
	cmc_ipc_map.access = (u16 __iomem *)&ipc_map->access;

	/* FMT */
	dev = &cmc_ipc_map.dev[IPC_FMT];

	strcpy(dev->name, "FMT");
	dev->id = IPC_FMT;

	dev->txq.head = (u32 __iomem *)&ipc_map->fmt_tx_head;
	dev->txq.tail = (u32 __iomem *)&ipc_map->fmt_tx_tail;
	dev->txq.buff = (u8 __iomem *)&ipc_map->fmt_tx_buff[0];
	dev->txq.size = DP_FMT_TX_BUFF_SZ;

	dev->rxq.head = (u32 __iomem *)&ipc_map->fmt_rx_head;
	dev->rxq.tail = (u32 __iomem *)&ipc_map->fmt_rx_tail;
	dev->rxq.buff = (u8 __iomem *)&ipc_map->fmt_rx_buff[0];
	dev->rxq.size = DP_FMT_RX_BUFF_SZ;

	dev->mask_req_ack = INT_MASK_REQ_ACK_F;
	dev->mask_res_ack = INT_MASK_RES_ACK_F;
	dev->mask_send    = INT_MASK_SEND_F;

	/* RAW */
	dev = &cmc_ipc_map.dev[IPC_RAW];

	strcpy(dev->name, "RAW");
	dev->id = IPC_RAW;

	dev->txq.head = (u32 __iomem *)&ipc_map->raw_tx_head;
	dev->txq.tail = (u32 __iomem *)&ipc_map->raw_tx_tail;
	dev->txq.buff = (u8 __iomem *)&ipc_map->raw_tx_buff[0];
	dev->txq.size = DP_RAW_TX_BUFF_SZ;

	dev->rxq.head = (u32 __iomem *)&ipc_map->raw_rx_head;
	dev->rxq.tail = (u32 __iomem *)&ipc_map->raw_rx_tail;
	dev->rxq.buff = (u8 __iomem *)&ipc_map->raw_rx_buff[0];
	dev->rxq.size = DP_RAW_RX_BUFF_SZ;

	dev->mask_req_ack = INT_MASK_REQ_ACK_R;
	dev->mask_res_ack = INT_MASK_RES_ACK_R;
	dev->mask_send    = INT_MASK_SEND_R;

	/* RFS */
	dev = &cmc_ipc_map.dev[IPC_RFS];

	strcpy(dev->name, "RFS");
	dev->id = IPC_RFS;

	dev->txq.head = (u32 __iomem *)&ipc_map->rfs_tx_head;
	dev->txq.tail = (u32 __iomem *)&ipc_map->rfs_tx_tail;
	dev->txq.buff = (u8 __iomem *)&ipc_map->rfs_tx_buff[0];
	dev->txq.size = DP_RFS_TX_BUFF_SZ;

	dev->rxq.head = (u32 __iomem *)&ipc_map->rfs_rx_head;
	dev->rxq.tail = (u32 __iomem *)&ipc_map->rfs_rx_tail;
	dev->rxq.buff = (u8 __iomem *)&ipc_map->rfs_rx_buff[0];
	dev->rxq.size = DP_RFS_RX_BUFF_SZ;

	dev->mask_req_ack = INT_MASK_REQ_ACK_RFS;
	dev->mask_res_ack = INT_MASK_RES_ACK_RFS;
	dev->mask_send    = INT_MASK_SEND_RFS;

	return dp_base;
}

/**
 *	DPRAM GPIO settings
 *
 *	SROM_NUM_ADDR_BITS value indicate the address line number or
 *	the mux/demux dpram type. if you want to set mux mode, define the
 *	SROM_NUM_ADDR_BITS to zero.
 *
 *	for CMC22x
 *	CMC22x has 16KB + a SFR register address.
 *	It used 14 bits (13bits for 16KB word address and 1 bit for SFR
 *	register)
 */
static void config_dpram_port_gpio(void)
{
	int addr_bits = SROM_NUM_ADDR_BITS;

	pr_info("[MDM] <%s> address line = %d bits\n", __func__, addr_bits);

	/*
	** Config DPRAM address/data GPIO pins
	*/

	/* Set GPIO for address bus (13 ~ 14 bits) */
	switch (addr_bits) {
	case 0:
		break;

	case 13 ... 14:
		s3c_gpio_cfgrange_nopull(GPIO_SROM_ADDR_BUS_LOW,
			EXYNOS4_GPIO_Y3_NR, S3C_GPIO_SFN(2));
		s3c_gpio_cfgrange_nopull(GPIO_SROM_ADDR_BUS_HIGH,
			(addr_bits - EXYNOS4_GPIO_Y3_NR), S3C_GPIO_SFN(2));
		break;

	default:
		pr_err("[MDM/E] <%s> Invalid addr_bits!!!\n", __func__);
		return;
	}

	/* Set GPIO for data bus (16 bits) */
	s3c_gpio_cfgrange_nopull(GPIO_SROM_DATA_BUS_LOW, 8, S3C_GPIO_SFN(2));
	s3c_gpio_cfgrange_nopull(GPIO_SROM_DATA_BUS_HIGH, 8, S3C_GPIO_SFN(2));

	/* Setup SROMC CSn pins */
	s3c_gpio_cfgpin(GPIO_DPRAM_CSN0, S3C_GPIO_SFN(2));

	/* Config OEn, WEn */
	s3c_gpio_cfgpin(GPIO_DPRAM_REN, S3C_GPIO_SFN(2));
	s3c_gpio_cfgpin(GPIO_DPRAM_WEN, S3C_GPIO_SFN(2));
}

static void init_sromc(void)
{
	struct clk *clk = NULL;

	/* SROMC clk enable */
	clk = clk_get(NULL, "sromc");
	if (!clk) {
		pr_err("[MDM/E] <%s> SROMC clock gate fail\n", __func__);
		return;
	}
	clk_enable(clk);
}

static void setup_sromc
(
	unsigned csn,
	struct sromc_cfg *cfg,
	struct sromc_access_cfg *acc_cfg
)
{
	unsigned bw = 0;	/* Bus width and wait control	*/
	unsigned bc = 0;	/* Vank control			*/
	void __iomem *bank_sfr = S5P_SROM_BC0 + (4 * csn);

	pr_err("[MDM] <%s> SROMC settings for CS%d...\n", __func__, csn);

	bw = __raw_readl(S5P_SROM_BW);
	bc = __raw_readl(bank_sfr);
	pr_err("[MDM] <%s> Old SROMC settings = BW(0x%08X), BC%d(0x%08X)\n",
		__func__, bw, csn, bc);

	/* Set the BW control field for the CSn */
	bw &= ~(SROMC_MASK << (csn << 2));
	bw |= (cfg->attr << (csn << 2));
	writel(bw, S5P_SROM_BW);

	/* Set SROMC memory access timing for the CSn */
	bc = acc_cfg->tacs | acc_cfg->tcos | acc_cfg->tacc |
	     acc_cfg->tcoh | acc_cfg->tcah | acc_cfg->tacp | acc_cfg->pmc;

	writel(bc, bank_sfr);

	/* Verify SROMC settings */
	bw = __raw_readl(S5P_SROM_BW);
	bc = __raw_readl(bank_sfr);
	pr_err("[MDM] <%s> New SROMC settings = BW(0x%08X), BC%d(0x%08X)\n",
		__func__, bw, csn, bc);
}

static void setup_dpram_speed(unsigned csn, struct sromc_access_cfg *acc_cfg)
{
	void __iomem *bank_sfr = S5P_SROM_BC0 + (4 * csn);
	unsigned bc = 0;

	bc = __raw_readl(bank_sfr);
	pr_info("[MDM] <%s> Old CS%d setting = 0x%08X\n", __func__, csn, bc);

	/* SROMC memory access timing setting */
	bc = acc_cfg->tacs | acc_cfg->tcos | acc_cfg->tacc |
	     acc_cfg->tcoh | acc_cfg->tcah | acc_cfg->tacp | acc_cfg->pmc;
	writel(bc, bank_sfr);

	bc = __raw_readl(bank_sfr);
	pr_info("[MDM] <%s> New CS%d setting = 0x%08X\n", __func__, csn, bc);
}

static int __init init_modem(void)
{
	struct sromc_cfg *cfg = NULL;
	struct sromc_access_cfg *acc_cfg = NULL;

	pr_err("[MODEM_IF] <%s> System Revision = %d\n", __func__, system_rev);

	setup_umts_modem_env();

	config_dpram_port_gpio();

	config_umts_modem_gpio();

	init_sromc();

	cfg = &cmc_idpram_cfg;
	acc_cfg = &cmc_idpram_access_cfg[DPRAM_SPEED_LOW];

	setup_sromc(cfg->csn, cfg, acc_cfg);

	if (!cmc_idpram_remap_mem_region(&cmc_idpram_cfg))
		return -1;

	platform_device_register(&umts_modem);

	pr_err("[MODEM_IF] %s: DONE!!\n", __func__);
	return 0;
}
late_initcall(init_modem);
/*device_initcall(init_modem);*/






#ifdef CONFIG_USBHUB_USB3503
static int (*usbhub_set_mode)(struct usb3503_hubctl *, int);
static struct usb3503_hubctl *usbhub_ctl;

#ifdef CONFIG_EXYNOS4_CPUFREQ
static int exynos_cpu_frequency_lock(void)
{
	unsigned int level, freq = 700;

	if (atomic_read(&umts_link_pm_data.freqlock) == 0) {
		if (exynos_cpufreq_get_level(freq * 1000, &level)) {
			pr_err("[MIF] exynos_cpufreq_get_level is fail\n");
			return -EINVAL;
		}

		if (exynos_cpufreq_lock(DVFS_LOCK_ID_USB_IF, level)) {
			pr_err("[MIF] exynos_cpufreq_lock is fail\n");
			return -EINVAL;
		}

		atomic_set(&umts_link_pm_data.freqlock, 1);
		pr_debug("[MIF] %s: <%d>%dMHz\n", __func__, level, freq);
	}
	return 0;
}

static int exynos_cpu_frequency_unlock(void)
{
	if (atomic_read(&umts_link_pm_data.freqlock) == 1) {
		exynos_cpufreq_lock_free(DVFS_LOCK_ID_USB_IF);
		atomic_set(&umts_link_pm_data.freqlock, 0);
		pr_debug("[MIF] %s\n", __func__);
	}
	return 0;
}
#else /* for CONFIG_EXYNOS4_CPUFREQ */
static int exynos_cpu_frequency_lock(void)
{
	return 0;
}

static int exynos_cpu_frequency_unlock(void)
{
	return 0;
}
#endif /* for CONFIG_EXYNOS4_CPUFREQ */

void set_host_states(struct platform_device *pdev, int type)
{
}

static int usb3503_hub_handler(void (*set_mode)(void), void *ctl)
{
	if (!set_mode || !ctl)
		return -EINVAL;

	usbhub_set_mode = (int (*)(struct usb3503_hubctl *, int))set_mode;
	usbhub_ctl = (struct usb3503_hubctl *)ctl;

	pr_info("[MDM] <%s> set_mode(%pF)\n", __func__, set_mode);

	return 0;
}

static int usb3503_hw_config(void)
{
	int err;

	err = gpio_request(GPIO_USB_HUB_RST, "HUB_RST");
	if (err) {
		pr_err("fail to request gpio %s\n", "HUB_RST");
	} else {
		gpio_direction_output(GPIO_USB_HUB_RST, 0);
		s3c_gpio_setpull(GPIO_USB_HUB_RST, S3C_GPIO_PULL_NONE);
	}
	s5p_gpio_set_drvstr(GPIO_USB_HUB_RST, S5P_GPIO_DRVSTR_LV1);
	/* need to check drvstr 1 or 2 */

	/* for USB3503 26Mhz Reference clock setting */
	err = gpio_request(GPIO_USB_HUB_INT, "HUB_INT");
	if (err) {
		pr_err("fail to request gpio %s\n", "HUB_INT");
	} else {
		gpio_direction_output(GPIO_USB_HUB_INT, 1);
		s3c_gpio_setpull(GPIO_USB_HUB_INT, S3C_GPIO_PULL_NONE);
	}

	return 0;
}

static int usb3503_reset_n(int val)
{
	gpio_set_value(GPIO_USB_HUB_RST, 0);

	/* hub off from cpuidle(LPA), skip the msleep schedule*/
	if (val) {
		msleep(20);
		pr_info("[MDM] <%s> val = %d\n", __func__,
			gpio_get_value(GPIO_USB_HUB_RST));
		gpio_set_value(GPIO_USB_HUB_RST, !!val);

		pr_info("[MDM] <%s> val = %d\n", __func__,
			gpio_get_value(GPIO_USB_HUB_RST));

		udelay(5); /* need it ?*/
	}
	return 0;
}

static struct usb3503_platform_data usb3503_pdata = {
	.initial_mode = USB3503_MODE_STANDBY,
	.reset_n = usb3503_reset_n,
	.register_hub_handler = usb3503_hub_handler,
	.port_enable = host_port_enable,
};

static struct i2c_board_info i2c_devs20_emul[] __initdata = {
	{
		I2C_BOARD_INFO(USB3503_I2C_NAME, 0x08),
		.platform_data = &usb3503_pdata,
	},
};

/* I2C20_EMUL */
static struct i2c_gpio_platform_data i2c20_platdata = {
	.sda_pin = GPIO_USB_HUB_SDA,
	.scl_pin = GPIO_USB_HUB_SCL,
	/*FIXME: need to timming tunning...  */
	.udelay	= 20,
};

static struct platform_device s3c_device_i2c20 = {
	.name = "i2c-gpio",
	.id = 20,
	.dev.platform_data = &i2c20_platdata,
};

static int __init init_usbhub(void)
{
	usb3503_hw_config();
	i2c_register_board_info(20, i2c_devs20_emul,
				ARRAY_SIZE(i2c_devs20_emul));

	platform_device_register(&s3c_device_i2c20);
	return 0;
}

device_initcall(init_usbhub);

static int host_port_enable(int port, int enable)
{
	int err;

	pr_info("[MDM] <%s> port(%d) control(%d)\n", __func__, port, enable);

	if (enable) {
		err = usbhub_set_mode(usbhub_ctl, USB3503_MODE_HUB);
		if (err < 0) {
			pr_err("[MDM] <%s> hub on fail\n", __func__);
			goto exit;
		}
		err = s5p_ehci_port_control(&s5p_device_ehci, port, 1);
		if (err < 0) {
			pr_err("[MDM] <%s> port(%d) enable fail\n", __func__,
				port);
			goto exit;
		}
	} else {
		err = s5p_ehci_port_control(&s5p_device_ehci, port, 0);
		if (err < 0) {
			pr_err("[MDM] <%s> port(%d) enable fail\n", __func__,
				port);
			goto exit;
		}
		err = usbhub_set_mode(usbhub_ctl, USB3503_MODE_STANDBY);
		if (err < 0) {
			pr_err("[MDM] <%s> hub off fail\n", __func__);
			goto exit;
		}
	}

	err = gpio_direction_output(umts_modem_data.gpio_host_active, enable);
	pr_info("[MDM] <%s> active state err(%d), en(%d), level(%d)\n",
		__func__, err, enable,
		gpio_get_value(umts_modem_data.gpio_host_active));

exit:
	return err;
}
#else /* for CONFIG_USBHUB_USB3503 */
void set_host_states(struct platform_device *pdev, int type)
{
	/*
	if (active_ctl.gpio_initialized) {
		pr_err(" [MODEM_IF] Active States =%d, %s\n", type, pdev->name);
		gpio_direction_output(umts_link_pm_data.gpio_link_active,
			type);
	} else
		active_ctl.gpio_request_host_active = 1;
		*/
	return;
}
#endif /* for CONFIG_USBHUB_USB3503 */

