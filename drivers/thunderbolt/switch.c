// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt driver - switch/port utility functions
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2018, Intel Corporation
 */

#include <linux/delay.h>
#include <linux/idr.h>
#include <linux/nvmem-provider.h>
#include <linux/pm_runtime.h>
#include <linux/sched/signal.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "tb.h"

/* Switch NVM support */

struct nvm_auth_status {
	struct list_head list;
	uuid_t uuid;
	u32 status;
};

static bool clx_enabled = true;
module_param_named(clx, clx_enabled, bool, 0444);
MODULE_PARM_DESC(clx, "allow low power states on the high-speed lanes (default: true)");

/*
 * Hold NVM authentication failure status per switch This information
 * needs to stay around even when the switch gets power cycled so we
 * keep it separately.
 */
static LIST_HEAD(nvm_auth_status_cache);
static DEFINE_MUTEX(nvm_auth_status_lock);

static struct nvm_auth_status *__nvm_get_auth_status(const struct tb_switch *sw)
{
	struct nvm_auth_status *st;

	list_for_each_entry(st, &nvm_auth_status_cache, list) {
		if (uuid_equal(&st->uuid, sw->uuid))
			return st;
	}

	return NULL;
}

static void nvm_get_auth_status(const struct tb_switch *sw, u32 *status)
{
	struct nvm_auth_status *st;

	mutex_lock(&nvm_auth_status_lock);
	st = __nvm_get_auth_status(sw);
	mutex_unlock(&nvm_auth_status_lock);

	*status = st ? st->status : 0;
}

static void nvm_set_auth_status(const struct tb_switch *sw, u32 status)
{
	struct nvm_auth_status *st;

	if (WARN_ON(!sw->uuid))
		return;

	mutex_lock(&nvm_auth_status_lock);
	st = __nvm_get_auth_status(sw);

	if (!st) {
		st = kzalloc(sizeof(*st), GFP_KERNEL);
		if (!st)
			goto unlock;

		memcpy(&st->uuid, sw->uuid, sizeof(st->uuid));
		INIT_LIST_HEAD(&st->list);
		list_add_tail(&st->list, &nvm_auth_status_cache);
	}

	st->status = status;
unlock:
	mutex_unlock(&nvm_auth_status_lock);
}

static void nvm_clear_auth_status(const struct tb_switch *sw)
{
	struct nvm_auth_status *st;

	mutex_lock(&nvm_auth_status_lock);
	st = __nvm_get_auth_status(sw);
	if (st) {
		list_del(&st->list);
		kfree(st);
	}
	mutex_unlock(&nvm_auth_status_lock);
}

static int nvm_validate_and_write(struct tb_switch *sw)
{
	unsigned int image_size;
	const u8 *buf;
	int ret;

	ret = tb_nvm_validate(sw->nvm);
	if (ret)
		return ret;

	ret = tb_nvm_write_headers(sw->nvm);
	if (ret)
		return ret;

	buf = sw->nvm->buf_data_start;
	image_size = sw->nvm->buf_data_size;

	if (tb_switch_is_usb4(sw))
		ret = usb4_switch_nvm_write(sw, 0, buf, image_size);
	else
		ret = dma_port_flash_write(sw->dma_port, 0, buf, image_size);
	if (ret)
		return ret;

	sw->nvm->flushed = true;
	return 0;
}

static int nvm_authenticate_host_dma_port(struct tb_switch *sw)
{
	int ret = 0;

	/*
	 * Root switch NVM upgrade requires that we disconnect the
	 * existing paths first (in case it is not in safe mode
	 * already).
	 */
	if (!sw->safe_mode) {
		u32 status;

		ret = tb_domain_disconnect_all_paths(sw->tb);
		if (ret)
			return ret;
		/*
		 * The host controller goes away pretty soon after this if
		 * everything goes well so getting timeout is expected.
		 */
		ret = dma_port_flash_update_auth(sw->dma_port);
		if (!ret || ret == -ETIMEDOUT)
			return 0;

		/*
		 * Any error from update auth operation requires power
		 * cycling of the host router.
		 */
		tb_sw_warn(sw, "failed to authenticate NVM, power cycling\n");
		if (dma_port_flash_update_auth_status(sw->dma_port, &status) > 0)
			nvm_set_auth_status(sw, status);
	}

	/*
	 * From safe mode we can get out by just power cycling the
	 * switch.
	 */
	dma_port_power_cycle(sw->dma_port);
	return ret;
}

static int nvm_authenticate_device_dma_port(struct tb_switch *sw)
{
	int ret, retries = 10;

	ret = dma_port_flash_update_auth(sw->dma_port);
	switch (ret) {
	case 0:
	case -ETIMEDOUT:
	case -EACCES:
	case -EINVAL:
		/* Power cycle is required */
		break;
	default:
		return ret;
	}

	/*
	 * Poll here for the authentication status. It takes some time
	 * for the device to respond (we get timeout for a while). Once
	 * we get response the device needs to be power cycled in order
	 * to the new NVM to be taken into use.
	 */
	do {
		u32 status;

		ret = dma_port_flash_update_auth_status(sw->dma_port, &status);
		if (ret < 0 && ret != -ETIMEDOUT)
			return ret;
		if (ret > 0) {
			if (status) {
				tb_sw_warn(sw, "failed to authenticate NVM\n");
				nvm_set_auth_status(sw, status);
			}

			tb_sw_info(sw, "power cycling the switch now\n");
			dma_port_power_cycle(sw->dma_port);
			return 0;
		}

		msleep(500);
	} while (--retries);

	return -ETIMEDOUT;
}

static void nvm_authenticate_start_dma_port(struct tb_switch *sw)
{
	struct pci_dev *root_port;

	/*
	 * During host router NVM upgrade we should not allow root port to
	 * go into D3cold because some root ports cannot trigger PME
	 * itself. To be on the safe side keep the root port in D0 during
	 * the whole upgrade process.
	 */
	root_port = pcie_find_root_port(sw->tb->nhi->pdev);
	if (root_port)
		pm_runtime_get_noresume(&root_port->dev);
}

static void nvm_authenticate_complete_dma_port(struct tb_switch *sw)
{
	struct pci_dev *root_port;

	root_port = pcie_find_root_port(sw->tb->nhi->pdev);
	if (root_port)
		pm_runtime_put(&root_port->dev);
}

static inline bool nvm_readable(struct tb_switch *sw)
{
	if (tb_switch_is_usb4(sw)) {
		/*
		 * USB4 devices must support NVM operations but it is
		 * optional for hosts. Therefore we query the NVM sector
		 * size here and if it is supported assume NVM
		 * operations are implemented.
		 */
		return usb4_switch_nvm_sector_size(sw) > 0;
	}

	/* Thunderbolt 2 and 3 devices support NVM through DMA port */
	return !!sw->dma_port;
}

static inline bool nvm_upgradeable(struct tb_switch *sw)
{
	if (sw->no_nvm_upgrade)
		return false;
	return nvm_readable(sw);
}

static int nvm_authenticate(struct tb_switch *sw, bool auth_only)
{
	int ret;

	if (tb_switch_is_usb4(sw)) {
		if (auth_only) {
			ret = usb4_switch_nvm_set_offset(sw, 0);
			if (ret)
				return ret;
		}
		sw->nvm->authenticating = true;
		return usb4_switch_nvm_authenticate(sw);
	} else if (auth_only) {
		return -EOPNOTSUPP;
	}

	sw->nvm->authenticating = true;
	if (!tb_route(sw)) {
		nvm_authenticate_start_dma_port(sw);
		ret = nvm_authenticate_host_dma_port(sw);
	} else {
		ret = nvm_authenticate_device_dma_port(sw);
	}

	return ret;
}

/**
 * tb_switch_nvm_read() - Read router NVM
 * @sw: Router whose NVM to read
 * @address: Start address on the NVM
 * @buf: Buffer where the read data is copied
 * @size: Size of the buffer in bytes
 *
 * Reads from router NVM and returns the requested data in @buf. Locking
 * is up to the caller. Returns %0 in success and negative errno in case
 * of failure.
 */
int tb_switch_nvm_read(struct tb_switch *sw, unsigned int address, void *buf,
		       size_t size)
{
	if (tb_switch_is_usb4(sw))
		return usb4_switch_nvm_read(sw, address, buf, size);
	return dma_port_flash_read(sw->dma_port, address, buf, size);
}

static int nvm_read(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct tb_nvm *nvm = priv;
	struct tb_switch *sw = tb_to_switch(nvm->dev);
	int ret;

	pm_runtime_get_sync(&sw->dev);

	if (!mutex_trylock(&sw->tb->lock)) {
		ret = restart_syscall();
		goto out;
	}

	ret = tb_switch_nvm_read(sw, offset, val, bytes);
	mutex_unlock(&sw->tb->lock);

out:
	pm_runtime_mark_last_busy(&sw->dev);
	pm_runtime_put_autosuspend(&sw->dev);

	return ret;
}

static int nvm_write(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct tb_nvm *nvm = priv;
	struct tb_switch *sw = tb_to_switch(nvm->dev);
	int ret;

	if (!mutex_trylock(&sw->tb->lock))
		return restart_syscall();

	/*
	 * Since writing the NVM image might require some special steps,
	 * for example when CSS headers are written, we cache the image
	 * locally here and handle the special cases when the user asks
	 * us to authenticate the image.
	 */
	ret = tb_nvm_write_buf(nvm, offset, val, bytes);
	mutex_unlock(&sw->tb->lock);

	return ret;
}

static int tb_switch_nvm_add(struct tb_switch *sw)
{
	struct tb_nvm *nvm;
	int ret;

	if (!nvm_readable(sw))
		return 0;

	nvm = tb_nvm_alloc(&sw->dev);
	if (IS_ERR(nvm)) {
		ret = PTR_ERR(nvm) == -EOPNOTSUPP ? 0 : PTR_ERR(nvm);
		goto err_nvm;
	}

	ret = tb_nvm_read_version(nvm);
	if (ret)
		goto err_nvm;

	/*
	 * If the switch is in safe-mode the only accessible portion of
	 * the NVM is the non-active one where userspace is expected to
	 * write new functional NVM.
	 */
	if (!sw->safe_mode) {
		ret = tb_nvm_add_active(nvm, nvm_read);
		if (ret)
			goto err_nvm;
	}

	if (!sw->no_nvm_upgrade) {
		ret = tb_nvm_add_non_active(nvm, nvm_write);
		if (ret)
			goto err_nvm;
	}

	sw->nvm = nvm;
	return 0;

err_nvm:
	tb_sw_dbg(sw, "NVM upgrade disabled\n");
	sw->no_nvm_upgrade = true;
	if (!IS_ERR(nvm))
		tb_nvm_free(nvm);

	return ret;
}

static void tb_switch_nvm_remove(struct tb_switch *sw)
{
	struct tb_nvm *nvm;

	nvm = sw->nvm;
	sw->nvm = NULL;

	if (!nvm)
		return;

	/* Remove authentication status in case the switch is unplugged */
	if (!nvm->authenticating)
		nvm_clear_auth_status(sw);

	tb_nvm_free(nvm);
}

/* port utility functions */

static const char *tb_port_type(const struct tb_regs_port_header *port)
{
	switch (port->type >> 16) {
	case 0:
		switch ((u8) port->type) {
		case 0:
			return "Inactive";
		case 1:
			return "Port";
		case 2:
			return "NHI";
		default:
			return "unknown";
		}
	case 0x2:
		return "Ethernet";
	case 0x8:
		return "SATA";
	case 0xe:
		return "DP/HDMI";
	case 0x10:
		return "PCIe";
	case 0x20:
		return "USB";
	default:
		return "unknown";
	}
}

static void tb_dump_port(struct tb *tb, const struct tb_port *port)
{
	const struct tb_regs_port_header *regs = &port->config;

	tb_dbg(tb,
	       " Port %d: %x:%x (Revision: %d, TB Version: %d, Type: %s (%#x))\n",
	       regs->port_number, regs->vendor_id, regs->device_id,
	       regs->revision, regs->thunderbolt_version, tb_port_type(regs),
	       regs->type);
	tb_dbg(tb, "  Max hop id (in/out): %d/%d\n",
	       regs->max_in_hop_id, regs->max_out_hop_id);
	tb_dbg(tb, "  Max counters: %d\n", regs->max_counters);
	tb_dbg(tb, "  NFC Credits: %#x\n", regs->nfc_credits);
	tb_dbg(tb, "  Credits (total/control): %u/%u\n", port->total_credits,
	       port->ctl_credits);
}

/**
 * tb_port_state() - get connectedness state of a port
 * @port: the port to check
 *
 * The port must have a TB_CAP_PHY (i.e. it should be a real port).
 *
 * Return: Returns an enum tb_port_state on success or an error code on failure.
 */
int tb_port_state(struct tb_port *port)
{
	struct tb_cap_phy phy;
	int res;
	if (port->cap_phy == 0) {
		tb_port_WARN(port, "does not have a PHY\n");
		return -EINVAL;
	}
	res = tb_port_read(port, &phy, TB_CFG_PORT, port->cap_phy, 2);
	if (res)
		return res;
	return phy.state;
}

/**
 * tb_wait_for_port() - wait for a port to become ready
 * @port: Port to wait
 * @wait_if_unplugged: Wait also when port is unplugged
 *
 * Wait up to 1 second for a port to reach state TB_PORT_UP. If
 * wait_if_unplugged is set then we also wait if the port is in state
 * TB_PORT_UNPLUGGED (it takes a while for the device to be registered after
 * switch resume). Otherwise we only wait if a device is registered but the link
 * has not yet been established.
 *
 * Return: Returns an error code on failure. Returns 0 if the port is not
 * connected or failed to reach state TB_PORT_UP within one second. Returns 1
 * if the port is connected and in state TB_PORT_UP.
 */
