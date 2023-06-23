// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/serial_8250.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/platform_data/i2c-xiic.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/spi/spi.h>
#include <linux/spi/xilinx_spi.h>
#include <net/devlink.h>
#include <linux/i2c.h>
#include <linux/mtd/mtd.h>
#include <linux/nvmem-consumer.h>
#include <linux/crc16.h>

#define PCI_VENDOR_ID_FACEBOOK			0x1d9b
#define PCI_DEVICE_ID_FACEBOOK_TIMECARD		0x0400

#define PCI_VENDOR_ID_CELESTICA			0x18d4
#define PCI_DEVICE_ID_CELESTICA_TIMECARD	0x1008

static struct class timecard_class = {
	.owner		= THIS_MODULE,
	.name		= "timecard",
};

struct ocp_reg {
	u32	ctrl;
	u32	status;
	u32	select;
	u32	version;
	u32	time_ns;
	u32	time_sec;
	u32	__pad0[2];
	u32	adjust_ns;
	u32	adjust_sec;
	u32	__pad1[2];
	u32	offset_ns;
	u32	offset_window_ns;
	u32	__pad2[2];
	u32	drift_ns;
	u32	drift_window_ns;
	u32	__pad3[6];
	u32	servo_offset_p;
	u32	servo_offset_i;
	u32	servo_drift_p;
	u32	servo_drift_i;
	u32	status_offset;
	u32	status_drift;
};

#define OCP_CTRL_ENABLE		BIT(0)
#define OCP_CTRL_ADJUST_TIME	BIT(1)
#define OCP_CTRL_ADJUST_OFFSET	BIT(2)
#define OCP_CTRL_ADJUST_DRIFT	BIT(3)
#define OCP_CTRL_ADJUST_SERVO	BIT(8)
#define OCP_CTRL_READ_TIME_REQ	BIT(30)
#define OCP_CTRL_READ_TIME_DONE	BIT(31)

#define OCP_STATUS_IN_SYNC	BIT(0)
#define OCP_STATUS_IN_HOLDOVER	BIT(1)

#define OCP_SELECT_CLK_NONE	0
#define OCP_SELECT_CLK_REG	0xfe

struct tod_reg {
	u32	ctrl;
	u32	status;
	u32	uart_polarity;
	u32	version;
	u32	adj_sec;
	u32	__pad0[3];
	u32	uart_baud;
	u32	__pad1[3];
	u32	utc_status;
	u32	leap;
};

#define TOD_CTRL_PROTOCOL	BIT(28)
#define TOD_CTRL_DISABLE_FMT_A	BIT(17)
#define TOD_CTRL_DISABLE_FMT_B	BIT(16)
#define TOD_CTRL_ENABLE		BIT(0)
#define TOD_CTRL_GNSS_MASK	GENMASK(3, 0)
#define TOD_CTRL_GNSS_SHIFT	24

#define TOD_STATUS_UTC_MASK		GENMASK(7, 0)
#define TOD_STATUS_UTC_VALID		BIT(8)
#define TOD_STATUS_LEAP_ANNOUNCE	BIT(12)
#define TOD_STATUS_LEAP_VALID		BIT(16)

struct ts_reg {
	u32	enable;
	u32	error;
	u32	polarity;
	u32	version;
	u32	__pad0[4];
	u32	cable_delay;
	u32	__pad1[3];
	u32	intr;
	u32	intr_mask;
	u32	event_count;
	u32	__pad2[1];
	u32	ts_count;
	u32	time_ns;
	u32	time_sec;
	u32	data_width;
	u32	data;
};

struct pps_reg {
	u32	ctrl;
	u32	status;
	u32	__pad0[6];
	u32	cable_delay;
};

#define PPS_STATUS_FILTER_ERR	BIT(0)
#define PPS_STATUS_SUPERV_ERR	BIT(1)

struct img_reg {
	u32	version;
};

struct gpio_reg {
	u32	gpio1;
	u32	__pad0;
	u32	gpio2;
	u32	__pad1;
};

struct irig_master_reg {
	u32	ctrl;
	u32	status;
	u32	__pad0;
	u32	version;
	u32	adj_sec;
	u32	mode_ctrl;
};

#define IRIG_M_CTRL_ENABLE	BIT(0)

struct irig_slave_reg {
	u32	ctrl;
	u32	status;
	u32	__pad0;
	u32	version;
	u32	adj_sec;
	u32	mode_ctrl;
};

#define IRIG_S_CTRL_ENABLE	BIT(0)

struct dcf_master_reg {
	u32	ctrl;
	u32	status;
	u32	__pad0;
	u32	version;
	u32	adj_sec;
};

#define DCF_M_CTRL_ENABLE	BIT(0)

struct dcf_slave_reg {
	u32	ctrl;
	u32	status;
	u32	__pad0;
	u32	version;
	u32	adj_sec;
};

#define DCF_S_CTRL_ENABLE	BIT(0)

struct signal_reg {
	u32	enable;
	u32	status;
	u32	polarity;
	u32	version;
	u32	__pad0[4];
	u32	cable_delay;
	u32	__pad1[3];
	u32	intr;
	u32	intr_mask;
	u32	__pad2[2];
	u32	start_ns;
	u32	start_sec;
	u32	pulse_ns;
	u32	pulse_sec;
	u32	period_ns;
	u32	period_sec;
	u32	repeat_count;
};

struct frequency_reg {
	u32	ctrl;
	u32	status;
};
#define FREQ_STATUS_VALID	BIT(31)
#define FREQ_STATUS_ERROR	BIT(30)
#define FREQ_STATUS_OVERRUN	BIT(29)
#define FREQ_STATUS_MASK	GENMASK(23, 0)

struct ptp_ocp_flash_info {
	const char *name;
	int pci_offset;
	int data_size;
	void *data;
};

struct ptp_ocp_firmware_header {
	char magic[4];
	__be16 pci_vendor_id;
	__be16 pci_device_id;
	__be32 image_size;
	__be16 hw_revision;
	__be16 crc;
};

#define OCP_FIRMWARE_MAGIC_HEADER "OCPC"

struct ptp_ocp_i2c_info {
	const char *name;
	unsigned long fixed_rate;
	size_t data_size;
	void *data;
};

struct ptp_ocp_ext_info {
	int index;
	irqreturn_t (*irq_fcn)(int irq, void *priv);
	int (*enable)(void *priv, u32 req, bool enable);
};

struct ptp_ocp_ext_src {
	void __iomem		*mem;
	struct ptp_ocp		*bp;
	struct ptp_ocp_ext_info	*info;
	int			irq_vec;
};

enum ptp_ocp_sma_mode {
	SMA_MODE_IN,
	SMA_MODE_OUT,
};

struct ptp_ocp_sma_connector {
	enum	ptp_ocp_sma_mode mode;
	bool	fixed_fcn;
	bool	fixed_dir;
	bool	disabled;
	u8	default_fcn;
};

struct ocp_attr_group {
	u64 cap;
	const struct attribute_group *group;
};

#define OCP_CAP_BASIC	BIT(0)
#define OCP_CAP_SIGNAL	BIT(1)
#define OCP_CAP_FREQ	BIT(2)

struct ptp_ocp_signal {
	ktime_t		period;
	ktime_t		pulse;
	ktime_t		phase;
	ktime_t		start;
	int		duty;
	bool		polarity;
	bool		running;
};

#define OCP_BOARD_ID_LEN		13
#define OCP_SERIAL_LEN			6

struct ptp_ocp {
	struct pci_dev		*pdev;
	struct device		dev;
	spinlock_t		lock;
	struct ocp_reg __iomem	*reg;
	struct tod_reg __iomem	*tod;
	struct pps_reg __iomem	*pps_to_ext;
	struct pps_reg __iomem	*pps_to_clk;
	struct gpio_reg __iomem	*pps_select;
	struct gpio_reg __iomem	*sma_map1;
	struct gpio_reg __iomem	*sma_map2;
	struct irig_master_reg	__iomem *irig_out;
	struct irig_slave_reg	__iomem *irig_in;
	struct dcf_master_reg	__iomem *dcf_out;
	struct dcf_slave_reg	__iomem *dcf_in;
	struct tod_reg		__iomem *nmea_out;
	struct frequency_reg	__iomem *freq_in[4];
	struct ptp_ocp_ext_src	*signal_out[4];
	struct ptp_ocp_ext_src	*pps;
	struct ptp_ocp_ext_src	*ts0;
	struct ptp_ocp_ext_src	*ts1;
	struct ptp_ocp_ext_src	*ts2;
	struct ptp_ocp_ext_src	*ts3;
	struct ptp_ocp_ext_src	*ts4;
	struct img_reg __iomem	*image;
	struct ptp_clock	*ptp;
	struct ptp_clock_info	ptp_info;
	struct platform_device	*i2c_ctrl;
	struct platform_device	*spi_flash;
	struct clk_hw		*i2c_clk;
	struct timer_list	watchdog;
	const struct attribute_group **attr_group;
	const struct ptp_ocp_eeprom_map *eeprom_map;
	struct dentry		*debug_root;
	time64_t		gnss_lost;
	int			id;
	int			n_irqs;
	int			gnss_port;
	int			gnss2_port;
	int			mac_port;	/* miniature atomic clock */
	int			nmea_port;
	bool			fw_loader;
	u8			fw_tag;
	u16			fw_version;
	u8			board_id[OCP_BOARD_ID_LEN];
	u8			serial[OCP_SERIAL_LEN];
	bool			has_eeprom_data;
	u32			pps_req_map;
	int			flash_start;
	u32			utc_tai_offset;
	u32			ts_window_adjust;
	u64			fw_cap;
	struct ptp_ocp_signal	signal[4];
	struct ptp_ocp_sma_connector sma[4];
	const struct ocp_sma_op *sma_op;
};

#define OCP_REQ_TIMESTAMP	BIT(0)
#define OCP_REQ_PPS		BIT(1)

struct ocp_resource {
	unsigned long offset;
	int size;
	int irq_vec;
	int (*setup)(struct ptp_ocp *bp, struct ocp_resource *r);
	void *extra;
	unsigned long bp_offset;
	const char * const name;
};

static int ptp_ocp_register_mem(struct ptp_ocp *bp, struct ocp_resource *r);
static int ptp_ocp_register_i2c(struct ptp_ocp *bp, struct ocp_resource *r);
static int ptp_ocp_register_spi(struct ptp_ocp *bp, struct ocp_resource *r);
static int ptp_ocp_register_serial(struct ptp_ocp *bp, struct ocp_resource *r);
static int ptp_ocp_register_ext(struct ptp_ocp *bp, struct ocp_resource *r);
static int ptp_ocp_fb_board_init(struct ptp_ocp *bp, struct ocp_resource *r);
static irqreturn_t ptp_ocp_ts_irq(int irq, void *priv);
static irqreturn_t ptp_ocp_signal_irq(int irq, void *priv);
static int ptp_ocp_ts_enable(void *priv, u32 req, bool enable);
static int ptp_ocp_signal_from_perout(struct ptp_ocp *bp, int gen,
				      struct ptp_perout_request *req);
static int ptp_ocp_signal_enable(void *priv, u32 req, bool enable);
static int ptp_ocp_sma_store(struct ptp_ocp *bp, const char *buf, int sma_nr);

static const struct ocp_attr_group fb_timecard_groups[];

struct ptp_ocp_eeprom_map {
	u16	off;
	u16	len;
	u32	bp_offset;
	const void * const tag;
};

#define EEPROM_ENTRY(addr, member)				\
	.off = addr,						\
	.len = sizeof_field(struct ptp_ocp, member),		\
	.bp_offset = offsetof(struct ptp_ocp, member)

#define BP_MAP_ENTRY_ADDR(bp, map) ({				\
	(void *)((uintptr_t)(bp) + (map)->bp_offset);		\
})

static struct ptp_ocp_eeprom_map fb_eeprom_map[] = {
	{ EEPROM_ENTRY(0x43, board_id) },
	{ EEPROM_ENTRY(0x00, serial), .tag = "mac" },
	{ }
};

#define bp_assign_entry(bp, res, val) ({				\
	uintptr_t addr = (uintptr_t)(bp) + (res)->bp_offset;		\
	*(typeof(val) *)addr = val;					\
})

#define OCP_RES_LOCATION(member) \
	.name = #member, .bp_offset = offsetof(struct ptp_ocp, member)

#define OCP_MEM_RESOURCE(member) \
	OCP_RES_LOCATION(member), .setup = ptp_ocp_register_mem

#define OCP_SERIAL_RESOURCE(member) \
	OCP_RES_LOCATION(member), .setup = ptp_ocp_register_serial

#define OCP_I2C_RESOURCE(member) \
	OCP_RES_LOCATION(member), .setup = ptp_ocp_register_i2c

#define OCP_SPI_RESOURCE(member) \
	OCP_RES_LOCATION(member), .setup = ptp_ocp_register_spi

#define OCP_EXT_RESOURCE(member) \
	OCP_RES_LOCATION(member), .setup = ptp_ocp_register_ext

/* This is the MSI vector mapping used.
 * 0: PPS (TS5)
 * 1: TS0
 * 2: TS1
 * 3: GNSS1
 * 4: GNSS2
 * 5: MAC
 * 6: TS2
 * 7: I2C controller
 * 8: HWICAP (notused)
 * 9: SPI Flash
 * 10: NMEA
 * 11: Signal Generator 1
 * 12: Signal Generator 2
 * 13: Signal Generator 3
 * 14: Signal Generator 4
 * 15: TS3
 * 16: TS4
 */

