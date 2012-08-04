/*
 * Copyright (c) 2010 Red Hat Inc.
 * Author : Dave Airlie <airlied@redhat.com>
 *
 *
 * Licensed under GPLv2
 *
 * vga_switcheroo.c - Support for laptop with dual GPU using one set of outputs

 Switcher interface - methods require for ATPX and DCM
 - switchto - this throws the output MUX switch
 - discrete_set_power - sets the power state for the discrete card

 GPU driver interface
 - set_gpu_state - this should do the equiv of s/r for the card
		  - this should *not* set the discrete power state
 - switch_check  - check if the device is in a position to switch now
 */

#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/fb.h>

#include <linux/pci.h>
#include <linux/vga_switcheroo.h>

#include <linux/vgaarb.h>

struct vga_switcheroo_client {
	struct pci_dev *pdev;
	struct fb_info *fb_info;
	int pwr_state;
	const struct vga_switcheroo_client_ops *ops;
	int id;
	bool active;
	struct list_head list;
};

static DEFINE_MUTEX(vgasr_mutex);

struct vgasr_priv {

	bool active;
	bool delayed_switch_active;
	enum vga_switcheroo_client_id delayed_client_id;

	struct dentry *debugfs_root;
	struct dentry *switch_file;

	int registered_clients;
	struct list_head clients;

	struct vga_switcheroo_handler *handler;
};

#define ID_BIT_AUDIO		0x100
#define client_is_audio(c)	((c)->id & ID_BIT_AUDIO)
#define client_is_vga(c)	((c)->id == -1 || !client_is_audio(c))
#define client_id(c)		((c)->id & ~ID_BIT_AUDIO)

static int vga_switcheroo_debugfs_init(struct vgasr_priv *priv);
static void vga_switcheroo_debugfs_fini(struct vgasr_priv *priv);

/* only one switcheroo per system */
static struct vgasr_priv vgasr_priv = {
	.clients = LIST_HEAD_INIT(vgasr_priv.clients),
};

int vga_switcheroo_register_handler(struct vga_switcheroo_handler *handler)
{
	mutex_lock(&vgasr_mutex);
	if (vgasr_priv.handler) {
		mutex_unlock(&vgasr_mutex);
		return -EINVAL;
	}

	vgasr_priv.handler = handler;
	mutex_unlock(&vgasr_mutex);
	return 0;
}
EXPORT_SYMBOL(vga_switcheroo_register_handler);

void vga_switcheroo_unregister_handler(void)
{
	mutex_lock(&vgasr_mutex);
	vgasr_priv.handler = NULL;
	mutex_unlock(&vgasr_mutex);
}
EXPORT_SYMBOL(vga_switcheroo_unregister_handler);

static void vga_switcheroo_enable(void)
{
	int ret;
	struct vga_switcheroo_client *client;

	/* call the handler to init */
	vgasr_priv.handler->init();

	list_for_each_entry(client, &vgasr_priv.clients, list) {
		if (client->id != -1)
			continue;
		ret = vgasr_priv.handler->get_client_id(client->pdev);
		if (ret < 0)
			return;

		client->id = ret;
	}
	vga_switcheroo_debugfs_init(&vgasr_priv);
	vgasr_priv.active = true;
}

static int register_client(struct pci_dev *pdev,
			   const struct vga_switcheroo_client_ops *ops,
			   int id, bool active)
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

	mutex_lock(&vgasr_mutex);
	list_add_tail(&client->list, &vgasr_priv.clients);
	if (client_is_vga(client))
		vgasr_priv.registered_clients++;

	/* if we get two clients + handler */
	if (!vgasr_priv.active &&
	    vgasr_priv.registered_clients == 2 && vgasr_priv.handler) {
		printk(KERN_INFO "vga_switcheroo: enabled\n");
		vga_switcheroo_enable();
	}
	mutex_unlock(&vgasr_mutex);
	return 0;
}