int tb_wait_for_port(struct tb_port *port, bool wait_if_unplugged)
{
	int retries = 10;
	int state;
	if (!port->cap_phy) {
		tb_port_WARN(port, "does not have PHY\n");
		return -EINVAL;
	}
	if (tb_is_upstream_port(port)) {
		tb_port_WARN(port, "is the upstream port\n");
		return -EINVAL;
	}

	while (retries--) {
		state = tb_port_state(port);
		if (state < 0)
			return state;
		if (state == TB_PORT_DISABLED) {
			tb_port_dbg(port, "is disabled (state: 0)\n");
			return 0;
		}
		if (state == TB_PORT_UNPLUGGED) {
			if (wait_if_unplugged) {
				/* used during resume */
				tb_port_dbg(port,
					    "is unplugged (state: 7), retrying...\n");
				msleep(100);
				continue;
			}
			tb_port_dbg(port, "is unplugged (state: 7)\n");
			return 0;
		}
		if (state == TB_PORT_UP) {
			tb_port_dbg(port, "is connected, link is up (state: 2)\n");
			return 1;
		}

		/*
		 * After plug-in the state is TB_PORT_CONNECTING. Give it some
		 * time.
		 */
		tb_port_dbg(port,
			    "is connected, link is not up (state: %d), retrying...\n",
			    state);
		msleep(100);
	}
	tb_port_warn(port,
		     "failed to reach state TB_PORT_UP. Ignoring port...\n");
	return 0;
}

/**
 * tb_port_add_nfc_credits() - add/remove non flow controlled credits to port
 * @port: Port to add/remove NFC credits
 * @credits: Credits to add/remove
 *
 * Change the number of NFC credits allocated to @port by @credits. To remove
 * NFC credits pass a negative amount of credits.
 *
 * Return: Returns 0 on success or an error code on failure.
 */
int tb_port_add_nfc_credits(struct tb_port *port, int credits)
{
	u32 nfc_credits;

	if (credits == 0 || port->sw->is_unplugged)
		return 0;

	/*
	 * USB4 restricts programming NFC buffers to lane adapters only
	 * so skip other ports.
	 */
	if (tb_switch_is_usb4(port->sw) && !tb_port_is_null(port))
		return 0;

	nfc_credits = port->config.nfc_credits & ADP_CS_4_NFC_BUFFERS_MASK;
	if (credits < 0)
		credits = max_t(int, -nfc_credits, credits);

	nfc_credits += credits;

	tb_port_dbg(port, "adding %d NFC credits to %lu", credits,
		    port->config.nfc_credits & ADP_CS_4_NFC_BUFFERS_MASK);

	port->config.nfc_credits &= ~ADP_CS_4_NFC_BUFFERS_MASK;
	port->config.nfc_credits |= nfc_credits;

	return tb_port_write(port, &port->config.nfc_credits,
			     TB_CFG_PORT, ADP_CS_4, 1);
}

/**
 * tb_port_clear_counter() - clear a counter in TB_CFG_COUNTER
 * @port: Port whose counters to clear
 * @counter: Counter index to clear
 *
 * Return: Returns 0 on success or an error code on failure.
 */
int tb_port_clear_counter(struct tb_port *port, int counter)
{
	u32 zero[3] = { 0, 0, 0 };
	tb_port_dbg(port, "clearing counter %d\n", counter);
	return tb_port_write(port, zero, TB_CFG_COUNTERS, 3 * counter, 3);
}

/**
 * tb_port_unlock() - Unlock downstream port
 * @port: Port to unlock
 *
 * Needed for USB4 but can be called for any CIO/USB4 ports. Makes the
 * downstream router accessible for CM.
 */
int tb_port_unlock(struct tb_port *port)
{
	if (tb_switch_is_icm(port->sw))
		return 0;
	if (!tb_port_is_null(port))
		return -EINVAL;
	if (tb_switch_is_usb4(port->sw))
		return usb4_port_unlock(port);
	return 0;
}

static int __tb_port_enable(struct tb_port *port, bool enable)
{
	int ret;
	u32 phy;

	if (!tb_port_is_null(port))
		return -EINVAL;

	ret = tb_port_read(port, &phy, TB_CFG_PORT,
			   port->cap_phy + LANE_ADP_CS_1, 1);
	if (ret)
		return ret;

	if (enable)
		phy &= ~LANE_ADP_CS_1_LD;
	else
		phy |= LANE_ADP_CS_1_LD;


	ret = tb_port_write(port, &phy, TB_CFG_PORT,
			    port->cap_phy + LANE_ADP_CS_1, 1);
	if (ret)
		return ret;

	tb_port_dbg(port, "lane %sabled\n", enable ? "en" : "dis");
	return 0;
}

/**
 * tb_port_enable() - Enable lane adapter
 * @port: Port to enable (can be %NULL)
 *
 * This is used for lane 0 and 1 adapters to enable it.
 */
int tb_port_enable(struct tb_port *port)
{
	return __tb_port_enable(port, true);
}

/**
 * tb_port_disable() - Disable lane adapter
 * @port: Port to disable (can be %NULL)
 *
 * This is used for lane 0 and 1 adapters to disable it.
 */
int tb_port_disable(struct tb_port *port)
{
	return __tb_port_enable(port, false);
}

/*
 * tb_init_port() - initialize a port
 *
 * This is a helper method for tb_switch_alloc. Does not check or initialize
 * any downstream switches.
 *
 * Return: Returns 0 on success or an error code on failure.
 */
static int tb_init_port(struct tb_port *port)
{
	int res;
	int cap;

	INIT_LIST_HEAD(&port->list);

	/* Control adapter does not have configuration space */
	if (!port->port)
		return 0;

	res = tb_port_read(port, &port->config, TB_CFG_PORT, 0, 8);
	if (res) {
		if (res == -ENODEV) {
			tb_dbg(port->sw->tb, " Port %d: not implemented\n",
			       port->port);
			port->disabled = true;
			return 0;
		}
		return res;
	}

	/* Port 0 is the switch itself and has no PHY. */
	if (port->config.type == TB_TYPE_PORT) {
		cap = tb_port_find_cap(port, TB_PORT_CAP_PHY);

		if (cap > 0)
			port->cap_phy = cap;
		else
			tb_port_WARN(port, "non switch port without a PHY\n");

		cap = tb_port_find_cap(port, TB_PORT_CAP_USB4);
		if (cap > 0)
			port->cap_usb4 = cap;

		/*
		 * USB4 ports the buffers allocated for the control path
		 * can be read from the path config space. Legacy
		 * devices we use hard-coded value.
		 */
		if (tb_switch_is_usb4(port->sw)) {
			struct tb_regs_hop hop;

			if (!tb_port_read(port, &hop, TB_CFG_HOPS, 0, 2))
				port->ctl_credits = hop.initial_credits;
		}
		if (!port->ctl_credits)
			port->ctl_credits = 2;

	} else {
		cap = tb_port_find_cap(port, TB_PORT_CAP_ADAP);
		if (cap > 0)
			port->cap_adap = cap;
	}

	port->total_credits =
		(port->config.nfc_credits & ADP_CS_4_TOTAL_BUFFERS_MASK) >>
		ADP_CS_4_TOTAL_BUFFERS_SHIFT;

	tb_dump_port(port->sw->tb, port);
	return 0;
}

static int tb_port_alloc_hopid(struct tb_port *port, bool in, int min_hopid,
			       int max_hopid)
{
	int port_max_hopid;
	struct ida *ida;

	if (in) {
		port_max_hopid = port->config.max_in_hop_id;
		ida = &port->in_hopids;
	} else {
		port_max_hopid = port->config.max_out_hop_id;
		ida = &port->out_hopids;
	}

	/*
	 * NHI can use HopIDs 1-max for other adapters HopIDs 0-7 are
	 * reserved.
	 */
	if (!tb_port_is_nhi(port) && min_hopid < TB_PATH_MIN_HOPID)
		min_hopid = TB_PATH_MIN_HOPID;

	if (max_hopid < 0 || max_hopid > port_max_hopid)
		max_hopid = port_max_hopid;

	return ida_simple_get(ida, min_hopid, max_hopid + 1, GFP_KERNEL);
}

/**
 * tb_port_alloc_in_hopid() - Allocate input HopID from port
 * @port: Port to allocate HopID for
 * @min_hopid: Minimum acceptable input HopID
 * @max_hopid: Maximum acceptable input HopID
 *
 * Return: HopID between @min_hopid and @max_hopid or negative errno in
 * case of error.
 */
int tb_port_alloc_in_hopid(struct tb_port *port, int min_hopid, int max_hopid)
{
	return tb_port_alloc_hopid(port, true, min_hopid, max_hopid);
}

/**
 * tb_port_alloc_out_hopid() - Allocate output HopID from port
 * @port: Port to allocate HopID for
 * @min_hopid: Minimum acceptable output HopID
 * @max_hopid: Maximum acceptable output HopID
 *
 * Return: HopID between @min_hopid and @max_hopid or negative errno in
 * case of error.
 */
int tb_port_alloc_out_hopid(struct tb_port *port, int min_hopid, int max_hopid)
{
	return tb_port_alloc_hopid(port, false, min_hopid, max_hopid);
}

/**
 * tb_port_release_in_hopid() - Release allocated input HopID from port
 * @port: Port whose HopID to release
 * @hopid: HopID to release
 */
void tb_port_release_in_hopid(struct tb_port *port, int hopid)
{
	ida_simple_remove(&port->in_hopids, hopid);
}

/**
 * tb_port_release_out_hopid() - Release allocated output HopID from port
 * @port: Port whose HopID to release
 * @hopid: HopID to release
 */
void tb_port_release_out_hopid(struct tb_port *port, int hopid)
{
	ida_simple_remove(&port->out_hopids, hopid);
}

static inline bool tb_switch_is_reachable(const struct tb_switch *parent,
					  const struct tb_switch *sw)
{
	u64 mask = (1ULL << parent->config.depth * 8) - 1;
	return (tb_route(parent) & mask) == (tb_route(sw) & mask);
}

/**
 * tb_next_port_on_path() - Return next port for given port on a path
 * @start: Start port of the walk
 * @end: End port of the walk
 * @prev: Previous port (%NULL if this is the first)
 *
 * This function can be used to walk from one port to another if they
 * are connected through zero or more switches. If the @prev is dual
 * link port, the function follows that link and returns another end on
 * that same link.
 *
 * If the @end port has been reached, return %NULL.
 *
 * Domain tb->lock must be held when this function is called.
 */
struct tb_port *tb_next_port_on_path(struct tb_port *start, struct tb_port *end,
				     struct tb_port *prev)
{
	struct tb_port *next;

	if (!prev)
		return start;

	if (prev->sw == end->sw) {
		if (prev == end)
			return NULL;
		return end;
	}

	if (tb_switch_is_reachable(prev->sw, end->sw)) {
		next = tb_port_at(tb_route(end->sw), prev->sw);
		/* Walk down the topology if next == prev */
		if (prev->remote &&
		    (next == prev || next->dual_link_port == prev))
			next = prev->remote;
	} else {
		if (tb_is_upstream_port(prev)) {
			next = prev->remote;
		} else {
			next = tb_upstream_port(prev->sw);
			/*
			 * Keep the same link if prev and next are both
			 * dual link ports.
			 */
			if (next->dual_link_port &&
			    next->link_nr != prev->link_nr) {
				next = next->dual_link_port;
			}
		}
	}

	return next != prev ? next : NULL;
}

/**
 * tb_port_get_link_speed() - Get current link speed
 * @port: Port to check (USB4 or CIO)
 *
 * Returns link speed in Gb/s or negative errno in case of failure.
 */
int tb_port_get_link_speed(struct tb_port *port)
{
	u32 val, speed;
	int ret;

	if (!port->cap_phy)
		return -EINVAL;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_phy + LANE_ADP_CS_1, 1);
	if (ret)
		return ret;

	speed = (val & LANE_ADP_CS_1_CURRENT_SPEED_MASK) >>
		LANE_ADP_CS_1_CURRENT_SPEED_SHIFT;
	return speed == LANE_ADP_CS_1_CURRENT_SPEED_GEN3 ? 20 : 10;
}

/**
 * tb_port_get_link_width() - Get current link width
 * @port: Port to check (USB4 or CIO)
 *
 * Returns link width. Return values can be 1 (Single-Lane), 2 (Dual-Lane)
 * or negative errno in case of failure.
 */
int tb_port_get_link_width(struct tb_port *port)
{
	u32 val;
	int ret;

	if (!port->cap_phy)
		return -EINVAL;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_phy + LANE_ADP_CS_1, 1);
	if (ret)
		return ret;

	return (val & LANE_ADP_CS_1_CURRENT_WIDTH_MASK) >>
		LANE_ADP_CS_1_CURRENT_WIDTH_SHIFT;
}

static bool tb_port_is_width_supported(struct tb_port *port, int width)
{
	u32 phy, widths;
	int ret;

	if (!port->cap_phy)
		return false;

	ret = tb_port_read(port, &phy, TB_CFG_PORT,
			   port->cap_phy + LANE_ADP_CS_0, 1);
	if (ret)
		return false;

	widths = (phy & LANE_ADP_CS_0_SUPPORTED_WIDTH_MASK) >>
		LANE_ADP_CS_0_SUPPORTED_WIDTH_SHIFT;

	return !!(widths & width);
}

/**
 * tb_port_set_link_width() - Set target link width of the lane adapter
 * @port: Lane adapter
 * @width: Target link width (%1 or %2)
 *
 * Sets the target link width of the lane adapter to @width. Does not
 * enable/disable lane bonding. For that call tb_port_set_lane_bonding().
 *
 * Return: %0 in case of success and negative errno in case of error
 */
int tb_port_set_link_width(struct tb_port *port, unsigned int width)
{
	u32 val;
	int ret;

	if (!port->cap_phy)
		return -EINVAL;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_phy + LANE_ADP_CS_1, 1);
	if (ret)
		return ret;

	val &= ~LANE_ADP_CS_1_TARGET_WIDTH_MASK;
	switch (width) {
	case 1:
		val |= LANE_ADP_CS_1_TARGET_WIDTH_SINGLE <<
			LANE_ADP_CS_1_TARGET_WIDTH_SHIFT;
		break;
	case 2:
		val |= LANE_ADP_CS_1_TARGET_WIDTH_DUAL <<
			LANE_ADP_CS_1_TARGET_WIDTH_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_phy + LANE_ADP_CS_1, 1);
}

