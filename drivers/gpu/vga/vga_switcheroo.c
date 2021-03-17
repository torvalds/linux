/*
 * vga_switcheroo.c - Support for laptop with dual GPU using one set of outputs
 *
 * Copyright (c) 2010 Red Hat Inc.
 * Author : Dave Airlie <airlied@redhat.com>
 *
 * Copyright (c) 2015 Lukas Wunner <lukas@wunner.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS
 * IN THE SOFTWARE.
 *
 */

#define pr_fmt(fmt) "vga_switcheroo: " fmt

#include <linux/apple-gmux.h>
#include <linux/console.h>
#include <linux/debugfs.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>

/**
 * DOC: Overview
 *
 * vga_switcheroo is the Linux subsystem for laptop hybrid graphics.
 * These come in two flavors:
 *
 * * muxed: Dual GPUs with a multiplexer chip to switch outputs between GPUs.
 * * muxless: Dual GPUs but only one of them is connected to outputs.
 *   The other one is merely used to offload rendering, its results
 *   are copied over PCIe into the framebuffer. On Linux this is
 *   supported with DRI PRIME.
 *
 * Hybrid graphics started to appear in the late Naughties and were initially
 * all muxed. Newer laptops moved to a muxless architecture for cost reasons.
 * A notable exception is the MacBook Pro which continues to use a mux.
 * Muxes come with varying capabilities: Some switch only the panel, others
 * can also switch external displays. Some switch all display pins at once
 * while others can switch just the DDC lines. (To allow EDID probing
 * for the inactive GPU.) Also, muxes are often used to cut power to the
 * discrete GPU while it is not used.
 *
 * DRM drivers register GPUs with vga_switcheroo, these are henceforth called
 * clients. The mux is called the handler. Muxless machines also register a
 * handler to control the power state of the discrete GPU, its ->switchto
 * callback is a no-op for obvious reasons. The discrete GPU is often equipped
 * with an HDA controller for the HDMI/DP audio signal, this will also
 * register as a client so that vga_switcheroo can take care of the correct
 * suspend/resume order when changing the discrete GPU's power state. In total
 * there can thus be up to three clients: Two vga clients (GPUs) and one audio
 * client (on the discrete GPU). The code is mostly prepared to support
 * machines with more than two GPUs should they become available.
 *
 * The GPU to which the outputs are currently switched is called the
 * active client in vga_switcheroo parlance. The GPU not in use is the
 * inactive client. When the inactive client's DRM driver is loaded,
 * it will be unable to probe the panel's EDID and hence depends on
 * VBIOS to provide its display modes. If the VBIOS modes are bogus or
 * if there is no VBIOS at all (which is common on the MacBook Pro),
 * a client may alternatively request that the DDC lines are temporarily
 * switched to it, provided that the handler supports this. Switching
 * only the DDC lines and not the entire output avoids unnecessary
 * flickering.
 */

/**
 * struct vga_switcheroo_client - registered client
 * @pdev: client pci device
 * @fb_info: framebuffer to which console is remapped on switching
 * @pwr_state: current power state if manual power control is used.
 *	For driver power control, call vga_switcheroo_pwr_state().
 * @ops: client callbacks
 * @id: client identifier. Determining the id requires the handler,
 *	so gpus are initially assigned VGA_SWITCHEROO_UNKNOWN_ID
 *	and later given their true id in vga_switcheroo_enable()
 * @active: whether the outputs are currently switched to this client
 * @driver_power_control: whether power state is controlled by the driver's
 *	runtime pm. If true, writing ON and OFF to the vga_switcheroo debugfs
 *	interface is a no-op so as not to interfere with runtime pm
 * @list: client list
 * @vga_dev: pci device, indicate which GPU is bound to current audio client
 *
 * Registered client. A client can be either a GPU or an audio device on a GPU.
 * For audio clients, the @fb_info and @active members are bogus. For GPU
 * clients, the @vga_dev is bogus.
 */
struct vga_switcheroo_client {
	struct pci_dev *pdev;
	struct fb_info *fb_info;
	enum vga_switcheroo_state pwr_state;
	const struct vga_switcheroo_client_ops *ops;
	enum vga_switcheroo_client_id id;
	bool active;
	bool driver_power_control;
	struct list_head list;
	struct pci_dev *vga_dev;
};

/*
 * protects access to struct vgasr_priv
 */
static DEFINE_MUTEX(vgasr_mutex);