static struct ocp_resource ocp_fb_resource[] = {
	{
		OCP_MEM_RESOURCE(reg),
		.offset = 0x01000000, .size = 0x10000,
	},
	{
		OCP_EXT_RESOURCE(ts0),
		.offset = 0x01010000, .size = 0x10000, .irq_vec = 1,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 0,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts1),
		.offset = 0x01020000, .size = 0x10000, .irq_vec = 2,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 1,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts2),
		.offset = 0x01060000, .size = 0x10000, .irq_vec = 6,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 2,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts3),
		.offset = 0x01110000, .size = 0x10000, .irq_vec = 15,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 3,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts4),
		.offset = 0x01120000, .size = 0x10000, .irq_vec = 16,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 4,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	/* Timestamp for PHC and/or PPS generator */
	{
		OCP_EXT_RESOURCE(pps),
		.offset = 0x010C0000, .size = 0x10000, .irq_vec = 0,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 5,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(signal_out[0]),
		.offset = 0x010D0000, .size = 0x10000, .irq_vec = 11,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 1,
			.irq_fcn = ptp_ocp_signal_irq,
			.enable = ptp_ocp_signal_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(signal_out[1]),
		.offset = 0x010E0000, .size = 0x10000, .irq_vec = 12,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 2,
			.irq_fcn = ptp_ocp_signal_irq,
			.enable = ptp_ocp_signal_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(signal_out[2]),
		.offset = 0x010F0000, .size = 0x10000, .irq_vec = 13,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 3,
			.irq_fcn = ptp_ocp_signal_irq,
			.enable = ptp_ocp_signal_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(signal_out[3]),
		.offset = 0x01100000, .size = 0x10000, .irq_vec = 14,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 4,
			.irq_fcn = ptp_ocp_signal_irq,
			.enable = ptp_ocp_signal_enable,
		},
	},
	{
		OCP_MEM_RESOURCE(pps_to_ext),
		.offset = 0x01030000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(pps_to_clk),
		.offset = 0x01040000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(tod),
		.offset = 0x01050000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(irig_in),
		.offset = 0x01070000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(irig_out),
		.offset = 0x01080000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(dcf_in),
		.offset = 0x01090000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(dcf_out),
		.offset = 0x010A0000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(nmea_out),
		.offset = 0x010B0000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(image),
		.offset = 0x00020000, .size = 0x1000,
	},
	{
		OCP_MEM_RESOURCE(pps_select),
		.offset = 0x00130000, .size = 0x1000,
	},
	{
		OCP_MEM_RESOURCE(sma_map1),
		.offset = 0x00140000, .size = 0x1000,
	},
	{
		OCP_MEM_RESOURCE(sma_map2),
		.offset = 0x00220000, .size = 0x1000,
	},
	{
		OCP_I2C_RESOURCE(i2c_ctrl),
		.offset = 0x00150000, .size = 0x10000, .irq_vec = 7,
		.extra = &(struct ptp_ocp_i2c_info) {
			.name = "xiic-i2c",
			.fixed_rate = 50000000,
			.data_size = sizeof(struct xiic_i2c_platform_data),
			.data = &(struct xiic_i2c_platform_data) {
				.num_devices = 2,
				.devices = (struct i2c_board_info[]) {
					{ I2C_BOARD_INFO("24c02", 0x50) },
					{ I2C_BOARD_INFO("24mac402", 0x58),
					  .platform_data = "mac" },
				},
			},
		},
	},
	{
		OCP_SERIAL_RESOURCE(gnss_port),
		.offset = 0x00160000 + 0x1000, .irq_vec = 3,
	},
	{
		OCP_SERIAL_RESOURCE(gnss2_port),
		.offset = 0x00170000 + 0x1000, .irq_vec = 4,
	},
	{
		OCP_SERIAL_RESOURCE(mac_port),
		.offset = 0x00180000 + 0x1000, .irq_vec = 5,
	},
	{
		OCP_SERIAL_RESOURCE(nmea_port),
		.offset = 0x00190000 + 0x1000, .irq_vec = 10,
	},
	{
		OCP_SPI_RESOURCE(spi_flash),
		.offset = 0x00310000, .size = 0x10000, .irq_vec = 9,
		.extra = &(struct ptp_ocp_flash_info) {
			.name = "xilinx_spi", .pci_offset = 0,
			.data_size = sizeof(struct xspi_platform_data),
			.data = &(struct xspi_platform_data) {
				.num_chipselect = 1,
				.bits_per_word = 8,
				.num_devices = 1,
				.devices = &(struct spi_board_info) {
					.modalias = "spi-nor",
				},
			},
		},
	},
	{
		OCP_MEM_RESOURCE(freq_in[0]),
		.offset = 0x01200000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(freq_in[1]),
		.offset = 0x01210000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(freq_in[2]),
		.offset = 0x01220000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(freq_in[3]),
		.offset = 0x01230000, .size = 0x10000,
	},
	{
		.setup = ptp_ocp_fb_board_init,
	},
	{ }
};

static const struct pci_device_id ptp_ocp_pcidev_id[] = {
	{ PCI_DEVICE_DATA(FACEBOOK, TIMECARD, &ocp_fb_resource) },
	{ PCI_DEVICE_DATA(CELESTICA, TIMECARD, &ocp_fb_resource) },
	{ }
};
MODULE_DEVICE_TABLE(pci, ptp_ocp_pcidev_id);

static DEFINE_MUTEX(ptp_ocp_lock);
static DEFINE_IDR(ptp_ocp_idr);

struct ocp_selector {
	const char *name;
	int value;
};

static const struct ocp_selector ptp_ocp_clock[] = {
	{ .name = "NONE",	.value = 0 },
	{ .name = "TOD",	.value = 1 },
	{ .name = "IRIG",	.value = 2 },
	{ .name = "PPS",	.value = 3 },
	{ .name = "PTP",	.value = 4 },
	{ .name = "RTC",	.value = 5 },
	{ .name = "DCF",	.value = 6 },
	{ .name = "REGS",	.value = 0xfe },
	{ .name = "EXT",	.value = 0xff },
	{ }
};

#define SMA_DISABLE		BIT(16)
#define SMA_ENABLE		BIT(15)
#define SMA_SELECT_MASK		GENMASK(14, 0)

static const struct ocp_selector ptp_ocp_sma_in[] = {
	{ .name = "10Mhz",	.value = 0x0000 },
	{ .name = "PPS1",	.value = 0x0001 },
	{ .name = "PPS2",	.value = 0x0002 },
	{ .name = "TS1",	.value = 0x0004 },
	{ .name = "TS2",	.value = 0x0008 },
	{ .name = "IRIG",	.value = 0x0010 },
	{ .name = "DCF",	.value = 0x0020 },
	{ .name = "TS3",	.value = 0x0040 },
	{ .name = "TS4",	.value = 0x0080 },
	{ .name = "FREQ1",	.value = 0x0100 },
	{ .name = "FREQ2",	.value = 0x0200 },
	{ .name = "FREQ3",	.value = 0x0400 },
	{ .name = "FREQ4",	.value = 0x0800 },
	{ .name = "None",	.value = SMA_DISABLE },
	{ }
};

static const struct ocp_selector ptp_ocp_sma_out[] = {
	{ .name = "10Mhz",	.value = 0x0000 },
	{ .name = "PHC",	.value = 0x0001 },
	{ .name = "MAC",	.value = 0x0002 },
	{ .name = "GNSS1",	.value = 0x0004 },
	{ .name = "GNSS2",	.value = 0x0008 },
	{ .name = "IRIG",	.value = 0x0010 },
	{ .name = "DCF",	.value = 0x0020 },
	{ .name = "GEN1",	.value = 0x0040 },
	{ .name = "GEN2",	.value = 0x0080 },
	{ .name = "GEN3",	.value = 0x0100 },
	{ .name = "GEN4",	.value = 0x0200 },
	{ .name = "GND",	.value = 0x2000 },
	{ .name = "VCC",	.value = 0x4000 },
	{ }
};

struct ocp_sma_op {
	const struct ocp_selector *tbl[2];
	void (*init)(struct ptp_ocp *bp);
	u32 (*get)(struct ptp_ocp *bp, int sma_nr);
	int (*set_inputs)(struct ptp_ocp *bp, int sma_nr, u32 val);
	int (*set_output)(struct ptp_ocp *bp, int sma_nr, u32 val);
};

static void
ptp_ocp_sma_init(struct ptp_ocp *bp)
{
	return bp->sma_op->init(bp);
}

static u32
ptp_ocp_sma_get(struct ptp_ocp *bp, int sma_nr)
{
	return bp->sma_op->get(bp, sma_nr);
}

static int
ptp_ocp_sma_set_inputs(struct ptp_ocp *bp, int sma_nr, u32 val)
{
	return bp->sma_op->set_inputs(bp, sma_nr, val);
}

static int
ptp_ocp_sma_set_output(struct ptp_ocp *bp, int sma_nr, u32 val)
{
	return bp->sma_op->set_output(bp, sma_nr, val);
}

static const char *
ptp_ocp_select_name_from_val(const struct ocp_selector *tbl, int val)
{
	int i;

	for (i = 0; tbl[i].name; i++)
		if (tbl[i].value == val)
			return tbl[i].name;
	return NULL;
}

static int
ptp_ocp_select_val_from_name(const struct ocp_selector *tbl, const char *name)
{
	const char *select;
	int i;

	for (i = 0; tbl[i].name; i++) {
		select = tbl[i].name;
		if (!strncasecmp(name, select, strlen(select)))
			return tbl[i].value;
	}
	return -EINVAL;
}

static ssize_t
ptp_ocp_select_table_show(const struct ocp_selector *tbl, char *buf)
{
	ssize_t count;
	int i;

	count = 0;
	for (i = 0; tbl[i].name; i++)
		count += sysfs_emit_at(buf, count, "%s ", tbl[i].name);
	if (count)
		count--;
	count += sysfs_emit_at(buf, count, "\n");
	return count;
}

static int
__ptp_ocp_gettime_locked(struct ptp_ocp *bp, struct timespec64 *ts,
			 struct ptp_system_timestamp *sts)
{
	u32 ctrl, time_sec, time_ns;
	int i;

	ptp_read_system_prets(sts);

	ctrl = OCP_CTRL_READ_TIME_REQ | OCP_CTRL_ENABLE;
	iowrite32(ctrl, &bp->reg->ctrl);

	for (i = 0; i < 100; i++) {
		ctrl = ioread32(&bp->reg->ctrl);
		if (ctrl & OCP_CTRL_READ_TIME_DONE)
			break;
	}
	ptp_read_system_postts(sts);

	if (sts && bp->ts_window_adjust) {
		s64 ns = timespec64_to_ns(&sts->post_ts);

		sts->post_ts = ns_to_timespec64(ns - bp->ts_window_adjust);
	}

	time_ns = ioread32(&bp->reg->time_ns);
	time_sec = ioread32(&bp->reg->time_sec);

	ts->tv_sec = time_sec;
	ts->tv_nsec = time_ns;

	return ctrl & OCP_CTRL_READ_TIME_DONE ? 0 : -ETIMEDOUT;
}

static int
ptp_ocp_gettimex(struct ptp_clock_info *ptp_info, struct timespec64 *ts,
		 struct ptp_system_timestamp *sts)
{
	struct ptp_ocp *bp = container_of(ptp_info, struct ptp_ocp, ptp_info);
	unsigned long flags;
	int err;

	spin_lock_irqsave(&bp->lock, flags);
	err = __ptp_ocp_gettime_locked(bp, ts, sts);
	spin_unlock_irqrestore(&bp->lock, flags);

	return err;
}

static void
__ptp_ocp_settime_locked(struct ptp_ocp *bp, const struct timespec64 *ts)
{
	u32 ctrl, time_sec, time_ns;
	u32 select;

	time_ns = ts->tv_nsec;
	time_sec = ts->tv_sec;

	select = ioread32(&bp->reg->select);
	iowrite32(OCP_SELECT_CLK_REG, &bp->reg->select);

	iowrite32(time_ns, &bp->reg->adjust_ns);
	iowrite32(time_sec, &bp->reg->adjust_sec);

	ctrl = OCP_CTRL_ADJUST_TIME | OCP_CTRL_ENABLE;
	iowrite32(ctrl, &bp->reg->ctrl);

	/* restore clock selection */
	iowrite32(select >> 16, &bp->reg->select);
}

static int
ptp_ocp_settime(struct ptp_clock_info *ptp_info, const struct timespec64 *ts)
{
	struct ptp_ocp *bp = container_of(ptp_info, struct ptp_ocp, ptp_info);
	unsigned long flags;

	spin_lock_irqsave(&bp->lock, flags);
	__ptp_ocp_settime_locked(bp, ts);
	spin_unlock_irqrestore(&bp->lock, flags);

	return 0;
}

static void
__ptp_ocp_adjtime_locked(struct ptp_ocp *bp, u32 adj_val)
{
	u32 select, ctrl;

	select = ioread32(&bp->reg->select);
	iowrite32(OCP_SELECT_CLK_REG, &bp->reg->select);

	iowrite32(adj_val, &bp->reg->offset_ns);
	iowrite32(NSEC_PER_SEC, &bp->reg->offset_window_ns);

	ctrl = OCP_CTRL_ADJUST_OFFSET | OCP_CTRL_ENABLE;
	iowrite32(ctrl, &bp->reg->ctrl);

	/* restore clock selection */
	iowrite32(select >> 16, &bp->reg->select);
}

static void
ptp_ocp_adjtime_coarse(struct ptp_ocp *bp, s64 delta_ns)
{
	struct timespec64 ts;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&bp->lock, flags);
	err = __ptp_ocp_gettime_locked(bp, &ts, NULL);
	if (likely(!err)) {
		set_normalized_timespec64(&ts, ts.tv_sec,
					  ts.tv_nsec + delta_ns);
		__ptp_ocp_settime_locked(bp, &ts);
	}
	spin_unlock_irqrestore(&bp->lock, flags);
}

static int
ptp_ocp_adjtime(struct ptp_clock_info *ptp_info, s64 delta_ns)
{
	struct ptp_ocp *bp = container_of(ptp_info, struct ptp_ocp, ptp_info);
	unsigned long flags;
	u32 adj_ns, sign;

	if (delta_ns > NSEC_PER_SEC || -delta_ns > NSEC_PER_SEC) {
		ptp_ocp_adjtime_coarse(bp, delta_ns);
		return 0;
	}

	sign = delta_ns < 0 ? BIT(31) : 0;
	adj_ns = sign ? -delta_ns : delta_ns;

	spin_lock_irqsave(&bp->lock, flags);
	__ptp_ocp_adjtime_locked(bp, sign | adj_ns);
	spin_unlock_irqrestore(&bp->lock, flags);

	return 0;
}

static int
ptp_ocp_null_adjfine(struct ptp_clock_info *ptp_info, long scaled_ppm)
{
	if (scaled_ppm == 0)
		return 0;

	return -EOPNOTSUPP;
}

static int
ptp_ocp_null_adjphase(struct ptp_clock_info *ptp_info, s32 phase_ns)
{
	return -EOPNOTSUPP;
}