/**
 * tb_port_set_lane_bonding() - Enable/disable lane bonding
 * @port: Lane adapter
 * @bonding: enable/disable bonding
 *
 * Enables or disables lane bonding. This should be called after target
 * link width has been set (tb_port_set_link_width()). Note in most
 * cases one should use tb_port_lane_bonding_enable() instead to enable
 * lane bonding.
 *
 * As a side effect sets @port->bonding accordingly (and does the same
 * for lane 1 too).
 *
 * Return: %0 in case of success and negative errno in case of error
 */
int tb_port_set_lane_bonding(struct tb_port *port, bool bonding)
{
	u32 val;
	int ret;

	if (!port->cap_phy)
		return -EINVAL;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_phy + LANE_ADP_CS_1, 1);
	if (ret)
		return ret;

	if (bonding)
		val |= LANE_ADP_CS_1_LB;
	else
		val &= ~LANE_ADP_CS_1_LB;

	ret = tb_port_write(port, &val, TB_CFG_PORT,
			    port->cap_phy + LANE_ADP_CS_1, 1);
	if (ret)
		return ret;

	/*
	 * When lane 0 bonding is set it will affect lane 1 too so
	 * update both.
	 */
	port->bonded = bonding;
	port->dual_link_port->bonded = bonding;

	return 0;
}

/**
 * tb_port_lane_bonding_enable() - Enable bonding on port
 * @port: port to enable
 *
 * Enable bonding by setting the link width of the port and the other
 * port in case of dual link port. Does not wait for the link to
 * actually reach the bonded state so caller needs to call
 * tb_port_wait_for_link_width() before enabling any paths through the
 * link to make sure the link is in expected state.
 *
 * Return: %0 in case of success and negative errno in case of error
 */
int tb_port_lane_bonding_enable(struct tb_port *port)
{
	int ret;

	/*
	 * Enable lane bonding for both links if not already enabled by
	 * for example the boot firmware.
	 */
	ret = tb_port_get_link_width(port);
	if (ret == 1) {
		ret = tb_port_set_link_width(port, 2);
		if (ret)
			goto err_lane0;
	}

	ret = tb_port_get_link_width(port->dual_link_port);
	if (ret == 1) {
		ret = tb_port_set_link_width(port->dual_link_port, 2);
		if (ret)
			goto err_lane0;
	}

	ret = tb_port_set_lane_bonding(port, true);
	if (ret)
		goto err_lane1;

	return 0;

err_lane1:
	tb_port_set_link_width(port->dual_link_port, 1);
err_lane0:
	tb_port_set_link_width(port, 1);
	return ret;
}

/**
 * tb_port_lane_bonding_disable() - Disable bonding on port
 * @port: port to disable
 *
 * Disable bonding by setting the link width of the port and the
 * other port in case of dual link port.
 */
void tb_port_lane_bonding_disable(struct tb_port *port)
{
	tb_port_set_lane_bonding(port, false);
	tb_port_set_link_width(port->dual_link_port, 1);
	tb_port_set_link_width(port, 1);
}

/**
 * tb_port_wait_for_link_width() - Wait until link reaches specific width
 * @port: Port to wait for
 * @width: Expected link width (%1 or %2)
 * @timeout_msec: Timeout in ms how long to wait
 *
 * Should be used after both ends of the link have been bonded (or
 * bonding has been disabled) to wait until the link actually reaches
 * the expected state. Returns %-ETIMEDOUT if the @width was not reached
 * within the given timeout, %0 if it did.
 */
int tb_port_wait_for_link_width(struct tb_port *port, int width,
				int timeout_msec)
{
	ktime_t timeout = ktime_add_ms(ktime_get(), timeout_msec);
	int ret;

	do {
		ret = tb_port_get_link_width(port);
		if (ret < 0) {
			/*
			 * Sometimes we get port locked error when
			 * polling the lanes so we can ignore it and
			 * retry.
			 */
			if (ret != -EACCES)
				return ret;
		} else if (ret == width) {
			return 0;
		}

		usleep_range(1000, 2000);
	} while (ktime_before(ktime_get(), timeout));

	return -ETIMEDOUT;
}

static int tb_port_do_update_credits(struct tb_port *port)
{
	u32 nfc_credits;
	int ret;

	ret = tb_port_read(port, &nfc_credits, TB_CFG_PORT, ADP_CS_4, 1);
	if (ret)
		return ret;

	if (nfc_credits != port->config.nfc_credits) {
		u32 total;

		total = (nfc_credits & ADP_CS_4_TOTAL_BUFFERS_MASK) >>
			ADP_CS_4_TOTAL_BUFFERS_SHIFT;

		tb_port_dbg(port, "total credits changed %u -> %u\n",
			    port->total_credits, total);

		port->config.nfc_credits = nfc_credits;
		port->total_credits = total;
	}

	return 0;
}

/**
 * tb_port_update_credits() - Re-read port total credits
 * @port: Port to update
 *
 * After the link is bonded (or bonding was disabled) the port total
 * credits may change, so this function needs to be called to re-read
 * the credits. Updates also the second lane adapter.
 */
int tb_port_update_credits(struct tb_port *port)
{
	int ret;

	ret = tb_port_do_update_credits(port);
	if (ret)
		return ret;
	return tb_port_do_update_credits(port->dual_link_port);
}

static int __tb_port_pm_secondary_set(struct tb_port *port, bool secondary)
{
	u32 phy;
	int ret;

	ret = tb_port_read(port, &phy, TB_CFG_PORT,
			   port->cap_phy + LANE_ADP_CS_1, 1);
	if (ret)
		return ret;

	if (secondary)
		phy |= LANE_ADP_CS_1_PMS;
	else
		phy &= ~LANE_ADP_CS_1_PMS;

	return tb_port_write(port, &phy, TB_CFG_PORT,
			     port->cap_phy + LANE_ADP_CS_1, 1);
}

static int tb_port_pm_secondary_enable(struct tb_port *port)
{
	return __tb_port_pm_secondary_set(port, true);
}

static int tb_port_pm_secondary_disable(struct tb_port *port)
{
	return __tb_port_pm_secondary_set(port, false);
}

/* Called for USB4 or Titan Ridge routers only */
static bool tb_port_clx_supported(struct tb_port *port, unsigned int clx_mask)
{
	u32 val, mask = 0;
	bool ret;

	/* Don't enable CLx in case of two single-lane links */
	if (!port->bonded && port->dual_link_port)
		return false;

	/* Don't enable CLx in case of inter-domain link */
	if (port->xdomain)
		return false;

	if (tb_switch_is_usb4(port->sw)) {
		if (!usb4_port_clx_supported(port))
			return false;
	} else if (!tb_lc_is_clx_supported(port)) {
		return false;
	}

	if (clx_mask & TB_CL1) {
		/* CL0s and CL1 are enabled and supported together */
		mask |= LANE_ADP_CS_0_CL0S_SUPPORT | LANE_ADP_CS_0_CL1_SUPPORT;
	}
	if (clx_mask & TB_CL2)
		mask |= LANE_ADP_CS_0_CL2_SUPPORT;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_phy + LANE_ADP_CS_0, 1);
	if (ret)
		return false;

	return !!(val & mask);
}

static int __tb_port_clx_set(struct tb_port *port, enum tb_clx clx, bool enable)
{
	u32 phy, mask;
	int ret;

	/* CL0s and CL1 are enabled and supported together */
	if (clx == TB_CL1)
		mask = LANE_ADP_CS_1_CL0S_ENABLE | LANE_ADP_CS_1_CL1_ENABLE;
	else
		/* For now we support only CL0s and CL1. Not CL2 */
		return -EOPNOTSUPP;

	ret = tb_port_read(port, &phy, TB_CFG_PORT,
			   port->cap_phy + LANE_ADP_CS_1, 1);
	if (ret)
		return ret;

	if (enable)
		phy |= mask;
	else
		phy &= ~mask;

	return tb_port_write(port, &phy, TB_CFG_PORT,
			     port->cap_phy + LANE_ADP_CS_1, 1);
}

static int tb_port_clx_disable(struct tb_port *port, enum tb_clx clx)
{
	return __tb_port_clx_set(port, clx, false);
}

static int tb_port_clx_enable(struct tb_port *port, enum tb_clx clx)
{
	return __tb_port_clx_set(port, clx, true);
}

/**
 * tb_port_is_clx_enabled() - Is given CL state enabled
 * @port: USB4 port to check
 * @clx_mask: Mask of CL states to check
 *
 * Returns true if any of the given CL states is enabled for @port.
 */
bool tb_port_is_clx_enabled(struct tb_port *port, unsigned int clx_mask)
{
	u32 val, mask = 0;
	int ret;

	if (!tb_port_clx_supported(port, clx_mask))
		return false;

	if (clx_mask & TB_CL1)
		mask |= LANE_ADP_CS_1_CL0S_ENABLE | LANE_ADP_CS_1_CL1_ENABLE;
	if (clx_mask & TB_CL2)
		mask |= LANE_ADP_CS_1_CL2_ENABLE;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_phy + LANE_ADP_CS_1, 1);
	if (ret)
		return false;

	return !!(val & mask);
}

static int tb_port_start_lane_initialization(struct tb_port *port)
{
	int ret;

	if (tb_switch_is_usb4(port->sw))
		return 0;

	ret = tb_lc_start_lane_initialization(port);
	return ret == -EINVAL ? 0 : ret;
}

/*
 * Returns true if the port had something (router, XDomain) connected
 * before suspend.
 */
static bool tb_port_resume(struct tb_port *port)
{
	bool has_remote = tb_port_has_remote(port);

	if (port->usb4) {
		usb4_port_device_resume(port->usb4);
	} else if (!has_remote) {
		/*
		 * For disconnected downstream lane adapters start lane
		 * initialization now so we detect future connects.
		 *
		 * For XDomain start the lane initialzation now so the
		 * link gets re-established.
		 *
		 * This is only needed for non-USB4 ports.
		 */
		if (!tb_is_upstream_port(port) || port->xdomain)
			tb_port_start_lane_initialization(port);
	}

	return has_remote || port->xdomain;
}

/**
 * tb_port_is_enabled() - Is the adapter port enabled
 * @port: Port to check
 */
bool tb_port_is_enabled(struct tb_port *port)
{
	switch (port->config.type) {
	case TB_TYPE_PCIE_UP:
	case TB_TYPE_PCIE_DOWN:
		return tb_pci_port_is_enabled(port);

	case TB_TYPE_DP_HDMI_IN:
	case TB_TYPE_DP_HDMI_OUT:
		return tb_dp_port_is_enabled(port);

	case TB_TYPE_USB3_UP:
	case TB_TYPE_USB3_DOWN:
		return tb_usb3_port_is_enabled(port);

	default:
		return false;
	}
}

/**
 * tb_usb3_port_is_enabled() - Is the USB3 adapter port enabled
 * @port: USB3 adapter port to check
 */
bool tb_usb3_port_is_enabled(struct tb_port *port)
{
	u32 data;

	if (tb_port_read(port, &data, TB_CFG_PORT,
			 port->cap_adap + ADP_USB3_CS_0, 1))
		return false;

	return !!(data & ADP_USB3_CS_0_PE);
}

/**
 * tb_usb3_port_enable() - Enable USB3 adapter port
 * @port: USB3 adapter port to enable
 * @enable: Enable/disable the USB3 adapter
 */
int tb_usb3_port_enable(struct tb_port *port, bool enable)
{
	u32 word = enable ? (ADP_USB3_CS_0_PE | ADP_USB3_CS_0_V)
			  : ADP_USB3_CS_0_V;

	if (!port->cap_adap)
		return -ENXIO;
	return tb_port_write(port, &word, TB_CFG_PORT,
			     port->cap_adap + ADP_USB3_CS_0, 1);
}

/**
 * tb_pci_port_is_enabled() - Is the PCIe adapter port enabled
 * @port: PCIe port to check
 */
bool tb_pci_port_is_enabled(struct tb_port *port)
{
	u32 data;

	if (tb_port_read(port, &data, TB_CFG_PORT,
			 port->cap_adap + ADP_PCIE_CS_0, 1))
		return false;

	return !!(data & ADP_PCIE_CS_0_PE);
}

/**
 * tb_pci_port_enable() - Enable PCIe adapter port
 * @port: PCIe port to enable
 * @enable: Enable/disable the PCIe adapter
 */
int tb_pci_port_enable(struct tb_port *port, bool enable)
{
	u32 word = enable ? ADP_PCIE_CS_0_PE : 0x0;
	if (!port->cap_adap)
		return -ENXIO;
	return tb_port_write(port, &word, TB_CFG_PORT,
			     port->cap_adap + ADP_PCIE_CS_0, 1);
}

/**
 * tb_dp_port_hpd_is_active() - Is HPD already active
 * @port: DP out port to check
 *
 * Checks if the DP OUT adapter port has HDP bit already set.
 */
int tb_dp_port_hpd_is_active(struct tb_port *port)
{
	u32 data;
	int ret;

	ret = tb_port_read(port, &data, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_2, 1);
	if (ret)
		return ret;

	return !!(data & ADP_DP_CS_2_HDP);
}

/**
 * tb_dp_port_hpd_clear() - Clear HPD from DP IN port
 * @port: Port to clear HPD
 *
 * If the DP IN port has HDP set, this function can be used to clear it.
 */
int tb_dp_port_hpd_clear(struct tb_port *port)
{
	u32 data;
	int ret;

	ret = tb_port_read(port, &data, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_3, 1);
	if (ret)
		return ret;

	data |= ADP_DP_CS_3_HDPC;
	return tb_port_write(port, &data, TB_CFG_PORT,
			     port->cap_adap + ADP_DP_CS_3, 1);
}

/**
 * tb_dp_port_set_hops() - Set video/aux Hop IDs for DP port
 * @port: DP IN/OUT port to set hops
 * @video: Video Hop ID
 * @aux_tx: AUX TX Hop ID
 * @aux_rx: AUX RX Hop ID
 *
 * Programs specified Hop IDs for DP IN/OUT port. Can be called for USB4
 * router DP adapters too but does not program the values as the fields
 * are read-only.
 */
