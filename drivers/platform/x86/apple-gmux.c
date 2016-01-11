/*
 *  Gmux driver for Apple laptops
 *
 *  Copyright (C) Canonical Ltd. <seth.forshee@canonical.com>
 *  Copyright (C) 2010-2012 Andreas Heider <andreas@meetr.de>
 *  Copyright (C) 2015 Lukas Wunner <lukas@wunner.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/backlight.h>
#include <linux/acpi.h>
#include <linux/pnp.h>
#include <linux/apple_bl.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/vga_switcheroo.h>
#include <linux/vgaarb.h>
#include <acpi/video.h>
#include <asm/io.h>

/**
 * DOC: Overview
 *
 * :1:  http://www.latticesemi.com/en/Products/FPGAandCPLD/LatticeXP2.aspx
 * :2:  http://www.renesas.com/products/mpumcu/h8s/h8s2100/h8s2113/index.jsp
 *
 * gmux is a microcontroller built into the MacBook Pro to support dual GPUs:
 * A {1}[Lattice XP2] on pre-retinas, a {2}[Renesas R4F2113] on retinas.
 *
 * (The MacPro6,1 2013 also has a gmux, however it is unclear why since it has
 * dual GPUs but no built-in display.)
 *
 * gmux is connected to the LPC bus of the southbridge. Its I/O ports are
 * accessed differently depending on the microcontroller: Driver functions
 * to access a pre-retina gmux are infixed `_pio_`, those for a retina gmux
 * are infixed `_index_`.
 */

struct apple_gmux_data {
	unsigned long iostart;
	unsigned long iolen;
	bool indexed;
	struct mutex index_lock;

	struct pci_dev *pdev;
	struct backlight_device *bdev;

	/* switcheroo data */
	acpi_handle dhandle;
	int gpe;
	enum vga_switcheroo_client_id switch_state_display;
	enum vga_switcheroo_client_id switch_state_ddc;
	enum vga_switcheroo_client_id switch_state_external;
	enum vga_switcheroo_state power_state;
	struct completion powerchange_done;
};

static struct apple_gmux_data *apple_gmux_data;

/*
 * gmux port offsets. Many of these are not yet used, but may be in the
 * future, and it's useful to have them documented here anyhow.
 */
#define GMUX_PORT_VERSION_MAJOR		0x04
#define GMUX_PORT_VERSION_MINOR		0x05
#define GMUX_PORT_VERSION_RELEASE	0x06
#define GMUX_PORT_SWITCH_DISPLAY	0x10
#define GMUX_PORT_SWITCH_GET_DISPLAY	0x11
#define GMUX_PORT_INTERRUPT_ENABLE	0x14
#define GMUX_PORT_INTERRUPT_STATUS	0x16
#define GMUX_PORT_SWITCH_DDC		0x28
#define GMUX_PORT_SWITCH_EXTERNAL	0x40
#define GMUX_PORT_SWITCH_GET_EXTERNAL	0x41
#define GMUX_PORT_DISCRETE_POWER	0x50
#define GMUX_PORT_MAX_BRIGHTNESS	0x70
#define GMUX_PORT_BRIGHTNESS		0x74
#define GMUX_PORT_VALUE			0xc2
#define GMUX_PORT_READ			0xd0
#define GMUX_PORT_WRITE			0xd4

#define GMUX_MIN_IO_LEN			(GMUX_PORT_BRIGHTNESS + 4)

#define GMUX_INTERRUPT_ENABLE		0xff
#define GMUX_INTERRUPT_DISABLE		0x00

#define GMUX_INTERRUPT_STATUS_ACTIVE	0
#define GMUX_INTERRUPT_STATUS_DISPLAY	(1 << 0)
#define GMUX_INTERRUPT_STATUS_POWER	(1 << 2)
#define GMUX_INTERRUPT_STATUS_HOTPLUG	(1 << 3)

#define GMUX_BRIGHTNESS_MASK		0x00ffffff
#define GMUX_MAX_BRIGHTNESS		GMUX_BRIGHTNESS_MASK

