#include <linux/export.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/phylink.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>

#include "sfp.h"

/**
 * struct sfp_bus - internal representation of a sfp bus
 */
struct sfp_bus {
	/* private: */
	struct kref kref;
	struct list_head node;
	struct device_node *device_node;

	const struct sfp_socket_ops *socket_ops;
	struct device *sfp_dev;
	struct sfp *sfp;

	const struct sfp_upstream_ops *upstream_ops;
	void *upstream;
	struct net_device *netdev;
	struct phy_device *phydev;

	bool registered;
	bool started;
};

/**
 * sfp_parse_port() - Parse the EEPROM base ID, setting the port type
 * @bus: a pointer to the &struct sfp_bus structure for the sfp module
 * @id: a pointer to the module's &struct sfp_eeprom_id
 * @support: optional pointer to an array of unsigned long for the
 *   ethtool support mask
 *
 * Parse the EEPROM identification given in @id, and return one of
 * %PORT_TP, %PORT_FIBRE or %PORT_OTHER. If @support is non-%NULL,
 * also set the ethtool %ETHTOOL_LINK_MODE_xxx_BIT corresponding with
 * the connector type.
 *
 * If the port type is not known, returns %PORT_OTHER.
 */
int sfp_parse_port(struct sfp_bus *bus, const struct sfp_eeprom_id *id,
		   unsigned long *support)
{
	int port;

	/* port is the physical connector, set this from the connector field. */
	switch (id->base.connector) {
	case SFP_CONNECTOR_SC:
	case SFP_CONNECTOR_FIBERJACK:
	case SFP_CONNECTOR_LC:
	case SFP_CONNECTOR_MT_RJ:
	case SFP_CONNECTOR_MU:
	case SFP_CONNECTOR_OPTICAL_PIGTAIL:
		if (support)
			phylink_set(support, FIBRE);
		port = PORT_FIBRE;
		break;

	case SFP_CONNECTOR_RJ45:
		if (support)
			phylink_set(support, TP);
		port = PORT_TP;
		break;

	case SFP_CONNECTOR_UNSPEC:
		if (id->base.e1000_base_t) {
			if (support)
				phylink_set(support, TP);
			port = PORT_TP;
			break;
		}
		/* fallthrough */
	case SFP_CONNECTOR_SG: /* guess */
	case SFP_CONNECTOR_MPO_1X12:
	case SFP_CONNECTOR_MPO_2X16:
	case SFP_CONNECTOR_HSSDC_II:
	case SFP_CONNECTOR_COPPER_PIGTAIL:
	case SFP_CONNECTOR_NOSEPARATE:
	case SFP_CONNECTOR_MXC_2X16:
		port = PORT_OTHER;
		break;
	default:
		dev_warn(bus->sfp_dev, "SFP: unknown connector id 0x%02x\n",
			 id->base.connector);
		port = PORT_OTHER;
		break;
	}

	return port;
}
EXPORT_SYMBOL_GPL(sfp_parse_port);

/**
 * sfp_parse_interface() - Parse the phy_interface_t
 * @bus: a pointer to the &struct sfp_bus structure for the sfp module
 * @id: a pointer to the module's &struct sfp_eeprom_id
 *
 * Derive the phy_interface_t mode for the information found in the
 * module's identifying EEPROM. There is no standard or defined way
 * to derive this information, so we use some heuristics.
 *
 * If the encoding is 64b66b, then the module must be >= 10G, so
 * return %PHY_INTERFACE_MODE_10GKR.
 *
 * If it's 8b10b, then it's 1G or slower. If it's definitely a fibre
 * module, return %PHY_INTERFACE_MODE_1000BASEX mode, otherwise return
 * %PHY_INTERFACE_MODE_SGMII mode.
 *
 * If the encoding is not known, return %PHY_INTERFACE_MODE_NA.
 */