/**
 * struct vgasr_priv - vga_switcheroo private data
 * @active: whether vga_switcheroo is enabled.
 *	Prerequisite is the registration of two GPUs and a handler
 * @delayed_switch_active: whether a delayed switch is pending
 * @delayed_client_id: client to which a delayed switch is pending
 * @debugfs_root: directory for vga_switcheroo debugfs interface
 * @switch_file: file for vga_switcheroo debugfs interface
 * @registered_clients: number of registered GPUs
 *	(counting only vga clients, not audio clients)
 * @clients: list of registered clients
 * @handler: registered handler
 * @handler_flags: flags of registered handler
 * @mux_hw_lock: protects mux state
 *	(in particular while DDC lines are temporarily switched)
 * @old_ddc_owner: client to which DDC lines will be switched back on unlock
 *
 * vga_switcheroo private data. Currently only one vga_switcheroo instance
 * per system is supported.
 */
struct vgasr_priv {
	bool active;
	bool delayed_switch_active;
	enum vga_switcheroo_client_id delayed_client_id;

	struct dentry *debugfs_root;
	struct dentry *switch_file;

	int registered_clients;
	struct list_head clients;

	const struct vga_switcheroo_handler *handler;
	enum vga_switcheroo_handler_flags_t handler_flags;
	struct mutex mux_hw_lock;
	int old_ddc_owner;
};

#define ID_BIT_AUDIO		0x100
#define client_is_audio(c)		((c)->id & ID_BIT_AUDIO)
#define client_is_vga(c)		(!client_is_audio(c))
#define client_id(c)		((c)->id & ~ID_BIT_AUDIO)

static int vga_switcheroo_debugfs_init(struct vgasr_priv *priv);
static void vga_switcheroo_debugfs_fini(struct vgasr_priv *priv);

/* only one switcheroo per system */
static struct vgasr_priv vgasr_priv = {
	.clients = LIST_HEAD_INIT(vgasr_priv.clients),
	.mux_hw_lock = __MUTEX_INITIALIZER(vgasr_priv.mux_hw_lock),
};

static bool vga_switcheroo_ready(void)
{
	/* we're ready if we get two clients + handler */
	return !vgasr_priv.active &&
	       vgasr_priv.registered_clients == 2 && vgasr_priv.handler;
}

static void vga_switcheroo_enable(void)
{
	int ret;
	struct vga_switcheroo_client *client;

	/* call the handler to init */
	if (vgasr_priv.handler->init)
		vgasr_priv.handler->init();

	list_for_each_entry(client, &vgasr_priv.clients, list) {
		if (!client_is_vga(client) ||
		     client_id(client) != VGA_SWITCHEROO_UNKNOWN_ID)
			continue;

		ret = vgasr_priv.handler->get_client_id(client->pdev);
		if (ret < 0)
			return;

		client->id = ret;
	}

	list_for_each_entry(client, &vgasr_priv.clients, list) {
		if (!client_is_audio(client) ||
		     client_id(client) != VGA_SWITCHEROO_UNKNOWN_ID)
			continue;

		ret = vgasr_priv.handler->get_client_id(client->vga_dev);
		if (ret < 0)
			return;

		client->id = ret | ID_BIT_AUDIO;
		if (client->ops->gpu_bound)
			client->ops->gpu_bound(client->pdev, ret);
	}

	vga_switcheroo_debugfs_init(&vgasr_priv);
	vgasr_priv.active = true;
}

/**
 * vga_switcheroo_register_handler() - register handler
 * @handler: handler callbacks
 * @handler_flags: handler flags
 *
 * Register handler. Enable vga_switcheroo if two vga clients have already
 * registered.
 *
 * Return: 0 on success, -EINVAL if a handler was already registered.
 */
int vga_switcheroo_register_handler(
			  const struct vga_switcheroo_handler *handler,
			  enum vga_switcheroo_handler_flags_t handler_flags)
{
	mutex_lock(&vgasr_mutex);
	if (vgasr_priv.handler) {
		mutex_unlock(&vgasr_mutex);
		return -EINVAL;
	}

	vgasr_priv.handler = handler;
	vgasr_priv.handler_flags = handler_flags;
	if (vga_switcheroo_ready()) {
		pr_info("enabled\n");
		vga_switcheroo_enable();
	}
	mutex_unlock(&vgasr_mutex);
	return 0;
}
EXPORT_SYMBOL(vga_switcheroo_register_handler);

/**
 * vga_switcheroo_unregister_handler() - unregister handler
 *
 * Unregister handler. Disable vga_switcheroo.
 */
void vga_switcheroo_unregister_handler(void)
{
	mutex_lock(&vgasr_mutex);
	mutex_lock(&vgasr_priv.mux_hw_lock);
	vgasr_priv.handler_flags = 0;
	vgasr_priv.handler = NULL;
	if (vgasr_priv.active) {
		pr_info("disabled\n");
		vga_switcheroo_debugfs_fini(&vgasr_priv);
		vgasr_priv.active = false;
	}
	mutex_unlock(&vgasr_priv.mux_hw_lock);
	mutex_unlock(&vgasr_mutex);
}
EXPORT_SYMBOL(vga_switcheroo_unregister_handler);