int tb_dp_port_set_hops(struct tb_port *port, unsigned int video,
			unsigned int aux_tx, unsigned int aux_rx)
{
	u32 data[2];
	int ret;

	if (tb_switch_is_usb4(port->sw))
		return 0;

	ret = tb_port_read(port, data, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_0, ARRAY_SIZE(data));
	if (ret)
		return ret;

	data[0] &= ~ADP_DP_CS_0_VIDEO_HOPID_MASK;
	data[1] &= ~ADP_DP_CS_1_AUX_RX_HOPID_MASK;
	data[1] &= ~ADP_DP_CS_1_AUX_RX_HOPID_MASK;

	data[0] |= (video << ADP_DP_CS_0_VIDEO_HOPID_SHIFT) &
		ADP_DP_CS_0_VIDEO_HOPID_MASK;
	data[1] |= aux_tx & ADP_DP_CS_1_AUX_TX_HOPID_MASK;
	data[1] |= (aux_rx << ADP_DP_CS_1_AUX_RX_HOPID_SHIFT) &
		ADP_DP_CS_1_AUX_RX_HOPID_MASK;

	return tb_port_write(port, data, TB_CFG_PORT,
			     port->cap_adap + ADP_DP_CS_0, ARRAY_SIZE(data));
}

/**
 * tb_dp_port_is_enabled() - Is DP adapter port enabled
 * @port: DP adapter port to check
 */
bool tb_dp_port_is_enabled(struct tb_port *port)
{
	u32 data[2];

	if (tb_port_read(port, data, TB_CFG_PORT, port->cap_adap + ADP_DP_CS_0,
			 ARRAY_SIZE(data)))
		return false;

	return !!(data[0] & (ADP_DP_CS_0_VE | ADP_DP_CS_0_AE));
}

/**
 * tb_dp_port_enable() - Enables/disables DP paths of a port
 * @port: DP IN/OUT port
 * @enable: Enable/disable DP path
 *
 * Once Hop IDs are programmed DP paths can be enabled or disabled by
 * calling this function.
 */
int tb_dp_port_enable(struct tb_port *port, bool enable)
{
	u32 data[2];
	int ret;

	ret = tb_port_read(port, data, TB_CFG_PORT,
			  port->cap_adap + ADP_DP_CS_0, ARRAY_SIZE(data));
	if (ret)
		return ret;

	if (enable)
		data[0] |= ADP_DP_CS_0_VE | ADP_DP_CS_0_AE;
	else
		data[0] &= ~(ADP_DP_CS_0_VE | ADP_DP_CS_0_AE);

	return tb_port_write(port, data, TB_CFG_PORT,
			     port->cap_adap + ADP_DP_CS_0, ARRAY_SIZE(data));
}

/* switch utility functions */

static const char *tb_switch_generation_name(const struct tb_switch *sw)
{
	switch (sw->generation) {
	case 1:
		return "Thunderbolt 1";
	case 2:
		return "Thunderbolt 2";
	case 3:
		return "Thunderbolt 3";
	case 4:
		return "USB4";
	default:
		return "Unknown";
	}
}

static void tb_dump_switch(const struct tb *tb, const struct tb_switch *sw)
{
	const struct tb_regs_switch_header *regs = &sw->config;

	tb_dbg(tb, " %s Switch: %x:%x (Revision: %d, TB Version: %d)\n",
	       tb_switch_generation_name(sw), regs->vendor_id, regs->device_id,
	       regs->revision, regs->thunderbolt_version);
	tb_dbg(tb, "  Max Port Number: %d\n", regs->max_port_number);
	tb_dbg(tb, "  Config:\n");
	tb_dbg(tb,
		"   Upstream Port Number: %d Depth: %d Route String: %#llx Enabled: %d, PlugEventsDelay: %dms\n",
	       regs->upstream_port_number, regs->depth,
	       (((u64) regs->route_hi) << 32) | regs->route_lo,
	       regs->enabled, regs->plug_events_delay);
	tb_dbg(tb, "   unknown1: %#x unknown4: %#x\n",
	       regs->__unknown1, regs->__unknown4);
}

/**
 * tb_switch_reset() - reconfigure route, enable and send TB_CFG_PKG_RESET
 * @sw: Switch to reset
 *
 * Return: Returns 0 on success or an error code on failure.
 */
int tb_switch_reset(struct tb_switch *sw)
{
	struct tb_cfg_result res;

	if (sw->generation > 1)
		return 0;

	tb_sw_dbg(sw, "resetting switch\n");

	res.err = tb_sw_write(sw, ((u32 *) &sw->config) + 2,
			      TB_CFG_SWITCH, 2, 2);
	if (res.err)
		return res.err;
	res = tb_cfg_reset(sw->tb->ctl, tb_route(sw));
	if (res.err > 0)
		return -EIO;
	return res.err;
}

/**
 * tb_switch_wait_for_bit() - Wait for specified value of bits in offset
 * @sw: Router to read the offset value from
 * @offset: Offset in the router config space to read from
 * @bit: Bit mask in the offset to wait for
 * @value: Value of the bits to wait for
 * @timeout_msec: Timeout in ms how long to wait
 *
 * Wait till the specified bits in specified offset reach specified value.
 * Returns %0 in case of success, %-ETIMEDOUT if the @value was not reached
 * within the given timeout or a negative errno in case of failure.
 */
int tb_switch_wait_for_bit(struct tb_switch *sw, u32 offset, u32 bit,
			   u32 value, int timeout_msec)
{
	ktime_t timeout = ktime_add_ms(ktime_get(), timeout_msec);

	do {
		u32 val;
		int ret;

		ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, offset, 1);
		if (ret)
			return ret;

		if ((val & bit) == value)
			return 0;

		usleep_range(50, 100);
	} while (ktime_before(ktime_get(), timeout));

	return -ETIMEDOUT;
}

/*
 * tb_plug_events_active() - enable/disable plug events on a switch
 *
 * Also configures a sane plug_events_delay of 255ms.
 *
 * Return: Returns 0 on success or an error code on failure.
 */
static int tb_plug_events_active(struct tb_switch *sw, bool active)
{
	u32 data;
	int res;

	if (tb_switch_is_icm(sw) || tb_switch_is_usb4(sw))
		return 0;

	sw->config.plug_events_delay = 0xff;
	res = tb_sw_write(sw, ((u32 *) &sw->config) + 4, TB_CFG_SWITCH, 4, 1);
	if (res)
		return res;

	res = tb_sw_read(sw, &data, TB_CFG_SWITCH, sw->cap_plug_events + 1, 1);
	if (res)
		return res;

	if (active) {
		data = data & 0xFFFFFF83;
		switch (sw->config.device_id) {
		case PCI_DEVICE_ID_INTEL_LIGHT_RIDGE:
		case PCI_DEVICE_ID_INTEL_EAGLE_RIDGE:
		case PCI_DEVICE_ID_INTEL_PORT_RIDGE:
			break;
		default:
			/*
			 * Skip Alpine Ridge, it needs to have vendor
			 * specific USB hotplug event enabled for the
			 * internal xHCI to work.
			 */
			if (!tb_switch_is_alpine_ridge(sw))
				data |= TB_PLUG_EVENTS_USB_DISABLE;
		}
	} else {
		data = data | 0x7c;
	}
	return tb_sw_write(sw, &data, TB_CFG_SWITCH,
			   sw->cap_plug_events + 1, 1);
}

static ssize_t authorized_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);

	return sysfs_emit(buf, "%u\n", sw->authorized);
}

static int disapprove_switch(struct device *dev, void *not_used)
{
	char *envp[] = { "AUTHORIZED=0", NULL };
	struct tb_switch *sw;

	sw = tb_to_switch(dev);
	if (sw && sw->authorized) {
		int ret;

		/* First children */
		ret = device_for_each_child_reverse(&sw->dev, NULL, disapprove_switch);
		if (ret)
			return ret;

		ret = tb_domain_disapprove_switch(sw->tb, sw);
		if (ret)
			return ret;

		sw->authorized = 0;
		kobject_uevent_env(&sw->dev.kobj, KOBJ_CHANGE, envp);
	}

	return 0;
}

static int tb_switch_set_authorized(struct tb_switch *sw, unsigned int val)
{
	char envp_string[13];
	int ret = -EINVAL;
	char *envp[] = { envp_string, NULL };

	if (!mutex_trylock(&sw->tb->lock))
		return restart_syscall();

	if (!!sw->authorized == !!val)
		goto unlock;

	switch (val) {
	/* Disapprove switch */
	case 0:
		if (tb_route(sw)) {
			ret = disapprove_switch(&sw->dev, NULL);
			goto unlock;
		}
		break;

	/* Approve switch */
	case 1:
		if (sw->key)
			ret = tb_domain_approve_switch_key(sw->tb, sw);
		else
			ret = tb_domain_approve_switch(sw->tb, sw);
		break;

	/* Challenge switch */
	case 2:
		if (sw->key)
			ret = tb_domain_challenge_switch_key(sw->tb, sw);
		break;

	default:
		break;
	}

	if (!ret) {
		sw->authorized = val;
		/*
		 * Notify status change to the userspace, informing the new
		 * value of /sys/bus/thunderbolt/devices/.../authorized.
		 */
		sprintf(envp_string, "AUTHORIZED=%u", sw->authorized);
		kobject_uevent_env(&sw->dev.kobj, KOBJ_CHANGE, envp);
	}

unlock:
	mutex_unlock(&sw->tb->lock);
	return ret;
}

static ssize_t authorized_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct tb_switch *sw = tb_to_switch(dev);
	unsigned int val;
	ssize_t ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;
	if (val > 2)
		return -EINVAL;

	pm_runtime_get_sync(&sw->dev);
	ret = tb_switch_set_authorized(sw, val);
	pm_runtime_mark_last_busy(&sw->dev);
	pm_runtime_put_autosuspend(&sw->dev);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(authorized);

static ssize_t boot_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);

	return sysfs_emit(buf, "%u\n", sw->boot);
}
static DEVICE_ATTR_RO(boot);

static ssize_t device_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);

	return sysfs_emit(buf, "%#x\n", sw->device);
}
static DEVICE_ATTR_RO(device);

static ssize_t
device_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);

	return sysfs_emit(buf, "%s\n", sw->device_name ?: "");
}
static DEVICE_ATTR_RO(device_name);

static ssize_t
generation_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);

	return sysfs_emit(buf, "%u\n", sw->generation);
}
static DEVICE_ATTR_RO(generation);

static ssize_t key_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);
	ssize_t ret;

	if (!mutex_trylock(&sw->tb->lock))
		return restart_syscall();

	if (sw->key)
		ret = sysfs_emit(buf, "%*phN\n", TB_SWITCH_KEY_SIZE, sw->key);
	else
		ret = sysfs_emit(buf, "\n");

	mutex_unlock(&sw->tb->lock);
	return ret;
}

static ssize_t key_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct tb_switch *sw = tb_to_switch(dev);
	u8 key[TB_SWITCH_KEY_SIZE];
	ssize_t ret = count;
	bool clear = false;

	if (!strcmp(buf, "\n"))
		clear = true;
	else if (hex2bin(key, buf, sizeof(key)))
		return -EINVAL;

	if (!mutex_trylock(&sw->tb->lock))
		return restart_syscall();

	if (sw->authorized) {
		ret = -EBUSY;
	} else {
		kfree(sw->key);
		if (clear) {
			sw->key = NULL;
		} else {
			sw->key = kmemdup(key, sizeof(key), GFP_KERNEL);
			if (!sw->key)
				ret = -ENOMEM;
		}
	}

	mutex_unlock(&sw->tb->lock);
	return ret;
}
static DEVICE_ATTR(key, 0600, key_show, key_store);

static ssize_t speed_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);

	return sysfs_emit(buf, "%u.0 Gb/s\n", sw->link_speed);
}

/*
 * Currently all lanes must run at the same speed but we expose here
 * both directions to allow possible asymmetric links in the future.
 */
static DEVICE_ATTR(rx_speed, 0444, speed_show, NULL);
static DEVICE_ATTR(tx_speed, 0444, speed_show, NULL);

static ssize_t lanes_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);

	return sysfs_emit(buf, "%u\n", sw->link_width);
}

/*
 * Currently link has same amount of lanes both directions (1 or 2) but
 * expose them separately to allow possible asymmetric links in the future.
 */
static DEVICE_ATTR(rx_lanes, 0444, lanes_show, NULL);
static DEVICE_ATTR(tx_lanes, 0444, lanes_show, NULL);

static ssize_t nvm_authenticate_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);
	u32 status;

	nvm_get_auth_status(sw, &status);
	return sysfs_emit(buf, "%#x\n", status);
}

static ssize_t nvm_authenticate_sysfs(struct device *dev, const char *buf,
				      bool disconnect)
{
	struct tb_switch *sw = tb_to_switch(dev);
	int val, ret;

	pm_runtime_get_sync(&sw->dev);

	if (!mutex_trylock(&sw->tb->lock)) {
		ret = restart_syscall();
		goto exit_rpm;
	}

	if (sw->no_nvm_upgrade) {
		ret = -EOPNOTSUPP;
		goto exit_unlock;
	}

	/* If NVMem devices are not yet added */
	if (!sw->nvm) {
		ret = -EAGAIN;
		goto exit_unlock;
	}

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		goto exit_unlock;

	/* Always clear the authentication status */
	nvm_clear_auth_status(sw);

	if (val > 0) {
		if (val == AUTHENTICATE_ONLY) {
			if (disconnect)
				ret = -EINVAL;
			else
				ret = nvm_authenticate(sw, true);
		} else {
			if (!sw->nvm->flushed) {
				if (!sw->nvm->buf) {
					ret = -EINVAL;
					goto exit_unlock;
				}

				ret = nvm_validate_and_write(sw);
				if (ret || val == WRITE_ONLY)
					goto exit_unlock;
			}
			if (val == WRITE_AND_AUTHENTICATE) {
				if (disconnect)
					ret = tb_lc_force_power(sw);
				else
					ret = nvm_authenticate(sw, false);
			}
		}
	}

exit_unlock:
	mutex_unlock(&sw->tb->lock);
exit_rpm:
	pm_runtime_mark_last_busy(&sw->dev);
	pm_runtime_put_autosuspend(&sw->dev);

	return ret;
}