static int
ptp_ocp_enable(struct ptp_clock_info *ptp_info, struct ptp_clock_request *rq,
	       int on)
{
	struct ptp_ocp *bp = container_of(ptp_info, struct ptp_ocp, ptp_info);
	struct ptp_ocp_ext_src *ext = NULL;
	u32 req;
	int err;

	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		req = OCP_REQ_TIMESTAMP;
		switch (rq->extts.index) {
		case 0:
			ext = bp->ts0;
			break;
		case 1:
			ext = bp->ts1;
			break;
		case 2:
			ext = bp->ts2;
			break;
		case 3:
			ext = bp->ts3;
			break;
		case 4:
			ext = bp->ts4;
			break;
		case 5:
			ext = bp->pps;
			break;
		}
		break;
	case PTP_CLK_REQ_PPS:
		req = OCP_REQ_PPS;
		ext = bp->pps;
		break;
	case PTP_CLK_REQ_PEROUT:
		switch (rq->perout.index) {
		case 0:
			/* This is a request for 1PPS on an output SMA.
			 * Allow, but assume manual configuration.
			 */
			if (on && (rq->perout.period.sec != 1 ||
				   rq->perout.period.nsec != 0))
				return -EINVAL;
			return 0;
		case 1:
		case 2:
		case 3:
		case 4:
			req = rq->perout.index - 1;
			ext = bp->signal_out[req];
			err = ptp_ocp_signal_from_perout(bp, req, &rq->perout);
			if (err)
				return err;
			break;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = -ENXIO;
	if (ext)
		err = ext->info->enable(ext, req, on);

	return err;
}

static int
ptp_ocp_verify(struct ptp_clock_info *ptp_info, unsigned pin,
	       enum ptp_pin_function func, unsigned chan)
{
	struct ptp_ocp *bp = container_of(ptp_info, struct ptp_ocp, ptp_info);
	char buf[16];

	switch (func) {
	case PTP_PF_NONE:
		snprintf(buf, sizeof(buf), "IN: None");
		break;
	case PTP_PF_EXTTS:
		/* Allow timestamps, but require sysfs configuration. */
		return 0;
	case PTP_PF_PEROUT:
		/* channel 0 is 1PPS from PHC.
		 * channels 1..4 are the frequency generators.
		 */
		if (chan)
			snprintf(buf, sizeof(buf), "OUT: GEN%d", chan);
		else
			snprintf(buf, sizeof(buf), "OUT: PHC");
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ptp_ocp_sma_store(bp, buf, pin + 1);
}

static const struct ptp_clock_info ptp_ocp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= KBUILD_MODNAME,
	.max_adj	= 100000000,
	.gettimex64	= ptp_ocp_gettimex,
	.settime64	= ptp_ocp_settime,
	.adjtime	= ptp_ocp_adjtime,
	.adjfine	= ptp_ocp_null_adjfine,
	.adjphase	= ptp_ocp_null_adjphase,
	.enable		= ptp_ocp_enable,
	.verify		= ptp_ocp_verify,
	.pps		= true,
	.n_ext_ts	= 6,
	.n_per_out	= 5,
};

static void
__ptp_ocp_clear_drift_locked(struct ptp_ocp *bp)
{
	u32 ctrl, select;

	select = ioread32(&bp->reg->select);
	iowrite32(OCP_SELECT_CLK_REG, &bp->reg->select);

	iowrite32(0, &bp->reg->drift_ns);

	ctrl = OCP_CTRL_ADJUST_DRIFT | OCP_CTRL_ENABLE;
	iowrite32(ctrl, &bp->reg->ctrl);

	/* restore clock selection */
	iowrite32(select >> 16, &bp->reg->select);
}

static void
ptp_ocp_utc_distribute(struct ptp_ocp *bp, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&bp->lock, flags);

	bp->utc_tai_offset = val;

	if (bp->irig_out)
		iowrite32(val, &bp->irig_out->adj_sec);
	if (bp->dcf_out)
		iowrite32(val, &bp->dcf_out->adj_sec);
	if (bp->nmea_out)
		iowrite32(val, &bp->nmea_out->adj_sec);

	spin_unlock_irqrestore(&bp->lock, flags);
}

static void
ptp_ocp_watchdog(struct timer_list *t)
{
	struct ptp_ocp *bp = from_timer(bp, t, watchdog);
	unsigned long flags;
	u32 status, utc_offset;

	status = ioread32(&bp->pps_to_clk->status);

	if (status & PPS_STATUS_SUPERV_ERR) {
		iowrite32(status, &bp->pps_to_clk->status);
		if (!bp->gnss_lost) {
			spin_lock_irqsave(&bp->lock, flags);
			__ptp_ocp_clear_drift_locked(bp);
			spin_unlock_irqrestore(&bp->lock, flags);
			bp->gnss_lost = ktime_get_real_seconds();
		}

	} else if (bp->gnss_lost) {
		bp->gnss_lost = 0;
	}

	/* if GNSS provides correct data we can rely on
	 * it to get leap second information
	 */
	if (bp->tod) {
		status = ioread32(&bp->tod->utc_status);
		utc_offset = status & TOD_STATUS_UTC_MASK;
		if (status & TOD_STATUS_UTC_VALID &&
		    utc_offset != bp->utc_tai_offset)
			ptp_ocp_utc_distribute(bp, utc_offset);
	}

	mod_timer(&bp->watchdog, jiffies + HZ);
}

static void
ptp_ocp_estimate_pci_timing(struct ptp_ocp *bp)
{
	ktime_t start, end;
	ktime_t delay;
	u32 ctrl;

	ctrl = ioread32(&bp->reg->ctrl);
	ctrl = OCP_CTRL_READ_TIME_REQ | OCP_CTRL_ENABLE;

	iowrite32(ctrl, &bp->reg->ctrl);

	start = ktime_get_ns();

	ctrl = ioread32(&bp->reg->ctrl);

	end = ktime_get_ns();

	delay = end - start;
	bp->ts_window_adjust = (delay >> 5) * 3;
}

static int
ptp_ocp_init_clock(struct ptp_ocp *bp)
{
	struct timespec64 ts;
	bool sync;
	u32 ctrl;

	ctrl = OCP_CTRL_ENABLE;
	iowrite32(ctrl, &bp->reg->ctrl);

	/* NO DRIFT Correction */
	/* offset_p:i 1/8, offset_i: 1/16, drift_p: 0, drift_i: 0 */
	iowrite32(0x2000, &bp->reg->servo_offset_p);
	iowrite32(0x1000, &bp->reg->servo_offset_i);
	iowrite32(0,	  &bp->reg->servo_drift_p);
	iowrite32(0,	  &bp->reg->servo_drift_i);

	/* latch servo values */
	ctrl |= OCP_CTRL_ADJUST_SERVO;
	iowrite32(ctrl, &bp->reg->ctrl);

	if ((ioread32(&bp->reg->ctrl) & OCP_CTRL_ENABLE) == 0) {
		dev_err(&bp->pdev->dev, "clock not enabled\n");
		return -ENODEV;
	}

	ptp_ocp_estimate_pci_timing(bp);

	sync = ioread32(&bp->reg->status) & OCP_STATUS_IN_SYNC;
	if (!sync) {
		ktime_get_clocktai_ts64(&ts);
		ptp_ocp_settime(&bp->ptp_info, &ts);
	}

	/* If there is a clock supervisor, then enable the watchdog */
	if (bp->pps_to_clk) {
		timer_setup(&bp->watchdog, ptp_ocp_watchdog, 0);
		mod_timer(&bp->watchdog, jiffies + HZ);
	}

	return 0;
}

static void
ptp_ocp_tod_init(struct ptp_ocp *bp)
{
	u32 ctrl, reg;

	ctrl = ioread32(&bp->tod->ctrl);
	ctrl |= TOD_CTRL_PROTOCOL | TOD_CTRL_ENABLE;
	ctrl &= ~(TOD_CTRL_DISABLE_FMT_A | TOD_CTRL_DISABLE_FMT_B);
	iowrite32(ctrl, &bp->tod->ctrl);

	reg = ioread32(&bp->tod->utc_status);
	if (reg & TOD_STATUS_UTC_VALID)
		ptp_ocp_utc_distribute(bp, reg & TOD_STATUS_UTC_MASK);
}

static const char *
ptp_ocp_tod_proto_name(const int idx)
{
	static const char * const proto_name[] = {
		"NMEA", "NMEA_ZDA", "NMEA_RMC", "NMEA_none",
		"UBX", "UBX_UTC", "UBX_LS", "UBX_none"
	};
	return proto_name[idx];
}

static const char *
ptp_ocp_tod_gnss_name(int idx)
{
	static const char * const gnss_name[] = {
		"ALL", "COMBINED", "GPS", "GLONASS", "GALILEO", "BEIDOU",
		"Unknown"
	};
	if (idx >= ARRAY_SIZE(gnss_name))
		idx = ARRAY_SIZE(gnss_name) - 1;
	return gnss_name[idx];
}

struct ptp_ocp_nvmem_match_info {
	struct ptp_ocp *bp;
	const void * const tag;
};

static int
ptp_ocp_nvmem_match(struct device *dev, const void *data)
{
	const struct ptp_ocp_nvmem_match_info *info = data;

	dev = dev->parent;
	if (!i2c_verify_client(dev) || info->tag != dev->platform_data)
		return 0;

	while ((dev = dev->parent))
		if (dev->driver && !strcmp(dev->driver->name, KBUILD_MODNAME))
			return info->bp == dev_get_drvdata(dev);
	return 0;
}

static inline struct nvmem_device *
ptp_ocp_nvmem_device_get(struct ptp_ocp *bp, const void * const tag)
{
	struct ptp_ocp_nvmem_match_info info = { .bp = bp, .tag = tag };

	return nvmem_device_find(&info, ptp_ocp_nvmem_match);
}

static inline void
ptp_ocp_nvmem_device_put(struct nvmem_device **nvmemp)
{
	if (!IS_ERR_OR_NULL(*nvmemp))
		nvmem_device_put(*nvmemp);
	*nvmemp = NULL;
}

static void
ptp_ocp_read_eeprom(struct ptp_ocp *bp)
{
	const struct ptp_ocp_eeprom_map *map;
	struct nvmem_device *nvmem;
	const void *tag;
	int ret;

	if (!bp->i2c_ctrl)
		return;

	tag = NULL;
	nvmem = NULL;

	for (map = bp->eeprom_map; map->len; map++) {
		if (map->tag != tag) {
			tag = map->tag;
			ptp_ocp_nvmem_device_put(&nvmem);
		}
		if (!nvmem) {
			nvmem = ptp_ocp_nvmem_device_get(bp, tag);
			if (IS_ERR(nvmem)) {
				ret = PTR_ERR(nvmem);
				goto fail;
			}
		}
		ret = nvmem_device_read(nvmem, map->off, map->len,
					BP_MAP_ENTRY_ADDR(bp, map));
		if (ret != map->len)
			goto fail;
	}

	bp->has_eeprom_data = true;

out:
	ptp_ocp_nvmem_device_put(&nvmem);
	return;

fail:
	dev_err(&bp->pdev->dev, "could not read eeprom: %d\n", ret);
	goto out;
}

static int
ptp_ocp_firstchild(struct device *dev, void *data)
{
	return 1;
}

static struct device *
ptp_ocp_find_flash(struct ptp_ocp *bp)
{
	struct device *dev, *last;

	last = NULL;
	dev = &bp->spi_flash->dev;

	while ((dev = device_find_child(dev, NULL, ptp_ocp_firstchild))) {
		if (!strcmp("mtd", dev_bus_name(dev)))
			break;
		put_device(last);
		last = dev;
	}
	put_device(last);

	return dev;
}

static int
ptp_ocp_devlink_fw_image(struct devlink *devlink, const struct firmware *fw,
			 const u8 **data, size_t *size)
{
	struct ptp_ocp *bp = devlink_priv(devlink);
	const struct ptp_ocp_firmware_header *hdr;
	size_t offset, length;
	u16 crc;

	hdr = (const struct ptp_ocp_firmware_header *)fw->data;
	if (memcmp(hdr->magic, OCP_FIRMWARE_MAGIC_HEADER, 4)) {
		devlink_flash_update_status_notify(devlink,
			"No firmware header found, flashing raw image",
			NULL, 0, 0);
		offset = 0;
		length = fw->size;
		goto out;
	}

	if (be16_to_cpu(hdr->pci_vendor_id) != bp->pdev->vendor ||
	    be16_to_cpu(hdr->pci_device_id) != bp->pdev->device) {
		devlink_flash_update_status_notify(devlink,
			"Firmware image compatibility check failed",
			NULL, 0, 0);
		return -EINVAL;
	}

	offset = sizeof(*hdr);
	length = be32_to_cpu(hdr->image_size);
	if (length != (fw->size - offset)) {
		devlink_flash_update_status_notify(devlink,
			"Firmware image size check failed",
			NULL, 0, 0);
		return -EINVAL;
	}

	crc = crc16(0xffff, &fw->data[offset], length);
	if (be16_to_cpu(hdr->crc) != crc) {
		devlink_flash_update_status_notify(devlink,
			"Firmware image CRC check failed",
			NULL, 0, 0);
		return -EINVAL;
	}

out:
	*data = &fw->data[offset];
	*size = length;

	return 0;
}

static int
ptp_ocp_devlink_flash(struct devlink *devlink, struct device *dev,
		      const struct firmware *fw)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct ptp_ocp *bp = devlink_priv(devlink);
	size_t off, len, size, resid, wrote;
	struct erase_info erase;
	size_t base, blksz;
	const u8 *data;
	int err;

	err = ptp_ocp_devlink_fw_image(devlink, fw, &data, &size);
	if (err)
		goto out;

	off = 0;
	base = bp->flash_start;
	blksz = 4096;
	resid = size;

	while (resid) {
		devlink_flash_update_status_notify(devlink, "Flashing",
						   NULL, off, size);

		len = min_t(size_t, resid, blksz);
		erase.addr = base + off;
		erase.len = blksz;

		err = mtd_erase(mtd, &erase);
		if (err)
			goto out;

		err = mtd_write(mtd, base + off, len, &wrote, data + off);
		if (err)
			goto out;

		off += blksz;
		resid -= len;
	}
out:
	return err;
}

static int
ptp_ocp_devlink_flash_update(struct devlink *devlink,
			     struct devlink_flash_update_params *params,
			     struct netlink_ext_ack *extack)
{
	struct ptp_ocp *bp = devlink_priv(devlink);
	struct device *dev;
	const char *msg;
	int err;

	dev = ptp_ocp_find_flash(bp);
	if (!dev) {
		dev_err(&bp->pdev->dev, "Can't find Flash SPI adapter\n");
		return -ENODEV;
	}

	devlink_flash_update_status_notify(devlink, "Preparing to flash",
					   NULL, 0, 0);

	err = ptp_ocp_devlink_flash(devlink, dev, params->fw);