/**
 * vga_switcheroo_handler_flags() - obtain handler flags
 *
 * Helper for clients to obtain the handler flags bitmask.
 *
 * Return: Handler flags. A value of 0 means that no handler is registered
 * or that the handler has no special capabilities.
 */
enum vga_switcheroo_handler_flags_t vga_switcheroo_handler_flags(void)
{
	return vgasr_priv.handler_flags;
}
EXPORT_SYMBOL(vga_switcheroo_handler_flags);

static int register_client(struct pci_dev *pdev,
			   const struct vga_switcheroo_client_ops *ops,
			   enum vga_switcheroo_client_id id,
			   struct pci_dev *vga_dev,
			   bool active,
			   bool driver_power_control)
{
	struct vga_switcheroo_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	client->pwr_state = VGA_SWITCHEROO_ON;
	client->pdev = pdev;
	client->ops = ops;
	client->id = id;
	client->active = active;
	client->driver_power_control = driver_power_control;
	client->vga_dev = vga_dev;

	mutex_lock(&vgasr_mutex);
	list_add_tail(&client->list, &vgasr_priv.clients);
	if (client_is_vga(client))
		vgasr_priv.registered_clients++;

	if (vga_switcheroo_ready()) {
		pr_info("enabled\n");
		vga_switcheroo_enable();
	}
	mutex_unlock(&vgasr_mutex);
	return 0;
}

/**
 * vga_switcheroo_register_client - register vga client
 * @pdev: client pci device
 * @ops: client callbacks
 * @driver_power_control: whether power state is controlled by the driver's
 *	runtime pm
 *
 * Register vga client (GPU). Enable vga_switcheroo if another GPU and a
 * handler have already registered. The power state of the client is assumed
 * to be ON. Beforehand, vga_switcheroo_client_probe_defer() shall be called
 * to ensure that all prerequisites are met.
 *
 * Return: 0 on success, -ENOMEM on memory allocation error.
 */
int vga_switcheroo_register_client(struct pci_dev *pdev,
				   const struct vga_switcheroo_client_ops *ops,
				   bool driver_power_control)
{
	return register_client(pdev, ops, VGA_SWITCHEROO_UNKNOWN_ID, NULL,
			       pdev == vga_default_device(),
			       driver_power_control);
}
EXPORT_SYMBOL(vga_switcheroo_register_client);

/**
 * vga_switcheroo_register_audio_client - register audio client
 * @pdev: client pci device
 * @ops: client callbacks
 * @vga_dev:  pci device which is bound to current audio client
 *
 * Register audio client (audio device on a GPU). The client is assumed
 * to use runtime PM. Beforehand, vga_switcheroo_client_probe_defer()
 * shall be called to ensure that all prerequisites are met.
 *
 * Return: 0 on success, -ENOMEM on memory allocation error, -EINVAL on getting
 * client id error.
 */
int vga_switcheroo_register_audio_client(struct pci_dev *pdev,
			const struct vga_switcheroo_client_ops *ops,
			struct pci_dev *vga_dev)
{
	enum vga_switcheroo_client_id id = VGA_SWITCHEROO_UNKNOWN_ID;

	/*
	 * if vga_switcheroo has enabled, that mean two GPU clients and also
	 * handler are registered. Get audio client id from bound GPU client
	 * id directly, otherwise, set it as VGA_SWITCHEROO_UNKNOWN_ID,
	 * it will set to correct id in later when vga_switcheroo_enable()
	 * is called.
	 */
	mutex_lock(&vgasr_mutex);
	if (vgasr_priv.active) {
		id = vgasr_priv.handler->get_client_id(vga_dev);
		if (id < 0) {
			mutex_unlock(&vgasr_mutex);
			return -EINVAL;
		}
	}
	mutex_unlock(&vgasr_mutex);

	return register_client(pdev, ops, id | ID_BIT_AUDIO, vga_dev,
			       false, true);
}
EXPORT_SYMBOL(vga_switcheroo_register_audio_client);

static struct vga_switcheroo_client *
find_client_from_pci(struct list_head *head, struct pci_dev *pdev)
{
	struct vga_switcheroo_client *client;

	list_for_each_entry(client, head, list)
		if (client->pdev == pdev)
			return client;
	return NULL;
}

static struct vga_switcheroo_client *
find_client_from_id(struct list_head *head,
		    enum vga_switcheroo_client_id client_id)
{
	struct vga_switcheroo_client *client;

	list_for_each_entry(client, head, list)
		if (client->id == client_id)
			return client;
	return NULL;
}

static struct vga_switcheroo_client *
find_active_client(struct list_head *head)
{
	struct vga_switcheroo_client *client;

	list_for_each_entry(client, head, list)
		if (client->active)
			return client;
	return NULL;
}