int vga_switcheroo_register_client(struct pci_dev *pdev,
				   const struct vga_switcheroo_client_ops *ops)
{
	return register_client(pdev, ops, -1,
			       pdev == vga_default_device());
}
EXPORT_SYMBOL(vga_switcheroo_register_client);

int vga_switcheroo_register_audio_client(struct pci_dev *pdev,
					 const struct vga_switcheroo_client_ops *ops,
					 int id, bool active)
{
	return register_client(pdev, ops, id | ID_BIT_AUDIO, active);
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
find_client_from_id(struct list_head *head, int client_id)
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
		if (client->active && client_is_vga(client))
			return client;
	return NULL;
}

int vga_switcheroo_get_client_state(struct pci_dev *pdev)
{
	struct vga_switcheroo_client *client;

	client = find_client_from_pci(&vgasr_priv.clients, pdev);
	if (!client)
		return VGA_SWITCHEROO_NOT_FOUND;
	if (!vgasr_priv.active)
		return VGA_SWITCHEROO_INIT;
	return client->pwr_state;
}
EXPORT_SYMBOL(vga_switcheroo_get_client_state);

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
		printk(KERN_INFO "vga_switcheroo: disabled\n");
		vga_switcheroo_debugfs_fini(&vgasr_priv);
		vgasr_priv.active = false;
	}
	mutex_unlock(&vgasr_mutex);
}
EXPORT_SYMBOL(vga_switcheroo_unregister_client);

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

