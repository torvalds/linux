/* $Date: 2005/10/22 00:42:59 $ $RCSfile: mac.c,v $ $Revision: 1.32 $ */
#include "gmac.h"
#include "regs.h"
#include "fpga_defs.h"

#define MAC_CSR_INTERFACE_GMII      0x0
#define MAC_CSR_INTERFACE_TBI       0x1
#define MAC_CSR_INTERFACE_MII       0x2
#define MAC_CSR_INTERFACE_RMII      0x3

/* Chelsio's MAC statistics. */
struct mac_statistics {

	/* Transmit */
	u32 TxFramesTransmittedOK;
	u32 TxReserved1;
	u32 TxReserved2;
	u32 TxOctetsTransmittedOK;
	u32 TxFramesWithDeferredXmissions;
	u32 TxLateCollisions;
	u32 TxFramesAbortedDueToXSCollisions;
	u32 TxFramesLostDueToIntMACXmitError;
	u32 TxReserved3;
	u32 TxMulticastFrameXmittedOK;
	u32 TxBroadcastFramesXmittedOK;
	u32 TxFramesWithExcessiveDeferral;
	u32 TxPAUSEMACCtrlFramesTransmitted;

	/* Receive */
	u32 RxFramesReceivedOK;
	u32 RxFrameCheckSequenceErrors;
	u32 RxAlignmentErrors;
	u32 RxOctetsReceivedOK;
	u32 RxFramesLostDueToIntMACRcvError;
	u32 RxMulticastFramesReceivedOK;
	u32 RxBroadcastFramesReceivedOK;
	u32 RxInRangeLengthErrors;
	u32 RxTxOutOfRangeLengthField;
	u32 RxFrameTooLongErrors;
	u32 RxPAUSEMACCtrlFramesReceived;
};

static int static_aPorts[] = {
	FPGA_GMAC_INTERRUPT_PORT0,
	FPGA_GMAC_INTERRUPT_PORT1,
	FPGA_GMAC_INTERRUPT_PORT2,
	FPGA_GMAC_INTERRUPT_PORT3
};

struct _cmac_instance {
	u32 index;
};

static int mac_intr_enable(struct cmac *mac)
{
	u32 mac_intr;

	if (t1_is_asic(mac->adapter)) {
		/* ASIC */

		/* We don't use the on chip MAC for ASIC products. */
	} else {
		/* FPGA */

		/* Set parent gmac interrupt. */
		mac_intr = readl(mac->adapter->regs + A_PL_ENABLE);
		mac_intr |= FPGA_PCIX_INTERRUPT_GMAC;
		writel(mac_intr, mac->adapter->regs + A_PL_ENABLE);

		mac_intr = readl(mac->adapter->regs + FPGA_GMAC_ADDR_INTERRUPT_ENABLE);
		mac_intr |= static_aPorts[mac->instance->index];
		writel(mac_intr,
		       mac->adapter->regs + FPGA_GMAC_ADDR_INTERRUPT_ENABLE);
	}

	return 0;
}

static int mac_intr_disable(struct cmac *mac)
{
	u32 mac_intr;

	if (t1_is_asic(mac->adapter)) {
		/* ASIC */

		/* We don't use the on chip MAC for ASIC products. */
	} else {
		/* FPGA */

		/* Set parent gmac interrupt. */
		mac_intr = readl(mac->adapter->regs + A_PL_ENABLE);
		mac_intr &= ~FPGA_PCIX_INTERRUPT_GMAC;
		writel(mac_intr, mac->adapter->regs + A_PL_ENABLE);

		mac_intr = readl(mac->adapter->regs + FPGA_GMAC_ADDR_INTERRUPT_ENABLE);
		mac_intr &= ~(static_aPorts[mac->instance->index]);
		writel(mac_intr,
		       mac->adapter->regs + FPGA_GMAC_ADDR_INTERRUPT_ENABLE);
	}

	return 0;
}

static int mac_intr_clear(struct cmac *mac)
{
	u32 mac_intr;

	if (t1_is_asic(mac->adapter)) {
		/* ASIC */

		/* We don't use the on chip MAC for ASIC products. */
	} else {
		/* FPGA */

		/* Set parent gmac interrupt. */
		writel(FPGA_PCIX_INTERRUPT_GMAC,
		       mac->adapter->regs +  A_PL_CAUSE);
		mac_intr = readl(mac->adapter->regs + FPGA_GMAC_ADDR_INTERRUPT_CAUSE);
		mac_intr |= (static_aPorts[mac->instance->index]);
		writel(mac_intr,
		       mac->adapter->regs + FPGA_GMAC_ADDR_INTERRUPT_CAUSE);
	}

	return 0;
}