/**
 * vga_switcheroo_client_probe_defer() - whether to defer probing a given client
 * @pdev: client pci device
 *
 * Determine whether any prerequisites are not fulfilled to probe a given
 * client. Drivers shall invoke this early on in their ->probe callback
 * and return %-EPROBE_DEFER if it evaluates to %true. Thou shalt not
 * register the client ere thou hast called this.
 *
 * Return: %true if probing should be deferred, otherwise %false.
 */
bool vga_switcheroo_client_probe_defer(struct pci_dev *pdev)
{
	if ((pdev->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
		/*
		 * apple-gmux is needed on pre-retina MacBook Pro
		 * to probe the panel if pdev is the inactive GPU.
		 */
		if (apple_gmux_present() && pdev != vga_default_device() &&
		    !vgasr_priv.handler_flags)
			return true;
	}

	return false;
}
EXPORT_SYMBOL(vga_switcheroo_client_probe_defer);

static enum vga_switcheroo_state
vga_switcheroo_pwr_state(struct vga_switcheroo_client *client)
{
	if (client->driver_power_control)
		if (pm_runtime_enabled(&client->pdev->dev) &&
		    pm_runtime_active(&client->pdev->dev))
			return VGA_SWITCHEROO_ON;
		else
			return VGA_SWITCHEROO_OFF;
	else
		return client->pwr_state;
}

/**
 * vga_switcheroo_get_client_state() - obtain power state of a given client
 * @pdev: client pci device
 *
 * Obtain power state of a given client as seen from vga_switcheroo.
 * The function is only called from hda_intel.c.
 *
 * Return: Power state.
 */
enum vga_switcheroo_state vga_switcheroo_get_client_state(struct pci_dev *pdev)
{
	struct vga_switcheroo_client *client;
	enum vga_switcheroo_state ret;

	mutex_lock(&vgasr_mutex);
	client = find_client_from_pci(&vgasr_priv.clients, pdev);
	if (!client)
		ret = VGA_SWITCHEROO_NOT_FOUND;
	else
		ret = vga_switcheroo_pwr_state(client);
	mutex_unlock(&vgasr_mutex);
	return ret;
}
EXPORT_SYMBOL(vga_switcheroo_get_client_state);

/**
 * vga_switcheroo_unregister_client() - unregister client
 * @pdev: client pci device
 *
 * Unregister client. Disable vga_switcheroo if this is a vga client (GPU).
 */
void vga_switcheroo_unregister_client(struct pci_dev *pdev)
{
	struct vga_switcheroo_client *client;

	mutex_lock(&vgasr_mutex);
	client = find_client_from_pci(&vgasr_priv.clients, pdev);
	if (client) {
		if (client_is_vga(client))
			vgasr_priv.registered_clients--;
		list_del(&client->list);
		kfree(client);
	}
	if (vgasr_priv.active && vgasr_priv.registered_clients < 2) {
		pr_info("disabled\n");
		vga_switcheroo_debugfs_fini(&vgasr_priv);
		vgasr_priv.active = false;
	}
	mutex_unlock(&vgasr_mutex);
}
EXPORT_SYMBOL(vga_switcheroo_unregister_client);

/**
 * vga_switcheroo_client_fb_set() - set framebuffer of a given client
 * @pdev: client pci device
 * @info: framebuffer
 *
 * Set framebuffer of a given client. The console will be remapped to this
 * on switching.
 */
void vga_switcheroo_client_fb_set(struct pci_dev *pdev,
				 struct fb_info *info)
{
	struct vga_switcheroo_client *client;

	mutex_lock(&vgasr_mutex);
	client = find_client_from_pci(&vgasr_priv.clients, pdev);
	if (client)
		client->fb_info = info;
	mutex_unlock(&vgasr_mutex);
}
EXPORT_SYMBOL(vga_switcheroo_client_fb_set);

/**
 * vga_switcheroo_lock_ddc() - temporarily switch DDC lines to a given client
 * @pdev: client pci device
 *
 * Temporarily switch DDC lines to the client identified by @pdev
 * (but leave the outputs otherwise switched to where they are).
 * This allows the inactive client to probe EDID. The DDC lines must
 * afterwards be switched back by calling vga_switcheroo_unlock_ddc(),
 * even if this function returns an error.
 *
 * Return: Previous DDC owner on success or a negative int on error.
 * Specifically, %-ENODEV if no handler has registered or if the handler
 * does not support switching the DDC lines. Also, a negative value
 * returned by the handler is propagated back to the caller.
 * The return value has merely an informational purpose for any caller
 * which might be interested in it. It is acceptable to ignore the return
 * value and simply rely on the result of the subsequent EDID probe,
 * which will be %NULL if DDC switching failed.
 */
int vga_switcheroo_lock_ddc(struct pci_dev *pdev)
{
	enum vga_switcheroo_client_id id;

	mutex_lock(&vgasr_priv.mux_hw_lock);
	if (!vgasr_priv.handler || !vgasr_priv.handler->switch_ddc) {
		vgasr_priv.old_ddc_owner = -ENODEV;
		return -ENODEV;
	}

	id = vgasr_priv.handler->get_client_id(pdev);
	vgasr_priv.old_ddc_owner = vgasr_priv.handler->switch_ddc(id);
	return vgasr_priv.old_ddc_owner;
}
EXPORT_SYMBOL(vga_switcheroo_lock_ddc);

/**
 * vga_switcheroo_unlock_ddc() - switch DDC lines back to previous owner
 * @pdev: client pci device
 *
 * Switch DDC lines back to the previous owner after calling
 * vga_switcheroo_lock_ddc(). This must be called even if
 * vga_switcheroo_lock_ddc() returned an error.
 *
 * Return: Previous DDC owner on success (i.e. the client identifier of @pdev)
 * or a negative int on error.
 * Specifically, %-ENODEV if no handler has registered or if the handler
 * does not support switching the DDC lines. Also, a negative value
 * returned by the handler is propagated back to the caller.
 * Finally, invoking this function without calling vga_switcheroo_lock_ddc()
 * first is not allowed and will result in %-EINVAL.
 */
int vga_switcheroo_unlock_ddc(struct pci_dev *pdev)
{
	enum vga_switcheroo_client_id id;
	int ret = vgasr_priv.old_ddc_owner;

	if (WARN_ON_ONCE(!mutex_is_locked(&vgasr_priv.mux_hw_lock)))
		return -EINVAL;

	if (vgasr_priv.old_ddc_owner >= 0) {
		id = vgasr_priv.handler->get_client_id(pdev);
		if (vgasr_priv.old_ddc_owner != id)
			ret = vgasr_priv.handler->switch_ddc(
						     vgasr_priv.old_ddc_owner);
	}
	mutex_unlock(&vgasr_priv.mux_hw_lock);
	return ret;
}
EXPORT_SYMBOL(vga_switcheroo_unlock_ddc);

/**
 * DOC: Manual switching and manual power control
 *
 * In this mode of use, the file /sys/kernel/debug/vgaswitcheroo/switch
 * can be read to retrieve the current vga_switcheroo state and commands
 * can be written to it to change the state. The file appears as soon as
 * two GPU drivers and one handler have registered with vga_switcheroo.
 * The following commands are understood:
 *
 * * OFF: Power off the device not in use.
 * * ON: Power on the device not in use.
 * * IGD: Switch to the integrated graphics device.
 *   Power on the integrated GPU if necessary, power off the discrete GPU.
 *   Prerequisite is that no user space processes (e.g. Xorg, alsactl)
 *   have opened device files of the GPUs or the audio client. If the
 *   switch fails, the user may invoke lsof(8) or fuser(1) on /dev/dri/
 *   and /dev/snd/controlC1 to identify processes blocking the switch.
 * * DIS: Switch to the discrete graphics device.
 * * DIGD: Delayed switch to the integrated graphics device.
 *   This will perform the switch once the last user space process has
 *   closed the device files of the GPUs and the audio client.
 * * DDIS: Delayed switch to the discrete graphics device.
 * * MIGD: Mux-only switch to the integrated graphics device.
 *   Does not remap console or change the power state of either gpu.
 *   If the integrated GPU is currently off, the screen will turn black.
 *   If it is on, the screen will show whatever happens to be in VRAM.
 *   Either way, the user has to blindly enter the command to switch back.
 * * MDIS: Mux-only switch to the discrete graphics device.
 *
 * For GPUs whose power state is controlled by the driver's runtime pm,
 * the ON and OFF commands are a no-op (see next section).
 *
 * For muxless machines, the IGD/DIS, DIGD/DDIS and MIGD/MDIS commands
 * should not be used.
 */

static int vga_switcheroo_show(struct seq_file *m, void *v)
{
	struct vga_switcheroo_client *client;
	int i = 0;

	mutex_lock(&vgasr_mutex);
	list_for_each_entry(client, &vgasr_priv.clients, list) {
		seq_printf(m, "%d:%s%s:%c:%s%s:%s\n", i,
			   client_id(client) == VGA_SWITCHEROO_DIS ? "DIS" :
								     "IGD",
			   client_is_vga(client) ? "" : "-Audio",
			   client->active ? '+' : ' ',
			   client->driver_power_control ? "Dyn" : "",
			   vga_switcheroo_pwr_state(client) ? "Pwr" : "Off",
			   pci_name(client->pdev));
		i++;
	}
	mutex_unlock(&vgasr_mutex);
	return 0;
}

static int vga_switcheroo_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, vga_switcheroo_show, NULL);
}

