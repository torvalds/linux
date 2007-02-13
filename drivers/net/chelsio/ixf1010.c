/* $Date: 2005/11/12 02:13:49 $ $RCSfile: ixf1010.c,v $ $Revision: 1.36 $ */
#include "gmac.h"
#include "elmer0.h"

/* Update fast changing statistics every 15 seconds */
#define STATS_TICK_SECS 15
/* 30 minutes for full statistics update */
#define MAJOR_UPDATE_TICKS (1800 / STATS_TICK_SECS)

/*
 * The IXF1010 can handle frames up to 16383 bytes but it's optimized for
 * frames up to 9831 (0x2667) bytes, so we limit jumbo frame size to this.
 * This length includes ethernet header and FCS.
 */
#define MAX_FRAME_SIZE 0x2667

/* MAC registers */
enum {
	/* Per-port registers */
	REG_MACADDR_LOW = 0,
	REG_MACADDR_HIGH = 0x4,
	REG_FDFC_TYPE = 0xC,
	REG_FC_TX_TIMER_VALUE = 0x1c,
	REG_IPG_RX_TIME1 = 0x28,
	REG_IPG_RX_TIME2 = 0x2c,
	REG_IPG_TX_TIME = 0x30,
	REG_PAUSE_THRES = 0x38,
	REG_MAX_FRAME_SIZE = 0x3c,
	REG_RGMII_SPEED = 0x40,
	REG_FC_ENABLE = 0x48,
	REG_DISCARD_CTRL_FRAMES = 0x54,
	REG_DIVERSE_CONFIG = 0x60,
	REG_RX_FILTER = 0x64,
	REG_MC_ADDR_LOW = 0x68,
	REG_MC_ADDR_HIGH = 0x6c,

	REG_RX_OCTETS_OK = 0x80,
	REG_RX_OCTETS_BAD = 0x84,
	REG_RX_UC_PKTS = 0x88,
	REG_RX_MC_PKTS = 0x8c,
	REG_RX_BC_PKTS = 0x90,
	REG_RX_FCS_ERR = 0xb0,
	REG_RX_TAGGED = 0xb4,
	REG_RX_DATA_ERR = 0xb8,
	REG_RX_ALIGN_ERR = 0xbc,
	REG_RX_LONG_ERR = 0xc0,
	REG_RX_JABBER_ERR = 0xc4,
	REG_RX_PAUSE_FRAMES = 0xc8,
	REG_RX_UNKNOWN_CTRL_FRAMES = 0xcc,
	REG_RX_VERY_LONG_ERR = 0xd0,
	REG_RX_RUNT_ERR = 0xd4,
	REG_RX_SHORT_ERR = 0xd8,
	REG_RX_SYMBOL_ERR = 0xe4,

	REG_TX_OCTETS_OK = 0x100,
	REG_TX_OCTETS_BAD = 0x104,
	REG_TX_UC_PKTS = 0x108,
	REG_TX_MC_PKTS = 0x10c,
	REG_TX_BC_PKTS = 0x110,
	REG_TX_EXCESSIVE_LEN_DROP = 0x14c,
	REG_TX_UNDERRUN = 0x150,
	REG_TX_TAGGED = 0x154,
	REG_TX_PAUSE_FRAMES = 0x15C,

	/* Global registers */
	REG_PORT_ENABLE = 0x1400,

	REG_JTAG_ID = 0x1430,

	RX_FIFO_HIGH_WATERMARK_BASE = 0x1600,
	RX_FIFO_LOW_WATERMARK_BASE = 0x1628,
	RX_FIFO_FRAMES_REMOVED_BASE = 0x1650,

	REG_RX_ERR_DROP = 0x167c,
	REG_RX_FIFO_OVERFLOW_EVENT = 0x1680,

	TX_FIFO_HIGH_WATERMARK_BASE = 0x1800,
	TX_FIFO_LOW_WATERMARK_BASE = 0x1828,
	TX_FIFO_XFER_THRES_BASE = 0x1850,

	REG_TX_FIFO_OVERFLOW_EVENT = 0x1878,
	REG_TX_FIFO_OOS_EVENT = 0x1884,

	TX_FIFO_FRAMES_REMOVED_BASE = 0x1888,

