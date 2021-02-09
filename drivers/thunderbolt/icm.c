// SPDX-License-Identifier: GPL-2.0
/*
 * Internal Thunderbolt Connection Manager. This is a firmware running on
 * the Thunderbolt host controller performing most of the low-level
 * handling.
 *
 * Copyright (C) 2017, Intel Corporation
 * Authors: Michael Jamet <michael.jamet@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/platform_data/x86/apple.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "ctl.h"
#include "nhi_regs.h"
#include "tb.h"

#define PCIE2CIO_CMD			0x30
#define PCIE2CIO_CMD_TIMEOUT		BIT(31)
#define PCIE2CIO_CMD_START		BIT(30)
#define PCIE2CIO_CMD_WRITE		BIT(21)
#define PCIE2CIO_CMD_CS_MASK		GENMASK(20, 19)
#define PCIE2CIO_CMD_CS_SHIFT		19
#define PCIE2CIO_CMD_PORT_MASK		GENMASK(18, 13)
#define PCIE2CIO_CMD_PORT_SHIFT		13

#define PCIE2CIO_WRDATA			0x34
#define PCIE2CIO_RDDATA			0x38

#define PHY_PORT_CS1			0x37
#define PHY_PORT_CS1_LINK_DISABLE	BIT(14)
#define PHY_PORT_CS1_LINK_STATE_MASK	GENMASK(29, 26)
#define PHY_PORT_CS1_LINK_STATE_SHIFT	26

#define ICM_TIMEOUT			5000	/* ms */
#define ICM_APPROVE_TIMEOUT		10000	/* ms */
#define ICM_MAX_LINK			4

static bool start_icm;
module_param(start_icm, bool, 0444);
MODULE_PARM_DESC(start_icm, "start ICM firmware if it is not running (default: false)");

/**
 * struct usb4_switch_nvm_auth - Holds USB4 NVM_AUTH status
 * @reply: Reply from ICM firmware is placed here
 * @request: Request that is sent to ICM firmware
 * @icm: Pointer to ICM private data
 */
struct usb4_switch_nvm_auth {
	struct icm_usb4_switch_op_response reply;
	struct icm_usb4_switch_op request;
	struct icm *icm;
};

/**
 * struct icm - Internal connection manager private data
 * @request_lock: Makes sure only one message is send to ICM at time
 * @rescan_work: Work used to rescan the surviving switches after resume
 * @upstream_port: Pointer to the PCIe upstream port this host
 *		   controller is connected. This is only set for systems
 *		   where ICM needs to be started manually
 * @vnd_cap: Vendor defined capability where PCIe2CIO mailbox resides
 *	     (only set when @upstream_port is not %NULL)
 * @safe_mode: ICM is in safe mode
 * @max_boot_acl: Maximum number of preboot ACL entries (%0 if not supported)
 * @rpm: Does the controller support runtime PM (RTD3)
 * @can_upgrade_nvm: Can the NVM firmware be upgrade on this controller
 * @proto_version: Firmware protocol version
 * @last_nvm_auth: Last USB4 router NVM_AUTH result (or %NULL if not set)
 * @veto: Is RTD3 veto in effect
 * @is_supported: Checks if we can support ICM on this controller
 * @cio_reset: Trigger CIO reset
 * @get_mode: Read and return the ICM firmware mode (optional)
 * @get_route: Find a route string for given switch
 * @save_devices: Ask ICM to save devices to ACL when suspending (optional)
 * @driver_ready: Send driver ready message to ICM
 * @set_uuid: Set UUID for the root switch (optional)
 * @device_connected: Handle device connected ICM message
 * @device_disconnected: Handle device disconnected ICM message
 * @xdomain_connected: Handle XDomain connected ICM message
 * @xdomain_disconnected: Handle XDomain disconnected ICM message
 * @rtd3_veto: Handle RTD3 veto notification ICM message
 */
struct icm {
	struct mutex request_lock;
	struct delayed_work rescan_work;
	struct pci_dev *upstream_port;
	int vnd_cap;
	bool safe_mode;
	size_t max_boot_acl;
	bool rpm;
	bool can_upgrade_nvm;
	u8 proto_version;
	struct usb4_switch_nvm_auth *last_nvm_auth;
	bool veto;
	bool (*is_supported)(struct tb *tb);
	int (*cio_reset)(struct tb *tb);
	int (*get_mode)(struct tb *tb);
	int (*get_route)(struct tb *tb, u8 link, u8 depth, u64 *route);
	void (*save_devices)(struct tb *tb);
	int (*driver_ready)(struct tb *tb,
			    enum tb_security_level *security_level,
			    u8 *proto_version, size_t *nboot_acl, bool *rpm);
	void (*set_uuid)(struct tb *tb);
	void (*device_connected)(struct tb *tb,
				 const struct icm_pkg_header *hdr);
	void (*device_disconnected)(struct tb *tb,
				    const struct icm_pkg_header *hdr);
	void (*xdomain_connected)(struct tb *tb,
				  const struct icm_pkg_header *hdr);
	void (*xdomain_disconnected)(struct tb *tb,
				     const struct icm_pkg_header *hdr);
	void (*rtd3_veto)(struct tb *tb, const struct icm_pkg_header *hdr);
};

struct icm_notification {
	struct work_struct work;
	struct icm_pkg_header *pkg;
	struct tb *tb;
};

struct ep_name_entry {
	u8 len;
	u8 type;
	u8 data[];
};

#define EP_NAME_INTEL_VSS	0x10

/* Intel Vendor specific structure */
struct intel_vss {
	u16 vendor;
	u16 model;
	u8 mc;
	u8 flags;
	u16 pci_devid;
	u32 nvm_version;
};

#define INTEL_VSS_FLAGS_RTD3	BIT(0)

static const struct intel_vss *parse_intel_vss(const void *ep_name, size_t size)
{
	const void *end = ep_name + size;

	while (ep_name < end) {
		const struct ep_name_entry *ep = ep_name;

		if (!ep->len)
			break;
		if (ep_name + ep->len > end)
			break;

		if (ep->type == EP_NAME_INTEL_VSS)
			return (const struct intel_vss *)ep->data;

		ep_name += ep->len;
	}

	return NULL;
}

static bool intel_vss_is_rtd3(const void *ep_name, size_t size)
{
	const struct intel_vss *vss;

	vss = parse_intel_vss(ep_name, size);
	if (vss)
		return !!(vss->flags & INTEL_VSS_FLAGS_RTD3);

	return false;
}

static inline struct tb *icm_to_tb(struct icm *icm)
{
	return ((void *)icm - sizeof(struct tb));
}

static inline u8 phy_port_from_route(u64 route, u8 depth)
{
	u8 link;

	link = depth ? route >> ((depth - 1) * 8) : route;
	return tb_phy_port_from_link(link);
}

static inline u8 dual_link_from_link(u8 link)
{
	return link ? ((link - 1) ^ 0x01) + 1 : 0;
}

static inline u64 get_route(u32 route_hi, u32 route_lo)
{
	return (u64)route_hi << 32 | route_lo;
}

static inline u64 get_parent_route(u64 route)
{
	int depth = tb_route_length(route);
	return depth ? route & ~(0xffULL << (depth - 1) * TB_ROUTE_SHIFT) : 0;
}

static int pci2cio_wait_completion(struct icm *icm, unsigned long timeout_msec)
{
	unsigned long end = jiffies + msecs_to_jiffies(timeout_msec);
	u32 cmd;

	do {
		pci_read_config_dword(icm->upstream_port,
				      icm->vnd_cap + PCIE2CIO_CMD, &cmd);
		if (!(cmd & PCIE2CIO_CMD_START)) {
			if (cmd & PCIE2CIO_CMD_TIMEOUT)
				break;
			return 0;
		}

		msleep(50);
	} while (time_before(jiffies, end));

	return -ETIMEDOUT;
}

static int pcie2cio_read(struct icm *icm, enum tb_cfg_space cs,
			 unsigned int port, unsigned int index, u32 *data)
{
	struct pci_dev *pdev = icm->upstream_port;
	int ret, vnd_cap = icm->vnd_cap;
	u32 cmd;

	cmd = index;
	cmd |= (port << PCIE2CIO_CMD_PORT_SHIFT) & PCIE2CIO_CMD_PORT_MASK;
	cmd |= (cs << PCIE2CIO_CMD_CS_SHIFT) & PCIE2CIO_CMD_CS_MASK;
	cmd |= PCIE2CIO_CMD_START;
	pci_write_config_dword(pdev, vnd_cap + PCIE2CIO_CMD, cmd);

	ret = pci2cio_wait_completion(icm, 5000);
	if (ret)
		return ret;

	pci_read_config_dword(pdev, vnd_cap + PCIE2CIO_RDDATA, data);
	return 0;
}

static int pcie2cio_write(struct icm *icm, enum tb_cfg_space cs,
			  unsigned int port, unsigned int index, u32 data)
{
	struct pci_dev *pdev = icm->upstream_port;
	int vnd_cap = icm->vnd_cap;
	u32 cmd;

	pci_write_config_dword(pdev, vnd_cap + PCIE2CIO_WRDATA, data);

	cmd = index;
	cmd |= (port << PCIE2CIO_CMD_PORT_SHIFT) & PCIE2CIO_CMD_PORT_MASK;
	cmd |= (cs << PCIE2CIO_CMD_CS_SHIFT) & PCIE2CIO_CMD_CS_MASK;
	cmd |= PCIE2CIO_CMD_WRITE | PCIE2CIO_CMD_START;
	pci_write_config_dword(pdev, vnd_cap + PCIE2CIO_CMD, cmd);

	return pci2cio_wait_completion(icm, 5000);
}

static bool icm_match(const struct tb_cfg_request *req,
		      const struct ctl_pkg *pkg)
{
	const struct icm_pkg_header *res_hdr = pkg->buffer;
	const struct icm_pkg_header *req_hdr = req->request;

	if (pkg->frame.eof != req->response_type)
		return false;
	if (res_hdr->code != req_hdr->code)
		return false;

	return true;
}