static int vga_switchon(struct vga_switcheroo_client *client)
{
	if (client->driver_power_control)
		return 0;
	if (vgasr_priv.handler->power_state)
		vgasr_priv.handler->power_state(client->id, VGA_SWITCHEROO_ON);
	/* call the driver callback to turn on device */
	client->ops->set_gpu_state(client->pdev, VGA_SWITCHEROO_ON);
	client->pwr_state = VGA_SWITCHEROO_ON;
	return 0;
}

static int vga_switchoff(struct vga_switcheroo_client *client)
{
	if (client->driver_power_control)
		return 0;
	/* call the driver callback to turn off device */
	client->ops->set_gpu_state(client->pdev, VGA_SWITCHEROO_OFF);
	if (vgasr_priv.handler->power_state)
		vgasr_priv.handler->power_state(client->id, VGA_SWITCHEROO_OFF);
	client->pwr_state = VGA_SWITCHEROO_OFF;
	return 0;
}

static void set_audio_state(enum vga_switcheroo_client_id id,
			    enum vga_switcheroo_state state)
{
	struct vga_switcheroo_client *client;

	client = find_client_from_id(&vgasr_priv.clients, id | ID_BIT_AUDIO);
	if (client)
		client->ops->set_gpu_state(client->pdev, state);
}