static ssize_t nvm_authenticate_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = nvm_authenticate_sysfs(dev, buf, false);
	if (ret)
		return ret;
	return count;
}
static DEVICE_ATTR_RW(nvm_authenticate);

static ssize_t nvm_authenticate_on_disconnect_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return nvm_authenticate_show(dev, attr, buf);
}

static ssize_t nvm_authenticate_on_disconnect_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;

	ret = nvm_authenticate_sysfs(dev, buf, true);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(nvm_authenticate_on_disconnect);

static ssize_t nvm_version_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);
	int ret;

	if (!mutex_trylock(&sw->tb->lock))
		return restart_syscall();

	if (sw->safe_mode)
		ret = -ENODATA;
	else if (!sw->nvm)
		ret = -EAGAIN;
	else
		ret = sysfs_emit(buf, "%x.%x\n", sw->nvm->major, sw->nvm->minor);

	mutex_unlock(&sw->tb->lock);

	return ret;
}
static DEVICE_ATTR_RO(nvm_version);

static ssize_t vendor_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);

	return sysfs_emit(buf, "%#x\n", sw->vendor);
}
static DEVICE_ATTR_RO(vendor);

static ssize_t
vendor_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);

	return sysfs_emit(buf, "%s\n", sw->vendor_name ?: "");
}
static DEVICE_ATTR_RO(vendor_name);

static ssize_t unique_id_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct tb_switch *sw = tb_to_switch(dev);

	return sysfs_emit(buf, "%pUb\n", sw->uuid);
}
static DEVICE_ATTR_RO(unique_id);

static struct attribute *switch_attrs[] = {
	&dev_attr_authorized.attr,
	&dev_attr_boot.attr,
	&dev_attr_device.attr,
	&dev_attr_device_name.attr,
	&dev_attr_generation.attr,
	&dev_attr_key.attr,
	&dev_attr_nvm_authenticate.attr,
	&dev_attr_nvm_authenticate_on_disconnect.attr,
	&dev_attr_nvm_version.attr,
	&dev_attr_rx_speed.attr,
	&dev_attr_rx_lanes.attr,
	&dev_attr_tx_speed.attr,
	&dev_attr_tx_lanes.attr,
	&dev_attr_vendor.attr,
	&dev_attr_vendor_name.attr,
	&dev_attr_unique_id.attr,
	NULL,
};

static umode_t switch_attr_is_visible(struct kobject *kobj,
				      struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct tb_switch *sw = tb_to_switch(dev);

	if (attr == &dev_attr_authorized.attr) {
		if (sw->tb->security_level == TB_SECURITY_NOPCIE ||
		    sw->tb->security_level == TB_SECURITY_DPONLY)
			return 0;
	} else if (attr == &dev_attr_device.attr) {
		if (!sw->device)
			return 0;
	} else if (attr == &dev_attr_device_name.attr) {
		if (!sw->device_name)
			return 0;
	} else if (attr == &dev_attr_vendor.attr)  {
		if (!sw->vendor)
			return 0;
	} else if (attr == &dev_attr_vendor_name.attr)  {
		if (!sw->vendor_name)
			return 0;
	} else if (attr == &dev_attr_key.attr) {
		if (tb_route(sw) &&
		    sw->tb->security_level == TB_SECURITY_SECURE &&
		    sw->security_level == TB_SECURITY_SECURE)
			return attr->mode;
		return 0;
	} else if (attr == &dev_attr_rx_speed.attr ||
		   attr == &dev_attr_rx_lanes.attr ||
		   attr == &dev_attr_tx_speed.attr ||
		   attr == &dev_attr_tx_lanes.attr) {
		if (tb_route(sw))
			return attr->mode;
		return 0;
	} else if (attr == &dev_attr_nvm_authenticate.attr) {
		if (nvm_upgradeable(sw))
			return attr->mode;
		return 0;
	} else if (attr == &dev_attr_nvm_version.attr) {
		if (nvm_readable(sw))
			return attr->mode;
		return 0;
	} else if (attr == &dev_attr_boot.attr) {
		if (tb_route(sw))
			return attr->mode;
		return 0;
	} else if (attr == &dev_attr_nvm_authenticate_on_disconnect.attr) {
		if (sw->quirks & QUIRK_FORCE_POWER_LINK_CONTROLLER)
			return attr->mode;
		return 0;
	}

	return sw->safe_mode ? 0 : attr->mode;
}

static const struct attribute_group switch_group = {
	.is_visible = switch_attr_is_visible,
	.attrs = switch_attrs,
};

static const struct attribute_group *switch_groups[] = {
	&switch_group,
	NULL,
};

static void tb_switch_release(struct device *dev)
{
	struct tb_switch *sw = tb_to_switch(dev);
	struct tb_port *port;

	dma_port_free(sw->dma_port);

	tb_switch_for_each_port(sw, port) {
		ida_destroy(&port->in_hopids);
		ida_destroy(&port->out_hopids);
	}

	kfree(sw->uuid);
	kfree(sw->device_name);
	kfree(sw->vendor_name);
	kfree(sw->ports);
	kfree(sw->drom);
	kfree(sw->key);
	kfree(sw);
}

static int tb_switch_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct tb_switch *sw = tb_to_switch(dev);
	const char *type;

	if (sw->config.thunderbolt_version == USB4_VERSION_1_0) {
		if (add_uevent_var(env, "USB4_VERSION=1.0"))
			return -ENOMEM;
	}

	if (!tb_route(sw)) {
		type = "host";
	} else {
		const struct tb_port *port;
		bool hub = false;

		/* Device is hub if it has any downstream ports */
		tb_switch_for_each_port(sw, port) {
			if (!port->disabled && !tb_is_upstream_port(port) &&
			     tb_port_is_null(port)) {
				hub = true;
				break;
			}
		}

		type = hub ? "hub" : "device";
	}

	if (add_uevent_var(env, "USB4_TYPE=%s", type))
		return -ENOMEM;
	return 0;
}

/*
 * Currently only need to provide the callbacks. Everything else is handled
 * in the connection manager.
 */
static int __maybe_unused tb_switch_runtime_suspend(struct device *dev)
{
	struct tb_switch *sw = tb_to_switch(dev);
	const struct tb_cm_ops *cm_ops = sw->tb->cm_ops;

	if (cm_ops->runtime_suspend_switch)
		return cm_ops->runtime_suspend_switch(sw);

	return 0;
}

static int __maybe_unused tb_switch_runtime_resume(struct device *dev)
{
	struct tb_switch *sw = tb_to_switch(dev);
	const struct tb_cm_ops *cm_ops = sw->tb->cm_ops;

	if (cm_ops->runtime_resume_switch)
		return cm_ops->runtime_resume_switch(sw);
	return 0;
}

static const struct dev_pm_ops tb_switch_pm_ops = {
	SET_RUNTIME_PM_OPS(tb_switch_runtime_suspend, tb_switch_runtime_resume,
			   NULL)
};

struct device_type tb_switch_type = {
	.name = "thunderbolt_device",
	.release = tb_switch_release,
	.uevent = tb_switch_uevent,
	.pm = &tb_switch_pm_ops,
};

static int tb_switch_get_generation(struct tb_switch *sw)
{
	switch (sw->config.device_id) {
	case PCI_DEVICE_ID_INTEL_LIGHT_RIDGE:
	case PCI_DEVICE_ID_INTEL_EAGLE_RIDGE:
	case PCI_DEVICE_ID_INTEL_LIGHT_PEAK:
	case PCI_DEVICE_ID_INTEL_CACTUS_RIDGE_2C:
	case PCI_DEVICE_ID_INTEL_CACTUS_RIDGE_4C:
	case PCI_DEVICE_ID_INTEL_PORT_RIDGE:
	case PCI_DEVICE_ID_INTEL_REDWOOD_RIDGE_2C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_REDWOOD_RIDGE_4C_BRIDGE:
		return 1;

	case PCI_DEVICE_ID_INTEL_WIN_RIDGE_2C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_2C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_FALCON_RIDGE_4C_BRIDGE:
		return 2;

	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_BRIDGE:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_2C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_4C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_2C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_4C_BRIDGE:
	case PCI_DEVICE_ID_INTEL_TITAN_RIDGE_DD_BRIDGE:
	case PCI_DEVICE_ID_INTEL_ICL_NHI0:
	case PCI_DEVICE_ID_INTEL_ICL_NHI1:
		return 3;

	default:
		if (tb_switch_is_usb4(sw))
			return 4;

		/*
		 * For unknown switches assume generation to be 1 to be
		 * on the safe side.
		 */
		tb_sw_warn(sw, "unsupported switch device id %#x\n",
			   sw->config.device_id);
		return 1;
	}
}

static bool tb_switch_exceeds_max_depth(const struct tb_switch *sw, int depth)
{
	int max_depth;

	if (tb_switch_is_usb4(sw) ||
	    (sw->tb->root_switch && tb_switch_is_usb4(sw->tb->root_switch)))
		max_depth = USB4_SWITCH_MAX_DEPTH;
	else
		max_depth = TB_SWITCH_MAX_DEPTH;

	return depth > max_depth;
}

/**
 * tb_switch_alloc() - allocate a switch
 * @tb: Pointer to the owning domain
 * @parent: Parent device for this switch
 * @route: Route string for this switch
 *
 * Allocates and initializes a switch. Will not upload configuration to
 * the switch. For that you need to call tb_switch_configure()
 * separately. The returned switch should be released by calling
 * tb_switch_put().
 *
 * Return: Pointer to the allocated switch or ERR_PTR() in case of
 * failure.
 */
struct tb_switch *tb_switch_alloc(struct tb *tb, struct device *parent,
				  u64 route)
{
	struct tb_switch *sw;
	int upstream_port;
	int i, ret, depth;

	/* Unlock the downstream port so we can access the switch below */
	if (route) {
		struct tb_switch *parent_sw = tb_to_switch(parent);
		struct tb_port *down;

		down = tb_port_at(route, parent_sw);
		tb_port_unlock(down);
	}

	depth = tb_route_length(route);

	upstream_port = tb_cfg_get_upstream_port(tb->ctl, route);
	if (upstream_port < 0)
		return ERR_PTR(upstream_port);

	sw = kzalloc(sizeof(*sw), GFP_KERNEL);
	if (!sw)
		return ERR_PTR(-ENOMEM);

	sw->tb = tb;
	ret = tb_cfg_read(tb->ctl, &sw->config, route, 0, TB_CFG_SWITCH, 0, 5);
	if (ret)
		goto err_free_sw_ports;

	sw->generation = tb_switch_get_generation(sw);

	tb_dbg(tb, "current switch config:\n");
	tb_dump_switch(tb, sw);

	/* configure switch */
	sw->config.upstream_port_number = upstream_port;
	sw->config.depth = depth;
	sw->config.route_hi = upper_32_bits(route);
	sw->config.route_lo = lower_32_bits(route);
	sw->config.enabled = 0;

	/* Make sure we do not exceed maximum topology limit */
	if (tb_switch_exceeds_max_depth(sw, depth)) {
		ret = -EADDRNOTAVAIL;
		goto err_free_sw_ports;
	}

	/* initialize ports */
	sw->ports = kcalloc(sw->config.max_port_number + 1, sizeof(*sw->ports),
				GFP_KERNEL);
	if (!sw->ports) {
		ret = -ENOMEM;
		goto err_free_sw_ports;
	}

	for (i = 0; i <= sw->config.max_port_number; i++) {
		/* minimum setup for tb_find_cap and tb_drom_read to work */
		sw->ports[i].sw = sw;
		sw->ports[i].port = i;

		/* Control port does not need HopID allocation */
		if (i) {
			ida_init(&sw->ports[i].in_hopids);
			ida_init(&sw->ports[i].out_hopids);
		}
	}

	ret = tb_switch_find_vse_cap(sw, TB_VSE_CAP_PLUG_EVENTS);
	if (ret > 0)
		sw->cap_plug_events = ret;

	ret = tb_switch_find_vse_cap(sw, TB_VSE_CAP_TIME2);
	if (ret > 0)
		sw->cap_vsec_tmu = ret;

	ret = tb_switch_find_vse_cap(sw, TB_VSE_CAP_LINK_CONTROLLER);
	if (ret > 0)
		sw->cap_lc = ret;

	ret = tb_switch_find_vse_cap(sw, TB_VSE_CAP_CP_LP);
	if (ret > 0)
		sw->cap_lp = ret;

	/* Root switch is always authorized */
	if (!route)
		sw->authorized = true;

	device_initialize(&sw->dev);
	sw->dev.parent = parent;
	sw->dev.bus = &tb_bus_type;
	sw->dev.type = &tb_switch_type;
	sw->dev.groups = switch_groups;
	dev_set_name(&sw->dev, "%u-%llx", tb->index, tb_route(sw));

	return sw;

err_free_sw_ports:
	kfree(sw->ports);
	kfree(sw);

	return ERR_PTR(ret);
}

/**
 * tb_switch_alloc_safe_mode() - allocate a switch that is in safe mode
 * @tb: Pointer to the owning domain
 * @parent: Parent device for this switch
 * @route: Route string for this switch
 *
 * This creates a switch in safe mode. This means the switch pretty much
 * lacks all capabilities except DMA configuration port before it is
 * flashed with a valid NVM firmware.
 *
 * The returned switch must be released by calling tb_switch_put().
 *
 * Return: Pointer to the allocated switch or ERR_PTR() in case of failure
 */
struct tb_switch *
tb_switch_alloc_safe_mode(struct tb *tb, struct device *parent, u64 route)
{
	struct tb_switch *sw;

	sw = kzalloc(sizeof(*sw), GFP_KERNEL);
	if (!sw)
		return ERR_PTR(-ENOMEM);

	sw->tb = tb;
	sw->config.depth = tb_route_length(route);
	sw->config.route_hi = upper_32_bits(route);
	sw->config.route_lo = lower_32_bits(route);
	sw->safe_mode = true;

	device_initialize(&sw->dev);
	sw->dev.parent = parent;
	sw->dev.bus = &tb_bus_type;
	sw->dev.type = &tb_switch_type;
	sw->dev.groups = switch_groups;
	dev_set_name(&sw->dev, "%u-%llx", tb->index, tb_route(sw));

	return sw;
}