static bool icm_copy(struct tb_cfg_request *req, const struct ctl_pkg *pkg)
{
	const struct icm_pkg_header *hdr = pkg->buffer;

	if (hdr->packet_id < req->npackets) {
		size_t offset = hdr->packet_id * req->response_size;

		memcpy(req->response + offset, pkg->buffer, req->response_size);
	}

	return hdr->packet_id == hdr->total_packets - 1;
}

static int icm_request(struct tb *tb, const void *request, size_t request_size,
		       void *response, size_t response_size, size_t npackets,
		       unsigned int timeout_msec)
{
	struct icm *icm = tb_priv(tb);
	int retries = 3;

	do {
		struct tb_cfg_request *req;
		struct tb_cfg_result res;

		req = tb_cfg_request_alloc();
		if (!req)
			return -ENOMEM;

		req->match = icm_match;
		req->copy = icm_copy;
		req->request = request;
		req->request_size = request_size;
		req->request_type = TB_CFG_PKG_ICM_CMD;
		req->response = response;
		req->npackets = npackets;
		req->response_size = response_size;
		req->response_type = TB_CFG_PKG_ICM_RESP;

		mutex_lock(&icm->request_lock);
		res = tb_cfg_request_sync(tb->ctl, req, timeout_msec);
		mutex_unlock(&icm->request_lock);

		tb_cfg_request_put(req);

		if (res.err != -ETIMEDOUT)
			return res.err == 1 ? -EIO : res.err;

		usleep_range(20, 50);
	} while (retries--);

	return -ETIMEDOUT;
}

/*
 * If rescan is queued to run (we are resuming), postpone it to give the
 * firmware some more time to send device connected notifications for next
 * devices in the chain.
 */
static void icm_postpone_rescan(struct tb *tb)
{
	struct icm *icm = tb_priv(tb);

	if (delayed_work_pending(&icm->rescan_work))
		mod_delayed_work(tb->wq, &icm->rescan_work,
				 msecs_to_jiffies(500));
}

static void icm_veto_begin(struct tb *tb)
{
	struct icm *icm = tb_priv(tb);

	if (!icm->veto) {
		icm->veto = true;
		/* Keep the domain powered while veto is in effect */
		pm_runtime_get(&tb->dev);
	}
}

static void icm_veto_end(struct tb *tb)
{
	struct icm *icm = tb_priv(tb);

	if (icm->veto) {
		icm->veto = false;
		/* Allow the domain suspend now */
		pm_runtime_mark_last_busy(&tb->dev);
		pm_runtime_put_autosuspend(&tb->dev);
	}
}

static bool icm_firmware_running(const struct tb_nhi *nhi)
{
	u32 val;

	val = ioread32(nhi->iobase + REG_FW_STS);
	return !!(val & REG_FW_STS_ICM_EN);
}

static bool icm_fr_is_supported(struct tb *tb)
{
	return !x86_apple_machine;
}

static inline int icm_fr_get_switch_index(u32 port)
{
	int index;

	if ((port & ICM_PORT_TYPE_MASK) != TB_TYPE_PORT)
		return 0;

	index = port >> ICM_PORT_INDEX_SHIFT;
	return index != 0xff ? index : 0;
}

static int icm_fr_get_route(struct tb *tb, u8 link, u8 depth, u64 *route)
{
	struct icm_fr_pkg_get_topology_response *switches, *sw;
	struct icm_fr_pkg_get_topology request = {
		.hdr = { .code = ICM_GET_TOPOLOGY },
	};
	size_t npackets = ICM_GET_TOPOLOGY_PACKETS;
	int ret, index;
	u8 i;

	switches = kcalloc(npackets, sizeof(*switches), GFP_KERNEL);
	if (!switches)
		return -ENOMEM;

	ret = icm_request(tb, &request, sizeof(request), switches,
			  sizeof(*switches), npackets, ICM_TIMEOUT);
	if (ret)
		goto err_free;

	sw = &switches[0];
	index = icm_fr_get_switch_index(sw->ports[link]);
	if (!index) {
		ret = -ENODEV;
		goto err_free;
	}

	sw = &switches[index];
	for (i = 1; i < depth; i++) {
		unsigned int j;

		if (!(sw->first_data & ICM_SWITCH_USED)) {
			ret = -ENODEV;
			goto err_free;
		}

		for (j = 0; j < ARRAY_SIZE(sw->ports); j++) {
			index = icm_fr_get_switch_index(sw->ports[j]);
			if (index > sw->switch_index) {
				sw = &switches[index];
				break;
			}
		}
	}

	*route = get_route(sw->route_hi, sw->route_lo);

err_free:
	kfree(switches);
	return ret;
}

static void icm_fr_save_devices(struct tb *tb)
{
	nhi_mailbox_cmd(tb->nhi, NHI_MAILBOX_SAVE_DEVS, 0);
}

static int
icm_fr_driver_ready(struct tb *tb, enum tb_security_level *security_level,
		    u8 *proto_version, size_t *nboot_acl, bool *rpm)
{
	struct icm_fr_pkg_driver_ready_response reply;
	struct icm_pkg_driver_ready request = {
		.hdr.code = ICM_DRIVER_READY,
	};
	int ret;

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (security_level)
		*security_level = reply.security_level & ICM_FR_SLEVEL_MASK;

	return 0;
}

static int icm_fr_approve_switch(struct tb *tb, struct tb_switch *sw)
{
	struct icm_fr_pkg_approve_device request;
	struct icm_fr_pkg_approve_device reply;
	int ret;

	memset(&request, 0, sizeof(request));
	memcpy(&request.ep_uuid, sw->uuid, sizeof(request.ep_uuid));
	request.hdr.code = ICM_APPROVE_DEVICE;
	request.connection_id = sw->connection_id;
	request.connection_key = sw->connection_key;

	memset(&reply, 0, sizeof(reply));
	/* Use larger timeout as establishing tunnels can take some time */
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_APPROVE_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR) {
		tb_warn(tb, "PCIe tunnel creation failed\n");
		return -EIO;
	}

	return 0;
}

static int icm_fr_add_switch_key(struct tb *tb, struct tb_switch *sw)
{
	struct icm_fr_pkg_add_device_key request;
	struct icm_fr_pkg_add_device_key_response reply;
	int ret;

	memset(&request, 0, sizeof(request));
	memcpy(&request.ep_uuid, sw->uuid, sizeof(request.ep_uuid));
	request.hdr.code = ICM_ADD_DEVICE_KEY;
	request.connection_id = sw->connection_id;
	request.connection_key = sw->connection_key;
	memcpy(request.key, sw->key, TB_SWITCH_KEY_SIZE);

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR) {
		tb_warn(tb, "Adding key to switch failed\n");
		return -EIO;
	}

	return 0;
}

static int icm_fr_challenge_switch_key(struct tb *tb, struct tb_switch *sw,
				       const u8 *challenge, u8 *response)
{
	struct icm_fr_pkg_challenge_device request;
	struct icm_fr_pkg_challenge_device_response reply;
	int ret;

	memset(&request, 0, sizeof(request));
	memcpy(&request.ep_uuid, sw->uuid, sizeof(request.ep_uuid));
	request.hdr.code = ICM_CHALLENGE_DEVICE;
	request.connection_id = sw->connection_id;
	request.connection_key = sw->connection_key;
	memcpy(request.challenge, challenge, TB_SWITCH_KEY_SIZE);

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR)
		return -EKEYREJECTED;
	if (reply.hdr.flags & ICM_FLAGS_NO_KEY)
		return -ENOKEY;

	memcpy(response, reply.response, TB_SWITCH_KEY_SIZE);

	return 0;
}

static int icm_fr_approve_xdomain_paths(struct tb *tb, struct tb_xdomain *xd)
{
	struct icm_fr_pkg_approve_xdomain_response reply;
	struct icm_fr_pkg_approve_xdomain request;
	int ret;

	memset(&request, 0, sizeof(request));
	request.hdr.code = ICM_APPROVE_XDOMAIN;
	request.link_info = xd->depth << ICM_LINK_INFO_DEPTH_SHIFT | xd->link;
	memcpy(&request.remote_uuid, xd->remote_uuid, sizeof(*xd->remote_uuid));

	request.transmit_path = xd->transmit_path;
	request.transmit_ring = xd->transmit_ring;
	request.receive_path = xd->receive_path;
	request.receive_ring = xd->receive_ring;

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR)
		return -EIO;

	return 0;
}

static int icm_fr_disconnect_xdomain_paths(struct tb *tb, struct tb_xdomain *xd)
{
	u8 phy_port;
	u8 cmd;

	phy_port = tb_phy_port_from_link(xd->link);
	if (phy_port == 0)
		cmd = NHI_MAILBOX_DISCONNECT_PA;
	else
		cmd = NHI_MAILBOX_DISCONNECT_PB;

	nhi_mailbox_cmd(tb->nhi, cmd, 1);
	usleep_range(10, 50);
	nhi_mailbox_cmd(tb->nhi, cmd, 2);
	return 0;
}

static struct tb_switch *alloc_switch(struct tb_switch *parent_sw, u64 route,
				      const uuid_t *uuid)
{
	struct tb *tb = parent_sw->tb;
	struct tb_switch *sw;

	sw = tb_switch_alloc(tb, &parent_sw->dev, route);
	if (IS_ERR(sw)) {
		tb_warn(tb, "failed to allocate switch at %llx\n", route);
		return sw;
	}

	sw->uuid = kmemdup(uuid, sizeof(*uuid), GFP_KERNEL);
	if (!sw->uuid) {
		tb_switch_put(sw);
		return ERR_PTR(-ENOMEM);
	}

	init_completion(&sw->rpm_complete);
	return sw;
}

static int add_switch(struct tb_switch *parent_sw, struct tb_switch *sw)
{
	u64 route = tb_route(sw);
	int ret;

	/* Link the two switches now */
	tb_port_at(route, parent_sw)->remote = tb_upstream_port(sw);
	tb_upstream_port(sw)->remote = tb_port_at(route, parent_sw);

	ret = tb_switch_add(sw);
	if (ret)
		tb_port_at(tb_route(sw), parent_sw)->remote = NULL;

	return ret;
}