	msg = err ? "Flash error" : "Flash complete";
	devlink_flash_update_status_notify(devlink, msg, NULL, 0, 0);

	put_device(dev);
	return err;
}

static int
ptp_ocp_devlink_info_get(struct devlink *devlink, struct devlink_info_req *req,
			 struct netlink_ext_ack *extack)
{
	struct ptp_ocp *bp = devlink_priv(devlink);
	const char *fw_image;
	char buf[32];
	int err;

	err = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (err)
		return err;

	fw_image = bp->fw_loader ? "loader" : "fw";
	sprintf(buf, "%d.%d", bp->fw_tag, bp->fw_version);
	err = devlink_info_version_running_put(req, fw_image, buf);
	if (err)
		return err;

	if (!bp->has_eeprom_data) {
		ptp_ocp_read_eeprom(bp);
		if (!bp->has_eeprom_data)
			return 0;
	}

	sprintf(buf, "%pM", bp->serial);
	err = devlink_info_serial_number_put(req, buf);
	if (err)
		return err;

	err = devlink_info_version_fixed_put(req,
			DEVLINK_INFO_VERSION_GENERIC_BOARD_ID,
			bp->board_id);
	if (err)
		return err;

	return 0;
}

static const struct devlink_ops ptp_ocp_devlink_ops = {
	.flash_update = ptp_ocp_devlink_flash_update,
	.info_get = ptp_ocp_devlink_info_get,
};

static void __iomem *
__ptp_ocp_get_mem(struct ptp_ocp *bp, resource_size_t start, int size)
{
	struct resource res = DEFINE_RES_MEM_NAMED(start, size, "ptp_ocp");

	return devm_ioremap_resource(&bp->pdev->dev, &res);
}

static void __iomem *
ptp_ocp_get_mem(struct ptp_ocp *bp, struct ocp_resource *r)
{
	resource_size_t start;

	start = pci_resource_start(bp->pdev, 0) + r->offset;
	return __ptp_ocp_get_mem(bp, start, r->size);
}

static void
ptp_ocp_set_irq_resource(struct resource *res, int irq)
{
	struct resource r = DEFINE_RES_IRQ(irq);
	*res = r;
}

static void
ptp_ocp_set_mem_resource(struct resource *res, resource_size_t start, int size)
{
	struct resource r = DEFINE_RES_MEM(start, size);
	*res = r;
}

static int
ptp_ocp_register_spi(struct ptp_ocp *bp, struct ocp_resource *r)
{
	struct ptp_ocp_flash_info *info;
	struct pci_dev *pdev = bp->pdev;
	struct platform_device *p;
	struct resource res[2];
	resource_size_t start;
	int id;

	start = pci_resource_start(pdev, 0) + r->offset;
	ptp_ocp_set_mem_resource(&res[0], start, r->size);
	ptp_ocp_set_irq_resource(&res[1], pci_irq_vector(pdev, r->irq_vec));

	info = r->extra;
	id = pci_dev_id(pdev) << 1;
	id += info->pci_offset;

	p = platform_device_register_resndata(&pdev->dev, info->name, id,
					      res, 2, info->data,
					      info->data_size);
	if (IS_ERR(p))
		return PTR_ERR(p);

	bp_assign_entry(bp, r, p);

	return 0;
}

static struct platform_device *
ptp_ocp_i2c_bus(struct pci_dev *pdev, struct ocp_resource *r, int id)
{
	struct ptp_ocp_i2c_info *info;
	struct resource res[2];
	resource_size_t start;

	info = r->extra;
	start = pci_resource_start(pdev, 0) + r->offset;
	ptp_ocp_set_mem_resource(&res[0], start, r->size);
	ptp_ocp_set_irq_resource(&res[1], pci_irq_vector(pdev, r->irq_vec));

	return platform_device_register_resndata(&pdev->dev, info->name,
						 id, res, 2,
						 info->data, info->data_size);
}

static int
ptp_ocp_register_i2c(struct ptp_ocp *bp, struct ocp_resource *r)
{
	struct pci_dev *pdev = bp->pdev;
	struct ptp_ocp_i2c_info *info;
	struct platform_device *p;
	struct clk_hw *clk;
	char buf[32];
	int id;

	info = r->extra;
	id = pci_dev_id(bp->pdev);

	sprintf(buf, "AXI.%d", id);
	clk = clk_hw_register_fixed_rate(&pdev->dev, buf, NULL, 0,
					 info->fixed_rate);
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	bp->i2c_clk = clk;

	sprintf(buf, "%s.%d", info->name, id);
	devm_clk_hw_register_clkdev(&pdev->dev, clk, NULL, buf);
	p = ptp_ocp_i2c_bus(bp->pdev, r, id);
	if (IS_ERR(p))
		return PTR_ERR(p);

	bp_assign_entry(bp, r, p);

	return 0;
}

/* The expectation is that this is triggered only on error. */
static irqreturn_t
ptp_ocp_signal_irq(int irq, void *priv)
{
	struct ptp_ocp_ext_src *ext = priv;
	struct signal_reg __iomem *reg = ext->mem;
	struct ptp_ocp *bp = ext->bp;
	u32 enable, status;
	int gen;

	gen = ext->info->index - 1;

	enable = ioread32(&reg->enable);
	status = ioread32(&reg->status);

	/* disable generator on error */
	if (status || !enable) {
		iowrite32(0, &reg->intr_mask);
		iowrite32(0, &reg->enable);
		bp->signal[gen].running = false;
	}

	iowrite32(0, &reg->intr);	/* ack interrupt */

	return IRQ_HANDLED;
}

static int
ptp_ocp_signal_set(struct ptp_ocp *bp, int gen, struct ptp_ocp_signal *s)
{
	struct ptp_system_timestamp sts;
	struct timespec64 ts;
	ktime_t start_ns;
	int err;

	if (!s->period)
		return 0;

	if (!s->pulse)
		s->pulse = ktime_divns(s->period * s->duty, 100);

	err = ptp_ocp_gettimex(&bp->ptp_info, &ts, &sts);
	if (err)
		return err;

	start_ns = ktime_set(ts.tv_sec, ts.tv_nsec) + NSEC_PER_MSEC;
	if (!s->start) {
		/* roundup() does not work on 32-bit systems */
		s->start = DIV64_U64_ROUND_UP(start_ns, s->period);
		s->start = ktime_add(s->start, s->phase);
	}

	if (s->duty < 1 || s->duty > 99)
		return -EINVAL;

	if (s->pulse < 1 || s->pulse > s->period)
		return -EINVAL;

	if (s->start < start_ns)
		return -EINVAL;

	bp->signal[gen] = *s;

	return 0;
}

static int
ptp_ocp_signal_from_perout(struct ptp_ocp *bp, int gen,
			   struct ptp_perout_request *req)
{
	struct ptp_ocp_signal s = { };

	s.polarity = bp->signal[gen].polarity;
	s.period = ktime_set(req->period.sec, req->period.nsec);
	if (!s.period)
		return 0;

	if (req->flags & PTP_PEROUT_DUTY_CYCLE) {
		s.pulse = ktime_set(req->on.sec, req->on.nsec);
		s.duty = ktime_divns(s.pulse * 100, s.period);
	}

	if (req->flags & PTP_PEROUT_PHASE)
		s.phase = ktime_set(req->phase.sec, req->phase.nsec);
	else
		s.start = ktime_set(req->start.sec, req->start.nsec);

	return ptp_ocp_signal_set(bp, gen, &s);
}

static int
ptp_ocp_signal_enable(void *priv, u32 req, bool enable)
{
	struct ptp_ocp_ext_src *ext = priv;
	struct signal_reg __iomem *reg = ext->mem;
	struct ptp_ocp *bp = ext->bp;
	struct timespec64 ts;
	int gen;

	gen = ext->info->index - 1;

	iowrite32(0, &reg->intr_mask);
	iowrite32(0, &reg->enable);
	bp->signal[gen].running = false;
	if (!enable)
		return 0;

	ts = ktime_to_timespec64(bp->signal[gen].start);
	iowrite32(ts.tv_sec, &reg->start_sec);
	iowrite32(ts.tv_nsec, &reg->start_ns);

	ts = ktime_to_timespec64(bp->signal[gen].period);
	iowrite32(ts.tv_sec, &reg->period_sec);
	iowrite32(ts.tv_nsec, &reg->period_ns);

	ts = ktime_to_timespec64(bp->signal[gen].pulse);
	iowrite32(ts.tv_sec, &reg->pulse_sec);
	iowrite32(ts.tv_nsec, &reg->pulse_ns);

	iowrite32(bp->signal[gen].polarity, &reg->polarity);
	iowrite32(0, &reg->repeat_count);

	iowrite32(0, &reg->intr);		/* clear interrupt state */
	iowrite32(1, &reg->intr_mask);		/* enable interrupt */
	iowrite32(3, &reg->enable);		/* valid & enable */

	bp->signal[gen].running = true;

	return 0;
}

static irqreturn_t
ptp_ocp_ts_irq(int irq, void *priv)
{
	struct ptp_ocp_ext_src *ext = priv;
	struct ts_reg __iomem *reg = ext->mem;
	struct ptp_clock_event ev;
	u32 sec, nsec;

	if (ext == ext->bp->pps) {
		if (ext->bp->pps_req_map & OCP_REQ_PPS) {
			ev.type = PTP_CLOCK_PPS;
			ptp_clock_event(ext->bp->ptp, &ev);
		}

		if ((ext->bp->pps_req_map & ~OCP_REQ_PPS) == 0)
			goto out;
	}

	/* XXX should fix API - this converts s/ns -> ts -> s/ns */
	sec = ioread32(&reg->time_sec);
	nsec = ioread32(&reg->time_ns);

	ev.type = PTP_CLOCK_EXTTS;
	ev.index = ext->info->index;
	ev.timestamp = sec * NSEC_PER_SEC + nsec;

	ptp_clock_event(ext->bp->ptp, &ev);

out:
	iowrite32(1, &reg->intr);	/* write 1 to ack */

	return IRQ_HANDLED;
}

static int
ptp_ocp_ts_enable(void *priv, u32 req, bool enable)
{
	struct ptp_ocp_ext_src *ext = priv;
	struct ts_reg __iomem *reg = ext->mem;
	struct ptp_ocp *bp = ext->bp;

	if (ext == bp->pps) {
		u32 old_map = bp->pps_req_map;

		if (enable)
			bp->pps_req_map |= req;
		else
			bp->pps_req_map &= ~req;

		/* if no state change, just return */
		if ((!!old_map ^ !!bp->pps_req_map) == 0)
			return 0;
	}

	if (enable) {
		iowrite32(1, &reg->enable);
		iowrite32(1, &reg->intr_mask);
		iowrite32(1, &reg->intr);
	} else {
		iowrite32(0, &reg->intr_mask);
		iowrite32(0, &reg->enable);
	}

	return 0;
}

static void
ptp_ocp_unregister_ext(struct ptp_ocp_ext_src *ext)
{
	ext->info->enable(ext, ~0, false);
	pci_free_irq(ext->bp->pdev, ext->irq_vec, ext);
	kfree(ext);
}

static int
ptp_ocp_register_ext(struct ptp_ocp *bp, struct ocp_resource *r)
{
	struct pci_dev *pdev = bp->pdev;
	struct ptp_ocp_ext_src *ext;
	int err;

	ext = kzalloc(sizeof(*ext), GFP_KERNEL);
	if (!ext)
		return -ENOMEM;

	ext->mem = ptp_ocp_get_mem(bp, r);
	if (IS_ERR(ext->mem)) {
		err = PTR_ERR(ext->mem);
		goto out;
	}

	ext->bp = bp;
	ext->info = r->extra;
	ext->irq_vec = r->irq_vec;

	err = pci_request_irq(pdev, r->irq_vec, ext->info->irq_fcn, NULL,
			      ext, "ocp%d.%s", bp->id, r->name);
	if (err) {
		dev_err(&pdev->dev, "Could not get irq %d\n", r->irq_vec);
		goto out;
	}

	bp_assign_entry(bp, r, ext);

	return 0;

out:
	kfree(ext);
	return err;
}

static int
ptp_ocp_serial_line(struct ptp_ocp *bp, struct ocp_resource *r)
{
	struct pci_dev *pdev = bp->pdev;
	struct uart_8250_port uart;

	/* Setting UPF_IOREMAP and leaving port.membase unspecified lets
	 * the serial port device claim and release the pci resource.
	 */
	memset(&uart, 0, sizeof(uart));
	uart.port.dev = &pdev->dev;
	uart.port.iotype = UPIO_MEM;
	uart.port.regshift = 2;
	uart.port.mapbase = pci_resource_start(pdev, 0) + r->offset;
	uart.port.irq = pci_irq_vector(pdev, r->irq_vec);
	uart.port.uartclk = 50000000;
	uart.port.flags = UPF_FIXED_TYPE | UPF_IOREMAP | UPF_NO_THRE_TEST;
	uart.port.type = PORT_16550A;

	return serial8250_register_8250_port(&uart);
}

static int
ptp_ocp_register_serial(struct ptp_ocp *bp, struct ocp_resource *r)
{
	int port;

	port = ptp_ocp_serial_line(bp, r);
	if (port < 0)
		return port;

	bp_assign_entry(bp, r, port);

	return 0;
}

static int
ptp_ocp_register_mem(struct ptp_ocp *bp, struct ocp_resource *r)
{
	void __iomem *mem;

	mem = ptp_ocp_get_mem(bp, r);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	bp_assign_entry(bp, r, mem);

	return 0;
}

static void
ptp_ocp_nmea_out_init(struct ptp_ocp *bp)
{
	if (!bp->nmea_out)
		return;

	iowrite32(0, &bp->nmea_out->ctrl);		/* disable */
	iowrite32(7, &bp->nmea_out->uart_baud);		/* 115200 */
	iowrite32(1, &bp->nmea_out->ctrl);		/* enable */
}

static void
_ptp_ocp_signal_init(struct ptp_ocp_signal *s, struct signal_reg __iomem *reg)
{
	u32 val;

	iowrite32(0, &reg->enable);		/* disable */

	val = ioread32(&reg->polarity);
	s->polarity = val ? true : false;
	s->duty = 50;
}

static void
ptp_ocp_signal_init(struct ptp_ocp *bp)
{
	int i;

	for (i = 0; i < 4; i++)
		if (bp->signal_out[i])
			_ptp_ocp_signal_init(&bp->signal[i],
					     bp->signal_out[i]->mem);
}