static int mac_get_address(struct cmac *mac, u8 addr[6])
{
	u32 data32_lo, data32_hi;

	data32_lo = readl(mac->adapter->regs
			  + MAC_REG_IDLO(mac->instance->index));
	data32_hi = readl(mac->adapter->regs
			  + MAC_REG_IDHI(mac->instance->index));

	addr[0] = (u8) ((data32_hi >> 8) & 0xFF);
	addr[1] = (u8) ((data32_hi) & 0xFF);
	addr[2] = (u8) ((data32_lo >> 24) & 0xFF);
	addr[3] = (u8) ((data32_lo >> 16) & 0xFF);
	addr[4] = (u8) ((data32_lo >> 8) & 0xFF);
	addr[5] = (u8) ((data32_lo) & 0xFF);
	return 0;
}

static int mac_reset(struct cmac *mac)
{
	u32 data32;
	int mac_in_reset, time_out = 100;
	int idx = mac->instance->index;

	data32 = readl(mac->adapter->regs + MAC_REG_CSR(idx));
	writel(data32 | F_MAC_RESET,
	       mac->adapter->regs + MAC_REG_CSR(idx));

	do {
		data32 = readl(mac->adapter->regs + MAC_REG_CSR(idx));

		mac_in_reset = data32 & F_MAC_RESET;
		if (mac_in_reset)
			udelay(1);
	} while (mac_in_reset && --time_out);

	if (mac_in_reset) {
		CH_ERR("%s: MAC %d reset timed out\n",
		       mac->adapter->name, idx);
		return 2;
	}

	return 0;
}

static int mac_set_rx_mode(struct cmac *mac, struct t1_rx_mode *rm)
{
	u32 val;

	val = readl(mac->adapter->regs
			    + MAC_REG_CSR(mac->instance->index));
	val &= ~(F_MAC_PROMISC | F_MAC_MC_ENABLE);
	val |= V_MAC_PROMISC(t1_rx_mode_promisc(rm) != 0);
	val |= V_MAC_MC_ENABLE(t1_rx_mode_allmulti(rm) != 0);
	writel(val,
	       mac->adapter->regs + MAC_REG_CSR(mac->instance->index));

	return 0;
}

static int mac_set_speed_duplex_fc(struct cmac *mac, int speed, int duplex,
				   int fc)
{
	u32 data32;

	data32 = readl(mac->adapter->regs
			       + MAC_REG_CSR(mac->instance->index));
	data32 &= ~(F_MAC_HALF_DUPLEX | V_MAC_SPEED(M_MAC_SPEED) |
		V_INTERFACE(M_INTERFACE) | F_MAC_TX_PAUSE_ENABLE |
		F_MAC_RX_PAUSE_ENABLE);

	switch (speed) {
	case SPEED_10:
	case SPEED_100:
		data32 |= V_INTERFACE(MAC_CSR_INTERFACE_MII);
		data32 |= V_MAC_SPEED(speed == SPEED_10 ? 0 : 1);
		break;
	case SPEED_1000:
		data32 |= V_INTERFACE(MAC_CSR_INTERFACE_GMII);
		data32 |= V_MAC_SPEED(2);
		break;
	}

	if (duplex >= 0)
		data32 |= V_MAC_HALF_DUPLEX(duplex == DUPLEX_HALF);

	if (fc >= 0) {
		data32 |= V_MAC_RX_PAUSE_ENABLE((fc & PAUSE_RX) != 0);
		data32 |= V_MAC_TX_PAUSE_ENABLE((fc & PAUSE_TX) != 0);
	}

	writel(data32,
	       mac->adapter->regs + MAC_REG_CSR(mac->instance->index));
	return 0;
}

static int mac_enable(struct cmac *mac, int which)
{
	u32 val;

	val = readl(mac->adapter->regs
			    + MAC_REG_CSR(mac->instance->index));
	if (which & MAC_DIRECTION_RX)
		val |= F_MAC_RX_ENABLE;
	if (which & MAC_DIRECTION_TX)
		val |= F_MAC_TX_ENABLE;
	writel(val,
	       mac->adapter->regs + MAC_REG_CSR(mac->instance->index));
	return 0;
}