/**
 * tb_switch_configure() - Uploads configuration to the switch
 * @sw: Switch to configure
 *
 * Call this function before the switch is added to the system. It will
 * upload configuration to the switch and makes it available for the
 * connection manager to use. Can be called to the switch again after
 * resume from low power states to re-initialize it.
 *
 * Return: %0 in case of success and negative errno in case of failure
 */
int tb_switch_configure(struct tb_switch *sw)
{
	struct tb *tb = sw->tb;
	u64 route;
	int ret;

	route = tb_route(sw);

	tb_dbg(tb, "%s Switch at %#llx (depth: %d, up port: %d)\n",
	       sw->config.enabled ? "restoring" : "initializing", route,
	       tb_route_length(route), sw->config.upstream_port_number);

	sw->config.enabled = 1;

	if (tb_switch_is_usb4(sw)) {
		/*
		 * For USB4 devices, we need to program the CM version
		 * accordingly so that it knows to expose all the
		 * additional capabilities.
		 */
		sw->config.cmuv = USB4_VERSION_1_0;
		sw->config.plug_events_delay = 0xa;

		/* Enumerate the switch */
		ret = tb_sw_write(sw, (u32 *)&sw->config + 1, TB_CFG_SWITCH,
				  ROUTER_CS_1, 4);
		if (ret)
			return ret;

		ret = usb4_switch_setup(sw);
	} else {
		if (sw->config.vendor_id != PCI_VENDOR_ID_INTEL)
			tb_sw_warn(sw, "unknown switch vendor id %#x\n",
				   sw->config.vendor_id);

		if (!sw->cap_plug_events) {
			tb_sw_warn(sw, "cannot find TB_VSE_CAP_PLUG_EVENTS aborting\n");
			return -ENODEV;
		}

		/* Enumerate the switch */
		ret = tb_sw_write(sw, (u32 *)&sw->config + 1, TB_CFG_SWITCH,
				  ROUTER_CS_1, 3);
	}
	if (ret)
		return ret;

	return tb_plug_events_active(sw, true);
}

static int tb_switch_set_uuid(struct tb_switch *sw)
{
	bool uid = false;
	u32 uuid[4];
	int ret;

	if (sw->uuid)
		return 0;

	if (tb_switch_is_usb4(sw)) {
		ret = usb4_switch_read_uid(sw, &sw->uid);
		if (ret)
			return ret;
		uid = true;
	} else {
		/*
		 * The newer controllers include fused UUID as part of
		 * link controller specific registers
		 */
		ret = tb_lc_read_uuid(sw, uuid);
		if (ret) {
			if (ret != -EINVAL)
				return ret;
			uid = true;
		}
	}

	if (uid) {
		/*
		 * ICM generates UUID based on UID and fills the upper
		 * two words with ones. This is not strictly following
		 * UUID format but we want to be compatible with it so
		 * we do the same here.
		 */
		uuid[0] = sw->uid & 0xffffffff;
		uuid[1] = (sw->uid >> 32) & 0xffffffff;
		uuid[2] = 0xffffffff;
		uuid[3] = 0xffffffff;
	}

	sw->uuid = kmemdup(uuid, sizeof(uuid), GFP_KERNEL);
	if (!sw->uuid)
		return -ENOMEM;
	return 0;
}

static int tb_switch_add_dma_port(struct tb_switch *sw)
{
	u32 status;
	int ret;

	switch (sw->generation) {
	case 2:
		/* Only root switch can be upgraded */
		if (tb_route(sw))
			return 0;

		fallthrough;
	case 3:
	case 4:
		ret = tb_switch_set_uuid(sw);
		if (ret)
			return ret;
		break;

	default:
		/*
		 * DMA port is the only thing available when the switch
		 * is in safe mode.
		 */
		if (!sw->safe_mode)
			return 0;
		break;
	}

	if (sw->no_nvm_upgrade)
		return 0;

	if (tb_switch_is_usb4(sw)) {
		ret = usb4_switch_nvm_authenticate_status(sw, &status);
		if (ret)
			return ret;

		if (status) {
			tb_sw_info(sw, "switch flash authentication failed\n");
			nvm_set_auth_status(sw, status);
		}

		return 0;
	}

	/* Root switch DMA port requires running firmware */
	if (!tb_route(sw) && !tb_switch_is_icm(sw))
		return 0;

	sw->dma_port = dma_port_alloc(sw);
	if (!sw->dma_port)
		return 0;

	/*
	 * If there is status already set then authentication failed
	 * when the dma_port_flash_update_auth() returned. Power cycling
	 * is not needed (it was done already) so only thing we do here
	 * is to unblock runtime PM of the root port.
	 */
	nvm_get_auth_status(sw, &status);
	if (status) {
		if (!tb_route(sw))
			nvm_authenticate_complete_dma_port(sw);
		return 0;
	}

	/*
	 * Check status of the previous flash authentication. If there
	 * is one we need to power cycle the switch in any case to make
	 * it functional again.
	 */
	ret = dma_port_flash_update_auth_status(sw->dma_port, &status);
	if (ret <= 0)
		return ret;

	/* Now we can allow root port to suspend again */
	if (!tb_route(sw))
		nvm_authenticate_complete_dma_port(sw);

	if (status) {
		tb_sw_info(sw, "switch flash authentication failed\n");
		nvm_set_auth_status(sw, status);
	}

	tb_sw_info(sw, "power cycling the switch now\n");
	dma_port_power_cycle(sw->dma_port);

	/*
	 * We return error here which causes the switch adding failure.
	 * It should appear back after power cycle is complete.
	 */
	return -ESHUTDOWN;
}

static void tb_switch_default_link_ports(struct tb_switch *sw)
{
	int i;

	for (i = 1; i <= sw->config.max_port_number; i++) {
		struct tb_port *port = &sw->ports[i];
		struct tb_port *subordinate;

		if (!tb_port_is_null(port))
			continue;

		/* Check for the subordinate port */
		if (i == sw->config.max_port_number ||
		    !tb_port_is_null(&sw->ports[i + 1]))
			continue;

		/* Link them if not already done so (by DROM) */
		subordinate = &sw->ports[i + 1];
		if (!port->dual_link_port && !subordinate->dual_link_port) {
			port->link_nr = 0;
			port->dual_link_port = subordinate;
			subordinate->link_nr = 1;
			subordinate->dual_link_port = port;

			tb_sw_dbg(sw, "linked ports %d <-> %d\n",
				  port->port, subordinate->port);
		}
	}
}

static bool tb_switch_lane_bonding_possible(struct tb_switch *sw)
{
	const struct tb_port *up = tb_upstream_port(sw);

	if (!up->dual_link_port || !up->dual_link_port->remote)
		return false;

	if (tb_switch_is_usb4(sw))
		return usb4_switch_lane_bonding_possible(sw);
	return tb_lc_lane_bonding_possible(sw);
}

static int tb_switch_update_link_attributes(struct tb_switch *sw)
{
	struct tb_port *up;
	bool change = false;
	int ret;

	if (!tb_route(sw) || tb_switch_is_icm(sw))
		return 0;

	up = tb_upstream_port(sw);

	ret = tb_port_get_link_speed(up);
	if (ret < 0)
		return ret;
	if (sw->link_speed != ret)
		change = true;
	sw->link_speed = ret;

	ret = tb_port_get_link_width(up);
	if (ret < 0)
		return ret;
	if (sw->link_width != ret)
		change = true;
	sw->link_width = ret;

	/* Notify userspace that there is possible link attribute change */
	if (device_is_registered(&sw->dev) && change)
		kobject_uevent(&sw->dev.kobj, KOBJ_CHANGE);

	return 0;
}

/**
 * tb_switch_lane_bonding_enable() - Enable lane bonding
 * @sw: Switch to enable lane bonding
 *
 * Connection manager can call this function to enable lane bonding of a
 * switch. If conditions are correct and both switches support the feature,
 * lanes are bonded. It is safe to call this to any switch.
 */
int tb_switch_lane_bonding_enable(struct tb_switch *sw)
{
	struct tb_switch *parent = tb_to_switch(sw->dev.parent);
	struct tb_port *up, *down;
	u64 route = tb_route(sw);
	int ret;

	if (!route)
		return 0;

	if (!tb_switch_lane_bonding_possible(sw))
		return 0;

	up = tb_upstream_port(sw);
	down = tb_port_at(route, parent);

	if (!tb_port_is_width_supported(up, 2) ||
	    !tb_port_is_width_supported(down, 2))
		return 0;

	/*
	 * Both lanes need to be in CL0. Here we assume lane 0 already be in
	 * CL0 and check just for lane 1.
	 */
	if (tb_wait_for_port(down->dual_link_port, false) <= 0)
		return -ENOTCONN;

	ret = tb_port_lane_bonding_enable(up);
	if (ret) {
		tb_port_warn(up, "failed to enable lane bonding\n");
		return ret;
	}

	ret = tb_port_lane_bonding_enable(down);
	if (ret) {
		tb_port_warn(down, "failed to enable lane bonding\n");
		tb_port_lane_bonding_disable(up);
		return ret;
	}

	ret = tb_port_wait_for_link_width(down, 2, 100);
	if (ret) {
		tb_port_warn(down, "timeout enabling lane bonding\n");
		return ret;
	}

	tb_port_update_credits(down);
	tb_port_update_credits(up);
	tb_switch_update_link_attributes(sw);

	tb_sw_dbg(sw, "lane bonding enabled\n");
	return ret;
}

/**
 * tb_switch_lane_bonding_disable() - Disable lane bonding
 * @sw: Switch whose lane bonding to disable
 *
 * Disables lane bonding between @sw and parent. This can be called even
 * if lanes were not bonded originally.
 */
void tb_switch_lane_bonding_disable(struct tb_switch *sw)
{
	struct tb_switch *parent = tb_to_switch(sw->dev.parent);
	struct tb_port *up, *down;

	if (!tb_route(sw))
		return;

	up = tb_upstream_port(sw);
	if (!up->bonded)
		return;

	down = tb_port_at(tb_route(sw), parent);

	tb_port_lane_bonding_disable(up);
	tb_port_lane_bonding_disable(down);

	/*
	 * It is fine if we get other errors as the router might have
	 * been unplugged.
	 */
	if (tb_port_wait_for_link_width(down, 1, 100) == -ETIMEDOUT)
		tb_sw_warn(sw, "timeout disabling lane bonding\n");

	tb_port_update_credits(down);
	tb_port_update_credits(up);
	tb_switch_update_link_attributes(sw);

	tb_sw_dbg(sw, "lane bonding disabled\n");
}

/**
 * tb_switch_configure_link() - Set link configured
 * @sw: Switch whose link is configured
 *
 * Sets the link upstream from @sw configured (from both ends) so that
 * it will not be disconnected when the domain exits sleep. Can be
 * called for any switch.
 *
 * It is recommended that this is called after lane bonding is enabled.
 *
 * Returns %0 on success and negative errno in case of error.
 */
int tb_switch_configure_link(struct tb_switch *sw)
{
	struct tb_port *up, *down;
	int ret;

	if (!tb_route(sw) || tb_switch_is_icm(sw))
		return 0;

	up = tb_upstream_port(sw);
	if (tb_switch_is_usb4(up->sw))
		ret = usb4_port_configure(up);
	else
		ret = tb_lc_configure_port(up);
	if (ret)
		return ret;

	down = up->remote;
	if (tb_switch_is_usb4(down->sw))
		return usb4_port_configure(down);
	return tb_lc_configure_port(down);
}

/**
 * tb_switch_unconfigure_link() - Unconfigure link
 * @sw: Switch whose link is unconfigured
 *
 * Sets the link unconfigured so the @sw will be disconnected if the
 * domain exists sleep.
 */
void tb_switch_unconfigure_link(struct tb_switch *sw)
{
	struct tb_port *up, *down;

	if (!tb_route(sw) || tb_switch_is_icm(sw))
		return;

	/*
	 * Unconfigure downstream port so that wake-on-connect can be
	 * configured after router unplug. No need to unconfigure upstream port
	 * since its router is unplugged.
	 */
	up = tb_upstream_port(sw);
	down = up->remote;
	if (tb_switch_is_usb4(down->sw))
		usb4_port_unconfigure(down);
	else
		tb_lc_unconfigure_port(down);

	if (sw->is_unplugged)
		return;

	up = tb_upstream_port(sw);
	if (tb_switch_is_usb4(up->sw))
		usb4_port_unconfigure(up);
	else
		tb_lc_unconfigure_port(up);
}

static void tb_switch_credits_init(struct tb_switch *sw)
{
	if (tb_switch_is_icm(sw))
		return;
	if (!tb_switch_is_usb4(sw))
		return;
	if (usb4_switch_credits_init(sw))
		tb_sw_info(sw, "failed to determine preferred buffer allocation, using defaults\n");
}

static int tb_switch_port_hotplug_enable(struct tb_switch *sw)
{
	struct tb_port *port;

	if (tb_switch_is_icm(sw))
		return 0;

	tb_switch_for_each_port(sw, port) {
		int res;

		if (!port->cap_usb4)
			continue;

		res = usb4_port_hotplug_enable(port);
		if (res)
			return res;
	}
	return 0;
}

/**
 * tb_switch_add() - Add a switch to the domain
 * @sw: Switch to add
 *
 * This is the last step in adding switch to the domain. It will read
 * identification information from DROM and initializes ports so that
 * they can be used to connect other switches. The switch will be
 * exposed to the userspace when this function successfully returns. To
 * remove and release the switch, call tb_switch_remove().
 *
 * Return: %0 in case of success and negative errno in case of failure
 */