phy_interface_t sfp_parse_interface(struct sfp_bus *bus,
				    const struct sfp_eeprom_id *id)
{
	phy_interface_t iface;

	/* Setting the serdes link mode is guesswork: there's no field in
	 * the EEPROM which indicates what mode should be used.
	 *
	 * If the module wants 64b66b, then it must be >= 10G.
	 *
	 * If it's a gigabit-only fiber module, it probably does not have
	 * a PHY, so switch to 802.3z negotiation mode. Otherwise, switch
	 * to SGMII mode (which is required to support non-gigabit speeds).
	 */
	switch (id->base.encoding) {
	case SFP_ENCODING_8472_64B66B:
		iface = PHY_INTERFACE_MODE_10GKR;
		break;

	case SFP_ENCODING_8B10B:
		if (!id->base.e1000_base_t &&
		    !id->base.e100_base_lx &&
		    !id->base.e100_base_fx)
			iface = PHY_INTERFACE_MODE_1000BASEX;
		else
			iface = PHY_INTERFACE_MODE_SGMII;
		break;

	default:
		iface = PHY_INTERFACE_MODE_NA;
		dev_err(bus->sfp_dev,
			"SFP module encoding does not support 8b10b nor 64b66b\n");
		break;
	}

	return iface;
}
EXPORT_SYMBOL_GPL(sfp_parse_interface);

/**
 * sfp_parse_support() - Parse the eeprom id for supported link modes
 * @bus: a pointer to the &struct sfp_bus structure for the sfp module
 * @id: a pointer to the module's &struct sfp_eeprom_id
 * @support: pointer to an array of unsigned long for the ethtool support mask
 *
 * Parse the EEPROM identification information and derive the supported
 * ethtool link modes for the module.
 */
void sfp_parse_support(struct sfp_bus *bus, const struct sfp_eeprom_id *id,
		       unsigned long *support)
{
	phylink_set(support, Autoneg);
	phylink_set(support, Pause);
	phylink_set(support, Asym_Pause);

	/* Set ethtool support from the compliance fields. */
	if (id->base.e10g_base_sr)
		phylink_set(support, 10000baseSR_Full);
	if (id->base.e10g_base_lr)
		phylink_set(support, 10000baseLR_Full);
	if (id->base.e10g_base_lrm)
		phylink_set(support, 10000baseLRM_Full);
	if (id->base.e10g_base_er)
		phylink_set(support, 10000baseER_Full);
	if (id->base.e1000_base_sx ||
	    id->base.e1000_base_lx ||
	    id->base.e1000_base_cx)
		phylink_set(support, 1000baseX_Full);
	if (id->base.e1000_base_t) {
		phylink_set(support, 1000baseT_Half);
		phylink_set(support, 1000baseT_Full);
	}

	switch (id->base.extended_cc) {
	case 0x00: /* Unspecified */
		break;
	case 0x02: /* 100Gbase-SR4 or 25Gbase-SR */
		phylink_set(support, 100000baseSR4_Full);
		phylink_set(support, 25000baseSR_Full);
		break;
	case 0x03: /* 100Gbase-LR4 or 25Gbase-LR */
	case 0x04: /* 100Gbase-ER4 or 25Gbase-ER */
		phylink_set(support, 100000baseLR4_ER4_Full);
		break;
	case 0x0b: /* 100Gbase-CR4 or 25Gbase-CR CA-L */
	case 0x0c: /* 25Gbase-CR CA-S */
	case 0x0d: /* 25Gbase-CR CA-N */
		phylink_set(support, 100000baseCR4_Full);
		phylink_set(support, 25000baseCR_Full);
		break;
	default:
		dev_warn(bus->sfp_dev,
			 "Unknown/unsupported extended compliance code: 0x%02x\n",
			 id->base.extended_cc);
		break;
	}

	/* For fibre channel SFP, derive possible BaseX modes */
	if (id->base.fc_speed_100 ||
	    id->base.fc_speed_200 ||
	    id->base.fc_speed_400) {
		if (id->base.br_nominal >= 31)
			phylink_set(support, 2500baseX_Full);
		if (id->base.br_nominal >= 12)
			phylink_set(support, 1000baseX_Full);
	}

	switch (id->base.connector) {
	case SFP_CONNECTOR_SC:
	case SFP_CONNECTOR_FIBERJACK:
	case SFP_CONNECTOR_LC:
	case SFP_CONNECTOR_MT_RJ:
	case SFP_CONNECTOR_MU:
	case SFP_CONNECTOR_OPTICAL_PIGTAIL:
		break;

	case SFP_CONNECTOR_UNSPEC:
		if (id->base.e1000_base_t)
			break;

	case SFP_CONNECTOR_SG: /* guess */
	case SFP_CONNECTOR_MPO_1X12:
	case SFP_CONNECTOR_MPO_2X16:
	case SFP_CONNECTOR_HSSDC_II:
	case SFP_CONNECTOR_COPPER_PIGTAIL:
	case SFP_CONNECTOR_NOSEPARATE:
	case SFP_CONNECTOR_MXC_2X16:
	default:
		/* a guess at the supported link modes */
		dev_warn(bus->sfp_dev,
			 "Guessing link modes, please report...\n");
		phylink_set(support, 1000baseT_Half);
		phylink_set(support, 1000baseT_Full);
		break;
	}
}
EXPORT_SYMBOL_GPL(sfp_parse_support);