/* stage one happens before delay */
static int vga_switchto_stage1(struct vga_switcheroo_client *new_client)
{
	struct vga_switcheroo_client *active;

	active = find_active_client(&vgasr_priv.clients);
	if (!active)
		return 0;

	if (vga_switcheroo_pwr_state(new_client) == VGA_SWITCHEROO_OFF)
		vga_switchon(new_client);

	vga_set_default_device(new_client->pdev);
	return 0;
}

/* post delay */
static int vga_switchto_stage2(struct vga_switcheroo_client *new_client)
{
	int ret;
	struct vga_switcheroo_client *active;

	active = find_active_client(&vgasr_priv.clients);
	if (!active)
		return 0;

	active->active = false;

	/* let HDA controller autosuspend if GPU uses driver power control */
	if (!active->driver_power_control)
		set_audio_state(active->id, VGA_SWITCHEROO_OFF);

	if (new_client->fb_info) {
		struct fb_event event;

		console_lock();
		event.info = new_client->fb_info;
		fb_notifier_call_chain(FB_EVENT_REMAP_ALL_CONSOLE, &event);
		console_unlock();
	}

	mutex_lock(&vgasr_priv.mux_hw_lock);
	ret = vgasr_priv.handler->switchto(new_client->id);
	mutex_unlock(&vgasr_priv.mux_hw_lock);
	if (ret)
		return ret;

	if (new_client->ops->reprobe)
		new_client->ops->reprobe(new_client->pdev);

	if (vga_switcheroo_pwr_state(active) == VGA_SWITCHEROO_ON)
		vga_switchoff(active);

	/* let HDA controller autoresume if GPU uses driver power control */
	if (!new_client->driver_power_control)
		set_audio_state(new_client->id, VGA_SWITCHEROO_ON);

	new_client->active = true;
	return 0;
}

static bool check_can_switch(void)
{
	struct vga_switcheroo_client *client;

	list_for_each_entry(client, &vgasr_priv.clients, list) {
		if (!client->ops->can_switch(client->pdev)) {
			pr_err("client %x refused switch\n", client->id);
			return false;
		}
	}
	return true;
}

