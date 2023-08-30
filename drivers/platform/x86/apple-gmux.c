// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Gmux driver for Apple laptops
 *
 *  Copyright (C) Canonical Ltd. <seth.forshee@canonical.com>
 *  Copyright (C) 2010-2012 Andreas Heider <andreas@meetr.de>
 *  Copyright (C) 2015 Lukas Wunner <lukas@wunner.de>
 *  Copyright (C) 2023 Orlando Chamberlain <orlandoch.dev@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/backlight.h>
#include <linux/acpi.h>
#include <linux/pnp.h>
#include <linux/apple-gmux.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/vga_switcheroo.h>
#include <linux/debugfs.h>
#include <acpi/video.h>
#include <asm/io.h>

/**
 * DOC: Overview
 *
 * gmux is a microcontroller built into the MacBook Pro to support dual GPUs:
 * A `Lattice XP2`_ on pre-retinas, a `Renesas R4F2113`_ on pre-T2 retinas.
 *
 * On T2 Macbooks, the gmux is part of the T2 Coprocessor's SMC. The SMC has
 * an I2C connection to a `NXP PCAL6524` GPIO expander, which enables/disables
 * the voltage regulators of the discrete GPU, drives the display panel power,
 * and has a GPIO to switch the eDP mux. The Intel CPU can interact with
 * gmux through MMIO, similar to how the main SMC interface is controlled.
 *
 * (The MacPro6,1 2013 also has a gmux, however it is unclear why since it has
 * dual GPUs but no built-in display.)
 *
 * gmux is connected to the LPC bus of the southbridge. Its I/O ports are
 * accessed differently depending on the microcontroller: Driver functions
 * to access a pre-retina gmux are infixed ``_pio_``, those for a pre-T2
 * retina gmux are infixed ``_index_``, and those on T2 Macs are infixed
 * with ``_mmio_``.
 *
 * .. _Lattice XP2:
 *     http://www.latticesemi.com/en/Products/FPGAandCPLD/LatticeXP2.aspx
 * .. _Renesas R4F2113:
 *     http://www.renesas.com/products/mpumcu/h8s/h8s2100/h8s2113/index.jsp
 * .. _NXP PCAL6524:
 *     https://www.nxp.com/docs/en/data-sheet/PCAL6524.pdf
 */

struct apple_gmux_config;

struct apple_gmux_data {
	u8 __iomem *iomem_base;
	unsigned long iostart;
	unsigned long iolen;
	const struct apple_gmux_config *config;
	struct mutex index_lock;

	struct backlight_device *bdev;

	/* switcheroo data */
	acpi_handle dhandle;
	int gpe;
	bool external_switchable;
	enum vga_switcheroo_client_id switch_state_display;
	enum vga_switcheroo_client_id switch_state_ddc;
	enum vga_switcheroo_client_id switch_state_external;
	enum vga_switcheroo_state power_state;
	struct completion powerchange_done;

	/* debugfs data */
	u8 selected_port;
	struct dentry *debug_dentry;
};

static struct apple_gmux_data *apple_gmux_data;

struct apple_gmux_config {
	u8 (*read8)(struct apple_gmux_data *gmux_data, int port);
	void (*write8)(struct apple_gmux_data *gmux_data, int port, u8 val);
	u32 (*read32)(struct apple_gmux_data *gmux_data, int port);
	void (*write32)(struct apple_gmux_data *gmux_data, int port, u32 val);
	const struct vga_switcheroo_handler *gmux_handler;
	enum vga_switcheroo_handler_flags_t handler_flags;
	unsigned long resource_type;
	bool read_version_as_u32;
	char *name;
};

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

static int gmux_mmio_wait(struct apple_gmux_data *gmux_data)
{
	int i = 200;
	u8 gwr = ioread8(gmux_data->iomem_base + GMUX_MMIO_COMMAND_SEND);

	while (i && gwr) {
		gwr = ioread8(gmux_data->iomem_base + GMUX_MMIO_COMMAND_SEND);
		udelay(100);
		i--;
	}

	return !!i;
}