static LIST_HEAD(sfp_buses);
static DEFINE_MUTEX(sfp_mutex);

static const struct sfp_upstream_ops *sfp_get_upstream_ops(struct sfp_bus *bus)
{
	return bus->registered ? bus->upstream_ops : NULL;
}

static struct sfp_bus *sfp_bus_get(struct device_node *np)
{
	struct sfp_bus *sfp, *new, *found = NULL;

	new = kzalloc(sizeof(*new), GFP_KERNEL);

	mutex_lock(&sfp_mutex);

	list_for_each_entry(sfp, &sfp_buses, node) {
		if (sfp->device_node == np) {
			kref_get(&sfp->kref);
			found = sfp;
			break;
		}
	}

	if (!found && new) {
		kref_init(&new->kref);
		new->device_node = np;
		list_add(&new->node, &sfp_buses);
		found = new;
		new = NULL;
	}

	mutex_unlock(&sfp_mutex);

	kfree(new);

	return found;
}

static void sfp_bus_release(struct kref *kref) __releases(sfp_mutex)
{
	struct sfp_bus *bus = container_of(kref, struct sfp_bus, kref);

	list_del(&bus->node);
	mutex_unlock(&sfp_mutex);
	kfree(bus);
}

static void sfp_bus_put(struct sfp_bus *bus)
{
	kref_put_mutex(&bus->kref, sfp_bus_release, &sfp_mutex);
}

static int sfp_register_bus(struct sfp_bus *bus)
{
	const struct sfp_upstream_ops *ops = bus->upstream_ops;
	int ret;

	if (ops) {
		if (ops->link_down)
			ops->link_down(bus->upstream);
		if (ops->connect_phy && bus->phydev) {
			ret = ops->connect_phy(bus->upstream, bus->phydev);
			if (ret)
				return ret;
		}
	}
	if (bus->started)
		bus->socket_ops->start(bus->sfp);
	bus->registered = true;
	return 0;
}

static void sfp_unregister_bus(struct sfp_bus *bus)
{
	const struct sfp_upstream_ops *ops = bus->upstream_ops;

	if (bus->registered) {
		if (bus->started)
			bus->socket_ops->stop(bus->sfp);
		if (bus->phydev && ops && ops->disconnect_phy)
			ops->disconnect_phy(bus->upstream);
	}
	bus->registered = false;
}

