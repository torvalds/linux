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

struct vga_switcheroo_client {
	struct pci_dev *pdev;
	struct fb_info *fb_info;
	int pwr_state;
	void (*set_gpu_state)(struct pci_dev *pdev, enum vga_switcheroo_state);
	void (*reprobe)(struct pci_dev *pdev);
	bool (*can_switch)(struct pci_dev *pdev);
	int id;
	bool active;
};

static DEFINE_MUTEX(vgasr_mutex);

struct vgasr_priv {

	bool active;
	bool delayed_switch_active;
	enum vga_switcheroo_client_id delayed_client_id;

	struct dentry *debugfs_root;
	struct dentry *switch_file;

	int registered_clients;
	struct vga_switcheroo_client clients[VGA_SWITCHEROO_MAX_CLIENTS];

	struct vga_switcheroo_handler *handler;
};

static int vga_switcheroo_debugfs_init(struct vgasr_priv *priv);
static void vga_switcheroo_debugfs_fini(struct vgasr_priv *priv);

/* only one switcheroo per system */
static struct vgasr_priv vgasr_priv;

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
	int i;
	int ret;
	/* call the handler to init */
	vgasr_priv.handler->init();

	for (i = 0; i < VGA_SWITCHEROO_MAX_CLIENTS; i++) {
		ret = vgasr_priv.handler->get_client_id(vgasr_priv.clients[i].pdev);
		if (ret < 0)
			return;

		vgasr_priv.clients[i].id = ret;
	}
	vga_switcheroo_debugfs_init(&vgasr_priv);
	vgasr_priv.active = true;
}

int vga_switcheroo_register_client(struct pci_dev *pdev,
				   void (*set_gpu_state)(struct pci_dev *pdev, enum vga_switcheroo_state),
				   void (*reprobe)(struct pci_dev *pdev),
				   bool (*can_switch)(struct pci_dev *pdev))
{
	int index;

	mutex_lock(&vgasr_mutex);
	/* don't do IGD vs DIS here */
	if (vgasr_priv.registered_clients & 1)
		index = 1;
	else
		index = 0;

	vgasr_priv.clients[index].pwr_state = VGA_SWITCHEROO_ON;
	vgasr_priv.clients[index].pdev = pdev;
	vgasr_priv.clients[index].set_gpu_state = set_gpu_state;
	vgasr_priv.clients[index].reprobe = reprobe;
	vgasr_priv.clients[index].can_switch = can_switch;
	vgasr_priv.clients[index].id = -1;
	if (pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW)
		vgasr_priv.clients[index].active = true;

	vgasr_priv.registered_clients |= (1 << index);

	/* if we get two clients + handler */
	if (vgasr_priv.registered_clients == 0x3 && vgasr_priv.handler) {
		printk(KERN_INFO "vga_switcheroo: enabled\n");
		vga_switcheroo_enable();
	}
	mutex_unlock(&vgasr_mutex);
	return 0;
}
EXPORT_SYMBOL(vga_switcheroo_register_client);

void vga_switcheroo_unregister_client(struct pci_dev *pdev)
{
	int i;

	mutex_lock(&vgasr_mutex);
	for (i = 0; i < VGA_SWITCHEROO_MAX_CLIENTS; i++) {
		if (vgasr_priv.clients[i].pdev == pdev) {
			vgasr_priv.registered_clients &= ~(1 << i);
			break;
		}
	}

	printk(KERN_INFO "vga_switcheroo: disabled\n");
	vga_switcheroo_debugfs_fini(&vgasr_priv);
	vgasr_priv.active = false;
	mutex_unlock(&vgasr_mutex);
}
EXPORT_SYMBOL(vga_switcheroo_unregister_client);

void vga_switcheroo_client_fb_set(struct pci_dev *pdev,
				 struct fb_info *info)
{
	int i;

	mutex_lock(&vgasr_mutex);
	for (i = 0; i < VGA_SWITCHEROO_MAX_CLIENTS; i++) {
		if (vgasr_priv.clients[i].pdev == pdev) {
			vgasr_priv.clients[i].fb_info = info;
			break;
		}
	}
	mutex_unlock(&vgasr_mutex);
}
EXPORT_SYMBOL(vga_switcheroo_client_fb_set);