static int vga_switcheroo_show(struct seq_file *m, void *v)
{
	struct vga_switcheroo_client *client;
	int i = 0;
	mutex_lock(&vgasr_mutex);
	list_for_each_entry(client, &vgasr_priv.clients, list) {
		seq_printf(m, "%d:%s%s:%c:%s:%s\n", i,
			   client_id(client) == VGA_SWITCHEROO_DIS ? "DIS" : "IGD",
			   client_is_vga(client) ? "" : "-Audio",
			   client->active ? '+' : ' ',
			   client->pwr_state ? "Pwr" : "Off",
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
	if (vgasr_priv.handler->power_state)
		vgasr_priv.handler->power_state(client->id, VGA_SWITCHEROO_ON);
	/* call the driver callback to turn on device */
	client->ops->set_gpu_state(client->pdev, VGA_SWITCHEROO_ON);
	client->pwr_state = VGA_SWITCHEROO_ON;
	return 0;
}

static int vga_switchoff(struct vga_switcheroo_client *client)
{
	/* call the driver callback to turn off device */
	client->ops->set_gpu_state(client->pdev, VGA_SWITCHEROO_OFF);
	if (vgasr_priv.handler->power_state)
		vgasr_priv.handler->power_state(client->id, VGA_SWITCHEROO_OFF);
	client->pwr_state = VGA_SWITCHEROO_OFF;
	return 0;
}

static void set_audio_state(int id, int state)
{
	struct vga_switcheroo_client *client;

	client = find_client_from_id(&vgasr_priv.clients, id | ID_BIT_AUDIO);
	if (client && client->pwr_state != state) {
		client->ops->set_gpu_state(client->pdev, state);
		client->pwr_state = state;
	}
}

/* stage one happens before delay */
static int vga_switchto_stage1(struct vga_switcheroo_client *new_client)
{
	struct vga_switcheroo_client *active;

	active = find_active_client(&vgasr_priv.clients);
	if (!active)
		return 0;

	if (new_client->pwr_state == VGA_SWITCHEROO_OFF)
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

	set_audio_state(active->id, VGA_SWITCHEROO_OFF);

	if (new_client->fb_info) {
		struct fb_event event;
		event.info = new_client->fb_info;
		fb_notifier_call_chain(FB_EVENT_REMAP_ALL_CONSOLE, &event);
	}

	ret = vgasr_priv.handler->switchto(new_client->id);
	if (ret)
		return ret;

	if (new_client->ops->reprobe)
		new_client->ops->reprobe(new_client->pdev);

	if (active->pwr_state == VGA_SWITCHEROO_ON)
		vga_switchoff(active);

	set_audio_state(new_client->id, VGA_SWITCHEROO_ON);

	new_client->active = true;
	return 0;
}

static bool check_can_switch(void)
{
	struct vga_switcheroo_client *client;

	list_for_each_entry(client, &vgasr_priv.clients, list) {
		if (!client->ops->can_switch(client->pdev)) {
			printk(KERN_ERR "vga_switcheroo: client %x refused switch\n", client->id);
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
	const char *pdev_name;
	int ret;
	bool delay = false, can_switch;
	bool just_mux = false;
	int client_id = -1;
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

	if (client_id == -1)
		goto out;
	client = find_client_from_id(&vgasr_priv.clients, client_id);
	if (!client)
		goto out;

	vgasr_priv.delayed_switch_active = false;

	if (just_mux) {
		ret = vgasr_priv.handler->switchto(client_id);
		goto out;
	}

	if (client->active)
		goto out;

	/* okay we want a switch - test if devices are willing to switch */
	can_switch = check_can_switch();

	if (can_switch == false && delay == false)
		goto out;

	if (can_switch) {
		pdev_name = pci_name(client->pdev);
		ret = vga_switchto_stage1(client);
		if (ret)
			printk(KERN_ERR "vga_switcheroo: switching failed stage 1 %d\n", ret);

		ret = vga_switchto_stage2(client);
		if (ret)
			printk(KERN_ERR "vga_switcheroo: switching failed stage 2 %d\n", ret);

	} else {
		printk(KERN_INFO "vga_switcheroo: setting delayed switch to client %d\n", client->id);
		vgasr_priv.delayed_switch_active = true;
		vgasr_priv.delayed_client_id = client_id;

		ret = vga_switchto_stage1(client);
		if (ret)
			printk(KERN_ERR "vga_switcheroo: delayed switching stage 1 failed %d\n", ret);
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
	if (priv->switch_file) {
		debugfs_remove(priv->switch_file);
		priv->switch_file = NULL;
	}
	if (priv->debugfs_root) {
		debugfs_remove(priv->debugfs_root);
		priv->debugfs_root = NULL;
	}
}

static int vga_switcheroo_debugfs_init(struct vgasr_priv *priv)
{
	/* already initialised */
	if (priv->debugfs_root)
		return 0;
	priv->debugfs_root = debugfs_create_dir("vgaswitcheroo", NULL);

	if (!priv->debugfs_root) {
		printk(KERN_ERR "vga_switcheroo: Cannot create /sys/kernel/debug/vgaswitcheroo\n");
		goto fail;
	}

	priv->switch_file = debugfs_create_file("switch", 0644,
						priv->debugfs_root, NULL, &vga_switcheroo_debugfs_fops);
	if (!priv->switch_file) {
		printk(KERN_ERR "vga_switcheroo: cannot create /sys/kernel/debug/vgaswitcheroo/switch\n");
		goto fail;
	}
	return 0;
fail:
	vga_switcheroo_debugfs_fini(priv);
	return -1;
}

int vga_switcheroo_process_delayed_switch(void)
{
	struct vga_switcheroo_client *client;
	const char *pdev_name;
	int ret;
	int err = -EINVAL;

	mutex_lock(&vgasr_mutex);
	if (!vgasr_priv.delayed_switch_active)
		goto err;

	printk(KERN_INFO "vga_switcheroo: processing delayed switch to %d\n", vgasr_priv.delayed_client_id);

	client = find_client_from_id(&vgasr_priv.clients,
				     vgasr_priv.delayed_client_id);
	if (!client || !check_can_switch())
		goto err;

	pdev_name = pci_name(client->pdev);
	ret = vga_switchto_stage2(client);
	if (ret)
		printk(KERN_ERR "vga_switcheroo: delayed switching failed stage 2 %d\n", ret);

	vgasr_priv.delayed_switch_active = false;
	err = 0;
err:
	mutex_unlock(&vgasr_mutex);
	return err;
}
EXPORT_SYMBOL(vga_switcheroo_process_delayed_switch);