static void update_switch(struct tb_switch *parent_sw, struct tb_switch *sw,
			  u64 route, u8 connection_id, u8 connection_key,
			  u8 link, u8 depth, bool boot)
{
	/* Disconnect from parent */
	tb_port_at(tb_route(sw), parent_sw)->remote = NULL;
	/* Re-connect via updated port*/
	tb_port_at(route, parent_sw)->remote = tb_upstream_port(sw);

	/* Update with the new addressing information */
	sw->config.route_hi = upper_32_bits(route);
	sw->config.route_lo = lower_32_bits(route);
	sw->connection_id = connection_id;
	sw->connection_key = connection_key;
	sw->link = link;
	sw->depth = depth;
	sw->boot = boot;

	/* This switch still exists */
	sw->is_unplugged = false;

	/* Runtime resume is now complete */
	complete(&sw->rpm_complete);
}

static void remove_switch(struct tb_switch *sw)
{
	struct tb_switch *parent_sw;

	parent_sw = tb_to_switch(sw->dev.parent);
	tb_port_at(tb_route(sw), parent_sw)->remote = NULL;
	tb_switch_remove(sw);
}

static void add_xdomain(struct tb_switch *sw, u64 route,
			const uuid_t *local_uuid, const uuid_t *remote_uuid,
			u8 link, u8 depth)
{
	struct tb_xdomain *xd;

	pm_runtime_get_sync(&sw->dev);

	xd = tb_xdomain_alloc(sw->tb, &sw->dev, route, local_uuid, remote_uuid);
	if (!xd)
		goto out;

	xd->link = link;
	xd->depth = depth;

	tb_port_at(route, sw)->xdomain = xd;

	tb_xdomain_add(xd);

out:
	pm_runtime_mark_last_busy(&sw->dev);
	pm_runtime_put_autosuspend(&sw->dev);
}

static void update_xdomain(struct tb_xdomain *xd, u64 route, u8 link)
{
	xd->link = link;
	xd->route = route;
	xd->is_unplugged = false;
}

static void remove_xdomain(struct tb_xdomain *xd)
{
	struct tb_switch *sw;

	sw = tb_to_switch(xd->dev.parent);
	tb_port_at(xd->route, sw)->xdomain = NULL;
	tb_xdomain_remove(xd);
}

static void
icm_fr_device_connected(struct tb *tb, const struct icm_pkg_header *hdr)
{
	const struct icm_fr_event_device_connected *pkg =
		(const struct icm_fr_event_device_connected *)hdr;
	enum tb_security_level security_level;
	struct tb_switch *sw, *parent_sw;
	bool boot, dual_lane, speed_gen3;
	struct icm *icm = tb_priv(tb);
	bool authorized = false;
	struct tb_xdomain *xd;
	u8 link, depth;
	u64 route;
	int ret;

	icm_postpone_rescan(tb);

	link = pkg->link_info & ICM_LINK_INFO_LINK_MASK;
	depth = (pkg->link_info & ICM_LINK_INFO_DEPTH_MASK) >>
		ICM_LINK_INFO_DEPTH_SHIFT;
	authorized = pkg->link_info & ICM_LINK_INFO_APPROVED;
	security_level = (pkg->hdr.flags & ICM_FLAGS_SLEVEL_MASK) >>
			 ICM_FLAGS_SLEVEL_SHIFT;
	boot = pkg->link_info & ICM_LINK_INFO_BOOT;
	dual_lane = pkg->hdr.flags & ICM_FLAGS_DUAL_LANE;
	speed_gen3 = pkg->hdr.flags & ICM_FLAGS_SPEED_GEN3;

	if (pkg->link_info & ICM_LINK_INFO_REJECTED) {
		tb_info(tb, "switch at %u.%u was rejected by ICM firmware because topology limit exceeded\n",
			link, depth);
		return;
	}

	sw = tb_switch_find_by_uuid(tb, &pkg->ep_uuid);
	if (sw) {
		u8 phy_port, sw_phy_port;

		parent_sw = tb_to_switch(sw->dev.parent);
		sw_phy_port = tb_phy_port_from_link(sw->link);
		phy_port = tb_phy_port_from_link(link);

		/*
		 * On resume ICM will send us connected events for the
		 * devices that still are present. However, that
		 * information might have changed for example by the
		 * fact that a switch on a dual-link connection might
		 * have been enumerated using the other link now. Make
		 * sure our book keeping matches that.
		 */
		if (sw->depth == depth && sw_phy_port == phy_port &&
		    !!sw->authorized == authorized) {
			/*
			 * It was enumerated through another link so update
			 * route string accordingly.
			 */
			if (sw->link != link) {
				ret = icm->get_route(tb, link, depth, &route);
				if (ret) {
					tb_err(tb, "failed to update route string for switch at %u.%u\n",
					       link, depth);
					tb_switch_put(sw);
					return;
				}
			} else {
				route = tb_route(sw);
			}

			update_switch(parent_sw, sw, route, pkg->connection_id,
				      pkg->connection_key, link, depth, boot);
			tb_switch_put(sw);
			return;
		}

		/*
		 * User connected the same switch to another physical
		 * port or to another part of the topology. Remove the
		 * existing switch now before adding the new one.
		 */
		remove_switch(sw);
		tb_switch_put(sw);
	}

	/*
	 * If the switch was not found by UUID, look for a switch on
	 * same physical port (taking possible link aggregation into
	 * account) and depth. If we found one it is definitely a stale
	 * one so remove it first.
	 */
	sw = tb_switch_find_by_link_depth(tb, link, depth);
	if (!sw) {
		u8 dual_link;

		dual_link = dual_link_from_link(link);
		if (dual_link)
			sw = tb_switch_find_by_link_depth(tb, dual_link, depth);
	}
	if (sw) {
		remove_switch(sw);
		tb_switch_put(sw);
	}

	/* Remove existing XDomain connection if found */
	xd = tb_xdomain_find_by_link_depth(tb, link, depth);
	if (xd) {
		remove_xdomain(xd);
		tb_xdomain_put(xd);
	}

	parent_sw = tb_switch_find_by_link_depth(tb, link, depth - 1);
	if (!parent_sw) {
		tb_err(tb, "failed to find parent switch for %u.%u\n",
		       link, depth);
		return;
	}

	ret = icm->get_route(tb, link, depth, &route);
	if (ret) {
		tb_err(tb, "failed to find route string for switch at %u.%u\n",
		       link, depth);
		tb_switch_put(parent_sw);
		return;
	}

	pm_runtime_get_sync(&parent_sw->dev);

	sw = alloc_switch(parent_sw, route, &pkg->ep_uuid);
	if (!IS_ERR(sw)) {
		sw->connection_id = pkg->connection_id;
		sw->connection_key = pkg->connection_key;
		sw->link = link;
		sw->depth = depth;
		sw->authorized = authorized;
		sw->security_level = security_level;
		sw->boot = boot;
		sw->link_speed = speed_gen3 ? 20 : 10;
		sw->link_width = dual_lane ? 2 : 1;
		sw->rpm = intel_vss_is_rtd3(pkg->ep_name, sizeof(pkg->ep_name));

		if (add_switch(parent_sw, sw))
			tb_switch_put(sw);
	}

	pm_runtime_mark_last_busy(&parent_sw->dev);
	pm_runtime_put_autosuspend(&parent_sw->dev);

	tb_switch_put(parent_sw);
}

static void
icm_fr_device_disconnected(struct tb *tb, const struct icm_pkg_header *hdr)
{
	const struct icm_fr_event_device_disconnected *pkg =
		(const struct icm_fr_event_device_disconnected *)hdr;
	struct tb_switch *sw;
	u8 link, depth;

	link = pkg->link_info & ICM_LINK_INFO_LINK_MASK;
	depth = (pkg->link_info & ICM_LINK_INFO_DEPTH_MASK) >>
		ICM_LINK_INFO_DEPTH_SHIFT;

	if (link > ICM_MAX_LINK || depth > TB_SWITCH_MAX_DEPTH) {
		tb_warn(tb, "invalid topology %u.%u, ignoring\n", link, depth);
		return;
	}

	sw = tb_switch_find_by_link_depth(tb, link, depth);
	if (!sw) {
		tb_warn(tb, "no switch exists at %u.%u, ignoring\n", link,
			depth);
		return;
	}

	pm_runtime_get_sync(sw->dev.parent);

	remove_switch(sw);

	pm_runtime_mark_last_busy(sw->dev.parent);
	pm_runtime_put_autosuspend(sw->dev.parent);

	tb_switch_put(sw);
}

static void
icm_fr_xdomain_connected(struct tb *tb, const struct icm_pkg_header *hdr)
{
	const struct icm_fr_event_xdomain_connected *pkg =
		(const struct icm_fr_event_xdomain_connected *)hdr;
	struct tb_xdomain *xd;
	struct tb_switch *sw;
	u8 link, depth;
	u64 route;

	link = pkg->link_info & ICM_LINK_INFO_LINK_MASK;
	depth = (pkg->link_info & ICM_LINK_INFO_DEPTH_MASK) >>
		ICM_LINK_INFO_DEPTH_SHIFT;

	if (link > ICM_MAX_LINK || depth > TB_SWITCH_MAX_DEPTH) {
		tb_warn(tb, "invalid topology %u.%u, ignoring\n", link, depth);
		return;
	}

	route = get_route(pkg->local_route_hi, pkg->local_route_lo);

	xd = tb_xdomain_find_by_uuid(tb, &pkg->remote_uuid);
	if (xd) {
		u8 xd_phy_port, phy_port;

		xd_phy_port = phy_port_from_route(xd->route, xd->depth);
		phy_port = phy_port_from_route(route, depth);

		if (xd->depth == depth && xd_phy_port == phy_port) {
			update_xdomain(xd, route, link);
			tb_xdomain_put(xd);
			return;
		}

		/*
		 * If we find an existing XDomain connection remove it
		 * now. We need to go through login handshake and
		 * everything anyway to be able to re-establish the
		 * connection.
		 */
		remove_xdomain(xd);
		tb_xdomain_put(xd);
	}

	/*
	 * Look if there already exists an XDomain in the same place
	 * than the new one and in that case remove it because it is
	 * most likely another host that got disconnected.
	 */
	xd = tb_xdomain_find_by_link_depth(tb, link, depth);
	if (!xd) {
		u8 dual_link;

		dual_link = dual_link_from_link(link);
		if (dual_link)
			xd = tb_xdomain_find_by_link_depth(tb, dual_link,
							   depth);
	}
	if (xd) {
		remove_xdomain(xd);
		tb_xdomain_put(xd);
	}

	/*
	 * If the user disconnected a switch during suspend and
	 * connected another host to the same port, remove the switch
	 * first.
	 */
	sw = tb_switch_find_by_route(tb, route);
	if (sw) {
		remove_switch(sw);
		tb_switch_put(sw);
	}

	sw = tb_switch_find_by_link_depth(tb, link, depth);
	if (!sw) {
		tb_warn(tb, "no switch exists at %u.%u, ignoring\n", link,
			depth);
		return;
	}

	add_xdomain(sw, route, &pkg->local_uuid, &pkg->remote_uuid, link,
		    depth);
	tb_switch_put(sw);
}