	REG_SPI_RX_BURST = 0x1c00,
	REG_SPI_RX_TRAINING = 0x1c04,
	REG_SPI_RX_CALENDAR = 0x1c08,
	REG_SPI_TX_SYNC = 0x1c0c
};

enum {                     /* RMON registers */
	REG_RxOctetsTotalOK = 0x80,
	REG_RxOctetsBad = 0x84,
	REG_RxUCPkts = 0x88,
	REG_RxMCPkts = 0x8c,
	REG_RxBCPkts = 0x90,
	REG_RxJumboPkts = 0xac,
	REG_RxFCSErrors = 0xb0,
	REG_RxDataErrors = 0xb8,
	REG_RxAlignErrors = 0xbc,
	REG_RxLongErrors = 0xc0,
	REG_RxJabberErrors = 0xc4,
	REG_RxPauseMacControlCounter = 0xc8,
	REG_RxVeryLongErrors = 0xd0,
	REG_RxRuntErrors = 0xd4,
	REG_RxShortErrors = 0xd8,
	REG_RxSequenceErrors = 0xe0,
	REG_RxSymbolErrors = 0xe4,

	REG_TxOctetsTotalOK = 0x100,
	REG_TxOctetsBad = 0x104,
	REG_TxUCPkts = 0x108,
	REG_TxMCPkts = 0x10c,
	REG_TxBCPkts = 0x110,
	REG_TxJumboPkts = 0x12C,
	REG_TxTotalCollisions = 0x134,
	REG_TxExcessiveLengthDrop = 0x14c,
	REG_TxUnderrun = 0x150,
	REG_TxCRCErrors = 0x158,
	REG_TxPauseFrames = 0x15c
};

enum {
	DIVERSE_CONFIG_PAD_ENABLE = 0x80,
	DIVERSE_CONFIG_CRC_ADD = 0x40
};

#define MACREG_BASE            0
#define MACREG(mac, mac_reg)   ((mac)->instance->mac_base + (mac_reg))

struct _cmac_instance {
	u32 mac_base;
	u32 index;
	u32 version;
	u32 ticks;
};

static void disable_port(struct cmac *mac)
{
	u32 val;

	t1_tpi_read(mac->adapter, REG_PORT_ENABLE, &val);
	val &= ~(1 << mac->instance->index);
	t1_tpi_write(mac->adapter, REG_PORT_ENABLE, val);
}

/*
 * Read the current values of the RMON counters and add them to the cumulative
 * port statistics.  The HW RMON counters are cleared by this operation.
 */
static void port_stats_update(struct cmac *mac)
{
	static struct {
		unsigned int reg;
		unsigned int offset;
	} hw_stats[] = {

#define HW_STAT(name, stat_name) \
	{ REG_##name, \
	  (&((struct cmac_statistics *)NULL)->stat_name) - (u64 *)NULL }

		/* Rx stats */
		HW_STAT(RxOctetsTotalOK, RxOctetsOK),
		HW_STAT(RxOctetsBad, RxOctetsBad),
		HW_STAT(RxUCPkts, RxUnicastFramesOK),
		HW_STAT(RxMCPkts, RxMulticastFramesOK),
		HW_STAT(RxBCPkts, RxBroadcastFramesOK),
		HW_STAT(RxJumboPkts, RxJumboFramesOK),
		HW_STAT(RxFCSErrors, RxFCSErrors),
		HW_STAT(RxAlignErrors, RxAlignErrors),
		HW_STAT(RxLongErrors, RxFrameTooLongErrors),
		HW_STAT(RxVeryLongErrors, RxFrameTooLongErrors),
		HW_STAT(RxPauseMacControlCounter, RxPauseFrames),
		HW_STAT(RxDataErrors, RxDataErrors),
		HW_STAT(RxJabberErrors, RxJabberErrors),
		HW_STAT(RxRuntErrors, RxRuntErrors),
		HW_STAT(RxShortErrors, RxRuntErrors),
		HW_STAT(RxSequenceErrors, RxSequenceErrors),
		HW_STAT(RxSymbolErrors, RxSymbolErrors),

		/* Tx stats (skip collision stats as we are full-duplex only) */
		HW_STAT(TxOctetsTotalOK, TxOctetsOK),
		HW_STAT(TxOctetsBad, TxOctetsBad),
		HW_STAT(TxUCPkts, TxUnicastFramesOK),
		HW_STAT(TxMCPkts, TxMulticastFramesOK),
		HW_STAT(TxBCPkts, TxBroadcastFramesOK),
		HW_STAT(TxJumboPkts, TxJumboFramesOK),
		HW_STAT(TxPauseFrames, TxPauseFrames),
		HW_STAT(TxExcessiveLengthDrop, TxLengthErrors),
		HW_STAT(TxUnderrun, TxUnderrun),
		HW_STAT(TxCRCErrors, TxFCSErrors)
	}, *p = hw_stats;
	u64 *stats = (u64 *) &mac->stats;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(hw_stats); i++) {
		u32 val;

		t1_tpi_read(mac->adapter, MACREG(mac, p->reg), &val);
		stats[p->offset] += val;
	}
}