static u8 gmux_pio_read8(struct apple_gmux_data *gmux_data, int port)
{
	return inb(gmux_data->iostart + port);
}

static void gmux_pio_write8(struct apple_gmux_data *gmux_data, int port,
			       u8 val)
{
	outb(val, gmux_data->iostart + port);
}

static u32 gmux_pio_read32(struct apple_gmux_data *gmux_data, int port)
{
	return inl(gmux_data->iostart + port);
}

static void gmux_pio_write32(struct apple_gmux_data *gmux_data, int port,
			     u32 val)
{
	int i;
	u8 tmpval;

	for (i = 0; i < 4; i++) {
		tmpval = (val >> (i * 8)) & 0xff;
		outb(tmpval, gmux_data->iostart + port + i);
	}
}

static int gmux_index_wait_ready(struct apple_gmux_data *gmux_data)
{
	int i = 200;
	u8 gwr = inb(gmux_data->iostart + GMUX_PORT_WRITE);

	while (i && (gwr & 0x01)) {
		inb(gmux_data->iostart + GMUX_PORT_READ);
		gwr = inb(gmux_data->iostart + GMUX_PORT_WRITE);
		udelay(100);
		i--;
	}

	return !!i;
}

static int gmux_index_wait_complete(struct apple_gmux_data *gmux_data)
{
	int i = 200;
	u8 gwr = inb(gmux_data->iostart + GMUX_PORT_WRITE);

	while (i && !(gwr & 0x01)) {
		gwr = inb(gmux_data->iostart + GMUX_PORT_WRITE);
		udelay(100);
		i--;
	}

	if (gwr & 0x01)
		inb(gmux_data->iostart + GMUX_PORT_READ);

	return !!i;
}

static u8 gmux_index_read8(struct apple_gmux_data *gmux_data, int port)
{
	u8 val;

	mutex_lock(&gmux_data->index_lock);
	gmux_index_wait_ready(gmux_data);
	outb((port & 0xff), gmux_data->iostart + GMUX_PORT_READ);
	gmux_index_wait_complete(gmux_data);
	val = inb(gmux_data->iostart + GMUX_PORT_VALUE);
	mutex_unlock(&gmux_data->index_lock);

	return val;
}

static void gmux_index_write8(struct apple_gmux_data *gmux_data, int port,
			      u8 val)
{
	mutex_lock(&gmux_data->index_lock);
	outb(val, gmux_data->iostart + GMUX_PORT_VALUE);
	gmux_index_wait_ready(gmux_data);
	outb(port & 0xff, gmux_data->iostart + GMUX_PORT_WRITE);
	gmux_index_wait_complete(gmux_data);
	mutex_unlock(&gmux_data->index_lock);
}

static u32 gmux_index_read32(struct apple_gmux_data *gmux_data, int port)
{
	u32 val;

	mutex_lock(&gmux_data->index_lock);
	gmux_index_wait_ready(gmux_data);
	outb((port & 0xff), gmux_data->iostart + GMUX_PORT_READ);
	gmux_index_wait_complete(gmux_data);
	val = inl(gmux_data->iostart + GMUX_PORT_VALUE);
	mutex_unlock(&gmux_data->index_lock);

	return val;
}

static void gmux_index_write32(struct apple_gmux_data *gmux_data, int port,
			       u32 val)
{
	int i;
	u8 tmpval;

	mutex_lock(&gmux_data->index_lock);

	for (i = 0; i < 4; i++) {
		tmpval = (val >> (i * 8)) & 0xff;
		outb(tmpval, gmux_data->iostart + GMUX_PORT_VALUE + i);
	}

	gmux_index_wait_ready(gmux_data);
	outb(port & 0xff, gmux_data->iostart + GMUX_PORT_WRITE);
	gmux_index_wait_complete(gmux_data);
	mutex_unlock(&gmux_data->index_lock);
}

static u8 gmux_read8(struct apple_gmux_data *gmux_data, int port)
{
	if (gmux_data->indexed)
		return gmux_index_read8(gmux_data, port);
	else
		return gmux_pio_read8(gmux_data, port);
}