static void
icm_fr_xdomain_disconnected(struct tb *tb, const struct icm_pkg_header *hdr)
{
	const struct icm_fr_event_xdomain_disconnected *pkg =
		(const struct icm_fr_event_xdomain_disconnected *)hdr;
	struct tb_xdomain *xd;

	/*
	 * If the connection is through one or multiple devices, the
	 * XDomain device is removed along with them so it is fine if we
	 * cannot find it here.
	 */
	xd = tb_xdomain_find_by_uuid(tb, &pkg->remote_uuid);
	if (xd) {
		remove_xdomain(xd);
		tb_xdomain_put(xd);
	}
}

static int icm_tr_cio_reset(struct tb *tb)
{
	return pcie2cio_write(tb_priv(tb), TB_CFG_SWITCH, 0, 0x777, BIT(1));
}

static int
icm_tr_driver_ready(struct tb *tb, enum tb_security_level *security_level,
		    u8 *proto_version, size_t *nboot_acl, bool *rpm)
{
	struct icm_tr_pkg_driver_ready_response reply;
	struct icm_pkg_driver_ready request = {
		.hdr.code = ICM_DRIVER_READY,
	};
	int ret;

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, 20000);
	if (ret)
		return ret;

	if (security_level)
		*security_level = reply.info & ICM_TR_INFO_SLEVEL_MASK;
	if (proto_version)
		*proto_version = (reply.info & ICM_TR_INFO_PROTO_VERSION_MASK) >>
				ICM_TR_INFO_PROTO_VERSION_SHIFT;
	if (nboot_acl)
		*nboot_acl = (reply.info & ICM_TR_INFO_BOOT_ACL_MASK) >>
				ICM_TR_INFO_BOOT_ACL_SHIFT;
	if (rpm)
		*rpm = !!(reply.hdr.flags & ICM_TR_FLAGS_RTD3);

	return 0;
}

static int icm_tr_approve_switch(struct tb *tb, struct tb_switch *sw)
{
	struct icm_tr_pkg_approve_device request;
	struct icm_tr_pkg_approve_device reply;
	int ret;

	memset(&request, 0, sizeof(request));
	memcpy(&request.ep_uuid, sw->uuid, sizeof(request.ep_uuid));
	request.hdr.code = ICM_APPROVE_DEVICE;
	request.route_lo = sw->config.route_lo;
	request.route_hi = sw->config.route_hi;
	request.connection_id = sw->connection_id;

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_APPROVE_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR) {
		tb_warn(tb, "PCIe tunnel creation failed\n");
		return -EIO;
	}

	return 0;
}

static int icm_tr_add_switch_key(struct tb *tb, struct tb_switch *sw)
{
	struct icm_tr_pkg_add_device_key_response reply;
	struct icm_tr_pkg_add_device_key request;
	int ret;

	memset(&request, 0, sizeof(request));
	memcpy(&request.ep_uuid, sw->uuid, sizeof(request.ep_uuid));
	request.hdr.code = ICM_ADD_DEVICE_KEY;
	request.route_lo = sw->config.route_lo;
	request.route_hi = sw->config.route_hi;
	request.connection_id = sw->connection_id;
	memcpy(request.key, sw->key, TB_SWITCH_KEY_SIZE);

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR) {
		tb_warn(tb, "Adding key to switch failed\n");
		return -EIO;
	}

	return 0;
}

static int icm_tr_challenge_switch_key(struct tb *tb, struct tb_switch *sw,
				       const u8 *challenge, u8 *response)
{
	struct icm_tr_pkg_challenge_device_response reply;
	struct icm_tr_pkg_challenge_device request;
	int ret;

	memset(&request, 0, sizeof(request));
	memcpy(&request.ep_uuid, sw->uuid, sizeof(request.ep_uuid));
	request.hdr.code = ICM_CHALLENGE_DEVICE;
	request.route_lo = sw->config.route_lo;
	request.route_hi = sw->config.route_hi;
	request.connection_id = sw->connection_id;
	memcpy(request.challenge, challenge, TB_SWITCH_KEY_SIZE);

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR)
		return -EKEYREJECTED;
	if (reply.hdr.flags & ICM_FLAGS_NO_KEY)
		return -ENOKEY;

	memcpy(response, reply.response, TB_SWITCH_KEY_SIZE);

	return 0;
}

static int icm_tr_approve_xdomain_paths(struct tb *tb, struct tb_xdomain *xd)
{
	struct icm_tr_pkg_approve_xdomain_response reply;
	struct icm_tr_pkg_approve_xdomain request;
	int ret;

	memset(&request, 0, sizeof(request));
	request.hdr.code = ICM_APPROVE_XDOMAIN;
	request.route_hi = upper_32_bits(xd->route);
	request.route_lo = lower_32_bits(xd->route);
	request.transmit_path = xd->transmit_path;
	request.transmit_ring = xd->transmit_ring;
	request.receive_path = xd->receive_path;
	request.receive_ring = xd->receive_ring;
	memcpy(&request.remote_uuid, xd->remote_uuid, sizeof(*xd->remote_uuid));

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR)
		return -EIO;

	return 0;
}

static int icm_tr_xdomain_tear_down(struct tb *tb, struct tb_xdomain *xd,
				    int stage)
{
	struct icm_tr_pkg_disconnect_xdomain_response reply;
	struct icm_tr_pkg_disconnect_xdomain request;
	int ret;

	memset(&request, 0, sizeof(request));
	request.hdr.code = ICM_DISCONNECT_XDOMAIN;
	request.stage = stage;
	request.route_hi = upper_32_bits(xd->route);
	request.route_lo = lower_32_bits(xd->route);
	memcpy(&request.remote_uuid, xd->remote_uuid, sizeof(*xd->remote_uuid));

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR)
		return -EIO;

	return 0;
}

static int icm_tr_disconnect_xdomain_paths(struct tb *tb, struct tb_xdomain *xd)
{
	int ret;

	ret = icm_tr_xdomain_tear_down(tb, xd, 1);
	if (ret)
		return ret;

	usleep_range(10, 50);
	return icm_tr_xdomain_tear_down(tb, xd, 2);
}

static void
__icm_tr_device_connected(struct tb *tb, const struct icm_pkg_header *hdr,
			  bool force_rtd3)
{
	const struct icm_tr_event_device_connected *pkg =
		(const struct icm_tr_event_device_connected *)hdr;
	bool authorized, boot, dual_lane, speed_gen3;
	enum tb_security_level security_level;
	struct tb_switch *sw, *parent_sw;
	struct tb_xdomain *xd;
	u64 route;

	icm_postpone_rescan(tb);

	/*
	 * Currently we don't use the QoS information coming with the
	 * device connected message so simply just ignore that extra
	 * packet for now.
	 */
	if (pkg->hdr.packet_id)
		return;

	route = get_route(pkg->route_hi, pkg->route_lo);
	authorized = pkg->link_info & ICM_LINK_INFO_APPROVED;
	security_level = (pkg->hdr.flags & ICM_FLAGS_SLEVEL_MASK) >>
			 ICM_FLAGS_SLEVEL_SHIFT;
	boot = pkg->link_info & ICM_LINK_INFO_BOOT;
	dual_lane = pkg->hdr.flags & ICM_FLAGS_DUAL_LANE;
	speed_gen3 = pkg->hdr.flags & ICM_FLAGS_SPEED_GEN3;

	if (pkg->link_info & ICM_LINK_INFO_REJECTED) {
		tb_info(tb, "switch at %llx was rejected by ICM firmware because topology limit exceeded\n",
			route);
		return;
	}

	sw = tb_switch_find_by_uuid(tb, &pkg->ep_uuid);
	if (sw) {
		/* Update the switch if it is still in the same place */
		if (tb_route(sw) == route && !!sw->authorized == authorized) {
			parent_sw = tb_to_switch(sw->dev.parent);
			update_switch(parent_sw, sw, route, pkg->connection_id,
				      0, 0, 0, boot);
			tb_switch_put(sw);
			return;
		}

		remove_switch(sw);
		tb_switch_put(sw);
	}

	/* Another switch with the same address */
	sw = tb_switch_find_by_route(tb, route);
	if (sw) {
		remove_switch(sw);
		tb_switch_put(sw);
	}

	/* XDomain connection with the same address */
	xd = tb_xdomain_find_by_route(tb, route);
	if (xd) {
		remove_xdomain(xd);
		tb_xdomain_put(xd);
	}

	parent_sw = tb_switch_find_by_route(tb, get_parent_route(route));
	if (!parent_sw) {
		tb_err(tb, "failed to find parent switch for %llx\n", route);
		return;
	}

	pm_runtime_get_sync(&parent_sw->dev);

	sw = alloc_switch(parent_sw, route, &pkg->ep_uuid);
	if (!IS_ERR(sw)) {
		sw->connection_id = pkg->connection_id;
		sw->authorized = authorized;
		sw->security_level = security_level;
		sw->boot = boot;
		sw->link_speed = speed_gen3 ? 20 : 10;
		sw->link_width = dual_lane ? 2 : 1;
		sw->rpm = force_rtd3;
		if (!sw->rpm)
			sw->rpm = intel_vss_is_rtd3(pkg->ep_name,
						    sizeof(pkg->ep_name));

		if (add_switch(parent_sw, sw))
			tb_switch_put(sw);
	}

	pm_runtime_mark_last_busy(&parent_sw->dev);
	pm_runtime_put_autosuspend(&parent_sw->dev);

	tb_switch_put(parent_sw);
}