static int mac_disable(struct cmac *mac, int which)
{
	u32 val;

	val = readl(mac->adapter->regs
			    + MAC_REG_CSR(mac->instance->index));
	if (which & MAC_DIRECTION_RX)
		val &= ~F_MAC_RX_ENABLE;
	if (which & MAC_DIRECTION_TX)
		val &= ~F_MAC_TX_ENABLE;
	writel(val,
	       mac->adapter->regs + MAC_REG_CSR(mac->instance->index));
	return 0;
}

#if 0
static int mac_set_ifs(struct cmac *mac, u32 mode)
{
	t1_write_reg_4(mac->adapter,
		       MAC_REG_IFS(mac->instance->index),
		       mode);
	return 0;
}

static int mac_enable_isl(struct cmac *mac)
{
	u32 data32 = readl(mac->adapter->regs
				   + MAC_REG_CSR(mac->instance->index));
	data32 |= F_MAC_RX_ENABLE | F_MAC_TX_ENABLE;
	t1_write_reg_4(mac->adapter,
		       MAC_REG_CSR(mac->instance->index),
		       data32);
	return 0;
}
#endif

static int mac_set_mtu(struct cmac *mac, int mtu)
{
	if (mtu > 9600)
		return -EINVAL;
	writel(mtu + ETH_HLEN + VLAN_HLEN,
	       mac->adapter->regs + MAC_REG_LARGEFRAMELENGTH(mac->instance->index));

	return 0;
}

static const struct cmac_statistics *mac_update_statistics(struct cmac *mac,
							   int flag)
{
	struct mac_statistics st;
	u32 *p = (u32 *) & st, i;

	writel(0,
	       mac->adapter->regs + MAC_REG_RMCNT(mac->instance->index));

	for (i = 0; i < sizeof(st) / sizeof(u32); i++)
		*p++ = readl(mac->adapter->regs
			     + MAC_REG_RMDATA(mac->instance->index));

	/* XXX convert stats */
	return &mac->stats;
}

static void mac_destroy(struct cmac *mac)
{
	kfree(mac);
}

static struct cmac_ops chelsio_mac_ops = {
	.destroy                 = mac_destroy,
	.reset                   = mac_reset,
	.interrupt_enable        = mac_intr_enable,
	.interrupt_disable       = mac_intr_disable,
	.interrupt_clear         = mac_intr_clear,
	.enable                  = mac_enable,
	.disable                 = mac_disable,
	.set_mtu                 = mac_set_mtu,
	.set_rx_mode             = mac_set_rx_mode,
	.set_speed_duplex_fc     = mac_set_speed_duplex_fc,
	.macaddress_get          = mac_get_address,
	.statistics_update       = mac_update_statistics,
};

static struct cmac *mac_create(adapter_t *adapter, int index)
{
	struct cmac *mac;
	u32 data32;

	if (index >= 4)
		return NULL;

	mac = kzalloc(sizeof(*mac) + sizeof(cmac_instance), GFP_KERNEL);
	if (!mac)
		return NULL;

	mac->ops = &chelsio_mac_ops;
	mac->instance = (cmac_instance *) (mac + 1);

	mac->instance->index = index;
	mac->adapter = adapter;

	data32 = readl(adapter->regs + MAC_REG_CSR(mac->instance->index));
	data32 &= ~(F_MAC_RESET | F_MAC_PROMISC | F_MAC_PROMISC |
		    F_MAC_LB_ENABLE | F_MAC_RX_ENABLE | F_MAC_TX_ENABLE);
	data32 |= F_MAC_JUMBO_ENABLE;
	writel(data32, adapter->regs + MAC_REG_CSR(mac->instance->index));

	/* Initialize the random backoff seed. */
	data32 = 0x55aa + (3 * index);
	writel(data32,
	       adapter->regs + MAC_REG_GMRANDBACKOFFSEED(mac->instance->index));

	/* Check to see if the mac address needs to be set manually. */
	data32 = readl(adapter->regs + MAC_REG_IDLO(mac->instance->index));
	if (data32 == 0 || data32 == 0xffffffff) {
		/*
		 * Add a default MAC address if we can't read one.
		 */
		writel(0x43FFFFFF - index,
		       adapter->regs + MAC_REG_IDLO(mac->instance->index));
		writel(0x0007,
		       adapter->regs + MAC_REG_IDHI(mac->instance->index));
	}

	(void) mac_set_mtu(mac, 1500);
	return mac;
}

const struct gmac t1_chelsio_mac_ops = {
	.create = mac_create
};