static void gmux_write8(struct apple_gmux_data *gmux_data, int port, u8 val)
{
	if (gmux_data->indexed)
		gmux_index_write8(gmux_data, port, val);
	else
		gmux_pio_write8(gmux_data, port, val);
}

static u32 gmux_read32(struct apple_gmux_data *gmux_data, int port)
{
	if (gmux_data->indexed)
		return gmux_index_read32(gmux_data, port);
	else
		return gmux_pio_read32(gmux_data, port);
}

static void gmux_write32(struct apple_gmux_data *gmux_data, int port,
			     u32 val)
{
	if (gmux_data->indexed)
		gmux_index_write32(gmux_data, port, val);
	else
		gmux_pio_write32(gmux_data, port, val);
}

static bool gmux_is_indexed(struct apple_gmux_data *gmux_data)
{
	u16 val;

	outb(0xaa, gmux_data->iostart + 0xcc);
	outb(0x55, gmux_data->iostart + 0xcd);
	outb(0x00, gmux_data->iostart + 0xce);

	val = inb(gmux_data->iostart + 0xcc) |
		(inb(gmux_data->iostart + 0xcd) << 8);

	if (val == 0x55aa)
		return true;

	return false;
}

/**
 * DOC: Backlight control
 *
 * :3:  http://www.ti.com/lit/ds/symlink/lp8543.pdf
 * :4:  http://www.ti.com/lit/ds/symlink/lp8545.pdf
 *
 * On single GPU MacBooks, the PWM signal for the backlight is generated by
 * the GPU. On dual GPU MacBook Pros by contrast, either GPU may be suspended
 * to conserve energy. Hence the PWM signal needs to be generated by a separate
 * backlight driver which is controlled by gmux. The earliest generation
 * MBP5 2008/09 uses a {3}[TI LP8543] backlight driver. All newer models
 * use a {4}[TI LP8545].
 */

static int gmux_get_brightness(struct backlight_device *bd)
{
	struct apple_gmux_data *gmux_data = bl_get_data(bd);
	return gmux_read32(gmux_data, GMUX_PORT_BRIGHTNESS) &
	       GMUX_BRIGHTNESS_MASK;
}

static int gmux_update_status(struct backlight_device *bd)
{
	struct apple_gmux_data *gmux_data = bl_get_data(bd);
	u32 brightness = bd->props.brightness;

	if (bd->props.state & BL_CORE_SUSPENDED)
		return 0;

	gmux_write32(gmux_data, GMUX_PORT_BRIGHTNESS, brightness);

	return 0;
}

static const struct backlight_ops gmux_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = gmux_get_brightness,
	.update_status = gmux_update_status,
};