static int vga_switcheroo_show(struct seq_file *m, void *v)
{
	int i;
	mutex_lock(&vgasr_mutex);
	for (i = 0; i < VGA_SWITCHEROO_MAX_CLIENTS; i++) {
		seq_printf(m, "%d:%s:%c:%s:%s\n", i,
			   vgasr_priv.clients[i].id == VGA_SWITCHEROO_DIS ? "DIS" : "IGD",
			   vgasr_priv.clients[i].active ? '+' : ' ',
			   vgasr_priv.clients[i].pwr_state ? "Pwr" : "Off",
			   pci_name(vgasr_priv.clients[i].pdev));
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
	client->set_gpu_state(client->pdev, VGA_SWITCHEROO_ON);
	client->pwr_state = VGA_SWITCHEROO_ON;
	return 0;
}

static int vga_switchoff(struct vga_switcheroo_client *client)
{
	/* call the driver callback to turn off device */
	client->set_gpu_state(client->pdev, VGA_SWITCHEROO_OFF);
	if (vgasr_priv.handler->power_state)
		vgasr_priv.handler->power_state(client->id, VGA_SWITCHEROO_OFF);
	client->pwr_state = VGA_SWITCHEROO_OFF;
	return 0;
}

/* stage one happens before delay */
static int vga_switchto_stage1(struct vga_switcheroo_client *new_client)
{
	int ret;
	int i;
	struct vga_switcheroo_client *active = NULL;

	if (new_client->active == true)
		return 0;

	for (i = 0; i < VGA_SWITCHEROO_MAX_CLIENTS; i++) {
		if (vgasr_priv.clients[i].active == true) {
			active = &vgasr_priv.clients[i];
			break;
		}
	}
	if (!active)
		return 0;

	/* power up the first device */
	ret = pci_enable_device(new_client->pdev);
	if (ret)
		return ret;

	if (new_client->pwr_state == VGA_SWITCHEROO_OFF)
		vga_switchon(new_client);

	/* swap shadow resource to denote boot VGA device has changed so X starts on new device */
	active->pdev->resource[PCI_ROM_RESOURCE].flags &= ~IORESOURCE_ROM_SHADOW;
	new_client->pdev->resource[PCI_ROM_RESOURCE].flags |= IORESOURCE_ROM_SHADOW;
	return 0;
}

/* post delay */
static int vga_switchto_stage2(struct vga_switcheroo_client *new_client)
{
	int ret;
	int i;
	struct vga_switcheroo_client *active = NULL;

	for (i = 0; i < VGA_SWITCHEROO_MAX_CLIENTS; i++) {
		if (vgasr_priv.clients[i].active == true) {
			active = &vgasr_priv.clients[i];
			break;
		}
	}
	if (!active)
		return 0;

	active->active = false;

	if (new_client->fb_info) {
		struct fb_event event;
		event.info = new_client->fb_info;
		fb_notifier_call_chain(FB_EVENT_REMAP_ALL_CONSOLE, &event);
	}

	ret = vgasr_priv.handler->switchto(new_client->id);
	if (ret)
		return ret;

	if (new_client->reprobe)
		new_client->reprobe(new_client->pdev);

	if (active->pwr_state == VGA_SWITCHEROO_ON)
		vga_switchoff(active);

	new_client->active = true;
	return 0;
}

static ssize_t
vga_switcheroo_debugfs_write(struct file *filp, const char __user *ubuf,
			     size_t cnt, loff_t *ppos)
{
	char usercmd[64];
	const char *pdev_name;
	int i, ret;
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
		for (i = 0; i < VGA_SWITCHEROO_MAX_CLIENTS; i++) {
			if (vgasr_priv.clients[i].active)
				continue;
			if (vgasr_priv.clients[i].pwr_state == VGA_SWITCHEROO_ON)
				vga_switchoff(&vgasr_priv.clients[i]);
		}
		goto out;
	}
	/* pwr on the device not in use */
	if (strncmp(usercmd, "ON", 2) == 0) {
		for (i = 0; i < VGA_SWITCHEROO_MAX_CLIENTS; i++) {
			if (vgasr_priv.clients[i].active)
				continue;
			if (vgasr_priv.clients[i].pwr_state == VGA_SWITCHEROO_OFF)
				vga_switchon(&vgasr_priv.clients[i]);
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

	for (i = 0; i < VGA_SWITCHEROO_MAX_CLIENTS; i++) {
		if (vgasr_priv.clients[i].id == client_id) {
			client = &vgasr_priv.clients[i];
			break;
		}
	}

	vgasr_priv.delayed_switch_active = false;

	if (just_mux) {
		ret = vgasr_priv.handler->switchto(client_id);
		goto out;
	}

	/* okay we want a switch - test if devices are willing to switch */
	can_switch = true;
	for (i = 0; i < VGA_SWITCHEROO_MAX_CLIENTS; i++) {
		can_switch = vgasr_priv.clients[i].can_switch(vgasr_priv.clients[i].pdev);
		if (can_switch == false) {
			printk(KERN_ERR "vga_switcheroo: client %d refused switch\n", i);
			break;
		}
	}

	if (can_switch == false && delay == false)
		goto out;

	if (can_switch == true) {
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
	struct vga_switcheroo_client *client = NULL;
	const char *pdev_name;
	bool can_switch = true;
	int i;
	int ret;
	int err = -EINVAL;

	mutex_lock(&vgasr_mutex);
	if (!vgasr_priv.delayed_switch_active)
		goto err;

	printk(KERN_INFO "vga_switcheroo: processing delayed switch to %d\n", vgasr_priv.delayed_client_id);

	for (i = 0; i < VGA_SWITCHEROO_MAX_CLIENTS; i++) {
		if (vgasr_priv.clients[i].id == vgasr_priv.delayed_client_id)
			client = &vgasr_priv.clients[i];
		can_switch = vgasr_priv.clients[i].can_switch(vgasr_priv.clients[i].pdev);
		if (can_switch == false) {
			printk(KERN_ERR "vga_switcheroo: client %d refused switch\n", i);
			break;
		}
	}

	if (can_switch == false || client == NULL)
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