/* No-op interrupt operation as this MAC does not support interrupts */
static int mac_intr_op(struct cmac *mac)
{
	return 0;
}

/* Expect MAC address to be in network byte order. */
static int mac_set_address(struct cmac *mac, u8 addr[6])
{
	u32 addr_lo, addr_hi;

	addr_lo = addr[2];
	addr_lo = (addr_lo << 8) | addr[3];
	addr_lo = (addr_lo << 8) | addr[4];
	addr_lo = (addr_lo << 8) | addr[5];

	addr_hi = addr[0];
	addr_hi = (addr_hi << 8) | addr[1];

	t1_tpi_write(mac->adapter, MACREG(mac, REG_MACADDR_LOW), addr_lo);
	t1_tpi_write(mac->adapter, MACREG(mac, REG_MACADDR_HIGH), addr_hi);
	return 0;
}

static int mac_get_address(struct cmac *mac, u8 addr[6])
{
	u32 addr_lo, addr_hi;

	t1_tpi_read(mac->adapter, MACREG(mac, REG_MACADDR_LOW), &addr_lo);
	t1_tpi_read(mac->adapter, MACREG(mac, REG_MACADDR_HIGH), &addr_hi);

	addr[0] = (u8) (addr_hi >> 8);
	addr[1] = (u8) addr_hi;
	addr[2] = (u8) (addr_lo >> 24);
	addr[3] = (u8) (addr_lo >> 16);
	addr[4] = (u8) (addr_lo >> 8);
	addr[5] = (u8) addr_lo;
	return 0;
}

/* This is intended to reset a port, not the whole MAC */
static int mac_reset(struct cmac *mac)
{
	return 0;
}

static int mac_set_rx_mode(struct cmac *mac, struct t1_rx_mode *rm)
{
	u32 val, new_mode;
	adapter_t *adapter = mac->adapter;
	u32 addr_lo, addr_hi;
	u8 *addr;

	t1_tpi_read(adapter, MACREG(mac, REG_RX_FILTER), &val);
	new_mode = val & ~7;
	if (!t1_rx_mode_promisc(rm) && mac->instance->version > 0)
		new_mode |= 1;     /* only set if version > 0 due to erratum */
	if (!t1_rx_mode_promisc(rm) && !t1_rx_mode_allmulti(rm)
	     && t1_rx_mode_mc_cnt(rm) <= 1)
		new_mode |= 2;
	if (new_mode != val)
		t1_tpi_write(adapter, MACREG(mac, REG_RX_FILTER), new_mode);
	switch (t1_rx_mode_mc_cnt(rm)) {
	case 0:
		t1_tpi_write(adapter, MACREG(mac, REG_MC_ADDR_LOW), 0);
		t1_tpi_write(adapter, MACREG(mac, REG_MC_ADDR_HIGH), 0);
		break;
	case 1:
		addr = t1_get_next_mcaddr(rm);
		addr_lo = (addr[2] << 24) | (addr[3] << 16) | (addr[4] << 8) |
			addr[5];
		addr_hi = (addr[0] << 8) | addr[1];
		t1_tpi_write(adapter, MACREG(mac, REG_MC_ADDR_LOW), addr_lo);
		t1_tpi_write(adapter, MACREG(mac, REG_MC_ADDR_HIGH), addr_hi);
		break;
	default:
		break;
	}
	return 0;
}