static void
ptp_ocp_attr_group_del(struct ptp_ocp *bp)
{
	sysfs_remove_groups(&bp->dev.kobj, bp->attr_group);
	kfree(bp->attr_group);
}

static int
ptp_ocp_attr_group_add(struct ptp_ocp *bp,
		       const struct ocp_attr_group *attr_tbl)
{
	int count, i;
	int err;

	count = 0;
	for (i = 0; attr_tbl[i].cap; i++)
		if (attr_tbl[i].cap & bp->fw_cap)
			count++;

	bp->attr_group = kcalloc(count + 1, sizeof(struct attribute_group *),
				 GFP_KERNEL);
	if (!bp->attr_group)
		return -ENOMEM;

	count = 0;
	for (i = 0; attr_tbl[i].cap; i++)
		if (attr_tbl[i].cap & bp->fw_cap)
			bp->attr_group[count++] = attr_tbl[i].group;

	err = sysfs_create_groups(&bp->dev.kobj, bp->attr_group);
	if (err)
		bp->attr_group[0] = NULL;

	return err;
}

static void
ptp_ocp_enable_fpga(u32 __iomem *reg, u32 bit, bool enable)
{
	u32 ctrl;
	bool on;

	ctrl = ioread32(reg);
	on = ctrl & bit;
	if (on ^ enable) {
		ctrl &= ~bit;
		ctrl |= enable ? bit : 0;
		iowrite32(ctrl, reg);
	}
}

static void
ptp_ocp_irig_out(struct ptp_ocp *bp, bool enable)
{
	return ptp_ocp_enable_fpga(&bp->irig_out->ctrl,
				   IRIG_M_CTRL_ENABLE, enable);
}

static void
ptp_ocp_irig_in(struct ptp_ocp *bp, bool enable)
{
	return ptp_ocp_enable_fpga(&bp->irig_in->ctrl,
				   IRIG_S_CTRL_ENABLE, enable);
}

static void
ptp_ocp_dcf_out(struct ptp_ocp *bp, bool enable)
{
	return ptp_ocp_enable_fpga(&bp->dcf_out->ctrl,
				   DCF_M_CTRL_ENABLE, enable);
}

static void
ptp_ocp_dcf_in(struct ptp_ocp *bp, bool enable)
{
	return ptp_ocp_enable_fpga(&bp->dcf_in->ctrl,
				   DCF_S_CTRL_ENABLE, enable);
}

static void
__handle_signal_outputs(struct ptp_ocp *bp, u32 val)
{
	ptp_ocp_irig_out(bp, val & 0x00100010);
	ptp_ocp_dcf_out(bp, val & 0x00200020);
}

static void
__handle_signal_inputs(struct ptp_ocp *bp, u32 val)
{
	ptp_ocp_irig_in(bp, val & 0x00100010);
	ptp_ocp_dcf_in(bp, val & 0x00200020);
}

static u32
ptp_ocp_sma_fb_get(struct ptp_ocp *bp, int sma_nr)
{
	u32 __iomem *gpio;
	u32 shift;

	if (bp->sma[sma_nr - 1].fixed_fcn)
		return (sma_nr - 1) & 1;

	if (bp->sma[sma_nr - 1].mode == SMA_MODE_IN)
		gpio = sma_nr > 2 ? &bp->sma_map2->gpio1 : &bp->sma_map1->gpio1;
	else
		gpio = sma_nr > 2 ? &bp->sma_map1->gpio2 : &bp->sma_map2->gpio2;
	shift = sma_nr & 1 ? 0 : 16;

	return (ioread32(gpio) >> shift) & 0xffff;
}

static int
ptp_ocp_sma_fb_set_output(struct ptp_ocp *bp, int sma_nr, u32 val)
{
	u32 reg, mask, shift;
	unsigned long flags;
	u32 __iomem *gpio;

	gpio = sma_nr > 2 ? &bp->sma_map1->gpio2 : &bp->sma_map2->gpio2;
	shift = sma_nr & 1 ? 0 : 16;

	mask = 0xffff << (16 - shift);

	spin_lock_irqsave(&bp->lock, flags);

	reg = ioread32(gpio);
	reg = (reg & mask) | (val << shift);

	__handle_signal_outputs(bp, reg);

	iowrite32(reg, gpio);

	spin_unlock_irqrestore(&bp->lock, flags);

	return 0;
}

static int
ptp_ocp_sma_fb_set_inputs(struct ptp_ocp *bp, int sma_nr, u32 val)
{
	u32 reg, mask, shift;
	unsigned long flags;
	u32 __iomem *gpio;

	gpio = sma_nr > 2 ? &bp->sma_map2->gpio1 : &bp->sma_map1->gpio1;
	shift = sma_nr & 1 ? 0 : 16;

	mask = 0xffff << (16 - shift);

	spin_lock_irqsave(&bp->lock, flags);

	reg = ioread32(gpio);
	reg = (reg & mask) | (val << shift);

	__handle_signal_inputs(bp, reg);

	iowrite32(reg, gpio);

	spin_unlock_irqrestore(&bp->lock, flags);

	return 0;
}

static void
ptp_ocp_sma_fb_init(struct ptp_ocp *bp)
{
	u32 reg;
	int i;

	/* defaults */
	bp->sma[0].mode = SMA_MODE_IN;
	bp->sma[1].mode = SMA_MODE_IN;
	bp->sma[2].mode = SMA_MODE_OUT;
	bp->sma[3].mode = SMA_MODE_OUT;
	for (i = 0; i < 4; i++)
		bp->sma[i].default_fcn = i & 1;

	/* If no SMA1 map, the pin functions and directions are fixed. */
	if (!bp->sma_map1) {
		for (i = 0; i < 4; i++) {
			bp->sma[i].fixed_fcn = true;
			bp->sma[i].fixed_dir = true;
		}
		return;
	}

	/* If SMA2 GPIO output map is all 1, it is not present.
	 * This indicates the firmware has fixed direction SMA pins.
	 */
	reg = ioread32(&bp->sma_map2->gpio2);
	if (reg == 0xffffffff) {
		for (i = 0; i < 4; i++)
			bp->sma[i].fixed_dir = true;
	} else {
		reg = ioread32(&bp->sma_map1->gpio1);
		bp->sma[0].mode = reg & BIT(15) ? SMA_MODE_IN : SMA_MODE_OUT;
		bp->sma[1].mode = reg & BIT(31) ? SMA_MODE_IN : SMA_MODE_OUT;

		reg = ioread32(&bp->sma_map1->gpio2);
		bp->sma[2].mode = reg & BIT(15) ? SMA_MODE_OUT : SMA_MODE_IN;
		bp->sma[3].mode = reg & BIT(31) ? SMA_MODE_OUT : SMA_MODE_IN;
	}
}

static const struct ocp_sma_op ocp_fb_sma_op = {
	.tbl		= { ptp_ocp_sma_in, ptp_ocp_sma_out },
	.init		= ptp_ocp_sma_fb_init,
	.get		= ptp_ocp_sma_fb_get,
	.set_inputs	= ptp_ocp_sma_fb_set_inputs,
	.set_output	= ptp_ocp_sma_fb_set_output,
};

static int
ptp_ocp_fb_set_pins(struct ptp_ocp *bp)
{
	struct ptp_pin_desc *config;
	int i;

	config = kcalloc(4, sizeof(*config), GFP_KERNEL);
	if (!config)
		return -ENOMEM;

	for (i = 0; i < 4; i++) {
		sprintf(config[i].name, "sma%d", i + 1);
		config[i].index = i;
	}

	bp->ptp_info.n_pins = 4;
	bp->ptp_info.pin_config = config;

	return 0;
}

static void
ptp_ocp_fb_set_version(struct ptp_ocp *bp)
{
	u64 cap = OCP_CAP_BASIC;
	u32 version;

	version = ioread32(&bp->image->version);

	/* if lower 16 bits are empty, this is the fw loader. */
	if ((version & 0xffff) == 0) {
		version = version >> 16;
		bp->fw_loader = true;
	}

	bp->fw_tag = version >> 15;
	bp->fw_version = version & 0x7fff;

	if (bp->fw_tag) {
		/* FPGA firmware */
		if (version >= 5)
			cap |= OCP_CAP_SIGNAL | OCP_CAP_FREQ;
	} else {
		/* SOM firmware */
		if (version >= 19)
			cap |= OCP_CAP_SIGNAL;
		if (version >= 20)
			cap |= OCP_CAP_FREQ;
	}

	bp->fw_cap = cap;
}

/* FB specific board initializers; last "resource" registered. */
static int
ptp_ocp_fb_board_init(struct ptp_ocp *bp, struct ocp_resource *r)
{
	int err;

	bp->flash_start = 1024 * 4096;
	bp->eeprom_map = fb_eeprom_map;
	bp->fw_version = ioread32(&bp->image->version);
	bp->sma_op = &ocp_fb_sma_op;

	ptp_ocp_fb_set_version(bp);

	ptp_ocp_tod_init(bp);
	ptp_ocp_nmea_out_init(bp);
	ptp_ocp_sma_init(bp);
	ptp_ocp_signal_init(bp);

	err = ptp_ocp_attr_group_add(bp, fb_timecard_groups);
	if (err)
		return err;

	err = ptp_ocp_fb_set_pins(bp);
	if (err)
		return err;

	return ptp_ocp_init_clock(bp);
}

static bool
ptp_ocp_allow_irq(struct ptp_ocp *bp, struct ocp_resource *r)
{
	bool allow = !r->irq_vec || r->irq_vec < bp->n_irqs;

	if (!allow)
		dev_err(&bp->pdev->dev, "irq %d out of range, skipping %s\n",
			r->irq_vec, r->name);
	return allow;
}

static int
ptp_ocp_register_resources(struct ptp_ocp *bp, kernel_ulong_t driver_data)
{
	struct ocp_resource *r, *table;
	int err = 0;

	table = (struct ocp_resource *)driver_data;
	for (r = table; r->setup; r++) {
		if (!ptp_ocp_allow_irq(bp, r))
			continue;
		err = r->setup(bp, r);
		if (err) {
			dev_err(&bp->pdev->dev,
				"Could not register %s: err %d\n",
				r->name, err);
			break;
		}
	}
	return err;
}

static ssize_t
ptp_ocp_show_output(const struct ocp_selector *tbl, u32 val, char *buf,
		    int def_val)
{
	const char *name;
	ssize_t count;

	count = sysfs_emit(buf, "OUT: ");
	name = ptp_ocp_select_name_from_val(tbl, val);
	if (!name)
		name = ptp_ocp_select_name_from_val(tbl, def_val);
	count += sysfs_emit_at(buf, count, "%s\n", name);
	return count;
}

static ssize_t
ptp_ocp_show_inputs(const struct ocp_selector *tbl, u32 val, char *buf,
		    int def_val)
{
	const char *name;
	ssize_t count;
	int i;

	count = sysfs_emit(buf, "IN: ");
	for (i = 0; tbl[i].name; i++) {
		if (val & tbl[i].value) {
			name = tbl[i].name;
			count += sysfs_emit_at(buf, count, "%s ", name);
		}
	}
	if (!val && def_val >= 0) {
		name = ptp_ocp_select_name_from_val(tbl, def_val);
		count += sysfs_emit_at(buf, count, "%s ", name);
	}
	if (count)
		count--;
	count += sysfs_emit_at(buf, count, "\n");
	return count;
}

static int
sma_parse_inputs(const struct ocp_selector * const tbl[], const char *buf,
		 enum ptp_ocp_sma_mode *mode)
{
	int idx, count, dir;
	char **argv;
	int ret;

	argv = argv_split(GFP_KERNEL, buf, &count);
	if (!argv)
		return -ENOMEM;

	ret = -EINVAL;
	if (!count)
		goto out;

	idx = 0;
	dir = *mode == SMA_MODE_IN ? 0 : 1;
	if (!strcasecmp("IN:", argv[0])) {
		dir = 0;
		idx++;
	}
	if (!strcasecmp("OUT:", argv[0])) {
		dir = 1;
		idx++;
	}
	*mode = dir == 0 ? SMA_MODE_IN : SMA_MODE_OUT;

	ret = 0;
	for (; idx < count; idx++)
		ret |= ptp_ocp_select_val_from_name(tbl[dir], argv[idx]);
	if (ret < 0)
		ret = -EINVAL;

out:
	argv_free(argv);
	return ret;
}

static ssize_t
ptp_ocp_sma_show(struct ptp_ocp *bp, int sma_nr, char *buf,
		 int default_in_val, int default_out_val)
{
	struct ptp_ocp_sma_connector *sma = &bp->sma[sma_nr - 1];
	const struct ocp_selector * const *tbl;
	u32 val;

	tbl = bp->sma_op->tbl;
	val = ptp_ocp_sma_get(bp, sma_nr) & SMA_SELECT_MASK;

	if (sma->mode == SMA_MODE_IN) {
		if (sma->disabled)
			val = SMA_DISABLE;
		return ptp_ocp_show_inputs(tbl[0], val, buf, default_in_val);
	}

	return ptp_ocp_show_output(tbl[1], val, buf, default_out_val);
}

static ssize_t
sma1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return ptp_ocp_sma_show(bp, 1, buf, 0, 1);
}

static ssize_t
sma2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return ptp_ocp_sma_show(bp, 2, buf, -1, 1);
}

static ssize_t
sma3_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return ptp_ocp_sma_show(bp, 3, buf, -1, 0);
}

static ssize_t
sma4_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return ptp_ocp_sma_show(bp, 4, buf, -1, 1);
}

static int
ptp_ocp_sma_store(struct ptp_ocp *bp, const char *buf, int sma_nr)
{
	struct ptp_ocp_sma_connector *sma = &bp->sma[sma_nr - 1];
	enum ptp_ocp_sma_mode mode;
	int val;

	mode = sma->mode;
	val = sma_parse_inputs(bp->sma_op->tbl, buf, &mode);
	if (val < 0)
		return val;

	if (sma->fixed_dir && (mode != sma->mode || val & SMA_DISABLE))
		return -EOPNOTSUPP;

	if (sma->fixed_fcn) {
		if (val != sma->default_fcn)
			return -EOPNOTSUPP;
		return 0;
	}

	sma->disabled = !!(val & SMA_DISABLE);

	if (mode != sma->mode) {
		if (mode == SMA_MODE_IN)
			ptp_ocp_sma_set_output(bp, sma_nr, 0);
		else
			ptp_ocp_sma_set_inputs(bp, sma_nr, 0);
		sma->mode = mode;
	}

	if (!sma->fixed_dir)
		val |= SMA_ENABLE;		/* add enable bit */

	if (sma->disabled)
		val = 0;

	if (mode == SMA_MODE_IN)
		val = ptp_ocp_sma_set_inputs(bp, sma_nr, val);
	else
		val = ptp_ocp_sma_set_output(bp, sma_nr, val);

	return val;
}