static void
icm_tr_device_connected(struct tb *tb, const struct icm_pkg_header *hdr)
{
	__icm_tr_device_connected(tb, hdr, false);
}

static void
icm_tr_device_disconnected(struct tb *tb, const struct icm_pkg_header *hdr)
{
	const struct icm_tr_event_device_disconnected *pkg =
		(const struct icm_tr_event_device_disconnected *)hdr;
	struct tb_switch *sw;
	u64 route;

	route = get_route(pkg->route_hi, pkg->route_lo);

	sw = tb_switch_find_by_route(tb, route);
	if (!sw) {
		tb_warn(tb, "no switch exists at %llx, ignoring\n", route);
		return;
	}
	pm_runtime_get_sync(sw->dev.parent);

	remove_switch(sw);

	pm_runtime_mark_last_busy(sw->dev.parent);
	pm_runtime_put_autosuspend(sw->dev.parent);

	tb_switch_put(sw);
}

static void
icm_tr_xdomain_connected(struct tb *tb, const struct icm_pkg_header *hdr)
{
	const struct icm_tr_event_xdomain_connected *pkg =
		(const struct icm_tr_event_xdomain_connected *)hdr;
	struct tb_xdomain *xd;
	struct tb_switch *sw;
	u64 route;

	if (!tb->root_switch)
		return;

	route = get_route(pkg->local_route_hi, pkg->local_route_lo);

	xd = tb_xdomain_find_by_uuid(tb, &pkg->remote_uuid);
	if (xd) {
		if (xd->route == route) {
			update_xdomain(xd, route, 0);
			tb_xdomain_put(xd);
			return;
		}

		remove_xdomain(xd);
		tb_xdomain_put(xd);
	}

	/* An existing xdomain with the same address */
	xd = tb_xdomain_find_by_route(tb, route);
	if (xd) {
		remove_xdomain(xd);
		tb_xdomain_put(xd);
	}

	/*
	 * If the user disconnected a switch during suspend and
	 * connected another host to the same port, remove the switch
	 * first.
	 */
	sw = tb_switch_find_by_route(tb, route);
	if (sw) {
		remove_switch(sw);
		tb_switch_put(sw);
	}

	sw = tb_switch_find_by_route(tb, get_parent_route(route));
	if (!sw) {
		tb_warn(tb, "no switch exists at %llx, ignoring\n", route);
		return;
	}

	add_xdomain(sw, route, &pkg->local_uuid, &pkg->remote_uuid, 0, 0);
	tb_switch_put(sw);
}

static void
icm_tr_xdomain_disconnected(struct tb *tb, const struct icm_pkg_header *hdr)
{
	const struct icm_tr_event_xdomain_disconnected *pkg =
		(const struct icm_tr_event_xdomain_disconnected *)hdr;
	struct tb_xdomain *xd;
	u64 route;

	route = get_route(pkg->route_hi, pkg->route_lo);

	xd = tb_xdomain_find_by_route(tb, route);
	if (xd) {
		remove_xdomain(xd);
		tb_xdomain_put(xd);
	}
}

static struct pci_dev *get_upstream_port(struct pci_dev *pdev)
{
	struct pci_dev *parent;

	parent = pci_upstream_bridge(pdev);
	while (parent) {
		if (!pci_is_pcie(parent))
			return NULL;
		if (pci_pcie_type(parent) == PCI_EXP_TYPE_UPSTREAM)
			break;
		parent = pci_upstream_bridge(parent);
	}

	if (!parent)
		return NULL;

	switch (parent->device) {
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_2C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_4C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_BRIDGE:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_2C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_4C_BRIDGE:
		return parent;
	}

	return NULL;
}

static bool icm_ar_is_supported(struct tb *tb)
{
	struct pci_dev *upstream_port;
	struct icm *icm = tb_priv(tb);

	/*
	 * Starting from Alpine Ridge we can use ICM on Apple machines
	 * as well. We just need to reset and re-enable it first.
	 * However, only start it if explicitly asked by the user.
	 */
	if (icm_firmware_running(tb->nhi))
		return true;
	if (!start_icm)
		return false;

	/*
	 * Find the upstream PCIe port in case we need to do reset
	 * through its vendor specific registers.
	 */
	upstream_port = get_upstream_port(tb->nhi->pdev);
	if (upstream_port) {
		int cap;

		cap = pci_find_ext_capability(upstream_port,
					      PCI_EXT_CAP_ID_VNDR);
		if (cap > 0) {
			icm->upstream_port = upstream_port;
			icm->vnd_cap = cap;

			return true;
		}
	}

	return false;
}

static int icm_ar_cio_reset(struct tb *tb)
{
	return pcie2cio_write(tb_priv(tb), TB_CFG_SWITCH, 0, 0x50, BIT(9));
}

static int icm_ar_get_mode(struct tb *tb)
{
	struct tb_nhi *nhi = tb->nhi;
	int retries = 60;
	u32 val;

	do {
		val = ioread32(nhi->iobase + REG_FW_STS);
		if (val & REG_FW_STS_NVM_AUTH_DONE)
			break;
		msleep(50);
	} while (--retries);

	if (!retries) {
		dev_err(&nhi->pdev->dev, "ICM firmware not authenticated\n");
		return -ENODEV;
	}

	return nhi_mailbox_mode(nhi);
}

static int
icm_ar_driver_ready(struct tb *tb, enum tb_security_level *security_level,
		    u8 *proto_version, size_t *nboot_acl, bool *rpm)
{
	struct icm_ar_pkg_driver_ready_response reply;
	struct icm_pkg_driver_ready request = {
		.hdr.code = ICM_DRIVER_READY,
	};
	int ret;

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (security_level)
		*security_level = reply.info & ICM_AR_INFO_SLEVEL_MASK;
	if (nboot_acl && (reply.info & ICM_AR_INFO_BOOT_ACL_SUPPORTED))
		*nboot_acl = (reply.info & ICM_AR_INFO_BOOT_ACL_MASK) >>
				ICM_AR_INFO_BOOT_ACL_SHIFT;
	if (rpm)
		*rpm = !!(reply.hdr.flags & ICM_AR_FLAGS_RTD3);

	return 0;
}

static int icm_ar_get_route(struct tb *tb, u8 link, u8 depth, u64 *route)
{
	struct icm_ar_pkg_get_route_response reply;
	struct icm_ar_pkg_get_route request = {
		.hdr = { .code = ICM_GET_ROUTE },
		.link_info = depth << ICM_LINK_INFO_DEPTH_SHIFT | link,
	};
	int ret;

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR)
		return -EIO;

	*route = get_route(reply.route_hi, reply.route_lo);
	return 0;
}

static int icm_ar_get_boot_acl(struct tb *tb, uuid_t *uuids, size_t nuuids)
{
	struct icm_ar_pkg_preboot_acl_response reply;
	struct icm_ar_pkg_preboot_acl request = {
		.hdr = { .code = ICM_PREBOOT_ACL },
	};
	int ret, i;

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR)
		return -EIO;

	for (i = 0; i < nuuids; i++) {
		u32 *uuid = (u32 *)&uuids[i];

		uuid[0] = reply.acl[i].uuid_lo;
		uuid[1] = reply.acl[i].uuid_hi;

		if (uuid[0] == 0xffffffff && uuid[1] == 0xffffffff) {
			/* Map empty entries to null UUID */
			uuid[0] = 0;
			uuid[1] = 0;
		} else if (uuid[0] != 0 || uuid[1] != 0) {
			/* Upper two DWs are always one's */
			uuid[2] = 0xffffffff;
			uuid[3] = 0xffffffff;
		}
	}

	return ret;
}

static int icm_ar_set_boot_acl(struct tb *tb, const uuid_t *uuids,
			       size_t nuuids)
{
	struct icm_ar_pkg_preboot_acl_response reply;
	struct icm_ar_pkg_preboot_acl request = {
		.hdr = {
			.code = ICM_PREBOOT_ACL,
			.flags = ICM_FLAGS_WRITE,
		},
	};
	int ret, i;

	for (i = 0; i < nuuids; i++) {
		const u32 *uuid = (const u32 *)&uuids[i];

		if (uuid_is_null(&uuids[i])) {
			/*
			 * Map null UUID to the empty (all one) entries
			 * for ICM.
			 */
			request.acl[i].uuid_lo = 0xffffffff;
			request.acl[i].uuid_hi = 0xffffffff;
		} else {
			/* Two high DWs need to be set to all one */
			if (uuid[2] != 0xffffffff || uuid[3] != 0xffffffff)
				return -EINVAL;

			request.acl[i].uuid_lo = uuid[0];
			request.acl[i].uuid_hi = uuid[1];
		}
	}

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR)
		return -EIO;

	return 0;
}

static int
icm_icl_driver_ready(struct tb *tb, enum tb_security_level *security_level,
		     u8 *proto_version, size_t *nboot_acl, bool *rpm)
{
	struct icm_tr_pkg_driver_ready_response reply;
	struct icm_pkg_driver_ready request = {
		.hdr.code = ICM_DRIVER_READY,
	};
	int ret;

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, 20000);
	if (ret)
		return ret;

	if (proto_version)
		*proto_version = (reply.info & ICM_TR_INFO_PROTO_VERSION_MASK) >>
				ICM_TR_INFO_PROTO_VERSION_SHIFT;

	/* Ice Lake always supports RTD3 */
	if (rpm)
		*rpm = true;

	return 0;
}

static void icm_icl_set_uuid(struct tb *tb)
{
	struct tb_nhi *nhi = tb->nhi;
	u32 uuid[4];

	pci_read_config_dword(nhi->pdev, VS_CAP_10, &uuid[0]);
	pci_read_config_dword(nhi->pdev, VS_CAP_11, &uuid[1]);
	uuid[2] = 0xffffffff;
	uuid[3] = 0xffffffff;

	tb->root_switch->uuid = kmemdup(uuid, sizeof(uuid), GFP_KERNEL);
}