int tb_switch_add(struct tb_switch *sw)
{
	int i, ret;

	/*
	 * Initialize DMA control port now before we read DROM. Recent
	 * host controllers have more complete DROM on NVM that includes
	 * vendor and model identification strings which we then expose
	 * to the userspace. NVM can be accessed through DMA
	 * configuration based mailbox.
	 */
	ret = tb_switch_add_dma_port(sw);
	if (ret) {
		dev_err(&sw->dev, "failed to add DMA port\n");
		return ret;
	}

	if (!sw->safe_mode) {
		tb_switch_credits_init(sw);

		/* read drom */
		ret = tb_drom_read(sw);
		if (ret)
			dev_warn(&sw->dev, "reading DROM failed: %d\n", ret);
		tb_sw_dbg(sw, "uid: %#llx\n", sw->uid);

		ret = tb_switch_set_uuid(sw);
		if (ret) {
			dev_err(&sw->dev, "failed to set UUID\n");
			return ret;
		}

		for (i = 0; i <= sw->config.max_port_number; i++) {
			if (sw->ports[i].disabled) {
				tb_port_dbg(&sw->ports[i], "disabled by eeprom\n");
				continue;
			}
			ret = tb_init_port(&sw->ports[i]);
			if (ret) {
				dev_err(&sw->dev, "failed to initialize port %d\n", i);
				return ret;
			}
		}

		tb_check_quirks(sw);

		tb_switch_default_link_ports(sw);

		ret = tb_switch_update_link_attributes(sw);
		if (ret)
			return ret;

		ret = tb_switch_tmu_init(sw);
		if (ret)
			return ret;
	}

	ret = tb_switch_port_hotplug_enable(sw);
	if (ret)
		return ret;

	ret = device_add(&sw->dev);
	if (ret) {
		dev_err(&sw->dev, "failed to add device: %d\n", ret);
		return ret;
	}

	if (tb_route(sw)) {
		dev_info(&sw->dev, "new device found, vendor=%#x device=%#x\n",
			 sw->vendor, sw->device);
		if (sw->vendor_name && sw->device_name)
			dev_info(&sw->dev, "%s %s\n", sw->vendor_name,
				 sw->device_name);
	}

	ret = usb4_switch_add_ports(sw);
	if (ret) {
		dev_err(&sw->dev, "failed to add USB4 ports\n");
		goto err_del;
	}

	ret = tb_switch_nvm_add(sw);
	if (ret) {
		dev_err(&sw->dev, "failed to add NVM devices\n");
		goto err_ports;
	}

	/*
	 * Thunderbolt routers do not generate wakeups themselves but
	 * they forward wakeups from tunneled protocols, so enable it
	 * here.
	 */
	device_init_wakeup(&sw->dev, true);

	pm_runtime_set_active(&sw->dev);
	if (sw->rpm) {
		pm_runtime_set_autosuspend_delay(&sw->dev, TB_AUTOSUSPEND_DELAY);
		pm_runtime_use_autosuspend(&sw->dev);
		pm_runtime_mark_last_busy(&sw->dev);
		pm_runtime_enable(&sw->dev);
		pm_request_autosuspend(&sw->dev);
	}

	tb_switch_debugfs_init(sw);
	return 0;

err_ports:
	usb4_switch_remove_ports(sw);
err_del:
	device_del(&sw->dev);

	return ret;
}

/**
 * tb_switch_remove() - Remove and release a switch
 * @sw: Switch to remove
 *
 * This will remove the switch from the domain and release it after last
 * reference count drops to zero. If there are switches connected below
 * this switch, they will be removed as well.
 */
void tb_switch_remove(struct tb_switch *sw)
{
	struct tb_port *port;

	tb_switch_debugfs_remove(sw);

	if (sw->rpm) {
		pm_runtime_get_sync(&sw->dev);
		pm_runtime_disable(&sw->dev);
	}

	/* port 0 is the switch itself and never has a remote */
	tb_switch_for_each_port(sw, port) {
		if (tb_port_has_remote(port)) {
			tb_switch_remove(port->remote->sw);
			port->remote = NULL;
		} else if (port->xdomain) {
			tb_xdomain_remove(port->xdomain);
			port->xdomain = NULL;
		}

		/* Remove any downstream retimers */
		tb_retimer_remove_all(port);
	}

	if (!sw->is_unplugged)
		tb_plug_events_active(sw, false);

	tb_switch_nvm_remove(sw);
	usb4_switch_remove_ports(sw);

	if (tb_route(sw))
		dev_info(&sw->dev, "device disconnected\n");
	device_unregister(&sw->dev);
}

/**
 * tb_sw_set_unplugged() - set is_unplugged on switch and downstream switches
 * @sw: Router to mark unplugged
 */
void tb_sw_set_unplugged(struct tb_switch *sw)
{
	struct tb_port *port;

	if (sw == sw->tb->root_switch) {
		tb_sw_WARN(sw, "cannot unplug root switch\n");
		return;
	}
	if (sw->is_unplugged) {
		tb_sw_WARN(sw, "is_unplugged already set\n");
		return;
	}
	sw->is_unplugged = true;
	tb_switch_for_each_port(sw, port) {
		if (tb_port_has_remote(port))
			tb_sw_set_unplugged(port->remote->sw);
		else if (port->xdomain)
			port->xdomain->is_unplugged = true;
	}
}

static int tb_switch_set_wake(struct tb_switch *sw, unsigned int flags)
{
	if (flags)
		tb_sw_dbg(sw, "enabling wakeup: %#x\n", flags);
	else
		tb_sw_dbg(sw, "disabling wakeup\n");

	if (tb_switch_is_usb4(sw))
		return usb4_switch_set_wake(sw, flags);
	return tb_lc_set_wake(sw, flags);
}

static void tb_switch_check_wakes(struct tb_switch *sw)
{
	if (device_may_wakeup(&sw->dev)) {
		if (tb_switch_is_usb4(sw))
			usb4_switch_check_wakes(sw);
	}
}

/**
 * tb_switch_resume() - Resume a switch after sleep
 * @sw: Switch to resume
 * @runtime: Is this resume from runtime suspend or system sleep
 *
 * Resumes and re-enumerates router (and all its children), if still plugged
 * after suspend. Don't enumerate device router whose UID was changed during
 * suspend. If this is resume from system sleep, notifies PM core about the
 * wakes occurred during suspend. Disables all wakes, except USB4 wake of
 * upstream port for USB4 routers that shall be always enabled.
 */
int tb_switch_resume(struct tb_switch *sw, bool runtime)
{
	struct tb_port *port;
	int err;

	tb_sw_dbg(sw, "resuming switch\n");

	/*
	 * Check for UID of the connected switches except for root
	 * switch which we assume cannot be removed.
	 */
	if (tb_route(sw)) {
		u64 uid;

		/*
		 * Check first that we can still read the switch config
		 * space. It may be that there is now another domain
		 * connected.
		 */
		err = tb_cfg_get_upstream_port(sw->tb->ctl, tb_route(sw));
		if (err < 0) {
			tb_sw_info(sw, "switch not present anymore\n");
			return err;
		}

		/* We don't have any way to confirm this was the same device */
		if (!sw->uid)
			return -ENODEV;

		if (tb_switch_is_usb4(sw))
			err = usb4_switch_read_uid(sw, &uid);
		else
			err = tb_drom_read_uid_only(sw, &uid);
		if (err) {
			tb_sw_warn(sw, "uid read failed\n");
			return err;
		}
		if (sw->uid != uid) {
			tb_sw_info(sw,
				"changed while suspended (uid %#llx -> %#llx)\n",
				sw->uid, uid);
			return -ENODEV;
		}
	}

	err = tb_switch_configure(sw);
	if (err)
		return err;

	if (!runtime)
		tb_switch_check_wakes(sw);

	/* Disable wakes */
	tb_switch_set_wake(sw, 0);

	err = tb_switch_tmu_init(sw);
	if (err)
		return err;

	/* check for surviving downstream switches */
	tb_switch_for_each_port(sw, port) {
		if (!tb_port_is_null(port))
			continue;

		if (!tb_port_resume(port))
			continue;

		if (tb_wait_for_port(port, true) <= 0) {
			tb_port_warn(port,
				     "lost during suspend, disconnecting\n");
			if (tb_port_has_remote(port))
				tb_sw_set_unplugged(port->remote->sw);
			else if (port->xdomain)
				port->xdomain->is_unplugged = true;
		} else {
			/*
			 * Always unlock the port so the downstream
			 * switch/domain is accessible.
			 */
			if (tb_port_unlock(port))
				tb_port_warn(port, "failed to unlock port\n");
			if (port->remote &&
			    tb_switch_resume(port->remote->sw, runtime)) {
				tb_port_warn(port,
					     "lost during suspend, disconnecting\n");
				tb_sw_set_unplugged(port->remote->sw);
			}
		}
	}
	return 0;
}

/**
 * tb_switch_suspend() - Put a switch to sleep
 * @sw: Switch to suspend
 * @runtime: Is this runtime suspend or system sleep
 *
 * Suspends router and all its children. Enables wakes according to
 * value of @runtime and then sets sleep bit for the router. If @sw is
 * host router the domain is ready to go to sleep once this function
 * returns.
 */
void tb_switch_suspend(struct tb_switch *sw, bool runtime)
{
	unsigned int flags = 0;
	struct tb_port *port;
	int err;

	tb_sw_dbg(sw, "suspending switch\n");

	/*
	 * Actually only needed for Titan Ridge but for simplicity can be
	 * done for USB4 device too as CLx is re-enabled at resume.
	 * CL0s and CL1 are enabled and supported together.
	 */
	if (tb_switch_is_clx_enabled(sw, TB_CL1)) {
		if (tb_switch_disable_clx(sw, TB_CL1))
			tb_sw_warn(sw, "failed to disable %s on upstream port\n",
				   tb_switch_clx_name(TB_CL1));
	}

	err = tb_plug_events_active(sw, false);
	if (err)
		return;

	tb_switch_for_each_port(sw, port) {
		if (tb_port_has_remote(port))
			tb_switch_suspend(port->remote->sw, runtime);
	}

	if (runtime) {
		/* Trigger wake when something is plugged in/out */
		flags |= TB_WAKE_ON_CONNECT | TB_WAKE_ON_DISCONNECT;
		flags |= TB_WAKE_ON_USB4;
		flags |= TB_WAKE_ON_USB3 | TB_WAKE_ON_PCIE | TB_WAKE_ON_DP;
	} else if (device_may_wakeup(&sw->dev)) {
		flags |= TB_WAKE_ON_USB4 | TB_WAKE_ON_USB3 | TB_WAKE_ON_PCIE;
	}

	tb_switch_set_wake(sw, flags);

	if (tb_switch_is_usb4(sw))
		usb4_switch_set_sleep(sw);
	else
		tb_lc_set_sleep(sw);
}

/**
 * tb_switch_query_dp_resource() - Query availability of DP resource
 * @sw: Switch whose DP resource is queried
 * @in: DP IN port
 *
 * Queries availability of DP resource for DP tunneling using switch
 * specific means. Returns %true if resource is available.
 */
bool tb_switch_query_dp_resource(struct tb_switch *sw, struct tb_port *in)
{
	if (tb_switch_is_usb4(sw))
		return usb4_switch_query_dp_resource(sw, in);
	return tb_lc_dp_sink_query(sw, in);
}

/**
 * tb_switch_alloc_dp_resource() - Allocate available DP resource
 * @sw: Switch whose DP resource is allocated
 * @in: DP IN port
 *
 * Allocates DP resource for DP tunneling. The resource must be
 * available for this to succeed (see tb_switch_query_dp_resource()).
 * Returns %0 in success and negative errno otherwise.
 */
int tb_switch_alloc_dp_resource(struct tb_switch *sw, struct tb_port *in)
{
	int ret;

	if (tb_switch_is_usb4(sw))
		ret = usb4_switch_alloc_dp_resource(sw, in);
	else
		ret = tb_lc_dp_sink_alloc(sw, in);

	if (ret)
		tb_sw_warn(sw, "failed to allocate DP resource for port %d\n",
			   in->port);
	else
		tb_sw_dbg(sw, "allocated DP resource for port %d\n", in->port);

	return ret;
}

/**
 * tb_switch_dealloc_dp_resource() - De-allocate DP resource
 * @sw: Switch whose DP resource is de-allocated
 * @in: DP IN port
 *
 * De-allocates DP resource that was previously allocated for DP
 * tunneling.
 */
void tb_switch_dealloc_dp_resource(struct tb_switch *sw, struct tb_port *in)
{
	int ret;

	if (tb_switch_is_usb4(sw))
		ret = usb4_switch_dealloc_dp_resource(sw, in);
	else
		ret = tb_lc_dp_sink_dealloc(sw, in);

	if (ret)
		tb_sw_warn(sw, "failed to de-allocate DP resource for port %d\n",
			   in->port);
	else
		tb_sw_dbg(sw, "released DP resource for port %d\n", in->port);
}

struct tb_sw_lookup {
	struct tb *tb;
	u8 link;
	u8 depth;
	const uuid_t *uuid;
	u64 route;
};

static int tb_switch_match(struct device *dev, const void *data)
{
	struct tb_switch *sw = tb_to_switch(dev);
	const struct tb_sw_lookup *lookup = data;

	if (!sw)
		return 0;
	if (sw->tb != lookup->tb)
		return 0;

	if (lookup->uuid)
		return !memcmp(sw->uuid, lookup->uuid, sizeof(*lookup->uuid));

	if (lookup->route) {
		return sw->config.route_lo == lower_32_bits(lookup->route) &&
		       sw->config.route_hi == upper_32_bits(lookup->route);
	}

	/* Root switch is matched only by depth */
	if (!lookup->depth)
		return !sw->depth;

	return sw->link == lookup->link && sw->depth == lookup->depth;
}

/**
 * tb_switch_find_by_link_depth() - Find switch by link and depth
 * @tb: Domain the switch belongs
 * @link: Link number the switch is connected
 * @depth: Depth of the switch in link
 *
 * Returned switch has reference count increased so the caller needs to
 * call tb_switch_put() when done with the switch.
 */
struct tb_switch *tb_switch_find_by_link_depth(struct tb *tb, u8 link, u8 depth)
{
	struct tb_sw_lookup lookup;
	struct device *dev;

	memset(&lookup, 0, sizeof(lookup));
	lookup.tb = tb;
	lookup.link = link;
	lookup.depth = depth;