static ssize_t
sma1_store(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_sma_store(bp, buf, 1);
	return err ? err : count;
}

static ssize_t
sma2_store(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_sma_store(bp, buf, 2);
	return err ? err : count;
}

static ssize_t
sma3_store(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_sma_store(bp, buf, 3);
	return err ? err : count;
}

static ssize_t
sma4_store(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_sma_store(bp, buf, 4);
	return err ? err : count;
}
static DEVICE_ATTR_RW(sma1);
static DEVICE_ATTR_RW(sma2);
static DEVICE_ATTR_RW(sma3);
static DEVICE_ATTR_RW(sma4);

static ssize_t
available_sma_inputs_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return ptp_ocp_select_table_show(bp->sma_op->tbl[0], buf);
}
static DEVICE_ATTR_RO(available_sma_inputs);

static ssize_t
available_sma_outputs_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return ptp_ocp_select_table_show(bp->sma_op->tbl[1], buf);
}
static DEVICE_ATTR_RO(available_sma_outputs);

#define EXT_ATTR_RO(_group, _name, _val)				\
	struct dev_ext_attribute dev_attr_##_group##_val##_##_name =	\
		{ __ATTR_RO(_name), (void *)_val }
#define EXT_ATTR_RW(_group, _name, _val)				\
	struct dev_ext_attribute dev_attr_##_group##_val##_##_name =	\
		{ __ATTR_RW(_name), (void *)_val }
#define to_ext_attr(x) container_of(x, struct dev_ext_attribute, attr)

/* period [duty [phase [polarity]]] */
static ssize_t
signal_store(struct device *dev, struct device_attribute *attr,
	     const char *buf, size_t count)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	struct ptp_ocp_signal s = { };
	int gen = (uintptr_t)ea->var;
	int argc, err;
	char **argv;

	argv = argv_split(GFP_KERNEL, buf, &argc);
	if (!argv)
		return -ENOMEM;

	err = -EINVAL;
	s.duty = bp->signal[gen].duty;
	s.phase = bp->signal[gen].phase;
	s.period = bp->signal[gen].period;
	s.polarity = bp->signal[gen].polarity;

	switch (argc) {
	case 4:
		argc--;
		err = kstrtobool(argv[argc], &s.polarity);
		if (err)
			goto out;
		fallthrough;
	case 3:
		argc--;
		err = kstrtou64(argv[argc], 0, &s.phase);
		if (err)
			goto out;
		fallthrough;
	case 2:
		argc--;
		err = kstrtoint(argv[argc], 0, &s.duty);
		if (err)
			goto out;
		fallthrough;
	case 1:
		argc--;
		err = kstrtou64(argv[argc], 0, &s.period);
		if (err)
			goto out;
		break;
	default:
		goto out;
	}

	err = ptp_ocp_signal_set(bp, gen, &s);
	if (err)
		goto out;

	err = ptp_ocp_signal_enable(bp->signal_out[gen], gen, s.period != 0);

out:
	argv_free(argv);
	return err ? err : count;
}

static ssize_t
signal_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	struct ptp_ocp_signal *signal;
	struct timespec64 ts;
	ssize_t count;
	int i;

	i = (uintptr_t)ea->var;
	signal = &bp->signal[i];

	count = sysfs_emit(buf, "%llu %d %llu %d", signal->period,
			   signal->duty, signal->phase, signal->polarity);

	ts = ktime_to_timespec64(signal->start);
	count += sysfs_emit_at(buf, count, " %ptT TAI\n", &ts);

	return count;
}
static EXT_ATTR_RW(signal, signal, 0);
static EXT_ATTR_RW(signal, signal, 1);
static EXT_ATTR_RW(signal, signal, 2);
static EXT_ATTR_RW(signal, signal, 3);

static ssize_t
duty_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;

	return sysfs_emit(buf, "%d\n", bp->signal[i].duty);
}
static EXT_ATTR_RO(signal, duty, 0);
static EXT_ATTR_RO(signal, duty, 1);
static EXT_ATTR_RO(signal, duty, 2);
static EXT_ATTR_RO(signal, duty, 3);

static ssize_t
period_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;

	return sysfs_emit(buf, "%llu\n", bp->signal[i].period);
}
static EXT_ATTR_RO(signal, period, 0);
static EXT_ATTR_RO(signal, period, 1);
static EXT_ATTR_RO(signal, period, 2);
static EXT_ATTR_RO(signal, period, 3);

static ssize_t
phase_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;

	return sysfs_emit(buf, "%llu\n", bp->signal[i].phase);
}
static EXT_ATTR_RO(signal, phase, 0);
static EXT_ATTR_RO(signal, phase, 1);
static EXT_ATTR_RO(signal, phase, 2);
static EXT_ATTR_RO(signal, phase, 3);

static ssize_t
polarity_show(struct device *dev, struct device_attribute *attr,
	      char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;

	return sysfs_emit(buf, "%d\n", bp->signal[i].polarity);
}
static EXT_ATTR_RO(signal, polarity, 0);
static EXT_ATTR_RO(signal, polarity, 1);
static EXT_ATTR_RO(signal, polarity, 2);
static EXT_ATTR_RO(signal, polarity, 3);

static ssize_t
running_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;

	return sysfs_emit(buf, "%d\n", bp->signal[i].running);
}
static EXT_ATTR_RO(signal, running, 0);
static EXT_ATTR_RO(signal, running, 1);
static EXT_ATTR_RO(signal, running, 2);
static EXT_ATTR_RO(signal, running, 3);

static ssize_t
start_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;
	struct timespec64 ts;

	ts = ktime_to_timespec64(bp->signal[i].start);
	return sysfs_emit(buf, "%llu.%lu\n", ts.tv_sec, ts.tv_nsec);
}
static EXT_ATTR_RO(signal, start, 0);
static EXT_ATTR_RO(signal, start, 1);
static EXT_ATTR_RO(signal, start, 2);
static EXT_ATTR_RO(signal, start, 3);

static ssize_t
seconds_store(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int idx = (uintptr_t)ea->var;
	u32 val;
	int err;

	err = kstrtou32(buf, 0, &val);
	if (err)
		return err;
	if (val > 0xff)
		return -EINVAL;

	if (val)
		val = (val << 8) | 0x1;

	iowrite32(val, &bp->freq_in[idx]->ctrl);

	return count;
}

static ssize_t
seconds_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int idx = (uintptr_t)ea->var;
	u32 val;

	val = ioread32(&bp->freq_in[idx]->ctrl);
	if (val & 1)
		val = (val >> 8) & 0xff;
	else
		val = 0;

	return sysfs_emit(buf, "%u\n", val);
}
static EXT_ATTR_RW(freq, seconds, 0);
static EXT_ATTR_RW(freq, seconds, 1);
static EXT_ATTR_RW(freq, seconds, 2);
static EXT_ATTR_RW(freq, seconds, 3);

static ssize_t
frequency_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int idx = (uintptr_t)ea->var;
	u32 val;

	val = ioread32(&bp->freq_in[idx]->status);
	if (val & FREQ_STATUS_ERROR)
		return sysfs_emit(buf, "error\n");
	if (val & FREQ_STATUS_OVERRUN)
		return sysfs_emit(buf, "overrun\n");
	if (val & FREQ_STATUS_VALID)
		return sysfs_emit(buf, "%lu\n", val & FREQ_STATUS_MASK);
	return 0;
}
static EXT_ATTR_RO(freq, frequency, 0);
static EXT_ATTR_RO(freq, frequency, 1);
static EXT_ATTR_RO(freq, frequency, 2);
static EXT_ATTR_RO(freq, frequency, 3);

static ssize_t
serialnum_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	if (!bp->has_eeprom_data)
		ptp_ocp_read_eeprom(bp);

	return sysfs_emit(buf, "%pM\n", bp->serial);
}
static DEVICE_ATTR_RO(serialnum);

static ssize_t
gnss_sync_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	ssize_t ret;

	if (bp->gnss_lost)
		ret = sysfs_emit(buf, "LOST @ %ptT\n", &bp->gnss_lost);
	else
		ret = sysfs_emit(buf, "SYNC\n");

	return ret;
}
static DEVICE_ATTR_RO(gnss_sync);

static ssize_t
utc_tai_offset_show(struct device *dev,
		    struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", bp->utc_tai_offset);
}

static ssize_t
utc_tai_offset_store(struct device *dev,
		     struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;
	u32 val;

	err = kstrtou32(buf, 0, &val);
	if (err)
		return err;

	ptp_ocp_utc_distribute(bp, val);

	return count;
}
static DEVICE_ATTR_RW(utc_tai_offset);

static ssize_t
ts_window_adjust_show(struct device *dev,
		      struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", bp->ts_window_adjust);
}

static ssize_t
ts_window_adjust_store(struct device *dev,
		       struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;
	u32 val;

	err = kstrtou32(buf, 0, &val);
	if (err)
		return err;

	bp->ts_window_adjust = val;

	return count;
}
static DEVICE_ATTR_RW(ts_window_adjust);

static ssize_t
irig_b_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 val;

	val = ioread32(&bp->irig_out->ctrl);
	val = (val >> 16) & 0x07;
	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t
irig_b_mode_store(struct device *dev,
		  struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	int err;
	u32 reg;
	u8 val;

	err = kstrtou8(buf, 0, &val);
	if (err)
		return err;
	if (val > 7)
		return -EINVAL;

	reg = ((val & 0x7) << 16);

	spin_lock_irqsave(&bp->lock, flags);
	iowrite32(0, &bp->irig_out->ctrl);		/* disable */
	iowrite32(reg, &bp->irig_out->ctrl);		/* change mode */
	iowrite32(reg | IRIG_M_CTRL_ENABLE, &bp->irig_out->ctrl);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(irig_b_mode);

static ssize_t
clock_source_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	const char *p;
	u32 select;

	select = ioread32(&bp->reg->select);
	p = ptp_ocp_select_name_from_val(ptp_ocp_clock, select >> 16);

	return sysfs_emit(buf, "%s\n", p);
}

static ssize_t
clock_source_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	int val;

	val = ptp_ocp_select_val_from_name(ptp_ocp_clock, buf);
	if (val < 0)
		return val;

	spin_lock_irqsave(&bp->lock, flags);
	iowrite32(val, &bp->reg->select);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(clock_source);

static ssize_t
available_clock_sources_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return ptp_ocp_select_table_show(ptp_ocp_clock, buf);
}
static DEVICE_ATTR_RO(available_clock_sources);

static ssize_t
clock_status_drift_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 val;
	int res;

	val = ioread32(&bp->reg->status_drift);
	res = (val & ~INT_MAX) ? -1 : 1;
	res *= (val & INT_MAX);
	return sysfs_emit(buf, "%d\n", res);
}
static DEVICE_ATTR_RO(clock_status_drift);

static ssize_t
clock_status_offset_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 val;
	int res;

	val = ioread32(&bp->reg->status_offset);
	res = (val & ~INT_MAX) ? -1 : 1;
	res *= (val & INT_MAX);
	return sysfs_emit(buf, "%d\n", res);
}
static DEVICE_ATTR_RO(clock_status_offset);

static ssize_t
tod_correction_show(struct device *dev,
		    struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 val;
	int res;

	val = ioread32(&bp->tod->adj_sec);
	res = (val & ~INT_MAX) ? -1 : 1;
	res *= (val & INT_MAX);
	return sysfs_emit(buf, "%d\n", res);
}

static ssize_t
tod_correction_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	int err, res;
	u32 val = 0;

	err = kstrtos32(buf, 0, &res);
	if (err)
		return err;
	if (res < 0) {
		res *= -1;
		val |= BIT(31);
	}
	val |= res;

	spin_lock_irqsave(&bp->lock, flags);
	iowrite32(val, &bp->tod->adj_sec);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(tod_correction);

#define _DEVICE_SIGNAL_GROUP_ATTRS(_nr)					\
	static struct attribute *fb_timecard_signal##_nr##_attrs[] = {	\
		&dev_attr_signal##_nr##_signal.attr.attr,		\
		&dev_attr_signal##_nr##_duty.attr.attr,			\
		&dev_attr_signal##_nr##_phase.attr.attr,		\
		&dev_attr_signal##_nr##_period.attr.attr,		\
		&dev_attr_signal##_nr##_polarity.attr.attr,		\
		&dev_attr_signal##_nr##_running.attr.attr,		\
		&dev_attr_signal##_nr##_start.attr.attr,		\
		NULL,							\
	}

#define DEVICE_SIGNAL_GROUP(_name, _nr)					\
	_DEVICE_SIGNAL_GROUP_ATTRS(_nr);				\
	static const struct attribute_group				\
			fb_timecard_signal##_nr##_group = {		\
		.name = #_name,						\
		.attrs = fb_timecard_signal##_nr##_attrs,		\
}

DEVICE_SIGNAL_GROUP(gen1, 0);
DEVICE_SIGNAL_GROUP(gen2, 1);
DEVICE_SIGNAL_GROUP(gen3, 2);
DEVICE_SIGNAL_GROUP(gen4, 3);

#define _DEVICE_FREQ_GROUP_ATTRS(_nr)					\
	static struct attribute *fb_timecard_freq##_nr##_attrs[] = {	\
		&dev_attr_freq##_nr##_seconds.attr.attr,		\
		&dev_attr_freq##_nr##_frequency.attr.attr,		\
		NULL,							\
	}

#define DEVICE_FREQ_GROUP(_name, _nr)					\
	_DEVICE_FREQ_GROUP_ATTRS(_nr);					\
	static const struct attribute_group				\
			fb_timecard_freq##_nr##_group = {		\
		.name = #_name,						\
		.attrs = fb_timecard_freq##_nr##_attrs,			\
}

DEVICE_FREQ_GROUP(freq1, 0);
DEVICE_FREQ_GROUP(freq2, 1);
DEVICE_FREQ_GROUP(freq3, 2);
DEVICE_FREQ_GROUP(freq4, 3);