static int mac_set_mtu(struct cmac *mac, int mtu)
{
	/* MAX_FRAME_SIZE inludes header + FCS, mtu doesn't */
	if (mtu > (MAX_FRAME_SIZE - 14 - 4))
		return -EINVAL;
	t1_tpi_write(mac->adapter, MACREG(mac, REG_MAX_FRAME_SIZE),
		     mtu + 14 + 4);
	return 0;
}

static int mac_set_speed_duplex_fc(struct cmac *mac, int speed, int duplex,
				   int fc)
{
	u32 val;

	if (speed >= 0 && speed != SPEED_100 && speed != SPEED_1000)
		return -1;
	if (duplex >= 0 && duplex != DUPLEX_FULL)
		return -1;

	if (speed >= 0) {
		val = speed == SPEED_100 ? 1 : 2;
		t1_tpi_write(mac->adapter, MACREG(mac, REG_RGMII_SPEED), val);
	}

	t1_tpi_read(mac->adapter, MACREG(mac, REG_FC_ENABLE), &val);
	val &= ~3;
	if (fc & PAUSE_RX)
		val |= 1;
	if (fc & PAUSE_TX)
		val |= 2;
	t1_tpi_write(mac->adapter, MACREG(mac, REG_FC_ENABLE), val);
	return 0;
}

static int mac_get_speed_duplex_fc(struct cmac *mac, int *speed, int *duplex,
				   int *fc)
{
	u32 val;

	if (duplex)
		*duplex = DUPLEX_FULL;
	if (speed) {
		t1_tpi_read(mac->adapter, MACREG(mac, REG_RGMII_SPEED),
			 &val);
		*speed = (val & 2) ? SPEED_1000 : SPEED_100;
	}
	if (fc) {
		t1_tpi_read(mac->adapter, MACREG(mac, REG_FC_ENABLE), &val);
		*fc = 0;
		if (val & 1)
			*fc |= PAUSE_RX;
		if (val & 2)
			*fc |= PAUSE_TX;
	}
	return 0;
}

static void enable_port(struct cmac *mac)
{
	u32 val;
	u32 index = mac->instance->index;
	adapter_t *adapter = mac->adapter;

	t1_tpi_read(adapter, MACREG(mac, REG_DIVERSE_CONFIG), &val);
	val |= DIVERSE_CONFIG_CRC_ADD | DIVERSE_CONFIG_PAD_ENABLE;
	t1_tpi_write(adapter, MACREG(mac, REG_DIVERSE_CONFIG), val);
	if (mac->instance->version > 0)
		t1_tpi_write(adapter, MACREG(mac, REG_RX_FILTER), 3);
	else /* Don't enable unicast address filtering due to IXF1010 bug */
		t1_tpi_write(adapter, MACREG(mac, REG_RX_FILTER), 2);

	t1_tpi_read(adapter, REG_RX_ERR_DROP, &val);
	val |= (1 << index);
	t1_tpi_write(adapter, REG_RX_ERR_DROP, val);

	/*
	 * Clear the port RMON registers by adding their current values to the
	 * cumulatice port stats and then clearing the stats.  Really.
	 */
	port_stats_update(mac);
	memset(&mac->stats, 0, sizeof(struct cmac_statistics));
	mac->instance->ticks = 0;

	t1_tpi_read(adapter, REG_PORT_ENABLE, &val);
	val |= (1 << index);
	t1_tpi_write(adapter, REG_PORT_ENABLE, val);

	index <<= 2;
	if (is_T2(adapter)) {
		/* T204: set the Fifo water level & threshold */
		t1_tpi_write(adapter, RX_FIFO_HIGH_WATERMARK_BASE + index, 0x740);
		t1_tpi_write(adapter, RX_FIFO_LOW_WATERMARK_BASE + index, 0x730);
		t1_tpi_write(adapter, TX_FIFO_HIGH_WATERMARK_BASE + index, 0x600);
		t1_tpi_write(adapter, TX_FIFO_LOW_WATERMARK_BASE + index, 0x1d0);
		t1_tpi_write(adapter, TX_FIFO_XFER_THRES_BASE + index, 0x1100);
	} else {
	/*
	 * Set the TX Fifo Threshold to 0x400 instead of 0x100 to work around
	 * Underrun problem. Intel has blessed this solution.
	 */
		t1_tpi_write(adapter, TX_FIFO_XFER_THRES_BASE + index, 0x400);
	}
}