/**
 * DOC: Graphics mux
 *
 * :5:  http://pimg-fpiw.uspto.gov/fdd/07/870/086/0.pdf
 * :6:  http://www.nxp.com/documents/data_sheet/CBTL06141.pdf
 * :7:  http://www.ti.com/lit/ds/symlink/hd3ss212.pdf
 * :8:  https://www.pericom.com/assets/Datasheets/PI3VDP12412.pdf
 * :9:  http://www.ti.com/lit/ds/symlink/sn74lv4066a.pdf
 * :10: http://pdf.datasheetarchive.com/indexerfiles/Datasheets-SW16/DSASW00308511.pdf
 * :11: http://www.ti.com/lit/ds/symlink/ts3ds10224.pdf
 *
 * On pre-retinas, the LVDS outputs of both GPUs feed into gmux which muxes
 * either of them to the panel. One of the tricks gmux has up its sleeve is
 * to lengthen the blanking interval of its output during a switch to
 * synchronize it with the GPU switched to. This allows for a flicker-free
 * switch that is imperceptible by the user ({5}[US 8,687,007 B2]).
 *
 * On retinas, muxing is no longer done by gmux itself, but by a separate
 * chip which is controlled by gmux. The chip is triple sourced, it is
 * either an {6}[NXP CBTL06142], {7}[TI HD3SS212] or {8}[Pericom PI3VDP12412].
 * The panel is driven with eDP instead of LVDS since the pixel clock
 * required for retina resolution exceeds LVDS' limits.
 *
 * Pre-retinas are able to switch the panel's DDC pins separately.
 * This is handled by a {9}[TI SN74LV4066A] which is controlled by gmux.
 * The inactive GPU can thus probe the panel's EDID without switching over
 * the entire panel. Retinas lack this functionality as the chips used for
 * eDP muxing are incapable of switching the AUX channel separately (see
 * the linked data sheets, Pericom would be capable but this is unused).
 * However the retina panel has the NO_AUX_HANDSHAKE_LINK_TRAINING bit set
 * in its DPCD, allowing the inactive GPU to skip the AUX handshake and
 * set up the output with link parameters pre-calibrated by the active GPU.
 *
 * The external DP port is only fully switchable on the first two unibody
 * MacBook Pro generations, MBP5 2008/09 and MBP6 2010. This is done by an
 * {6}[NXP CBTL06141] which is controlled by gmux. It's the predecessor of the
 * eDP mux on retinas, the difference being support for 2.7 versus 5.4 Gbit/s.
 *
 * The following MacBook Pro generations replaced the external DP port with a
 * combined DP/Thunderbolt port and lost the ability to switch it between GPUs,
 * connecting it either to the discrete GPU or the Thunderbolt controller.
 * Oddly enough, while the full port is no longer switchable, AUX and HPD
 * are still switchable by way of an {10}[NXP CBTL03062] (on pre-retinas
 * MBP8 2011 and MBP9 2012) or two {11}[TI TS3DS10224] (on retinas) under the
 * control of gmux. Since the integrated GPU is missing the main link,
 * external displays appear to it as phantoms which fail to link-train.
 *
 * gmux receives the HPD signal of all display connectors and sends an
 * interrupt on hotplug. On generations which cannot switch external ports,
 * the discrete GPU can then be woken to drive the newly connected display.
 * The ability to switch AUX on these generations could be used to improve
 * reliability of hotplug detection by having the integrated GPU poll the
 * ports while the discrete GPU is asleep, but currently we do not make use
 * of this feature.
 *
 * gmux' initial switch state on bootup is user configurable via the EFI
 * variable `gpu-power-prefs-fa4ce28d-b62f-4c99-9cc3-6815686e30f9` (5th byte,
 * 1 = IGD, 0 = DIS). Based on this setting, the EFI firmware tells gmux to
 * switch the panel and the external DP connector and allocates a framebuffer
 * for the selected GPU.
 */

static void gmux_read_switch_state(struct apple_gmux_data *gmux_data)
{
	if (gmux_read8(gmux_data, GMUX_PORT_SWITCH_DDC) == 1)
		gmux_data->switch_state_ddc = VGA_SWITCHEROO_IGD;
	else
		gmux_data->switch_state_ddc = VGA_SWITCHEROO_DIS;

	if (gmux_read8(gmux_data, GMUX_PORT_SWITCH_DISPLAY) == 2)
		gmux_data->switch_state_display = VGA_SWITCHEROO_IGD;
	else
		gmux_data->switch_state_display = VGA_SWITCHEROO_DIS;

	if (gmux_read8(gmux_data, GMUX_PORT_SWITCH_EXTERNAL) == 2)
		gmux_data->switch_state_external = VGA_SWITCHEROO_IGD;
	else
		gmux_data->switch_state_external = VGA_SWITCHEROO_DIS;
}

static void gmux_write_switch_state(struct apple_gmux_data *gmux_data)
{
	if (gmux_data->switch_state_ddc == VGA_SWITCHEROO_IGD)
		gmux_write8(gmux_data, GMUX_PORT_SWITCH_DDC, 1);
	else
		gmux_write8(gmux_data, GMUX_PORT_SWITCH_DDC, 2);

	if (gmux_data->switch_state_display == VGA_SWITCHEROO_IGD)
		gmux_write8(gmux_data, GMUX_PORT_SWITCH_DISPLAY, 2);
	else
		gmux_write8(gmux_data, GMUX_PORT_SWITCH_DISPLAY, 3);

	if (gmux_data->switch_state_external == VGA_SWITCHEROO_IGD)
		gmux_write8(gmux_data, GMUX_PORT_SWITCH_EXTERNAL, 2);
	else
		gmux_write8(gmux_data, GMUX_PORT_SWITCH_EXTERNAL, 3);
}