static struct attribute *fb_timecard_attrs[] = {
	&dev_attr_serialnum.attr,
	&dev_attr_gnss_sync.attr,
	&dev_attr_clock_source.attr,
	&dev_attr_available_clock_sources.attr,
	&dev_attr_sma1.attr,
	&dev_attr_sma2.attr,
	&dev_attr_sma3.attr,
	&dev_attr_sma4.attr,
	&dev_attr_available_sma_inputs.attr,
	&dev_attr_available_sma_outputs.attr,
	&dev_attr_clock_status_drift.attr,
	&dev_attr_clock_status_offset.attr,
	&dev_attr_irig_b_mode.attr,
	&dev_attr_utc_tai_offset.attr,
	&dev_attr_ts_window_adjust.attr,
	&dev_attr_tod_correction.attr,
	NULL,
};
static const struct attribute_group fb_timecard_group = {
	.attrs = fb_timecard_attrs,
};
static const struct ocp_attr_group fb_timecard_groups[] = {
	{ .cap = OCP_CAP_BASIC,	    .group = &fb_timecard_group },
	{ .cap = OCP_CAP_SIGNAL,    .group = &fb_timecard_signal0_group },
	{ .cap = OCP_CAP_SIGNAL,    .group = &fb_timecard_signal1_group },
	{ .cap = OCP_CAP_SIGNAL,    .group = &fb_timecard_signal2_group },
	{ .cap = OCP_CAP_SIGNAL,    .group = &fb_timecard_signal3_group },
	{ .cap = OCP_CAP_FREQ,	    .group = &fb_timecard_freq0_group },
	{ .cap = OCP_CAP_FREQ,	    .group = &fb_timecard_freq1_group },
	{ .cap = OCP_CAP_FREQ,	    .group = &fb_timecard_freq2_group },
	{ .cap = OCP_CAP_FREQ,	    .group = &fb_timecard_freq3_group },
	{ },
};

static void
gpio_input_map(char *buf, struct ptp_ocp *bp, u16 map[][2], u16 bit,
	       const char *def)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (bp->sma[i].mode != SMA_MODE_IN)
			continue;
		if (map[i][0] & (1 << bit)) {
			sprintf(buf, "sma%d", i + 1);
			return;
		}
	}
	if (!def)
		def = "----";
	strcpy(buf, def);
}

static void
gpio_output_map(char *buf, struct ptp_ocp *bp, u16 map[][2], u16 bit)
{
	char *ans = buf;
	int i;

	strcpy(ans, "----");
	for (i = 0; i < 4; i++) {
		if (bp->sma[i].mode != SMA_MODE_OUT)
			continue;
		if (map[i][1] & (1 << bit))
			ans += sprintf(ans, "sma%d ", i + 1);
	}
}

static void
_signal_summary_show(struct seq_file *s, struct ptp_ocp *bp, int nr)
{
	struct signal_reg __iomem *reg = bp->signal_out[nr]->mem;
	struct ptp_ocp_signal *signal = &bp->signal[nr];
	char label[8];
	bool on;
	u32 val;

	if (!signal)
		return;

	on = signal->running;
	sprintf(label, "GEN%d", nr + 1);
	seq_printf(s, "%7s: %s, period:%llu duty:%d%% phase:%llu pol:%d",
		   label, on ? " ON" : "OFF",
		   signal->period, signal->duty, signal->phase,
		   signal->polarity);

	val = ioread32(&reg->enable);
	seq_printf(s, " [%x", val);
	val = ioread32(&reg->status);
	seq_printf(s, " %x]", val);

	seq_printf(s, " start:%llu\n", signal->start);
}

static void
_frequency_summary_show(struct seq_file *s, int nr,
			struct frequency_reg __iomem *reg)
{
	char label[8];
	bool on;
	u32 val;

	if (!reg)
		return;

	sprintf(label, "FREQ%d", nr + 1);
	val = ioread32(&reg->ctrl);
	on = val & 1;
	val = (val >> 8) & 0xff;
	seq_printf(s, "%7s: %s, sec:%u",
		   label,
		   on ? " ON" : "OFF",
		   val);

	val = ioread32(&reg->status);
	if (val & FREQ_STATUS_ERROR)
		seq_printf(s, ", error");
	if (val & FREQ_STATUS_OVERRUN)
		seq_printf(s, ", overrun");
	if (val & FREQ_STATUS_VALID)
		seq_printf(s, ", freq %lu Hz", val & FREQ_STATUS_MASK);
	seq_printf(s, "  reg:%x\n", val);
}

static int
ptp_ocp_summary_show(struct seq_file *s, void *data)
{
	struct device *dev = s->private;
	struct ptp_system_timestamp sts;
	struct ts_reg __iomem *ts_reg;
	char *buf, *src, *mac_src;
	struct timespec64 ts;
	struct ptp_ocp *bp;
	u16 sma_val[4][2];
	u32 ctrl, val;
	bool on, map;
	int i;

	buf = (char *)__get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	bp = dev_get_drvdata(dev);

	seq_printf(s, "%7s: /dev/ptp%d\n", "PTP", ptp_clock_index(bp->ptp));
	if (bp->gnss_port != -1)
		seq_printf(s, "%7s: /dev/ttyS%d\n", "GNSS1", bp->gnss_port);
	if (bp->gnss2_port != -1)
		seq_printf(s, "%7s: /dev/ttyS%d\n", "GNSS2", bp->gnss2_port);
	if (bp->mac_port != -1)
		seq_printf(s, "%7s: /dev/ttyS%d\n", "MAC", bp->mac_port);
	if (bp->nmea_port != -1)
		seq_printf(s, "%7s: /dev/ttyS%d\n", "NMEA", bp->nmea_port);

	memset(sma_val, 0xff, sizeof(sma_val));
	if (bp->sma_map1) {
		u32 reg;

		reg = ioread32(&bp->sma_map1->gpio1);
		sma_val[0][0] = reg & 0xffff;
		sma_val[1][0] = reg >> 16;

		reg = ioread32(&bp->sma_map1->gpio2);
		sma_val[2][1] = reg & 0xffff;
		sma_val[3][1] = reg >> 16;

		reg = ioread32(&bp->sma_map2->gpio1);
		sma_val[2][0] = reg & 0xffff;
		sma_val[3][0] = reg >> 16;

		reg = ioread32(&bp->sma_map2->gpio2);
		sma_val[0][1] = reg & 0xffff;
		sma_val[1][1] = reg >> 16;
	}

	sma1_show(dev, NULL, buf);
	seq_printf(s, "   sma1: %04x,%04x %s",
		   sma_val[0][0], sma_val[0][1], buf);

	sma2_show(dev, NULL, buf);
	seq_printf(s, "   sma2: %04x,%04x %s",
		   sma_val[1][0], sma_val[1][1], buf);

	sma3_show(dev, NULL, buf);
	seq_printf(s, "   sma3: %04x,%04x %s",
		   sma_val[2][0], sma_val[2][1], buf);

	sma4_show(dev, NULL, buf);
	seq_printf(s, "   sma4: %04x,%04x %s",
		   sma_val[3][0], sma_val[3][1], buf);

	if (bp->ts0) {
		ts_reg = bp->ts0->mem;
		on = ioread32(&ts_reg->enable);
		src = "GNSS1";
		seq_printf(s, "%7s: %s, src: %s\n", "TS0",
			   on ? " ON" : "OFF", src);
	}

	if (bp->ts1) {
		ts_reg = bp->ts1->mem;
		on = ioread32(&ts_reg->enable);
		gpio_input_map(buf, bp, sma_val, 2, NULL);
		seq_printf(s, "%7s: %s, src: %s\n", "TS1",
			   on ? " ON" : "OFF", buf);
	}

	if (bp->ts2) {
		ts_reg = bp->ts2->mem;
		on = ioread32(&ts_reg->enable);
		gpio_input_map(buf, bp, sma_val, 3, NULL);
		seq_printf(s, "%7s: %s, src: %s\n", "TS2",
			   on ? " ON" : "OFF", buf);
	}

	if (bp->ts3) {
		ts_reg = bp->ts3->mem;
		on = ioread32(&ts_reg->enable);
		gpio_input_map(buf, bp, sma_val, 6, NULL);
		seq_printf(s, "%7s: %s, src: %s\n", "TS3",
			   on ? " ON" : "OFF", buf);
	}

	if (bp->ts4) {
		ts_reg = bp->ts4->mem;
		on = ioread32(&ts_reg->enable);
		gpio_input_map(buf, bp, sma_val, 7, NULL);
		seq_printf(s, "%7s: %s, src: %s\n", "TS4",
			   on ? " ON" : "OFF", buf);
	}

	if (bp->pps) {
		ts_reg = bp->pps->mem;
		src = "PHC";
		on = ioread32(&ts_reg->enable);
		map = !!(bp->pps_req_map & OCP_REQ_TIMESTAMP);
		seq_printf(s, "%7s: %s, src: %s\n", "TS5",
			   on && map ? " ON" : "OFF", src);

		map = !!(bp->pps_req_map & OCP_REQ_PPS);
		seq_printf(s, "%7s: %s, src: %s\n", "PPS",
			   on && map ? " ON" : "OFF", src);
	}

	if (bp->fw_cap & OCP_CAP_SIGNAL)
		for (i = 0; i < 4; i++)
			_signal_summary_show(s, bp, i);

	if (bp->fw_cap & OCP_CAP_FREQ)
		for (i = 0; i < 4; i++)
			_frequency_summary_show(s, i, bp->freq_in[i]);

	if (bp->irig_out) {
		ctrl = ioread32(&bp->irig_out->ctrl);
		on = ctrl & IRIG_M_CTRL_ENABLE;
		val = ioread32(&bp->irig_out->status);
		gpio_output_map(buf, bp, sma_val, 4);
		seq_printf(s, "%7s: %s, error: %d, mode %d, out: %s\n", "IRIG",
			   on ? " ON" : "OFF", val, (ctrl >> 16), buf);
	}

	if (bp->irig_in) {
		on = ioread32(&bp->irig_in->ctrl) & IRIG_S_CTRL_ENABLE;
		val = ioread32(&bp->irig_in->status);
		gpio_input_map(buf, bp, sma_val, 4, NULL);
		seq_printf(s, "%7s: %s, error: %d, src: %s\n", "IRIG in",
			   on ? " ON" : "OFF", val, buf);
	}

	if (bp->dcf_out) {
		on = ioread32(&bp->dcf_out->ctrl) & DCF_M_CTRL_ENABLE;
		val = ioread32(&bp->dcf_out->status);
		gpio_output_map(buf, bp, sma_val, 5);
		seq_printf(s, "%7s: %s, error: %d, out: %s\n", "DCF",
			   on ? " ON" : "OFF", val, buf);
	}

	if (bp->dcf_in) {
		on = ioread32(&bp->dcf_in->ctrl) & DCF_S_CTRL_ENABLE;
		val = ioread32(&bp->dcf_in->status);
		gpio_input_map(buf, bp, sma_val, 5, NULL);
		seq_printf(s, "%7s: %s, error: %d, src: %s\n", "DCF in",
			   on ? " ON" : "OFF", val, buf);
	}

	if (bp->nmea_out) {
		on = ioread32(&bp->nmea_out->ctrl) & 1;
		val = ioread32(&bp->nmea_out->status);
		seq_printf(s, "%7s: %s, error: %d\n", "NMEA",
			   on ? " ON" : "OFF", val);
	}

	/* compute src for PPS1, used below. */
	if (bp->pps_select) {
		val = ioread32(&bp->pps_select->gpio1);
		src = &buf[80];
		mac_src = "GNSS1";
		if (val & 0x01) {
			gpio_input_map(src, bp, sma_val, 0, NULL);
			mac_src = src;
		} else if (val & 0x02) {
			src = "MAC";
		} else if (val & 0x04) {
			src = "GNSS1";
		} else {
			src = "----";
			mac_src = src;
		}
	} else {
		src = "?";
		mac_src = src;
	}
	seq_printf(s, "MAC PPS1 src: %s\n", mac_src);

	gpio_input_map(buf, bp, sma_val, 1, "GNSS2");
	seq_printf(s, "MAC PPS2 src: %s\n", buf);

	/* assumes automatic switchover/selection */
	val = ioread32(&bp->reg->select);
	switch (val >> 16) {
	case 0:
		sprintf(buf, "----");
		break;
	case 2:
		sprintf(buf, "IRIG");
		break;
	case 3:
		sprintf(buf, "%s via PPS1", src);
		break;
	case 6:
		sprintf(buf, "DCF");
		break;
	default:
		strcpy(buf, "unknown");
		break;
	}
	val = ioread32(&bp->reg->status);
	seq_printf(s, "%7s: %s, state: %s\n", "PHC src", buf,
		   val & OCP_STATUS_IN_SYNC ? "sync" : "unsynced");

	if (!ptp_ocp_gettimex(&bp->ptp_info, &ts, &sts)) {
		struct timespec64 sys_ts;
		s64 pre_ns, post_ns, ns;

		pre_ns = timespec64_to_ns(&sts.pre_ts);
		post_ns = timespec64_to_ns(&sts.post_ts);
		ns = (pre_ns + post_ns) / 2;
		ns += (s64)bp->utc_tai_offset * NSEC_PER_SEC;
		sys_ts = ns_to_timespec64(ns);

		seq_printf(s, "%7s: %lld.%ld == %ptT TAI\n", "PHC",
			   ts.tv_sec, ts.tv_nsec, &ts);
		seq_printf(s, "%7s: %lld.%ld == %ptT UTC offset %d\n", "SYS",
			   sys_ts.tv_sec, sys_ts.tv_nsec, &sys_ts,
			   bp->utc_tai_offset);
		seq_printf(s, "%7s: PHC:SYS offset: %lld  window: %lld\n", "",
			   timespec64_to_ns(&ts) - ns,
			   post_ns - pre_ns);
	}

	free_page((unsigned long)buf);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ptp_ocp_summary);