static void
icm_icl_device_connected(struct tb *tb, const struct icm_pkg_header *hdr)
{
	__icm_tr_device_connected(tb, hdr, true);
}

static void icm_icl_rtd3_veto(struct tb *tb, const struct icm_pkg_header *hdr)
{
	const struct icm_icl_event_rtd3_veto *pkg =
		(const struct icm_icl_event_rtd3_veto *)hdr;

	tb_dbg(tb, "ICM rtd3 veto=0x%08x\n", pkg->veto_reason);

	if (pkg->veto_reason)
		icm_veto_begin(tb);
	else
		icm_veto_end(tb);
}

static bool icm_tgl_is_supported(struct tb *tb)
{
	u32 val;

	/*
	 * If the firmware is not running use software CM. This platform
	 * should fully support both.
	 */
	val = ioread32(tb->nhi->iobase + REG_FW_STS);
	return !!(val & REG_FW_STS_NVM_AUTH_DONE);
}

static void icm_handle_notification(struct work_struct *work)
{
	struct icm_notification *n = container_of(work, typeof(*n), work);
	struct tb *tb = n->tb;
	struct icm *icm = tb_priv(tb);

	mutex_lock(&tb->lock);

	/*
	 * When the domain is stopped we flush its workqueue but before
	 * that the root switch is removed. In that case we should treat
	 * the queued events as being canceled.
	 */
	if (tb->root_switch) {
		switch (n->pkg->code) {
		case ICM_EVENT_DEVICE_CONNECTED:
			icm->device_connected(tb, n->pkg);
			break;
		case ICM_EVENT_DEVICE_DISCONNECTED:
			icm->device_disconnected(tb, n->pkg);
			break;
		case ICM_EVENT_XDOMAIN_CONNECTED:
			if (tb_is_xdomain_enabled())
				icm->xdomain_connected(tb, n->pkg);
			break;
		case ICM_EVENT_XDOMAIN_DISCONNECTED:
			if (tb_is_xdomain_enabled())
				icm->xdomain_disconnected(tb, n->pkg);
			break;
		case ICM_EVENT_RTD3_VETO:
			icm->rtd3_veto(tb, n->pkg);
			break;
		}
	}

	mutex_unlock(&tb->lock);

	kfree(n->pkg);
	kfree(n);
}

static void icm_handle_event(struct tb *tb, enum tb_cfg_pkg_type type,
			     const void *buf, size_t size)
{
	struct icm_notification *n;

	n = kmalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		return;

	INIT_WORK(&n->work, icm_handle_notification);
	n->pkg = kmemdup(buf, size, GFP_KERNEL);
	n->tb = tb;

	queue_work(tb->wq, &n->work);
}

static int
__icm_driver_ready(struct tb *tb, enum tb_security_level *security_level,
		   u8 *proto_version, size_t *nboot_acl, bool *rpm)
{
	struct icm *icm = tb_priv(tb);
	unsigned int retries = 50;
	int ret;

	ret = icm->driver_ready(tb, security_level, proto_version, nboot_acl,
				rpm);
	if (ret) {
		tb_err(tb, "failed to send driver ready to ICM\n");
		return ret;
	}

	/*
	 * Hold on here until the switch config space is accessible so
	 * that we can read root switch config successfully.
	 */
	do {
		struct tb_cfg_result res;
		u32 tmp;

		res = tb_cfg_read_raw(tb->ctl, &tmp, 0, 0, TB_CFG_SWITCH,
				      0, 1, 100);
		if (!res.err)
			return 0;

		msleep(50);
	} while (--retries);

	tb_err(tb, "failed to read root switch config space, giving up\n");
	return -ETIMEDOUT;
}

static int icm_firmware_reset(struct tb *tb, struct tb_nhi *nhi)
{
	struct icm *icm = tb_priv(tb);
	u32 val;

	if (!icm->upstream_port)
		return -ENODEV;

	/* Put ARC to wait for CIO reset event to happen */
	val = ioread32(nhi->iobase + REG_FW_STS);
	val |= REG_FW_STS_CIO_RESET_REQ;
	iowrite32(val, nhi->iobase + REG_FW_STS);

	/* Re-start ARC */
	val = ioread32(nhi->iobase + REG_FW_STS);
	val |= REG_FW_STS_ICM_EN_INVERT;
	val |= REG_FW_STS_ICM_EN_CPU;
	iowrite32(val, nhi->iobase + REG_FW_STS);

	/* Trigger CIO reset now */
	return icm->cio_reset(tb);
}

static int icm_firmware_start(struct tb *tb, struct tb_nhi *nhi)
{
	unsigned int retries = 10;
	int ret;
	u32 val;

	/* Check if the ICM firmware is already running */
	if (icm_firmware_running(nhi))
		return 0;

	dev_dbg(&nhi->pdev->dev, "starting ICM firmware\n");

	ret = icm_firmware_reset(tb, nhi);
	if (ret)
		return ret;

	/* Wait until the ICM firmware tells us it is up and running */
	do {
		/* Check that the ICM firmware is running */
		val = ioread32(nhi->iobase + REG_FW_STS);
		if (val & REG_FW_STS_NVM_AUTH_DONE)
			return 0;

		msleep(300);
	} while (--retries);

	return -ETIMEDOUT;
}

static int icm_reset_phy_port(struct tb *tb, int phy_port)
{
	struct icm *icm = tb_priv(tb);
	u32 state0, state1;
	int port0, port1;
	u32 val0, val1;
	int ret;

	if (!icm->upstream_port)
		return 0;

	if (phy_port) {
		port0 = 3;
		port1 = 4;
	} else {
		port0 = 1;
		port1 = 2;
	}

	/*
	 * Read link status of both null ports belonging to a single
	 * physical port.
	 */
	ret = pcie2cio_read(icm, TB_CFG_PORT, port0, PHY_PORT_CS1, &val0);
	if (ret)
		return ret;
	ret = pcie2cio_read(icm, TB_CFG_PORT, port1, PHY_PORT_CS1, &val1);
	if (ret)
		return ret;

	state0 = val0 & PHY_PORT_CS1_LINK_STATE_MASK;
	state0 >>= PHY_PORT_CS1_LINK_STATE_SHIFT;
	state1 = val1 & PHY_PORT_CS1_LINK_STATE_MASK;
	state1 >>= PHY_PORT_CS1_LINK_STATE_SHIFT;

	/* If they are both up we need to reset them now */
	if (state0 != TB_PORT_UP || state1 != TB_PORT_UP)
		return 0;

	val0 |= PHY_PORT_CS1_LINK_DISABLE;
	ret = pcie2cio_write(icm, TB_CFG_PORT, port0, PHY_PORT_CS1, val0);
	if (ret)
		return ret;

	val1 |= PHY_PORT_CS1_LINK_DISABLE;
	ret = pcie2cio_write(icm, TB_CFG_PORT, port1, PHY_PORT_CS1, val1);
	if (ret)
		return ret;

	/* Wait a bit and then re-enable both ports */
	usleep_range(10, 100);

	ret = pcie2cio_read(icm, TB_CFG_PORT, port0, PHY_PORT_CS1, &val0);
	if (ret)
		return ret;
	ret = pcie2cio_read(icm, TB_CFG_PORT, port1, PHY_PORT_CS1, &val1);
	if (ret)
		return ret;

	val0 &= ~PHY_PORT_CS1_LINK_DISABLE;
	ret = pcie2cio_write(icm, TB_CFG_PORT, port0, PHY_PORT_CS1, val0);
	if (ret)
		return ret;

	val1 &= ~PHY_PORT_CS1_LINK_DISABLE;
	return pcie2cio_write(icm, TB_CFG_PORT, port1, PHY_PORT_CS1, val1);
}

static int icm_firmware_init(struct tb *tb)
{
	struct icm *icm = tb_priv(tb);
	struct tb_nhi *nhi = tb->nhi;
	int ret;

	ret = icm_firmware_start(tb, nhi);
	if (ret) {
		dev_err(&nhi->pdev->dev, "could not start ICM firmware\n");
		return ret;
	}

	if (icm->get_mode) {
		ret = icm->get_mode(tb);

		switch (ret) {
		case NHI_FW_SAFE_MODE:
			icm->safe_mode = true;
			break;

		case NHI_FW_CM_MODE:
			/* Ask ICM to accept all Thunderbolt devices */
			nhi_mailbox_cmd(nhi, NHI_MAILBOX_ALLOW_ALL_DEVS, 0);
			break;

		default:
			if (ret < 0)
				return ret;

			tb_err(tb, "ICM firmware is in wrong mode: %u\n", ret);
			return -ENODEV;
		}
	}

	/*
	 * Reset both physical ports if there is anything connected to
	 * them already.
	 */
	ret = icm_reset_phy_port(tb, 0);
	if (ret)
		dev_warn(&nhi->pdev->dev, "failed to reset links on port0\n");
	ret = icm_reset_phy_port(tb, 1);
	if (ret)
		dev_warn(&nhi->pdev->dev, "failed to reset links on port1\n");

	return 0;
}

static int icm_driver_ready(struct tb *tb)
{
	struct icm *icm = tb_priv(tb);
	int ret;

	ret = icm_firmware_init(tb);
	if (ret)
		return ret;

	if (icm->safe_mode) {
		tb_info(tb, "Thunderbolt host controller is in safe mode.\n");
		tb_info(tb, "You need to update NVM firmware of the controller before it can be used.\n");
		tb_info(tb, "For latest updates check https://thunderbolttechnology.net/updates.\n");
		return 0;
	}

	ret = __icm_driver_ready(tb, &tb->security_level, &icm->proto_version,
				 &tb->nboot_acl, &icm->rpm);
	if (ret)
		return ret;

	/*
	 * Make sure the number of supported preboot ACL matches what we
	 * expect or disable the whole feature.
	 */
	if (tb->nboot_acl > icm->max_boot_acl)
		tb->nboot_acl = 0;

	if (icm->proto_version >= 3)
		tb_dbg(tb, "USB4 proxy operations supported\n");

	return 0;
}