static int gmux_switchto(enum vga_switcheroo_client_id id)
{
	apple_gmux_data->switch_state_ddc = id;
	apple_gmux_data->switch_state_display = id;
	apple_gmux_data->switch_state_external = id;

	gmux_write_switch_state(apple_gmux_data);

	return 0;
}

static int gmux_switch_ddc(enum vga_switcheroo_client_id id)
{
	enum vga_switcheroo_client_id old_ddc_owner =
		apple_gmux_data->switch_state_ddc;

	if (id == old_ddc_owner)
		return id;

	pr_debug("Switching DDC from %d to %d\n", old_ddc_owner, id);
	apple_gmux_data->switch_state_ddc = id;

	if (id == VGA_SWITCHEROO_IGD)
		gmux_write8(apple_gmux_data, GMUX_PORT_SWITCH_DDC, 1);
	else
		gmux_write8(apple_gmux_data, GMUX_PORT_SWITCH_DDC, 2);

	return old_ddc_owner;
}

/**
 * DOC: Power control
 *
 * gmux is able to cut power to the discrete GPU. It automatically takes care
 * of the correct sequence to tear down and bring up the power rails for
 * core voltage, VRAM and PCIe.
 */

static int gmux_set_discrete_state(struct apple_gmux_data *gmux_data,
				   enum vga_switcheroo_state state)
{
	reinit_completion(&gmux_data->powerchange_done);

	if (state == VGA_SWITCHEROO_ON) {
		gmux_write8(gmux_data, GMUX_PORT_DISCRETE_POWER, 1);
		gmux_write8(gmux_data, GMUX_PORT_DISCRETE_POWER, 3);
		pr_debug("Discrete card powered up\n");
	} else {
		gmux_write8(gmux_data, GMUX_PORT_DISCRETE_POWER, 1);
		gmux_write8(gmux_data, GMUX_PORT_DISCRETE_POWER, 0);
		pr_debug("Discrete card powered down\n");
	}

	gmux_data->power_state = state;

	if (gmux_data->gpe >= 0 &&
	    !wait_for_completion_interruptible_timeout(&gmux_data->powerchange_done,
						       msecs_to_jiffies(200)))
		pr_warn("Timeout waiting for gmux switch to complete\n");

	return 0;
}

static int gmux_set_power_state(enum vga_switcheroo_client_id id,
				enum vga_switcheroo_state state)
{
	if (id == VGA_SWITCHEROO_IGD)
		return 0;

	return gmux_set_discrete_state(apple_gmux_data, state);
}

static int gmux_get_client_id(struct pci_dev *pdev)
{
	/*
	 * Early Macbook Pros with switchable graphics use nvidia
	 * integrated graphics. Hardcode that the 9400M is integrated.
	 */
	if (pdev->vendor == PCI_VENDOR_ID_INTEL)
		return VGA_SWITCHEROO_IGD;
	else if (pdev->vendor == PCI_VENDOR_ID_NVIDIA &&
		 pdev->device == 0x0863)
		return VGA_SWITCHEROO_IGD;
	else
		return VGA_SWITCHEROO_DIS;
}

static const struct vga_switcheroo_handler gmux_handler_indexed = {
	.switchto = gmux_switchto,
	.power_state = gmux_set_power_state,
	.get_client_id = gmux_get_client_id,
};

static const struct vga_switcheroo_handler gmux_handler_classic = {
	.switchto = gmux_switchto,
	.switch_ddc = gmux_switch_ddc,
	.power_state = gmux_set_power_state,
	.get_client_id = gmux_get_client_id,
};