/* IXF1010 ports do not have separate enables for TX and RX */
static int mac_enable(struct cmac *mac, int which)
{
	if (which & (MAC_DIRECTION_RX | MAC_DIRECTION_TX))
		enable_port(mac);
	return 0;
}

static int mac_disable(struct cmac *mac, int which)
{
	if (which & (MAC_DIRECTION_RX | MAC_DIRECTION_TX))
		disable_port(mac);
	return 0;
}

#define RMON_UPDATE(mac, name, stat_name) \
	t1_tpi_read((mac)->adapter, MACREG(mac, REG_##name), &val); \
	(mac)->stats.stat_name += val;

/*
 * This function is called periodically to accumulate the current values of the
 * RMON counters into the port statistics.  Since the counters are only 32 bits
 * some of them can overflow in less than a minute at GigE speeds, so this
 * function should be called every 30 seconds or so.
 *
 * To cut down on reading costs we update only the octet counters at each tick
 * and do a full update at major ticks, which can be every 30 minutes or more.
 */
static const struct cmac_statistics *mac_update_statistics(struct cmac *mac,
							   int flag)
{
	if (flag == MAC_STATS_UPDATE_FULL ||
	    MAJOR_UPDATE_TICKS <= mac->instance->ticks) {
		port_stats_update(mac);
		mac->instance->ticks = 0;
	} else {
		u32 val;

		RMON_UPDATE(mac, RxOctetsTotalOK, RxOctetsOK);
		RMON_UPDATE(mac, TxOctetsTotalOK, TxOctetsOK);
		mac->instance->ticks++;
	}
	return &mac->stats;
}

static void mac_destroy(struct cmac *mac)
{
	kfree(mac);
}

static struct cmac_ops ixf1010_ops = {
	.destroy                  = mac_destroy,
	.reset                    = mac_reset,
	.interrupt_enable         = mac_intr_op,
	.interrupt_disable        = mac_intr_op,
	.interrupt_clear          = mac_intr_op,
	.enable                   = mac_enable,
	.disable                  = mac_disable,
	.set_mtu                  = mac_set_mtu,
	.set_rx_mode              = mac_set_rx_mode,
	.set_speed_duplex_fc      = mac_set_speed_duplex_fc,
	.get_speed_duplex_fc      = mac_get_speed_duplex_fc,
	.statistics_update        = mac_update_statistics,
	.macaddress_get           = mac_get_address,
	.macaddress_set           = mac_set_address,
};

static int ixf1010_mac_reset(adapter_t *adapter)
{
	u32 val;

	t1_tpi_read(adapter, A_ELMER0_GPO, &val);
	if ((val & 1) != 0) {
		val &= ~1;
		t1_tpi_write(adapter, A_ELMER0_GPO, val);
		udelay(2);
	}
	val |= 1;
	t1_tpi_write(adapter, A_ELMER0_GPO, val);
	udelay(2);

	t1_tpi_write(adapter, REG_PORT_ENABLE, 0);
	return 0;
}

static struct cmac *ixf1010_mac_create(adapter_t *adapter, int index)
{
	struct cmac *mac;
	u32 val;

	if (index > 9)
		return NULL;

	mac = kzalloc(sizeof(*mac) + sizeof(cmac_instance), GFP_KERNEL);
	if (!mac)
		return NULL;

	mac->ops = &ixf1010_ops;
	mac->instance = (cmac_instance *)(mac + 1);

	mac->instance->mac_base = MACREG_BASE + (index * 0x200);
	mac->instance->index    = index;
	mac->adapter  = adapter;
	mac->instance->ticks    = 0;

	t1_tpi_read(adapter, REG_JTAG_ID, &val);
	mac->instance->version = val >> 28;
	return mac;
}

struct gmac t1_ixf1010_ops = {
	STATS_TICK_SECS,
	ixf1010_mac_create,
	ixf1010_mac_reset
};