static ssize_t
vga_switcheroo_debugfs_write(struct file *filp, const char __user *ubuf,
			     size_t cnt, loff_t *ppos)
{
	char usercmd[64];
	int ret;
	bool delay = false, can_switch;
	bool just_mux = false;
	enum vga_switcheroo_client_id client_id = VGA_SWITCHEROO_UNKNOWN_ID;
	struct vga_switcheroo_client *client = NULL;

	if (cnt > 63)
		cnt = 63;

	if (copy_from_user(usercmd, ubuf, cnt))
		return -EFAULT;

	mutex_lock(&vgasr_mutex);

	if (!vgasr_priv.active) {
		cnt = -EINVAL;
		goto out;
	}

	/* pwr off the device not in use */
	if (strncmp(usercmd, "OFF", 3) == 0) {
		list_for_each_entry(client, &vgasr_priv.clients, list) {
			if (client->active || client_is_audio(client))
				continue;
			if (client->driver_power_control)
				continue;
			set_audio_state(client->id, VGA_SWITCHEROO_OFF);
			if (client->pwr_state == VGA_SWITCHEROO_ON)
				vga_switchoff(client);
		}
		goto out;
	}
	/* pwr on the device not in use */
	if (strncmp(usercmd, "ON", 2) == 0) {
		list_for_each_entry(client, &vgasr_priv.clients, list) {
			if (client->active || client_is_audio(client))
				continue;
			if (client->driver_power_control)
				continue;
			if (client->pwr_state == VGA_SWITCHEROO_OFF)
				vga_switchon(client);
			set_audio_state(client->id, VGA_SWITCHEROO_ON);
		}
		goto out;
	}

	/* request a delayed switch - test can we switch now */
	if (strncmp(usercmd, "DIGD", 4) == 0) {
		client_id = VGA_SWITCHEROO_IGD;
		delay = true;
	}

	if (strncmp(usercmd, "DDIS", 4) == 0) {
		client_id = VGA_SWITCHEROO_DIS;
		delay = true;
	}

	if (strncmp(usercmd, "IGD", 3) == 0)
		client_id = VGA_SWITCHEROO_IGD;

	if (strncmp(usercmd, "DIS", 3) == 0)
		client_id = VGA_SWITCHEROO_DIS;

	if (strncmp(usercmd, "MIGD", 4) == 0) {
		just_mux = true;
		client_id = VGA_SWITCHEROO_IGD;
	}
	if (strncmp(usercmd, "MDIS", 4) == 0) {
		just_mux = true;
		client_id = VGA_SWITCHEROO_DIS;
	}

	if (client_id == VGA_SWITCHEROO_UNKNOWN_ID)
		goto out;
	client = find_client_from_id(&vgasr_priv.clients, client_id);
	if (!client)
		goto out;

	vgasr_priv.delayed_switch_active = false;

	if (just_mux) {
		mutex_lock(&vgasr_priv.mux_hw_lock);
		ret = vgasr_priv.handler->switchto(client_id);
		mutex_unlock(&vgasr_priv.mux_hw_lock);
		goto out;
	}

	if (client->active)
		goto out;

	/* okay we want a switch - test if devices are willing to switch */
	can_switch = check_can_switch();

	if (can_switch == false && delay == false)
		goto out;

	if (can_switch) {
		ret = vga_switchto_stage1(client);
		if (ret)
			pr_err("switching failed stage 1 %d\n", ret);

		ret = vga_switchto_stage2(client);
		if (ret)
			pr_err("switching failed stage 2 %d\n", ret);

	} else {
		pr_info("setting delayed switch to client %d\n", client->id);
		vgasr_priv.delayed_switch_active = true;
		vgasr_priv.delayed_client_id = client_id;

		ret = vga_switchto_stage1(client);
		if (ret)
			pr_err("delayed switching stage 1 failed %d\n", ret);
	}

out:
	mutex_unlock(&vgasr_mutex);
	return cnt;
}

static const struct file_operations vga_switcheroo_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = vga_switcheroo_debugfs_open,
	.write = vga_switcheroo_debugfs_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void vga_switcheroo_debugfs_fini(struct vgasr_priv *priv)
{
	debugfs_remove(priv->switch_file);
	priv->switch_file = NULL;

	debugfs_remove(priv->debugfs_root);
	priv->debugfs_root = NULL;
}

static int vga_switcheroo_debugfs_init(struct vgasr_priv *priv)
{
	static const char mp[] = "/sys/kernel/debug";

	/* already initialised */
	if (priv->debugfs_root)
		return 0;
	priv->debugfs_root = debugfs_create_dir("vgaswitcheroo", NULL);

	if (!priv->debugfs_root) {
		pr_err("Cannot create %s/vgaswitcheroo\n", mp);
		goto fail;
	}

	priv->switch_file = debugfs_create_file("switch", 0644,
						priv->debugfs_root, NULL,
						&vga_switcheroo_debugfs_fops);
	if (!priv->switch_file) {
		pr_err("cannot create %s/vgaswitcheroo/switch\n", mp);
		goto fail;
	}
	return 0;
fail:
	vga_switcheroo_debugfs_fini(priv);
	return -1;
}

/**
 * vga_switcheroo_process_delayed_switch() - helper for delayed switching
 *
 * Process a delayed switch if one is pending. DRM drivers should call this
 * from their ->lastclose callback.
 *
 * Return: 0 on success. -EINVAL if no delayed switch is pending, if the client
 * has unregistered in the meantime or if there are other clients blocking the
 * switch. If the actual switch fails, an error is reported and 0 is returned.
 */
int vga_switcheroo_process_delayed_switch(void)
{
	struct vga_switcheroo_client *client;
	int ret;
	int err = -EINVAL;

	mutex_lock(&vgasr_mutex);
	if (!vgasr_priv.delayed_switch_active)
		goto err;

	pr_info("processing delayed switch to %d\n",
		vgasr_priv.delayed_client_id);

	client = find_client_from_id(&vgasr_priv.clients,
				     vgasr_priv.delayed_client_id);
	if (!client || !check_can_switch())
		goto err;

	ret = vga_switchto_stage2(client);
	if (ret)
		pr_err("delayed switching failed stage 2 %d\n", ret);

	vgasr_priv.delayed_switch_active = false;
	err = 0;
err:
	mutex_unlock(&vgasr_mutex);
	return err;
}
EXPORT_SYMBOL(vga_switcheroo_process_delayed_switch);