static u8 gmux_mmio_read8(struct apple_gmux_data *gmux_data, int port)
{
	u8 val;

	mutex_lock(&gmux_data->index_lock);
	gmux_mmio_wait(gmux_data);
	iowrite8((port & 0xff), gmux_data->iomem_base + GMUX_MMIO_PORT_SELECT);
	iowrite8(GMUX_MMIO_READ | sizeof(val),
		gmux_data->iomem_base + GMUX_MMIO_COMMAND_SEND);
	gmux_mmio_wait(gmux_data);
	val = ioread8(gmux_data->iomem_base);
	mutex_unlock(&gmux_data->index_lock);

	return val;
}

static void gmux_mmio_write8(struct apple_gmux_data *gmux_data, int port,
			      u8 val)
{
	mutex_lock(&gmux_data->index_lock);
	gmux_mmio_wait(gmux_data);
	iowrite8(val, gmux_data->iomem_base);

	iowrite8(port & 0xff, gmux_data->iomem_base + GMUX_MMIO_PORT_SELECT);
	iowrite8(GMUX_MMIO_WRITE | sizeof(val),
		gmux_data->iomem_base + GMUX_MMIO_COMMAND_SEND);

	gmux_mmio_wait(gmux_data);
	mutex_unlock(&gmux_data->index_lock);
}

static u32 gmux_mmio_read32(struct apple_gmux_data *gmux_data, int port)
{
	u32 val;

	mutex_lock(&gmux_data->index_lock);
	gmux_mmio_wait(gmux_data);
	iowrite8((port & 0xff), gmux_data->iomem_base + GMUX_MMIO_PORT_SELECT);
	iowrite8(GMUX_MMIO_READ | sizeof(val),
		gmux_data->iomem_base + GMUX_MMIO_COMMAND_SEND);
	gmux_mmio_wait(gmux_data);
	val = be32_to_cpu(ioread32(gmux_data->iomem_base));
	mutex_unlock(&gmux_data->index_lock);

	return val;
}

static void gmux_mmio_write32(struct apple_gmux_data *gmux_data, int port,
			       u32 val)
{
	mutex_lock(&gmux_data->index_lock);
	iowrite32(cpu_to_be32(val), gmux_data->iomem_base);
	iowrite8(port & 0xff, gmux_data->iomem_base + GMUX_MMIO_PORT_SELECT);
	iowrite8(GMUX_MMIO_WRITE | sizeof(val),
		gmux_data->iomem_base + GMUX_MMIO_COMMAND_SEND);
	gmux_mmio_wait(gmux_data);
	mutex_unlock(&gmux_data->index_lock);
}

static u8 gmux_read8(struct apple_gmux_data *gmux_data, int port)
{
	return gmux_data->config->read8(gmux_data, port);
}

static void gmux_write8(struct apple_gmux_data *gmux_data, int port, u8 val)
{
	return gmux_data->config->write8(gmux_data, port, val);
}

static u32 gmux_read32(struct apple_gmux_data *gmux_data, int port)
{
	return gmux_data->config->read32(gmux_data, port);
}

static void gmux_write32(struct apple_gmux_data *gmux_data, int port,
			     u32 val)
{
	return gmux_data->config->write32(gmux_data, port, val);
}