	dev = bus_find_device(&tb_bus_type, NULL, &lookup, tb_switch_match);
	if (dev)
		return tb_to_switch(dev);

	return NULL;
}

/**
 * tb_switch_find_by_uuid() - Find switch by UUID
 * @tb: Domain the switch belongs
 * @uuid: UUID to look for
 *
 * Returned switch has reference count increased so the caller needs to
 * call tb_switch_put() when done with the switch.
 */
struct tb_switch *tb_switch_find_by_uuid(struct tb *tb, const uuid_t *uuid)
{
	struct tb_sw_lookup lookup;
	struct device *dev;

	memset(&lookup, 0, sizeof(lookup));
	lookup.tb = tb;
	lookup.uuid = uuid;

	dev = bus_find_device(&tb_bus_type, NULL, &lookup, tb_switch_match);
	if (dev)
		return tb_to_switch(dev);

	return NULL;
}

/**
 * tb_switch_find_by_route() - Find switch by route string
 * @tb: Domain the switch belongs
 * @route: Route string to look for
 *
 * Returned switch has reference count increased so the caller needs to
 * call tb_switch_put() when done with the switch.
 */
struct tb_switch *tb_switch_find_by_route(struct tb *tb, u64 route)
{
	struct tb_sw_lookup lookup;
	struct device *dev;

	if (!route)
		return tb_switch_get(tb->root_switch);

	memset(&lookup, 0, sizeof(lookup));
	lookup.tb = tb;
	lookup.route = route;

	dev = bus_find_device(&tb_bus_type, NULL, &lookup, tb_switch_match);
	if (dev)
		return tb_to_switch(dev);

	return NULL;
}

/**
 * tb_switch_find_port() - return the first port of @type on @sw or NULL
 * @sw: Switch to find the port from
 * @type: Port type to look for
 */
struct tb_port *tb_switch_find_port(struct tb_switch *sw,
				    enum tb_port_type type)
{
	struct tb_port *port;

	tb_switch_for_each_port(sw, port) {
		if (port->config.type == type)
			return port;
	}

	return NULL;
}

static int tb_switch_pm_secondary_resolve(struct tb_switch *sw)
{
	struct tb_switch *parent = tb_switch_parent(sw);
	struct tb_port *up, *down;
	int ret;

	if (!tb_route(sw))
		return 0;

	up = tb_upstream_port(sw);
	down = tb_port_at(tb_route(sw), parent);
	ret = tb_port_pm_secondary_enable(up);
	if (ret)
		return ret;

	return tb_port_pm_secondary_disable(down);
}

static int __tb_switch_enable_clx(struct tb_switch *sw, enum tb_clx clx)
{
	struct tb_switch *parent = tb_switch_parent(sw);
	bool up_clx_support, down_clx_support;
	struct tb_port *up, *down;
	int ret;

	if (!tb_switch_is_clx_supported(sw))
		return 0;

	/*
	 * Enable CLx for host router's downstream port as part of the
	 * downstream router enabling procedure.
	 */
	if (!tb_route(sw))
		return 0;

	/* Enable CLx only for first hop router (depth = 1) */
	if (tb_route(parent))
		return 0;

	ret = tb_switch_pm_secondary_resolve(sw);
	if (ret)
		return ret;

	up = tb_upstream_port(sw);
	down = tb_port_at(tb_route(sw), parent);

	up_clx_support = tb_port_clx_supported(up, clx);
	down_clx_support = tb_port_clx_supported(down, clx);

	tb_port_dbg(up, "%s %ssupported\n", tb_switch_clx_name(clx),
		    up_clx_support ? "" : "not ");
	tb_port_dbg(down, "%s %ssupported\n", tb_switch_clx_name(clx),
		    down_clx_support ? "" : "not ");

	if (!up_clx_support || !down_clx_support)
		return -EOPNOTSUPP;

	ret = tb_port_clx_enable(up, clx);
	if (ret)
		return ret;

	ret = tb_port_clx_enable(down, clx);
	if (ret) {
		tb_port_clx_disable(up, clx);
		return ret;
	}

	ret = tb_switch_mask_clx_objections(sw);
	if (ret) {
		tb_port_clx_disable(up, clx);
		tb_port_clx_disable(down, clx);
		return ret;
	}

	sw->clx = clx;

	tb_port_dbg(up, "%s enabled\n", tb_switch_clx_name(clx));
	return 0;
}

/**
 * tb_switch_enable_clx() - Enable CLx on upstream port of specified router
 * @sw: Router to enable CLx for
 * @clx: The CLx state to enable
 *
 * Enable CLx state only for first hop router. That is the most common
 * use-case, that is intended for better thermal management, and so helps
 * to improve performance. CLx is enabled only if both sides of the link
 * support CLx, and if both sides of the link are not configured as two
 * single lane links and only if the link is not inter-domain link. The
 * complete set of conditions is described in CM Guide 1.0 section 8.1.
 *
 * Return: Returns 0 on success or an error code on failure.
 */
int tb_switch_enable_clx(struct tb_switch *sw, enum tb_clx clx)
{
	struct tb_switch *root_sw = sw->tb->root_switch;

	if (!clx_enabled)
		return 0;

	/*
	 * CLx is not enabled and validated on Intel USB4 platforms before
	 * Alder Lake.
	 */
	if (root_sw->generation < 4 || tb_switch_is_tiger_lake(root_sw))
		return 0;

	switch (clx) {
	case TB_CL1:
		/* CL0s and CL1 are enabled and supported together */
		return __tb_switch_enable_clx(sw, clx);

	default:
		return -EOPNOTSUPP;
	}
}

static int __tb_switch_disable_clx(struct tb_switch *sw, enum tb_clx clx)
{
	struct tb_switch *parent = tb_switch_parent(sw);
	struct tb_port *up, *down;
	int ret;

	if (!tb_switch_is_clx_supported(sw))
		return 0;

	/*
	 * Disable CLx for host router's downstream port as part of the
	 * downstream router enabling procedure.
	 */
	if (!tb_route(sw))
		return 0;

	/* Disable CLx only for first hop router (depth = 1) */
	if (tb_route(parent))
		return 0;

	up = tb_upstream_port(sw);
	down = tb_port_at(tb_route(sw), parent);
	ret = tb_port_clx_disable(up, clx);
	if (ret)
		return ret;

	ret = tb_port_clx_disable(down, clx);
	if (ret)
		return ret;

	sw->clx = TB_CLX_DISABLE;

	tb_port_dbg(up, "%s disabled\n", tb_switch_clx_name(clx));
	return 0;
}

/**
 * tb_switch_disable_clx() - Disable CLx on upstream port of specified router
 * @sw: Router to disable CLx for
 * @clx: The CLx state to disable
 *
 * Return: Returns 0 on success or an error code on failure.
 */
int tb_switch_disable_clx(struct tb_switch *sw, enum tb_clx clx)
{
	if (!clx_enabled)
		return 0;

	switch (clx) {
	case TB_CL1:
		/* CL0s and CL1 are enabled and supported together */
		return __tb_switch_disable_clx(sw, clx);

	default:
		return -EOPNOTSUPP;
	}
}

/**
 * tb_switch_mask_clx_objections() - Mask CLx objections for a router
 * @sw: Router to mask objections for
 *
 * Mask the objections coming from the second depth routers in order to
 * stop these objections from interfering with the CLx states of the first
 * depth link.
 */
int tb_switch_mask_clx_objections(struct tb_switch *sw)
{
	int up_port = sw->config.upstream_port_number;
	u32 offset, val[2], mask_obj, unmask_obj;
	int ret, i;

	/* Only Titan Ridge of pre-USB4 devices support CLx states */
	if (!tb_switch_is_titan_ridge(sw))
		return 0;

	if (!tb_route(sw))
		return 0;

	/*
	 * In Titan Ridge there are only 2 dual-lane Thunderbolt ports:
	 * Port A consists of lane adapters 1,2 and
	 * Port B consists of lane adapters 3,4
	 * If upstream port is A, (lanes are 1,2), we mask objections from
	 * port B (lanes 3,4) and unmask objections from Port A and vice-versa.
	 */
	if (up_port == 1) {
		mask_obj = TB_LOW_PWR_C0_PORT_B_MASK;
		unmask_obj = TB_LOW_PWR_C1_PORT_A_MASK;
		offset = TB_LOW_PWR_C1_CL1;
	} else {
		mask_obj = TB_LOW_PWR_C1_PORT_A_MASK;
		unmask_obj = TB_LOW_PWR_C0_PORT_B_MASK;
		offset = TB_LOW_PWR_C3_CL1;
	}

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH,
			 sw->cap_lp + offset, ARRAY_SIZE(val));
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(val); i++) {
		val[i] |= mask_obj;
		val[i] &= ~unmask_obj;
	}

	return tb_sw_write(sw, &val, TB_CFG_SWITCH,
			   sw->cap_lp + offset, ARRAY_SIZE(val));
}

/*
 * Can be used for read/write a specified PCIe bridge for any Thunderbolt 3
 * device. For now used only for Titan Ridge.
 */
static int tb_switch_pcie_bridge_write(struct tb_switch *sw, unsigned int bridge,
				       unsigned int pcie_offset, u32 value)
{
	u32 offset, command, val;
	int ret;

	if (sw->generation != 3)
		return -EOPNOTSUPP;

	offset = sw->cap_plug_events + TB_PLUG_EVENTS_PCIE_WR_DATA;
	ret = tb_sw_write(sw, &value, TB_CFG_SWITCH, offset, 1);
	if (ret)
		return ret;

	command = pcie_offset & TB_PLUG_EVENTS_PCIE_CMD_DW_OFFSET_MASK;
	command |= BIT(bridge + TB_PLUG_EVENTS_PCIE_CMD_BR_SHIFT);
	command |= TB_PLUG_EVENTS_PCIE_CMD_RD_WR_MASK;
	command |= TB_PLUG_EVENTS_PCIE_CMD_COMMAND_VAL
			<< TB_PLUG_EVENTS_PCIE_CMD_COMMAND_SHIFT;
	command |= TB_PLUG_EVENTS_PCIE_CMD_REQ_ACK_MASK;

	offset = sw->cap_plug_events + TB_PLUG_EVENTS_PCIE_CMD;

	ret = tb_sw_write(sw, &command, TB_CFG_SWITCH, offset, 1);
	if (ret)
		return ret;

	ret = tb_switch_wait_for_bit(sw, offset,
				     TB_PLUG_EVENTS_PCIE_CMD_REQ_ACK_MASK, 0, 100);
	if (ret)
		return ret;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, offset, 1);
	if (ret)
		return ret;

	if (val & TB_PLUG_EVENTS_PCIE_CMD_TIMEOUT_MASK)
		return -ETIMEDOUT;

	return 0;
}

/**
 * tb_switch_pcie_l1_enable() - Enable PCIe link to enter L1 state
 * @sw: Router to enable PCIe L1
 *
 * For Titan Ridge switch to enter CLx state, its PCIe bridges shall enable
 * entry to PCIe L1 state. Shall be called after the upstream PCIe tunnel
 * was configured. Due to Intel platforms limitation, shall be called only
 * for first hop switch.
 */
int tb_switch_pcie_l1_enable(struct tb_switch *sw)
{
	struct tb_switch *parent = tb_switch_parent(sw);
	int ret;

	if (!tb_route(sw))
		return 0;

	if (!tb_switch_is_titan_ridge(sw))
		return 0;

	/* Enable PCIe L1 enable only for first hop router (depth = 1) */
	if (tb_route(parent))
		return 0;

	/* Write to downstream PCIe bridge #5 aka Dn4 */
	ret = tb_switch_pcie_bridge_write(sw, 5, 0x143, 0x0c7806b1);
	if (ret)
		return ret;

	/* Write to Upstream PCIe bridge #0 aka Up0 */
	return tb_switch_pcie_bridge_write(sw, 0, 0x143, 0x0c5806b1);
}

/**
 * tb_switch_xhci_connect() - Connect internal xHCI
 * @sw: Router whose xHCI to connect
 *
 * Can be called to any router. For Alpine Ridge and Titan Ridge
 * performs special flows that bring the xHCI functional for any device
 * connected to the type-C port. Call only after PCIe tunnel has been
 * established. The function only does the connect if not done already
 * so can be called several times for the same router.
 */
int tb_switch_xhci_connect(struct tb_switch *sw)
{
	struct tb_port *port1, *port3;
	int ret;

	if (sw->generation != 3)
		return 0;

	port1 = &sw->ports[1];
	port3 = &sw->ports[3];

	if (tb_switch_is_alpine_ridge(sw)) {
		bool usb_port1, usb_port3, xhci_port1, xhci_port3;

		usb_port1 = tb_lc_is_usb_plugged(port1);
		usb_port3 = tb_lc_is_usb_plugged(port3);
		xhci_port1 = tb_lc_is_xhci_connected(port1);
		xhci_port3 = tb_lc_is_xhci_connected(port3);

		/* Figure out correct USB port to connect */
		if (usb_port1 && !xhci_port1) {
			ret = tb_lc_xhci_connect(port1);
			if (ret)
				return ret;
		}
		if (usb_port3 && !xhci_port3)
			return tb_lc_xhci_connect(port3);
	} else if (tb_switch_is_titan_ridge(sw)) {
		ret = tb_lc_xhci_connect(port1);
		if (ret)
			return ret;
		return tb_lc_xhci_connect(port3);
	}

	return 0;
}

/**
 * tb_switch_xhci_disconnect() - Disconnect internal xHCI
 * @sw: Router whose xHCI to disconnect
 *
 * The opposite of tb_switch_xhci_connect(). Disconnects xHCI on both
 * ports.
 */
void tb_switch_xhci_disconnect(struct tb_switch *sw)
{
	if (sw->generation == 3) {
		struct tb_port *port1 = &sw->ports[1];
		struct tb_port *port3 = &sw->ports[3];

		tb_lc_xhci_disconnect(port1);
		tb_port_dbg(port1, "disconnected xHCI\n");
		tb_lc_xhci_disconnect(port3);
		tb_port_dbg(port3, "disconnected xHCI\n");
	}
}