static int icm_suspend(struct tb *tb)
{
	struct icm *icm = tb_priv(tb);

	if (icm->save_devices)
		icm->save_devices(tb);

	nhi_mailbox_cmd(tb->nhi, NHI_MAILBOX_DRV_UNLOADS, 0);
	return 0;
}

/*
 * Mark all switches (except root switch) below this one unplugged. ICM
 * firmware will send us an updated list of switches after we have send
 * it driver ready command. If a switch is not in that list it will be
 * removed when we perform rescan.
 */
static void icm_unplug_children(struct tb_switch *sw)
{
	struct tb_port *port;

	if (tb_route(sw))
		sw->is_unplugged = true;

	tb_switch_for_each_port(sw, port) {
		if (port->xdomain)
			port->xdomain->is_unplugged = true;
		else if (tb_port_has_remote(port))
			icm_unplug_children(port->remote->sw);
	}
}

static int complete_rpm(struct device *dev, void *data)
{
	struct tb_switch *sw = tb_to_switch(dev);

	if (sw)
		complete(&sw->rpm_complete);
	return 0;
}

static void remove_unplugged_switch(struct tb_switch *sw)
{
	struct device *parent = get_device(sw->dev.parent);

	pm_runtime_get_sync(parent);

	/*
	 * Signal this and switches below for rpm_complete because
	 * tb_switch_remove() calls pm_runtime_get_sync() that then waits
	 * for it.
	 */
	complete_rpm(&sw->dev, NULL);
	bus_for_each_dev(&tb_bus_type, &sw->dev, NULL, complete_rpm);
	tb_switch_remove(sw);

	pm_runtime_mark_last_busy(parent);
	pm_runtime_put_autosuspend(parent);

	put_device(parent);
}

static void icm_free_unplugged_children(struct tb_switch *sw)
{
	struct tb_port *port;

	tb_switch_for_each_port(sw, port) {
		if (port->xdomain && port->xdomain->is_unplugged) {
			tb_xdomain_remove(port->xdomain);
			port->xdomain = NULL;
		} else if (tb_port_has_remote(port)) {
			if (port->remote->sw->is_unplugged) {
				remove_unplugged_switch(port->remote->sw);
				port->remote = NULL;
			} else {
				icm_free_unplugged_children(port->remote->sw);
			}
		}
	}
}

static void icm_rescan_work(struct work_struct *work)
{
	struct icm *icm = container_of(work, struct icm, rescan_work.work);
	struct tb *tb = icm_to_tb(icm);

	mutex_lock(&tb->lock);
	if (tb->root_switch)
		icm_free_unplugged_children(tb->root_switch);
	mutex_unlock(&tb->lock);
}

static void icm_complete(struct tb *tb)
{
	struct icm *icm = tb_priv(tb);

	if (tb->nhi->going_away)
		return;

	/*
	 * If RTD3 was vetoed before we entered system suspend allow it
	 * again now before driver ready is sent. Firmware sends a new RTD3
	 * veto if it is still the case after we have sent it driver ready
	 * command.
	 */
	icm_veto_end(tb);
	icm_unplug_children(tb->root_switch);

	/*
	 * Now all existing children should be resumed, start events
	 * from ICM to get updated status.
	 */
	__icm_driver_ready(tb, NULL, NULL, NULL, NULL);

	/*
	 * We do not get notifications of devices that have been
	 * unplugged during suspend so schedule rescan to clean them up
	 * if any.
	 */
	queue_delayed_work(tb->wq, &icm->rescan_work, msecs_to_jiffies(500));
}

static int icm_runtime_suspend(struct tb *tb)
{
	nhi_mailbox_cmd(tb->nhi, NHI_MAILBOX_DRV_UNLOADS, 0);
	return 0;
}

static int icm_runtime_suspend_switch(struct tb_switch *sw)
{
	if (tb_route(sw))
		reinit_completion(&sw->rpm_complete);
	return 0;
}

static int icm_runtime_resume_switch(struct tb_switch *sw)
{
	if (tb_route(sw)) {
		if (!wait_for_completion_timeout(&sw->rpm_complete,
						 msecs_to_jiffies(500))) {
			dev_dbg(&sw->dev, "runtime resuming timed out\n");
		}
	}
	return 0;
}

static int icm_runtime_resume(struct tb *tb)
{
	/*
	 * We can reuse the same resume functionality than with system
	 * suspend.
	 */
	icm_complete(tb);
	return 0;
}

static int icm_start(struct tb *tb)
{
	struct icm *icm = tb_priv(tb);
	int ret;

	if (icm->safe_mode)
		tb->root_switch = tb_switch_alloc_safe_mode(tb, &tb->dev, 0);
	else
		tb->root_switch = tb_switch_alloc(tb, &tb->dev, 0);
	if (IS_ERR(tb->root_switch))
		return PTR_ERR(tb->root_switch);

	tb->root_switch->no_nvm_upgrade = !icm->can_upgrade_nvm;
	tb->root_switch->rpm = icm->rpm;

	if (icm->set_uuid)
		icm->set_uuid(tb);

	ret = tb_switch_add(tb->root_switch);
	if (ret) {
		tb_switch_put(tb->root_switch);
		tb->root_switch = NULL;
	}

	return ret;
}

static void icm_stop(struct tb *tb)
{
	struct icm *icm = tb_priv(tb);

	cancel_delayed_work(&icm->rescan_work);
	tb_switch_remove(tb->root_switch);
	tb->root_switch = NULL;
	nhi_mailbox_cmd(tb->nhi, NHI_MAILBOX_DRV_UNLOADS, 0);
	kfree(icm->last_nvm_auth);
	icm->last_nvm_auth = NULL;
}

static int icm_disconnect_pcie_paths(struct tb *tb)
{
	return nhi_mailbox_cmd(tb->nhi, NHI_MAILBOX_DISCONNECT_PCIE_PATHS, 0);
}

static void icm_usb4_switch_nvm_auth_complete(void *data)
{
	struct usb4_switch_nvm_auth *auth = data;
	struct icm *icm = auth->icm;
	struct tb *tb = icm_to_tb(icm);

	tb_dbg(tb, "NVM_AUTH response for %llx flags %#x status %#x\n",
	       get_route(auth->reply.route_hi, auth->reply.route_lo),
	       auth->reply.hdr.flags, auth->reply.status);

	mutex_lock(&tb->lock);
	if (WARN_ON(icm->last_nvm_auth))
		kfree(icm->last_nvm_auth);
	icm->last_nvm_auth = auth;
	mutex_unlock(&tb->lock);
}

static int icm_usb4_switch_nvm_authenticate(struct tb *tb, u64 route)
{
	struct usb4_switch_nvm_auth *auth;
	struct icm *icm = tb_priv(tb);
	struct tb_cfg_request *req;
	int ret;

	auth = kzalloc(sizeof(*auth), GFP_KERNEL);
	if (!auth)
		return -ENOMEM;

	auth->icm = icm;
	auth->request.hdr.code = ICM_USB4_SWITCH_OP;
	auth->request.route_hi = upper_32_bits(route);
	auth->request.route_lo = lower_32_bits(route);
	auth->request.opcode = USB4_SWITCH_OP_NVM_AUTH;

	req = tb_cfg_request_alloc();
	if (!req) {
		ret = -ENOMEM;
		goto err_free_auth;
	}

	req->match = icm_match;
	req->copy = icm_copy;
	req->request = &auth->request;
	req->request_size = sizeof(auth->request);
	req->request_type = TB_CFG_PKG_ICM_CMD;
	req->response = &auth->reply;
	req->npackets = 1;
	req->response_size = sizeof(auth->reply);
	req->response_type = TB_CFG_PKG_ICM_RESP;

	tb_dbg(tb, "NVM_AUTH request for %llx\n", route);

	mutex_lock(&icm->request_lock);
	ret = tb_cfg_request(tb->ctl, req, icm_usb4_switch_nvm_auth_complete,
			     auth);
	mutex_unlock(&icm->request_lock);

	tb_cfg_request_put(req);
	if (ret)
		goto err_free_auth;
	return 0;

err_free_auth:
	kfree(auth);
	return ret;
}

static int icm_usb4_switch_op(struct tb_switch *sw, u16 opcode, u32 *metadata,
			      u8 *status, const void *tx_data, size_t tx_data_len,
			      void *rx_data, size_t rx_data_len)
{
	struct icm_usb4_switch_op_response reply;
	struct icm_usb4_switch_op request;
	struct tb *tb = sw->tb;
	struct icm *icm = tb_priv(tb);
	u64 route = tb_route(sw);
	int ret;

	/*
	 * USB4 router operation proxy is supported in firmware if the
	 * protocol version is 3 or higher.
	 */
	if (icm->proto_version < 3)
		return -EOPNOTSUPP;

	/*
	 * NVM_AUTH is a special USB4 proxy operation that does not
	 * return immediately so handle it separately.
	 */
	if (opcode == USB4_SWITCH_OP_NVM_AUTH)
		return icm_usb4_switch_nvm_authenticate(tb, route);

	memset(&request, 0, sizeof(request));
	request.hdr.code = ICM_USB4_SWITCH_OP;
	request.route_hi = upper_32_bits(route);
	request.route_lo = lower_32_bits(route);
	request.opcode = opcode;
	if (metadata)
		request.metadata = *metadata;

	if (tx_data_len) {
		request.data_len_valid |= ICM_USB4_SWITCH_DATA_VALID;
		if (tx_data_len < ARRAY_SIZE(request.data))
			request.data_len_valid =
				tx_data_len & ICM_USB4_SWITCH_DATA_LEN_MASK;
		memcpy(request.data, tx_data, tx_data_len * sizeof(u32));
	}

	memset(&reply, 0, sizeof(reply));
	ret = icm_request(tb, &request, sizeof(request), &reply, sizeof(reply),
			  1, ICM_TIMEOUT);
	if (ret)
		return ret;

	if (reply.hdr.flags & ICM_FLAGS_ERROR)
		return -EIO;

	if (status)
		*status = reply.status;

	if (metadata)
		*metadata = reply.metadata;

	if (rx_data_len)
		memcpy(rx_data, reply.data, rx_data_len * sizeof(u32));

	return 0;
}