/**
 * DOC: Backlight control
 *
 * On single GPU MacBooks, the PWM signal for the backlight is generated by
 * the GPU. On dual GPU MacBook Pros by contrast, either GPU may be suspended
 * to conserve energy. Hence the PWM signal needs to be generated by a separate
 * backlight driver which is controlled by gmux. The earliest generation
 * MBP5 2008/09 uses a `TI LP8543`_ backlight driver. Newer models
 * use a `TI LP8545`_ or a TI LP8548.
 *
 * .. _TI LP8543: https://www.ti.com/lit/ds/symlink/lp8543.pdf
 * .. _TI LP8545: https://www.ti.com/lit/ds/symlink/lp8545.pdf
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
	u32 brightness = backlight_get_brightness(bd);

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
 * On pre-retinas, the LVDS outputs of both GPUs feed into gmux which muxes
 * either of them to the panel. One of the tricks gmux has up its sleeve is
 * to lengthen the blanking interval of its output during a switch to
 * synchronize it with the GPU switched to. This allows for a flicker-free
 * switch that is imperceptible by the user (`US 8,687,007 B2`_).
 *
 * On retinas, muxing is no longer done by gmux itself, but by a separate
 * chip which is controlled by gmux. The chip is triple sourced, it is
 * either an `NXP CBTL06142`_, `TI HD3SS212`_ or `Pericom PI3VDP12412`_.
 * The panel is driven with eDP instead of LVDS since the pixel clock
 * required for retina resolution exceeds LVDS' limits.
 *
 * Pre-retinas are able to switch the panel's DDC pins separately.
 * This is handled by a `TI SN74LV4066A`_ which is controlled by gmux.
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
 * `NXP CBTL06141`_ which is controlled by gmux. It's the predecessor of the
 * eDP mux on retinas, the difference being support for 2.7 versus 5.4 Gbit/s.
 *
 * The following MacBook Pro generations replaced the external DP port with a
 * combined DP/Thunderbolt port and lost the ability to switch it between GPUs,
 * connecting it either to the discrete GPU or the Thunderbolt controller.
 * Oddly enough, while the full port is no longer switchable, AUX and HPD
 * are still switchable by way of an `NXP CBTL03062`_ (on pre-retinas
 * MBP8 2011 and MBP9 2012) or two `TI TS3DS10224`_ (on pre-t2 retinas) under
 * the control of gmux. Since the integrated GPU is missing the main link,
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
 * Our switching policy for the external port is that on those generations
 * which are able to switch it fully, the port is switched together with the
 * panel when IGD / DIS commands are issued to vga_switcheroo. It is thus
 * possible to drive e.g. a beamer on battery power with the integrated GPU.
 * The user may manually switch to the discrete GPU if more performance is
 * needed.
 *
 * On all newer generations, the external port can only be driven by the
 * discrete GPU. If a display is plugged in while the panel is switched to
 * the integrated GPU, *both* GPUs will be in use for maximum performance.
 * To decrease power consumption, the user may manually switch to the
 * discrete GPU, thereby suspending the integrated GPU.
 *
 * gmux' initial switch state on bootup is user configurable via the EFI
 * variable ``gpu-power-prefs-fa4ce28d-b62f-4c99-9cc3-6815686e30f9`` (5th byte,
 * 1 = IGD, 0 = DIS). Based on this setting, the EFI firmware tells gmux to
 * switch the panel and the external DP connector and allocates a framebuffer
 * for the selected GPU.
 *
 * .. _US 8,687,007 B2: https://pimg-fpiw.uspto.gov/fdd/07/870/086/0.pdf
 * .. _NXP CBTL06141:   https://www.nxp.com/documents/data_sheet/CBTL06141.pdf
 * .. _NXP CBTL06142:   https://www.nxp.com/documents/data_sheet/CBTL06141.pdf
 * .. _TI HD3SS212:     https://www.ti.com/lit/ds/symlink/hd3ss212.pdf
 * .. _Pericom PI3VDP12412: https://www.pericom.com/assets/Datasheets/PI3VDP12412.pdf
 * .. _TI SN74LV4066A:  https://www.ti.com/lit/ds/symlink/sn74lv4066a.pdf
 * .. _NXP CBTL03062:   http://pdf.datasheetarchive.com/indexerfiles/Datasheets-SW16/DSASW00308511.pdf
 * .. _TI TS3DS10224:   https://www.ti.com/lit/ds/symlink/ts3ds10224.pdf
 */