/**
 * sfp_get_module_info() - Get the ethtool_modinfo for a SFP module
 * @bus: a pointer to the &struct sfp_bus structure for the sfp module
 * @modinfo: a &struct ethtool_modinfo
 *
 * Fill in the type and eeprom_len parameters in @modinfo for a module on
 * the sfp bus specified by @bus.
 *
 * Returns 0 on success or a negative errno number.
 */
int sfp_get_module_info(struct sfp_bus *bus, struct ethtool_modinfo *modinfo)
{
	if (!bus->registered)
		return -ENOIOCTLCMD;
	return bus->socket_ops->module_info(bus->sfp, modinfo);
}
EXPORT_SYMBOL_GPL(sfp_get_module_info);

/**
 * sfp_get_module_eeprom() - Read the SFP module EEPROM
 * @bus: a pointer to the &struct sfp_bus structure for the sfp module
 * @ee: a &struct ethtool_eeprom
 * @data: buffer to contain the EEPROM data (must be at least @ee->len bytes)
 *
 * Read the EEPROM as specified by the supplied @ee. See the documentation
 * for &struct ethtool_eeprom for the region to be read.
 *
 * Returns 0 on success or a negative errno number.
 */
int sfp_get_module_eeprom(struct sfp_bus *bus, struct ethtool_eeprom *ee,
			  u8 *data)
{
	if (!bus->registered)
		return -ENOIOCTLCMD;
	return bus->socket_ops->module_eeprom(bus->sfp, ee, data);
}
EXPORT_SYMBOL_GPL(sfp_get_module_eeprom);

/**
 * sfp_upstream_start() - Inform the SFP that the network device is up
 * @bus: a pointer to the &struct sfp_bus structure for the sfp module
 *
 * Inform the SFP socket that the network device is now up, so that the
 * module can be enabled by allowing TX_DISABLE to be deasserted. This
 * should be called from the network device driver's &struct net_device_ops
 * ndo_open() method.
 */
void sfp_upstream_start(struct sfp_bus *bus)
{
	if (bus->registered)
		bus->socket_ops->start(bus->sfp);
	bus->started = true;
}
EXPORT_SYMBOL_GPL(sfp_upstream_start);

/**
 * sfp_upstream_stop() - Inform the SFP that the network device is down
 * @bus: a pointer to the &struct sfp_bus structure for the sfp module
 *
 * Inform the SFP socket that the network device is now up, so that the
 * module can be disabled by asserting TX_DISABLE, disabling the laser
 * in optical modules. This should be called from the network device
 * driver's &struct net_device_ops ndo_stop() method.
 */
void sfp_upstream_stop(struct sfp_bus *bus)
{
	if (bus->registered)
		bus->socket_ops->stop(bus->sfp);
	bus->started = false;
}
EXPORT_SYMBOL_GPL(sfp_upstream_stop);

/**
 * sfp_register_upstream() - Register the neighbouring device
 * @np: device node for the SFP bus
 * @ndev: network device associated with the interface
 * @upstream: the upstream private data
 * @ops: the upstream's &struct sfp_upstream_ops
 *
 * Register the upstream device (eg, PHY) with the SFP bus. MAC drivers
 * should use phylink, which will call this function for them. Returns
 * a pointer to the allocated &struct sfp_bus.
 *
 * On error, returns %NULL.
 */
struct sfp_bus *sfp_register_upstream(struct device_node *np,
				      struct net_device *ndev, void *upstream,
				      const struct sfp_upstream_ops *ops)
{
	struct sfp_bus *bus = sfp_bus_get(np);
	int ret = 0;

	if (bus) {
		rtnl_lock();
		bus->upstream_ops = ops;
		bus->upstream = upstream;
		bus->netdev = ndev;

		if (bus->sfp)
			ret = sfp_register_bus(bus);
		rtnl_unlock();
	}

	if (ret) {
		sfp_bus_put(bus);
		bus = NULL;
	}

	return bus;
}
EXPORT_SYMBOL_GPL(sfp_register_upstream);