/**
 * DOC: Interrupt
 *
 * gmux is also connected to a GPIO pin of the southbridge and thereby is able
 * to trigger an ACPI GPE. On the MBP5 2008/09 it's GPIO pin 22 of the Nvidia
 * MCP79, on all following generations it's GPIO pin 6 of the Intel PCH.
 * The GPE merely signals that an interrupt occurred, the actual type of event
 * is identified by reading a gmux register.
 */

static inline void gmux_disable_interrupts(struct apple_gmux_data *gmux_data)
{
	gmux_write8(gmux_data, GMUX_PORT_INTERRUPT_ENABLE,
		    GMUX_INTERRUPT_DISABLE);
}

static inline void gmux_enable_interrupts(struct apple_gmux_data *gmux_data)
{
	gmux_write8(gmux_data, GMUX_PORT_INTERRUPT_ENABLE,
		    GMUX_INTERRUPT_ENABLE);
}

static inline u8 gmux_interrupt_get_status(struct apple_gmux_data *gmux_data)
{
	return gmux_read8(gmux_data, GMUX_PORT_INTERRUPT_STATUS);
}

static void gmux_clear_interrupts(struct apple_gmux_data *gmux_data)
{
	u8 status;

	/* to clear interrupts write back current status */
	status = gmux_interrupt_get_status(gmux_data);
	gmux_write8(gmux_data, GMUX_PORT_INTERRUPT_STATUS, status);
}

static void gmux_notify_handler(acpi_handle device, u32 value, void *context)
{
	u8 status;
	struct pnp_dev *pnp = (struct pnp_dev *)context;
	struct apple_gmux_data *gmux_data = pnp_get_drvdata(pnp);

	status = gmux_interrupt_get_status(gmux_data);
	gmux_disable_interrupts(gmux_data);
	pr_debug("Notify handler called: status %d\n", status);

	gmux_clear_interrupts(gmux_data);
	gmux_enable_interrupts(gmux_data);

	if (status & GMUX_INTERRUPT_STATUS_POWER)
		complete(&gmux_data->powerchange_done);
}

static int gmux_suspend(struct device *dev)
{
	struct pnp_dev *pnp = to_pnp_dev(dev);
	struct apple_gmux_data *gmux_data = pnp_get_drvdata(pnp);

	gmux_disable_interrupts(gmux_data);
	return 0;
}

static int gmux_resume(struct device *dev)
{
	struct pnp_dev *pnp = to_pnp_dev(dev);
	struct apple_gmux_data *gmux_data = pnp_get_drvdata(pnp);

	gmux_enable_interrupts(gmux_data);
	gmux_write_switch_state(gmux_data);
	if (gmux_data->power_state == VGA_SWITCHEROO_OFF)
		gmux_set_discrete_state(gmux_data, gmux_data->power_state);
	return 0;
}

static struct pci_dev *gmux_get_io_pdev(void)
{
	struct pci_dev *pdev = NULL;

	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, pdev))) {
		u16 cmd;

		pci_read_config_word(pdev, PCI_COMMAND, &cmd);
		if (!(cmd & PCI_COMMAND_IO))
			continue;

		return pdev;
	}

	return NULL;
}