static int
ptp_ocp_tod_status_show(struct seq_file *s, void *data)
{
	struct device *dev = s->private;
	struct ptp_ocp *bp;
	u32 val;
	int idx;

	bp = dev_get_drvdata(dev);

	val = ioread32(&bp->tod->ctrl);
	if (!(val & TOD_CTRL_ENABLE)) {
		seq_printf(s, "TOD Slave disabled\n");
		return 0;
	}
	seq_printf(s, "TOD Slave enabled, Control Register 0x%08X\n", val);

	idx = val & TOD_CTRL_PROTOCOL ? 4 : 0;
	idx += (val >> 16) & 3;
	seq_printf(s, "Protocol %s\n", ptp_ocp_tod_proto_name(idx));

	idx = (val >> TOD_CTRL_GNSS_SHIFT) & TOD_CTRL_GNSS_MASK;
	seq_printf(s, "GNSS %s\n", ptp_ocp_tod_gnss_name(idx));

	val = ioread32(&bp->tod->version);
	seq_printf(s, "TOD Version %d.%d.%d\n",
		val >> 24, (val >> 16) & 0xff, val & 0xffff);

	val = ioread32(&bp->tod->status);
	seq_printf(s, "Status register: 0x%08X\n", val);

	val = ioread32(&bp->tod->adj_sec);
	idx = (val & ~INT_MAX) ? -1 : 1;
	idx *= (val & INT_MAX);
	seq_printf(s, "Correction seconds: %d\n", idx);

	val = ioread32(&bp->tod->utc_status);
	seq_printf(s, "UTC status register: 0x%08X\n", val);
	seq_printf(s, "UTC offset: %ld  valid:%d\n",
		val & TOD_STATUS_UTC_MASK, val & TOD_STATUS_UTC_VALID ? 1 : 0);
	seq_printf(s, "Leap second info valid:%d, Leap second announce %d\n",
		val & TOD_STATUS_LEAP_VALID ? 1 : 0,
		val & TOD_STATUS_LEAP_ANNOUNCE ? 1 : 0);

	val = ioread32(&bp->tod->leap);
	seq_printf(s, "Time to next leap second (in sec): %d\n", (s32) val);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ptp_ocp_tod_status);

static struct dentry *ptp_ocp_debugfs_root;

static void
ptp_ocp_debugfs_add_device(struct ptp_ocp *bp)
{
	struct dentry *d;

	d = debugfs_create_dir(dev_name(&bp->dev), ptp_ocp_debugfs_root);
	bp->debug_root = d;
	debugfs_create_file("summary", 0444, bp->debug_root,
			    &bp->dev, &ptp_ocp_summary_fops);
	if (bp->tod)
		debugfs_create_file("tod_status", 0444, bp->debug_root,
				    &bp->dev, &ptp_ocp_tod_status_fops);
}

static void
ptp_ocp_debugfs_remove_device(struct ptp_ocp *bp)
{
	debugfs_remove_recursive(bp->debug_root);
}

static void
ptp_ocp_debugfs_init(void)
{
	ptp_ocp_debugfs_root = debugfs_create_dir("timecard", NULL);
}

static void
ptp_ocp_debugfs_fini(void)
{
	debugfs_remove_recursive(ptp_ocp_debugfs_root);
}

static void
ptp_ocp_dev_release(struct device *dev)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	mutex_lock(&ptp_ocp_lock);
	idr_remove(&ptp_ocp_idr, bp->id);
	mutex_unlock(&ptp_ocp_lock);
}

static int
ptp_ocp_device_init(struct ptp_ocp *bp, struct pci_dev *pdev)
{
	int err;

	mutex_lock(&ptp_ocp_lock);
	err = idr_alloc(&ptp_ocp_idr, bp, 0, 0, GFP_KERNEL);
	mutex_unlock(&ptp_ocp_lock);
	if (err < 0) {
		dev_err(&pdev->dev, "idr_alloc failed: %d\n", err);
		return err;
	}
	bp->id = err;

	bp->ptp_info = ptp_ocp_clock_info;
	spin_lock_init(&bp->lock);
	bp->gnss_port = -1;
	bp->gnss2_port = -1;
	bp->mac_port = -1;
	bp->nmea_port = -1;
	bp->pdev = pdev;

	device_initialize(&bp->dev);
	dev_set_name(&bp->dev, "ocp%d", bp->id);
	bp->dev.class = &timecard_class;
	bp->dev.parent = &pdev->dev;
	bp->dev.release = ptp_ocp_dev_release;
	dev_set_drvdata(&bp->dev, bp);

	err = device_add(&bp->dev);
	if (err) {
		dev_err(&bp->dev, "device add failed: %d\n", err);
		goto out;
	}

	pci_set_drvdata(pdev, bp);

	return 0;

out:
	ptp_ocp_dev_release(&bp->dev);
	put_device(&bp->dev);
	return err;
}

static void
ptp_ocp_symlink(struct ptp_ocp *bp, struct device *child, const char *link)
{
	struct device *dev = &bp->dev;

	if (sysfs_create_link(&dev->kobj, &child->kobj, link))
		dev_err(dev, "%s symlink failed\n", link);
}

static void
ptp_ocp_link_child(struct ptp_ocp *bp, const char *name, const char *link)
{
	struct device *dev, *child;

	dev = &bp->pdev->dev;

	child = device_find_child_by_name(dev, name);
	if (!child) {
		dev_err(dev, "Could not find device %s\n", name);
		return;
	}

	ptp_ocp_symlink(bp, child, link);
	put_device(child);
}

static int
ptp_ocp_complete(struct ptp_ocp *bp)
{
	struct pps_device *pps;
	char buf[32];

	if (bp->gnss_port != -1) {
		sprintf(buf, "ttyS%d", bp->gnss_port);
		ptp_ocp_link_child(bp, buf, "ttyGNSS");
	}
	if (bp->gnss2_port != -1) {
		sprintf(buf, "ttyS%d", bp->gnss2_port);
		ptp_ocp_link_child(bp, buf, "ttyGNSS2");
	}
	if (bp->mac_port != -1) {
		sprintf(buf, "ttyS%d", bp->mac_port);
		ptp_ocp_link_child(bp, buf, "ttyMAC");
	}
	if (bp->nmea_port != -1) {
		sprintf(buf, "ttyS%d", bp->nmea_port);
		ptp_ocp_link_child(bp, buf, "ttyNMEA");
	}
	sprintf(buf, "ptp%d", ptp_clock_index(bp->ptp));
	ptp_ocp_link_child(bp, buf, "ptp");

	pps = pps_lookup_dev(bp->ptp);
	if (pps)
		ptp_ocp_symlink(bp, pps->dev, "pps");

	ptp_ocp_debugfs_add_device(bp);

	return 0;
}

static void
ptp_ocp_phc_info(struct ptp_ocp *bp)
{
	struct timespec64 ts;
	u32 version, select;
	bool sync;

	version = ioread32(&bp->reg->version);
	select = ioread32(&bp->reg->select);
	dev_info(&bp->pdev->dev, "Version %d.%d.%d, clock %s, device ptp%d\n",
		 version >> 24, (version >> 16) & 0xff, version & 0xffff,
		 ptp_ocp_select_name_from_val(ptp_ocp_clock, select >> 16),
		 ptp_clock_index(bp->ptp));

	sync = ioread32(&bp->reg->status) & OCP_STATUS_IN_SYNC;
	if (!ptp_ocp_gettimex(&bp->ptp_info, &ts, NULL))
		dev_info(&bp->pdev->dev, "Time: %lld.%ld, %s\n",
			 ts.tv_sec, ts.tv_nsec,
			 sync ? "in-sync" : "UNSYNCED");
}

static void
ptp_ocp_serial_info(struct device *dev, const char *name, int port, int baud)
{
	if (port != -1)
		dev_info(dev, "%5s: /dev/ttyS%-2d @ %6d\n", name, port, baud);
}

static void
ptp_ocp_info(struct ptp_ocp *bp)
{
	static int nmea_baud[] = {
		1200, 2400, 4800, 9600, 19200, 38400,
		57600, 115200, 230400, 460800, 921600,
		1000000, 2000000
	};
	struct device *dev = &bp->pdev->dev;
	u32 reg;

	ptp_ocp_phc_info(bp);

	ptp_ocp_serial_info(dev, "GNSS", bp->gnss_port, 115200);
	ptp_ocp_serial_info(dev, "GNSS2", bp->gnss2_port, 115200);
	ptp_ocp_serial_info(dev, "MAC", bp->mac_port, 57600);
	if (bp->nmea_out && bp->nmea_port != -1) {
		int baud = -1;

		reg = ioread32(&bp->nmea_out->uart_baud);
		if (reg < ARRAY_SIZE(nmea_baud))
			baud = nmea_baud[reg];
		ptp_ocp_serial_info(dev, "NMEA", bp->nmea_port, baud);
	}
}

static void
ptp_ocp_detach_sysfs(struct ptp_ocp *bp)
{
	struct device *dev = &bp->dev;

	sysfs_remove_link(&dev->kobj, "ttyGNSS");
	sysfs_remove_link(&dev->kobj, "ttyMAC");
	sysfs_remove_link(&dev->kobj, "ptp");
	sysfs_remove_link(&dev->kobj, "pps");
}

static void
ptp_ocp_detach(struct ptp_ocp *bp)
{
	int i;

	ptp_ocp_debugfs_remove_device(bp);
	ptp_ocp_detach_sysfs(bp);
	ptp_ocp_attr_group_del(bp);
	if (timer_pending(&bp->watchdog))
		del_timer_sync(&bp->watchdog);
	if (bp->ts0)
		ptp_ocp_unregister_ext(bp->ts0);
	if (bp->ts1)
		ptp_ocp_unregister_ext(bp->ts1);
	if (bp->ts2)
		ptp_ocp_unregister_ext(bp->ts2);
	if (bp->ts3)
		ptp_ocp_unregister_ext(bp->ts3);
	if (bp->ts4)
		ptp_ocp_unregister_ext(bp->ts4);
	if (bp->pps)
		ptp_ocp_unregister_ext(bp->pps);
	for (i = 0; i < 4; i++)
		if (bp->signal_out[i])
			ptp_ocp_unregister_ext(bp->signal_out[i]);
	if (bp->gnss_port != -1)
		serial8250_unregister_port(bp->gnss_port);
	if (bp->gnss2_port != -1)
		serial8250_unregister_port(bp->gnss2_port);
	if (bp->mac_port != -1)
		serial8250_unregister_port(bp->mac_port);
	if (bp->nmea_port != -1)
		serial8250_unregister_port(bp->nmea_port);
	platform_device_unregister(bp->spi_flash);
	platform_device_unregister(bp->i2c_ctrl);
	if (bp->i2c_clk)
		clk_hw_unregister_fixed_rate(bp->i2c_clk);
	if (bp->n_irqs)
		pci_free_irq_vectors(bp->pdev);
	if (bp->ptp)
		ptp_clock_unregister(bp->ptp);
	kfree(bp->ptp_info.pin_config);
	device_unregister(&bp->dev);
}

static int
ptp_ocp_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct devlink *devlink;
	struct ptp_ocp *bp;
	int err;

	devlink = devlink_alloc(&ptp_ocp_devlink_ops, sizeof(*bp), &pdev->dev);
	if (!devlink) {
		dev_err(&pdev->dev, "devlink_alloc failed\n");
		return -ENOMEM;
	}

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device\n");
		goto out_free;
	}

	bp = devlink_priv(devlink);
	err = ptp_ocp_device_init(bp, pdev);
	if (err)
		goto out_disable;

	/* compat mode.
	 * Older FPGA firmware only returns 2 irq's.
	 * allow this - if not all of the IRQ's are returned, skip the
	 * extra devices and just register the clock.
	 */
	err = pci_alloc_irq_vectors(pdev, 1, 17, PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (err < 0) {
		dev_err(&pdev->dev, "alloc_irq_vectors err: %d\n", err);
		goto out;
	}
	bp->n_irqs = err;
	pci_set_master(pdev);

	err = ptp_ocp_register_resources(bp, id->driver_data);
	if (err)
		goto out;

	bp->ptp = ptp_clock_register(&bp->ptp_info, &pdev->dev);
	if (IS_ERR(bp->ptp)) {
		err = PTR_ERR(bp->ptp);
		dev_err(&pdev->dev, "ptp_clock_register: %d\n", err);
		bp->ptp = NULL;
		goto out;
	}

	err = ptp_ocp_complete(bp);
	if (err)
		goto out;

	ptp_ocp_info(bp);
	devlink_register(devlink);
	return 0;

out:
	ptp_ocp_detach(bp);
out_disable:
	pci_disable_device(pdev);
out_free:
	devlink_free(devlink);
	return err;
}

static void
ptp_ocp_remove(struct pci_dev *pdev)
{
	struct ptp_ocp *bp = pci_get_drvdata(pdev);
	struct devlink *devlink = priv_to_devlink(bp);

	devlink_unregister(devlink);
	ptp_ocp_detach(bp);
	pci_disable_device(pdev);

	devlink_free(devlink);
}

static struct pci_driver ptp_ocp_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= ptp_ocp_pcidev_id,
	.probe		= ptp_ocp_probe,
	.remove		= ptp_ocp_remove,
};

static int
ptp_ocp_i2c_notifier_call(struct notifier_block *nb,
			  unsigned long action, void *data)
{
	struct device *dev, *child = data;
	struct ptp_ocp *bp;
	bool add;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
	case BUS_NOTIFY_DEL_DEVICE:
		add = action == BUS_NOTIFY_ADD_DEVICE;
		break;
	default:
		return 0;
	}

	if (!i2c_verify_adapter(child))
		return 0;

	dev = child;
	while ((dev = dev->parent))
		if (dev->driver && !strcmp(dev->driver->name, KBUILD_MODNAME))
			goto found;
	return 0;

found:
	bp = dev_get_drvdata(dev);
	if (add)
		ptp_ocp_symlink(bp, child, "i2c");
	else
		sysfs_remove_link(&bp->dev.kobj, "i2c");

	return 0;
}

static struct notifier_block ptp_ocp_i2c_notifier = {
	.notifier_call = ptp_ocp_i2c_notifier_call,
};

static int __init
ptp_ocp_init(void)
{
	const char *what;
	int err;

	ptp_ocp_debugfs_init();

	what = "timecard class";
	err = class_register(&timecard_class);
	if (err)
		goto out;

	what = "i2c notifier";
	err = bus_register_notifier(&i2c_bus_type, &ptp_ocp_i2c_notifier);
	if (err)
		goto out_notifier;

	what = "ptp_ocp driver";
	err = pci_register_driver(&ptp_ocp_driver);
	if (err)
		goto out_register;

	return 0;

out_register:
	bus_unregister_notifier(&i2c_bus_type, &ptp_ocp_i2c_notifier);
out_notifier:
	class_unregister(&timecard_class);
out:
	ptp_ocp_debugfs_fini();
	pr_err(KBUILD_MODNAME ": failed to register %s: %d\n", what, err);
	return err;
}

static void __exit
ptp_ocp_fini(void)
{
	bus_unregister_notifier(&i2c_bus_type, &ptp_ocp_i2c_notifier);
	pci_unregister_driver(&ptp_ocp_driver);
	class_unregister(&timecard_class);
	ptp_ocp_debugfs_fini();
}

module_init(ptp_ocp_init);
module_exit(ptp_ocp_fini);

MODULE_DESCRIPTION("OpenCompute TimeCard driver");
MODULE_LICENSE("GPL v2");