/**
 * sfp_unregister_upstream() - Unregister sfp bus
 * @bus: a pointer to the &struct sfp_bus structure for the sfp module
 *
 * Unregister a previously registered upstream connection for the SFP
 * module. @bus is returned from sfp_register_upstream().
 */
void sfp_unregister_upstream(struct sfp_bus *bus)
{
	rtnl_lock();
	sfp_unregister_bus(bus);
	bus->upstream = NULL;
	bus->netdev = NULL;
	rtnl_unlock();

	sfp_bus_put(bus);
}
EXPORT_SYMBOL_GPL(sfp_unregister_upstream);

/* Socket driver entry points */
int sfp_add_phy(struct sfp_bus *bus, struct phy_device *phydev)
{
	const struct sfp_upstream_ops *ops = sfp_get_upstream_ops(bus);
	int ret = 0;

	if (ops && ops->connect_phy)
		ret = ops->connect_phy(bus->upstream, phydev);

	if (ret == 0)
		bus->phydev = phydev;

	return ret;
}
EXPORT_SYMBOL_GPL(sfp_add_phy);

void sfp_remove_phy(struct sfp_bus *bus)
{
	const struct sfp_upstream_ops *ops = sfp_get_upstream_ops(bus);

	if (ops && ops->disconnect_phy)
		ops->disconnect_phy(bus->upstream);
	bus->phydev = NULL;
}
EXPORT_SYMBOL_GPL(sfp_remove_phy);

void sfp_link_up(struct sfp_bus *bus)
{
	const struct sfp_upstream_ops *ops = sfp_get_upstream_ops(bus);

	if (ops && ops->link_up)
		ops->link_up(bus->upstream);
}
EXPORT_SYMBOL_GPL(sfp_link_up);

void sfp_link_down(struct sfp_bus *bus)
{
	const struct sfp_upstream_ops *ops = sfp_get_upstream_ops(bus);

	if (ops && ops->link_down)
		ops->link_down(bus->upstream);
}
EXPORT_SYMBOL_GPL(sfp_link_down);

int sfp_module_insert(struct sfp_bus *bus, const struct sfp_eeprom_id *id)
{
	const struct sfp_upstream_ops *ops = sfp_get_upstream_ops(bus);
	int ret = 0;

	if (ops && ops->module_insert)
		ret = ops->module_insert(bus->upstream, id);

	return ret;
}
EXPORT_SYMBOL_GPL(sfp_module_insert);

void sfp_module_remove(struct sfp_bus *bus)
{
	const struct sfp_upstream_ops *ops = sfp_get_upstream_ops(bus);

	if (ops && ops->module_remove)
		ops->module_remove(bus->upstream);
}
EXPORT_SYMBOL_GPL(sfp_module_remove);

struct sfp_bus *sfp_register_socket(struct device *dev, struct sfp *sfp,
				    const struct sfp_socket_ops *ops)
{
	struct sfp_bus *bus = sfp_bus_get(dev->of_node);
	int ret = 0;

	if (bus) {
		rtnl_lock();
		bus->sfp_dev = dev;
		bus->sfp = sfp;
		bus->socket_ops = ops;

		if (bus->netdev)
			ret = sfp_register_bus(bus);
		rtnl_unlock();
	}

	if (ret) {
		sfp_bus_put(bus);
		bus = NULL;
	}

	return bus;
}
EXPORT_SYMBOL_GPL(sfp_register_socket);

void sfp_unregister_socket(struct sfp_bus *bus)
{
	rtnl_lock();
	sfp_unregister_bus(bus);
	bus->sfp_dev = NULL;
	bus->sfp = NULL;
	bus->socket_ops = NULL;
	rtnl_unlock();

	sfp_bus_put(bus);
}
EXPORT_SYMBOL_GPL(sfp_unregister_socket);