static int gmux_probe(struct pnp_dev *pnp, const struct pnp_device_id *id)
{
	struct apple_gmux_data *gmux_data;
	struct resource *res;
	struct backlight_properties props;
	struct backlight_device *bdev;
	u8 ver_major, ver_minor, ver_release;
	int ret = -ENXIO;
	acpi_status status;
	unsigned long long gpe;
	struct pci_dev *pdev = NULL;

	if (apple_gmux_data)
		return -EBUSY;

	gmux_data = kzalloc(sizeof(*gmux_data), GFP_KERNEL);
	if (!gmux_data)
		return -ENOMEM;
	pnp_set_drvdata(pnp, gmux_data);

	res = pnp_get_resource(pnp, IORESOURCE_IO, 0);
	if (!res) {
		pr_err("Failed to find gmux I/O resource\n");
		goto err_free;
	}

	gmux_data->iostart = res->start;
	gmux_data->iolen = res->end - res->start;

	if (gmux_data->iolen < GMUX_MIN_IO_LEN) {
		pr_err("gmux I/O region too small (%lu < %u)\n",
		       gmux_data->iolen, GMUX_MIN_IO_LEN);
		goto err_free;
	}

	if (!request_region(gmux_data->iostart, gmux_data->iolen,
			    "Apple gmux")) {
		pr_err("gmux I/O already in use\n");
		goto err_free;
	}

	/*
	 * Invalid version information may indicate either that the gmux
	 * device isn't present or that it's a new one that uses indexed
	 * io
	 */

	ver_major = gmux_read8(gmux_data, GMUX_PORT_VERSION_MAJOR);
	ver_minor = gmux_read8(gmux_data, GMUX_PORT_VERSION_MINOR);
	ver_release = gmux_read8(gmux_data, GMUX_PORT_VERSION_RELEASE);
	if (ver_major == 0xff && ver_minor == 0xff && ver_release == 0xff) {
		if (gmux_is_indexed(gmux_data)) {
			u32 version;
			mutex_init(&gmux_data->index_lock);
			gmux_data->indexed = true;
			version = gmux_read32(gmux_data,
				GMUX_PORT_VERSION_MAJOR);
			ver_major = (version >> 24) & 0xff;
			ver_minor = (version >> 16) & 0xff;
			ver_release = (version >> 8) & 0xff;
		} else {
			pr_info("gmux device not present or IO disabled\n");
			ret = -ENODEV;
			goto err_release;
		}
	}
	pr_info("Found gmux version %d.%d.%d [%s]\n", ver_major, ver_minor,
		ver_release, (gmux_data->indexed ? "indexed" : "classic"));

	/*
	 * Apple systems with gmux are EFI based and normally don't use
	 * VGA. In addition changing IO+MEM ownership between IGP and dGPU
	 * disables IO/MEM used for backlight control on some systems.
	 * Lock IO+MEM to GPU with active IO to prevent switch.
	 */
	pdev = gmux_get_io_pdev();
	if (pdev && vga_tryget(pdev,
			       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM)) {
		pr_err("IO+MEM vgaarb-locking for PCI:%s failed\n",
			pci_name(pdev));
		ret = -EBUSY;
		goto err_release;
	} else if (pdev)
		pr_info("locked IO for PCI:%s\n", pci_name(pdev));
	gmux_data->pdev = pdev;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = gmux_read32(gmux_data, GMUX_PORT_MAX_BRIGHTNESS);

	/*
	 * Currently it's assumed that the maximum brightness is less than
	 * 2^24 for compatibility with old gmux versions. Cap the max
	 * brightness at this value, but print a warning if the hardware
	 * reports something higher so that it can be fixed.
	 */
	if (WARN_ON(props.max_brightness > GMUX_MAX_BRIGHTNESS))
		props.max_brightness = GMUX_MAX_BRIGHTNESS;

	bdev = backlight_device_register("gmux_backlight", &pnp->dev,
					 gmux_data, &gmux_bl_ops, &props);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto err_release;
	}

	gmux_data->bdev = bdev;
	bdev->props.brightness = gmux_get_brightness(bdev);
	backlight_update_status(bdev);

	/*
	 * The backlight situation on Macs is complicated. If the gmux is
	 * present it's the best choice, because it always works for
	 * backlight control and supports more levels than other options.
	 * Disable the other backlight choices.
	 */
	acpi_video_set_dmi_backlight_type(acpi_backlight_vendor);
	apple_bl_unregister();

	gmux_data->power_state = VGA_SWITCHEROO_ON;

	gmux_data->dhandle = ACPI_HANDLE(&pnp->dev);
	if (!gmux_data->dhandle) {
		pr_err("Cannot find acpi handle for pnp device %s\n",
		       dev_name(&pnp->dev));
		ret = -ENODEV;
		goto err_notify;
	}

	status = acpi_evaluate_integer(gmux_data->dhandle, "GMGP", NULL, &gpe);
	if (ACPI_SUCCESS(status)) {
		gmux_data->gpe = (int)gpe;

		status = acpi_install_notify_handler(gmux_data->dhandle,
						     ACPI_DEVICE_NOTIFY,
						     &gmux_notify_handler, pnp);
		if (ACPI_FAILURE(status)) {
			pr_err("Install notify handler failed: %s\n",
			       acpi_format_exception(status));
			ret = -ENODEV;
			goto err_notify;
		}

		status = acpi_enable_gpe(NULL, gmux_data->gpe);
		if (ACPI_FAILURE(status)) {
			pr_err("Cannot enable gpe: %s\n",
			       acpi_format_exception(status));
			goto err_enable_gpe;
		}
	} else {
		pr_warn("No GPE found for gmux\n");
		gmux_data->gpe = -1;
	}

	apple_gmux_data = gmux_data;
	init_completion(&gmux_data->powerchange_done);
	gmux_enable_interrupts(gmux_data);
	gmux_read_switch_state(gmux_data);

	/*
	 * Retina MacBook Pros cannot switch the panel's AUX separately
	 * and need eDP pre-calibration. They are distinguishable from
	 * pre-retinas by having an "indexed" gmux.
	 *
	 * Pre-retina MacBook Pros can switch the panel's DDC separately.
	 */
	if (gmux_data->indexed)
		ret = vga_switcheroo_register_handler(&gmux_handler_indexed,
					      VGA_SWITCHEROO_NEEDS_EDP_CONFIG);
	else
		ret = vga_switcheroo_register_handler(&gmux_handler_classic,
					      VGA_SWITCHEROO_CAN_SWITCH_DDC);
	if (ret) {
		pr_err("Failed to register vga_switcheroo handler\n");
		goto err_register_handler;
	}

	return 0;