/**
 * DOC: Driver power control
 *
 * In this mode of use, the discrete GPU automatically powers up and down at
 * the discretion of the driver's runtime pm. On muxed machines, the user may
 * still influence the muxer state by way of the debugfs interface, however
 * the ON and OFF commands become a no-op for the discrete GPU.
 *
 * This mode is the default on Nvidia HybridPower/Optimus and ATI PowerXpress.
 * Specifying nouveau.runpm=0, radeon.runpm=0 or amdgpu.runpm=0 on the kernel
 * command line disables it.
 *
 * After the GPU has been suspended, the handler needs to be called to cut
 * power to the GPU. Likewise it needs to reinstate power before the GPU
 * can resume. This is achieved by vga_switcheroo_init_domain_pm_ops(),
 * which augments the GPU's suspend/resume functions by the requisite
 * calls to the handler.
 *
 * When the audio device resumes, the GPU needs to be woken. This is achieved
 * by a PCI quirk which calls device_link_add() to declare a dependency on the
 * GPU. That way, the GPU is kept awake whenever and as long as the audio
 * device is in use.
 *
 * On muxed machines, if the mux is initially switched to the discrete GPU,
 * the user ends up with a black screen when the GPU powers down after boot.
 * As a workaround, the mux is forced to the integrated GPU on runtime suspend,
 * cf. https://bugs.freedesktop.org/show_bug.cgi?id=75917
 */

static void vga_switcheroo_power_switch(struct pci_dev *pdev,
					enum vga_switcheroo_state state)
{
	struct vga_switcheroo_client *client;

	if (!vgasr_priv.handler->power_state)
		return;

	client = find_client_from_pci(&vgasr_priv.clients, pdev);
	if (!client)
		return;

	if (!client->driver_power_control)
		return;

	vgasr_priv.handler->power_state(client->id, state);
}

/* switcheroo power domain */
static int vga_switcheroo_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	ret = dev->bus->pm->runtime_suspend(dev);
	if (ret)
		return ret;
	mutex_lock(&vgasr_mutex);
	if (vgasr_priv.handler->switchto) {
		mutex_lock(&vgasr_priv.mux_hw_lock);
		vgasr_priv.handler->switchto(VGA_SWITCHEROO_IGD);
		mutex_unlock(&vgasr_priv.mux_hw_lock);
	}
	pci_bus_set_current_state(pdev->bus, PCI_D3cold);
	vga_switcheroo_power_switch(pdev, VGA_SWITCHEROO_OFF);
	mutex_unlock(&vgasr_mutex);
	return 0;
}

static int vga_switcheroo_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	mutex_lock(&vgasr_mutex);
	vga_switcheroo_power_switch(pdev, VGA_SWITCHEROO_ON);
	mutex_unlock(&vgasr_mutex);
	pci_wakeup_bus(pdev->bus);
	ret = dev->bus->pm->runtime_resume(dev);
	if (ret)
		return ret;

	return 0;
}

/**
 * vga_switcheroo_init_domain_pm_ops() - helper for driver power control
 * @dev: vga client device
 * @domain: power domain
 *
 * Helper for GPUs whose power state is controlled by the driver's runtime pm.
 * After the GPU has been suspended, the handler needs to be called to cut
 * power to the GPU. Likewise it needs to reinstate power before the GPU
 * can resume. To this end, this helper augments the suspend/resume functions
 * by the requisite calls to the handler. It needs only be called on platforms
 * where the power switch is separate to the device being powered down.
 */
int vga_switcheroo_init_domain_pm_ops(struct device *dev,
				      struct dev_pm_domain *domain)
{
	/* copy over all the bus versions */
	if (dev->bus && dev->bus->pm) {
		domain->ops = *dev->bus->pm;
		domain->ops.runtime_suspend = vga_switcheroo_runtime_suspend;
		domain->ops.runtime_resume = vga_switcheroo_runtime_resume;

		dev_pm_domain_set(dev, domain);
		return 0;
	}
	dev_pm_domain_set(dev, NULL);
	return -EINVAL;
}
EXPORT_SYMBOL(vga_switcheroo_init_domain_pm_ops);

void vga_switcheroo_fini_domain_pm_ops(struct device *dev)
{
	dev_pm_domain_set(dev, NULL);
}
EXPORT_SYMBOL(vga_switcheroo_fini_domain_pm_ops);