static void gmux_read_switch_state(struct apple_gmux_data *gmux_data)
{
	if (gmux_read8(gmux_data, GMUX_PORT_SWITCH_DDC) == 1)
		gmux_data->switch_state_ddc = VGA_SWITCHEROO_IGD;
	else
		gmux_data->switch_state_ddc = VGA_SWITCHEROO_DIS;

	if (gmux_read8(gmux_data, GMUX_PORT_SWITCH_DISPLAY) & 1)
		gmux_data->switch_state_display = VGA_SWITCHEROO_DIS;
	else
		gmux_data->switch_state_display = VGA_SWITCHEROO_IGD;

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
	if (apple_gmux_data->external_switchable)
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

static enum vga_switcheroo_client_id gmux_get_client_id(struct pci_dev *pdev)
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

static const struct vga_switcheroo_handler gmux_handler_no_ddc = {
	.switchto = gmux_switchto,
	.power_state = gmux_set_power_state,
	.get_client_id = gmux_get_client_id,
};

static const struct vga_switcheroo_handler gmux_handler_ddc = {
	.switchto = gmux_switchto,
	.switch_ddc = gmux_switch_ddc,
	.power_state = gmux_set_power_state,
	.get_client_id = gmux_get_client_id,
};

static const struct apple_gmux_config apple_gmux_pio = {
	.read8 = &gmux_pio_read8,
	.write8 = &gmux_pio_write8,
	.read32 = &gmux_pio_read32,
	.write32 = &gmux_pio_write32,
	.gmux_handler = &gmux_handler_ddc,
	.handler_flags = VGA_SWITCHEROO_CAN_SWITCH_DDC,
	.resource_type = IORESOURCE_IO,
	.read_version_as_u32 = false,
	.name = "classic"
};

static const struct apple_gmux_config apple_gmux_index = {
	.read8 = &gmux_index_read8,
	.write8 = &gmux_index_write8,
	.read32 = &gmux_index_read32,
	.write32 = &gmux_index_write32,
	.gmux_handler = &gmux_handler_no_ddc,
	.handler_flags = VGA_SWITCHEROO_NEEDS_EDP_CONFIG,
	.resource_type = IORESOURCE_IO,
	.read_version_as_u32 = true,
	.name = "indexed"
};

static const struct apple_gmux_config apple_gmux_mmio = {
	.read8 = &gmux_mmio_read8,
	.write8 = &gmux_mmio_write8,
	.read32 = &gmux_mmio_read32,
	.write32 = &gmux_mmio_write32,
	.gmux_handler = &gmux_handler_no_ddc,
	.handler_flags = VGA_SWITCHEROO_NEEDS_EDP_CONFIG,
	.resource_type = IORESOURCE_MEM,
	.read_version_as_u32 = true,
	.name = "T2"
};


/**
 * DOC: Interrupt
 *
 * gmux is also connected to a GPIO pin of the southbridge and thereby is able
 * to trigger an ACPI GPE. ACPI name GMGP holds this GPIO pin's number. On the
 * MBP5 2008/09 it's GPIO pin 22 of the Nvidia MCP79, on following generations
 * it's GPIO pin 6 of the Intel PCH, on MMIO gmux's it's pin 21.
 *
 * The GPE merely signals that an interrupt occurred, the actual type of event
 * is identified by reading a gmux register.
 *
 * In addition to the GMGP name, gmux's ACPI device also has two methods GMSP
 * and GMLV. GMLV likely means "GMUX Level", and reads the value of the GPIO,
 * while GMSP likely means "GMUX Set Polarity", and seems to write to the GPIO's
 * value. On newer Macbooks (This was introduced with or sometime before the
 * MacBookPro14,3), the ACPI GPE method differentiates between the OS type: On
 * Darwin, only a notification is signaled, whereas on other OSes, the GPIO's
 * value is read and then inverted.
 *
 * Because Linux masquerades as Darwin, it ends up in the notification-only code
 * path. On MMIO gmux's, this seems to lead to us being unable to clear interrupts,
 * unless we call GMSP(0). Without this, there is a flood of status=0 interrupts
 * that can't be cleared. This issue seems to be unique to MMIO gmux's.
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
	/* Prevent flood of status=0 interrupts */
	if (gmux_data->config == &apple_gmux_mmio)
		acpi_execute_simple_method(gmux_data->dhandle, "GMSP", 0);
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

/**
 * DOC: Debugfs Interface
 *
 * gmux ports can be accessed from userspace as a debugfs interface. For example:
 *
 * # echo 4 > /sys/kernel/debug/apple_gmux/selected_port
 * # cat /sys/kernel/debug/apple_gmux/selected_port_data | xxd -p
 * 00000005
 *
 * Reads 4 bytes from port 4 (GMUX_PORT_VERSION_MAJOR).
 *
 * 1 and 4 byte writes are also allowed.
 */

static ssize_t gmux_selected_port_data_write(struct file *file,
		const char __user *userbuf, size_t count, loff_t *ppos)
{
	struct apple_gmux_data *gmux_data = file->private_data;

	if (*ppos)
		return -EINVAL;

	if (count == 1) {
		u8 data;

		if (copy_from_user(&data, userbuf, 1))
			return -EFAULT;

		gmux_write8(gmux_data, gmux_data->selected_port, data);
	} else if (count == 4) {
		u32 data;

		if (copy_from_user(&data, userbuf, 4))
			return -EFAULT;

		gmux_write32(gmux_data, gmux_data->selected_port, data);
	} else
		return -EINVAL;

	return count;
}

static ssize_t gmux_selected_port_data_read(struct file *file,
		char __user *userbuf, size_t count, loff_t *ppos)
{
	struct apple_gmux_data *gmux_data = file->private_data;
	u32 data;

	data = gmux_read32(gmux_data, gmux_data->selected_port);

	return simple_read_from_buffer(userbuf, count, ppos, &data, sizeof(data));
}

static const struct file_operations gmux_port_data_ops = {
	.open = simple_open,
	.write = gmux_selected_port_data_write,
	.read = gmux_selected_port_data_read
};

static void gmux_init_debugfs(struct apple_gmux_data *gmux_data)
{
	gmux_data->debug_dentry = debugfs_create_dir(KBUILD_MODNAME, NULL);

	debugfs_create_u8("selected_port", 0644, gmux_data->debug_dentry,
			&gmux_data->selected_port);
	debugfs_create_file("selected_port_data", 0644, gmux_data->debug_dentry,
			gmux_data, &gmux_port_data_ops);
}

static void gmux_fini_debugfs(struct apple_gmux_data *gmux_data)
{
	debugfs_remove_recursive(gmux_data->debug_dentry);
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

static int is_thunderbolt(struct device *dev, void *data)
{
	return to_pci_dev(dev)->is_thunderbolt;
}

static int gmux_probe(struct pnp_dev *pnp, const struct pnp_device_id *id)
{
	struct apple_gmux_data *gmux_data;
	struct resource *res;
	struct backlight_properties props;
	struct backlight_device *bdev = NULL;
	u8 ver_major, ver_minor, ver_release;
	bool register_bdev = true;
	int ret = -ENXIO;
	acpi_status status;
	unsigned long long gpe;
	enum apple_gmux_type type;
	u32 version;

	if (apple_gmux_data)
		return -EBUSY;

	if (!apple_gmux_detect(pnp, &type)) {
		pr_info("gmux device not present\n");
		return -ENODEV;
	}

	gmux_data = kzalloc(sizeof(*gmux_data), GFP_KERNEL);
	if (!gmux_data)
		return -ENOMEM;
	pnp_set_drvdata(pnp, gmux_data);

	switch (type) {
	case APPLE_GMUX_TYPE_MMIO:
		gmux_data->config = &apple_gmux_mmio;
		mutex_init(&gmux_data->index_lock);

		res = pnp_get_resource(pnp, IORESOURCE_MEM, 0);
		gmux_data->iostart = res->start;
		/* Although the ACPI table only allocates 8 bytes, we need 16. */
		gmux_data->iolen = 16;
		if (!request_mem_region(gmux_data->iostart, gmux_data->iolen,
					"Apple gmux")) {
			pr_err("gmux I/O already in use\n");
			goto err_free;
		}
		gmux_data->iomem_base = ioremap(gmux_data->iostart, gmux_data->iolen);
		if (!gmux_data->iomem_base) {
			pr_err("couldn't remap gmux mmio region");
			goto err_release;
		}
		goto get_version;
	case APPLE_GMUX_TYPE_INDEXED:
		gmux_data->config = &apple_gmux_index;
		mutex_init(&gmux_data->index_lock);
		break;
	case APPLE_GMUX_TYPE_PIO:
		gmux_data->config = &apple_gmux_pio;
		break;
	}

	res = pnp_get_resource(pnp, IORESOURCE_IO, 0);
	gmux_data->iostart = res->start;
	gmux_data->iolen = resource_size(res);

	if (!request_region(gmux_data->iostart, gmux_data->iolen,
			    "Apple gmux")) {
		pr_err("gmux I/O already in use\n");
		goto err_free;
	}

get_version:
	if (gmux_data->config->read_version_as_u32) {
		version = gmux_read32(gmux_data, GMUX_PORT_VERSION_MAJOR);
		ver_major = (version >> 24) & 0xff;
		ver_minor = (version >> 16) & 0xff;
		ver_release = (version >> 8) & 0xff;
	} else {
		ver_major = gmux_read8(gmux_data, GMUX_PORT_VERSION_MAJOR);
		ver_minor = gmux_read8(gmux_data, GMUX_PORT_VERSION_MINOR);
		ver_release = gmux_read8(gmux_data, GMUX_PORT_VERSION_RELEASE);
	}
	pr_info("Found gmux version %d.%d.%d [%s]\n", ver_major, ver_minor,
		ver_release, gmux_data->config->name);

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = gmux_read32(gmux_data, GMUX_PORT_MAX_BRIGHTNESS);

#if IS_REACHABLE(CONFIG_ACPI_VIDEO)
	register_bdev = acpi_video_get_backlight_type() == acpi_backlight_apple_gmux;
#endif
	if (register_bdev) {
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
			goto err_unmap;
		}

		gmux_data->bdev = bdev;
		bdev->props.brightness = gmux_get_brightness(bdev);
		backlight_update_status(bdev);
	}

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

	/*
	 * If Thunderbolt is present, the external DP port is not fully
	 * switchable. Force its AUX channel to the discrete GPU.
	 */
	gmux_data->external_switchable =
		!bus_for_each_dev(&pci_bus_type, NULL, NULL, is_thunderbolt);
	if (!gmux_data->external_switchable)
		gmux_write8(gmux_data, GMUX_PORT_SWITCH_EXTERNAL, 3);

	apple_gmux_data = gmux_data;
	init_completion(&gmux_data->powerchange_done);
	gmux_enable_interrupts(gmux_data);
	gmux_read_switch_state(gmux_data);

	/*
	 * Retina MacBook Pros cannot switch the panel's AUX separately
	 * and need eDP pre-calibration. They are distinguishable from
	 * pre-retinas by having an "indexed" or "T2" gmux.
	 *
	 * Pre-retina MacBook Pros can switch the panel's DDC separately.
	 */
	ret = vga_switcheroo_register_handler(gmux_data->config->gmux_handler,
			gmux_data->config->handler_flags);
	if (ret) {
		pr_err("Failed to register vga_switcheroo handler\n");
		goto err_register_handler;
	}

	gmux_init_debugfs(gmux_data);
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
err_unmap:
	if (gmux_data->iomem_base)
		iounmap(gmux_data->iomem_base);
err_release:
	if (gmux_data->config->resource_type == IORESOURCE_MEM)
		release_mem_region(gmux_data->iostart, gmux_data->iolen);
	else
		release_region(gmux_data->iostart, gmux_data->iolen);
err_free:
	kfree(gmux_data);
	return ret;
}

static void gmux_remove(struct pnp_dev *pnp)
{
	struct apple_gmux_data *gmux_data = pnp_get_drvdata(pnp);

	gmux_fini_debugfs(gmux_data);
	vga_switcheroo_unregister_handler();
	gmux_disable_interrupts(gmux_data);
	if (gmux_data->gpe >= 0) {
		acpi_disable_gpe(NULL, gmux_data->gpe);
		acpi_remove_notify_handler(gmux_data->dhandle,
					   ACPI_DEVICE_NOTIFY,
					   &gmux_notify_handler);
	}

	backlight_device_unregister(gmux_data->bdev);

	if (gmux_data->iomem_base) {
		iounmap(gmux_data->iomem_base);
		release_mem_region(gmux_data->iostart, gmux_data->iolen);
	} else
		release_region(gmux_data->iostart, gmux_data->iolen);
	apple_gmux_data = NULL;
	kfree(gmux_data);
}

static const struct pnp_device_id gmux_device_ids[] = {
	{GMUX_ACPI_HID, 0},
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