err_register_handler:
	gmux_disable_interrupts(gmux_data);
	apple_gmux_data = NULL;
	if (gmux_data->gpe >= 0)
		acpi_disable_gpe(NULL, gmux_data->gpe);
err_enable_gpe:
	if (gmux_data->gpe >= 0)
		acpi_remove_notify_handler(gmux_data->dhandle,
					   ACPI_DEVICE_NOTIFY,
					   &gmux_notify_handler);
err_notify:
	backlight_device_unregister(bdev);
err_release:
	if (gmux_data->pdev)
		vga_put(gmux_data->pdev,
			VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM);
	pci_dev_put(pdev);
	release_region(gmux_data->iostart, gmux_data->iolen);
err_free:
	kfree(gmux_data);
	return ret;
}

static void gmux_remove(struct pnp_dev *pnp)
{
	struct apple_gmux_data *gmux_data = pnp_get_drvdata(pnp);

	vga_switcheroo_unregister_handler();
	gmux_disable_interrupts(gmux_data);
	if (gmux_data->gpe >= 0) {
		acpi_disable_gpe(NULL, gmux_data->gpe);
		acpi_remove_notify_handler(gmux_data->dhandle,
					   ACPI_DEVICE_NOTIFY,
					   &gmux_notify_handler);
	}

	if (gmux_data->pdev) {
		vga_put(gmux_data->pdev,
			VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM);
		pci_dev_put(gmux_data->pdev);
	}
	backlight_device_unregister(gmux_data->bdev);

	release_region(gmux_data->iostart, gmux_data->iolen);
	apple_gmux_data = NULL;
	kfree(gmux_data);

	acpi_video_register();
	apple_bl_register();
}

static const struct pnp_device_id gmux_device_ids[] = {
	{"APP000B", 0},
	{"", 0}
};

static const struct dev_pm_ops gmux_dev_pm_ops = {
	.suspend = gmux_suspend,
	.resume = gmux_resume,
};

static struct pnp_driver gmux_pnp_driver = {
	.name		= "apple-gmux",
	.probe		= gmux_probe,
	.remove		= gmux_remove,
	.id_table	= gmux_device_ids,
	.driver		= {
			.pm = &gmux_dev_pm_ops,
	},
};

module_pnp_driver(gmux_pnp_driver);
MODULE_AUTHOR("Seth Forshee <seth.forshee@canonical.com>");
MODULE_DESCRIPTION("Apple Gmux Driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pnp, gmux_device_ids);