static int icm_usb4_switch_nvm_authenticate_status(struct tb_switch *sw,
						   u32 *status)
{
	struct usb4_switch_nvm_auth *auth;
	struct tb *tb = sw->tb;
	struct icm *icm = tb_priv(tb);
	int ret = 0;

	if (icm->proto_version < 3)
		return -EOPNOTSUPP;

	auth = icm->last_nvm_auth;
	icm->last_nvm_auth = NULL;

	if (auth && auth->reply.route_hi == sw->config.route_hi &&
	    auth->reply.route_lo == sw->config.route_lo) {
		tb_dbg(tb, "NVM_AUTH found for %llx flags %#x status %#x\n",
		       tb_route(sw), auth->reply.hdr.flags, auth->reply.status);
		if (auth->reply.hdr.flags & ICM_FLAGS_ERROR)
			ret = -EIO;
		else
			*status = auth->reply.status;
	} else {
		*status = 0;
	}

	kfree(auth);
	return ret;
}

/* Falcon Ridge */
static const struct tb_cm_ops icm_fr_ops = {
	.driver_ready = icm_driver_ready,
	.start = icm_start,
	.stop = icm_stop,
	.suspend = icm_suspend,
	.complete = icm_complete,
	.handle_event = icm_handle_event,
	.approve_switch = icm_fr_approve_switch,
	.add_switch_key = icm_fr_add_switch_key,
	.challenge_switch_key = icm_fr_challenge_switch_key,
	.disconnect_pcie_paths = icm_disconnect_pcie_paths,
	.approve_xdomain_paths = icm_fr_approve_xdomain_paths,
	.disconnect_xdomain_paths = icm_fr_disconnect_xdomain_paths,
};

/* Alpine Ridge */
static const struct tb_cm_ops icm_ar_ops = {
	.driver_ready = icm_driver_ready,
	.start = icm_start,
	.stop = icm_stop,
	.suspend = icm_suspend,
	.complete = icm_complete,
	.runtime_suspend = icm_runtime_suspend,
	.runtime_resume = icm_runtime_resume,
	.runtime_suspend_switch = icm_runtime_suspend_switch,
	.runtime_resume_switch = icm_runtime_resume_switch,
	.handle_event = icm_handle_event,
	.get_boot_acl = icm_ar_get_boot_acl,
	.set_boot_acl = icm_ar_set_boot_acl,
	.approve_switch = icm_fr_approve_switch,
	.add_switch_key = icm_fr_add_switch_key,
	.challenge_switch_key = icm_fr_challenge_switch_key,
	.disconnect_pcie_paths = icm_disconnect_pcie_paths,
	.approve_xdomain_paths = icm_fr_approve_xdomain_paths,
	.disconnect_xdomain_paths = icm_fr_disconnect_xdomain_paths,
};

/* Titan Ridge */
static const struct tb_cm_ops icm_tr_ops = {
	.driver_ready = icm_driver_ready,
	.start = icm_start,
	.stop = icm_stop,
	.suspend = icm_suspend,
	.complete = icm_complete,
	.runtime_suspend = icm_runtime_suspend,
	.runtime_resume = icm_runtime_resume,
	.runtime_suspend_switch = icm_runtime_suspend_switch,
	.runtime_resume_switch = icm_runtime_resume_switch,
	.handle_event = icm_handle_event,
	.get_boot_acl = icm_ar_get_boot_acl,
	.set_boot_acl = icm_ar_set_boot_acl,
	.approve_switch = icm_tr_approve_switch,
	.add_switch_key = icm_tr_add_switch_key,
	.challenge_switch_key = icm_tr_challenge_switch_key,
	.disconnect_pcie_paths = icm_disconnect_pcie_paths,
	.approve_xdomain_paths = icm_tr_approve_xdomain_paths,
	.disconnect_xdomain_paths = icm_tr_disconnect_xdomain_paths,
	.usb4_switch_op = icm_usb4_switch_op,
	.usb4_switch_nvm_authenticate_status =
		icm_usb4_switch_nvm_authenticate_status,
};

/* Ice Lake */
static const struct tb_cm_ops icm_icl_ops = {
	.driver_ready = icm_driver_ready,
	.start = icm_start,
	.stop = icm_stop,
	.complete = icm_complete,
	.runtime_suspend = icm_runtime_suspend,
	.runtime_resume = icm_runtime_resume,
	.handle_event = icm_handle_event,
	.approve_xdomain_paths = icm_tr_approve_xdomain_paths,
	.disconnect_xdomain_paths = icm_tr_disconnect_xdomain_paths,
	.usb4_switch_op = icm_usb4_switch_op,
	.usb4_switch_nvm_authenticate_status =
		icm_usb4_switch_nvm_authenticate_status,
};

struct tb *icm_probe(struct tb_nhi *nhi)
{
	struct icm *icm;
	struct tb *tb;

	tb = tb_domain_alloc(nhi, sizeof(struct icm));
	if (!tb)
		return NULL;

	icm = tb_priv(tb);
	INIT_DELAYED_WORK(&icm->rescan_work, icm_rescan_work);
	mutex_init(&icm->request_lock);

	switch (nhi->pdev->device) {
	case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_2C_NHI:
	case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_4C_NHI:
		icm->can_upgrade_nvm = true;
		icm->is_supported = icm_fr_is_supported;
		icm->get_route = icm_fr_get_route;
		icm->save_devices = icm_fr_save_devices;
		icm->driver_ready = icm_fr_driver_ready;
		icm->device_connected = icm_fr_device_connected;
		icm->device_disconnected = icm_fr_device_disconnected;
		icm->xdomain_connected = icm_fr_xdomain_connected;
		icm->xdomain_disconnected = icm_fr_xdomain_disconnected;
		tb->cm_ops = &icm_fr_ops;
		break;

	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_2C_NHI:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_4C_NHI:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_NHI:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_NHI:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_NHI:
		icm->max_boot_acl = ICM_AR_PREBOOT_ACL_ENTRIES;
		/*
		 * NVM upgrade has not been tested on Apple systems and
		 * they don't provide images publicly either. To be on
		 * the safe side prevent root switch NVM upgrade on Macs
		 * for now.
		 */
		icm->can_upgrade_nvm = !x86_apple_machine;
		icm->is_supported = icm_ar_is_supported;
		icm->cio_reset = icm_ar_cio_reset;
		icm->get_mode = icm_ar_get_mode;
		icm->get_route = icm_ar_get_route;
		icm->save_devices = icm_fr_save_devices;
		icm->driver_ready = icm_ar_driver_ready;
		icm->device_connected = icm_fr_device_connected;
		icm->device_disconnected = icm_fr_device_disconnected;
		icm->xdomain_connected = icm_fr_xdomain_connected;
		icm->xdomain_disconnected = icm_fr_xdomain_disconnected;
		tb->cm_ops = &icm_ar_ops;
		break;

	case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_2C_NHI:
	case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_4C_NHI:
		icm->max_boot_acl = ICM_AR_PREBOOT_ACL_ENTRIES;
		icm->can_upgrade_nvm = !x86_apple_machine;
		icm->is_supported = icm_ar_is_supported;
		icm->cio_reset = icm_tr_cio_reset;
		icm->get_mode = icm_ar_get_mode;
		icm->driver_ready = icm_tr_driver_ready;
		icm->device_connected = icm_tr_device_connected;
		icm->device_disconnected = icm_tr_device_disconnected;
		icm->xdomain_connected = icm_tr_xdomain_connected;
		icm->xdomain_disconnected = icm_tr_xdomain_disconnected;
		tb->cm_ops = &icm_tr_ops;
		break;

	case PCI_DEVICE_ID_INTEL_ICL_NHI0:
	case PCI_DEVICE_ID_INTEL_ICL_NHI1:
		icm->is_supported = icm_fr_is_supported;
		icm->driver_ready = icm_icl_driver_ready;
		icm->set_uuid = icm_icl_set_uuid;
		icm->device_connected = icm_icl_device_connected;
		icm->device_disconnected = icm_tr_device_disconnected;
		icm->xdomain_connected = icm_tr_xdomain_connected;
		icm->xdomain_disconnected = icm_tr_xdomain_disconnected;
		icm->rtd3_veto = icm_icl_rtd3_veto;
		tb->cm_ops = &icm_icl_ops;
		break;

	case PCI_DEVICE_ID_INTEL_TGL_NHI0:
	case PCI_DEVICE_ID_INTEL_TGL_NHI1:
	case PCI_DEVICE_ID_INTEL_TGL_H_NHI0:
	case PCI_DEVICE_ID_INTEL_TGL_H_NHI1:
		icm->is_supported = icm_tgl_is_supported;
		icm->driver_ready = icm_icl_driver_ready;
		icm->set_uuid = icm_icl_set_uuid;
		icm->device_connected = icm_icl_device_connected;
		icm->device_disconnected = icm_tr_device_disconnected;
		icm->xdomain_connected = icm_tr_xdomain_connected;
		icm->xdomain_disconnected = icm_tr_xdomain_disconnected;
		icm->rtd3_veto = icm_icl_rtd3_veto;
		tb->cm_ops = &icm_icl_ops;
		break;

	case PCI_DEVICE_ID_INTEL_MAPLE_RIDGE_4C_NHI:
		icm->is_supported = icm_tgl_is_supported;
		icm->get_mode = icm_ar_get_mode;
		icm->driver_ready = icm_tr_driver_ready;
		icm->device_connected = icm_tr_device_connected;
		icm->device_disconnected = icm_tr_device_disconnected;
		icm->xdomain_connected = icm_tr_xdomain_connected;
		icm->xdomain_disconnected = icm_tr_xdomain_disconnected;
		tb->cm_ops = &icm_tr_ops;
		break;
	}

	if (!icm->is_supported || !icm->is_supported(tb)) {
		dev_dbg(&nhi->pdev->dev, "ICM not supported on this controller\n");
		tb_domain_put(tb);
		return NULL;
	}

	tb_dbg(tb, "using firmware connection manager\n");

	return tb;
}
